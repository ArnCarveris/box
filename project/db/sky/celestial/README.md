# Sun, moon and stars

A sun disk, a moon with correct phases and a star field drawn into the sky.

## Details

The sky pass draws a sun disk with a configurable angular radius and intensity and a moon disk lit by the sun, so the phase follows the sun and moon directions rather than being authored. Stars are an entity with a Stars component and a rotation, drawn as a procedural or textured field that fades as the sky brightens. The moon and stars use CelestialLight for their top of atmosphere intensity and color.

Verification: unit tests compute the phase term from known sun and moon directions, cover the fade curve and the star rotation. Visual scenes capture a full moon, a crescent and a clear night with stars, kept as deliverables.
