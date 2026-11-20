# 08 · Debugging And Profiling: GCBudget

**Status:** not written yet. Filled in **Phases 7–8**, once the telemetry and stores exist.

**Will contain:** how to see GC working in UE 5.8.4, with exact commands:
- this project's CSV stats (`GCBudget/*`, `GCBWorkload/*`) and the engine's (`GC/Count`, the CSV event `GC`, `Exclusive/GameThread/GarbageCollection`, `PhysicalUsedMB`);
- Unreal Insights with `-trace=default,counters`: the `ConditionalCollectGarbage` timer, the `GarbageCollection` region, the "UObject Count" counter;
- `-LogCmds="LogGarbage Log"` and how to read each line;
- `obj list -countsort`, `gc.ListClusters`, `gc.DumpAnalyticsToLog`, `-LLM -LLMCSV`.

The verified sources for all of these are already in [01b_Research_Findings.md](01b_Research_Findings.md) §4 and [Phase2_Raw_Notes/B_measurement.md](Phase2_Raw_Notes/B_measurement.md).
