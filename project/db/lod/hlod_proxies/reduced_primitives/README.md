# Reduced primitive proxies

Replace a subtree with a smaller set of primitives that keeps its silhouette and material mix.

## Details

Cluster the primitives of a subtree by material and proximity and emit a few larger primitives that approximate the whole, which is the cheapest proxy and the one the brief prefers. The generated proxy records its approximation error so the screen space metric knows when it may be used. Unit tests cover clustering, the error estimate and material preservation.
