/**
 * @file SmoothExtrusion.c
 * @brief Smooth side normals of ProfileExtrusion, with sharp corners and flat caps.
 */

#include <box.h>
#include <bake_test.h>
#include "mesh_cache.h"
#include <math.h>

#include "../../../geometry_assets.h"

/* An arch: straight foot and sides, two cubic Bezier quarter arcs over the top */
static const char *smooth_arch =
    "using box.geometry\n"
    "arch_outline { Path2D: {start: {-0.72, 0.16}, segments: ["
    "{kind: Line, end: {0.72, 0.16}}, {kind: Line, end: {0.72, 1.5}},"
    "{kind: CubicBezier, end: {0, 2.22}, control1: {0.72, 1.898}, control2: {0.398, 2.22}},"
    "{kind: CubicBezier, end: {-0.72, 1.5}, control1: {-0.398, 2.22}, control2: {-0.72, 1.898}}"
    "], closed: true}}\n"
    "arch { Profile2D: {outer: arch_outline}}\n";

static ecs_world_t *smooth_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    return world;
}

static const box_mesh_t *smooth_mesh(ecs_world_t *world, ecs_entity_t e) {
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(shape != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    test_assert(mesh != NULL);
    return mesh;
}

/** Collect the normals of the bottom side wall vertices at a profile point.
 *
 * @param mesh The unit extrusion mesh
 * @param x The x of the profile point
 * @param z The y of the profile point, on Z
 * @param normals Receives up to 4 normals
 * @return The number of side vertices at the point
 */
static int smooth_side_normals(
    const box_mesh_t *mesh,
    float x,
    float z,
    vec3 normals[4])
{
    int count = 0;
    for (int i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        if (fabsf(v->normal[1]) > 0.5f || fabsf(v->position[1] + 0.5f) > 1e-5f ||
            fabsf(v->position[0] - x) > 1e-4f || fabsf(v->position[2] - z) > 1e-4f)
        {
            continue;
        }
        if (count < 4) {
            glm_vec3_copy(ECS_CONST_CAST(float*, v->normal), normals[count]);
        }
        count ++;
    }
    return count;
}

void SmoothExtrusion_circle(void) {
    ecs_world_t *world = smooth_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ProfileExtrusion, {.profile = test_circle(world, 0), .depth = 1,
        .cap_start = true, .cap_end = true, .smooth = true});
    const box_mesh_t *mesh = smooth_mesh(world, e);
    for (int i = 0; i < 64; i ++) {
        float t = (float)i * 2 * GLM_PI / 64;
        vec3 normals[4];
        test_int(smooth_side_normals(mesh, cosf(t), sinf(t), normals), 2);
        test_assert(glm_vec3_dot(normals[0], normals[1]) > 0.99999f);
        test_assert(normals[0][0] * cosf(t) + normals[0][2] * sinf(t) > 0.9999f);
        test_flt(glm_vec3_norm(normals[0]), 1);
    }
    ecs_fini(world);
}

void SmoothExtrusion_rectangle(void) {
    ecs_world_t *world = smooth_world();
    vec2 points[4] = {{-2, -1}, {2, -1}, {2, 1}, {-2, 1}};
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ProfileExtrusion, {.profile = test_profile(world, points, 4, NULL, 0),
        .depth = 1, .cap_start = true, .cap_end = true, .smooth = true});
    const box_mesh_t *mesh = smooth_mesh(world, e);
    for (int i = 0; i < 4; i ++) {
        vec3 normals[4];
        test_int(smooth_side_normals(mesh, points[i][0], points[i][1], normals), 2);
        test_assert(fabsf(glm_vec3_dot(normals[0], normals[1])) < 1e-5f);
    }
    for (int i = 0; i < mesh->vertex_count; i ++) {
        const float *n = mesh->vertices[i].normal;
        test_assert(fabsf(fabsf(n[0]) + fabsf(n[1]) + fabsf(n[2]) - 1) < 1e-5f);
    }
    ecs_fini(world);
}

