// Particle draw: one instanced draw per blend and sprite of the pools of the scene of a view,
// every instance a slot of a chunk of CHUNK slots of a pool that the chunk buffer names. A
// circle or sprite faces the camera; a card is a quad in world space that tumbles around its
// axis and lies flat once it rests on the ground plane. A particle shades at its world position
// and is placed relative to the render origin: a pool kept in the space of model through the
// translation of model minus the render origin, which the backend takes in f64.
#include "include/lights.wgsl"
#include "include/shadow.wgsl"
#include "include/cookie.wgsl"
#include "include/particle.wgsl"
#include "include/fog_blend.wgsl"

const PI: f32 = 3.141592653589793;

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read> clusters: array<u32>;
@group(0) @binding(9) var aerial_volume: texture_3d<f32>;
@group(0) @binding(10) var aerial_sampler: sampler;

// The blocks of the pools of the packet, BOX_GPU_PARTICLE_STRIDE apart
struct EmitterBlock {
    @size(624) emitter: Emitter,
};

// Slots of a pool one entry of the chunk buffer draws, BOX_GPU_PARTICLE_CHUNK
const CHUNK: u32 = 64u;

@group(1) @binding(0) var<storage, read> blocks: array<EmitterBlock>;
@group(1) @binding(1) var<storage, read> particles: array<Particle>;
// The pool and the first slot of every chunk the views of the packet draw
@group(1) @binding(2) var<storage, read> chunks: array<vec2u>;
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
    // x: ParticleShape, y: ParticleBlend, z: 1 when the emitter has a ParticleAppearance
    @location(8) @interpolate(flat) look: vec4u,
    // x: softness, y: fraction lit
    @location(9) @interpolate(flat) params: vec2f,
    // The position relative to the render origin, which the sun shadows are looked up at
    @location(10) relative: vec3f,
};

// Screen width in pixels a card seen edge on keeps, so it shows as a thin line without holes.
const CARD_MIN_PIXELS: f32 = 1.5;

