#include <box.h>
#include <bake_test.h>
#include "mesh_cache.h"
#include <math.h>

#include "../../../geometry_assets.h"

static ecs_world_t *surface_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    return world;
}

static bool enclosed(ecs_world_t *world, ecs_entity_t e, vec3 point) {
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    return box_surface_enclosed(shape, mesh, point);
}

void Parametric_sweep_shared_topology(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t a = ecs_new(world), b = ecs_new(world);
    Sweep first = test_sweep(world), second = first;
    second.scale[0] = 2;
    second.taper = 0.25f;
    ecs_set_ptr(world, a, Sweep, &first);
    ecs_set_ptr(world, b, Sweep, &second);
    const ShapeMesh *sa = ecs_get(world, a, ShapeMesh), *sb = ecs_get(world, b, ShapeMesh);
    test_uint(sa->mesh, sb->mesh);
    test_assert(memcmp(&sa->surface, &sb->surface, sizeof(sa->surface)) != 0);
    test_flt(sa->scale[0], 1);
    ecs_fini(world);
}

void Parametric_sweep_evaluation(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t e = ecs_new(world);
    Sweep sweep = test_sweep(world);
    ecs_set_ptr(world, e, Sweep, &sweep);
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, e, SurfaceOuter, 0.25f, 0.5f, &sample), true);
    test_flt(sample.position[1], 1.5f);
    test_flt(hypotf(sample.position[0], sample.position[2]), 1);
    test_flt(glm_vec3_norm(sample.normal), 1);
    test_assert(sample.normal[0] * sample.position[0] + sample.normal[2] * sample.position[2] > 0.99f);
    test_bool(box_surface_evaluate(world, e, SurfaceStart, 0.2f, 0.7f, &sample), true);
    test_assert(sample.normal[1] < -0.99f);
    test_bool(box_surface_evaluate(world, e, SurfaceEnd, 0.2f, 0.7f, &sample), true);
    test_assert(sample.normal[1] > 0.99f);
    ecs_fini(world);
}

void Parametric_sweep_hollow(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t e = ecs_new(world);
    Sweep sweep = test_sweep(world);
    sweep.profile = test_circle(world, 0.75f);
    ecs_set_ptr(world, e, Sweep, &sweep);
    test_bool(enclosed(world, e, (vec3){0, 1.5f, 0}), false);
    test_bool(enclosed(world, e, (vec3){0.875f, 1.5f, 0}), true);
    test_bool(enclosed(world, e, (vec3){2, 1.5f, 0}), false);
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, e, SurfaceInner, 0.2f, 0.5f, &sample), true);
    test_assert(sample.normal[0] * sample.position[0] + sample.normal[2] * sample.position[2] < 0);
    ecs_fini(world);
}

void Parametric_sweep_frame(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t e = ecs_new(world);
    Sweep sweep = test_sweep(world);
    test_route_bend(world, sweep.path, 2, -2);
    ecs_set_ptr(world, e, Sweep, &sweep);
    box_surface_sample_t previous;
    box_surface_evaluate(world, e, SurfaceOuter, 0.1f, 0, &previous);
    for (int i = 1; i <= 100; i ++) {
        box_surface_sample_t sample;
        box_surface_evaluate(world, e, SurfaceOuter, 0.1f, (float)i / 100, &sample);
        test_assert(isfinite(sample.normal[0]));
        test_assert(glm_vec3_dot(previous.normal, sample.normal) > 0.9f);
        previous = sample;
    }
    ecs_fini(world);
}

void Parametric_sweep_strip(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t e = ecs_new(world);
    Sweep sweep = test_sweep(world);
    sweep.profile = 0;
    sweep.contour = test_path2(world, (vec2[]){{-1, 0}, {1, 0}}, 2, false);
    ecs_set_ptr(world, e, Sweep, &sweep);
    box_surface_sample_t a;
    test_bool(box_surface_evaluate(world, e, SurfaceOuter, 0.5f, 0.5f, &a), true);
    test_bool(box_surface_evaluate(world, e, SurfaceInner, 0.5f, 0.5f, &a), false);
    test_bool(box_surface_evaluate(world, e, SurfaceStart, 0.5f, 0.5f, &a), false);
    ecs_fini(world);
}

