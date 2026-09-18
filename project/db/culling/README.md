# Culling

Frustum and Hi-Z occlusion culling that turn renderer owned instance data into a visible set per view.

## Details

Culling turns renderer owned instance data into a visible set per view: a frustum test over the instance bounds feeds the indirect draws, and a Hi-Z depth pyramid adds an occlusion test that does not drop visible objects when the camera moves fast. Both run per view, so one world rendered through two views is culled once per view against the same instance data. The visible sets are also what the shadow cascades and the later LOD epic build on.

Verification is unit tests for the frustum planes and the occlusion test in the test/view project, visual integration scenes that show nothing disappears with a fast moving camera, and benchmarks that report visible and culled instance counts and the frame time with culling on. The depth pyramid keeps the reverse Z convention with an infinite far plane that scene_core established, and a precision test guards it. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

This epic introduces no components of its own: it fills the visible sets that the View entity and its per view toggles configure.

## References

- design/DESIGN.md, section Graphics (culling)
- design/datamodel/camera.flecs
