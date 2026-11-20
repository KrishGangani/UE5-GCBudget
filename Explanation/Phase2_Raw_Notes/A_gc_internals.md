> **Raw Phase 2 research notes (A_gc_internals), UE 5.8.4, 2026-11-21.** Kept as an evidence trail. Every claim cites a file:line in the local engine or a URL. **The claims the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md) (marked ✔). Treat anything here that is not restated there as unverified.**

# P04 Phase 2, Part A: how the 5.8.4 garbage collector works

Engine: `C:\Program Files\Epic Games\UE_5.8\Engine`, `Build/Build.version` = 5.8.4, CL 58902052.
Paths are relative to `Engine/Source/Runtime/`. **[inferred]** marks a conclusion drawn from code flow rather than one explicit line.

---

## A1. Who starts a GC in a running game

| Step | Evidence | Notes |
|---|---|---|
| `UWorld::Tick` calls `GEngine->ConditionalCollectGarbage()` near its end, inside a trace scope named `ConditionalCollectGarbage` | `Engine/Private/LevelTick.cpp:1969–1970` | After all tick groups, `OnWorldPostActorTick` (:1906), async traces, net flush and the FX tick; before `OnWorldTickEnd` (:2061) |
| `ConditionalCollectGarbage` runs once per frame (`GFrameCounter != LastGCFrame`) and is wrapped in `QUICK_SCOPE_CYCLE_COUNTER(STAT_ConditionalCollectGarbage)` | `Engine/Private/UnrealEngine.cpp:2185–2191` | Non-virtual (`Engine/Classes/Engine/Engine.h:2820`), so a project can't override it |
| Each frame it adds the **game** delta time to `TimeSinceLastPendingKillPurge` | `UnrealEngine.cpp:2265` | With a fixed step (`-deterministic -FPS=60`) the GC cadence is therefore an exact number of frames **[inferred]** |
| Branch order each frame: (1) if incremental reachability is pending → `PerformIncrementalReachabilityAnalysis(GetReachabilityAnalysisTimeLimit())`; (2) else if no purge is pending and the timer exceeded `gc.TimeBetweenPurgingPendingKillObjects` (and that value is > 0) → `PerformGarbageCollectionAndCleanupActors()`; (3) else `IncrementalPurgeGarbage(true, GetIncrementalGCTimePerFrame())` | `UnrealEngine.cpp:2276–2294` | So at most one GC "kind" of work runs per frame. Setting the interval to 0 disables timer-started cycles |
| `PerformGarbageCollectionAndCleanupActors` calls `TryCollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, bForcePurge)`; `bForcePurge` starts `true` and becomes `false` as soon as any game world exists | `UnrealEngine.cpp:2316–2336` | In a game the timer-driven GC is a **non-full purge** (incremental purge) |
| `ForceGarbageCollection(bForcePurge)` only sets the timer past the interval and ORs `bFullPurgeTriggered` | `UnrealEngine.cpp:2136–2139` | `ForceGarbageCollection(true)` takes the full-purge branch (:2216–2231), which calls `TryCollectGarbage(…, true)` (:2222) |
| `gc.TimeBetweenPurgingPendingKillObjects`: C++ default 60.0 s, `BaseEngine.ini` sets **61.1** ("pick a fractional number to keep phase shifting and avoid collisions") | `UnrealEngine.cpp:1777`; `Engine/Config/BaseEngine.ini:1748–1749` | |
| `gc.IncrementalGCTimePerFrame` = 0.002 s (2 ms): the per-frame time limit for incremental **purge** | `UnrealEngine.cpp:1826–1831` | Not the reachability limit (A4) |
| `GARBAGE_COLLECTION_KEEPFLAGS` = `RF_Standalone` in the editor, `RF_NoFlags` in a game | `CoreUObject/Public/UObject/GarbageCollection.h:28` | With `RF_NoFlags` the slow all-objects keep-flag scan is skipped (A3) |
| The public `CollectGarbage(KeepFlags, bPerformFullPurge = true)` and `TryCollectGarbage(…, bPerformFullPurge = true)` **default to a full purge** | `CoreUObject/Public/UObject/UObjectGlobals.h:952, 962` | A hand-written `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS)` call is blocking and never incremental (A4) |
| Console: `obj gc` = `CollectGarbage(KEEPFLAGS, true)`; `obj gc incremental` = non-full purge | `UnrealEngine.cpp:9338–9347` | |

