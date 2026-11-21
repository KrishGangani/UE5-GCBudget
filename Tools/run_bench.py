"""P04 GCBudget - run a benchmark batch on the packaged build and collect every artefact.

Adapted from the portfolio's reference project (UE5-UpdateBudget, Tools/run_bench.py) so this repository stays
standalone. Run it through the machine-wide UE job queue, never directly.

Usage:
    python -I -u run_bench.py --configs E0,U0,U1,U2 --items 10000[,100000] [--runs N] [--capture 60] [--warmup 10]
                              [--gc-frames 120] [--seed 1] [--label NAME] [--trace] [--verify-gc] [--extra "ARGS"]
                              [--no-prime] [--dry-run]

What one batch does:
  1. Pins ITSELF to the performance cores (pin_self) before anything else, so every game it starts inherits the mask
     from its first instruction; then records the environment once, including the mask read back from Windows.
  2. Runs one PRIMING run first (discarded) so file, driver and PSO caches are warm for every measured run.
  3. Runs every (config x items) combination --runs times, INTERLEAVED: the config order rotates every round, so
     thermal drift spreads over all configs. The default --runs is the number of configs, so every config sits in
     every order position exactly once (portfolio protocol). E0 has no items: it runs once per round, at N = 0,
     whatever --items says.
  4. Before each run, deletes the build's GameUserSettings.ini (it can override VSync / frame-rate caps).
  5. Launches the real game exe directly (not the bootstrap), waits with a timeout, and kills the process tree in
     `finally`, so a failed or hung run never survives into the next one.
  6. After each run, moves the CSV capture, the run report and the run's log into Benchmarks/Raw as
     <batch>_<Config>_N<items>_r<run>.<ext> (<batch> = YYYY-MM-DD_HHMMSS[_label]). Nothing is ever overwritten.
     The manifest is rewritten after every run, so a crash loses nothing.
  7. Writes Benchmarks/Summaries/<batch>_summary.md: per combination, the median across runs of each run's statistic,
     with [min-max across runs].
  8. Cross-checks the end-of-run checksums: every run with the same items, seed and frame count must report the same
     checksum, whatever its configuration (success criterion SC7). A mismatch, a probe error, or a GC request that
     didn't start a cycle fails the batch.

Every run uses: fixed step (-deterministic -FPS=60), real GPU at 1280x720 windowed, no VSync, no sound, -NoVerifyGC
(--verify-gc swaps it for -VERIFYGC: a diagnostic run, never a timed one), and a capture window started/stopped by
the game's BenchStart/BenchEnd events. Untraced runs add -traceautostart=0;
traced runs (--trace) leave it out, because it would also block the requested trace.
"""
from __future__ import annotations

import argparse
import datetime as dt
import glob
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)   # python -I does not put the script's folder on the path
import cpu_affinity  # noqa: E402  (same folder)
import csv_stats     # noqa: E402  (same folder)

PROJECT_DIR = os.path.dirname(TOOLS_DIR)
BUILD_DIR = os.path.join(PROJECT_DIR, "Build", "Windows")
# The real game executable, launched directly: the packaged bootstrap Build\Windows\GCBudget.exe only starts this one.
# Launching it directly makes the game a direct child, so its exit code and its kill are reliable.
EXE = os.path.join(BUILD_DIR, "GCBudget", "Binaries", "Win64", "GCBudget.exe")
SAVED = os.path.join(BUILD_DIR, "GCBudget", "Saved")
RAW_DIR = os.path.join(PROJECT_DIR, "Benchmarks", "Raw")
SUMMARY_DIR = os.path.join(PROJECT_DIR, "Benchmarks", "Summaries")
ENGINE_BUILD_VERSION = r"C:\Program Files\Epic Games\UE_5.8\Engine\Build\Build.version"
STARTUP_MARGIN_SECONDS = 180.0  # generous allowance for engine start, populating items and shutdown

