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

## Phase 5 onward

*(Filled in step by step: see `03`.)*
