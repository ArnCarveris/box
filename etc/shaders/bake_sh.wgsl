// Probe projection: turns the six capture faces of each probe of a chunk into the L2 SH
// irradiance of the probe (Ramamoorthi and Hanrahan), in the basis and coefficient order of
// mesh.wgsl. One workgroup per probe sums radiance times basis times the solid angle of every
// texel (Sloan, "Stupid Spherical Harmonics Tricks"), then scales each band by the clamped
// cosine convolution A_l. Texels no shape covered (alpha 0) see the sky: the hemisphere ambient
// as radiance a + 1.5 b y, whose irradiance over pi is exactly the a + b y the forward pass
// uses, or with params.size.z the radiance E_lm / A_l of the SH of an environment source, so
// an open probe gets the SH of the environment.
//
// Baked lights exist only in the probes, so their direct light at the probe is added too, as
// Unity's light probes hold the direct light of baked lights: a point, spot or directional
// light of irradiance c from direction w projects to c Y_lm(w) before the convolution. It is
// occluded when the face texel toward the light saw a surface nearer than the light, for a
// directional light any surface.
//
// The projection also writes the visibility of each probe: an octahedral map of the mean and
// mean square distance to the surfaces its faces saw, which mesh.wgsl tests a point against
// before it blends the probe in (DDGI's Chebyshev visibility).
//
// The cull entry point lists, before the faces render, the instances of one batch each face of
// each probe of the chunk draws: those whose sphere spans the smallest size at their depth in
// the face their center is in, and whose box reaches into the 90 degree frustum of the face.
// The slots of a face keep slot order, compacted by a workgroup prefix sum, so depth ties
// resolve the same way in every bake.
//
// The reflect_store entry point writes the faces of the reflection probes of a chunk into mip 0
// of their cubemap slot instead (params.probes holds the slots), mirrored to the WebGPU cubemap
// convention, with the sky radiance where nothing covered a texel (mip 0 of the environment cube
// with params.size.z, the hemisphere of the projection otherwise). It writes
// them into a source cube array of the chunk too, one cube per probe, whose full mip chain
// reflect_mip box filters level by level. reflect_prefilter then fills each higher mip level of
// the slots with the GGX prefiltered radiance of the source (split sum, Karis), sampling a
// lower source mip by the solid angle of each sample (filtered importance sampling). The
// projection runs on the reflection captures too, writing the SH of each slot.
//
// The environment processing reuses reflect_mip and reflect_prefilter on one cube (params.probes
// all 0, params.size.y 1): a Box environment box filters its own mips, a Ggx one box filters a
// source cube and prefilters its mips from it. environment_sh projects a mip of at most 16
// texels per edge of the box filtered chain onto the same SH as project.
//
// A texel whose radiance is not finite (a NaN or infinity a draw shaded) counts as uncovered in
// the projection and the store: the prefilter and the SH sum spread one such texel over the
// whole probe, while the forward pass shows it as one pixel.
//
// Face f of probe p is the tile at column f and row p of the atlas, size texels on each side.
// Its texel (x, y), y down, looks along
// forward + u right + v up with u, v in [-1, 1] and right = cross(forward, up). The depth of
// a face is reversed with an infinite far plane: near over the view depth, 0 where nothing is.

#include "include/instance.wgsl"
#include "include/cube.wgsl"

