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

## Reopened: grey, not black
Two engine causes, both fixed. A UI view lit its tree with a flat white environment, so a
black node kept the few percent a dielectric reflects and came out grey 43 over a black sky;
UI views now set `unlit` in the view packet and `mesh.wgsl` shades them as albedo plus
emissive (`unlit_shade`). And an element that composites its subtree as a group drew its
own body into the group target with its opacity and the group quad applied that opacity
again (0.75 x 0.75); the element is tagged `RenderGrouped` for the life of its group and
`box_render_fill_instance` keeps its draw at full strength. `console_app.png` is a capture
of `box_app` on the default scene through the command port after `key.press Grave`: the pane
is 1 over the black sky and 25% of the floor shows through it.
Baselines refreshed with the reason: every UI scene (`ui_*`, `resolution_scale`, `console`)
now shows the authored node colors instead of the lit ones; `ui_group_alpha` and
`ui_group_alpha_direct` also lose the doubled opacity of the group element.
Note: the shaders are read from the installed etc, so the binary must run through
`bake3 run`; the raw path under `.bake/local_env/.../bin` still showed the old grey.
