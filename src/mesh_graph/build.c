/**
 * @file mesh_graph/build.c
 * @brief Builds the plans of the mesh graphs that changed, evaluates them on the job system and
 * delivers their meshes on the main thread.
 */

#include "mesh_graph.h"
#include "../jobs/jobs.h"

void box_mesh_graph_status(
    ecs_world_t *world,
    const box_mg_record_t *r,
    MeshGraphState state,
    const char *error)
{
    ecs_set(world, r->entity, MeshGraphStatus, {
        .state = state,
        .revision = r->revision,
        .error = state == MeshGraphFailed ? ECS_CONST_CAST(char*, error) : NULL,
        .vertices = r->vertices,
        .triangles = r->triangles,
        .time = r->time
    });
}

box_mg_output_t* box_mesh_graph_output(
    box_mesh_graphs_t *g,
    ecs_entity_t node)
{
    box_mg_output_t *result = ecs_map_get_deref(&g->outputs, box_mg_output_t, node);
    if (!result) {
        result = ecs_map_insert_alloc_t(&g->outputs, box_mg_output_t, node);
        ecs_os_memset_t(result, 0, box_mg_output_t);
        result->node = node;
    }

    return result;
}

void box_mesh_graph_unreach(
    box_mesh_graphs_t *g,
    box_mg_record_t *r)
{
    const ecs_entity_t *reached = ecs_vec_first_t(&r->reached, ecs_entity_t);
    int32_t i, count = ecs_vec_count(&r->reached);
    for (i = 0; i < count; i ++) {
        ecs_map_val_t *users = ecs_map_get(&g->reached, reached[i]);
        if (users && !-- *users) {
            ecs_map_remove(&g->reached, reached[i]);
        }
    }

    ecs_vec_clear(&r->reached);
}

/** Count the entities a record reached, so their changes find it.
 *
 * @param g The state
 * @param r The record
 */
static void box_mesh_graph_reach(
    box_mesh_graphs_t *g,
    box_mg_record_t *r)
{
    const ecs_entity_t *reached = ecs_vec_first_t(&r->reached, ecs_entity_t);
    int32_t i, count = ecs_vec_count(&r->reached);
    for (i = 0; i < count; i ++) {
        ecs_map_ensure(&g->reached, reached[i])[0] ++;
    }
}

void box_mesh_graph_deliver(
    ecs_world_t *world,
    box_mesh_graphs_t *g,
    ecs_entity_t output,
    box_mg_value_t *value,
    uint64_t hash)
{
    box_mg_output_t *out = ecs_map_get_deref(&g->outputs, box_mg_output_t, output);
    ecs_vec_t shapes;
    int32_t i, count;
    if (!out) {
        goto done;
    }

    box_mg_value_keep(value);
    box_mg_value_release(out->value);
    out->value = value;
    out->hash = hash;
    box_mg_cache_trim(world, g);
    ecs_vec_init_t(NULL, &shapes, ecs_entity_t, 0);
    ecs_map_iter_t it = ecs_map_iter(&g->records);
    while (ecs_map_next(&it)) {
        box_mg_record_t *r = ecs_map_ptr(&it);
        if (r->output == output) {
            *ecs_vec_append_t(NULL, &shapes, ecs_entity_t) = r->entity;
        }
    }

    count = ecs_vec_count(&shapes);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&shapes, ecs_entity_t, i);
        if (ecs_is_alive(world, e) && ecs_owns(world, e, MeshGraph)) {
            ecs_modified(world, e, MeshGraph);
        }
    }

    ecs_vec_fini_t(NULL, &shapes, ecs_entity_t);
done:
    return;
}

/** Give a record the value of its output, as a newer mesh than the one it has.
 *
 * @param world The world or a stage
 * @param g The state
 * @param r The record
 * @param output The output node the value is of
 * @param node The output node of the plan, with its value
 * @param generation Generation of the request
 * @param time Seconds of the evaluation
 */
static void box_mesh_graph_accept(
    ecs_world_t *world,
    box_mesh_graphs_t *g,
    box_mg_record_t *r,
    ecs_entity_t output,
    const box_mg_node_t *node,
    int32_t generation,
    float time)
{
    box_mg_output_t *out = ecs_map_get_deref(&g->outputs, box_mg_output_t, output);
    bool changed = !out || out->hash != node->hash || !out->value;
    r->delivered = generation;
    if (changed) {
        box_mesh_graph_deliver(world, g, output, node->value, node->hash);
    }

    if (changed || !r->revision) {
        r->revision ++;
        r->time = time;
    }

    r->vertices = node->value->mesh.vertex_count;
    r->triangles = node->value->mesh.index_count / 3;
    if (r->output == output && generation == r->submitted) {
        box_mesh_graph_status(world, r, MeshGraphReady, NULL);
    }
}

