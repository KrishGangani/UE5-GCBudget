// P04 GCBudget (benchmark) - UGCBBenchmarkSubsystem.

#include "GCBBenchmarkSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GCBItemDef.h"
#include "GCBObjectItemStore.h"
#include "GCBStructItemStores.h"
#include "GCBudgetTelemetrySubsystem.h"
#include "GCBWorkload.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Stats/Stats.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GCBBenchmarkSubsystem)

DEFINE_LOG_CATEGORY_STATIC(LogGCBBench, Log, All);

// Workload columns (GCBWorkload/Items, /Expired, /UpdateMs, ...) and the capture events BenchStart / BenchEnd.
CSV_DEFINE_CATEGORY(GCBWorkload, true);

namespace GCBBench
{
	/** Parks the engine's own GC timer so only the director starts cycles (02_System_Architecture.md section 6). */
	const TCHAR* const ParkedTimerSeconds = TEXT("3600");

	/** U2's incremental reachability slice: Epic's documented example, 2 ms (02 section 7). */
	const TCHAR* const IncrementalTimeLimit = TEXT("0.002");

	/** Frames to wait after BenchEnd before quitting, so the CSV profiler finishes writing its file. */
	constexpr int32 ExitDelayFrames = 3;

	/**
	 * Largest store and longest GC interval accepted. Together they keep U0's peak object count below the cooked
	 * game's 2,097,152-object limit (UObjectBase.cpp:1363, and the packaged log): 1,000,000 live items, plus at most 61.1 s of garbage at 1% per second (the engine
	 * timer when -GCBGCFrames=0; 3600 frames is 60 s) = 611,000, plus 977 pages and about 35,500 engine objects,
	 * is about 1.65 million.
	 */
	constexpr int32 MaxItems = 1000000;
	constexpr int32 MaxGCFrames = 3600;

	/**
	 * Sets a console variable from code (the highest priority short of the console) and records the value now in
	 * effect. Returns false if the variable doesn't exist; OutPrevious receives the value it had before.
	 */
	static bool SetConsoleVariable(const TCHAR* Name, const TCHAR* Value, FString& OutPrevious)
	{
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		if (!Variable)
		{
			UE_LOG(LogGCBBench, Error, TEXT("Console variable %s not found; the configuration is incomplete."), Name);
			return false;
		}
		OutPrevious = Variable->GetString();
		Variable->Set(Value, ECVF_SetByCode);
		UE_LOG(LogGCBBench, Display, TEXT("%s = %s (was %s)"), Name, *Variable->GetString(), *OutPrevious);
		CSV_METADATA(Name, *Variable->GetString());
		return true;
	}

	/**
	 * Escapes a string for a JSON value: a backslash before every quote and backslash, and a four-digit unicode
	 * escape for every control character. Not FString::ReplaceCharWithEscapedChar(): its set includes the escaped
	 * apostrophe (UnrealString.h.inl:1881), which JSON does not allow, and it leaves most control characters as is.
	 */
	static FString JsonEscape(const FString& Value)
	{
		FString Escaped;
		Escaped.Reserve(Value.Len());
		for (const TCHAR Char : Value)
		{
			if (Char == TEXT('"') || Char == TEXT('\\'))
			{
				Escaped.AppendChar(TEXT('\\'));
				Escaped.AppendChar(Char);
			}
			else if (Char < 0x20)
			{
				Escaped += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(Char));
			}
			else
			{
				Escaped.AppendChar(Char);
			}
		}
		return Escaped;
	}

	/** Records a console variable's current value in the log and the CSV metadata without changing it. */
	static void RecordConsoleVariable(const TCHAR* Name)
	{
		if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			UE_LOG(LogGCBBench, Display, TEXT("%s = %s (unchanged)"), Name, *Variable->GetString());
			CSV_METADATA(Name, *Variable->GetString());
		}
	}
}

bool UGCBBenchmarkSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGCBBenchmarkSubsystem::Deinitialize()
{
	// Hand back every console variable this run changed, so a world that outlives the benchmark (PIE, map travel)
	// never inherits a parked GC timer or incremental reachability.
	for (const TPair<FString, FString>& Saved : ChangedConsoleVariables)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Saved.Key))
		{
			Variable->Set(*Saved.Value, ECVF_SetByCode);
		}
	}
	ChangedConsoleVariables.Reset();
	Super::Deinitialize();
}

void UGCBBenchmarkSubsystem::SetGCVariable(const TCHAR* Name, const TCHAR* Value)
{
	FString Previous;
	if (GCBBench::SetConsoleVariable(Name, Value, Previous))
	{
		ChangedConsoleVariables.Emplace(Name, MoveTemp(Previous));
	}
}