## A2. The phases of one GC cycle (non-full purge, the game default)

`CollectGarbageInternal` times the whole blocking call into `GTimingInfo.LastGCDuration` and bumps the CSV stat `GC/Count` (`CoreUObject/Private/UObject/GarbageCollection.cpp:5666–5675`). Inside it, `FReachabilityAnalysisState::CollectGarbage` (:5963) calls `PerformReachabilityAnalysisAndConditionallyPurgeGarbage` (:5990), which runs three parts:

| Part | What it does | Evidence |
|---|---|---|
| **Pre** (`PreCollectGarbageImpl`) | Releases the GC lock, optionally flushes async loading, broadcasts `GetPreGarbageCollectDelegate()`, re-acquires the lock; broadcasts `GetGarbageCollectStartedDelegate()`; **finishes any previous incremental purge first**; logs "Collecting garbage"; locks the UObject hash tables; **verifies GC assumptions when `gc.VerifyAssumptions` is on** | `:5700–5824`; delegate at :5741; verification at :5803–5818 |
| **Reachability** (`CollectGarbageImpl` → `FRealtimeGC::PerformReachabilityAnalysis`) | Marks everything "maybe unreachable", marks clusters and roots reachable, then traverses references from the roots | `:5827–5838`, `:4643–4710` |
| **Post** (`PostCollectGarbageImpl`) | Updates GC history, dissolves clusters that need it, dissolves unreachable clusters, clears weak references, **gathers unreachable objects** (a scan of the whole object array), unlocks, broadcasts `PostReachabilityAnalysis`, unhashes unreachable objects (BeginDestroy) only if full purge or `gc.IncrementalBeginDestroyEnabled=0`, sets `GObjPurgeIsRequired`, broadcasts `GetPostGarbageCollect()` | `:5841–5961`; gather :5886–5889; unhash :5908–5911; delegate :5943 |
| **Purge** (later frames) | `IncrementalPurgeGarbage(true, 2 ms)` from `ConditionalCollectGarbage` each frame: BeginDestroy (unhash) then FinishDestroy and delete, under a time limit; on completion broadcasts `GetPostPurgeGarbageDelegate()` and `GarbageCollectComplete` | `GarbageCollection.cpp:4794–4876`; `UnrealEngine.cpp:2290–2294` |

**Consequence:** with default settings one GC cycle costs one **blocking** frame (Pre + Reachability + Post, i.e. `GetLastGCDuration()`), followed by several frames of time-limited purge work **[inferred from the call graph above]**.

## A3. Reachability analysis in detail

