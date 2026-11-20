> **Raw Phase 2 research notes (C_data_layout), UE 5.8.4, 2026-11-21.** Kept as an evidence trail. **The findings the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md) (marked ✔).**

# P04 Phase 2, Part C: data layout, `TObjectPtr` and handles

Paths are relative to `Engine/Source/Runtime/` (or `Engine/Plugins/` where stated). **[inferred]** = drawn from code flow.

---

## C1. What a UObject costs before it does anything

Not sizes (those are measured in Phase 8), only *what exists per object*, read from the headers:

| Per object | Evidence |
|---|---|
| `UObjectBase` members: `ObjectFlags`, `InternalIndex`, `ClassPrivate`, the name (with an object-hash index), `OuterPrivate`, plus the virtual table of `UObject` | `CoreUObject/Public/UObject/UObjectBase.h:399–464` |
| One `FUObjectItem` in the global object array: packed flags, ref count and pointer, `SerialNumber`, `ClusterRootIndex` (and a remote id when remote objects are compiled in) | `CoreUObject/Public/UObject/UObjectArray.h:41–95` |
| Entries in the UObject name and outer hash tables (that is what "unhashing" removes at purge time) | `UnhashUnreachableObjects`, `GarbageCollection.cpp:6288–6345` |
| A GC traversal of its reflected references (schema) every cycle while reachable, plus its Outer and Class | A3; cluster code handles `EMemberlessId::Outer/Class` explicitly (`UObjectClusters.cpp:674–687`) |
| A slot in the gather scan every cycle, reachable or not | `GatherUnreachableObjects`, `GarbageCollection.cpp:5388–5410` |
| BeginDestroy / FinishDestroy / destructor at purge when it dies | A2 |

**A struct element in a `TArray`** has only its own fields. If it holds no strong object reference, GC never visits it (A3, `GarbageCollection.cpp:6869–6872`).

## C2. `TObjectPtr` in a packaged game vs the editor

| Fact | Evidence |
|---|---|
| `UE_WITH_OBJECT_HANDLE_LATE_RESOLVE`, `UE_WITH_OBJECT_HANDLE_TRACKING` and `UE_WITH_OBJECT_HANDLE_TYPE_SAFETY` are all `(!UE_WITH_REMOTE_OBJECT_HANDLE && WITH_EDITORONLY_DATA)` | `CoreUObject/Public/UObject/ObjectHandleDefines.h:9–21` |
| `UE_WITH_REMOTE_OBJECT_HANDLE` defaults to 0 | `Core/Public/Misc/CoreMiscDefines.h:618–620` |
| Without late resolve (and in a monolithic build), `FObjectHandle` **is** `UObject*` and `UE_OBJECT_HANDLE_IS_OBJECT_PTR` = 1 | `CoreUObject/Public/UObject/ObjectHandle.h:29–55` |
| `ResolveObjectHandle` calls `OnHandleRead` (the access-tracking hook) only with late resolve or tracking compiled in; otherwise it is a plain pointer read | `ObjectHandle.h:603–615` |
| With tracking compiled in, `OnHandleRead` costs an atomic load and a branch, and calls the registered callbacks only if any exist (`HandleReadCallbackQuantity > 0`) | `CoreUObject/Public/UObject/ObjectHandleTracking.h:103–121`; callback API :70–98 |
| The GC write barrier (A5) is compiled in **regardless** of editor data | `ObjectPtr.h:23–25` |

**Consequence [inferred]:**
- In a packaged Development or Shipping game, reading a `TObjectPtr` costs the same as reading a raw pointer.
- Writing one costs one extra global-bool check (the barrier).
- In the editor (`WITH_EDITORONLY_DATA`), reads also pass through the tracking hook, and unresolved handles can be resolved lazily on first access.

So per-item access costs measured in the editor would not describe a shipped game. That is a second reason, besides GC itself (A8), for packaged-only measurement.

## C3. Generational handles the engine already uses

| Pattern | What it stores | Evidence |
|---|---|---|
| `FWeakObjectPtr` | `ObjectIndex` + `ObjectSerialNumber` into `GUObjectArray`; valid only if the serial number still matches | `CoreUObject/Public/UObject/WeakObjectPtr.h:570–574`; check :414–438 |
| `FMassEntityHandle` (moved to MassCore in 5.8) | `Index` + `SerialNumber` | `Mass/MassCore/Public/Mass/EntityHandle.h:12, 23, 26`; the old `MassEntity/Public/MassEntityHandle.h` is `UE_DEPRECATED_HEADER(5.8, …)` |
| GAS active gameplay effects | Stored as **structs** (`struct FActiveGameplayEffect : public FFastArraySerializerItem`) in `TArray<FActiveGameplayEffect> GameplayEffects_Internal`, addressed by `FActiveGameplayEffectHandle` | `Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Public/GameplayEffect.h:1355, 1417, 1911` |

**What this tells the design:** "a struct array plus an (index, generation) handle" is not a new idea. Epic uses it for weak object references, for Mass entities and for active gameplay effects. P04 makes no novelty claim for the pattern. Its contribution is the **measurement** and a small, reusable version for ordinary gameplay code (see `D_existing_work.md`).

## C4. Runtime cluster API (for the clustering baseline)

| API | Evidence | Constraint |
|---|---|---|
| `UObjectBaseUtility::CreateCluster()` (public, virtual) | `UObjectBaseUtility.h:425`; `UObjectClusters.cpp:927–1000` | Needs `gc.CreateGCClusters` (asserted). Only objects in the root's **package** join; the rest become mutable references. Clusters below `gc.MinGCClusterSize` (5 in `BaseEngine.ini`) are discarded |
| `CanBeInCluster()` override returning `true` | `UObjectBaseUtility.h:420`; default `UObjectClusters.cpp:922–925` | Epic's KB article shows exactly this override (D2) |
| `AddToCluster(Root, bAddAsMutableObject)` | `UObjectBaseUtility.h:437`; `UObjectClusters.cpp:877–920` | Adds one object (and what it references) to an existing cluster |
| `GUObjectClusters.DissolveCluster(Object)` | `CoreUObject/Public/UObject/UObjectArray.h:1593–1650, 1675` | Exported; needed to release clustered items that were dropped (A6) |

**Not safe to call while incremental reachability is pending [inferred]:** the cluster arrays are read by the GC passes. The clustering baseline therefore never runs together with incremental reachability.

## C5. Iteration cost (to be measured, not assumed)

- A dense `TArray<FStruct>` is contiguous, so a full pass over 100k elements streams memory.
- A `TArray<TObjectPtr<UItem>>` is contiguous *pointers*; each element access dereferences into a separate heap allocation made by `StaticAllocateObject`. Allocation order may or may not keep objects close **[inferred]**.
- The Phase 1 hypothesis (H7) is that the struct layout is faster per item. The size of the difference is a measurement, and it may be small at 10k items if everything fits in cache.
