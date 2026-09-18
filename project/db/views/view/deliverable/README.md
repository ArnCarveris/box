# View component

`box.view` adds `View` (camera, target, clear, clear_alpha, priority, shadows,
effects, material_layers, enabled, the flags true through a ctor), `RenderTarget`
(width, height) and the private `ViewImpl` and `RenderTargetImpl`. A view entity
overrides `ClusterSettings`, `EnvironmentLight` and `MaterialLayerSettings` for
itself with their singleton as the default; `BakeSettings` follows the first view
because the probe bake is shared, as `include/box/view.h` documents.

The packet gained a `views` vector of `box_view_t` sorted by priority: the struct
keeps its uniform layout first and appends the target handle, size, clear color,
flags and the light and sorted ranges of the view. `p->view` stays a copy of
`views[0]`, so every existing reader and test is untouched, and a world with no
`View` synthesizes one from the active camera, which keeps all 13 existing scenes
bit identical against `materials/deliverable/integration`.

Lights are culled per view into one shared array with a per view range, the
sorted list is ordered per camera, and every view gets its own depth target, view
uniform, light and cluster buffers and milk glass copy, with four prefiltered
environments shared by generation. `Capture` on a camera reads that view's
target: `two_views.flecs` writes the main view at 960x540 and the side view's
640x360 `RenderTarget` next to it. 25 tests in `test/view`, six projects green,
lint 0, 586 fps; `Extract.*` stays within 2.6% of a same session baseline of the
previous commit, while `bench.json` compares against the faster materials run.
