// P04 GCBudget (benchmark) - the director: applies one configuration, owns the GC cadence, runs one timed capture.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "GCBBenchmarkSubsystem.generated.h"

/** The configurations of 01_Problem_Analysis.md section 6.2. Only the ones built so far are accepted (03 roadmap). */
enum class EGCBConfig : uint8
{
	None,   // no -GCBConfig: the world behaves like a normal empty map with engine-default GC
	E0,     // no items: the engine's own GC cost in this project
};

/**
 * Runs the benchmark for one game world. A world subsystem instead of a placed actor because the project has no
 * hand-authored map; a tickable one because it drives the GC cadence and the capture timeline every frame. It ticks
 * inside FTickableGameObject::TickObjects (LevelTick.cpp:1821), before the engine's GC call (:1970) in the same frame.
 *
 * GC cadence (02_System_Architecture.md section 6): the engine's own timer is parked
 * (gc.TimeBetweenPurgingPendingKillObjects = 3600) and every GCFrames frames the director calls
 * GEngine->ForceGarbageCollection(false). That only moves the timer, so the cycle starts in the same frame through the
 * engine's normal non-full-purge path.
 *
 * Timeline (world time since begin play; a fixed step makes it an exact frame count):
 *   [0, Warmup)          warm-up: GC cycles already run, nothing measured
 *   Warmup               CSV_EVENT "BenchStart"  -> capture starts with -csvStartOnEvent=BenchStart
 *   Warmup + Capture     CSV_EVENT "BenchEnd"    -> capture stops at the end of this frame (-csvStopOnEvent=BenchEnd)
 *   next frame           the run report is written (outside the capture)
 *   a few frames later   with -GCBExit the game quits, after the CSV profiler has written its file
 *
 * Command line (all optional):
 *   -GCBConfig=NAME   E0 (more configurations arrive with their stores); absent = no benchmark
 *   -GCBItems=N       items in the store (default 0)
 *   -GCBSeed=S        workload seed (default 1)
 *   -GCBGCFrames=F    frames between director-requested GC cycles (default 120 = 2 s at 60 Hz; 0 = engine default timer)
 *   -GCBWarmup=SEC    warm-up seconds (default 10)
 *   -GCBCapture=SEC   measured seconds (default 60)
 *   -GCBRunTag=NAME   name of the run report and CSV metadata tag (default "run")
 *   -GCBExit          quit when the run is finished
 */
UCLASS()
class UGCBBenchmarkSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin UWorldSubsystem
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End UWorldSubsystem

	//~ Begin USubsystem
	/** Restores every console variable the run changed. */
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

protected:
	//~ Begin UWorldSubsystem
	/** Game and PIE worlds only. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

private:
	/** Values read once from the command line. */
	struct FRunSettings
	{
		FString ConfigName;
		EGCBConfig Config = EGCBConfig::None;
		int32 Items = 0;
		int32 Seed = 1;
		int32 GCFrames = 120;
		double WarmupSeconds = 10.0;
		double CaptureSeconds = 60.0;
		FString RunTag = TEXT("run");
		bool bExitWhenDone = false;
	};

	enum class ERunPhase : uint8
	{
		Idle,     // no benchmark requested, or the settings were rejected
		WarmUp,
		Capture,
		Done
	};

	static FRunSettings ReadRunSettings();

	/** Maps a -GCBConfig name to a configuration; None if unknown or not built yet. */
	static EGCBConfig ParseConfig(const FString& Name);

	/** Returns false (and logs an error) if the settings would produce a misleading run. */
	bool ValidateRunSettings() const;

	/** Applies this configuration's GC console variables from code and records each value in the CSV metadata. */
	void ApplyGCSettings();

	/** Sets one GC console variable from code and remembers its previous value for Deinitialize. */
	void SetGCVariable(const TCHAR* Name, const TCHAR* Value);

	/** Every GCFrames frames: asks the engine for a normal (non-full-purge) GC cycle this frame. */
	void UpdateGCCadence();

	/** Writes Saved/GCBRuns/<RunTag>.json: the settings and the run's own counters (outside the capture). */
	void WriteRunReport() const;

	FRunSettings Settings;

	/** Console variables this run changed, with the values they had before (restored in Deinitialize). */
	TArray<TPair<FString, FString>> ChangedConsoleVariables;
	ERunPhase Phase = ERunPhase::Idle;
	double BeginPlayTime = 0.0;
	double BenchStartTime = 0.0;

	/** World ticks since begin play (1 on the first tick). */
	int64 FrameIndex = 0;

	/** GC cycles the director asked for, and the frame index of the BenchStart event. */
	int32 GCRequests = 0;
	int64 BenchStartFrame = -1;
	int64 BenchEndFrame = -1;
	int32 FramesSinceEnd = 0;
};
