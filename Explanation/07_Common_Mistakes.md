# 07 · Common Mistakes: GCBudget

**Rule:** every mistake listed here was either made in this project (and says when) or is a known trap verified in the engine source (and cites it). For each one: how to spot it, how to prevent it, how to debug it. **Current to:** the end of Phase 5 (re-checked in Phase 6). The GC-specific traps that Phase 5 code must avoid are listed in advance in §2, from the Phase 2 research.

---

## 1. Build and run pipeline (made in Phase 4)

| Mistake | What happened | How to spot it | Prevention |
|---|---|---|---|
| **Using `%~dp0` after `shift` in a batch file** | `smoke_game.bat` built its paths after `shift`. In batch, `shift` also shifts `%0`, so `%~dp0` became the current directory. The packaged exe "could not be found" (exit 3), and the uncooked smoke log was written one folder above the repository (`09` F-5a) | "The system cannot find the path specified."; a log file missing from where the script says it writes | Capture `set "HERE=%~dp0"` at the top, before any `shift`, and build every path from `%HERE%` (`05` D1) |
| **Leaving out `DefaultInput.ini`** | The first uncooked smoke run logged `LogEnhancedInput: Warning: … does not have a valid PlayerInput object` (`09` F-3a) | One warning at map load in an otherwise clean log | Keep the template's two Enhanced Input keys (`05` A8). The C++ defaults are the classic input classes (`Runtime/Engine/Private/UserInterface/InputSettings.cpp:52–53`) |
| **Letting the engine write a secret into a committed config file** | The default-enabled Android File Server editor module appended a section with a random `SecurityToken` to `DefaultEngine.ini` on the first editor-binary run (`09` F-3a). It was caught before any commit | `git diff` on `Config\` after the first editor run; a grep for `SecurityToken` | Disable the plugin in the `.uproject` (`05` A1), delete the section, and re-check after the next editor run (`09` F-3c) |
| Passing a command-line switch the engine doesn't parse | *(Avoided.)* `-ExitAfterSeconds`, seen in other projects' scripts, appears nowhere in the 5.8.4 runtime source (a grep of `Engine/Source/Runtime` finds no match). An unparsed switch is silently ignored, so a script can look right and do nothing | The run doesn't behave as the switch promises; a grep of the source for the switch name | Cite the parse site of every switch a script uses (`06` §6) |
| Running a UE build or game directly instead of through the queue | *(Avoided.)* Ten projects share one machine; a stray compile corrupts another project's benchmark | — | Every UE job runs as `ue_queue.py run …` (`09` lists the exact commands) |
| Expecting the Test configuration | The launcher engine can't build Test (portfolio fact, `research/04` §0b) | BuildCookRun: "Targets cannot be built in the Test configuration with this engine distribution" | Headline runs use packaged **Development** with the GC adjustments in `01` §7 |
| A redirection in a queue command | *(Made in Phase 5.)* The S3/S4 job's first step was a gate, `dir <file> >nul`. The job log shows it reached `cmd` as `>/dev/null`, which `cmd` can't open, so the job failed in 0.0 s with "The system cannot find the path specified." although the file existed | A job that fails before its first `echo` | Queue commands use no redirection; a gate is `type <file>` (`09` S3) |
| Running a heavy offline analysis without looking at the queue first | Ten projects share one machine. Parsing dozens of large captures takes CPU from whichever benchmark holds the queue | The queue status shows another project's batch in its measurement phase | *(Made in Phase 7: a 20 s parse of 33 baseline captures ran during another project's baseline batch; the coordinator was told which runs overlapped, `09` Phase 7.)* Check `ue_queue.py status` and the running job's last log line before any parse, and wait if it is measuring |

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

## 3. Telemetry and director (S1) and tools (S2)

| Mistake | Why it is wrong | How to spot it | Prevention in this project |
|---|---|---|---|
| Reading `GetLastGCDuration()` in a `PostGarbageCollect` handler | The value is stored after the blocking call returns, which is after that delegate fires, so the handler sees the **previous** cycle | Pauses that lag one cycle behind a spike | Read it in `OnWorldTickEnd`, after `ConditionalCollectGarbage` returned (`05` B2; `GarbageCollection.cpp:5666–5675, 5943`) |
| "Disabling" the engine's GC timer with 0 | `ConditionalCollectGarbage` starts a cycle only if the interval is > 0 (`UnrealEngine.cpp:2284`), so `ForceGarbageCollection` stops working too: no GC at all | `GC/Count` stays 0 and memory grows | Park it at 3600 s (`05` C8) |
| Binding a global delegate without removing it, or without checking the world | `FWorldDelegates` fire for every world; a dead listener left bound is a dangling `this` | Crashes after map travel; doubled stats in PIE | `Remove` in `Deinitialize`; every handler checks `InWorld != GetWorld()` (`05` B2) |
| Reading the clock in the wrong order inside the bracket | Our own bookkeeping would be measured as GC time | A floor that drifts with code changes | Clock last when opening, first when closing (`05` B2) |
| Running an unknown or unbuilt configuration silently | A typo in `-GCBConfig` would measure an empty world and look like a perfect result | A suspiciously good run | Unknown names are rejected with an `Error`; the run stays idle and writes no report, so the runner fails it (`05` C8) |
| Launching the packaged **bootstrap** exe, or launching through `cmd /c start` | The bootstrap's exit code and kill don't reliably reach the real game; `start` always returns 0 | Failed runs counted as successes | `run_bench.py` launches `Build\Windows\GCBudget\Binaries\Win64\GCBudget.exe` directly, with a timeout and a kill in `finally` (`05` D4) |
| Pinning each game after it starts | The engine's start-up threads already ran unpinned, and a per-run handle is extra work | Bimodal start-up timings | The runner pins **itself** first; children inherit the mask (`05` D2, D4) |
| `python -I` with a sibling `import` | Isolated mode leaves the script's folder off `sys.path`, so `import csv_stats` fails | `ModuleNotFoundError` in the queue log | `sys.path.insert(0, TOOLS_DIR)` (`05` D4) |
| A shell one-liner that embeds backslash paths in Python string literals | `\r`, `\v` and others become control characters in the output file | Corrupted documentation lines | *(Made while writing these docs.)* Write content with an editor or a script file, never inside `python -c "…"` |
| Passing `-traceautostart=0` to a run that should be traced | The same switch that stops an open Insights from tracing a run also blocks a trace asked for with `-trace=`/`-tracefile=` (`Runtime/Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:2017–2021`) | The traced run finishes and no `.utrace` file exists | *(Made in the first S1 job.)* `run_bench.py` adds the switch to untraced runs only (`05` D4) |
| A Windows path in an Insights command | `TimingInsights.ExportTimingEvents` reads its file name with escapes on (`Developer/TraceInsights/Private/Insights/TimingProfiler/TimingProfilerManager.cpp:817–818`): every backslash disappears and the path becomes drive-relative (`D:UNREAL PORTFOLIOP04…`) | The Insights log says "Exported … events", but the expected file doesn't exist | *(Made in the first validation job.)* The response file gets the path with forward slashes (`05` D6) |
| Classifying GC frames in a trace by duration alone | Purge frames after a cycle can be as long as a cycle-start frame, so a threshold mixes the two | More "cycle" events than cycles (here 22 for 12) | *(Made in the first validation.)* Match frames by index and check the alignment against the known cadence (`05` D6) |
| A percentile of a column that is non-zero on one frame in a hundred | It is 0 whatever the values are | A summary column of zeros for a cost that obviously exists | *(Made in S4's summary, found in S6.)* Report the maximum, or the statistic over the frames where the value exists (`05` D4, D5) |
| Reading a CSV row's FrameTime as that frame's own time | The CSV profiler measures FrameTime at the start of a frame, for the frame before it (`Runtime/Core/Private/ProfilingDebugging/CsvProfiler.cpp:150–155, 4369–4390`) | A stall whose GameThreadTime and FrameTime sit in different rows | *(Made in S2's hitch rule, found in S6.)* A frame's own duration is the next row's FrameTime; per-frame attribution uses GameThreadTime (`05` D3, D5) |
| Chaining a traced validation after a traced run with `&&` | A traced run can fail for reasons outside the measured code (here: exit 777003 after its log had closed, `09` S2), and the chain then stops before the validation | The job fails although the trace was written | *(Made in jobs 4 and 5.)* The validation runs in its own short job on the recorded trace; traced runs never share a chain with untraced batches |
| A script edit that puts a real line break inside a `TEXT("…")` literal | A string literal can't span source lines; the compiler rejects the file | A build error in a file the edit touched | *(Made while applying a review fix; found by reading the result before the build.)* After any scripted source edit, check every changed line for an unescaped `\n` and an even number of quotes |

## 4. Handles and tests (S3)

| Mistake | Why it is wrong | How to spot it | Prevention in this project |
|---|---|---|---|
| Starting generations at 0 | A default-constructed handle (`Generation = 0`) would match a fresh slot | A handle nobody issued resolves | Slots start at generation 1; the `DefaultIsInvalid` test checks it (`05` B3, B5) |
| Incrementing a generation without a wrap check | Signed overflow is undefined behaviour; a wrap to 0 would also revalidate default handles | Only after 2³¹ releases of one slot, so practically never in a test | `Release` wraps `MAX_int32` to 1 explicitly (`04` §16; `05` B4) |
| Tests that only cover the obvious cases | Allocators fail on rare interleavings of allocate and release | A test suite that passes while real use corrupts state | One million random operations against a simple model, from a fixed seed, so a failure is reproducible (`04` §20; `05` B5) |
| Trusting a test run's console output instead of its exit code | A run with failures still prints a lot of normal-looking output | A queue job "ok" with failing tests | `run_tests.bat` returns the editor's exit code, which is non-zero after any failure (`06` §18) |

## 5. The workload and the UObject stores (S4)

| Mistake | Why it is wrong | How to spot it | Prevention in this project |
|---|---|---|---|
| A new object in a clustered page that is **not** in the page's cluster | The GC never traces a cluster root's own references (`GarbageCollection.cpp:3130–3139`). An object only the root points at is freed, and the root's array keeps a dangling pointer | A crash or a wrong checksum after a GC that didn't follow a rebuild | *(Found in review, before the first build.)* Every replacement item in U1 joins its page's cluster at once with `AddToCluster(Page)` (`05` C5) |
| Calling `CreateCluster` with `gc.CreateGCClusters 0` | It asserts (`UObjectClusters.cpp:929`) | A crash on U1's first rebuild | `PrepareForGC` checks the variable and logs an error once per store instead (`05` C5) |
| Counting a cluster `CreateCluster` silently freed | Below `gc.MinGCClusterSize` (5 in `BaseEngine.ini:1753`) the cluster is freed with no message (`UObjectClusters.cpp:958–995`) | A report claiming more clusters than exist | *(Found in review.)* Count a rebuild only if the page has a cluster afterwards (`05` C5) |
| A floating-point workload | The stores' loops are compiled separately; vectorisation and fused multiply-adds can round differently, so the same math gives different bits per store | Checksums that differ between configurations doing the same work | Integer ticks and Q16.16 fixed point (`04`; `05` C2) |
| Expiring an item without `MarkAsGarbage()` | Until the next GC its object still exists, so a weak reference to it still resolves | Probe lookups that find expired items | Expiry calls `MarkAsGarbage()`; a weak pointer then fails at once (`WeakObjectPtr.h:214–218`) |
| Strong references in the lookup probe | A `TObjectPtr` keeps the expired object alive, so the GC can't collect it | Garbage that never goes away; a growing UObject count | The probe uses `TWeakObjectPtr`, not as a `UPROPERTY` (`05` C5) |
| A long GC interval with a million UObjects | Garbage builds up between cycles (1% of the items per second); past 2,097,152 objects the cooked game stops with a fatal error | "Maximum number of UObjects exceeded" | *(Found in review.)* `-GCBGCFrames` is capped at 3,600 and `-GCBItems` at 1,000,000 (`05` C8) |
| A function-level `static` as a "warn once" flag | It lives for the whole process, so later runs in the same editor session stay silent | A second PIE run with the same problem logs nothing | *(Found in review.)* The flag is a member of the store (`05` C5) |

## 6. The struct store with references (S5)

| Mistake | Why it is wrong | How to spot it | Prevention in this project |
|---|---|---|---|
| Assuming a struct array is invisible to the GC | If the element holds a `TObjectPtr`, the GC walks every element of a `UPROPERTY` array (`GarbageCollection.cpp:6874–6876`) | GC time that grows with N even though the UObject count doesn't | That is configuration S1's purpose: it keeps one reference per element on purpose, so the measurement separates objects from references (`05` C6) |
| A public header that includes a header of a private dependency | By Epic's definition a private dependency is one "nothing in our public include files depend on" (`Programs/UnrealBuildTool/Configuration/Rules/ModuleRules.cs:1256–1270`). It compiles in this module, but a module that includes the header would not get the dependency's include path **[inferred]** | An include error in a module that only includes our header | The plugin became a public dependency in the step whose public header first includes it (`05` A3) |

## 7. The proposed store (S7)

| Mistake | Why it is wrong | How to spot it | Prevention in this project |
|---|---|---|---|
| Replacing an object reference with a raw pointer to "hide it from the GC" | The GC can't see it, so the object can be freed while the pointer remains | A crash or garbage values after a GC cycle | S0 stores an **index** into the definition table, which stays referenced through `UPROPERTY` arrays (`06` §27) |
| An index into a table that can change order | Every stored index silently points at a different entry after a reorder or a removal | Items using the wrong definition; a checksum that no longer matches U0's | The definitions are created once per run, in index order, and never changed (`05` C1, C6) |