COMMON_ARGS = ("-windowed -ResX=1280 -ResY=720 -nosound -unattended -deterministic -FPS=60 -novsync "
               "-GCBExit -csvStartOnEvent=BenchStart -csvStopOnEvent=BenchEnd")
# GC verification: off for every timed run (01b #11); --verify-gc turns it on for a diagnostic run. Each switch sets
# gc.VerifyAssumptions from the command line (Obj.cpp:5915-5944); the director records the value in every capture.
NO_VERIFY_GC = "-NoVerifyGC"
VERIFY_GC = "-VERIFYGC"
# Untraced runs pass -traceautostart=0 so an open Insights can't trace them (TraceAuxiliary.cpp:267-268). Traced runs
# must NOT pass it: it also blocks the trace that -trace=/-tracefile= request (TraceAuxiliary.cpp:2017-2021).
NO_AUTO_TRACE = "-traceautostart=0"

# Extra command-line arguments per configuration (02_System_Architecture.md section 9). Each configuration is added
# in the roadmap step that builds it (03_Implementation_Roadmap.md).
CONFIGS = {
    "E0": "-GCBConfig=E0",        # no items: the engine's own GC cost in this project
    "U0": "-GCBConfig=U0",        # one UObject per item, in pages of 1,024
    "U1": "-GCBConfig=U1",        # U0 + every page a GC cluster, rebuilt before each cycle
    "U2": "-GCBConfig=U2",        # U0 + incremental reachability (Experimental), 2 ms slices
    "S1": "-GCBConfig=S1",        # structs in one array, each still holding a TObjectPtr to its definition
}

UPDATE_NS = "GCBWorkload/UpdateNsPerItem"
PROBE_NS = "GCBWorkload/ProbeNsPerLookup"
CLUSTER_MS = "GCBWorkload/ClusterMs"
SUMMARY_COLUMNS = ("GameThreadTime", "FrameTime", "PhysicalUsedMB", csv_stats.UOBJECTS, UPDATE_NS, PROBE_NS, CLUSTER_MS)


def powershell(command: str) -> str:
    result = subprocess.run(["powershell", "-NoProfile", "-Command", command], capture_output=True, text=True)
    return result.stdout.strip()


def environment_record(args) -> dict:
    """Facts that must accompany every result (Benchmarks/README.md, environment record)."""
    with open(ENGINE_BUILD_VERSION, encoding="utf-8") as handle:
        build = json.load(handle)
    return {
        "date": dt.datetime.now().isoformat(timespec="seconds"),
        "engine": f"{build['MajorVersion']}.{build['MinorVersion']}.{build['PatchVersion']} (CL {build['Changelist']})",
        "build_config": "Development (packaged)",
        "compiler": "MSVC 14.44.35229, Windows SDK 10.0.22621.0",
        "cpu": powershell("(Get-CimInstance Win32_Processor).Name"),
        "gpus": powershell("(Get-CimInstance Win32_VideoController | ForEach-Object { $_.Name + ' (driver ' + $_.DriverVersion + ')' }) -join '; '"),
        "ram_gb": powershell("[math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB,1)"),
        "os": powershell("$o=Get-CimInstance Win32_OperatingSystem; $o.Caption + ' build ' + $o.BuildNumber"),
        "power_plan": powershell("powercfg /getactivescheme"),
        "on_ac_power": powershell("(Get-CimInstance Win32_Battery | Select-Object -First 1).BatteryStatus") == "2",
        "resolution": "1280x720 windowed, default scalability",
        "common_args": " ".join([COMMON_ARGS, VERIFY_GC if args.verify_gc else NO_VERIFY_GC] + ([] if args.trace else [NO_AUTO_TRACE])),
        "traced": args.trace,
        "gc_verification": args.verify_gc,
        "seed": args.seed,
        "gc_cadence_frames": args.gc_frames,
        "extra_args": args.extra,
        "cpu_affinity": cpu_affinity.applied_mask(),
    }


def unique_path(path: str) -> str:
    """Raw data is never overwritten (Benchmarks/README.md)."""
    if os.path.exists(path):
        raise FileExistsError(f"refusing to overwrite raw data: {path}")
    return path


