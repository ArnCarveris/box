#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "paths.h"
#include "mesh_cache.h"
#include "../../../geometry_assets.h"

static ecs_world_t *catmull_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    return world;
}

static void catmull_segment2(Path2D *path, PathSegmentKind kind, float x, float y)
{
    *ecs_vec_append_t(NULL, &path->segments, PathSegment2D) =
        (PathSegment2D){.kind = kind, .end = {x, y}};
}

static void catmull_segment3(Path3D *path, PathSegmentKind kind, float x, float y, float z)
{
    *ecs_vec_append_t(NULL, &path->segments, PathSegment3D) =
        (PathSegment3D){.kind = kind, .end = {x, y, z}};
}

static void catmull_near(const float *a, const float *b, int dimensions, float tolerance)
{
    for (int i = 0; i < dimensions; i ++) {
        test_assert(fabsf(a[i] - b[i]) <= tolerance);
    }
}

/* Unit direction of the derivative of a cubic at its start or end. */
static void catmull_slope(vec3 cubic[4], bool end, vec3 out)
{
    if (end) {
        glm_vec3_sub(cubic[3], cubic[2], out);
    } else {
        glm_vec3_sub(cubic[1], cubic[0], out);
    }
    glm_vec3_scale(out, 3, out);
}

/* Check that a shape built a valid mesh, through box_surface_sample and on the CPU. */
static void catmull_mesh(ecs_world_t *world, ecs_entity_t e)
{
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(shape != NULL);
    test_assert(shape->mesh != 0);
    test_assert(shape->surface.p[0][0] != 0);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, shape->mesh);
    test_assert(mesh != NULL);
    test_assert(mesh->vertex_count > 0);
    test_assert(mesh->index_count > 0);
    test_int(mesh->index_count % 3, 0);
    for (int32_t i = 0; i < mesh->index_count; i ++) {
        test_assert(mesh->indices[i] < (uint32_t)mesh->vertex_count);
    }
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *vertex = &mesh->vertices[i];
        box_surface_sample_t sample;
        box_surface_sample(&shape->surface, (int)vertex->position[2],
            vertex->position[0], vertex->position[1], &sample);
        for (int j = 0; j < 3; j ++) {
            test_assert(isfinite(sample.position[j]));
            test_assert(isfinite(sample.normal[j]));
        }
        test_assert(fabsf(glm_vec3_norm(sample.normal) - 1) < 0.001f);
        vec3 position;
        box_surface_vertex(shape, vertex, position);
        catmull_near(position, sample.position, 3, 0.0001f);
    }
    uint32_t resolved = box_mesh_cache_resolve(cache, shape);
    test_assert(resolved != 0);
    const box_mesh_t *cpu = box_mesh_cache_get(cache, resolved);
    test_assert(cpu != NULL);
    test_int(cpu->vertex_count, mesh->vertex_count);
    for (int32_t i = 0; i < cpu->vertex_count; i ++) {
        for (int j = 0; j < 3; j ++) {
            test_assert(isfinite(cpu->vertices[i].position[j]));
            test_assert(isfinite(cpu->vertices[i].normal[j]));
        }
    }
    box_mesh_cache_release(cache, resolved);
}

void CatmullRom_anchors_2d(void)
{
    ecs_world_t *world = catmull_world();
    vec2 anchors[] = {{0, 0}, {1, 2}, {3, 1}, {4, 3}, {6, 0}};
    Path2D path = {0};
    glm_vec2_copy(anchors[0], path.start);
    for (int i = 1; i < 5; i ++) {
        catmull_segment2(&path, PathCatmullRom, anchors[i][0], anchors[i][1]);
    }
    vec2 p;
    for (int i = 0; i < 5; i ++) {
        test_bool(box_path2_evaluate(&path, (float)i / 4, p), true);
        catmull_near(p, anchors[i], 2, 0.00001f);
    }
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Path2D, &path);
    ecs_vec_t samples = {0};
    test_bool(box_path2_sample(world, e, &samples), true);
    test_int(ecs_vec_count(&samples), 1 + 4 * 16);
    const vec2 *points = ecs_vec_first(&samples);
    for (int i = 0; i < 5; i ++) {
        catmull_near(points[i * 16], anchors[i], 2, 0.00001f);
    }
    ecs_vec_fini_t(NULL, &samples, vec2);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment2D);
    ecs_fini(world);
}

