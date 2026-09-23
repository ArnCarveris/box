# Systems panel of the stats command

A stats systems panel that lists the ten ECS systems that spent the most time in the last frame, by name, with the frame total.

## Details

The SystemMonitor singleton already attributes frame time per system; its top list grows from eight to ten (`BOX_MONITOR_TOP`), its collecting system runs immediate and marks the component modified after every collected frame, so the panel redraws. `stats systems` shows the SystemsPanel template of `etc/ui/stats_panels.flecs`, which adds the singleton with enabled set and removes it again when hidden, so the flecs system time measurement is only on while the panel is up; the command prints the same rows.

Verification: monitor StatsPanels for the enabled singleton, the named rows and the ten row cap; command Stats for the listing and the printed rows; the stats and monitor command scenes; the default scene with the panel open.
