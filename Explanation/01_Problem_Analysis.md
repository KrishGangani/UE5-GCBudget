# 01 · Problem Analysis: GCBudget

**Phase:** 1 (Problem Analysis), 2026-11-21; updated after Phase 2 the same day (evidence in [01b_Research_Findings.md](01b_Research_Findings.md))
**Engine:** UE **5.8.4** (CL 58902052). Engine facts are read from the local source and cited as `File:line` (paths relative to `Engine/Source/Runtime/`).
**Status of every claim:** **[verified]** (read in engine source or a cited source), **[hypothesis]** (to be confirmed by measurement), or **[target]** (a pass/fail threshold, not a result).

---

## 1. The problem in one paragraph

Gameplay code often makes every small, numerous thing a UObject: each buff or timed effect, each inventory entry, each projectile payload, each quest-step record. The convenience is real: reflection, Blueprint access, `TObjectPtr` references and automatic lifetime.

The cost appears in the garbage collector. Every live UObject is one more entry the GC must scan, every reachable one is traversed with all its references, and every dead one must be destroyed. A design that is fine at 1,000 items can turn every GC cycle into a visible frame spike at 100,000.

The fix is well known in principle (store hot data in plain structs), but teams rarely see the **curve**: how many milliseconds each design costs per item, what the engine's own mitigations (clustering, incremental reachability) actually buy, and what the struct design costs in return.

## 2. Where this happens in real games

| Evidence | What it shows | Source |
|---|---|---|
| Epic knowledge base, "Garbage Collector Internals" (Ari Arnbjörnsson, Epic) | One measured example: 598,753 objects and 1.8 M references took 18.11 ms of reachability; "Usually bigger games don't need more than a couple hundred thousand UObjects alive at any time" | [Phase2_Raw_Notes/D](Phase2_Raw_Notes/D_existing_work.md) §D2 |
| Epic forum, a 50-player World Partition server | GC spikes of "around 90ms to 100ms" every minute at 125k–250k objects. Reducing 24.5k objects *raised* GC time by 0.3 ms: object count alone doesn't predict cost | §D3 |
| UE 5.8 | Epic added a "UObject Count" Insights trace counter (`Engine/Private/LevelTick.cpp:105, 1973`). Tom Looman's note: object count "directly affects garbage collection performance" | §D4; `01b` §2 |
| Epic's "Great Hitch Hunt" talk coverage | Garbage collection is listed among the main hitch categories | [research/02](Research/02_technical_challenges.md) [T1-D] |
| Job postings, 2026 | Memory appears in 18 of 57 postings. EA NHL: "taking ownership of memory management, multi-threading, and frame-rate optimization". Ubisoft Snowdrop: "memory usage and loading times" | [research/01](Research/01_job_market.md) §2 (EA5, U8), §4 |

**What these share:** the cost is real and known, but public evidence is anecdotes and single data points, not controlled curves.

## 3. What already exists, and why it doesn't fully solve the problem

