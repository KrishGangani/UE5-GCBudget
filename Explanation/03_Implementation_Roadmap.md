# 03 · Implementation Roadmap: GCBudget

**Written:** end of Phase 3, 2026-11-21 · **Revised:** 2026-11-21 at the start of Phase 5 (step order; see "Changes" at the end) · **Engine:** UE 5.8.4

**Rules for every step:**
- Each step is small enough to read in one sitting: a few hundred lines at most.
- A step is finished only when its code **compiles with 0 warnings through the UE job queue**, its check **passes** (its automation tests included), and its `Explanation/` entries (`05`, `06`, `04`, `07` where relevant; `02` if the design changed) are **written in the same step and the same commit**.
- After each step that adds code: a review for correctness, then a clean-up pass (reuse, simplification, efficiency, altitude). The findings are listed in `09`'s audit trail.
- **The baselines are captured before the proposed store exists.** U0, U1, U2 and S1 are measured (S6) before S0's store is written (S7). The handle allocator comes earlier (S3) because S1 uses it too: S1 and S0 then differ only in the reference each element holds.

---

**Progress (2026-11-21):** Phases 1–3 ✅. Phase 4 (F1–F3) ✅ (`09`: 0 errors, 0 warnings; packaged smoke run on the RTX GPU). Phase 5: S1 ✅ (`09`: 0 warnings; the E0 cadence and counters checked; the bracket validation runs in its own queue job). S2 next.

## Phase 4: Foundation (no gameplay code yet)

