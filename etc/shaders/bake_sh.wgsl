// Probe projection: turns the six capture faces of each probe of a chunk into the L2 SH
// irradiance of the probe (Ramamoorthi and Hanrahan), in the basis and coefficient order of
// mesh.wgsl. One workgroup per probe sums radiance times basis times the solid angle of every
// texel (Sloan, "Stupid Spherical Harmonics Tricks"), then scales each band by the clamped
// cosine convolution A_l. Texels no shape covered (alpha 0) see the hemisphere ambient as
// radiance a + 1.5 b y, whose irradiance over pi is exactly the a + b y the forward pass uses.
//
// Baked lights exist only in the probes, so their direct light at the probe is added too, as
// Unity's light probes hold the direct light of baked lights: a point, spot or directional
// light of irradiance c from direction w projects to c Y_lm(w) before the convolution. It is
// occluded when the face texel toward the light saw a surface nearer than the light, for a
// directional light any surface.
//
// Face f of probe p is the tile at column f and row p of the atlas, size texels on each side.
// Its texel (x, y), y down, looks along
// forward + u right + v up with u, v in [-1, 1] and right = cross(forward, up). The depth of
// a face is reversed with an infinite far plane: near over the view depth, 0 where nothing is.

struct Params {
    // x: face edge in texels, y: probes of the chunk
    size: vec4u,
    sky: vec4f,
    ground: vec4f,
    // Volume index of each probe of the chunk, four per vector
    probes: array<vec4u, 8>,
    // x: Baked lights, y: directional lights among them, which come first
    lights: vec4u,
    // x: near plane of the faces
    near: vec4f,
    // World position of each probe of the chunk
    positions: array<vec4f, 32>,
};

// A light as in mesh.wgsl: position is the direction toward a directional light.
struct Light {
    position: vec3f,
    range: f32,
    color: vec3f,
    spot_scale: f32,
    direction: vec3f,
    spot_offset: f32,
};

@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var captures: texture_2d<f32>;
@group(0) @binding(2) var<storage, read_write> volume: array<vec4f>;
@group(0) @binding(3) var depths: texture_depth_2d;
@group(0) @binding(4) var<storage, read> lights: array<Light>;

const GROUP: u32 = 64u;
const SH_COEFFICIENTS: u32 = 9u;
const PI: f32 = 3.141592653589793;

var<workgroup> partial: array<array<vec3f, 9>, 64>;

// Texel of the atlas that holds texel (x, y) of a face of a probe.
fn tile(probe: u32, face: u32, x: u32, y: u32) -> vec2u {
    let size = max(params.size.x, 1u);
    return vec2u(face * size + x, probe * size + y);
}

// Axis a face looks along and its up axis.
fn face_axes(face: u32) -> mat2x3f {
    switch face {
        case 0u: { return mat2x3f(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0)); }
        case 1u: { return mat2x3f(vec3f(-1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0)); }
        case 2u: { return mat2x3f(vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, -1.0)); }
        case 3u: { return mat2x3f(vec3f(0.0, -1.0, 0.0), vec3f(0.0, 0.0, 1.0)); }
        case 4u: { return mat2x3f(vec3f(0.0, 0.0, 1.0), vec3f(0.0, 1.0, 0.0)); }
        default: { return mat2x3f(vec3f(0.0, 0.0, -1.0), vec3f(0.0, 1.0, 0.0)); }
    }
}

// Solid angle of the face area from the face center to (x, y), signed by the quadrant.
fn corner(x: f32, y: f32) -> f32 {
    return atan2(x * y, sqrt(x * x + y * y + 1.0));
}

// Sloan's real SH basis up to band 2, in the order of mesh.wgsl.
fn basis(d: vec3f) -> array<f32, 9> {
    return array<f32, 9>(
        0.2820947918,
        0.4886025119 * d.y,
        0.4886025119 * d.z,
        0.4886025119 * d.x,
        1.0925484306 * d.x * d.y,
        1.0925484306 * d.y * d.z,
        0.3153915653 * (3.0 * d.z * d.z - 1.0),
        1.0925484306 * d.x * d.z,
        0.5462742153 * (d.x * d.x - d.y * d.y));
}

// Clamped cosine convolution of the band of a coefficient.
fn band(k: u32) -> f32 {
    if (k == 0u) {
        return PI;
    }

    if (k < 4u) {
        return 2.0 * PI / 3.0;
    }

    return PI / 4.0;
}

