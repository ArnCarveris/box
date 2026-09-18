# Appearance over lifetime

Size, color, opacity and rotation curves over lifetime, circle, sprite and texture appearances, and blending.

## Details

Curves over normalised age for size, color, opacity and rotation are components on the emitter, evaluated inside the simulation pass so no per particle CPU work is needed. Particles render as camera facing quads showing a circle, a texture or an animated sprite sheet, using the blend modes from the material epic for smoke, fire, sparks and magic. Soft particles fade against the depth buffer so they do not cut into geometry.

Verification: unit tests cover curve evaluation at sampled points, sprite frame selection and the soft particle fade term. Visual scenes render smoke, fire, sparks and a magic effect as deliverables, and a benchmark reports the fill rate cost of a screen filling smoke effect.
