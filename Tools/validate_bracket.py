"""P04 GCBudget - check the telemetry's per-frame GC bracket against Unreal Insights' own GC timer.

Usage (through the UE job queue, after a run with run_bench.py --trace):
    python -I -u validate_bracket.py CAPTURE.csv TRACE.utrace
    python -I -u validate_bracket.py --label LABEL       (the newest traced, non-priming run of batch label LABEL)
    add --reuse-export to compare against an export already written for that run (starts nothing: no queue needed)

What it does:
  1. Exports every "ConditionalCollectGarbage" timing event on the game thread from the trace, with
     UnrealInsights.exe -NoUI -AutoQuit and the command TimingInsights.ExportTimingEvents
     (TimingProfilerManager.cpp:802; the same command line as Epic's own functional test, ExportCommandsTests.cpp:148).
     The command goes through an "@=" response file (InsightsManager.cpp:1089-1100), because the paths contain spaces;
     inside it the export path uses forward slashes, because the command reads it with escapes on (:817-818).
     A previous export of the same run is deleted first, and the new file must be newer than the launch, so a failed
     export can never be mistaken for a fresh one.
  2. Aligns both sides FRAME BY FRAME, and checks the alignment instead of assuming it:
       - the timer wraps the engine's GC call exactly (LevelTick.cpp:1969), once per world frame, so trace event i is
         frame i + 1. Check: the frames the director requests cycles on (multiples of the run report's gc_frames)
         must be the trace's largest events, i.e. the residue class with the largest median duration;
       - CSV row j is frame bench_start_frame + 1 + j (the capture opens the frame after BenchStart). Check: every
         cycle-start row must then fall on a multiple of gc_frames.
  3. For every captured cycle-start frame, compares the CSV's GC ms (bracket minus floor, as in csv_stats.py) and its
     CyclePauseMs with the trace's duration for the same frame. For every other frame, bracket minus trace is the
     non-GC work inside the bracket, which the floor is meant to remove.
The result is written next to the capture as <capture>_bracket.json. Exit code 0 if both alignments hold and at least
one cycle was compared; the differences themselves are reported, not judged (09 records the reading).
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


def export_timer(trace: str, out_csv: str, timeout: float = 600.0) -> None:
    """Writes every TIMER event on the game thread to out_csv with Unreal Insights."""
    response = out_csv + ".cmd.txt"
    log = out_csv + ".insights.log"
    for stale in (out_csv, log):
        if os.path.exists(stale):
            os.remove(stale)
    # The export command reads its file name with FParse::Token and escapes on (TimingProfilerManager.cpp:817-818),
    # so a backslash would be eaten as an escape character: the path goes in with forward slashes.
    with open(response, "w", encoding="utf-8") as handle:
        handle.write(f'TimingInsights.ExportTimingEvents "{out_csv.replace(os.sep, "/")}" '
                     f'-columns=TimerName,StartTime,EndTime,Duration -threads=GameThread -timers={TIMER}\n')
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


def read_events(out_csv: str) -> list[float]:
    """Durations in ms of every TIMER event in an export, in start order (StartTime/EndTime are in seconds)."""
    events = []
    with open(out_csv, newline="", encoding="utf-8", errors="replace") as handle:
        for row in csv.DictReader(handle):
            if row.get("TimerName") == TIMER:
                start, end = float(row["StartTime"]), float(row["EndTime"])
                events.append((start, (end - start) * 1000.0))
    return [duration for _, duration in sorted(events)]


def compare(capture: str, report: dict, durations: list[float]) -> dict:
    """Frame-matched comparison of the capture's cycle frames with the trace (module docstring, steps 2-3)."""
    header, rows, _, finalised = csv_stats.load_capture(capture)
    floor = csv_stats.gc_metrics(header, rows)["floor_ms"]
    starts = csv_stats.per_frame(header, rows, csv_stats.CYCLE_STARTS, 0.0)
    pauses = csv_stats.per_frame(header, rows, csv_stats.CYCLE_PAUSE, 0.0)
    bracket = csv_stats.per_frame(header, rows, csv_stats.BRACKET)
    period = report["gc_frames"]

    residue = max(range(period), key=lambda r: csv_stats.percentile(sorted(durations[r::period]), 50.0))
    first_frame = report["bench_start_frame"] + 1
    cycle_rows = [j for j, s in enumerate(starts) if s > 0]
    result = {
        "capture": os.path.basename(capture), "finalised": finalised, "trace_events": len(durations),
        "run_frames": report["frames"], "gc_frames": period, "csv_floor_ms": floor,
        "trace_largest_residue": residue, "trace_aligned": (residue + 1) % period == 0,
        "csv_aligned": bool(cycle_rows) and all((first_frame + j) % period == 0 for j in cycle_rows),
    }
    if not (result["trace_aligned"] and result["csv_aligned"]):
        return result

    def trace_at(row: int) -> float | None:
        index = first_frame + row - 1
        return durations[index] if 0 <= index < len(durations) else None

    cycles = [{"frame": first_frame + j, "bracket_minus_floor_ms": bracket[j] - floor, "cycle_pause_ms": pauses[j],
               "trace_ms": trace_at(j)}
              for j in cycle_rows if bracket[j] is not None and bracket[j] >= 0.0 and trace_at(j) is not None]
    differences = [c["bracket_minus_floor_ms"] - c["trace_ms"] for c in cycles]
    overhead = [bracket[j] - trace_at(j) for j in range(len(rows))
                if starts[j] == 0 and bracket[j] is not None and bracket[j] >= 0.0 and trace_at(j) is not None]
    result.update({
        "cycles": cycles,
        "bracket_minus_floor_minus_trace_ms": csv_stats.summarize(differences),
        "max_abs_difference_ms": max((abs(d) for d in differences), default=None),
        "cycle_pause_minus_trace_ms": csv_stats.summarize([c["cycle_pause_ms"] - c["trace_ms"] for c in cycles]),
        "other_frames_bracket_minus_trace_ms": csv_stats.summarize(overhead),
    })
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
    parser.add_argument("--reuse-export", action="store_true", help="use the run's existing export; don't start Insights")
    args = parser.parse_args(argv)
    if args.label:
        args.trace = newest_traced_run(args.label)
        args.capture = os.path.splitext(args.trace)[0] + ".csv"
    if not args.capture or not args.trace:
        parser.error("give CAPTURE.csv and TRACE.utrace, or --label")

    base = os.path.splitext(args.capture)[0]
    out_csv = os.path.abspath(base + "_ccg_events.csv")
    if args.reuse_export:
        if not os.path.exists(out_csv) or os.path.getmtime(out_csv) < os.path.getmtime(args.trace):
            parser.error(f"no export newer than the trace: {out_csv}")
    else:
        export_timer(os.path.abspath(args.trace), out_csv)
    with open(base + "_report.json", encoding="utf-8") as handle:
        report = json.load(handle)
    result = compare(args.capture, report, read_events(out_csv))
    with open(base + "_bracket.json", "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    print(json.dumps({k: v for k, v in result.items() if k != "cycles"}, indent=2))
    return 0 if result["trace_aligned"] and result["csv_aligned"] and result.get("cycles") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
