> **Raw Phase 2 research notes (D_existing_work), 2026-11-21.** Web sources with URLs, read on this date. "Opened" = the page was fetched and read; "search result only" = only a search-engine summary was seen, so the source is not cited for anything that matters. **The conclusions the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md).**

# P04 Phase 2, Part D: existing work and what it leaves open

---

## D1. Epic documentation: "Incremental Garbage Collection in Unreal Engine" (5.8 docs). Opened

URL: https://dev.epicgames.com/documentation/en-us/unreal-engine/incremental-garbage-collection-in-unreal-engine?application_version=5.8

- "Learn to use this Experimental feature, but use caution when shipping with it." and "We are initially releasing this feature as experimental".
- "Incremental reachability is not fully thread safe." An object manipulated on a worker thread "will not be marked as reachable", which "can result in the object being garbage collected prematurely". "We recommend enabling incremental reachability in single-threaded builds only (for example, dedicated servers)."
- Cvars on the page: `gc.AllowIncrementalReachability=1`, `gc.AllowIncrementalGather=1`, `gc.IncrementalReachabilityTimeLimit=0.002` ("sets the soft time limit to 2ms"), `gc.DelayReachabilityIteration` ("Default: 10", for stress-testing the barrier), `gc.VerifyNoUnreachableObjects`, `gc.ContinuousIncrementalGC`.
- The barrier: "any assignment to a `TObjectPtr`-exposed `UPROPERTY` immediately marks the object as reachable … also known as a garbage collector write barrier". Raw-pointer `UPROPERTY`s must be converted to `TObjectPtr`, "otherwise garbage collection might reclaim some of UObjects' memory too early".

**Checked against 5.8.4 source (Part A):**
- The Experimental label matches the cvar help text.
- The 2 ms value is an example: the source default is 5 ms.
- `gc.DelayReachabilityIteration` (default 10) does not exist under that name. The source has `gc.DelayReachabilityIterations`, default 0.

## D2. Epic knowledge base: "Garbage Collector Internals" (Ari Arnbjörnsson, Epic). Opened

Forum copy: https://forums.unrealengine.com/t/knowledge-base-garbage-collector-internals/501800 (posted 2022-02-24; replies to 2025). The KB page https://dev.epicgames.com/community/learning/knowledge-base/ePKR/unreal-engine-garbage-collector-internals rendered empty when fetched.

- Slow marking "might be a symptom of your game having too many UObjects alive"; "Usually bigger games don't need more than a couple hundred thousand UObjects alive at any time."
- One measured example, without clusters: "18.11 ms for GC - 99367 refs/ms while processing 1799453 references from 598753 objects with 0 clusters". With one cluster: reachability 0.85 ms, but "10.099500 ms for MarkObjectsAsUnreachable Phase" and a total of 11.47 ms. Explanation given: "how long MarkClusteredObjectsAsReachable takes".
- "it's maybe a 50% ms saving in a Shipping build for the clustered objects." "In Development and Test builds my cluster was actually not faster at all due to GC-verification." Remedy given: `-NoVerifyGC` and `gc.VerifyAssumptionsOnFullPurge=0`.
- How to cluster at runtime: override `CanBeInCluster()` to return true, then call `CreateCluster()` on the parent.
- Profiling advice: `-LogCmds="logGarbage verbose"`; read the "Reachability Analysis", "Gather Unreachable Objects" and "GC purged" lines.
- Engine version: not stated. Incremental reachability was "early on and experimental as of Unreal Engine 5.4".

**What it leaves open:**
- One data point on one machine (126 threads), an older engine version.
- No curve against object count, no per-frame hitch view, and no comparison with struct storage.

## D3. Epic forum: "Garbage collection slow on the server with world partitioning". Opened

URL: https://forums.unrealengine.com/t/garbage-collection-slow-on-the-server-with-world-partitioning/2670407 (posts dated Sept 15 to Oct 1; the year is not printed in the fetched text. **2025 [inferred]**, because the poster upgrades to UE 5.6.1 and staff mention the 5.7 Preview.)

- A 50-player World Partition dedicated server. GC "around 90ms to 100ms" every minute at about 250k → 125k objects. Asset clustering took it "from around 92ms to 78ms", and object removal brought it to about 18–19 ms.
- "Reduced object count by roughly 24.5K (197725 to 173042)" yet "Increased the server GC time by 0.3ms (18.74ms to 19.07ms)". **Object count alone doesn't predict GC time.** References and what is traversed matter too.
- Ari (Epic): clustering "only moves the time cost from reference-tracking to clearing the GC flag for clusters".
- ZhiKangShao (Epic) on incremental GC: "I would not recommend using that currently (in neither 5.5 or 5.6)". It is used only on single-threaded Fortnite Battle Royale dedicated servers. Staff also suggest `obj list -countsort` and trying a shorter GC period (30 s), and ask for Insights breakdowns (`PerformReachabilityAnalysis`, `GatherUnreachableObjects`).

