# Picking at a million objects

Pick an object in a scene of a million nested objects in under a millisecond.

## Details

Build the verification scene the brief asks for, 50k top level objects containing a million descendants, and measure picks from a moving camera. The measurement must stay under one millisecond and must not scale with the leaf count, which is what the two level index is for. Tune the index and, if needed, cache the last pick for repeated queries at the same screen position.

Verification: the benchmark report and the scene are the deliverables, and a unit test asserts that the pick result matches a brute force pick on a smaller version of the same scene.
