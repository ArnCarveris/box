# HLOD proxies

Generated proxies that replace a whole subtree with a cheaper representation, so a distant building is one draw.

## Details

A suitable subtree is replaced by a proxy: first a reduced set of primitives, then a merged and simplified mesh and optionally an impostor for very distant objects. Replaced descendants are not traversed or rendered. Generation runs off the main thread and a proxy is only used once it is ready, and selection optimises total cost counting instances, triangles, draw and material groups and GPU memory.

Verification: unit tests cover proxy selection per level, that replaced descendants are skipped and the cost model. Visual tests compare a proxy against its full subtree at the switch distance and measure the image difference, and a benchmark reports draw calls and triangles with and without proxies.

The three subtasks are the three representations, in the order the brief prefers them.
