# Scroll component for UI elements

The console scrollback overflows into the scene; add a Scroll component to box.ui with the CSS overflow modes (visible, hidden, scroll, auto per axis) that clips the children to the box and moves them by an offset the wheel, a drag or a script sets.

## Details

Nothing in `box.ui` clips a child that sticks out of its parent except the composite path of a
transparent subtree, so the console scrollback runs past its pane into the scene. Add
`Scroll {x, y, offset}` to `include/box/ui.h` where `x` and `y` are an `Overflow` enum with the
CSS values `Visible` (the default, today's behaviour), `Hidden` (clip, no user scrolling),
`Scroll` (clip, always scrollable) and `Auto` (clip, scrollable when the content is larger
than the box), and `offset` is the scroll position in logical pixels the framework clamps to
the content size. The layout pass in `src/ui/layout.c` measures the content of a scrolled
element without the box limiting it, and places the children shifted by the offset; the render
path clips the subtree to the Bounds of the element on every draw, not only when compositing,
through a clip rectangle on the node (`src/ui/tree.c`, the UI packet in `src/render`) that the
renderer applies as a scissor or in the shader. The wheel (`InputScroll`, `Mouse.scroll_delta`)
over a hovered scrollable element moves the offset on the innermost scrollable ancestor, and a
script or C can write it directly; a `ConsoleLog` that grows keeps the offset at the end
unless the user scrolled up, which is what the console needs. A scrollbar is not part of this
task. Update `etc/ui/console.flecs` to put `Scroll {y: Auto}` on its scrollback pane so the
log clips and scrolls; the `lines` cap of `ConsoleLog` stays as the amount of log it keeps.

Verification: `test/ui` cases for each mode on each axis (clipping, clamping of the offset,
content larger and smaller than the box, wheel on hovered and unhovered elements, nested
scrollables, the console log staying pinned to the end while it grows and staying put after a
scroll up), a `ui_scroll` visual scene with the four modes side by side baselined, the console
visual and command scenes refreshed with the overflow gone, every other scene unchanged, and
a `bench/ui` case for the layout of a scrolled subtree with `bench.json` attached.

## References

- `include/box/ui.h`, `src/ui/layout.c`, `src/ui/tree.c`, `src/ui/console.c`,
  `src/render/extract.c`, `etc/ui/console.flecs`, `project/db/ui/deliverable/README.md`,
  `project/db/ore_engine/ui_canonical/README.md` (the scroll view widget builds on this)
