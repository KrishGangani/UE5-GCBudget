# 09 · Testing And Benchmarks: GCBudget

**Rule:** every check is recorded with its date, its exact command and its result, **including failures**. The functional and benchmark sections are filled in during Phases 5–8. Results from earlier phases are recorded as they happen.

---

## Phase 4 · Foundation checks (2026-11-21)

Every step ran through the machine-wide UE job queue (`tools\ue_queue.py` at the portfolio root), never directly. Toolchain for every build: **MSVC 14.44.35229**, Windows SDK 10.0.22621.0 (from the UBT log). The steps of each job were chained with `&&`, so a failure stops the chain:

```text
python -I -u <portfolio>\tools\ue_queue.py run --project P04 --label "<label>" --timeout 10800 --idle 1500 -- ^
  echo === F1 editor build && Tools\build.bat GCBudgetEditor Development ^
  && echo === F2 game build && Tools\build.bat GCBudget Development ^
  && echo === F3 uncooked smoke && Tools\smoke_game.bat uncooked ^
  && echo === F4 package && Tools\package.bat Development ^
  && echo === F5 packaged smoke && Tools\smoke_game.bat packaged
```

| Queue job | What changed before it | Steps |
|---|---|---|
| 1 · `20261121_010904_700626` | — (first build) | F1–F5; failed at F5 |
| 2 · `20261121_012404_061358` | `DefaultInput.ini` added; `smoke_game.bat` fixed | F3–F5 (`--timeout 7200`) |
| 3 · `20261121_015033_166381` | Android File Server plugin disabled in the `.uproject`, its engine-written ini section deleted (a portfolio rule) | F1–F5: **the final state** |

### Final results (job 3, the committed configuration)

| # | Check | Command (from `P04_GCBudget\`) | Result |
|---|---|---|---|
| F-1 | Editor target builds | `Tools\build.bat GCBudgetEditor Development` | ✅ Succeeded in 2.51 s. UBT re-created the makefile (".uproject file is newer"); 1 action (target metadata). **0 errors, 0 warnings**. The full compile was F-1a (below) |
| F-2 | Game target builds | `Tools\build.bat GCBudget Development` | ✅ Succeeded in 10.02 s; 2 actions (relink `GCBudget.exe` for the changed plugin set, metadata); **0 errors, 0 warnings** |
| F-3c | Uncooked game loads the map and quits | `Tools\smoke_game.bat uncooked` | ✅ Exit 0. `GCBudgetCore` mounted, both modules loaded before `LoadMap`; `Template_Default` in 0.040 s. **0 errors.** Warnings: 21 × `LogEditorDataStorageUI` only (engine-side, from the editor binary in `-game`; the same in the reference project). **`DefaultEngine.ini` unchanged afterwards** (no Android File Server section) |
| F-4c | Package a Development build | `Tools\package.bat` | ✅ BUILD SUCCESSFUL; BuildCookRun 58.31 s; **516 packages cooked** (523 total, 7 skipped by platform); cook "Success - 0 error(s), 0 warning(s)"; 0 warnings in the UAT output. Output `Build\Windows\GCBudget.exe` (git-ignored) |
| F-5c | **Packaged smoke run on the GPU** | `Tools\smoke_game.bat packaged` | ✅ Exit 0. D3D12, SM6; **chosen adapter 0 = NVIDIA GeForce RTX 5070 Laptop GPU** (driver 610.47), not the integrated Intel GPU; 1280×720 windowed; `Template_Default` in 0.061 s; **0 errors, 0 warnings** |

### Earlier attempts (kept, because failures are part of the record)

| # | Job | Result |
|---|---|---|
| F-1a | 1 | ✅ Editor target: full compile, 12 actions, 56.71 s, **0 errors, 0 warnings** (`UnrealEditor-GCBudget.dll`, `UnrealEditor-GCBudgetCore.dll`) |
| F-2a | 1 | ✅ Game target: full compile, 24 actions, 37.56 s, **0 errors, 0 warnings** |
| F-3a | 1 | ⚠️ Exit 0, 0 errors, but **1 × `LogEnhancedInput` warning** ("does not have a valid PlayerInput object"). Cause: no `DefaultInput.ini`, so the classic input classes were used → fixed (`05` A8). This run also let the engine's Android File Server editor module write a section with a random `SecurityToken` into `DefaultEngine.ini` → removed before any commit, plugin disabled (`05` A1, A7). The log itself went to the wrong folder because of the bug found in F-5a |
| F-4a | 1 | ✅ BUILD SUCCESSFUL, BuildCookRun 65.77 s, 516 packages, 0 warnings |
| F-5a | 1 | ❌ **Exit 3**, "The system cannot find the path specified." **Script bug:** `shift` also shifts `%0`, so `%~dp0` after it pointed at the queue's working directory, not `Tools\` → fixed by capturing the folder first (`05` D1, `07` §1). The same bug had sent F-3a's log one folder above the repository; that stray folder was deleted and its log kept in the project's git-ignored `Saved\Logs\` |
| F-3b | 2 | ✅ Exit 0; the `LogEnhancedInput` warning is gone ("Enhanced Input local player subsystem has initialized the user settings!"); only the 21 engine-side warnings remain |
| F-4b | 2 | ✅ BUILD SUCCESSFUL, BuildCookRun 63.32 s, 516 packages, 0 warnings |
| F-5b | 2 | ✅ Exit 0, RTX 5070 Laptop adapter 0, **0 errors, 0 warnings** |

### What the smoke logs show about GC (evidence used in `01b` #13)

| Line | Uncooked (editor binary, F-3c) | Packaged Development (F-5c) |
|---|---|---|
| "Presizing for max … objects, including … objects not considered by GC" | 25,165,824 objects, **0** not considered by GC | **2,097,152** objects (the cooked default, `01b` §3), 1 not considered by GC (`BaseEngine.ini`'s placeholder value) |
| "… objects as part of root set at end of initial load" | 48,318 | 34,895 |
| "CloseDisregardForGC: …" | **0/0** objects in the disregard-for-GC pool | **34,895/34,895** objects in the pool |

So in the packaged build every object loaded at start-up is excluded from GC work, while in the uncooked game none are. That is a measured confirmation that GC numbers from the editor binary don't describe a shipped game. (Disabling the Android File Server plugin removed 7 objects from both counts; job 2's runs showed 48,325 and 34,902.)

