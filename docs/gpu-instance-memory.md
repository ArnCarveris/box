# GPU instance memory

This report looks at the memory that each instance of a shape costs on the GPU: the instance
page buffers it occupies and the bytes the extract uploads for it. It measures each stream,
evaluates options for the surface, transform and shading streams, and ends with a ranked list of
changes. It is an investigation of the GpuInstanceData epic; no engine code, shader or test was
changed for it.

Sizes are in bytes, and MB is 10^6 bytes.

## Summary

- The surface stream is 384 of the 448 bytes of a mesh slot and 384 of the 512 bytes of a 2D
  slot. In every bench scene it holds only zero records. Nothing on the GPU reads the surface
  stream of a 2D page, and a mesh page reads it only for parametric surfaces. These were 1.9% of
  the instances of the spaceship scene and 0% of every bench scene. It still takes 75% to 86% of
  the allocated page memory. It also takes 75% to 86% of every full upload: new shapes, batches
  that move to a new run, grown pages and text relayout.
- In steady state the surface stream costs nothing, because a slot uploads it only when its
  `ShapeMesh` changes or the slot is written again. Steady traffic is the transforms of
  `DynamicTransform` shapes, the shading of `DynamicMaterial` shapes and their material records.
- Runs and pages are allocated in powers of two, so a page holds 1.2 to 3.7 times as many slots as
  there are live instances. In the shipped scenes the 64 slot minimum run of small batches adds
  more.
- A shape that owned a material component got an 80 byte record of its own in the material
  table. In the Extract bench the table held 100,001 records whose contents took only 2 distinct
  values. The table now shares records by content (O1, implemented) and holds 2.
- The transform stream cannot shrink without loss. 23% of the instances of the Japanese garden
  scene have a sheared model matrix, which a TRS encoding cannot hold.

The recommended order: drop the surface stream from 2D pages, then from mesh batches without a
parametric surface, deduplicate the material table by content, and copy grown pages on the GPU.
The first two take a mesh slot from 448 to 64 bytes and a 2D slot from 512 to 128 bytes.

## How the numbers were obtained

**Measured.** A driver outside the repository linked the unchanged objects of the
`bench/render` suites Extract, Shapes2D, Text, Material, Transition and Texture against
`libbox.a`. It replaced `bench_iter` with four measured frames after each bench's own setup. It
wrapped `box_render_set_backend` (`-Wl,--wrap`) so that every packet passed through a recorder
before it reached the bench's backend. The recorder summed the following per store and stream:

- the `BOX_RENDER_BUFFER` creates and destroys of the page handles (allocated page buffers);
- the uploads into page handles (full run uploads);
- the scatter segments (`box_render_scatter_t.size`) and the change buffer upload.

After the setup frames and after the measured frames the driver read
`box_render_draws_stats_t` (`transform_bytes`, `shading_bytes`, `surface_bytes`, `upload_bytes`,
`material_bytes`) and `RenderStatus.upload_bytes`. From the CPU state (`box_draws_state`) it
read the batch runs, the page holes, the surface vectors that are nonzero, the orthogonality of
the transforms and the distinct shading and material records. The counters agreed with the
packet totals in every case. All figures are byte counts. They do not depend on timing or on the
debug build that was used (arm64 Linux).

**Rebuilt scenes.** The job has no GPU, so the Cull and LargeBatch benches cannot run as they are:
they open a headless surface and read GPU buffers back. Their scenes were rebuilt in the driver
with the same entities and a backend that drops packets. These are Cull's 100k box scene,
`many_batches_shadows` and `town` (`etc/scenes/town.flecs`). The extract does not depend on the
backend, so the instance traffic is the same. Five shipped scenes that are not benches were run
the same way: `spaceship`, `santorini`, `japanese_garden`, `cheese_market` and `ui_showcase`.
No bench scene contains a parametric surface, so these were needed. The spawn scene, 1,000 boxes
added in each of 64 frames, is not a bench either.

**Derived from the code.** `LargeBatch.instances_1m` and `instances_1m5`, and the 1M shape scene
of `bench/scene` (`Spatial.build_1m`), exceeded the 2 GiB memory limit of the job. Their figures
below are derived from the allocator in `store.c` and `batch.c` and labeled as derived. The
`bench/scene` suites set no render backend and no view, so as they run they upload no instance
data at all. Their figure is for the same 1M boxes with a view added.

**Not measured.** GPU time is not measured: render, cull and scatter passes, and the cost of a
vertex shader loading the surface record. Every statement about shader cost below is derived
from the shader code.

## Where the bytes go

### The slot layout, from the code

