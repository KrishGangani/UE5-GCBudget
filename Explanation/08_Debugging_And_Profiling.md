# 08 · Debugging And Profiling: GCBudget

**Rule:** every command, switch and console variable here was checked in the UE 5.8.4 source, and cites where it is parsed or defined (relative to `Engine/Source/` unless stated). The project's own instruments come first, then the engine's. **Current to:** the end of Phase 5 (written in Phase 6). Phase 2's measurement notes are in [Phase2_Raw_Notes/B_measurement.md](Phase2_Raw_Notes/B_measurement.md).

---

## 1. The project's own instruments

| Instrument | What it shows | Explained in |
|---|---|---|
| **Telemetry CSV stats** (category `GCBudget`) | Per frame: `BracketMs` (the time around the engine's GC call), `GCFrame`, `CycleStarts`, `CyclePauseMs`, `UObjects`. Switch them off with `gcb.Telemetry 0` | `05` B1–B2; `02` §4.1 |
| **Workload CSV stats** (category `GCBWorkload`) | Per frame: `Items`, `Expired`, `UpdateMs`, `UpdateNsPerItem`, `ProbeNsPerLookup`, `ProbeErrors`, `GCTrigger` (the request frame), `ClusterMs` (U1's rebuild); the events `BenchStart` and `BenchEnd` | `05` C8 |
| **The director's log** (`LogGCBBench`) | Every GC variable it set or recorded, with the old value; the run description; "Populated N items in X ms"; both struct element sizes; `BenchStart` and `BenchEnd` with their frames; the report path with its checksum and probe errors. **A rejected run logs an `Error` saying why** (unknown configuration, items out of range, U1 without the director's cadence…) and stays idle | `05` C8 |
| **The run report** (`Saved\GCBRuns\<tag>.json`) | Settings, frame counters, GC requests against the cycles seen inside and outside the bracket, cluster rebuilds, expiries, probe totals, the checksum | `05` C8 |
| `Tools\check_batch.py <batch>` | Offline checks of a recorded batch: cycle cadence, the engine's cycle count, pauses only on cycle frames, the report's counters, the GC settings in the log, the UObject range, `ClusterMs` and GameThreadTime on request frames. Exit 1 if anything fails | `05` D5 |
| `Tools\validate_bracket.py --label <label>` | The bracket against Insights' own GC timer, frame by frame, with both alignments checked (`--reuse-export` reruns the comparison without Insights) | `05` D6; `09` S2-6 |
| `Tools\csv_stats.py --selftest` | The statistics against hand-worked values, the hitch rule included | `05` D3 |

## 2. The CSV profiler

| Item | Use here | Source |
|---|---|---|
| `-csvStartOnEvent=BenchStart`, `-csvStopOnEvent=BenchEnd` | Start and stop a capture on a named `CSV_EVENT`, whatever its category | `Runtime/Core/Private/ProfilingDebugging/CsvProfiler.cpp:5081, 5090` |
| Output folder | `<Saved>\Profiling\CSV\`; the runner moves each file to `Benchmarks\Raw` | `CsvProfiler.cpp:4741` |
| The `FrameTime` column | **The previous frame's duration**: measured at the start of a frame, after the frame-rate wait (unless `csv.UseLegacyFrameTime 1`). Attribute work per frame with GameThreadTime, or read the next row | `CsvProfiler.cpp:150–155, 4369–4390`; `09` S6 |
| The engine's GC columns | `GC/Count` (a cross-check of our cycle count); `Exclusive/GameThread/GarbageCollection` (partial: Pre, gather, cluster dissolving and Post fall outside it, `07` §2) | `Runtime/CoreUObject/Private/UObject/GarbageCollection.cpp:80, 5674` |
| The CSV event `GC` | Fired by every reachability pass: the first one and each incremental slice (U2) | `GarbageCollection.cpp:5650, 5660` |

## 3. Unreal Insights

| Item | Use here | Source |
|---|---|---|
| `-trace=default,counters -tracefile="<path>.utrace"` | Writes a trace to a file; `counters` is needed for the 5.8 "UObject Count" counter. **Never together with `-traceautostart=0`**, which blocks a requested trace | `Runtime/Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:1952, 2000, 2017–2021`; the counters channel `:138` |
| `Trace.Start`, `Trace.File`, `Trace.Stop` (console) | Start, start to a file and stop tracing at run time. `Trace.Stop` before quitting is the first thing Phase 7 tries against exit 777003 | `TraceAuxiliary.cpp:1837, 1846, 1856` |
| The `ConditionalCollectGarbage` timer | Wraps the engine's GC call exactly, once per frame: the trace-side twin of our bracket | `Runtime/Engine/Private/LevelTick.cpp:1969` |
| The region `GarbageCollection` | One span per cycle, from its start to the end of its purge, across frames | `GarbageCollection.cpp:5716, 4876, 5959` |
| The "UObject Count" counter | The live object count, from the world tick | `LevelTick.cpp:105, 1973` |
| A headless export | `UnrealInsights.exe -OpenTraceFile=<trace> -AutoQuit -NoUI -ExecOnAnalysisCompleteCmd="@=<file>"`, the file holding `TimingInsights.ExportTimingEvents "<csv>" -columns=… -threads=GameThread -timers=ConditionalCollectGarbage`. **Forward slashes in the path**: the command reads it with escapes on | `Developer/TraceInsights/Private/Insights/TimingProfiler/TimingProfilerManager.cpp:802, 817–818`; `…/Insights/InsightsManager.cpp:1089–1100` |

## 4. GC console variables, commands and logs

| Name | Kind | Use here | Source |
|---|---|---|---|
| `-NoVerifyGC` / `-VERIFYGC` | switches | Set `gc.VerifyAssumptions` from the command line: off for every timed run, on for the cluster check (`run_bench.py --verify-gc`) | `Runtime/CoreUObject/Private/UObject/Obj.cpp:5915–5944` |
| `gc.TimeBetweenPurgingPendingKillObjects` | cvar | The engine's GC timer; the director parks it at 3600 s and triggers cycles itself. It must stay > 0 | `05` C8; `Runtime/Engine/Private/UnrealEngine.cpp:2284` |
| `gc.AllowIncrementalReachability`, `gc.IncrementalReachabilityTimeLimit` | cvars | U2's incremental reachability and its slice | `GarbageCollection.cpp:304, 312` |
| `gc.AllowParallelGC` | cvar | Recorded in every capture | `GarbageCollection.cpp:4720` |
| `gc.CreateGCClusters`, `gc.MinGCClusterSize` | cvars | Clustering on or off (U1 refuses to cluster when off) and the smallest cluster kept | `Runtime/CoreUObject/Private/UObject/UObjectClusters.cpp:28, 45`; `Config/BaseEngine.ini:1752–1753` |
| `gc.ListClusters [Hierarchy]` | command | Dumps every cluster, and with `Hierarchy` every member: how to see U1's pages as clusters | `UObjectClusters.cpp:569` |
| `gc.FindStaleClusters`, `gc.DumpRefsToCluster Root=<name>` | commands | Clusters nothing references; everything that references a cluster's objects | `UObjectClusters.cpp:575, 581` |
| `gc.DumpAnalyticsToLog 1` | cvar | At the end of each GC, the engine's own phase totals (reachability, gather, unhash, destroy; object and cluster counts) | `GarbageCollection.cpp:377`; `Runtime/CoreUObject/Private/UObject/ReachabilityAnalysisState.cpp:83–107` |
| `gc.CollectGarbageEveryFrame` | cvar | A GC every frame: a stress test for references the GC can't see. Never in a timed run | `UnrealEngine.cpp:1767` |
| `obj gc` | command | A collection now | `UnrealEngine.cpp:9340` |
| `obj list class=GCBItemObject [-countsort]` | command | Counts and memory of the live objects of a class: U0's items, pages and definitions | `UnrealEngine.cpp:9607, 9879` |
| `stat GC` | command | The engine's GC stat group | `Runtime/CoreUObject/Public/UObject/GarbageCollection.h:38` |
| `-LogCmds="LogGarbage Log"` | switch | `LogGarbage` defaults to Warning. At Log level each cycle prints "Collecting garbage", "… ms for Verify GC Assumptions", "GC Reachability Analysis total time …", "… ms for Gather Unreachable Objects …", "… ms for dissolving GC clusters", "… ms for unhashing unreachable objects …" and "GC purged …". `Verbose` adds the mark phase and each incremental purge slice. The lines cost game-thread time inside GC, so only diagnostic batches use them (Phase 8's drill-downs) | `GarbageCollection.h:37`; `Runtime/Core/Private/Logging/LogSuppressionInterface.cpp:519–540`; `GarbageCollection.cpp:5780, 5817, 6077, 5425, 5861, 6341, 5234, 4568, 5225` |
| `-LLM`, `-LLMCSV` | switches | Low-level memory tracking (tags `UObject`, `GC`), with its own CSV | `Runtime/Core/Private/HAL/LLM/LLM.cpp:2318, 2324` |

## 5. Crash reporting, for exit 777003

| Switch | What it does in 5.8.4 | Source |
|---|---|---|
| `-crashreports` | Sets `GAlwaysReportCrash` | `Runtime/Launch/Private/Windows/LaunchWindows.cpp:216` |
| `-nocrashreports` | Skips submitting **ensure** reports only. It doesn't stop the crash-reporting thread, the one that exits with 777003, so it can't test that cause | `Runtime/Core/Private/Misc/AssertionMacros.cpp:607, 666`; `Runtime/Core/Private/Windows/WindowsPlatformCrashContext.cpp:1300–1345` |
| `-unattended` | No error dialogs | `LaunchWindows.cpp:211–214` |

## 6. Recipes for this project

| Symptom | First step | Then |
|---|---|---|
| A run has no report | The log's `LogGCBBench: Error` line says which setting was rejected | Fix the command line; nothing was measured |
| Checksums differ between configurations | `check_batch.py` on the batch: are the frame counts equal? | The same frames but a different checksum is a store bug: run U0 and the suspect store with the same seed and compare the reports' expiry counts |
| A crash or garbage values in U1 | A `-VERIFYGC` run (`run_bench.py --verify-gc --configs U1`) | `gc.ListClusters Hierarchy` in an editor session: is every live item a member of its page's cluster? (`07` §5) |
| Probe errors > 0 | The report's `probe_errors`, with `probe_stale` and `probe_resolved` | A resolved reference to the wrong item is a handle or weak-pointer bug (`05` B3, C5) |
| A hitch that isn't in the GC bracket | GameThreadTime and `ClusterMs` in the **same** row | U1's rebuild runs in the director's tick, outside the bracket (`09` S6) |
| The bracket and Insights disagree | `validate_bracket.py --label <traced batch>` | Check the alignment flags first, then the "other frames" overhead against the floor |
| Exit 777003 on a traced run | The log: did it reach "Log file closed"? Is there a `Saved\Crashes` folder? | Phase 7's investigation (`09`) |
