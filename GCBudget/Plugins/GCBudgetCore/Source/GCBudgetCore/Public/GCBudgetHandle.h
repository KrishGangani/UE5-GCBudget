// GCBudgetCore - generational handles for items kept in a struct array instead of one UObject each.

#pragma once

#include "Containers/BitArray.h"
#include "CoreMinimal.h"
#include "GCBudgetHandle.generated.h"

#define UE_API GCBUDGETCORE_API

/**
 * A stable, copyable reference to one item in a struct store: the slot's index plus the slot's generation when the
 * handle was issued. The same shape as the engine's FWeakObjectPtr (ObjectIndex + ObjectSerialNumber) and
 * FMassEntityHandle (Index + SerialNumber), on purpose (01b_Research_Findings.md #15).
 *
 * A USTRUCT so it can live in UPROPERTYs and pass through Blueprint as an opaque value. int32 fields, not uint32,
 * because Blueprint has no unsigned 32-bit type. A default handle (Index INDEX_NONE, Generation 0) is never valid.
 */
USTRUCT(BlueprintType)
struct UE_API FGCBudgetHandle
{
	GENERATED_BODY()

	FGCBudgetHandle() = default;
	FGCBudgetHandle(int32 InIndex, int32 InGeneration) : Index(InIndex), Generation(InGeneration) {}

	/** Slot index in the owner's array; INDEX_NONE for a handle that was never issued. */
	UPROPERTY()
	int32 Index = INDEX_NONE;

	/** The slot's generation when this handle was issued; generations start at 1, so 0 never matches. */
	UPROPERTY()
	int32 Generation = 0;

	/** True if the handle was ever issued. It may still be stale: only the allocator can say that. */
	bool IsSet() const { return Index != INDEX_NONE; }

	bool operator==(const FGCBudgetHandle& Other) const { return Index == Other.Index && Generation == Other.Generation; }
	bool operator!=(const FGCBudgetHandle& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FGCBudgetHandle& Handle)
	{
		return HashCombineFast(::GetTypeHash(Handle.Index), ::GetTypeHash(Handle.Generation));
	}
};

/**
 * Hands out slot indices with generations for an array the OWNER keeps, typically a UPROPERTY() TArray<FMyItem>.
 * The owner keeps its array the same length as NumSlots() (a new slot is always Items.Num()), so reflection,
 * garbage-collection visibility (for elements that hold object references) and Blueprint access stay correct by
 * construction. The allocator holds only integers, so it is unit-testable without a world.
 *
 *   Allocate()        reuses the most recently released slot (LIFO: the hottest memory) or appends one
 *   Release(Handle)   increments the slot's generation, so every outstanding handle to it becomes stale
 *   Resolve(Handle)   the slot index if the handle is current, else INDEX_NONE (inline: one bounds check, one bit,
 *                     one compare)
 *
 * Limit: a generation wraps after 2^31 - 1 releases of one slot, after which a very old handle could match again.
 */
class FGCBudgetSlotAllocator
{
public:
	/** Returns a handle to a free slot (reused LIFO, or appended). The caller writes its item at Handle.Index. */
	UE_API FGCBudgetHandle Allocate();

	/** Frees the handle's slot and invalidates every handle to it. Returns false if the handle was not current. */
	UE_API bool Release(FGCBudgetHandle Handle);

	/** True if the handle refers to a live slot with the same generation. */
	bool IsValid(FGCBudgetHandle Handle) const
	{
		return Generations.IsValidIndex(Handle.Index) && Alive[Handle.Index] && Generations[Handle.Index] == Handle.Generation;
	}

	/** The slot index of a current handle, or INDEX_NONE for a stale or never-issued one. */
	int32 Resolve(FGCBudgetHandle Handle) const
	{
		return IsValid(Handle) ? Handle.Index : INDEX_NONE;
	}

	/** True if the slot holds a live item. */
	bool IsSlotAlive(int32 Index) const
	{
		return Alive.IsValidIndex(Index) && Alive[Index];
	}

	/** The current handle of a live slot; a default (invalid) handle for a free or unknown slot. */
	FGCBudgetHandle GetHandle(int32 Index) const
	{
		return IsSlotAlive(Index) ? FGCBudgetHandle(Index, Generations[Index]) : FGCBudgetHandle();
	}

	/** Live items. */
	int32 Num() const { return NumAlive; }

	/** Slots ever created (live + free): the length the owner's array must have. */
	int32 NumSlots() const { return Generations.Num(); }

	/** Pre-sizes the bookkeeping for NumSlots slots, so a known population never reallocates. */
	UE_API void Reserve(int32 InNumSlots);

	/** Forgets every slot. Handles issued before are invalid afterwards (no slot exists for them). */
	UE_API void Reset();

private:
	/** Per slot: the generation a current handle must carry. Starts at 1; incremented on Release. */
	TArray<int32> Generations;

	/** Released slots, most recent last (LIFO reuse). */
	TArray<int32> FreeSlots;

	/** Per slot: does it hold a live item? */
	TBitArray<> Alive;

	int32 NumAlive = 0;
};

#undef UE_API