struct Params {
    // x: face edge in texels, y: probes of the chunk, z: 1 when uncovered texels see the
    // environment instead of the hemisphere
    size: vec4u,
    sky: vec4f,
    ground: vec4f,
    // Volume index of each probe of the chunk, four per vector
    probes: array<vec4u, 32>,
    // x: Baked lights, y: directional lights among them, which come first
    lights: vec4u,
    // x: near plane of the faces, y: pixels per unit at depth 1, z: smallest size in pixels,
    // w: spacing of the probes of the volume
    near: vec4f,
    // World position of each probe of the chunk
    positions: array<vec4f, 128>,
    // L2 SH irradiance of the environment, in the basis and order of basis()
    environment: array<vec4f, 9>,
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

// The instances of one batch the cull tests, at a dynamic offset per batch.
struct CullJob {
    // First slot of the run of the batch in its instance page, or first entry of lists when
    // list is 1
    first: u32,
    count: u32,
    list: u32,
    // Index of the batch among the batches of the chunk
    draw: u32,
    // Box around the mesh of the batch
    center: vec3f,
    index_count: u32,
    extent: vec3f,
    // First entry of the batch in the slots of a face
    region: u32,
    // Batches of the chunk
    draws: u32,
    // Slots of a face, those of every batch
    stride: u32,
    pad0: u32,
    pad1: u32,
};

@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var captures: texture_2d<f32>;
@group(0) @binding(2) var<storage, read_write> volume: array<vec4f>;
@group(0) @binding(3) var depths: texture_depth_2d;
@group(0) @binding(4) var<storage, read> lights: array<Light>;
@group(0) @binding(5) var<uniform> job: CullJob;
@group(0) @binding(6) var<storage, read_write> slots: array<u32>;
@group(0) @binding(7) var<storage, read_write> args: array<u32>;
@group(0) @binding(8) var<storage, read> lists: array<u32>;
@group(0) @binding(9) var<storage, read_write> visibility: array<u32>;
@group(0) @binding(10) var cube_store: texture_storage_2d_array<rgba16float, write>;
@group(0) @binding(11) var cube_source: texture_2d_array<f32>;
@group(0) @binding(12) var cube_mip: texture_storage_2d_array<rgba16float, write>;
@group(0) @binding(13) var source_store: texture_storage_2d_array<rgba16float, write>;
@group(0) @binding(14) var source: texture_cube_array<f32>;
@group(0) @binding(15) var source_sampler: sampler;
@group(0) @binding(16) var prefiltered: texture_storage_2d_array<rgba16float, write>;
@group(0) @binding(17) var environment_cube: texture_cube<f32>;
@group(1) @binding(0) var<storage, read> transforms: array<InstanceTransform>;
@group(1) @binding(1) var<storage, read> surface_assets: array<vec4f>;

const GROUP: u32 = 64u;
const SH_COEFFICIENTS: u32 = 9u;
const PI: f32 = 3.141592653589793;

// Texels per edge of the octahedral distance map of a probe, as in mesh.wgsl; one per lane.
const VISIBILITY_SIZE: u32 = 8u;

// Largest distance of the map in probe spacings: no point of the cells around a probe is
// farther, so a direction that saw no surface or a farther one stores this.
const VISIBILITY_MAX: f32 = 2.0;

// Directions per edge of a texel of the map whose distances the texel averages.
const VISIBILITY_SAMPLES: u32 = 4u;

// GGX samples of a texel of the prefiltered mip levels.
const PREFILTER_SAMPLES: u32 = 64u;

// Face edge in texels of the last prefiltered mip level at least.
const PREFILTER_MIN_SIZE: u32 = 4u;

// Invocations of a workgroup of the cull.
const CULL_GROUP: u32 = 256u;

// Words of the indexed indirect draw arguments of a draw.
const ARGS: u32 = 5u;

var<workgroup> partial: array<array<vec3f, 9>, 64>;

// Per invocation of the cull, three faces per word in fields of ten bits.
var<workgroup> face_scan: array<vec2u, 256>;

// Texel of the atlas that holds texel (x, y) of a face of a probe.
fn tile(probe: u32, face: u32, x: u32, y: u32) -> vec2u {
    let size = max(params.size.x, 1u);
    return vec2u(face * size + x, probe * size + y);
}

// A shape covered the capture texel and its radiance is finite. The exponent bits are tested
// because a shader compiler may fold comparisons with NaN.
fn covered(texel: vec4f) -> bool {
    let exponent = bitcast<vec3u>(texel.rgb) & vec3u(0x7f800000u);
    return texel.a > 0.5 && all(exponent != vec3u(0x7f800000u));
}

// Solid angle of the face area from the face center to (x, y), signed by the quadrant.
fn corner(x: f32, y: f32) -> f32 {
    return atan2(x * y, sqrt(x * x + y * y + 1.0));
}

// Solid angle of texel (x, y) of a cube face of size texels per edge. It is symmetric in u and
// v, so it holds for the atlas tiles and the mirrored WebGPU cubemap faces alike.
fn texel_solid_angle(x: u32, y: u32, size: u32) -> f32 {
    let step = 2.0 / f32(size);
    let u0 = f32(x) * step - 1.0;
    let v0 = 1.0 - f32(y + 1u) * step;
    let u1 = u0 + step;
    let v1 = v0 + step;
    return corner(u0, v0) - corner(u0, v1) - corner(u1, v0) + corner(u1, v1);
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

// Sum the SH coefficients of the lanes of a workgroup of GROUP invocations into partial[0].
// Every lane calls it from uniform control flow.
fn reduce_sh(lane: u32, sum: array<vec3f, 9>) {
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

// Direction of a point of the octahedral map, the inverse of octahedral_uv() of mesh.wgsl.
fn octahedral_direction(uv: vec2f) -> vec3f {
    let p = uv * 2.0 - 1.0;
    var d = vec3f(p, 1.0 - abs(p.x) - abs(p.y));
    if (d.z < 0.0) {
        d = vec3f((1.0 - abs(p.yx)) * select(vec2f(-1.0), vec2f(1.0), p >= vec2f(0.0)), d.z);
    }

    return normalize(d);
}

// Mean and mean square distance in probe spacings to the surfaces the faces of a probe saw
// over a texel of its octahedral map, averaged over VISIBILITY_SAMPLES squared directions.
fn visibility_moments(probe: u32, texel: u32) -> vec2f {
    let spacing = max(params.near.w, 1e-4);
    let cell = vec2f(f32(texel % VISIBILITY_SIZE), f32(texel / VISIBILITY_SIZE));
    var moments = vec2f(0.0);
    for (var s = 0u; s < VISIBILITY_SAMPLES * VISIBILITY_SAMPLES; s++) {
        let sub = vec2f(f32(s % VISIBILITY_SAMPLES), f32(s / VISIBILITY_SAMPLES)) + 0.5;
        let uv = (cell + sub / f32(VISIBILITY_SAMPLES)) / f32(VISIBILITY_SIZE);
        let hit = surface_distance(probe, octahedral_direction(uv));
        let r = select(VISIBILITY_MAX, min(hit / spacing, VISIBILITY_MAX), hit >= 0.0);
        moments += vec2f(r, r * r);
    }

    return moments / f32(VISIBILITY_SAMPLES * VISIBILITY_SAMPLES);
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
    var sum: array<vec3f, 9>;
    for (var t = lane; t < per_face * 6u; t += GROUP) {
        let face = t / per_face;
        let x = (t % per_face) % size;
        let y = (t % per_face) / size;
        let weight = texel_solid_angle(x, y, size);
        let axes = face_axes(face);
        let right = cross(axes[0], axes[1]);
        let u = (f32(x) + 0.5) * step - 1.0;
        let v = 1.0 - (f32(y) + 0.5) * step;
        let d = normalize(axes[0] + right * u + axes[1] * v);
        let texel = textureLoad(captures, tile(probe, face, x, y), 0);
        let radiance = select(sky(d), texel.rgb, covered(texel)) * weight;
        let y_lm = basis(d);
        for (var k = 0u; k < SH_COEFFICIENTS; k++) {
            sum[k] += radiance * y_lm[k];
        }
    }

    reduce_sh(lane, sum);
    let index = params.probes[probe / 4u][probe % 4u];
    if (lane < SH_COEFFICIENTS) {
        let e = (partial[0][lane] + direct(probe, lane)) * band(lane);
        volume[index * SH_COEFFICIENTS + lane] = vec4f(e, 0.0);
    }

    let texels = VISIBILITY_SIZE * VISIBILITY_SIZE;
    if (lane < texels) {
        visibility[index * texels + lane] = pack2x16float(visibility_moments(probe, lane));
    }
}

// Faces of a probe that draw an instance, one bit per face in the order of face_axes. Face f
// sees along axis a = f / 2 where the depth along a is at least the distance along the other
// two axes; the box reaches into it when it does so for each of them on its nearest corner.
fn cull_faces(slot: u32, eye: vec3f) -> u32 {
    let m = instance_model(transforms[slot]);
    let c0 = m[0].xyz;
    let c1 = m[1].xyz;
    let c2 = m[2].xyz;
    let center = c0 * job.center.x + c1 * job.center.y + c2 * job.center.z + m[3].xyz;
    let e = abs(c0) * job.extent.x + abs(c1) * job.extent.y + abs(c2) * job.extent.z;
    let q = center - eye;
    let a = abs(q);
    let radius = length(e);
    let w = max(a.x, max(a.y, a.z));
    if (w > radius && 2.0 * radius * params.near.y < params.near.z * w) {
        return 0u;
    }

    var mask = 0u;
    for (var f = 0u; f < 6u; f++) {
        let axis = f / 2u;
        let b = (axis + 1u) % 3u;
        let c = (axis + 2u) % 3u;
        let depth = select(q[axis], -q[axis], (f & 1u) != 0u) + e[axis];
        if (depth > 0.0 && depth + e[b] >= a[b] && depth + e[c] >= a[c]) {
            mask |= 1u << f;
        }
    }

    return mask;
}

// Count of a face in a packed scan entry.
fn face_count(v: vec2u, f: u32) -> u32 {
    return (select(v.y, v.x, f < 3u) >> ((f % 3u) * 10u)) & 1023u;
}

@compute @workgroup_size(256)
fn cull(@builtin(workgroup_id) group: vec3u, @builtin(local_invocation_index) lane: u32) {
    let probe = group.x;
    let eye = params.positions[probe].xyz;
    var running = array<u32, 6>(0u, 0u, 0u, 0u, 0u, 0u);
    for (var start = 0u; start < job.count; start += CULL_GROUP) {
        let i = start + lane;
        var slot = 0u;
        var mask = 0u;
        if (i < job.count) {
            slot = job.first + i;
            if (job.list != 0u) {
                slot = lists[job.first + i];
            }

            mask = cull_faces(slot, eye);
        }

        let own = vec2u(
            (mask & 1u) | (((mask >> 1u) & 1u) << 10u) | (((mask >> 2u) & 1u) << 20u),
            ((mask >> 3u) & 1u) | (((mask >> 4u) & 1u) << 10u) | (((mask >> 5u) & 1u) << 20u));
        face_scan[lane] = own;
        workgroupBarrier();
        for (var step = 1u; step < CULL_GROUP; step *= 2u) {
            var add = vec2u(0u);
            if (lane >= step) {
                add = face_scan[lane - step];
            }

            workgroupBarrier();
            face_scan[lane] += add;
            workgroupBarrier();
        }

        let before = face_scan[lane] - own;
        let total = face_scan[CULL_GROUP - 1u];
        for (var f = 0u; f < 6u; f++) {
            if (((mask >> f) & 1u) != 0u) {
                let at = (probe * 6u + f) * job.stride + job.region + running[f] +
                    face_count(before, f);
                slots[at] = slot;
            }

            running[f] += face_count(total, f);
        }

        workgroupBarrier();
    }

    if (lane < 6u) {
        let at = ((probe * 6u + lane) * job.draws + job.draw) * ARGS;
        args[at] = job.index_count;
        args[at + 1u] = running[lane];
        args[at + 2u] = 0u;
        args[at + 3u] = 0u;
        args[at + 4u] = 0u;
    }
}

// Radiance a texel no shape covered sees in a direction: the radiance of the SH of the
// environment with params.size.z, else the hemisphere ambient.
fn sky(d: vec3f) -> vec3f {
    if (params.size.z != 0u) {
        let y_lm = basis(d);
        var radiance = vec3f(0.0);
        for (var k = 0u; k < SH_COEFFICIENTS; k++) {
            radiance += params.environment[k].rgb / band(k) * y_lm[k];
        }

        return radiance;
    }

    let a = (params.sky.rgb + params.ground.rgb) * 0.5;
    let b = (params.sky.rgb - params.ground.rgb) * 0.75;
    return a + b * d.y;
}

// Radiance a stored cubemap texel no shape covered sees: mip 0 of the environment with
// params.size.z, whose texel directions are those of cube_direction, else sky().
fn sky_texel(d: vec3f) -> vec3f {
    if (params.size.z != 0u) {
        return textureSampleLevel(environment_cube, source_sampler, d, 0.0).rgb;
    }

    return sky(d);
}

// Layer of a face of a probe of the chunk in the cubemap array, six per slot.
fn cube_layer(probe: u32, face: u32) -> u32 {
    return params.probes[probe / 4u][probe % 4u] * 6u + face;
}

// Texel (x, y) of face z % 6 of probe z / 6 of the chunk, into its slot and its source cube. A
// cubemap face texel at column x sees along forward - u right with the right of the atlas, so it
// is texel size - 1 - x of the tile.
@compute @workgroup_size(8, 8)
fn reflect_store(@builtin(global_invocation_id) id: vec3u) {
    let size = max(params.size.x, 1u);
    let probe = id.z / 6u;
    let face = id.z % 6u;
    if (id.x >= size || id.y >= size || probe >= params.size.y) {
        return;
    }

    let d = cube_direction(face, id.xy, size);
    let texel = textureLoad(captures, tile(probe, face, size - 1u - id.x, id.y), 0);
    let radiance = vec4f(select(sky_texel(d), texel.rgb, covered(texel)), 1.0);
    textureStore(cube_store, id.xy, cube_layer(probe, face), radiance);
    textureStore(source_store, id.xy, id.z, radiance);
}

// Texel (x, y) of the next mip level of face z % 6 of probe z / 6 of the source cubes: the mean
// of the two by two texels of the level above.
@compute @workgroup_size(8, 8)
fn reflect_mip(@builtin(global_invocation_id) id: vec3u) {
    let size = textureDimensions(cube_mip);
    if (id.x >= size.x || id.y >= size.y || id.z / 6u >= params.size.y) {
        return;
    }

    let last = textureDimensions(cube_source) - vec2u(1u);
    let p = id.xy * 2u;
    var sum = vec4f(0.0);
    for (var i = 0u; i < 4u; i++) {
        let q = min(p + vec2u(i & 1u, i >> 1u), last);
        sum += textureLoad(cube_source, q, id.z, 0);
    }

    textureStore(cube_mip, id.xy, id.z, sum * 0.25);
}

// Point i of n of the Hammersley set.
fn hammersley(i: u32, n: u32) -> vec2f {
    return vec2f(f32(i) / f32(n), f32(reverseBits(i)) * 2.3283064365386963e-10);
}

// Half vector of a GGX sample around a normal for alpha = roughness squared, as mesh.wgsl.
fn ggx_half(xi: vec2f, a: f32, n: vec3f) -> vec3f {
    let phi = 2.0 * PI * xi.x;
    let cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    let sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
    let up = select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 0.0, 1.0), abs(n.z) < 0.999);
    let tx = normalize(cross(up, n));
    let ty = cross(n, tx);
    return normalize(tx * (sin_theta * cos(phi)) + ty * (sin_theta * sin(phi)) + n * cos_theta);
}

// Texel (x, y) of a prefiltered mip level of face z % 6 of the slot of probe z / 6 of the chunk.
// Mip k of N levels, the last one at least PREFILTER_MIN_SIZE texels, holds the radiance
// prefiltered for the perceptual roughness k / (N - 1) with n = v = r. Each sample reads the
// source mip whose texels span the solid angle the sample stands for.
@compute @workgroup_size(8, 8)
fn reflect_prefilter(@builtin(global_invocation_id) id: vec3u) {
    let size = textureDimensions(prefiltered).x;
    let probe = id.z / 6u;
    let face = id.z % 6u;
    if (id.x >= size || id.y >= size || probe >= params.size.y) {
        return;
    }

    let full = max(params.size.x, 1u);
    let levels = max(firstLeadingBit(full / PREFILTER_MIN_SIZE) + 1u, 2u);
    let mip = firstLeadingBit(full) - firstLeadingBit(max(size, 1u));
    let roughness = min(f32(mip) / f32(levels - 1u), 1.0);
    let a = roughness * roughness;
    let n = cube_direction(face, id.xy, size);
    let texel_angle = 4.0 * PI / (6.0 * f32(full) * f32(full));
    var sum = vec3f(0.0);
    var weight = 0.0;
    for (var i = 0u; i < PREFILTER_SAMPLES; i++) {
        let h = ggx_half(hammersley(i, PREFILTER_SAMPLES), a, n);
        let ndh = max(dot(n, h), 0.0);
        let l = 2.0 * ndh * h - n;
        let ndl = dot(n, l);
        if (ndl > 0.0) {
            let k = ndh * ndh * (a * a - 1.0) + 1.0;
            let pdf = a * a / (PI * k * k) * 0.25;
            let sample_angle = 1.0 / (f32(PREFILTER_SAMPLES) * max(pdf, 1e-6));
            let lod = max(0.5 * log2(sample_angle / texel_angle) + 1.0, 0.0);
            sum += textureSampleLevel(source, source_sampler, l, probe, lod).rgb * ndl;
            weight += ndl;
        }
    }

    let radiance = sum / max(weight, 1e-6);
    textureStore(prefiltered, id.xy, cube_layer(probe, face), vec4f(radiance, 1.0));
}

// L2 SH irradiance of an environment cubemap, in the basis, order and convolution of project,
// into the first SH_COEFFICIENTS entries of volume. cube_source holds the six faces of a box
// filtered mip level of the cubemap, at most 16 texels per edge, in the WebGPU cubemap
// convention. A texel whose radiance is not finite adds nothing.
@compute @workgroup_size(64)
fn environment_sh(@builtin(local_invocation_index) lane: u32) {
    let size = max(textureDimensions(cube_source).x, 1u);
    let per_face = size * size;
    var sum: array<vec3f, 9>;
    for (var t = lane; t < per_face * 6u; t += GROUP) {
        let face = t / per_face;
        let texel = vec2u((t % per_face) % size, (t % per_face) / size);
        let d = cube_direction(face, texel, size);
        let c = textureLoad(cube_source, texel, face, 0);
        let radiance = select(vec3f(0.0), c.rgb, covered(vec4f(c.rgb, 1.0))) *
            texel_solid_angle(texel.x, texel.y, size);
        let y_lm = basis(d);
        for (var k = 0u; k < SH_COEFFICIENTS; k++) {
            sum[k] += radiance * y_lm[k];
        }
    }

    reduce_sh(lane, sum);
    if (lane < SH_COEFFICIENTS) {
        volume[lane] = vec4f(partial[0][lane] * band(lane), 0.0);
    }
}
