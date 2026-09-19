# Build page

`pm.py` folds every bake3 `--build-json` report under `project/db` into report rows with their
per project totals and step tree, a history per kind and per kind and project and an overall
summary (`aggregate_builds`, `/api/builds`); `summarize_build` puts the kind, target, files,
time, commit and steps of one in the deliverable listing of an item.

The **Build** tab mirrors the Tests and Benchmarks pages: tiles for the latest build time per
kind (debug, release, wasm) with sparklines over the epics, files, link time, commit and
reports; collapsible trend charts per kind and per step kind; and one sortable, searchable
report to project to step tree, slowest files first, with a chart under an open row. Same
tokens, both themes, inline SVG and persisted rows, sort and search.

## Verified

- `project/test_pm.py --json`: 61 cases green, three new (aggregation over a temporary tree with
  two reports and `/api/builds`, the deliverable listing of a build report, a node DOM check).
- Headless Chrome on a private server on port 8099 (8090 untouched): no console errors, with
  `build_page.png` dark and `build_page_light.png` light, tree open to the steps.
