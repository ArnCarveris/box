# Systems panel of the stats command

A stats systems panel that lists the ten ECS systems that spent the most time in the last frame, by name, with the frame total.

## Details

`stats systems` shows the SystemsPanel template of `etc/ui/stats_panels.flecs`, which reads the EcsStats singleton: showing the panel adds it, which imports the flecs stats addon, and hiding it removes the singleton, which disables the monitors of the addon again (only when the mirror imported it) and turns the system time measurement off unless a SystemMonitor still reads it. Every period (one second by default) `src/monitor/stats.c` reduces the per frame samples of the addon since the last collection, at most the sixty it keeps, into the average, minimum and maximum milliseconds per frame of every system, keeps the ten with the highest average, leaves the monitors of the addon itself out, and marks the component modified, which is what redraws the panel. `SystemTime` gained min_ms and max_ms, `BOX_MONITOR_TOP` grew to ten, and the SystemMonitor per frame source stays as it was.

Verification: monitor Stats for the min, average and maximum, the modified event per period, the addon being disabled on remove and enabled again on set, and the monitors being left out; monitor StatsPanels for the imported addon, the named rows and the ten row cap; command Stats for the listing and the printed table; the stats and monitor command scenes; the default scene with the panel open, read twice a second apart.
