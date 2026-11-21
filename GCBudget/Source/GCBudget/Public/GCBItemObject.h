// P04 GCBudget (baselines U0/U1/U2) - one UObject per item, grouped into pages.

#pragma once

#include "UObject/Object.h"
#include "GCBItemObject.generated.h"

class UGCBItemDef;

/**
 * One item as its own UObject: the design most projects start with (configuration U0). The same fields as the
 * struct stores, plus everything a UObject brings (01b_Research_Findings.md section 5).
 */
UCLASS()
class UGCBItemObject : public UObject
{
	GENERATED_BODY()

public:
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

	/** Epic's recipe for runtime clusters: the class must allow being in one (Phase2_Raw_Notes/D_existing_work.md D2). */
	virtual bool CanBeInCluster() const override { return true; }
};

/**
 * Up to Capacity items. In U1 each page is a GC cluster root; U0 and U2 use the same pages without clusters, so
 * clustering is the only difference between U0 and U1.
 */
UCLASS()
class UGCBItemPage : public UObject
{
	GENERATED_BODY()

public:
	static constexpr int32 Capacity = 1024;

	UPROPERTY()
	TArray<TObjectPtr<UGCBItemObject>> Items;

	/** An item was replaced since the page's cluster was last built (U1): the cluster must be rebuilt. */
	bool bChanged = true;
};
