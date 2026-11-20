# 06 · Unreal APIs And Macros: GCBudget

**Rule:** only APIs and macros the project **actually uses** are explained, each in the 8-point format: (1) what it is, (2) why it exists, (3) how it works, (4) when to use it, (5) when not to, (6) common mistakes, (7) a small example, (8) how it applies here. Engine facts cite UE 5.8.4 `File:line`, relative to `Engine/Source/` unless stated.

**Current to:** Phase 5 step S1. Later steps add their APIs in the step that first uses each.

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

---

## APIs added in step S1 (telemetry and director)

## 7. `UWorldSubsystem` / `UTickableWorldSubsystem`

1. **What:** engine-managed singletons with a world's lifetime. `UTickableWorldSubsystem` also implements `FTickableGameObject` (`Runtime/Engine/Public/Subsystems/WorldSubsystem.h:16, 80`).
2. **Why:** a place for per-world systems without placing actors in maps, and with a guaranteed create/destroy order.
3. **How:**
   - The world creates one instance of every subclass whose `DoesSupportWorldType` accepts it (protected in the base, :61–66).
   - The engine calls `Initialize` (when the world initialises), `OnWorldBeginPlay` (when play starts, :43) and `Deinitialize` (when the world is torn down).
   - A tickable subsystem's `Tick` runs inside `FTickableGameObject::TickObjects`, after `TG_PostPhysics` and before `TG_PostUpdateWork`, and before the engine's GC call in the same frame (`LevelTick.cpp:1821, 1970`).
4. **When:** world-scoped services: telemetry, directors, registries.
5. **When not:** state that must survive map changes (use a game-instance subsystem), or something designers place per level (an actor).
6. **Common mistakes:**
   - Forgetting `DoesSupportWorldType`, so the subsystem also runs in editor worlds.
   - Doing world setup in `Initialize`, before the world has begun play.
   - A `GetStatId` that returns nothing useful.
7. **Example:** `UCLASS() class UMySubsystem : public UTickableWorldSubsystem { … virtual void Tick(float) override; virtual TStatId GetStatId() const override; };`
8. **Here:** the telemetry is a plain world subsystem (it reacts to delegates). The director is tickable (it drives the cadence and the timeline). Both are Game/PIE only.

## 8. `FWorldDelegates::OnWorldPostActorTick` / `OnWorldTickEnd`

1. **What:** static multicast delegates the world broadcasts at fixed points of `UWorld::Tick` (`Runtime/Engine/Classes/Engine/World.h:4519–4527`).
2. **Why:** let systems run code at well-defined moments of every world's tick without subclassing the world.
3. **How:**
   - `OnWorldPostActorTick` fires after all tick groups, only when actors tick (`LevelTick.cpp:1650–1656, 1898–1907`).
   - `OnWorldTickEnd` fires at the very end of `UWorld::Tick` (:2061).
   - `ConditionalCollectGarbage` runs between the two (:1970).
   - Both pass the world, the tick type and the delta.
4. **When:** to observe or bracket a phase of the world tick.
5. **When not:** for per-actor logic (use ticks); for heavy work (it runs on the game thread inside the frame).
6. **Common mistakes:**
   - Forgetting that the delegates are **global**: every world's tick calls every bound handler, so each handler must check its world.
   - Not removing the binding when the listener dies.
