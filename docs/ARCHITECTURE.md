# Architecture

Box is a library of flecs modules plus the `boxrun` application. Everything an application
configures is a component value; the engine reacts to changes of those values with observers
and systems. `DESIGN.md` is the brief, this page describes what exists. Per task decisions are
in the `project/db/<epic>/<task>/deliverable/README.md` files.

## Rules that shape the code

- Import `Box` and nothing else. `include/box.h` includes every public header and `BoxImport`
  (`src/box.c`) imports the modules in a fixed order, so component ids are the same in every
  world.
- Zero values are literal. A component applies exactly as specified: an all-zero struct or a
  zero field never means "use a default" (zero speed does not move, `enabled = false` is off,
  an all-zero `CullSettings` draws every instance). Defaults live in constructors (`ctor`
  hooks), which run when a component is added without a value, such as script `Foo: {}`. A C
  literal starts from zero, so `ecs_set(e, View, {.camera = c})` renders unlit and unculled.
  Zero may only mean a documented mode that is its literal meaning.
- Components with `char*` or `ecs_vec` members install ctor, dtor, copy and move with
  `box_owned_hooks` (box.types); flecs gives reflected structs a default ctor only.
- Inside OnAdd and OnRemove observers the entity is mid table-move, so structural changes go
  through `it->stage`. `ecs_get_mut` raises no OnSet; call `ecs_modified`. `CLAUDE.md` lists
  the other flecs pitfalls.
- State the engine produces lives in separate components (`AppStatus`, `WorldTransform`,
  `RenderStatus`, `SurfaceStatus`, `ShaderStatus`, `TextureStatus`, `CullStatus`, `PerfStatus`),
  never in the component a user writes.
- Tests assert observable behaviour (pixels, status components, command output), not internals.
- Public headers are `include/box/*.h`; a module keeps private declarations in a header next to
  its sources (`src/render/render.h`, `src/spatial/spatial.h`). Every directory under `test/`
  is a test project.

## Modules

| Module | Sources | Purpose and public components |
|---|---|---|
| `box.types` | `src/types` | `vec2/3/4`, `mat4`, `rgb`, `rgba`; `box_owned_hooks` |
| `box.transform` | `src/transform` | `Position`, `Rotation`, `Scale`, `WorldTransform`, `DynamicTransform`, `ManualTransform`; static and dynamic propagation |
| `box.geometry` | `src/geometry` | 3D shapes (`Box`, `Sphere`, `Cone`, `Torus`, `Mesh3`, ...), 2D shapes (`Rect`, `Circle`, `Line`, `Border`, `Image`); mesh cache |
| `box.camera` | `src/camera` | `Camera`, reversed Z projection |
| `box.surface` | `src/surface` | `Surface`, `SurfaceStatus`, glfw windows, offscreen targets |
| `box.view` | `src/view` | `View` (camera, clear, lighting, shadows, culling, effects), `Scene` |
| `box.effects` | `src/effects` | `Bloom`, `Ssao`, `HeightFog`, `AutoExposure`, `Tonemap` settings |
| `box.material` | `src/material` | `Rgb`, `Opacity`, `Tint`, `PbrMaterial`, `Emissive`, `BlendMode`, `BackFaceMode`, `PbrTextures`, `TextureWrap`, `TextureTransform`, `DynamicMaterial`, `NoShadow` |
| `box.texture` | `src/texture` | `TexturePath`, `Texture`, `TextureStatus`; decoding on the job system |
| `box.light` | `src/light` | `DirectionalLight`, `PointLight`, `SpotLight`, `AmbientLight` |
| `box.movement` | `src/movement` | `Velocity`, `AngularVelocity` (data only, controllers write `Velocity`) |
| `box.layout` | `src/layout` | `Layout`, `LayoutItem`, `LayoutBounds`, `Sizing`, `Padding` |
| `box.text` | `src/text` | `Font`, `FontStatus`, `Text`, `TextBounds`; MSDF atlas |
| `box.input` | `src/input` | `Input`, `Key`, `Mouse`, `InputEvent`, synthetic queue, `Action`, `ActionState`, `Binding` |
| `box.controllers` | `src/controllers` | `CameraController`, `FirstPersonController`, their default actions, `box_controller_move` |
| `box.app` | `src/app` | `App`, `AppStatus`, argument parsing, the run loop |
| `box.jobs` | `src/jobs` | Worker pool (private header `src/jobs/jobs.h`) |
| `box.render` | `src/render` | `RenderSettings`, `RenderStatus`, `CullStatus`, packet ring, render thread, webgpu backend |
| `box.shader` | `src/render` | `Shader`, `ShaderStatus`, `ShaderSettings`, WGSL preprocessor |
| `box.spatial` | `src/spatial` | Per scene BVH index; queries in `src/spatial/spatial.h` (private) |
| `box.projection` | `src/projection` | `ProjectedAnchor`, `ProjectedHidden` |
| `box.command` | `src/command` | `Terminal`, `TerminalStatus`, `TerminalServer`, command registry |
| `box.perf` | `src/perf` | `PerfRun`, `PerfStatus`, `PerfState` |

