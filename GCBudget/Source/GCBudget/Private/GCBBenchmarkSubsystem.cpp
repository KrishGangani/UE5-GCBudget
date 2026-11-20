// P04 GCBudget (benchmark) - UGCBBenchmarkSubsystem.

#include "GCBBenchmarkSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GCBudgetTelemetrySubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/Stats.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GCBBenchmarkSubsystem)

DEFINE_LOG_CATEGORY_STATIC(LogGCBBench, Log, All);

// Workload columns (GCBWorkload/Items, /GCTrigger) and the capture events BenchStart / BenchEnd.
CSV_DEFINE_CATEGORY(GCBWorkload, true);

namespace GCBBench
{
	/** Parks the engine's own GC timer so only the director starts cycles (02_System_Architecture.md section 6). */
	const TCHAR* const ParkedTimerSeconds = TEXT("3600");

	/** Frames to wait after BenchEnd before quitting, so the CSV profiler finishes writing its file. */
	constexpr int32 ExitDelayFrames = 3;

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
	FParse::Value(CommandLine, TEXT("GCBWarmup="), Read.WarmupSeconds);
	FParse::Value(CommandLine, TEXT("GCBCapture="), Read.CaptureSeconds);
	FParse::Value(CommandLine, TEXT("GCBRunTag="), Read.RunTag);
	Read.bExitWhenDone = FParse::Param(CommandLine, TEXT("GCBExit"));
	Read.Config = ParseConfig(Read.ConfigName);
	return Read;
}

EGCBConfig UGCBBenchmarkSubsystem::ParseConfig(const FString& Name)
{
	if (Name.Equals(TEXT("E0"), ESearchCase::IgnoreCase))
	{
		return EGCBConfig::E0;
	}
	return EGCBConfig::None;
}

bool UGCBBenchmarkSubsystem::ValidateRunSettings() const
{
	bool bValid = true;
	if (Settings.Config == EGCBConfig::None)
	{
		UE_LOG(LogGCBBench, Error, TEXT("-GCBConfig=%s is unknown or not built yet (this build supports: E0)."), *Settings.ConfigName);
		bValid = false;
	}
	if (Settings.Config == EGCBConfig::E0 && Settings.Items != 0)
	{
		UE_LOG(LogGCBBench, Error, TEXT("E0 is the no-item configuration, but -GCBItems=%d was given."), Settings.Items);
		bValid = false;
	}
	if (Settings.WarmupSeconds < 0.0 || Settings.CaptureSeconds <= 0.0 || Settings.GCFrames < 0)
	{
		UE_LOG(LogGCBBench, Error, TEXT("Invalid run timing: warm-up %.3f s, capture %.3f s, GC every %d frames (need warm-up >= 0, capture > 0, frames >= 0)."),
			Settings.WarmupSeconds, Settings.CaptureSeconds, Settings.GCFrames);
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

	// Settings every configuration shares, recorded so each capture states its own GC environment.
	// gc.VerifyAssumptions must read 0 in timed runs: -NoVerifyGC (01b_Research_Findings.md #11).
	GCBBench::RecordConsoleVariable(TEXT("gc.VerifyAssumptions"));
	GCBBench::RecordConsoleVariable(TEXT("gc.AllowParallelGC"));
	GCBBench::RecordConsoleVariable(TEXT("gc.CreateGCClusters"));
	GCBBench::RecordConsoleVariable(TEXT("gc.AllowIncrementalReachability"));
	GCBBench::RecordConsoleVariable(TEXT("gc.IncrementalReachabilityTimeLimit"));
	GCBBench::RecordConsoleVariable(TEXT("gc.IncrementalGCTimePerFrame"));
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
	BeginPlayTime = InWorld.GetTimeSeconds();
	Phase = ERunPhase::WarmUp;

	// Run description inside the CSV itself, so every capture is self-describing.
	CSV_METADATA(TEXT("GCBRunTag"), *Settings.RunTag);
	CSV_METADATA(TEXT("GCBConfig"), *Settings.ConfigName.ToUpper());
	CSV_METADATA(TEXT("GCBItems"), *FString::FromInt(Settings.Items));
	CSV_METADATA(TEXT("GCBSeed"), *FString::FromInt(Settings.Seed));
	CSV_METADATA(TEXT("GCBGCFrames"), *FString::FromInt(Settings.GCFrames));
	UE_LOG(LogGCBBench, Display, TEXT("Benchmark %s: config %s, %d items, seed %d, GC every %d frames, warm-up %.1f s, capture %.1f s"),
		*Settings.RunTag, *Settings.ConfigName.ToUpper(), Settings.Items, Settings.Seed, Settings.GCFrames,
		Settings.WarmupSeconds, Settings.CaptureSeconds);
}

void UGCBBenchmarkSubsystem::UpdateGCCadence()
{
	const bool bRequest = Settings.GCFrames > 0 && FrameIndex % Settings.GCFrames == 0;
	if (bRequest)
	{
		// Moves the engine's timer past the interval (UnrealEngine.cpp:2136-2139): this frame's ConditionalCollectGarbage
		// starts a non-full-purge cycle, or waits until a still-running purge has finished (:2282-2287).
		GEngine->ForceGarbageCollection(false);
		++GCRequests;
	}
	CSV_CUSTOM_STAT(GCBWorkload, GCTrigger, bRequest ? 1 : 0, ECsvCustomStatOp::Set);
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
		UpdateGCCadence();
		CSV_CUSTOM_STAT(GCBWorkload, Items, Settings.Items, ECsvCustomStatOp::Set);
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

	const FString Report = FString::Printf(
		TEXT("{\n")
		TEXT("  \"run\": \"%s\",\n  \"config\": \"%s\",\n  \"items\": %d,\n  \"seed\": %d,\n  \"gc_frames\": %d,\n")
		TEXT("  \"warmup_s\": %.3f,\n  \"capture_s\": %.3f,\n  \"bench_start_frame\": %lld,\n  \"bench_end_frame\": %lld,\n")
		TEXT("  \"frames\": %lld,\n  \"gc_requests\": %d,\n  \"gc_cycle_starts_observed\": %d,\n  \"gc_cycle_starts_outside_bracket\": %d\n}\n"),
		*GCBBench::JsonEscape(Settings.RunTag), *GCBBench::JsonEscape(Settings.ConfigName.ToUpper()), Settings.Items, Settings.Seed, Settings.GCFrames,
		Settings.WarmupSeconds, Settings.CaptureSeconds, BenchStartFrame, BenchEndFrame,
		FrameIndex, GCRequests, CycleStarts, CyclesOutside);

	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GCBRuns"), Settings.RunTag + TEXT(".json"));
	if (FFileHelper::SaveStringToFile(Report, *Path))
	{
		UE_LOG(LogGCBBench, Display, TEXT("Run report written: %s"), *FPaths::ConvertRelativePathToFull(Path));
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
