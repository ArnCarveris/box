# Weather: completion report

## What was built
- `Weather {overcast, fog, rain, transition, current}` on the sky: continuous amounts eased exponentially toward their targets under the fixed step, snapping on the first frame so captures are stable, with the current amounts published for other systems. Overcast greys the sky view radiance, fades the sun, moon and stars, greys the aerial haze and the fog color and flattens the view light while the atmosphere, its tables and the sun direction stay the base; fog multiplies the sky driven height fog density up to fourteen times; rain drives the wetness material layer amount, with the rain emitter itself left to the particles epic.
- The overcast amount rides in the sky block of the packet and reaches the environment prefilter through the shared sky include, so the IBL follows the overcast deck.

## Verification
- 1127 unit tests across thirteen projects, all passing; the `Weather` suite in `test/render` holds 21 cases over blending, snapping, the sky, light and fog effects of each amount and the wetness coupling.
- Scenes in `integration/`: `weather_overcast` and `weather_fog`, baselined; every earlier scene bit identical.
- Stress benchmarks: `static_100k` 2.27 ms mean, 4.45 ms p99, no spike, `material_100k` 2.11 ms mean, 4.30 ms p99, no spike, `templates_100k` 1.09 ms mean, 1.75 ms p99, no spike, `spikes` 6.80 ms mean, 17.77 ms p99, no spike. Microbenchmarks in `bench/` against the sky epic: the Sky extract cases 5 to 10% slower with the weather blend added to the per frame sky extraction, the command dispatch cases 6 to 10% slower in a module this epic did not touch.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- Blending the weather on top of the atmosphere tables rather than inside them keeps the sun moving under an overcast deck and costs nothing when the amounts are zero.
- A private blend component added by `EcsWith` lets a re-set target keep the current eased amounts instead of restarting the transition.

## Not in this epic
- Rain particles and wet surfaces beyond the wetness layer amount belong to `particles/rain`.