## Running a world

`apps/boxrun` creates a world, imports `Box`, calls `box_app_parse` and `box_app_run`. The
parser only writes the `App` singleton:

| Option | `App` member | Notes |
|---|---|---|
| `--headless` | `headless` | No window, fixed delta of `1/fps` |
| `--frames <n>` | `frames` | Stop after n frames |
| `--timeout <s\|infinite>` | `timeout` | Wall seconds, the clock starts at the first frame |
| `--cpu-budget <pct>` | `cpu_budget` | Percentage of one core, default 100 |
| `--fps <n>` | `fps` | Target rate, default 60, 0 uncapped |
| `--port <n>` | `port` | Command and REST port, default 27750, 0 disables |
| `<file>` | `scene` | Script loaded before the first frame |

`box_app_run` refuses a headless run without `frames` or `timeout`, a headless run without its
temporary folder, and a headless run on a machine whose load average exceeds its core count
unless `App.unguarded` is set. It loads the
scene as a managed script, starts the `TerminalServer` on `port` and steps the world until it
quits. `BOX_TEST=1` sets `unguarded` and `unpaced` in every new world. The `AppFrame` system
(`EcsPostFrame`) counts frames into `AppStatus.frame`, stops on `frames` or `timeout` and in a
paced headless run sleeps for as long as the process exceeded its CPU budget. Setting
`App.headless` creates `/tmp/box/<pid>` (`AppStatus.tmp_dir`).

The temporary folder is private: `/tmp/box/<pid>` is made with mode 0700 and must be owned by
the user. `/tmp/box` is accepted only when it is a real folder (not a link) owned by the user
or by root and, when others can write it, sticky, so no other user can rename or replace the
folder of a process; otherwise the run refuses to start (a new `/tmp/box` is made 0700, so on
a shared machine either each user removes it or root owns it with mode 1777). A folder of the
user already at `/tmp/box/<pid>` was left by a crashed run with the same pid and is removed
first; a link, a file or a folder of another user there is refused and left alone. A refused
folder leaves `tmp_dir` unset and quits the headless world, and `box_app_run` returns nonzero
before the scene loads. The windowed terminal session root is the same folder. The worlds of
one process share it, and the last one deleted removes it without following links. The browser build (`bake3 --target em apps/boxrun`) renders into `#canvas` with the
page's WebGPU device and has no render thread and no workers.

## Threads

- The main thread owns the world: it runs every system and observer, and no other thread reads
  or writes the world. flecs REST and terminal connections arrive on their own threads but are
  served on the main thread during `ecs_progress`.
- The render thread (`box.render`) is the only thread that calls webgpu. It consumes render
  packets filled by the main thread. With `RenderSettings.main_thread` (the default in the
  browser) the main thread renders the packet at the end of the frame instead.
- Job workers (`box.jobs`) run IO and asset work (image decoding, font loading, glyph
  generation) without world access. `box_job_submit` queues a `box_job_desc_t`: `work` runs
  on a worker, `complete` runs on the main thread in `JobsComplete` (`EcsOnLoad`) in submission
  order, `ctx_free` releases the context. The pool starts on the first job and is sized from
  the core count and `App.cpu_budget` (at most 16). With zero workers, always in the browser,
  work runs inline in `JobsInline` (`EcsPostFrame`). Deleting the world drops queued work.
  Headless frames wait for decodes and glyph jobs in flight so captures are deterministic.