void CatmullRom_anchors_3d(void)
{
    ecs_world_t *world = catmull_world();
    vec3 anchors[] = {{0, 0, 0}, {1, 2, -1}, {3, 1, 2}, {4, 3, 0}};
    Path3D path = {0};
    glm_vec3_copy(anchors[0], path.start);
    for (int i = 1; i < 4; i ++) {
        catmull_segment3(&path, PathCatmullRom, anchors[i][0], anchors[i][1], anchors[i][2]);
    }
    vec3 p;
    for (int i = 0; i < 4; i ++) {
        test_bool(box_path3_evaluate(&path, (float)i / 3, p), true);
        catmull_near(p, anchors[i], 3, 0.00001f);
    }
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Path3D, &path);
    ecs_vec_t samples = {0};
    test_bool(box_path3_sample(world, e, &samples), true);
    test_int(ecs_vec_count(&samples), 1 + 3 * 16);
    const vec3 *points = ecs_vec_first(&samples);
    for (int i = 0; i < 4; i ++) {
        catmull_near(points[i * 16], anchors[i], 3, 0.00001f);
    }
    ecs_vec_fini_t(NULL, &samples, vec3);

    /* A closed path passes through every anchor and the start, and gets a Catmull-Rom closing
     * segment sampled in 16 steps. */
    path.closed = true;
    ecs_set_ptr(world, e, Path3D, &path);
    test_int(box_path3_count(&path), 4);
    for (int i = 0; i <= 4; i ++) {
        test_bool(box_path3_evaluate(&path, (float)i / 4, p), true);
        catmull_near(p, anchors[i % 4], 3, 0.00001f);
    }
    samples = (ecs_vec_t){0};
    test_bool(box_path3_sample(world, e, &samples), true);
    test_int(ecs_vec_count(&samples), 4 * 16);
    points = ecs_vec_first(&samples);
    for (int i = 0; i < 4; i ++) {
        catmull_near(points[i * 16], anchors[i], 3, 0.00001f);
    }
    ecs_vec_fini_t(NULL, &samples, vec3);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);
    ecs_fini(world);
}

void CatmullRom_slope_continuous(void)
{
    ecs_world_t *world = catmull_world();
    Path3D path = {0};
    catmull_segment3(&path, PathCatmullRom, 1, 2, 0);
    catmull_segment3(&path, PathCatmullRom, 3, 1, 1);
    catmull_segment3(&path, PathCatmullRom, 4, 4, -1);
    test_int(box_path3_count(&path), 3);
    for (int i = 0; i + 1 < 3; i ++) {
        vec3 a[4], b[4], out, in;
        test_int(box_path3_cubic(&path, i, a), PathCatmullRom);
        test_int(box_path3_cubic(&path, i + 1, b), PathCatmullRom);
        catmull_near(a[3], b[0], 3, 0);
        catmull_slope(a, true, out);
        catmull_slope(b, false, in);
        catmull_near(out, in, 3, 0.00001f);
    }

    /* The same through evaluation: the slope on both sides of the anchor at t = 1 / 3 agrees. */
    float h = 0.0005f, t = 1.0f / 3;
    vec3 before, at, after, left, right;
    test_bool(box_path3_evaluate(&path, t - h, before), true);
    test_bool(box_path3_evaluate(&path, t, at), true);
    test_bool(box_path3_evaluate(&path, t + h, after), true);
    glm_vec3_sub(at, before, left);
    glm_vec3_sub(after, at, right);
    glm_vec3_scale(left, 1 / h, left);
    glm_vec3_scale(right, 1 / h, right);
    catmull_near(left, right, 3, 0.05f);

    /* At the ends of an open path the neighbour is the endpoint itself. */
    vec3 first[4], last[4];
    box_path3_cubic(&path, 0, first);
    box_path3_cubic(&path, 2, last);
    catmull_near(first[1], (vec3){1.0f / 6, 2.0f / 6, 0}, 3, 0.00001f);
    catmull_near(last[2], (vec3){4 - 1.0f / 6, 4 - 3.0f / 6, -1 + 2.0f / 6}, 3, 0.00001f);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);
    ecs_fini(world);
}

