// Particle draw: one indirect draw per pool of the scene of a view, six vertices per used
// slot. A live particle is a quad facing the camera, its size wide and turned by its rotation
// plus its spin times its age, shaded as a soft circle, smoothstep(1, 0, d^2) over the quad,
// in its color times its opacity; a free slot collapses outside the clip volume. Size,
// rotation, spin, opacity and color are drawn from the seed and the spawn index, each from its
// own stream.
#include "include/lights.wgsl"
#include "include/particle.wgsl"

@group(0) @binding(0) var<uniform> view: View;

@group(1) @binding(0) var<uniform> emitter: Emitter;
@group(1) @binding(1) var<storage, read> particles: array<Particle>;

struct VertexOut {
    @builtin(position) clip: vec4f,
    @location(0) uv: vec2f,
    @location(1) opacity: f32,
    @location(2) color: vec3f,
};

// Corners of the two triangles of the quad.
const CORNERS = array<vec2f, 6>(
    vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0),
    vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));

@vertex
fn vs(@builtin(vertex_index) vertex: u32, @builtin(instance_index) slot: u32) -> VertexOut {
    var out: VertexOut;
    let p = particles[slot];
    let corner = CORNERS[vertex];
    out.uv = corner;
    out.opacity = 0.0;
    out.color = vec3f(0.0);
    out.clip = vec4f(2.0, 2.0, 2.0, 1.0);
    if (p.remaining == 0u) {
        return out;
    }

    let seed = emitter.spawn.y;
    let life = particle_life(seed, p.index, emitter.life.y, emitter.life.z);
    let age = f32(life - p.remaining) * 1e-6;
    let size = particle_draw(seed, p.index, STREAM_SIZE, emitter.speed_size.zw);
    let rotation = particle_draw(seed, p.index, STREAM_ROTATION, emitter.rotation_spin.xy);
    let spin = particle_draw(seed, p.index, STREAM_SPIN, emitter.rotation_spin.zw);
    let angle = radians(rotation + spin * age);
    let turned = vec2f(
        corner.x * cos(angle) - corner.y * sin(angle),
        corner.x * sin(angle) + corner.y * cos(angle));
    let right = vec3f(view.view[0].x, view.view[1].x, view.view[2].x);
    let up = vec3f(view.view[0].y, view.view[1].y, view.view[2].y);
    let world = p.position + (right * turned.x + up * turned.y) * (0.5 * size);
    out.clip = view.view_proj * vec4f(world, 1.0);
    out.opacity = particle_draw(seed, p.index, STREAM_OPACITY, emitter.opacity.xy);
    out.color = particle_color(seed, p.index, emitter.color.rgb, emitter.variance.rgb);
    return out;
}

@fragment
fn fs(in: VertexOut) -> @location(0) vec4f {
    // smoothstep(1, 0, d2), written with ascending edges
    let falloff = 1.0 - smoothstep(0.0, 1.0, dot(in.uv, in.uv));
    let alpha = clamp(in.opacity, 0.0, 1.0) * falloff;
    if (alpha <= 0.0) {
        discard;
    }

    return vec4f(in.color, alpha);
}