## The render packet

A ring of `RenderSettings.in_flight` packets (default 2, at most 8) carries a frame from the
world to the renderer; their vectors keep their storage, so a steady frame does not allocate.
A packet holds, in apply order: resource changes (create, update, destroy of a buffer, mesh,
texture, target or shader by handle), uploads into buffers, the targets to render and the
views with their batches. Bytes live in one 16 byte aligned data block. The first
`box_render_packet` call of a frame acquires a free packet and waits when all are in flight
(`RenderStatus.stalls`). `box_render_sync` waits until every submitted packet rendered; deleting
the world does the same before `AppStatus.tmp_dir` is removed.

A backend (`box_render_backend_t`) is `init`, `render`, `fini` on the consuming thread and
`free` on the main thread. A new ring has a null backend that records what it received. The
first `Surface` replaces it with the webgpu backend, so a world without surfaces never creates
a device. The backend keeps targets, meshes, textures and shaders by handle.

## Frame phases

Systems run in these phases, in order. Only the ones that matter to the frame are listed.

| Phase | Work |
|---|---|
| `EcsPreFrame` | Texture and font job completion checks, controller action enable/disable |
| `EcsOnLoad` | Surface poll (glfw events, quit on close), `InputUpdate`, `ActionUpdate`, `JobsComplete`, cull stats readback |
| `EcsOnUpdate` | Controllers write `Position`/`Rotation` from `ActionState`; perf camera orbit |
| `EcsPostUpdate` | `LayoutUpdate` relays out trees of marked elements |
| `EcsPreStore` | `TransformStatic` and the dynamic transform systems write `WorldTransform` |
| `EcsOnStore` | `BoxProjectionUpdate`, `SpatialUpdate`, `TextRefresh` (glyph placement) |
| `RenderExtract` | Views, draws, lights, shadows, effects, textures, shaders, text atlas, captures, surface targets |
| `RenderSubmit` | Hands the packet to the consumer |
| `EcsPostFrame` | `AppFrame`, `JobsInline`, perf pacing |

Input first: devices and the synthetic queue become `Input`, then `ActionState`, which is the
only thing controllers read. Layout runs before transforms so a layout written `Position` has a
`WorldTransform` in the same frame. Projection runs after transforms so a nameplate follows its
target in the same frame. `RenderExtract` and `RenderSubmit` are phases that depend on
`EcsOnStore`.

### Transforms

`WorldTransform` is parent * T * R * S. Static entities are updated by observers and the
`TransformStatic` system on change; entities with `DynamicTransform` (inherited by every
descendant) are recomputed every frame. `ManualTransform` means the application writes
`WorldTransform`. Hierarchies use `ChildOf` or the `Parent` component.

## Surfaces and the GPU device

`Surface` has a size in points (default 1280x720), `resolution_scale`, `vsync`, `texture`,
`texture_quality`, `write_to_file` and `view_root`. In `RenderExtract` the surface gets a target
handle and, unless headless (or `texture` or `BOX_TEST`), a glfw window with a `CAMetalLayer`.
Headless targets are `RGBA8UnormSrgb` textures. When the render size (pixels over
`resolution_scale`) differs from the image, views render into a scene texture that a blit scales.
`SurfaceStatus` reports pixel and render sizes. A target renders the views whose parent is its
`view_root`, or every view when that is 0. `write_to_file` writes a PNG every frame (a relative
path lands in `AppStatus.tmp_dir`); colors are display encoded, so a clear color comes back
unchanged.

## Shaders

`Shader {path, defines}` is the shader abstraction. A C style preprocessor
(`src/render/preprocess.c`: `#include`, `#define`, `#ifdef`, `#pragma once`) runs in
`RenderExtract`, reloads on mtime or size change (`ShaderSettings.poll_interval`) and the backend
compiles in an error scope, keeping the old module on failure. Compile errors come back into
`ShaderStatus` as `file:line: message`, mapped through a per line source table. Engine shaders
live in `etc/shaders` (blit, mesh, shape2d, scatter, cull, cluster, shadow, bloom, ssao, fog,
tonemap, exposure) with shared includes. `tools/wgsl_check.mjs` validates them with Tint.