def launch(name: str, config: str, items: int, args) -> tuple[str, int | None, float, float]:
    """Starts one run and waits for it. Returns (command, exit code or None on timeout, start time, wall seconds)."""
    gcb_args = (f"-GCBItems={items} -GCBSeed={args.seed} -GCBGCFrames={args.gc_frames} -GCBWarmup={args.warmup} "
                f"-GCBCapture={args.capture} -GCBRunTag={name}")
    trace_args = (f'-trace=default,counters -tracefile="{os.path.join(RAW_DIR, name + ".utrace")}"' if args.trace
                  else NO_AUTO_TRACE)
    # One string, so quoted arguments reach Unreal exactly as written.
    verify = VERIFY_GC if args.verify_gc else NO_VERIFY_GC
    command = f'"{EXE}" {COMMON_ARGS} {verify} {CONFIGS[config]} {gcb_args} {trace_args} {args.extra}'.strip()
    if args.dry_run:
        print("DRY:", command)
        return command, 0, time.time(), 0.0

    gus = os.path.join(SAVED, "Config", "Windows", "GameUserSettings.ini")
    if os.path.exists(gus):
        os.remove(gus)
    started = time.time()
    process = subprocess.Popen(command)   # inherits this process's CPU affinity (main: pin_self)
    try:
        exit_code = process.wait(timeout=args.warmup + args.capture + STARTUP_MARGIN_SECONDS)
    except subprocess.TimeoutExpired:
        exit_code = None
    finally:
        if process.poll() is None:   # timeout, Ctrl+C or any other error: never leave a game running into the next run
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], capture_output=True)
    return command, exit_code, started, time.time() - started