**What it leaves open:** a production anecdote with uncontrolled changes. It is still the best public evidence that reference count, not only object count, drives cost. That evidence shaped the extra `S1` configuration (structs that still hold a reference), which separates the two.

## D4. Tom Looman, "Unreal Engine 5.8 Performance Highlights". Opened

URL: https://tomlooman.com/unreal-engine-5-8-performance-highlights/

- Lists: Added "UObject Count" trace counter (with a note to use the counters trace channel). His comment: it "is helpful to keep an eye on your total object count which directly affects garbage collection performance".
- Also: "GC: Add the option to GC based on pending actors to unload" (disabled by default), and "LightWeightInstances code marked as deprecated".

**Important for the README:** the README and `research/02` quoted the 5.8 change as Epic saying the counter was added "since object count strongly affects GC cost". **That sentence is not on Looman's page,** and Epic's release-notes page could not be read (D6). The README is corrected: the counter is verified in source (Part B1.2), and the GC remark is attributed to Looman in his own words.

## D5. Epic documentation: "Garbage Collection Settings in the Unreal Engine Project Settings" (5.8). Opened

URL: https://dev.epicgames.com/documentation/en-us/unreal-engine/garbage-collection-settings-in-the-unreal-engine-project-settings?application_version=5.8

- Settings described: Time Between Purging Pending Kill Objects; Allow Parallel GC ("will use multiple threads"); Incremental BeginDestroy Enabled ("destroy objects incrementally using a time limit each frame"); Create Garbage Collector UObject Clusters; Asset Clustering Enabled; Actor Clustering Enabled; Blueprint Clustering Enabled; Minimum GC Cluster Size; Maximum Object Count Not Considered By GC (cooked only); Maximum Number of UObjects that Can Exist in Cooked Game ("as small as possible"); Verify UObjects Are Not FGCObjects.
- Parallel GC is described as a normal setting, not as Experimental.

## D6. Epic UE 5.8 release notes. Not readable

URL: https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes. The fetch returned only a table-of-contents heading (the page body is rendered by script). **No claim in this project rests on it.**

## D7. Portfolio research and other community sources

- `research/02` [T1-E] (copied to [../Research/02_technical_challenges.md](../Research/02_technical_challenges.md)): the incremental GC docs summary; a Cesium for Unreal issue reporting a 629 ms reachability hitch (https://github.com/CesiumGS/cesium-unreal/issues/980, search result only, not relied on).
- UE4-era community wiki "Garbage Collection Overview" (https://nerivec.github.io/old-ue4-wiki/pages/garbage-collection-overview.html, search result only) and an Epic forum thread "Inventory System: Struct of Items or Array of classes?" (https://forums.unrealengine.com/t/inventory-system-struct-of-items-or-array-of-classes/14045, search result only). Both give the folk advice "use structs when you have thousands of things". **Neither gives measurements.**

## D8. Engine-internal precedents (Part C3)

- `FWeakObjectPtr` (index + serial number).
- Mass entities (`FMassEntityHandle`: index + serial number; fragments are structs).
- GAS active effects (a struct array addressed by `FActiveGameplayEffectHandle`).

Epic itself keeps high-count gameplay state out of UObjects in these systems.

---

## D9. Gap assessment (re-checked in Phase 2, as the roadmap required)

| Claim in the roadmap | After Phase 2 |
|---|---|
| "GC reachability time grows with UObject count" | **Supported by source and by Epic staff** (D2, D3), with a caveat: references and the reachable set matter, not only the count (D3). Our workload separates objects from references (configuration `S1`) |
| "Compare with GC clustering" | Possible at runtime (`CreateCluster`, C4). Epic staff say clustering moves cost rather than removing it, and Development verification hides any gain (D2, D3). Our runs use `-NoVerifyGC` so clustering gets a fair test |
| "Compare with incremental reachability (Experimental)" | Experimental, not fully thread-safe, and **not recommended by Epic staff for 5.5/5.6** (D1, D3). We measure it as an option a studio might try, with the safety caveat stated |
| "Move hot data to struct arrays with stable handles" | **Not new**: Epic uses the pattern (D8), and community advice recommends it (D7) |
| "Measured GC-cost-vs-design curves" | **Not found publicly:** single data points only (D2, D3). This remains the real gap |

**Honest conclusion:**
- The **technique** gap is **weak**: the pattern exists, and we claim no novelty for it.
- The **measurement** gap is **medium**: no controlled, reproducible 5.8 comparison was found of one-UObject-per-item, clustering, incremental reachability and struct storage on one workload, with per-frame hitches, memory and the access-cost trade-off reported together.
- Scope adjustment: the project is presented as a **measured engineering study plus a small reusable toolkit** (GC telemetry and generational handles), not as a new GC technique. The README and `01` say so.
