# Geometry

Shape components. Setting one selects a cached unit mesh plus an instance scale (engine
component `ShapeMesh`). Also parametric surfaces (paths, profiles, sweeps, lathes, shells),
authored meshes, SDF 2D shapes and the shape registry. Module `box.geometry`
(`BoxGeometryImport`), header `include/box/geometry.h`, sources `src/geometry/`. Material
comes from box.material (`Rgb`, `PbrMaterial`, ...), placement from box.transform.

## Primitives

All centered on the origin unless noted. `segments` defaults to 24 (clamped 3..512), `smooth`
defaults to false (faceted).

| Shape | Fields (in order) | Notes |
|---|---|---|
| `Box` | `x, y, z` | Size |
| `RoundedBox` | `x, y, z, radius, segments` 4 | Segments per corner, rounded up to even |
| `Quad` | `x, y` | XY plane, faces +Z (rotate -pi/2 about X for a floor) |
| `Triangle` | `x, y` | Isosceles, base x, height y, XY plane, faces +Z |
| `RightTriangle` | `x, y` | Legs along X and Y, right angle at -x |
| `TrianglePrism` / `RightTrianglePrism` | `x, y, z` | The triangles extruded along Z (depth z) |
| `NGon` | `sides` 6, `radius` | Flat regular polygon, XY plane, faces +Z |
| `Star` | `points` (>= 3), `inner` 0.45 (relative), `radius` | Flat, XY plane; worked example of the shape registry (`src/geometry/star.c`) |
| `Sphere` | `segments`, `smooth`, `radius` | UV sphere |
| `HemiSphere` | `segments`, `smooth`, `radius` | Upper half, base at y = 0 |
| `IcoSphere` | `segments` (subdivision level 0..6, default 0), `smooth`, `radius` | |
| `Cone` | `segments`, `smooth`, `length`, `radius` | Along Y |
| `Cylinder` | `segments`, `smooth`, `length`, `radius` | Along Y |
| `Frustum` | `segments`, `smooth`, `length`, `radius_bottom`, `radius_top` | Truncated cone along Y |
| `Capsule` | `segments`, `smooth`, `length`, `radius` | `length` is the cylinder part; total height length + 2 radius |
| `Tube` | `segments`, `smooth`, `length`, `radius`, `thickness`, `arc` | Hollow cylinder; `arc` in radians, 0 = full |
| `Torus` | `segments` 24, `rings` 12, `smooth`, `radius`, `tube` | Ring in the XZ plane |

`Bevel {radius, top_front, ..., back_right (12 Box edges), top, bottom, segments 4}` rounds the
edges of a Box, Cylinder, Frustum, Cone, Tube, the prisms and ProfileExtrusion. Radii are in
shape units and scale down together when they do not fit (CSS border-radius). `radius` != 0
overrides the per-edge radii. No effect on other shapes. Non-uniform `Scale` stretches it.

```flecs
using box.transform
using box.geometry
using box.material

crate { Position: {0, 0.5, 0}; Box: {1, 1, 1}; Bevel: {radius: 0.02, segments: 1}; Rgb: {150, 110, 70} }
ball { Position: {2, 0.5, 0}; Sphere: {radius: 0.5, segments: 32, smooth: true} }
pebble { IcoSphere: {radius: 0.54, segments: 2, smooth: true} }
slab { RoundedBox: {31.8, 0.18, 27.8, 0.42} }
pipe { Tube: {length: 2, radius: 0.3, thickness: 0.05, arc: 3.1416, smooth: true} }
```

## Authored mesh

`Mesh3 {vertices, normals, uvs, indices, colors}` (vecs). Empty normals = per-face normals,
empty uvs = zeros, empty indices = draw in order, `colors` one rgba per vertex or empty.
Inherited by prefab instances (`ecs_get_mut` on an inheriting instance returns NULL; override
with `ecs_set`/`ecs_ensure`).

```flecs
leaf {
  Mesh3: {
    vertices: [{0, 0, 0}, {-0.07, 0.005, 0.13}, {0, 0, 0.34}, {0.07, 0.005, 0.13}],
    indices: [0, 1, 2, 0, 2, 3]
  }
  BackFaceMode: {BackFacePlain}
}
```