// Share of its size a card at rest is lifted above the ground plane once it lies flat, so it
// does not fight the depth of the ground it lies on.
const CARD_REST_LIFT: f32 = 0.02;

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
    let chunk = chunks[instance / CHUNK];
    let emitter = blocks[chunk.x].emitter;
    let slot = chunk.y + instance % CHUNK;
    if (slot >= emitter.pool.x) {
        return out;
    }

    let p = particles[emitter.gradient_max.z + slot];
    if (p.remaining == 0u) {
        return out;
    }

    out.look = vec4u(emitter.look.x, emitter.look.y, emitter.styled, 0u);
    out.params = emitter.params.yw;

    let seed = emitter.spawn.y;
    let life_us = particle_life(seed, p.index, emitter.life.y, emitter.life.z);
    let age = f32(life_us - p.remaining) * 1e-6;
    let life = f32(life_us) * 1e-6;
    let t = particle_norm_age(age, life);
    let size = particle_scaled(particle_draw(seed, p.index, STREAM_SIZE, emitter.speed_size.zw),
        emitter.curve_rows.x, emitter.curve_max.x, emitter.curve_values.x, t,
        particle_curve_blend(seed, p.index, CURVE_SIZE, emitter.curve_max.x));
    let rotation = particle_draw(seed, p.index, STREAM_ROTATION, emitter.rotation_spin.xy);
    let spin = particle_draw(seed, p.index, STREAM_SPIN, emitter.rotation_spin.zw);
    let angle = radians(particle_rotation(rotation, spin, emitter.curve_rows.z,
        emitter.curve_max.z, emitter.curve_values.z,
        particle_curve_blend(seed, p.index, CURVE_ROTATION, emitter.curve_max.z),
        emitter.curve_rows.w, emitter.curve_max.w, emitter.curve_values.w,
        particle_curve_blend(seed, p.index, CURVE_SPIN, emitter.curve_max.w), age, life));
    let c = cos(angle);
    let s = sin(angle);
    let right = vec3f(view.view[0].x, view.view[1].x, view.view[2].x);
    let up = vec3f(view.view[0].y, view.view[1].y, view.view[2].y);
    let toward = vec3f(view.view[0].z, view.view[1].z, view.view[2].z);
    out.normal = toward;
    let corner = CORNERS[vertex % 6u];
    var axis_x = vec2f(c, s);
    var axis_y = vec2f(-s, c);
    var stretched = 1.0;
    // A local emitter draws its particles through its current model
    var center = p.position;
    var travel = p.velocity;
    // The center relative to the render origin, which the clip position is projected from
    var relative = p.position - view.render_delta.xyz;
    if (emitter.gradient_max.w != 0u) {
        let placed = mat3x3f(emitter.model[0].xyz, emitter.model[1].xyz, emitter.model[2].xyz) *
            p.position;
        center = placed + emitter.model[3].xyz;
        relative = placed + emitter.relative.xyz;
        travel = (emitter.model * vec4f(p.velocity, 0.0)).xyz;
    }

    var moving = travel;
    if (emitter.params.z > 0.0 && emitter.gradient_max.y != 0u) {
        moving = particle_moving(travel, particle_motion_scaled(
            particle_motion_velocity(emitter.motion, seed, p.index, t), emitter.turbulence.z));
    }

    let velocity = (view.view * vec4f(moving, 0.0)).xy;
    let speed = sqrt(dot(velocity, velocity));
    if (emitter.params.z > 0.0 && speed > 1e-6) {
        axis_x = velocity / speed;
        axis_y = vec2f(-axis_x.y, axis_x.x);
        stretched = particle_stretch(emitter.params.z, speed);
    }

    let turned = axis_x * (corner.x * stretched) + axis_y * corner.y;
    var spread = (right * turned.x + up * turned.y) * (0.5 * size);
    if (emitter.look.x == PARTICLE_CARD) {
        // The rotation around its axis, held at the age it came to rest once it rests
        let word = particle_card_word(emitter.gradient_max.z, emitter.life.w, slot);
        var rested = -1.0;
        var landed = 0.0;
        if (word != 0u) {
            let at = f32(word - 1u) * 1e-6;
            rested = age - at;
            landed = radians(particle_rotation(rotation, spin, emitter.curve_rows.z,
                emitter.curve_max.z, emitter.curve_values.z,
                particle_curve_blend(seed, p.index, CURVE_ROTATION, emitter.curve_max.z),
                emitter.curve_rows.w, emitter.curve_max.w, emitter.curve_values.w,
                particle_curve_blend(seed, p.index, CURVE_SPIN, emitter.curve_max.w), at,
                life));
        }

        let q = particle_card(seed, p.index, angle, landed, rested);
        let ex = particle_quat_rotate(q, vec3f(1.0, 0.0, 0.0));
        let ey = particle_quat_rotate(q, vec3f(0.0, 1.0, 0.0));
        let n = particle_quat_rotate(q, vec3f(0.0, 0.0, 1.0));
        let offset = ex * corner.x + ey * corner.y;
        let ortho = view.lights.w != 0u;
        let to_eye = view.camera.xyz - center;
        let eye = select(to_eye * inverseSqrt(max(dot(to_eye, to_eye), 1e-12)), toward,
            ortho || dot(to_eye, to_eye) <= 1e-12);
        let facing = dot(n, eye);
        let side = select(-1.0, 1.0, facing >= 0.0);
        out.normal = n * side;

        // Widen a card seen near edge on to CARD_MIN_PIXELS across: move the corners along the
        // screen direction the card is thin in, by the share of the size it lacks.
        let distance = select(max(-(view.view * vec4f(center, 1.0)).z, 0.0), 1.0, ortho);
        let pixel = 2.0 * view.cluster.y * distance / max(view.viewport.y, 1.0);
        let lack = max(CARD_MIN_PIXELS * pixel / max(size, 1e-6) - abs(facing), 0.0);
        let thin = n - eye * facing;
        let thin_len = length(thin);
        var widen = vec3f(0.0);
        if (lack > 0.0 && thin_len > 1e-6) {
            let across = (eye - n * facing) / thin_len;
            widen = thin / thin_len * (-side * lack * dot(offset, across));
        }

        var lift = 0.0;
        if (rested >= 0.0) {
            lift = CARD_REST_LIFT * size * particle_card_ease(rested);
        }

        spread = (offset + widen) * (0.5 * size) + vec3f(0.0, lift, 0.0);
    }

    let world = center + spread;

    out.uv = corner;
    if (emitter.look.x == PARTICLE_SPRITE || emitter.look.x == PARTICLE_CARD) {
        let frame = particle_frame(emitter.look.z, emitter.look.w, emitter.params.x, age,
            life);
        let rect = particle_frame_rect(frame, emitter.look.z, emitter.look.w);
        out.tex = rect.xy + (corner * vec2f(0.5, -0.5) + 0.5) * rect.zw;
    }

    out.clip = view.render_view_proj * vec4f(relative + spread, 1.0);
    out.world = world;
    out.relative = relative + spread;
    out.depth = -(view.view * vec4f(world, 1.0)).z;
    out.opacity = particle_scaled(
        particle_draw(seed, p.index, STREAM_OPACITY, emitter.opacity_radial.xy),
        emitter.curve_rows.y, emitter.curve_max.y, emitter.curve_values.y, t,
        particle_curve_blend(seed, p.index, CURVE_OPACITY, emitter.curve_max.y));
    out.color = particle_tinted(
        particle_color(seed, p.index, emitter.color.rgb, emitter.variance.rgb),
        emitter.gradient_row, emitter.gradient_max.x, emitter.gradient, t,
        particle_curve_blend(seed, p.index, CURVE_COLOR, emitter.gradient_max.x));
    if (emitter.params.y > 0.0) {
        out.proj = view_projection_depth();
    }

    return out;
}

