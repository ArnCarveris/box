#include <box.h>
#include <bake_test.h>
#include "paths.h"
#include "../../../geometry_assets.h"

static ecs_world_t *assets_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    return world;
}

void Assets_mixed_paths(void)
{
    ecs_world_t *world = assets_world();
    PathSegment2D segments[] = {
        {.kind = PathLine, .end = {2, 0}},
        {.kind = PathCubicBezier, .control1 = {2, 1}, .control2 = {3, 2}, .end = {4, 2}}
    };
    Path2D path = {0};
    for (int i = 0; i < 2; i ++) {
        *ecs_vec_append_t(NULL, &path.segments, PathSegment2D) = segments[i];
    }
    vec2 p;
    test_bool(box_path2_evaluate(&path, 0.25f, p), true);
    test_flt(p[0], 1);
    test_flt(p[1], 0);
    test_bool(box_path2_evaluate(&path, 0.75f, p), true);
    test_flt(p[0], 2.625f);
    test_flt(p[1], 1.375f);
    ecs_entity_t asset = ecs_new(world), lathe = ecs_new(world);
    ecs_set_ptr(world, asset, Path2D, &path);
    ecs_set(world, lathe, Lathe, {.profile = asset});
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, lathe, SurfaceOuter, 0, 0.75f, &sample), true);
    test_flt(sample.position[0], p[0]);
    test_flt(sample.position[1], p[1]);
    path.closed = true;
    test_bool(box_path2_evaluate(&path, 5.0f / 6, p), true);
    test_assert(fabsf(p[0] - 2) < 0.00001f);
    test_assert(fabsf(p[1] - 1) < 0.00001f);
    *ecs_vec_append_t(NULL, &path.segments, PathSegment2D) = (PathSegment2D){
        .kind = PathCubicBezier, .control1 = {3, 2}, .control2 = {0, 1}, .end = {0, 0}};
    test_bool(box_path2_evaluate(&path, 1, p), true);
    test_flt(p[0], 0);
    test_flt(p[1], 0);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment2D);
    ecs_fini(world);
}

void Assets_multisegment_sweep(void)
{
    ecs_world_t *world = assets_world();
    Path3D path = {0};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) =
        (PathSegment3D){.kind = PathLine, .end = {0, 1, 0}};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) =
        (PathSegment3D){.kind = PathCubicBezier, .control1 = {0, 2, 0},
            .control2 = {1, 2, 0}, .end = {1, 3, 0}};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) =
        (PathSegment3D){.kind = PathCubicBezier, .control1 = {1, 4, 0},
            .control2 = {2, 4, 1}, .end = {2, 5, 1}};
    ecs_entity_t route = ecs_new(world), e = ecs_new(world);
    ecs_set_ptr(world, route, Path3D, &path);
    Sweep sweep = {.path = route, .profile = test_circle(world, 0.5f),
        .scale = {0.1f, 0.1f}, .taper = 1, .cap_start = true, .cap_end = true};
    ecs_set_ptr(world, e, Sweep, &sweep);
    for (int i = 0; i <= 30; i ++) {
        vec3 center;
        box_surface_sample_t sample;
        float t = (float)i / 30;
        /* v is a fraction of the path length, mapped to the path parameter */
        float parameter = box_surface_sweep_parameter(&ecs_get(world, e, ShapeMesh)->surface, t);
        test_bool(box_path3_evaluate(&path, parameter, center), true);
        test_bool(box_surface_evaluate(world, e, SurfaceOuter, 0, t, &sample), true);
        test_assert(fabsf(glm_vec3_distance(center, sample.position) - 0.1f) < 0.0001f);
    }
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);
    ecs_fini(world);
}