UGCBBenchmarkSubsystem::FRunSettings UGCBBenchmarkSubsystem::ReadRunSettings()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FRunSettings Read;
	FParse::Value(CommandLine, TEXT("GCBConfig="), Read.ConfigName);
	FParse::Value(CommandLine, TEXT("GCBItems="), Read.Items);
	FParse::Value(CommandLine, TEXT("GCBSeed="), Read.Seed);
	FParse::Value(CommandLine, TEXT("GCBGCFrames="), Read.GCFrames);
	FParse::Value(CommandLine, TEXT("GCBProbe="), Read.ProbeLookups);
	FParse::Value(CommandLine, TEXT("GCBWarmup="), Read.WarmupSeconds);
	FParse::Value(CommandLine, TEXT("GCBCapture="), Read.CaptureSeconds);
	FParse::Value(CommandLine, TEXT("GCBRunTag="), Read.RunTag);
	Read.bExitWhenDone = FParse::Param(CommandLine, TEXT("GCBExit"));
	Read.bStopTraceBeforeExit = FParse::Param(CommandLine, TEXT("GCBStopTraceBeforeExit"));
	Read.Config = ParseConfig(Read.ConfigName);
	return Read;
}

EGCBConfig UGCBBenchmarkSubsystem::ParseConfig(const FString& Name)
{
	static const TPair<const TCHAR*, EGCBConfig> Known[] = {
		{ TEXT("E0"), EGCBConfig::E0 },
		{ TEXT("U0"), EGCBConfig::U0 },
		{ TEXT("U1"), EGCBConfig::U1 },
		{ TEXT("U2"), EGCBConfig::U2 },
		{ TEXT("S1"), EGCBConfig::S1 },
		{ TEXT("S0"), EGCBConfig::S0 },
	};
	for (const TPair<const TCHAR*, EGCBConfig>& Entry : Known)
	{
		if (Name.Equals(Entry.Key, ESearchCase::IgnoreCase))
		{
			return Entry.Value;
		}
	}
	return EGCBConfig::None;
}

bool UGCBBenchmarkSubsystem::ValidateRunSettings() const
{
	bool bValid = true;
	if (Settings.Config == EGCBConfig::None)
	{
		UE_LOG(LogGCBBench, Error, TEXT("-GCBConfig=%s is unknown or not built yet (this build supports: E0, U0, U1, U2, S1, S0)."), *Settings.ConfigName);
		bValid = false;
	}
	if (Settings.Config == EGCBConfig::E0 && Settings.Items != 0)
	{
		UE_LOG(LogGCBBench, Error, TEXT("E0 is the no-item configuration, but -GCBItems=%d was given."), Settings.Items);
		bValid = false;
	}
	if (Settings.Config != EGCBConfig::E0 && Settings.Config != EGCBConfig::None && (Settings.Items < 1 || Settings.Items > GCBBench::MaxItems))
	{
		UE_LOG(LogGCBBench, Error, TEXT("-GCBItems=%d is outside 1..%d for config %s."), Settings.Items, GCBBench::MaxItems, *Settings.ConfigName);
		bValid = false;
	}
	if (Settings.WarmupSeconds < 0.0 || Settings.CaptureSeconds <= 0.0 || Settings.GCFrames < 0 || Settings.GCFrames > GCBBench::MaxGCFrames || Settings.ProbeLookups < 0)
	{
		UE_LOG(LogGCBBench, Error, TEXT("Invalid run settings: warm-up %.3f s, capture %.3f s, GC every %d frames, %d lookups (need warm-up >= 0, capture > 0, frames 0..%d, lookups >= 0)."),
			Settings.WarmupSeconds, Settings.CaptureSeconds, Settings.GCFrames, Settings.ProbeLookups, GCBBench::MaxGCFrames);
		bValid = false;
	}
	if (Settings.Config == EGCBConfig::U1 && Settings.GCFrames == 0)
	{
		// U1 rebuilds its clusters only before the cycles the director requests; under the engine's own timer the
		// expired items would stay in their clusters for the whole run.
		UE_LOG(LogGCBBench, Error, TEXT("U1 needs the director's GC cadence (-GCBGCFrames > 0)."));
		bValid = false;
	}
	return bValid;
}