void Parametric_rounded_contour(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t e = ecs_new(world);
    Sweep sweep = test_sweep(world);
    sweep.scale[0] = 2;
    ecs_set_ptr(world, e, Sweep, &sweep);
    box_surface_sample_t a, b;
    box_surface_evaluate(world, e, SurfaceOuter, 0.24999f, 0.5f, &a);
    box_surface_evaluate(world, e, SurfaceOuter, 0.25001f, 0.5f, &b);
    test_assert(glm_vec3_distance(a.position, b.position) < 0.001f);
    test_assert(fabsf(hypotf(a.position[0], a.position[2]) - 1) < 0.001f);
    ecs_fini(world);
}

void Parametric_loft(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t e = ecs_new(world);
    ecs_entity_t profile = test_circle(world, 0);
    LoftSection sections[3] = {
        {.offset = {0, 0, 0}, .profile = profile, .scale = {1, 1}},
        {.offset = {1, 2, 0}, .profile = profile, .scale = {2, 1}},
        {.offset = {0, 4, 0}, .profile = profile, .scale = {1, 0.5f}}
    };
    Loft loft = {.cap_start = true, .cap_end = true};
    ecs_vec_init_t(NULL, &loft.sections, LoftSection, 3);
    ecs_vec_set_count_t(NULL, &loft.sections, LoftSection, 3);
    memcpy(ecs_vec_first(&loft.sections), sections, sizeof(sections));
    ecs_set_ptr(world, e, Loft, &loft);
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, e, SurfaceOuter, 0, 0.5f, &sample), true);
    test_flt(sample.position[0], 3);
    test_flt(sample.position[1], 2);
    ecs_vec_fini_t(NULL, &loft.sections, LoftSection);
    test_bool(enclosed(world, e, (vec3){1, 2, 0}), true);
    ecs_fini(world);
}

void Parametric_lathe_cavity(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t e = ecs_new(world);
    vec2 outer[2] = {{2, 0}, {2, 3}}, inner[2] = {{1.5f, 0}, {1.5f, 3}};
    Lathe lathe = {.cap_start = true, .cap_end = true};
    lathe.profile = test_path2(world, outer, 2, false);
    lathe.inner = test_path2(world, inner, 2, false);
    ecs_set_ptr(world, e, Lathe, &lathe);
    test_bool(enclosed(world, e, (vec3){0, 1.5f, 0}), false);
    test_bool(enclosed(world, e, (vec3){1.75f, 1.5f, 0}), true);
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, e, SurfaceStart, 0, 0.5f, &sample), true);
    test_flt(sample.position[0], 1.75f);
    Lathe *value = ecs_get_mut(world, e, Lathe);
    value->cap_end = false;
    ecs_modified(world, e, Lathe);
    test_bool(box_surface_evaluate(world, e, SurfaceEnd, 0, 0.5f, &sample), false);
    test_bool(box_surface_evaluate(world, e, SurfaceStart, 0, 0.5f, &sample), true);
    ecs_fini(world);
}

void Parametric_shells(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, EllipsoidShell, {.radius = {2, 3, 4}, .longitude_end = GLM_PI * 2,
        .latitude_start = -GLM_PI_2, .latitude_end = GLM_PI_2, .thickness = 0.2f});
    box_surface_sample_t outer, inner;
    box_surface_evaluate(world, e, SurfaceOuter, 0, 0.5f, &outer);
    box_surface_evaluate(world, e, SurfaceInner, 0, 0.5f, &inner);
    test_flt(outer.position[0], 2);
    test_flt(inner.position[0], 1.8f);
    test_bool(enclosed(world, e, (vec3){0, 0, 0}), false);
    test_bool(enclosed(world, e, (vec3){1.9f, 0, 0}), true);
    ecs_entity_t t = ecs_new(world);
    ecs_set(world, t, TorusShell, {.radius = 3, .tube = 1, .ring_end = GLM_PI * 2,
        .tube_end = GLM_PI * 2, .thickness = 0.2f});
    test_bool(enclosed(world, t, (vec3){0, 0, 0}), false);
    test_bool(enclosed(world, t, (vec3){3, 0, 0}), false);
    test_bool(enclosed(world, t, (vec3){3.9f, 0, 0}), true);
    ecs_fini(world);
}

