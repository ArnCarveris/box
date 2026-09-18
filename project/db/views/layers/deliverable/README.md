# Render layers

`box.view` adds the `Layer` relationship: an entity or a light joins a layer with
`(Layer, any_entity)`. It fragments, is not exclusive and is inherited on
instantiation, so a prefab hands its layers to its instances. `View` gains
`layers[8]` and `shadow_layers[8]`, entity lists that end at their first zero and
show every layer when empty; `BOX_VIEW_LAYER_MAX` documents the eight entries.

A layer gets a bit the first time an entity joins it, up to 32, after which a
warning says the rest is shown everywhere. Instances keep their membership in
`RenderInstance`, lights resolve theirs once per table, a mask of 0 is drawn by
every view, and `box_view_t` carries `layer_mask` and the `caster_mask` the shadows
task reads. Dynamic and sorted draws are filtered per view during extract.

Static slots of a mesh are partitioned into contiguous runs of one mask
(`src/render/batch.c`): a slot lands in the run of its mask at the cost of one
move per run behind it, the runs travel in `packet.static_groups` only when they
change, and the renderer draws the runs a view shows, coalescing neighbours. A
scene without layers keeps one unpartitioned array and its single draw call.

16 tests in `view.Layers`, six projects green, lint 0, 596 fps, wasm builds, the 14
existing scenes bit identical. `Extract.dynamic_10k` +3.8%, `static_1000_changed`
-2.0%; `Tiling.extract_10k` reads +12% but the pre change binary measures the same
range on this loaded machine. `layers_views_a/b.png` show one world through two
views: layer `a` red, layer `b` blue, the green pillar in both with its own light.
