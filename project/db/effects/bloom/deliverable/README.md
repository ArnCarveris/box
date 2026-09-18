# Bloom

- `Bloom {intensity, radius, knee, firefly_clamp}` sits on `box.effects.stack.bloom`
  (order 800, before the tone map), off by default, so a scene turns it on with
  `Effect: {enabled: true}` and every existing capture stays bit identical.
- The `pyramid` input asks the frame for a blurred mip chain of the image the pass reads: the
  thirteen tap downsample and eight tap tent upsample of the milk glass chain, mixed back up at
  a constant of 0.5, in RGBA16F from half the view down while the coarsest level still holds 8
  texels (6 levels at 1280x720, 8 at most); `radius` picks how many feed back.
- There is no threshold: every texel blooms and `intensity` mixes the pyramid into the image,
  which keeps the energy of the frame, while `knee` is an optional soft quadratic rolloff and
  never a cut off. The first downsample decodes to linear, clamps a texel above `firefly_clamp`
  back to that luminance and weights its five tap groups by the Karis average 1/(1 + luma), so
  one bright texel cannot carry a level; the clamp bites past white, which the float targets keep.
- 27 `Effects` cases and 112 `view` green and `bench.json` unchanged against the task before
  it. At 1280x720 over 300 headless frames the pass costs 0.47 ms mean CPU frame time against
  0.17 off, the median of five runs, its eleven pyramid passes included. `bloom.png` is four
  emissive boxes at strengths 0.6 to 40 and a 200 strength spark in a dark room, where 48431
  pixels change against the same scene with the effect off.