**What Phase 4 taught us:**
1. **Batch `shift` shifts `%0` too.** Capture `%~dp0` first. A path bug in a helper script can write files outside the repository, which is why every output path is now built from one captured variable.
2. **Enhanced Input needs its two classes set in the project.** The C++ defaults are the classic classes; the template's `DefaultInput.ini` is what selects Enhanced Input.
3. **The first editor-binary run can write secrets into config.** Diff `Config\` after it; disable plugins the project doesn't use.
4. Chaining the steps in one queue job is efficient, but one failing step means re-queueing. The retries repeated only what each fix affected, and the final job re-ran everything once, so the table above describes the committed state.

## Launcher-engine limits that affect this project (checked in Phase 2 and Phase 4)

| Limit | Evidence | Consequence |
|---|---|---|
| No Test configuration | Portfolio fact ([research/04](Research/04_local_engine_verification.md) §0b) | Headline builds are packaged Development |
| GC pointer checks (`ENABLE_GC_OBJECT_CHECKS`, "roughly 20%" per Epic's comment) are compiled into Development | `Runtime/CoreUObject/Public/UObject/GarbageCollection.h:30–33` | Absolute GC times are higher than Shipping; relative comparisons are fair. An optional Shipping cross-check (our own file output, since Shipping has no CSV profiler) is planned for Phase 8. Packaging Shipping has **not** been tried yet **[unverified]** |
| `CSV_TRACK_UOBJECT_COUNT` = 0 in the precompiled engine | `Runtime/CoreUObject/Public/UObject/UObjectStats.h:9–10` | Our own CSV stat for the UObject count |
| Engine source can't be changed | Launcher build | Everything is project- or plugin-level; the GC call is bracketed with public delegates, not modified (`01b` #14) |

---

## Phase 5 · Implementation

Every build and game run goes through the job queue; the steps of one job are chained with `&&`, so a failure stops the chain. Paths are relative to `P04_GCBudget\`.

### S1 · Telemetry and director, E0 only (2026-11-21)

| Queue job | What changed before it | Outcome |
|---|---|---|
| 4 · `20261121_030143_118795` | First S1 and S2 code | Builds, package and the E0 batch `2026-11-21_031914_s1e0` ✅. The traced run `2026-11-21_032015_s1trace` ❌ wrote **no trace** (S2 section) and exited 777003, so the chain stopped before the bracket validation |
| 5 · `20261121_032950_9d9f11` | Every fix in the audit trail below | Builds, package and the E0 batch `2026-11-21_035628_s1e0b` ✅; the traced run `2026-11-21_035728_s1traceb` wrote its trace and exited 777003 again (S2 section). **The committed state** |

S1 has no automation tests. Its checks are packaged runs, measured with the S2 tools (committed in the next step):

| # | Check | Command | Result (job 5) |
|---|---|---|---|
| S1-1 | Editor target builds | `Tools\build.bat GCBudgetEditor Development` | ✅ 8 actions (3 compiles, 4 links, metadata), 9.90 s, **0 errors, 0 warnings** |
| S1-2 | Game target builds | `Tools\build.bat GCBudget Development` | ✅ 5 actions, 17.32 s, **0 errors, 0 warnings** |
| S1-3 | Package a Development build | `Tools\package.bat Development` | ✅ BUILD SUCCESSFUL, BuildCookRun 57.38 s, cook "0 error(s), 0 warning(s)" |
| S1-4 | E0: cadence, cycle count, pause, counters, GC settings | `python -I -u Tools\run_bench.py --configs E0 --items 0 --runs 3 --warmup 5 --capture 20 --label s1e0b`, then `python -I Tools\check_batch.py 2026-11-21_035628_s1e0b` | ✅ in all 4 runs (priming included): 1,200 captured frames; **10 cycle starts, exactly 120 frames apart, on the director's trigger frames**; the engine's own `GC/Count` also 10; `CyclePauseMs` > 0 on every cycle frame and 0 on every other; the run report's 12 requests = 12 cycles inside the bracket, **0 outside**; `gc.VerifyAssumptions` false and the timer at 3600 in the log; 0 log errors, 0 warnings; UObjects constant at 35,501 |
| S1-5 | The same on the traced run | `python -I Tools\check_batch.py 2026-11-21_035728_s1traceb` | ✅ the same counts and settings; the trace file exists |
| S1-6 | Bracket vs Insights' `ConditionalCollectGarbage` timer | `python -I -u Tools\validate_bracket.py --label s1traceb`, in its own queue job | ✅ per captured cycle, bracket − floor = Insights' timer for the same frame, **±0.008 ms** (S2-6) |

(The run report counts the whole run, warm-up included: 12 requests in 1,501 frames. The CSV holds only the 20 s capture: 10 cycles.)

**E0 in numbers.** These describe the empty project with 20 s captures; they are checks of the instrument, not results. Each cell is the median across the 3 runs, then [min–max]:

| Batch | Cycle pause p95 / max (ms) | GC frame ms p95 | Floor (ms) | GameThreadTime median (ms) | UObjects |
|---|---|---|---|---|---|
| `s1e0` (job 4) | 2.623 [2.008–2.740] / 2.713 [2.652–2.759] | 1.920 [1.887–2.408] | 0.028 [0.026–0.029] | 1.273 [1.246–1.276] | 35,501 |
| `s1e0b` (job 5) | 1.089 [0.996–2.762] / 1.152 [1.068–2.904] | 1.072 [0.997–1.839] | 0.028 [0.027–0.030] | 1.278 [1.251–1.287] | 35,501 |

**An observation, not explained yet.** The cycle pauses fall into two groups. Of the 100 cycles in the 10 E0 runs of jobs 4 and 5 (priming and traced runs included), 89 took 0.5–1.2 ms and 10 took 2.6–2.9 ms (one 1.9 ms). The slow ones came one or two per run, in 6 of the 10 runs, at no fixed position. With only 10 cycles per capture, one slow cycle decides the p95, which is why the two batches' p95 differ so much. The cause is **[unverified]**. The baseline and Phase 8 batches use 60 s captures (30 cycles), and Phase 8's diagnostics batch (`LogGarbage` breakdown) is where a slow cycle gets taken apart.

**Audit trail (S1 code):**
1. **Review** (the code-review skill at medium, over the whole S1+S2 change): 10 findings, all fixed before job 5. Five concern S1:
   - The telemetry counted **every** GC cycle, including those started outside its bracket (map load, another world's tick). Now only a cycle that starts while the bracket is open counts; the others are counted separately and reported (`05` B2).
   - The director parked the GC timer and **never restored it**. Every change now goes through `SetGCVariable`, and `Deinitialize` restores them all (`05` C8).
   - A comment claimed the bracket contains no other variable work. The class comment now lists what else runs inside it (`05` B1).
   - With telemetry switched off, a stale bracket start could be published when it was switched on again. The disabled path now resets the frame (`05` B2).
   - The run tag went into the JSON report unescaped (`05` C8).
2. **Clean-up pass** (a structured self-review; this project uses no sub-agents): no change to the S1 code.
3. **Found by reading the patched code before job 5:** the script that applied a review fix put a real line break inside a `TEXT("…")` literal, which would not have compiled (`07` §3).
4. **Found by the S3–S5 review** (code-review, medium), and fixed before job 5: the first escaper, `FString::ReplaceCharWithEscapedChar()`, emits `\'`, which JSON does not allow (`Runtime/Core/Public/Containers/UnrealString.h.inl:1881`). It is now a few explicit lines (`05` C8).