| Stream | Mesh page | 2D page | Read on the GPU by |
|---|---|---|---|
| Transform | 48, `box_render_transform_t` | 48 | `cull.wgsl`, `mesh.wgsl`, `shadow.wgsl`, `bake_sh.wgsl`, `shape2d.wgsl` |
| Shading | 16, `box_render_mesh_shading_t` | 64, `box_render_shading_t` | `mesh.wgsl`, `shadow.wgsl` (cutoff), `shape2d.wgsl` |
| Surface | 384, `box_render_surface_t` | 384 | `mesh.wgsl` and `shadow.wgsl` through `place()`; not `shape2d.wgsl`, not `cull.wgsl` |
| Total | 448 | 512 | |

`store.c` creates one buffer per stream for every page, sized `capacity * stride`, for both
stores (`draw.c`, `box_render_draws_init`). `box_draws_slot_add` appends a zeroed surface for
every slot, also for 2D slots and glyphs. A new slot is marked with `BOX_DRAW_ALL`, so every
stream uploads. The same happens when a slot is written again: a key change, a shape moved
into a slot by a swap remove, a run moved to a new place, a grown page or a text relayout.
`shape2d.wgsl` declares only bindings 0 and 2 of its instance group. It still uses
`gpu->mesh_layout`, so a 2D page must have a buffer at binding 3.

The surface words, from `src/geometry` and the shaders:

| Vectors | Content | Read by |
|---|---|---|
| `p[0]`–`p[8]` | Kind, parameters, prism footprint, offsets into the surface assets | `surface.wgsl` evaluation, `mesh.wgsl` and `shadow.wgsl` branches |
| `p[9]`–`p[18]` | Never written | Nothing |
| `p[19].x` | The shape has a render mesh (`ShapeMesh.render_mesh`) | `mesh.wgsl`, `shadow.wgsl` |
| `p[20]`, `p[21]` | `SurfaceMaterials` color and metallic, roughness per region | `mesh.wgsl` |
| `p[22]`, `p[23]` | Center and half extent of the local bounds | CPU only: `box_draws_bounds`, `box_draws_shadow_region` |

`box_mesh_cache_resolve` resolves surface kinds 1 to 6 on the CPU into a render mesh. The batch
is then keyed by that mesh (`draw.c`, `box_draws_key`), and `place()` takes the `p[19]` branch.
In that case the GPU reads only `p[0].x`, `p[19].x`, `p[20]` and `p[21]`. The vertex shader
evaluates the surface only for kind 7 (`ProfileExtrusion`, which reads `p[1].y`) and when
resolution fails. The measurements agree: the spaceship scene has 166 parametric instances, all
with a render mesh, and the only nonzero vectors are 0 to 8, 19, 22 and 23. The geometry
primitives scene has one instance of each of the 7 kinds; 6 have a render mesh.

### Allocated page buffers, measured

Bytes of the page buffers alive after setup. Slots are the slots of the page buffers. Instances
are the live slots.
The Extract, Shapes2D, Text, Material and Cull suites of `bench/render` report these as the
`page_slots`, `page_transform_bytes`, `page_shading_bytes`, `page_surface_bytes` and `page_bytes`
counters of `box_render_draws_stats_t` (one helper, `bench_render_memory` in
`bench/render/src/bench_memory.c`). Material reports them in every case, Cull after the measured
frames of every case; the Cull suite needs a GPU. The counters cover both stores, so `Cull.town`
reports its mesh and 2D pages together.

The mesh rows are reproduced exactly: Extract 262,144 slots and 117.4 MB, Material.tint_subtree
and the Cull 100k scene (every case but `many_batches_shadows` and `town`) 131,072 slots and
58.7 MB, `Cull.many_batches_shadows` 32,768 slots and 14.7 MB. The 2D rows were measured before
S1 and D1. The slot counts still match, but a 2D slot is now 112 bytes: 48 transform, 64 shading
and no surface. The benches report Shapes2D.dynamic_material 29.4 MB (12.6 transform, 16.8
shading), Shapes2D.ui_tree_idle 1.8 MB, Text.layout_10k 14.7 MB (6.3 transform, 8.4 shading)
and `Cull.town` 8,448 slots and 3.70 MB, of which 0.03 MB are the 2D page.

