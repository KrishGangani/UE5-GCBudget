# 05 · Line By Line Code Explanation: GCBudget

**Rule:** every file that exists in `GCBudget\` and `Tools\` is explained here, line by line, in the same step it is written. Sections follow the build order in [03_Implementation_Roadmap.md](03_Implementation_Roadmap.md).

| Part | Step | Files |
|---|---|---|
| A | Phase 4, F1–F3 (foundation) | `.uproject`, two `Target.cs`, two `Build.cs`, two module `.cpp`, `.uplugin`, three config `.ini`, `Tools\*.bat`, `Tools\cpu_affinity.py` |
| B | Phase 5, S1 (telemetry + director, E0 only) | `GCBudgetTelemetrySubsystem.h/.cpp` (plugin), `GCBBenchmarkSubsystem.h/.cpp` (game) |
| C | Phase 5, S2 (measurement tools) | `Tools\csv_stats.py`, `Tools\run_bench.py`, `Tools\validate_bracket.py`, `Tools\check_batch.py` |
| D | Phase 5, S3 (handles + tests, plugin) | `GCBudgetHandle.h/.cpp`, `Private/Tests/GCBudgetHandleTests.cpp`, `Tools\run_tests.bat` |
| E | Phase 5, S4 (workload + UObject stores, game) | `GCBItemDef.h`, `GCBWorkload.h/.cpp`, `GCBItemObject.h`, `GCBItemStore.h/.cpp`, `GCBObjectItemStore.h/.cpp`, the director's S4 changes, `Tools\run_bench.py`'s S4 changes |
| F | Phase 5, S5 (struct store with references, game) | `GCBStructItemStores.h/.cpp`, the director's S5 changes, `GCBudget.Build.cs` (the plugin becomes public), `Tools\run_bench.py`'s S5 change |
| G | Phase 5, S7 (the proposed store, game) | `GCBStructItemStores.h` (S0's element and store), the director's and `Tools\run_bench.py`'s S7 changes |

Engine facts cite `File:line` in UE 5.8.4, relative to `Engine/Source/` unless stated.

---

## Part A · Foundation (Phase 4)

### A1. `GCBudget.uproject`: the project descriptor

```json
{
	"FileVersion": 3,
	"EngineAssociation": "5.8",
	"Category": "",
	"Description": "P04 GCBudget: garbage-collection-aware gameplay data (UE 5.8.4 portfolio project).",
	"Modules": [ { "Name": "GCBudget", "Type": "Runtime", "LoadingPhase": "Default" } ],
	"Plugins": [
		{ "Name": "GCBudgetCore", "Enabled": true },
		{ "Name": "AndroidFileServer", "Enabled": false }
	]
}
```

| Line | What it does, and why |
|---|---|
| `"FileVersion": 3` | The descriptor schema version. 3 is what Epic's 5.8 `TP_Blank` template uses (`Templates/TP_Blank/TP_Blank.uproject`). A wrong value makes the editor refuse or upgrade the file |
| `"EngineAssociation": "5.8"` | Tells the launcher and the "Generate project files" shell action which installed engine opens the project. A launcher build is identified by its version string. The template leaves it blank, which makes double-clicking the file prompt for an engine |
| `"Category"`, `"Description"` | Shown in the project browser. Cosmetic |
| `"Modules"` | Every C++ module the **project** owns. UnrealBuildTool compiles exactly these, plus plugin modules |
| `"Name": "GCBudget"` | The module name. It must match the folder `Source\GCBudget\` and the class in `GCBudget.Build.cs`. Module names are unique per project (UBT keys them in one dictionary, `research/04` §0) |
| `"Type": "Runtime"` | Built into game **and** editor targets. *Alternative:* `Editor` (editor-only), which would leave the benchmark code out of packaged builds, and packaged builds are the only ones this project measures |
| `"LoadingPhase": "Default"` | Loads after engine startup and before the first world exists. That is early enough for the module's classes (the world subsystems in Phase 5) to be registered before the map loads |
| `"Plugins"` / `"GCBudgetCore"` | Enables the project's own plugin explicitly. The plugin is `EnabledByDefault`, so it would load anyway; stating it keeps the dependency visible in one place |
| `"AndroidFileServer"`, `"Enabled": false` | Turns **off** an engine plugin that is `EnabledByDefault` (`Engine/Plugins/Runtime/AndroidFileServer/AndroidFileServer.uplugin:15`). Its editor module (`AndroidFileServerEditor`, :25–26) writes a settings section with a **random `SecurityToken`** into `Config\DefaultEngine.ini` the first time the editor binary runs, which happened in the first Phase 4 smoke run (`09` F-3a). A security token must not reach a public repository, and this project never deploys to Android, so the plugin is disabled and the section deleted (a portfolio-wide rule since 2026-11-21) |
| *Not included:* `ModelingToolsEditorMode` | The template enables this editor-only plugin. This project authors no meshes, so it is left out |

### A2. `Source\GCBudget.Target.cs` and `GCBudgetEditor.Target.cs`: build targets

```csharp
using UnrealBuildTool;

public class GCBudgetTarget : TargetRules
{
	public GCBudgetTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("GCBudget");
	}
}
```

| Line | What it does, and why |
|---|---|
| `// P04 GCBudget - game target …` | A one-line header comment saying which target this file defines |
| `using UnrealBuildTool;` | Target files are **C#**, compiled by UBT itself, not by the C++ compiler. This brings `TargetRules`, `TargetType` and the version enums into scope |
| `public class GCBudgetTarget : TargetRules` | UBT finds targets by class name `<Name>Target`, so this class defines the target `GCBudget`. That is why `build.bat GCBudget …` works |
| `(TargetInfo Target) : base(Target)` | UBT passes in the platform, configuration and architecture being built; the base constructor stores them |
| `Type = TargetType.Game` | A standalone game executable with no editor code: the target that gets packaged and measured. The editor file uses `TargetType.Editor`, which builds DLLs loaded by `UnrealEditor.exe` |
| `DefaultBuildSettings = BuildSettingsVersion.V7` | Opts into the newest default build settings. `V7` is `Latest` in 5.8.4 (`Programs/UnrealBuildTool/Configuration/Rules/TargetRules.cs:170, 177`). Older values keep older defaults for compatibility |
| `IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8` | Uses 5.8's header include order, also `Latest` (`TargetRules.cs:235, 242`). Pinning it means an engine upgrade doesn't silently change what our code includes |
| `ExtraModuleNames.Add("GCBudget")` | Tells the target which project module to build (`TargetRules.cs:2819`). Plugin modules come in through the plugin system |

**Why two target files?** The editor and a packaged game are different executables with different module sets, so UBT needs one target rule for each. The editor target is needed even though nothing is measured in the editor: the cook step runs the editor binary (`UnrealEditor-Cmd`), which loads the project's modules.

### A3. `Source\GCBudget\GCBudget.Build.cs`: the game module's build rules

```csharp
public class GCBudget : ModuleRules
{
	public GCBudget(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// GCBudgetCore is public: a public header (GCBStructItemStores.h) includes its GCBudgetHandle.h.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GCBudgetCore" });
	}
}
```

| Line | What it does, and why |
|---|---|
| The two header comments | Say what this module is for, and the dependency rule (game → plugin, never the reverse, `02` §3) |
| `class GCBudget : ModuleRules` | The class name **must equal the module name**; UBT maps each `*.Build.cs` to its module by it |
| `ReadOnlyTargetRules Target` | A module can read target settings (platform, configuration) but not change them, so module rules can't fight each other |
| `PCHUsage = UseExplicitOrSharedPCHs` | Use a **shared precompiled header** from the engine unless the module names its own (`Rules/ModuleRules.cs:220`). The build log shows it: `SharedPCH.Engine.Project…` for the game target, `SharedPCH.UnrealEd.Project…` for the editor. Precompiled headers make each `.cpp` compile faster. *Rejected:* `NoPCHs` (slow), a private PCH (unneeded for a small module) |
| `PublicDependencyModuleNames …` | Modules whose **headers our public headers will include**. Phase 5's public headers declare UObjects and world subsystems, so `Core` (containers, strings), `CoreUObject` (UObject, reflection, GC) and `Engine` (worlds, subsystems) are public |
| `GCBudgetCore` in `PublicDependencyModuleNames` | **Rule of thumb: private unless a public header needs it.** Until S4 only `.cpp` files used the plugin, so it was private. Since S5 a public header, `GCBStructItemStores.h`, includes the plugin's `GCBudgetHandle.h`, so the dependency is public (F3). Over-using public dependencies spreads include paths and slows builds |
| *Not included:* `InputCore`, `EnhancedInput` | The template adds them, but this project reads no player input in code. The dependency list stays honest |

### A4. `Source\GCBudget\Private\GCBudgetModule.cpp`

```cpp
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, GCBudget, "GCBudget");
```