void SmoothExtrusion_arc_joints(void) {
    ecs_world_t *world = smooth_world();
    test_int(ecs_script_run(world, "arch", smooth_arch, NULL), 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ProfileExtrusion, {.profile = ecs_lookup(world, "arch"), .depth = 1,
        .cap_start = true, .cap_end = true, .smooth = true});
    const box_mesh_t *mesh = smooth_mesh(world, e);
    vec3 normals[4];

    /* The joint between the two arcs is smooth */
    test_int(smooth_side_normals(mesh, 0, 2.22f, normals), 2);
    test_assert(glm_vec3_dot(normals[0], normals[1]) > 0.99999f);
    test_assert(normals[0][2] > 0.9999f);

    /* So is every sample inside an arc */
    int samples = 0;
    for (int i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        if (fabsf(v->normal[1]) > 0.5f || v->position[2] < 1.55f || v->position[2] > 2.2f) {
            continue;
        }
        test_int(smooth_side_normals(mesh, v->position[0], v->position[2], normals), 2);
        test_assert(glm_vec3_dot(normals[0], normals[1]) > 0.99999f);
        samples ++;
    }
    test_assert(samples > 8);

    /* The joints between the straight sides and the arcs stay sharp: the side keeps its normal */
    for (int side = 0; side < 2; side ++) {
        float x = side ? 0.72f : -0.72f;
        test_int(smooth_side_normals(mesh, x, 1.5f, normals), 2);
        test_assert(glm_vec3_dot(normals[0], normals[1]) < 0.99999f);
        int straight = 0;
        for (int k = 0; k < 2; k ++) {
            straight += fabsf(normals[k][2]) < 1e-5f && normals[k][0] * x > 0;
        }
        test_int(straight, 1);
    }

    /* The corners at the foot stay sharp */
    test_int(smooth_side_normals(mesh, 0.72f, 0.16f, normals), 2);
    test_assert(fabsf(glm_vec3_dot(normals[0], normals[1])) < 1e-5f);
    ecs_fini(world);
}

void SmoothExtrusion_curve_corner(void) {
    /* The arch with a slight kink where the two arcs meet, well under the crease angle */
    ecs_world_t *world = smooth_world();
    test_int(ecs_script_run(world, "kink",
        "using box.geometry\n"
        "kink_outline { Path2D: {start: {-0.72, 0.16}, segments: ["
        "{kind: Line, end: {0.72, 0.16}}, {kind: Line, end: {0.72, 1.5}},"
        "{kind: CubicBezier, end: {0, 2.22}, control1: {0.72, 1.898}, control2: {0.398, 2.17}},"
        "{kind: CubicBezier, end: {-0.72, 1.5}, control1: {-0.398, 2.17}, control2: {-0.72, 1.898}}"
        "], closed: true}}\n"
        "kink { Profile2D: {outer: kink_outline}}\n", NULL), 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ProfileExtrusion, {.profile = ecs_lookup(world, "kink"), .depth = 1,
        .cap_start = true, .cap_end = true, .smooth = true});
    const box_mesh_t *mesh = smooth_mesh(world, e);
    vec3 normals[4];

    /* The kink stays sharp, each side keeps the normal of its own edge */
    test_int(smooth_side_normals(mesh, 0, 2.22f, normals), 2);
    test_assert(glm_vec3_dot(normals[0], normals[1]) < 0.99f);
    test_assert(glm_vec3_dot(normals[0], normals[1]) > 0.9f);
    test_assert(normals[0][0] * normals[1][0] < 0);

    /* Inside the arcs it is still smooth */
    int samples = 0;
    for (int i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        if (fabsf(v->normal[1]) > 0.5f || v->position[2] < 1.55f || v->position[2] > 2.2f) {
            continue;
        }
        test_int(smooth_side_normals(mesh, v->position[0], v->position[2], normals), 2);
        test_assert(glm_vec3_dot(normals[0], normals[1]) > 0.99999f);
        samples ++;
    }
    test_assert(samples > 8);
    ecs_fini(world);
}