void Parametric_pole_frame(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, EllipsoidShell, {.radius = {2, 3, 4}, .longitude_end = GLM_PI * 2,
        .latitude_start = -GLM_PI_2, .latitude_end = GLM_PI_2});
    box_surface_sample_t sample;
    for (int i = 0; i < 2; i ++) {
        test_bool(box_surface_evaluate(world, e, SurfaceOuter, 0.2f, (float)i, &sample), true);
        test_assert(isfinite(sample.normal[0]));
        test_flt(glm_vec3_norm(sample.normal), 1);
        test_flt(glm_vec3_norm(sample.tangent), 1);
        test_assert(fabsf(glm_vec3_dot(sample.normal, sample.tangent)) < 0.01f);
    }
    ecs_fini(world);
}

static ecs_entity_t profile_with_hole(ecs_world_t *world) {
    vec2 points[8] = {{-2, -2}, {2, -2}, {2, 2}, {-2, 2},
        {-1, -1}, {-1, 1}, {1, 1}, {1, -1}};
    int32_t hole = 4;
    return test_profile(world, points, 8, &hole, 1);
}

void Parametric_profile_holes(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t asset = profile_with_hole(world);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ProfileExtrusion, {.profile = asset, .depth = 3,
        .cap_start = true, .cap_end = true});
    test_bool(enclosed(world, e, (vec3){0, 0, 0}), false);
    test_bool(enclosed(world, e, (vec3){1.5f, 0, 0}), true);
    test_bool(enclosed(world, e, (vec3){1.5f, 2, 0}), false);
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, e, SurfaceStart, 0.5f, 0.5f, &sample), false);
    test_bool(box_surface_evaluate(world, e, SurfaceStart, 0.875f, 0.5f, &sample), true);
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    test_assert(mesh->index_count > 48);
    test_assert(mesh->surface_asset_count >= 8);
    ecs_fini(world);
}

void Parametric_profile_shared_depth(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t asset = profile_with_hole(world);
    ecs_entity_t a = ecs_new(world), b = ecs_new(world);
    ecs_set(world, a, ProfileExtrusion, {.profile = asset, .depth = 1});
    ecs_set(world, b, ProfileExtrusion, {.profile = asset, .depth = 4});
    test_uint(ecs_get(world, a, ShapeMesh)->mesh, ecs_get(world, b, ShapeMesh)->mesh);
    test_flt(ecs_get(world, a, ShapeMesh)->max[1], 0.5f);
    test_flt(ecs_get(world, b, ShapeMesh)->max[1], 2);
    Profile2D *profile = ecs_get_mut(world, asset, Profile2D);
    Path2D *path = ecs_get_mut(world, profile->outer, Path2D);
    PathSegment2D *segments = ecs_vec_first(&path->segments);
    segments[0].end[0] = 3;
    ecs_modified(world, profile->outer, Path2D);
    test_flt(ecs_get(world, a, ShapeMesh)->max[0], 3);
    test_flt(ecs_get(world, b, ShapeMesh)->max[0], 3);
    test_uint(ecs_get(world, a, ShapeMesh)->mesh, ecs_get(world, b, ShapeMesh)->mesh);
    ecs_remove(world, asset, Profile2D);
    test_uint(ecs_get(world, a, ShapeMesh)->mesh, 0);
    ecs_fini(world);
}

