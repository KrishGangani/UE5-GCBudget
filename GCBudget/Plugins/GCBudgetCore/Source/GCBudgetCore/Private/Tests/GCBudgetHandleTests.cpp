// GCBudgetCore - automation tests for FGCBudgetHandle and FGCBudgetSlotAllocator (no world needed).
// Run: Tools\run_tests.bat (editor, headless), filter "GCBudget.Handles".

#include "GCBudgetHandle.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GCBudgetHandleTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCBudgetHandleDefaultTest, "GCBudget.Handles.DefaultIsInvalid", GCBudgetHandleTests::Flags)
bool FGCBudgetHandleDefaultTest::RunTest(const FString& Parameters)
{
	FGCBudgetSlotAllocator Slots;
	const FGCBudgetHandle Default;
	TestFalse(TEXT("a default handle is not set"), Default.IsSet());
	TestFalse(TEXT("a default handle is invalid on an empty allocator"), Slots.IsValid(Default));
	Slots.Allocate();
	TestFalse(TEXT("a default handle stays invalid when slots exist"), Slots.IsValid(Default));
	TestEqual(TEXT("Resolve(default) is INDEX_NONE"), Slots.Resolve(Default), INDEX_NONE);
	TestFalse(TEXT("generation 0 never matches"), Slots.IsValid(FGCBudgetHandle(0, 0)));
	TestFalse(TEXT("releasing a default handle does nothing"), Slots.Release(Default));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCBudgetHandleAllocateResolveTest, "GCBudget.Handles.AllocateResolve", GCBudgetHandleTests::Flags)
