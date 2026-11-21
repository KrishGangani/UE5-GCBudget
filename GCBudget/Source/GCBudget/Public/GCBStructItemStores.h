// P04 GCBudget - item stores that keep items as structs in one array, addressed by generational handles.

#pragma once

#include "GCBItemDef.h"
#include "GCBItemStore.h"
#include "GCBudgetHandle.h"
#include "GCBStructItemStores.generated.h"

/**
 * Configuration S1's element: the same fields as UGCBItemObject, as a plain struct, but still holding one UObject
 * reference (its definition). A UPROPERTY array of these is walked element by element by the GC
 * (GarbageCollection.cpp:6869-6876), so S1 separates "number of UObjects" from "number of references" (H2).
 */
USTRUCT()
struct FGCBItemDataRef
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UGCBItemDef> Def;

	UPROPERTY()
	int32 TicksLeft = 0;

	/** Q16.16 fixed point. */
	UPROPERTY()
	int32 Magnitude = 0;

	UPROPERTY()
	int32 Charges = 0;

	/** Run-unique id of this item (GCBWorkload::MakeSerial), checked by the lookup probe. */
	UPROPERTY()
	int32 Serial = 0;
};

/**
 * Configuration S0's element, the proposed design: the same fields, but the definition is an index into the store's
 * definition table instead of a pointer. No field is an object reference, so the GC schema of an array of these is
 * empty and the collector never looks inside it (01b_Research_Findings.md #5).
 */
USTRUCT()
struct FGCBItemData
{
	GENERATED_BODY()

	/** Position of the item's definition in the store's Defs (UGCBItemDef::Index). */
	UPROPERTY()
	int32 DefIndex = 0;

	UPROPERTY()
	int32 TicksLeft = 0;

	/** Q16.16 fixed point. */
	UPROPERTY()
	int32 Magnitude = 0;

	UPROPERTY()
	int32 Charges = 0;

	/** Run-unique id of this item (GCBWorkload::MakeSerial), checked by the lookup probe. */
	UPROPERTY()
	int32 Serial = 0;
};

/**
 * How the shared struct-store code reads and writes an element's definition, per element type. Declared before the
 * templates that call them: a qualified call is looked up where the template is defined.
 */
namespace GCBStructItem
{
	FORCEINLINE const UGCBItemDef* GetDef(const FGCBItemDataRef& Item, const TArray<TObjectPtr<UGCBItemDef>>& /*Defs*/) { return Item.Def; }
	FORCEINLINE void SetDef(FGCBItemDataRef& Item, UGCBItemDef* Def) { Item.Def = Def; }

	FORCEINLINE const UGCBItemDef* GetDef(const FGCBItemData& Item, const TArray<TObjectPtr<UGCBItemDef>>& Defs) { return Defs[Item.DefIndex]; }
	FORCEINLINE void SetDef(FGCBItemData& Item, UGCBItemDef* Def) { Item.DefIndex = Def->Index; }
}

/**
 * Shared code of the struct stores: lane = slot. Populate allocates slots 0..N-1 in order. An expiry releases the
 * slot and allocates it again at once: the allocator is LIFO, so the same slot comes back with a new generation,
 * and every outstanding handle to the old item becomes stale (FGCBudgetSlotAllocator).
 * The element arrays themselves are UPROPERTYs of the concrete classes (a UCLASS can't be a template), and the
 * per-element-type code lives in the member templates below, so the two struct stores can't drift apart.
 */
UCLASS(Abstract)
class UGCBStructItemStoreBase : public UGCBItemStore
{
	GENERATED_BODY()

protected:
	template <typename TItem>
	void PopulateStructItems(TArray<TItem>& Items)
	{
		Items.Reset(NumLanes);
		Slots.Reset();
		Slots.Reserve(NumLanes);
		ProbeHandles.Reset(ProbePoolSize);   // a second Populate starts from scratch, like the object store
		for (int32 Lane = 0; Lane < NumLanes; ++Lane)
		{
			const FGCBudgetHandle Handle = Slots.Allocate();
			check(Handle.Index == Lane);
			Items.AddDefaulted();
			WriteItem(Items[Lane], GCBWorkload::MakeItemParams(Seed, Lane, 0), CurrentSerial(Lane));
		}
	}

