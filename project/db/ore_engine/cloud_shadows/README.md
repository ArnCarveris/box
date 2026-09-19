# Cloud shadows

A moving cloud shadow layer over the sun light driven by the weather's overcast.

## Details

Ore Else's render view has a cloud_shadow block. Add CloudShadows {enabled, scale, speed, density}
to the sky or weather module, sampled in the light pass as a tiled noise that scrolls with wind and
darkens the sun by the overcast, with the quality tier able to switch it off.

Verification: A unit test of the parameters reaching the packet, a visual scene under partial overcast, every
other scene unchanged.

## References

- project/db/weather/deliverable/README.md
- project/db/sky/deliverable/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/engine.flecs
