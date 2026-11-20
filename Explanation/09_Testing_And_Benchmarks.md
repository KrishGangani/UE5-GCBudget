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
| F-5a | 1 | ❌ **Exit 3**, "The system cannot find the path specified." **Script bug:** `shift` also shifts `%0`, so `%~dp0` after it pointed at the queue's working directory, not `Tools\` → fixed by capturing the folder first (`05` A9, `07` §1). The same bug had sent F-3a's log one folder above the repository; that stray folder was deleted and its log kept in the project's git-ignored `Saved\Logs\` |
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
   - The director parked the GC timer and **never restored it**. Every change now goes through `SetGCVariable`, and `Deinitialize` restores them all (`05` B4).
   - A comment claimed the bracket contains no other variable work. The class comment now lists what else runs inside it (`05` B1).
   - With telemetry switched off, a stale bracket start could be published when it was switched on again. The disabled path now resets the frame (`05` B2).
   - The run tag went into the JSON report unescaped (`05` B4).
2. **Clean-up pass** (a structured self-review; this project uses no sub-agents): no change to the S1 code.
3. **Found by reading the patched code before job 5:** the script that applied a review fix put a real line break inside a `TEXT("…")` literal, which would not have compiled (`07` §3).
4. **Found by the S3–S5 review** (code-review, medium), and fixed before job 5: the first escaper, `FString::ReplaceCharWithEscapedChar()`, emits `\'`, which JSON does not allow (`Runtime/Core/Public/Containers/UnrealString.h.inl:1881`). It is now a few explicit lines (`05` B4).

### S2 · Measurement tools (2026-11-21)

| # | Check | Command | Result |
|---|---|---|---|
| S2-1 | The statistics against hand-worked values | `python -I Tools\csv_stats.py --selftest` | ✅ "selftest: OK (percentiles, summary, event parsing and GC metrics match hand-worked values)", exit 0 |
| S2-2 | The runner's command lines without running anything | `python -I Tools\run_bench.py --configs E0 --items 0 --runs 1 --label drycheck --dry-run` | ✅ two commands (priming, then run 1) for the real exe `Build\Windows\GCBudget\Binaries\Win64\GCBudget.exe`, each with `-traceautostart=0`; no file written |
| S2-3 | One E0 batch end to end | job 5, S1-4 | ✅ 4 runs, each with its CSV, run report and log in `Benchmarks\Raw`; manifest and summary written. The environment record has `cpu_affinity` "0xFFFF (performance cores only; hybrid CPU)" read back from Windows, AC power, the Turbo power plan |
| S2-4 | A traced run writes a trace | job 4, then job 5 | ❌ job 4: no `.utrace` at all, because `-traceautostart=0` also blocks a trace requested with `-trace=`/`-tracefile=` (`Runtime/Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:2017–2021`; `07` §3). Fixed: traced runs leave it out (`05` C2). ✅ job 5: "Trace started (writing to file …)", a 40,960,423-byte trace. That run still exited 777003 (below) |
| S2-5 | The batch checker | `python -I Tools\check_batch.py <batch>` on `2026-11-21_031914_s1e0`, `2026-11-21_035628_s1e0b`, `2026-11-21_035728_s1traceb`, and a batch that doesn't exist | ✅ "all runs pass", exit 0, for the three batches; exit 2 with a message for the missing one |
| S2-6 | Bracket validation | `python -I -u Tools\validate_bracket.py --label s1traceb` (its own queue job); after the comparison was replaced, `… --label s1traceb --reuse-export` (no Unreal process) | ❌ job 6 `20261121_035916_18e2b4`: Insights analysed the trace and logged "Exported 1503 timing events", but no file appeared: the command reads its file name with escapes on, so every backslash vanished and the path became drive-relative (`D:UNREAL PORTFOLIOP04…`). No such file was found on D: (seven levels deep) or in the usual C: folders, so it was probably never written **[unverified]**. Fixed: forward slashes (`05` C3). ✅ job 7 `20261121_045731_fefa5f`: export written, 1,503 events. Its distribution comparison treated every event over 10× the median as a cycle frame and found 22 for 12 cycles: the purge frames two frames after each cycle passed the threshold too. **Replaced by a frame-matched comparison** and rerun on the same export: both alignments hold (the trace's largest residue is 119, i.e. frames 120, 240, … 1440; every CSV cycle row lies on a multiple of 120). Over the 10 captured cycles (0.60–0.98 ms) **bracket − floor − Insights = median +0.002 ms, at most ±0.008 ms**. `CyclePauseMs` reads 0.006 ms [0.0057–0.0084] less than Insights. On the 1,190 other frames, bracket − Insights = median 0.027 ms (the floor is 0.029 ms), p95 0.044, p99 0.147, max 0.591 ms: the non-GC work in the bracket is small and steady, with rare outliers, and one landing on a cycle frame would inflate that cycle **[inferred]** |

**Exit code 777003 on traced runs: the evidence.**
- Both traced runs (jobs 4 and 5) exited with 777003. Every untraced run before and after them exited with 0.
- In job 5 tracing really started, the run completed its capture and its report, and the log ends normally ("LogExit: Exiting.", "Log file closed"). The process then ended with 777003. There is no crash folder and no dump.
- 777003 is `ECrashExitCodes::CrashReporterCrashed` (`Runtime/Core/Public/GenericPlatform/GenericPlatformCrashContext.h:45`). On Windows the crash-reporting thread exits with it when it catches an exception **in its own loop** (`Runtime/Core/Private/Windows/WindowsPlatformCrashContext.cpp:1300–1345`).
- So something crashed during shutdown, after the log had closed, and only with tracing on **[inferred]**. The cause is **not found [unverified]**.
- **Decision** (the coordinator's rule: at most one retry): no further traced reruns. The trace that job 5 wrote is validated in a separate short job, and traced runs never share a chain with untraced batches again. The runner keeps treating any non-zero exit as a failed run, so a traced run is never used as measurement data.

**Audit trail (S2 code):**
1. **Review** (the code-review skill at medium, over the whole S1+S2 change): six of the ten findings concern S2, all fixed before job 5:
   - `validate_bracket.py` could read a stale export from an earlier run. It now deletes it first and requires a file newer than the launch (`05` C3).
   - It matched trace events to frames with fixed 120-event windows, which start-up frames would have misaligned. Cycle frames are now found by duration (`05` C3).
   - It compared frames whose bracket never opened (`BracketMs` −1). They are skipped now (`05` C3).
   - `csv_stats.py` counted a cycle cut off by the **end** of the capture as complete. Such cycles are partial now (`05` C1).
   - `--label` was not validated, although it becomes part of file names and `-GCBRunTag` (`05` C2).
   - `--label` in `validate_bracket.py` matched as a substring, so `E0` could match a configuration name. It now matches the batch label exactly (`05` C3).
2. **Clean-up pass** (structured self-review): `frame_events` found the EVENTS column once per frame; it is now found once per capture. A lambda in the summary became a named nested function (`05` C1, C2).
3. **`check_batch.py`** (added after job 5 from a throwaway checker, so that S1-4 has a committed command): a structured self-review fixed a membership test that rebuilt a set on every frame, and made a missing report or log a failed run instead of a traceback (`05` C4).