| Existing tool or practice | What it does | What it doesn't do | Evidence |
|---|---|---|---|
| Parallel reachability (`gc.AllowParallelGC`, on by default) | Spreads reference traversal over the task-graph workers | Doesn't reduce the work. More objects still means more work | [verified] `CoreUObject/Private/UObject/GarbageCollection.cpp:4717–4736, 5678–5687` |
| Incremental purge (`gc.IncrementalBeginDestroyEnabled`, on; 2 ms per frame) | Spreads object destruction over frames | Reachability and gather still run in one blocking frame | [verified] `GarbageCollection.cpp:230`; `Engine/Private/UnrealEngine.cpp:1826, 2290–2294` |
| **Incremental reachability** (`gc.AllowIncrementalReachability`, **Experimental**, off) | Splits reference traversal into time-limited slices (default 5 ms) using the `TObjectPtr` write barrier | Not fully thread-safe; Epic recommends it "in single-threaded builds only" and staff did not recommend it in 5.5/5.6. The first and last frames of a cycle are not sliced | [verified] `GarbageCollection.cpp:302–324, 6019–6033`; docs and forum (`01b` #6–8) |
| **GC clusters** (`CreateCluster`, asset/actor clustering) | One reachability decision for a whole group; clustered objects' references aren't traversed | Each clustered object still costs a flag write per GC. Dropped objects stay alive until the cluster is dissolved. Asset and actor clustering are off in `BaseEngine.ini`. In Development builds, verification cancels the gain (Epic KB) | [verified] `GarbageCollection.cpp:4324–4346`; `Engine/Config/BaseEngine.ini:1754–1755`; `01b` #10–11 |
| Disregard-for-GC pool | Objects loaded at startup are never collected or traversed | Only for initial-load objects in cooked builds; runtime gameplay objects never qualify | [verified] `CoreUObject/Private/UObject/UObjectArray.cpp:146–204` |
| The "UObject Count" trace counter (5.8) | Shows the live object count in Insights | Shows the count, not the cost, and not what to do about it | [verified] `LevelTick.cpp:105, 1973` |
| Struct + generational handle in engine systems: `FWeakObjectPtr`, Mass entities, GAS active effects | Keeps high-count state out of UObjects | A pattern in engine internals, not a measured guide for ordinary gameplay data | [verified] `01b` #15 |
| Community advice ("use structs for thousands of things") | The right instinct | No measurements, no trade-off numbers | `01b` §6 (search results only) |

**The gap, after Phase 2 (stated honestly):**
- The **technique is not new**: the engine itself uses struct arrays with (index, generation) handles. P04 claims no new technique.
- What is missing in public is a **controlled, reproducible measurement** on UE 5.8 of the design choices on one workload. The choices: one UObject per item; the same with clusters; the same with incremental reachability; a struct array that still holds a reference per item; and a struct array with handles.
- The measurement should report GC pause, per-frame hitches, UObject count, memory and per-item access cost **together**, so the trade-off is visible.
- **Gap strength:** medium for the measurement, weak for the technique (`01b` #16). The project is a **measured engineering study plus a small reusable toolkit** (GC telemetry and generational handles), not a new GC technique.

## 4. Root-cause hypotheses

Each hypothesis gets a measurement that can confirm or reject it. The baselines (U0, U1, U2, S1) are captured before the proposed store (S0) exists.

| # | Hypothesis | Why we suspect it | How we test it |
|---|---|---|---|
| H1 | **The per-cycle GC pause grows about linearly with live UObjects** (one per item) | Reachability walks every reachable object and the gather step scans every object (`01b` #4) | U0 pause vs N ∈ {0, 1k, 10k, 30k, 100k}; linear fit, slope in µs per 1,000 items, R² reported |
| H2 | **References, not only objects, drive traversal cost** | The forum case (count down, time up); struct arrays that hold references are walked element by element (`01b` #5) | S1 (structs with one `TObjectPtr` each) vs S0 (no references) vs U0, same N |
| H3 | **A struct array with no UObject references adds no per-item GC cost** | Such an array emits nothing into the GC schema (`01b` #5) | S0 pause at N = 100k vs N = 0 |
| H4 | **Clustering cuts traversal but not total cost under churn** | Clustered objects still cost a flag write each; dropped items need dissolve + rebuild (`01b` #10) | U1 vs U0: pause, per-frame GC time, cluster maintenance time |
| H5 | **Incremental reachability caps the traversal slices but leaves first- and last-frame spikes that still grow with N** | Pre, the flag swap, cluster/root marking, the gather scan and Post are not sliced (`01b` #7) | U2 per-frame GC time: largest frame per cycle vs N; slice count per cycle |
| H6 | **Churn adds purge cost, which incremental purge spreads** | BeginDestroy/FinishDestroy per dead object, 2 ms per frame (`01b` #3) | Purge frames per cycle and their ms, U0 vs S0 (S0 has no purge work for items) |
| H7 | **Per-item access is cheaper with structs** | Contiguous elements vs one heap object per item. In packaged builds a `TObjectPtr` read is a raw pointer read, so any gap is layout (`01b` #9) | Per-item update ns and per-lookup ns (handle vs `TWeakObjectPtr`), packaged only |
| H8 | **Structs use less memory per item** | A UObject adds its header, an object-array slot and hash entries (`01b` §5) | Working-set delta per item vs the N = 0 run |

**Not a cause we accept without checking:** "GC cost is just the object count". H2 exists because a production case contradicts it (§2).

## 5. The cost of leaving it unsolved

At 60 fps a frame has **16.67 ms**. A GC cycle that blocks the game thread for 10 ms, on top of 8 ms of normal work, drops that frame below 60 fps every time GC runs.

*Arithmetic illustration only, not a measurement:* if reachability walked about 100 references per microsecond (the order of magnitude in Epic's KB example: 1,799,453 references in about 18 ms on a 126-thread machine), then 100,000 items with five references each would add about 5 ms per cycle. A laptop with fewer cores would be slower.

When this is ignored, teams typically:
- live with a periodic hitch and lengthen the GC interval, which lets memory grow;
- try incremental reachability, which is Experimental and not thread-safe;
- or rewrite the data layout late, under deadline, without numbers to guide it.

## 6. What "solved" means: success criteria, defined now

### 6.1 The workload (identical in every run)

The item model is deliberately minimal. It is a **workload**, not an inventory system: the subject is how GC cost scales with the number of objects and references.

- **Definitions:** 16 item definitions (`UGCBItemDef`, UObjects) created at start. Every item refers to one, chosen by a fixed seed.
- **Item state** (a "timed effect payload"): `TimeLeft` (s), `Magnitude`, `Charges`, plus its definition, held as a `TObjectPtr` in U0/U1/U2/S1 and as an index in S0.
- **Per frame:** every item runs `TimeLeft -= dt` and `Magnitude *= 1 − Decay·dt`, where `Decay` is read from its definition. Expired items are removed and replaced at once by a new item from the same seeded stream, so N stays constant.
- **Churn:** lifetimes are uniform in [50, 150] s (mean 100 s), and initial remaining times are uniform within each lifetime. That gives a steady **1% of N per second**: about 2,000 items die per GC cycle at N = 100k.
- **UObject items are invalidated on expiry** with `MarkAsGarbage()`, as actors are on destroy. Weak references then fail at once, matching handle semantics.
- **Random access probe:** each frame, 1,000 lookups through stable references (`TWeakObjectPtr` for UObject stores, `FGCBudgetHandle` for struct stores), drawn from a seeded pool of references in which some items have expired. Each lookup is checked against the simulation's ground truth.
- **Sizes:** N ∈ {0, 1,000, 10,000, 30,000, 100,000}. The headline sizes are **10,000** and **100,000**.
- **GC cadence:** a cycle every **120 frames** (2.0 s at the fixed 60 Hz step), about 30 cycles per 60 s capture. The benchmark director parks the engine's own timer and requests each cycle with `ForceGarbageCollection(false)`, which starts it through the engine's normal path (design in `02` §6, evidence `01b` #1). *Why 2 s and not the default 61.1 s:* 30 cycles per run give stable percentiles; the default would give one cycle per minute. The per-cycle pause depends on how much garbage a cycle finds, so the churn rate is fixed alongside it. The trade-off is stated in §8.
- **Camera:** fixed and static. Nothing renders per item; the scene is the engine's `Template_Default` map.

### 6.2 Compared configurations (all in the same build, chosen on the command line)

| ID | Configuration | Why it is included |
|---|---|---|
| E0 | No items (N = 0) | The engine's own GC cost in this project: the floor every other result is compared with |
| **U0** | One `UGCBItemObject` per item, grouped into pages of 1,024, default GC settings | What most projects do. The pages make U1's only difference the clustering |
| **U1** | U0 + each page a GC cluster (`CreateCluster`); pages with changes are rebuilt once per cycle, in the frame before it starts | The engine's clustering, used as well as runtime data allows |
| **U2** | U0 + `gc.AllowIncrementalReachability 1`, time limit 2 ms (Epic's docs example) | The engine's Experimental incremental GC |
| **S1** | Struct array, each element holding one `TObjectPtr` to its definition, plus generational handles | Separates "number of UObjects" from "number of references" (H2) |
| **S0** | **Struct array with no UObject references** (definition by index) **plus generational handles** (`FGCBudgetHandle`). **The proposed design** | Removes items from the GC's work entirely (H3) |

### 6.3 Metrics

| ID | Metric | Unit | How |
|---|---|---|---|
| M1 | **GC pause per cycle**: p95 and max | ms | `GetLastGCDuration()` read in `PostGarbageCollect`, our CSV stat (`01b` #3) |
| M2 | **Per-frame GC time on GC frames**: p95 and max | ms | Our bracket around the engine's GC call minus its non-GC floor (`01b` §4); validated once against the Insights timer `ConditionalCollectGarbage` |
| M3 | **GC-caused hitches per minute** | count/min | Frames where M2 > 5 ms (*GC spikes*: above 5.8.4's own default incremental slice, `GarbageCollection.cpp:310`), and frames where `FrameTime` > 33.3 ms **and** the frame is a GC frame (*GC hitches*) |
| M4 | **UObject count** | count | `GUObjectArray.GetObjectArrayNumMinusAvailable()`, our CSV stat each frame (the CSV `ObjectCount` category is compiled out, `01b` B-4) |
| M5 | **Resident memory** | MB; bytes per item | CSV `PhysicalUsedMB` (working set), median over the capture; minus the E0 run, divided by N |
| M6 | **Per-item update cost** | ns per item | Our timer around the store's update loop, divided by N, median over frames |
| M7 | **Per-lookup cost** and **correctness** | ns per lookup; errors | Our timer around the 1,000-lookup probe; stale lookups that resolve and live lookups that fail |
| M8 | Game-thread time per frame: median, p95 | ms | CSV `GameThreadTime` (context: the steady cost of each design) |
| M9 | Cluster maintenance (U1); barrier-marked objects (U2) | ms; count | Our CSV stat (U1); `-LogCmds="LogGarbage Log"` diagnostic batch (U2) |

Engine cross-checks are kept in every CSV: `GC/Count`, the CSV event `GC`, and `Exclusive/GameThread/GarbageCollection`.

### 6.4 Pass/fail criteria [target]

All values are medians across runs, with the [min–max] range across runs, unless stated otherwise.

| # | Criterion | Threshold |
|---|---|---|
| SC1 | **GC cost independent of item count** | S0 at N = 100k: M1 p95 ≤ E0's M1 p95 + 0.25 ms |
| SC2 | **Beats every engine option on the worst frame** | At N = 100k, S0's M2 max is lower than the **best** of U0, U1 and U2, with non-overlapping run ranges. If not met, we report it and analyse why. No reframing |
| SC3 | **No extra hitches** | S0 at N = 100k: GC spikes per minute (M3) ≤ E0's, and 0 GC hitches (> 33.3 ms) per minute |
| SC4 | **UObject count flat** | S0 at N = 100k adds ≤ 32 UObjects over E0 (store and definitions). Workload check: U0 adds ≥ N |
| SC5 | **Memory** | S0 bytes per item (M5) ≤ 50% of U0's at N = 100k |
| SC6 | **Access cost is no worse** | S0 per-item update (M6) ≤ U0's. S0 per-lookup (M7) ≤ 1.5 × U0's `TWeakObjectPtr::Get()` lookup. A miss is reported as the trade-off, not hidden |
| SC7 | **Correctness** | 0 stale lookups resolved and 0 live lookups rejected, in every run. The final checksum of item state is identical across all six configurations for the same seed (the simulation is the same work everywhere) |
| SC8 | **The curve is published** | U0, S1 and S0 at all five N, each with ≥ 3 runs. H1's slope and R² reported, whatever they turn out to be |

**10k results are always reported** next to 100k, so the curve shape is visible. The criteria are judged at 100k, where the effect is largest.

## 7. Measurement plan (summary; the full protocol goes in `09_Testing_And_Benchmarks.md`)

- **Build:** packaged **Development**, standalone, never the editor or PIE.
  - The launcher engine can't build Test (portfolio fact, [research/04](Research/04_local_engine_verification.md) §0b). Shipping has no CSV profiler.
  - Editor and uncooked numbers are not comparable: no disregard-for-GC pool, keep-flag scans, `TObjectPtr` access tracking (`01b` #9, #13).
- **GC settings for fairness:** `-NoVerifyGC` on every timed run (`01b` #11). Development's built-in GC pointer checks ("roughly 20%" per Epic's comment) can't be removed from a launcher engine. They apply to every configuration equally and are stated as a limit (`01b` #12).
- **Determinism:** fixed step `-deterministic -FPS=60`; fixed seed; `r.VSync 0`; windowed 1280×720 on the real GPU; `-nosound`; `-traceautostart=0`; `GameUserSettings.ini` reset before runs.
  - The capture is opened and closed by the game itself: `CSV_EVENT` BenchStart after a 10 s warm-up (which includes at least four GC cycles after the items are created), and BenchEnd after 60 s.
- **CPU:** every timed run is **pinned to the performance cores** (mask `0xFFFF` on this i7-14650HX; `Tools\cpu_affinity.py`). The runner pins itself before it records the environment or launches anything, so every game inherits the mask from its first instruction. It launches the real exe directly, waits with a timeout, kills the tree in `finally`, and records the mask read back from Windows.
- **Run order:**
  - one discarded priming run per batch;
  - then interleaved runs with the config order rotated each round. **Runs per combination = the number of configs in the batch**, so every config sits in every order position once.
- **Batches:**
  - **A (headline):** U0, U1, U2, S1, S0 × N {10k, 100k} × 5 runs.
  - **B (curve):** U0, S1, S0 × N {0, 1k, 10k, 30k, 100k} × 3 runs. The N = 0 runs are E0.
  - **C (diagnostics, never headline):** the `LogGarbage` phase breakdown; verification on vs off; an Insights trace (`-trace=default,counters`) to validate the bracket against `ConditionalCollectGarbage` and to cross-check the UObject count; LLM memory attribution.
- **Statistics:**
  - median, p95 and p99 per run (linear-interpolation percentiles), then the median across runs with [min–max];
  - improvements are claimed only when the ranges don't overlap;
  - M1 statistics are over cycles (about 30 per run), M2 over GC frames, M6–M8 over all captured frames.
- **Environment recorded with every batch:**
  - i7-14650HX (8P + 8E, 24 threads), RTX 5070 Laptop GPU, 32 GB RAM, Windows 11 build 26200;
  - project on D: (NVMe), engine on C: (NVMe);
  - UE 5.8.4, Development, MSVC 14.44.35229;
  - AC power, power plan, affinity mask.
- **Raw data:** untouched captures in `Benchmarks\Raw\`; computed results in `Benchmarks\Summaries\`, each naming its batch.

## 8. Limits we accept up front

- **A 2 s GC cadence is a stress cadence.** It samples many cycles. At the default 61.1 s, "spikes per minute" would scale down roughly by 2/61.1 **[inferred]**, while each cycle would find more garbage. One diagnostic run at the default interval shows the per-cycle effect.
- **Development is not Shipping.** Absolute GC times include Epic's pointer checks. Relative results are what we claim. A Shipping cross-check with our own file output is optional in Phase 8.
- **One machine, a laptop with a hybrid CPU.** Parallel GC scales with cores. Under the P-core pin the engine still creates workers for all 24 threads (`01b` B-6). The numbers describe this setup.
- **Incremental reachability is measured, not endorsed.** It is Experimental and not fully thread-safe (`01b` #8).
- **Clustering is measured with a page design of our own choosing.** Other designs could do better or worse. We document the design and its maintenance cost.
- **Structs give up things UObjects provide:** per-item reflection as an object, `UFUNCTION`s on the item, per-item Blueprint subclasses, and automatic lifetime. The migration guide (Phase 10) says when to keep UObjects.
- **A small N may show no difference.** That's a valid result.
