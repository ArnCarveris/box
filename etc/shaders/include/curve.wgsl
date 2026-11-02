// Cubic Bezier evaluators of curves, with de Casteljau's algorithm. The C twins are
// box_curve_cubic and box_curve_cubic3 in src/curve/curve.c; a change to one is a change to both.
// Includes resolve from the folder of the including file first, so a file in this folder
// includes it as "curve.wgsl" and a shader in etc/shaders as "include/curve.wgsl".
#pragma once

fn curve_mix(a: f32, b: f32, t: f32) -> f32 {
    return a * (1.0 - t) + b * t;
}

fn curve_cubic(start: f32, control1: f32, control2: f32, end: f32, t: f32) -> f32 {
    let a = curve_mix(start, control1, t);
    let b = curve_mix(control1, control2, t);
    let c = curve_mix(control2, end, t);
    return curve_mix(curve_mix(a, b, t), curve_mix(b, c, t), t);
}

fn curve_mix3(a: vec3f, b: vec3f, t: f32) -> vec3f {
    return a * (1.0 - t) + b * t;
}

fn curve_cubic3(start: vec3f, control1: vec3f, control2: vec3f, end: vec3f, t: f32) -> vec3f {
    let a = curve_mix3(start, control1, t);
    let b = curve_mix3(control1, control2, t);
    let c = curve_mix3(control2, end, t);
    return curve_mix3(curve_mix3(a, b, t), curve_mix3(b, c, t), t);
}
