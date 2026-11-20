// GCBudgetCore - per-frame garbage-collection telemetry for one game world.

#pragma once

#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GCBudgetTelemetrySubsystem.generated.h"

#define UE_API GCBUDGETCORE_API

/** What the telemetry measured in one frame. Published to the CSV profiler (category GCBudget) and to OnFrameStats. */
struct FGCBudgetFrameStats
{
	/**
	 * Game-thread time from FWorldDelegates::OnWorldPostActorTick to OnWorldTickEnd, in ms: the engine's
	 * ConditionalCollectGarbage call plus a small, steady remainder that the analysis subtracts (its "floor").
	 * Negative when the bracket did not open this frame (a paused or time-only world tick).
	 */
	double BracketMs = -1.0;

	/** GC work could run in this frame: a cycle started, or incremental reachability, unhash or purge was pending. */
	bool bGCFrame = false;

	/** GC cycles that started in this frame (PreGarbageCollect broadcasts). 0 or 1 in practice. */
	int32 CycleStarts = 0;

	/** The engine's own timing of the blocking GC call (GetLastGCDuration), in ms, on a frame where a cycle started; else 0. */
	double CyclePauseMs = 0.0;

	/** Live UObjects at the end of the frame: the same value as the 5.8 "UObject Count" trace counter. */
	int32 UObjects = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnGCBudgetFrameStats, const FGCBudgetFrameStats& /*Stats*/);

/**
 * Measures, every frame, how long the engine's garbage-collection call kept the game thread busy, without touching
 * engine code. UEngine::ConditionalCollectGarbage is not virtual, so it can't be wrapped. It runs between two public
 * world delegates, OnWorldPostActorTick and OnWorldTickEnd (LevelTick.cpp:1906, 1970, 2061), so this subsystem times
 * that bracket. The bracket also contains other end-of-tick work (async-trace finish, net-driver flushes, the FX and
 * SpeedTree ticks, the CSV world counts). In this project's workload those are small and steady, so the analysis
 * removes them as the bracket's non-GC "floor" (01b_Research_Findings.md section 4); a spike in one of them would
 * be counted as GC time, which is why the bracket is validated against Insights' own timer (Tools/validate_bracket.py).
 *
 * A world subsystem: it has exactly the world's lifetime and needs no actor in a map. It reacts to delegates
 * instead of ticking, because the bracket must sit *around* the engine's GC call.
 *
 * Console variable gcb.Telemetry (1 = on, default; 0 = off).
 * Limitation: in PIE with several worlds, only the world whose tick runs the GC sees it in its bracket.
 */
UCLASS(MinimalAPI)
class UGCBudgetTelemetrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Broadcast once per frame at the end of the world tick, after the CSV stats are recorded. */
	FOnGCBudgetFrameStats OnFrameStats;

	/** The stats of the most recently finished frame. */
	const FGCBudgetFrameStats& GetLastFrameStats() const { return LastFrame; }

	/** GC cycles that started inside this world's bracket since this subsystem was created. */
	int32 GetTotalCycleStarts() const { return TotalCycleStarts; }

	/** GC cycles that started while the bracket was closed (map load, another world's tick, telemetry off). */
	int32 GetCycleStartsOutsideBracket() const { return CycleStartsOutsideBracket; }

	//~ Begin USubsystem
	/** Binds the world and GC delegates. They are global, so every callback checks that its world is the one ticking. */
	UE_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	/** Unbinds every delegate bound in Initialize. */
	UE_API virtual void Deinitialize() override;
	//~ End USubsystem

protected:
	//~ Begin UWorldSubsystem
	/** Game and PIE worlds only: editor worlds have no gameplay GC to measure. */
	UE_API virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

private:
	/** Opens the frame's bracket and records whether GC work was already pending. */
	void HandlePostActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);

	/** Closes the bracket, reads the cycle pause and the object count, and publishes the frame's stats. */
	void HandleWorldTickEnd(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);

	/** Counts a GC cycle start (the engine broadcasts this once per cycle, never per incremental slice). */
	void HandlePreGarbageCollect();

	/** Closes the bracket and clears the frame's scratch state. */
	void ResetFrame();

	FDelegateHandle PostActorTickHandle;
	FDelegateHandle WorldTickEndHandle;
	FDelegateHandle PreGarbageCollectHandle;

	/** FPlatformTime::Seconds() when the bracket opened this frame; negative while closed. */
	double BracketStartSeconds = -1.0;

	/** GC work was pending when the bracket opened. */
	bool bWorkPendingAtOpen = false;

	/** Cycle starts since the last OnWorldTickEnd. */
	int32 CycleStartsThisFrame = 0;

	int32 TotalCycleStarts = 0;
	int32 CycleStartsOutsideBracket = 0;

	FGCBudgetFrameStats LastFrame;
};

#undef UE_API