void Assets_large_counts(void)
{
    ecs_world_t *world = assets_world();
    ecs_entity_t profile = test_circle(world, 0);
    Loft loft = {.cap_start = true, .cap_end = true};
    for (int i = 0; i < 12; i ++) {
        *ecs_vec_append_t(NULL, &loft.sections, LoftSection) =
            (LoftSection){.profile = profile, .offset = {0, i, 0}, .scale = {1, 1}};
    }
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Loft, &loft);
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, e, SurfaceOuter, 0, 1, &sample), true);
    test_flt(sample.position[1], 11);
    vec2 points[20];
    for (int i = 0; i < 20; i ++) {
        points[i][0] = 1;
        points[i][1] = i;
    }
    ecs_entity_t path = test_path2(world, points, 20, false), lathe = ecs_new(world);
    ecs_set(world, lathe, Lathe, {.profile = path});
    test_bool(box_surface_evaluate(world, lathe, SurfaceOuter, 0, 1, &sample), true);
    test_flt(sample.position[1], 19);
    ecs_vec_fini_t(NULL, &loft.sections, LoftSection);
    ecs_fini(world);
}

void Assets_many_holes(void)
{
    ecs_world_t *world = assets_world();
    vec2 points[28] = {{-10, -3}, {10, -3}, {10, 3}, {-10, 3}};
    int32_t holes[6];
    for (int i = 0; i < 6; i ++) {
        float x = -7.5f + i * 3;
        holes[i] = 4 + i * 4;
        glm_vec2_copy((vec2){x - 0.5f, -0.5f}, points[holes[i]]);
        glm_vec2_copy((vec2){x + 0.5f, -0.5f}, points[holes[i] + 1]);
        glm_vec2_copy((vec2){x + 0.5f, 0.5f}, points[holes[i] + 2]);
        glm_vec2_copy((vec2){x - 0.5f, 0.5f}, points[holes[i] + 3]);
    }
    ecs_entity_t profile = test_profile(world, points, 28, holes, 6), e = ecs_new(world);
    ecs_set(world, e, ProfileExtrusion, {.profile = profile, .depth = 2,
        .cap_start = true, .cap_end = true});
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(shape->mesh != 0);
    test_bool(box_profile_contains(&shape->surface, 0, 0), true);
    for (int i = 0; i < 6; i ++) {
        test_bool(box_profile_contains(&shape->surface, -7.5f + i * 3, 0), false);
    }
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    for (int i = 0; i < mesh->index_count; i ++) {
        test_assert(mesh->indices[i] < (uint32_t)mesh->vertex_count);
    }
    ecs_fini(world);
}

void Assets_loft_transform(void)
{
    ecs_world_t *world = assets_world();
    ecs_entity_t profile = test_profile(world, (vec2[]){{1,0},{2,0},{2,1},{1,1}}, 4, NULL, 0);
    LoftSection a = {.profile = profile, .offset = {3, 2, 1}, .rotation = {0, 0, GLM_PI_2}, .scale = {2, 3}};
    LoftSection b = {.profile = profile, .offset = {0, 6, 0}, .rotation = {0.1f, 0.2f, 0.3f}, .scale = {1, 2}};
    Loft loft = {0};
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) = a;
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) = b;
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Loft, &loft);
    box_surface_sample_t sample;
    test_bool(box_surface_evaluate(world, e, SurfaceOuter, 0, 0.5f, &sample), true);
    vec3 pa = {2,0,0}, pb = {1,0,0}, expected;
    mat4 rotation;
    glm_euler_yxz(a.rotation, rotation); glm_mat4_mulv3(rotation, pa, 1, pa); glm_vec3_add(pa, a.offset, pa);
    glm_euler_yxz(b.rotation, rotation); glm_mat4_mulv3(rotation, pb, 1, pb); glm_vec3_add(pb, b.offset, pb);
    glm_vec3_lerp(pa, pb, 0.5f, expected);
    test_assert(glm_vec3_distance(expected, sample.position) < 0.00001f);
    ecs_vec_fini_t(NULL, &loft.sections, LoftSection);
    ecs_fini(world);
}