void CatmullRom_slope_continuous_closed(void)
{
    ecs_world_t *world = catmull_world();
    /* Without a closing segment: the last segment ends at the start. */
    Path2D path = {.closed = true};
    catmull_segment2(&path, PathCatmullRom, 2, 0);
    catmull_segment2(&path, PathCatmullRom, 2, 2);
    catmull_segment2(&path, PathCatmullRom, 0, 2);
    catmull_segment2(&path, PathCatmullRom, 0, 0);
    test_int(box_path2_count(&path), 4);
    for (int i = 0; i < 4; i ++) {
        vec3 a[4], b[4], out, in;
        box_path2_cubic(&path, i, a);
        box_path2_cubic(&path, (i + 1) % 4, b);
        catmull_near(a[3], b[0], 3, 0);
        catmull_slope(a, true, out);
        catmull_slope(b, false, in);
        catmull_near(out, in, 3, 0.00001f);
        test_flt(a[1][2], 0);
    }

    /* With a closing segment, which is Catmull-Rom because the last segment is. */
    ecs_vec_remove_last(&path.segments);
    test_int(box_path2_count(&path), 4);
    vec3 closing[4], first[4], out, in;
    test_int(box_path2_cubic(&path, 3, closing), PathCatmullRom);
    box_path2_cubic(&path, 0, first);
    catmull_near(closing[3], first[0], 3, 0);
    catmull_slope(closing, true, out);
    catmull_slope(first, false, in);
    catmull_near(out, in, 3, 0.00001f);
    catmull_near(closing[1], (vec3){-2.0f / 6, 2 - 2.0f / 6, 0}, 3, 0.00001f);
    catmull_near(first[1], (vec3){(2 - 0) / 6.0f, (0 - 2) / 6.0f, 0}, 3, 0.00001f);

    /* The wrap through evaluation, either side of t = 0 and t = 1. */
    float h = 0.0005f;
    vec2 start, after, before, end;
    test_bool(box_path2_evaluate(&path, 0, start), true);
    test_bool(box_path2_evaluate(&path, h, after), true);
    test_bool(box_path2_evaluate(&path, 1 - h, before), true);
    test_bool(box_path2_evaluate(&path, 1, end), true);
    catmull_near(start, end, 2, 0.00001f);
    vec2 left, right;
    glm_vec2_sub(end, before, left);
    glm_vec2_sub(after, start, right);
    glm_vec2_scale(left, 1 / h, left);
    glm_vec2_scale(right, 1 / h, right);
    catmull_near(left, right, 2, 0.05f);

    /* A closed path whose last segment is a line still closes with a line. */
    PathSegment2D *last = ecs_vec_last_t(&path.segments, PathSegment2D);
    last->kind = PathLine;
    test_int(box_path2_cubic(&path, 3, closing), PathLine);
    catmull_near(closing[1], (vec3){0, 2 - 2.0f / 3, 0}, 3, 0.00001f);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment2D);
    ecs_fini(world);
}

