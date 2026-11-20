// GCBudgetCore - UGCBudgetTelemetrySubsystem.

#include "GCBudgetTelemetrySubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GCBudgetTelemetrySubsystem)

// Columns GCBudget/BracketMs, /GCFrame, /CycleStarts, /CyclePauseMs, /UObjects in every CSV capture.
CSV_DEFINE_CATEGORY(GCBudget, true);

namespace GCBudgetTelemetry
{
	static TAutoConsoleVariable<int32> CVarTelemetry(
		TEXT("gcb.Telemetry"),
		1,
		TEXT("GCBudgetCore telemetry: 1 = measure and publish per-frame GC stats (CSV category GCBudget), 0 = off."),
		ECVF_Default);

	/** True while the engine still owes GC work for the current cycle (an incremental slice, unhash or purge). */
	static bool IsGCWorkPending()
	{
		return IsIncrementalReachabilityAnalysisPending() || IsIncrementalUnhashPending() || IsIncrementalPurgePending();
	}

	static bool IsEnabled()
	{
		return CVarTelemetry.GetValueOnGameThread() != 0;
	}
}

bool UGCBudgetTelemetrySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGCBudgetTelemetrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::HandlePostActorTick);
	WorldTickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ThisClass::HandleWorldTickEnd);
	PreGarbageCollectHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddUObject(this, &ThisClass::HandlePreGarbageCollect);
}

void UGCBudgetTelemetrySubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(PostActorTickHandle);
	FWorldDelegates::OnWorldTickEnd.Remove(WorldTickEndHandle);
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(PreGarbageCollectHandle);

	Super::Deinitialize();
}

void UGCBudgetTelemetrySubsystem::HandlePostActorTick(UWorld* InWorld, ELevelTick /*TickType*/, float /*DeltaSeconds*/)
{
	if (InWorld != GetWorld() || !GCBudgetTelemetry::IsEnabled())
	{
		return;
	}

	bWorkPendingAtOpen = GCBudgetTelemetry::IsGCWorkPending();
	// Read the clock last, so the pending checks above are not inside the bracket.
	BracketStartSeconds = FPlatformTime::Seconds();
}

void UGCBudgetTelemetrySubsystem::HandlePreGarbageCollect()
{
	// Global delegate: every process-wide GC fires it. Only a cycle that starts while this world's bracket is open ran
	// inside the bracket, so only that one is attributed to the frame. Cycles started elsewhere (map load, another
	// world's tick in PIE, telemetry switched off) are counted separately and never published as a GC frame.
	if (BracketStartSeconds < 0.0)
	{
		++CycleStartsOutsideBracket;
		return;
	}
	++CycleStartsThisFrame;
	++TotalCycleStarts;
}

void UGCBudgetTelemetrySubsystem::HandleWorldTickEnd(UWorld* InWorld, ELevelTick /*TickType*/, float /*DeltaSeconds*/)
{
	if (InWorld != GetWorld())
	{
		return;
	}
	if (!GCBudgetTelemetry::IsEnabled())
	{
		// Switched off: close the bracket anyway, so a later switch-on can never publish a stale start time.
		ResetFrame();
		return;
	}

	// Read the clock first, so nothing below is inside the bracket.
	const double Now = FPlatformTime::Seconds();

	FGCBudgetFrameStats Stats;
	Stats.BracketMs = (BracketStartSeconds >= 0.0) ? (Now - BracketStartSeconds) * 1000.0 : -1.0;
	Stats.CycleStarts = CycleStartsThisFrame;
	Stats.bGCFrame = bWorkPendingAtOpen || CycleStartsThisFrame > 0;
	// GetLastGCDuration() is stored after the blocking GC call returns, which is after PostGarbageCollect fires
	// (GarbageCollection.cpp:5666-5675), so it is read here and not in a PostGarbageCollect handler.
	Stats.CyclePauseMs = (CycleStartsThisFrame > 0) ? GetLastGCDuration() * 1000.0 : 0.0;
	Stats.UObjects = GUObjectArray.GetObjectArrayNumMinusAvailable();

	CSV_CUSTOM_STAT(GCBudget, BracketMs, Stats.BracketMs, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GCBudget, GCFrame, Stats.bGCFrame ? 1 : 0, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GCBudget, CycleStarts, Stats.CycleStarts, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GCBudget, CyclePauseMs, Stats.CyclePauseMs, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GCBudget, UObjects, Stats.UObjects, ECsvCustomStatOp::Set);

	LastFrame = Stats;
	ResetFrame();

	OnFrameStats.Broadcast(Stats);
}

void UGCBudgetTelemetrySubsystem::ResetFrame()
{
	BracketStartSeconds = -1.0;
	bWorkPendingAtOpen = false;
	CycleStartsThisFrame = 0;
}
