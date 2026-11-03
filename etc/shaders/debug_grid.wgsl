// Infinite grid of the debug pass of a view (GridSettings): a triangle covers the target and
// every fragment casts the ray of its pixel from the near plane through inv_view_proj, which
// works for an orthographic camera too, onto the plane y = height. A ray that does not hit the
// plane in front of the camera discards; a hit writes its own depth, so the depth of the view
// (compare Greater, no writes) hides it behind geometry and it stays on its plane.
//
// The lines are anti aliased as Ben Golus's "pristine grid": a line keeps its width in pixels
// close up, thinner than a pixel it fades by its coverage instead of breaking up, and where a
// cell gets under a pixel it turns into its average coverage, so there is no moire. The change
// of the plane over a pixel comes from the rays of points around the pixel rather than from
// screen derivatives, which a 2x2 quad on the horizon would make huge. Minor and major lines
// fade out besides where their cells get under a few pixels. The world X axis (z = 0) is red
// and the Z axis (x = 0) blue, as wide as the major lines.
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

// Where the ray of a point of the target meets the plane.
struct GridRay {
    // Point of the plane, far along the ray when it points away from the plane
    hit: vec3f,
    // Slope of the ray toward the plane, positive when it hits it in front of the camera
    toward: f32,
};

// Cast the ray of a point of the target (uv, 0 at the top left) from the near plane (depth 1,
// reversed Z) through depth 0.5, finite for an infinite far plane too, onto the plane.
fn grid_ray(uv: vec2f, height: f32) -> GridRay {
    let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let near4 = view.inv_view_proj * vec4f(ndc, 1.0, 1.0);
    let mid4 = view.inv_view_proj * vec4f(ndc, 0.5, 1.0);
    let origin = near4.xyz / near4.w;
    let dir = normalize(mid4.xyz / mid4.w - origin);
    let side = select(-1.0, 1.0, height >= origin.y);
    var r: GridRay;
    r.toward = dir.y * side;
    let t = abs(height - origin.y) / max(r.toward, 1e-4);
    r.hit = vec3f(origin.x + dir.x * t, height, origin.z + dir.z * t);
    return r;
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

// Fade of lines whose cells get under a few pixels across them (8 to 3), from the change of
// the cell coordinate over a pixel.
fn grid_cell_fade(d: vec2f) -> vec2f {
    return 1.0 - smoothstep(vec2f(0.125), vec2f(0.35), d);
}

// Coverage of the lines of a grid of cells of a size at a point of the plane, with the change
// of the point over a pixel, the lines of each axis faded out where their cells get under a few
// pixels across them.
fn grid_lines(p: vec2f, deriv: vec2f, cell: f32, width: f32) -> f32 {
    let uv = p / cell;
    let d = deriv / cell;
    let dist = 1.0 - abs(fract(uv) * 2.0 - 1.0);
    let g = grid_cover(dist, d, width) * grid_cell_fade(d);
    return mix(g.x, 1.0, g.y);
}

// Coverage of the two axes of the plane at the distances of a point to them in world units and
// their change over a pixel: lines of a width in pixels that, unlike the repeating lines, never
// turn into an average. They fade out with the largest cells of the grid, before the
// linearized change of a pixel near the horizon gets so large that it covers the axes.
fn grid_axes(dist: vec2f, deriv: vec2f, width: f32) -> vec2f {
    let d = max(deriv, vec2f(1e-6));
    let target_width = width * d;
    let draw = max(target_width, d);
    let aa = d * 1.5;
    return (1.0 - smoothstep(draw - aa, draw + aa, abs(dist) * 2.0)) *
        saturate(target_width / draw);
}

@fragment fn fs(in: FullscreenOut) -> GridOut {
    let height = grid.params.x;
    let cell = max(grid.params.y, 1e-6);
    let fade_distance = max(grid.params.z, 1e-4);
    let width = grid.params.w;
    let major_every = grid.config.x;
    let major_width = grid.config.y;

    // The ray of the pixel and of points an eighth of a pixel to each side of it, whose
    // difference is the change of the plane over a pixel along x and y.
    let ray = grid_ray(in.uv, height);
    let hit = ray.hit;
    let e = view.viewport.zw * 0.125;
    let dx = (grid_ray(in.uv + vec2f(e.x, 0.0), height).hit.xz -
        grid_ray(in.uv - vec2f(e.x, 0.0), height).hit.xz) * 4.0;
    let dy = (grid_ray(in.uv + vec2f(0.0, e.y), height).hit.xz -
        grid_ray(in.uv - vec2f(0.0, e.y), height).hit.xz) * 4.0;
    let deriv = vec2f(length(vec2f(dx.x, dy.x)), length(vec2f(dx.y, dy.y)));

    let minor = grid_lines(hit.xz, deriv, cell, width);
    let major = select(0.0,
        grid_lines(hit.xz, deriv, cell * max(major_every, 1.0), major_width),
        major_every >= 1.0);
    let axes = grid_axes(hit.zx, deriv.yx, major_width) *
        grid_cell_fade(deriv.yx / (cell * max(major_every, 1.0)));

    // The lines in the color that covers more, the Z axis (blue) and the X axis (red) over them,
    // premultiplied.
    let lines = select(grid.color, grid.major_color, major >= minor);
    let axis_alpha = axes * grid.major_color.a;
    var color = vec4f(lines.rgb, 1.0) * lines.a * max(minor, major);
    color = vec4f(0.0, 0.0, 1.0, 1.0) * axis_alpha.y + color * (1.0 - axis_alpha.y);
    color = vec4f(1.0, 0.0, 0.0, 1.0) * axis_alpha.x + color * (1.0 - axis_alpha.x);

    let distance_fade = 1.0 - smoothstep(fade_distance * 0.5, fade_distance,
        distance(hit, view.camera.xyz));
    let horizon_fade = smoothstep(0.0, 0.04, ray.toward);
    let fade = distance_fade * horizon_fade;

    let clip = view.view_proj * vec4f(hit, 1.0);
    let depth = clip.z / clip.w;
    if (ray.toward <= 1e-6 || grid.params.y <= 0.0 || color.a * fade <= 0.0 || depth <= 0.0 ||
        depth > 1.0)
    {
        discard;
    }

    var out: GridOut;
    out.color = saturate(color * fade);
    out.depth = depth;
    return out;
}
