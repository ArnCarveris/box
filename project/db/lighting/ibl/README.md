# Image based lighting

Environment lighting from a constant color or a cubemap, prefiltered per roughness for diffuse and specular.

## Details

EnvironmentLight on the world or on a view holds a color, a cubemap texture entity or a source entity that renders one, which is how the sky epic later supplies the atmosphere. The renderer prefilters the environment into a roughness mip chain for specular and an irradiance representation for diffuse, and the surface shader combines both with the PBR parameters. Prefiltering only reruns when the source changes.

Verification: unit tests cover the component, source selection and prefilter invalidation. A visual scene renders a grid of spheres over roughness and metallic under a colored environment, which is the standard PBR check, and a benchmark reports the prefilter cost when the environment changes every frame.
