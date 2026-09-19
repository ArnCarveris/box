# Console caret stays put on a space

Typing a space in the console input does not move the caret; it jumps at the next non space character, so the caret must advance by the width of every typed space.

## Details

The caret of a `TextInput` sits at the measured width of the text before it, and the text
measure in `src/ui/text.c` drops a run of trailing spaces (`box_ui_text_width` returns the
width up to the last word start), which is right for wrapping a paragraph but wrong for the
line an input measures, so the caret only moves once a character ends the run. Measure the
text before the caret with its trailing spaces, either through a measure entry that keeps
them or by having the input measure up to the caret with the space width of the font, and keep
the wrap rule for ordinary `Text`. The same rule may hide a trailing space in the scrollback
prompt echo; check it.

Verification: a `test/ui` case typing `a`, space, space into an input asserts the caret x grows
by the space width each time and fails before the fix, `test/ui` green, the console visual
scene and command scene unchanged.

## References

- `src/ui/text_input.c`, `src/ui/text.c`, `test/ui/src/TextInput.c`
  (`caret_sits_at_the_width_of_the_text_before_it`), `project/db/ui/text/README.md`
