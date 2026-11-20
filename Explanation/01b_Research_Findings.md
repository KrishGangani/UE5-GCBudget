# 01b · Research Findings (Phase 2): GCBudget

**Phase:** 2 (Research), completed 2026-11-21 · **Engine:** UE 5.8.4 (CL 58902052), read from `C:\Program Files\Epic Games\UE_5.8\Engine`

**Method:**
- Four investigations: GC internals, measurement, data layout and handles, and existing work on the web.
- Every engine claim cites a path relative to `Engine/Source/Runtime/` (or `Engine/Config/`, `Engine/Plugins/`) plus a line number in 5.8.4.
- The claims that drive design decisions were opened and re-read before being written here; they are marked ✔.
- Claims drawn from following code flow rather than one explicit line are marked **[inferred]**.
- Anything not checked is **[unverified]**. Hypotheses still need a measurement and are marked **[to measure]**.

**Why this phase matters.** Phase 1 said what we *think* drives GC cost. Phase 2 checks what the 5.8.4 collector actually does. The sixteen findings below shaped the design, and six more changed the benchmark plan. One also corrected the README: a quote it attributed to the 5.8 release could not be traced to Epic, nor found on the page it was taken from.

---

## 1. What Phase 2 changed

| # | Finding | Consequence for GCBudget |
|---|---|---|
| 1 | **In a game, GC is started by `UEngine::ConditionalCollectGarbage` at the end of `UWorld::Tick`.** It runs once per frame, adds the **game** delta to its timer and starts a **non-full-purge** cycle once the timer passes `gc.TimeBetweenPurgingPendingKillObjects` ✔ (`Engine/Private/LevelTick.cpp:1969–1970`; `Engine/Private/UnrealEngine.cpp:2185, 2265, 2284–2287, 2323–2336`) | With a fixed step the GC cadence is an exact number of frames. In Phase 3 this became: the director parks the timer (3600 s) and calls `ForceGarbageCollection(false)` every 120 frames, which only moves the timer (`UnrealEngine.cpp:2136–2139`) so the cycle starts through the same branch (`02` §6). The timer field itself is private (`Engine/Classes/Engine/Engine.h:2006–2012`). The harness never calls `CollectGarbage()` itself (next row) |
| 2 | **`CollectGarbage` and `TryCollectGarbage` default to a full purge** ✔ (`CoreUObject/Public/UObject/UObjectGlobals.h:952, 962`), and **full purges never run incrementally** ✔ (`CoreUObject/Private/UObject/GarbageCollection.cpp:5686, 5983`) | A hand-written `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS)` would silently disable the incremental baseline and turn purge into one blocking step. Listed in `07_Common_Mistakes.md` |
| 3 | **A default cycle is one blocking frame, then several time-sliced purge frames.** The blocking frame is Pre + reachability + Post, which `GetLastGCDuration()` times exactly ✔ (`GarbageCollection.cpp:5666–5675, 5700–5961`). The purge then runs under `gc.IncrementalGCTimePerFrame` = 2 ms per frame ✔ (`UnrealEngine.cpp:1826, 2290–2294`) | Two metrics are needed, not one: the **per-cycle pause** (`GetLastGCDuration`) and the **per-frame GC time** (blocking frame plus purge frames, and with incremental reachability every slice) |
| 4 | **The 5.8 mark phase is a flag swap, not a loop.** The per-object costs are: walking every **reachable** object's references, a cheap flag write per **clustered** object, a **gather scan over every object** in the object array, and BeginDestroy/FinishDestroy per **dead** object ✔ (`GarbageCollection.cpp:4503–4507, 4295–4346, 5388–5410`; `GarbageCollectionInternalFlags.h:116–123`) | Cost model for the analysis: fixed + reachable objects + references + total objects (gather) + clustered objects + dead objects. The workload is built so these can be told apart (§2 of `01`) |
| 5 | **A struct-array `UPROPERTY` whose element holds no strong object reference adds nothing to the GC schema**, so GC never visits its elements ✔ (`FArrayProperty::EmitReferenceInfo`, `GarbageCollection.cpp:6869–6872`). An element that does hold a `TObjectPtr` makes the array a `StructArray` member that GC walks element by element ✔ (`:6874–6876`) | The proposed store (S0) keeps no `TObjectPtr` in its elements; definitions are referenced by index. A second struct store (S1) **keeps** one `TObjectPtr` per element, so the measurement can separate "number of UObjects" from "number of references" |
| 6 | **Parallel reachability is the default and is not experimental** (`gc.AllowParallelGC` = 1, help text "Used to control parallel GC.") ✔ (`GarbageCollection.cpp:4717–4723`; `BaseEngine.ini:1747`). **Incremental reachability and incremental gather are labelled "(experimental)"**, both default 0, and the incremental time limit defaults to **5 ms** ✔ (`:302–324`). The task brief and the README grouped "parallel and incremental" as Experimental; only the incremental paths are | The incremental baseline (U2) enables incremental reachability only. It uses **2 ms**, the value Epic's 5.8 docs give as their example, because 5 ms is a third of a 60 fps frame. Parallel GC stays on in every configuration, as shipped |
| 7 | **Incremental reachability still has non-incremental edges.** The first frame pays Pre, the flag swap, cluster marking and root marking before the time limit applies. The last frame pays cluster dissolving, weak-reference clearing, the gather scan and the Post delegates ✔ (`GarbageCollection.cpp:6019–6033, 4542–4570, 5849, 5900, 5939`). A new GC request while one is pending finishes the pending one as a full purge ✔ (`:5967–5977`) | Hypothesis H5 is now specific: U2 should cut the *traversal* spike but may leave first-frame and last-frame spikes that grow with object count **[to measure]**. Per-frame GC time, not only the per-cycle total, is the metric that can show this |
| 8 | **Incremental reachability is not fully thread-safe**: Epic's docs recommend it "in single-threaded builds only (for example, dedicated servers)", and Epic staff did not recommend it in 5.5 or 5.6 (D1, D3) | U2 is measured as an option a studio might try. Every item operation in this project runs on the game thread, so the workload is safe. The docs carry the shipping caveat, and U2 is never presented as a recommendation |
| 9 | **The `TObjectPtr` write barrier is compiled into every configuration** (`UE_OBJECT_PTR_GC_BARRIER` = 1) and only does work while incremental reachability is pending ✔ (`CoreUObject/Public/UObject/ObjectPtr.h:23–25, 337–361`). **Access tracking and late resolve exist only with editor-only data** ✔ (`ObjectHandleDefines.h:9–21`; `ObjectHandle.h:29–55, 603–615`). In a packaged game a `TObjectPtr` read is a raw pointer read | Every UObject reference in the project is a `TObjectPtr` (required for U2's correctness). Per-item access cost is measured in packaged builds only: editor numbers would include the tracking hook |
| 10 | **Runtime clustering is possible but fits immutable data.** `CreateCluster()`, `CanBeInCluster()`, `AddToCluster()` and `GUObjectClusters.DissolveCluster()` are public and exported ✔ (`UObjectBaseUtility.h:420, 425, 437`; `UObjectArray.h:1593–1650, 1675`). Only objects in the root's package join, clusters under `gc.MinGCClusterSize` (5) are discarded, clustered objects' own references are not traversed, and **clustered objects are marked reachable whenever their root is** ✔ (`UObjectClusters.cpp:741–751, 927–996`; `GarbageCollection.cpp:4324–4346`). Asset and actor clustering are **off** in `BaseEngine.ini` ✔ (`:1754–1755`) | The clustering baseline (U1) groups items into **pages** that are cluster roots. A dropped item stays alive until its page's cluster is dissolved, so U1 rebuilds the clusters of changed pages **once per GC cycle, in the frame before the cycle starts**, and that work is counted as GC-related time (`02` §6). U1 is never combined with U2 |
| 11 | **Packaged Development verifies GC assumptions on every GC by default** (`gc.VerifyAssumptions` = `!SHIPPING && !TEST && !WITH_EDITOR`) ✔ (`GarbageCollectionVerification.cpp:36–42`; used at `GarbageCollection.cpp:5803`). Epic's KB: a cluster "was actually not faster at all due to GC-verification" in Development (D2) | **Every timed run passes `-NoVerifyGC`** (parsed at `CoreUObject/Private/UObject/Obj.cpp:5917`; it switches the three verification cvars off, :5918–5946). One diagnostic batch measures the cost of verification itself |
| 12 | **Development keeps GC pointer checks that Epic says "add roughly 20% overhead to GC times"** (`ENABLE_GC_OBJECT_CHECKS`) ✔ (`CoreUObject/Public/UObject/GarbageCollection.h:30–33`). It is a compile-time define in the precompiled launcher engine | **Launcher-engine limit:** absolute Development GC times are higher than Shipping. All configurations pay it, so comparisons stay fair. An optional Shipping cross-check (our own file output, since Shipping has no CSV profiler) is planned for Phase 8 |
| 13 | **The disregard-for-GC pool is cooked-only and can't be enabled in the editor** ✔ (`UObjectArray.cpp:146–204`, check at :202). The Phase 4 smoke runs confirmed it: uncooked, "CloseDisregardForGC: 0/0 objects in disregard for GC pool" with 48,318 objects in the root set; packaged Development, "34,895/34,895 objects in disregard for GC pool" (`09`, smoke-log table) | Editor and uncooked GC numbers are not comparable. **Packaged builds only** for every GC number |
| 14 | **`ConditionalCollectGarbage` is not virtual** (`Engine/Classes/Engine/Engine.h:2820`), and the engine's CSV column `Exclusive/GameThread/GarbageCollection` covers **only** reachability traversal and incremental purge: Pre, gather, cluster dissolving and Post fall outside it ✔ (`GarbageCollection.cpp:5646–5654, 4796`) | Our telemetry brackets the call between `FWorldDelegates::OnWorldPostActorTick` and `OnWorldTickEnd` ✔ (`LevelTick.cpp:1906, 2061`) and subtracts the bracket's non-GC floor offline (§4). Phase 5 validates the bracket against the Insights timer `ConditionalCollectGarbage` (`LevelTick.cpp:1969`) |
| 15 | **The engine's own patterns already keep high-count state out of UObjects**: `FWeakObjectPtr` (index + serial number), Mass (`FMassEntityHandle`: index + serial number, struct fragments) and GAS active effects (a `TArray<FActiveGameplayEffect>` addressed by handles) ✔ (`WeakObjectPtr.h:570–574`; `Mass/MassCore/Public/Mass/EntityHandle.h:12, 23, 26`; `GameplayAbilities/…/GameplayEffect.h:1355, 1911`) | **No novelty claim** for "struct array + generational handle". Our handle copies the engine's (index, generation) shape on purpose, and the per-lookup baseline for UObjects is the engine's own `TWeakObjectPtr::Get()` |
| 16 | **The gap is narrower than the roadmap said** (D9): the technique is established, and public evidence is single data points (one KB example: 18.11 ms for 598,753 objects; one production thread) | Reframed as a **measured engineering study plus a small reusable toolkit** (GC telemetry and generational handles). README and `01` say so plainly |

**Benchmark-plan changes:**

| # | Finding | Consequence |
|---|---|---|
| B-1 | Development verifies GC by default (#11) | `-NoVerifyGC` on every timed run; verification cost measured once, separately |
| B-2 | The cadence follows game time (#1) | A cycle every 120 frames (2.0 s at the 60 Hz fixed step), requested by the director; about 30 cycles per 60 s capture |
| B-3 | The engine's CSV GC column is partial (#14) | Per-frame GC time = our bracket minus its non-GC floor. The engine columns `GC/Count`, CSV event `GC` and `Exclusive/GameThread/GarbageCollection` are kept in every CSV as cross-checks |
| B-4 | The 5.8 "UObject Count" counter is **trace-only** and needs the `counters` channel, which the 5.8.4 default preset (`cpu,gpu,frame,log,bookmark,screenshot,region`) does not include ✔ (`LevelTick.cpp:105, 1973`; `Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:138`). The CSV `ObjectCount` category is compiled out ✔ (`CoreUObject/Public/UObject/UObjectStats.h:9–10`) | Our own CSV stat records `GUObjectArray.GetObjectArrayNumMinusAvailable()` (the same value the trace counter shows). Insights runs use `-trace=default,counters` |
| B-5 | `LogGarbage` defaults to Warning ✔ (`GarbageCollection.h:37`), and its timing lines run inside GC | Per-phase breakdown (`-LogCmds="LogGarbage Log"`) only in a diagnostic batch, never in headline runs |
| B-6 | The engine counts all 24 logical CPUs even when the process is pinned **[inferred]** (`Core/Private/Windows/WindowsPlatformMisc.cpp:2614–2690, 2719–2737`) | Timed runs are pinned to the P-cores (mask `0xFFFF`, portfolio protocol). Parallel GC then runs about 22 workers on 16 CPUs. That is the same for every configuration, and it is recorded in the environment |

---

## 2. README claims, checked

| README claim | Verdict | Evidence | Action |
|---|---|---|---|
| "Epic added a 'UObject Count' counter in 5.8" | **True**, but it is an Insights **trace** counter, not a CSV stat | `LevelTick.cpp:105, 1973`; absent from 5.6.1 and 5.7.4 `LevelTick.cpp` (grep) | Wording made precise |
| …"since object count strongly affects GC cost" (in quotation marks) | **Not verifiable as a quote.** Tom Looman's 5.8 page says the counter helps watch object count, "which directly affects garbage collection performance" (his words). Epic's release notes page could not be read | D4, D6 | **Quote removed**; Looman's remark attributed to him |
| "GC reachability time grows with it and causes hitches" | **Supported** (mechanism in source; Epic KB and forum), with the caveat that references matter too | #4, #5; D2, D3 | Caveat added; configuration S1 isolates it |
| "incremental reachability (Experimental)" | **True** | #6, D1 | Kept; thread-safety caveat added |
| "parallel … paths (Experimental)" (task brief) | **False for parallel**: default on, not labelled experimental | #6 | Not claimed anywhere |
| "`TObjectPtr` write barriers" | **True** (always compiled; active only during incremental reachability) | #9 | Kept |
| "GC clustering" | **True**; runtime API exists; asset/actor clustering off by default | #10 | Kept, with the churn constraint |
| "Memory Insights, LLM tags, `obj list`" | **Present** in the Development build: LLM via `-LLM`/`-LLMCSV`, tags `UObject`/`GC`, `obj list -countsort` | `Core/Private/HAL/LLM/LLM.cpp:2318, 2324`; `Core/Public/HAL/LowLevelMemTracker.h:233, 241`; `UnrealEngine.cpp:9607, 9879` | Kept |
| "per-item access cost (µs)" | Unit too coarse: an update is expected to be nanoseconds per item **[inferred]** | — | Reported in **ns** |
| "gap strength: Medium" | **Medium for measurement, weak for technique** | #15, #16 | README updated |

---

## 3. GC internals (evidence)

| Fact | Evidence (5.8.4) |
|---|---|
| Per-frame branch order: pending incremental reachability → timer-started cycle → incremental purge | `UnrealEngine.cpp:2276–2294` ✔ |
| The timer adds `FApp::GetDeltaTime()`; a game world turns the cycle into a non-full purge | `UnrealEngine.cpp:2265, 2323–2336` ✔ |
| `gc.TimeBetweenPurgingPendingKillObjects`: C++ 60.0, `BaseEngine.ini` 61.1 | `UnrealEngine.cpp:1777`; `Engine/Config/BaseEngine.ini:1749` ✔ |
| `CollectGarbageInternal` times the blocking call into `LastGCDuration`, then `GC/Count` += 1 | `GarbageCollection.cpp:5666–5675` ✔ |
| Pre: delegates, previous purge finished, "Collecting garbage", verification | `:5700–5824` ✔ |
| Post: cluster dissolving, gather, `PostReachabilityAnalysis`, unhash only if full purge or incremental BeginDestroy off, `PostGarbageCollect` | `:5841–5961` ✔ |
| Options: Parallel unless forced single-threaded; IncrementalReachability only for non-full purges | `:5678–5687, 4727–4736` ✔ |
| Worker count = task-graph workers (clamped) | `:7330–7333, 7494–7497` |
| Clustered objects marked reachable with their root, one flag write each | `:4324–4346` ✔ |
| Gather scans the whole object array from the first GC index | `:5378–5410` ✔ |
| A Garbage object referenced from a cluster forces a full walk of that cluster and dissolves it | `:1226–1240` ✔ |
| Max UObjects in a cooked game: 2,097,152 by default | `CoreUObject/Private/UObject/UObjectBase.cpp:1363, 1382–1386` ✔ |
| `GARBAGE_COLLECTION_KEEPFLAGS` = `RF_NoFlags` in a game, so the slow keep-flag scan is skipped | `GarbageCollection.h:28`; `GarbageCollection.cpp:4425` ✔ |

## 4. Measurement (evidence)

| Fact | Evidence (5.8.4) |
|---|---|
| CSV category `GC` (on by default); `GC/Count`; CSV event `GC` per reachability pass | `GarbageCollection.cpp:80, 5674, 5650, 5660` ✔ |
| `Exclusive/GameThread/GarbageCollection` = reachability (non-full) + incremental purge; `…GarbageCollectionFull` = full-purge reachability. Column naming: thread prefix, then category prefix | `:5651, 4796, 5661`; `Core/Private/ProfilingDebugging/CsvProfiler.cpp:2447–2457` ✔ |
| `PhysicalUsedMB` = working set on Windows | `CsvProfiler.cpp:4175, 4189`; `Core/Private/Windows/WindowsPlatformMemory.cpp:342` ✔ |
| Insights: CPU timer `ConditionalCollectGarbage` around the engine call; region `GarbageCollection` from cycle start to completion | `LevelTick.cpp:1969`; `GarbageCollection.cpp:5716, 4876, 5959` ✔ |
| `GetLastGCDuration()` and the GC delegates are exported; `FStats` is not reachable from a plugin | `GarbageCollection.h:192, 199`; `UObjectGlobals.h:3478–3508`; `GarbageCollection.cpp:6217` ✔ |
| Pending-work queries: `IsIncrementalReachabilityAnalysisPending`, `IsIncrementalPurgePending`, `IsIncrementalUnhashPending` | `GarbageCollection.cpp:6180`; `UObjectGlobals.h:979, 986` ✔ |
| `-LogCmds=` exists; the GC log lines and their formats | `Core/Private/Logging/LogSuppressionInterface.cpp:519`; `GarbageCollection.cpp:5234, 5425, 6071, 6077, 6082` ✔ |
| `-traceautostart=0` stops an open Insights from tracing timed runs | `Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:267–268` |

**The per-frame GC metric, defined:**

```text
bracket(f)    = time from OnWorldPostActorTick to OnWorldTickEnd in frame f (our telemetry, every frame)
GC frame      = a cycle started in f (PreGarbageCollect fired), or GC work was pending when the bracket opened
floor(run)    = median bracket over the run's non-GC frames
GC ms(f)      = bracket(f) − floor(run)        for GC frames; 0 otherwise
```

Between the two delegates, the workload has nothing variable except GC: async-trace finish, a net flush with no net driver, the FX tick with no effects, the CSV world counts **[inferred]**. The floor is published, not hidden.

## 5. Data layout and handles (evidence)

| Fact | Evidence (5.8.4) |
|---|---|
| Per UObject: the `UObjectBase` members plus the vtable, one `FUObjectItem`, hash-table entries, a traversal while reachable (including Outer and Class), a gather-scan slot, and purge work when it dies | `UObjectBase.h:399–464`; `UObjectArray.h:41–95`; `GarbageCollection.cpp:6288–6345`; `UObjectClusters.cpp:674–687` |
| A packaged game's `FObjectHandle` is `UObject*`; reads don't call the tracking hook | `ObjectHandle.h:29–55, 603–615` ✔ |
| With tracking compiled in (editor), a read costs an atomic load and a branch, plus callbacks if any are registered | `ObjectHandleTracking.h:103–121` |
| `FWeakObjectPtr` = `ObjectIndex` + `ObjectSerialNumber` | `WeakObjectPtr.h:570–574` ✔ |

## 6. Existing work, updated

| Source | What it gives | What it leaves open | Evidence |
|---|---|---|---|
| Epic docs: Incremental GC (5.8) | The feature, its cvars, the barrier, the thread-safety warning | No numbers; its example limit (2 ms) and one cvar name/default differ from source | D1 |
| Epic KB: GC Internals (Ari Arnbjörnsson, 2022) | One measurement (598,753 objects: 18.11 ms; with one cluster 11.47 ms), the runtime clustering recipe, the Development verification caveat, "a couple hundred thousand UObjects" guidance | One data point, an unstated older version, a 126-thread machine | D2 |
| Epic forum: slow server GC with World Partition (2025 [inferred]) | Production timings; "object count alone doesn't predict GC time"; Epic staff on clustering ("only moves the time cost") and incremental GC ("would not recommend") | Uncontrolled changes; a dedicated server | D3 |
| Tom Looman, 5.8 highlights | The new "UObject Count" trace counter | No GC measurements | D4 |
| Engine precedents: weak pointers, Mass, GAS | The struct + generational-handle pattern | No published comparison for ordinary gameplay data | #15 |
| Community advice (UE4 wiki, forum) | "Use structs when you have thousands of things" | No measurements | D7 (search results only) |

**The gap, stated conservatively:**
- The pattern is established, and P04 claims no new technique.
- Among the public sources reviewed, none gives a **controlled, reproducible** 5.8 comparison, on one workload, of one UObject per item, the same with clusters, the same with incremental reachability, a struct array that still holds references, and a struct array with handles.
- None reports per-frame hitches, memory, UObject count and per-item access cost together.
- Studio-internal measurements aren't public, and a web search can't prove absence. This is a statement about what was reviewed.

## 7. Hypothesis status after Phase 2

| # | Hypothesis (`01` §4) | Status |
|---|---|---|
| H1 | Per-cycle pause grows about linearly with live UObjects (one per item) | **Supported by code** (#4) **[to measure]**: slope and linearity |
| H2 | References drive traversal cost, not only object count | **Supported by code** (#5) and by D3 **[to measure]** with S1 vs S0 |
| H3 | A struct array with no UObject references adds no per-item GC cost | **Supported by code** (#5): GC never visits the elements. The measurement checks that nothing else scales with N |
| H4 | Clustering cuts traversal but adds per-object marking and churn maintenance | **Supported by code** (#10) and Epic staff (D3) **[to measure]**: the net effect |
| H5 | Incremental reachability caps traversal slices but leaves first- and last-frame spikes | **Supported by code** (#7) **[to measure]** |
| H6 | Churn adds purge cost, spread by incremental purge | **Supported by code** (#3) **[to measure]** |
| H7 | Struct iteration is cheaper per item than UObject iteration | Packaged `TObjectPtr` reads are raw (#9), so any difference is layout and indirection **[to measure]** |
| H8 | Structs use less memory per item | **Supported by code** (§5 lists what a UObject adds) **[to measure]**: bytes per item |

## 8. Carried into Phase 3

- **Telemetry** in the plugin: the bracket, the GC-frame flag, the per-cycle pause from `GetLastGCDuration()` in `PostGarbageCollect`, the live UObject count, all as CSV stats once per frame.
- **Handles** in the plugin: an (index, generation) handle and a slot allocator, a plain C++ struct testable without a world.
- **Configurations** E0 (no items), U0, U1, U2, S1, S0, all switched by command line in one build.
- **Cadence** owned by the director (a cycle every 120 frames); `-NoVerifyGC`; fixed step; P-core affinity; packaged Development only.
- U1 pages and their once-per-cycle cluster rebuild; U2 at 2 ms; no U1 + U2 combination.
- Every UObject reference is a `TObjectPtr`; no gameplay code touches items off the game thread.

**Raw research notes**, with every quote, are in [Phase2_Raw_Notes/](Phase2_Raw_Notes/README.md) as an audit trail. This file is authoritative: everything the design depends on is restated above with its evidence.
