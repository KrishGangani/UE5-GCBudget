# Benchmark batch 2026-11-21_052736_s4verifygc

## Environment

```json
{
  "date": "2026-11-21T05:27:36",
  "engine": "5.8.4 (CL 58902052)",
  "build_config": "Development (packaged)",
  "compiler": "MSVC 14.44.35229, Windows SDK 10.0.22621.0",
  "cpu": "Intel(R) Core(TM) i7-14650HX",
  "gpus": "Intel(R) UHD Graphics (driver 32.0.101.7088); NVIDIA GeForce RTX 5070 Laptop GPU (driver 32.0.16.1047)",
  "ram_gb": "31.6",
  "os": "Microsoft Windows 11 Home Single Language build 26200",
  "power_plan": "Power Scheme GUID: 6fecc5ae-f350-48a5-b669-b472cb895ccf  (Turbo)",
  "on_ac_power": true,
  "resolution": "1280x720 windowed, default scalability",
  "common_args": "-windowed -ResX=1280 -ResY=720 -nosound -unattended -deterministic -FPS=60 -novsync -GCBExit -csvStartOnEvent=BenchStart -csvStopOnEvent=BenchEnd -VERIFYGC -traceautostart=0",
  "traced": false,
  "gc_verification": true,
  "seed": 1,
  "gc_cadence_frames": 120,
  "extra_args": "",
  "cpu_affinity": "0xFFFF (performance cores only; hybrid CPU)"
}
```

## Results

Each cell: median across runs of each run's statistic, then [min-max across runs]. GC metrics as defined in `Tools/csv_stats.py` (01b_Research_Findings.md section 4).

| Config | N | Runs | Cycles | Cycle pause p95 / max (ms) | GC frame ms p95 / max | Spikes /min | Hitches /min | Floor (ms) | UObjects | PhysicalUsedMB | GameThreadTime median (ms) | Update ns/item | Probe ns/lookup | Cluster ms p95 | Probe errors | Log errors |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| U1 | 10000 | 1 | 10 [10-10] | 3.208 [3.208-3.208] / 3.228 [3.228-3.228] | 3.178 [3.178-3.178] / 3.256 [3.256-3.256] | 0.00 [0.00-0.00] | 0.00 [0.00-0.00] | 0.027 [0.027-0.027] | 45649 [45649-45649] | 1261.6 [1261.6-1261.6] | 1.360 [1.360-1.360] | 9.73 [9.73-9.73] | 29.50 [29.50-29.50] | 0.000 [0.000-0.000] | 0 | 0 |
| U1 | 100000 | 1 | 10 [10-10] | 13.455 [13.455-13.455] / 13.552 [13.552-13.552] | 13.276 [13.276-13.276] / 13.581 [13.581-13.581] | 30.00 [30.00-30.00] | 30.00 [30.00-30.00] | 0.029 [0.029-0.029] | 136740 [136740-136740] | 1312.2 [1312.2-1312.2] | 2.191 [2.191-2.191] | 8.79 [8.79-8.79] | 41.00 [41.00-41.00] | 0.000 [0.000-0.000] | 0 | 0 |

## Checksums (SC7)

Every measured run with the same items, seed and frame count, per configuration:

| N | Seed | Frames | Checksums per configuration | Equal |
|---|---|---|---|---|
| 10000 | 1 | 1501 | U1: 0x83C178D5 | yes |
| 100000 | 1 | 1501 | U1: 0xBD626A07 | yes |

Raw files: `Benchmarks/Raw/2026-11-21_052736_s4verifygc_*` (CSV, run report and log per run) and `2026-11-21_052736_s4verifygc_manifest.json`.
