# 12 · Future Scalability: GCBudget

How the toolkit (the plugin's telemetry and handles, and the struct-store pattern) would grow in a production game. None of this is built.

| Need | Direction | Why it fits |
|---|---|---|
| Replication | Keep items in an `FFastArraySerializer` array of structs, with handles as stable client-side ids | The data is already a struct array; no per-item UObject to replicate |
| Save games | Serialise the struct array (it is a `UPROPERTY`) plus each slot's generation | Handles stay valid across a load if generations are saved |
| Polymorphic items | One struct array per item type, or a type tag plus a definition index | S0's definition-by-index already separates shared data from per-item state |
| Larger N | Chunked arrays (no reallocation), parallel update over chunks | The per-item update is independent per lane |
| A CI performance gate | Run the director headless and fail the build if the telemetry's GC pause p95 rises above a budget | The run report and `check_batch.py` already give pass/fail signals |
| A content rule | A data-validation check that flags containers of many small UObjects | The measured cost of U0 (`09` S6) is the argument |
| When to use Mass | Many entities with many fragments and systems | Mass is the engine's own version of the same idea; this project's store suits a single item type |
| Open questions first | The unverified items in `09` (777003, the slow E0 cycles, U1's scaling, U2) | Each needs an Insights drill-down before any of the above is built on these numbers |
