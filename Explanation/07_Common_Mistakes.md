# 07 · Common Mistakes: GCBudget

**Rule:** every mistake listed here was either made in this project (and says when) or is a known trap verified in the engine source (and cites it). For each one: how to spot it, how to prevent it, how to debug it. **Current to:** Phase 5 step S1. The GC-specific traps that Phase 5 code must avoid are listed in advance in §2, from the Phase 2 research.

---

## 1. Build and run pipeline (made in Phase 4)

| Mistake | What happened | How to spot it | Prevention |
|---|---|---|---|
| **Using `%~dp0` after `shift` in a batch file** | `smoke_game.bat` built its paths after `shift`. In batch, `shift` also shifts `%0`, so `%~dp0` became the current directory. The packaged exe "could not be found" (exit 3), and the uncooked smoke log was written one folder above the repository (`09` F-5a) | "The system cannot find the path specified."; a log file missing from where the script says it writes | Capture `set "HERE=%~dp0"` at the top, before any `shift`, and build every path from `%HERE%` (`05` A9) |
| **Leaving out `DefaultInput.ini`** | The first uncooked smoke run logged `LogEnhancedInput: Warning: … does not have a valid PlayerInput object` (`09` F-3a) | One warning at map load in an otherwise clean log | Keep the template's two Enhanced Input keys (`05` A8). The C++ defaults are the classic input classes (`Runtime/Engine/Private/UserInterface/InputSettings.cpp:52–53`) |
| **Letting the engine write a secret into a committed config file** | The default-enabled Android File Server editor module appended a section with a random `SecurityToken` to `DefaultEngine.ini` on the first editor-binary run (`09` F-3a). It was caught before any commit | `git diff` on `Config\` after the first editor run; a grep for `SecurityToken` | Disable the plugin in the `.uproject` (`05` A1), delete the section, and re-check after the next editor run (`09` F-3c) |
| Passing a command-line switch the engine doesn't parse | *(Avoided.)* `-ExitAfterSeconds`, seen in other projects' scripts, appears nowhere in the 5.8.4 runtime source (a grep of `Engine/Source/Runtime` finds no match). An unparsed switch is silently ignored, so a script can look right and do nothing | The run doesn't behave as the switch promises; a grep of the source for the switch name | Cite the parse site of every switch a script uses (`06` §6) |
| Running a UE build or game directly instead of through the queue | *(Avoided.)* Ten projects share one machine; a stray compile corrupts another project's benchmark | — | Every UE job runs as `ue_queue.py run …` (`09` lists the exact commands) |
| Expecting the Test configuration | The launcher engine can't build Test (portfolio fact, `research/04` §0b) | BuildCookRun: "Targets cannot be built in the Test configuration with this engine distribution" | Headline runs use packaged **Development** with the GC adjustments in `01` §7 |

## 2. GC traps the Phase 5 code must avoid (from Phase 2)

| Trap | Why it is wrong | Evidence | Prevention in this project |
|---|---|---|---|
| Calling `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS)` to "trigger a GC" | The default is a **full purge**: blocking, never incremental, purge in one step | `Runtime/CoreUObject/Public/UObject/UObjectGlobals.h:952`; `Runtime/CoreUObject/Private/UObject/GarbageCollection.cpp:5686` | The director uses `ForceGarbageCollection(false)`, which goes through the engine's normal path (`02` §6) |
| A raw `UObject*` `UPROPERTY` with incremental reachability on | The write barrier lives in `TObjectPtr`. A raw-pointer assignment during a pending incremental cycle isn't seen, and the object can be freed early | `Runtime/CoreUObject/Public/UObject/ObjectPtr.h:23–25, 337–361`; Epic's incremental GC docs | `TObjectPtr` everywhere (`02` §8) |
| Touching UObjects from worker threads with incremental reachability on | "Not fully thread safe": an object changed on a worker thread may not be marked reachable | Epic's incremental GC docs (`Phase2_Raw_Notes/D` §D1) | All item operations on the game thread |
| Measuring GC in a packaged Development build without `-NoVerifyGC` | Development verifies GC assumptions on every GC by default, which inflates every pause and hides cluster gains | `Runtime/CoreUObject/Private/UObject/GarbageCollectionVerification.cpp:36–42`; Epic KB (`Phase2_Raw_Notes/D` §D2) | `-NoVerifyGC` on every timed run |
| Measuring GC in the editor or PIE | No disregard-for-GC pool, a keep-flag scan over every object, `TObjectPtr` access tracking | `Runtime/CoreUObject/Private/UObject/UObjectArray.cpp:202`; `GarbageCollection.cpp:4425`; `ObjectHandleDefines.h:9–21` | Packaged builds only |
| Trusting `Exclusive/GameThread/GarbageCollection` as "the GC time" | It covers reachability traversal and incremental purge only: Pre, gather, cluster dissolving and Post fall outside it | `GarbageCollection.cpp:5646–5654, 4796` | Our bracket (`01b` §4), with the engine column kept as a cross-check |
| Expecting the 5.8 "UObject Count" counter in a default trace | It is on the `counters` channel, which the 5.8.4 default preset lacks | `Runtime/Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:138` | `-trace=default,counters` |
| Dropping a clustered object and expecting it to be collected | Clustered objects are marked reachable whenever their root is | `GarbageCollection.cpp:4324–4346` | U1 dissolves and rebuilds changed pages before each cycle (`02` §6) |
| Letting shared definitions join a cluster | The first cluster absorbs them, every other cluster references it, and dissolving it dissolves its referencers recursively | `Runtime/CoreUObject/Private/UObject/UObjectClusters.cpp:136–178, 741–751` | `UGCBItemDef::CanBeInCluster()` returns false (`02` §4.2) |
| Keeping a raw index or element pointer into a struct array across frames | Indices get reused; pointers dangle when the array grows | `TArray` semantics | Handles with generations; `Reserve(N)` at populate (`02` §8) |

## 3. Telemetry and director (S1)

| Mistake | Why it is wrong | How to spot it | Prevention in this project |
|---|---|---|---|
| Reading `GetLastGCDuration()` in a `PostGarbageCollect` handler | The value is stored after the blocking call returns, which is after that delegate fires, so the handler sees the **previous** cycle | Pauses that lag one cycle behind a spike | Read it in `OnWorldTickEnd`, after `ConditionalCollectGarbage` returned (`05` B2; `GarbageCollection.cpp:5666–5675, 5943`) |
| "Disabling" the engine's GC timer with 0 | `ConditionalCollectGarbage` starts a cycle only if the interval is > 0 (`UnrealEngine.cpp:2284`), so `ForceGarbageCollection` stops working too: no GC at all | `GC/Count` stays 0 and memory grows | Park it at 3600 s (`05` B4) |
| Binding a global delegate without removing it, or without checking the world | `FWorldDelegates` fire for every world; a dead listener left bound is a dangling `this` | Crashes after map travel; doubled stats in PIE | `Remove` in `Deinitialize`; every handler checks `InWorld != GetWorld()` (`05` B2) |
| Reading the clock in the wrong order inside the bracket | Our own bookkeeping would be measured as GC time | A floor that drifts with code changes | Clock last when opening, first when closing (`05` B2) |
| Running an unknown or unbuilt configuration silently | A typo in `-GCBConfig` would measure an empty world and look like a perfect result | A suspiciously good run | Unknown names are rejected with an `Error`; the run stays idle and writes no report, so the runner fails it (`05` B4) |
| A shell one-liner that embeds backslash paths in Python string literals | `\r`, `\v` and others become control characters in the output file | Corrupted documentation lines | *(Made while writing these docs.)* Write content with an editor or a script file, never inside `python -c "…"` |
| A script edit that puts a real line break inside a `TEXT("…")` literal | A string literal can't span source lines; the compiler rejects the file | A build error in a file the edit touched | *(Made while applying a review fix; found by reading the result before the build.)* After any scripted source edit, check every changed line for an unescaped `\n` and an even number of quotes |
