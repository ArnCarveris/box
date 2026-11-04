# GPU instance memory

This report summarizes instance-buffer memory and upload costs, the optimizations
already implemented, and the remaining tradeoffs. Sizes are bytes; MB means 10^6
bytes. Measurements are historical snapshots, not performance guarantees.

## Current layout

| Store | Transform | Shading | Surface | Total per slot |
| --- | ---: | ---: | ---: | ---: |
| Plain mesh | 48 | 16 | 0 | 64 |
| 2D shapes and glyphs | 48 | 64 | 0 | 112 |
| Parametric surface | 48 | 16 | 384 | 448 |

Originally every mesh slot carried 448 bytes and every 2D slot 512 bytes. Most
of that was a zero-filled surface record: no benchmark scene used it, and only
1.9% of spaceship instances did. Removing unused surface streams produced the
largest savings. Static streams upload only when changed; steady traffic comes
from dynamic transforms, dynamic shading and private material records.

The transform holds three float4 rows of the model matrix, including instance
scale. Mesh shading holds a material slot, composed tint and alpha. Packed 2D
shading holds color, emission, shape words, UVs, clip and rank. Surface records
hold parametric geometry and per-region material data.

## Measurement scope

The initial measurements used an external driver linked against unchanged
`bench/render` objects. It recorded page-buffer allocations, full uploads, scatter
segments and change-buffer uploads, then checked them against
`box_render_draws_stats_t` and `RenderStatus.upload_bytes`. CPU inspection supplied
run/page occupancy, distinct materials and transform orthogonality. These are
byte counts, independent of the debug build used on arm64 Linux.

Scenes requiring a GPU were reconstructed with a backend that dropped packets.
Shipped scenes such as spaceship and japanese_garden supplied parametric surfaces
and realistic transform hierarchies absent from the basic benches. Large scenes
that exceeded the driver's 2 GiB limit were derived from allocator rules and are
marked below.

Post-change measurements came from release `bench/render` on arm64 macOS with a
GPU, plus null-backend drivers for shipped scenes. GPU render/cull/scatter time
and vertex-shader surface loads were not measured by this investigation.

The benchmark counters now include `page_slots`, `page_transform_bytes`,
`page_shading_bytes`, `page_surface_bytes`, `page_bytes`, `materials` and upload/copy
counts. Use `bake3 run --cfg release bench/render --local-env` to repeat them;
Cull requires a GPU. `bench/scene` normally has no backend or view and uploads no
instance data.

## Allocated page buffers

The table includes both stores where applicable. Slots are allocated page capacity;
instances are live slots. The spaceship row is the similarly sized
`Parametric.extract_spaceship_size` benchmark, not the original shipped scene.
LargeBatch rows are derived from the current allocator and confirmed by a null
backend driver.

Measured after S1, D1, S2, O2 and InstanceRunExact:

| Scene | Instances | Slots | Transform | Shading | Surface | Total | Surface share |
|---|---|---|---|---|---|---|---|
| Extract (100k `Box` + 10k dynamic `Sphere`) | 110,000 | 110,000 | 5.3 MB | 1.8 MB | 0 | 7.0 MB | 0% |
| Material.tint_subtree | 110,000 | 110,000 | 5.3 MB | 1.8 MB | 0 | 7.0 MB | 0% |
| Cull 100k scene | 100,001 | 100,004 | 4.8 MB | 1.6 MB | 0 | 6.4 MB | 0% |
| Cull.many_batches_shadows (400 batches) | 20,000 | 20,800 | 1.0 MB | 0.33 MB | 0 | 1.3 MB | 0% |
| Cull.town, mesh | 4,801 | 4,864 | 0.23 MB | 0.08 MB | 0 | 0.31 MB | 0% |
| Cull.town, 2D | 120 | 124 | 0.006 MB | 0.008 MB | 0 | 0.014 MB | 0% |
| Shapes2D.dynamic_material | 110,000 | 110,000 | 5.3 MB | 7.0 MB | 0 | 12.3 MB | 0% |
| Shapes2D.ui_tree_idle | 10,100 | 10,100 | 0.48 MB | 0.65 MB | 0 | 1.1 MB | 0% |
| Text.layout_10k (glyphs) | 110,000 | 110,000 | 5.3 MB | 7.0 MB | 0 | 12.3 MB | 0% |
| spaceship (`Parametric.extract_spaceship_size`) | 8,792 | 10,960 | 0.53 MB | 0.18 MB | 0.98 MB | 1.7 MB | 58% |
| Spawn, 64 × 1,000 boxes | 64,000 | 84,312 | 4.0 MB | 1.3 MB | 0 | 5.4 MB | 0% |
| LargeBatch 1M, **derived** | 1,048,576 | 1,048,576 | 50.3 MB | 16.8 MB | 0 | 67.1 MB | 0% |
| LargeBatch 1.5M, **derived** | 1,572,864 | 1,572,864 | 75.5 MB | 25.2 MB | 0 | 100.7 MB | 0% |

