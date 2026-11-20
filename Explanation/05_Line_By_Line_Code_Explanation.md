# 05 · Line By Line Code Explanation: GCBudget

**Rule:** every file that exists in `GCBudget\` and `Tools\` is explained here, line by line, in the same step it is written. Sections follow the build order in [03_Implementation_Roadmap.md](03_Implementation_Roadmap.md).

| Part | Step | Files |
|---|---|---|
| A | Phase 4, F1–F3 (foundation) | `.uproject`, two `Target.cs`, two `Build.cs`, two module `.cpp`, `.uplugin`, three config `.ini`, `Tools\*.bat`, `Tools\cpu_affinity.py` |
| B onward | Phase 5, S1 onward | Written with the code |

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
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		// GCBudgetCore is private for now: no public header of this module includes a plugin header yet.
		PrivateDependencyModuleNames.AddRange(new string[] { "GCBudgetCore" });
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
| `PrivateDependencyModuleNames … "GCBudgetCore"` | **Rule of thumb: private unless a public header needs it.** Today only `.cpp` files could use the plugin. It becomes public in the step where a public header of this module first includes a plugin header (the struct stores hold `FGCBudgetHandle`, S6), and `05` records that change then. Over-using public dependencies spreads include paths and slows builds |
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

## Part B onward · Phase 5

*(Written with the code, step by step: S1 telemetry and director first. See `03`.)*