### S2 · Measurement tools (2026-11-21)

| # | Check | Command | Result |
|---|---|---|---|
| S2-1 | The statistics against hand-worked values | `python -I Tools\csv_stats.py --selftest` | ✅ "selftest: OK (percentiles, summary, event parsing and GC metrics match hand-worked values)", exit 0 |
| S2-2 | The runner's command lines without running anything | `python -I Tools\run_bench.py --configs E0 --items 0 --runs 1 --label drycheck --dry-run` | ✅ two commands (priming, then run 1) for the real exe `Build\Windows\GCBudget\Binaries\Win64\GCBudget.exe`, each with `-traceautostart=0`; no file written |
| S2-3 | One E0 batch end to end | job 5, S1-4 | ✅ 4 runs, each with its CSV, run report and log in `Benchmarks\Raw`; manifest and summary written. The environment record has `cpu_affinity` "0xFFFF (performance cores only; hybrid CPU)" read back from Windows, AC power, the Turbo power plan |
| S2-4 | A traced run writes a trace | job 4, then job 5 | ❌ job 4: no `.utrace` at all, because `-traceautostart=0` also blocks a trace requested with `-trace=`/`-tracefile=` (`Runtime/Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:2017–2021`; `07` §3). Fixed: traced runs leave it out (`05` D4). ✅ job 5: "Trace started (writing to file …)", a 40,960,423-byte trace. That run still exited 777003 (below) |
| S2-5 | The batch checker | `python -I Tools\check_batch.py <batch>` on `2026-11-21_031914_s1e0`, `2026-11-21_035628_s1e0b`, `2026-11-21_035728_s1traceb`, and a batch that doesn't exist | ✅ "all runs pass", exit 0, for the three batches; exit 2 with a message for the missing one |
| S2-6 | Bracket validation | `python -I -u Tools\validate_bracket.py --label s1traceb` (its own queue job); after the comparison was replaced, `… --label s1traceb --reuse-export` (no Unreal process) | ❌ job 6 `20261121_035916_18e2b4`: Insights analysed the trace and logged "Exported 1503 timing events", but no file appeared: the command reads its file name with escapes on, so every backslash vanished and the path became drive-relative (`D:UNREAL PORTFOLIOP04…`). No such file was found on D: (seven levels deep) or in the usual C: folders, so it was probably never written **[unverified]**. Fixed: forward slashes (`05` D6). ✅ job 7 `20261121_045731_fefa5f`: export written, 1,503 events. Its distribution comparison treated every event over 10× the median as a cycle frame and found 22 for 12 cycles: the purge frames two frames after each cycle passed the threshold too. **Replaced by a frame-matched comparison** and rerun on the same export: both alignments hold (the trace's largest residue is 119, i.e. frames 120, 240, … 1440; every CSV cycle row lies on a multiple of 120). Over the 10 captured cycles (0.60–0.98 ms) **bracket − floor − Insights = median +0.002 ms, at most ±0.008 ms**. `CyclePauseMs` reads 0.006 ms [0.0057–0.0084] less than Insights. On the 1,190 other frames, bracket − Insights = median 0.027 ms (the floor is 0.029 ms), p95 0.044, p99 0.147, max 0.591 ms: the non-GC work in the bracket is small and steady, with rare outliers, and one landing on a cycle frame would inflate that cycle **[inferred]** |

**Exit code 777003 on traced runs: the evidence.**
- Both traced runs (jobs 4 and 5) exited with 777003. Every untraced run before and after them exited with 0.
- In job 5 tracing really started, the run completed its capture and its report, and the log ends normally ("LogExit: Exiting.", "Log file closed"). The process then ended with 777003. There is no crash folder and no dump.
- 777003 is `ECrashExitCodes::CrashReporterCrashed` (`Runtime/Core/Public/GenericPlatform/GenericPlatformCrashContext.h:45`). On Windows the crash-reporting thread exits with it when it catches an exception **in its own loop** (`Runtime/Core/Private/Windows/WindowsPlatformCrashContext.cpp:1300–1345`).
- So something crashed during shutdown, after the log had closed, and only with tracing on **[inferred]**. The cause is **not found [unverified]**.
- **Decision** (the coordinator's rule: at most one retry): no further traced reruns. The trace that job 5 wrote is validated in a separate short job, and traced runs never share a chain with untraced batches again. The runner keeps treating any non-zero exit as a failed run, so a traced run is never used as measurement data.

**Audit trail (S2 code):**
1. **Review** (the code-review skill at medium, over the whole S1+S2 change): six of the ten findings concern S2, all fixed before job 5:
   - `validate_bracket.py` could read a stale export from an earlier run. It now deletes it first and requires a file newer than the launch (`05` D6).
   - It matched trace events to frames with fixed 120-event windows, which start-up frames would have misaligned. Cycle frames are now found by duration (`05` D6).
   - It compared frames whose bracket never opened (`BracketMs` −1). They are skipped now (`05` D6).
   - `csv_stats.py` counted a cycle cut off by the **end** of the capture as complete. Such cycles are partial now (`05` D3).
   - `--label` was not validated, although it becomes part of file names and `-GCBRunTag` (`05` D4).
   - `--label` in `validate_bracket.py` matched as a substring, so `E0` could match a configuration name. It now matches the batch label exactly (`05` D6).
2. **Clean-up pass** (structured self-review): `frame_events` found the EVENTS column once per frame; it is now found once per capture. A lambda in the summary became a named nested function (`05` D3, D4).
3. **`check_batch.py`** (added after job 5 from a throwaway checker, so that S1-4 has a committed command): a structured self-review fixed a membership test that rebuilt a set on every frame, and made a missing report or log a failed run instead of a traceback (`05` D5).

### S3 · Handles and their tests, plugin only (2026-11-21)

S3 (plugin) and S4 (game module) were built and checked in **one** queue job and are committed separately (`03`, Changes): the two modules compile independently, so each commit's code was compiled exactly as committed.

| Queue job | What changed before it | Outcome |
|---|---|---|
| 8 · `20261121_040416_88b112` | S3 and S4 in the tree | ❌ failed in 0.0 s at its first step, a gate `dir <file> >nul` that reached `cmd` as `>/dev/null` (`07` §1). Nothing was built |
| 9 · `20261121_045731_a270b9` | The gate became `type <file>` | ✅ every step. **The committed state of S3 and S4** |

| # | Check | Command | Result (job 9) |
|---|---|---|---|
| S3-1 | Editor target builds (S3 + S4) | `Tools\build.bat GCBudgetEditor Development` | ✅ 13 actions (8 compiles), 13.30 s, **0 errors, 0 warnings** |
| S3-2 | Game target builds (S3 + S4) | `Tools\build.bat GCBudget Development` | ✅ 10 actions (8 compiles), 15.81 s, **0 errors, 0 warnings** |
| S3-3 | The handle tests | `Tools\run_tests.bat GCBudget` | ✅ "Found 6 automation tests based on 'GCBudget'"; all six `Result={Success}` (`DefaultIsInvalid`, `AllocateResolve`, `ReleaseMakesStale`, `LifoReuseNewGeneration`, `Reset`, `RandomAgainstModel`: 1,000,000 operations, 0 mismatches); "TEST COMPLETE. EXIT CODE: 0" |

**Audit trail (S3 code):** the review (the code-review skill at medium, over all of the staged S3–S5 code before the first build) found nothing in the S3 code. The clean-up pass changed nothing in it either.

### S4 · The workload and the UObject stores, U0 / U1 / U2 (2026-11-21)

Built and checked in job 9 with S3 (above). Functional checks use 5 s of warm-up and 20 s of capture; they are checks, not measurements.

| # | Check | Command | Result (job 9) |
|---|---|---|---|
| S4-1 | Package a Development build | `Tools\package.bat Development` | ✅ BUILD SUCCESSFUL, BuildCookRun 59.69 s, cook "0 error(s), 0 warning(s)" |
| S4-2 | Determinism: the same seed twice, U0 = U1 = U2 (SC7) | `python -I -u Tools\run_bench.py --configs E0,U0,U1,U2 --items 10000 --runs 2 --warmup 5 --capture 20 --label s4det` | ✅ all 9 runs exit 0. All six U runs (N = 10,000, seed 1, 1,501 frames) report checksum **0x83C178D5**, and 2,733 expiries each. The summary's checksum table says "yes". (2 runs per configuration: a check, so the runner's rotation warning applies) |
| S4-3 | A different seed does different work | `… --configs U0 --items 10000 --runs 1 --no-prime --seed 2 --warmup 5 --capture 20 --label s4seed2` | ✅ checksum **0x51DA406A**, 2,755 expiries |
| S4-4 | Cadence and counters in every item configuration | `python -I Tools\check_batch.py <batch>` for `2026-11-21_052508_s4det`, `2026-11-21_052719_s4seed2`, `2026-11-21_052736_s4verifygc` | ✅ "all runs pass": 10 cycle starts 120 frames apart on the trigger frames, `GC/Count` 10, 12 requests = 12 cycles in the bracket, 0 outside, 0 log errors |
| S4-5 | UObject count = E0 + N + pages + store, and expired items are collected | the same, plus the minimum after every cycle | ✅ E0: **35,528**, constant. U0, U1, U2 at 10,000: the post-cycle minimum is **45,539–45,544** in all ten cycles of every run; the formula gives 35,528 + 10,000 + 10 pages + 1 store = **45,539**. The maximum, 45,766, is expired items waiting for the next cycle (≈ 1% per second × 2 s × 10,000 = 200). The count doesn't grow, in U1 too, so the rebuild before each cycle really frees the expired items. At 100,000 (U1): minima 135,638–135,663, against a formula of 135,627; the 11–36 extra are probably items that expired during the purge frames (about 17 per frame at this N) **[inferred]**. E0 is 27 higher than in S1 (35,501): the 16 definitions plus 11, which matches five new classes × (class object + default object) plus the handle's struct type **[inferred]** |
| S4-6 | U1 builds one cluster per page | the run reports' `clusters_rebuilt` | ✅ 120 = 10 pages × 12 cycles at 10,000; 1,176 = 98 × 12 at 100,000. Every page changed before every cycle, as `02` §7's caveat predicted |
| S4-7 | `-VERIFYGC` finds no cluster-assumption errors | `… --configs U1 --items 10000,100000 --runs 1 --no-prime --verify-gc --warmup 5 --capture 20 --label s4verifygc` | ✅ both logs read `gc.VerifyAssumptions = true`; 0 errors, 0 warnings; the 10,000 run's checksum equals the unverified runs' (0x83C178D5) |
| S4-8 | Lookup probe: 0 errors | the run reports | ✅ 1,500,000 lookups per run (1,000 × 1,500 frames), **0 errors**; 96% resolved, the other 4% correctly rejected as stale (59,451 at seed 1) |

**In numbers** (20 s captures; checks, not results): the median cycle pause per run was E0 0.81–0.84 ms (the priming run 0.87), U0 0.92–1.19, U1 1.01–1.13 and U2 1.19–1.28 at 10,000 items. One U0 run and both U2 runs had a single cycle near 3 ms (2.98–3.22), like the occasional slow E0 cycles in S1 (cause still **[unverified]**). With `-VERIFYGC`, U1 rose to 2.9 ms at 10,000 and 12.9 ms at 100,000. That is the verification's own cost, and the reason timed runs use `-NoVerifyGC` (`01b` #11). Populating 10,000 items took 11–20 ms; 100,000 took 179 ms (with verification on).

**Audit trail (S4 code):**
1. **Review** (the code-review skill at medium, over the staged S3–S5 code before the first build). Seven findings; each engine claim was checked in the source before the fix:
   - *Important:* in U1, an item created after its page's cluster was built belongs to no cluster. The GC never traces a cluster root's own references (`GarbageCollection.cpp:3130–3139`), so any cycle that didn't follow a rebuild (the engine timer, or a request deferred by a running purge) would free live items while their page still pointed at them. Fixed: every replacement item joins its page's cluster at once (`AddToCluster`, `UObjectClusters.cpp:877–900`), and U1 rejects `-GCBGCFrames=0` (`05` C5, C8; `02` §14).
   - *Important:* the item limit alone didn't bound the object count, because garbage builds up between cycles. Fixed: `-GCBGCFrames` ≤ 3,600, which keeps U0 near 1.65 million objects, below 2,097,152 (`UObjectBase.cpp:1363`; `05` C8).
   - `clusters_rebuilt` counted pages whose cluster `CreateCluster` had freed as too small (`UObjectClusters.cpp:958–995`). Fixed: only real clusters are counted (`05` C5).
   - A second `Populate` kept the old pages and probe references. Fixed: both are reset (`05` C5).
   - The "clusters disabled" error was logged once per process. Fixed: once per store (`05` C5).
   - The run tag's JSON escaping (S1 code): fixed in S1 (`09` S1).
   - A comment in `GCBudget.Build.cs` would become wrong once S5's public header includes a plugin header: handled in S5.
2. **Clean-up pass** (structured self-review): `Populate` didn't restart the probe cursors, so a second `Populate` with fewer lanes could leave one past the end of the new pool; both are reset now (`05` C4). The probe references are reserved once; the runner builds the checksum groups once per batch.

### S5 · The struct store that still holds references, configuration S1 (2026-11-21)

S5 was built and checked in the same queue job as the S6 baselines (S6 adds no code).

| Queue job | What changed before it | Outcome |
|---|---|---|
| 10 · `20261121_041046_7c3358` | S5 staged, not yet in the tree; queued early with a gate | ❌ failed in 0.0 s at the same `>nul` gate as job 8 (`07` §1). Nothing was built |
| 11 · `20261121_053330_44cef8` | S5 in the tree; the gate became `type <file>` | ✅ every step, 1,812 s. **The committed state of S5**, and the S6 baselines |

| # | Check | Command | Result (job 11) |
|---|---|---|---|
| S5-1 | Editor target builds | `Tools\build.bat GCBudgetEditor Development` | ✅ 14 actions, 12.23 s, **0 errors, 0 warnings** |
| S5-2 | Game target builds | `Tools\build.bat GCBudget Development` | ✅ 10 actions, 16.32 s, **0 errors, 0 warnings** |
| S5-3 | The handle tests still pass | `Tools\run_tests.bat GCBudget` | ✅ 6 of 6 `Result={Success}`, "TEST COMPLETE. EXIT CODE: 0" |
| S5-4 | Package | `Tools\package.bat Development` | ✅ BUILD SUCCESSFUL, BuildCookRun 58.63 s, cook "0 error(s), 0 warning(s)" |
| S5-5 | S1's checksum equals U0's | `python -I -u Tools\run_bench.py --configs U0,S1 --items 10000 --runs 2 --warmup 5 --capture 20 --label s5check` | ✅ all runs exit 0; **S1 0x83C178D5 = U0 0x83C178D5** (seed 1, 1,501 frames), the same value as S4's U runs. The 60 s baselines (S6) also agree at every N |
| S5-6 | UObject count = E0 + store; probe errors 0 | `python -I Tools\check_batch.py 2026-11-21_062953_s5check`, and the run reports | ✅ "all runs pass". S1 at 10,000: **35,534, constant**: E0 of this build (35,533, S6 Batch B) + 1 store. Probe: 1,500,000 lookups, 0 errors. E0 rose by 5 from S4 (35,528), which matches two new classes × (class object + default object) plus S1's struct type **[inferred]** |

**Audit trail (S5 code):** the S3–S5 review (S4 section) found two things in the staged S5 code: a second `Populate` kept the old probe handles (now reset, `05` C6), and the `Build.cs` comment that S5 would make wrong (the plugin is now a public dependency, `05` A3). A structured self-review of the director's S5 change found nothing to change.

### S6 · Baselines before the proposed store exists (2026-11-21)

Both batches ran in job 11 (S5 section), on the S5 build: packaged Development, fixed step, the runner pinned to the performance cores, 10 s of warm-up, **60 s of capture (30 cycles)**, one priming run each, interleaved with a rotating order. These are the **first data** for H1, H2, H4, H5 and H6, recorded before S0's code exists (`03`). They are not the results: those come from Phase 8's full batches, with S0.

- **Batch A** `2026-11-21_063108_s6batchA`: `python -I -u Tools\run_bench.py --configs U0,U1,U2,S1 --items 10000,100000 --label s6batchA` (4 runs per combination, the protocol's default for 4 configurations).
- **Batch B** `2026-11-21_064925_s6batchB`: `python -I -u Tools\run_bench.py --configs U0,S1,E0 --items 1000,10000,30000,100000 --runs 2 --label s6batchB`. E0 is the N = 0 point, so the batch has 3 configurations and 2 runs, and the runner warns that the rotation is unbalanced.
- Both: `python -I Tools\check_batch.py <batch>` → "all runs pass" (30 cycles 120 frames apart on the trigger frames, `GC/Count` 30, 35 requests = 35 cycles in the bracket, 0 outside, 0 log errors). **The checksums agree across every configuration at every N** (the summaries' SC7 tables). Probe errors: 0 in every run.

Each cell below is the median across runs of each run's statistic, then [min–max across runs], from the batch summaries.

**Batch A**

| Config | N | Cycle pause p95 / max (ms) | GC frame ms p95 | Spikes / min | Hitches / min | UObjects | Update ns / item | Probe ns / lookup |
|---|---|---|---|---|---|---|---|---|
| S1 | 10,000 | 1.094 [1.052–1.982] / 1.269 [1.068–2.669] | 1.028 [0.991–1.037] | 0 | 0 | 35,534 | 2.70 [2.67–2.74] | 11.15 [10.80–11.40] |
| U0 | 10,000 | 1.340 [1.257–1.364] / 1.452 [1.301–1.635] | 1.194 [1.182–1.267] | 0 | 0 | 45,653 | 10.32 [10.21–10.34] | 30.20 [30.00–30.60] |
| U1 | 10,000 | 1.460 [1.399–1.519] / 1.537 [1.431–1.608] | 1.379 [1.351–1.407] | 0 | 0 | 45,653 | 11.33 [10.96–11.65] | 30.68 [30.30–31.40] |
| U2 | 10,000 | 1.372 [1.308–1.499] / 1.487 [1.439–1.585] | 1.301 [1.285–1.324] | 0 | 0 | 45,653 | 10.35 [10.30–10.81] | 30.35 [30.20–30.80] |
| S1 | 100,000 | 1.981 [1.875–2.017] / 2.118 [2.084–2.373] | 1.819 [1.757–1.874] | 0 | 0 | 35,534 | 2.55 [2.53–2.66] | 31.95 [31.70–33.60] |
| U0 | 100,000 | 4.464 [4.302–4.701] / 4.848 [4.612–5.482] | 3.811 [3.713–4.098] | 0.00 [0.00–2.00] | 0 | 136,723 | 10.12 [9.82–10.24] | 40.70 [40.30–41.70] |
| U1 | 100,000 | 3.097 [3.032–3.179] / 3.311 [3.062–3.610] | 2.969 [2.926–3.015] | 0 | **29.00 [29.00–29.00]** | 136,728 | 9.96 [9.66–11.10] | 39.75 [38.80–43.20] |
| U2 | 100,000 | 4.859 [3.381–5.119] / 5.119 [3.877–5.586] | 3.886 [3.296–4.667] | 1.50 [0.00–3.00] | 0 | 136,742 | 10.04 [9.73–10.57] | 40.70 [39.80–42.90] |

U1's cluster rebuild, per request frame (`check_batch.py`, the per-run median and max of `ClusterMs` on the request frames): **14.8–15.4 ms** (max 16.3–18.5) at 10,000; **1,377–1,391 ms** (max 1,459–1,545) at 100,000.

**Batch B** (U0 and S1; E0 is the N = 0 row)

| N | S1 pause p95 / max (ms) | U0 pause p95 / max (ms) | UObjects S1 / U0 | Update ns / item S1 / U0 | Probe ns / lookup S1 / U0 |
|---|---|---|---|---|---|
| 0 (E0) | 1.031 [0.958–1.104] / 1.179 [1.115–1.244] | (the same run) | 35,533 | — | — |
| 1,000 | 1.013 [0.989–1.036] / 1.046 | 1.046 [0.989–1.102] / 1.089 | 35,534 / 36,546 | 3.55 / 10.90 | 4.35 / 9.05 |
| 10,000 | 1.073 [1.049–1.097] / 1.095 | 1.233 [1.158–1.308] / 1.332 | 35,534 / 45,653 | 2.69 / 10.37 | 11.05 / 30.55 |
| 30,000 | 1.217 [1.201–1.233] / 1.243 | 2.018 [1.957–2.078] / 2.143 | 35,534 / 65,887 | 2.54 / 9.88 | 19.30 / 37.85 |
| 100,000 | 1.910 [1.856–1.965] / 2.061 | 4.011 [3.934–4.089] / 4.301 | 35,534 / 136,723 | 2.55 / 9.75 | 31.80 / 39.95 |

**What the data says, without the verdicts Phase 8 will give:**
- **U0's cycle pause grows with N**: p95 about 1.0 ms up to 1,000 items, 4.0–4.5 ms at 100,000. **S1's** grows too, but to about 1.9–2.0 ms. Both keep one definition reference per item. That a large part of U0's extra cost is per *object* rather than per *reference* is a reading of these numbers **[inferred]**; S0 (no reference at all) is the next data point.
- **U1 trades GC time for maintenance.** At 100,000 its cycle pause is lower than U0's (p95 3.10 against 4.46 ms). But rebuilding every changed page before every cycle (and every page changes, as `02` §7 warned) takes about **1.38 s per cycle** in the director's tick, outside the GC bracket. That is a 1.3-second stall on every cycle, every 2 s (29 hitches per minute as the capture can count them, caveat 1). Per page the rebuild costs about 1.5 ms at 10,000 and about 14 ms at 100,000, so the per-page cost grows with N. The cause is **[unverified]**, a Phase 8 diagnostics item.
- **U2 doesn't lower the pause at 100,000** (p95 4.86 [3.38–5.12] against 4.46 ms) and shows 1.5 spikes per minute. Why is **[unverified]**: Phase 8 checks with `LogGarbage` whether reachability actually ran in slices.
- **Per-item work:** about 2.5–2.7 ns per item in the struct store against about 10 ns per UObject. Lookups: 4–32 ns through handles, 9–41 ns through weak pointers. Both grow with N, plausibly from cache effects **[inferred]**.
- **Memory:** `PhysicalUsedMB` stays within 1,271–1,313 MB in every combination, inside the run-to-run spread at these N. It isn't interpretable here.

**Caveats found while reading the baselines:**
1. **A CSV row's FrameTime is the previous frame's duration.** In U1's captures the request row shows GameThreadTime and ClusterMs of about 1.26 s and FrameTime of 4 ms; the next row shows FrameTime 1.27 s. The engine confirms it: by default (`csv.UseLegacyFrameTime` 0) FrameTime is measured at the start of a frame, after the frame-rate wait, for the frame before (`Runtime/Core/Private/ProfilingDebugging/CsvProfiler.cpp:150–155, 4369–4390`). The first hitch rule (FrameTime > 33.3 ms on a GC frame) therefore looked at the wrong frame. It now uses a GC frame's **own** duration, the next row's FrameTime (`05` D3), and the S5/S6 manifests and summaries were **re-analysed from their raw captures** (`python -I Tools\run_bench.py --resummarize <batch> --reanalyse`). Only one number changed: U1 at 100,000 went from 30 to **29** hitches per run. Both ends of the capture explain the difference. Its first row (frame 601) is a purge frame carrying frame 600's 1,221 ms, a request made just before the capture, which the old rule counted. Its last row (frame 4200) is a cycle start whose own duration lies after the capture, which the new rule can't see. Every one of the 30 captured cycles stalled. In the S1–S4 batches the only frames over 33.3 ms are in `2026-11-21_052736_s4verifygc` (U1 at 100,000 with verification on), and both rules give it 10 hitches, so those committed records stand. `check_batch.py` reports GameThreadTime on request frames for the same reason.
2. **A percentile of a sparse column is 0.** The first summaries showed "Cluster ms p95" = 0.000 everywhere, because `ClusterMs` is non-zero on one frame in 120. The column is now the maximum (U1 at 100,000: 1,466.6 [1,458.6–1,544.9] ms). The rewritten summaries come from the same re-analysis; no run was repeated.

**Audit trail (S6, tools only):** the three fixes above: the hitch rule in `csv_stats.py` (its self-test now places the long frame's duration in the next row, and fails under the old rule); the summary's cluster column, `--resummarize` and `--reanalyse` in `run_bench.py`; request-frame `ClusterMs` and GameThreadTime in `check_batch.py`. Checked with a structured self-review, the self-test, and by rerunning the tools on the recorded batches.

### S7 · The proposed store, configuration S0 (2026-11-21)

| Queue job | What changed before it | Outcome |
|---|---|---|
| 12 · `20261121_070422_a60ec2` | S0 in the tree (after the S6 commit) | ✅ every step. **The committed state of S7** |

| # | Check | Command | Result (job 12) |
|---|---|---|---|
| S7-1 | Editor target builds | `Tools\build.bat GCBudgetEditor Development` | ✅ 6 actions, 8.47 s, **0 errors, 0 warnings** |
| S7-2 | Game target builds | `Tools\build.bat GCBudget Development` | ✅ 5 actions, 15.42 s, **0 errors, 0 warnings** |
| S7-3 | The handle tests still pass | `Tools\run_tests.bat GCBudget` | ✅ 6 of 6 `Result={Success}`, "TEST COMPLETE. EXIT CODE: 0" |
| S7-4 | Package | `Tools\package.bat Development` | ✅ BUILD SUCCESSFUL, BuildCookRun 58.97 s, cook "0 error(s), 0 warning(s)" |
| S7-5 | S0's checksum equals U0's (SC7), and S1's | `python -I -u Tools\run_bench.py --configs E0,U0,S1,S0 --items 10000,100000 --runs 2 --warmup 5 --capture 20 --label s7check` | ✅ all 15 runs exit 0. **S0 = S1 = U0** = 0x83C178D5 at 10,000 and 0xBD626A07 at 100,000 (the same values as S4's U runs), with identical expiry counts (2,733 and 27,389) |
| S7-6 | UObject count = E0 + store; probe errors 0 | `python -I Tools\check_batch.py 2026-11-21_082841_s7check`, and the run reports | ✅ "all runs pass". S0 at 10,000 and at 100,000: **35,537, constant** = E0 of this build (35,536) + 1 store, the same as S1. U0: post-cycle minimum 45,547 at 10,000 = E0 + 10,000 + 10 pages + 1 store. Probe errors 0 in every run. E0 rose by 3 from S5, which matches one new class × (class object + default object) plus S0's struct type **[inferred]** |
| S7-7 | The element sizes `04` §30 states | the director's log line, every item run | ✅ "Struct element sizes: S1 (FGCBItemDataRef) 24 bytes, S0 (FGCBItemData) 20 bytes" |
| S7-8 | A short S0 vs U0 sanity batch | the same batch | ✅ S0 runs like the others and is never slower. 20 s captures, 2 runs, so these are **checks, not results**: the median cycle pause per run at 100,000 was S0 0.74–0.81 ms, S1 1.55–1.69, U0 3.08–3.10, and E0 0.80–0.89 with no items at all. At 10 cycles per capture the p95 is set by the occasional slow cycle (2.5–2.9 ms in E0, S0 and S1 alike, as in S1) |

**Audit trail (S7 code):** a structured self-review of the change (S0's element, its two definition accessors declared before the templates that call them, its store, the director's and the runner's S0 entries, the element-size log line) found nothing to change. The shared struct-store code it relies on was reviewed in S5.

**Phase 5 is complete:** all seven steps compiled with 0 warnings through the queue, and each passed its checks. The baselines (S6) were recorded before S0 existed. The headline and curve batches with all six configurations, and the Shipping cross-check, are Phase 8's (`03`).

## Phase 6 · Explanation (2026-11-21)

Phase 6 changes no game code. It made the explanation consistent with the final code, and re-checked every engine fact.

| # | Check | Command | Result |
|---|---|---|---|
| P6-1 | `05` describes the final code, file by file | `05` rewritten from step-by-step parts into four parts by area (project, plugin, game module, tools); every `05` reference in `04`, `07` and `09` remapped to the new sections (58 references) | ✅ every tracked file in `GCBudget\` and `Tools\` has its section |
| P6-2 | Every `File:line` citation, by content | `python -I Tools\check_citations.py 0*.md Phase2_Raw_Notes\*.md` (in `Explanation\`), with no benchmark running | ✅ 518 citations resolve, exit 0. 476 passed the automatic content check. The 42 listed for a person were all read in the source: **41 correct**; **1 replaced**: `01b` cited the cluster builder (`UObjectClusters.cpp:674–687`) for "GC traverses each object's Outer and Class", and now cites the collector itself (`FastReferenceCollector.h:1017–1019`), noted in `Phase2_Raw_Notes/C`. Shorthand citations (`:lines` after a full one) are included |
| P6-3 | `06` §6 covers every switch the final scripts pass | each new row's parsing site read in the source | ✅ ten rows added (`-deterministic`, `-FPS=`, `-novsync`, the CSV events, the trace switches, `-NoVerifyGC`/`-VERIFYGC`, `-LogCmds=`, the automation `-ExecCmds=`, `-ReportExportPath=`, the project's `-GCB…` switches) |
| P6-4 | `08` written, every command checked | the parsing or definition site of each item read in the source | ✅ 08 §1–§6. Two findings: (a) the `LogGarbage` lines Phase 2 listed are `Log` level, and the extra Verbose lines add the mark phase and purge slices; the per-class and traversal counts need `PERF_DETAILED_PER_CLASS_GC_STATS`, which the launcher engine doesn't enable. (b) **`-nocrashreports` only skips ensure reports** (`AssertionMacros.cpp:607, 666`); it does not stop the crash-reporting thread that exits with 777003, so it can't test that cause. Phase 7's investigation uses `Trace.Stop` before quitting instead (`TraceAuxiliary.cpp:1856`) |
| P6-5 | Links | `python -I <portfolio>\tools\check_links.py .` | ✅ 0 problems |

**Audit trail (Phase 6):** `check_citations.py` is new; a structured self-review checked its exit codes on a doc with a missing file and on one with shorthand citations, and that it reads nothing but engine files and the docs.

## Phase 7 · Testing (2026-11-21)

**Owner direction (via the coordinator):** finish with the data already recorded. One queue job builds and runs the automation tests. No new benchmarks, no long runs, no 777003 probe. The Phase 7 job queued earlier was cancelled before it ran.

| Queue job | Steps | Outcome |
|---|---|---|
| `20261121_101742_fc0390` | `Tools\build.bat GCBudgetEditor Development`, `Tools\build.bat GCBudget Development`, `Tools\run_tests.bat GCBudget` | ✅ both targets "Result: Succeeded", **0 warnings**; "Found 11 automation tests based on 'GCBudget'", **11 of 11 `Result={Success}`**, "TEST COMPLETE. EXIT CODE: 0" |

| Test | What it proves | What it does not prove |
|---|---|---|
| `GCBudget.Handles.*` (6, from S3) | Allocation, staleness after release, LIFO reuse with a new generation, reset, 1,000,000 random operations against a model | Anything about stores or GC |
| `GCBudget.Handles.GenerationWrapsToOne` | Releasing at `MAX_int32` doesn't overflow; the generation wraps to 1, never 0; default and generation-0 handles stay invalid; **the documented limit holds**: after a full wrap, a first-life handle matches again | That 2³¹ real releases behave the same (the test hook sets the generation directly) |
| `GCBudget.Handles.GrowShrinkRegrow` | Holes are refilled first (generation 2), then slots are appended (generation 1); no released handle becomes current again | Store-level growth during a run (the benchmark stores never grow mid-run) |
| `GCBudget.Stores.StaleAfterExpiry` | For U0 (weak pointers), S1 and S0 (handles): every reference to a removed item is rejected, none resolves to its replacement, and re-pointed references resolve | U1 (clusters are not built in an editor test); behaviour across a real GC cycle (no GC runs inside the test) |
| `GCBudget.Stores.RepopulateShrinkGrow` | Repopulating to 500 and then 2,500 items gives the same count and checksum as a fresh store of that size, with no probe errors | Memory being released (only state equality is checked) |
| `GCBudget.Stores.ChecksumsAgree` | U0 = S1 = S0 checksums at 4,000 items after 1,200 frames, in the editor build | The packaged build (that is `09` S7-5) |

**Not tested, as directed:** long (600 s) runs, the object-limit run (U0 at 1,000,000 items, GC every 60 s), the three expected rejections in a packaged run, and the 777003 probe. The probe's switch (`-GCBStopTraceBeforeExit`, `05` C8) is built but **was never run**, so 777003's cause stays **unknown**, with the evidence in S2. The object limit is covered only by the arithmetic behind the director's limits (`05` C8) and by S4's 100k `-VERIFYGC` run (peak 137,929 objects).

**Incident:** during Phase 7, a 20 s offline parse of 33 baseline captures ran while another project's baseline batch held the queue (about 08:59:05–08:59:25 local; that project's batch `2026-11-21_085156_S3baseline`). The coordinator was told. `07` §1 records it.

## Results: the success criteria

**Not the protocol's final batches.** Phase 8's full matrix (balanced runs of all six configurations at all five N) was **not run**, by the owner's direction. Every number below comes from the recorded check and baseline batches. A verdict resting on S0 data comes from a 20 s, 2-run check batch and is marked *indicative*.

| SC | Target | Result | Evidence (batch) |
|---|---|---|---|
| SC1 | S0 at 100k: cycle-pause p95 ≤ E0's + 0.25 ms | ✅ **met, indicative**: S0 1.408 [0.991–1.824] ms against E0 1.916 [0.968–2.865] ms | `2026-11-21_082841_s7check` (20 s captures, 2 runs: a check batch, not the protocol's) |
| SC2 | S0's worst GC frame at 100k below the best of U0, U1 and U2 | ✅ **met, indicative**: S0 GC-frame max 1.836 [1.142–2.530] ms against U1's 3.339, U0's 4.874 and U2's 5.139 ms (the best is U1). The capture lengths differ (S0: 20 s, 2 runs; U: 60 s, 4 runs), so the ranges are not a like-for-like non-overlap test | `…_s7check`; `2026-11-21_063108_s6batchA` |
| SC3 | S0 at 100k: spikes/min ≤ E0's, 0 hitches/min | ✅ met: 0.00 spikes and 0.00 hitches per minute (E0: 0.00 / 0.00) | `…_s7check` |
| SC4 | S0 adds ≤ 32 UObjects over E0; U0 adds ≥ N | ✅ met: S0 35,537 vs E0 35,536 (+1, the store); U0 at 100k: post-cycle floor 135,647 (+100,111) | `…_s7check` |
| SC5 | S0 bytes per item ≤ 50% of U0's | **not measured**: the working set (`PhysicalUsedMB`) differs between runs by more than the configurations differ at these N. What is measured: S0's element is 20 bytes (logged), a U0 item is a full UObject plus its page slot | `…_s7check`; `09` S7-7 |
| SC6 | S0 update ≤ U0's; S0 lookup ≤ 1.5 × U0's | ✅ met: update 2.86 vs 8.46 ns/item; lookup 33.00 vs 41.05 ns (0.80 ×) at 100k | `…_s7check` |
| SC7 | 0 lookup errors; identical checksums across configurations | ✅ met: 0 probe errors in every run of every batch; U0 = U1 = U2 = S1 = S0 = 0x83C178D5 at 10k (1,501 frames), and equal across every configuration present at every N in every batch | `09` S4-2, S5-5, S6, S7-5 |
| SC8 | U0, S1 and S0 at all five N, ≥ 3 runs each; H1's slope | ❌ **not met**: U0 and S1 at 1k–100k with 2 runs each; S0 only at 10k and 100k (check batch); no slope fitted | `2026-11-21_064925_s6batchB` |

**The headline finding (H4):** at 100,000 items, U1's clusters lower the GC pause (p95 3.10 ms against U0's 4.46 ms), but rebuilding the changed pages before each cycle stalls the game thread for **about 1.4 s per cycle**. The per-run worst request frame has a median of 1,473.8 ms, against U0's 7.6 ms. In this churning workload, clustering trades a smaller pause for a far bigger hitch. U1 is the baseline as designed in `03`; it was not tuned (`11`).

**What stays unverified** (each was planned as a Phase 8 Insights drill-down, which was not run):
1. the cause of exit 777003 on traced runs;
2. the occasional 2.6–3.2 ms cycles in every configuration, E0 included;
3. why U1's per-page rebuild cost grows with N (about 1.5 ms per page at 10k, about 14 ms at 100k);
4. why U2 doesn't lower the pause at 100k.

**Not done:** the Shipping cross-check, the H1 slope fit and the demo video.