void CatmullRom_next_to_line(void)
{
    ecs_world_t *world = catmull_world();
    Path2D path = {0};
    catmull_segment2(&path, PathLine, 1, 0);
    catmull_segment2(&path, PathCatmullRom, 2, 1);
    catmull_segment2(&path, PathLine, 3, 1);
    vec3 line[4], curve[4], after[4];
    test_int(box_path2_cubic(&path, 0, line), PathLine);
    test_int(box_path2_cubic(&path, 1, curve), PathCatmullRom);
    test_int(box_path2_cubic(&path, 2, after), PathLine);
    catmull_near(line[1], (vec3){1.0f / 3, 0, 0}, 3, 0.00001f);
    catmull_near(line[2], (vec3){2.0f / 3, 0, 0}, 3, 0.00001f);
    /* The neighbours are the anchors of the lines: the start and the end of the path. */
    catmull_near(curve[0], (vec3){1, 0, 0}, 3, 0);
    catmull_near(curve[1], (vec3){1 + 2.0f / 6, 1.0f / 6, 0}, 3, 0.00001f);
    catmull_near(curve[2], (vec3){2 - 2.0f / 6, 1 - 1.0f / 6, 0}, 3, 0.00001f);
    catmull_near(curve[3], (vec3){2, 1, 0}, 3, 0);
    catmull_near(after[1], (vec3){2 + 1.0f / 3, 1, 0}, 3, 0.00001f);

    vec2 p;
    test_bool(box_path2_evaluate(&path, 1.0f / 6, p), true);
    catmull_near(p, (vec2){0.5f, 0}, 2, 0.00001f);
    test_bool(box_path2_evaluate(&path, 0.5f, p), true);
    catmull_near(p, (vec2){1.5f, 0.5f}, 2, 0.00001f);

    /* Lines sample in one step, the Catmull-Rom in 16. */
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Path2D, &path);
    ecs_vec_t samples = {0};
    test_bool(box_path2_sample(world, e, &samples), true);
    test_int(ecs_vec_count(&samples), 1 + 1 + 16 + 1);
    const vec2 *points = ecs_vec_first(&samples);
    catmull_near(points[1], (vec2){1, 0}, 2, 0);
    catmull_near(points[9], (vec2){1.5f, 0.5f}, 2, 0.00001f);
    catmull_near(points[17], (vec2){2, 1}, 2, 0);
    catmull_near(points[18], (vec2){3, 1}, 2, 0);
    ecs_vec_fini_t(NULL, &samples, vec2);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment2D);
    ecs_fini(world);
}

void CatmullRom_next_to_bezier(void)
{
    ecs_world_t *world = catmull_world();
    Path3D path = {0};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) = (PathSegment3D){
        .kind = PathCubicBezier, .control1 = {0, 5, 0}, .control2 = {1, 5, 0}, .end = {1, 1, 0}};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) = (PathSegment3D){
        .kind = PathCatmullRom, .control1 = {9, 9, 9}, .control2 = {9, 9, 9}, .end = {2, 0, 1}};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) = (PathSegment3D){
        .kind = PathCubicBezier, .control1 = {3, -4, 1}, .control2 = {4, -4, 1}, .end = {4, 2, 2}};
    vec3 before[4], curve[4], after[4];
    test_int(box_path3_cubic(&path, 0, before), PathCubicBezier);
    test_int(box_path3_cubic(&path, 1, curve), PathCatmullRom);
    test_int(box_path3_cubic(&path, 2, after), PathCubicBezier);
    /* The Bezier keeps its control points. */
    catmull_near(before[1], (vec3){0, 5, 0}, 3, 0);
    catmull_near(before[2], (vec3){1, 5, 0}, 3, 0);
    catmull_near(after[1], (vec3){3, -4, 1}, 3, 0);
    /* The Catmull-Rom ignores its own control points and those of its neighbours, and takes
     * their anchors. */
    catmull_near(curve[0], (vec3){1, 1, 0}, 3, 0);
    catmull_near(curve[1], (vec3){1 + 2.0f / 6, 1 + 0.0f / 6, 1.0f / 6}, 3, 0.00001f);
    catmull_near(curve[2], (vec3){2 - 3.0f / 6, 0 - 1.0f / 6, 1 - 2.0f / 6}, 3, 0.00001f);
    catmull_near(curve[3], (vec3){2, 0, 1}, 3, 0);
    vec3 p;
    test_bool(box_path3_evaluate(&path, 1.0f / 3, p), true);
    catmull_near(p, (vec3){1, 1, 0}, 3, 0.00001f);
    test_bool(box_path3_evaluate(&path, 2.0f / 3, p), true);
    catmull_near(p, (vec3){2, 0, 1}, 3, 0.00001f);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);
    ecs_fini(world);
}

