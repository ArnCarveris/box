// Falloff of the point and spot lights, shared by the forward pass and the fog volumes. Needs
// Light of lights.wgsl.
#pragma once

// Distance below which a point or spot light stops getting brighter, so a surface at the light
// stays finite.
const LIGHT_MIN_DISTANCE: f32 = 0.01;

// Radiance of a point or spot light at a position: inverse square falloff, smoothly windowed
// to zero at the range as in Frostbite's smoothDistanceAtt, times the smooth cone of a spot.
fn attenuation(light: Light, l: vec3f, dist: f32) -> f32 {
    let ratio = dist / max(light.range, 1e-4);
    let window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    let cone = clamp(dot(-l, light.direction) * light.spot_scale + light.spot_offset, 0.0, 1.0);
    let near_ = max(dist, LIGHT_MIN_DISTANCE);
    return window * window / (near_ * near_) * cone * cone;
}
