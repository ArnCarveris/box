# Rain weather effect

Rain as a weather state of the sky using the particle system, synchronized with the atmosphere and fog.

## Details

The states task of the weather epic ships overcast and fog; rain waits for particles. This task adds the rain state to the weather component: a camera following emitter with streak sprites, intensity driving emission rate and the overcast and fog parameters, and wetness on material layers where the materials epic provides it. Verification is a visual scene with light and heavy rain and a unit test that the weather state drives the emitter and atmosphere parameters.

## References

- `design/DESIGN.md`, section "Sky", weather system
- `weather/states`, `particles/appearance`
