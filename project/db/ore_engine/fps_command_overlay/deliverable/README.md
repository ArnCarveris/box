# fps command opens the overlay

## Built

`box_fps_command` in `src/command/fps.c` now always enables the overlay on the
target for a plain `fps`, printing the window instead of leaving it disabled.
`fps on` and `fps off` are unchanged: same messages, same enable/disable of
the target's `FpsOverlay`. Target resolution (primary view, singleton
fallback) and the `fps_text` follow-through in `src/monitor/fps.c` already did
the right thing.

## Verified

`test/command` Fps suite: added `on_form_enables_the_overlay_on_the_target`
and `off_form_disables_the_overlay_on_the_target`, updated
`first_call_starts_the_window_without_the_panel` (renamed to
`first_call_starts_the_window_with_the_panel`) to assert the first plain call
enables the overlay. Full `test/command`: 218 passed, 0 failed. Lint: 0
problems. `tools/integration.py command`: all scenes pass, `fps.flecs`
unchanged.

## Left out

No manual `box_app` console check (deferred to hand verification by the
requester); no bench, this task touches no per-frame hot path.
