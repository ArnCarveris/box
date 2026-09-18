# bake3 JSON test report

Extend the bake3 test harness with a --json report listing every case, its status and execution time.

## Details

The bake3 test binary runs every case in a child process, serially or from a parallel job plan. Both paths now time each case and record its outcome as pass, fail, empty, quarantined or error. Passing `--json <path>` after `--` writes a report with the project id, totals, wall-clock time and the per-case list.

Local environments previously copied the harness templates only when missing, so a template change never reached an existing environment. They now refresh when the source templates are newer.

## References

- bake3 commit 485c481 (harness report), 290a398 (template refresh)
- bake3 README, section "Test projects"
