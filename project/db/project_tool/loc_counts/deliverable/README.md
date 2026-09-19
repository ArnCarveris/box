# Lines of code in build reports

Three parts. Bake3 (two commits, reinstalled) runs a `loc` step after discovery for every
touched project, `cloc --json --quiet` over its `src`/`include` folders (excluding `.bake`,
`generated`, `deps`, `bundles`), writing `loc: {files, code, comment, blank, by_language}`
under `totals.project.<name>` and a workspace total, with a `{"note": ...}` degrade when
`cloc` is absent. `project/pm.py`/`index.html` show lines of code per project and in total
on the Build page next to build times, with a trend chart per kind and the delta since the
previous report; `test_pm.py` covers parsing, the note case and the delta.
`tools/loc_backfill.py` walks the 42 existing build reports, checks out each `git.sha` into
a worktree and fills in its counts in place; box's history grows 29,989 -> 54,858 code lines.

## Verified

Bake3's 148 self tests and 228 unit checks pass; `test_pm.py` (64 cases) is green; a fresh
`bake3 --build-json` report of box carries real counts; `test/core` (27 cases) passes
through the reinstalled bake3 in the `loc_counts` environment.

Left out: per-project charts only plot the `code` line count, not comment/blank breakdowns.