| Line | What it does, and why |
|---|---|
| `// P04 GCBudget - primary game module registration.` | Header comment |
| `#include "Modules/ModuleManager.h"` | Declares `IMPLEMENT_PRIMARY_GAME_MODULE` and `FDefaultGameModuleImpl`. Include the specific header, not a giant one ("include what you use") |
| the comment above the macro | Says why no custom module class exists |
| `IMPLEMENT_PRIMARY_GAME_MODULE(…)` | Generates the module's entry point (`Runtime/Core/Public/Modules/ModuleManager.h:1094–1136`). In a **monolithic** (packaged) build it also defines `GInternalProjectName` from `UE_PROJECT_NAME`, which UBT passes in from the `.uproject`, and `GIsGameAgnosticExe = false` (:1100–1104). It then expands to `IMPLEMENT_MODULE`, which in monolithic builds creates a static `FStaticallyLinkedModuleRegistrant` (:946–951) and in modular (editor) builds a `FModuleInitializerEntry` whose function returns `new ModuleImplClass()` (:955–969). **Primary** means "the project's main game module"; a project needs at least one. *Without it:* the module never registers, and a packaged game has no project name to start from |
| `FDefaultGameModuleImpl` | An engine class with empty startup and shutdown that reports `IsGameModule() == true` (`ModuleManager.h:892–900`). We have no startup logic, so our own class would be an extra class with no reason |
| `GCBudget` | The module name; it must match the `.uproject` and `Build.cs`. The macro's static assert warns on a mismatch (:950, :968) |
| `"GCBudget"` | The old "game name" argument. **Ignored in 5.8**: the monolithic overloads name it `DEPRECATED_GameName`, and Epic's comment says specifying it "is no longer necessary" (:1094–1100). We pass the project name anyway, because the macro still takes three arguments |

**Why there is no `GCBudget.h`:** the template ships an empty module header that nothing includes. It adds nothing, so it is left out.

### A5. `Plugins\GCBudgetCore\GCBudgetCore.uplugin`: the plugin descriptor

| Field | Value | Why |
|---|---|---|
| `FileVersion`, `Version`, `VersionName` | 3, 1, "1.0" | The same schema as Epic's plugins in 5.8 (e.g. `Plugins/Runtime/SignificanceManager/SignificanceManager.uplugin:2`) |
| `FriendlyName`, `Description`, `Category` | "GC Budget Core", one sentence, "Performance" | Shown in the editor's Plugins window. The description says exactly what Phase 5 will put in it: GC telemetry and generational handles |
| `CreatedBy…`, `DocsURL`, `MarketplaceURL`, `SupportURL` | empty | Optional fields, kept so the file has the standard shape. No links are published from a portfolio plugin |
| `EnabledByDefault` | `true` | A project that copies the plugin gets it working without editing its `.uproject`. Epic's own optional plugins use `false`, because they ship with every project; ours is copied in on purpose |
| `CanContainContent` | `false` | Code only. Saying so avoids an empty content mount point |
| `IsBetaVersion`, `IsExperimentalVersion` | `false`, `false` | No warning badge. That is honest for small, tested code, which still doesn't make it "production-ready" |
| `Installed` | `false` | Part of the project, not installed into the engine |
| `Modules[0]` | `GCBudgetCore`, `Runtime`, `Default` | One runtime module (it must exist in packaged builds) at the default phase |

### A6. `GCBudgetCore.Build.cs` and `Private\GCBudgetCoreModule.cpp`

- **`Build.cs`:** the same pattern as A3, with `Core`, `CoreUObject` and `Engine` **public**. Phase 5's public plugin headers declare a USTRUCT handle (`CoreUObject`) and a `UWorldSubsystem` (`Engine`), so any module that includes them needs those include paths. The header comment repeats the dependency rule: the plugin must never depend on a game module.
- **`IMPLEMENT_MODULE(FDefaultModuleImpl, GCBudgetCore);`** is the non-primary version of A4: an ordinary module entry point (`ModuleManager.h:943–969`). `FDefaultModuleImpl` is the empty default implementation (:884–886).

The build log line `[Adaptive Build] Excluded from GCBudgetCore unity file: GCBudgetCoreModule.cpp` is normal. UBT compiles files that git reports as changed on their own instead of inside the "unity" batch file, to speed up iteration ("Using 'git status' to determine working set for adaptive non-unity build", in the same log).

### A7. `Config\DefaultEngine.ini`