void CatmullRom_evaluate_sample_pack(void)
{
    ecs_world_t *world = catmull_world();
    Path3D path = {.start = {0, 0, 0}};
    catmull_segment3(&path, PathLine, 0, 1, 0);
    catmull_segment3(&path, PathCatmullRom, 1, 2, 0.5f);
    catmull_segment3(&path, PathCatmullRom, 0.5f, 3.5f, 1);
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) = (PathSegment3D){
        .kind = PathCubicBezier, .control1 = {0, 4, 1}, .control2 = {0, 4.5f, 1},
        .end = {0.5f, 5, 1.5f}};
    catmull_segment3(&path, PathCatmullRom, 1.5f, 5.5f, 1);
    ecs_entity_t route = ecs_new(world), e = ecs_new(world);
    ecs_set_ptr(world, route, Path3D, &path);
    int total = box_path3_count(&path);
    test_int(total, 5);

    /* Sampling agrees with evaluation at the parameters of the samples. */
    ecs_vec_t samples = {0};
    test_bool(box_path3_sample(world, route, &samples), true);
    test_int(ecs_vec_count(&samples), 1 + 1 + 16 * 4);
    const vec3 *points = ecs_vec_first(&samples);
    int index = 1;
    for (int i = 0; i < total; i ++) {
        int steps = i == 0 ? 1 : 16;
        for (int j = 1; j <= steps; j ++) {
            vec3 p;
            test_bool(box_path3_evaluate(&path, (i + (float)j / steps) / total, p), true);
            catmull_near(points[index ++], p, 3, 0.0001f);
        }
    }
    ecs_vec_fini_t(NULL, &samples, vec3);

    /* The packed sweep data agrees with evaluation: a thin profile stays at its radius. */
    Sweep sweep = {.path = route, .profile = test_circle(world, 0.5f),
        .scale = {0.1f, 0.1f}, .taper = 1, .cap_start = true, .cap_end = true};
    ecs_set_ptr(world, e, Sweep, &sweep);
    for (int i = 0; i <= 40; i ++) {
        vec3 center;
        box_surface_sample_t sample;
        float t = (float)i / 40;
        test_bool(box_path3_evaluate(&path, t, center), true);
        test_bool(box_surface_evaluate(world, e, SurfaceOuter, 0, t, &sample), true);
        test_assert(fabsf(glm_vec3_distance(center, sample.position) - 0.1f) < 0.0001f);
    }
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);

    /* The packed lathe data agrees with evaluation of the profile. */
    Path2D profile = {.start = {0.2f, 0}};
    catmull_segment2(&profile, PathLine, 0.5f, 0.2f);
    catmull_segment2(&profile, PathCatmullRom, 0.8f, 0.9f);
    catmull_segment2(&profile, PathCatmullRom, 0.4f, 1.5f);
    catmull_segment2(&profile, PathCatmullRom, 0.6f, 2);
    ecs_entity_t asset = ecs_new(world), lathe = ecs_new(world);
    ecs_set_ptr(world, asset, Path2D, &profile);
    ecs_set(world, lathe, Lathe, {.profile = asset});
    for (int i = 0; i <= 40; i ++) {
        vec2 p;
        box_surface_sample_t sample;
        float t = (float)i / 40;
        test_bool(box_path2_evaluate(&profile, t, p), true);
        test_bool(box_surface_evaluate(world, lathe, SurfaceOuter, 0, t, &sample), true);
        test_assert(fabsf(sample.position[0] - p[0]) < 0.0001f);
        test_assert(fabsf(sample.position[1] - p[1]) < 0.0001f);
    }
    ecs_vec_fini_t(NULL, &profile.segments, PathSegment2D);
    ecs_fini(world);
}