void SmoothExtrusion_holes(void) {
    ecs_world_t *world = smooth_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ProfileExtrusion, {.profile = test_circle(world, 0.5f), .depth = 1,
        .cap_start = true, .cap_end = true, .smooth = true});
    const box_mesh_t *mesh = smooth_mesh(world, e);
    for (int i = 0; i < 64; i ++) {
        float t = (float)i * 2 * GLM_PI / 64;
        vec3 normals[4];
        test_int(smooth_side_normals(mesh, cosf(t) * 0.5f, sinf(t) * 0.5f, normals), 2);
        test_assert(glm_vec3_dot(normals[0], normals[1]) > 0.99999f);
        /* The wall of the hole faces the hole */
        test_assert(normals[0][0] * cosf(t) + normals[0][2] * sinf(t) < -0.9999f);
    }
    ecs_fini(world);
}

void SmoothExtrusion_mesh_key(void) {
    ecs_world_t *world = smooth_world();
    ecs_entity_t profile = test_circle(world, 0);
    ecs_entity_t flat = ecs_new(world), smooth = ecs_new(world), other = ecs_new(world);
    ecs_set(world, flat, ProfileExtrusion, {.profile = profile, .depth = 1});
    ecs_set(world, smooth, ProfileExtrusion, {.profile = profile, .depth = 1, .smooth = true});
    ecs_set(world, other, ProfileExtrusion, {.profile = profile, .depth = 3, .smooth = true});
    const ShapeMesh *a = ecs_get(world, flat, ShapeMesh);
    const ShapeMesh *b = ecs_get(world, smooth, ShapeMesh);
    const ShapeMesh *c = ecs_get(world, other, ShapeMesh);
    test_assert(a->mesh != 0);
    test_assert(b->mesh != 0);
    test_assert(a->mesh != b->mesh);
    test_uint(b->mesh, c->mesh);

    /* A rectangle has only sharp corners, still a smooth one does not share the flat mesh */
    vec2 points[4] = {{-2, -1}, {2, -1}, {2, 1}, {-2, 1}};
    ecs_entity_t box = test_profile(world, points, 4, NULL, 0);
    ecs_set(world, flat, ProfileExtrusion, {.profile = box, .depth = 1});
    ecs_set(world, smooth, ProfileExtrusion, {.profile = box, .depth = 1, .smooth = true});
    test_assert(ecs_get(world, flat, ShapeMesh)->mesh != ecs_get(world, smooth, ShapeMesh)->mesh);
    ecs_fini(world);
}

void SmoothExtrusion_default_flat(void) {
    ecs_world_t *world = smooth_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ProfileExtrusion, {.profile = test_circle(world, 0), .depth = 1});
    test_bool(ecs_get(world, e, ProfileExtrusion)->smooth, false);
    test_flt(ecs_get(world, e, ShapeMesh)->surface.p[8][0], 0);
    const box_mesh_t *mesh = smooth_mesh(world, e);
    for (int i = 0; i < 64; i ++) {
        float t = (float)i * 2 * GLM_PI / 64;
        vec3 normals[4];
        test_int(smooth_side_normals(mesh, cosf(t), sinf(t), normals), 2);
        test_assert(glm_vec3_dot(normals[0], normals[1]) < 0.999f);
    }
    ecs_fini(world);
}

void SmoothExtrusion_caps_flat(void) {
    ecs_world_t *world = smooth_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ProfileExtrusion, {.profile = test_circle(world, 0), .depth = 1,
        .cap_start = true, .cap_end = true, .smooth = true});
    const box_mesh_t *mesh = smooth_mesh(world, e);
    int caps = 0;
    for (int i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        if (fabsf(v->normal[1]) > 0.5f) {
            test_flt(fabsf(v->normal[1]), 1);
            test_flt(v->normal[1], v->position[1] > 0 ? 1 : -1);
            caps ++;
        } else {
            test_assert(fabsf(v->normal[1]) < 1e-6f);
        }
    }
    test_int(caps, 128);
    ecs_fini(world);
}