| Section / key | Why |
|---|---|
| Header comments | Say that benchmark-relevant keys restate engine defaults on purpose |
| `GameMapsSettings: GameDefaultMap / EditorStartupMap = /Engine/Maps/Templates/Template_Default` | A shipped engine map with a floor and lights, so no map has to be authored and the project stays buildable from text files. The cook includes it automatically as the default map |
| `[/Script/Engine.Engine] bSmoothFrameRate=False, bUseFixedFrameRate=False` | Both are already the engine defaults. Restated so nobody can turn on smoothing or a frame cap without it showing in a diff; either would make frame time stop reflecting real work |
| `[SystemSettings] r.VSync=0` | Restated for the same reason: VSync makes Present wait for the monitor |
| `[/Script/Engine.GarbageCollectionSettings]` | The engine applies this section to the matching `gc.*` cvars at startup with project-setting priority (`Runtime/Launch/Private/LaunchEngineLoop.cpp:2820`) |
| `gc.TimeBetweenPurgingPendingKillObjects=61.1` | `BaseEngine.ini`'s value (`Engine/Config/BaseEngine.ini:1749`). In Phase 5 the director overrides it from code for every run (`02` §6); the restatement keeps the project's own baseline explicit |
| `gc.AllowParallelGC=True` | `BaseEngine.ini:1747`. Parallel reachability stays on in every configuration, as shipped (`01b` #6) |
| `gc.IncrementalBeginDestroyEnabled=True` | `BaseEngine.ini:1751`. Destruction is time-sliced, 2 ms per frame (`01b` #3) |
| `gc.CreateGCClusters=True` | `BaseEngine.ini:1752`. Must stay on, or U1's `CreateCluster` asserts (`Runtime/CoreUObject/Private/UObject/UObjectClusters.cpp:929`) |
| `gc.AssetClustreringEnabled=False`, `gc.ActorClusteringEnabled=False` | `BaseEngine.ini:1754–1755` (the misspelling "Clustrering" is the engine's own cvar name). Automatic clustering of loaded assets and levels stays off, so the only clusters in the project are the ones U1 creates |
| `gc.AllowIncrementalReachability=0`, `gc.AllowIncrementalGather=0` | Not in `BaseEngine.ini`; these are the C++ defaults (`Runtime/CoreUObject/Private/UObject/GarbageCollection.cpp:302, 318`). Restated because U2 turns the first one on from code, and the baseline must be visibly off |
| `RendererSettings`, `WindowsTargetSettings` | DX12 with Shader Model 6, virtual shadow maps, no static lighting, as in Epic's 5.8 template. Standard modern defaults, not benchmark choices |
| *(removed)* `[/Script/AndroidFileServerEditor.AndroidFileServerRuntimeSettings]` | The engine's Android File Server editor module appended this section, with a random `SecurityToken`, on the first editor-binary run (`09` F-3a). It was **deleted before any commit**, and the plugin is disabled in the `.uproject` (A1) so it can't come back. Job 3 (`09` F-3c) confirms the file stays clean |

### A8. `Config\DefaultGame.ini` and `Config\DefaultInput.ini`

- **`DefaultGame.ini`** sets the project name and description shown by the engine. Nothing else is needed: the default map is cooked automatically, and the project has no content of its own.
- **`DefaultInput.ini`** sets only `DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput` and `DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent`, the two keys Epic's template sets for Enhanced Input.
  - *Why it exists:* the C++ defaults are the classic `UPlayerInput` and `UInputComponent` (`Runtime/Engine/Private/UserInterface/InputSettings.cpp:52–53`), and `BaseInput.ini` doesn't override them.
  - The Enhanced Input plugin is enabled by default. Its local-player subsystem then finds no Enhanced player-input object and warns at startup: "does not have a valid PlayerInput object. Failed to load user settings." (`Engine/Plugins/EnhancedInput/Source/EnhancedInput/Private/EnhancedInputSubsystems.cpp:116–119`).
  - The first Phase 4 smoke run showed exactly that warning; the file fixes it (`09` F-3a/F-3b).
  - The rest of the template's input file (axis configs, touch settings) is not needed: the project reads no input.

### A9. `Tools\build.bat`, `package.bat`, `smoke_game.bat`

| Script | Key lines | Why |
|---|---|---|
| all three | `@echo off`, `setlocal`, `set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"` | Quiet output, variables local to the script, and one place to change the engine path. The `rem` header says how to use the script and that it must run through the machine-wide UE job queue |
| `build.bat [Target] [Config]` | `call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" %TARGET% Win64 %CONFIG% -Project=… -WaitMutex -NoHotReloadFromIDE` then `exit /b %ERRORLEVEL%` | A reproducible command-line build. `-WaitMutex` waits if another UBT is running instead of failing. `call` returns to this script so `exit /b` can pass UBT's exit code on to the queue. `%~dp0` (this script's folder) makes it work from any working directory |
| `package.bat [Config]` | `RunUAT.bat BuildCookRun -project=… -noP4 -utf8output -unattended -platform=Win64 -clientconfig=%CONFIG% -build -cook -stage -pak -archive -archivedirectory=…` | Builds, cooks, stages, paks and archives a standalone game into `..\Build\Windows\` (git-ignored). **Development by default**, because the launcher engine can't build Test (`research/04` §0b) and Shipping has no CSV profiler |
| `smoke_game.bat [uncooked\|packaged] [args]` | `set "HERE=%~dp0"` **before** any `shift` | **A bug found in Phase 4:** in a batch file, `shift` also shifts `%0`, so `%~dp0` after it no longer means "this script's folder". The first version built its paths after `shift`; the packaged exe path was then wrong and the uncooked log went to the wrong folder (`09` F-5a, `07`). Capturing the folder first fixes both |
| `smoke_game.bat` mode `uncooked` | `UnrealEditor-Cmd.exe <uproject> -game -nullrhi -nosound -unattended -nosplash -traceautostart=0 -ExecCmds="quit" -log -abslog=…` | Runs the uncooked game with the editor binary, no window and no GPU, loads the default map, then quits. `-traceautostart=0` stops an open Insights from tracing it (`Runtime/Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:267–268`); `-abslog=` names the log file (`Runtime/Core/Private/GenericPlatform/GenericPlatformOutputDevices.cpp:87`) |
| `smoke_game.bat` mode `packaged` | `Build\Windows\GCBudget.exe -windowed -ResX=1280 -ResY=720 -nosound -unattended -nosplash -traceautostart=0 -ExecCmds="quit" -log -abslog=…` | Runs the packaged Development build on the real GPU at the benchmark resolution (`-Windowed`, `Runtime/Engine/Private/GameEngine.cpp:291`; `ResX=`, :326) and quits after the map loads. Its log names the chosen GPU adapter |
| `%1 … %9` after `shift` | Passes up to nine extra arguments through | `%*` would still contain the mode word, because `shift` doesn't change `%*` |

These scripts are launched **only through the UE job queue** (`python -I -u …\tools\ue_queue.py run …`), never directly. The exact queue command lines are in `09`.

### A10. `Tools\cpu_affinity.py`

Copied from the portfolio's reference project (UE5-UpdateBudget, `Tools/run_bench.py`) so this repository stays standalone. Phase 5's `run_bench.py` imports it.

| Part | What it does, and why |
|---|---|
| Module docstring | Says why the file exists and how the Phase 5 runner must use it: (1) pin **itself** with `pin_self(performance_core_mask())` before recording the environment or launching anything; (2) record `applied_mask()`; (3) launch the real game exe directly with `subprocess.Popen`, wait with a timeout, kill the tree in `finally`. It also says what not to do: `cmd /c start /affinity` always returns exit code 0, which would hide failed runs |
| `from __future__ import annotations` | Allows the `int \| None` annotation style on every supported Python version |
| `import ctypes, struct, sys` | Windows API calls, unpacking binary records, and the platform check |
| `KERNEL32 = …` block | Loads `kernel32` once, only on Windows (`use_last_error=True` keeps Windows' error code available), and declares the C types of the three calls used, so 64-bit handles and masks pass through intact. Off Windows it stays `None` and every function degrades gracefully |
| `performance_core_mask()` | Calls `GetLogicalProcessorInformationEx(RelationProcessorCore)` twice: once to learn the buffer size, once to fill it. It walks the variable-length records, ORs each core's group-0 affinity mask (offset 32) into a per-`EfficiencyClass` total (byte 9), and returns the total of the **highest** class, the P-cores. It returns `None` on a non-hybrid CPU (one class only), so the runner can fall back to no pinning |
| `pin_self(mask)` | `SetProcessAffinityMask(GetCurrentProcess(), mask)` on the runner itself. A child process inherits its parent's affinity **from its first instruction**, so each game runs pinned for its whole life, engine start-up and every task-graph worker (and so parallel GC) included. *Rejected:* pinning each game after `Popen` (the first version, copied from the reference project): a window exists between process start and the pin call, and it needs an extra handle per run |
| `applied_mask()` | Reads the mask back with `GetProcessAffinityMask`, so the environment record states what Windows actually applied, not what was requested. It labels a mask equal to the system mask as "all logical processors" |
| `if __name__ == "__main__"` | Prints the mask, for the environment record. On this machine it prints `0xFFFF` (logical CPUs 0–15 = the 8 P-cores with hyper-threading) |

**Why pinning matters for this project in particular:** parallel GC spreads reachability over the task-graph workers (`01b` #6). If Windows put some of them, or the game thread, on E-cores in some runs and not others, GC pauses would vary for reasons unrelated to the data design. Under the pin the engine still creates workers for all 24 logical CPUs (`01b` B-6); every configuration sees the same, and the mask is recorded.

---

## Part B · Step S1: telemetry and the director (no items yet)

### B1. `GCBudgetTelemetrySubsystem.h` (plugin, public)

| Line(s) | What it does, and why |
|---|---|
| `#include "Engine/EngineBaseTypes.h"` | Declares `ELevelTick` (`Runtime/Engine/Classes/Engine/EngineBaseTypes.h:69`), the type of the world delegates' second parameter |
| `#include "Subsystems/WorldSubsystem.h"` | The base class `UWorldSubsystem` |
| `#include "GCBudgetTelemetrySubsystem.generated.h"` | UnrealHeaderTool's generated code for this header. It must be the **last** include |
| `#define UE_API GCBUDGETCORE_API` … `#undef UE_API` | 5.8's export convention (the reference project and Epic's 5.8 plugins use it). `GCBUDGETCORE_API` is `dllexport` while compiling this module and `dllimport` in modules that include the header. `#undef` at the end keeps the short name from leaking into other headers |
| `struct FGCBudgetFrameStats` | One frame's results as **plain data**: no UObject, no reflection, because it is only passed to C++ listeners and copied once per frame |
| `double BracketMs = -1.0` | The bracketed time, in ms. `double`, because `FPlatformTime::Seconds()` is double and a sub-millisecond value must not lose precision. **−1 means "not measured"**: the bracket opens only when actors tick (02 §12, limit 9), and a missing value must never look like 0 ms |
| `bool bGCFrame` | Whether this frame can contain GC work. The offline floor is computed from frames where this is false (01b §4) |
| `int32 CycleStarts` | `PreGarbageCollect` broadcasts in this frame. The engine fires that delegate once per cycle, not per incremental slice (`GarbageCollection.cpp:5724–5742`) |
| `double CyclePauseMs` | The engine's own timing of the blocking call (`GetLastGCDuration`), the per-cycle pause metric M1 |
| `int32 UObjects` | The live UObject count, the same value as 5.8's "UObject Count" trace counter (`LevelTick.cpp:1973`), because the CSV `ObjectCount` stat is compiled out (01b B-4) |
| `DECLARE_MULTICAST_DELEGATE_OneParam(FOnGCBudgetFrameStats, …)` | A native (C++-only) multicast delegate type. Native, not dynamic: no Blueprint binding is needed, and a native broadcast is a cheap list walk, not a reflection call |
| `UCLASS(MinimalAPI)` | Exports only what other modules need to *use* the class (`StaticClass`, so `GetSubsystem<UGCBudgetTelemetrySubsystem>()` works from the game module). The members marked `UE_API` are exported individually |
| `class UGCBudgetTelemetrySubsystem : public UWorldSubsystem` | One instance per world, created and destroyed with it. *Rejected:* an engine subsystem (there is no world tick to bracket), an actor (needs a map to place it), a tickable subsystem (a tick runs *inside* the frame, but the bracket must sit *around* the engine's GC call). The class comment also lists what else runs inside the bracket (async-trace finish, net-driver flushes, the FX and SpeedTree ticks, the CSV world counts): small and steady in this workload, removed as the floor, and checked against Insights' own timer |
| `GENERATED_BODY()` | Inserts the reflection boilerplate UHT generated for this class |
| `FOnGCBudgetFrameStats OnFrameStats` | The public event later steps (the demo overlay) can bind to. A plain member, not a `UPROPERTY`: native delegates aren't reflected |
| `GetLastFrameStats()`, `GetTotalCycleStarts()`, `GetCycleStartsOutsideBracket()` | Inline read-only accessors. The director's run report uses the two counters: cycles that started inside this world's bracket (they must equal the director's requests) and cycles that started elsewhere (map load, another world's tick, telemetry switched off) |
| `Initialize` / `Deinitialize` (`UE_API virtual … override`) | `USubsystem`'s lifetime hooks. The delegates are **global**, so binding happens once per subsystem and unbinding is mandatory. A missed `Remove` would leave a dangling `this` in a process-wide delegate |
| `DoesSupportWorldType` (protected) | Restricts creation to Game and PIE worlds. Protected in the base (`Runtime/Engine/Public/Subsystems/WorldSubsystem.h:61–66`), so the override is too |
| `HandlePostActorTick`, `HandleWorldTickEnd`, `HandlePreGarbageCollect`, `ResetFrame` (private) | The three callbacks, whose parameter lists match the delegate types exactly (`Runtime/Engine/Classes/Engine/World.h:4519, 4526`; `FSimpleMulticastDelegate`), and one helper that closes the bracket and clears the frame's scratch state |
| `FDelegateHandle …Handle` ×3 | What `Add…` returns; `Remove(Handle)` needs it to unbind precisely this binding |
| `double BracketStartSeconds = -1.0` | The clock value when the bracket opened; negative while it is closed |
| `bool bWorkPendingAtOpen`, `int32 CycleStartsThisFrame`, `int32 TotalCycleStarts`, `int32 CycleStartsOutsideBracket`, `FGCBudgetFrameStats LastFrame` | Per-frame scratch state and the run totals |

### B2. `GCBudgetTelemetrySubsystem.cpp`

| Line(s) | What it does, and why |
|---|---|
| Includes | `Engine/World.h` (`FWorldDelegates`, `UWorld`), `HAL/IConsoleManager.h` (the cvar), `HAL/PlatformTime.h` (the clock), `ProfilingDebugging/CsvProfiler.h` (CSV macros), `UObject/GarbageCollection.h` (`GetLastGCDuration`, `GarbageCollection.h:199`), `UObject/UObjectArray.h` (`GUObjectArray`, `UObjectArray.h:1674`), `UObject/UObjectGlobals.h` (the pending-work queries, `UObjectGlobals.h:979, 986, 1000`, and `FCoreUObjectDelegates`) |
| `#include UE_INLINE_GENERATED_CPP_BY_NAME(…)` | Compiles UHT's generated `.gen.cpp` into this file instead of separately, the 5.x pattern for faster builds |
| `CSV_DEFINE_CATEGORY(GCBudget, true)` | Creates the CSV category, enabled by default. Every stat below becomes a column `GCBudget/<Name>` (category prefix, no thread prefix for custom stats: `Runtime/Core/Private/ProfilingDebugging/CsvProfiler.cpp:2447–2457`) |
| `TAutoConsoleVariable<int32> CVarTelemetry(TEXT("gcb.Telemetry"), 1, …, ECVF_Default)` | A console variable owned by this file. `ECVF_Default`, not a cheat flag, so it can be set from config or the command line in any build. The `gcb.` prefix keeps the project's cvars apart from the engine's `gc.` ones |
| `IsGCWorkPending()` | True while the current cycle still owes work: an incremental reachability slice, unhashing or purging. Those three queries are exactly what `ConditionalCollectGarbage` checks before choosing its branch (`UnrealEngine.cpp:2276–2294`) |
| `DoesSupportWorldType` | Game and PIE only |
| `Initialize` | `AddUObject(this, &ThisClass::…)` binds a member function. The binding checks that the object is alive before each call. `ThisClass` is the alias `GENERATED_BODY` provides |
| `Deinitialize` | Removes all three bindings before `Super::Deinitialize()`, the reverse of `Initialize` |
| `HandlePostActorTick` | Ignores other worlds and the "off" setting. It records whether work is already pending, then reads the clock **last**, so the checks themselves are outside the bracket |
| `HandlePreGarbageCollect` | The delegate is global: **every** process-wide GC fires it. Only a cycle that starts while this world's bracket is open (`BracketStartSeconds >= 0`) ran inside the bracket, so only that one is counted for the frame. A cycle started anywhere else is counted in `CycleStartsOutsideBracket` and never published as a GC frame. *Found in review (S1):* the first version counted every cycle, so a GC during map load or another PIE world's tick would have been attributed to the next frame's bracket |
| `HandleWorldTickEnd` | Ignores other worlds. When telemetry is off it still calls `ResetFrame()`, so switching it back on can never publish a stale start time (*found in review*). Otherwise it reads the clock **first**, so building and publishing the stats is outside the bracket |
| `Stats.CyclePauseMs = … GetLastGCDuration() * 1000.0` | **A subtle point found in the source.** `GetLastGCDuration()` is stored *after* the blocking GC call returns, and that is after `PostGarbageCollect` has already fired (`GarbageCollection.cpp:5666–5675` vs :5943). A `PostGarbageCollect` handler would read the **previous** cycle's value. `OnWorldTickEnd` runs after `ConditionalCollectGarbage` returns, so the value read there is this cycle's |
| `GUObjectArray.GetObjectArrayNumMinusAvailable()` | The live object count (`UObjectArray.h:1266–1269`) |
| Five `CSV_CUSTOM_STAT(GCBudget, …, ECsvCustomStatOp::Set)` | One value per frame per column. `Set` replaces, so the last write in a frame wins. The `bool` is written as 0/1 (`int32` overload; the double overload carries the ms values, `CsvProfiler.h:438–443`). Each call costs tens of nanoseconds, so five per frame are negligible |
| `ResetFrame()`, then `OnFrameStats.Broadcast(Stats)` | Listeners get the finished frame. Resetting *before* broadcasting means a listener can't observe a half-reset state |

**What would break if…**
- *…the clock were read before the pending checks in `HandlePostActorTick`?* The bracket would include three function calls of our own: a small, constant bias in every frame. The floor would remove it, but it is better not to measure ourselves.
- *…`CyclePauseMs` were read in a `PostGarbageCollect` handler?* Every value would belong to the previous cycle. With identical cycles nobody would notice, until the first spike appeared one cycle late.

### B3. `GCBBenchmarkSubsystem.h` (game module, public)

| Line(s) | What it does, and why |
|---|---|
| `enum class EGCBConfig : uint8 { None, E0 }` | The configurations this build understands. S1 has only `E0`; later steps add `U0`–`S0` as their stores exist, and an unknown name is rejected (B4) instead of silently running something else |
| Class comment | The cadence design and the run timeline, so the file explains itself |
| `UCLASS()` + `UTickableWorldSubsystem` | One director per game world that ticks every frame (`WorldSubsystem.h:80`). No `MinimalAPI`: nothing outside this module uses it |
| `OnWorldBeginPlay` | Runs once the world starts play: the right moment to read settings and set up the run |
| `Deinitialize` (`USubsystem`) | Restores every console variable the run changed (B4) |
| `Tick` / `GetStatId` | `FTickableGameObject`'s interface. `GetStatId` names this tickable in the stats system |
| `DoesSupportWorldType` (protected) | Game and PIE only |
| `struct FRunSettings` | Every command-line value, read once into plain fields. Defaults match `02` §7: GC every 120 frames, 10 s warm-up, 60 s capture |
| `enum class ERunPhase` | `Idle` → `WarmUp` → `Capture` → `Done` |
| `ParseConfig`, `ValidateRunSettings`, `ApplyGCSettings`, `UpdateGCCadence`, `WriteRunReport` | One responsibility each (B4) |
| `int64 FrameIndex`, `BenchStartFrame`, `BenchEndFrame` | Frame counters, 64-bit so they can never wrap, even in a long soak |
| `int32 GCRequests`; `TArray<TPair<FString, FString>> ChangedConsoleVariables` | Cycles the director asked for, which the run report compares with the telemetry's count. And every console variable the run changed, with its previous value, for `Deinitialize` |

### B4. `GCBBenchmarkSubsystem.cpp`

| Line(s) | What it does, and why |
|---|---|
| `DEFINE_LOG_CATEGORY_STATIC(LogGCBBench, Log, All)` | A log category private to this file, verbosity `Log` |
| `CSV_DEFINE_CATEGORY(GCBWorkload, true)` | The workload's CSV columns and the category of the capture events |
| `ParkedTimerSeconds = TEXT("3600")` | The value that moves the engine's own GC timer out of the way. It must stay **> 0**, because `ConditionalCollectGarbage` starts a cycle only if the interval is positive (`UnrealEngine.cpp:2284`), and `ForceGarbageCollection` relies on that branch |
| `ExitDelayFrames = 3` | Gives the CSV profiler time to write its file before the game quits (the same value the reference project verified) |
| `SetConsoleVariable(Name, Value, OutPrevious)` (static) | Finds the cvar, remembers its current value, sets the new one with `ECVF_SetByCode` (above project settings and the command line, `IConsoleManager.h:155–187`), logs both and writes the new one into the CSV metadata. A missing cvar is an `Error`, which makes the runner reject the run |
| `RecordConsoleVariable(Name)` (static) | The same without changing anything: the capture states its own GC environment |
| `JsonEscape(Value)` (static) | A few lines of hand-written escaping: a backslash before every `"` and `\`, and a four-digit unicode escape (`\u00XX`) for every character below 0x20, so a run tag can never break the report's JSON. *Found in review, twice:* the first version wrote the tag unescaped; the second used `FString::ReplaceCharWithEscapedChar()`, whose set includes `\'` (`Runtime/Core/Public/Containers/UnrealString.h.inl:1881`), which JSON does not allow, and which leaves every other control character as it is |
| `SetGCVariable(Name, Value)` / `Deinitialize()` | Every change goes through `SetGCVariable`, which records the previous value; `Deinitialize` sets each one back. A world that outlives the benchmark (PIE, map travel) then never inherits a parked GC timer (*found in review*: the first version never restored it) |
| `ReadRunSettings` | `FParse::Value` for each `-GCB…=` value and `FParse::Param` for the `-GCBExit` flag. The prefix keeps them apart from engine switches |
| `ParseConfig` | Case-insensitive name → enum; anything else → `None` |
| `ValidateRunSettings` | Rejects an unknown config, items with E0, and impossible timings. **A rejected run stays idle and writes no report**, so the runner flags it (`run_bench.py`: report missing, log errors) instead of measuring an empty world by mistake |
| `ApplyGCSettings` | Parks the timer through `SetGCVariable` (unless `-GCBGCFrames=0`, a diagnostic run at the engine's default cadence). It records `gc.VerifyAssumptions`, which must read 0 under `-NoVerifyGC`, plus parallel GC, clustering, incremental reachability and the purge time limit |
| `OnWorldBeginPlay` | Reads and validates the settings, applies the GC settings, starts the warm-up and writes the run description into the CSV metadata |
| `UpdateGCCadence` | `FrameIndex % GCFrames == 0` gives a request every 120 frames. `GEngine->ForceGarbageCollection(false)` only moves the engine's timer (`UnrealEngine.cpp:2136–2139`). The cycle starts later in the **same** frame, inside `ConditionalCollectGarbage`, through the normal branch (:2282–2287), or after a still-running purge finishes. `GCTrigger` marks the request frame in the CSV |
| `Tick` | Counts frames; runs the cadence in warm-up and capture; moves through the timeline like the reference project's director. `CSV_EVENT(GCBWorkload, TEXT("BenchStart"))` is the capture's start event: `-csvStartOnEvent=BenchStart` matches the event name whatever its category (`CsvProfiler.cpp:5081`) |
| `WriteRunReport` | A small JSON file written one frame after `BenchEnd` (outside the capture): the settings (strings escaped), the frame counters, the director's GC requests, and the telemetry's cycle counts inside and outside the bracket. `%lld` for `int64`, which is `long long` on this platform |

**Why the director owns the cadence instead of setting the engine timer to 2 s:** the timer field is private (`Engine.h:2006–2012`), and later configurations (U1) must do work in the frame *before* a cycle. Only the code that triggers the cycle knows which frame that is (02 §6).

## Part C · Step S2: the measurement tools (`Tools\`)

### C1. `csv_stats.py`

Adapted from the reference project's script (P01's rules for reading a capture are kept unchanged). New in this project:

| Part | What it does, and why |
|---|---|
| Constants `FPS`, `SPIKE_MS = 5.0`, `HITCH_MS = 1000/30` | The analysis thresholds live here, not in the game (02 §4.1), so changing them never needs a rebuild. 5 ms is 5.8.4's default incremental reachability slice (`GarbageCollection.cpp:310`); 33.3 ms is two 60 Hz frames |
| Column-name constants | The exact CSV column names the telemetry and the engine write (`GCBudget/…`, `GC/Count`, `Exclusive/GameThread/GarbageCollection`) |
| `summarize([])` → `{"n": 0}` | A configuration with no GC frames (possible in a short run) must produce an empty summary, not an exception |
| `per_frame(header, rows, column, missing)` | One value per frame. `GC/Count` is written only on cycle frames, so its empty cells mean 0 |
| `gc_metrics(...)`: the floor | Median bracket over frames where `GCFrame == 0` and the bracket opened (`BracketMs ≥ 0`) |
| GC ms per GC frame | `max(0, bracket − floor)`. Clamped at 0: a GC frame cheaper than the median non-GC frame is noise, not negative GC time |
| Cycles | A cycle-start frame plus the GC frames that follow it with no gap: the blocking frame, then purge frames, or incremental slices in U2. A cycle cut off by the **start or the end** of the capture is **partial** and left out of the per-cycle statistics (the end case was *found in review*) |
| `cycle_pause_ms` | `CyclePauseMs` on cycle-start frames (M1) |
| `cycle_total_ms`, `cycle_max_frame_ms`, `cycle_frames` | Per complete cycle: total GC work, the worst single frame, and how many frames it spread over. U2's whole point is the difference between "total" and "worst frame" |
| `engine_gc_count`, `engine_gc_time_on_gc_frames_ms` | The engine's own columns, kept as cross-checks: the cycle count must equal ours, and the engine's partial GC time must stay below our bracket |
| `spikes`, `hitches`, `minutes`, `…_per_min` | M3. A hitch is a GC frame whose **own** duration exceeds 33.3 ms, and a frame's own duration is the **next** row's `FrameTime`: the CSV profiler measures FrameTime at the start of a frame, for the frame before it (`Runtime/Core/Private/ProfilingDebugging/CsvProfiler.cpp:150–155, 4369–4390`). The last row has no next row, so its frame can't count. *Corrected in S6:* the first version read the GC frame's own row (`09` S6). Minutes = frames / 60 / 60, exact in a fixed-step run |
| `selftest()` | Eight hand-made frames, where every expected value (floor 0.20, a partial first cycle, two complete cycles, one spike, one hitch) is worked out in the comments, plus a capture that ends mid-cycle. The hitch's 40 ms sits in the row **after** its GC frame, so the test fails under the first, wrong rule |
| `events_index(header)` / `frame_events(header, row, index)` | The EVENTS column is found once per capture, not once per frame (*clean-up pass*: the reference version lowercased the whole header on every frame) |

### C2. `run_bench.py`

Adapted from the reference project's runner. Unchanged parts: the batch name, interleaving with the config order rotated each round, the priming run, deleting `GameUserSettings.ini`, never overwriting raw data, and the manifest rewritten after every run. New or changed:

| Part | What it does, and why |
|---|---|
| `sys.path.insert(0, TOOLS_DIR)` then `import cpu_affinity, csv_stats` | `python -I` leaves the script's folder off the import path, so the sibling modules are added explicitly |
| `EXE = …\GCBudget\Binaries\Win64\GCBudget.exe` | The **real** game exe, not the bootstrap. The game is then a direct child, so its exit code and its kill are reliable |
| `COMMON_ARGS`, `NO_AUTO_TRACE` | The protocol's fixed settings plus `-NoVerifyGC` (01b #11) and `-GCBExit`. Untraced runs add `-traceautostart=0`, so an open Insights can't trace them. **Traced runs must not**: the same switch also blocks a trace requested with `-trace=`/`-tracefile=` (`TraceAuxiliary.cpp:2017–2021`). *Found in the first S1 job:* the traced run wrote no trace file |
| `CONFIGS = {"E0": "-GCBConfig=E0"}` | One entry per configuration, added in the step that builds it |
| `environment_record(args)` | The reference record plus the GC cadence, any extra arguments, and **`cpu_affinity.applied_mask()`**: the mask Windows actually applied, read back |
| `launch(...)` | Builds the command (with `-tracefile=` when `--trace`), resets `GameUserSettings.ini`, `Popen`s the exe, waits with a timeout of warm-up + capture + 180 s, and **kills the process tree in `finally`**, whatever happened |
| `collect(...)` | Moves this run's CSV, run report (`Saved\GCBRuns\<name>.json`) and log into `Benchmarks\Raw`, and runs `csv_stats.analyse` on the capture |
| `is_failure(...)` | A run fails on a non-zero exit, a timeout, no or partial capture, a missing log or report, or any `Error:` in the log |
| `across_runs(values)` | "Median across runs [min–max]", the protocol's statistic |
| `write_summary(...)` | One table row per (config, N): cycles, pause p95/max, GC-frame ms p95/max, spikes and hitches per minute, floor, UObjects, working set, game-thread median, log errors |
| `main`: `pin_self` **before anything else** | Pins the runner to the P-cores, so every game it starts inherits the mask from its first instruction. It refuses to measure if pinning fails on a hybrid CPU. `SetThreadExecutionState` keeps the display awake during the batch, so a locked desktop can't throttle a windowed game |
| `--label` check | Letters, digits, `_` and `-` only: the label becomes part of file names and of `-GCBRunTag`, which `FParse::Value` would cut at a space (*found in review*) |
| `--runs 0` → number of configs | The protocol's balanced rotation |

### C3. `validate_bracket.py`

| Part | What it does, and why |
|---|---|
| Purpose | Checks that our bracket (minus its floor) measures what Insights' own `ConditionalCollectGarbage` timer measures (`LevelTick.cpp:1969`). That is the S1 acceptance check of the measurement method |
| `export_timer(...)` | Deletes any previous export of the same run, writes a one-line **response file** and runs `UnrealInsights.exe -OpenTraceFile=… -AutoQuit -NoUI -ExecOnAnalysisCompleteCmd="@=<file>"`. The command `TimingInsights.ExportTimingEvents <csv> -columns=… -threads=GameThread -timers=ConditionalCollectGarbage` (`Developer/TraceInsights/Private/Insights/TimingProfiler/TimingProfilerManager.cpp:802`) is the same command line as Epic's functional test (`…/Tests/FunctionalTests/ExportCommandsTests.cpp:148`). The `@=` form (`…/Insights/InsightsManager.cpp:1089–1100`) avoids nested quotes, which a path with a space would otherwise need. Inside the file the export path is written with **forward slashes**: the command reads its file name with `FParse::Token` and escapes on (`…/TimingProfilerManager.cpp:817–818`), so a backslash would be eaten (*found in the first validation job*, `09` S2-6). The export must be newer than the launch, so a failed export can never be mistaken for a fresh one (*found in review*). The process is killed in `finally` if it outlives the timeout |
| `compare(capture, report, durations)` | **Frame by frame, with both alignments checked.** The timer runs once per world frame, so trace event *i* is frame *i* + 1: the residue class (index mod `gc_frames`) with the largest median duration must be the one whose frames are multiples of `gc_frames`, the director's cycle frames. CSV row *j* is frame `bench_start_frame` + 1 + *j*: every cycle-start row must land on a multiple of `gc_frames`. Then, per captured cycle, bracket − floor and `CyclePauseMs` against the trace's duration for the **same frame**; for every other frame, bracket − trace, which is the non-GC work the floor is meant to remove. The differences are reported, not judged. *Replaced after the first successful validation:* the first version called every trace event over 10× the median a "cycle frame" and compared distributions. That also caught the purge frames two frames after each cycle (22 such events for 12 cycles), so it compared cycle-start frames with a mix of cycle and purge frames (`09` S2-6) |
| `--label` | Picks the newest traced, non-priming run whose **batch label** is exactly the one given (a regular expression on the batch-name prefix, so a label like `E0` can't match a config name) |
| `read_events(out_csv)`, `--reuse-export` | Reads an export's durations in start order. With `--reuse-export` the comparison runs on the export already written for that run (it must be newer than the trace), so a change to the comparison needs no Unreal process and no queue job |

### C4. `check_batch.py`

| Part | What it does, and why |
|---|---|
| Purpose | The functional checks of S1 (and later steps) over a batch that is already recorded: it reads `Benchmarks\Raw` and starts nothing, so it needs no job queue. It gives every check in `09` a committed, repeatable command |
| `check_run(csv_path)` | For one run, from its capture: the frames with a cycle start, their spacing, the director's `GCTrigger` frames, the engine's `GC/Count` column, the `CyclePauseMs` values and the UObject range. From its run report: requests and cycles inside and outside the bracket. From its log: `gc.VerifyAssumptions`, the GC timer, and the error and warning lines (timestamped `Log…: Error:` / `Warning:` lines only, so a message that merely contains the word isn't counted). It returns the list of failed checks |
| The checks | No frame with two cycle starts; starts exactly `gc_frames` apart and on the trigger frames (unless `gc_frames` is 0, the engine-timer diagnostic); the engine's count equals ours; a pause on every cycle frame and on no other; requests = cycles inside the bracket; both GC settings present in the log; no log errors. A missing report or log fails the run (*found in the self-review*: it raised a traceback) |
| `main(argv)` | Runs every capture of the batch, priming included; exit code 0 if all pass, 1 if any fails, 2 for a usage error or an unknown batch |
| Request frames *(S6)* | Per run, the median and maximum of `ClusterMs` and of GameThreadTime on the director's request frames, where U1's cluster rebuild runs: inside the director's tick, so outside the GC bracket. GameThreadTime, not FrameTime, because a row's FrameTime is the previous frame's duration (`09` S6) |
| Reuse | Column names and the CSV parser come from `csv_stats.py`, so the two tools can't disagree about what a column means |


## Part D · Step S3: generational handles (plugin)

### D1. `GCBudgetHandle.h`

| Line(s) | What it does, and why |
|---|---|
| `#include "Containers/BitArray.h"`, `"CoreMinimal.h"`, `"GCBudgetHandle.generated.h"` | `TBitArray` for the alive flags; the core types; UHT's generated code (last) |
| `USTRUCT(BlueprintType) struct UE_API FGCBudgetHandle` | A reflected struct, so a handle can be a `UPROPERTY` (saved, copied, seen by tools) and pass through Blueprint as an opaque value. `UE_API` (= `GCBUDGETCORE_API`) exports its generated reflection function, because other modules use the type in their own reflected classes |
| `GENERATED_BODY()` | The reflection boilerplate; it also makes `StaticStruct()` available |
| Two constructors | `= default` keeps the default member initialisers (an invalid handle). `(InIndex, InGeneration)` is how the allocator issues handles (`04` §14) |
| `UPROPERTY() int32 Index = INDEX_NONE` | The slot. `INDEX_NONE` (−1) for "never issued". No specifiers: saved and copied, but not editable or visible in Blueprint, so nobody can forge one |
| `UPROPERTY() int32 Generation = 0` | Generations start at 1, so 0 can never match a live slot. `int32`, because Blueprint has no `uint32` |
| `IsSet()` | "Was ever issued", which is **not** "is valid". Only the allocator knows whether the slot still holds that generation |
| `operator==` / `!=`, `GetTypeHash` | Value semantics, so handles can be compared and used as `TMap`/`TSet` keys. `HashCombineFast` mixes the two fields (`Runtime/Core/Public/Templates/TypeHash.h:89`) |
| `class FGCBudgetSlotAllocator` | Plain C++, no UObject: it holds only integers, so the automation tests can create it on the stack. **The owner keeps the data array** (D3 and Part E explain why) |
| `Allocate()` / `Release()` (`UE_API`) | Defined in the `.cpp` (D2) and exported for other modules |
| `IsValid(Handle)` (inline) | Three checks in the cheapest order: the index is in range (`IsValidIndex` also rejects −1), the slot is alive (one bit), the generations match. Inline, because the probe calls it a thousand times per frame |
| `Resolve(Handle)` | Index or `INDEX_NONE`: the one call a lookup needs |
| `IsSlotAlive`, `GetHandle`, `Num`, `NumSlots` | Small read-only queries. `GetHandle` returns the *current* handle of a live slot, which is how a store hands out a reference |
| `Reserve`, `Reset` (`UE_API`) | Pre-sizing for a known population; forgetting everything |
| Private members | `TArray<int32> Generations` (per slot), `TArray<int32> FreeSlots` (LIFO stack), `TBitArray<> Alive` (one bit per slot), `int32 NumAlive` |
| The wrap-around limit in the class comment | After 2³¹ − 1 releases of one slot the generation returns to 1, and a very old handle could match again. Stated, not hidden (02 §12) |

### D2. `GCBudgetHandle.cpp`

| Line(s) | What it does, and why |
|---|---|
| `#include UE_INLINE_GENERATED_CPP_BY_NAME(GCBudgetHandle)` | Compiles the USTRUCT's generated code here |
| `Allocate()` | Pops the most recently released slot (`Pop(EAllowShrinking::No)`, `Runtime/Core/Public/Containers/Array.h:1440`: no shrinking, so a churn-heavy store never reallocates the stack) or appends a new slot at generation 1 with a new alive bit. Marks it alive and counts it |
| `Release(Handle)` | Refuses anything that isn't current (`false`): a stale, foreign or double release can't corrupt the bookkeeping. Otherwise it clears the alive bit, **increments the generation** (wrapping to 1, never 0), pushes the slot on the free stack and uncounts it |
| `Reserve`, `Reset` | Forward to the three containers |

*Why LIFO:* a slot released this frame is refilled this frame, so the store stays dense, and the reused memory is the hottest in the cache. A FIFO queue would spread reuse over the whole array.

### D3. `Private/Tests/GCBudgetHandleTests.cpp`

| Test | What it proves |
|---|---|
| `GCBudget.Handles.DefaultIsInvalid` | A default handle is never valid, generation 0 never matches, releasing it does nothing |
| `GCBudget.Handles.AllocateResolve` | Slots 0, 1, …; generation 1; `Resolve`, `Num`, `NumSlots`, `GetHandle`; out-of-range indices rejected |
| `GCBudget.Handles.ReleaseMakesStale` | A released handle is stale, a second release fails, the slot remains |
| `GCBudget.Handles.LifoReuseNewGeneration` | Reuse is LIFO, a reused slot has generation 2, and the old handle stays stale |
| `GCBudget.Handles.Reset` | Everything is forgotten; allocation restarts at slot 0 |
| `GCBudget.Handles.RandomAgainstModel` | 1,000,000 seeded random operations (45% allocate, 45% release, 10% check). After each: the live count matches the model; sampled live handles resolve to themselves; sampled released handles are stale. Expected mismatches: **0** |

- `#if WITH_DEV_AUTOMATION_TESTS` compiles the tests only where development tests exist: every configuration except Test and Shipping (`Programs/UnrealBuildTool/Configuration/UEBuildTarget.cs:6315, 6331`).
- `IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, "Pretty.Name", Flags)` declares a test class; its `RunTest` is the body (`Runtime/Core/Public/Misc/AutomationTest.h:4297`).
- The flags `EditorContext | ClientContext | ProductFilter` make the tests run in the headless editor run and appear under the product filter.

### D4. `Tools\run_tests.bat`

Runs `UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests GCBudget;Quit" -unattended -nullrhi …`.
- The engine sets a critical error when any test fails, so the process exits non-zero (`Developer/AutomationController/Private/AutomationCommandline.cpp:491–504`). The queue job therefore fails on a failed test.
- It captures `%~dp0` first, the Phase 4 lesson (`07` §1).

## Part E · Step S4: the workload and the UObject stores (U0, U1, U2)

### E1. `GCBItemDef.h`

| Line(s) | What it does, and why |
|---|---|
| `UCLASS() class UGCBItemDef : public UObject` | Shared definition data stays a UObject in **every** configuration, as an item or effect definition asset would in a game. Only *per-item* state moves to structs |
| `UPROPERTY() int32 Index` | The definition's position in the table: what S0 items store instead of a pointer, and what the checksum folds in (so U and S stores produce the same CRC) |
| `UPROPERTY() int32 DecayQ16`, `BaseMagnitude` | The parameters every item reads each frame. Fixed point (E2) |
| `CanBeInCluster() const override { return false; }` | Keeps definitions out of U1's page clusters. Otherwise the first page's `CreateCluster` would absorb them (they are in the same package and referenced), every later page would reference that page's cluster, and rebuilding page 0 would dissolve every cluster that references it (`UObjectClusters.cpp:136–178, 741–751`). As "mutable objects" outside any cluster, the GC simply visits these 16 objects |

### E2. `GCBWorkload.h` / `.cpp`

| Line(s) | What it does, and why |
|---|---|
| `namespace GCBWorkload` | Free functions, not a class: the workload has no state of its own |
| `NumDefinitions = 16`, `TicksPerSecond = 60`, `MinLifetimeTicks`, `MaxLifetimeTicks` | `01` §6.1's numbers as `constexpr`: 16 definitions; lifetimes uniform in [50, 150] s, so 1% of the items expire per second |
| `struct FItemParams` | What a new item starts with |
| `Hash32(X)` | MurmurHash3's 32-bit finaliser: shifts and multiplies that spread consecutive inputs over all 32 bits. `FORCEINLINE`, because it is a handful of instructions |
| `MakeItemParams(Seed, Lane, Generation)` | A **pure function**: the same arguments always give the same item, whatever the store, its iteration order or its slot reuse. Three chained hashes give the definition, the lifetime and (generation 0 only) a random start point inside the lifetime, so expiries are spread out from the first frame |
| `MakeDefinition(Seed, DefIndex, …)` | Decay 1–32 / 65536 per tick (0.0015–0.05%) and a start magnitude of 64–127, both from the seed |
| `MakeSerial(Lane, Generation, NumLanes)` | `Generation × N + Lane`: unique for every item of the run, so the probe can tell whether a reference found *the* item it was taken from |
| `AgeItem(TicksLeft, Magnitude, DecayQ16, Ticks)` | One tick of work: `TicksLeft -= Ticks`; `Magnitude -= (Magnitude × DecayQ16 × Ticks) >> 16` in 64-bit, so the product can't overflow. Returns "expired" |
| *Why integer math* | Every store computes **bit-identical** state, whatever its memory layout and the compiler's floating-point choices. The end-of-run checksum can then prove that all configurations did exactly the same work (SC7). Floats could differ by contraction or reassociation between call sites |
| `ChecksumItem(Crc, …)` | Folds five fields into a running `FCrc::MemCrc32` (`Runtime/Core/Public/Misc/Crc.h:29`). Order-sensitive, so the lanes are always visited in order |

### E3. `GCBItemObject.h`: `UGCBItemObject` and `UGCBItemPage`

| Line(s) | What it does, and why |
|---|---|
| `UGCBItemObject : UObject` | U0–U2's item: the same five fields as the structs. Each field is a `UPROPERTY`, as real gameplay code would mark them (reflection, saving). Only `Def` is a reference the GC follows |
| `UPROPERTY() TObjectPtr<UGCBItemDef> Def` | `TObjectPtr`, never a raw pointer: U2's incremental reachability needs the write barrier (`01b` #9) |
| `CanBeInCluster() … return true` | Epic's recipe for runtime clusters. The default asks the outer chain, which is fine here too, but stating it makes U1 independent of what the outer classes decide |
| `UGCBItemPage : UObject`, `Capacity = 1024`, `UPROPERTY() TArray<TObjectPtr<UGCBItemObject>> Items` | Groups items, so a cluster covers 1,024 of them (02 §7). U0 and U2 use the same pages without clusters, so U1 differs **only** by clustering |
| `bool bChanged = true` | Set when an item is replaced. It starts true, so the first `PrepareForGC` builds every cluster |

### E4. `GCBItemStore.h` / `.cpp`: the store interface

| Line(s) | What it does, and why |
|---|---|
| `struct FGCBProbeResult` | Counts for one probe pass. `Sink` sums the magnitudes read, so the compiler can't remove the reads as unused |
| `UCLASS(Abstract) class UGCBItemStore : public UObject` | A UObject so the director can own it as a `UPROPERTY` and the GC sees everything it references. `Abstract`: it can't be instantiated |
| `ProbePoolSize = 4096`, `ProbeRefreshPerFrame = 8` | 4,096 stored references, 8 re-pointed per frame, so a reference lives about 8.5 s and some of them see their item expire. Stale-reference detection is exercised every frame |
| `Populate(Count, Seed, Defs)` (non-virtual) | Sets the lane state, calls the store's `PopulateItems`, then fills the probe pool with references to lanes spread over the store and restarts both probe cursors (*found in the clean-up pass*: without that, a second `Populate` with fewer lanes could leave a cursor past the end of the new pool) |
| `Update`, `RunProbe`, `Checksum` | `PURE_VIRTUAL(…)`: UObject classes can't be truly abstract in C++ (the engine must be able to construct a class default object), so the macro gives each function a body that asserts if it is ever called |
| `PrepareForGC()` | A no-op by default; U1 overrides it |
| `CurrentSerial(Lane)` | The ground truth: which item a lane holds *now* |
| `NextLife(Lane, OutSerial)` | Advances a lane's generation and returns its next item. The only place a lane changes, so the truth can't drift from the stores |
| `template <typename ResolveFunction> ProbeLoop(Lookups, Resolve)` | The probe loop all stores share. Each store passes a **lambda** that resolves one reference in its own way; because it's a template, the lambda is inlined into the loop (no virtual call per lookup). Each result is checked: a resolved reference must find its own item's serial, and a rejected one must really be stale |
| `UPROPERTY() TArray<TObjectPtr<UGCBItemDef>> Defs` | The definitions, held by the store too (as a real store would) |
| Private `FProbeEntry`, `ProbePool`, cursors, `SetProbeEntry` | The pool's lane/serial records; the concrete store keeps the matching native references |

### E5. `GCBObjectItemStore.h` / `.cpp` (U0, U1, U2)

| Line(s) | What it does, and why |
|---|---|
| `bool bClusterPages` | U1's switch, set by the director before `Populate` |
| `PopulateItems()` | Resets the pages and the probe references, so a second `Populate` starts from scratch like the struct stores (*found in review*). Then creates `ceil(N / 1024)` pages and N items with `NewObject<UGCBItemObject>(Page)`. The page is the outer, so every item is in the world's package and `CreateCluster` may include it (`UObjectClusters.cpp:741–751`) |
| `CreateItem(...)` | `NewObject` plus the five fields. This is the per-item allocation cost a UObject design pays on every expiry: name, hash entries, object-array slot |
| `Update(Ticks)` | Pages in order, slots in order (= lane order). `AgeItem` through `Item->Def->DecayQ16`: two pointer reads per item, which is what a UObject design does. On expiry: `NextLife`, **`MarkAsGarbage()`** (so a `TWeakObjectPtr` fails at once, as with a destroyed actor; `UObjectBaseUtility.h:207–216`), a new object in the slot, the page marked changed. **In U1 the new object joins the page's cluster at once** (`AddToCluster(Page)`, `UObjectClusters.cpp:877–900`): the GC never traces a cluster root's own references (`GarbageCollection.cpp:3130–3139`), so an item outside the cluster would be freed while the page still points at it. *Found in review:* the first version relied on the rebuild before each requested cycle, which a GC started any other way (the engine timer under `-GCBGCFrames=0`, a request deferred by a running purge) would not get |
| `PrepareForGC()` (U1) | Skips unless clustering; refuses to call `CreateCluster` if `gc.CreateGCClusters` is 0 (it would assert, `UObjectClusters.cpp:929`) and logs that once per store (*found in review*: a function-level `static` logged it once per process). For each changed page: `GUObjectClusters.DissolveCluster(Page)` if it has one, then `Page->CreateCluster()` (`UObjectBaseUtility.h:425`; `UObjectArray.h:1593–1650`). The dissolve comes first because a clustered object stays reachable while its cluster is kept (`GarbageCollection.cpp:4324–4346`); the new cluster contains only the items the page references now. A rebuild is counted only if a cluster exists afterwards: `CreateCluster` frees one below `gc.MinGCClusterSize` (`UObjectClusters.cpp:958–995`; *found in review*) |
| `Checksum()` | Lane order; the definition's `Index`, so the CRC is layout-independent |
| `CaptureProbeRef` / `RunProbe` | `TWeakObjectPtr<UGCBItemObject>`: the engine's own index + serial-number reference (`WeakObjectPtr.h:570–574`). `Get()` returns null for a garbage or destroyed object. The references are deliberately **not** `UPROPERTY`: a weak reference must not keep anything alive |

### E6. `GCBBenchmarkSubsystem` changes in S4

| Change | Why |
|---|---|
| `EGCBConfig` gains `U0`, `U1`, `U2`; `ParseConfig` uses a small table | One place lists the configurations this build accepts |
| `ValidateRunSettings`: items 1…1,000,000 for item configs; `-GCBGCFrames` 0…3,600; U1 needs `-GCBGCFrames` > 0 | Together the two limits keep U0's peak object count near 1.65 million: 1,000,000 live items, at most 61.1 s of garbage at 1% per second (611,000), 977 pages and about 35,500 engine objects. That is below the cooked game's 2,097,152-object limit (`UObjectBase.cpp:1363`). U1 rebuilds clusters only before the cycles the director requests, so under the engine timer its expired items would never leave their clusters. (*Found in review:* the first version bounded only the item count, so a long GC interval could pile up enough garbage to hit the limit) |
| `ApplyGCSettings`: U2 sets `gc.AllowIncrementalReachability 1` and `gc.IncrementalReachabilityTimeLimit 0.002` from code | 02 §7 |
| `CreateWorkload()` | 16 definitions in every configuration (E0 included, so object counts compare like for like), then the store and `Populate`, timed (`populate_ms` in the report) |
| `UpdateWorkload(DeltaTime)` | Ticks = round(Δt × 60), at least 1: exactly 1 in fixed-step runs. Times `Update` and the probe separately; publishes `Items`, `Expired`, `UpdateMs`, `UpdateNsPerItem` (M6), `ProbeNsPerLookup` (M7), `ProbeErrors` |
| `UpdateGCCadence`: `PrepareForGC` timed as `ClusterMs`, just before `ForceGarbageCollection(false)` | U1's maintenance lands in the cycle's own frame and is visible as its own column |
| `UPROPERTY(Transient) Definitions`, `Store` | Owned by the director, so the GC sees them; `Transient`, because nothing is saved |
| Run report: `populate_ms`, `clusters_rebuilt`, `expired`, the probe totals, `checksum` | SC7 needs the checksum and the probe errors per run |

### E7. `Tools\run_bench.py` changes in S4

| Change | Why |
|---|---|
| `CONFIGS` gains `U0`, `U1`, `U2` | The configurations this build accepts (the director rejects any other) |
| `combinations(round_configs)`: E0 runs once per round, at N = 0, whatever `--items` says; U0, U1 and U2 need `--items` > 0 | E0 has no items, and the director rejects E0 with items. One E0 run per round keeps the rotation balanced |
| `SUMMARY_COLUMNS` and the summary gain `UpdateNsPerItem` (M6), `ProbeNsPerLookup` (M7), `ClusterMs` p95 and the probe errors | The per-item and per-lookup costs and U1's maintenance, next to the GC metrics |
| `is_failure`: a probe error, or fewer cycles in the bracket than GC requests, fails the run | A lookup that finds the wrong item, or a request that started no cycle, would make the run's numbers meaningless |
| `checksum_groups` / `checksum_mismatches`, and a "Checksums (SC7)" table in the summary | Every measured run with the same items, seed and frame count must report the same checksum, whatever its configuration. A mismatch fails the batch |
| *(S6)* The summary's cluster column is the **maximum** of `ClusterMs`; `--resummarize BATCH` rewrites a batch's summary from its manifest, and with `--reanalyse` first recomputes every run's statistics from its raw CSV, so a corrected rule in `csv_stats.py` can be applied to recorded batches without running anything | `ClusterMs` is non-zero only on the request frames (1 in 120), so any percentile over all frames is 0; and the hitch rule was corrected after the baselines were recorded (*both found reading the baselines*, `09` S6) |
| `--verify-gc` swaps `-NoVerifyGC` for `-VERIFYGC` | A diagnostic run with the engine's GC verification on (`Runtime/CoreUObject/Private/UObject/Obj.cpp:5915–5944`); never for timed runs. The environment record says which one a batch used |

## Part F · Step S5: the struct store that still holds references (configuration S1)

### F1. `GCBStructItemStores.h` / `.cpp`

| Line(s) | What it does, and why |
|---|---|
| `#include "GCBudgetHandle.h"` | The plugin's handles and slot allocator (Part D). This public header includes a plugin header, so the plugin became a **public** dependency (F3) |
| `USTRUCT() struct FGCBItemDataRef` | S1's element: `TObjectPtr<UGCBItemDef> Def`, `TicksLeft`, `Magnitude`, `Charges`, `Serial`. The same fields as `UGCBItemObject`, as a plain struct. Because it holds a strong reference, a `UPROPERTY` array of these is walked **element by element** by the GC (`Runtime/CoreUObject/Private/UObject/GarbageCollection.cpp:6874–6876`; `01b` #5). That is the point of S1: as many references as U0, but almost no UObjects (H2) |
| `namespace GCBStructItem { GetDef; SetDef }` | How the shared code reads and writes an element's definition. One pair of overloads per element type; S0 adds its own in S7. They are declared **before** the templates that call them, because a qualified call is looked up where the template is defined (`04` §29) |
| `UCLASS(Abstract) class UGCBStructItemStoreBase` | The code both struct stores share. A `UCLASS` can't be a template, so the shared code is a set of **member templates** over the element type, and each concrete class owns its own `UPROPERTY` array. The two stores therefore run exactly the same logic |
| `PopulateStructItems(Items)` | Clears the items, the slot allocator and the probe handles, then allocates slots 0…N−1 in order; `check(Handle.Index == Lane)` states that lane = slot (`06` §26). `AddDefaulted` then `WriteItem` |
| `UpdateStructItems(Items, Ticks)` | Slot order (= lane order). Skips a free slot, which never happens in this workload but keeps the store correct with holes. `AgeItem` through `GetDef(Item, Defs)->DecayQ16`. On expiry: `NextLife`, then `Release` the slot's handle and `Allocate` again. The allocator is LIFO, so the **same** slot comes back with a new generation (`check(Reused.Index == Slot)`): every outstanding handle to the old item is now stale, and the array stays dense |
| `RunStructProbe(Items, Lookups)` | The shared probe loop (E4) with a lambda that resolves a handle: `Slots.Resolve(Handle)` (one bounds check, one bit, one compare), then reads the item. `[this, &Items]` captures the array by reference, which is safe because the lambda never outlives the call (`04` §23) |
| `StructChecksum(Items)` | Lane order, the definition's `Index`: the same CRC as U0 for the same work (SC7) |
| `CaptureProbeRef(PoolIndex, Lane)` | The probe's reference is the lane's current **handle** (`Slots.GetHandle(Lane)`), a plain value that keeps nothing alive |
| `Slots`, `ProbeHandles` | The allocator and the probe's handles: plain C++, not `UPROPERTY`, because they hold no object references |
| `WriteItem(Item, Params, Serial)` (private) | Fills one element from the workload's parameters; `SetDef` stores the definition the element type's way |
| `UCLASS() class UGCBStructRefItemStore` | Configuration S1. Four one-line overrides that call the templates with its own `UPROPERTY() TArray<FGCBItemDataRef> Items` |
| `.cpp` | Only the generated reflection code; all the logic is in the header's templates |

### F2. `GCBBenchmarkSubsystem` changes in S5

| Change | Why |
|---|---|
| `EGCBConfig::S1`, one more row in `ParseConfig`'s table | The configuration this step adds |
| `CreateWorkload()`: S1 creates a `UGCBStructRefItemStore`, every other item configuration a `UGCBObjectItemStore` | The director only chooses the store; the workload, the probe and the GC cadence are the same code for all of them |

### F3. `GCBudget.Build.cs`: the plugin becomes a public dependency

`PublicDependencyModuleNames` now lists `GCBudgetCore`, and the private list is gone. `GCBStructItemStores.h` is a public header that includes the plugin's `GCBudgetHandle.h`, so any module that includes it must see the plugin's include path too (the rule in A3).

### F4. `Tools\run_bench.py` change in S5

`CONFIGS` gains `S1`.

## Part G · Step S7: the proposed store (configuration S0)

### G1. `GCBStructItemStores.h` changes in S7

| Line(s) | What it does, and why |
|---|---|
| `USTRUCT() struct FGCBItemData` | S0's element: `DefIndex`, `TicksLeft`, `Magnitude`, `Charges`, `Serial`, five `int32` fields. **No field is an object reference**, so a `UPROPERTY` array of these adds nothing to the class's GC schema, and the collector never looks inside it (`GarbageCollection.cpp:6869–6872`; `01b` #5). 20 bytes per item, against 24 for S1's element (`04` §30) |
| `GetDef(const FGCBItemData&, Defs)` / `SetDef(FGCBItemData&, Def)` | The definition by index: `Defs[Item.DefIndex]` on read, `Def->Index` on write. They sit next to S1's pair, above the templates (`04` §29) |
| `UCLASS() class UGCBStructItemStore` | Configuration S0. The same four one-line overrides as S1's store, over its own `UPROPERTY() TArray<FGCBItemData> Items`. All the logic is the shared templates', so S0 and S1 differ **only** in the element type |

### G2. Director and runner changes in S7

| Change | Why |
|---|---|
| `EGCBConfig::S0`, one more row in `ParseConfig`'s table; `CreateWorkload()` creates a `UGCBStructItemStore` for S0 | The configuration this step adds |
| `Tools\run_bench.py`: `CONFIGS` gains `S0` | So batches can include it |
