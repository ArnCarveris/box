# Spatial index

A two level, incrementally updated index over root bounds and colliders that answers ray, sphere and frustum queries.

## Details

The index is a bounding volume hierarchy over root bounds with a second level over the colliders inside a root, updated incrementally from the observers that maintain bounds; dynamic objects live in a separate cheaper structure rebuilt per frame. It answers ray, sphere and frustum queries without walking the scene. It lives on the game side because picking, controllers and gameplay all need it.

Verification: unit tests cover build, incremental insert, remove and move, and compare query results against a brute force reference on randomised scenes. A benchmark reports build and query time on a million colliders and the per frame update cost with a thousand movers.
