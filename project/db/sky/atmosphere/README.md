# Atmosphere rendering

A physically based atmosphere drawn behind the scene, with aerial perspective on distant geometry.

## Details

Atmosphere holds the planet, Rayleigh, Mie, ozone and ground parameters from the datamodel; the renderer precomputes transmittance and multiple scattering LUTs, renders a sky view LUT per frame and draws the sky where the depth buffer is empty. Aerial perspective applies the same LUTs to geometry using depth, so distance haze matches the sky. The expensive LUTs are only recomputed when parameters change or the sun moves far enough to matter.

Verification: unit tests cover the component, LUT invalidation thresholds and the sun direction taken from the referenced light entity. Visual scenes capture the sky at several sun elevations including sunrise and noon, and a benchmark reports the LUT and sky pass cost.
