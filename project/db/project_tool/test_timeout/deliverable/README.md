# bake3 test harness timeout

Implemented in the bake3 repository (five commits, binary reinstalled). Each test case
runs in its own process group and the runner blocks in poll on the child's pipe instead of
spinning: idle workers used 13 ms of CPU over 4 seconds with four workers. A per case
timeout of 60 seconds by default (`--timeout <sec>`, 0 disables, or a per suite `"timeout"`
in project.json) kills the process group, prints `TIMEOUT Suite.case`, counts the case as
failed with `"status": "timeout"` and a top level timeout count in the JSON report, and exits
non zero. Bench cases run in process, so they got a setitimer watchdog with a 600 second
default. Three bake3 self tests cover the timeout, a quiet but finishing case and the idle
CPU; the full bake3 suite (146 tests) passes. `tests.json` mirrors those three cases.
