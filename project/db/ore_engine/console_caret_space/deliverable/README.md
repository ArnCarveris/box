# console_caret_space

## Built

`box_ui_input_offset` in `src/ui/text_input.c` measured the text before the
caret with `box_ui_text_run`, which drops a run of trailing spaces (the wrap
rule for paragraph `Text`). The offset now adds the width of the trailing
spaces/tabs of the prefix back on top of the measured width, so the caret
advances by the space width of the font on every typed space. `text.c`'s wrap
rule is untouched, so ordinary `Text` still drops trailing spaces when it
wraps.

## Verified

- Added `TextInput_caret_advances_by_the_width_of_a_typed_space` to
  `test/ui/src/TextInput.c` (and `test/ui/project.json`); it fails before the
  fix (caret does not move on the first space) and passes after.
- `bake3 run test/ui --local-env=caret -- TextInput -j 8`: 14 passed.
- `bake3 run test/ui --local-env=caret -- -j 8`: 183 passed (whole project,
  includes `Console`, untouched by this change).
- `python3 tools/lint.py`: 0 problems.

## Left out

The scrollback prompt echo in `src/ui/console.c` may hide a trailing space
the same way; that file is owned by another agent working in parallel, so it
was not checked or touched here. The console and command visual scenes were
not re-rendered (out of scope of this task's file ownership and the economy
rule); the caret is a small offset change unlikely to move those scenes, but
the owning agent should confirm.
