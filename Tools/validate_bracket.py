"""P04 GCBudget - check the telemetry's per-frame GC bracket against Unreal Insights' own GC timer.

Usage (through the UE job queue, after a run with run_bench.py --trace):
    python -I -u validate_bracket.py CAPTURE.csv TRACE.utrace
    python -I -u validate_bracket.py --label LABEL       (the newest traced, non-priming run of batch label LABEL)

What it does:
  1. Exports every "ConditionalCollectGarbage" timing event on the game thread from the trace, with
     UnrealInsights.exe -NoUI -AutoQuit and the command TimingInsights.ExportTimingEvents
     (TimingProfilerManager.cpp:802; the same command line as Epic's own functional test, ExportCommandsTests.cpp:148).
     The command goes through an "@=" response file (InsightsManager.cpp:1089-1100), because the paths contain spaces.
     A previous export of the same run is deleted first, and the new file must be newer than the launch, so a failed
     export can never be mistaken for a fresh one.
  2. That timer wraps the engine's GC call exactly (LevelTick.cpp:1969), once per frame. A trace event is a cycle
     frame if it lasts more than CYCLE_FACTOR x the median event (and at least CYCLE_MIN_MS): GC cycle frames are an
     order of magnitude longer than the others, so this needs no alignment with the CSV.
  3. Compares the trace's cycle frames with the CSV's GC ms on cycle-start frames (bracket minus floor, as in
     csv_stats.py; frames whose bracket did not open are skipped), and the trace's other frames with the CSV floor.
The trace covers the whole run and the CSV only the capture, so the comparison is between distributions, not
frame by frame; in a steady configuration both sample the same kind of frames. The result is written next to the
capture as <capture>_bracket.json.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)   # python -I does not put the script's folder on the path
import csv_stats  # noqa: E402  (same folder)

RAW_DIR = os.path.join(os.path.dirname(TOOLS_DIR), "Benchmarks", "Raw")
INSIGHTS = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealInsights.exe"
TIMER = "ConditionalCollectGarbage"
CYCLE_FACTOR = 10.0
CYCLE_MIN_MS = 0.2


def export_timer(trace: str, out_csv: str, timeout: float = 600.0) -> list[tuple[float, float]]:
    """Returns (start seconds, duration ms) of every TIMER event on the game thread, in start order."""
    response = out_csv + ".cmd.txt"
    log = out_csv + ".insights.log"
    for stale in (out_csv, log):
        if os.path.exists(stale):
            os.remove(stale)
    with open(response, "w", encoding="utf-8") as handle:
        handle.write(f'TimingInsights.ExportTimingEvents "{out_csv}" -columns=TimerName,StartTime,EndTime,Duration '
                     f'-threads=GameThread -timers={TIMER}\n')
    command = f'"{INSIGHTS}" -OpenTraceFile="{trace}" -ABSLOG="{log}" -AutoQuit -NoUI -ExecOnAnalysisCompleteCmd="@={response}" -log'
    launched = time.time()
    process = subprocess.Popen(command)
    try:
        process.wait(timeout=timeout)
    finally:
        if process.poll() is None:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], capture_output=True)
    if not os.path.exists(out_csv) or os.path.getmtime(out_csv) < launched:
        raise RuntimeError(f"Insights wrote no fresh export; see {log}")
    events = []
    with open(out_csv, newline="", encoding="utf-8", errors="replace") as handle:
        for row in csv.DictReader(handle):
            if row.get("TimerName") == TIMER:
                start, end = float(row["StartTime"]), float(row["EndTime"])
                events.append((start, (end - start) * 1000.0))   # StartTime/EndTime are in seconds
    events.sort()
    return events


def compare(capture: str, events: list[tuple[float, float]]) -> dict:
    header, rows, _, finalised = csv_stats.load_capture(capture)
    gc = csv_stats.gc_metrics(header, rows)
    starts = csv_stats.per_frame(header, rows, csv_stats.CYCLE_STARTS, 0.0)
    bracket = csv_stats.per_frame(header, rows, csv_stats.BRACKET)
    floor = gc["floor_ms"]
    csv_cycle_ms = [max(0.0, b - floor) for b, s in zip(bracket, starts)
                    if s > 0.0 and b is not None and b >= 0.0 and floor is not None]

    durations = [d for _, d in events]
    median_all = csv_stats.percentile(sorted(durations), 50.0) if durations else 0.0
    threshold = max(CYCLE_MIN_MS, CYCLE_FACTOR * median_all)
    trace_cycle_ms = [d for d in durations if d > threshold]
    others = [d for d in durations if d <= threshold]

    trace_cycle, csv_cycle = csv_stats.summarize(trace_cycle_ms), csv_stats.summarize(csv_cycle_ms)
    trace_other = csv_stats.summarize(others)
    result = {
        "capture": os.path.basename(capture), "finalised": finalised, "trace_events": len(events),
        "cycle_threshold_ms": threshold, "trace_cycle_frames": len(trace_cycle_ms), "csv_cycle_frames": len(csv_cycle_ms),
        "trace_cycle_frame_ms": trace_cycle, "csv_cycle_frame_gc_ms": csv_cycle,
        "trace_other_frames_ms": trace_other, "csv_floor_ms": floor,
    }
    if trace_cycle["n"] and csv_cycle["n"]:
        result["difference_ms"] = {k: csv_cycle[k] - trace_cycle[k] for k in ("median", "p95", "max")}
    if trace_other["n"] and floor is not None:
        result["floor_minus_trace_other_median_ms"] = floor - trace_other["median"]
    return result


def newest_traced_run(label: str) -> str:
    """The newest non-priming .utrace whose batch label is exactly LABEL (YYYY-MM-DD_HHMMSS_<label>_...)."""
    pattern = re.compile(r"^\d{4}-\d{2}-\d{2}_\d{6}_" + re.escape(label) + r"_.*(?<!_prime)\.utrace$")
    traces = [os.path.join(RAW_DIR, f) for f in os.listdir(RAW_DIR) if pattern.match(f)]
    if not traces:
        raise SystemExit(f"no traced run with batch label {label!r} in {RAW_DIR}")
    return max(traces, key=os.path.getmtime)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture", nargs="?")
    parser.add_argument("trace", nargs="?")
    parser.add_argument("--label", help="use the newest traced run in Benchmarks/Raw whose batch label is LABEL")
    args = parser.parse_args(argv)
    if args.label:
        args.trace = newest_traced_run(args.label)
        args.capture = os.path.splitext(args.trace)[0] + ".csv"
    if not args.capture or not args.trace:
        parser.error("give CAPTURE.csv and TRACE.utrace, or --label")

    out_csv = os.path.splitext(args.capture)[0] + "_ccg_events.csv"
    events = export_timer(os.path.abspath(args.trace), os.path.abspath(out_csv))
    result = compare(args.capture, events)
    with open(os.path.splitext(args.capture)[0] + "_bracket.json", "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    print(json.dumps(result, indent=2))
    return 0 if result["trace_events"] and result["trace_cycle_frames"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
