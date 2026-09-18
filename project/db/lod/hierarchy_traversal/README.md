# LOD hierarchy traversal

Renderer owned hierarchy traversal that visits nodes instead of entities and short circuits on culling.

## Details

The renderer keeps the LOD hierarchy as its own data, built from the authored tree, and traverses it from the roots each frame: a node rejected by the frustum or the Hi-Z test stops traversal of its descendants, and a node that resolves to a proxy stops there too. Nothing visits every renderable entity per frame. When the authored hierarchy is unsuitable, renderer only spatial clusters are inserted as extra nodes without touching Parent or ChildOf, and proxies never affect gameplay entities, picking or collision.

Verification: unit tests cover traversal order, culling short circuits, cluster creation and that ECS identity and picking results are unchanged. A benchmark reports visited nodes against total leaves for a large scene, which must not scale with the leaf count.
