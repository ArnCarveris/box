#include "include/lights.wgsl"
#include "include/fullscreen.wgsl"

const OVERDRAW_STOPS: u32 = 9u;

override mode: u32 = 1u;

@group(0) @binding(0) var<uniform> view: View;

@group(1) @binding(3) var overdraw_count: texture_2d<f32>;

fn overdraw_linear(c: vec3f) -> vec3f {
    return select(pow((c + 0.055) / 1.055, vec3f(2.4)), c / 12.92, c <= vec3f(0.04045));
}

fn overdraw_ramp(layers: f32) -> vec3f {
    var stops = array<vec3f, OVERDRAW_STOPS>(
        vec3f(0.0, 0.0, 0.0),
        vec3f(0.05, 0.1, 0.45),
        vec3f(0.0, 0.45, 0.85),
        vec3f(0.0, 0.75, 0.45),
        vec3f(0.55, 0.85, 0.0),
        vec3f(1.0, 0.85, 0.0),
        vec3f(1.0, 0.5, 0.0),
        vec3f(0.9, 0.1, 0.1),
        vec3f(1.0, 1.0, 1.0)
    );
    let n = u32(clamp(round(layers), 0.0, f32(OVERDRAW_STOPS - 1u)));
    return overdraw_linear(stops[n]);
}

// The count holds the fragments of the plain meshes in red and of the alpha tested meshes in
// green; mode is the OverdrawMode of the view: 1 shows both, 2 the plain, 3 the alpha tested.
@fragment fn fs(f: FullscreenOut) -> @location(0) vec4f {
    let count = textureLoad(overdraw_count, vec2i(f.pos.xy), 0).rg;
    var layers = count.r + count.g;
    if (mode == 2u) {
        layers = count.r;
    } else if (mode == 3u) {
        layers = count.g;
    }

    return vec4f(overdraw_ramp(layers), 1.0);
}