void UGCBBenchmarkSubsystem::ApplyGCSettings()
{
	// The director owns the cadence; 0 keeps the engine's own timer (61.1 s in BaseEngine.ini) for diagnostic runs.
	if (Settings.GCFrames > 0)
	{
		SetGCVariable(TEXT("gc.TimeBetweenPurgingPendingKillObjects"), GCBBench::ParkedTimerSeconds);
	}
	else
	{
		GCBBench::RecordConsoleVariable(TEXT("gc.TimeBetweenPurgingPendingKillObjects"));
	}

	if (Settings.Config == EGCBConfig::U2)
	{
		SetGCVariable(TEXT("gc.AllowIncrementalReachability"), TEXT("1"));
		SetGCVariable(TEXT("gc.IncrementalReachabilityTimeLimit"), GCBBench::IncrementalTimeLimit);
	}
	else
	{
		GCBBench::RecordConsoleVariable(TEXT("gc.AllowIncrementalReachability"));
		GCBBench::RecordConsoleVariable(TEXT("gc.IncrementalReachabilityTimeLimit"));
	}

	// Settings every configuration shares, recorded so each capture states its own GC environment.
	// gc.VerifyAssumptions must read 0 in timed runs: -NoVerifyGC (01b_Research_Findings.md #11).
	GCBBench::RecordConsoleVariable(TEXT("gc.VerifyAssumptions"));
	GCBBench::RecordConsoleVariable(TEXT("gc.AllowParallelGC"));
	GCBBench::RecordConsoleVariable(TEXT("gc.CreateGCClusters"));
	GCBBench::RecordConsoleVariable(TEXT("gc.IncrementalGCTimePerFrame"));
}

void UGCBBenchmarkSubsystem::CreateWorkload()
{
	// The shared definitions exist in every configuration (E0 included), so the object counts compare like for like.
	Definitions.Reserve(GCBWorkload::NumDefinitions);
	for (int32 Index = 0; Index < GCBWorkload::NumDefinitions; ++Index)
	{
		UGCBItemDef* Def = NewObject<UGCBItemDef>(this);
		Def->Index = Index;
		GCBWorkload::MakeDefinition(static_cast<uint32>(Settings.Seed), Index, Def->DecayQ16, Def->BaseMagnitude);
		Definitions.Add(Def);
	}

	if (Settings.Config == EGCBConfig::E0)
	{
		return;
	}

	if (Settings.Config == EGCBConfig::S1)
	{
		Store = NewObject<UGCBStructRefItemStore>(this);
	}
	else if (Settings.Config == EGCBConfig::S0)
	{
		Store = NewObject<UGCBStructItemStore>(this);
	}
	else
	{
		UGCBObjectItemStore* ObjectStore = NewObject<UGCBObjectItemStore>(this);
		ObjectStore->bClusterPages = (Settings.Config == EGCBConfig::U1);
		Store = ObjectStore;
	}

	const double Start = FPlatformTime::Seconds();
	Store->Populate(Settings.Items, static_cast<uint32>(Settings.Seed), Definitions);
	Totals.PopulateMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	UE_LOG(LogGCBBench, Display, TEXT("Populated %d items in %.1f ms"), Store->Num(), Totals.PopulateMs);
	// The element sizes 04 section 30 states, read from the compiler rather than assumed.
	UE_LOG(LogGCBBench, Display, TEXT("Struct element sizes: S1 (FGCBItemDataRef) %d bytes, S0 (FGCBItemData) %d bytes"),
		static_cast<int32>(sizeof(FGCBItemDataRef)), static_cast<int32>(sizeof(FGCBItemData)));
}

void UGCBBenchmarkSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	Settings = ReadRunSettings();
	if (Settings.ConfigName.IsEmpty())
	{
		return; // No benchmark requested: the world behaves like a normal empty map.
	}
	if (!ValidateRunSettings())
	{
		return; // Stay idle; the run report is never written, so the runner flags the run (log errors, no report).
	}

	ApplyGCSettings();
	CreateWorkload();
	BeginPlayTime = InWorld.GetTimeSeconds();
	Phase = ERunPhase::WarmUp;

	// Run description inside the CSV itself, so every capture is self-describing.
	CSV_METADATA(TEXT("GCBRunTag"), *Settings.RunTag);
	CSV_METADATA(TEXT("GCBConfig"), *Settings.ConfigName.ToUpper());
	CSV_METADATA(TEXT("GCBItems"), *FString::FromInt(Settings.Items));
	CSV_METADATA(TEXT("GCBSeed"), *FString::FromInt(Settings.Seed));
	CSV_METADATA(TEXT("GCBGCFrames"), *FString::FromInt(Settings.GCFrames));
	CSV_METADATA(TEXT("GCBProbe"), *FString::FromInt(Settings.ProbeLookups));
	UE_LOG(LogGCBBench, Display, TEXT("Benchmark %s: config %s, %d items, seed %d, GC every %d frames, %d lookups per frame, warm-up %.1f s, capture %.1f s"),
		*Settings.RunTag, *Settings.ConfigName.ToUpper(), Settings.Items, Settings.Seed, Settings.GCFrames, Settings.ProbeLookups,
		Settings.WarmupSeconds, Settings.CaptureSeconds);
}

