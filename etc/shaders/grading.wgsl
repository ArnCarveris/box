// Color grading: white balance and exposure as one matrix, contrast, saturation, vibrance,
// lift/gamma/gain and a 2D strip LUT, in that order. A step at its default is skipped, so the
// defaults leave the image as is.
//
// The LUT at binding 4 is N slices of NxN side by side: red to the right within a slice, green
// downward, blue from slice to slice. It is sampled as stored, sRGB encoded, with bilinear
// filtering within the two nearest slices blended by blue, on the color clamped to 0..1 and
// encoded, and its result is decoded back to linear.
#include "include/effect.wgsl"
#include "include/grading.wgsl"

@group(0) @binding(2) var<uniform> grading: Grading;

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    let src = textureLoad(input_texture, vec2i(v.pos.xy), 0);
    return vec4f(grade(grading, src.rgb), src.a);
}
