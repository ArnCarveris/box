/**
 * @file mesh_test.h
 * @brief Helpers of the tests of box.mesh: worlds, small graphs and the bounds of values.
 */

#ifndef MESH_TEST_H
#define MESH_TEST_H

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <math.h>
#include "mesh_graph.h"
#include "mesh_cache.h"

/** Nodes of a rail: a tube along a straight path with balls on five points */
typedef struct test_rail_t {
    ecs_entity_t path;       /**< Entity with the Path3D, from (0, 0, 0) to (4, 0, 0) */
    ecs_entity_t curve;      /**< MeshPath of radius 0.25 */
    ecs_entity_t tube;       /**< MeshTube of 8 sides */
    ecs_entity_t points;     /**< MeshPointsOnCurves of 5 points */
    ecs_entity_t ball_shape; /**< Prefab with an IcoSphere of radius 0.5 */
    ecs_entity_t ball;       /**< MeshShape of the ball */
    ecs_entity_t balls;      /**< MeshInstanceOnPoints of the balls on the points */
    ecs_entity_t all;        /**< MeshMerge of the tube and the balls */
} test_rail_t;

/** Create a headless world, which waits for its evaluations.
 *
 * @return The world
 */
static ecs_world_t* test_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    return world;
}

/** Create a named entity.
 *
 * @param world The world
 * @param name The name
 * @return The entity
 */
static ecs_entity_t test_named(
    ecs_world_t *world,
    const char *name)
{
    return ecs_entity(world, { .name = name });
}

/** Create a path entity with a straight Path3D.
 *
 * @param world The world
 * @param name The name
 * @param from Start point
 * @param to End point
 * @return The entity
 */
static ecs_entity_t test_line(
    ecs_world_t *world,
    const char *name,
    vec3 from,
    vec3 to)
{
    ecs_entity_t e = test_named(world, name);
    PathSegment3D segment = { .kind = PathLine };
    glm_vec3_copy(to, segment.end);
    Path3D path = { .segments = BOX_VEC_VIEW(PathSegment3D, segment) };
    glm_vec3_copy(from, path.start);
    ecs_set_ptr(world, e, Path3D, &path);

    return e;
}

/** Create the nodes of a rail.
 *
 * @param world The world
 * @param scope Parent of the nodes, 0 for the root
 * @param r Receives the nodes
 */
static void test_rail(
    ecs_world_t *world,
    ecs_entity_t scope,
    test_rail_t *r)
{
    ecs_entity_t old = ecs_set_scope(world, scope);
    r->path = test_line(world, "rail_path", (vec3){0, 0, 0}, (vec3){4, 0, 0});
    r->curve = test_named(world, "rail_curve");
    ecs_set(world, r->curve, MeshPath, { .path = r->path, .radius = 0.25f });
    r->tube = test_named(world, "rail_tube");
    ecs_set(world, r->tube, MeshTube, { .curves = r->curve, .sides = 8 });
    r->points = test_named(world, "rail_points");
    ecs_set(world, r->points, MeshPointsOnCurves, { .curves = r->curve, .count = 5 });
    r->ball_shape = ecs_entity(world, { .name = "rail_ball_shape" });
    ecs_add_id(world, r->ball_shape, EcsPrefab);
    ecs_set(world, r->ball_shape, IcoSphere, { .segments = 1, .smooth = true,
        .radius = 0.5f });
    r->ball = test_named(world, "rail_ball");
    ecs_set(world, r->ball, MeshShape, { .entity = r->ball_shape });
    r->balls = test_named(world, "rail_balls");
    ecs_set(world, r->balls, MeshInstanceOnPoints, { .points = r->points,
        .instance = r->ball });
    r->all = test_named(world, "rail_all");
    ecs_set(world, r->all, MeshMerge, {
        .inputs = BOX_VEC_VIEW(ecs_entity_t, r->tube, r->balls) });
    ecs_set_scope(world, old);
}

/** Run frames until a MeshGraph is no longer loading.
 *
 * @param world The world
 * @param e The shape entity
 * @return The status
 */
static const MeshGraphStatus* test_wait(
    ecs_world_t *world,
    ecs_entity_t e)
{
    int32_t frames = 0;
    ecs_progress(world, 0);
    while (ecs_get(world, e, MeshGraphStatus)->state == MeshGraphLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    return ecs_get(world, e, MeshGraphStatus);
}

/** Get the mesh a shape resolved to.
 *
 * @param world The world
 * @param e The shape entity
 * @return The mesh, NULL for none
 */
static const box_mesh_t* test_shape_mesh(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);

    return sm && sm->mesh ? box_mesh_cache_get(box_mesh_cache_of(world), sm->mesh) : NULL;
}

/** Evaluate a node, which must succeed.
 *
 * @param world The world
 * @param node The node
 * @return The value, released by the caller
 */
static box_mg_value_t* test_eval(
    ecs_world_t *world,
    ecs_entity_t node)
{
    char *error = NULL;
    box_mg_value_t *result = box_mesh_graph_evaluate(world, node, &error);
    if (error) {
        printf("%s\n", error);
    }

    test_assert(result != NULL);
    test_assert(error == NULL);

    return result;
}

/** Compute the bounds of positions.
 *
 * @param positions The positions
 * @param count Number of positions
 * @param min Receives the minimum
 * @param max Receives the maximum
 */
static void test_bounds(
    const vec3 *positions,
    int32_t count,
    vec3 min,
    vec3 max)
{
    int32_t i;
    glm_vec3_fill(min, 1e30f);
    glm_vec3_fill(max, -1e30f);
    for (i = 0; i < count; i ++) {
        glm_vec3_minv(min, (float*)positions[i], min);
        glm_vec3_maxv(max, (float*)positions[i], max);
    }
}

/** Check that a float is within a tolerance of an expected value.
 *
 * @param value The value
 * @param expect The expected value
 * @param eps The tolerance
 */
static void test_near(
    float value,
    float expect,
    float eps)
{
    if (fabsf(value - expect) > eps) {
        printf("expected %f, got %f\n", (double)expect, (double)value);
    }

    test_assert(fabsf(value - expect) <= eps);
}

/** Check that the last failure of a graph mentions a text.
 *
 * @param world The world
 * @param e The shape entity
 * @param text The text
 */
static void test_failed(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *text)
{
    const MeshGraphStatus *status = ecs_get(world, e, MeshGraphStatus);
    test_assert(status != NULL);
    test_int(status->state, MeshGraphFailed);
    test_assert(status->error != NULL);
    if (!strstr(status->error, text)) {
        printf("error '%s' does not mention '%s'\n", status->error, text);
    }

    test_assert(strstr(status->error, text) != NULL);
}

#endif