void Assets_lifecycle(void)
{
    ecs_world_t *world = assets_world();
    ecs_entity_t path = ecs_new(world), profile = ecs_new(world), prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, profile, Profile2D, {.outer = path});
    ecs_set(world, prefab, ProfileExtrusion, {.profile = profile, .depth = 2});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t override = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t alternate = test_circle(world, 0);
    ecs_set(world, override, ProfileExtrusion, {.profile = alternate, .depth = 1});
    uint32_t override_mesh = ecs_get(world, override, ShapeMesh)->mesh;
    test_uint(ecs_get(world, instance, ShapeMesh)->mesh, 0);
    ecs_entity_t source = test_path2(world, (vec2[]){{-1,-1},{1,-1},{1,1},{-1,1}}, 4, true);
    Path2D source_value = *ecs_get(world, source, Path2D);
    ecs_set_ptr(world, path, Path2D, &source_value);
    test_assert(ecs_get(world, instance, ShapeMesh)->mesh != 0);
    test_uint(ecs_get(world, instance, ShapeMesh)->mesh, ecs_get(world, prefab, ShapeMesh)->mesh);
    Path2D *value = ecs_get_mut(world, path, Path2D);
    ((PathSegment2D*)ecs_vec_first(&value->segments))[0].end[0] = 2;
    ecs_modified(world, path, Path2D);
    test_flt(ecs_get(world, instance, ShapeMesh)->max[0], 2);
    test_uint(ecs_get(world, override, ShapeMesh)->mesh, override_mesh);
    ecs_delete(world, path);
    test_uint(ecs_get(world, instance, ShapeMesh)->mesh, 0);
    test_uint(ecs_get(world, prefab, ShapeMesh)->mesh, 0);
    test_uint(ecs_get(world, override, ShapeMesh)->mesh, override_mesh);
    ecs_fini(world);
}

void Assets_invalid(void)
{
    ecs_world_t *world = assets_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sweep, {.path = ecs_new(world), .profile = ecs_new(world), .scale = {1,1}});
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, 0);
    ecs_entity_t invalid = test_profile(world, (vec2[]){{0,0},{1,1},{0,1},{1,0}}, 4, NULL, 0);
    ecs_entity_t extrusion = ecs_new(world);
    ecs_set(world, extrusion, ProfileExtrusion, {.profile = invalid, .depth = 1, .cap_end = true});
    test_uint(ecs_get(world, extrusion, ShapeMesh)->mesh, 0);
    ecs_entity_t a = test_circle(world, 0), b = test_circle(world, 0.5f);
    Loft loft = {0};
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) = (LoftSection){.profile = a, .scale = {1,1}};
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) = (LoftSection){.profile = b, .scale = {1,1}};
    ecs_entity_t l = ecs_new(world);
    ecs_set_ptr(world, l, Loft, &loft);
    test_uint(ecs_get(world, l, ShapeMesh)->mesh, 0);
    ecs_vec_fini_t(NULL, &loft.sections, LoftSection);
    ecs_fini(world);
}