## Scenes

A `Scene` is an isolated space. An entity belongs to itself when it has `Scene`, else to its
nearest ancestor with `Scene` (through `ChildOf` or `Parent`), else to the default scene (0).
Nested scenes are separate: an entity of an inner scene is not part of the outer one.

Membership comes from queries with a `Scene(self|up)` term, never from a walk up the hierarchy:
`src/render/scene.c` keeps one cached query that `box_render_scene_of` asks for a single entity,
and the light queries carry the same term. The renderer partitions by scene: batches are keyed
by scene, and a view draws only the batches of the scene of its camera. Lights, ambient light,
light clusters and the spatial index are per scene, so entities of different scenes never
interact even when they overlap. Adding or removing `Scene` or reparenting queues the subtree in
the draw, light and spatial observers, which move it in the next frame. The only cross scene
reference is `ProjectedAnchor`: it projects the world position of a target through a view's
camera into pixel `Position` of a UI scene (origin at center, y up) in the same frame. Behind
the camera, past the far plane or with a missing target the entity gets `ProjectedHidden` and
collapses to a point.

## Draws and instancing

`RenderExtractDraws` (`src/render/draw.c`) resolves 3D shapes to a shared unit mesh (keyed by
topology and quantized proportions) plus an instance scale in the private `ShapeMesh`; radius
and size are never baked. A mesh is sent once per backend as a resource create. Shapes live in
persistent batches keyed by scene, mesh, pipeline (shader, blend, back face, textures) and
whether they are dynamic; removed slots are filled with the last one. An instance is 128 bytes
(model matrix with scale, normal data, color, emissive, metallic/roughness, UV transform).

Observers on shape, material, transform, hierarchy and scene components queue changed entities,
so an unchanged frame visits and uploads nothing (`RenderStatus.upload_bytes`). Static batches
upload changes into a change buffer that `scatter.wgsl` scatters into their slots; entities with
`DynamicTransform` or `DynamicMaterial` fill dynamic batches every frame from cached queries. A
batch is one instanced indirect draw and is limited to 1M instances.

Materials: `Rgb`, `PbrMaterial`, `Emissive`, `Opacity` and `Tint` resolve on the CPU, composed
root down, into the instance. `BlendMode`, `BackFaceMode` and alpha below 1 key the batch.
`mesh.wgsl` is GGX, height correlated Smith, Schlick and a Karis environment BRDF. Textures
(`PbrTextures`: albedo, emissive, roughness, normal) are one bind group per batch, so batches
are keyed by texture entities; normal maps use a cotangent frame (no tangents).

## Renderer passes

For each target the backend applies resource changes and uploads, runs the scatter pass, then
for each view in packet order (creation order, only views whose camera has a `Camera`):

1. Effects begin: a view with enabled effects renders into an `rgba16float` target.
2. Light clustering (`cluster.wgsl`, only with point or spot lights).
3. Culling (`gpu_cull.c`, `cull.wgsl`): frustum, minimum size, and Hi-Z occlusion, below.
4. Shadow cascades (`gpu_shadow.c`), only with `View.shadows.enabled`.
5. Forward pass: opaque 3D meshes, then 2D shapes and text, then blended draws, indirectly
   drawn from the compacted visible slots. Depth is `Depth32Float`, reversed Z, cleared to 0
   per view. A view that clears replaces the color, any other view draws over what is there.
6. With occlusion: late cull phase and a second forward pass for what became visible.
7. The effects chain, the last effect writing the sRGB target.

After the views the target is blitted if its render size differs, presented, and for a capture
copied to a readback buffer and written as PNG on the render thread.

