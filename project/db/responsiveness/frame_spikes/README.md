# Frame spike harness

A harness that proves large workloads lower the average frame rate instead of stalling a frame.

## Details

Extend the benchmark app to report the worst frame and the p99 next to the mean, add a spike budget expressed as a multiple of the mean and fail a run when a frame exceeds it. A scene set stresses loading, baking, rasterization and LOD generation at the same time so regressions from any epic are caught. The harness attributes a spike to a system using the monitoring timers, which makes the report actionable.

Verification: the benchmark reports are the deliverable and unit tests cover the spike detection itself. Run the harness against the existing static and material stress scenes to establish the baseline.