void Parametric_attachments(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t target = ecs_new(world), attachment = ecs_new(world);
    Sweep sweep = test_sweep(world);
    ecs_set_ptr(world, target, Sweep, &sweep);
    ecs_set(world, target, Position, {5, 0, 0});
    ecs_set(world, attachment, SurfaceAttachment, {.target = target,
        .region = SurfaceOuter, .u = 0.25f, .v = 0.5f, .offset = 0.5f});
    ecs_progress(world, 0);
    const WorldTransform *m = ecs_get(world, attachment, WorldTransform);
    test_not_null(m);
    test_assert(fabsf(m->m[3][0] - 3.5f) < 0.001f);
    test_flt(m->m[3][1], 1.5f);
    sweep.scale[0] = sweep.scale[1] = 2;
    ecs_set_ptr(world, target, Sweep, &sweep);
    ecs_progress(world, 0);
    m = ecs_get(world, attachment, WorldTransform);
    test_assert(fabsf(m->m[3][0] - 2.5f) < 0.001f);
    ecs_set(world, target, Position, {7, 0, 0});
    ecs_progress(world, 0);
    m = ecs_get(world, attachment, WorldTransform);
    test_assert(fabsf(m->m[3][0] - 4.5f) < 0.001f);
    ecs_remove(world, attachment, SurfaceAttachment);
    test_bool(ecs_has_id(world, attachment, ManualTransform), false);
    ecs_fini(world);
}

void Parametric_lod_independent(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t target = ecs_new(world);
    Sweep sweep = test_sweep(world);
    test_route_bend(world, sweep.path, 2, 0);
    ecs_set_ptr(world, target, Sweep, &sweep);
    box_surface_sample_t before, after;
    box_surface_evaluate(world, target, SurfaceOuter, 0.123f, 0.456f, &before);
    sweep.segments = 64;
    sweep.rings = 64;
    ecs_set_ptr(world, target, Sweep, &sweep);
    box_surface_evaluate(world, target, SurfaceOuter, 0.123f, 0.456f, &after);
    test_assert(memcmp(&before, &after, sizeof(before)) == 0);
    ecs_fini(world);
}

void Parametric_regions(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t e = ecs_new(world);
    Sweep sweep = test_sweep(world);
    sweep.cap_start = false;
    sweep.cap_end = false;
    ecs_set_ptr(world, e, Sweep, &sweep);
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, e, SurfaceInner, 0, 0, &sample), false);
    test_bool(box_surface_evaluate(world, e, SurfaceStart, 0, 0, &sample), false);
    test_bool(box_surface_evaluate(world, e, SurfaceOuter, -1, 0, &sample), false);
    ecs_add(world, e, SurfaceMaterials);
    SurfaceMaterials *materials = ecs_get_mut(world, e, SurfaceMaterials);
    materials->outer.color = (rgba){255, 0, 0, 255};
    ecs_modified(world, e, SurfaceMaterials);
    uint32_t color;
    memcpy(&color, &ecs_get(world, e, ShapeMesh)->surface.p[20][0], sizeof(color));
    test_uint(color, 0xff0000ffu);
    uint32_t parameters;
    memcpy(&parameters, &ecs_get(world, e, ShapeMesh)->surface.p[21][1], sizeof(parameters));
    test_uint(parameters, 0x8000ffffu);
    ecs_remove(world, e, SurfaceMaterials);
    test_flt(ecs_get(world, e, ShapeMesh)->surface.p[20][0], 0);
    ecs_fini(world);
}

void Parametric_prism(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t e = ecs_new(world);
    BeveledPrism prism = {.depth = 2, .inset = 0.2f, .offset = {0.5f, 0}, .bevel = 0.1f};
    vec2 points[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    ecs_vec_init_t(NULL, &prism.footprint, vec2, 4);
    ecs_vec_set_count_t(NULL, &prism.footprint, vec2, 4);
    memcpy(ecs_vec_first(&prism.footprint), points, sizeof(points));
    ecs_set_ptr(world, e, BeveledPrism, &prism);
    ecs_vec_fini_t(NULL, &prism.footprint, vec2);
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, e, SurfaceOuter, 0, 0, &sample), true);
    test_flt(sample.position[0], -0.9f);
    test_flt(sample.position[1], -1);
    test_flt(sample.position[2], -0.9f);
    box_surface_evaluate(world, e, SurfaceOuter, 0, 1, &sample);
    test_flt(sample.position[0], -0.2f);
    test_flt(sample.position[1], 1);
    test_flt(sample.position[2], -0.7f);
    test_bool(enclosed(world, e, (vec3){0, 0, 0}), true);
    ecs_fini(world);
}

