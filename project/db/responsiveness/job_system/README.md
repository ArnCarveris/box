# Job system

A worker pool with prioritised jobs, completion on the main thread and a per frame time budget.

## Details

A box.job module runs a pool sized from the core count with a priority queue; a job is submitted with a priority and an optional world position, runs on a worker and reports completion on the main thread in PreUpdate inside a configurable time budget. Jobs can be cancelled and superseded, which matters when the camera moves and a queued bake or rasterization is no longer interesting. Everything is configured through components, including a JobSettings singleton for the worker count, the budget and the priority policy.

Verification: unit tests cover submission, priority ordering, position based priority, cancellation, superseding, completion on the main thread, the budget and shutdown with jobs in flight. A benchmark submits 100k short jobs and reports throughput and the worst frame time.
