/**
 * @file MeshGraph.c
 * @brief Tests for the MeshGraph shape: evaluation on the job system, the mesh it keeps during
 * an edit, the value cache, graph errors and the node kind registry.
 */

#include "mesh_test.h"

/** The rail of the authoring example as a script */
#define MESH_RAIL "test/assets/mesh/rail.flecs"

/** Node of a kind registered by the tests, as an application registers its own */
typedef struct TestScale {
    ecs_entity_t mesh;  /**< Mesh input node */
    float factor;       /**< Scale of the positions */
} TestScale;

/** Scale the positions of a mesh, the eval of TestScale.
 *
 * @param node The TestScale
 * @param inputs The mesh
 * @param counts Values per port
 * @param out The mesh
 * @param error Unused
 * @return 0
 */
static int test_scale_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const TestScale *t = node;
    int32_t i;
    (void)counts;
    (void)error;
    box_mg_value_copy(out, inputs[0]);
    for (i = 0; i < out->mesh.vertex_count; i ++) {
        glm_vec3_scale(out->mesh.positions[i], t->factor, out->mesh.positions[i]);
    }

    return 0;
}

/** Create a MeshGraph shape.
 *
 * @param world The world
 * @param name The name
 * @param output The output node
 * @return The shape
 */
static ecs_entity_t make_shape(
    ecs_world_t *world,
    const char *name,
    ecs_entity_t output)
{
    ecs_entity_t e = test_named(world, name);
    ecs_set(world, e, MeshGraph, { .output = output });

    return e;
}

void MeshGraph_shape_after_job(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t shape = make_shape(world, "rail", r.all);
    test_assert(ecs_has(world, shape, ShapeMesh));
    test_assert(test_shape_mesh(world, shape) == NULL);

    ecs_progress(world, 0);
    test_int(ecs_get(world, shape, MeshGraphStatus)->state, MeshGraphLoading);
    test_assert(test_shape_mesh(world, shape) == NULL);

    const MeshGraphStatus *status = test_wait(world, shape);
    test_int(status->state, MeshGraphReady);
    test_int(status->revision, 1);
    const box_mesh_t *mesh = test_shape_mesh(world, shape);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, status->vertices);
    test_assert(mesh->min[0] >= -0.5f && mesh->min[0] < -0.35f);
    test_assert(mesh->max[0] <= 4.5f && mesh->max[0] > 4.35f);
    test_assert(mesh->max[1] <= 0.5f && mesh->max[1] > 0.35f);

    ecs_fini(world);
}

void MeshGraph_keeps_mesh_during_edit(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t shape = make_shape(world, "rail", r.tube);
    test_int(test_wait(world, shape)->state, MeshGraphReady);
    uint32_t before = ecs_get(world, shape, ShapeMesh)->mesh;
    test_assert(before != 0);
    test_int(ecs_get(world, shape, MeshGraphStatus)->vertices, 2 * 9);

    ecs_set(world, r.tube, MeshTube, { .curves = r.curve, .sides = 16 });
    ecs_progress(world, 0);
    test_int(ecs_get(world, shape, MeshGraphStatus)->state, MeshGraphLoading);
    test_int(ecs_get(world, shape, ShapeMesh)->mesh, before);

    const MeshGraphStatus *status = test_wait(world, shape);
    test_int(status->state, MeshGraphReady);
    test_int(status->revision, 2);
    test_int(status->vertices, 2 * 17);
    test_assert(ecs_get(world, shape, ShapeMesh)->mesh != before);
    test_int(test_shape_mesh(world, shape)->vertex_count, 2 * 17);

    ecs_fini(world);
}

void MeshGraph_status_counts(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t shape = make_shape(world, "rail", r.all);
    const MeshGraphStatus *status = test_wait(world, shape);
    const box_mesh_t *mesh = test_shape_mesh(world, shape);
    test_int(status->state, MeshGraphReady);
    test_assert(status->error == NULL);
    test_int(status->vertices, mesh->vertex_count);
    test_int(status->triangles, mesh->index_count / 3);
    test_assert(status->time >= 0.0f);

    ecs_fini(world);
}

