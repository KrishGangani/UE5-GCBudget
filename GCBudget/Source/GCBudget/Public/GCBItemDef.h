// P04 GCBudget (workload) - one of the 16 shared item definitions every item refers to.

#pragma once

#include "UObject/Object.h"
#include "GCBItemDef.generated.h"

/**
 * Shared, rarely changing data that stays a UObject in every configuration, the way an item or effect definition
 * would be an asset in a real game. Items refer to it by TObjectPtr (U0, U1, U2, S1) or by Index (S0).
 */
UCLASS()
class UGCBItemDef : public UObject
{
	GENERATED_BODY()

public:
	/** Position in the director's definition table; what S0 items store instead of a pointer. */
	UPROPERTY()
	int32 Index = INDEX_NONE;

	/** Magnitude lost per tick, as a Q16 fraction of the current magnitude (65536 = all of it). */
	UPROPERTY()
	int32 DecayQ16 = 0;

	/** Magnitude a new item starts with, Q16.16 fixed point. */
	UPROPERTY()
	int32 BaseMagnitude = 0;

	/**
	 * Never part of a GC cluster. Otherwise the first item page to build a cluster would absorb the definitions,
	 * every other page's cluster would reference that cluster, and rebuilding it would dissolve all of them
	 * (UObjectClusters.cpp:136-178, 741-751; 02_System_Architecture.md section 4.2).
	 */
	virtual bool CanBeInCluster() const override { return false; }
};
