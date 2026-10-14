// Screen space ambient occlusion from the depth of the view. There is no normal buffer, so
// `occlusion` derives the normal of a pixel from the view positions of its neighbors: per
// axis it takes the neighbor whose depth is closest, which keeps the normal of the surface at
// a silhouette. 16 hemisphere samples around that normal, turned by a 4x4 pattern of angles,
// count how many land behind the depth of the view. The occlusion image holds the occlusion and
// the view depth of every pixel; `composite` averages the occlusion over the 4x4 pattern among
// pixels of similar depth and darkens the image by it.
#include "include/effect.wgsl"

struct Ssao {
    proj: mat4x4f,
    inv_proj: mat4x4f,
    // radius, bias, intensity, unused
    params: vec4f,
};

@group(0) @binding(2) var<uniform> ssao: Ssao;
@group(0) @binding(3) var depth_texture: texture_depth_2d;
@group(0) @binding(4) var ao_texture: texture_2d<f32>;

const SAMPLES: u32 = 16u;

const KERNEL = array<vec3f, 16>(
    vec3f(-0.1317, 0.0111, 0.0710),
    vec3f(-0.1502, 0.0025, 0.0309),
    vec3f(-0.0241, -0.1561, 0.0412),
    vec3f(-0.0378, 0.1638, 0.0640),
    vec3f(0.0782, -0.0718, 0.1732),
    vec3f(0.1475, -0.0592, 0.1704),
    vec3f(0.1026, -0.0562, 0.2428),
    vec3f(0.2314, 0.1564, 0.1405),
    vec3f(0.0595, 0.0201, 0.3570),
    vec3f(-0.4049, -0.0128, 0.1067),
    vec3f(0.1814, 0.2853, 0.3435),
    vec3f(0.1759, 0.1490, 0.5013),
    vec3f(-0.3117, -0.1653, 0.5197),
    vec3f(0.5231, -0.3185, 0.3615),
    vec3f(-0.5073, -0.0286, 0.6190),
    vec3f(0.4406, 0.0358, 0.7806),
);

const PATTERN = array<f32, 16>(
    0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);

fn depth_at(texel: vec2i) -> f32 {
    let size = vec2i(textureDimensions(depth_texture));
    return textureLoad(depth_texture, clamp(texel, vec2i(0), size - 1), 0);
}

fn view_position(texel: vec2i, depth: f32) -> vec3f {
    let size = vec2f(textureDimensions(depth_texture));
    let uv = (vec2f(texel) + 0.5) / size;
    let ndc = vec4f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    let p = ssao.inv_proj * ndc;
    return p.xyz / p.w;
}

fn view_z(depth: f32) -> f32 {
    let z = ssao.inv_proj[2].z * depth + ssao.inv_proj[3].z;
    let w = ssao.inv_proj[2].w * depth + ssao.inv_proj[3].w;
    return z / w;
}

fn neighbor(center: vec3f, texel: vec2i, a: vec2i, b: vec2i) -> vec3f {
    let pa = view_position(texel + a, depth_at(texel + a));
    let pb = view_position(texel + b, depth_at(texel + b));
    return select(center - pb, pa - center, abs(pa.z - center.z) < abs(pb.z - center.z));
}

@fragment fn occlusion(v: FullscreenOut) -> @location(0) vec4f {
    let texel = vec2i(v.pos.xy);
    let depth = depth_at(texel);
    if (depth <= 0.0) {
        return vec4f(1.0, 0.0, 0.0, 1.0);
    }

    let radius = ssao.params.x;
    let p = view_position(texel, depth);
    let fade = 1.0 - smoothstep(radius * 40.0, radius * 100.0, -p.z);
    if (fade <= 0.0) {
        return vec4f(1.0, p.z, 0.0, 1.0);
    }

    let dx = neighbor(p, texel, vec2i(1, 0), vec2i(-1, 0));
    let dy = neighbor(p, texel, vec2i(0, 1), vec2i(0, -1));
    let n = normalize(cross(dy, dx));
    let index = u32(texel.x & 3) + u32(texel.y & 3) * 4u;
    let angle = (PATTERN[index] + 0.5) * (6.283185307 / 16.0);
    let jitter = vec3f(cos(angle), sin(angle), 0.0);
    var t = jitter - n * dot(jitter, n);
    if (dot(t, t) < 1e-6) {
        t = vec3f(0.0, 1.0, 0.0) - n * n.y;
    }
    t = normalize(t);
    let b = cross(n, t);
    let size = vec2f(textureDimensions(depth_texture));
    var occluded = 0.0;
    for (var i = 0u; i < SAMPLES; i++) {
        let k = KERNEL[i];
        let s = p + (t * k.x + b * k.y + n * k.z) * radius;
        let clip = ssao.proj * vec4f(s, 1.0);
        let uv = vec2f(clip.x, -clip.y) / clip.w * 0.5 + 0.5;
        if (any(uv < vec2f(0.0)) || any(uv >= vec2f(1.0))) {
            continue;
        }

        let scene_depth = depth_at(vec2i(uv * size));
        if (scene_depth <= 0.0) {
            continue;
        }

        let scene_z = view_z(scene_depth);
        let range = smoothstep(0.0, 1.0, radius / max(abs(p.z - scene_z), 1e-5));
        occluded += select(0.0, range, scene_z >= s.z + ssao.params.y);
    }

    let ao = 1.0 - occluded / f32(SAMPLES) * ssao.params.z * fade;
    return vec4f(clamp(ao, 0.0, 1.0), p.z, 0.0, 1.0);
}

@fragment fn composite(v: FullscreenOut) -> @location(0) vec4f {
    let texel = vec2i(v.pos.xy);
    let src = textureLoad(input_texture, texel, 0);
    let depth = depth_at(texel);
    if (depth <= 0.0) {
        return src;
    }

    let size = vec2i(textureDimensions(ao_texture));
    let z = textureLoad(ao_texture, texel, 0).g;
    let tolerance = max(abs(z) * 0.05, ssao.params.x * 0.5);
    var sum = 0.0;
    var weight = 0.0;
    for (var y = -2; y < 2; y++) {
        for (var x = -2; x < 2; x++) {
            let at = clamp(texel + vec2i(x, y), vec2i(0), size - 1);
            let s = textureLoad(ao_texture, at, 0);
            let w = select(0.0, 1.0, s.g < 0.0 && abs(s.g - z) < tolerance);
            sum += s.r * w;
            weight += w;
        }
    }

    let ao = select(1.0, sum / weight, weight > 0.0);
    return vec4f(src.rgb * ao, src.a);
}
