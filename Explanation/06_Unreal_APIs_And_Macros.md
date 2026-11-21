# 06 · Unreal APIs And Macros: GCBudget

**Rule:** only APIs and macros the project **actually uses** are explained, each in the 8-point format: (1) what it is, (2) why it exists, (3) how it works, (4) when to use it, (5) when not to, (6) common mistakes, (7) a small example, (8) how it applies here. Engine facts cite UE 5.8.4 `File:line`, relative to `Engine/Source/` unless stated.

**Current to:** Phase 5 step S5. Later steps add their APIs in the step that first uses each.

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

## APIs added in step S3 (handles and their tests)

## 16. `USTRUCT(BlueprintType)`, `GENERATED_BODY()`, `UPROPERTY()` on struct fields

1. **What:** reflection markers for a C++ struct (`Runtime/CoreUObject/Public/UObject/ObjectMacros.h:780`). The Unreal Header Tool reads them and generates the struct's type information (`GCBudgetHandle.generated.h`).
2. **Why:** a reflected struct can be a `UPROPERTY` of a UObject, be saved, be copied by the editor, and (with `BlueprintType`) pass through Blueprint.
3. **How:** `GENERATED_BODY()` expands to the generated declarations. Each `UPROPERTY()` field is listed in the struct's type information, so the engine can see it: and, if it is an object reference, so can the garbage collector.
4. **When:** data that must be stored in reflected containers, serialised, or used from Blueprint.
5. **When not:** purely internal data on hot paths that nothing else needs to see. `FGCBudgetSlotAllocator` is a plain C++ class for that reason.
6. **Common mistakes:**
   - Fields without `UPROPERTY()`, which reflection (and the GC) can't see.
   - `uint32` fields in a `BlueprintType` struct: Blueprint has no unsigned 32-bit type, so the struct can't be used there.
7. **Example:** `USTRUCT(BlueprintType) struct FMyHandle { GENERATED_BODY() UPROPERTY() int32 Index = INDEX_NONE; };`
8. **Here:** `FGCBudgetHandle`, two `int32` fields. Its `GetTypeHash` friend (`04` §15) uses `HashCombineFast` (`Runtime/Core/Public/Templates/TypeHash.h:89`).

## 17. `TBitArray<>`, and `TArray::Pop` / `Push` with `EAllowShrinking`

1. **What:** a dynamic array of bits (`Runtime/Core/Public/Containers/BitArray.h:360`), and `TArray`'s stack operations (`Runtime/Core/Public/Containers/Array.h:1440`).
2. **Why:** one flag per element in one bit; and a stack without extra containers.
3. **How:** `Alive.Add(false)` appends a bit; `Alive[Index]` reads or writes one (through a small proxy object). `Reserve(N)` pre-sizes the storage (`BitArray.h:811`). `Pop(EAllowShrinking::No)` removes the last element without giving memory back.
4. **When:** per-element flags over large arrays.
5. **When not:** when the flags are always read together with other per-element data: a field in the element is then cheaper than a second array.
6. **Common mistakes:** `Pop()` with the default shrinking policy, which can reallocate repeatedly as a stack grows and shrinks.
7. **Example:** `TBitArray<> Alive; Alive.Add(true); if (Alive[0]) { … }`
8. **Here:** `FGCBudgetSlotAllocator::Alive` and `FreeSlots`.

## 18. Automation tests: `IMPLEMENT_SIMPLE_AUTOMATION_TEST`, `EAutomationTestFlags`, `TestEqual` / `TestTrue` / `TestFalse`

1. **What:** Unreal's built-in unit-test framework (`Runtime/Core/Public/Misc/AutomationTest.h`). The macro declares a test class and registers it under a dotted name (`:4297–4298`, `:4367–4368`).
2. **Why:** tests that run inside the engine, are listed in the Session Frontend, and can run headless from the command line.
3. **How:**
   - The macro creates `FMyTest` with a `RunTest(const FString& Parameters)` that the author writes.
   - The flags say where it may run (`EditorContext`, `ClientContext`) and which filter it belongs to (`ProductFilter`) (`AutomationTest.h:88–142`).
   - `TestEqual(What, Actual, Expected)` and friends log a failure with the description and the values (`:1985, 2367, 2603`); the test fails if any check failed.
   - Running `UnrealEditor-Cmd.exe <project> -ExecCmds="Automation RunTests <filter>;Quit" -unattended -nullrhi` runs every test whose name contains the filter, then quits. A failure sets `GIsCriticalError`, so the process exits with a non-zero code (`Developer/AutomationController/Private/AutomationCommandline.cpp:491–504`).
4. **When:** logic that can be tested without a world: containers, math, parsers.
5. **When not:** behaviour that needs a running game or real timing: the packaged runs check those.
6. **Common mistakes:**
   - A test that always returns `true` without making any checks.
   - Running the tests without `;Quit`, so the editor never exits, or without checking the exit code, so a failure goes unnoticed.
   - Forgetting `#if WITH_DEV_AUTOMATION_TESTS`, which breaks builds without tests.
