> **Raw Phase 2 research notes (B_measurement), UE 5.8.4, 2026-11-21.** Kept as an evidence trail. **The findings the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md) (marked ✔).**

# P04 Phase 2, Part B: how to measure GC cost in a packaged 5.8.4 build

Paths are relative to `Engine/Source/Runtime/` unless they start with `Engine/Config`. **[inferred]** = drawn from code flow.

The question: which numbers does the engine already produce about GC, which ones are complete, and what must the project measure itself?

---

## B1. What the engine already emits

### B1.1 CSV profiler (the main capture tool, Development builds)

| Column / event | Produced by | What it covers | Evidence |
|---|---|---|---|
| CSV category `GC` | `CSV_DEFINE_CATEGORY_MODULE(COREUOBJECT_API, GC, true)` | Enabled by default | `CoreUObject/Private/UObject/GarbageCollection.cpp:80` |
| `GC/Count` | `CSV_CUSTOM_STAT(GC, Count, 1, Accumulate)` at the end of `CollectGarbageInternal` | Number of GC **cycle starts** in the frame (incremental reachability slices after the first don't pass through here) | `:5674` |
| CSV event `GC` | `CSV_EVENT_GLOBAL(TEXT("GC"))` | Fired by **every** reachability pass: the first one and each incremental slice | `:5650`, `:5660` |
| `Exclusive/GameThread/GarbageCollection` | `CSV_SCOPED_TIMING_STAT_EXCLUSIVE(GarbageCollection)` in `CollectGarbageIncremental` and in `IncrementalPurgeGarbage` | **Reachability traversal (non-full purge) + incremental purge only.** Pre (delegates, verification, previous purge), the gather scan, cluster dissolving and the Post work are **outside** it, so they are charged to the enclosing exclusive scope, `WorldTickMisc` (`Engine/Private/LevelTick.cpp:1506`) **[inferred]** | `:5651`, `:4796`; naming rule `Core/Private/ProfilingDebugging/CsvProfiler.cpp:2447–2457` (thread prefix, then category prefix) |
| `Exclusive/GameThread/GarbageCollectionFull` | same macro in `CollectGarbageFull` | Reachability of a full purge only | `:5661` |
| `Exclusive/GameThread/GarbageCollectionDebug` | garbage-reference tracking rerun | Only with `gc.GarbageReferenceTrackingEnabled` | `:6101` |
| `PhysicalUsedMB` | `CSV_CUSTOM_STAT_MINIMAL_GLOBAL(PhysicalUsedMB, …)` from `FPlatformMemoryStats::UsedPhysical` | **Process working set** on Windows (`ProcessMemoryCounters.WorkingSetSize`) | `CsvProfiler.cpp:4175, 4189`; `Core/Private/Windows/WindowsPlatformMemory.cpp:342` |
| `ObjectCount/Total` | `RecordWorldCountsToCSV` | **Not available:** compiled only when `CSV_TRACK_UOBJECT_COUNT`, which defaults to **0**; a launcher engine can't change it | `Engine/Private/LevelTick.cpp:65–66, 1480–1483`; `CoreUObject/Public/UObject/UObjectStats.h:9–10` |
| `Ticks/*`, `ActorCount/*` | `RecordWorldCountsToCSV` at the end of `UWorld::Tick` (cvars `csv.RecordTickCounts`, `csv.RecordActorCounts`, default 1) | Unrelated, but they run **inside** the measurement bracket chosen in B3 | `LevelTick.cpp:1409, 1419, 1987` |

### B1.2 Unreal Insights (trace)

| Item | Evidence | Notes |
|---|---|---|
| CPU timer **`ConditionalCollectGarbage`** wraps the engine's per-frame GC call exactly | `TRACE_CPUPROFILER_EVENT_SCOPE(ConditionalCollectGarbage)`, `LevelTick.cpp:1969` | The most complete per-frame GC time the engine records. Needs the `cpu` channel (in the default preset) |
| Named events `CollectGarbageIncremental`, `CollectGarbageFull`, `IncrementalPurgeGarbage`, `FRealtimeGC_PerformReachabilityAnalysis`, plus scopes `GatherUnreachableObjects`, `UnhashUnreachableObjects`, `DissolveUnreachableClusters`, `MarkClusteredObjectsAsReachableTask`, `Broadcast*` | `GarbageCollection.cpp:5649, 5659, 4794, 6035, 5366, 6290, 5291, 4313, 5740, 5904, 5942` | Per-phase breakdown in Timing Insights |
| Region `GarbageCollection` from cycle start to cycle completion | `TRACE_BEGIN_REGION` `:5716`; `TRACE_END_REGION` `:4876` (after incremental purge) or `:5959` (full purge) | A whole-cycle span across frames. Needs the `region` channel (in the default preset) |
| **Counter "UObject Count"** (new in 5.8) | `TRACE_DECLARE_INT_COUNTER(UObjectCount, TEXT("UObject Count"))` `LevelTick.cpp:105`; set every world tick right after GC: `TRACE_COUNTER_SET(UObjectCount, GUObjectArray.GetObjectArrayNumMinusAvailable())` `:1973` | **Absent from the 5.6.1 and 5.7.4 `LevelTick.cpp`** (grep for `UObjectCount` finds only the CSV `ObjectCount` line there). It is a **trace** counter, not a CSV stat |
| The counter needs the **`counters`** channel, which the 5.8.4 **default preset does not include**: `GDefaultChannels = "cpu,gpu,frame,log,bookmark,screenshot,region"` | `Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:138`; channel `Core/Public/ProfilingDebugging/CountersTrace.h:45` | Use `-trace=default,counters` |
| An open Insights auto-traces every Development run unless `-traceautostart=0` | `TraceAuxiliary.cpp:267–268` | Timed runs pass `-traceautostart=0`; trace runs are separate |

### B1.3 Stats system (`stat` commands)

| Item | Evidence |
|---|---|
| Stats group `STATGROUP_GC` ("Garbage Collection") | `CoreUObject/Public/UObject/GarbageCollection.h:38` |
| Cycle stats: `STAT_CollectGarbageInternal`, `STAT_IncrementalPurgeGarbage`, `STAT_FArchiveRealtimeGC_PerformReachabilityAnalysis`, `CollectGarbageInternal.GatherUnreachableObjects`, `…DissolveUnreachableClusters`, `…VerifyGCAssumptions`, `UnhashUnreachableObjects` | `GarbageCollection.cpp:5714, 4795, 6036, 5367, 5292, 5805, 6291` |
| `STAT_GCMarkTime` ("GC Mark Time") and `STAT_GCSweepTime` ("GC Sweep Time") in `STATGROUP_Game` | `Engine/Public/EngineStats.h:52–53`; used `UnrealEngine.cpp:2278, 2286, 2291` |
| `STAT_ConditionalCollectGarbage` (quick scope) | `UnrealEngine.cpp:2191` |

Useful interactively (`stat gc`); not used for headline numbers because the CSV profiler and Insights already cover it, and the stats system adds its own overhead **[inferred]**.

### B1.4 Log lines (`LogGarbage`)

`LogGarbage` defaults to **Warning** (`GarbageCollection.h:37`), so the timing lines below appear only with `-LogCmds="LogGarbage Log"` (`-LogCmds=` parsed in `Core/Private/Logging/LogSuppressionInterface.cpp:519`).

| Line (format string) | Evidence |
|---|---|
| `"%ls%lsCollecting garbage%ls"` ("Resuming", "Incrementally", "while async loading") | `GarbageCollection.cpp:5780` |
| `"%.2f ms for Verify GC Assumptions"` | `:5817` |
| `"GC Reachability iteration time: %.2f ms (%.2f ms on reference traversal, latency: %.3f ms)"` (each incremental slice) | `:6071` |
| `"GC Reachability Analysis total time: %.2f ms (%.2f ms on reference traversal)"` | `:6077` |
| `"%.2f ms for %lsGC - %d refs/ms while processing %d references from %d objects %ls with %d clusters"` | `:6082` |
| `"%f ms for Gather Unreachable Objects (%d objects collected / %d scanned with %d thread(s))"` | `:5425` |
| `"%f ms for dissolving GC clusters"` | `:5861` |
| `"%f ms for %lsunhashing unreachable objects (%d objects unhashed)"` | `:6341` |
| `"GC purged %i objects (%i -> %i) in %.3fms (%d iteration(s))"` | `:5234` |
| `gc.DumpAnalyticsToLog 1` dumps `FStats` (Total, Verify, Reachability, MarkAsUnreachable, Reference Collection, GatherUnreachable, Unhashing, DestroyGarbage, object/cluster/barrier counts) at the end of each GC (non-Shipping) | cvar `GarbageCollection.cpp:373–381`; `FStats::DumpToLog` `CoreUObject/Private/UObject/ReachabilityAnalysisState.cpp:83–107` |

`FStats` itself (`CoreUObject/Public/UObject/ReachabilityAnalysis.h:80–122`) is reached only through `UE::GC::Private::GetGarbageCollectionStats()` (`GarbageCollection.cpp:6217`), which no public header exports, so a plugin can't read it.

### B1.5 Public C++ accessors and delegates (usable from a plugin)

| API | Meaning | Evidence |
|---|---|---|
| `double GetLastGCDuration()` | Wall time of the last **blocking** `CollectGarbageInternal` call (Pre + first reachability pass + Post). Not updated by later incremental slices | decl. `GarbageCollection.h:199`; set `GarbageCollection.cpp:5672` |
| `double GetLastGCTime()` | Timestamp at the end of `PostCollectGarbageImpl` | `GarbageCollection.h:192`; `GarbageCollection.cpp:5947` |
| `FCoreUObjectDelegates::GetPreGarbageCollectDelegate()`, `GetGarbageCollectStartedDelegate()`, `PostReachabilityAnalysis`, `GetPostGarbageCollect()`, `GetPostPurgeGarbageDelegate()`, `GarbageCollectComplete` | Fired at cycle start, after reachability (last slice), after Post, after purge, at cycle completion | decl. `CoreUObject/Public/UObject/UObjectGlobals.h:3478–3508`; broadcasts `GarbageCollection.cpp:5741, 5762, 5905, 5943, 4854, 4871 / 5954` |
| `IsIncrementalReachabilityAnalysisPending()`, `IsIncrementalPurgePending()`, `IsIncrementalUnhashPending()` | Is GC work still owed for the current cycle? | `GarbageCollection.cpp:6180`; `UObjectGlobals.h:979, 986` |
| `GUObjectArray.GetObjectArrayNumMinusAvailable()` | Live UObject count (the same value the 5.8 trace counter shows) | `CoreUObject/Public/UObject/UObjectArray.h:1266–1269` |
| `FWorldDelegates::OnWorldPostActorTick` / `OnWorldTickEnd` | Fired after the tick groups and at the very end of `UWorld::Tick`; `ConditionalCollectGarbage` runs between them | `LevelTick.cpp:1906, 2061` (call at :1970) |

## B2. Memory tools

| Tool | Evidence | Use here |
|---|---|---|
| CSV `PhysicalUsedMB` (working set) | B1.1 | Resident MB per frame, every run |
| LLM: compiled into Development (`LLM_ENABLED_IN_CONFIG` = not Shipping, and not Test unless allowed) | `Core/Public/HAL/LowLevelMemTrackerDefines.h:25–30` | Off by default; `-LLM` turns it on, `-LLMCSV` writes its own CSV (`Core/Private/HAL/LLM/LLM.cpp:2318, 2324`). It adds tracking overhead, so it is used only in separate memory-attribution runs **[inferred]** |
| LLM tags `UObject` and `GC` exist; projects can declare their own with `LLM_DEFINE_TAG` / `LLM_SCOPE_BYTAG` | `Core/Public/HAL/LowLevelMemTracker.h:233, 241, 437, 384` | A `GCBudget` tag for the item stores, Phase 5 |
| `obj list [class=…] [-countsort]` | `Engine/Private/UnrealEngine.cpp:9607, 9879` | Object counts and sizes by class, for a one-off check |
| Memory Insights: `-trace=default,memory` (+ `memtag`) | trace channels (B1.2) | Optional allocation view in Phase 8 |

## B3. The measurement chosen (carried to 01b)

| Need | Method | Why this one |
|---|---|---|
| **Per-frame GC game-thread time** (hitches) | Our telemetry timestamps `FWorldDelegates::OnWorldPostActorTick` and `OnWorldTickEnd` for the game world, every frame. The bracket = GC + a small, steady remainder (async-trace finish, net flush with no net driver, FX tick with no effects, CSV world counts). A frame is a **GC frame** if a cycle started in it (`PreGarbageCollect` fired) or GC work was pending at the bracket start. Offline: `GC ms = bracket − median(bracket of non-GC frames)` per run | `ConditionalCollectGarbage` is not virtual (A1), so it can't be wrapped. The engine's CSV `GarbageCollection` column misses Pre/Post (B1.1). The two delegates bracket the call with nothing variable in between in this workload **[inferred]**; Phase 5 validates the bracket against the Insights `ConditionalCollectGarbage` timer |
| **Per-cycle blocking pause** | `GetLastGCDuration()` read in `GetPostGarbageCollect()` | Engine-measured, complete for the blocking call |
| **Per-cycle phase breakdown** | `-LogCmds="LogGarbage Log"` in a separate diagnostic batch | The log lines cost game-thread time inside GC, so they stay out of headline runs |
| **UObject count** | `GUObjectArray.GetObjectArrayNumMinusAvailable()` as our own CSV stat each frame; cross-checked once with the 5.8 trace counter (`-trace=default,counters`) | The CSV `ObjectCount` category is compiled out (B1.1) |
| **Resident MB** | CSV `PhysicalUsedMB`; delta against an N = 0 run | Engine-provided, every frame |
| **Engine cross-checks in every CSV** | `GC/Count`, CSV event `GC`, `Exclusive/GameThread/GarbageCollection` | Free, and they make our own numbers auditable |
| **Cadence** | `gc.TimeBetweenPurgingPendingKillObjects` set by the director; fixed step | Deterministic frame count between cycles (A1) |
| **Development build fairness** | `-NoVerifyGC` on every run | Verification is on by default in Development and hits clusters hardest (A8; D2) |