For comparison, the original Extract scene allocated 262,144 slots and 117.4 MB;
Shapes2D.dynamic_material allocated 134.2 MB and Text.layout_10k 67.1 MB.
These comparisons include both stream removal and tighter capacity allocation.

## Implemented changes

### S1 and S2: omit unused surface streams

2D pages have no surface stream (S1). Mesh batches use separate plain and surface
stores (S2), selected by the batch key. Pages without surfaces bind a shared zero
record at binding 3. Mesh and shadow shaders guard surface reads; 2D shaders never
read them.

S1 removed 384 bytes per 2D slot, reducing 512 to 128 before shading packing.
S2 removed 384 bytes per plain mesh slot, reducing 448 to 64. Full uploads and
CPU batch copies lose the same bytes. This does not change culling, which reads
transforms only. Gaining or losing a parametric surface changes the batch key.

The 384-byte surface record remains for surface batches. Its vectors 0..8 hold
geometry parameters and asset offsets; 19 identifies a resolved render mesh;
20..21 hold region materials; 22..23 are CPU bounds. Vectors 9..18 are unused.
Most surface kinds resolve to meshes on the CPU, but ProfileExtrusion and failed
resolutions still need shader evaluation.

Each store now sizes its chunk from its largest stride and the 128 MiB binding
limit: 2,796,202 plain mesh slots, 2,097,152 2D slots or 349,525 surface slots.
The million-box scenes therefore fit in one plain-mesh batch/page.

### O1: share material records by content

An owned material component previously gave each shape a private 80-byte table
record, even when thousands had identical contents. The table now hashes changed
contents and reference-counts shared records. A steady frame hashes nothing.
Prefabs, `DynamicMaterial` shapes and active material transitions keep private
records so the render thread can patch them.

| Scene | Records | Setup material upload | Per frame |
|---|---|---|---|
| Extract | 100,001 → 2 | 8,000,080 → 160 bytes | 0 |
| Material.blended_sort, Material.tint_subtree | 110,001 → 3 | 8,800,080 → 240 bytes | 0 |
| Material.dynamic_material | 110,001 → 10,002 | 8,800,080 → 800,160 bytes | 800,000 |

Town's records fell from 4,802 to 49, japanese_garden's from 21,684 to 262 and
cheese_market's from 19,028 to 130. Dynamic materials still upload their private
records every frame. An inherited material transition can temporarily give each
affected material owner a private record; completing it hashes those records back
into the shared table.

### O3: copy page growth and run moves on the GPU

Grown buffers use `BOX_RENDER_BUFFER_KEEP`, preserving old bytes with GPU copies.
Moved runs send one `box_render_copy_t` per stream; only new slots upload.
Copies run after resource creation and before uploads/scatters. This replaces
repeated CPU uploads of unchanged records.

Independent copies between different buffers run directly. Same-buffer copies,
or copies whose sources another copy writes, use staging. Staging is released
after 60 packets without staged copies and periodically shrinks to recent demand.
Counters include `copies`, `copy_bytes`, `kept_pages`, `kept_bytes`,
`copies_direct`, `copies_staged`, `copy_staging` and `copy_staging_peak`.

Before surface removal and tighter growth, the 64 × 1,000-box spawn experiment
uploaded 57.6 MB for 28.7 MB of records. GPU copying reduced that upload to
29.4 MB. This is an intermediate measurement; it does not describe today's
64-byte slots and in-place run growth.

