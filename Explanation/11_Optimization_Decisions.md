# 11 · Optimization Decisions: GCBudget

**No optimisation was made from measurements.** Phase 8, where data-driven changes would have been made, was not run (by the owner's direction, `09`).

Two decisions are recorded so nobody mistakes them for omissions:
- **U1 was not tuned.** Its ~1.4 s rebuild stall at 100k (`09` S6) could be reduced, for example with larger pages or by rebuilding only some clusters per cycle. But U1 is the baseline as designed in `03`, and tuning a baseline to look better would bias the comparison.
- **Fixes made during implementation were correctness fixes, not optimisations.** They are listed in `09`'s audit trails (for example U1's `AddToCluster` and the hitch rule).
