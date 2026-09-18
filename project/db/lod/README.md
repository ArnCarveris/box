# Hierarchical LOD

Screen space error driven hierarchical LOD over the authored entity tree, with primitive reduction, detail pruning and generated proxies.

## Details

LOD selection uses projected screen space error with hysteresis rather than distance thresholds, for perspective and orthographic cameras, with quality settings per view and more aggressive settings allowed for shadow views. The authored hierarchy is the LOD hierarchy: a subtree may be replaced by a cheaper representation and replaced descendants are neither traversed nor rendered. Parametric shapes reduce their tessellation by projected size, visually insignificant descendants such as trim, window frames, bolts and railings are pruned, and suitable subtrees get generated proxies, preferring reduced primitives and falling back to merged and simplified meshes and impostors.

Runtime selection works on renderer owned hierarchy data and never visits every renderable entity, traversal is integrated with frustum and Hi-Z culling so a rejected parent stops its descendants, and renderer only spatial clusters can be added when the authored hierarchy is unsuitable, without touching Parent or ChildOf. Generated data is cached and invalidated incrementally, generation runs on the job system and never blocks rendering, and LOD never affects gameplay entities, picking or collision. Verification is unit tests per mechanism, debug statistics and the brief's roughly four million object city where traversal cost must scale with visited nodes and the frame must stay spike free. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: the per view LOD quality and error settings, LodPin for subtrees that must not be pruned, the proxy and cluster records owned by the renderer and the LOD statistics component.

## References

- design/DESIGN.md, section LOD
- meshoptimizer, evaluated in the merged meshes subtask
