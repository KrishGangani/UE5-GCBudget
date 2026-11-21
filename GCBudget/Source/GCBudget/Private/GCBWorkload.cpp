// P04 GCBudget (workload) - GCBWorkload.

#include "GCBWorkload.h"

#include "Misc/Crc.h"

namespace GCBWorkload
{
	FItemParams MakeItemParams(uint32 Seed, int32 Lane, int32 LaneGeneration)
	{
		// Three chained hashes of (Seed, Lane, Generation): independent of every other lane and of call order.
		const uint32 H1 = Hash32(Hash32(Seed + 0x9E3779B9u * static_cast<uint32>(Lane)) ^ (0x85EBCA6Bu * static_cast<uint32>(LaneGeneration + 1)));
		const uint32 H2 = Hash32(H1);
		const uint32 H3 = Hash32(H2);

		FItemParams Params;
		Params.DefIndex = static_cast<int32>(H1 % NumDefinitions);
		const int32 Lifetime = MinLifetimeTicks + static_cast<int32>(H2 % static_cast<uint32>(MaxLifetimeTicks - MinLifetimeTicks + 1));
		Params.TicksLeft = (LaneGeneration == 0) ? 1 + static_cast<int32>(H3 % static_cast<uint32>(Lifetime)) : Lifetime;
		Params.Charges = 1 + static_cast<int32>((H1 >> 8) % 5u);
		return Params;
	}

	void MakeDefinition(uint32 Seed, int32 DefIndex, int32& OutDecayQ16, int32& OutBaseMagnitude)
	{
		const uint32 H = Hash32(Hash32(Seed ^ 0xDEF1u) + static_cast<uint32>(DefIndex));
		OutDecayQ16 = 1 + static_cast<int32>(H % 32u);                        // 0.0015% .. 0.05% per tick
		OutBaseMagnitude = (64 + static_cast<int32>((H >> 8) % 64u)) << 16;   // 64 .. 127, Q16.16
	}

	uint32 ChecksumItem(uint32 Crc, int32 Serial, int32 DefIndex, int32 TicksLeft, int32 Magnitude, int32 Charges)
	{
		const int32 Fields[5] = { Serial, DefIndex, TicksLeft, Magnitude, Charges };
		return FCrc::MemCrc32(Fields, static_cast<int32>(sizeof(Fields)), Crc);
	}
}
