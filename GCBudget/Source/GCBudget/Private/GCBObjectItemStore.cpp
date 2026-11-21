// P04 GCBudget (baselines U0/U1/U2) - UGCBObjectItemStore.

#include "GCBObjectItemStore.h"

#include "GCBItemDef.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectArray.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GCBObjectItemStore)

DEFINE_LOG_CATEGORY_STATIC(LogGCBObjectStore, Log, All);

void UGCBObjectItemStore::PopulateItems()
{
	const int32 NumPages = (NumLanes + UGCBItemPage::Capacity - 1) / UGCBItemPage::Capacity;
	Pages.Reset(NumPages);   // a second Populate starts from scratch, like the struct stores
	ProbeRefs.Reset(ProbePoolSize);
	for (int32 PageIndex = 0; PageIndex < NumPages; ++PageIndex)
	{
		UGCBItemPage* Page = NewObject<UGCBItemPage>(this);
		const int32 FirstLane = PageIndex * UGCBItemPage::Capacity;
		const int32 Count = FMath::Min(UGCBItemPage::Capacity, NumLanes - FirstLane);
		Page->Items.Reserve(Count);
		for (int32 Slot = 0; Slot < Count; ++Slot)
		{
			const int32 Lane = FirstLane + Slot;
			Page->Items.Add(CreateItem(Page, GCBWorkload::MakeItemParams(Seed, Lane, 0), CurrentSerial(Lane)));
		}
		Pages.Add(Page);
	}
}

UGCBItemObject* UGCBObjectItemStore::CreateItem(UGCBItemPage* Page, const GCBWorkload::FItemParams& Params, int32 Serial) const
{
	// Outer = the page: every item is in the world's package, so U1's CreateCluster can include it
	// (UObjectClusters.cpp:741-751).
	UGCBItemObject* Item = NewObject<UGCBItemObject>(Page);
	UGCBItemDef* Def = Defs[Params.DefIndex];
	Item->Def = Def;
	Item->TicksLeft = Params.TicksLeft;
	Item->Magnitude = Def->BaseMagnitude;
	Item->Charges = Params.Charges;
	Item->Serial = Serial;
	return Item;
}

int32 UGCBObjectItemStore::Update(int32 Ticks)
{
	int32 Expired = 0;
	for (int32 PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex)
	{
		UGCBItemPage* Page = Pages[PageIndex];
		TArray<TObjectPtr<UGCBItemObject>>& Items = Page->Items;
		for (int32 Slot = 0; Slot < Items.Num(); ++Slot)
		{
			UGCBItemObject* Item = Items[Slot];
			if (GCBWorkload::AgeItem(Item->TicksLeft, Item->Magnitude, Item->Def->DecayQ16, Ticks))
			{
				int32 Serial = 0;
				const GCBWorkload::FItemParams Params = NextLife(PageIndex * UGCBItemPage::Capacity + Slot, Serial);
				// Garbage now: weak references fail at once, and the next GC cycle collects the object.
				Item->MarkAsGarbage();
				UGCBItemObject* NewItem = CreateItem(Page, Params, Serial);
				Items[Slot] = NewItem;
				if (bClusterPages)
				{
					// The page's cluster never traces the page's own references, so the new item must be a member
					// (a no-op while the page has no cluster). UObjectClusters.cpp:877-900.
					NewItem->AddToCluster(Page);
				}
				Page->bChanged = true;
				++Expired;
			}
		}
	}
	return Expired;
}

void UGCBObjectItemStore::PrepareForGC()
{
	ClustersRebuilt = 0;
	if (!bClusterPages)
	{
		return;
	}

	// CreateCluster() asserts that clusters are enabled (UObjectClusters.cpp:929): never call it if someone set
	// gc.CreateGCClusters 0; the run then measures unclustered pages and says so in the log.
	static const IConsoleVariable* CreateClusters = IConsoleManager::Get().FindConsoleVariable(TEXT("gc.CreateGCClusters"));
	if (CreateClusters && CreateClusters->GetInt() == 0)
	{
		UE_CLOG(!bWarnedNoClusters, LogGCBObjectStore, Error, TEXT("U1: gc.CreateGCClusters is 0, so no clusters are built."));
		bWarnedNoClusters = true;
		return;
	}

	for (UGCBItemPage* Page : Pages)
	{
		if (!Page->bChanged)
		{
			continue;
		}
		// Dissolve first: an expired item is still in the old cluster, and a clustered object is kept alive while
		// its root is reachable. The new cluster contains only the items the page references now.
		if (GUObjectClusters.GetObjectCluster(Page))
		{
			GUObjectClusters.DissolveCluster(Page);
		}
		Page->CreateCluster();
		Page->bChanged = false;
		// CreateCluster frees a cluster below gc.MinGCClusterSize (UObjectClusters.cpp:958-995): count real ones only.
		ClustersRebuilt += GUObjectClusters.GetObjectCluster(Page) ? 1 : 0;
	}
}

uint32 UGCBObjectItemStore::Checksum() const
{
	uint32 Crc = 0;
	for (int32 Lane = 0; Lane < NumLanes; ++Lane)
	{
		const UGCBItemObject* Item = GetItem(Lane);
		Crc = GCBWorkload::ChecksumItem(Crc, Item->Serial, Item->Def->Index, Item->TicksLeft, Item->Magnitude, Item->Charges);
	}
	return Crc;
}

void UGCBObjectItemStore::CaptureProbeRef(int32 PoolIndex, int32 Lane)
{
	if (PoolIndex >= ProbeRefs.Num())
	{
		ProbeRefs.SetNum(PoolIndex + 1);
	}
	ProbeRefs[PoolIndex] = GetItem(Lane);
}

FGCBProbeResult UGCBObjectItemStore::RunProbe(int32 Lookups)
{
	return ProbeLoop(Lookups, [this](int32 PoolIndex, int32& OutMagnitude) -> int32
	{
		// TWeakObjectPtr::Get() checks the serial number and fails for garbage or destroyed objects.
		if (const UGCBItemObject* Item = ProbeRefs[PoolIndex].Get())
		{
			OutMagnitude = Item->Magnitude;
			return Item->Serial;
		}
		return INDEX_NONE;
	});
}
