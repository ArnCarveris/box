# Render module

Render handles, static and dynamic instance batches, deltas and the Extract system feeding frame packets.

## Details

`RenderMesh` entities receive a zero initialized `RenderInstance` record. OnSet observers on WorldTransform and the material components mark static entities dirty; the Extract system in PreStore assigns each dirty entity a slot in its mesh's static batch, writes its 3x4 transform, linear color, emissive and PBR parameters into a `static_updates` delta and updates its world AABB. Removed entities swap the last slot into their place so batches stay dense.

Entities with `DynamicTransform` or `DynamicMaterial` on themselves or an ancestor are extracted every frame, grouped by mesh into the packet's dynamic instance array. The packet also carries new meshes, static counts, the camera view and the directional light. A debug build validates every instance record against the slot tables after each extract. 27 tests cover slots, deltas, swaps, dynamic transitions, AABB, camera and light extraction.
