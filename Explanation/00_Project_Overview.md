# 00 · Project Overview: GCBudget

> *"This was the problem. This was my investigation. This was my solution. These were my technical decisions. This was how I measured the results."*

**Status (2026-11-21):**
- Phases 1–3 are done: the problem in `01`, verified research in `01b`, the final architecture v1.0 in `02`.
- Phase 4 (foundation) is done: the UE project, the plugin skeleton and the build scripts. Both targets build with 0 errors and 0 warnings, and the packaged Development build runs on the RTX GPU (all through the UE job queue, `09`).
- No gameplay code exists yet, and no results exist yet.

---

## A. Identity

| | |
|---|---|
| **Name** | GCBudget: garbage-collection-aware gameplay data |
| **Technical category** | Memory and resource optimisation; UObject lifecycle and garbage collection |
| **Main objective** | Measure how UE 5.8's garbage-collection cost scales with the number of UObjects and references a gameplay design creates. Compare one UObject per item, GC clusters, Experimental incremental reachability, and struct arrays with stable handles on one workload. Package the measurement and the handle storage as a small reusable plugin |
| **Difficulty** | ★★★½☆ Advanced |
| **Core UE technologies** | UObject/GC internals (reachability, gather, purge, clusters, the disregard pool), `TObjectPtr` and its write barrier, `USTRUCT` arrays and the GC schema, generational handles, `gc.*` console variables, CSV profiler, Unreal Insights (the 5.8 "UObject Count" counter), LLM tags, `obj list` |
| **Relation to existing work** | The struct-plus-handle pattern is **not new**: the engine uses it for weak object pointers, Mass entities and GAS active effects. Epic documents clustering and incremental reachability and has published single data points. P04's contribution is the **controlled measurement on one workload** and a small, reusable telemetry and handle toolkit. See `01_Problem_Analysis.md` §3 and `01b` §6 |

## B–D. Problem, solution, architecture

- Problem, root-cause hypotheses, success criteria and the measurement plan: **[01_Problem_Analysis.md](01_Problem_Analysis.md)**
- What Phase 2 verified in the engine source, and how it changed the plan: **[01b_Research_Findings.md](01b_Research_Findings.md)**
- Solution and architecture (final v1.0): **[02_System_Architecture.md](02_System_Architecture.md)**
- The ordered build steps: **[03_Implementation_Roadmap.md](03_Implementation_Roadmap.md)**

## E. Technical demonstration

**What the viewer sees**
- A minimal level with a live overlay: the configuration (U0, U1, U2, S1 or S0), the item count, the live UObject count, and a scrolling graph of game-thread ms with GC frames marked in red.
- 100,000 "timed effect" items are alive, about 1,000 expire and are replaced every second, and the GC runs every 2 seconds.

**Before** (U0: one UObject per item)
- The UObject count reads the empty-level count plus one object per item.
- Every 2 seconds the frame graph shows a GC spike.
- The overlay prints the pause in ms and the number of objects purged.

**Engine mitigations** (U1 clusters, U2 incremental reachability)
- U1: the spike changes shape (less traversal, more cluster work).
- U2: the spike is cut into slices, but the first and last frames of each cycle remain visible.
- Both are shown honestly, including their costs and Epic's own caveats.

**After** (S0: struct array with handles)
- The UObject count stays at the empty-level value.
- The GC spike is the same height as in an empty level.
- The overlay shows the per-item update and lookup cost, so the trade-off is on screen too.

**Recorded material:**
- a side-by-side video of U0 vs S0;
- curves of GC pause against item count for U0, S1 and S0;
- the per-frame GC timeline for U0, U1, U2 and S0 at 100k;
- an Unreal Insights walkthrough of one GC cycle (the `ConditionalCollectGarbage` timer, the "UObject Count" counter).

## G. Portfolio presentation (produced in Phase 10)

1. Problem statement, one paragraph (from `01` §1).
2. The investigation: how the 5.8.4 GC actually works (one diagram from `01b`), and hypotheses H1–H8 with what the data showed for each.
3. The architecture diagram (from `02` §5) and the per-frame flow.
4. Key decisions and rejected alternatives: why the bracket measurement, why pages for clustering, why handles shaped like the engine's own, why no novelty is claimed.
5. Before/after results against **all four** baselines (U0, U1, U2, S1), with the full environment and method.
6. What didn't work or didn't improve, stated plainly.
7. A short **migration guide**: when to keep UObjects, and how to move hot data to a struct store.
8. Video, source repository and the complete `Explanation/` folder.

## H. Industry relevance

**Skills demonstrated:**
- engine-level understanding of the UObject lifecycle and the garbage collector, read in the engine source rather than assumed: what is traversed, what is scanned, what is sliced, and what verification costs in Development;
- data-oriented design: the GC schema, contiguous storage, generational handles;
- memory measurement: working set, UObject counts, LLM tags;
- honest benchmarking of noisy events: GC cycles, percentiles, interleaved runs, CPU pinning on a hybrid laptop;
- knowing the limits of engine features: Experimental flags, thread-safety caveats, build-configuration effects.

**Studios and roles that ask for this:**
- EA Vancouver NHL: "taking ownership of memory management, multi-threading, and frame-rate optimization";
- Ubisoft Snowdrop: "memory, performance, load times", "memory usage and loading times";
- memory management appears in 18 of 57 postings reviewed;
- engine and performance roles that profile with Insights.

Sources: [research/01](Research/01_job_market.md) (EA5, U8, §4).

**Interview questions it prepares you for:**
- "Walk me through what Unreal's garbage collector does in one cycle. Which part scales with what?"
- "You have a GC hitch every minute. How do you find out what's causing it, and what are your options?"
- "When should gameplay data be a UObject and when a USTRUCT? What do you give up?"
- "What is a GC cluster? When does it help, and when does it hurt?"
- "What does incremental reachability need from your code? Why is it risky?"
- "Why must `UPROPERTY` object pointers be `TObjectPtr` in UE5?"
- "How do you keep a stable reference to an element of an array that changes? What is a generational handle?"
- "Why are editor GC numbers different from a packaged build?"
- "How would you prove a memory or GC optimisation worked?"
