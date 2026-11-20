# P04 · GCBudget: garbage-collection-aware gameplay data

**Domain:** Memory & resource optimisation · UObject lifecycle · **Difficulty:** ★★★½☆ · **Engine:** UE 5.8.4

> **Status: planned; Phases 1–3 (problem analysis, research, architecture) are starting.** The ten projects are developed in parallel, and every Unreal Engine build and benchmark runs one at a time on the same machine, so measurements stay clean. Everything below is the plan. The success criteria are **targets fixed before any code exists, not results**. Real numbers will only come from controlled before/after runs, each traceable to a raw benchmark batch.

## The problem

Designs that make every item, effect or payload a UObject raise the UObject count. GC reachability time grows with it and causes hitches. Epic added a "UObject Count" counter in 5.8 "since object count strongly affects GC cost"

## Proposed solution

Measure GC cost against object count. Move hot gameplay data to struct arrays with stable handles. Keep UObjects where reflection or ownership needs them. Compare with GC clustering and incremental reachability (Experimental)

## Main Unreal Engine technologies

UObject/GC internals, `TObjectPtr`, USTRUCT arrays, handles, `gc.*` cvars, Memory Insights, LLM tags, `obj list`

## Success criteria (targets, defined before implementation)

GC pause ms (p95/max) at 10k / 100k items; UObject count; resident MB; GC-caused hitches per minute; per-item access cost (µs) so the trade-off is visible

## Existing work it must be compared against

GC docs, GC clustering, incremental reachability (Experimental), `TObjectPtr` write barriers, 5.8 UObject Count counter

**The gap it targets:** Measured GC-cost-vs-design curves and a migration pattern for gameplay data (gap strength: Medium (my assessment; re-checked in Phase 2)).

**Main risk:** Must not turn into a "basic inventory". The subject is object-count scaling

## Why studios care

Memory appears in 18/57 postings; EA NHL "ownership of memory management"; Snowdrop "memory usage and loading times"; GC hitch reports

## Method

Every project in the portfolio follows the same ten phases: problem analysis → research → architecture → foundation → implementation → explanation → testing → profiling → review → portfolio. Each phase is documented in `Explanation/` (files 00–12, including a line-by-line code walkthrough), and every claimed improvement must be measured against the existing solutions above on the same workload. Engine: Unreal Engine 5.8.4, C++.

## Folder layout

```text
P04_GCBudget\
├── README.md        ← this page (becomes the project's portfolio page)
├── Explanation\     ← 00–12, created when the project starts
├── Benchmarks\      ← Raw\ + Summaries\
├── Media\           ← diagrams, screenshots, demo video
├── Tools\           ← analysis and run scripts
└── GCBudget\      ← the Unreal Engine project (created in its Phase 4)
```
