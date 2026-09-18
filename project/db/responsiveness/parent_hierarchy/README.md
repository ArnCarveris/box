# Parent hierarchies

Support the non fragmenting Parent relationship next to ChildOf in transforms, bounds and extraction.

## Details

Flecs offers Parent as a non fragmenting alternative to ChildOf; the brief asks for both, and the million object scenes need it because fragmenting a scene of tiny children into as many tables is expensive. Transform propagation, bounds aggregation, tint resolution and extraction learn to read either relationship, with one traversal helper so the rule lives in a single place. Mixing both in one tree is allowed and documented.

Verification: unit tests mirror the existing transform hierarchy suite for Parent, cover mixed trees, reparenting between the two and the dynamic subtree rules. A benchmark compares table counts, transform propagation and extraction cost for a 100k entity tree built with each relationship.