| Fact | Evidence |
|---|---|
| "Mark all objects maybe-unreachable" is **not** a loop over objects in 5.8: it swaps the meaning of two flag bits (`FGCFlags::SwapReachableAndMaybeUnreachable()`), under the object-array lock | `GarbageCollection.cpp:4503–4507`; `CoreUObject/Private/UObject/GarbageCollectionInternalFlags.h:116–123` |
| Then every **cluster** is visited and **every clustered object** gets a cheap reachable-flag write (in parallel) | `MarkClusteredObjectsAsReachable`, `GarbageCollection.cpp:4295–4380`; per-object loop :4334–4346 |
| Then roots (`GRoots`) are marked and become the initial work list | `MarkRootObjectsAsReachable`, `:4382–4422` |
| The slow scan of **all** objects for keep flags runs only when `KeepFlags != RF_NoFlags` (editor) | `:4425–4452`; comment "This is super slow as we need to look through all existing UObjects" |
| `GObjectCountDuringLastMarkPhase` = live objects minus the disregard-for-GC pool | `:4516` |
| Reference traversal loops `PerformReachabilityAnalysisPass` until no work is left; with incremental reachability it breaks out when `GReachabilityState.IsTimeLimitExceeded()` | `:4643–4690`, break at :4668 |
| Options per run: `Parallel` unless single-threaded GC is forced; `EliminateGarbage` if garbage elimination is on; `IncrementalReachability` **only if `gc.AllowIncrementalReachability` and not a full purge** | `GetReferenceCollectorOptions`, `:5678–5687` |
| Worker count = task-graph worker threads, clamped to `UE::GC::MaxWorkers` | `:7330–7333`, `:7494–7497` |
| `gc.AllowParallelGC` (C++ default 1; `BaseEngine.ini` True). Help text: "Used to control parallel GC." **Not labelled experimental** | `:4717–4723`; `BaseEngine.ini:1747` |
| Single-threaded GC is forced when threading is off, the platform lacks multithreading, < 2 cores, `gc.AllowParallelGC 0`, or per-class detailed stats are compiled in | `ShouldForceSingleThreadedGC`, `:4727–4736` |
| A struct-array UPROPERTY whose element type holds **no strong object reference** emits **nothing** into the GC schema, so GC never looks at its elements | `FArrayProperty::EmitReferenceInfo`, `:6869–6872` |
| An array of structs that **does** hold references becomes a `StructArray` schema member: GC walks every element | `:6874–6876` |

## A4. Incremental reachability (Experimental)

| Fact | Evidence |
|---|---|
| `gc.AllowIncrementalReachability` default **0**, help text "Set to control incremental Reachability Analysis **(experimental)**" | `GarbageCollection.cpp:302–308` |
| `gc.IncrementalReachabilityTimeLimit` default **0.005 s (5 ms)**, "(experimental)". **The 5.8 docs page uses 0.002 s (2 ms) as its example**; that is an example, not the default | `:310–316`; docs (D1) |
| `gc.AllowIncrementalGather` default 0, "(experimental)" | `:318–324` |
| `gc.DelayReachabilityIterations` default 0 (non-Shipping only); **the docs page calls it `gc.DelayReachabilityIteration` with default 10**: a doc/source mismatch | `:293–299`; docs (D1) |
| The time limit is used only for non-full purges: `bReachabilityUsingTimeLimit = !bFullPurge && GAllowIncrementalReachability` | `:5983` |
| The **first** iteration does all of Pre (A2) plus `StartReachabilityAnalysis` (flag swap, cluster marking, roots) before the time-limited passes; the timer starts "when we actually start processing" | `:6019–6033`, `:4542–4570` |
| Later iterations come from `PerformIncrementalReachabilityAnalysis(TimeLimit)`, which `ConditionalCollectGarbage` calls once per frame while pending | `:6185–6205`; `UnrealEngine.cpp:2276–2280` |
| Gather, cluster dissolving and the Post delegates run only when reachability is **no longer pending** (the last iteration) | `:5849`, `:5900`, `:5939` (`if (!GIsIncrementalReachabilityPending)`) |
| A new GC request while incremental reachability is pending **finishes the current one as a full purge first** | `:5967–5977` |
| The log reports each slice's overshoot: "GC Reachability iteration time: … (latency: …)" | `:6066–6072` |
| A full purge (`CollectGarbage` default, `ForceGarbageCollection(true)`, `obj gc`) never runs incrementally | `:5686`, `:5983` |

**What this means for hitches [inferred]:** incremental reachability caps the *reference-traversal* slices near the limit, but the first frame still pays Pre + the flag swap + cluster marking + roots, and the last frame still pays the gather scan, cluster dissolving and the Post work. Whether that leaves a visible spike at 100k objects is a **measurement question** (hypothesis H5 in `01`).

## A5. The write barrier (why incremental reachability needs `TObjectPtr`)

