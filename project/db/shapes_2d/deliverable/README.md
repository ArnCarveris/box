# 2D shapes: completion report

## What was built
- `box.geometry` gained `Circle`, `Border`, `Line` and `Image`. A circle is a disc whose segment count follows its radius in steps of eight so nearby radii share one ngon mesh, and a line divides its endpoints and thickness by the largest of them so one outline is one mesh at any size.
- `Image` adds `(With, Rect)` and bakes its uv region, `ImageFit` (`Cover` crops, `Contain` widens against the pixel size of the texture, re-read when the upload finishes) and rotation into the quad uvs; the texture binds through `PbrTextures` like any material.
- `Border` adds `(With, Rect)` and carries no geometry: its radii and widths travel as ratios of the smaller half extent in four new instance words with a mode bit, and `include/shape2d.wgsl` rebuilds the rounded rectangle from the normalized local position, so changing a radius uploads one instance and builds no mesh.
- The renderer draws every 2D shape through the instanced path of the primitives, so tint, layers, blending and views apply unchanged.

## Verification
- 1322 unit tests across 15 projects, all passing; `test/geometry` holds 96 with the new `Shapes2D` and `Coverage` suites and `test/render` 369 with its `Shapes2D` suite.
- Scenes in `integration/`: `shapes_2d` shows the sheet of discs, lines, borders and fitted images; every earlier scene bit identical.
- Benchmarks in `bench/` against the collision epic: the instance growing from 140 to 156 bytes costs nothing measurable, `Extract.dynamic_10k` sits within noise of the previous epic.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- Quantizing the mesh key (segment count by radius, line by its largest dimension) is what keeps thousands of differently sized shapes on a handful of meshes.
- A shader side rounded rectangle beats geometry for borders: the instance already carries the normalized position, so radius and width edits never touch a vertex buffer.

## Not in this epic
- The shadow pass still casts the full rectangle of a rounded shape, and an opaque border has a hard edge where a blended one antialiases.
- Text and SVG stay in the textures epic; a UI tree over these shapes is the UI epic.
