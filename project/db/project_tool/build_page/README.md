# Build page

A Build tab mirroring the Tests and Benchmarks pages: a summary header, an expandable hierarchy of build reports down to steps and files, and charts of build time over epics.

## Details

The tracker reads every `build*.json` under the deliverable folders and serves them from an endpoint like the test and benchmark ones. The page opens with summary tiles (latest total build time per kind: debug, release, wasm; compiled file count; link time; git SHA of the latest report; trend sparklines over epics), then one collapsible tree: report rows (epic, kind, SHA, total time, trend), expanding into per project rows with totals by step kind, expanding into the steps with their durations, the slowest files first; a search box filters by file or project name and rows sort by duration. Charts under the rows show the build time over the report history and the split by kind; the same tokens, themes, persistence and inline SVG as the other pages.

Verification: tracker tests for the aggregation over a temporary tree with two reports, a node stub DOM check of the tree, and a headless pass without console errors with a screenshot as the deliverable.

## References

- `project/pm.py`, `project/index.html`, the report hierarchy task
