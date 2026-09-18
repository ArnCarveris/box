# ECS statistics

`EcsStats` imports `FlecsStats` the first time it is set and mirrors the addon
into reflected fields every `period_s` seconds: entity and table count, systems
ran, frame, system and merge time, fps, the average frame time over the last
`history` samples and the slowest systems of the period. The world numbers come
from `EcsWorldSummary` and the per system times from the `(EcsSystemStats,
EcsPeriod1s)` map the addon keeps on the world.

Removing the component stops the mirroring; the addon stays imported because a
flecs module cannot be unloaded.

- `tests.json`: 7 cases in `test/monitor` suite `Stats`, all pass
- `bench.json`: `Monitor.ecs_stats` costs 11.8 us per frame over `no_monitor`
- `command.log`: `test/integration/command/monitor.flecs` reading the singletons