void SmoothExtrusion_evaluation(void) {
    ecs_world_t *world = smooth_world();
    ecs_entity_t profile = test_circle(world, 0);
    ecs_entity_t flat = ecs_new(world), smooth = ecs_new(world);
    ecs_set(world, flat, ProfileExtrusion, {.profile = profile, .depth = 2});
    ecs_set(world, smooth, ProfileExtrusion, {.profile = profile, .depth = 2,
        .cap_start = true, .cap_end = true, .smooth = true});
    box_surface_sample_t before, after;

    /* Across the profile point at a quarter turn the smooth normal is continuous and radial */
    test_bool(box_surface_evaluate(world, smooth, SurfaceOuter, 0.25f - 0.0001f, 0.5f, &before),
        true);
    test_bool(box_surface_evaluate(world, smooth, SurfaceOuter, 0.25f + 0.0001f, 0.5f, &after),
        true);
    test_assert(glm_vec3_dot(before.normal, after.normal) > 0.99999f);
    test_bool(box_surface_evaluate(world, smooth, SurfaceOuter, 0.25f, 0.5f, &before), true);
    test_assert(before.normal[2] > 0.9999f);
    test_flt(glm_vec3_norm(before.normal), 1);

    /* Halfway along a segment the smooth normal points at the middle of the segment */
    float t = 2.5f * 2 * GLM_PI / 64;
    test_bool(box_surface_evaluate(world, smooth, SurfaceOuter, 2.5f / 64, 0.5f, &before), true);
    test_assert(before.normal[0] * cosf(t) + before.normal[2] * sinf(t) > 0.9999f);

    /* The flat normal jumps from one segment to the next */
    test_bool(box_surface_evaluate(world, flat, SurfaceOuter, 0.25f - 0.0001f, 0.5f, &before),
        true);
    test_bool(box_surface_evaluate(world, flat, SurfaceOuter, 0.25f + 0.0001f, 0.5f, &after),
        true);
    test_assert(glm_vec3_dot(before.normal, after.normal) < 0.999f);

    /* The caps stay flat */
    test_bool(box_surface_evaluate(world, smooth, SurfaceEnd, 0.5f, 0.5f, &before), true);
    test_assert(before.normal[1] > 0.9999f);
    ecs_fini(world);
}

void SmoothExtrusion_attachment(void) {
    ecs_world_t *world = smooth_world();
    ecs_entity_t target = ecs_new(world), attachment = ecs_new(world);
    ecs_set(world, target, ProfileExtrusion, {.profile = test_circle(world, 0), .depth = 2,
        .smooth = true});
    ecs_set(world, target, Position, {0, 0, 0});
    ecs_set(world, attachment, SurfaceAttachment, {.target = target,
        .region = SurfaceOuter, .u = 2.5f / 64, .v = 0.5f, .offset = 0.5f});
    ecs_progress(world, 0);
    const WorldTransform *m = ecs_get(world, attachment, WorldTransform);
    test_not_null(m);
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, target, SurfaceOuter, 2.5f / 64, 0.5f, &sample), true);
    vec3 expected;
    glm_vec3_scale(sample.normal, 0.5f, expected);
    glm_vec3_add(sample.position, expected, expected);
    test_assert(glm_vec3_distance(expected, (vec3){(float)m->position[0],
        (float)m->position[1], (float)m->position[2]}) < 0.001f);
    float t = 2.5f * 2 * GLM_PI / 64;
    test_assert(sample.normal[0] * cosf(t) + sample.normal[2] * sinf(t) > 0.9999f);
    ecs_fini(world);
}
