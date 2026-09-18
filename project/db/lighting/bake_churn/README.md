# Bake cost under churn

With baking on the 100k stress scenes went from 1.25 ms to 4.2 ms mean per frame; bring churn cost back near the dynamic only figure.

## Details

The stress scenes move between 1 and 1000 static entities per frame. Each move re-lays out or invalidates probes, rebuilds the occupancy grid and rebakes up to the budget of probes, and the renderer thread work shows up as main thread time because the packet queue blocks when two packets are in flight. Profile the three suspects: the occupancy grid rebuild (make it incremental, clearing and refilling only the voxels of moved instances, or rebuild at most every n frames), the per frame budget (a moved static entity in a scene without static lights should cost nothing: skip the bake entirely when no static light or emitter is in range of the dirty probes), and the CPU invalidation scan.

Verification: `python3 tools/integration.py bench` reports the stress scenes at or below 2 ms mean and 4 ms p99 with baking on, the baked room and lights city renders stay unchanged, and `Bake.invalidate_one_of_100k` stays under 200 us. Record before and after numbers in the deliverable.

## References

- `project/db/lighting/bake_resolution/deliverable/README.md`
- `src/renderer/bake.c`, `src/render/extract_lights.c`