| Fact | Evidence |
|---|---|
| `UE_OBJECT_PTR_GC_BARRIER` defaults to **1** in every configuration | `CoreUObject/Public/UObject/ObjectPtr.h:23–25` |
| Constructing, copying, moving or assigning an `FObjectPtr` calls `ConditionallyMarkAsReachable`, which marks the target reachable **only while `UE::GC::GIsIncrementalReachabilityPending`** | `ObjectPtr.h:72–76, 99–158, 160–164`; body :337–361 |
| The same barrier exists for mutable views of `TObjectPtr` and for arrays, sets and maps of them | `ObjectPtr.h:1590–1673` |
| The docs: "any assignment to a `TObjectPtr`-exposed `UPROPERTY` immediately marks the object as reachable … also known as a garbage collector write barrier", and raw-pointer `UPROPERTY`s must be converted "otherwise garbage collection might reclaim some of UObjects' memory too early" | D1 |
| Barrier-marked objects are fed back into the next iteration (`GReachableObjects`) and counted in `GGCStats.NumBarrierObjects` | `GarbageCollection.cpp:4592–4600` |

**Cost outside incremental GC [inferred]:** a load of a global `bool` and a predictable branch per pointer write. No barrier cost on reads.

## A6. Clusters

| Fact | Evidence |
|---|---|
| `gc.CreateGCClusters` C++ default 1; `BaseEngine.ini` True | `CoreUObject/Private/UObject/UObjectClusters.cpp:26–32`; `BaseEngine.ini:1752` |
| `gc.AssetClustreringEnabled` (sic) C++ default 1, but **`BaseEngine.ini` sets it False** | `UObjectClusters.cpp:34–40`; `BaseEngine.ini:1754` |
| `gc.MinGCClusterSize` C++ default 2, `BaseEngine.ini` sets 5 | `UObjectClusters.cpp:43–49`; `BaseEngine.ini:1753` |
| `gc.ActorClusteringEnabled` C++ default 1, `BaseEngine.ini` False | `Engine/Private/Level.cpp:95–101`; `BaseEngine.ini:1755` |
| Clusters created automatically from loaded packages need cooked data, initial load finished, `gc.CreateGCClusters` **and** asset clustering on | `CanCreateObjectClusters`, `UObjectClusters.cpp:854–857`; `CreateClustersFromPackage` :860–874 |
| **Runtime API, public and exported:** `UObjectBaseUtility::CreateCluster()` (virtual), `CanBeInCluster()` (virtual), `AddToCluster(…)` | `CoreUObject/Public/UObject/UObjectBaseUtility.h:420, 425, 437`; impl `UObjectClusters.cpp:877–1000` |
| `CreateCluster` asserts `GCreateGCClusters`, collects everything the root references (transitively) that can be in a cluster, isn't rooted, isn't disregard-for-GC and **is in the root's package**; anything else becomes a "mutable object" the GC still explores | `UObjectClusters.cpp:927–960`; rules at :741–751 |
| A cluster smaller than `GetMinClusterSize()` is discarded | `:958`, `:986–996` |
| `UObjectBaseUtility::CanBeInCluster()` defaults to the outer's answer; `AActor::CanBeInCluster()` returns `bCanBeInCluster` | `UObjectClusters.cpp:922–925`; `Engine/Private/Actor.cpp:566–569` |
| During reachability, a clustered object's **own references are not traversed**; only the cluster's referenced clusters and mutable objects are | `MarkReferencedClustersAsReachable`, `GarbageCollection.cpp:1189–1241`; mutable objects :1110–1160 |
| If a cluster references a Garbage object, all its objects are traversed this time and the cluster is flagged to be dissolved | `:1226–1240` |
| **Clustered objects are marked reachable whenever their root is**, with no per-object garbage check | `:4324–4346` |
| A cluster can be dissolved from game code: `GUObjectClusters.DissolveCluster(UObjectBaseUtility*)` is exported | `CoreUObject/Public/UObject/UObjectArray.h:1593–1650, 1675` |
| Console: `gc.ListClusters`, `gc.FindStaleClusters`, `gc.DumpRefsToCluster`, `gc.SuggestClusters` | `UObjectClusters.cpp:569–590` |

