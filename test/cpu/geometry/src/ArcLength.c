#include <box.h>
#include <bake_test.h>
#include "paths.h"
#include "../../../geometry_assets.h"

static ecs_world_t *arc_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    return world;
}

/* A straight path along +X of two lines, 1 and 4 long, so its two segments split the path
 * parameter evenly but not its length. */
static ecs_entity_t arc_path(ecs_world_t *world) {
    Path3D path = {0};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) =
        (PathSegment3D){.kind = PathLine, .end = {1, 0, 0}};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) =
        (PathSegment3D){.kind = PathLine, .end = {5, 0, 0}};
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Path3D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);
    return e;
}

/* A 4 by 1 rectangle, edges of 4, 1, 4 and 1, 10 around. */
static ecs_entity_t arc_rectangle(ecs_world_t *world) {
    return test_profile(world, (vec2[]){{0, 0}, {4, 0}, {4, 1}, {0, 1}}, 4, NULL, 0);
}

static ecs_entity_t arc_sweep(ecs_world_t *world, ecs_entity_t profile, float sx, float sy) {
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sweep, {.path = arc_path(world), .profile = profile,
        .scale = {sx, sy}, .taper = 1, .cap_start = true, .cap_end = true, .rings = 10});
    return e;
}

static const box_surface_data_t *arc_data(ecs_world_t *world, ecs_entity_t e) {
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(shape != NULL);
    test_assert(shape->surface.p[0][0] == 1);
    return &shape->surface;
}

void ArcLength_path_table(void) {
    ecs_world_t *world = arc_world();
    ecs_entity_t e = arc_sweep(world, test_circle(world, 0), 0.1f, 0.1f);
    const box_surface_data_t *d = arc_data(world, e);
    test_assert(fabsf(d->p[9][2] - 5) < 0.001f);
    test_assert(fabsf(box_surface_sweep_parameter(d, 0) - 0) < 0.0001f);
    test_assert(fabsf(box_surface_sweep_parameter(d, 1) - 1) < 0.0001f);
    /* The first fifth of the length is the first segment, half the parameter. The table is linear
     * between its steps of 1/64, so at the kink between the segments it is off by a step at most */
    test_assert(fabsf(box_surface_sweep_parameter(d, 0.2f) - 0.5f) < 1.0f / 64);
    test_assert(fabsf(box_surface_sweep_parameter(d, 0.1f) - 0.25f) < 0.002f);
    test_assert(fabsf(box_surface_sweep_parameter(d, 0.6f) - 0.75f) < 0.002f);
    float previous = -1;
    for (int i = 0; i <= 100; i ++) {
        float t = box_surface_sweep_parameter(d, (float)i / 100);
        test_assert(t >= previous);
        previous = t;
    }
    ecs_fini(world);
}

void ArcLength_path_curve(void) {
    ecs_world_t *world = arc_world();
    /* A Catmull-Rom path through unevenly spaced points */
    Path3D path = {0};
    float xs[] = {0.5f, 1, 4, 4.5f, 9};
    for (int i = 0; i < 5; i ++) {
        *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) =
            (PathSegment3D){.kind = PathCatmullRom, .end = {xs[i], 0, i % 2 ? 1.0f : 0.0f}};
    }
    ecs_entity_t route = ecs_new(world);
    ecs_set_ptr(world, route, Path3D, &path);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sweep, {.path = route, .profile = test_circle(world, 0),
        .scale = {0.1f, 0.1f}, .taper = 1, .rings = 64});
    const box_surface_data_t *d = arc_data(world, e);
    /* Even steps of the length fraction cover even distances along the path */
    vec3 points[65];
    float total = 0;
    for (int i = 0; i <= 64; i ++) {
        test_bool(box_path3_evaluate(&path, box_surface_sweep_parameter(d, (float)i / 64),
            points[i]), true);
        if (i) {
            total += glm_vec3_distance(points[i - 1], points[i]);
        }
    }
    test_assert(fabsf(total - d->p[9][2]) < 0.01f * d->p[9][2]);
    for (int i = 1; i <= 64; i ++) {
        float step = glm_vec3_distance(points[i - 1], points[i]);
        test_assert(fabsf(step - total / 64) < 0.05f * total / 64);
    }
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);
    ecs_fini(world);
}