| Scene | Instances | Slots | Transform | Shading | Surface | Total | Surface share |
|---|---|---|---|---|---|---|---|
| Extract (100k `Box` + 10k dynamic `Sphere`) | 110,000 | 262,144 | 12.6 MB | 4.2 MB | 100.7 MB | 117.4 MB | 86% |
| Material.tint_subtree | 110,000 | 131,072 | 6.3 MB | 2.1 MB | 50.3 MB | 58.7 MB | 86% |
| Cull 100k scene | 100,001 | 131,072 | 6.3 MB | 2.1 MB | 50.3 MB | 58.7 MB | 86% |
| Cull.many_batches_shadows (400 batches) | 20,000 | 32,768 | 1.6 MB | 0.5 MB | 12.6 MB | 14.7 MB | 86% |
| Cull.town, mesh | 4,801 | 8,192 | 0.39 MB | 0.13 MB | 3.15 MB | 3.67 MB | 86% |
| Cull.town, 2D | 120 | 256 | 0.01 MB | 0.02 MB | 0.10 MB | 0.13 MB | 75% |
| Shapes2D.dynamic_material | 110,000 | 262,144 | 12.6 MB | 21.0 MB | 100.7 MB | 134.2 MB | 75% |
| Shapes2D.ui_tree_idle | 10,100 | 16,384 | 0.8 MB | 1.3 MB | 6.3 MB | 8.4 MB | 75% |
| Text.layout_10k (glyphs) | 110,000 | 131,072 | 6.3 MB | 10.5 MB | 50.3 MB | 67.1 MB | 75% |
| spaceship (scene) | 8,762 | 32,768 | 1.6 MB | 0.5 MB | 12.6 MB | 14.7 MB | 86% |
| japanese_garden (scene) | 21,683 | 65,536 | 3.1 MB | 1.0 MB | 25.2 MB | 29.4 MB | 86% |
| Spawn, 64 × 1,000 boxes | 64,000 | 131,072 | 6.3 MB | 2.1 MB | 50.3 MB | 58.7 MB | 86% |
| LargeBatch 1M, **derived** | 1,048,576 | 1,048,639 | 50.3 MB | 16.8 MB | 402.7 MB | 469.8 MB | 86% |
| LargeBatch 1.5M, **derived** | 1,572,864 | 1,660,244 | 79.7 MB | 26.6 MB | 637.5 MB | 743.8 MB | 86% |
| bench/scene 1M boxes with a view, **derived** | 1,000,000 | 1,048,575 | 50.3 MB | 16.8 MB | 402.7 MB | 469.8 MB | 86% |

Derivation for the large scenes: a batch holds at most `BOX_DRAWS_CHUNK` = 349,525 instances
(128 MiB / 384), so 1M boxes of one key take three full pages. A full run's capacity is capped
at the chunk, and so are its page buffers. The last batch of 1M boxes holds 1 instance in a 64
slot run on a fourth page. The fifth batch of 1.5M holds 174,764 instances in a 262,144 slot run.

### Uploads, measured

| Scene | What a frame uploads | Surface share |
|---|---|---|
| Extract, setup | 49.3 MB: transform 5.3, shading 1.8, surface 42.2; plus 8.0 MB of material records, 160 bytes after O1 | 86% of the stream bytes |
| Extract.unchanged | 480,000 per frame: the transforms of 10,000 `DynamicTransform` spheres | 0 |
| Extract.changed_1pct | 532,224 per frame: 480,000 plus a transform scatter of 1,000 entries (52 each) | 0 |
| Shapes2D.dynamic_material | 640,000 per frame: shading of 10,000 shapes | 0 |
| Shapes2D.changed_1pct | 68,096 per frame: shading scatter of 1,000 entries (68 each) | 0 |
| Shapes2D.ui_tree_add_remove | 1,024 per frame for 1 added and 1 removed shape: segments of 68, 184 and 404 bytes (header and entries), each padded to 256 | 404 of 656 segment bytes |
| Text.layout_10k | 51.2 MB per frame: transform 4.8, shading 8.0, surface 38.4 | 75% (74% over four frames) |
| Text.changed_1pct | 92,000 per frame on average: transform and shading scatters of 675 glyphs | 0 |
| Material.dynamic_material | 960,000 per frame: shading 160,000 and material records 800,000 | 0 |
| Material.tint_subtree | 200,192 per frame: shading scatter of 10,000 entries (20 each) | 0 |
| Transition.active_1000 | 2.97 MB over four frames; its batch key changes, so a run upload and a scatter of surfaces | 26% |
| Spawn, 64 × 1,000 boxes | 57.6 MB in total for 28.7 MB of records: transform 6.3, shading 2.3, surface 49.0 | 85% |

The spawn scene uploads twice what its instances hold. When its batch outgrows its run, the
batch moves to a new run and uploads every instance again (`box_draws_reserve`). When a page
buffer grows, every batch of the page uploads every stream again (`box_draws_pages`). The old run
stays behind as a hole: the page ends at 131,072 slots with a hole of 64,512 slots.

## Surface stream

The options, with savings per slot and worked totals for measured scenes. They are listed in the
order they are recommended.

### S1. No surface stream in 2D pages

