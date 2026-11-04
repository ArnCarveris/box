/**
 * @file Polygon2D.c
 * @brief Tests for the resolution of Polygon2D: its bounds, edges, holes, curves, strokes and the
 * distance the shader and picking measure.
 */

#include <box.h>
#include <bake_test.h>
#include "shape2d.h"
#include "../../../geometry_assets.h"

static ecs_world_t *polygon_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    return world;
}

/** An L shaped concave outline: a 4 by 4 square without its top right 2 by 2 quarter */
static const vec2 polygon_l[6] = {{0, 0}, {4, 0}, {4, 2}, {2, 2}, {2, 4}, {0, 4}};

/** Sample the edges of the Polygon2D of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param edges Receives the edges
 * @return Whether the path is open
 */
static bool polygon_edges(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_vec_t *edges)
{
    bool open = false;
    test_bool(box_shape2d_polygon_edges(world, ecs_get(world, e, Polygon2D), edges, &open),
        true);

    return open;
}

/** The L resolves to a polygon of six edges with its bounds and the quad that holds them. */
void Polygon2D_concave_bounds(void)
{
    ecs_world_t *world = polygon_world();
    ecs_entity_t path = test_path2(world, polygon_l, 6, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {1, 1}});
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    const ShapeMesh *mesh = ecs_get(world, e, ShapeMesh);
    test_assert(shape != NULL);
    test_assert(mesh != NULL);
    test_int(shape->kind, BOX_SHAPE2D_POLYGON);
    test_int(shape->edges, 6);
    test_flt(shape->center[0], 2);
    test_flt(shape->center[1], 2);
    test_flt(shape->size[0], 4);
    test_flt(shape->size[1], 4);
    test_flt(shape->width, 0);
    test_assert(mesh->mesh != 0);
    test_flt(mesh->scale[0], 8);
    test_flt(mesh->scale[1], 8);
    ecs_fini(world);
}

/** The distance of the L is negative in its arms and positive in its notch, a unit from its inner
 * edges.
 */
void Polygon2D_concave_distance(void)
{
    ecs_world_t *world = polygon_world();
    ecs_entity_t path = test_path2(world, polygon_l, 6, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {1, 1}});
    ecs_vec_t edges = {0};
    test_bool(polygon_edges(world, e, &edges), false);
    test_int(ecs_vec_count(&edges), 6);
    const vec4 *v = ecs_vec_first(&edges);
    test_flt(box_shape2d_polygon_distance(v, 6, 0, 1, 1), -1);
    test_flt(box_shape2d_polygon_distance(v, 6, 0, 3, 1), -1);
    test_flt(box_shape2d_polygon_distance(v, 6, 0, 1, 3), -1);
    test_flt(box_shape2d_polygon_distance(v, 6, 0, 3, 3), 1);
    test_flt(box_shape2d_polygon_distance(v, 6, 0, 5, 1), 1);
    test_flt(box_shape2d_polygon_distance(v, 6, 0, -1, 2), 1);
    test_bool(box_shape2d_polygon_contains(world, e, 1, 3), true);
    test_bool(box_shape2d_polygon_contains(world, e, 3, 3), false);
    ecs_vec_fini_t(NULL, &edges, vec4);
    ecs_fini(world);
}

/** The hole of a Profile2D is outside, the middle of the hole a unit from its edges. */
void Polygon2D_hole_even_odd(void)
{
    ecs_world_t *world = polygon_world();
    vec2 points[8] = {{-3, -3}, {3, -3}, {3, 3}, {-3, 3}, {-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    int32_t hole = 4;
    ecs_entity_t profile = test_profile(world, points, 8, &hole, 1);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = profile, .scale = {1, 1}});
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    test_int(shape->edges, 8);
    test_flt(shape->center[0], 0);
    test_flt(shape->size[0], 6);
    test_bool(box_shape2d_polygon_contains(world, e, 0, 0), false);
    test_bool(box_shape2d_polygon_contains(world, e, 2, 0), true);
    test_bool(box_shape2d_polygon_contains(world, e, 0, -2), true);
    test_bool(box_shape2d_polygon_contains(world, e, 4, 0), false);
    ecs_vec_t edges = {0};
    polygon_edges(world, e, &edges);
    test_flt(box_shape2d_polygon_distance(ecs_vec_first(&edges), 8, 0, 0, 0), 1);
    ecs_vec_fini_t(NULL, &edges, vec4);
    ecs_fini(world);
}