void MeshGraph_prefab_instances_share(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t prefab = test_named(world, "rail_prefab");
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, MeshGraph, { .output = r.all });
    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, prefab);
    test_int(test_wait(world, prefab)->state, MeshGraphReady);
    ecs_progress(world, 0);

    uint32_t mesh = ecs_get(world, prefab, ShapeMesh)->mesh;
    test_assert(mesh != 0);
    test_int(ecs_get(world, a, ShapeMesh)->mesh, mesh);
    test_int(ecs_get(world, b, ShapeMesh)->mesh, mesh);
    test_assert(!ecs_owns(world, a, MeshGraphStatus));

    box_mesh_graph_stats_t stats;
    box_mesh_graph_stats(world, &stats);
    test_int(stats.jobs, 1);

    ecs_fini(world);
}

void MeshGraph_outputs_share_nodes(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t bark = make_shape(world, "bark", r.tube);
    ecs_entity_t leaves = make_shape(world, "leaves", r.balls);
    test_int(test_wait(world, bark)->state, MeshGraphReady);
    test_int(test_wait(world, leaves)->state, MeshGraphReady);

    box_mesh_graph_stats_t stats;
    box_mesh_graph_stats(world, &stats);
    test_int(stats.jobs, 1);
    test_int(stats.evaluated, 3);
    test_assert(test_shape_mesh(world, bark) != NULL);
    test_assert(test_shape_mesh(world, leaves) != NULL);

    ecs_fini(world);
}

void MeshGraph_cache_hit_on_undo(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    box_mesh_graph_stats_t stats;
    test_rail(world, 0, &r);
    ecs_entity_t shape = make_shape(world, "rail", r.tube);
    test_int(test_wait(world, shape)->state, MeshGraphReady);
    uint32_t first = ecs_get(world, shape, ShapeMesh)->mesh;
    box_mesh_graph_stats(world, &stats);
    test_int(stats.evaluated, 1);

    ecs_set(world, r.tube, MeshTube, { .curves = r.curve, .sides = 12 });
    test_int(test_wait(world, shape)->state, MeshGraphReady);
    box_mesh_graph_stats(world, &stats);
    test_int(stats.evaluated, 2);
    int32_t hits = stats.hits;

    ecs_set(world, r.tube, MeshTube, { .curves = r.curve, .sides = 8 });
    ecs_progress(world, 0);
    const MeshGraphStatus *status = ecs_get(world, shape, MeshGraphStatus);
    test_int(status->state, MeshGraphReady);
    test_int(status->revision, 3);
    test_int(status->vertices, 2 * 9);
    box_mesh_graph_stats(world, &stats);
    test_int(stats.evaluated, 2);
    test_assert(stats.hits > hits);
    test_int(stats.jobs, 2);
    test_int(test_shape_mesh(world, shape)->vertex_count, 2 * 9);
    test_assert(ecs_get(world, shape, ShapeMesh)->mesh != 0);
    (void)first;

    ecs_fini(world);
}

void MeshGraph_graphs_share_subgraph(void) {
    ecs_world_t *world = test_world();
    test_rail_t a, b;
    box_mesh_graph_stats_t stats;
    test_rail(world, test_named(world, "a"), &a);
    test_rail(world, test_named(world, "b"), &b);
    ecs_entity_t shape_a = make_shape(world, "shape_a", a.all);
    test_int(test_wait(world, shape_a)->state, MeshGraphReady);
    box_mesh_graph_stats(world, &stats);
    int32_t evaluated = stats.evaluated;
    test_assert(evaluated > 0);

    ecs_entity_t shape_b = make_shape(world, "shape_b", b.all);
    test_int(test_wait(world, shape_b)->state, MeshGraphReady);
    box_mesh_graph_stats(world, &stats);
    test_int(stats.evaluated, evaluated);
    test_int(stats.jobs, 1);
    test_int(ecs_get(world, shape_a, ShapeMesh)->mesh,
        ecs_get(world, shape_b, ShapeMesh)->mesh);
    test_int(ecs_get(world, shape_b, MeshGraphStatus)->revision, 1);

    ecs_fini(world);
}

