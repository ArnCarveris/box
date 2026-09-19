# Time of day

- `TimeOfDay` (`box.sky`) turns an hour, a day of the year, a latitude and a north offset
  into the direction of the sun and the moon and the orientation of the celestial sphere,
  and writes the `Rotation` of the three entities every frame. It names them itself or takes
  them from the atmosphere, advances the hour by `time_scale` and wraps into the next day.
- The declination follows the axial tilt over the year and the hour angle the turn of the
  planet, so the sun rises east, stands at the latitude at noon and tilts by the tilt at the
  solstices. The moon trails the sun by `moon_position_offset` hours around the planet and
  along the ecliptic, which makes twelve hours a full moon and zero a new one.
- Every value is derived from the component in the same frame, so a script, the `set` command
  of a terminal or a test that writes `hour` jumps the sky at once; `daylight` is published
  for other systems, 0 below six degrees of twilight and 1 above it.
- 28 new `Clock` tests cover the elevation and azimuth of known dates, the wrap at midnight
  and at the end of the year, the scale, the jump, the phase and the pole of the star field.
  455 `render` and `view` tests green, lint 0. `time_of_day_morning`, `_noon` and `_night`
  capture one scene at three hours; `day_contact_sheet.png` is the same scene every two hours.
- `Sky.advance_the_clock` is 0.69 us per frame, the whole of `ecs_progress` included.
