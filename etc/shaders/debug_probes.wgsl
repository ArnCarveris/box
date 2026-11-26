// Probe views of the debug pass of a view (ViewDebug.light_probes and reflection_probes): a
// sphere at every probe of the irradiance volume the view samples, lit by its own SH, a mirror
// sphere at the capture point of every reflection probe its clusters list, and the influence and
// projection boxes of those probes as lines.
//
// The spheres and boxes are placed and hit relative to the render origin (render_view_proj),
// which keeps their precision near it: the stored center of a probe and the camera are moved by
// it once, as instance_relative moves a record (probe_relative); the light of a probe is taken
// with directions alone. Without a render origin these are the local positions exactly.
#include "include/lights.wgsl"
#include "include/probes.wgsl"

const PI: f32 = 3.141592653589793;

// Radius of a reflection probe sphere in world units without a probe_size.
const REFLECTION_RADIUS: f32 = 0.25;

// Radius of a light probe sphere in probe spacings without a probe_size.
const LIGHT_RADIUS: f32 = 0.25;

// Colors of the hatch of a probe that is invalid for every cell around it.
const INVALID_COLOR: vec3f = vec3f(0.3, 0.0, 0.0);
const INVALID_HATCH: vec3f = vec3f(0.05, 0.0, 0.0);

// Width of a stripe of the hatch in pixels.
const INVALID_STRIPE: f32 = 4.0;

// Color of a reflection probe whose cubemap holds no capture yet.
const NOT_READY_COLOR: vec3f = vec3f(0.25, 0.25, 0.25);

// Colors of the influence and projection boxes.
const INFLUENCE_COLOR: vec3f = vec3f(1.0, 1.0, 0.0);
const PROJECTION_COLOR: vec3f = vec3f(0.0, 1.0, 1.0);

// Largest scale of the quad of a sphere, for a camera at or inside the sphere.
const QUAD_SCALE_MAX: f32 = 64.0;

struct ProbeDebug {
    // x: world radius of the spheres, 0 for the default of the kind of probe
    params: vec4f,
};

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(5) var<storage, read> reflection_probes: array<ReflectionProbe>;
@group(0) @binding(6) var reflection_cubes: texture_cube_array<f32>;
@group(0) @binding(7) var reflection_sampler: sampler;

@group(1) @binding(0) var<uniform> debug: ProbeDebug;

struct SphereOut {
    @builtin(position) pos: vec4f,
    // Point of the quad relative to the render origin
    @location(0) world: vec3f,
    // Center relative to the render origin and radius of the sphere
    @location(1) @interpolate(flat) sphere: vec4f,
    @location(2) @interpolate(flat) index: u32,
};

struct SphereFragment {
    @location(0) color: vec4f,
    @builtin(frag_depth) depth: f32,
};

// Where the ray of a pixel meets a sphere.
struct SphereHit {
    point: vec3f,
    normal: vec3f,
    // Direction of the ray, from the camera
    dir: vec3f,
    depth: f32,
    hit: bool,
};

fn orthographic() -> bool {
    return view.lights.w == 1u;
}

// A stored local position, the center of a probe or the camera, relative to the render origin.
fn probe_relative(local: vec3f) -> vec3f {
    return local - view.render_delta.xyz;
}

// World direction the camera looks along.
fn camera_forward() -> vec3f {
    return -normalize(vec3f(view.view[0].z, view.view[1].z, view.view[2].z));
}