void MeshGraph_cache_budget(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    box_mesh_graph_stats_t stats;
    ecs_singleton_set(world, MeshGraphCache, { .budget = 1 });
    test_rail(world, 0, &r);
    ecs_entity_t shape = make_shape(world, "rail", r.tube);
    test_int(test_wait(world, shape)->state, MeshGraphReady);
    box_mesh_graph_stats(world, &stats);
    test_int(stats.values, 1);
    test_int(stats.evicted, 0);

    ecs_set(world, r.tube, MeshTube, { .curves = r.curve, .sides = 12 });
    test_int(test_wait(world, shape)->state, MeshGraphReady);
    box_mesh_graph_stats(world, &stats);
    test_int(stats.values, 1);
    test_int(stats.evicted, 1);
    test_int(test_shape_mesh(world, shape)->vertex_count, 2 * 13);

    ecs_fini(world);
}

void MeshGraph_error_cycle(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t a = test_named(world, "a"), b = test_named(world, "b");
    ecs_set(world, a, MeshTransform, { .input = b });
    ecs_set(world, b, MeshTransform, { .input = a });
    ecs_entity_t shape = make_shape(world, "cycle", a);
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    test_failed(world, shape, "cycle");
    test_failed(world, shape, "'cycle'");
    test_assert(test_shape_mesh(world, shape) == NULL);

    ecs_fini(world);
}

void MeshGraph_error_wrong_type(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t bad = test_named(world, "bad");
    ecs_set(world, bad, MeshTube, { .curves = r.tube });
    ecs_entity_t shape = make_shape(world, "wrong", bad);
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    test_failed(world, shape, "input 'curves' of 'bad' takes Curves, 'rail_tube' outputs Mesh");

    ecs_fini(world);
}

void MeshGraph_error_missing_node(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t shape = make_shape(world, "rail", r.tube);
    test_int(test_wait(world, shape)->state, MeshGraphReady);

    ecs_log_set_level(-4);
    ecs_delete(world, r.curve);
    ecs_progress(world, 0);
    test_failed(world, shape, "input 'curves' of 'rail_tube' is a missing node");

    ecs_fini(world);
}

void MeshGraph_error_no_node_component(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t empty = test_named(world, "empty");
    ecs_entity_t t = test_named(world, "t");
    ecs_set(world, t, MeshTransform, { .input = empty });
    ecs_entity_t shape = make_shape(world, "waits", t);
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    test_int(ecs_get(world, shape, MeshGraphStatus)->state, MeshGraphLoading);
    ecs_progress(world, 0);
    test_failed(world, shape, "'empty' has no node component");

    ecs_fini(world);
}

void MeshGraph_error_output_not_mesh(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t shape = make_shape(world, "curves", r.curve);
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    test_failed(world, shape, "outputs Curves, not a Mesh");

    ecs_fini(world);
}

void MeshGraph_error_unconnected_input(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t tube = test_named(world, "tube");
    ecs_set(world, tube, MeshTube, { .sides = 8 });
    ecs_entity_t shape = make_shape(world, "unconnected", tube);
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    test_failed(world, shape, "input 'curves' of 'tube' has no node");

    ecs_fini(world);
}