def collect(name: str, started: float) -> dict:
    """Moves this run's artefacts into Benchmarks/Raw and returns what was found."""
    record: dict = {}
    os.makedirs(RAW_DIR, exist_ok=True)

    csv_files = [p for p in glob.glob(os.path.join(SAVED, "Profiling", "CSV", "*.csv")) if os.path.getmtime(p) >= started]
    if csv_files:
        destination = unique_path(os.path.join(RAW_DIR, name + ".csv"))
        shutil.move(max(csv_files, key=os.path.getmtime), destination)
        record["capture"] = os.path.basename(destination)
        try:
            analysis = csv_stats.analyse(destination, SUMMARY_COLUMNS)
            record["stats"] = analysis["columns"]
            record["gc"] = analysis["gc"]
            record["finalised"] = analysis["finalised"]
            record["events"] = analysis["events"]
            record["frames"] = analysis["frames"]
        except (ValueError, IndexError) as error:
            record["analysis_error"] = str(error)

    report = os.path.join(SAVED, "GCBRuns", name + ".json")
    if os.path.exists(report):
        destination = unique_path(os.path.join(RAW_DIR, name + "_report.json"))
        shutil.move(report, destination)
        with open(destination, encoding="utf-8") as handle:
            record["report"] = json.load(handle)
    else:
        record["report_missing"] = True

    log = os.path.join(SAVED, "Logs", "GCBudget.log")
    if os.path.exists(log) and os.path.getmtime(log) >= started:   # only this run's log, never a stale one
        with open(log, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        record["log_errors"] = text.count("Error:")
        record["log_warnings"] = text.count("Warning:")
        shutil.copy(log, unique_path(os.path.join(RAW_DIR, name + ".log")))
    else:
        record["log_missing"] = True
    return record


def is_failure(record: dict) -> bool:
    report = record.get("report", {})
    return (record.get("exit_code") != 0 or "stats" not in record or record.get("log_errors")
            or record.get("log_missing") or record.get("report_missing") or record.get("finalised") is False
            or "analysis_error" in record
            or report.get("probe_errors", 0) != 0                                         # a lookup found the wrong item
            or report.get("gc_requests") != report.get("gc_cycle_starts_observed"))       # a request started no cycle


def checksum_groups(records: list[dict]) -> dict[tuple, dict[str, set]]:
    """(items, seed, frames) -> {config: checksums seen}, over the measured runs that have a store."""
    groups: dict[tuple, dict[str, set]] = {}
    for record in records:
        report = record.get("report")
        if record.get("priming") or not report or record["config"] == "E0":
            continue
        key = (report["items"], report["seed"], report["frames"])
        groups.setdefault(key, {}).setdefault(record["config"], set()).add(report["checksum"])
    return groups


def checksum_mismatches(groups: dict[tuple, dict[str, set]]) -> list[str]:
    """Every run in a group must report one and the same checksum, whatever its configuration (SC7)."""
    problems = []
    for (items, seed, frames), by_config in sorted(groups.items()):
        seen = set().union(*by_config.values())
        if len(seen) != 1:
            problems.append(f"N={items} seed={seed} frames={frames}: " +
                            ", ".join(f"{c} {sorted(v)}" for c, v in sorted(by_config.items())))
    return problems


def write_manifest(batch: str, environment: dict, records: list[dict]) -> None:
    os.makedirs(RAW_DIR, exist_ok=True)
    with open(os.path.join(RAW_DIR, batch + "_manifest.json"), "w", encoding="utf-8") as handle:
        json.dump({"environment": environment, "runs": records}, handle, indent=2)


def across_runs(values: list[float], digits: int = 3) -> str:
    """Median across runs with [min-max across runs] (Benchmarks/README.md, statistics rule)."""
    values = [v for v in values if v is not None]
    if not values:
        return "n/a"
    return f"{statistics.median(values):.{digits}f} [{min(values):.{digits}f}-{max(values):.{digits}f}]"


def write_summary(batch: str, environment: dict, records: list[dict]) -> str:
    os.makedirs(SUMMARY_DIR, exist_ok=True)
    groups: dict[tuple, list[dict]] = {}
    for record in records:
        if "gc" in record and not record.get("priming"):
            groups.setdefault((record["config"], record["items"]), []).append(record)

    lines = [f"# Benchmark batch {batch}", "", "## Environment", "", "```json", json.dumps(environment, indent=2), "```", "",
             "## Results", "",
             "Each cell: median across runs of each run's statistic, then [min-max across runs]. GC metrics as defined in "
             "`Tools/csv_stats.py` (01b_Research_Findings.md section 4).", "",
             "| Config | N | Runs | Cycles | Cycle pause p95 / max (ms) | GC frame ms p95 / max | Spikes /min | Hitches /min | "
             "Floor (ms) | UObjects | PhysicalUsedMB | GameThreadTime median (ms) | Update ns/item | Probe ns/lookup | "
             "Cluster ms max | Probe errors | Log errors |",
             "|" + "---|" * 17]
    for (config, items), group in sorted(groups.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        gc = [g["gc"] for g in group]

        def stat(column: str, key: str, group=group) -> list:
            return [g["stats"].get(column, {}).get(key) for g in group]
        lines.append(" | ".join([
            f"| {config}", str(items), str(len(group)),
            across_runs([x["cycle_starts"] for x in gc], 0),
            across_runs([x["cycle_pause_ms"].get("p95") for x in gc]) + " / " + across_runs([x["cycle_pause_ms"].get("max") for x in gc]),
            across_runs([x["gc_frame_ms"].get("p95") for x in gc]) + " / " + across_runs([x["gc_frame_ms"].get("max") for x in gc]),
            across_runs([x["spikes_per_min"] for x in gc], 2),
            across_runs([x["hitches_per_min"] for x in gc], 2),
            across_runs([x["floor_ms"] for x in gc]),
            across_runs(stat(csv_stats.UOBJECTS, "median"), 0),
            across_runs(stat("PhysicalUsedMB", "median"), 1),
            across_runs(stat("GameThreadTime", "median")),
            across_runs(stat(UPDATE_NS, "median"), 2),
            across_runs(stat(PROBE_NS, "median"), 2),
            across_runs(stat(CLUSTER_MS, "max")),   # non-zero only on the 1-in-gc_frames request frames, so a percentile is 0
            str(sum(g.get("report", {}).get("probe_errors", 0) for g in group)),
            str(sum(g.get("log_errors", 0) for g in group)) + " |",
        ]))
    groups_by_checksum = checksum_groups(records)
    if groups_by_checksum:
        lines += ["", "## Checksums (SC7)", "",
                  "Every measured run with the same items, seed and frame count, per configuration:", "",
                  "| N | Seed | Frames | Checksums per configuration | Equal |", "|---|---|---|---|---|"]
        for (items, seed, frames), by_config in sorted(groups_by_checksum.items()):
            equal = len(set().union(*by_config.values())) == 1
            lines.append(f"| {items} | {seed} | {frames} | " +
                         "; ".join(f"{c}: {', '.join(sorted(v))}" for c, v in sorted(by_config.items())) +
                         f" | {'yes' if equal else '**NO**'} |")
    lines += ["", f"Raw files: `Benchmarks/Raw/{batch}_*` (CSV, run report and log per run) and `{batch}_manifest.json`."]
    path = os.path.join(SUMMARY_DIR, batch + "_summary.md")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return path


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--configs", default="E0")
    parser.add_argument("--items", default="0")
    parser.add_argument("--runs", type=int, default=0, help="runs per combination; 0 (default) = the number of configs")
    parser.add_argument("--warmup", type=float, default=10.0)
    parser.add_argument("--capture", type=float, default=60.0)
    parser.add_argument("--gc-frames", type=int, default=120)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--cooldown", type=float, default=5.0)
    parser.add_argument("--label", default="")
    parser.add_argument("--trace", action="store_true", help="also write an Insights trace per run (-trace=default,counters)")
    parser.add_argument("--verify-gc", action="store_true", help="-VERIFYGC instead of -NoVerifyGC (diagnostic runs only)")
    parser.add_argument("--extra", default="", help="extra command-line arguments for every run (diagnostic batches)")
    parser.add_argument("--no-prime", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resummarize", metavar="BATCH", help="rewrite BATCH's summary from its manifest; runs nothing")
    parser.add_argument("--reanalyse", action="store_true",
                        help="with --resummarize: first recompute every run's statistics from its raw CSV and rewrite the manifest")
    args = parser.parse_args(argv)

    if args.resummarize:
        manifest_path = os.path.join(RAW_DIR, args.resummarize + "_manifest.json")
        with open(manifest_path, encoding="utf-8") as handle:
            manifest = json.load(handle)
        if args.reanalyse:
            # After a change to csv_stats.py's rules: the raw captures are the record, the statistics are derived from them.
            for record in manifest["runs"]:
                if "capture" in record:
                    analysis = csv_stats.analyse(os.path.join(RAW_DIR, record["capture"]), SUMMARY_COLUMNS)
                    record.update(stats=analysis["columns"], gc=analysis["gc"], finalised=analysis["finalised"],
                                  events=analysis["events"], frames=analysis["frames"])
            manifest["reanalysed"] = dt.datetime.now().isoformat(timespec="seconds")
            with open(manifest_path, "w", encoding="utf-8") as handle:
                json.dump(manifest, handle, indent=2)
        print("summary:", write_summary(args.resummarize, manifest["environment"], manifest["runs"]))
        return 0

    if not re.fullmatch(r"[A-Za-z0-9_-]*", args.label):
        parser.error("--label may contain only letters, digits, '_' and '-' (it becomes part of file names and -GCBRunTag)")
    configs = [c.strip().upper() for c in args.configs.split(",") if c.strip()]
    unknown = [c for c in configs if c not in CONFIGS]
    if unknown:
        parser.error(f"unknown config(s) {unknown}; available: {sorted(CONFIGS)}")
    item_counts = [int(n) for n in args.items.split(",") if n.strip()]
    if any(c != "E0" for c in configs) and not all(n > 0 for n in item_counts):
        parser.error("every configuration except E0 needs --items > 0 (E0 always runs at N = 0)")
    if args.runs <= 0:
        args.runs = len(configs)
    elif args.runs % len(configs):
        print(f"WARNING: --runs {args.runs} is not a multiple of {len(configs)} configs; the rotation is unbalanced")
    if not os.path.exists(EXE) and not args.dry_run:
        parser.error(f"packaged build not found: {EXE} (run Tools\\package.bat first)")

    if not args.dry_run and cpu_affinity.KERNEL32 is not None:
        # Keep the display and the system awake while the batch runs (ES_CONTINUOUS | ES_SYSTEM_REQUIRED |
        # ES_DISPLAY_REQUIRED): a locked desktop can throttle a windowed game. Applies only while this process lives.
        cpu_affinity.KERNEL32.SetThreadExecutionState(0x80000000 | 0x00000001 | 0x00000002)
        # Pin once, before anything is recorded or started: every game started below inherits the mask.
        mask = cpu_affinity.performance_core_mask()
        if mask and not cpu_affinity.pin_self(mask):
            parser.error(f"could not pin the batch to the performance cores (0x{mask:X}); refusing to measure without it")

    batch = dt.datetime.now().strftime("%Y-%m-%d_%H%M%S") + (f"_{args.label}" if args.label else "")
    environment = environment_record(args) if not args.dry_run else {}
    records: list[dict] = []

    def run(config: str, items: int, run_index: int, priming: bool = False) -> None:
        name = f"{batch}_{config}_N{items}_r{run_index}" + ("_prime" if priming else "")
        command, exit_code, started, wall = launch(name, config, items, args)
        record = {"name": name, "config": config, "items": items, "run": run_index, "priming": priming,
                  "exit_code": exit_code, "wall_seconds": round(wall, 1), "command": command}
        if not args.dry_run:
            record.update(collect(name, started))
            records.append(record)
            write_manifest(batch, environment, records)
            gc = record.get("gc", {})
            pause = gc.get("cycle_pause_ms", {})
            print(f"  {name}: exit {exit_code}, {wall:.0f} s, {gc.get('cycle_starts')} cycles, pause median "
                  f"{pause.get('median', float('nan')):.3f} ms max {pause.get('max', float('nan')):.3f} ms, floor "
                  f"{gc.get('floor_ms')}, log errors {record.get('log_errors')}" + ("  [PRIMING, discarded]" if priming else ""),
                  flush=True)
            time.sleep(args.cooldown)

    def combinations(round_configs: list[str]) -> list[tuple[str, int]]:
        """This round's (config, items) runs in order; E0 has no items, so it runs once, at N = 0."""
        ordered: list[tuple[str, int]] = []
        for items in item_counts:
            for config in round_configs:
                combo = (config, 0 if config == "E0" else items)
                if combo not in ordered:
                    ordered.append(combo)
        return ordered

    if not args.no_prime:
        run(*combinations(configs)[0], 0, priming=True)

    for run_index in range(1, args.runs + 1):
        rotation = (run_index - 1) % len(configs)
        round_configs = configs[rotation:] + configs[:rotation]       # interleave: rotate the order every round
        for config, items in combinations(round_configs):
            run(config, items, run_index)

    if args.dry_run:
        return 0
    print("summary:", write_summary(batch, environment, records))
    failures = [r["name"] for r in records if not r.get("priming") and is_failure(r)]
    groups = checksum_groups(records)
    mismatches = checksum_mismatches(groups)
    for (items, seed, frames), by_config in sorted(groups.items()):
        print(f"  checksums N={items} seed={seed} frames={frames}: " +
              "; ".join(f"{c} {', '.join(sorted(v))}" for c, v in sorted(by_config.items())))
    if failures:
        print("ATTENTION - runs with a non-zero exit, timeout, no/partial capture, missing log/report, log errors, "
              "probe errors or a GC request without a cycle:", failures)
    if mismatches:
        print("ATTENTION - checksum mismatch (the configurations did different work):", mismatches)
    return 1 if failures or mismatches else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