**Culling.** A compute pass tests each draw's instances (mesh bounds moved by the model matrix)
against the clip planes and `min_dimension_px` and writes visible slots plus indirect draw args.
`CullSettings {enabled, occlusion, min_dimension_px}`: occlusion is two phase Hi-Z. The early
phase draws what the view drew last frame (a history per instance slot, keyed by view and
target), a depth pyramid keeps the farthest depth, the late phase tests every instance against
at most 4x4 texels and draws the newly visible, so a fast camera never misses an instance.
Blended draws keep a CPU back to front sort and are culled in slot order. `CullStatus` on each
View reports instances, visible, frustum culled and occlusion culled, read back asynchronously.

**Shadows.** Cascaded maps for the first directional light of the view's scene.
`RenderExtractShadows` splits near..min(`max_distance`, far) by `split_lambda`; each cascade is
fitted to the bounding sphere of its slice with the center snapped to whole texels (no shimmer),
rendered into a reversed Z depth array with `shadow.wgsl`. Cascades cull casters by frustum
only, so off-screen casters still shadow the view. `mesh.wgsl` uses bias and normal offset in
texels, a `filter_size` PCF and a fade over the last cascade. `NoShadow` and blended shapes cast
none. Shadows are off unless `View.shadows.enabled`.

**Effects.** `View.effects` is an ordered list of `ViewEffect {enabled, effect}` that point at
entities with `Tonemap` (Khronos PBR Neutral), `AutoExposure` (log luminance histogram, eye
adaptation), `Bloom` (mip chain, threshold, optional temporal), `Ssao` (half resolution, normals
from depth) or `HeightFog` (lit by the scene sun). Each is an on-demand engine shader; disabled
entries add no pass or target, and unused state is freed the next frame.

**2D.** `Rect`, `Circle`, `Line`, `Border` and `Image` resolve to one unit quad and a private
`Shape2D`; `shape2d.wgsl` shades analytic signed distances anti-aliased over the pixel footprint,
so edges stay crisp at any scale. They are unlit, instanced, GPU culled, blended, tinted and
dynamic like 3D shapes, draw after opaque 3D and use GreaterEqual depth, so coplanar shapes draw
in batch order.

## Lights

`DirectionalLight`, `PointLight`, `SpotLight` and `AmbientLight` take color from `Rgb`, position
from `WorldTransform` and direction from the local +Z axis. Constructors give intensity 1, range
10 and a 20/30 degree cone. `RenderExtractLights` collects the lights of each scene into a
storage buffer per scene, directional first, uploaded only when its bytes change. Every view
gets the buffer, counts and ambient of the scene of its camera. `AmbientLight` (sky, ground,
intensity) on a scene root, any entity of the scene or as a singleton for the default scene is
summed per scene. A scene without light entities gets a default sun and sky, so a scene whose
light has intensity 0 is dark. `View.lighting.enabled` false draws base color and emission only.
`cluster.wgsl` splits the view frustum into 16x9x24 clusters and lists up to 128 point or spot
lights per cluster; `mesh.wgsl` shades every directional light plus the cluster's lights.

**Bake settings.** `LightBake` on a `Scene` root (or a singleton for the default scene) holds
the baked lighting settings: `probe_spacing` 2, `bounces` 2, `capture_size` 16, `budget_ms` 2,
`bounds_padding` 1 and `cache` (`".box/bake"`, owned string, empty for none), all set by the
constructor and taken literally otherwise. Adding it makes the engine add `LightBakeStatus`
(`probes`, `baked`, `pass`, `converged`, `cache_hit`), removed with it. `NoBake` keeps a static
shape out of the captures. These are data only for now: nothing samples a volume yet.

## Spatial index

`box.spatial` keeps, per scene, an index over world AABBs of shapes (unit mesh bounds times
`ShapeMesh` scale, placed by `WorldTransform`): a static BVH (binned SAH build, SAH insert with
AVL rotations) and a dynamic BVH with fat leaves for `DynamicTransform` shapes. Observers queue
changed shapes and subtree roots; `SpatialUpdate` (`EcsOnStore`) places only those. Dynamic
shapes are marked stale and placed by the first query after a frame, so an idle frame pays
nothing. Queries (`src/spatial/spatial.h`) are scoped to one scene: nearest ray with a narrow
phase filter, all hits sorted, box and sphere overlap, entity bounds. `box_controller_move`
uses them for collision. Bounds are boxes; there is no mesh level narrow phase and no picking.