void Assets_documentation(void)
{
    ecs_world_t *world = assets_world();
    const char *script =
        "using box.geometry\n"
        "\n"
        "frame_outline {\n"
        "    Path2D: {\n"
        "        start: {-2, -2},\n"
        "        segments: [\n"
        "            {kind: Line, end: {2, -2}},\n"
        "            {kind: Line, end: {2, 2}},\n"
        "            {kind: Line, end: {-2, 2}}\n"
        "        ],\n"
        "        closed: true\n"
        "    }\n"
        "}\n"
        "\n"
        "frame_opening {\n"
        "    Path2D: {\n"
        "        start: {-1, -1},\n"
        "        segments: [\n"
        "            {kind: Line, end: {1, -1}},\n"
        "            {kind: Line, end: {1, 1}},\n"
        "            {kind: Line, end: {-1, 1}}\n"
        "        ],\n"
        "        closed: true\n"
        "    }\n"
        "}\n"
        "\n"
        "solid_profile {\n"
        "    Profile2D: {outer: frame_outline}\n"
        "}\n"
        "\n"
        "frame_profile {\n"
        "    Profile2D: {\n"
        "        outer: frame_outline,\n"
        "        holes: [frame_opening]\n"
        "    }\n"
        "}\n"
        "\n"
        "frame {\n"
        "    ProfileExtrusion: {\n"
        "        profile: frame_profile,\n"
        "        depth: 0.2,\n"
        "        cap_start: true,\n"
        "        cap_end: true\n"
        "    }\n"
        "}\n"
        "\n"
        "duct_route {\n"
        "    Path3D: {\n"
        "        start: {0, -2, 0},\n"
        "        segments: [\n"
        "            {kind: Line, end: {0, 0, 0}},\n"
        "            {\n"
        "                kind: CubicBezier,\n"
        "                control1: {0, 2, 0},\n"
        "                control2: {2, 2, 0},\n"
        "                end: {2, 4, 0}\n"
        "            },\n"
        "            {\n"
        "                kind: CubicBezier,\n"
        "                control1: {2, 6, 0},\n"
        "                control2: {4, 6, 2},\n"
        "                end: {4, 8, 2}\n"
        "            }\n"
        "        ],\n"
        "        closed: false\n"
        "    }\n"
        "}\n"
        "\n"
        "duct {\n"
        "    Sweep: {\n"
        "        path: duct_route,\n"
        "        profile: frame_profile,\n"
        "        scale: {0.1, 0.1},\n"
        "        taper: 1,\n"
        "        roll: 0,\n"
        "        cap_start: true,\n"
        "        cap_end: true\n"
        "    }\n"
        "}\n"
        "\n"
        "ribbon_edge {\n"
        "    Path2D: {\n"
        "        start: {-0.5, 0},\n"
        "        segments: [{kind: Line, end: {0.5, 0}}],\n"
        "        closed: false\n"
        "    }\n"
        "}\n"
        "\n"
        "ribbon {\n"
        "    Sweep: {\n"
        "        path: duct_route,\n"
        "        contour: ribbon_edge,\n"
        "        scale: {1, 1},\n"
        "        taper: 1,\n"
        "        cap_start: false,\n"
        "        cap_end: false\n"
        "    }\n"
        "}\n"
        "\n"
        "canopy {\n"
        "    Loft: {\n"
        "        sections: [\n"
        "            {\n"
        "                profile: solid_profile,\n"
        "                offset: {0, 0, 0},\n"
        "                rotation: {0, 0, 0},\n"
        "                scale: {1, 0.5}\n"
        "            },\n"
        "            {\n"
        "                profile: solid_profile,\n"
        "                offset: {0.25, 2, 0},\n"
        "                rotation: {0, 0, 0.1},\n"
        "                scale: {0.75, 0.5}\n"
        "            },\n"
        "            {\n"
        "                profile: solid_profile,\n"
        "                offset: {0, 4, 0},\n"
        "                rotation: {0, 0, 0.2},\n"
        "                scale: {0.25, 0.25}\n"
        "            }\n"
        "        ],\n"
        "        cap_start: true,\n"
        "        cap_end: true\n"
        "    }\n"
        "}\n"
        "\n"
        "cup_outer {\n"
        "    Path2D: {\n"
        "        start: {0, 0},\n"
        "        segments: [\n"
        "            {kind: Line, end: {1, 0}},\n"
        "            {\n"
        "                kind: CubicBezier,\n"
        "                control1: {1, 0.6},\n"
        "                control2: {1.2, 1.4},\n"
        "                end: {1.2, 2}\n"
        "            }\n"
        "        ],\n"
        "        closed: false\n"
        "    }\n"
        "}\n"
        "\n"
        "cup_inner {\n"
        "    Path2D: {\n"
        "        start: {0, 0.1},\n"
        "        segments: [\n"
        "            {kind: Line, end: {0.9, 0.1}},\n"
        "            {\n"
        "                kind: CubicBezier,\n"
        "                control1: {0.9, 0.7},\n"
        "                control2: {1.1, 1.4},\n"
        "                end: {1.1, 2}\n"
        "            }\n"
        "        ],\n"
        "        closed: false\n"
        "    }\n"
        "}\n"
        "\n"
        "cup {\n"
        "    Lathe: {\n"
        "        profile: cup_outer,\n"
        "        inner: cup_inner,\n"
        "        cap_start: true,\n"
        "        cap_end: true\n"
        "    }\n"
        "}\n"
        ;
    test_int(ecs_script_run(world, "documentation", script, NULL), 0);
    const char *names[] = {"frame", "duct", "ribbon", "canopy", "cup"};
    for (int i = 0; i < 5; i ++) {
        const ShapeMesh *shape = ecs_get(world, ecs_lookup(world, names[i]), ShapeMesh);
        test_assert(shape && shape->mesh);
    }
    ecs_fini(world);
}