// Distance from a probe to the surface its faces saw in a direction, negative where they saw
// none.
fn surface_distance(probe: u32, d: vec3f) -> f32 {
    let a = abs(d);
    var face = select(5u, 4u, d.z > 0.0);
    if (a.x >= a.y && a.x >= a.z) {
        face = select(1u, 0u, d.x > 0.0);
    } else if (a.y >= a.z) {
        face = select(3u, 2u, d.y > 0.0);
    }

    let axes = face_axes(face);
    let right = cross(axes[0], axes[1]);
    let f = dot(d, axes[0]);
    let size = max(params.size.x, 1u);
    let u = dot(d, right) / f;
    let v = dot(d, axes[1]) / f;
    let x = min(u32(max((u + 1.0) * 0.5 * f32(size), 0.0)), size - 1u);
    let y = min(u32(max((1.0 - v) * 0.5 * f32(size), 0.0)), size - 1u);
    let depth = textureLoad(depths, tile(probe, face, x, y), 0);
    return select(-1.0, params.near.x / max(depth, 1e-20) / f, depth > 0.0);
}

// Radiance of a point or spot light at a distance, as attenuation() of mesh.wgsl.
fn attenuation(light: Light, l: vec3f, dist: f32) -> f32 {
    let ratio = dist / max(light.range, 1e-4);
    let window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    let cone = clamp(dot(-l, light.direction) * light.spot_scale + light.spot_offset, 0.0, 1.0);
    return window * window / (dist * dist + 1.0) * cone * cone;
}

// Coefficient k of the unconvolved SH of the direct light of the Baked lights at a probe.
fn direct(probe: u32, k: u32) -> vec3f {
    let p = params.positions[probe].xyz;
    let count = min(params.lights.x, arrayLength(&lights));
    var sum = vec3f(0.0);
    for (var i = 0u; i < count; i++) {
        let light = lights[i];
        var d = normalize(light.position);
        var c = light.color;
        if (i >= params.lights.y) {
            let delta = light.position - p;
            let dist = length(delta);
            d = delta / max(dist, 1e-4);
            c = light.color * attenuation(light, d, dist);
            let hit = surface_distance(probe, d);
            if (hit >= 0.0 && hit < dist * 0.95) {
                c = vec3f(0.0);
            }
        } else if (surface_distance(probe, d) >= 0.0) {
            c = vec3f(0.0);
        }

        sum += c * basis(d)[k];
    }

    return sum;
}

@compute @workgroup_size(64)
fn project(@builtin(workgroup_id) group: vec3u, @builtin(local_invocation_index) lane: u32) {
    let probe = group.x;
    let size = max(params.size.x, 1u);
    let per_face = size * size;
    let step = 2.0 / f32(size);
    let a = (params.sky.rgb + params.ground.rgb) * 0.5;
    let b = (params.sky.rgb - params.ground.rgb) * 0.75;
    var sum: array<vec3f, 9>;
    for (var t = lane; t < per_face * 6u; t += GROUP) {
        let face = t / per_face;
        let x = (t % per_face) % size;
        let y = (t % per_face) / size;
        let u0 = f32(x) * step - 1.0;
        let v0 = 1.0 - f32(y + 1u) * step;
        let u1 = u0 + step;
        let v1 = v0 + step;
        let weight = corner(u0, v0) - corner(u0, v1) - corner(u1, v0) + corner(u1, v1);
        let axes = face_axes(face);
        let right = cross(axes[0], axes[1]);
        let d = normalize(axes[0] + right * (u0 + u1) * 0.5 + axes[1] * (v0 + v1) * 0.5);
        let texel = textureLoad(captures, tile(probe, face, x, y), 0);
        let radiance = select(a + b * d.y, texel.rgb, texel.a > 0.5) * weight;
        let y_lm = basis(d);
        for (var k = 0u; k < SH_COEFFICIENTS; k++) {
            sum[k] += radiance * y_lm[k];
        }
    }

    partial[lane] = sum;
    workgroupBarrier();
    for (var stride = GROUP / 2u; stride > 0u; stride = stride / 2u) {
        if (lane < stride) {
            for (var k = 0u; k < SH_COEFFICIENTS; k++) {
                partial[lane][k] += partial[lane + stride][k];
            }
        }

        workgroupBarrier();
    }

    if (lane < SH_COEFFICIENTS) {
        let index = params.probes[probe / 4u][probe % 4u];
        let e = (partial[0][lane] + direct(probe, lane)) * band(lane);
        volume[index * SH_COEFFICIENTS + lane] = vec4f(e, 0.0);
    }
}
