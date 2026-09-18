# Screen space reflections

Screen space reflections weighted by roughness, falling back to the environment on a miss.

## Details

SSR marches the depth buffer per pixel, weights the hit by material roughness and fades at screen edges and on misses into the IBL environment so there is no hard boundary. Step count, thickness, maximum roughness and intensity are fields on an Ssr component. Rough reflections sample a blurred color mip instead of tracing more rays.

Verification: unit tests cover the component, the roughness weighting function and the edge fade. A visual scene renders a wet floor over a roughness sweep with SSR on and off, and a benchmark reports the pass cost at half and full resolution.