## Text, layout and UI

**Text.** `Text {value, size, line_height, width, align, font}` and `Font {path}`
(`box.text.DefaultFont` is B612 in `etc/fonts`). Fonts load with stb_truetype on the job system.
Glyph MSDFs are generated lazily per glyph on workers into RGBA8 atlas pages that grow and then
page. Layout handles UTF-8, kerning, newlines, word breaks and alignment; the block is centered
on the origin and measured into `TextBounds`. Glyphs are slots of the 2D batches keyed by atlas
page (`src/render/text_draw.c`), shaded by `shape2d.wgsl`. No shaping (ligatures, RTL).

**Layout.** `Layout` on a parent arranges its children in `OrderedChildren` order in logical
pixels: rows and columns, fit and grow sizing with weights, min/max, padding, spacing, alignment,
and `DirectionNone` anchoring with x/y offsets. `LayoutItem` sets per child sizing. Observers
mark elements; `LayoutUpdate` (`EcsPostUpdate`) relays out the trees of marked elements and writes
`Position` x/y, `LayoutBounds` and `Rect` only where they changed. A reorder through
`ecs_set_child_order` needs `ecs_modified(parent, Layout)`.

**UI scenes.** A UI is a `Scene` with an orthographic `Camera` and a `View` that does not clear,
drawn over the 3D view of the same surface. `ProjectedAnchor` places UI over 3D objects.

## Cameras and views

`box_camera_projection` builds a reversed Z projection per target size (depth 1 at near, 0 at
far). A perspective camera without a far plane is infinite; orthographic shows `ortho_height`
units or the surface height divided by `ortho_scale`. The aspect follows the surface unless set.
A `View` renders the camera's scene into the targets that include it; its constructor enables
lighting, `lighting.baked` (use the bake of the scene when there is one; a C literal opts out),
culling with occlusion, 1 px minimum size and the shadow defaults.

## Input and controllers

`box.input` has two sources: a platform hook (`box_input_platform_push`, glfw and browser
backends not implemented) and a synthetic queue applied in order with per event frame delays.
`InputUpdate` writes the `Input` singleton and emits script events at `box.input.screen`.
`Action` entities get an `ActionState` (down, pressed, released, start, delta, drag) written by
`ActionUpdate`; `Binding` entities (key, modifiers) map keys to actions, and script events
`on.pressed`, `on.held`, `on.dragged`, `on.released` fire for them. Real input is ignored while
unfocused. Picking is not implemented.

`CameraController` and `FirstPersonController` read only `ActionState`, never devices. Default
actions live under `box.controllers.camera` and `box.controllers.first_person` and stay disabled
while no controller uses them. Remap by editing a `Binding` or pointing `actions` at another
scope. `box_controller_move` moves by velocity times delta and, for `FirstPersonController` and
with `collide` for `CameraController`, collides a capsule against shapes found by spatial
overlap queries (exact boxes, balls, cylinders, capsules, otherwise oriented bounds), with
sliding, step-up, ground probe and headroom checks.

## Command interface