void ArcLength_profile_table(void) {
    ecs_world_t *world = arc_world();
    ecs_entity_t e = arc_sweep(world, arc_rectangle(world), 1, 1);
    const box_surface_data_t *d = arc_data(world, e);
    test_assert(fabsf(d->p[9][0] - 10) < 0.0001f);
    test_assert(fabsf(box_surface_profile_length(d, 0) - 10) < 0.0001f);
    test_flt(d->p[9][1], 0);
    test_assert(fabsf(box_surface_profile_parameter(d, 0, 0) - 0) < 0.0001f);
    test_assert(fabsf(box_surface_profile_parameter(d, 0.4f, 0) - 0.25f) < 0.0001f);
    test_assert(fabsf(box_surface_profile_parameter(d, 0.45f, 0) - 0.375f) < 0.0001f);
    test_assert(fabsf(box_surface_profile_parameter(d, 0.5f, 0) - 0.5f) < 0.0001f);
    test_assert(fabsf(box_surface_profile_parameter(d, 0.7f, 0) - 0.625f) < 0.0001f);
    test_assert(fabsf(box_surface_profile_parameter(d, 1, 0) - 1) < 0.0001f);
    ecs_fini(world);
}

void ArcLength_profile_scale(void) {
    ecs_world_t *world = arc_world();
    /* The scale makes the edges 4, 2, 4 and 2 */
    ecs_entity_t e = arc_sweep(world, arc_rectangle(world), 1, 2);
    const box_surface_data_t *d = arc_data(world, e);
    test_assert(fabsf(d->p[9][0] - 12) < 0.0001f);
    test_assert(fabsf(box_surface_profile_parameter(d, 4.0f / 12, 0) - 0.25f) < 0.0001f);
    test_assert(fabsf(box_surface_profile_parameter(d, 5.0f / 12, 0) - 0.375f) < 0.0001f);
    ecs_fini(world);
}

void ArcLength_profile_holes(void) {
    ecs_world_t *world = arc_world();
    ecs_entity_t e = arc_sweep(world, test_circle(world, 0.5f), 1, 1);
    const box_surface_data_t *d = arc_data(world, e);
    float outer = 64 * 2 * sinf(GLM_PI / 64), inner = outer * 0.5f;
    test_assert(fabsf(d->p[9][0] - outer) < 0.001f);
    test_assert(fabsf(d->p[9][1] - inner) < 0.001f);
    /* Evenly spaced points keep the parameter */
    test_assert(fabsf(box_surface_profile_parameter(d, 0.3f, 1) - 0.3f) < 0.0001f);
    ecs_fini(world);
}

void ArcLength_open_contour(void) {
    ecs_world_t *world = arc_world();
    ecs_entity_t contour = test_path2(world, (vec2[]){{0, 0}, {1, 0}, {4, 0}}, 3, false);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sweep, {.path = arc_path(world), .contour = contour,
        .scale = {1, 1}, .taper = 1, .rings = 10});
    const box_surface_data_t *d = arc_data(world, e);
    test_assert(fabsf(d->p[9][0] - 4) < 0.0001f);
    test_assert(fabsf(box_surface_profile_parameter(d, 0.25f, 0) - 0.5f) < 0.0001f);
    test_assert(fabsf(box_surface_profile_parameter(d, 0.625f, 0) - 0.75f) < 0.0001f);
    ecs_fini(world);
}

void ArcLength_mesh_uvs(void) {
    ecs_world_t *world = arc_world();
    ecs_entity_t e = arc_sweep(world, arc_rectangle(world), 1, 1);
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    test_assert(mesh != NULL);
    /* The wall vertices: u by the length around the profile, v by the length along the path,
     * and the rings at the path parameter of their length */
    float expected[5] = {0, 0.4f, 0.5f, 0.9f, 1};
    int walls = 0;
    for (int i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *vertex = &mesh->vertices[i];
        if (vertex->position[2] >= 2) {
            continue;
        }
        walls ++;
        int point = (int)roundf(vertex->position[0] * 4);
        bool closing = point == 0 && vertex->uv[0] > 0.5f;
        test_assert(fabsf(vertex->uv[0] - expected[closing ? 4 : point]) < 0.0001f);
        float parameter = box_surface_sweep_parameter(&shape->surface, vertex->uv[1]);
        test_assert(fabsf(vertex->position[1] - parameter) < 0.0001f);
        int ring = (int)roundf(vertex->uv[1] * 10);
        test_assert(fabsf(vertex->uv[1] - (float)ring / 10) < 0.0001f);
    }
    test_int(walls, 4 * 11 * 2);
    ecs_fini(world);
}

void ArcLength_evaluate(void) {
    ecs_world_t *world = arc_world();
    ecs_entity_t e = arc_sweep(world, arc_rectangle(world), 1, 1);
    box_surface_sample_t sample;
    /* u 0.45 is halfway up the first short edge, v 0.6 is 3 along the path of 5 */
    test_bool(box_surface_evaluate(world, e, SurfaceOuter, 0.45f, 0.6f, &sample), true);
    test_assert(fabsf(sample.position[0] - 3) < 0.01f);
    test_assert(fabsf(hypotf(sample.position[1], sample.position[2]) -
        hypotf(4, 0.5f)) < 0.001f);
    test_bool(box_surface_evaluate(world, e, SurfaceOuter, 0, 0.2f, &sample), true);
    test_assert(fabsf(sample.position[0] - 1) < 0.01f);
    ecs_fini(world);
}

