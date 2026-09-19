# Renderer instance cap

Drawing more than about 250k instances aborts with a wgpu invalid bind group; grow or page the instance buffers so a scene with a million drawn instances renders.

## Details

The controller collision bench wanted a million drawn nested objects and had to settle for 50k towers of colliders because the instance storage buffers stop at a fixed size and the bind group that references them becomes invalid past it. Find the limit (buffer size, binding size limit or a bind group cache keyed on the old buffer), grow the buffers by doubling with the bind group recreated on growth or draw in chunks that fit the device limit, and keep the frame packet static and dynamic paths both working past the old cap.

Verification: a reproducer test that extracts 300k static instances and renders a frame, a bench scene of a million cubes in `test/integration/bench` recorded in the deliverable, every visual scene unchanged.

## References

- `project/db/collision/controller_collision/deliverable/README.md`, `src/renderer/`
