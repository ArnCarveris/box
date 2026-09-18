# System monitoring

`SystemMonitor` is off by default. Setting `enabled` turns on the flecs system
time measurement (`ecs_measure_system_time`), which accumulates `time_spent` in
every `ecs_system_t`; a PostFrame system differences that total per system and
publishes the frame total, the slowest eight systems and the time per pipeline
phase. Removing the component turns the measurement off again unless the stats
addon still needs it.

The attribution needs no hooks of its own: flecs already measures per system
time, so the monitor only subtracts the previous frame's total.

- `tests.json`: 7 cases in `test/monitor` suite `Systems`, all pass
- `bench.json`: `Monitor.system_monitor` costs 2.7 us per frame over
  `no_monitor` with 32 systems; all three monitors together cost 18.9 us
