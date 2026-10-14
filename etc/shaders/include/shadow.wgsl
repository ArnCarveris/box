// Cascaded shadow map of the first directional light of the scene, read by the forward pass.
// A cascade covers a slice of the view depth; the lookup moves along the normal by the normal
// offset, adds the depth bias and filters with filter_size by filter_size bilinear comparisons.
// Depth is reversed: 1 is nearest the light, so a fragment is lit when its depth is at least
// the stored one. The last cascade fades out from shadow_params.x to shadow_params.y.
#pragma once

@group(0) @binding(3) var shadow_map: texture_depth_2d_array;
@group(0) @binding(4) var shadow_sampler: sampler_comparison;

// Fraction of light that reaches a position through one cascade.
fn shadow_cascade(index: u32, world: vec3f, normal: vec3f) -> f32 {
    let offset = world + normal * view.shadow_offset[index];
    let p = view.shadow_matrix[index] * vec4f(offset, 1.0);
    let uv = p.xy * vec2f(0.5, -0.5) + vec2f(0.5);
    if (any(uv < vec2f(0.0)) || any(uv > vec2f(1.0)) || p.z > 1.0) {
        return 1.0;
    }

    let depth = p.z + view.shadow_bias[index];
    let taps = i32(view.shadow_config.y);
    let first = -0.5 * f32(taps - 1);
    var sum = 0.0;
    for (var y = 0; y < taps; y++) {
        for (var x = 0; x < taps; x++) {
            let o = (vec2f(f32(x), f32(y)) + first) * view.shadow_params.z;
            sum += textureSampleCompareLevel(shadow_map, shadow_sampler, uv + o, index, depth);
        }
    }

    return sum / f32(max(taps * taps, 1));
}

// Fraction of the first directional light that reaches a position with a normal.
fn shadow_factor(world: vec3f, normal: vec3f) -> f32 {
    let count = view.shadow_config.x;
    let depth = -(view.view * vec4f(world, 1.0)).z;
    if (count == 0u || depth >= view.shadow_params.y) {
        return 1.0;
    }

    var index = count - 1u;
    for (var i = 0u; i < count; i++) {
        if (depth < view.shadow_split[i]) {
            index = i;
            break;
        }
    }

    let shadow = shadow_cascade(index, world, normal);
    let span = max(view.shadow_params.y - view.shadow_params.x, 1e-4);
    let fade = clamp((depth - view.shadow_params.x) / span, 0.0, 1.0);
    return mix(shadow, 1.0, fade);
}
