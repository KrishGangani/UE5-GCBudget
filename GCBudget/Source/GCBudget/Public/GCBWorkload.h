// P04 GCBudget (workload) - the item model every store simulates, as pure functions.

#pragma once

#include "CoreMinimal.h"

/**
 * The workload of 01_Problem_Analysis.md section 6.1, deliberately minimal: a timed "effect payload" with a
 * remaining time, a decaying magnitude and a charge count. It is not an inventory system.
 *
 * Integer math only: ticks for time, Q16.16 fixed point for magnitude. Every store therefore computes
 * bit-identical state whatever its memory layout or the compiler's floating-point choices, and the end-of-run
 * checksum can prove that all configurations did the same work (success criterion SC7).
 *
 * Items are identified by a lane (0..N-1). A lane's items are a pure function of (Seed, Lane, LaneGeneration),
 * so a store's iteration order or slot reuse can never change which item a lane gets.
 */
namespace GCBWorkload
{
	/** Shared definitions (UGCBItemDef) in every run. */
	constexpr int32 NumDefinitions = 16;

	/** The simulation's tick: one fixed 60 Hz frame. */
	constexpr int32 TicksPerSecond = 60;

	/** Lifetimes are uniform in [50, 150] s (mean 100 s): 1% of the items expire per second. */
	constexpr int32 MinLifetimeTicks = 50 * TicksPerSecond;
	constexpr int32 MaxLifetimeTicks = 150 * TicksPerSecond;

	/** What a new item starts with. */
	struct FItemParams
	{
		int32 DefIndex = 0;
		int32 TicksLeft = 0;
		int32 Charges = 0;
	};

	/** A 32-bit integer mixer (MurmurHash3's finaliser): a good spread from consecutive inputs. */
	FORCEINLINE uint32 Hash32(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x85EBCA6Bu;
		X ^= X >> 13;
		X *= 0xC2B2AE35u;
		X ^= X >> 16;
		return X;
	}

	/**
	 * The item lane Lane gets in its LaneGeneration-th life. Generation 0 (the initial population) starts somewhere
	 * inside its lifetime, so expiries are spread out from the first frame instead of arriving all at once.
	 */
	FItemParams MakeItemParams(uint32 Seed, int32 Lane, int32 LaneGeneration);

	/** Definition DefIndex's decay and starting magnitude (a pure function of Seed and DefIndex). */
	void MakeDefinition(uint32 Seed, int32 DefIndex, int32& OutDecayQ16, int32& OutBaseMagnitude);

	/** A run-unique serial number for a lane's generation (used to check that a lookup found the right item). */
	FORCEINLINE int32 MakeSerial(int32 Lane, int32 LaneGeneration, int32 NumLanes)
	{
		return LaneGeneration * NumLanes + Lane;
	}

	/** Advances one item by Ticks. Returns true when it has expired. */
	FORCEINLINE bool AgeItem(int32& TicksLeft, int32& Magnitude, int32 DecayQ16, int32 Ticks)
	{
		TicksLeft -= Ticks;
		Magnitude -= static_cast<int32>((static_cast<int64>(Magnitude) * DecayQ16 * Ticks) >> 16);
		return TicksLeft <= 0;
	}

	/** Folds one item's state into a running checksum (FCrc-based, order-sensitive). */
	uint32 ChecksumItem(uint32 Crc, int32 Serial, int32 DefIndex, int32 TicksLeft, int32 Magnitude, int32 Charges);
}
