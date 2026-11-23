// Slicing of the froxel grid of the fog volumes of a view, shared by froxel.wgsl and the blended
// draws that look the integrated grid up (fog_blend.wgsl).
#pragma once

#include "froxel.h"

// Share of the depth of a grid from view depth near_ to far_, 0 at the near plane and 1 at its
// far end, of a view depth, clamped to 0..1.
fn froxel_share_of(depth: f32, near_: f32, far_: f32, ortho: bool) -> f32 {
    let x = saturate((depth - near_) / max(far_ - near_, 1e-6));
    if (ortho) {
        return x;
    }

    let k = exp2(BOX_FROXEL_EXPONENT) - 1.0;
    return log2(1.0 + x * k) / BOX_FROXEL_EXPONENT;
}
