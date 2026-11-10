// Indirect light of the mesh shaders: the ambient of the view, the probe volume (scene_baked),
// the reflection probes (scene_reflections) and the environment (scene_environment). The includer
// includes mesh_common.wgsl first.
#pragma once

// Indirect light of a fragment, irradiance over pi: in the direction of its normal for the
// diffuse term and in the direction its specular term samples.
struct Indirect {
    diffuse: vec3f,
    specular: vec3f,
};

// Ambient light of the view in a direction: the SH of its environment with scene_environment,
// else the hemisphere ambient of its scene.
fn view_ambient(dir: vec3f) -> vec3f {
    if (scene_environment) {
        return ambient(dir);
    }

    return mix(view.ambient_ground.rgb, view.ambient_sky.rgb, dir.y * 0.5 + 0.5);
}

// Whether the fragments sample the probe volume of the view.
fn view_samples_volume() -> bool {
    return scene_baked && volume.size.w != 0u;
}

// Point of the octahedral map of a direction, in [0, 1] on each axis.
fn octahedral_uv(d: vec3f) -> vec2f {
    var p = d.xy / (abs(d.x) + abs(d.y) + abs(d.z));
    if (d.z < 0.0) {
        p = (1.0 - abs(p.yx)) * select(vec2f(-1.0), vec2f(1.0), p >= vec2f(0.0));
    }

    return p * 0.5 + 0.5;
}

// Distance moments of a texel of the map of a probe. A texel past an edge of the map is the
// texel mirrored across that edge, which the octahedral fold makes its neighbour.
fn visibility_moments(index: u32, x: i32, y: i32) -> vec2f {
    var tx = x;
    var ty = y;
    if (tx < 0 || tx >= VISIBILITY_SIZE) {
        tx = clamp(tx, 0, VISIBILITY_SIZE - 1);
        ty = VISIBILITY_SIZE - 1 - ty;
    }

    if (ty < 0 || ty >= VISIBILITY_SIZE) {
        ty = clamp(ty, 0, VISIBILITY_SIZE - 1);
        tx = VISIBILITY_SIZE - 1 - tx;
    }

    let texels = u32(VISIBILITY_SIZE * VISIBILITY_SIZE);
    return unpack2x16float(probe_visibility[index * texels + u32(tx + ty * VISIBILITY_SIZE)]);
}

// Chance that a probe sees a point, from the offset of the point from the probe: one up to the
// mean distance its faces saw in that direction, then Chebyshev's bound from the variance,
// cubed, as DDGI weighs its probes. The four texels around the point are read directly unless
// one of them lies past an edge of the map.
fn visibility_from(index: u32, offset: vec3f) -> f32 {
    let len = length(offset);
    let dist = len / volume.origin.w;
    let t = octahedral_uv(offset / max(len, 1e-6)) * f32(VISIBILITY_SIZE) - 0.5;
    let i = vec2i(floor(t));
    let f = t - floor(t);
    var m00: vec2f;
    var m10: vec2f;
    var m01: vec2f;
    var m11: vec2f;
    if (all(i >= vec2i(0)) && all(i < vec2i(VISIBILITY_SIZE - 1))) {
        let row = u32(VISIBILITY_SIZE);
        let at = index * row * row + u32(i.x) + u32(i.y) * row;
        m00 = unpack2x16float(probe_visibility[at]);
        m10 = unpack2x16float(probe_visibility[at + 1u]);
        m01 = unpack2x16float(probe_visibility[at + row]);
        m11 = unpack2x16float(probe_visibility[at + row + 1u]);
    } else {
        m00 = visibility_moments(index, i.x, i.y);
        m10 = visibility_moments(index, i.x + 1, i.y);
        m01 = visibility_moments(index, i.x, i.y + 1);
        m11 = visibility_moments(index, i.x + 1, i.y + 1);
    }
    let low = mix(m00, m10, f.x);
    let high = mix(m01, m11, f.x);
    let m = mix(low, high, f.y);
    if (len <= 1e-6 || dist <= m.x) {
        return 1.0;
    }

    let variance = max(m.y - m.x * m.x, VISIBILITY_VARIANCE);
    let delta = dist - m.x;
    let p = variance / (variance + delta * delta);
    return p * p * p;
}

