# Rain as a weather state

`Weather.rain` is a whole weather now. The blended amount creates one emitter under `box.sky`,
a box volume that follows the camera of the first view, and drives it every frame: the rate, the
size of a drop and the wind rise with it, the drops are spawned at their terminal velocity so the
wind bends the whole column instead of only what has been falling a while, gravity holds them
there against the drag and a ground collision kills them where they land. The same amount is
added to the overcast and the fog the sky, the light of the view and the fog pass are extracted
with, and to the wetness material layer, so one number greys the sky, thickens the air, wets the
surfaces and fills it with drops, all over the transition of the weather. A rain that starts
fills its column with a burst so the first frame looks like the hundredth, a rain that clears
stops the emitter and leaves the drops in the air, and a world without `box.particle` keeps
everything but the drops. The drops are drawn by a new `ParticleAppearance.stretch`: the quad is
laid over the direction the particle moves in on the screen and grown along it with its speed,
which costs nothing when it is zero and leaves every other pool placed by the expression it had.
Verified: 532 tests (21 new: a `Rain` suite of 17 in `test/particle` and 4 streak cases in
`Appearance`), lint 0, 87 scenes pass with `weather_rain` and `weather_rain_heavy` new and the
other 85 bit identical, `bench/particle` green at 13 cases with `Extract.rain_column` at 249 ns
per emitter. Left out: splashes where a drop lands, and rain that stops under a roof.
