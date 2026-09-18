# Detail pruning

Pruning of visually insignificant descendants such as trim, window frames, bolts and railings.

## Details

A node whose projected contribution falls below the threshold is dropped together with its subtree, using the aggregated bounds and a contribution estimate from projected size and material contrast. Pruning is a traversal decision and never changes the authored scene, so the entities still exist for gameplay, picking and collision. A LodPin component keeps a subtree that must never be pruned.

Verification: unit tests cover the contribution estimate, the threshold decision, pinning, and that pruning a parent prunes its descendants. A visual test compares a distant building with and without pruning and the benchmark reports the instance count difference.