// Irradiance blended from the 8 probes around a position: trilinear weights times the wrapped
// cosine toward the probe and the visibility from the probe, as in DDGI. Invalid probes are
// skipped. The ambient makes up the weight the visible probes lack below VISIBILITY_FALLBACK, so
// a surface that sees none of its probes does not take the light of the probes behind it.
fn volume_irradiance(world: vec3f, n: vec3f, spec: vec3f) -> Indirect {
    let size = volume.size.xyz;
    let g = clamp((world - volume.origin.xyz) / volume.origin.w, vec3f(0.0), vec3f(size - 1u));
    let base = min(vec3u(floor(g)), max(size, vec3u(2u)) - 2u);
    let t = clamp(g - vec3f(base), vec3f(0.0), vec3f(1.0));
    let cells = max(size, vec3u(2u)) - 1u;
    let cell = base.x + (base.y + base.z * cells.y) * cells.x;
    let corners = cell_valid[cell >> 2u] >> ((cell & 3u) * 8u);
    let biased = world + n * (VISIBILITY_BIAS * volume.origin.w);
    var diffuse = vec3f(0.0);
    var specular = vec3f(0.0);
    var total = 0.0;
    var reach = 0.0;
    for (var i = 0u; i < 8u; i++) {
        let o = vec3u(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
        let p = min(base + o, size - 1u);
        let index = p.x + (p.y + p.z * size.y) * size.x;
        let valid = (corners >> i) & 1u;
        let trilinear = select(1.0 - t, t, o == vec3u(1u));
        let position = volume.origin.xyz + vec3f(p) * volume.origin.w;
        let to_probe = position - world;
        let dist = length(to_probe);
        let dir = select(n, to_probe / max(dist, 1e-4), dist > 1e-4);
        let wrap = (dot(dir, n) + 1.0) * 0.5;
        let reached = trilinear.x * trilinear.y * trilinear.z * (wrap * wrap + 0.2) * f32(valid);
        if (reached > 0.0) {
            let w = reached * visibility_from(index, biased - position);
            let at = index * SH_COEFFICIENTS;
            let e0 = probes[at].rgb;
            let e1 = probes[at + 1u].rgb;
            let e2 = probes[at + 2u].rgb;
            let e3 = probes[at + 3u].rgb;
            let e4 = probes[at + 4u].rgb;
            let e5 = probes[at + 5u].rgb;
            let e6 = probes[at + 6u].rgb;
            let e7 = probes[at + 7u].rgb;
            let e8 = probes[at + 8u].rgb;
            diffuse += sh_irradiance(e0, e1, e2, e3, e4, e5, e6, e7, e8, n) * w;
            specular += sh_irradiance(e0, e1, e2, e3, e4, e5, e6, e7, e8, spec) * w;
            total += w;
            reach += reached;
        }
    }

    if (reach <= 0.0) {
        return Indirect(view_ambient(n), view_ambient(spec));
    }

    let fallback = max(VISIBILITY_FALLBACK * reach - total, 0.0);
    let scale = 1.0 / (PI * (total + fallback));
    diffuse = (diffuse + view_ambient(n) * (PI * fallback)) * scale;
    specular = (specular + view_ambient(spec) * (PI * fallback)) * scale;
    return Indirect(max(diffuse, vec3f(0.0)), max(specular, vec3f(0.0)));
}

// A world direction or offset in the frame of the boxes of a reflection probe.
fn probe_local(p: ReflectionProbe, v: vec3f) -> vec3f {
    return vec3f(dot(v, p.axes[0].xyz), dot(v, p.axes[1].xyz), dot(v, p.axes[2].xyz));
}

// Weight of a reflection probe at a position: 1 inside its influence box, falling linearly to 0
// over blend toward its faces and 0 outside; a blend of 0 is a hard edge.
fn probe_fade(p: ReflectionProbe, world: vec3f) -> f32 {
    let inside = p.extent - abs(probe_local(p, world - p.center));
    let d = min(min(inside.x, inside.y), inside.z);
    if (p.blend > 0.0) {
        return clamp(d / p.blend, 0.0, 1.0);
    }

    return select(0.0, 1.0, d >= 0.0);
}

// Direction a reflection probe is sampled in for a ray from a position along dir: from the
// capture point toward where the ray leaves the projection box (parallax correction), or dir
// itself when the box has no volume. A position outside the box that looks away from it uses
// the direction from the capture point to the position.
fn probe_direction(p: ReflectionProbe, world: vec3f, dir: vec3f) -> vec3f {
    if (any(p.projection <= vec3f(0.0))) {
        return dir;
    }

    let o = probe_local(p, world - p.center);
    let d = probe_local(p, dir);
    let safe = select(d, vec3f(1e-6), abs(d) < vec3f(1e-6));
    let exit = max((p.projection - o) / safe, (-p.projection - o) / safe);
    let t = max(min(min(exit.x, exit.y), exit.z), 0.0);
    let result = world + dir * t - p.capture;
    return select(dir, result, dot(result, result) > 1e-8);
}

// Irradiance over pi in a direction from the SH of the cubemap slot of a reflection probe.
fn probe_irradiance(slot: u32, d: vec3f) -> vec3f {
    let at = slot * SH_COEFFICIENTS;
    let e = sh_irradiance(reflection_sh[at].rgb, reflection_sh[at + 1u].rgb,
        reflection_sh[at + 2u].rgb, reflection_sh[at + 3u].rgb, reflection_sh[at + 4u].rgb,
        reflection_sh[at + 5u].rgb, reflection_sh[at + 6u].rgb, reflection_sh[at + 7u].rgb,
        reflection_sh[at + 8u].rgb, d);
    return max(e / PI, vec3f(0.0));
}

fn luminance(c: vec3f) -> f32 {
    return dot(c, vec3f(0.2126, 0.7152, 0.0722));
}

// Prefiltered radiance a reflection probe reflects toward a position along the specular
// direction spec at a mip level. For a view that samples baked lighting it is scaled by the
// luminance of the irradiance grid at the position over that of the SH of the probe, both in
// direction spec, within REFLECTION_NORMALIZE_MIN and REFLECTION_NORMALIZE_MAX, so a dim
// alcove inside a bright probe does not reflect its bright light.
fn probe_radiance(p: ReflectionProbe, world: vec3f, spec: vec3f, mip: f32, grid: vec3f) -> vec3f {
    let d = probe_direction(p, world, normalize(spec));
    let radiance = textureSampleLevel(reflection_cubes, reflection_sampler, d, p.slot, mip).rgb;
    if (!view_samples_volume()) {
        return radiance;
    }

    let own = luminance(probe_irradiance(p.slot, spec));
    let scale = luminance(grid) / max(own, 1e-4);
    return radiance * clamp(scale, REFLECTION_NORMALIZE_MIN, REFLECTION_NORMALIZE_MAX);
}

// Prefiltered radiance of the environment of the view along spec, an infinitely distant probe.
// For a view that samples baked lighting it is normalized as probe_radiance normalizes a probe.
fn environment_radiance(spec: vec3f, roughness: f32, grid: vec3f) -> vec3f {
    let mip = roughness * f32(textureNumLevels(environment_cube) - 1u);
    let d = environment_direction(view, normalize(spec));
    let radiance = textureSampleLevel(environment_cube, reflection_sampler, d, mip).rgb *
        view.environment.x;
    if (!view_samples_volume()) {
        return radiance;
    }

    let own = luminance(environment_irradiance(spec));
    let scale = luminance(grid) / max(own, 1e-4);
    return radiance * clamp(scale, REFLECTION_NORMALIZE_MIN, REFLECTION_NORMALIZE_MAX);
}

// Indirect specular light from the reflection probes of a cluster, in blend order: each one takes
// its fade weight of the weight still left. The scene probe, last in the probe buffer, takes what
// is left, else the environment of the view, else the fallback.
fn reflections(world: vec3f, spec: vec3f, roughness: f32, cluster: u32, fallback: vec3f) -> vec3f {
    let mip = roughness * f32(textureNumLevels(reflection_cubes) - 1u);
    var sum = vec3f(0.0);
    var left = 1.0;
    if (view.grid.w > 0u) {
        let count = clusters[2u + 2u * cluster];
        let listed = cluster_probes(count);
        let first = 1u + 2u * cluster_count(view) + clusters[1u + 2u * cluster] +
            cluster_lights(count);
        for (var i = 0u; i < listed && left > REFLECTION_EPSILON; i++) {
            let p = reflection_probes[clusters[first + i]];
            let w = probe_fade(p, world) * left * f32(p.ready);
            if (w > 0.0) {
                sum += probe_radiance(p, world, spec, mip, fallback) * w;
                left -= w;
            }
        }
    }

    if (left > REFLECTION_EPSILON) {
        let last = view.reflection.x - 1u;
        var rest = fallback;
        if (view.reflection.x > view.grid.w && reflection_probes[last].ready != 0u) {
            rest = probe_radiance(reflection_probes[last], world, spec, mip, fallback);
        } else if (scene_environment && view.environment.w > 0.0) {
            rest = environment_radiance(spec, roughness, fallback);
        }

        sum += rest * left;
    }

    return sum;
}

// Every texture it reads is read with textureSampleLevel, so it may run in non uniform control
// flow.
fn indirect_light(world: vec3f, n: vec3f, spec: vec3f, roughness: f32, cluster: u32) -> Indirect {
    var e: Indirect;
    if (view_samples_volume()) {
        e = volume_irradiance(world, n, spec);
    } else {
        e = Indirect(view_ambient(n), view_ambient(spec));
    }

    if (scene_reflections && view.reflection.x > 0u) {
        e.specular = reflections(world, spec, roughness, cluster, e.specular);
    } else if (scene_environment && view.environment.w > 0.0) {
        e.specular = environment_radiance(spec, roughness, e.specular);
    }

    return e;
}

// Cluster of a fragment, read by the lights and the reflection probes of the view.
fn fragment_cluster(world: vec3f) -> u32 {
    var cluster = 0u;
    if (view.lights.y > view.lights.x || (scene_reflections && view.grid.w > 0u)) {
        cluster = cluster_index(view, cluster_view_position(view, world));
    }

    return cluster;
}

fn view_occlusion(pos: vec4f) -> f32 {
    let size = vec2i(textureDimensions(gtao_image));
    return textureLoad(gtao_image, min(vec2i(pos.xy), size - 1), 0).r;
}

// Specular occlusion from the ambient occlusion (Lagarde, Moving Frostbite to PBR), with a the
// squared perceptual roughness.
fn specular_occlusion(ndv: f32, ao: f32, a: f32) -> f32 {
    return clamp(pow(ndv + ao, exp2(-16.0 * a - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

// Analytic fit of the split sum environment BRDF (Karis), scale and bias of f0.
fn env_brdf(roughness: f32, ndv: f32) -> vec2f {
    let c0 = vec4f(-1.0, -0.0275, -0.572, 0.022);
    let c1 = vec4f(1.0, 0.0425, 1.04, -0.04);
    let r = roughness * c0 + c1;
    let a004 = min(r.x * r.x, exp2(-9.28 * ndv)) * r.x + r.y;
    return vec2f(-1.04, 1.04) * a004 + r.zw;
}