/** A hole that winds the same way as the outline is still a hole, by the even-odd rule. */
void Polygon2D_hole_winding_ignored(void)
{
    ecs_world_t *world = polygon_world();
    vec2 points[8] = {{-3, -3}, {3, -3}, {3, 3}, {-3, 3}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
    int32_t hole = 4;
    ecs_entity_t profile = test_profile(world, points, 8, &hole, 1);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = profile, .scale = {1, 1}});
    test_bool(box_shape2d_polygon_contains(world, e, 0, 0), false);
    test_bool(box_shape2d_polygon_contains(world, e, 2, 2), true);
    ecs_fini(world);
}

/** A line and a cubic Bezier in 16 steps: 17 edges and the bounds of the curve. */
void Polygon2D_curved_samples(void)
{
    ecs_world_t *world = polygon_world();
    Path2D path = {.start = {-2, 0}, .closed = true};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment2D) =
        (PathSegment2D){.kind = PathLine, .end = {2, 0}};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment2D) =
        (PathSegment2D){.kind = PathCubicBezier, .control1 = {2, 2.6667f},
            .control2 = {-2, 2.6667f}, .end = {-2, 0}};
    ecs_entity_t asset = ecs_new(world);
    ecs_set_ptr(world, asset, Path2D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment2D);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = asset, .scale = {1, 1}});
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    test_int(shape->edges, 17);
    test_flt(shape->size[0], 4);
    test_assert(fabsf(shape->size[1] - 2) < 0.01f);
    test_assert(fabsf(shape->center[1] - 1) < 0.01f);
    test_bool(box_shape2d_polygon_contains(world, e, 0, 1.9f), true);
    test_bool(box_shape2d_polygon_contains(world, e, 1.9f, 1.9f), false);
    ecs_fini(world);
}

/** Three Catmull-Rom segments and the one closing the path back to its start: 64 edges. */
void Polygon2D_catmull_rom_closed(void)
{
    ecs_world_t *world = polygon_world();
    Path2D path = {.start = {2, 0}, .closed = true};
    vec2 ends[3] = {{0, 2}, {-2, 0}, {0, -2}};
    for (int i = 0; i < 3; i ++) {
        PathSegment2D *s = ecs_vec_append_t(NULL, &path.segments, PathSegment2D);
        *s = (PathSegment2D){.kind = PathCatmullRom};
        glm_vec2_copy(ends[i], s->end);
    }

    ecs_entity_t asset = ecs_new(world);
    ecs_set_ptr(world, asset, Path2D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment2D);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = asset, .scale = {1, 1}});
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    test_int(shape->edges, 64);
    test_bool(box_shape2d_polygon_contains(world, e, 0, 0), true);
    test_bool(box_shape2d_polygon_contains(world, e, 1.5f, 1.5f), false);
    ecs_fini(world);
}

/** The scale and offset map the points, a negative scale flipping the notch of the L. */
void Polygon2D_scale_offset(void)
{
    ecs_world_t *world = polygon_world();
    ecs_entity_t path = test_path2(world, polygon_l, 6, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {2, -1}, .offset = {1, 10}});
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    test_flt(shape->center[0], 5);
    test_flt(shape->center[1], 8);
    test_flt(shape->size[0], 8);
    test_flt(shape->size[1], 4);
    test_flt(ecs_get(world, e, ShapeMesh)->scale[0], 18);
    test_flt(ecs_get(world, e, ShapeMesh)->scale[1], 20);
    test_bool(box_shape2d_polygon_contains(world, e, 7, 7), false);
    test_bool(box_shape2d_polygon_contains(world, e, 7, 9), true);
    ecs_fini(world);
}

/** An open path is a stroke of its width, with round ends and joins, its bounds grown by half the
 * width.
 */