**Design consequence [inferred]:** a clustered gameplay item that is dropped from its container stays alive until its cluster is dissolved. Clusters fit data whose reference graph doesn't change (assets). For gameplay data that churns, the cluster must be dissolved and rebuilt, and that work belongs in the measurement.

## A7. The disregard-for-GC pool and object limits

| Fact | Evidence |
|---|---|
| In cooked builds `gc.MaxObjectsNotConsideredByGC` (`BaseEngine.ini` = 1) enables a pool; everything allocated during initial load lands in it and is never collected or traversed | `UObjectBase.cpp:1370–1380`; `UObjectArray.cpp:146–204` |
| **The pool can't be enabled in the editor:** `checkf(!DisregardForGCEnabled() || !GIsEditor, …)` | `UObjectArray.cpp:202` |
| Max UObjects in a cooked game: default `2 * 1024 * 1024` (2,097,152), overridable by `gc.MaxObjectsInGame` | `UObjectBase.cpp:1342–1349, 1363, 1382–1386` |
| Live object count accessor: `GUObjectArray.GetObjectArrayNumMinusAvailable()`; first collectable index: `GetFirstGCIndex()` | `CoreUObject/Public/UObject/UObjectArray.h:1266–1269, 1155–1158` |

## A8. Development vs Test/Shipping vs editor (what changes GC cost)

| Difference | Evidence | Effect on this project |
|---|---|---|
| `gc.VerifyAssumptions` defaults to **true in Development non-editor builds** (`!UE_BUILD_SHIPPING && !UE_BUILD_TEST && !WITH_EDITOR`): every GC verifies the disregard pool and clusters | `CoreUObject/Private/UObject/GarbageCollectionVerification.cpp:36–42`; used at `GarbageCollection.cpp:5803` | Packaged Development runs must pass **`-NoVerifyGC`**, parsed at `Obj.cpp:5917`, which sets `gc.VerifyAssumptions`, `…OnFullPurge` and `…Chance` off (:5918–5946) |
| `ENABLE_GC_OBJECT_CHECKS` is on unless Test or Shipping; Epic's comment: pointer checks "add roughly 20% overhead to GC times" | `GarbageCollection.h:30–33` | **A compile-time define in the precompiled launcher engine.** Development GC times carry it in every configuration. It can't be turned off without a source build |
| `ENABLE_GC_HISTORY` = `!UE_BUILD_SHIPPING` | `GarbageCollection.h:35` | History is sized by `gc.HistorySize`; left at its default |
| `VERIFY_DISREGARD_GC_ASSUMPTIONS` = `!UE_BUILD_SHIPPING` | `GarbageCollectionVerification.h:15` | Enables the verification code that `-NoVerifyGC` switches off |
| Editor: keep-flag scan (`RF_Standalone`), no disregard pool, editor-only objects | A1, A3, A7 | **Editor GC numbers are not comparable**: packaged builds only |
| Editor builds also have `TObjectPtr` access tracking and late resolve | Part C | Per-item access cost also differs in the editor |

## A9. Hybrid CPU and parallel GC

| Fact | Evidence |
|---|---|
| `FWindowsPlatformMisc::NumberOfCoresIncludingHyperthreads()` counts the machine's logical processors through `QueryCpuInformation`; the filter used there is a NUMA-node mask, **not the process affinity mask** | `Core/Private/Windows/WindowsPlatformMisc.cpp:2614–2690`, `QueryCpuInformation` |
| Worker threads to spawn = logical − 2 when hyper-threading is present | `WindowsPlatformMisc.cpp:2719–2737` |

**Consequence [inferred]:** when a run is pinned to the 16 P-core logical CPUs (the portfolio protocol), the engine still creates about 22 task-graph workers for a 24-thread CPU, and parallel GC runs those workers on 16 CPUs. Every configuration sees the same thing, so comparisons stay fair; absolute parallel-GC numbers are specific to this setup. Recorded in the environment.
