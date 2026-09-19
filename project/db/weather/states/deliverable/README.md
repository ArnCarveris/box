# Weather states

A `Weather` singleton holds overcast, fog and rain as continuous amounts with a
`transition` in seconds; `current` is what the frame is drawn with, snapped on
the first frame and eased after it, so a command lands over a second.

Overcast blends the sky view radiance toward a flat grey deck, fades the sun
disk, the moon and the stars, greys the aerial haze and the fog color, and
flattens the light of the view toward the grey of its own luminance. Fog
multiplies the sky driven fog density up to fourteen times. Rain drives the
amount of the wetness material layer. The atmosphere, its medium and both
tables stay the clear sky they were, so the sun keeps moving under overcast.

Verified: 21 new `Weather` cases, 358 `render`, 140 `view`, 27 `core` and 30
`shader` tests green, lint 0 problems, all 58 visual scenes pass with the other
56 bit identical. `weather_overcast.png` and `weather_fog.png` are the scenes.

Left out: the rain emitter and the heavy rain particle budget benchmark, which
belong to `particles/rain`.
