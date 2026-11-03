// Infinite grid of the debug pass of a view (GridSettings): a triangle covers the target and
// every fragment casts the ray of its pixel from the near plane through inv_view_proj, which
// works for an orthographic camera too, onto the plane y = height. A ray that does not hit the
// plane in front of the camera discards; a hit writes its own depth, so the depth of the view
// (compare Greater, no writes) hides it behind geometry and it stays on its plane.
//
// The lines are anti aliased with screen derivatives as Ben Golus's "pristine grid": a line
// keeps its width in pixels close up, thinner than a pixel it fades by its coverage instead of
// breaking up, and where a cell gets under a pixel it turns into its average coverage, so there
// is no moire. Minor and major lines fade out besides where their cells get under a few pixels.
// The world X axis (z = 0) is red and the Z axis (x = 0) blue, as wide as the major lines.
//
// The alpha fades out between half the fade distance and the fade distance from the camera and
// for grazing rays near the horizon. The output is premultiplied and linear, the colors of the
// uniform are decoded on the CPU.
#include "include/lights.wgsl"
#include "include/fullscreen.wgsl"

struct Grid {
    // Minor lines, linear, straight alpha
    color: vec4f,
    // Major lines, linear, straight alpha
    major_color: vec4f,
    // x: height of the plane, y: cell size, z: fade distance, w: line width in pixels
    params: vec4f,
    // x: cells between major lines, 0 for none, y: width of the major lines and the axes in
    // pixels
    config: vec4f,
};

@group(0) @binding(0) var<uniform> view: View;
@group(1) @binding(0) var<uniform> grid: Grid;

struct GridOut {
    @location(0) color: vec4f,
    @builtin(frag_depth) depth: f32,
};

// Change of each coordinate of uv over a pixel, the length of its screen gradient.
fn grid_deriv(uv: vec2f) -> vec2f {
    let dx = dpdx(uv);
    let dy = dpdy(uv);
    return vec2f(length(vec2f(dx.x, dy.x)), length(vec2f(dx.y, dy.y)));
}

// Coverage of the lines of the two coordinates: dist is twice the distance to the nearest line
// in units of the coordinate, deriv its change over a pixel and width the width of a line in
// pixels.
fn grid_cover(dist: vec2f, deriv: vec2f, width: f32) -> vec2f {
    let d = max(deriv, vec2f(1e-6));
    let target_width = min(width * d, vec2f(0.5));
    let draw = clamp(target_width, d, vec2f(0.5));
    let aa = d * 1.5;
    var g = 1.0 - smoothstep(draw - aa, draw + aa, dist);
    g *= saturate(target_width / draw);
    g = mix(g, target_width, saturate(d * 2.0 - 1.0));
    return g;
}

// Coverage of the lines of a grid of cells of a size at a point of the plane, the lines of each
// axis faded out where their cells get under a few pixels across them (8 to 3).
fn grid_lines(p: vec2f, cell: f32, width: f32) -> f32 {
    let uv = p / cell;
    let deriv = grid_deriv(uv);
    let dist = 1.0 - abs(fract(uv) * 2.0 - 1.0);
    let fade = 1.0 - smoothstep(vec2f(0.125), vec2f(0.35), deriv);
    let g = grid_cover(dist, deriv, width) * fade;
    return mix(g.x, 1.0, g.y);
}

@fragment fn fs(in: FullscreenOut) -> GridOut {
    let height = grid.params.x;
    let cell = max(grid.params.y, 1e-6);
    let fade_distance = max(grid.params.z, 1e-4);
    let width = grid.params.w;
    let major_every = grid.config.x;
    let major_width = grid.config.y;

    // The ray of the pixel from the near plane (depth 1, reversed Z) through depth 0.5, which
    // is finite for an infinite far plane too.
    let ndc = vec2f(in.uv.x * 2.0 - 1.0, 1.0 - in.uv.y * 2.0);
    let near4 = view.inv_view_proj * vec4f(ndc, 1.0, 1.0);
    let mid4 = view.inv_view_proj * vec4f(ndc, 0.5, 1.0);
    let origin = near4.xyz / near4.w;
    let dir = normalize(mid4.xyz / mid4.w - origin);
    let t = (height - origin.y) / select(dir.y, 1.0, abs(dir.y) < 1e-6);
    let hits = abs(dir.y) >= 1e-6 && t > 0.0;
    let hit = select(origin, origin + dir * t, hits);

    // Derivatives first, in uniform control flow.
    let minor = grid_lines(hit.xz, cell, width);
    let major = select(0.0, grid_lines(hit.xz, cell * max(major_every, 1.0), major_width),
        major_every >= 1.0);
    let axis_deriv = grid_deriv(hit.zx);
    let axes = grid_cover(abs(hit.zx) * 2.0, axis_deriv, major_width);

    var color = select(grid.color, grid.major_color, major >= minor);
    var cover = max(minor, major);
    if (axes.y > cover && axes.y >= axes.x) {
        color = vec4f(0.0, 0.0, 1.0, grid.major_color.a);
        cover = axes.y;
    }

    if (axes.x > cover) {
        color = vec4f(1.0, 0.0, 0.0, grid.major_color.a);
        cover = axes.x;
    }

    let distance_fade = 1.0 - smoothstep(fade_distance * 0.5, fade_distance,
        distance(hit, view.camera.xyz));
    let horizon_fade = smoothstep(0.0, 0.04, abs(dir.y));
    let alpha = saturate(color.a * cover * distance_fade * horizon_fade);

    let clip = view.view_proj * vec4f(hit, 1.0);
    let depth = clip.z / clip.w;
    if (!hits || grid.params.y <= 0.0 || alpha <= 0.0 || depth <= 0.0 || depth > 1.0) {
        discard;
    }

    var out: GridOut;
    out.color = vec4f(color.rgb * alpha, alpha);
    out.depth = depth;
    return out;
}