// Corner of the quad of a sphere for one of its 6 vertices, in the plane through its center
// facing the camera, relative to the render origin as the center is. In perspective the quad is scaled by d / sqrt(d^2 - r^2), where the cone
// of rays that touch the sphere crosses that plane.
fn sphere_vertex(i: u32, center: vec3f, radius: f32, index: u32) -> SphereOut {
    var corners = array<vec2f, 6>(
        vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0),
        vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
    let c = corners[i % 6u];
    var toward = camera_forward();
    var scale = 1.0;
    if (!orthographic()) {
        let offset = center - probe_relative(view.camera.xyz);
        let d = length(offset);
        toward = select(toward, offset / d, d > 1e-6);
        let k = d * d - radius * radius;
        scale = select(QUAD_SCALE_MAX, min(d / sqrt(max(k, 1e-12)), QUAD_SCALE_MAX), k > 0.0);
    }

    let up = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(toward.y) > 0.99);
    let right = normalize(cross(toward, up));
    let upward = cross(right, toward);
    let world = center + (right * c.x + upward * c.y) * (radius * scale);
    var o: SphereOut;
    o.pos = view.render_view_proj * vec4f(world, 1.0);
    o.world = world;
    o.sphere = vec4f(center, radius);
    o.index = index;
    return o;
}

// Intersect the ray of a fragment, through the point of its quad, with its sphere: the hit
// nearest the camera in front of it, the far one for a camera inside the sphere, relative to the
// render origin.
fn sphere_hit(in: SphereOut) -> SphereHit {
    var r: SphereHit;
    let center = in.sphere.xyz;
    let radius = in.sphere.w;
    var origin = probe_relative(view.camera.xyz);
    var dir = normalize(in.world - origin);
    if (orthographic()) {
        dir = camera_forward();
        origin = in.world;
    }

    let oc = origin - center;
    let b = dot(oc, dir);
    let h = b * b - (dot(oc, oc) - radius * radius);
    r.dir = dir;
    r.hit = false;
    if (h < 0.0) {
        return r;
    }

    let s = sqrt(h);
    var t = -b - s;
    if (t < 0.0 && !orthographic()) {
        t = -b + s;
    }

    r.point = origin + dir * t;
    r.normal = normalize(r.point - center);
    let clip = view.render_view_proj * vec4f(r.point, 1.0);
    r.depth = clip.z / max(clip.w, 1e-12);
    r.hit = clip.w > 0.0 && r.depth >= 0.0 && r.depth <= 1.0 &&
        (orthographic() || t >= 0.0);
    return r;
}

// Radius of a sphere: probe_size, else the default of its kind.
fn sphere_radius(fallback: f32) -> f32 {
    return select(fallback, debug.params.x, debug.params.x > 0.0);
}

// Grid coordinate of a probe of the volume from its index, x fastest.
fn probe_coord(p: u32) -> vec3u {
    let size = volume.size.xyz;
    return vec3u(p % size.x, (p / size.x) % size.y, p / (size.x * size.y));
}