7. **Example:** `Handle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &UMy::OnTickEnd); … FWorldDelegates::OnWorldTickEnd.Remove(Handle);`
8. **Here:** the two ends of the GC bracket (01b #14).

## 9. GC hooks: `FCoreUObjectDelegates::GetPreGarbageCollectDelegate`, `GetLastGCDuration`, `IsIncremental…Pending`

1. **What:**
   - A delegate broadcast at the start of every GC cycle (`UObjectGlobals.h:3478`; broadcast `GarbageCollection.cpp:5741`).
   - The wall time of the last blocking GC call (`GarbageCollection.h:199`).
   - Queries for pending incremental work (`UObjectGlobals.h:979, 986, 1000`).
2. **Why:** let systems prepare for GC or observe it without changing the engine.
3. **How:**
   - The pre-GC delegate fires once per cycle, never for later incremental slices (`GarbageCollection.cpp:5724`).
   - `GetLastGCDuration()` is stored after the blocking call returns (`:5666–5675`), which is *after* `PostGarbageCollect` (:5943).
   - The pending queries read the GC's own flags.
4. **When:** telemetry, clearing caches before GC.
5. **When not:** to create or destroy UObjects inside the GC delegates: the GC is running.
6. **Common mistakes:** reading `GetLastGCDuration()` in a `PostGarbageCollect` handler, which returns the previous cycle's value.
7. **Example:** `FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddUObject(this, &UMy::OnPreGC);`
8. **Here:** cycle starts and the per-cycle pause (M1); the GC-frame flag.

## 10. `GUObjectArray.GetObjectArrayNumMinusAvailable()`

1. **What:** the number of claimed slots in the global UObject array, i.e. live UObjects (`UObjectArray.h:1266–1269`; `GUObjectArray` at :1674).
2. **Why:** the engine's own object count. 5.8's "UObject Count" trace counter shows exactly this value (`LevelTick.cpp:1973`).
3. **How:** array size minus the estimated free-list size. It is cheap: two integer reads.
4. **When:** to track object counts per frame.
5. **When not:** for per-class counts (use `obj list`).
6. **Common mistakes:** treating it as "objects GC must traverse". The disregard-for-GC pool is included in it but never traversed (`GetFirstGCIndex`, :1155).
7. **Example:** `const int32 Live = GUObjectArray.GetObjectArrayNumMinusAvailable();`
8. **Here:** the CSV stat `GCBudget/UObjects` (M4).

## 11. CSV profiler: `CSV_DEFINE_CATEGORY`, `CSV_CUSTOM_STAT`, `CSV_EVENT`, `CSV_METADATA`

1. **What:** macros that write per-frame values, events and key/value metadata into the CSV capture (`Runtime/Core/Public/ProfilingDebugging/CsvProfiler.h:50–166`).
2. **Why:** a cheap, always-compiled-in (Development/Test) per-frame record that tools can parse.
3. **How:**
   - `CSV_DEFINE_CATEGORY(Name, bDefaultOn)` creates a category.
   - `CSV_CUSTOM_STAT(Cat, Stat, Value, Op)` records a value; `Set` keeps the last value of the frame. There are overloads for `float`, `double` and `int32` (`CsvProfiler.h:438–443`). The column is `Cat/Stat` (`CsvProfiler.cpp:2447–2457`).
   - `CSV_EVENT(Cat, Fmt)` adds an event to the frame's `EVENTS` column (and a trace bookmark).
   - `CSV_METADATA(Key, Value)` is written once, at the end of the file.
4. **When:** per-frame numbers a benchmark needs.
5. **When not:**
   - Per item in a hot loop: each call costs tens of nanoseconds, so publish once per frame.
   - In Shipping, where the macros compile to nothing.
6. **Common mistakes:**
   - A disabled category writes nothing.
   - `Accumulate` vs `Set` confusion.
   - Expecting metadata set *after* the capture to appear in it.
7. **Example:** `CSV_CUSTOM_STAT(GCBudget, BracketMs, Ms, ECsvCustomStatOp::Set);`
8. **Here:** the categories `GCBudget` (telemetry) and `GCBWorkload` (director), the events `BenchStart`/`BenchEnd` that open and close the capture, and the run's GC settings as metadata.

## 12. Console variables: `TAutoConsoleVariable`, `IConsoleManager::FindConsoleVariable`, `Set(…, ECVF_SetByCode)`

1. **What:** declaring the project's own cvar, and finding and setting the engine's cvars from code (`Runtime/Core/Public/HAL/IConsoleManager.h`).
2. **Why:** runtime switches that console, config and code can all change, each with a priority.
3. **How:**
   - `TAutoConsoleVariable<int32>(Name, Default, Help, Flags)` registers at static-init time.
   - `FindConsoleVariable(Name)` returns `IConsoleVariable*` or null.
   - `Set(Value, ECVF_SetByCode)` sets with "code" priority, which is above project settings and the command line and below the console (`IConsoleManager.h:155–187`).
4. **When:** benchmark switches; reading engine settings.
5. **When not:** for values that must be fixed at compile time.
6. **Common mistakes:**
   - Setting with a lower priority than the cvar's current origin, so the value doesn't change.
   - Cheat-flagged cvars can't be set from config.
   - Assuming a cvar exists without checking for null.
7. **Example:** `IConsoleManager::Get().FindConsoleVariable(TEXT("gc.AllowIncrementalReachability"))->Set(TEXT("1"), ECVF_SetByCode);`
8. **Here:** `gcb.Telemetry`; the director parks `gc.TimeBetweenPurgingPendingKillObjects` and (U2) enables incremental reachability, and logs and records each value.

## 13. `UEngine::ForceGarbageCollection(bool bFullPurge)`

1. **What:** requests a GC cycle at the next `ConditionalCollectGarbage` (`Engine.h:2828`; `UnrealEngine.cpp:2136–2139`).
2. **Why:** lets game code ask for a GC at a moment it chooses, through the engine's normal path.
3. **How:**
   - It sets the GC timer past the interval.
   - With `true` it also sets `bFullPurgeTriggered`, which selects the blocking full-purge branch.
4. **When:** at a known safe moment (a loading screen, or here, a fixed cadence).
5. **When not:** `true` during gameplay. A full purge is blocking and never incremental (01b #2).
6. **Common mistakes:**
   - Calling `CollectGarbage()` directly instead: its default is a full purge.
   - Parking the timer at 0, which disables cycles entirely (`UnrealEngine.cpp:2284`).
7. **Example:** `GEngine->ForceGarbageCollection(false);`
8. **Here:** the director's 120-frame cadence.

## 14. `FParse::Value` / `FParse::Param` / `FCommandLine::Get()`

1. **What:** read `-Key=Value` and `-Flag` from the process command line.
2. **Why:** benchmark settings without rebuilding.
3. **How:** a case-insensitive search of the command-line string. Overloads exist for `int32`, `double`, `FString` and more.
4. **When:** run configuration.
5. **When not:** for user settings (use config).
6. **Common mistakes:** a prefix collision (`-GCBItems=` vs a longer name starting the same). Unique prefixes avoid it.
7. **Example:** `FParse::Value(FCommandLine::Get(), TEXT("GCBItems="), Items);`
8. **Here:** every `-GCB…` setting.

## 15. Small utilities: `FPlatformTime::Seconds`, `FFileHelper::SaveStringToFile`, `FPaths`, `FPlatformMisc::RequestExit`, `DEFINE_LOG_CATEGORY_STATIC` / `UE_LOG`

| API | Use here | Note |
|---|---|---|
| `FPlatformTime::Seconds()` | Every timer in the project | A high-resolution `double` (`Runtime/Core/Public/Windows/WindowsPlatformTime.h:23`) |
| `FFileHelper::SaveStringToFile`, `FPaths::ProjectSavedDir()`, `FPaths::Combine` | The run report | Creates the folder if needed; returns false on failure, which is logged as an `Error` |
| `FPlatformMisc::RequestExit(false, …)` | Quit after the capture | `false` = a normal shutdown, so the CSV profiler finishes writing its file |
| `DEFINE_LOG_CATEGORY_STATIC(LogX, Log, All)` + `UE_LOG` | Each file's own log category | `Error` lines make the runner reject a run |
| `UCLASS(MinimalAPI)` + `UE_API` | The telemetry class | Exports `StaticClass` and the marked members only |
| `DECLARE_MULTICAST_DELEGATE_OneParam` | `OnFrameStats` | A native delegate: no reflection, no Blueprint |
| `UE_INLINE_GENERATED_CPP_BY_NAME` | Every `.cpp` with a `UCLASS` | Compiles the generated code into the same file |
