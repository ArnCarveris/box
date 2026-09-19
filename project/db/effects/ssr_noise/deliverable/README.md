# SSR miss noise

- The cause was the hit rule, not the fallback. A step counted as a hit only when it landed
  behind the surface by less than the thickness, so a ray crossing a steeply receding surface
  jumped from in front of it to far behind it in one step and found nothing at all. Which
  pixels did that was decided by the per pixel jitter on the step offset, and with no temporal
  or spatial resolve behind the pass that turned into an ordered lattice of single missed
  pixels showing the sky over otherwise solid reflections.
- A step is now a crossing when the ray passed behind the surface while the step before it was
  still in front of it, which finds a hit from the change of side alone whatever the step
  phase, and the crossing is bisected as before. The thickness became the tolerance of the
  confidence instead of the hit test: how far behind the refined crossing landed, measured
  against the distance the ray and the surface both moved over that step, fades the hit into
  the prefiltered environment rather than stepping to it. The edge fade now follows the whole
  path of a ray instead of the texel it hit, a ray fades out over the last third of its span,
  and the step offset is a fixed half step, because a per pixel offset has nothing to resolve
  it. `renderer/ssr.h` carries the same rules for the tests.
- `python3 tools/ssr_check.py --env <name>` renders the scene and counts the pixels of the
  floor that keep the sky while at most one of their eight neighbours does. That count falls
  from 1185 to 56 of 183189 floor pixels, against 31 for the same scene with the effect off,
  which is the dither of the tone map alone.
- 46 `Effects` and 136 `view` cases green, every scene matches, and at 1280x720 over 300
  headless frames the pass costs 0.271 ms mean CPU frame time against 0.198 with the effect
  off, the median of five runs, against 0.292 before. `ssr.png` is the re-baselined scene.
