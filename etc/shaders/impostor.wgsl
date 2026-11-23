// Copies an impostor frame (render/gpu_impostor.c) into its cell of an atlas. The pass renders
// the cell with a viewport and a scissor over it, so the position of a fragment is the texel of
// the atlas and the texel of the frame is that minus the corner of the cell.
//
// A texel the frame covers (alpha above 0) keeps its color and alpha. An empty texel takes the
// average color of the covered texels of the nearest square ring around it, up to the dilation,
// with an alpha of 0: the mips and the filter of the atlas then blend the colors of the object
// into its border instead of the clear color.
#include "include/fullscreen.wgsl"

struct Params {
    // x, y: corner of the cell in the atlas, z: dilation in texels
    cell: vec4i,
};

@group(0) @binding(0) var frame: texture_2d<f32>;
@group(0) @binding(1) var<uniform> params: Params;

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    let size = vec2i(textureDimensions(frame));
    let p = clamp(vec2i(floor(v.pos.xy)) - params.cell.xy, vec2i(0), size - 1);
    let c = textureLoad(frame, p, 0);
    if (c.a > 0.0) {
        return c;
    }

    for (var r = 1; r <= params.cell.z; r ++) {
        var sum = vec3f(0.0);
        var count = 0.0;
        for (var dy = -r; dy <= r; dy ++) {
            let ring = dy == -r || dy == r;
            var dx = -r;
            while (dx <= r) {
                let q = p + vec2i(dx, dy);
                if (all(q >= vec2i(0)) && all(q < size)) {
                    let s = textureLoad(frame, q, 0);
                    if (s.a > 0.0) {
                        sum += s.rgb;
                        count += 1.0;
                    }
                }
                dx += select(2 * r, 1, ring);
            }
        }

        if (count > 0.0) {
            return vec4f(sum / count, 0.0);
        }
    }

    return vec4f(c.rgb, 0.0);
}
