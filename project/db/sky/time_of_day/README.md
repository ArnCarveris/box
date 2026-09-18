# Time of day

A simulated clock that drives the sun, moon, stars and atmosphere from hour, day of year and latitude.

## Details

TimeOfDay advances the hour by time_scale, computes the solar and lunar directions for the date, latitude and north offset, writes the rotations of the sun and moon light entities, rotates the star field and publishes a daylight factor other systems read. Writing the hour from a script or a command jumps the sky in the same frame, which makes reproducible captures trivial for agents. Light intensity and color follow the transmittance at the current sun elevation.

Verification: unit tests check sun elevation and azimuth against known values for a date and latitude, cover the wrap at midnight, time_scale and an immediate jump when hour is set. A visual test captures a full day at two hour steps into a contact sheet.