7. **Example:** `IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyTest, "Project.Area.Case", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter) bool FMyTest::RunTest(const FString&) { TestEqual(TEXT("sum"), 1 + 1, 2); return true; }`
8. **Here:** six tests named `GCBudget.Handles.*`, run by `Tools\run_tests.bat` through the job queue.

## 19. `FRandomStream`

1. **What:** a small seeded pseudo-random generator (`Runtime/Core/Public/Math/RandomStream.h:19`).
2. **Why:** the same seed gives the same sequence on every run and every machine, unlike `FMath::Rand`.
3. **How:** `FRandomStream Random(1234); Random.RandRange(0, 99)` returns an integer in [0, 99] (`:209`).
4. **When:** reproducible tests and procedural content.
5. **When not:** anything that must be unpredictable.
6. **Common mistakes:** seeding from the clock in a test, which makes a failure impossible to reproduce.
7. **Example:** `FRandomStream Random(Seed); const int32 Roll = Random.RandRange(0, 99);`
8. **Here:** the one-million-operation model test. The game's workload uses its own integer hash instead (S4), because it must give every lane its values independently of call order.

## APIs added in step S4 (the workload and the UObject stores)

## 20. `UCLASS(Abstract)` and `PURE_VIRTUAL`

1. **What:** a class specifier that marks a UObject class as not to be instantiated in the editor, and a macro for "abstract" virtual functions (`Runtime/Core/Public/Misc/CoreMiscDefines.h:99–103`).
2. **Why:** C++'s `= 0` would make the class impossible to construct, but Unreal constructs every class's default object.
3. **How:** normally `PURE_VIRTUAL(Func, return 0;)` expands to a body that calls `LowLevelFatalError` and then runs the given statements. With `CHECK_PUREVIRTUALS` it becomes a real `= 0`, so the compiler finds missing overrides.
4. **When:** an interface-like UObject base whose subclasses must implement some functions.
5. **When not:** if a sensible default exists: provide it (as `PrepareForGC() {}` does).
6. **Common mistakes:** forgetting the return statement for non-`void` functions, which leaves the function without a return value.
7. **Example:** `virtual int32 Update(int32 Ticks) PURE_VIRTUAL(UMyBase::Update, return 0;);`
8. **Here:** `UGCBItemStore`.

## 21. `NewObject<T>(Outer)` and the outer chain

1. **What:** the way to create a UObject at run time (`Runtime/CoreUObject/Public/UObject/UObjectGlobals.h:1959`).
2. **Why:** a UObject needs a slot in the global object array, a name and an **outer** (the object it lives in). `new` can't provide them.
3. **How:** `NewObject<UGCBItemObject>(Page)` allocates the object, registers it, runs its constructor, and sets `Page` as its outer. The outer chain ends at a package, so the outer decides which package an object belongs to.
4. **When:** any UObject created during play.
5. **When not:** plain data without reflection or GC needs: a struct.
6. **Common mistakes:**
   - Leaving the outer as the transient package when the object should belong to something.
   - Creating an object and keeping it only in a non-`UPROPERTY` raw pointer, so the GC frees it.
7. **Example:** `UMyObject* Object = NewObject<UMyObject>(this);`
8. **Here:** definitions (outer: the director), the store, pages (outer: the store) and items (outer: their page). Every item is therefore in the world's package, which is one of `CreateCluster`'s conditions (§23).

## 22. `MarkAsGarbage()` and `TWeakObjectPtr<T>`

1. **What:** a call that flags an object as garbage now (`Runtime/CoreUObject/Public/UObject/UObjectBaseUtility.h:207–216`), and a reference that doesn't keep its object alive (`WeakObjectPtr.h:569–575`).
2. **Why:** gameplay code needs "this is dead" to take effect immediately, long before the next GC cycle frees the memory.
3. **How:**
   - `MarkAsGarbage()` sets the object's garbage flag; the next GC cycle won't keep it alive through references and purges it.
   - A weak pointer stores the object's **index** in the global object array and that slot's **serial number**. `Get()` returns null if the slot now holds another object, or if the object is garbage (`WeakObjectPtr.h:214–218`).
4. **When:** destroying gameplay objects; observing objects you don't own.
5. **When not:** on an object in the root set (it asserts, `UObjectBaseUtility.h:209`); for ownership (use a `UPROPERTY` `TObjectPtr`).
6. **Common mistakes:**
   - Expecting `MarkAsGarbage` to free memory immediately.
   - Holding a weak pointer as a `TObjectPtr` "to be safe", which keeps the object alive.
7. **Example:** `TWeakObjectPtr<UMyObject> Weak = Object; Object->MarkAsGarbage(); check(!Weak.Get());`
8. **Here:** an item's expiry; the probe's 4,096 references in U0, U1 and U2.

## 23. GC clusters: `CanBeInCluster`, `CreateCluster`, `AddToCluster`, `GUObjectClusters`