void CatmullRom_sweep_mesh(void)
{
    ecs_world_t *world = catmull_world();
    Path3D route = {.start = {0, 0, 0}};
    catmull_segment3(&route, PathCatmullRom, 1, 2, 0);
    catmull_segment3(&route, PathCatmullRom, 2, 1, 1);
    catmull_segment3(&route, PathCatmullRom, 3, 3, 0);
    ecs_entity_t path = ecs_new(world);
    ecs_set_ptr(world, path, Path3D, &route);
    ecs_vec_fini_t(NULL, &route.segments, PathSegment3D);

    /* A closed Catmull-Rom profile of four anchors, closed by a Catmull-Rom segment. */
    Path2D outline = {.start = {0.25f, 0}, .closed = true};
    catmull_segment2(&outline, PathCatmullRom, 0, 0.25f);
    catmull_segment2(&outline, PathCatmullRom, -0.25f, 0);
    catmull_segment2(&outline, PathCatmullRom, 0, -0.25f);
    ecs_entity_t outer = ecs_new(world), profile = ecs_new(world), e = ecs_new(world);
    ecs_set_ptr(world, outer, Path2D, &outline);
    ecs_vec_fini_t(NULL, &outline.segments, PathSegment2D);
    ecs_set(world, profile, Profile2D, {.outer = outer});
    ecs_vec_t samples = {0};
    test_bool(box_path2_sample(world, outer, &samples), true);
    test_int(ecs_vec_count(&samples), 4 * 16);
    ecs_vec_fini_t(NULL, &samples, vec2);

    Sweep sweep = {.path = path, .profile = profile, .scale = {1, 1}, .taper = 1,
        .cap_start = true, .cap_end = true, .rings = 48};
    ecs_set_ptr(world, e, Sweep, &sweep);
    catmull_mesh(world, e);
    ecs_fini(world);
}

void CatmullRom_lathe_mesh(void)
{
    ecs_world_t *world = catmull_world();
    Path2D outline = {.start = {0, 0}};
    catmull_segment2(&outline, PathLine, 0.4f, 0);
    catmull_segment2(&outline, PathCatmullRom, 0.75f, 0.6f);
    catmull_segment2(&outline, PathCatmullRom, 0.35f, 1.4f);
    catmull_segment2(&outline, PathCatmullRom, 0.5f, 2);
    ecs_entity_t profile = ecs_new(world), e = ecs_new(world);
    ecs_set_ptr(world, profile, Path2D, &outline);
    ecs_vec_fini_t(NULL, &outline.segments, PathSegment2D);
    ecs_set(world, e, Lathe, {.profile = profile, .cap_start = true, .cap_end = true});
    catmull_mesh(world, e);
    ecs_fini(world);
}

void CatmullRom_script(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    const char *script =
        "using box.geometry\n"
        "\n"
        "route {\n"
        "    Path3D: {\n"
        "        start: {0, 0, 0},\n"
        "        segments: [\n"
        "            {kind: CatmullRom, end: {1, 2, 0}},\n"
        "            {kind: CatmullRom, end: {2, 1, 1}},\n"
        "            {kind: Line, end: {2, 3, 1}}\n"
        "        ]\n"
        "    }\n"
        "}\n"
        "\n"
        "ring {\n"
        "    Path2D: {\n"
        "        start: {0.2, 0},\n"
        "        segments: [\n"
        "            {kind: CatmullRom, end: {0, 0.2}},\n"
        "            {kind: CatmullRom, end: {-0.2, 0}},\n"
        "            {kind: CatmullRom, end: {0, -0.2}}\n"
        "        ],\n"
        "        closed: true\n"
        "    }\n"
        "}\n"
        "\n"
        "section { Profile2D: {outer: ring} }\n"
        "\n"
        "tube {\n"
        "    Sweep: {path: route, profile: section, scale: {1, 1}, taper: 1,\n"
        "        cap_start: true, cap_end: true}\n"
        "}\n";
    test_int(ecs_script_run(world, "catmull_rom", script, NULL), 0);
    const Path3D *route = ecs_get(world, ecs_lookup(world, "route"), Path3D);
    test_assert(route != NULL);
    test_int(ecs_vec_get_t(&route->segments, PathSegment3D, 0)->kind, PathCatmullRom);
    ecs_entity_t tube = ecs_lookup(world, "tube");
    catmull_mesh(world, tube);
    ecs_entity_t session = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, session, "logs error 20", NULL);
    test_str(out, "");
    ecs_os_free(out);
    ecs_fini(world);
}

void CatmullRom_invalid_kind(void)
{
    ecs_world_t *world = catmull_world();
    Path2D path = {0};
    catmull_segment2(&path, PathCatmullRom, 1, 1);
    vec2 p;
    test_bool(box_path2_evaluate(&path, 0.5f, p), true);
    ecs_vec_first_t(&path.segments, PathSegment2D)->kind = (PathSegmentKind)3;
    test_bool(box_path2_evaluate(&path, 0.5f, p), false);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment2D);
    ecs_fini(world);
}