void MeshGraph_failed_keeps_mesh(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t shape = make_shape(world, "rail", r.tube);
    test_int(test_wait(world, shape)->state, MeshGraphReady);
    uint32_t mesh = ecs_get(world, shape, ShapeMesh)->mesh;

    ecs_log_set_level(-4);
    ecs_set(world, r.tube, MeshTube, { .curves = r.balls });
    ecs_progress(world, 0);
    test_failed(world, shape, "takes Curves");
    test_int(ecs_get(world, shape, ShapeMesh)->mesh, mesh);
    test_int(ecs_get(world, shape, MeshGraphStatus)->revision, 1);

    ecs_log_set_level(-1);
    ecs_set(world, r.tube, MeshTube, { .curves = r.curve, .sides = 8 });
    const MeshGraphStatus *status = test_wait(world, shape);
    test_int(status->state, MeshGraphReady);
    test_assert(status->error == NULL);

    ecs_fini(world);
}

void MeshGraph_remove_graph(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t shape = make_shape(world, "rail", r.tube);
    test_int(test_wait(world, shape)->state, MeshGraphReady);

    ecs_remove(world, shape, MeshGraph);
    test_assert(!ecs_has(world, shape, ShapeMesh));
    test_assert(!ecs_has(world, shape, MeshGraphStatus));
    ecs_set(world, r.tube, MeshTube, { .curves = r.curve, .sides = 12 });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    box_mesh_graph_stats_t stats;
    box_mesh_graph_stats(world, &stats);
    test_int(stats.jobs, 1);

    ecs_fini(world);
}

void MeshGraph_output_changed_keeps_mesh(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t shape = make_shape(world, "rail", r.tube);
    test_int(test_wait(world, shape)->state, MeshGraphReady);
    uint32_t mesh = ecs_get(world, shape, ShapeMesh)->mesh;

    ecs_set(world, shape, MeshGraph, { .output = r.all });
    test_int(ecs_get(world, shape, ShapeMesh)->mesh, mesh);
    ecs_progress(world, 0);
    test_int(ecs_get(world, shape, MeshGraphStatus)->state, MeshGraphLoading);
    test_int(ecs_get(world, shape, ShapeMesh)->mesh, mesh);

    const MeshGraphStatus *status = test_wait(world, shape);
    test_int(status->state, MeshGraphReady);
    test_int(status->revision, 2);
    test_assert(ecs_get(world, shape, ShapeMesh)->mesh != mesh);

    ecs_fini(world);
}

void MeshGraph_script_graph(void) {
    ecs_world_t *world = test_world();
    char *path = box_app_locate(MESH_RAIL);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t rail = ecs_lookup(world, "rail");
    test_assert(rail != 0);
    const MeshGraphStatus *status = test_wait(world, rail);
    if (status->error) {
        printf("%s\n", status->error);
    }

    test_int(status->state, MeshGraphReady);
    const box_mesh_t *mesh = test_shape_mesh(world, rail);
    test_assert(mesh != NULL);
    test_assert(status->triangles > 5 * 20);
    test_assert(mesh->min[0] >= -0.2f && mesh->min[0] < -0.12f);
    test_assert(mesh->max[0] <= 4.2f && mesh->max[0] > 4.12f);

    ecs_fini(world);
}

