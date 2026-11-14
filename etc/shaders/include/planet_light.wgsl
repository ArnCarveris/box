// Sunlight of the planets on their meshes (PlanetAtmosphere with Scene.planet_light): the
// instances at or below the entity of a planet carry its planet light slot, and the directional
// light along the direction of its sun reaches their fragments through the transmittance of the
// atmosphere and the penumbra of the shadow of the planet from the angular radius of the sun
// disk (planet_light_sun). The sun also lights them through the sky of the atmosphere: the
// irradiance of the sky at their altitude and sun angle, from the sky irradiance LUT of the
// planet scaled by the intensity of its shell, on the hemisphere around their normal
// (planet_light_sky), so the ground and the clouds under the twilight glow are softly lit by the
// air. Other lights, the ambient and the image based lighting are unchanged.
//
// The transmittance and sky irradiance LUTs of every planet are a layer of one array, the
// transmittance LUT at its top and the sky irradiance LUT below it, read as a storage texture
// with a bilinear filter of its own, since the sampled textures of the mesh pipelines are at
// the limit of the default device.
#pragma once

// Planet light slots, slot 0 is none (BOX_RENDER_PLANET_LIGHT_MAX).
const PLANET_LIGHTS: u32 = 64u;

// Smallest cosine between a directional light and the sun of a planet for the light to be the
// sun.
const PLANET_SUN_COS: f32 = 0.9999;

struct PlanetLight {
    // xyz: world position of the center, w: km per world unit, 0 for an empty slot
    center: vec4f,
    // x: bottom radius, y: top radius in km, z: layer of its LUTs, w: scale of its sky light
    radii: vec4f,
    // xyz: direction toward the sun, w: angular radius of the sun disk in radians
    sun: vec4f,
};

@group(0) @binding(22) var<uniform> planet_lights: array<PlanetLight, PLANET_LIGHTS>;
@group(0) @binding(23) var planet_luts: texture_storage_2d_array<rgba16float, read>;

// Texels of the transmittance LUT at the top of a layer (BOX_RENDER_ATMOS_TRANSMITTANCE_WIDTH
// and BOX_RENDER_ATMOS_TRANSMITTANCE_HEIGHT) and of the sky irradiance LUT below it
// (BOX_RENDER_ATMOS_IRRADIANCE_WIDTH and BOX_RENDER_ATMOS_IRRADIANCE_HEIGHT).
const PLANET_TRANSMITTANCE_SIZE: vec2i = vec2i(256, 64);
const PLANET_IRRADIANCE_SIZE: vec2i = vec2i(64, 16);

// The sun of the planet of a fragment and what reaches the fragment of it: directly through the
// transmittance, and through the sky as the irradiance of a surface facing up, per unit of sun
// irradiance.
struct PlanetSun {
    direction: vec3f,
    transmittance: vec3f,
    up: vec3f,
    sky: vec3f,
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

// A LUT of size texels at row top of a layer at texture coordinates, filtered bilinearly
// between the texel centers as a linear clamp sampler does.
fn planet_lut(layer: i32, top: i32, uv: vec2f, size: vec2i) -> vec3f {
    let p = clamp(uv * vec2f(size) - 0.5, vec2f(0.0), vec2f(size - 1));
    let f = vec2i(floor(p));
    let i = f + vec2i(0, top);
    let j = min(f + 1, size - 1) + vec2i(0, top);
    let w = p - vec2f(f);
    let a = textureLoad(planet_luts, i, layer).rgb;
    let b = textureLoad(planet_luts, vec2i(j.x, i.y), layer).rgb;
    let c = textureLoad(planet_luts, vec2i(i.x, j.y), layer).rgb;
    let d = textureLoad(planet_luts, j, layer).rgb;
    return mix(mix(a, b, w.x), mix(c, d, w.x), w.y);
}

// The transmittance LUT of a layer at texture coordinates.
fn planet_transmittance(layer: i32, uv: vec2f) -> vec3f {
    return planet_lut(layer, 0, uv, PLANET_TRANSMITTANCE_SIZE);
}

// The sky irradiance LUT below the transmittance LUT of a layer at a radius and a cosine of the
// sun zenith angle, as multi_to_uv of atmosphere_common.wgsl.
fn planet_irradiance(layer: i32, rb: f32, rt: f32, r: f32, mu: f32) -> vec3f {
    let uv = vec2f(clamp(0.5 + 0.5 * mu, 0.0, 1.0),
        clamp((r - rb) / max(rt - rb, 1e-6), 0.0, 1.0));
    return planet_lut(layer, PLANET_TRANSMITTANCE_SIZE.y, uv, PLANET_IRRADIANCE_SIZE);
}

// Fraction of a uniform disk of angular radius radius whose center is elevation above a
// straight horizon that is above it: the area of the circular segment, 1 / 2 at the horizon. A
// disk of radius 0 is a step at the horizon.
fn planet_disk_visible(elevation: f32, radius: f32) -> f32 {
    if (!(radius > 0.0)) {
        return select(0.0, 1.0, elevation >= 0.0);
    }

    let x = clamp(elevation / radius, -1.0, 1.0);
    return 1.0 - (acos(x) - x * sqrt(max(0.0, 1.0 - x * x))) / PI;
}

// The sun of a planet light slot at a world position: the transmittance of the LUT at
// r = max(|p|, bottom radius) and mu = dot(p / |p|, sun), with p the position from the center
// in km, times the penumbra of the shadow of the planet: the fraction of the sun disk above the
// horizon of the planet at r (planet_disk_visible), and the sky irradiance of the LUT at r and
// mu around up, p / |p|, times the scale of the sky light. A direction of zero for no slot or an empty one, which no light
// matches.
fn planet_light_sun(slot: u32, world: vec3f) -> PlanetSun {
    var result = PlanetSun(vec3f(0.0), vec3f(1.0), vec3f(0.0, 1.0, 0.0), vec3f(0.0));
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
    let shadow = planet_disk_visible(acos(horizon) - acos(mu), planet.sun.w);
    let uv = planet_transmittance_uv(rb, planet.radii.y, r, mu);
    let layer = i32(planet.radii.z);
    result.direction = planet.sun.xyz;
    result.transmittance = max(planet_transmittance(layer, uv), vec3f(0.0)) * shadow;
    result.up = up;
    result.sky = max(planet_irradiance(layer, rb, planet.radii.y, r, mu), vec3f(0.0)) *
        planet.radii.w;
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

// Sky irradiance of a directional light toward l on a surface of normal n, per unit of its
// radiance: that of the sun of the planet when l is its direction, weighted by the part of the
// sky above the surface, (1 + dot(n, up)) / 2 of a uniform sky; else 0.
fn planet_light_sky(sun: PlanetSun, l: vec3f, n: vec3f) -> vec3f {
    if (dot(l, sun.direction) > PLANET_SUN_COS) {
        return sun.sky * (0.5 + 0.5 * dot(n, sun.up));
    }

    return vec3f(0.0);
}
