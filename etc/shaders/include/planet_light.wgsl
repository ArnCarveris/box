// Sunlight of the planets on their meshes (PlanetAtmosphere with Scene.planet_light): the
// instances at or below the entity of a planet carry its planet light slot, and the directional
// light along the direction of its sun reaches their fragments through the transmittance of the
// atmosphere and the fade into the shadow of the planet, as the shell lights its air
// (planet_light_sun). Other lights, the ambient and the image based lighting are unchanged.
//
// The transmittance LUTs of every planet are the layers of one array, read as a storage texture
// with a bilinear filter of its own, since the sampled textures of the mesh pipelines are at the
// limit of the default device.
#pragma once

// Planet light slots, slot 0 is none (BOX_RENDER_PLANET_LIGHT_MAX).
const PLANET_LIGHTS: u32 = 64u;

// Smallest cosine between a directional light and the sun of a planet for the light to be the
// sun.
const PLANET_SUN_COS: f32 = 0.9999;

// Width in cosine of the fade of the light into the shadow of the planet, either side of its
// horizon, as earth_shadow of the shells.
const PLANET_SHADOW_FADE: f32 = 0.02;

struct PlanetLight {
    // xyz: world position of the center, w: km per world unit, 0 for an empty slot
    center: vec4f,
    // x: bottom radius, y: top radius in km, z: layer of its transmittance LUT
    radii: vec4f,
    // xyz: direction toward the sun
    sun: vec4f,
};

@group(0) @binding(22) var<uniform> planet_lights: array<PlanetLight, PLANET_LIGHTS>;
@group(0) @binding(23) var planet_luts: texture_storage_2d_array<rgba16float, read>;

// The sun of the planet of a fragment and what reaches the fragment of it.
struct PlanetSun {
    direction: vec3f,
    transmittance: vec3f,
};

// Planet light slot of the fragment being shaded, set by the fragment stage before it lights
// the fragment, 0 for none.
var<private> fragment_planet: u32 = 0u;

// Texture coordinates of the transmittance LUT at a radius and a cosine of the zenith angle, as
// transmittance_to_uv of atmosphere_common.wgsl.
fn planet_transmittance_uv(rb: f32, rt: f32, r: f32, mu: f32) -> vec2f {
    let h = sqrt(max(0.0, rt * rt - rb * rb));
    let rho = sqrt(max(0.0, r * r - rb * rb));
    let disc = max(0.0, r * r * (mu * mu - 1.0) + rt * rt);
    let d = max(0.0, -r * mu + sqrt(disc));
    let d_min = rt - r;
    let d_max = rho + h;
    var x_mu = 0.0;
    if (d_max > d_min + 1e-6) {
        x_mu = (d - d_min) / (d_max - d_min);
    }

    let x_r = rho / max(h, 1e-6);
    return vec2f(clamp(x_mu, 0.0, 1.0), clamp(x_r, 0.0, 1.0));
}

// The transmittance LUT of a layer at texture coordinates, filtered bilinearly between the
// texel centers as a linear clamp sampler does.
fn planet_transmittance(layer: i32, uv: vec2f) -> vec3f {
    let size = vec2i(textureDimensions(planet_luts));
    let p = clamp(uv * vec2f(size) - 0.5, vec2f(0.0), vec2f(size - 1));
    let i = vec2i(floor(p));
    let j = min(i + 1, size - 1);
    let w = p - vec2f(i);
    let a = textureLoad(planet_luts, i, layer).rgb;
    let b = textureLoad(planet_luts, vec2i(j.x, i.y), layer).rgb;
    let c = textureLoad(planet_luts, vec2i(i.x, j.y), layer).rgb;
    let d = textureLoad(planet_luts, j, layer).rgb;
    return mix(mix(a, b, w.x), mix(c, d, w.x), w.y);
}

// The sun of a planet light slot at a world position: the transmittance of the LUT at
// r = max(|p|, bottom radius) and mu = dot(p / |p|, sun), with p the position from the center
// in km, times the fade into the shadow of the planet. A direction of zero for no slot or an
// empty one, which no light matches.
fn planet_light_sun(slot: u32, world: vec3f) -> PlanetSun {
    var result = PlanetSun(vec3f(0.0), vec3f(1.0));
    if (slot == 0u || slot >= PLANET_LIGHTS) {
        return result;
    }

    let planet = planet_lights[slot];
    if (!(planet.center.w > 0.0)) {
        return result;
    }

    let p = (world - planet.center.xyz) * planet.center.w;
    let len = length(p);
    let rb = planet.radii.x;
    let r = max(len, rb);
    var up = vec3f(0.0, 1.0, 0.0);
    if (len > 1e-6) {
        up = p / len;
    }

    let mu = clamp(dot(up, planet.sun.xyz), -1.0, 1.0);
    let horizon = -sqrt(max(0.0, 1.0 - (rb * rb) / max(r * r, 1e-6)));
    let shadow = smoothstep(horizon - PLANET_SHADOW_FADE, horizon + PLANET_SHADOW_FADE, mu);
    let uv = planet_transmittance_uv(rb, planet.radii.y, r, mu);
    result.direction = planet.sun.xyz;
    result.transmittance = max(planet_transmittance(i32(planet.radii.z), uv), vec3f(0.0)) *
        shadow;
    return result;
}

// Factor of the radiance of a directional light toward l: the transmittance of the sun of the
// planet when l is its direction, else 1.
fn planet_light_factor(sun: PlanetSun, l: vec3f) -> vec3f {
    if (dot(l, sun.direction) > PLANET_SUN_COS) {
        return sun.transmittance;
    }

    return vec3f(1.0);
}
