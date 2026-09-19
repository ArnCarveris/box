# Lines of code in build reports

bake3 counts lines of code with cloc per built project and puts them in the build report; the build page shows them over time; the existing reports are backfilled at their git SHA.

## Details

Three parts, in order. In the bake3 repository (`/Users/sandermertens/GitHub/SanderMertens/bake3`), a build step after discovery runs `cloc --json --quiet` over the source and include folders of every project the build touched (skipping bundles, `.bake` and generated files, with `cloc` absent degrading to no counts and a note) and writes `loc: {files, code, comment, blank, by_language}` per project under `totals.project.<name>` plus a workspace `loc` total into the `--build-json` report, timed as its own step kind so the build page can show its cost; tests and a single line commit in bake3, never pushed, then a reinstall so this project's builds pick it up. In `project/pm.py` and `project/index.html`, the Build page gains the lines of code per project and in total next to the build times, with a chart over reports and the delta between consecutive reports; `project/test_pm.py` covers the parsing and a report without counts. Then `tools/loc_backfill.py` walks every `project/db/*/deliverable/build_*.json`, checks out the report's `git.sha` into a temporary git worktree, runs the same cloc invocation over the same folders and writes the counts into the report in place (idempotent, reports with counts are skipped unless `--force`, a dirty flag in the report is noted in the written block), and is run once so every existing report carries counts.

Verification: bake3 tests, `project/test_pm.py` green, a fresh `bake3 --build-json` report of this project carrying counts, the backfilled reports committed and the Build page showing the series; restart the pm server afterwards (`pkill -f "pm.py serve --port 8090"; nohup python3 project/pm.py serve --port 8090 > /tmp/box_pm_serve.log 2>&1 &`).

## References

- `project/db/project_tool/build_report/deliverable/README.md`, `project/db/project_tool/build_page/deliverable/README.md`, `tools/epic.py`
