// L2 spherical harmonics irradiance, shared by the ambient of a view, the probe volume of the
// forward pass and the probe debug view.
#pragma once

// Irradiance in a direction from the L2 SH irradiance coefficients E_lm of a probe
// (Ramamoorthi and Hanrahan, Sloan's real basis), before the division by pi and the clamp.
fn sh_irradiance(e0: vec3f, e1: vec3f, e2: vec3f, e3: vec3f, e4: vec3f, e5: vec3f,
    e6: vec3f, e7: vec3f, e8: vec3f, d: vec3f) -> vec3f
{
    let band0 = e0 * 0.2820947918;
    let band1 = (e1 * d.y + e2 * d.z + e3 * d.x) * 0.4886025119;
    let band2 = (e4 * (d.x * d.y) + e5 * (d.y * d.z) + e7 * (d.x * d.z)) * 1.0925484306
        + e6 * (0.3153915653 * (3.0 * d.z * d.z - 1.0))
        + e8 * (0.5462742153 * (d.x * d.x - d.y * d.y));
    return band0 + band1 + band2;
}