The 2D store gets a layout with two streams (`box_draw_layout_t.streams = 2`, or a zero stride),
and 2D batches stop keeping `b->surfaces`. The backend binds a shared one record zero buffer at
binding 3 for 2D pages, as it binds `gpu->no_assets` at binding 1.

- Saves 384 bytes per 2D slot, allocated and uploaded: 512 → 128.
  - Text.layout_10k allocates 67.1 → 16.8 MB, and its relayout uploads 51.2 → 12.8 MB per frame.
  - Shapes2D.dynamic_material allocates 134.2 → 33.6 MB.
  - Town's 2D page allocates 131 KB → 33 KB.
  - An added UI element scatters 136 entry bytes instead of 524.
- Render, cull and scatter: no shader work moves; `shape2d.wgsl` never read the stream. There is
  one scatter dispatch less per 2D page in a frame that writes surfaces, and 384 bytes less of
  `memcpy` per write on the main thread.
- Touches `draw.c` (2D layout), `batch.c` (`box_draws_slot_add`, `box_draws_stream`,
  `box_draws_slot_changed`, `box_draws_slot_remove`, the `BOX_DRAW_ALL` masks of 2D batches),
  `text_draw.c`, `store.c` (the loops over `layout.streams` already work) and `gpu_draw.c`
  (`box_gpu_instance_group` requires all three stream buffers). The test
  `test/cpu/render/src/InstanceStreams.c` expects surface bytes for every new instance and must
  be updated.
- Risk: none on WebGPU limits; the binding count stays the same. The zero buffer must hold one
  full record, because `place()` is not run for 2D draws but the layout keeps the binding.
- Effort: small.

### S2. A mesh layout without a surface stream for batches without a parametric surface

A batch is keyed by its mesh, and the mesh decides whether its instances have a surface. A
render mesh exists only for a surface, and the grid mesh of a kind 7 surface is keyed by its
profile. So a bit `surface` in `box_draw_key_t`, set from `shape->surface.p[0][0] != 0`, can
send the batch to one of two mesh stores: plain (transform and shading) or surface (all three
streams). Plain pages bind the zero buffer at binding 3. `place()` in `mesh.wgsl` and the caster
position in `shadow.wgsl` load the record only when the slot is inside the buffer, for example
with `if (v.slot < arrayLength(&surfaces))`. Otherwise `surface_data` stays zero, the plain mesh
path.

- Saves 384 bytes per plain mesh slot, allocated and uploaded: 448 → 64.
  - Extract allocates 117.4 → 16.8 MB, and its setup upload is 49.3 → 7.0 MB.
  - The Cull 100k scene allocates 58.7 → 8.4 MB.
  - Town allocates 3.67 → 0.52 MB.
  - The spaceship scene keeps its 166 surface instances on 384 byte slots; its 8,596 plain
    instances save 3.3 MB of upload.
  - Spawn uploads 57.6 → 8.6 MB.
  - LargeBatch 1M allocates 469.8 → 67.1 MB (derived).
- Render: the vertex shaders of plain meshes stop copying 24 `vec4` into `surface_data` for
  every vertex. `surface_data` is a private variable that `sf()` indexes dynamically, so the
  compilers are likely to keep the full copy today. The new check costs one `arrayLength` and a
  compare. This could not be timed without a GPU. Cull is unchanged, since it reads only
  transforms. Scatter loses the surface segments of mesh pages.
- Slots per page: the chunk was derived from the surface stride (`BOX_DRAWS_CHUNK`), so plain
  batches still split at 349,525 instances. Done in the StoreChunkSize follow-up: each store
  derives its chunk from its own largest stride (`box_draws_layout_chunk`), 128 MiB / 48 =
  2,796,202 slots for plain meshes, 128 MiB / 64 = 2,097,152 for 2D and 349,525 for surfaces,
  so the 1M box scenes are one batch, one page and one cull job range.
- Touches `draw.h` (key, stores), `draw.c` (`box_draws_key`, store setup), `batch.c` (store
  choice, the `surfaces` vector only for surface batches), `gpu_draw.c` (the bind group),
  `mesh.wgsl`, `shadow.wgsl` and `include/instance.wgsl` (docs), and the `InstanceStreams` tests.
  `box_draws_bounds` and `box_draws_shadow_region` read `p[22]` and `p[23]` from the CPU copy.
  For a plain batch that copy is zero, which is the branch they already take.
- Risk: a shape whose `ShapeMesh` gains or loses a surface now changes key. This already happens
  through its mesh handle. It relies on reading inside a one record buffer, with no reliance on
  robust access. No new binding.
- Effort: medium.

### S3. A table of deduplicated surface records referenced by a 32 bit id

