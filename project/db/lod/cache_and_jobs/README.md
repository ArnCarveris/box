# LOD caching and the city benchmark

Incremental caching and invalidation, job driven generation, debug statistics and the four million object city.

## Details

Generated LOD data is cached and invalidated per affected node: moving a rigid root regenerates nothing, while a changed descendant invalidates only the proxies that contain it. Generation runs on the job system, prioritises visible and nearby content and never blocks the render thread. Statistics report selected levels, visited nodes, proxy counts and the generation queue depth for debugging.

Verification: unit tests cover invalidation for root movement, descendant changes, destruction and the priority order. The brief's verification is a roughly four million object city built from representative hierarchies rather than a flat root, where traversal cost must scale with visited nodes and the frame must stay free of spikes; the benchmark report and a capture of the city are the deliverables.