void UGCBBenchmarkSubsystem::UpdateWorkload(float DeltaTime)
{
	int32 Expired = 0;
	double UpdateMs = 0.0;
	double ProbeNs = 0.0;
	int32 ProbeErrors = 0;

	if (Store)
	{
		// One tick per fixed 60 Hz frame; a variable-step run advances by its own rounded tick count.
		const int32 Ticks = FMath::Max(1, FMath::RoundToInt32(DeltaTime * GCBWorkload::TicksPerSecond));

		const double UpdateStart = FPlatformTime::Seconds();
		Expired = Store->Update(Ticks);
		UpdateMs = (FPlatformTime::Seconds() - UpdateStart) * 1000.0;

		Store->RefreshProbe(FrameIndex);
		const double ProbeStart = FPlatformTime::Seconds();
		const FGCBProbeResult Probe = Store->RunProbe(Settings.ProbeLookups);
		const double ProbeSeconds = FPlatformTime::Seconds() - ProbeStart;
		ProbeNs = (Settings.ProbeLookups > 0) ? ProbeSeconds * 1.0e9 / Settings.ProbeLookups : 0.0;
		ProbeErrors = Probe.Errors;

		Totals.Expired += Expired;
		Totals.ProbeLookups += Settings.ProbeLookups;
		Totals.ProbeResolved += Probe.Resolved;
		Totals.ProbeStale += Probe.Stale;
		Totals.ProbeErrors += Probe.Errors;
		Totals.ProbeSink += Probe.Sink;
	}

	const int32 Items = Store ? Store->Num() : 0;
	CSV_CUSTOM_STAT(GCBWorkload, Items, Items, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GCBWorkload, Expired, Expired, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GCBWorkload, UpdateMs, UpdateMs, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GCBWorkload, UpdateNsPerItem, (Items > 0) ? UpdateMs * 1.0e6 / Items : 0.0, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GCBWorkload, ProbeNsPerLookup, ProbeNs, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GCBWorkload, ProbeErrors, ProbeErrors, ECsvCustomStatOp::Set);
}

void UGCBBenchmarkSubsystem::UpdateGCCadence()
{
	const bool bRequest = Settings.GCFrames > 0 && FrameIndex % Settings.GCFrames == 0;
	double ClusterMs = 0.0;
	if (bRequest)
	{
		if (Store)
		{
			const double Start = FPlatformTime::Seconds();
			Store->PrepareForGC();
			ClusterMs = (FPlatformTime::Seconds() - Start) * 1000.0;
			if (const UGCBObjectItemStore* ObjectStore = Cast<UGCBObjectItemStore>(Store.Get()))
			{
				Totals.ClustersRebuilt += ObjectStore->GetClustersRebuilt();
			}
		}
		// Moves the engine's timer past the interval (UnrealEngine.cpp:2136-2139): this frame's ConditionalCollectGarbage
		// starts a non-full-purge cycle, or waits until a still-running purge has finished (:2282-2287).
		GEngine->ForceGarbageCollection(false);
		++Totals.GCRequests;
	}
	CSV_CUSTOM_STAT(GCBWorkload, GCTrigger, bRequest ? 1 : 0, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(GCBWorkload, ClusterMs, ClusterMs, ECsvCustomStatOp::Set);
}

void UGCBBenchmarkSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Phase == ERunPhase::Idle)
	{
		return;
	}

	++FrameIndex;
	const double Now = GetWorld()->GetTimeSeconds();
	const double Elapsed = Now - BeginPlayTime;

	if (Phase != ERunPhase::Done)
	{
		UpdateWorkload(DeltaTime);
		UpdateGCCadence();
	}

	switch (Phase)
	{
	case ERunPhase::WarmUp:
		if (Elapsed >= Settings.WarmupSeconds)
		{
			BenchStartTime = Now;
			BenchStartFrame = FrameIndex;
			Phase = ERunPhase::Capture;
			CSV_EVENT(GCBWorkload, TEXT("BenchStart"));
			UE_LOG(LogGCBBench, Display, TEXT("BenchStart at %.3f s, frame %lld (warm-up %.1f s)"), Now, FrameIndex, Settings.WarmupSeconds);
		}
		break;

	case ERunPhase::Capture:
		if (Now - BenchStartTime >= Settings.CaptureSeconds)
		{
			// The stop request is processed at the end of this frame; the report is written next frame, outside the capture.
			CSV_EVENT(GCBWorkload, TEXT("BenchEnd"));
			BenchEndFrame = FrameIndex;
			Phase = ERunPhase::Done;
			FramesSinceEnd = 0;
			UE_LOG(LogGCBBench, Display, TEXT("BenchEnd after %.3f s of capture, frame %lld"), Now - BenchStartTime, FrameIndex);
		}
		break;

	case ERunPhase::Done:
		++FramesSinceEnd;
		if (FramesSinceEnd == 1)
		{
			WriteRunReport();
			if (Settings.bStopTraceBeforeExit && FTraceAuxiliary::IsConnected())
			{
				// The exit-777003 probe (09, Phase 7): end the trace while the engine is fully up, two frames before
				// the exit request, instead of leaving it to engine shutdown (TraceAuxiliary.h:230).
				const bool bStopped = FTraceAuxiliary::Stop();
				UE_LOG(LogGCBBench, Display, TEXT("Trace stopped before exit: %s"), bStopped ? TEXT("yes") : TEXT("no"));
			}
		}
		if (Settings.bExitWhenDone && FramesSinceEnd >= GCBBench::ExitDelayFrames)
		{
			Settings.bExitWhenDone = false; // Request once.
			FPlatformMisc::RequestExit(false, TEXT("UGCBBenchmarkSubsystem"));
		}
		break;

	default:
		break;
	}
}

