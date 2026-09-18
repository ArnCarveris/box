# Test and benchmark report pages

Per suite time breakdown charts with expandable test cases, the same for benchmarks, and no horizontal page scroll.

## Details

The Tests page shows, per test project, a breakdown of time spent per suite as the primary view: a table of suites with case count, status and time, and a chart of time spent over time with one line per suite, built from every report under `project/db`. Each suite expands into its individual test cases with their status and time and a per case time history. The Benchmarks page does the same for bake3 benchmark suites and cases once the native bake3 benchmark feature lands, alongside the scene benchmarks: a suite level time breakdown, a chart over time and expansion into cases with their median, confidence interval and samples.

The page currently always shows a horizontal scrollbar because the detail panel sits to the right of the viewport; the layout must fit the viewport width with the panel hidden and never scroll horizontally. Keep the persisted UI state, the light and dark themes and the inline SVG charts.

This task waits for the bake3 benchmark integration so the report format is final.