This is like the material table: the surface stream becomes a 4 byte id stream, and the records
live once in a table bound at binding 3.

- Saves 380 bytes per slot over today. After S2 it saves 380 bytes per surface slot only, and
  adds 384 bytes per distinct record.
  - Spaceship: 166 surface instances hold 71 distinct records. Their surface traffic goes from
    63.7 KB to 27.3 KB of table and 664 bytes of ids.
  - The surface pages themselves are larger than that because of run minimums; see O2.
- Render: one more dependent load per vertex (`surfaces[ids[slot]]`) for surface batches. The
  table needs reference counts on the main thread, like `materials.c`.
- Touches the same files as S2, plus a table module.
- Risk: a 128 MiB table binding holds 349,525 records, plenty. No new binding if it replaces
  binding 3.
- Effort: medium to large.
- Recommended only after S2, if scenes with many parametric instances appear. No bench has any.

### S4. A compact encoding per surface kind

The GPU reads at most vectors 0 to 8 and 19 to 21, and 19 only in `.x`. Vectors 22 and 23 are
CPU only and 9 to 18 are never written. A packed record of 11 or 12 `vec4` (176 or 192 bytes)
keeps every kind. A render mesh instance needs only the kind, the flag and the region materials
(48 bytes), if the GPU fallback for a failed resolution is dropped.

- Saves 192 to 336 bytes per surface slot. That is nothing for plain slots after S2.
- Render: `sf()` and the `p[19]`, `p[20]`, `p[21]` reads of `mesh.wgsl` and `shadow.wgsl` must
  remap indices. The asset offsets in `p[4]`, `p[5]`, `p[7]` and `p[8]` stay, because they
  address `surface_assets` from 24 on. `box_surface_data_t` keeps its CPU layout and the extract
  packs it.
- Risk: the CPU evaluator (`surface_math.c`) and the shader must agree on two layouts. A
  mistake shows only on parametric shapes, which no bench covers.
- Effort: medium.
- Rejected for now: after S2 it saves only on the rare surface slots, and S3 saves more on them.

### S5. Allocating the surface buffer of a shared page only when a run holds a surface

Rejected. Pages are shared by many batches, so one parametric instance makes the whole page pay
the 384 byte stride: up to 128 MiB for a full page. A store per layout (S2) has no such cliff.

## Transform stream

The 48 bytes are three `vec4` rows of the model matrix with the instance scale folded in
(`box_draws_transform`). `mesh.wgsl` derives the normal from the cofactor matrix of the columns,
which is correct for non uniform scale and shear. The transform is the hot stream: it is what
moved and dynamic shapes upload every frame, 480,000 of the 532,224 bytes in a frame of
Extract.changed_1pct.

What the measured transforms contain:

| Scene | Instances | Orthogonal columns | Of those, uniform scale | Sheared | Largest translation |
|---|---|---|---|---|---|
| Extract | 110,000 | 110,000 | 110,000 | 0 | 333 |
| Cull.town | 4,921 | 4,921 | 489 | 0 | 610 |
| japanese_garden | 21,683 | 16,612 | 4,398 | 5,071 | 15.7 |
| cheese_market | 19,419 | 19,377 | 4,763 | 42 | 13.8 |
| santorini | 9,514 | 9,482 | 2,316 | 32 | 17.6 |
| Text.layout_10k | 110,000 | 110,000 | 0 | 0 | 11,914 |

The options:

- **T1. Position, quaternion and scale (TRS).** Rejected. 40 bytes with f32 (saves 8), or 32
  bytes with a snorm16 quaternion and f16 scale (saves 16). It cannot hold shear. A non uniform
  scaled parent with a rotated child makes one, and 23% of the garden's instances have it. A
  flag with a full matrix fallback would need two layouts or a 48 byte slot anyway. Every reader
  would build the matrix from the quaternion: about 20 extra multiply-adds per vertex in
  `mesh.wgsl`, `shadow.wgsl` and `shape2d.wgsl`, per instance in `cull.wgsl` and `bake_sh.wgsl`,
  and the cofactor on top. The transition resolver (`transition.c`) writes transforms on the
  render thread and would have to encode them too.
- **T2. f16 for the 3×3 part and f32 for the translation.** 18 + 12 bytes, padded to 32 (saves
  16, 33% of the stream). Rejected as a default. An f16 value is rounded within 2^-12 of itself,
  so the 204 unit floor of the Cull scene moves its edges by up to 0.025 units. Neighboring
  instances stop meeting exactly, which gives cracks and shadow leaks along shared edges. Cull
  would add an unpack per instance, the vertex shaders one per vertex. It could return as an
  opt-in for large crowds of small dynamic objects, where the per frame upload matters most.