## Paths, profiles and parametric surfaces

| Component | Fields (defaults) | Notes |
|---|---|---|
| `Path2D` / `Path3D` | `start`, `segments` [{`kind` Line/CubicBezier/CatmullRom, `end`, `control1`, `control2`}], `closed` | Asset entity. Closed paths get a closing segment (CatmullRom if the last is) |
| `Profile2D` | `outer` (closed Path2D entity), `holes` (closed Path2D entities) | Asset entity |
| `Sweep` | `path` (Path3D), `profile` or `contour` (open Path2D, uncapped strip), `scale` {1,1}, `taper` 1, `roll`, `cap_start`/`cap_end` true, `segments` 16 (1..64), `rings` 24 (3..256) | Rotation minimizing frame; along a horizontal path profile y points down |
| `Loft` | `sections` [{`profile`, `offset`, `rotation`, `scale` {1,1}}] (>= 2), `cap_start`, `cap_end`, `segments` 16, `rings` 24 | Sections need the same point/hole counts |
| `Lathe` | `profile` (Path2D: x = radius, y = height), `inner`, `cap_start`, `cap_end`, `segments` 32 (3..256), `rings` 24 | Revolves about Y |
| `ProfileExtrusion` | `profile`, `depth`, `cap_start`, `cap_end`, `smooth` | Along Y, profile x on X, y on Z |
| `BeveledPrism` | `footprint` (3 or 4 strictly convex corners, x on X, y on Z), `depth`, `inset`, `offset`, `bevel` | Along Y, top shrinks by inset, shifts by offset |
| `EllipsoidShell` | `radius` (vec3), `longitude_start/end`, `latitude_start/end` (rad), `thickness`, `rims`, `segments` 32, `rings` 24 | Partial ellipsoid |
| `TorusShell` | `radius`, `tube`, `ring_start/end`, `tube_start/end`, `thickness`, `rims`, `segments` 32, `rings` 24 | Partial torus about Y |

Curved profile segments sample in 16 steps (or the `segments` of the Sweep/Loft). The
parametric shapes are inherited by prefab instances.

```flecs
assets {
  prefab DeckShape { Path2D: {start: {-0.5, -0.5}, closed: true, segments: [
    {kind: Line, end: {0.5, -0.5}}, {kind: Line, end: {0.5, 0.5}}, {kind: Line, end: {-0.5, 0.5}}]} }
  prefab DeckProfile { Profile2D: {outer: DeckShape} }
  JarProfile { Path2D: {start: {0.27, 0}, segments: [
    {kind: CubicBezier, control1: {0.39, 0.08}, control2: {0.53, 0.4}, end: {0.46, 0.66}},
    {kind: CubicBezier, control1: {0.43, 0.81}, control2: {0.31, 0.85}, end: {0.32, 0.95}}]} }
}
bridge {
  deck_path { Path3D: {start: {0, 0.4, 0}, segments: [
    {kind: CatmullRom, end: {2.7, 1, 0}}, {kind: CatmullRom, end: {5.4, 0.4, 0}}]} }
  deck { Sweep: {path: deck_path, profile: assets.DeckProfile, scale: {1.28, 0.13}, rings: 64} }
}
jar { Lathe: {profile: assets.JarProfile, cap_start: true, cap_end: true, segments: 32, rings: 20} }
```

## Surface regions, attachments and materials

- `SurfaceRegion`: `SurfaceOuter`, `SurfaceInner`, `SurfaceStart`, `SurfaceEnd`, `SurfaceUMin`,
  `SurfaceUMax`, `SurfaceVMin`, `SurfaceVMax`; u, v in 0..1 per region.
- `SurfaceAttachment {target, region, u, v, offset, rotation}`: places the entity on the target's
  parametric surface each frame (system `SurfacePlacement`). The entity gets `ManualTransform`;
  Z = surface normal, X = u tangent, then rotation and Scale.
