# bake3 test harness timeout

The bake3 test harness busy waits on case subprocesses and never times them out, so a hung case burns cores for hours; block on the pipe and kill a case after a timeout, reporting it failed.

## Details

Twice now a test run left processes spinning at full CPU for hours: once test cases themselves in a stale build, once the runner's eight workers in `bake_run_subprocess_to_stream` waiting on children that never exited. Benchmarks taken while that happened drifted by 20% across unrelated modules. The harness must wait on the child's pipe with a blocking read or poll instead of spinning, apply a per case timeout (default 60 seconds, `--timeout <s>` on the run and a `"timeout"` field per suite in `project.json`), kill the process group of a case that exceeds it, report the case as failed with a `timeout` status in the terminal and in the `--json` report, and exit non zero. The bench harness gets the same treatment with a larger default.

Verification: bake3 self tests with a case that sleeps past a short timeout (reported failed with the timeout status, the process gone, the run finishing within the timeout plus a margin), a case that produces no output for a while but finishes (not killed), and a CPU check that a waiting worker uses no CPU. The installed binary is rebuilt.

## References

- bake3 `templates/test_harness/bake_test.c` (`bake_run_subprocess_to_stream`, the worker loop)
- `docs/PROCESS.md`, the noise sources section
