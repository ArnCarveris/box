# CPU monitoring

`CpuMonitor` publishes process and machine CPU usage as a reflected singleton.
`src/core/cpu.c` holds the measurement primitives (`box_cpu_process_time`,
`box_cpu_wall_time`, `box_cpu_cores`, `box_cpu_sample`, `box_cpu_sleep_ms`), so
the budget pacing, the startup oversubscription check and the monitor all read
one piece of code and `box.core` keeps no dependency on `box.monitor`.
`src/monitor/cpu_sample.c` turns two readings into moving averages over
`window_s` and publishes the sleep the budget inserted.

Machine load comes from `host_processor_info` on macOS, `/proc/stat` on Linux
and is reported as zero on the web, where only process time is available.

- `tests.json`: 9 cases in `test/monitor` suite `Cpu`, all pass
- `tests_core.json`: 5 cases in `test/core` suite `App`, the budget test included
- `bench.json`: `Monitor.cpu_monitor` costs 3.9 us per frame over `no_monitor`
- `command.log`: `test/integration/command/monitor.flecs` reading the singletons