/** Run the plan of a job on a worker.
 *
 * @param ctx The plan
 */
static void box_mesh_graph_work(
    void *ctx)
{
    box_mg_plan_t *plan = ctx;
    ecs_time_t start = {0};
    ecs_time_measure(&start);
    box_mg_plan_eval(plan);
    plan->time = ecs_time_measure(&start);
    ecs_os_mutex_lock(plan->sync->lock);
    plan->sync->working --;
    ecs_os_mutex_unlock(plan->sync->lock);
}

/** Cache the values of a plan and deliver the meshes of its targets on the main thread.
 *
 * @param world The world
 * @param ctx The plan
 */
static void box_mesh_graph_complete(
    ecs_world_t *world,
    void *ctx)
{
    box_mg_plan_t *plan = ctx;
    box_mesh_graphs_t *g = box_mesh_graphs_get(world);
    int32_t i, count = ecs_vec_count(&plan->targets);
    if (!g) {
        goto done;
    }

    box_mg_plan_cache(world, g, plan);
    for (i = 0; i < count; i ++) {
        const box_mg_target_t *t = ecs_vec_get_t(&plan->targets, box_mg_target_t, i);
        const box_mg_node_t *node = ecs_vec_get_t(&plan->nodes, box_mg_node_t, t->node);
        box_mg_record_t *r = ecs_map_get_deref(&g->records, box_mg_record_t, t->entity);
        if (!r || !ecs_is_alive(world, t->entity) || t->generation <= r->delivered) {
            continue;
        }

        if (node->value) {
            box_mesh_graph_accept(world, g, r, t->output, node, t->generation,
                (float)plan->time);
        } else if (t->generation == r->submitted) {
            ecs_err("%s", node->error);
            box_mesh_graph_status(world, r, MeshGraphFailed, node->error);
        }
    }

done:
    return;
}

/** Free the plan of a job.
 *
 * @param ctx The plan
 */
static void box_mesh_graph_free(
    void *ctx)
{
    box_mg_plan_fini(ctx);
    ecs_os_free(ctx);
}

/** Fail the build of a record.
 *
 * @param world The world or a stage
 * @param r The record
 * @param error Why it failed
 */
static void box_mesh_graph_fail(
    ecs_world_t *world,
    box_mg_record_t *r,
    const char *error)
{
    r->submitted ++;
    ecs_err("%s", error);
    box_mesh_graph_status(world, r, MeshGraphFailed, error);
}

/** Build the plan of a record into the shared plan of the frame.
 *
 * @param world The world or a stage
 * @param g The state
 * @param plan The plan of the frame
 * @param r The record
 */
static void box_mesh_graph_build_record(
    ecs_world_t *world,
    box_mesh_graphs_t *g,
    box_mg_plan_t *plan,
    box_mg_record_t *r)
{
    const MeshGraph *graph = ecs_get(world, r->entity, MeshGraph);
    char *name = ecs_get_path(world, r->entity);
    char *error = NULL;
    int32_t index = -1;
    bool wait = !r->waiting;
    r->dirty = false;
    r->waiting = false;
    r->held = false;
    box_mesh_graph_unreach(g, r);
    box_mg_walk_t rc = BOX_MG_WALK_ERROR;
    if (!graph->output) {
        error = flecs_asprintf("mesh graph '%s': no output node", name);
    } else {
        rc = box_mg_plan_walk(world, g, plan, name, graph->output, wait, &r->reached, &index,
            &error);
    }

    box_mesh_graph_reach(g, r);
    if (rc == BOX_MG_WALK_WAIT || rc == BOX_MG_WALK_HOLD) {
        r->waiting = rc == BOX_MG_WALK_WAIT;
        r->held = rc == BOX_MG_WALK_HOLD;
        box_mesh_graph_status(world, r, MeshGraphLoading, NULL);
        goto done;
    }

    const box_mg_node_t *node = index >= 0
        ? ecs_vec_get_t(&plan->nodes, box_mg_node_t, index)
        : NULL;
    if (rc == BOX_MG_WALK_OK && node->type != MeshValueMesh) {
        char *output = ecs_get_path(world, graph->output);
        error = flecs_asprintf("mesh graph '%s': output '%s' outputs %s, not a Mesh", name,
            output, box_mg_type_name(node->type));
        ecs_os_free(output);
        rc = BOX_MG_WALK_ERROR;
    }

    if (rc != BOX_MG_WALK_OK) {
        box_mesh_graph_fail(world, r, error);
        goto done;
    }

    box_mg_output_t *out = box_mesh_graph_output(g, graph->output);
    int32_t generation = ++ r->submitted;
    if ((out->value && out->hash == node->hash) || node->value) {
        box_mesh_graph_accept(world, g, r, graph->output, node, generation, 0.0f);
    } else {
        *ecs_vec_append_t(NULL, &plan->targets, box_mg_target_t) = (box_mg_target_t){
            .entity = r->entity, .output = graph->output, .node = index,
            .generation = generation
        };
        box_mg_plan_need(plan, index);
        box_mesh_graph_status(world, r, MeshGraphLoading, NULL);
    }

done:
    ecs_os_free(error);
    ecs_os_free(name);
}

