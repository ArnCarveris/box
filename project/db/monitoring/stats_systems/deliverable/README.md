# stats_systems

## Built
- `stats systems` shows the SystemsPanel template: the ten systems that spent the most time
  in the last frame, by name and slowest first, with the frame total and system count. Showing
  the panel adds SystemMonitor with enabled set; hiding it removes the singleton, which turns
  the flecs system time measurement off again.
- SystemMonitor's top list holds ten entries and its collecting system runs immediate and marks
  the component modified per collected frame.

## Verified
- monitor StatsPanels (18) and Systems (7), command Stats (14) in `tests.json`; monitor (84),
  command (233) and ui (280) projects pass; lint 0 problems.
- The stats and monitor command scenes with their regenerated expectations.
- Default scene with the panel open: ten named rows, BoxRenderExtract and BoxUiUpdate on top.

## Left out
- No bench: nothing new runs while the panel is closed.