| Step | What gets built | How it is checked |
|---|---|---|
| **F1** | `GCBudget\GCBudget.uproject` (EngineAssociation 5.8; the project plugin on, the engine's Android File Server plugin off), `Source\GCBudget.Target.cs`, `GCBudgetEditor.Target.cs`, the game module (`GCBudget.Build.cs`, `GCBudgetModule.cpp`) | `Tools\build.bat GCBudgetEditor Development` and `Tools\build.bat GCBudget Development` succeed through the queue with **0 errors, 0 warnings** |
| **F2** | The plugin skeleton: `Plugins\GCBudgetCore\GCBudgetCore.uplugin`, `GCBudgetCore.Build.cs`, `GCBudgetCoreModule.cpp` | Builds in F1; the smoke-run log shows the plugin mounted and both modules loaded |
| **F3** | `Config\DefaultEngine.ini` (default map, frame-timing keys, the GC defaults restated; no engine-written sections), `DefaultGame.ini`, `DefaultInput.ini` (the Enhanced Input classes); `Tools\build.bat`, `package.bat`, `smoke_game.bat`, `cpu_affinity.py` | Uncooked smoke run exits 0 with no warnings from project code; `Tools\package.bat` (Development) succeeds with 0 errors and 0 warnings; one packaged smoke run on the real GPU exits 0 and logs the RTX adapter |

## Phase 5: Implementation

| Step | What gets built | How it is checked | Explanation files touched |
|---|---|---|---|
| **S1. Telemetry + director skeleton (E0 only)** | `UGCBudgetTelemetrySubsystem` (bracket, GC-frame flag, cycle pause, UObject count, CSV category `GCBudget`, `gcb.Telemetry`); `UGCBBenchmarkSubsystem` without items: command-line values, GC cvars from code, the 120-frame `ForceGarbageCollection(false)` trigger, `BenchStart`/`BenchEnd`, CSV metadata, run report, exit | Packaged E0 runs, fixed step: `CycleStarts` = 1 every 120 frames and matches the engine's `GC/Count`; `CyclePauseMs` > 0 on those frames; `gc.VerifyAssumptions` reads 0 with `-NoVerifyGC`. **Bracket validation:** one traced run (`-trace=default,counters`) whose bracket minus floor is compared with the engine's `ConditionalCollectGarbage` timer (`Tools\validate_bracket.py`); the difference is reported | 04, 05, 06, 07, 09 |
| **S2. Measurement tools** | `Tools\csv_stats.py` (adapted from the reference project, plus the GC metrics), `Tools\run_bench.py` (adapted: pins itself to the P-cores with `pin_self` before anything else, launches the real exe directly, waits with a timeout, kills the tree in `finally`, records `applied_mask()`, interleaves configs with rotated order, writes the manifest and summary), `Tools\validate_bracket.py` | `csv_stats.py --selftest` against hand-computed values; one E0 batch of 3 runs end to end | 05, 09 |
| **S3. Handles + allocator, with unit tests** | `FGCBudgetHandle`, `FGCBudgetSlotAllocator` (plugin); `Tools\run_tests.bat` | Automation tests (no world): allocate/resolve; a stale handle after release; LIFO reuse gives a new generation; a default handle is invalid; reset; 1,000,000 random operations against a reference model with 0 mismatches | 04, 05, 06, 09 |
| **S4. Workload + UObject stores (U0, U1, U2)** | `UGCBItemDef`, the integer workload (`GCBWorkload`), `UGCBItemObject`, `UGCBItemPage`, `UGCBItemStore`, `UGCBObjectItemStore` (with U1's paged clusters, `PrepareForGC`, `ClusterMs`); the `TWeakObjectPtr` probe; the checksum; the director's U0/U1/U2 configurations | Determinism: the same seed gives the same checksum twice, a different seed a different one, and U0 = U1 = U2. UObject count = E0 + N + pages + store. Expired items are collected (flat UObject count). U1 builds one cluster per page, and a `-VERIFYGC` diagnostic run reports no cluster-assumption errors. Probe errors 0 | 04, 05, 06, 07, 09 |
| **S5. Struct store with references (S1)** | `FGCBItemDataRef`, `UGCBStructRefItemStore`, using S3's handles | The S1 checksum equals U0's for the same seed; UObject count = E0 + definitions + store; probe errors 0 | 05, 06, 07, 09 |
| **S6. Baseline capture** | No new code | **Batch A without S0** (U0, U1, U2, S1 × N {10k, 100k}, 4 runs each, interleaved) and **Batch B without S0** (U0, S1 × N {0, 1k, 10k, 30k, 100k}, 2 runs each). H1, H2, H4, H5 and H6 get their first data **before S0 exists** | 09 |
| **S7. The proposed store (S0)** | `FGCBItemData`, `UGCBStructItemStore` (definition by index; shares the struct-store implementation with S1) | The S0 checksum equals U0's; UObject count = E0 + definitions + store; probe errors 0; one short S0 vs U0 sanity batch | 05, 06, 07, 09 |

**Why S6 uses fewer runs than Phase 8's batches:** the protocol sets runs per combination to the number of configs in the batch. S6's batches have 4 and 2 configs; Phase 8's have 5 and 3.

## Phases 6–10 (after the code exists)

| Phase | Work |
|---|---|
| 6 Explanation | Consolidate 04–07 into a single, consistent line-by-line pass. Re-check every API and line citation against 5.8.4 |
| 7 Testing | Edge cases: N = 0 and 1; every item expiring in one frame; a handle released twice; level travel with live stores; `gc.CreateGCClusters 0` with U1 (must fall back cleanly). A 30-minute soak per config with a flat UObject count |
| 8 Profiling | **Batch A** (U0, U1, U2, S1, S0 × N {10k, 100k} × 5 runs) and **Batch B** (U0, S1, S0 × N {0, 1k, 10k, 30k, 100k} × 3 runs), each with its own priming run: the results SC1–SC8 are judged on. **Batch C** diagnostics: the `LogGarbage` phase breakdown per config at 100k; verification on vs off (U0, U1); the default 61.1 s cadence for one U0/S0 pair; an Insights trace per config; LLM attribution (`-LLM -LLMCSV`) for U0 vs S0. The **Shipping cross-check** (owner's decision, 2026-11-21): package Shipping, which removes Development's GC pointer checks, and measure with the game's own file output (Shipping has no CSV profiler). Development stays the headline configuration; if Shipping packaging fails, the reason is recorded. Optimisation only with data (`11_Optimization_Decisions.md`) |
| 9 Code review | A written self-review of memory safety, GC visibility and handle correctness: weaknesses, not praise |
| 10 Portfolio | Charts, the architecture page, the optional demo overlay (its cost measured, on vs off), the demo video, README results, the migration guide, interview Q&A |

**Rough matrix cost (per run: about 15 s load, 2–5 s populate, 10 s warm-up, 60 s capture ≈ 90 s):**
- Batch A: 50 runs + priming ≈ 77 min.
- Batch B: 45 runs + priming ≈ 70 min.
- S6: 32 + 20 runs ≈ 80 min.

All are scripted, unattended, and run through the queue.

## Changes to this roadmap

| Date | Change | Why |
|---|---|---|
| 2026-11-21 (start of Phase 5) | The handles moved from S5 to **S3**; the three UObject configurations became one step, **S4** (were S3 and S4); S1's store became **S5**, the baseline capture **S6**, S0 **S7** | One machine serves ten projects, and every build waits in a shared queue (often 1–2 hours). S3 (plugin only) and S4 (game module only) are built and checked in **one** queue job but committed separately. The two modules compile independently, so each commit's state was compiled exactly as committed |
| 2026-11-21 | The headline and curve batches (were S9) and the diagnostics batch (were S10) moved to **Phase 8**; the optional overlay (was S11) to **Phase 10** | Phase 5 is implementation; measuring the finished system is Phase 8's job, as in the portfolio's method |
| 2026-11-21 | The Shipping cross-check is in Phase 8's plan | The owner's decision, relayed by the coordinator |