void UGCBBenchmarkSubsystem::WriteRunReport() const
{
	const UGCBudgetTelemetrySubsystem* Telemetry = GetWorld()->GetSubsystem<UGCBudgetTelemetrySubsystem>();
	const int32 CycleStarts = Telemetry ? Telemetry->GetTotalCycleStarts() : -1;
	const int32 CyclesOutside = Telemetry ? Telemetry->GetCycleStartsOutsideBracket() : -1;
	const uint32 Checksum = Store ? Store->Checksum() : 0u;

	const FString Report = FString::Printf(
		TEXT("{\n")
		TEXT("  \"run\": \"%s\",\n  \"config\": \"%s\",\n  \"items\": %d,\n  \"seed\": %d,\n  \"gc_frames\": %d,\n  \"probe_lookups_per_frame\": %d,\n")
		TEXT("  \"warmup_s\": %.3f,\n  \"capture_s\": %.3f,\n  \"bench_start_frame\": %lld,\n  \"bench_end_frame\": %lld,\n  \"frames\": %lld,\n")
		TEXT("  \"populate_ms\": %.3f,\n  \"gc_requests\": %d,\n  \"gc_cycle_starts_observed\": %d,\n  \"gc_cycle_starts_outside_bracket\": %d,\n  \"clusters_rebuilt\": %lld,\n")
		TEXT("  \"expired\": %lld,\n  \"probe_lookups\": %lld,\n  \"probe_resolved\": %lld,\n  \"probe_stale\": %lld,\n  \"probe_errors\": %lld,\n")
		TEXT("  \"probe_sink\": %lld,\n  \"checksum\": \"0x%08X\"\n}\n"),
		*GCBBench::JsonEscape(Settings.RunTag), *GCBBench::JsonEscape(Settings.ConfigName.ToUpper()), Settings.Items, Settings.Seed, Settings.GCFrames, Settings.ProbeLookups,
		Settings.WarmupSeconds, Settings.CaptureSeconds, BenchStartFrame, BenchEndFrame, FrameIndex,
		Totals.PopulateMs, Totals.GCRequests, CycleStarts, CyclesOutside, Totals.ClustersRebuilt,
		Totals.Expired, Totals.ProbeLookups, Totals.ProbeResolved, Totals.ProbeStale, Totals.ProbeErrors,
		Totals.ProbeSink, Checksum);

	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GCBRuns"), Settings.RunTag + TEXT(".json"));
	if (FFileHelper::SaveStringToFile(Report, *Path))
	{
		UE_LOG(LogGCBBench, Display, TEXT("Run report written: %s (checksum 0x%08X, probe errors %lld)"),
			*FPaths::ConvertRelativePathToFull(Path), Checksum, Totals.ProbeErrors);
	}
	else
	{
		UE_LOG(LogGCBBench, Error, TEXT("Could not write the run report: %s"), *Path);
	}
}

TStatId UGCBBenchmarkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGCBBenchmarkSubsystem, STATGROUP_Tickables);
}
