# 06 · Unreal APIs And Macros: GCBudget

**Rule:** only APIs and macros the project **actually uses** are explained, each in the 8-point format: (1) what it is, (2) why it exists, (3) how it works, (4) when to use it, (5) when not to, (6) common mistakes, (7) a small example, (8) how it applies here. Engine facts cite UE 5.8.4 `File:line`, relative to `Engine/Source/` unless stated.

**Current to:** Phase 4 (foundation). Phase 5 adds the GC, subsystem, CSV and handle APIs (`02` §4 lists them) in the step that first uses each.

---

## 1. `IMPLEMENT_PRIMARY_GAME_MODULE` / `IMPLEMENT_MODULE`

1. **What:** macros that generate a module's entry point, so the engine can find and start the module.
2. **Why it exists:** a module is a DLL in the editor and a slice of one big executable in a packaged game. The engine needs a uniform way to create each module's `IModuleInterface` object in both cases.
3. **How:**
   - Modular builds: a static `FModuleInitializerEntry` registers a function that returns `new ModuleImplClass()` (`Runtime/Core/Public/Modules/ModuleManager.h:955–969`).
   - Monolithic builds: a static `FStaticallyLinkedModuleRegistrant` (:946–951).
   - The primary-game variant also defines `GInternalProjectName` and `GIsGameAgnosticExe` in monolithic builds (:1100–1124).
   - A static assert warns if the name passed doesn't match the module being compiled (:950, :968).
4. **When to use:** `IMPLEMENT_PRIMARY_GAME_MODULE` exactly once per project, in its main game module. `IMPLEMENT_MODULE` in every other module, plugins included.
5. **When not:** don't use the primary variant in a plugin. Don't write a custom module class when the module has no startup or shutdown work: `FDefaultModuleImpl` / `FDefaultGameModuleImpl` exist for that (:884–900).
6. **Common mistakes:**
   - A module name that differs from the `Build.cs` class or folder.
   - Two primary game modules.
   - Forgetting the macro: the module then fails to load ("module could not be found" style errors) or links without registering.
7. **Example:** `IMPLEMENT_MODULE(FDefaultModuleImpl, GCBudgetCore);`
8. **Here:** `GCBudgetModule.cpp` (primary, with `FDefaultGameModuleImpl`) and `GCBudgetCoreModule.cpp` (plugin, with `FDefaultModuleImpl`). Neither module needs startup logic: Phase 5's classes register themselves through reflection.

## 2. `ModuleRules`: dependencies and `PCHUsage`

1. **What:** the C# class in `<Module>.Build.cs` that tells UBT how to compile one module.
2. **Why:** each module declares its own include paths, dependencies and compile settings, so modules can be built and reused independently.
3. **How:**
   - `PublicDependencyModuleNames` adds a dependency **and** exports its include paths to anyone who depends on this module.
   - `PrivateDependencyModuleNames` keeps the dependency internal.
   - `PCHUsage = UseExplicitOrSharedPCHs` uses an engine shared precompiled header unless the module names its own (`Programs/UnrealBuildTool/Configuration/Rules/ModuleRules.cs:220`).
4. **When:** every module needs one.
5. **When not:** don't make a dependency public "just in case": it spreads include paths and makes builds slower and more coupled.
6. **Common mistakes:**
   - A public header includes a header from a *private* dependency. It compiles today and breaks the first time another module includes that header.
   - A game module listed as a dependency of a plugin (an inverted dependency).
7. **Example:** `PrivateDependencyModuleNames.AddRange(new string[] { "GCBudgetCore" });`
8. **Here:** the game module depends on the plugin privately until a public header needs a plugin type (S6). The plugin depends only on `Core`, `CoreUObject` and `Engine`, publicly, because its Phase 5 public headers declare a USTRUCT and a world subsystem.

## 3. `TargetRules`: `Type`, `DefaultBuildSettings`, `IncludeOrderVersion`, `ExtraModuleNames`

1. **What:** the C# class in `<Name>.Target.cs` that describes one executable (game, editor, server, client, program).
2. **Why:** different executables need different module sets and settings. The game target must not contain editor code.
3. **How:**
   - `Type` picks the executable kind.
   - `DefaultBuildSettings = BuildSettingsVersion.V7` and `IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8` pin the 5.8 defaults; both are `Latest` in 5.8.4 (`Rules/TargetRules.cs:170, 177, 235, 242`).
   - `ExtraModuleNames` lists the project modules to build (:2819).
