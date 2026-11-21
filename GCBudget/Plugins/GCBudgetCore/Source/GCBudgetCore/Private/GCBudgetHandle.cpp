// GCBudgetCore - FGCBudgetSlotAllocator.

#include "GCBudgetHandle.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GCBudgetHandle)

FGCBudgetHandle FGCBudgetSlotAllocator::Allocate()
{
	int32 Index;
	if (FreeSlots.Num() > 0)
	{
		Index = FreeSlots.Pop(EAllowShrinking::No);   // LIFO: the most recently released slot
	}
	else
	{
		Index = Generations.Add(1);                   // a new slot starts at generation 1 (0 never matches)
		Alive.Add(false);
	}
	Alive[Index] = true;
	++NumAlive;
	return FGCBudgetHandle(Index, Generations[Index]);
}

bool FGCBudgetSlotAllocator::Release(FGCBudgetHandle Handle)
{
	if (!IsValid(Handle))
	{
		return false;   // stale, never issued, or already released: nothing to free
	}
	const int32 Index = Handle.Index;
	Alive[Index] = false;
	// Every outstanding handle to this slot carries the old generation and is now stale. Wrap to 1, never to 0.
	Generations[Index] = (Generations[Index] == MAX_int32) ? 1 : Generations[Index] + 1;
	FreeSlots.Push(Index);
	--NumAlive;
	return true;
}

void FGCBudgetSlotAllocator::Reserve(int32 InNumSlots)
{
	Generations.Reserve(InNumSlots);
	FreeSlots.Reserve(InNumSlots);
	Alive.Reserve(InNumSlots);
}

void FGCBudgetSlotAllocator::Reset()
{
	Generations.Reset();
	FreeSlots.Reset();
	Alive.Reset();
	NumAlive = 0;
}
