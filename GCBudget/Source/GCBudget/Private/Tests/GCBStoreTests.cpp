// P04 GCBudget (tests) - store-level automation tests: stale references after removal, repopulating to a smaller and a
// larger N, and equal checksums across stores. Run: Tools\run_tests.bat (editor, headless), filter "GCBudget.Stores".

#include "GCBItemDef.h"
#include "GCBObjectItemStore.h"
#include "GCBStructItemStores.h"
#include "GCBWorkload.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GCBStoreTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;
	constexpr uint32 Seed = 7;

	/** The 16 shared definitions, made the way the director makes them. */
	TArray<TObjectPtr<UGCBItemDef>> MakeDefinitions()
	{
		TArray<TObjectPtr<UGCBItemDef>> Defs;
		for (int32 Index = 0; Index < GCBWorkload::NumDefinitions; ++Index)
		{
			UGCBItemDef* Def = NewObject<UGCBItemDef>(GetTransientPackage());
			Def->Index = Index;
			GCBWorkload::MakeDefinition(Seed, Index, Def->DecayQ16, Def->BaseMagnitude);
			Defs.Add(Def);
		}
		return Defs;
	}

	/** A fresh store of each tested kind: U0 (objects), S1 (structs with a reference), S0 (structs by index). U1's
	 *  clusters are left to the packaged -VERIFYGC runs (09 S4-7). */
	TArray<TPair<const TCHAR*, UGCBItemStore*>> MakeStores()
	{
		return {
			{ TEXT("U0"), NewObject<UGCBObjectItemStore>(GetTransientPackage()) },
			{ TEXT("S1"), NewObject<UGCBStructRefItemStore>(GetTransientPackage()) },
			{ TEXT("S0"), NewObject<UGCBStructItemStore>(GetTransientPackage()) },
		};
	}

	/** Advances a store Frames fixed-step frames, as the director does (one tick per frame, then the probe refresh). */
	void Run(UGCBItemStore* Store, int32 Frames)
	{
		for (int32 Frame = 1; Frame <= Frames; ++Frame)
		{
			Store->Update(1);
			Store->RefreshProbe(Frame);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCBStoreStaleAfterExpiryTest, "GCBudget.Stores.StaleAfterExpiry", GCBStoreTests::Flags)
bool FGCBStoreStaleAfterExpiryTest::RunTest(const FString& Parameters)
{
	// Every reference the probe holds must stop resolving once its item is removed, and resolve again once it is
	// re-pointed at the replacement: through weak pointers (U0) and through generational handles (S1, S0).
	const TArray<TObjectPtr<UGCBItemDef>> Defs = GCBStoreTests::MakeDefinitions();
	constexpr int32 Items = 1000;   // the probe pool is min(4096, N) = 1000 references
	for (const TPair<const TCHAR*, UGCBItemStore*>& Entry : GCBStoreTests::MakeStores())
	{
		UGCBItemStore* Store = Entry.Value;
		Store->Populate(Items, GCBStoreTests::Seed, Defs);

		const FGCBProbeResult Fresh = Store->RunProbe(Items);
		TestEqual(*FString::Printf(TEXT("%s: every fresh reference resolves"), Entry.Key), Fresh.Resolved, Items);
		TestEqual(*FString::Printf(TEXT("%s: no errors before removal"), Entry.Key), Fresh.Errors, 0);

		// One update longer than the longest lifetime: every item expires exactly once and is replaced.
		const int32 Expired = Store->Update(GCBWorkload::MaxLifetimeTicks + 1);
		TestEqual(*FString::Printf(TEXT("%s: every item expired once"), Entry.Key), Expired, Items);

		const FGCBProbeResult AfterRemoval = Store->RunProbe(Items);
		TestEqual(*FString::Printf(TEXT("%s: every reference to a removed item is rejected"), Entry.Key), AfterRemoval.Stale, Items);
		TestEqual(*FString::Printf(TEXT("%s: none resolves to the replacement"), Entry.Key), AfterRemoval.Resolved, 0);
		TestEqual(*FString::Printf(TEXT("%s: no errors after removal"), Entry.Key), AfterRemoval.Errors, 0);

		// Re-point the whole pool (8 references per frame) at the current items: they resolve again.
		for (int32 Frame = 1; Frame <= Items / UGCBItemStore::ProbeRefreshPerFrame; ++Frame)
		{
			Store->RefreshProbe(Frame);
		}
		const FGCBProbeResult Refreshed = Store->RunProbe(Items);
		TestEqual(*FString::Printf(TEXT("%s: re-pointed references resolve"), Entry.Key), Refreshed.Resolved, Items);
		TestEqual(*FString::Printf(TEXT("%s: no errors after re-pointing"), Entry.Key), Refreshed.Errors, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCBStoreRepopulateTest, "GCBudget.Stores.RepopulateShrinkGrow", GCBStoreTests::Flags)
bool FGCBStoreRepopulateTest::RunTest(const FString& Parameters)
{
	// A store populated again with fewer, then more items must behave exactly like a fresh store of that size: the
	// reset paths the S3-S5 review fixed (09 S4, S5).
	const TArray<TObjectPtr<UGCBItemDef>> Defs = GCBStoreTests::MakeDefinitions();
	const TArray<TPair<const TCHAR*, UGCBItemStore*>> Reused = GCBStoreTests::MakeStores();
	const TArray<TPair<const TCHAR*, UGCBItemStore*>> Fresh = GCBStoreTests::MakeStores();
	for (int32 Kind = 0; Kind < Reused.Num(); ++Kind)
	{
		const TCHAR* Name = Reused[Kind].Key;
		UGCBItemStore* Store = Reused[Kind].Value;

		Store->Populate(3000, GCBStoreTests::Seed, Defs);
		GCBStoreTests::Run(Store, 300);
		Store->Populate(500, GCBStoreTests::Seed, Defs);                 // shrink
		UGCBItemStore* Small = GCBStoreTests::MakeStores()[Kind].Value;
		Small->Populate(500, GCBStoreTests::Seed, Defs);
		TestEqual(*FString::Printf(TEXT("%s: Num after shrinking"), Name), Store->Num(), 500);
		TestEqual(*FString::Printf(TEXT("%s: shrunk = fresh, checksum"), Name), static_cast<int64>(Store->Checksum()), static_cast<int64>(Small->Checksum()));
		TestEqual(*FString::Printf(TEXT("%s: no probe errors after shrinking"), Name), Store->RunProbe(1000).Errors, 0);

		Store->Populate(2500, GCBStoreTests::Seed, Defs);                // grow
		UGCBItemStore* Large = Fresh[Kind].Value;
		Large->Populate(2500, GCBStoreTests::Seed, Defs);
		GCBStoreTests::Run(Store, 600);
		GCBStoreTests::Run(Large, 600);
		TestEqual(*FString::Printf(TEXT("%s: Num after growing"), Name), Store->Num(), 2500);
		TestEqual(*FString::Printf(TEXT("%s: grown = fresh, checksum after 600 frames"), Name), static_cast<int64>(Store->Checksum()), static_cast<int64>(Large->Checksum()));
		TestEqual(*FString::Printf(TEXT("%s: no probe errors after growing"), Name), Store->RunProbe(1000).Errors, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCBStoreChecksumsAgreeTest, "GCBudget.Stores.ChecksumsAgree", GCBStoreTests::Flags)
bool FGCBStoreChecksumsAgreeTest::RunTest(const FString& Parameters)
{
	// SC7 at unit level: the same seed and frames give the same checksum in every store, whatever its layout.
	const TArray<TObjectPtr<UGCBItemDef>> Defs = GCBStoreTests::MakeDefinitions();
	TArray<int64> Checksums;   // TestEqual has no uint32 overload (AutomationTest.h:1985-1987)
	for (const TPair<const TCHAR*, UGCBItemStore*>& Entry : GCBStoreTests::MakeStores())
	{
		Entry.Value->Populate(4000, GCBStoreTests::Seed, Defs);
		GCBStoreTests::Run(Entry.Value, 1200);
		Checksums.Add(static_cast<int64>(Entry.Value->Checksum()));
	}
	TestEqual(TEXT("U0 = S1"), Checksums[0], Checksums[1]);
	TestEqual(TEXT("U0 = S0"), Checksums[0], Checksums[2]);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
