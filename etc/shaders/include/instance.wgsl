// Instance data of a batch, one per slot of each stream of its instance page. The streams are
// separate buffers of the page with the same slots, bound together as the instance group:
//
// - binding 0, InstanceTransform (box_render_transform_t): the three rows of the affine model
//   matrix with the instance scale, which the cull, the shadow casters and the capture cull
//   read, and which a moved instance uploads;
// - binding 2, InstanceShading (box_render_shading_t): the material constants. Material holds
//   metallic, roughness and, for an alpha tested instance (AlphaCutoff), in z the cutoff below
//   which its fragments are discarded; color.a is its alpha. ids.x is the clip index of a 2D
//   instance (shape2d.wgsl), yzw are 0;
// - binding 3, SurfaceData (box_render_surface_t): the parametric surface the vertex stage
//   evaluates (surface.wgsl), zero for a plain mesh.
//
// Binding 1 holds the surface assets of the mesh of the draw.
#pragma once
#include "surface.wgsl"

struct InstanceTransform {
    rows: array<vec4f, 3>,
};

struct InstanceShading {
    color: vec4f,
    emissive: vec4f,
    material: vec4f,
    uv: vec4f,
    ids: vec4u,
};

// Model matrix of an instance, its fourth row (0, 0, 0, 1).
fn instance_model(t: InstanceTransform) -> mat4x4f {
    let r = t.rows;
    return mat4x4f(vec4f(r[0].x, r[1].x, r[2].x, 0.0), vec4f(r[0].y, r[1].y, r[2].y, 0.0),
        vec4f(r[0].z, r[1].z, r[2].z, 0.0), vec4f(r[0].w, r[1].w, r[2].w, 1.0));
}