- `SurfaceMaterials {outer, inner, rim, cap}`, each `SurfaceMaterial {color rgba (x shape
  color), metallic, roughness}` (replace the shape's); constructed white with default PBR.
- C: `box_surface_evaluate(world, e, region, u, v, &sample)` -> position, normal, tangent.

```flecs
moss { SurfaceAttachment: {target: trunk, region: SurfaceOuter, u: 0.25, v: 0.5, offset: -0.002} }
bowl { EllipsoidShell: {radius: {1, 0.34, 1}, longitude_start: 0, longitude_end: 6.2832,
  latitude_start: -1.5708, latitude_end: -0.12, thickness: 0.04, rims: true}
  SurfaceMaterials: {inner: {color: {70, 80, 104, 255}, metallic: 0.9, roughness: 0.4}} }
```

## 2D shapes (unlit, SDF shaded quads in the XY plane, facing +Z)

| Component | Fields (defaults) |
|---|---|
| `Rect` | `width`, `height` |
| `Circle` | `radius` |
| `Line` | `x1, y1, x2, y2`, `width` 1 (square ends) |
| `Polygon2D` | `path` (closed Path2D, Profile2D or Path3D seen from above), `scale` {1,1}, `offset`, `width` 1 (stroke of an open path). Even-odd fill, max `BOX_POLYGON2D_MAX_EDGES` = 4096 edges |
| `Border` | `width`, `radius` (Rect corners), `color` opaque black; drawn inside the edge |
| `BoxShadow` | `offset_x`, `offset_y`, `blur`, `spread`, `color` opaque black |
| `Image` | `texture`, `u0, v0` 0, `u1, v1` 1 (v 0 = top); Rgb defaults to white |
| `ImageSlice` | `left, top, right, bottom` (texels), `scale` 1, `edges`/`center` SliceStretch/SliceTile, `hollow` |

Color = Rgb x albedo texture + emission. Same-depth 2D shapes draw in batch order.
Rect/Circle/Line share one instanced batch, polygons another. `Border` is transitionable
(width, radius, color).

## Rules and gotchas

- Positional initializers follow field order: `Sphere: {1}` sets `segments`, not radius. Use
  named fields for round shapes.
- Round shapes are faceted unless `smooth: true`.
- Invalid values are logged (`invalid Sweep on <path>: ...`, `empty ShapeMesh ...`), not
  rejected. Check the log when a shape does not appear.
- A disabled shape draws in no pass until enabled; its children still draw.
- `Rotation` is in radians.
- Parametric surfaces keep their own UVs; `texture.Mapping` MappingObject applies to a Sweep
  (lengths in path units / size) and MappingFrame to all.
- Picking does not read textures: a shape is picked by its whole geometry.

## Cost

- Meshes are cached by key (topology + quantized proportions, 1/4096); size goes into the
  instance scale. All Boxes share one mesh; Spheres share per `segments`/`smooth`. Capsule
  length/radius, Tube thickness/arc, Frustum radii ratio, Torus tube/radius, RoundedBox and
  Bevel proportions are in the key, so many distinct proportions create many meshes.
- Parametric surfaces carry a per-instance surface record (384 extra bytes vs 64 for a plain
  mesh instance); prefer primitives for large counts.
- LOD: `View.lod {levels, distances[4]}` (max `BOX_LOD_LEVELS_MAX` = 4) halves segment counts per
  level for round shapes, RoundedBox, Bevel, Sweep, Loft, Lathe and the shells. Off by default.

## C API: registering a shape

```c
box_shape_register(world, &(box_shape_desc_t){
    .component = ecs_id(MyShape),
    .key = my_key,
    .generate = my_generate,
    .scale = my_scale
});
```

`key` fills `box_shape_key_t` (`i[4]` topology, `f[4]` proportions); values with equal keys share
a mesh. `generate` builds the unit mesh with `box_mesh_vertex` and `box_mesh_triangle`
(counter-clockwise). `scale` writes the instance scale (NULL for 1).

Register before the component is first used. `*_world` variants read other entities; `lod` /
`lod_world` add LOD keys; `surface` adds a parametric surface. Use `box_shape_quantize` for
float key members. Examples: `src/geometry/star.c`, `src/terrain/terrain.c`.