void Parametric_inherited_edit(void) {
    ecs_world_t *world = surface_world();
    ecs_add_pair(world, ecs_id(Sweep), EcsOnInstantiate, EcsInherit);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    Sweep sweep = test_sweep(world);
    ecs_set_ptr(world, prefab, Sweep, &sweep);
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    uint32_t mesh = ecs_get(world, instance, ShapeMesh)->mesh;
    sweep.scale[0] = sweep.scale[1] = 2;
    ecs_set_ptr(world, prefab, Sweep, &sweep);
    test_uint(ecs_get(world, instance, ShapeMesh)->mesh, mesh);
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, instance, SurfaceOuter, 0, 0.5f, &sample), true);
    test_flt(hypotf(sample.position[0], sample.position[2]), 2);
    ecs_fini(world);
}

void Parametric_script_authoring(void) {
    ecs_world_t *world = surface_world();
    test_int(ecs_script_run(world, "surfaces",
        "using box.geometry\n"
        "edge { Path2D: {start: {-1,-1}, segments: [{kind: Line, end: {1,-1}},"
        "{kind: Line, end: {1,1}}, {kind: Line, end: {-1,1}}], closed: true}}\n"
        "profile { Profile2D: {outer: edge}}\n"
        "route { Path3D: {segments: [{kind: Line, end: {0,1,0}}]}}\n"
        "curve { Sweep: {path: route, profile: profile}}\n"
        "body { Loft: {sections: [{profile: profile}, {profile: profile, offset: {0,2,0}}]}}\n"
        "cup { Lathe: {profile: edge}}\n"
        "frame { ProfileExtrusion: {profile: profile, depth: 0.2, cap_end: true}}\n", NULL), 0);
    const Sweep *sweep = ecs_get(world, ecs_lookup(world, "curve"), Sweep);
    test_not_null(sweep);
    test_flt(sweep->taper, 1);
    test_flt(sweep->scale[0], 1);
    test_uint(ecs_get(world, ecs_lookup(world, "body"), ShapeMesh)->surface.p[0][0], 2);
    test_uint(ecs_get(world, ecs_lookup(world, "cup"), ShapeMesh)->surface.p[0][0], 3);
    test_uint(ecs_get(world, ecs_lookup(world, "frame"), ShapeMesh)->surface.p[0][0], 7);
    ecs_fini(world);
}

void Parametric_attachment_chain(void) {
    ecs_world_t *world = surface_world();
    ecs_entity_t a = ecs_new(world), b = ecs_new(world), target = ecs_new(world);
    Sweep sweep = test_sweep(world);
    ecs_set_ptr(world, target, Sweep, &sweep);
    ecs_set_ptr(world, b, Sweep, &sweep);
    ecs_set(world, a, SurfaceAttachment, {.target = b, .u = 0, .v = 0.5f});
    ecs_set(world, b, SurfaceAttachment, {.target = target, .u = 0.25f, .v = 0.5f});
    ecs_set(world, target, Position, {2, 0, 0});
    for (int i = 0; i < 2; i ++) {
        ecs_set(world, target, Position, {2 + 4 * i, 0, 0});
        ecs_progress(world, 0);
        const WorldTransform *ma = ecs_get(world, a, WorldTransform);
        const WorldTransform *mb = ecs_get(world, b, WorldTransform);
        box_surface_sample_t sample;
        box_surface_evaluate(world, b, SurfaceOuter, 0, 0.5f, &sample);
        vec3 expected;
        glm_mat4_mulv3(ECS_CONST_CAST(vec4*, mb->m), sample.position, 1, expected);
        test_assert(glm_vec3_distance(expected, ECS_CONST_CAST(float*, ma->m[3])) < 0.0001f);
    }
    ecs_fini(world);
}