void MeshGraph_node_kinds(void) {
    ecs_world_t *world = test_world();
    const MeshNodeKind *tube = ecs_get(world, ecs_id(MeshTube), MeshNodeKind);
    test_assert(tube != NULL);
    test_assert(tube->id > 0);
    test_int(tube->output, MeshValueMesh);
    test_int(tube->input_count, 2);
    test_str(tube->inputs[0].name, "curves");
    test_int(tube->inputs[0].type, MeshValueCurves);
    test_assert(!tube->inputs[0].optional);
    test_str(tube->inputs[1].name, "profile");
    test_assert(tube->inputs[1].optional);
    test_assert(!tube->source);
    test_assert(!tube->random);
    test_int(tube->param_count, 15);
    test_str(tube->params[0].name, "sides");

    const MeshNodeKind *merge = ecs_get(world, ecs_id(MeshMerge), MeshNodeKind);
    test_int(merge->output, MeshValueAny);
    test_assert(merge->inputs[0].list);
    test_assert(ecs_get(world, ecs_id(MeshShape), MeshNodeKind)->source);
    test_assert(ecs_get(world, ecs_id(MeshPointsOnMesh), MeshNodeKind)->random);
    test_assert(ecs_get(world, ecs_id(MeshDisplace), MeshNodeKind)->random);
    test_assert(ecs_get(world, ecs_id(MeshPointsOnCurves), MeshNodeKind)->random);
    test_int(ecs_get(world, ecs_id(MeshPath), MeshNodeKind)->output, MeshValueCurves);
    test_int(ecs_get(world, ecs_id(MeshPointsOnMesh), MeshNodeKind)->output, MeshValuePoints);

    test_assert(ecs_lookup(world, "box.mesh.Graph") == ecs_id(MeshGraph));
    test_assert(ecs_lookup(world, "box.mesh.nodes.Tube") == ecs_id(MeshTube));
    test_assert(ecs_lookup(world, "box.mesh.nodes.Mesh3") == ecs_id(MeshMesh3));
    test_assert(ecs_lookup(world, "box.mesh.nodes.Branches") == ecs_id(MeshBranches));
    test_assert(ecs_get(world, ecs_id(MeshBranches), MeshNodeKind)->random);
    test_int(ecs_get(world, ecs_id(MeshBranches), MeshNodeKind)->param_count, 26);
    test_int(ecs_get(world, ecs_id(MeshPrune), MeshNodeKind)->input_count, 2);

    ecs_fini(world);
}

void MeshGraph_custom_kind(void) {
    ecs_world_t *world = test_world();
    ECS_COMPONENT(world, TestScale);
    int32_t id = box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(TestScale),
        .output = MeshValueMesh,
        .inputs = {{ "mesh", offsetof(TestScale, mesh), MeshValueMesh }},
        .params = {{ "factor", offsetof(TestScale, factor), ECS_SIZEOF(float) }},
        .eval = test_scale_eval
    });
    test_assert(id > 0);
    test_int(ecs_get(world, ecs_id(TestScale), MeshNodeKind)->id, id);

    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t big = test_named(world, "big");
    ecs_set(world, big, TestScale, { .mesh = r.ball, .factor = 2 });
    ecs_entity_t shape = make_shape(world, "scaled", big);
    test_int(test_wait(world, shape)->state, MeshGraphReady);
    const box_mesh_t *mesh = test_shape_mesh(world, shape);
    test_assert(mesh->max[1] > 0.7f && mesh->max[1] <= 1.0f);

    ecs_log_set_level(-4);
    test_int(box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(TestScale),
        .output = MeshValueMesh,
        .eval = test_scale_eval,
        .flags = BOX_MESH_NODE_RANDOM
    }), 0);

    ecs_fini(world);
}

void MeshGraph_shape_of_graph(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t inner = make_shape(world, "inner", r.all);
    ecs_entity_t source = test_named(world, "source");
    ecs_set(world, source, MeshShape, { .entity = inner });
    ecs_entity_t lifted = test_named(world, "lifted");
    ecs_set(world, lifted, MeshTransform, { .input = source, .position = {0, 5, 0},
        .scale = {1, 1, 1} });
    ecs_entity_t outer = make_shape(world, "outer", lifted);

    ecs_progress(world, 0);
    test_int(ecs_get(world, outer, MeshGraphStatus)->state, MeshGraphLoading);
    const MeshGraphStatus *status = test_wait(world, outer);
    test_int(status->state, MeshGraphReady);
    test_int(status->vertices, ecs_get(world, inner, MeshGraphStatus)->vertices);
    const box_mesh_t *mesh = test_shape_mesh(world, outer);
    test_assert(mesh->min[1] > 4.4f && mesh->min[1] < 4.8f);

    ecs_fini(world);
}