// Whether a probe is valid for some cell around it: a cell has it as a corner when the corner
// clamped to the grid is the probe, which on an axis of one probe holds for both of its ends.
// It is invalid, as inside static geometry, when the corner bit is clear in every such cell.
fn probe_valid(coord: vec3u) -> bool {
    let size = volume.size.xyz;
    let cells = max(size, vec3u(2u)) - 1u;
    let at = vec3i(coord);
    for (var j = 0u; j < 8u; j++) {
        let c = at - vec3i(vec3u(j & 1u, (j >> 1u) & 1u, (j >> 2u) & 1u));
        if (any(c < vec3i(0)) || any(c >= vec3i(cells))) {
            continue;
        }

        let cell = u32(c.x) + (u32(c.y) + u32(c.z) * cells.y) * cells.x;
        let corners = (cell_valid[cell >> 2u] >> ((cell & 3u) * 8u)) & 0xFFu;
        for (var i = 0u; i < 8u; i++) {
            let o = vec3u(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
            if (all(min(vec3u(c) + o, size - 1u) == coord) && ((corners >> i) & 1u) != 0u) {
                return true;
            }
        }
    }

    return false;
}

@vertex fn vs_light(@builtin(vertex_index) i: u32, @builtin(instance_index) p: u32) -> SphereOut {
    let coord = probe_coord(p);
    let center = probe_relative(volume.origin.xyz) + vec3f(coord) * volume.origin.w;
    return sphere_vertex(i, center, sphere_radius(LIGHT_RADIUS * volume.origin.w), p);
}

// A light probe lit by its own SH in the direction of the world normal of the hit, unshadowed;
// an invalid probe in a dark red hatch.
@fragment fn fs_light(in: SphereOut) -> SphereFragment {
    let r = sphere_hit(in);
    if (!r.hit) {
        discard;
    }

    var color: vec3f;
    if (probe_valid(probe_coord(in.index))) {
        let e = zh3_irradiance(volume_probe(in.index), r.normal);
        color = max(e / PI, vec3f(0.0));
    } else {
        let stripe = fract((in.pos.x + in.pos.y) / (INVALID_STRIPE * 2.0));
        color = select(INVALID_HATCH, INVALID_COLOR, stripe < 0.5);
    }

    var out: SphereFragment;
    out.color = vec4f(color, 1.0);
    out.depth = r.depth;
    return out;
}

@vertex fn vs_reflection(@builtin(vertex_index) i: u32, @builtin(instance_index) p: u32) ->
    SphereOut
{
    let center = probe_relative(reflection_probes[p].capture);
    return sphere_vertex(i, center, sphere_radius(REFLECTION_RADIUS), p);
}

// A reflection probe as a mirror: its cubemap in the reflection direction of the hit, at mip 0;
// gray until its slot holds a capture.
@fragment fn fs_reflection(in: SphereOut) -> SphereFragment {
    let r = sphere_hit(in);
    if (!r.hit) {
        discard;
    }

    let p = reflection_probes[in.index];
    var color = NOT_READY_COLOR;
    if (p.ready != 0u) {
        color = textureSampleLevel(reflection_cubes, reflection_sampler,
            reflect(r.dir, r.normal), p.slot, 0.0).rgb;
    }

    var out: SphereFragment;
    out.color = vec4f(color, 1.0);
    out.depth = r.depth;
    return out;
}

struct LineOut {
    @builtin(position) pos: vec4f,
    @location(0) @interpolate(flat) color: vec3f,
};

// End i of the 12 edges of a box of half extents extent around the center of a probe, along its
// axes, relative to the render origin: edge i / 2 runs along axis i / 8.
fn box_vertex(p: ReflectionProbe, i: u32, extent: vec3f) -> vec3f {
    let edge = i / 2u;
    let axis = edge / 4u;
    let k = edge % 4u;
    let e = f32(i & 1u) * 2.0 - 1.0;
    let b0 = f32(k & 1u) * 2.0 - 1.0;
    let b1 = f32((k >> 1u) & 1u) * 2.0 - 1.0;
    var c = vec3f(b0, b1, e);
    if (axis == 0u) {
        c = vec3f(e, b0, b1);
    } else if (axis == 1u) {
        c = vec3f(b0, e, b1);
    }

    let l = c * extent;
    return probe_relative(p.center) + p.axes[0].xyz * l.x + p.axes[1].xyz * l.y + p.axes[2].xyz * l.z;
}

// Instance 2k draws the influence box of probe k, 2k + 1 its projection box, which is left out
// (clipped) when it has no volume.
@vertex fn vs_box(@builtin(vertex_index) i: u32, @builtin(instance_index) instance: u32) ->
    LineOut
{
    let p = reflection_probes[instance >> 1u];
    let projection = (instance & 1u) == 1u;
    var o: LineOut;
    if (projection && any(p.projection <= vec3f(0.0))) {
        o.pos = vec4f(0.0, 0.0, 2.0, 1.0);
        o.color = PROJECTION_COLOR;
        return o;
    }

    let extent = select(p.extent, p.projection, projection);
    o.pos = view.render_view_proj * vec4f(box_vertex(p, i % 24u, extent), 1.0);
    o.color = select(INFLUENCE_COLOR, PROJECTION_COLOR, projection);
    return o;
}

@fragment fn fs_box(in: LineOut) -> @location(0) vec4f {
    return vec4f(in.color, 1.0);
}
