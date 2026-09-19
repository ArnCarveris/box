# Vertex color alpha

Built: `rgba` joined `box.types` with reflection, `Mesh3.colors` switched from
`rgb` to it, and `box_mesh3_colors` packs the alpha byte from the component
instead of forcing it opaque. The glTF importer reads a `COLOR_0` alpha as a
linear byte for a `VEC4` accessor and defaults to opaque for a `VEC3` one or no
colors at all. The mesh shader already multiplied alpha into roughness from
the vertex colors task, so no shader change was needed.

Verified: `geometry.Mesh3` gained a case that a vertex alpha of 128 packs to
128, `render.Extract` gained a case that alpha reaches the upload, `gltf.Mesh`
checks the alpha of its four component fixture; 62 tests pass and
`tools/lint.py` reports 0 problems. `mesh3.flecs` gained a `vertex_alpha`
entity with an alpha gradient and was rebaselined; `gltf.flecs`'s fixture
stays a `VEC3` so it is untouched. Three existing tests that built or read
`Mesh3.colors` as `rgb` were updated to `rgba`, since the type itself changed;
no assertion's intent changed, only the added alpha byte.

Left out: the roughness change is subtle under this scene's flat light.