`box.command` serves the agent interface on `App.port`. `TerminalServer` handles
`/terminal/new`, `/terminal/close`, `/terminal/exec?session=&cmd=` and
`/resource/<path>` (confined to the session's `resources` folder); other paths go to flecs REST.
A `Terminal` session has a cwd; every line goes to the session log and `commands.log`. Commands
are script functions registered with `box_command_register` (`src/command`):

- Shell and components: `cd`, `ls`, `touch`, `rm`, `mv`, `man`, `add`, `remove`, `set`, `get`,
  `clear`, guarded against flecs aborts. flecs entities are read only. `rm`, `mv`, `add`,
  `remove` and `clear` refuse modules, sessions (any `Terminal` entity) and everything in their
  scope, so one session cannot close or rewire another or remove `TerminalHost`; `set` refuses
  sessions and, inside a module, only changes components the entity already has (singletons
  such as `TerminalLimits`). Without a `TerminalHost` commands fail with "box.command is not
  imported", `box_terminal_new` returns 0 and requests get HTTP 503.
- Search: `query`, `iter`, `next`, `done`, `has`, `grep`, `grep_recur`. Iteration re-runs the
  query from an offset, so no iterator stays open.
- Scripts: `script_read`, `script_write`, `script_patch`, `script_grep`, limited to the working
  directory; a write parses first and restores the old code when evaluation fails.
- Input: `key.*`, `mouse.*`, `input.*`, `action.*` feed the synthetic queue.
- Render: `capture [cameras]` and `render <entity|prefab|template>`.
- `commands` and `logs` list history and are quiet.

The HTTP server has no deferred reply, so commands that wait (`input.wait`, `capture`, `render`)
answer at once with a pending state and the caller polls.

**Limits.** Numeric arguments are range checked: an i32 outside the i32 range, a non finite
f64, walk `steps` above 1000, `dur` above 36000 frames and `next` counts above 1000 fail with
"is out of range, min to max" (a rejected `next` keeps the iteration). The `TerminalLimits`
singleton holds the session bounds, with defaults from its constructor. `sessions` (16) caps
the sessions `/terminal/new` keeps open (HTTP 400 "too many sessions") and `idle_timeout`
(1800 s) closes such a session when it ran no command for that long (`TerminalIdle`, every
frame); both are read live and do not apply to the `Terminal` entities of a scene. `history`
(1000 lines, the `commands` output; `TerminalStatus.commands` stays the total), `log_size`
(1 MiB, then `commands.log` moves to `commands.log.1`, logrotate style) and `images` (100) are
read when a session opens and do not change open sessions. An image of `capture` or `render`
counts against `images` from the moment it is named until it is deleted: naming one more
deletes the oldest image the renderer is done with (written, or dropped once
`RenderStatus.rendered` passed its packet), and when every kept image is still being written
the command fails, so a burst of captures cannot leave files behind.

**Captures.** `box_render_capture` queues a camera (or explicit settings); `RenderExtractCaptures`
creates an offscreen target for one packet, an exclusive view assigned to it and a PNG path, and
destroys the target in the next packet. An exclusive target renders only its views and other
targets skip assigned views, so a capture never shows on a surface. A capture of a camera that a
view renders uses that view's lighting, shadows, effects and culling; `render` puts the subject
in a private `Scene`, fits a 45 degree camera to its bounds, captures 512x512 plain lit and
deletes the scene the next frame.

## Testing and performance tooling

- Tests: `bake3 run test/<project> --local-env -- -j 12`, projects `command`, `controller`,
  `core`, `geometry`, `input`, `perf`, `render`, `scene`, `transform`, `ui`. `BOX_TEST=1`
  disables the CPU budget and load guard. `test/transform` was written by the user.
- Visual regression: `tools/integration.py visual` renders every `test/integration/visual/*.flecs`
  headless at 640x360 for 3 frames and compares it with `baseline/<scene>.png` (largest channel
  delta and number of differing pixels), writing an HTML report; `--update-baseline` refreshes.
- Performance scenes: `box.perf` `PerfRun` (fps 30/60/90/120) paces frames to the target
  interval, orbits a camera around the bounds of every shape and counts intervals outside 10%
  as errors; `PerfStatus` and a JSON report carry the numbers. `tools/integration.py perf` runs
  `test/integration/perf`. `test/perf` uses a fake clock so it is machine independent.
- Benchmarks: `bench/{core,controller,render,scene,transform,ui}` and `tools/bench.py` report
  median, confidence interval and percentiles; `--baseline` flags changes beyond 5%.
- Epic pipeline: `tools/epic.py <epic> --env <name>` runs build, tests, visual, perf, bench,
  shaders and wasm into `project/db/<epic>/deliverable`.
- Style: `tools/lint.py` enforces `design/coding_style.c`; `project/pm.py` is the task tool.

## From a component change to a reconfiguration

Public components are plain data with reflection, so a script, the REST API and C code change
them the same way. A module reacts with an `OnSet` observer when the change must take effect at
once (`App.headless`, `App.fps`), with a system that reads the component every frame when it
steers per frame work (`AppFrame`, transform systems), or with `OnAdd`/`OnRemove` observers
when presence matters.
