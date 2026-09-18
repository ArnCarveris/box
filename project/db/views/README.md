# Views

Views that bind a camera to a render target with layers and per view toggles, plus cascaded shadows for the directional light.

## Details

A view is an entity that binds a camera to a render target and decides what is drawn: which layers are visible, which layers cast shadows, which effects run and whether shadows and material layers are enabled. Layers are user defined entities used through a fragmenting relationship so an entity or a light can be in more than one layer, and a view with no layers configured renders everything. Shadows are cascaded maps for the single directional light, split between the static and dynamic instance buffers so a static scene does not re-render its cascades every frame. The render command composes a temporary view and camera, which is what lets an agent render any entity, prefab or template on its own.

Verification is a new test/view unit test project for the components, layer masks and cascade splits, visual integration scenes that render one world through two views with different layers, and benchmarks for the frame time with shadows on. Depth is already reverse Z with an infinite far plane from scene_core; every depth target this epic creates keeps that convention and a precision test guards it. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: View, the Layer relationship, ShadowSettings and the per view toggle fields that the effects, materials and LOD epics fill in later. The render command task adds no components.

## References

- design/DESIGN.md, section Graphics (views and layers, shadows)
- design/datamodel/camera.flecs and design/datamodel/material.flecs (NoShadow)
