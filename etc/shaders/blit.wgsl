// Scales the scene texture of a target to the presented image.
#include "include/fullscreen.wgsl"

@group(0) @binding(0) var scene: texture_2d<f32>;
@group(0) @binding(1) var scene_sampler: sampler;

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    return textureSample(scene, scene_sampler, v.uv);
}