1. **What:** the engine's way to treat a group of objects as one unit during reachability analysis (`UObjectBaseUtility.h:420–437`; `UObjectArray.h:1593–1675`).
2. **Why:** marking one cluster is much cheaper than tracing every object in it.
3. **How:**
   - `Root->CreateCluster()` collects everything the root references, transitively, that `CanBeInCluster()`, isn't in another cluster and is in the root's package (`UObjectClusters.cpp:741–751, 927–999`). Anything else it references is recorded as a "mutable" object.
   - During GC, every member of a kept cluster is marked reachable without being traced (`GarbageCollection.cpp:4324–4346`), and **the root's own references aren't traced either**: only its referenced clusters and mutable objects are (`:3130–3139`).
   - `Object->AddToCluster(Root)` adds one more object to an existing cluster, collecting its references the same way (`UObjectClusters.cpp:877–900`).
   - `GUObjectClusters.GetObjectCluster(Object)` returns its cluster or null (`:112–134`); `DissolveCluster(Object)` turns the members back into ordinary objects (`:136–179`).
   - A cluster smaller than `gc.MinGCClusterSize` (5, `Config/BaseEngine.ini:1753`) is freed silently (`UObjectClusters.cpp:958–995`). `gc.CreateGCClusters` must be on (`BaseEngine.ini:1752`); `CreateCluster` asserts otherwise (`UObjectClusters.cpp:929`).
4. **When:** large groups of objects that live and die together, loaded or created once.
5. **When not:** groups whose membership changes all the time. Every change means keeping the cluster correct by hand, and that maintenance is exactly the cost U1 measures.
6. **Common mistakes:**
   - Adding an object to the root's array but not to its cluster: the GC frees it (`07` §5).
   - Letting shared objects join the first cluster that references them.
   - Assuming a cluster exists after `CreateCluster`.
7. **Example:** `Page->CreateCluster(); NewItem->AddToCluster(Page); GUObjectClusters.DissolveCluster(Page);`
8. **Here:** U1 only: one cluster per page of 1,024 items, rebuilt before each requested cycle.

## 24. `FCrc::MemCrc32`

1. **What:** a 32-bit CRC over a block of memory (`Runtime/Core/Public/Misc/Crc.h:29`).
2. **Why:** a cheap, order-sensitive fingerprint of data.
3. **How:** `FCrc::MemCrc32(Data, Length, Crc)` continues from a previous CRC, so a long sequence can be folded in piece by piece.
4. **When:** checksums and change detection.
5. **When not:** anything security-related (a CRC is easy to forge), or hashing for containers (`GetTypeHash`).
6. **Common mistakes:** hashing structs that contain padding bytes, whose values are undefined.
7. **Example:** `uint32 Crc = 0; Crc = FCrc::MemCrc32(&Value, sizeof(Value), Crc);`
8. **Here:** `GCBWorkload::ChecksumItem` folds five `int32` fields per item (no padding) in lane order.

## 25. `UPROPERTY(Transient)` and `Cast<T>`

1. **What:** a property that is never saved, and a checked downcast between UObject classes (`Runtime/CoreUObject/Public/Templates/Casts.h:89`).
2. **Why:** GC visibility without serialisation; and a safe way to ask "is this store a `UGCBObjectItemStore`?".
3. **How:** `Transient` keeps the property in reflection, so the GC still sees the reference, but excludes it from saving. `Cast<T>(Object)` returns null when the object isn't a `T`.
4. **When:** run-time state owned by a UObject; type checks on UObjects.
5. **When not:** `Cast` on non-UObject types (use C++ casts).
6. **Common mistakes:** dropping `UPROPERTY` and keeping a raw pointer, which the GC doesn't see.
7. **Example:** `if (const UMyStore* S = Cast<UMyStore>(Store.Get())) { … }`
8. **Here:** the director's `Definitions` and `Store`; reading U1's cluster count for the run report.

## API added in step S5

## 26. `check(Condition)`

1. **What:** Unreal's assertion macro (`Runtime/Core/Public/Misc/AssertionMacros.h:229`).
2. **Why:** to state an assumption that must hold, and stop at once (with a call stack) if it doesn't.
3. **How:** `DO_CHECK` is 1 in Debug and Development and `USE_CHECKS_IN_SHIPPING` (0 by default) in Test and Shipping (`Runtime/Core/Public/Misc/Build.h:206, 246, 274, 302, 330`). With checks on, a false condition logs the expression and the location, then crashes. With them off, `check(expr)` becomes `{ CA_ASSUME(expr); }`, a hint for static analysis: the expression is never evaluated (`AssertionMacros.h:319`).
4. **When:** internal invariants that a bug, not the user, would break.
5. **When not:** for anything that can legitimately fail at run time (bad input, missing files): log an error and recover instead. And never with side effects inside, because they vanish in Test and Shipping.
6. **Common mistakes:** `check(DoSomething())`, whose call disappears in Test and Shipping.
7. **Example:** `check(Handle.Index == Lane);`
8. **Here:** the struct stores' "lane = slot" invariant, after populating and after every LIFO reuse.
