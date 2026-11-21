// P04 GCBudget (workload) - the interface every configuration's item store implements.

#pragma once

#include "GCBWorkload.h"
#include "UObject/Object.h"
#include "GCBItemStore.generated.h"

class UGCBItemDef;

/** What one probe pass found. */
struct FGCBProbeResult
{
	int32 Resolved = 0;   // lookups that found an item
	int32 Stale = 0;      // lookups whose reference had expired (correctly rejected)
	int32 Errors = 0;     // a reference that resolved to the wrong item, or a live one that was rejected
	int64 Sink = 0;       // sum of the magnitudes read, so the reads can't be optimised away
};

/**
 * Holds N items (lanes 0..N-1) and simulates them: the same work in every configuration, only the memory layout
 * differs (02_System_Architecture.md section 9). The director calls, each frame:
 *   Update(Ticks)        age every item; replace the expired ones (returns how many expired)
 *   RefreshProbe(Frame)  point a few probe references at current items (the rest age and some go stale)
 *   RunProbe(Lookups)    resolve that many probe references through the store's stable-reference type
 *   PrepareForGC()       just before the director requests a GC cycle (U1 rebuilds clusters; others do nothing)
 *
 * The lane bookkeeping (each lane's generation) is the simulation's ground truth, used to check every lookup.
 */
UCLASS(Abstract)
class UGCBItemStore : public UObject
{
	GENERATED_BODY()

public:
	/** References in the probe pool, and how many are re-pointed at current items each frame (02 section 7). */
	static constexpr int32 ProbePoolSize = 4096;
	static constexpr int32 ProbeRefreshPerFrame = 8;

	/** Creates Count items from Seed, all referring to Defs (indexed by UGCBItemDef::Index), and fills the probe pool. */
	void Populate(int32 Count, uint32 InSeed, const TArray<TObjectPtr<UGCBItemDef>>& InDefs);

	virtual int32 Update(int32 Ticks) PURE_VIRTUAL(UGCBItemStore::Update, return 0;);

	/** Re-points ProbeRefreshPerFrame pool entries at lanes chosen from (Seed, FrameIndex). */
	void RefreshProbe(int64 FrameIndex);

	virtual FGCBProbeResult RunProbe(int32 Lookups) PURE_VIRTUAL(UGCBItemStore::RunProbe, return FGCBProbeResult(););

	virtual void PrepareForGC() {}

	/** CRC of every item's state in lane order: equal in every configuration for the same seed and frames (SC7). */
	virtual uint32 Checksum() const PURE_VIRTUAL(UGCBItemStore::Checksum, return 0;);

	int32 Num() const { return NumLanes; }

protected:
	/** Creates lane 0..NumLanes-1's first items. */
	virtual void PopulateItems() PURE_VIRTUAL(UGCBItemStore::PopulateItems, );

	/** Points probe reference PoolIndex at Lane's current item, in the store's own reference type. */
	virtual void CaptureProbeRef(int32 PoolIndex, int32 Lane) PURE_VIRTUAL(UGCBItemStore::CaptureProbeRef, );

	/** The serial of Lane's current item. */
	int32 CurrentSerial(int32 Lane) const { return GCBWorkload::MakeSerial(Lane, LaneGenerations[Lane], NumLanes); }

	/** Lane's expired item is being replaced: advances the lane and returns its next item and serial. */
	GCBWorkload::FItemParams NextLife(int32 Lane, int32& OutSerial)
	{
		const int32 Generation = ++LaneGenerations[Lane];
		OutSerial = GCBWorkload::MakeSerial(Lane, Generation, NumLanes);
		return GCBWorkload::MakeItemParams(Seed, Lane, Generation);
	}

	/**
	 * The probe loop every store runs, with its own Resolve(PoolIndex, OutMagnitude) -> serial or INDEX_NONE inlined
	 * into it (no virtual call per lookup). Each result is checked against the lane's ground truth.
	 */
	template <typename ResolveFunction>
	FGCBProbeResult ProbeLoop(int32 Lookups, ResolveFunction&& Resolve)
	{
		FGCBProbeResult Result;
		if (ProbePool.Num() == 0)
		{
			return Result;
		}
		for (int32 Lookup = 0; Lookup < Lookups; ++Lookup)
		{
			const int32 PoolIndex = ProbeCursor;
			ProbeCursor = (ProbeCursor + 1 == ProbePool.Num()) ? 0 : ProbeCursor + 1;
			const FProbeEntry& Entry = ProbePool[PoolIndex];
			int32 Magnitude = 0;
			const int32 Serial = Resolve(PoolIndex, Magnitude);
			if (Serial != INDEX_NONE)
			{
				++Result.Resolved;
				Result.Sink += Magnitude;
				Result.Errors += (Serial != Entry.Serial) ? 1 : 0;            // resolved to a different item
			}
			else
			{
				++Result.Stale;
				Result.Errors += (CurrentSerial(Entry.Lane) == Entry.Serial) ? 1 : 0;   // rejected a live item
			}
		}
		return Result;
	}

	/** The shared definitions, indexed by UGCBItemDef::Index. */
	UPROPERTY()
	TArray<TObjectPtr<UGCBItemDef>> Defs;

	uint32 Seed = 1;
	int32 NumLanes = 0;

	/** Per lane: how many times its item has been replaced (0 for the initial population). */
	TArray<int32> LaneGenerations;

private:
	struct FProbeEntry
	{
		int32 Lane = INDEX_NONE;
		int32 Serial = INDEX_NONE;   // the item the reference pointed at when it was taken
	};

	void SetProbeEntry(int32 PoolIndex, int32 Lane);

	TArray<FProbeEntry> ProbePool;
	int32 ProbeCursor = 0;
	int32 RefreshCursor = 0;
};
