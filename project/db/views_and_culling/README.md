# Views and culling

Views that bind a camera to a target with layers and per view toggles, frustum and Hi-Z occlusion culling, and cascaded shadows.

## Details

A view is an entity that binds a camera to a render target and decides what is drawn: which layers are visible, which layers cast shadows, which effects run and whether shadows and material layers are enabled. Layers are user defined entities used through a fragmenting relationship so an entity or a light can be in more than one layer, and a view with no layers configured renders everything. Culling turns renderer owned instance data into a visible set per view with a frustum test and a Hi-Z depth pyramid that does not drop visible objects when the camera moves fast. Shadows are cascaded maps for the single directional light, split between the static and dynamic instance buffers so a static scene does not re-render its cascades every frame.

Verification is a new test/view unit test project for the components, layer masks, frustum planes and cascade splits, visual integration scenes that render one world through two views with different layers, and benchmarks that report visible and culled instance counts and the frame time with culling and shadows on. Depth is already reverse Z with an infinite far plane from scene_core; every depth target this epic creates keeps that convention and a precision test guards it. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: View, the Layer relationship, ShadowSettings and the per view toggle fields that the effects, materials and LOD epics fill in later. The render command task adds no components; it composes a temporary view and camera.

## References

- design/DESIGN.md, section Graphics (views and layers, shadows, culling)
- design/datamodel/camera.flecs and design/datamodel/material.flecs (NoShadow)
