# Waiter caught up to the raise of its own load frame

## Built

`ActionWaiters` in `src/input/events.c` now keeps the last raise of a phase that
reached no waiter (`box_missed_t`, per action and phase) and delivers it right
away when `on.pressed`, `on.down` or `on.up` registers, the way `box_pick_hover`
retries a missed hover. `box_input_deliver` returns the number of waiters it
resolved so `BoxInputRaise` knows when to keep or clear the miss. `box_input_emit`
still resolves its waiters immediately and never reaches this path, so a UI click
stays single shot; typed characters are untouched.

## Verified

A reproducer in `test/input` binds a key, injects its press, then loads a script
whose async block awaits `on.pressed` in the same frame; it failed before the fix
and passes after. `test/input` (64 tests), `test/ui` (174), `test/picking` (41)
and `tools/integration.py command` (13 scenes) all pass. Lint 0.

## Left out

Deferring the first input update instead, the other option the task listed, was
not pursued; the catch-up list is a smaller, more local change.
