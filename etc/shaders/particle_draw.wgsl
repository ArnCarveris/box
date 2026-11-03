// Particle draw: one indirect draw per pool of the scene of a view, draw.x vertices per used
// slot, in slot order or, for a pool sorted back to front, in the order of its sort keys. A live
// particle faces the camera, its size wide and turned by its rotation; a free slot collapses
// outside the clip volume. Size, rotation, spin and opacity are drawn from the seed and the spawn
// index, each from its own stream, then the curves of the emitter scale them by the age over the
// life (particle.wgsl), read from their rows of the curve table unless they are constant; the
// size, opacity and color clamp at 0.
//
// A circle is shaded smoothstep(1, 0, d^2) over its quad, a sprite shows a frame of its sheet and
// a billboard is the mesh of the corner buffer turned to face the camera. A soft particle fades
// in front of the depth of the view (group 2), a lit one is shaded by the lights of the scene
// with a wrapped diffuse term, the first directional light through its shadows and light cookie
// (cookie.wgsl), and an emitter with an appearance gets the aerial perspective of
// the view. An additive pool adds color times opacity, an alpha pool blends over the image.
#include "include/lights.wgsl"
#include "include/shadow.wgsl"
#include "include/cookie.wgsl"
#include "include/particle.wgsl"

const PI: f32 = 3.141592653589793;

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read> clusters: array<u32>;
@group(0) @binding(9) var aerial_volume: texture_3d<f32>;
@group(0) @binding(10) var aerial_sampler: sampler;

@group(1) @binding(0) var<uniform> emitter: Emitter;
@group(1) @binding(1) var<storage, read> particles: array<Particle>;
@group(1) @binding(2) var<storage, read> keys: array<vec2u>;
@group(1) @binding(3) var<storage, read> corners: array<Corner>;
@group(1) @binding(4) var sprite_texture: texture_2d<f32>;
@group(1) @binding(5) var sprite_sampler: sampler;
@group(1) @binding(6) var<storage, read> curves: array<f32>;

@group(2) @binding(0) var depth_texture: texture_depth_2d;

struct VertexOut {
    @builtin(position) clip: vec4f,
    @location(0) uv: vec2f,
    @location(1) opacity: f32,
    @location(2) tex: vec2f,
    @location(3) color: vec3f,
    @location(4) world: vec3f,
    @location(5) normal: vec3f,
    @location(6) depth: f32,
    @location(7) @interpolate(flat) proj: vec4f,
};

// Corners of the two triangles of the quad.
const CORNERS = array<vec2f, 6>(
    vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0),
    vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));

// P[2][2], P[3][2], P[2][3] and P[3][3] of the projection of the view, the view times the
// inverse of its rigid view matrix.
fn view_projection_depth() -> vec4f {
    let r = transpose(mat3x3f(view.view[0].xyz, view.view[1].xyz, view.view[2].xyz));
    let t = -(r * view.view[3].xyz);
    let inv = mat4x4f(vec4f(r[0], 0.0), vec4f(r[1], 0.0), vec4f(r[2], 0.0), vec4f(t, 1.0));
    let p = view.view_proj * inv;
    return vec4f(p[2].z, p[3].z, p[2].w, p[3].w);
}