void ArcLength_scale_ratio_key(void) {
    ecs_world_t *world = arc_world();
    ecs_entity_t profile = arc_rectangle(world);
    ecs_entity_t a = arc_sweep(world, profile, 1, 1);
    ecs_entity_t b = arc_sweep(world, profile, 3, 3);
    ecs_entity_t c = arc_sweep(world, profile, 1, 3);
    const ShapeMesh *sa = ecs_get(world, a, ShapeMesh);
    const ShapeMesh *sb = ecs_get(world, b, ShapeMesh);
    const ShapeMesh *sc = ecs_get(world, c, ShapeMesh);
    /* Each Sweep has its own path entity with the same points */
    test_uint(sa->mesh, sb->mesh);
    test_assert(sa->mesh != sc->mesh);
    test_assert(fabsf(sb->surface.p[9][0] - 30) < 0.001f);
    ecs_fini(world);
}

void ArcLength_corner_ring(void) {
    ecs_world_t *world = arc_world();
    /* An L of two lines, 1 along +X and 1.5 along +Z, so the corner is at path parameter 0.5 and
     * at 0.4 of the length, between the rings 9 and 10 of 24 */
    Path3D path = {0};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) =
        (PathSegment3D){.kind = PathLine, .end = {1, 0, 0}};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) =
        (PathSegment3D){.kind = PathLine, .end = {1, 0, 1.5f}};
    ecs_entity_t route = ecs_new(world);
    ecs_set_ptr(world, route, Path3D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sweep, {.path = route, .profile = test_circle(world, 0),
        .scale = {0.1f, 0.1f}, .taper = 1, .cap_start = true, .cap_end = true, .rings = 24});
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(shape != NULL);
    float parameters[2], fractions[2];
    test_int(box_surface_sweep_corners(&shape->surface, parameters, fractions), 1);
    test_flt(parameters[0], 0.5f);
    test_assert(fabsf(fractions[0] - 0.4f) < 0.0001f);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    test_assert(mesh != NULL);
    int corner = 0, walls = 0;
    float previous[2] = {-1, -1};
    for (int i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *vertex = &mesh->vertices[i];
        if (vertex->position[2] >= 2) {
            continue;
        }
        walls ++;
        int side = walls % 2;
        if (vertex->position[1] == 0.5f) {
            corner ++;
            test_assert(fabsf(vertex->uv[1] - 0.4f) < 0.0001f);
        }
        /* The rows of each edge go along the path, the ring after the first of an edge
         * starting over */
        if (vertex->uv[1] == 0) {
            previous[side] = -1;
        }
        test_assert(vertex->uv[1] > previous[side]);
        previous[side] = vertex->uv[1];
    }
    /* 25 rows, so one row of each of the 64 edges, both sides, is on the corner */
    test_int(walls, 64 * 25 * 2);
    test_int(corner, 64 * 2);
    ecs_fini(world);
}

void ArcLength_smooth_rows(void) {
    ecs_world_t *world = arc_world();
    /* A Catmull-Rom path joins its segments smoothly, so its rings stay at even lengths */
    Path3D path = {0};
    float xs[] = {0.5f, 1, 4, 4.5f, 9};
    for (int i = 0; i < 5; i ++) {
        *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) =
            (PathSegment3D){.kind = PathCatmullRom, .end = {xs[i], 0, i % 2 ? 1.0f : 0.0f}};
    }
    ecs_entity_t route = ecs_new(world);
    ecs_set_ptr(world, route, Path3D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sweep, {.path = route, .profile = arc_rectangle(world),
        .scale = {0.1f, 0.1f}, .taper = 1, .cap_start = true, .cap_end = true, .rings = 24});
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(shape != NULL);
    float parameters[8], fractions[8];
    test_int(box_surface_sweep_corners(&shape->surface, parameters, fractions), 0);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    test_assert(mesh != NULL);
    int walls = 0;
    for (int i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *vertex = &mesh->vertices[i];
        if (vertex->position[2] >= 2) {
            continue;
        }
        int ring = (walls / 2) % 25;
        walls ++;
        test_flt(vertex->uv[1], (float)ring / 24);
        test_flt(vertex->position[1],
            box_surface_sweep_parameter(&shape->surface, (float)ring / 24));
    }
    test_int(walls, 4 * 25 * 2);
    ecs_fini(world);
}
