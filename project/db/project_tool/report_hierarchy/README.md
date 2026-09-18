# Report pages as a project, suite, case hierarchy

Tests and Benchmarks pages open with a summary panel and show a clear project to suite to case hierarchy with statistics at every level.

## Details

Both pages are hard to read today. Each page starts with a summary panel for the whole project: totals (projects, suites, cases, pass and fail, total time for tests; projects, suites, cases, total median time for benchmarks), the trend over the last reports as small sparklines, and the latest report time. Below it one collapsible tree: project rows with their own summary statistics (case count, pass and fail or median total, time, trend sparkline, status chip), expanding to suite rows with the same statistics, expanding to case rows with the case statistics (status and time for tests; median, confidence interval, mean, stddev, p99, iterations, samples and outliers for benchmarks) and a history sparkline. Rows align in a grid with fixed columns so numbers line up, expansions and the sort column persist in the UI state, and a search box filters by name at any level.

The scene benchmarks keep their own section below the microbenchmarks with the same row style. The existing charts move into the expanded rows (a suite's chart under its row, a project's chart under its row) so the page reads top down from summary to detail. Keep the inline SVG charts, the light and dark themes and the endpoints; extend the aggregation in `pm.py` only where a level lacks summary statistics.

Verification: tracker tests for the per level summary aggregation, a node stub DOM check of the tree rendering and filtering, and a headless Chrome pass with screenshots of both pages in the deliverable.

## References

- `project/pm.py`, `project/index.html`, `project/test_pm.py`
