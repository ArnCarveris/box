# Ten thousand transitioning entities

Ten thousand entities with ten transitions each, at frame rate and without steady state allocation.

## Details

Build the brief's verification scene of 10k entities each transitioning ten values with staggered start times, run it in release and report frame time, packet size and the renderer side evaluation cost. Lay the records out so a hundred thousand active transitions sit in contiguous arrays that vectorise. The report is the deliverable.

Verification: the benchmark report plus unit tests asserting the packet stays within the expected size for that scene and that the steady state does not allocate.
