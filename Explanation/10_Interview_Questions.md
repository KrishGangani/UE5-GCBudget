# 10 · Interview Questions: GCBudget

Answers come from this project's code, the UE 5.8.4 source and its recorded measurements (`09`).

1. **What does Unreal's GC cost scale with?** Reachable objects and the references it follows. Gather also scans the whole object array. Here U0's cycle-pause p95 grows from about 1.0 ms at 1k items to about 4.0 ms at 100k (`09` S6, Batch B).
2. **Objects or references: which matter more?** Both, but objects more in this workload. S1 keeps one `TObjectPtr` per element yet has almost no UObjects, and its pause reaches only about 1.9 ms at 100k against U0's 4.0 ms. A struct array with no references isn't in the GC schema at all (`GarbageCollection.cpp:6869–6876`).
3. **Why not just use GC clusters?** A cluster is marked reachable as a unit, but the root's own references are never traced (`GarbageCollection.cpp:3130–3139`). With churn, every new object must join the cluster and every changed cluster must be rebuilt. At 100k that rebuild stalled the game about 1.4 s per cycle (`09` S6).
4. **What bug did the review find in U1?** Replacement items weren't in their page's cluster, so any GC that didn't follow a rebuild would free live items while the page still pointed at them. Fix: `AddToCluster` on creation (`05` C5).
5. **Does incremental reachability help?** Not here: U2 at 2 ms slices didn't lower the pause at 100k (4.86 against 4.46 ms p95). Why is unverified (`09`).
6. **How do you reference items without UObjects?** With generational handles: an index plus a generation. `Resolve` checks the slot is alive and the generation matches, so a reused slot never answers for an old handle (`05` B3). The documented limit is a wrap after 2³¹ − 1 releases of one slot.
7. **How did you make five memory layouts provably do the same work?** Integer ticks and Q16.16 fixed point, per-lane hashed parameters, and a CRC over all items in lane order. The checksums are identical across configurations (`09` S4, S7).
8. **How do you measure GC per frame without engine changes?** Bracket the engine's GC call between `OnWorldPostActorTick` and `OnWorldTickEnd` and subtract the non-GC floor. It matched Insights' `ConditionalCollectGarbage` within 0.008 ms per cycle (`09` S2-6).
9. **What measurement traps did you hit?** `-traceautostart=0` also blocks a requested trace. A CSV row's `FrameTime` is the previous frame's duration (`CsvProfiler.cpp:4369–4390`). Development verifies GC assumptions unless you pass `-NoVerifyGC`. And a percentile of a sparse column is 0 (`07`).
10. **What would you ship?** S0 for high-count, high-churn items with a shared, fixed definition table, handles for references, and UObjects only for the definitions. What I'd avoid: clustering a store whose membership changes every cycle.