void Polygon2D_open_stroke(void)
{
    ecs_world_t *world = polygon_world();
    vec2 points[3] = {{0, 0}, {4, 0}, {4, 4}};
    ecs_entity_t path = test_path2(world, points, 3, false);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {1, 1}, .width = 2});
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    test_int(shape->edges, 2);
    test_flt(shape->width, 2);
    test_flt(shape->center[0], 2);
    test_flt(shape->center[1], 2);
    test_flt(shape->size[0], 6);
    test_flt(shape->size[1], 6);
    ecs_vec_t edges = {0};
    test_bool(polygon_edges(world, e, &edges), true);
    const vec4 *v = ecs_vec_first(&edges);
    test_flt(box_shape2d_polygon_distance(v, 2, 1, 2, 0), -1);
    test_flt(box_shape2d_polygon_distance(v, 2, 1, 2, 3), 1);
    test_flt(box_shape2d_polygon_distance(v, 2, 1, -2, 0), 1);
    test_assert(fabsf(box_shape2d_polygon_distance(v, 2, 1, 5, -1) - (sqrtf(2) - 1)) < 1e-5f);
    test_bool(box_shape2d_polygon_contains(world, e, 4.5f, 2), true);
    test_bool(box_shape2d_polygon_contains(world, e, 2, 2), false);
    ecs_vec_fini_t(NULL, &edges, vec4);
    ecs_fini(world);
}

/** A stroke without a width draws nothing. */
void Polygon2D_open_without_width_not_drawn(void)
{
    ecs_world_t *world = polygon_world();
    vec2 points[3] = {{0, 0}, {4, 0}, {4, 4}};
    ecs_entity_t path = test_path2(world, points, 3, false);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {1, 1}, .width = 0});
    test_int(ecs_get(world, e, Shape2D)->kind, BOX_SHAPE2D_POLYGON);
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, 0);
    ecs_fini(world);
}

/** Polygon2D constructs at a scale of 1 with a stroke one unit wide and no path. */
void Polygon2D_default_values(void)
{
    ecs_world_t *world = polygon_world();
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Polygon2D);
    const Polygon2D *p = ecs_get(world, e, Polygon2D);
    test_flt(p->scale[0], 1);
    test_flt(p->scale[1], 1);
    test_flt(p->offset[0], 0);
    test_flt(p->width, 1);
    test_uint(p->path, 0);
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, 0);
    ecs_fini(world);
}

/** A path without an area, a missing path, no path and a zero scale draw nothing. */
void Polygon2D_invalid_not_drawn(void)
{
    ecs_world_t *world = polygon_world();
    vec2 line[3] = {{0, 0}, {1, 0}, {2, 0}};
    ecs_entity_t flat = test_path2(world, line, 3, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = flat, .scale = {1, 1}});
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, 0);
    ecs_set(world, e, Polygon2D, {.path = ecs_new(world), .scale = {1, 1}});
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, 0);
    ecs_set(world, e, Polygon2D, {.path = 0, .scale = {1, 1}});
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, 0);
    ecs_entity_t path = test_path2(world, polygon_l, 6, true);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {1, 1}});
    test_assert(ecs_get(world, e, ShapeMesh)->mesh != 0);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {0, 1}});
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, 0);
    ecs_fini(world);
}

/** An outline of more than BOX_POLYGON2D_MAX_EDGES edges draws nothing, one of that many does. */
void Polygon2D_too_many_edges_not_drawn(void)
{
    ecs_world_t *world = polygon_world();
    int32_t count = BOX_POLYGON2D_MAX_EDGES + 1;
    vec2 *points = ecs_os_malloc_n(vec2, count);
    for (int32_t i = 0; i < count; i ++) {
        float t = (float)i * 2 * GLM_PIf / (float)count;
        points[i][0] = cosf(t);
        points[i][1] = sinf(t);
    }

    ecs_entity_t path = test_path2(world, points, count, true);
    ecs_entity_t e = ecs_new(world);
    ecs_log_set_level(-4);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {1, 1}});
    test_int(ecs_get(world, e, Shape2D)->edges, count);
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, 0);
    ecs_entity_t fits = test_path2(world, points, count - 1, true);
    ecs_set(world, e, Polygon2D, {.path = fits, .scale = {1, 1}});
    test_int(ecs_get(world, e, Shape2D)->edges, count - 1);
    test_assert(ecs_get(world, e, ShapeMesh)->mesh != 0);
    ecs_os_free(points);
    ecs_fini(world);
}

