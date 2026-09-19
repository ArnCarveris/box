# Console as a translucent left pane

## Built
`console.flecs` sizes its `Ui` root from the surface (`height: 0`) and pins
`panel` left with a new `box.ui` primitive, `Fill`: a fraction of the parent's
box, or, for a root with neither `Layout` nor `Rect`, the view that draws it.
`panel` fills half the view width and its whole height, `Rgb {0,0,0}` at
`Opacity 0.75`, no border, no shadow. `scrollback` drops its fixed height to
grow from the top; `prompt` follows through the column flow, not the bottom.

## Verified
`test/ui -- -j 8`: 182 passed (2 new `Layout` cases for `Fill`), lint clean.
Visual baseline of `console.flecs` refreshed (screenshot here), its command
scene unchanged and green. Full `--env console` sweep: `visual` (96 scenes)
and `command` (17), only console's baseline touched.

## Left out
No flex "remaining space" mode: `input` fills `prompt`'s box, not the space
left after `marker` (invisible, `TextInput` draws no background).
