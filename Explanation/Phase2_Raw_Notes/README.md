# Phase 2 raw notes

These are the working notes behind [01b_Research_Findings.md](../01b_Research_Findings.md). They are kept so every statement in the findings can be traced back to the exact engine line or web page it came from.

| File | Scope |
|---|---|
| [A_gc_internals.md](A_gc_internals.md) | Who starts a GC, the phases of one cycle, reachability (full, parallel, incremental), the `TObjectPtr` write barrier, clusters, the disregard-for-GC pool, Development vs Shipping vs editor, the hybrid CPU |
| [B_measurement.md](B_measurement.md) | CSV stats, trace timers and the 5.8 "UObject Count" counter, stats, `LogGarbage` lines, public accessors and delegates, memory tools, and the measurement method chosen |
| [C_data_layout.md](C_data_layout.md) | What a UObject costs per object, `TObjectPtr` in packaged vs editor builds, generational handles the engine already uses, the runtime cluster API |
| [D_existing_work.md](D_existing_work.md) | Epic docs, Epic knowledge base and forum threads, Tom Looman's 5.8 notes, community advice, and the re-checked gap |

**The findings file is authoritative.** These notes are the audit trail. Engine paths are relative to `C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\` (UE 5.8.4, CL 58902052) unless stated otherwise.
