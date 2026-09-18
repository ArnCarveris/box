# Hi-Z occlusion culling

A hierarchical depth pyramid and an occlusion test that produces no artefacts with fast moving cameras.

## Details

After the depth prepass the renderer builds a mip chain of the depth buffer with the reduction that matches reverse Z, then tests the screen space bounds of each instance against it. To stay artefact free with a fast camera the test uses this frame's depth in a two pass scheme: draw what was visible last frame, build the pyramid, then re-test everything that was rejected and draw the newly visible objects. Culling results feed the same indirect draw path as frustum culling.

Verification: a visual test sweeps a camera at high speed past occluders and compares captures against a run with occlusion culling disabled, where the images must match. Unit tests cover the pyramid reduction and the screen bound projection, and a benchmark on a dense city-like scene reports occluded instances and frame time.