/** A Rect of the same entity draws instead of its Polygon2D, until it is removed. */
void Polygon2D_rect_wins(void)
{
    ecs_world_t *world = polygon_world();
    ecs_entity_t path = test_path2(world, polygon_l, 6, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {1, 1}});
    ecs_set(world, e, Rect, {10, 20});
    test_int(ecs_get(world, e, Shape2D)->kind, BOX_SHAPE2D_RECT);
    ecs_remove(world, e, Rect);
    test_int(ecs_get(world, e, Shape2D)->kind, BOX_SHAPE2D_POLYGON);
    test_flt(ecs_get(world, e, Shape2D)->size[0], 4);
    ecs_fini(world);
}

/** Removing the Polygon2D removes the resolved shape and its mesh. */
void Polygon2D_remove(void)
{
    ecs_world_t *world = polygon_world();
    ecs_entity_t path = test_path2(world, polygon_l, 6, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {1, 1}});
    test_assert(ecs_has(world, e, Shape2D));
    ecs_remove(world, e, Polygon2D);
    test_assert(!ecs_has(world, e, Shape2D));
    test_assert(!ecs_has(world, e, ShapeMesh));
    ecs_fini(world);
}

/** Border and Image resolve as for a Rect, but a polygon keeps its corners. */
void Polygon2D_border_and_image(void)
{
    ecs_world_t *world = polygon_world();
    ecs_entity_t path = test_path2(world, polygon_l, 6, true);
    ecs_entity_t texture = ecs_new(world);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {1, 1}});
    ecs_set(world, e, Border, {.width = 0.5f, .radius = 3, .color = {255, 0, 0, 255}});
    ecs_set(world, e, Image, {.texture = texture, .u0 = 0.25f, .u1 = 0.75f, .v1 = 1});
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    test_flt(shape->border, 0.5f);
    test_flt(shape->radius, 0);
    test_uint(shape->border_color.r, 255);
    test_uint(shape->texture, texture);
    test_flt(shape->uv[0], 0.5f);
    test_flt(shape->uv[2], 0.25f);
    test_bool(shape->sliced, false);
    ecs_fini(world);
}

/** A change of the Path2D resolves the polygon again. */
void Polygon2D_path_change_updates(void)
{
    ecs_world_t *world = polygon_world();
    ecs_entity_t path = test_path2(world, polygon_l, 6, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {1, 1}});
    test_flt(ecs_get(world, e, Shape2D)->size[0], 4);
    Path2D *value = ecs_get_mut(world, path, Path2D);
    ((PathSegment2D*)ecs_vec_first(&value->segments))[0].end[0] = 6;
    ((PathSegment2D*)ecs_vec_first(&value->segments))[1].end[0] = 6;
    ecs_modified(world, path, Path2D);
    test_flt(ecs_get(world, e, Shape2D)->size[0], 6);
    test_flt(ecs_get(world, e, ShapeMesh)->scale[0], 12);
    ecs_fini(world);
}

/** A change of a hole of the Profile2D resolves the polygon again. */
void Polygon2D_profile_hole_change_updates(void)
{
    ecs_world_t *world = polygon_world();
    vec2 points[8] = {{-3, -3}, {3, -3}, {3, 3}, {-3, 3}, {-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    int32_t hole = 4;
    ecs_entity_t profile = test_profile(world, points, 8, &hole, 1);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = profile, .scale = {1, 1}});
    test_bool(box_shape2d_polygon_contains(world, e, 0, 0), false);
    test_int(ecs_get(world, e, Shape2D)->edges, 8);
    ecs_entity_t hole_path = *(ecs_entity_t*)ecs_vec_first(
        &ecs_get(world, profile, Profile2D)->holes);
    vec2 bigger[5] = {{-2, -2}, {2, -2}, {2, 0}, {2, 2}, {-2, 2}};
    ecs_entity_t source = test_path2(world, bigger, 5, true);
    Path2D copy = *ecs_get(world, source, Path2D);
    ecs_set_ptr(world, hole_path, Path2D, &copy);
    test_int(ecs_get(world, e, Shape2D)->edges, 9);
    test_bool(box_shape2d_polygon_contains(world, e, 1.5f, 0), false);
    ecs_fini(world);
}