void Assets_shared_edits(void)
{
    ecs_world_t *world = assets_world();
    ecs_entity_t path = test_path2(world, (vec2[]){{-1,-1},{1,-1},{1,1},{-1,1}}, 4, true);
    ecs_entity_t profile = ecs_new(world), extrusion = ecs_new(world), sweep = ecs_new(world), loft_entity = ecs_new(world);
    ecs_set(world, profile, Profile2D, {.outer = path});
    ecs_set(world, extrusion, ProfileExtrusion, {.profile = profile, .depth = 2});
    Sweep value = test_sweep(world);
    value.profile = profile;
    ecs_set_ptr(world, sweep, Sweep, &value);
    Loft loft = {0};
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) = (LoftSection){.profile = profile, .scale = {1,1}};
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) = (LoftSection){.profile = profile, .scale = {1,1}, .offset = {0,2,0}};
    ecs_set_ptr(world, loft_entity, Loft, &loft);
    ecs_entity_t entities[] = {extrusion, sweep, loft_entity};
    uint32_t before[3];
    for (int i = 0; i < 3; i ++) {
        before[i] = ecs_get(world, entities[i], ShapeMesh)->mesh;
    }
    Path2D *p = ecs_get_mut(world, path, Path2D);
    ((PathSegment2D*)ecs_vec_first(&p->segments))[0].end[0] = 2;
    ecs_modified(world, path, Path2D);
    for (int i = 0; i < 3; i ++) {
        const ShapeMesh *shape = ecs_get(world, entities[i], ShapeMesh);
        test_assert(shape->mesh != 0 && shape->mesh != before[i]);
    }
    ecs_remove(world, path, Path2D);
    for (int i = 0; i < 3; i ++) {
        test_uint(ecs_get(world, entities[i], ShapeMesh)->mesh, 0);
    }
    ecs_vec_fini_t(NULL, &loft.sections, LoftSection);
    ecs_fini(world);
}

void Assets_inherited_paths(void)
{
    ecs_world_t *world = assets_world();
    ecs_entity_t source = test_path2(world, (vec2[]){{-1,-1},{1,-1},{1,1},{-1,1}}, 4, true);
    Path2D value = *ecs_get(world, source, Path2D);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set_ptr(world, prefab, Path2D, &value);
    ecs_entity_t path = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t profile = ecs_new(world), e = ecs_new(world);
    ecs_set(world, profile, Profile2D, {.outer = path});
    ecs_set(world, e, ProfileExtrusion, {.profile = profile, .depth = 1});
    test_assert(ecs_get(world, e, ShapeMesh)->mesh);
    Path2D *p = ecs_get_mut(world, prefab, Path2D);
    ((PathSegment2D*)ecs_vec_first(&p->segments))[0].end[0] = 3;
    ecs_modified(world, prefab, Path2D);
    test_flt(ecs_get(world, e, ShapeMesh)->max[0], 3);
    ecs_remove(world, prefab, Path2D);
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, 0);
    ecs_fini(world);
}

void Assets_open_contour(void)
{
    ecs_world_t *world = assets_world();
    Sweep sweep = test_sweep(world);
    sweep.profile = 0;
    sweep.contour = test_path2(world, (vec2[]){{-1,0},{1,0}}, 2, false);
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Sweep, &sweep);
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    vec3 a, b;
    box_surface_vertex(shape, &mesh->vertices[0], a);
    box_surface_vertex(shape, &mesh->vertices[1], b);
    test_flt(glm_vec3_distance(a, b), 2);
    test_uint((int)shape->surface.p[3][3], 1);
    ecs_fini(world);
}

void Assets_triangulation_failure(void)
{
    ecs_world_t *world = assets_world();
    vec2 points[] = {{-1,-1},{1,-1},{1,1},{-1,1},{3,3},{4,3},{4,4},{3,4}};
    int32_t hole = 4;
    ecs_entity_t profile = test_profile(world, points, 8, &hole, 1), e = ecs_new(world);
    ecs_set(world, e, ProfileExtrusion, {.profile = profile, .depth = 1, .cap_start = true});
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, 0);
    ecs_fini(world);
}