### O2 and InstanceRunExact: reduce unused capacity

Runs and page buffers grow by 1.5, rounded up to four slots, with a 16-slot minimum
run. A run at the top of its page extends in place. Other moved runs leave holes
for reuse; page buffers do not shrink.

A new batch starts with its exact count rounded up to four, subject to the minimum.
Growth headroom is added only after it outgrows that run. This keeps one-frame
scene creation compact: Extract holds 110,000 slots instead of the intermediate
137,568 or original 262,144.

The tradeoff is an earlier first growth for batches that gain instances soon after
creation. GPU copying makes that cheaper, and top-of-page extension avoids a move.
The spawn scene grows to 84,312 slots with ten page growths and no run moves.
Fixed 4,096-slot page steps were rejected: they waste small pages and can produce
more copying when growth is gradual.

### D1: pack 2D shading

Dropping two reserved words and storing color as unorm16x4 reduced shading from
80 to 64 bytes and the post-S1 slot from 128 to 112. The shader unpacks two words
per vertex. Quantization steps are finer than the smallest linear-light step of
an 8-bit sRGB color.

## Remaining options

| Option | Saving | Decision |
| --- | --- | --- |
| S3: shared surface table plus 32-bit IDs | 380 bytes per surface slot, plus 384 per distinct table record | Deferred until many parametric instances justify it |
| M1: material ID/alpha in 8 bytes, tint table | 8 bytes per mesh slot; shading scatters shrink from 20 to 12 bytes | Deferred; smaller benefit and an extra tinted-vertex load |
| S4: compact surface encoding | 192..336 bytes per surface slot | Rejected for now; two CPU/GPU layouts, less benefit than S3 |
| S5: allocate surfaces on demand within shared pages | Depends on page contents | Rejected; one surface makes the whole page pay |
| T1: position/quaternion/scale | 8..16 bytes per transform | Rejected; cannot represent shear |
| T2: half-float 3×3 matrix, float translation | 16 bytes per transform | Rejected as default; cracks and shadow leaks along shared edges |
| T3: quantized translation | Encoding dependent | Rejected; insufficient world/UI precision |
| T4: 2D affine matrix | Encoding dependent | Rejected; world-space UI can rotate in 3D |
| M2: 8-bit alpha | No saving unless tint also moves | Rejected; visible fade steps |
| D2: material table for 2D | Only style fields can move | Rejected; glyph UVs, shape words, clip and rank remain per instance |

S3 would reduce spaceship's 166 surface instances (71 distinct records) from
63.7 KB of surface contents to about 27.3 KB of records and 664 bytes of IDs,
excluding page headroom. It adds a dependent vertex load and reference counting.
A separate deduplicated 2D style table may be more suitable than reusing mesh
materials, but is not implemented.

The full 48-byte transform remains necessary: japanese_garden has 5,071 sheared
instances out of 21,683 (23%), from rotated children under nonuniformly scaled
parents. Half-float linear transforms can move a 204-unit floor edge by roughly
0.025 units, while half-float translations step by 0.5 at 1,000 units. UI glyphs
in Text.layout_10k reach 11,914 units. Camera-relative translations would require
rewriting transforms whenever the camera moves.

## Upload and CPU costs

A scatter entry holds a four-byte slot plus the stream record: 52 bytes for a
transform, 20 for mesh shading, 68 for 2D shading and 388 for a surface. Segments
add a 16-byte header and pad to 256 bytes. No separate scatter encoding change is
recommended after removing unused surfaces.

Dynamic streams remain the steady-state cost. Extract's 10,000 dynamic spheres
upload 480,000 transform bytes per frame. `Material.dynamic_material` uploads
160,000 shading bytes plus 800,000 private material bytes per frame. Static
surfaces do not upload unless their mesh changes or their slot is rewritten.

Batches also keep CPU stream copies. S1/S2 remove unused surface copies there,
saving about 42 MB in Extract. `ShapeMesh` still holds geometry-side surface data;
reducing that is outside this report.

Further work should be driven by scenes that exercise the deferred paths, and
should measure GPU time as well as bytes. In particular, time surface vertex
loads and verify copy behavior on both wgpu-native and browser backends before
drawing GPU performance conclusions from these memory results.
