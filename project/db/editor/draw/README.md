# Immediate mode drawing API

A box.draw module that draws an instance of a prefab or template for one frame, keyed so the instance keeps its identity, optionally on an overlay layer drawn over the scene.

## Details

box_draw takes a box_draw_desc_t with an asset, a key, a transform and an overlay flag and returns the instance entity, which lives under box.draw.instances. The same asset and key return the same instance every frame, so the tasks and the Hovered tag of a template instance survive, and a BoxDrawRetire system hides instances that were not drawn this frame and deletes them after BOX_DRAW_RETIRE_FRAMES frames.

An overlay draw joins the box.draw.overlay layer, which is registered as a UI layer so the views of the scene hide it, and a box.draw.overlay_view with the camera of the primary view draws it unlit on top. Every descendant of an overlay instance joins the layer as well.

Verified with the Draw suite of test/draw.