- **T3. Quantized or f16 translation.** Rejected. f16 has a step of 0.5 at 1,000 units, and
  the UI glyphs of Text.layout_10k are at up to 11,914 units. f32 keeps a step of about 1 mm at
  10 km, which is enough. Translations relative to the camera would need every transform uploaded
  again whenever the camera moves.
- **T4. A 2D affine transform for 2D pages.** Rejected. World space UI is rotated in 3D (see the
  rank comment in `shape2d.wgsl`), and clips read the full world position.

The transform stays at 48 bytes. The page growth fix (O3) and S1 and S2 remove more upload than
any transform encoding, without loss.

## Mesh shading stream

The 16 bytes are the material slot, the composed tint as four unorm16 and the alpha. Measured:
no instance of any scene is tinted except in Material.tint_subtree (10,000 of 110,000). Every
instance has alpha 1 except in Material.blended_sort.

- **M1. Material id and alpha in 8 bytes, tint moved to a table.** Word 0 holds the material id
  (21 bits cover `BOX_RENDER_MATERIALS_MAX` = 2,097,152). Word 1 holds an index into a table of
  composed tints (16 bits, 0 = none) and the alpha as unorm16. This saves 8 bytes per mesh slot,
  64 → 56 after S2 (12.5%), and a shading scatter entry goes from 20 to 12 bytes.
  - Material.tint_subtree scatters 200,192 → about 120,000 per frame.
  - Material.dynamic_material uploads 160,000 → 80,000 of shading per frame, next to its
    800,000 of material records (see O1).
  - Render: one more dependent load in the vertex shader, for tinted instances only.
  - Touches `box_render_mesh_shading_t`, `box_draws_mesh_shading`, `instance.wgsl`,
    `mesh.wgsl`, `shadow.wgsl`, `transitions.c` and `transition.c`, which patch shading records
    in the packet, and a tint table with reference counts.
  - Effort: medium.
  - Deferred: the saving is the smallest of the list, and a tint change of a subtree would
    still touch each instance whose composed tint differs.
- **M2. Alpha as unorm8 in the material word.** Rejected. Slow fades show 8 bit steps, and the
  record stays 16 bytes unless the tint moves as well.

## 2D shading stream

The 80 bytes per slot:

- `color`: linear RGB, alpha, 16 bytes.
- `emissive`: linear RGB, 12 bytes; `w` is the aspect, the negated line width or 0 for a glyph.
- `material`: the shape words, 16 bytes.
- `uv`: 16 bytes.
- `ids`: the clip, the rank and two reserved words that are always 0 (8 bytes unused).

- **D1. Drop the reserved words and pack the color as unorm16x4.** 80 → 64 bytes, a 2D slot goes
  128 → 112 after S1.
  - Linear color and alpha are within 0 to 1 after `box_draws_tinted`. Unorm16 steps (1.5e-5)
    are finer than the smallest linear step of an 8 bit sRGB value (3e-4).
  - Shapes2D.dynamic_material uploads 800,000 → 640,000 per frame; Text.layout_10k 12.8 → 11.2
    MB per frame after S1.
  - Render: one `unpack2x16unorm` pair per vertex in `shape2d.wgsl`.
  - Touches `box_render_shading_t` and its static assert, `box_draws_material`,
    `box_draws_shape2d`, `text_draw.c`, `shape2d.wgsl`, and the shading records that
    `transition.c` patches.
  - Effort: small to medium.
- **D2. Reuse the material table for 2D.** Rejected. The material table is per owner. UI
  elements own their `Rgb`, so it would hold one record per element. Glyphs carry their atlas
  area in `uv`, and every 2D instance carries its shape words, aspect, clip and rank. A table
  could only take the color, emissive and border color, and would add a dependent load. The
  measured contents are highly redundant: Shapes2D.ui_tree_idle has 3 distinct records among
  10,100 when the rank is ignored, and Text.layout_10k has 9 among 110,000. A content
  deduplicated style table (32 bytes per instance) may be worth a look after O1 shows how
  deduplication behaves.

## Other per slot costs

### O1. The material table

`materials.c` gave every prefab, and every mesh shape that owns a material component, a record
of its own. Records were not shared by content. Every bench scene sets `Rgb` per shape, so in
those scenes each instance paid an 80 byte record (`box_render_material_t`) in a buffer that
doubles (`capacity`), next to its stream slots. Measured before the change:

| Scene | Records | Distinct contents | Buffer records | Buffer |
|---|---|---|---|---|
| Extract | 100,001 | 2 | 131,072 | 10.5 MB |
| Material.tint_subtree | 110,001 | 3 | 131,072 | 10.5 MB |
| Cull.many_batches_shadows | 20,001 | 2 | 32,768 | 2.6 MB |
| Cull.town | 4,802 | 49 | 8,192 | 0.7 MB |
| japanese_garden | 21,684 | 262 | 32,768 | 2.6 MB |
| cheese_market | 19,028 | 130 | 32,768 | 2.6 MB |
| spaceship (prefabs) | 330 | 39 | 512 | 0.04 MB |

Implemented: the table shares records by content, with reference counts. This removes about 80
bytes per instance that owns its material. Prefabs, `DynamicMaterial` shapes and shapes with a
running material transition keep private records, which `transition.c` patches by slot. Shader
cost: none. The `materials`, `setup_material_bytes` and `material_bytes` counters of
`bench/render` (release) report:

| Scene | Records | Setup material upload | Per frame |
|---|---|---|---|
| Extract | 100,001 → 2 | 8,000,080 → 160 bytes | 0 |
| Material.blended_sort, Material.tint_subtree | 110,001 → 3 | 8,800,080 → 240 bytes | 0 |
| Material.dynamic_material | 110,001 → 10,002 | 8,800,080 → 800,160 bytes | 800,000 |

The 10,000 shapes of Material.dynamic_material keep private records and upload 800,000 bytes per
frame, as before. Frame timings did not change measurably: a steady frame hashes nothing.

The content of a shape is hashed only when its material changes; the shape then takes or creates
the record of its new content. A shape whose slot changes scatters only its 16 byte mesh shading
(a 20 byte entry). When a material transition starts, the shape takes a private record, and when
it ends, the shape returns to a shared record.

Known cost: an inherited material transition gives every affected instance that owns a
`MaterialId` a private record for its duration (`box_transition_material_walk`). N such
instances upload N records per frame, and hash N records on the frame the transition ends.

### O2. Unused capacity of runs and pages

Measured slots per live instance: 1.19 to 2.38 in the benches, 3.02 in the garden and 3.74 in
the spaceship scene. There are three causes:

- A run's capacity is the power of two at or above its count, and at least
  `BOX_DRAWS_MIN_CAPACITY` (64). The spaceship scene's 162 batches have 20,352 run slots for
  8,762 instances.
- A page's buffers grow to the power of two at or above its top (`box_draws_store_reserve`).
  Extract's top is 147,456 slots and its buffers hold 262,144.
- A batch that outgrows its run leaves a hole. The spawn scene ends with 64,512 free slots below
  its top, and page buffers never shrink.

Today every unused slot costs 448 or 512 bytes. After S1 and S2 it costs 64 or 128, so S2 alone
removes 86% of this waste. What is left, for example 16.8 MB for Extract's 7.0 MB of live
slots, can be reduced as follows:

- Grow runs and page buffers by 1.5 instead of 2, or round page buffers up to a multiple of
  4,096 slots.
- Lower the minimum run to 16 slots.

Each of these makes runs move and pages grow more often. Without O3 that means more full
re-uploads, so O3 should come first.

### O3. Full re-uploads when a run moves or a page grows

When a page buffer is created again, `box_draws_pages` marks every batch of the page `full`, and
the main thread uploads every stream of every batch again. The backend could copy the old buffer
into the new one with `copyBufferToBuffer` before it releases it. The extract would then mark
nothing, and a moved run could be copied from its old slots the same way. In the spawn scene
this would remove most of the uploads that repeat existing records: 57.6 MB uploaded for 28.7 MB
of records, 8.6 MB for 4.1 MB after S2. This is derived from the code. The cost moves to GPU
copies, which run on the GPU timeline instead of through `writeBuffer` staging. The page buffers
need `CopySrc` usage. Touches `store.c`, `batch.c` and the buffer create in the backend
(`gpu.c`). Effort: medium.

Implemented: a grown page is created with `BOX_RENDER_BUFFER_KEEP`, which the backend honors by
copying the old buffer into the new one before it releases it. A moved run sends one
`box_render_copy_t` per stream from its old run, run after the resources and before the uploads
and scatters through a staging buffer (source and destination are often the same page), and only
the slots it got since its last send upload. The draw stats count `copies`, `copy_bytes`,
`kept_pages` and `kept_bytes`. Measured with the null backend, the spawn scene uploads 57.6 → 29.4
MB for its 28.7 MB of records; the rest is the slot words and segment padding of the scatters of
the frames without a move. Its 6 moves copy 28.2 MB on the GPU instead.

### O4. Scatter entries

