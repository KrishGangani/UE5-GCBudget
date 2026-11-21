"""P04 GCBudget - statistics for Unreal CSV-profiler captures, including the GC metrics of this project.

Adapted from the portfolio's reference project (UE5-UpdateBudget, Tools/csv_stats.py) so this repository stays
standalone. No tool shipped with UE 5.8 reports p95/p99, so this script does.

Usage:
    python -I csv_stats.py CAPTURE.csv [CAPTURE2.csv ...] [--columns A,B,...] [--json OUT.json]
    python -I csv_stats.py --selftest

Rules (documented so every number can be reproduced):
  * A finalised Unreal capture ends with two non-data rows: the full header and a "[key],value,..." metadata row.
    Both are dropped, and the END header is used for column lookup (the first header is only a prefix of it when
    columns appear mid-capture). If they are missing (crash or partial write) every row is data and the result is flagged.
  * The EVENTS column holds "Category/Name##timestamp" entries separated by ";".
  * Percentiles use linear interpolation between closest ranks (numpy's default "linear" method):
      position = p/100 * (n - 1);  value = lower + (upper - lower) * fraction.
  * GC metrics (01b_Research_Findings.md section 4, 01_Problem_Analysis.md section 6.3):
      floor        = median GCBudget/BracketMs over frames with GCBudget/GCFrame == 0 (bracket opened)
      GC ms        = max(0, BracketMs - floor) on frames with GCFrame == 1           (metric M2)
      cycle pause  = GCBudget/CyclePauseMs on frames with GCBudget/CycleStarts > 0     (metric M1)
      a cycle      = a cycle-start frame plus the GC frames that follow it without a gap (incremental slices, purge);
                     a cycle cut off by the start or the end of the capture is partial and left out of cycle statistics
      GC spike     = a GC frame with GC ms > SPIKE_MS (5 ms: 5.8.4's default incremental slice)
      GC hitch     = a GC frame that itself lasted > HITCH_MS (33.33 ms: two 60 Hz frames)  (metric M3, both per minute).
                     A frame's own duration is the NEXT row's FrameTime: the CSV profiler measures FrameTime at the start
                     of a frame, for the frame before it (CsvProfiler.cpp:150-155, 4369-4390)
      minutes      = captured frames / FPS / 60 (fixed-step runs simulate exactly 1/FPS per frame)
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import sys

FPS = 60.0
SPIKE_MS = 5.0
HITCH_MS = 1000.0 / 30.0

BRACKET = "GCBudget/BracketMs"
GC_FRAME = "GCBudget/GCFrame"
CYCLE_STARTS = "GCBudget/CycleStarts"
CYCLE_PAUSE = "GCBudget/CyclePauseMs"
UOBJECTS = "GCBudget/UObjects"
ENGINE_GC_COUNT = "GC/Count"
ENGINE_GC_TIME = "Exclusive/GameThread/GarbageCollection"

DEFAULT_COLUMNS = (
    "FrameTime",
    "GameThreadTime",
    "RenderThreadTime",
    "GPUTime",
    "PhysicalUsedMB",
    UOBJECTS,
    "GCBWorkload/Items",
)

csv.field_size_limit(1 << 30)


def percentile(sorted_values: list[float], p: float) -> float:
    """Linear-interpolation percentile of an already sorted, non-empty list. p in [0, 100]."""
    if not sorted_values:
        raise ValueError("percentile of an empty list")
    if len(sorted_values) == 1:
        return sorted_values[0]
    position = (p / 100.0) * (len(sorted_values) - 1)
    lower = math.floor(position)
    upper = min(lower + 1, len(sorted_values) - 1)
    fraction = position - lower
    return sorted_values[lower] + (sorted_values[upper] - sorted_values[lower]) * fraction


def summarize(values: list[float]) -> dict:
    """Count, mean, min, median, p95, p99 and max of a list of numbers (an empty list gives {"n": 0})."""
    if not values:
        return {"n": 0}
    ordered = sorted(values)
    return {
        "n": len(ordered),
        "mean": sum(ordered) / len(ordered),
        "min": ordered[0],
        "median": percentile(ordered, 50.0),
        "p95": percentile(ordered, 95.0),
        "p99": percentile(ordered, 99.0),
        "max": ordered[-1],
    }


def load_capture(path: str) -> tuple[list[str], list[list[str]], dict, bool]:
    """Return (header, data rows, metadata dict, finalised) of an Unreal CSV capture."""
    with open(path, newline="", encoding="utf-8", errors="replace") as handle:
        rows = list(csv.reader(handle))
    if len(rows) < 2:
        raise ValueError(f"{path}: too few rows to be an Unreal capture")
    first_header = rows[0]
    finalised = (len(rows) >= 3 and bool(rows[-1]) and rows[-1][0].strip().startswith("[")
                 and rows[-2][:len(first_header)] == first_header)
    if not finalised:
        return first_header, rows[1:], {}, False

    header, data, meta_row = rows[-2], rows[1:-2], rows[-1]
    # Metadata pairs: [key],value,[key],value,...  Stored without brackets, lower-case.
    metadata = {}
    for index in range(0, len(meta_row) - 1, 2):
        key = meta_row[index].strip().strip("[]").lower()
        if key:
            metadata[key] = meta_row[index + 1].strip()
    return header, data, metadata, True


def events_index(header: list[str]) -> int | None:
    """Index of the EVENTS column (case-insensitive), or None if the capture has none."""
    lowered = [h.lower() for h in header]
    return lowered.index("events") if "events" in lowered else None


def frame_events(header: list[str], row: list[str], index: int | None = -1) -> list[str]:
    """Event names ("Category/Name", timestamps removed) recorded on one frame.
    Pass the EVENTS column index (events_index) when parsing many rows, so the header is searched only once."""
    if index == -1:
        index = events_index(header)
    if index is None or index >= len(row) or not row[index].strip():
        return []
    return [entry.split("##")[0].strip() for entry in row[index].split(";") if entry.strip()]


def column_values(header: list[str], rows: list[list[str]], column: str) -> list[float]:
    """Numeric values of one column (empty cells skipped). Raises ValueError if the column is absent."""
    index = header.index(column)
    return [float(row[index]) for row in rows if index < len(row) and row[index].strip() != ""]


def per_frame(header: list[str], rows: list[list[str]], column: str, missing: float | None = None) -> list[float | None]:
    """One value per frame (None, or `missing`, where the column is absent or the cell is empty)."""
    if column not in header:
        return [missing] * len(rows)
    index = header.index(column)
    return [float(row[index]) if index < len(row) and row[index].strip() != "" else missing for row in rows]


def summarize_rows(header: list[str], rows: list[list[str]], columns: tuple[str, ...]) -> dict:
    result = {}
    for column in columns:
        if column in header:
            values = column_values(header, rows, column)
            if values:
                result[column] = summarize(values)
    return result


def gc_metrics(header: list[str], rows: list[list[str]], fps: float = FPS) -> dict:
    """The project's GC metrics for one capture (rules in the module docstring)."""
    bracket = per_frame(header, rows, BRACKET)
    gc_frame = per_frame(header, rows, GC_FRAME, 0.0)
    starts = per_frame(header, rows, CYCLE_STARTS, 0.0)
    pause = per_frame(header, rows, CYCLE_PAUSE, 0.0)
    frame_time = per_frame(header, rows, "FrameTime")
    engine_count = per_frame(header, rows, ENGINE_GC_COUNT, 0.0)
    engine_time = per_frame(header, rows, ENGINE_GC_TIME, 0.0)

    floor_samples = [b for b, g in zip(bracket, gc_frame) if b is not None and b >= 0.0 and g == 0.0]
    floor = percentile(sorted(floor_samples), 50.0) if floor_samples else None

    gc_ms: list[float] = []          # per GC frame
    hitches = 0
    cycles: list[dict] = []          # per cycle: frames, total GC ms, largest single frame
    current: dict | None = None
    for i in range(len(rows)):
        is_gc = gc_frame[i] == 1.0 and bracket[i] is not None and bracket[i] >= 0.0
        if not is_gc:
            current = None
            continue
        ms = max(0.0, bracket[i] - floor) if floor is not None else bracket[i]
        gc_ms.append(ms)
        own_duration = frame_time[i + 1] if i + 1 < len(rows) else None   # FrameTime of row i+1 = frame i (docstring)
        if own_duration is not None and own_duration > HITCH_MS:
            hitches += 1
        if starts[i] > 0.0 or current is None:
            # A new cycle starts here (or the capture began mid-cycle, which is counted as a partial cycle).
            current = {"frames": 0, "total_ms": 0.0, "max_ms": 0.0, "complete": starts[i] > 0.0}
            cycles.append(current)
        current["frames"] += 1
        current["total_ms"] += ms
        current["max_ms"] = max(current["max_ms"], ms)
    if current is not None:
        # The capture ended inside this cycle (its last GC frames were not captured): partial, not complete.
        current["complete"] = False

    complete = [c for c in cycles if c["complete"]]
    minutes = len(rows) / fps / 60.0 if rows else 0.0
    spikes = sum(1 for ms in gc_ms if ms > SPIKE_MS)
    return {
        "floor_ms": floor,
        "floor_samples": len(floor_samples),
        "gc_frames": len(gc_ms),
        "gc_frame_ms": summarize(gc_ms),
        "cycle_starts": int(sum(starts)),
        "engine_gc_count": int(sum(engine_count)),
        "cycle_pause_ms": summarize([p for p, s in zip(pause, starts) if s > 0.0]),
        "cycle_total_ms": summarize([c["total_ms"] for c in complete]),
        "cycle_max_frame_ms": summarize([c["max_ms"] for c in complete]),
        "cycle_frames": summarize([float(c["frames"]) for c in complete]),
        "engine_gc_time_on_gc_frames_ms": summarize([t for t, g in zip(engine_time, gc_frame) if g == 1.0]),
        "spikes": spikes,
        "hitches": hitches,
        "minutes": minutes,
        "spikes_per_min": spikes / minutes if minutes else None,
        "hitches_per_min": hitches / minutes if minutes else None,
    }


