// Probe projection: turns the six capture faces of each probe of a chunk into the L2 SH
// irradiance of the probe (Ramamoorthi and Hanrahan), in the basis and coefficient order of
// mesh.wgsl. One workgroup per probe sums radiance times basis times the solid angle of every
// texel (Sloan, "Stupid Spherical Harmonics Tricks"), then scales each band by the clamped
// cosine convolution A_l. Texels no shape covered (alpha 0) see the hemisphere ambient as
// radiance a + 1.5 b y, whose irradiance over pi is exactly the a + b y the forward pass uses.
//
// Face f of probe p is layer 6 p + f. Its texel (x, y), y down, looks along
// forward + u right + v up with u, v in [-1, 1] and right = cross(forward, up).

struct Params {
    // x: face edge in texels, y: probes of the chunk
    size: vec4u,
    sky: vec4f,
    ground: vec4f,
    // Volume index of each probe of the chunk, four per vector
    probes: array<vec4u, 8>,
};

@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var captures: texture_2d_array<f32>;
@group(0) @binding(2) var<storage, read_write> volume: array<vec4f>;

const GROUP: u32 = 64u;
const SH_COEFFICIENTS: u32 = 9u;
const PI: f32 = 3.141592653589793;

var<workgroup> partial: array<array<vec3f, 9>, 64>;

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
        let texel = textureLoad(captures, vec2u(x, y), probe * 6u + face, 0);
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
        volume[index * SH_COEFFICIENTS + lane] = vec4f(partial[0][lane] * band(lane), 0.0);
    }
}
