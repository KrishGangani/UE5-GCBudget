# 03 · Implementation Roadmap: GCBudget

**Written:** end of Phase 3, 2026-11-21 · **Engine:** UE 5.8.4

**Rules for every step:**
- Each step is small enough to read in one sitting: a few hundred lines at most.
- A step is finished only when its code **compiles with 0 warnings through the UE job queue**, its check **passes**, and its `Explanation/` entries (`05`, `06`, and `07` where relevant; `02` if the design changed) are **written in the same step and the same commit**.
- After each step that adds C++: a written self-review (weaknesses, not praise) and a clean-up pass.
- **The baselines are captured before the proposed store exists.** U0, U1, U2 and S1 are measured (step S7) before S0's store is written (step S8). The handle allocator comes earlier (S5) because S1 uses it too: S1 and S0 then differ only in the reference each element holds.

---

**Progress (2026-11-21):** Phases 1–3 ✅. Phase 4 (F1–F3) ✅ (`09`: 0 errors, 0 warnings; packaged smoke run on the RTX GPU). Next: **S1**.

## Phase 4: Foundation (no gameplay code yet)

| Step | What gets built | How it is checked |
|---|---|---|
| **F1** | `GCBudget\GCBudget.uproject` (EngineAssociation 5.8; the project plugin on, the engine's Android File Server plugin off), `Source\GCBudget.Target.cs`, `GCBudgetEditor.Target.cs`, the game module (`GCBudget.Build.cs`, `GCBudgetModule.cpp`) | `Tools\build.bat GCBudgetEditor Development` and `Tools\build.bat GCBudget Development` succeed through the queue with **0 errors, 0 warnings** |
| **F2** | The plugin skeleton: `Plugins\GCBudgetCore\GCBudgetCore.uplugin`, `GCBudgetCore.Build.cs`, `GCBudgetCoreModule.cpp` | Builds in F1; the smoke-run log shows the plugin mounted and both modules loaded |
| **F3** | `Config\DefaultEngine.ini` (default map, frame-timing keys, the GC defaults restated; no engine-written sections), `DefaultGame.ini`, `DefaultInput.ini` (the Enhanced Input classes); `Tools\build.bat`, `package.bat`, `smoke_game.bat`, `cpu_affinity.py` | Uncooked smoke run exits 0 with no warnings from project code; `Tools\package.bat` (Development) succeeds with 0 errors and 0 warnings; one packaged smoke run on the real GPU exits 0 and logs the RTX adapter |

## Phase 5: Implementation

| Step | What gets built | How it is checked | Explanation files touched |
|---|---|---|---|
| **S1. Telemetry + director skeleton (E0 only)** | `UGCBudgetTelemetrySubsystem` (bracket, GC-frame flag, cycle pause, UObject count, CSV category `GCBudget`, `gcb.Telemetry`); `UGCBBenchmarkSubsystem` without items: command-line values, GC cvars from code, the 120-frame `ForceGarbageCollection(false)` trigger, `BenchStart`/`BenchEnd`, CSV metadata, run report, exit | Packaged E0 run, fixed step: `CycleStarts` = 1 every 120 frames and matches the engine's `GC/Count`; `CyclePauseMs` > 0 on those frames; no "Verify GC Assumptions" work with `-NoVerifyGC`. **Bracket validation:** one Insights trace (`-trace=default,counters`) where our bracket minus floor is compared with the engine's `ConditionalCollectGarbage` timer frame by frame; the difference is reported | 04, 05, 06, 07, 09 |
| **S2. Measurement tools** | `Tools\csv_stats.py` (adapted from the reference project: per-run median/p95/p99 with linear interpolation; per-cycle and per-GC-frame statistics; the bracket floor; spike and hitch counts), `Tools\run_bench.py` (adapted: pins itself to the P-cores with `pin_self` before anything else, launches the real exe directly, waits with a timeout, kills the tree in `finally`, records `applied_mask()`, interleaves configs with rotated order, writes the manifest and summary) | `csv_stats.py --selftest` against hand-computed values; one E0 batch of 3 runs end to end | 05, 08, 09 |
| **S3. Workload + UObject stores (U0, U2)** | `UGCBItemDef`, `UGCBItemObject`, `UGCBItemPage`, `UGCBItemStore`, `UGCBObjectItemStore`; per-lane hashed item parameters; churn with `MarkAsGarbage`; the `TWeakObjectPtr` probe; checksum; CSV category `GCBWorkload` | Determinism: the same seed gives the same checksum twice, a different seed a different one. UObject count = E0 + N + pages + definitions. Each cycle purges about 2% of N. U2 logs incremental slices (`-LogCmds="LogGarbage Log"`, diagnostic) | 04, 05, 06, 07, 09 |
| **S4. Clustering (U1)** | `bClusterPages`: pages become cluster roots (`CreateCluster`); `PrepareForGC` dissolves and rebuilds changed pages; `ClusterMs` | `gc.ListClusters` shows one cluster per page at N = 100k; the UObject count stays flat (expired items are collected); a `-VERIFYGC` diagnostic run reports no cluster-assumption errors; the U1 checksum equals U0's | 05, 06, 07, 09 |
| **S5. Handles + allocator, with unit tests** | `FGCBudgetHandle`, `FGCBudgetSlotAllocator` (plugin) | Automation tests (no world): allocate/release/resolve; a stale handle after release; LIFO reuse gives a new generation; a default handle is invalid; reset; 1 M random operations against a reference model with 0 mismatches | 04, 05, 06, 09 |
| **S6. Struct store with references (S1)** | `FGCBItemDataRef`, `UGCBStructRefItemStore`, using S5 | The S1 checksum equals U0's for the same seed; UObject count = E0 + definitions + store; GC still visits every element (the reference count rises in the `LogGarbage` diagnostic line) | 05, 06, 07, 09 |
| **S7. Baseline capture** | No new code | **Batch A without S0** (U0, U1, U2, S1 × N {10k, 100k}, 4 runs each, interleaved) and **Batch B without S0** (U0, S1 × N {0, 1k, 10k, 30k, 100k}, 2 runs each). H1, H2, H4, H5 and H6 get their first data **before S0 exists** | 09 |
| **S8. The proposed store (S0)** | `FGCBItemData`, `UGCBStructItemStore` (definition by index; shares the struct-store template with S1) | The S0 checksum equals U0's; UObject count = E0 + definitions + store; probe errors 0 | 05, 06, 07, 09 |
| **S9. Headline and curve batches** | No new code | Full **Batch A** (5 configs × 2 N × 5 runs) and **Batch B** (3 configs × 5 N × 3 runs), each with its own priming run. These are the results SC1–SC8 are judged on; the S7 baseline runs are kept as history, not mixed in | 09, 11 |
| **S10. Diagnostics batch (C)** | No new code | The `LogGarbage` phase breakdown per config at 100k; verification on vs off (U0, U1); the default 61.1 s cadence for one U0/S0 pair; an Insights trace per config; LLM attribution (`-LLM -LLMCSV`) for U0 vs S0 | 08, 09 |
| **S11. (Optional) demo overlay + Blueprint library** | On-screen config, counts and GC timeline; `UGCBudgetHandleLibrary` | Overlay cost measured (on vs off) and reported, not assumed | 05, 06, 07 |

**Why S7 uses fewer runs than S9:** the protocol sets runs per combination to the number of configs in the batch. S7's batches have 4 and 2 configs; S9's have 5 and 3.

## Phases 6–10 (after the code exists)

| Phase | Work |
|---|---|
| 6 Explanation | Consolidate 04–07 into a single, consistent line-by-line pass. Re-check every API and line citation against 5.8.4 |
| 7 Testing | Edge cases: N = 0 and 1; every item expiring in one frame; a handle released twice; level travel with live stores; `gc.CreateGCClusters 0` with U1 (must fall back cleanly). A 30-minute soak per config with a flat UObject count |
| 8 Profiling | Batches A and B as the headline; Batch C diagnostics; an optional Shipping cross-check with our own file output; optimisation only with data (`11_Optimization_Decisions.md`) |
| 9 Code review | A written self-review of memory safety, GC visibility and handle correctness: weaknesses, not praise |
| 10 Portfolio | Charts, the architecture page, the demo video, README results, the migration guide, interview Q&A |

**Rough matrix cost (per run: about 15 s load, 2–5 s populate, 10 s warm-up, 60 s capture ≈ 90 s):**
- Batch A: 50 runs + priming ≈ 77 min.
- Batch B: 45 runs + priming ≈ 70 min.
- S7: 32 + 20 runs ≈ 80 min.

All are scripted, unattended, and run through the queue.