def analyse(path: str, columns: tuple[str, ...] = DEFAULT_COLUMNS, fps: float = FPS) -> dict:
    header, data, metadata, finalised = load_capture(path)
    event_counts: dict[str, int] = {}
    index = events_index(header)
    for row in data:
        for event in frame_events(header, row, index):
            key = event.split("/")[0] if event.startswith("PSO/") else event
            event_counts[key] = event_counts.get(key, 0) + 1
    return {"file": os.path.basename(path), "frames": len(data), "finalised": finalised, "metadata": metadata,
            "events": event_counts, "columns": summarize_rows(header, data, columns), "gc": gc_metrics(header, data, fps)}


def selftest() -> None:
    """Checks the statistics, parsing and GC metrics against values worked out by hand."""
    values = [float(v) for v in range(1, 11)]                   # 1..10
    assert percentile(values, 50) == 5.5                          # position 4.5 -> 5 + 0.5*(6-5)
    assert abs(percentile(values, 95) - 9.55) < 1e-12             # position 8.55 -> 9 + 0.55*1
    assert abs(percentile(values, 99) - 9.91) < 1e-12             # position 8.91 -> 9 + 0.91*1
    assert percentile([7.0], 99) == 7.0
    s = summarize([4.0, 1.0, 3.0, 2.0])
    assert s["n"] == 4 and s["min"] == 1.0 and s["max"] == 4.0 and s["mean"] == 2.5 and s["median"] == 2.5
    assert summarize([]) == {"n": 0}
    header = ["EVENTS", "FrameTime"]
    assert frame_events(header, ["GCBWorkload/BenchStart##1.5;PSO/Encountered new graphics PSO##1.6", "3"]) == \
        ["GCBWorkload/BenchStart", "PSO/Encountered new graphics PSO"]
    assert frame_events(header, ["", "3"]) == []

    # GC metrics on 8 hand-made frames. Non-GC brackets 0.10, 0.20, 0.30 -> floor 0.20.
    # Cycle 1: start frame (bracket 6.20 -> 6.0 ms, pause 5.9) + one purge frame (1.20 -> 1.0 ms).
    # Cycle 2: start frame (2.20 -> 2.0 ms; it lasted 40 ms, which the NEXT row's FrameTime holds -> a hitch), then a
    # non-GC frame ends it. Row 5's own FrameTime (10) is the frame before, so it must not count.
    # Frame 0 is a GC frame without a start: a partial cycle (the capture began mid-cycle), excluded from cycle stats.
    h = ["FrameTime", BRACKET, GC_FRAME, CYCLE_STARTS, CYCLE_PAUSE, ENGINE_GC_COUNT]
    r = [["10", "1.20", "1", "0", "0", ""],     # partial cycle: 1.0 ms
         ["10", "0.10", "0", "0", "0", ""],
         ["10", "6.20", "1", "1", "5.9", "1"],  # cycle 1 start: 6.0 ms -> spike
         ["10", "1.20", "1", "0", "0", ""],     # cycle 1 purge: 1.0 ms
         ["10", "0.20", "0", "0", "0", ""],
         ["10", "2.20", "1", "1", "2.1", "1"],  # cycle 2 start: 2.0 ms, hitch (its duration is in the next row)
         ["40", "0.30", "0", "0", "0", ""],     # FrameTime 40 = the duration of row 5's frame
         ["10", "-1", "0", "0", "0", ""]]       # bracket not opened: ignored for the floor
    g = gc_metrics(h, r, fps=60.0)
    assert abs(g["floor_ms"] - 0.20) < 1e-12 and g["floor_samples"] == 3
    assert g["gc_frames"] == 4 and abs(g["gc_frame_ms"]["max"] - 6.0) < 1e-9
    assert g["cycle_starts"] == 2 and g["engine_gc_count"] == 2
    assert g["cycle_pause_ms"]["n"] == 2 and abs(g["cycle_pause_ms"]["max"] - 5.9) < 1e-12
    assert g["cycle_total_ms"]["n"] == 2 and abs(g["cycle_total_ms"]["max"] - 7.0) < 1e-9
    assert abs(g["cycle_max_frame_ms"]["min"] - 2.0) < 1e-9
    assert g["spikes"] == 1 and g["hitches"] == 1
    assert abs(g["minutes"] - 8 / 60.0 / 60.0) < 1e-12

    # A capture that ends inside a cycle: the cycle is partial, so it is not in the cycle statistics.
    r_end = r[:5] + [["10", "3.20", "1", "1", "3.1", "1"]]   # frames 0-4 as above, then a cycle starts on the last frame
    g_end = gc_metrics(h, r_end, fps=60.0)
    assert g_end["cycle_starts"] == 2 and g_end["cycle_pause_ms"]["n"] == 2   # the pause is still counted
    assert g_end["cycle_total_ms"]["n"] == 1                                 # only cycle 1 is complete
    print("selftest: OK (percentiles, summary, event parsing and GC metrics match hand-worked values)")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("captures", nargs="*")
    parser.add_argument("--columns", default=",".join(DEFAULT_COLUMNS))
    parser.add_argument("--fps", type=float, default=FPS)
    parser.add_argument("--json")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)

    if args.selftest:
        selftest()
        return 0
    if not args.captures:
        parser.error("give at least one capture, or --selftest")

    columns = tuple(c.strip() for c in args.columns.split(",") if c.strip())
    results = [analyse(path, columns, args.fps) for path in args.captures]
    for result in results:
        flag = "" if result["finalised"] else "  [NOT FINALISED: partial capture, treat with care]"
        print(f"{result['file']}  ({result['frames']} frames, config={result['metadata'].get('gcbconfig', '?')}, "
              f"events={result['events']}){flag}")
        for column, s in result["columns"].items():
            print(f"  {column:<28} median {s['median']:10.3f}  p95 {s['p95']:10.3f}  p99 {s['p99']:10.3f}  max {s['max']:10.3f}")
        g = result["gc"]
        pause, frame = g["cycle_pause_ms"], g["gc_frame_ms"]
        print(f"  GC: {g['cycle_starts']} cycles (engine GC/Count {g['engine_gc_count']}), floor {g['floor_ms']} ms")
        if pause["n"]:
            print(f"      cycle pause  median {pause['median']:.3f}  p95 {pause['p95']:.3f}  max {pause['max']:.3f} ms")
        if frame["n"]:
            print(f"      GC frame ms  median {frame['median']:.3f}  p95 {frame['p95']:.3f}  max {frame['max']:.3f} "
                  f"({g['gc_frames']} frames); spikes/min {g['spikes_per_min']:.2f}, hitches/min {g['hitches_per_min']:.2f}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(results, handle, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
