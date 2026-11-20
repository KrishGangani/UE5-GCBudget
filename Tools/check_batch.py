"""P04 GCBudget - functional checks over one recorded batch (offline: reads Benchmarks/Raw, starts nothing).

Usage:
    python -I check_batch.py <batch>        e.g. 2026-11-21_035628_s1e0b

For every run of the batch (priming included), from its CSV capture, run report and log:
  - cycle starts: one per GC request, exactly the report's gc_frames apart, on the same frames as the director's
    GCTrigger column, never two in one frame;
  - the engine's own GC/Count column counts the same cycles;
  - CyclePauseMs is > 0 on every cycle-start frame and 0 on every other frame;
  - the report's GC requests equal the cycles the telemetry saw inside the bracket;
  - the log records gc.VerifyAssumptions and the GC timer the director set, and has no errors;
  - the UObject count's range over the capture is printed (flat for E0; a sawtooth bounded by one cycle's garbage
    for the item configurations).
Exit code 0 if every run passes, 1 otherwise. Thresholds and column names come from csv_stats.py.
"""
from __future__ import annotations

import glob
import json
import os
import re
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)   # python -I does not put the script's folder on the path
import csv_stats  # noqa: E402  (same folder)

RAW_DIR = os.path.join(os.path.dirname(TOOLS_DIR), "Benchmarks", "Raw")
GC_TRIGGER = "GCBWorkload/GCTrigger"
LOG_LINE = re.compile(r"^\[[^\]]*\]\[[^\]]*\]\w+: (Error|Warning):", re.M)


def check_run(csv_path: str) -> list[str]:
    """Prints one run's findings and returns the failed checks (empty if it passes)."""
    name = os.path.basename(csv_path)[:-len(".csv")]
    header, rows, _metadata, _finalised = csv_stats.load_capture(csv_path)
    starts = csv_stats.per_frame(header, rows, csv_stats.CYCLE_STARTS, 0.0)
    pauses = csv_stats.per_frame(header, rows, csv_stats.CYCLE_PAUSE, 0.0)
    triggers = csv_stats.per_frame(header, rows, GC_TRIGGER, 0.0)
    engine_counts = csv_stats.per_frame(header, rows, csv_stats.ENGINE_GC_COUNT, 0.0)
    objects = [v for v in csv_stats.per_frame(header, rows, csv_stats.UOBJECTS) if v is not None]

    start_frames = [i for i, v in enumerate(starts) if v > 0]
    start_set = set(start_frames)
    trigger_frames = [i for i, v in enumerate(triggers) if v > 0]
    gaps = sorted({b - a for a, b in zip(start_frames, start_frames[1:])})
    engine_cycles = int(sum(engine_counts))
    pause_missing = [i for i in start_frames if not pauses[i] > 0]
    pause_elsewhere = [i for i, v in enumerate(pauses) if v > 0 and i not in start_set]

    report_path = os.path.join(RAW_DIR, name + "_report.json")
    log_path = os.path.join(RAW_DIR, name + ".log")
    if not os.path.exists(report_path) or not os.path.exists(log_path):
        print(f"{name}: FAILED: the run report or the log is missing")
        return ["report or log missing"]
    with open(report_path, encoding="utf-8") as handle:
        report = json.load(handle)
    with open(log_path, encoding="utf-8", errors="replace") as handle:
        log = handle.read()
    verify = re.findall(r"gc\.VerifyAssumptions = (\S+)", log)
    timer = re.findall(r"gc\.TimeBetweenPurgingPendingKillObjects = (\S+)", log)
    levels = LOG_LINE.findall(log)

    failed = []
    if any(v > 1 for v in starts):
        failed.append("two cycle starts in one frame")
    if report["gc_frames"] > 0 and gaps and gaps != [report["gc_frames"]]:
        failed.append(f"cycle starts {gaps} frames apart, expected {report['gc_frames']}")
    if report["gc_frames"] > 0 and trigger_frames != start_frames:
        failed.append("cycle starts are not on the director's trigger frames")
    if engine_cycles != len(start_frames):
        failed.append(f"engine GC/Count {engine_cycles} != {len(start_frames)} cycle starts")
    if pause_missing or pause_elsewhere:
        failed.append(f"CyclePauseMs missing on {len(pause_missing)} cycle frames, present on {len(pause_elsewhere)} others")
    if report["gc_requests"] != report["gc_cycle_starts_observed"]:
        failed.append(f"{report['gc_requests']} requests but {report['gc_cycle_starts_observed']} cycles in the bracket")
    if not verify or not timer:
        failed.append("gc.VerifyAssumptions or the GC timer is missing from the log")
    if "Error" in levels:
        failed.append(f"{levels.count('Error')} log errors")

    print(f"{name}: {len(rows)} frames; {len(start_frames)} cycle starts, {gaps or '-'} frames apart, on the trigger "
          f"frames: {trigger_frames == start_frames}; engine GC/Count {engine_cycles}; pause > 0 on every cycle frame: "
          f"{not pause_missing}, elsewhere: {len(pause_elsewhere)}; UObjects {min(objects):.0f}-{max(objects):.0f}")
    print(f"    report: {report['gc_requests']} requests, {report['gc_cycle_starts_observed']} cycles in the bracket, "
          f"{report.get('gc_cycle_starts_outside_bracket')} outside; log: gc.VerifyAssumptions {verify}, timer {timer}, "
          f"{levels.count('Error')} errors, {levels.count('Warning')} warnings; trace file: "
          f"{os.path.exists(os.path.join(RAW_DIR, name + '.utrace'))}" + ("" if not failed else f"\n    FAILED: {failed}"))
    return failed


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print(__doc__)
        return 2
    captures = sorted(glob.glob(os.path.join(RAW_DIR, argv[0] + "_*.csv")))
    if not captures:
        print(f"no captures for batch {argv[0]} in {RAW_DIR}")
        return 2
    failures = [os.path.basename(path) for path in captures if check_run(path)]
    print("all runs pass" if not failures else f"FAILED runs: {failures}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
