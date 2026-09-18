# Strict layer views

`View.strict_layers` (default false) makes a view that lists layers draw the
members of those layers only, instead of also drawing every entity that joined
no layer. It is off unless `View.layers` is non empty, so a strict view without
a list still shows everything; `box_view_t.strict_layers` carries the resolved
flag to the renderer.

`box_layer_shown(mask, view_mask, strict)` and `box_view_shows(view, mask)` in
`src/renderer/packet.h` are the one test both sides use: a mask of zero is shown
unless the view is strict. Extract applies it to dynamic instances
(`extract.c`), sorted draws (`transparent.c`) and lights (`extract_lights.c`);
the renderer applies it to the static slot runs in `scene.c` and to the culling
ranges in `cull.c`, where a mesh without runs holds unlayered slots only and a
strict view now skips it instead of drawing it whole.

`src/command/render.c` sets the flag on the temporary view of the render
command, so `render <live entity>` no longer shows the scene behind its subject:
`render_render_0_0.png` of the command scene is the crate on the clear color
alone, and `render.expected` is unchanged.

22 tests in `view.Layers` (6 new, `tests.json`), 12 in `command.Render` (1 new
asserting the packet of a live subject), all six test projects green, lint 0.
`bench.json` against the frustum culling baseline: every extract case within
noise, nothing above 3% slower.
