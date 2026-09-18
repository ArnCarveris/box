# Auto exposure and tone mapping

- `AutoExposure {speed_up, speed_down, min_ev, max_ev, target, compensation, pinned}` and
  `ToneMapping {white, dither}` sit on `box.effects.stack.auto_exposure` (order 850) and
  `.tonemap` (900), both off, so a scene turns one on with `Effect: {enabled: true}` and the 26
  existing scenes stay bit identical; a parameter component may now hold bools and structs.
- The `exposure` input is work the frame does for a pass: a compute pass bins its log luminance
  into 256 bins (bin 0 black, the rest 16 stops from 2^-10), a second averages them and steps
  the exposure by at most speed_up or speed_down stops per second towards `log2(target/average)`,
  clamps it to the EV range and scales by 2^compensation; the state lives in a buffer of the
  view and `pinned` freezes it.
- The tone map is the last pass: the Khronos PBR Neutral curve, the closest analytic stand in for
  the Tony McMapface LUT (hue preserving, highlights desaturating to white), then an ordered 8 by
  8 Bayer dither of one output step. Targets between two passes are RGBA16F.
- 22 `Effects`, 112 `view`, 24 `shader` and 223 `render` cases green. `Effects.extract_chain` is
  0.81 us for eight passes against 0.74 and nothing else in `bench.json` leaves the noise. At
  1280x720 over 300 headless frames the tone map costs 0.22 ms mean CPU frame time against 0.18
  off and the auto exposure 0.27 against 0.18, the median of five runs. `tonemap.png` clips none
  of its 518400 pixels where the curve off clips 230183; `auto_exposure.png` lifts 36 to 151.