/** Submit the plan of a frame as a job, or free it when no target needs an evaluation.
 *
 * @param world The world or a stage
 * @param g The state
 * @param plan The plan, the job takes it
 */
static void box_mesh_graph_submit(
    ecs_world_t *world,
    box_mesh_graphs_t *g,
    box_mg_plan_t *plan)
{
    if (!ecs_vec_count(&plan->targets)) {
        box_mesh_graph_free(plan);
        goto done;
    }

    plan->sync = g->sync;
    g->sync->refs ++;
    ecs_os_mutex_lock(g->sync->lock);
    g->sync->working ++;
    ecs_os_mutex_unlock(g->sync->lock);
    g->stats.jobs ++;
    if (box_job_submit(world, &(box_job_desc_t){
        .work = box_mesh_graph_work,
        .complete = box_mesh_graph_complete,
        .ctx_free = box_mesh_graph_free,
        .ctx = plan
    })) {
        ecs_os_mutex_lock(g->sync->lock);
        g->sync->working --;
        ecs_os_mutex_unlock(g->sync->lock);
        box_mesh_graph_free(plan);
    }

done:
    return;
}

void box_mesh_graphs_build(
    ecs_world_t *world,
    box_mesh_graphs_t *g)
{
    ecs_vec_t build;
    int32_t i, count;
    ecs_vec_init_t(NULL, &build, ecs_entity_t, 0);
    ecs_map_iter_t it = ecs_map_iter(&g->records);
    while (ecs_map_next(&it)) {
        box_mg_record_t *r = ecs_map_ptr(&it);
        if (r->dirty || r->waiting || r->held) {
            *ecs_vec_append_t(NULL, &build, ecs_entity_t) = r->entity;
        }
    }

    count = ecs_vec_count(&build);
    if (!count) {
        goto done;
    }

    box_mg_plan_t *plan = ecs_os_malloc_t(box_mg_plan_t);
    box_mg_plan_init(plan);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&build, ecs_entity_t, i);
        box_mg_record_t *r = ecs_map_get_deref(&g->records, box_mg_record_t, e);
        if (!r) {
            continue;
        }

        if (!ecs_is_alive(world, e) || !ecs_owns(world, e, MeshGraph)) {
            r->dirty = r->waiting = r->held = false;
            continue;
        }

        box_mesh_graph_build_record(world, g, plan, r);
    }

    box_mg_cache_trim(world, g);
    box_mesh_graph_submit(world, g, plan);
done:
    ecs_vec_fini_t(NULL, &build, ecs_entity_t);
}

box_mg_value_t* box_mesh_graph_evaluate(
    ecs_world_t *world,
    ecs_entity_t node,
    char **error)
{
    box_mesh_graphs_t *g = box_mesh_graphs_get(world);
    box_mg_value_t *result = NULL;
    char *problem = NULL;
    int32_t index = -1;
    box_mg_plan_t plan;
    if (!g) {
        problem = ecs_os_strdup("box.mesh is not imported");
        goto done;
    }

    char *name = ecs_is_alive(world, node) ? ecs_get_path(world, node) : ecs_os_strdup("0");
    box_mg_plan_init(&plan);
    box_mg_walk_t rc = box_mg_plan_walk(world, g, &plan, name, node, false, NULL, &index,
        &problem);
    if (rc == BOX_MG_WALK_HOLD) {
        problem = flecs_asprintf("mesh graph '%s': a source is loading", name);
    } else if (rc == BOX_MG_WALK_OK) {
        box_mg_plan_need(&plan, index);
        box_mg_plan_eval(&plan);
        box_mg_plan_cache(world, g, &plan);
        const box_mg_node_t *n = ecs_vec_get_t(&plan.nodes, box_mg_node_t, index);
        result = box_mg_value_keep(n->value);
        problem = result ? NULL : ecs_os_strdup(n->error);
    }

    box_mg_plan_fini(&plan);
    ecs_os_free(name);
done:
    if (error) {
        *error = problem;
    } else {
        ecs_os_free(problem);
    }

    return result;
}

void box_mesh_graph_stats(
    const ecs_world_t *world,
    box_mesh_graph_stats_t *stats)
{
    const box_mesh_graphs_t *g = box_mesh_graphs_get(world);
    ecs_os_memset_t(stats, 0, box_mesh_graph_stats_t);
    if (g) {
        *stats = g->stats;
        stats->bytes = g->cache_bytes;
    }
}
