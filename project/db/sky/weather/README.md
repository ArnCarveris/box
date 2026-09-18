# Weather

Overcast, fog and rain weather states that blend on top of the atmosphere.

## Details

A Weather component holds overcast, fog and rain amounts: overcast blends the sky towards a plain color and flattens the sun, fog raises the height fog density and rain drives a particle emitter and a wetness material layer. The values are continuous and transition smoothly so an agent can set one from a command and still get a stable frame. The atmosphere remains the base, so the sun keeps moving under overcast.

Verification: unit tests cover the blend between clear and overcast, the fog coupling, the emitter and wetness driving and the transition behaviour. Visual scenes capture clear, overcast, foggy and rainy versions of one scene, and a benchmark checks that heavy rain stays inside the particle budget.