bool FGCBudgetHandleAllocateResolveTest::RunTest(const FString& Parameters)
{
	FGCBudgetSlotAllocator Slots;
	const FGCBudgetHandle A = Slots.Allocate();
	const FGCBudgetHandle B = Slots.Allocate();
	TestEqual(TEXT("first slot is 0"), A.Index, 0);
	TestEqual(TEXT("second slot is 1"), B.Index, 1);
	TestEqual(TEXT("generations start at 1"), A.Generation, 1);
	TestEqual(TEXT("Resolve(A)"), Slots.Resolve(A), 0);
	TestEqual(TEXT("Resolve(B)"), Slots.Resolve(B), 1);
	TestEqual(TEXT("Num"), Slots.Num(), 2);
	TestEqual(TEXT("NumSlots"), Slots.NumSlots(), 2);
	TestTrue(TEXT("GetHandle returns the current handle"), Slots.GetHandle(1) == B);
	TestFalse(TEXT("an out-of-range index is invalid"), Slots.IsValid(FGCBudgetHandle(7, 1)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCBudgetHandleStaleTest, "GCBudget.Handles.ReleaseMakesStale", GCBudgetHandleTests::Flags)
bool FGCBudgetHandleStaleTest::RunTest(const FString& Parameters)
{
	FGCBudgetSlotAllocator Slots;
	const FGCBudgetHandle A = Slots.Allocate();
	TestTrue(TEXT("release succeeds once"), Slots.Release(A));
	TestFalse(TEXT("a released handle is stale"), Slots.IsValid(A));
	TestEqual(TEXT("Resolve(stale) is INDEX_NONE"), Slots.Resolve(A), INDEX_NONE);
	TestFalse(TEXT("a second release fails"), Slots.Release(A));
	TestEqual(TEXT("Num after release"), Slots.Num(), 0);
	TestEqual(TEXT("the slot still exists"), Slots.NumSlots(), 1);
	TestFalse(TEXT("GetHandle of a free slot is invalid"), Slots.GetHandle(0).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCBudgetHandleLifoTest, "GCBudget.Handles.LifoReuseNewGeneration", GCBudgetHandleTests::Flags)
bool FGCBudgetHandleLifoTest::RunTest(const FString& Parameters)
{
	FGCBudgetSlotAllocator Slots;
	const FGCBudgetHandle A = Slots.Allocate();   // slot 0
	const FGCBudgetHandle B = Slots.Allocate();   // slot 1
	Slots.Release(A);
	Slots.Release(B);
	const FGCBudgetHandle C = Slots.Allocate();   // LIFO: slot 1 (released last)
	const FGCBudgetHandle D = Slots.Allocate();   // then slot 0
	TestEqual(TEXT("LIFO reuses the most recently released slot first"), C.Index, 1);
	TestEqual(TEXT("then the one before"), D.Index, 0);
	TestEqual(TEXT("a reused slot has a new generation"), C.Generation, 2);
	TestFalse(TEXT("the old handle to the reused slot stays stale"), Slots.IsValid(B));
	TestTrue(TEXT("the new handle is current"), Slots.IsValid(C));
	TestEqual(TEXT("no slot was appended"), Slots.NumSlots(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCBudgetHandleResetTest, "GCBudget.Handles.Reset", GCBudgetHandleTests::Flags)
bool FGCBudgetHandleResetTest::RunTest(const FString& Parameters)
{
	FGCBudgetSlotAllocator Slots;
	Slots.Reserve(16);
	const FGCBudgetHandle A = Slots.Allocate();
	Slots.Reset();
	TestEqual(TEXT("Num after Reset"), Slots.Num(), 0);
	TestEqual(TEXT("NumSlots after Reset"), Slots.NumSlots(), 0);
	TestFalse(TEXT("an old handle is invalid after Reset"), Slots.IsValid(A));
	TestEqual(TEXT("allocation starts again at slot 0"), Slots.Allocate().Index, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCBudgetHandleRandomModelTest, "GCBudget.Handles.RandomAgainstModel", GCBudgetHandleTests::Flags)
bool FGCBudgetHandleRandomModelTest::RunTest(const FString& Parameters)
{
	// One million random operations, each checked against a simple model: the set of live handles and every handle
	// ever issued. A live handle must resolve to its own index; every other issued handle must resolve to INDEX_NONE.
	constexpr int32 Operations = 1000000;
	FRandomStream Random(1234);
	FGCBudgetSlotAllocator Slots;
	TArray<FGCBudgetHandle> Live;
	TArray<FGCBudgetHandle> Dead;   // released handles (sampled checks)
	int32 Mismatches = 0;

	for (int32 Step = 0; Step < Operations; ++Step)
	{
		const int32 Roll = Random.RandRange(0, 99);
		if (Roll < 45 || Live.Num() == 0)
		{
			Live.Add(Slots.Allocate());
		}
		else if (Roll < 90)
		{
			const int32 Pick = Random.RandRange(0, Live.Num() - 1);
			const FGCBudgetHandle Handle = Live[Pick];
			Live.RemoveAtSwap(Pick, EAllowShrinking::No);
			Mismatches += Slots.Release(Handle) ? 0 : 1;
			if (Dead.Num() < 4096) { Dead.Add(Handle); } else { Dead[Random.RandRange(0, Dead.Num() - 1)] = Handle; }
		}
		else
		{
			// A check: a random live handle resolves to itself, and a random dead handle is stale.
			if (Live.Num() > 0)
			{
				const FGCBudgetHandle Handle = Live[Random.RandRange(0, Live.Num() - 1)];
				Mismatches += (Slots.Resolve(Handle) == Handle.Index) ? 0 : 1;
			}
			if (Dead.Num() > 0)
			{
				Mismatches += (Slots.Resolve(Dead[Random.RandRange(0, Dead.Num() - 1)]) == INDEX_NONE) ? 0 : 1;
			}
		}
		Mismatches += (Slots.Num() == Live.Num()) ? 0 : 1;
	}

	// Final sweep: every live handle is current.
	for (const FGCBudgetHandle& Handle : Live)
	{
		Mismatches += Slots.IsValid(Handle) ? 0 : 1;
	}

	TestEqual(TEXT("mismatches against the model over 1,000,000 operations"), Mismatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCBudgetHandleWrapTest, "GCBudget.Handles.GenerationWrapsToOne", GCBudgetHandleTests::Flags)
bool FGCBudgetHandleWrapTest::RunTest(const FString& Parameters)
{
	// Phase 7: the generation's wrap-around, reached with the test hook instead of 2^31 releases of one slot.
	FGCBudgetSlotAllocator Slots;
	const FGCBudgetHandle FirstLife = Slots.Allocate();                 // slot 0, generation 1
	Slots.SetGenerationForTesting(FirstLife.Index, MAX_int32);
	const FGCBudgetHandle LastLife = Slots.GetHandle(FirstLife.Index);  // the same slot at generation MAX_int32
	TestEqual(TEXT("the hook set the generation"), LastLife.Generation, MAX_int32);
	TestTrue(TEXT("a handle at MAX_int32 is current"), Slots.IsValid(LastLife));
	TestTrue(TEXT("releasing at MAX_int32 succeeds (no overflow)"), Slots.Release(LastLife));

	const FGCBudgetHandle Wrapped = Slots.Allocate();
	TestEqual(TEXT("LIFO reuses the same slot"), Wrapped.Index, FirstLife.Index);
	TestEqual(TEXT("the generation wraps to 1, never to 0"), Wrapped.Generation, 1);
	TestFalse(TEXT("the MAX_int32 handle is stale"), Slots.IsValid(LastLife));
	TestFalse(TEXT("a default handle is still invalid"), Slots.IsValid(FGCBudgetHandle()));
	TestFalse(TEXT("generation 0 still never matches"), Slots.IsValid(FGCBudgetHandle(FirstLife.Index, 0)));

	// The documented limit (GCBudgetHandle.h, 02 section 12): after a full wrap, a handle from the slot's first life
	// matches again. The test states it rather than hiding it.
	TestTrue(TEXT("documented limit: a handle 2^31 - 1 releases old matches again"), Slots.IsValid(FirstLife));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCBudgetHandleGrowShrinkTest, "GCBudget.Handles.GrowShrinkRegrow", GCBudgetHandleTests::Flags)
bool FGCBudgetHandleGrowShrinkTest::RunTest(const FString& Parameters)
{
	// Phase 7: an allocator that grows, shrinks (holes) and grows again, which the benchmark workload never does.
	FGCBudgetSlotAllocator Slots;
	TArray<FGCBudgetHandle> First;
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		First.Add(Slots.Allocate());
	}
	for (int32 Index = 0; Index < 1000; Index += 2)
	{
		Slots.Release(First[Index]);                                     // every other slot becomes a hole
	}
	TestEqual(TEXT("live after shrinking"), Slots.Num(), 500);
	TestEqual(TEXT("slots kept after shrinking"), Slots.NumSlots(), 1000);
	int32 WrongState = 0;
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		WrongState += (Slots.IsValid(First[Index]) == (Index % 2 == 1)) ? 0 : 1;
	}
	TestEqual(TEXT("released handles stale, kept handles current"), WrongState, 0);

	TArray<FGCBudgetHandle> Regrown;
	for (int32 Index = 0; Index < 700; ++Index)
	{
		Regrown.Add(Slots.Allocate());
	}
	int32 Reused = 0;
	int32 Appended = 0;
	for (const FGCBudgetHandle& Handle : Regrown)
	{
		Reused += (Handle.Index < 1000 && Handle.Generation == 2) ? 1 : 0;
		Appended += (Handle.Index >= 1000 && Handle.Generation == 1) ? 1 : 0;
	}
	TestEqual(TEXT("the 500 holes are refilled first, with generation 2"), Reused, 500);
	TestEqual(TEXT("then 200 new slots are appended, with generation 1"), Appended, 200);
	TestEqual(TEXT("slots after regrowing"), Slots.NumSlots(), 1200);
	TestEqual(TEXT("live after regrowing"), Slots.Num(), 1200);
	int32 StaleRevived = 0;
	for (int32 Index = 0; Index < 1000; Index += 2)
	{
		StaleRevived += Slots.IsValid(First[Index]) ? 1 : 0;
	}
	TestEqual(TEXT("no handle from before the shrink is current again"), StaleRevived, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