/** An instance of a prefab with a Polygon2D resolves its own shape. */
void Polygon2D_prefab_instance(void)
{
    ecs_world_t *world = polygon_world();
    ecs_entity_t path = test_path2(world, polygon_l, 6, true);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Polygon2D, {.path = path, .scale = {1, 1}});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_int(ecs_get(world, instance, Shape2D)->kind, BOX_SHAPE2D_POLYGON);
    test_flt(ecs_get(world, instance, Shape2D)->size[0], 4);
    test_assert(ecs_get(world, instance, ShapeMesh)->mesh != 0);
    test_bool(box_shape2d_polygon_contains(world, instance, 3, 3), false);
    test_bool(box_shape2d_polygon_contains(world, instance, 1, 1), true);
    ecs_fini(world);
}

/** The documented plaza: the same outline drives a Polygon2D and, through a Profile2D, a
 * ProfileExtrusion.
 */
void Polygon2D_script(void)
{
    ecs_world_t *world = polygon_world();
    const char *code =
        "using box.geometry\n"
        "plaza_outline {\n"
        "  Path2D: {start: {-6, -4}, closed: true, segments: [\n"
        "    {kind: Line, end: {6, -4}},\n"
        "    {kind: CatmullRom, end: {7, 3}},\n"
        "    {kind: CatmullRom, end: {0, 6}},\n"
        "    {kind: Line, end: {-6, 3}}\n"
        "  ]}\n"
        "}\n"
        "plaza_profile {\n"
        "  Profile2D: {outer: plaza_outline}\n"
        "}\n"
        "map_plaza {\n"
        "  Polygon2D: {path: plaza_outline, scale: {4, -4}}\n"
        "  Border: {width: 1.5, color: {90, 80, 60, 255}}\n"
        "}\n"
        "plaza_block {\n"
        "  ProfileExtrusion: {profile: plaza_profile, depth: 0.2, cap_start: true,\n"
        "    cap_end: true}\n"
        "}\n";
    test_assert(ecs_script_run(world, "polygon", code, NULL) == 0);
    ecs_entity_t e = ecs_lookup(world, "map_plaza");
    ecs_entity_t block = ecs_lookup(world, "plaza_block");
    test_assert(e != 0);
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    test_int(shape->kind, BOX_SHAPE2D_POLYGON);
    test_int(shape->edges, 1 + 16 + 16 + 1 + 1);
    test_assert(ecs_get(world, e, ShapeMesh)->mesh != 0);
    test_flt(shape->border, 1.5f);
    test_assert(shape->size[0] >= 52);
    test_flt(shape->center[1], -4);
    test_assert(ecs_get(world, block, ShapeMesh)->mesh != 0);
    test_bool(box_shape2d_polygon_contains(world, e, 0, 0), true);
    test_bool(box_shape2d_polygon_contains(world, e, 0, 20), false);
    ecs_fini(world);
}

/** A Path3D is seen from above, its x and z, and a change of it resolves the polygon again. */
void Polygon2D_path3_from_above(void)
{
    ecs_world_t *world = polygon_world();
    ecs_entity_t road = test_route(world, (vec3){0, 5, 0}, (vec3){0, 5, 10},
        (vec3){10, 5, 20}, (vec3){20, 5, 20});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Polygon2D, {.path = road, .scale = {0.5f, -0.5f}, .width = 2});
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    test_int(shape->edges, 16);
    test_flt(shape->width, 2);
    test_flt(shape->center[0], 5);
    test_flt(shape->center[1], -5);
    test_flt(shape->size[0], 12);
    test_flt(shape->size[1], 12);
    test_bool(box_shape2d_polygon_contains(world, e, 0, -0.5f), true);
    test_bool(box_shape2d_polygon_contains(world, e, 10, -10.5f), true);
    test_bool(box_shape2d_polygon_contains(world, e, 10, 0), false);

    Path3D *value = ecs_get_mut(world, road, Path3D);
    ((PathSegment3D*)ecs_vec_first(&value->segments))[0].end[0] = 40;
    ecs_modified(world, road, Path3D);
    test_flt(ecs_get(world, e, Shape2D)->size[0], 22);
    ecs_fini(world);
}
