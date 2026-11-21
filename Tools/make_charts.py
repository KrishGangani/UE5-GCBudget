"""P04 GCBudget - the README's two charts, from recorded batch summaries' numbers (values copied from 09; no new runs).

Usage: python -I make_charts.py      (writes Media/gc_pause_vs_items.png and Media/u1_tradeoff.png)
"""
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

MEDIA = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Media")
os.makedirs(MEDIA, exist_ok=True)

# Batch B (2026-11-21_064925_s6batchB, 60 s captures, 2 runs): cycle pause p95, median across runs, ms.
N = [1000, 10000, 30000, 100000]
U0 = [1.046, 1.233, 2.018, 4.011]
S1 = [1.013, 1.073, 1.217, 1.910]
E0 = 1.031
# S7 check batch (2026-11-21_082841_s7check, 20 s captures, 2 runs): S0 at 100k.
S0_100K = 1.408

fig, ax = plt.subplots(figsize=(7, 4.2), dpi=120)
ax.plot(N, U0, "o-", color="#c0392b", label="U0: one UObject per item")
ax.plot(N, S1, "s-", color="#2980b9", label="S1: structs holding a TObjectPtr")
ax.axhline(E0, color="#7f8c8d", linestyle="--", label="E0: no items (engine floor)")
ax.plot([100000], [S0_100K], "D", color="#27ae60", markersize=9, label="S0: structs, definition by index (check batch)")
ax.set_xscale("log")
ax.set_xlabel("items (N)")
ax.set_ylabel("GC cycle pause p95 (ms)")
ax.set_title("GC pause vs item count, UE 5.8.4 packaged Development")
ax.grid(True, which="both", alpha=0.3)
ax.legend(fontsize=8, loc="upper left")
fig.tight_layout()
fig.savefig(os.path.join(MEDIA, "gc_pause_vs_items.png"))

# Batch A (2026-11-21_063108_s6batchA, 100k items, 4 runs): GC pause p95 vs the worst game-thread frame.
labels = ["U0", "U1 (clusters)", "U2 (incremental)", "S1"]
pause = [4.464, 3.097, 4.859, 1.981]
# Per run, the largest GameThreadTime on a request frame (check_batch.py), median across the 4 runs (09 Results).
worst = [7.61, 1473.8, 7.54, 3.93]
fig, (a1, a2) = plt.subplots(1, 2, figsize=(8, 3.8), dpi=120)
a1.bar(labels, pause, color=["#c0392b", "#8e44ad", "#d35400", "#2980b9"])
a1.set_ylabel("GC cycle pause p95 (ms)")
a1.set_title("GC pause (lower is better)")
a2.bar(labels, worst, color=["#c0392b", "#8e44ad", "#d35400", "#2980b9"])
a2.set_yscale("log")
a2.set_ylabel("worst request frame (ms, log)")
a2.set_title("U1's cluster rebuild stall")
for axis in (a1, a2):
    axis.tick_params(axis="x", labelsize=7)
fig.suptitle("100,000 items: U1 lowers the GC pause but stalls ~1.4 s per cycle", fontsize=10)
fig.tight_layout()
fig.savefig(os.path.join(MEDIA, "u1_tradeoff.png"))
print("wrote", MEDIA)