@vertex
fn vs(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> VertexOut {
    var out: VertexOut;
    out.clip = vec4f(2.0, 2.0, 2.0, 1.0);
    var slot = instance;
    if (emitter.draw.y != 0u) {
        slot = keys[emitter.life.w + instance].y;
    }

    let p = particles[slot];
    if (p.remaining == 0u) {
        return out;
    }

    let seed = emitter.spawn.y;
    let life_us = particle_life(seed, p.index, emitter.life.y, emitter.life.z);
    let age = f32(life_us - p.remaining) * 1e-6;
    let life = f32(life_us) * 1e-6;
    let t = particle_norm_age(age, life);
    let size = particle_scaled(particle_draw(seed, p.index, STREAM_SIZE, emitter.speed_size.zw),
        emitter.curve_rows.x, emitter.curve_values.x, t);
    let rotation = particle_draw(seed, p.index, STREAM_ROTATION, emitter.rotation_spin.xy);
    let spin = particle_draw(seed, p.index, STREAM_SPIN, emitter.rotation_spin.zw);
    let angle = radians(particle_rotation(rotation, spin, emitter.curve_rows.z,
        emitter.curve_values.z, emitter.curve_rows.w, emitter.curve_values.w, age, life));
    let c = cos(angle);
    let s = sin(angle);
    let right = vec3f(view.view[0].x, view.view[1].x, view.view[2].x);
    let up = vec3f(view.view[0].y, view.view[1].y, view.view[2].y);
    let toward = vec3f(view.view[0].z, view.view[1].z, view.view[2].z);
    var world: vec3f;
    out.normal = toward;
    if (emitter.look.x == PARTICLE_BILLBOARD) {
        let corner = corners[vertex];
        let local = corner.position.xyz * size;
        let n = corner.normal.xyz;
        world = p.position + right * (local.x * c - local.y * s) +
            up * (local.x * s + local.y * c) + toward * local.z;
        out.normal = right * (n.x * c - n.y * s) + up * (n.x * s + n.y * c) + toward * n.z;
        out.tex = vec2f(corner.position.w, corner.normal.w);
    } else {
        let corner = CORNERS[vertex % 6u];
        var axis_x = vec2f(c, s);
        var axis_y = vec2f(-s, c);
        var stretched = 1.0;
        let velocity = (view.view * vec4f(p.velocity, 0.0)).xy;
        let speed = sqrt(dot(velocity, velocity));
        if (emitter.params.z > 0.0 && speed > 1e-6) {
            axis_x = velocity / speed;
            axis_y = vec2f(-axis_x.y, axis_x.x);
            stretched = particle_stretch(emitter.params.z, speed);
        }

        let turned = axis_x * (corner.x * stretched) + axis_y * corner.y;
        world = p.position + (right * turned.x + up * turned.y) * (0.5 * size);
        out.uv = corner;
        if (emitter.look.x == PARTICLE_SPRITE) {
            let frame = particle_frame(emitter.look.z, emitter.look.w, emitter.params.x, age,
                life);
            let rect = particle_frame_rect(frame, emitter.look.z, emitter.look.w);
            out.tex = rect.xy + (corner * vec2f(0.5, -0.5) + 0.5) * rect.zw;
        }
    }

    out.clip = view.view_proj * vec4f(world, 1.0);
    out.world = world;
    out.depth = -(view.view * vec4f(world, 1.0)).z;
    out.opacity = particle_scaled(
        particle_draw(seed, p.index, STREAM_OPACITY, emitter.opacity_radial.xy),
        emitter.curve_rows.y, emitter.curve_values.y, t);
    out.color = particle_tinted(
        particle_color(seed, p.index, emitter.color.rgb, emitter.variance.rgb),
        emitter.gradient_row, emitter.gradient, t);
    if (emitter.params.y > 0.0) {
        out.proj = view_projection_depth();
    }

    return out;
}

// Hemisphere ambient light of the scene in a direction.
fn ambient(dir: vec3f) -> vec3f {
    return mix(view.ambient_ground.rgb, view.ambient_sky.rgb, dir.y * 0.5 + 0.5);
}

// Radiance of a point or spot light at a distance, as attenuation in mesh.wgsl.
fn attenuation(light: Light, l: vec3f, dist: f32) -> f32 {
    let ratio = dist / max(light.range, 1e-4);
    let window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    let cone = clamp(dot(-l, light.direction) * light.spot_scale + light.spot_offset, 0.0, 1.0);
    let near_ = max(dist, 0.01);
    return window * window / (near_ * near_) * cone * cone;
}

// Diffuse term of a light from direction l: wrapped around a flat particle, which stands for a
// volume, and Lambert for the faces of a billboard mesh.
fn diffuse(n: vec3f, l: vec3f) -> f32 {
    let ndl = dot(n, l);
    if (emitter.look.x == PARTICLE_BILLBOARD) {
        return max(ndl, 0.0);
    }

    return clamp(ndl * 0.5 + 0.5, 0.0, 1.0);
}

// Light a white particle reflects toward the eye: the directional lights, the first through its
// shadow map and light cookie, the point and spot lights of the cluster of the fragment and the
// ambient light. A billboard has no meaningful surface, so it passes a zero normal and the cookie
// reads an isotropic footprint.
fn particle_light(world: vec3f, n: vec3f) -> vec3f {
    var sum = ambient(n);
    var shadow = shadow_factor(world, n);
    if (view.lights.x > 0u) {
        shadow *= light_cookie(world, normalize(lights[0].position), vec3f(0.0));
    }

    for (var i = 0u; i < view.lights.x; i++) {
        let light = lights[i];
        let visible = select(1.0, shadow, i == 0u);
        sum += light.color * (diffuse(n, normalize(light.position)) * visible / PI);
    }

    if (view.lights.y > view.lights.x) {
        let cluster = cluster_index(view, cluster_view_position(view, world));
        let first = 1u + 2u * cluster_count(view) + clusters[1u + 2u * cluster];
        let count = cluster_lights(clusters[2u + 2u * cluster]);
        for (var i = 0u; i < count; i++) {
            let light = lights[clusters[first + i]];
            let delta = light.position - world;
            let dist = length(delta);
            let l = delta / max(dist, 1e-4);
            sum += light.color * (attenuation(light, l, dist) * diffuse(n, l) / PI);
        }
    }

    return sum;
}

// Color of a fragment seen through the atmosphere: an alpha particle is attenuated and adds the
// in scattered light by its coverage, the light an additive one adds is only attenuated.
fn aerial_perspective(color: vec3f, pos: vec4f, world: vec3f) -> vec3f {
    let uv = pos.xy * view.viewport.zw;
    let km = length(world - view.camera.xyz) * view.aerial.x;
    let slices = view.aerial.z;
    let w = (km * view.aerial.y * (slices - 1.0) + 0.5) / slices;
    let ap = textureSampleLevel(aerial_volume, aerial_sampler, vec3f(uv, w), 0.0);
    if (emitter.look.y == PARTICLE_ADDITIVE) {
        return color * ap.a;
    }

    return color * ap.a + ap.rgb;
}

@fragment
fn fs(in: VertexOut) -> @location(0) vec4f {
    let texel = textureSample(sprite_texture, sprite_sampler, in.tex);
    var alpha = clamp(in.opacity, 0.0, 1.0);
    var color = in.color;
    if (emitter.look.x == PARTICLE_CIRCLE) {
        // smoothstep(1, 0, d2), written with ascending edges
        alpha *= 1.0 - smoothstep(0.0, 1.0, dot(in.uv, in.uv));
    } else if (emitter.look.x == PARTICLE_SPRITE) {
        color *= texel.rgb;
        alpha *= texel.a;
    }

    if (emitter.params.y > 0.0) {
        let d = textureLoad(depth_texture, vec2i(in.clip.xy), 0);
        alpha *= particle_soft(particle_view_depth(in.proj, d), in.depth, emitter.params.y);
    }

    if (alpha <= 0.0) {
        discard;
    }

    if (emitter.params.w > 0.0 && view.lights.z != 0u) {
        let lit = particle_light(in.world, normalize(in.normal));
        color *= mix(vec3f(1.0), lit, emitter.params.w);
    }

    if (emitter.draw.w != 0u && view.aerial.w > 0.0) {
        color = aerial_perspective(color, in.clip, in.world);
    }

    if (emitter.look.y == PARTICLE_ADDITIVE) {
        return vec4f(max(color, vec3f(0.0)) * alpha, 0.0);
    }

    return vec4f(color, alpha);
}
