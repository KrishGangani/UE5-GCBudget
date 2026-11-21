// P04 GCBudget (baselines U0/U1/U2) - the store that keeps one UObject per item.

#pragma once

#include "GCBItemObject.h"
#include "GCBItemStore.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "GCBObjectItemStore.generated.h"

/**
 * One UGCBItemObject per lane, in pages of UGCBItemPage::Capacity (lane = page * Capacity + slot).
 * Expiry calls MarkAsGarbage() on the old object (as actors are on destroy, so weak references fail at once) and
 * puts a new object in its slot. The old object becomes garbage for the next GC cycle to find and purge.
 *
 * U1 (bClusterPages): every page is a GC cluster root. Two engine rules shape it:
 *  - a cluster member stays reachable while its cluster is kept, even after expiry (GarbageCollection.cpp:4324-4346),
 *    so PrepareForGC() dissolves and rebuilds the cluster of every page that changed, just before each cycle;
 *  - a cluster root's own references are never traced (GarbageCollection.cpp:3130-3139), so an item that is not a
 *    member would be freed while its page still points at it. Every replacement item therefore joins its page's
 *    cluster the moment it is created (AddToCluster), and no GC, requested or not, can free a live item.
 * That maintenance is part of what U1 costs.
 * The probe's stable reference is the engine's own: TWeakObjectPtr (index + serial number).
 */
UCLASS()
class UGCBObjectItemStore : public UGCBItemStore
{
	GENERATED_BODY()

public:
	/** U1: pages are GC cluster roots, rebuilt before each cycle when they changed. Set before Populate. */
	bool bClusterPages = false;

	virtual int32 Update(int32 Ticks) override;
	virtual FGCBProbeResult RunProbe(int32 Lookups) override;
	virtual void PrepareForGC() override;
	virtual uint32 Checksum() const override;

	/** Clusters actually built in the last PrepareForGC (U1); a page below gc.MinGCClusterSize gets none. */
	int32 GetClustersRebuilt() const { return ClustersRebuilt; }

protected:
	virtual void PopulateItems() override;
	virtual void CaptureProbeRef(int32 PoolIndex, int32 Lane) override;

private:
	/** A new item for Lane in Page, from the workload's parameters. */
	UGCBItemObject* CreateItem(UGCBItemPage* Page, const GCBWorkload::FItemParams& Params, int32 Serial) const;

	UGCBItemObject* GetItem(int32 Lane) const
	{
		return Pages[Lane / UGCBItemPage::Capacity]->Items[Lane % UGCBItemPage::Capacity];
	}

	UPROPERTY()
	TArray<TObjectPtr<UGCBItemPage>> Pages;

	/** The probe's references: not UPROPERTY on purpose, a weak reference must not keep its item alive. */
	TArray<TWeakObjectPtr<UGCBItemObject>> ProbeRefs;

	int32 ClustersRebuilt = 0;

	/** U1 with gc.CreateGCClusters 0 logs its error once per store, so every such run says so. */
	bool bWarnedNoClusters = false;
};