4. **When:** one file per executable type the project builds.
5. **When not:** don't add a Server or Client target unless the project runs one. This project is single-player and measures a standalone game.
6. **Common mistakes:**
   - `BuildSettingsVersion.Latest`: it silently changes on an engine upgrade, which invalidates baselines.
   - A class name that doesn't end in `Target`.
   - Expecting `-clientconfig=Test` to work on a launcher engine (it can't build Test, `research/04` §0b).
7. **Example:** `Type = TargetType.Game;`
8. **Here:** `GCBudget.Target.cs` (packaged and measured) and `GCBudgetEditor.Target.cs`. The editor target is needed because cooking runs the editor binary with the project's modules.

## 4. Descriptor fields: `LoadingPhase`, `Type`, `EnabledByDefault`

1. **What:** JSON fields in `.uproject` and `.uplugin` that control when and where modules load and whether a plugin is on.
2. **Why:** some modules must load before the engine starts (e.g. platform features), some only in the editor, most at the default point.
3. **How:** the plugin and module managers read the descriptors at startup and load each module at its phase, in the target types its `Type` allows.
4. **When:** `Runtime` + `Default` for gameplay code that must exist in packaged builds.
5. **When not:** `PreDefault`/`PostConfigInit` only when another module needs yours that early. `Editor` for code that must never ship.
6. **Common mistakes:** marking a module `Editor` and then wondering why the packaged game lacks its classes.
7. **Example:** `{ "Name": "GCBudgetCore", "Type": "Runtime", "LoadingPhase": "Default" }`
8. **Here:** both modules are `Runtime`/`Default`. The smoke runs show both loaded before the map (`09` F-3).

## 5. Config sections used: `GameMapsSettings`, `Engine`, `SystemSettings`, `GarbageCollectionSettings`, `InputSettings`

1. **What:** `.ini` sections that set engine defaults for this project. They layer over `Engine/Config/Base*.ini`.
2. **Why:** settings live in data, not code, so they can differ per project and per platform without recompiling.
3. **How:**
   - `[/Script/Module.Class]` sections fill `config` properties of that class.
   - `[SystemSettings]` sets console variables.
   - `[/Script/Engine.GarbageCollectionSettings]` is applied to the `gc.*` console variables at startup with project-setting priority (`Runtime/Launch/Private/LaunchEngineLoop.cpp:2820`). Some GC values are also read directly at boot, such as the UObject array size (`Runtime/CoreUObject/Private/UObject/UObjectBase.cpp:1371–1386`).
4. **When:** for project-wide defaults that every run shares.
5. **When not:** for values a benchmark varies per run. Those are set from code per run, so every capture records its own configuration (`02` §9).
6. **Common mistakes:**
   - Assuming a key is applied when it belongs to a different section.
   - Misspelling a cvar. The engine's own `gc.AssetClustreringEnabled` is spelled that way, and "fixing" the spelling silently does nothing.
   - Setting a cheat cvar from ini, which is refused. The `gc.*` cvars used here are `ECVF_Default`.
7. **Example:** `[/Script/Engine.GarbageCollectionSettings]` / `gc.CreateGCClusters=True`
8. **Here:** see `05` A7–A8. The GC section restates the 5.8.4 defaults. `InputSettings` selects the Enhanced Input classes (`Runtime/Engine/Private/UserInterface/InputSettings.cpp:52–53` are the classic C++ defaults it replaces).

## 6. Command-line switches used by the scripts

| Switch | Effect | Evidence (5.8.4) |
|---|---|---|
| `-game` | Run the editor binary as a game (uncooked smoke only) | Without `GAME` the editor binary sets itself up as the editor (`Runtime/Launch/Private/LaunchEngineLoop.cpp:2413–2416`) |
| `-nullrhi` | No rendering device: for smoke runs only, never for measurement (no real GPU work) | The engine loads the `NullDrv` RHI module (`Runtime/RHI/Private/DynamicRHI.cpp:63`); the F-3 smoke log shows `NullDrv` loaded |
| `-nosound` | No audio device | `Runtime/Core/Public/Misc/App.h:436` |
| `-unattended` | No dialogs that would wait for a user | `Runtime/Core/Private/Misc/App.cpp:278` |
| `-traceautostart=0` | Don't auto-connect to a running Insights | `Runtime/Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:267–268` |
| `-ExecCmds="quit"` | Run console commands after start-up; `quit` ends the run | Queued as deferred commands (`Runtime/Engine/Private/UnrealEngine.cpp:2552`); the smoke log shows `UGameEngine::HandleExitCommand` |
| `-abslog=<path>` | Write the log to an absolute path | `Runtime/Core/Private/GenericPlatform/GenericPlatformOutputDevices.cpp:87` |
| `-windowed -ResX= -ResY=` | Windowed at a fixed resolution | `Runtime/Engine/Private/GameEngine.cpp:291, 326` |
| `BuildCookRun … -build -cook -stage -pak -archive` | Build, cook, stage, pak, copy to the archive folder | UAT; the package log shows each stage |

**Switches decided for Phase 5 timed runs** (verified in `01b`): `-deterministic -FPS=60` (fixed step), `-NoVerifyGC` (`Runtime/CoreUObject/Private/UObject/Obj.cpp:5917`), `-LogCmds="LogGarbage Log"` for diagnostics only (`Runtime/Core/Private/Logging/LogSuppressionInterface.cpp:519`), and `-trace=default,counters` for Insights runs (`01b` B-4).
