# stats_systems

## Built
- `stats systems` shows the ten ECS systems with the highest average time per frame over the
  last second, with the cheapest and most expensive frame of each, refreshed every second
  from the flecs stats addon (the sixty per frame samples of its one second period, reduced
  over the frames since the last collection).
- Showing the panel adds EcsStats, which imports the addon; hiding it removes the singleton,
  which disables the monitors of the addon again and turns the system time measurement off.
  The monitors of the addon itself are left out of the list.
- SystemTime carries min_ms and max_ms, BOX_MONITOR_TOP is ten, EcsStats is marked modified
  per collection.

## Verified
- monitor Stats (12), StatsPanels (18) and Systems (7), command Stats (14) in `tests.json`;
  monitor (89) and command (233) projects pass; lint 0 problems.
- The stats and monitor command scenes with their regenerated expectations.
- Default scene with the panel open, read twice a second apart: BoxRenderExtract on top with
  min 1.316, avg 1.332, max 1.351 ms; the numbers change between reads.

## Left out
- No bench: nothing runs while the panel is closed, the addon's monitors are disabled then.
