// Vertex stage of a triangle that covers the target, with uv (0, 0) at the top left.
#pragma once

struct FullscreenOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
};

@vertex fn vs(@builtin(vertex_index) i: u32) -> FullscreenOut {
    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    var o: FullscreenOut;
    o.pos = vec4f(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = vec2f(p.x, 1.0 - p.y);
    return o;
}
