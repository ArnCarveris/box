# render command

`render <subject>` resolves a path against the session cwd and writes
`resources/render_<frame>_<n>.png`, printing that name like `capture`; the image appears
once the frame rendered. A prefab is instantiated with `(IsA, prefab)` and a template by
adding it as a component, both under `terminals.<session>.scratch`, which sits 10000 units
above the scene so a subject that is not in it renders alone; a live entity is rendered in
place. The subject and its descendants join a temporary layer that a temporary `View`
lists, with a 512x512 `RenderTarget`, its own camera, a neutral `EnvironmentLight` and a
`DirectionalLight` from the upper left. The camera frames the union of the `AABB`s three
quarters from above front at `radius / sin(fov / 2)`, fov 45. A per session list advances
once per frame in `BoxRenderCommandPending`: it frames and requests the capture as soon as
the bounds exist, and deletes the scratch entity, view, camera, light, target and layer
when the file is on disk. A session in a world without views gets one for the active
camera, so the surface keeps rendering. Layers do not isolate yet, an entity in no layer is
drawn by every view, so a live subject still shows its scene. Prefabs with geometry aborted
before this task; the shape observers now set `RenderMesh` through `it->stage`. 11 tests
(`tests.json`), `render.log` with its three images, `bench.json`, six test projects and
four command scenes green, lint 0.
