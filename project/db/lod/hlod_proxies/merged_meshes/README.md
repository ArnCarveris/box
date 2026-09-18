# Merged and simplified meshes

Merge a subtree into one mesh and simplify it with an error bound, evaluating meshoptimizer first.

## Details

Evaluate meshoptimizer for error bounded simplification rather than writing a simplifier, and keep a short research note with the decision, the licence and the integration cost as a deliverable. If it fits, vendor it as a bundle and use it to produce a merged mesh per material group with a measured error. Unit tests cover merging, the error bound and the material grouping.
