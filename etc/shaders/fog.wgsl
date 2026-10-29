// Exponential height fog: the density falls off exponentially above the base height and the
// optical depth along the ray from the near plane to the pixel is integrated analytically; a
// pixel without geometry integrates to infinity. The fog color is lit by the sun of the scene
// with a Henyey-Greenstein phase that peaks toward the sun, and by its ambient light.
//
// In a view that renders the Atmosphere of its scene a fog that refers to it takes its in scatter
// color from the sky instead: the ambient and the glow toward the main light, which the fog
// pass of atmosphere.wgsl wrote once for the view into the 2x1 image at binding 4, the glow
// with the phase of the reference, Henyey-Greenstein with g the Mie anisotropy clamped to 0.92
// or a broad lobe, whichever is larger.
#include "include/effect.wgsl"

struct Fog {
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

@group(0) @binding(2) var<uniform> fog: Fog;
@group(0) @binding(3) var depth_texture: texture_depth_2d;
// Texel 0: the ambient of the sky, texel 1: the glow toward the main light
@group(0) @binding(4) var sky_texture: texture_2d<f32>;

const ANISOTROPY: f32 = 0.3;

fn world_at(uv: vec2f, depth: f32) -> vec4f {
    return fog.inv_view_proj * vec4f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
}

// Henyey-Greenstein phase times 4 pi, so isotropic scattering is 1.
fn phase(cos_theta: f32) -> f32 {
    let g = ANISOTROPY;
    let d = max(1.0 + g * g - 2.0 * g * cos_theta, 1e-4);
    return (1.0 - g * g) / (d * sqrt(d));
}

// Phase of the glow of the sky toward the main light, at most 1 toward it.
fn sky_phase(cos_theta: f32, g: f32) -> f32 {
    let d = max(1.0 + g * g - 2.0 * g * cos_theta, 1e-4);
    let hg = pow((1.0 - g) * (1.0 - g) / d, 1.5);
    let broad = pow(clamp(0.5 + 0.5 * cos_theta, 0.0, 1.0), 3.0);
    return clamp(max(hg, broad), 0.0, 1.0);
}

fn optical_depth(origin: vec3f, dir: vec3f, dist: f32, sky: bool) -> f32 {
    let f = fog.params.x;
    let base = fog.color.a * exp(clamp(-f * (origin.y - fog.params.y), -80.0, 80.0));
    let k = f * dir.y;
    if (sky) {
        return select(1e9, base / k, k > 1e-5);
    }

    if (abs(k * dist) < 1e-4) {
        return base * dist;
    }

    return base * (1.0 - exp(clamp(-k * dist, -80.0, 80.0))) / k;
}

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    let texel = vec2i(v.pos.xy);
    let src = textureLoad(input_texture, texel, 0);
    let depth = textureLoad(depth_texture, texel, 0);
    let near = world_at(v.uv, 1.0);
    let origin = near.xyz / near.w;
    let sky = depth <= 0.0;
    let far = world_at(v.uv, max(depth, 1e-7));
    let target_pos = far.xyz / far.w;
    let ray = target_pos - origin;
    let dist = length(ray);
    let dir = ray / max(dist, 1e-6);
    let tau = optical_depth(origin, dir, dist, sky);
    let factor = min(fog.params.z, 1.0 - exp(-max(tau, 0.0)));
    var light = vec3f(1.0);
    if (fog.params.w > 0.5) {
        light = fog.ambient.rgb;
        if (fog.sun.w > 0.5) {
            light += fog.sun_color.rgb * phase(dot(dir, fog.sun.xyz));
        }
    }

    var inscatter = fog.color.rgb * light;
    if (fog.atmos.x > 0.5) {
        let ambient = textureLoad(sky_texture, vec2i(0, 0), 0).rgb;
        let glow = textureLoad(sky_texture, vec2i(1, 0), 0).rgb;
        inscatter = ambient * fog.atmos.w;
        if (fog.sun.w > 0.5) {
            inscatter += glow * sky_phase(dot(dir, fog.sun.xyz), fog.atmos.y) * fog.atmos.z;
        }
    }

    let a = src.a * (1.0 - factor) + factor;
    return vec4f(src.rgb * (1.0 - factor) + inscatter * factor, a);
}
