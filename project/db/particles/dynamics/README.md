# Forces and randomisation

Gravity, wind and drag forces plus per particle randomisation of direction, speed, size, color and lifetime.

## Details

Forces are entities referenced by an emitter or global to the scene, so a project can add wind that affects every emitter, while drag is a per emitter coefficient. Randomisation is expressed as a range or a variance per property and evaluated with a per particle hash, which keeps the simulation stateless and reproducible from a seed. Forces are applied in the same pass as integration so there is no extra dispatch.

Verification: unit tests check the integrator against analytic motion under gravity and drag, cover each randomised property and that a fixed seed reproduces identical particles. Visual scenes show smoke bending in wind and sparks with speed variance, and a benchmark compares three active forces against none.
