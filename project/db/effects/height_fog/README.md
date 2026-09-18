# Height fog

Height based fog integrated along the view ray, with the parameters the sky epic synchronises against.

## Details

Fog density falls off exponentially with world height and is integrated along the view ray reconstructed from depth, with color, density, falloff, a start distance and a maximum opacity on a Fog component. The effect runs before tone mapping and reads the same depth target as the other effects. The sky epic later drives the color and density from the atmosphere.

Verification: unit tests compare the analytic integral against hand computed values for several camera heights and cover the component defaults. A visual scene renders a valley at several densities and fog heights, and a benchmark reports the pass cost at 1080p.
