// Wind over grass: the wave of the Wind at a point of a field of grass, a function of the local
// world x and z and of the wind of the frame only, so the blades (grass.wgsl) and the far field
// of a terrain can share it and every pass agrees on it. The wave reads the gust field of the
// Wind (wind_gust.wgsl) as the WindSway meshes and the particles with global wind do, so a gust
// front rolls over a field as it rolls through the plants beside it, and sways around that lean
// in the slow wave along the wind of wind.wgsl.
//
// The includer passes its GrassWind, which the CPU fills from the Wind of the frame
// (box_render_wind_frame) relative to the render origin of the packet: the offset of the gust
// field and the phase of the sway already take the origin into account.
#pragma once

#include "wind_gust.wgsl"

// The wind of a frame as a field of grass reads it (box_gpu_grass_params_t.wind_*)
struct GrassWind {
    field: vec4f,       // Normalized world x and z the wind blows toward, local x and z offset
                        // of the gust field
    strength: vec4f,    // Speed, gust, 1 over the gust scale, phase of the sway 0..1
    time: vec4f,        // Phase of the swing and flutter of the blades 0..1, the wind clock
                        // times BOX_RENDER_GRASS_WIND_RATE turns per second; yzw 0
};

const GRASS_WIND_TAU: f32 = 6.283185307179586;

// Wind speed at which the grass takes its full lean and flutter (BOX_WIND_FULL_SPEED)
const GRASS_WIND_FULL: f32 = 10.0;

// Relative amplitudes of the sway along and across the wind, as WIND_SWAY and WIND_CROSS of
// wind.wgsl (BOX_RENDER_WIND_SWAY and BOX_RENDER_WIND_CROSS)
const GRASS_WIND_SWAY: f32 = 0.1;
const GRASS_WIND_CROSS: f32 = 0.06;

// How much more the grass answers the gusts than the plants: the strength of the wind on the
// grass takes the gust factor g of the field as 1 + GRASS_WIND_GUST_GAIN * (g - 1), so the light
// grass leans over in a gust and rises in the lull behind it and the gusts roll over the field as
// waves at the places of the gusts of the plants
const GRASS_WIND_GUST_GAIN: f32 = 3.0;

// Lean in radians of a blade in a wind of strength s (the speed times the gust over
// GRASS_WIND_FULL): GRASS_WIND_LEAN * s / (1 + GRASS_WIND_SOFT * s), so the lean grows with the
// wind and levels off before the blades lie flat
const GRASS_WIND_LEAN: f32 = 1.1;
const GRASS_WIND_SOFT: f32 = 0.5;

// The wave of the wind at a local world x and z: xy the direction and strength the blades there
// lean toward, the strength in units of the full wind (speed times the gust factor raised by
// GRASS_WIND_GUST_GAIN, over GRASS_WIND_FULL),
// z the gust factor between 1 - gust and 1 + gust, w how much the gust stirs the blades, 0..1,
// the gust factor over its top squared. A calm leaves xy and w 0.
fn grass_wind_wave(xz: vec2f, w: GrassWind) -> vec4f {
    let speed = w.strength.x;
    if (speed <= 0.0) {
        return vec4f(0.0, 0.0, 1.0, 0.0);
    }

    let dir = w.field.xy;
    let gust = w.strength.y;
    let g = wind_gust(xz, w.field.zw, dir, w.strength.z, gust);
    let s = speed * max(1.0 + GRASS_WIND_GUST_GAIN * (g - 1.0), 0.0) / GRASS_WIND_FULL;
    let phase = GRASS_WIND_TAU * w.strength.w - 0.5 * dot(xz, dir) * w.strength.z;
    let along = 1.0 + GRASS_WIND_SWAY * sin(phase);
    let cross = GRASS_WIND_CROSS * sin(2.0 * phase + 1.3);
    let e = clamp(g / (1.0 + gust), 0.0, 1.0);
    return vec4f(dir * (s * along) + vec2f(-dir.y, dir.x) * (s * cross), g, e * e);
}

// Lean in radians of a blade in a wind of a strength in units of the full wind.
fn grass_wind_lean(strength: f32) -> f32 {
    return GRASS_WIND_LEAN * strength / (1.0 + GRASS_WIND_SOFT * strength);
}
