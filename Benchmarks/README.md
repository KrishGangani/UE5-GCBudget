# P04 · Benchmarks

Every number in this project's write-up must trace back to a file in this folder. The method is in [../Explanation/01_Problem_Analysis.md §7](../Explanation/01_Problem_Analysis.md). The results, batch by batch, are in [../Explanation/09_Testing_And_Benchmarks.md](../Explanation/09_Testing_And_Benchmarks.md).

## Measurement rules

1. **Baselines first.** U0, U1, U2 and S1 are captured (roadmap step S7) before the proposed store S0 exists, on the same workload, build and machine as every later run.
2. **Packaged Development build only**, never PIE or the editor. The launcher engine can't build Test.
   - Editor GC numbers are not comparable: no disregard-for-GC pool, keep-flag scans, `TObjectPtr` access tracking.
3. **GC fairness flags:**
   - every timed run passes **`-NoVerifyGC`**, because Development verifies GC assumptions on every GC by default;
   - the GC cadence (a cycle every 120 frames) is set by the game itself, the same in every configuration;
   - the per-run GC settings are written into each CSV's metadata.
4. **Fixed step** (`-deterministic -FPS=60`), so every run simulates the same frames. The capture window is opened and closed by the game itself (`BenchStart` / `BenchEnd` CSV events), never by a timer.
5. **Pinned to the performance cores** (mask `0xFFFF` on this i7-14650HX, `Tools\cpu_affinity.py`).
   - The runner pins **itself** first (`pin_self`), before recording the environment or launching anything, so every game it starts inherits the mask from its first instruction.
   - It launches the real exe directly, waits with a timeout, and kills the process tree in `finally`.
   - The mask read back from Windows (`applied_mask()`) is recorded in the environment.
6. **One discarded priming run per batch**, then **runs per combination = the number of configs in the batch**, interleaved, with the config order rotated every round, so every config sits in every order position once.
7. **Statistics:**
   - median, p95 and p99 per run (linear-interpolation percentiles);
   - per GC cycle for pauses, per GC frame for per-frame GC time, per frame for everything else;
   - then the median across runs with the [min–max] across runs.
   - FPS is never used as evidence.
8. **The environment is recorded with every batch** (below), automatically, by `Tools\run_bench.py` (Phase 5).

## Folders

| Folder | What goes in it | Rule |
|---|---|---|
| `Raw\` | CSV profiler captures (`.csv`), run reports, game logs (`.log`), Unreal Insights traces (`.utrace`), one `_manifest.json` per batch | **Never edited.** Anything re-processed is written to `Summaries\`. Only the manifests are in git (exact command lines, environment, per-run statistics). Every batch cited in the docs is attached as `<batch>_raw.zip` to this repository's **Releases** page |
| `Summaries\` | Computed statistics, SC1–SC8 pass/fail tables, charts | Each summary names the batch it was computed from |

## File naming

`<batch>_<Config>_N<items>_r<run>.<ext>`, with `<batch>` = `YYYY-MM-DD_HHMMSS[_label]` (time-stamped, so two batches never collide; the runner refuses to overwrite).
- `<Config>`: `U0` one UObject per item · `U1` clustered pages · `U2` incremental reachability · `S1` structs with a reference · `S0` structs with handles. N = 0 runs are the empty-level baseline **E0**.
- Example: `2026-11-28_101500_U0_N100000_r1.csv` (+ run report + `.log`), with `2026-11-28_101500_manifest.json` in `Raw\` and `2026-11-28_101500_summary.md` in `Summaries\`.
- `_r0_prime` files are the discarded priming run. They are kept for transparency and excluded from every summary.

## Environment record (written into every manifest and summary)

```text
Engine:            UE 5.8.4 (CL 58902052), launcher build
Build:             Development, standalone packaged (never PIE); -NoVerifyGC
Compiler:          MSVC 14.44.35229 (VS 2022), Windows SDK 10.0.22621.0
CPU / GPU / RAM:   i7-14650HX (8P + 8E, 24 threads) / RTX 5070 Laptop / 32 GB
CPU affinity:      0xFFFF (performance cores only): the runner pins itself, every game inherits it; read back with applied_mask()
Storage:           project on D: (Kingston NVMe), engine on C: (WD NVMe)
OS:                Windows 11 build 26200
Power:             AC, power plan recorded per batch
Resolution:        1280x720 windowed, default scalability, no VSync, no sound
GC cadence:        a cycle every 120 frames (2.0 s at the 60 Hz fixed step), triggered by the game
Command line:      exact, per run, in the manifest
Warm-up excluded:  10 s     Capture length: 60 s     Runs per combination: number of configs (interleaved) + 1 priming run per batch
```

## Run log

| Batch | Configs | N | Runs | Purpose | Summary |
|---|---|---|---|---|---|
| — | — | — | — | No benchmark batches yet. Phase 4 checks are in [09](../Explanation/09_Testing_And_Benchmarks.md) | — |
