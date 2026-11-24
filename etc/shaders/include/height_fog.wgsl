// Exponential height fog: the density falls off exponentially above the base height and the
// optical depth along the ray from the near plane to the pixel is integrated analytically; a
// pixel without geometry integrates to infinity. The fog color is lit by the sun of the scene
// with a Henyey-Greenstein phase that peaks toward the sun, and by its ambient light.
//
// In a view that renders the Atmosphere of its scene a fog that refers to it takes its in scatter
// color from the sky instead: the ambient and the glow toward the main light, which the fog
// pass of atmosphere.wgsl wrote once for the view into a 2x1 image, the glow with the phase of
// the reference, Henyey-Greenstein with g the Mie anisotropy clamped to 0.92 or a broad lobe,
// whichever is larger.
//
// The fog pass (fog.wgsl) applies it to the image, and the apply draw of the fog volumes
// (fog_volume.wgsl) after the volumes when the view's first effect is the fog.
#pragma once

struct HeightFog {
    inv_view_proj: mat4x4f,
    // rgb: linear fog color, a: density
    color: vec4f,
    // falloff, base height, max opacity, 1 when lit
    params: vec4f,
    // xyz: direction toward the sun, w: 1 when the scene has one
    sun: vec4f,
    // rgb: sun radiance times its inscatter
    sun_color: vec4f,
    // rgb: ambient light times its inscatter
    ambient: vec4f,
    // 1 when the color comes from the atmosphere, phase g, sun and ambient inscatter
    atmos: vec4f,
};

const HEIGHT_FOG_ANISOTROPY: f32 = 0.3;

fn height_fog_world(hf: HeightFog, uv: vec2f, depth: f32) -> vec3f {
    let p = hf.inv_view_proj * vec4f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    return p.xyz / p.w;
}

// Henyey-Greenstein phase times 4 pi, so isotropic scattering is 1.
fn height_fog_phase(cos_theta: f32) -> f32 {
    let g = HEIGHT_FOG_ANISOTROPY;
    let d = max(1.0 + g * g - 2.0 * g * cos_theta, 1e-4);
    return (1.0 - g * g) / (d * sqrt(d));
}

// Phase of the glow of the sky toward the main light, at most 1 toward it.
fn height_fog_sky_phase(cos_theta: f32, g: f32) -> f32 {
    let d = max(1.0 + g * g - 2.0 * g * cos_theta, 1e-4);
    let hg = pow((1.0 - g) * (1.0 - g) / d, 1.5);
    let broad = pow(clamp(0.5 + 0.5 * cos_theta, 0.0, 1.0), 3.0);
    return clamp(max(hg, broad), 0.0, 1.0);
}

fn height_fog_optical_depth(hf: HeightFog, origin: vec3f, dir: vec3f, dist: f32, sky: bool)
    -> f32
{
    let f = hf.params.x;
    let base = hf.color.a * exp(clamp(-f * (origin.y - hf.params.y), -80.0, 80.0));
    let k = f * dir.y;
    if (sky) {
        return select(1e9, base / k, k > 1e-5);
    }

    if (abs(k * dist) < 1e-4) {
        return base * dist;
    }

    return base * (1.0 - exp(clamp(-k * dist, -80.0, 80.0))) / k;
}

// In scatter color (rgb) and share of the light of the pixel the fog replaces (a) at a clip
// depth of a uv of the image, no depth for the sky. sky_texture holds the ambient of the sky in
// texel 0 and the glow toward the main light in texel 1.
fn height_fog(hf: HeightFog, uv: vec2f, depth: f32, sky_texture: texture_2d<f32>) -> vec4f {
    let origin = height_fog_world(hf, uv, 1.0);
    let sky = depth <= 0.0;
    let target_pos = height_fog_world(hf, uv, max(depth, 1e-7));
    let ray = target_pos - origin;
    let dist = length(ray);
    let dir = ray / max(dist, 1e-6);
    let tau = height_fog_optical_depth(hf, origin, dir, dist, sky);
    let factor = min(hf.params.z, 1.0 - exp(-max(tau, 0.0)));
    var light = vec3f(1.0);
    if (hf.params.w > 0.5) {
        light = hf.ambient.rgb;
        if (hf.sun.w > 0.5) {
            light += hf.sun_color.rgb * height_fog_phase(dot(dir, hf.sun.xyz));
        }
    }

    var inscatter = hf.color.rgb * light;
    if (hf.atmos.x > 0.5) {
        let ambient = textureLoad(sky_texture, vec2i(0, 0), 0).rgb;
        let glow = textureLoad(sky_texture, vec2i(1, 0), 0).rgb;
        inscatter = ambient * hf.atmos.w;
        if (hf.sun.w > 0.5) {
            inscatter += glow * height_fog_sky_phase(dot(dir, hf.sun.xyz), hf.atmos.y) *
                hf.atmos.z;
        }
    }

    return vec4f(inscatter, factor);
}
