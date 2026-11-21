// P04 GCBudget (workload) - UGCBItemStore.

#include "GCBItemStore.h"

#include "GCBItemDef.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GCBItemStore)

void UGCBItemStore::Populate(int32 Count, uint32 InSeed, const TArray<TObjectPtr<UGCBItemDef>>& InDefs)
{
	Seed = InSeed;
	NumLanes = FMath::Max(0, Count);
	Defs = InDefs;
	LaneGenerations.Init(0, NumLanes);

	PopulateItems();

	// The probe pool starts with references to lanes spread over the whole store. The cursors restart too, so a
	// second Populate with fewer lanes can never leave one past the end of the new pool.
	ProbePool.SetNum(FMath::Min(ProbePoolSize, NumLanes));
	ProbeCursor = 0;
	RefreshCursor = 0;
	for (int32 PoolIndex = 0; PoolIndex < ProbePool.Num(); ++PoolIndex)
	{
		SetProbeEntry(PoolIndex, static_cast<int32>(GCBWorkload::Hash32(Seed ^ (0xB0B0u + static_cast<uint32>(PoolIndex))) % static_cast<uint32>(NumLanes)));
	}
}

void UGCBItemStore::RefreshProbe(int64 FrameIndex)
{
	if (ProbePool.Num() == 0)
	{
		return;
	}
	for (int32 Refresh = 0; Refresh < ProbeRefreshPerFrame; ++Refresh)
	{
		const uint32 Key = static_cast<uint32>(FrameIndex) * ProbeRefreshPerFrame + static_cast<uint32>(Refresh);
		const int32 Lane = static_cast<int32>(GCBWorkload::Hash32(Seed ^ GCBWorkload::Hash32(Key)) % static_cast<uint32>(NumLanes));
		SetProbeEntry(RefreshCursor, Lane);
		RefreshCursor = (RefreshCursor + 1 == ProbePool.Num()) ? 0 : RefreshCursor + 1;
	}
}

void UGCBItemStore::SetProbeEntry(int32 PoolIndex, int32 Lane)
{
	ProbePool[PoolIndex] = { Lane, CurrentSerial(Lane) };
	CaptureProbeRef(PoolIndex, Lane);
}