// Hemisphere ambient light of the scene in a direction.
fn ambient(dir: vec3f) -> vec3f {
    return mix(view.ambient_ground.rgb, view.ambient_sky.rgb, dir.y * 0.5 + 0.5);
}

// Radiance of a point or spot light at a distance, as attenuation in include/attenuation.wgsl.
fn attenuation(light: Light, l: vec3f, dist: f32) -> f32 {
    let ratio = dist / max(light.range, 1e-4);
    let window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    let cone = clamp(dot(-l, light.direction) * light.spot_scale + light.spot_offset, 0.0, 1.0);
    let near_ = max(dist, 0.01);
    return window * window / (near_ * near_) * cone * cone;
}

// Diffuse term of a light from direction l, wrapped around a flat particle, which stands for a
// volume.
fn diffuse(n: vec3f, l: vec3f) -> f32 {
    return clamp(dot(n, l) * 0.5 + 0.5, 0.0, 1.0);
}

// Light a white particle reflects toward the eye: the directional lights, the first through its
// shadow map and light cookie, the point and spot lights of the cluster of the fragment and the
// ambient light. A billboard has no meaningful surface, so it passes a zero normal and the cookie
// reads an isotropic footprint. relative is the position relative to the render origin, which
// the shadows are looked up at.
fn particle_light(world: vec3f, relative: vec3f, n: vec3f) -> vec3f {
    var sum = ambient(n);
    var shadow = shadow_factor(world, relative, n);
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
fn aerial_perspective(color: vec3f, pos: vec4f, world: vec3f, blend: u32) -> vec3f {
    let uv = pos.xy * view.viewport.zw;
    let km = length(world - view.camera.xyz) * view.aerial.x;
    let slices = view.aerial.z;
    let w = (km * view.aerial.y * (slices - 1.0) + 0.5) / slices;
    let ap = textureSampleLevel(aerial_volume, aerial_sampler, vec3f(uv, w), 0.0);
    if (blend == PARTICLE_ADDITIVE) {
        return color * ap.a;
    }

    return color * ap.a + ap.rgb;
}

@fragment
fn fs(in: VertexOut) -> @location(0) vec4f {
    let texel = textureSample(sprite_texture, sprite_sampler, in.tex);
    var alpha = clamp(in.opacity, 0.0, 1.0);
    var color = in.color;
    if (in.look.x == PARTICLE_CIRCLE) {
        // smoothstep(1, 0, d2), written with ascending edges
        alpha *= 1.0 - smoothstep(0.0, 1.0, dot(in.uv, in.uv));
    } else if (in.look.x == PARTICLE_SPRITE || in.look.x == PARTICLE_CARD) {
        color *= texel.rgb;
        alpha *= texel.a;
    }

    if (in.params.x > 0.0) {
        let d = textureLoad(depth_texture, vec2i(in.clip.xy), 0);
        alpha *= particle_soft(particle_view_depth(in.proj, d), in.depth, in.params.x);
    }

    if (alpha <= 0.0) {
        discard;
    }

    if (in.params.y > 0.0 && view.lights.z == 1u) {
        let lit = particle_light(in.world, in.relative, normalize(in.normal));
        color *= mix(vec3f(1.0), lit, in.params.y);
    }

    if (in.look.z != 0u && view.aerial.w > 0.0) {
        color = aerial_perspective(color, in.clip, in.world, in.look.y);
    }

    if (view.fog_grid.w > 0.0) {
        // The fog in front of a particle dims the light an additive one adds; an alpha one is
        // dimmed and adds the in scattered light by its coverage.
        let fog = fog_froxels(in.clip, in.world);
        color *= fog.a;
        if (in.look.y != PARTICLE_ADDITIVE) {
            color += fog.rgb;
        }
    }

    if (in.look.y == PARTICLE_ADDITIVE) {
        return vec4f(max(color, vec3f(0.0)) * alpha, 0.0);
    }

    return vec4f(color, alpha);
}