An entry is the 4 byte slot plus the stride: 52 (transform), 20 (mesh shading), 68 (2D
shading), 388 (surface). Each page and stream segment adds a 16 byte header and is aligned to
256. The slot word is 8% of a transform entry and 20% of a mesh shading entry. Most of the cost
is in the 388 byte surface entries, which S1 and S2 remove for every slot that is not parametric.
No separate change is recommended. `scatter.wgsl` copies an entry word by word in one
invocation: 97 words for a surface entry. That loop also shrinks with S1 and S2.

### O5. CPU side copies

A batch keeps a CPU copy of every stream of every slot (`transforms`, `shadings` or
`mesh_shadings`, and `surfaces`). For a mesh slot that is 448 bytes, of which 384 are the
surface. Every upload is copied into the packet data, then into the backend's write. The
Extract setup packet carried 57.3 MB of data before O1: 49.3 MB of streams and 8.0 MB of
material records. With O1 the records take 160 bytes, and the packet carries 49.3 MB.
`ShapeMesh` is another 464 bytes per entity, 400 of which is `box_surface_data_t`. These
copies do not reach the GPU, but they are on the path to it and count against memory: the 1M
scenes did not fit in the job's 2 GiB. S1 and S2 drop the CPU surface copy of 2D and plain
batches, saving 42 MB in Extract. `ShapeMesh` belongs to `box.geometry` and is out of scope here.

## Ranked recommendations

Each item can become its own task. The savings use the measured scenes above.

1. **Drop the surface stream from 2D pages (S1).**
   - 2D slots go from 512 to 128 bytes.
   - Text relayout uploads 75% less per frame, and 2D page memory is 75% smaller.
   - No shader reads change.
   - Effort: small.
2. **Give mesh batches without a parametric surface a layout without the surface stream (S2).**
   - Mesh slots go from 448 to 64 bytes, and mesh page memory and full uploads are 86% smaller.
   - Plain meshes skip the per vertex surface load.
   - Effort: medium. It reuses the zero buffer binding of item 1.
3. **Deduplicate the material table by content (O1).** Implemented.
   - Removes the 80 byte record per instance of shapes that own their material: 100,001 → 2
     records in Extract, 4,802 → 49 in town.
   - Extract's setup upload of material records goes from 8,000,080 to 160 bytes.
   - Effort: medium.
4. **Copy grown pages and moved runs on the GPU instead of uploading them again (O3).**
   - Removes the repeated uploads of growth: spawning uploads half as much.
   - This makes item 5 safe.
   - Effort: medium.
5. **Tighten run and page growth (O2).**
   - Use a factor of 1.5 or 4,096 slot steps for page buffers, and a 16 slot minimum run.
   - After items 2 and 4, this cuts most of the 1.2 to 3.7 slots allocated per instance.
   - Effort: small.
6. **Pack the 2D shading into 64 bytes (D1).**
   - Drop the reserved words and store the color as unorm16x4.
   - Saves 16 of the 128 bytes per 2D slot that remain after item 1.
   - Effort: small to medium.
7. **Size the pages of each store by its own largest stride (follow-up of S2).**
   - Plain mesh pages hold 2,796,202 slots, so 1M boxes are one batch.
   - Effort: small.

Deferred: S3 (surface table) and M1 (8 byte mesh shading). Both are sound, but they save little
once items 2 and 3 are done.

Rejected:

- S4 (compact surface encoding): after S2 it applies only to rare slots, and S3 saves more.
- S5 (surface buffer per page on demand): one surface makes the whole shared page pay for it.
- T1 (TRS transform): it cannot hold shear, 23% of the garden.
- T2 (f16 matrix): cracks along shared edges at scene scales.
- T3 (quantized translation): no precision at UI and world coordinates.
- T4 (2D affine transform): world space UI is rotated in 3D.
- M2 (unorm8 alpha): steps in fades.
- D2 (material table for 2D): the material table is per owner, and glyphs carry their atlas
  area per instance.

## Follow-ups

- Before item 1:
  - Add allocated page bytes per stream and the page slot count to `box_render_draws_stats_t`.
  - Report them, with the three stream counters, from Extract, Shapes2D and Text in
    `bench/render`, so these measurements can be repeated without the driver used here.
- With item 2:
  - Add a bench scene with parametric surfaces, spaceship sized: no bench covers the surface
    path today.
  - Add tests for a batch that changes between the plain and the surface store.
  - Update the comments of `box_render_surface_t`, `include/instance.wgsl` and the
    `BOX_DRAWS_CHUNK` definition.
- On a job with a GPU:
  - Time `Cull.town`, `Cull.shadows_100k` and `LargeBatch.instances_1m` before and after item 2,
    for the effect of the skipped surface load on the vertex stage. This was not measured here.
- With item 4:
  - Check that the `CopySrc` usage and `copyBufferToBuffer` on 128 MiB buffers behave on the
    Emscripten Dawn build as well as on wgpu-native.
