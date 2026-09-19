# Ten thousand transitioning entities

`transitions_10k.flecs` is staggered: each entity delays its ten transitions by up to a
second and keeps them in flight for the whole run, while the churn of the benchmark app
restarts one position per entity per second (`--warmup 90`, `--churn 167-167`).

The renderer no longer keeps the records of an instance next to it. `box_transition_pool_t`
holds one entry per instance and, per channel, the arrays `times`, `spans` and `outs`, so a
frame interpolates every active record in seven passes over contiguous memory, always four
floats, with no branch on the channel or on the part of it a record moves; both ends of a
record are widened to the whole channel when it starts. `box_transition_fill` stays for a
single instance and shares the composition with the pool.

Release measurements. `bench_transitions_10k.json`: 10000 static instances, 60000 records in
the lanes, 9.12 ms mean, 7.95 ms p50, 15.19 ms p99, 110 fps, 0 spikes, against 9.2 ms for the
old scene that held a sixth of the records. Packet: 808 bytes per entity that starts (256
command, 6x64 records, 168 static update), 8.1 MB on the frame the scene loads and 135 KB per
frame in the steady state. `bench.json`: the lane pass is 115 us for 60k records (1.9 ns
each), lanes plus composition 353 us per frame for 10k instances against 349 us for the old
per instance fill, so throughput is unchanged (it is bound by the transform composition and
by memory) while an instance now costs 452 bytes plus 80 per moving channel instead of 860.

Verified by 410 unit tests (40 in `test/transition`, the new `Scale` suite: the packet byte
budget of the scene, a frame of the steady state that allocates nothing through the OS API
hooks, pool arrays that never grow again, the lanes matching `box_transition_fill` float for
float, staggered delays), and 73 of 74 visual scenes bit identical; `console.flecs` differs
from the in flight console work, not from this task.

Left out: the benchmark app restarts positions only, so the other nine transitions of an
entity stay in flight by their duration instead of being restarted as they finish. A
`--transition-churn` in `apps/box_bench` (not owned here) would close that.