	template <typename TItem>
	int32 UpdateStructItems(TArray<TItem>& Items, int32 Ticks)
	{
		int32 Expired = 0;
		const int32 Count = Items.Num();
		for (int32 Slot = 0; Slot < Count; ++Slot)
		{
			if (!Slots.IsSlotAlive(Slot))
			{
				continue;   // never true in this workload (expired slots are refilled at once), but a store must allow holes
			}
			TItem& Item = Items[Slot];
			if (GCBWorkload::AgeItem(Item.TicksLeft, Item.Magnitude, GCBStructItem::GetDef(Item, Defs)->DecayQ16, Ticks))
			{
				int32 Serial = 0;
				const GCBWorkload::FItemParams Params = NextLife(Slot, Serial);
				Slots.Release(Slots.GetHandle(Slot));
				const FGCBudgetHandle Reused = Slots.Allocate();   // LIFO: the slot just released
				check(Reused.Index == Slot);
				WriteItem(Items[Reused.Index], Params, Serial);
				++Expired;
			}
		}
		return Expired;
	}

	template <typename TItem>
	FGCBProbeResult RunStructProbe(const TArray<TItem>& Items, int32 Lookups)
	{
		return ProbeLoop(Lookups, [this, &Items](int32 PoolIndex, int32& OutMagnitude) -> int32
		{
			const int32 Slot = Slots.Resolve(ProbeHandles[PoolIndex]);   // INDEX_NONE if the handle is stale
			if (Slot == INDEX_NONE)
			{
				return INDEX_NONE;
			}
			const TItem& Item = Items[Slot];
			OutMagnitude = Item.Magnitude;
			return Item.Serial;
		});
	}

	template <typename TItem>
	uint32 StructChecksum(const TArray<TItem>& Items) const
	{
		uint32 Crc = 0;
		for (int32 Lane = 0; Lane < NumLanes; ++Lane)
		{
			const TItem& Item = Items[Lane];
			Crc = GCBWorkload::ChecksumItem(Crc, Item.Serial, GCBStructItem::GetDef(Item, Defs)->Index, Item.TicksLeft, Item.Magnitude, Item.Charges);
		}
		return Crc;
	}

	virtual void CaptureProbeRef(int32 PoolIndex, int32 Lane) override
	{
		if (PoolIndex >= ProbeHandles.Num())
		{
			ProbeHandles.SetNum(PoolIndex + 1);
		}
		ProbeHandles[PoolIndex] = Slots.GetHandle(Lane);
	}

	/** Slot bookkeeping for the concrete class's element array (lane = slot). */
	FGCBudgetSlotAllocator Slots;

	/** The probe's references: plain handles, which keep nothing alive. */
	TArray<FGCBudgetHandle> ProbeHandles;

private:
	template <typename TItem>
	void WriteItem(TItem& Item, const GCBWorkload::FItemParams& Params, int32 Serial) const
	{
		UGCBItemDef* Def = Defs[Params.DefIndex];
		GCBStructItem::SetDef(Item, Def);
		Item.TicksLeft = Params.TicksLeft;
		Item.Magnitude = Def->BaseMagnitude;
		Item.Charges = Params.Charges;
		Item.Serial = Serial;
	}
};

/** Configuration S1: structs in one array, each still holding a TObjectPtr to its definition. */
UCLASS()
class UGCBStructRefItemStore : public UGCBStructItemStoreBase
{
	GENERATED_BODY()

public:
	virtual int32 Update(int32 Ticks) override { return UpdateStructItems(Items, Ticks); }
	virtual FGCBProbeResult RunProbe(int32 Lookups) override { return RunStructProbe(Items, Lookups); }
	virtual uint32 Checksum() const override { return StructChecksum(Items); }

protected:
	virtual void PopulateItems() override { PopulateStructItems(Items); }

private:
	/** A UPROPERTY so the GC sees each element's definition reference: that is the cost S1 exists to measure. */
	UPROPERTY()
	TArray<FGCBItemDataRef> Items;
};

/** Configuration S0, the proposed store: structs in one array, the definition by index, nothing for the GC to trace. */
UCLASS()
class UGCBStructItemStore : public UGCBStructItemStoreBase
{
	GENERATED_BODY()

public:
	virtual int32 Update(int32 Ticks) override { return UpdateStructItems(Items, Ticks); }
	virtual FGCBProbeResult RunProbe(int32 Lookups) override { return RunStructProbe(Items, Lookups); }
	virtual uint32 Checksum() const override { return StructChecksum(Items); }

protected:
	virtual void PopulateItems() override { PopulateStructItems(Items); }

private:
	/** A UPROPERTY for reflection and saving. It holds no object reference, so the GC has nothing to trace in it. */
	UPROPERTY()
	TArray<FGCBItemData> Items;
};
