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
    MeshGraphStatus status = {
        .state = state,
        .revision = r->revision,
        .error = state == MeshGraphFailed ? ECS_CONST_CAST(char*, error) : NULL,
        .vertices = r->vertices,
        .triangles = r->triangles,
        .time = r->time,
        .levels = r->delivered_levels > 0 ? r->delivered_levels : 1
    };
    int32_t l;
    for (l = 0; l < BOX_LOD_LEVELS_MAX && l < status.levels; l ++) {
        status.level_vertices[l] = r->level_vertices[l];
        status.level_triangles[l] = r->level_triangles[l];
    }

    ecs_set_ptr(world, r->entity, MeshGraphStatus, &status);
}

uint64_t box_mesh_graph_levels_hash(
    const uint64_t *hashes,
    int32_t levels)
{
    uint64_t result = hashes[0];
    if (levels > 1) {
        result = box_mg_hash(result, &levels, ECS_SIZEOF(int32_t));
        result = box_mg_hash(result, &hashes[1], ECS_SIZEOF(uint64_t) * (levels - 1));
        result = result ? result : 1;
    }

    return result;
}

/** Check whether an output has the values of levels, as the first levels of its own: graphs
 * that name one output with fewer levels than another draw the levels of the other.
 *
 * @param out The output
 * @param hashes The hash of each level
 * @param levels Number of levels
 * @return Whether it has them
 */
static bool box_mesh_graph_output_has(
    const box_mg_output_t *out,
    const uint64_t *hashes,
    int32_t levels)
{
    bool result = out && out->value && out->levels >= levels;
    int32_t l;
    if (result && out->levels == levels) {
        result = out->hash == box_mesh_graph_levels_hash(hashes, levels);
    }

    for (l = 0; result && out->levels > levels && l < levels; l ++) {
        result = out->lod_hashes[l] == hashes[l];
    }

    return result;
}

void box_mesh_graph_output_clear(
    box_mg_output_t *out)
{
    int32_t l;
    box_mg_value_release(out->value);
    out->value = NULL;
    out->hash = 0;
    for (l = 0; l < BOX_LOD_LEVELS_MAX; l ++) {
        box_mg_value_release(out->lods[l]);
        out->lods[l] = NULL;
        out->lod_hashes[l] = 0;
    }

    out->levels = 0;
}

void box_mesh_graph_output_copy(
    box_mg_output_t *dst,
    const box_mg_output_t *src)
{
    int32_t l;
    box_mesh_graph_output_clear(dst);
    dst->value = box_mg_value_keep(src->value);
    dst->hash = src->hash;
    dst->levels = src->levels;
    for (l = 0; l < BOX_LOD_LEVELS_MAX; l ++) {
        dst->lods[l] = box_mg_value_keep(src->lods[l]);
        dst->lod_hashes[l] = src->lod_hashes[l];
    }
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
    box_mg_value_t *const *values,
    const uint64_t *hashes,
    int32_t levels)
{
    box_mg_output_t *out = ecs_map_get_deref(&g->outputs, box_mg_output_t, output);
    ecs_vec_t shapes;
    int32_t i, count, l;
    if (!out) {
        goto done;
    }

    for (l = 0; l < levels; l ++) {
        box_mg_value_keep(values[l]);
    }

    box_mesh_graph_output_clear(out);
    out->value = values[0];
    out->hash = box_mesh_graph_levels_hash(hashes, levels);
    out->levels = levels;
    out->lod_hashes[0] = hashes[0];
    for (l = 1; l < levels; l ++) {
        out->lods[l] = values[l];
        out->lod_hashes[l] = hashes[l];
    }

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

/** Give a record the values of the levels of its output, as a newer mesh than the one it has.
 *
 * @param world The world or a stage
 * @param g The state
 * @param r The record
 * @param output The output node the values are of
 * @param plan The plan
 * @param nodes The output node of the plan of each level, with its value
 * @param levels Number of levels
 * @param generation Generation of the request
 * @param time Seconds of the evaluation
 */
static void box_mesh_graph_accept(
    ecs_world_t *world,
    box_mesh_graphs_t *g,
    box_mg_record_t *r,
    ecs_entity_t output,
    const box_mg_plan_t *plan,
    const int32_t *nodes,
    int32_t levels,
    int32_t generation,
    float time)
{
    box_mg_output_t *out = ecs_map_get_deref(&g->outputs, box_mg_output_t, output);
    box_mg_value_t *values[BOX_LOD_LEVELS_MAX];
    uint64_t hashes[BOX_LOD_LEVELS_MAX];
    int32_t l;
    for (l = 0; l < levels; l ++) {
        const box_mg_node_t *node = ecs_vec_get_t(&plan->nodes, box_mg_node_t, nodes[l]);
        values[l] = node->value;
        hashes[l] = node->hash;
    }

    bool changed = !box_mesh_graph_output_has(out, hashes, levels);
    r->delivered = generation;
    if (changed) {
        box_mesh_graph_deliver(world, g, output, values, hashes, levels);
    }

    if (changed || !r->revision) {
        r->revision ++;
        r->time = time;
    }

    r->vertices = values[0]->mesh.vertex_count;
    r->triangles = values[0]->mesh.index_count / 3;
    r->delivered_levels = levels;
    for (l = 0; l < BOX_LOD_LEVELS_MAX; l ++) {
        r->level_vertices[l] = l < levels ? values[l]->mesh.vertex_count : 0;
        r->level_triangles[l] = l < levels ? values[l]->mesh.index_count / 3 : 0;
    }

    if (r->output == output && generation == r->submitted) {
        box_mesh_graph_status(world, r, MeshGraphReady, NULL);
    }
}

/** Accept the levels an output has delivered already for a record whose graph did not change
 * them.
 *
 * @param world The world or a stage
 * @param r The record
 * @param out The output, with its values
 * @param levels Levels of detail of the record
 * @param generation Generation of the request
 */
static void box_mesh_graph_accept_delivered(
    ecs_world_t *world,
    box_mg_record_t *r,
    const box_mg_output_t *out,
    int32_t levels,
    int32_t generation)
{
    int32_t l;
    r->delivered = generation;
    r->revision = r->revision ? r->revision : 1;
    r->vertices = out->value->mesh.vertex_count;
    r->triangles = out->value->mesh.index_count / 3;
    r->delivered_levels = levels;
    for (l = 0; l < BOX_LOD_LEVELS_MAX; l ++) {
        const box_mg_value_t *v = l ? out->lods[l] : out->value;
        r->level_vertices[l] = l < levels && v ? v->mesh.vertex_count : 0;
        r->level_triangles[l] = l < levels && v ? v->mesh.index_count / 3 : 0;
    }

    if (generation == r->submitted) {
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
        const box_mg_node_t *failed = NULL;
        int32_t l;
        for (l = 0; l < t->levels && !failed; l ++) {
            const box_mg_node_t *node = ecs_vec_get_t(&plan->nodes, box_mg_node_t,
                t->nodes[l]);
            failed = node->value ? NULL : node;
        }

        box_mg_record_t *r = ecs_map_get_deref(&g->records, box_mg_record_t, t->entity);
        if (!r || !ecs_is_alive(world, t->entity) || t->generation <= r->delivered) {
            continue;
        }

        if (!failed) {
            box_mesh_graph_accept(world, g, r, t->output, plan, t->nodes, t->levels,
                t->generation, (float)plan->time);
        } else if (t->generation == r->submitted) {
            const char *error = failed->error ? failed->error : "an output has no value";
            ecs_err("%s", error);
            box_mesh_graph_status(world, r, MeshGraphFailed, error);
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
    int32_t nodes[BOX_LOD_LEVELS_MAX] = {-1, -1, -1, -1};
    int32_t levels = r->levels > 1 ? r->levels : 1, l;
    bool wait = !r->waiting;
    r->dirty = false;
    r->waiting = false;
    r->held = false;
    box_mesh_graph_unreach(g, r);
    box_mg_walk_t rc = BOX_MG_WALK_ERROR;
    if (!graph->output) {
        error = flecs_asprintf("mesh graph '%s': no output node", name);
    } else {
        rc = BOX_MG_WALK_OK;
        for (l = 0; l < levels && rc == BOX_MG_WALK_OK; l ++) {
            rc = box_mg_plan_walk(world, g, plan, name, graph->output, wait, l, &r->reached,
                &nodes[l], &error);
        }
    }

    box_mesh_graph_reach(g, r);
    if (rc == BOX_MG_WALK_WAIT || rc == BOX_MG_WALK_HOLD) {
        r->waiting = rc == BOX_MG_WALK_WAIT;
        r->held = rc == BOX_MG_WALK_HOLD;
        box_mesh_graph_status(world, r, MeshGraphLoading, NULL);
        goto done;
    }

    for (l = 0; l < levels && rc == BOX_MG_WALK_OK; l ++) {
        const box_mg_node_t *node = ecs_vec_get_t(&plan->nodes, box_mg_node_t, nodes[l]);
        if (node->type != MeshValueMesh) {
            char *output = ecs_get_path(world, graph->output);
            error = flecs_asprintf("mesh graph '%s': output '%s' outputs %s, not a Mesh", name,
                output, box_mg_type_name(node->type));
            ecs_os_free(output);
            rc = BOX_MG_WALK_ERROR;
        }
    }

    if (rc != BOX_MG_WALK_OK) {
        box_mesh_graph_fail(world, r, error);
        goto done;
    }

    box_mg_output_t *out = box_mesh_graph_output(g, graph->output);
    int32_t generation = ++ r->submitted;
    uint64_t hashes[BOX_LOD_LEVELS_MAX];
    bool ready = true;
    for (l = 0; l < levels; l ++) {
        const box_mg_node_t *node = ecs_vec_get_t(&plan->nodes, box_mg_node_t, nodes[l]);
        hashes[l] = node->hash;
        ready = ready && node->value;
    }

    bool same = box_mesh_graph_output_has(out, hashes, levels);
    if (same || ready) {
        if (same && !ready) {
            box_mesh_graph_accept_delivered(world, r, out, levels, generation);
        } else {
            box_mesh_graph_accept(world, g, r, graph->output, plan, nodes, levels, generation,
                0.0f);
        }
    } else {
        box_mg_target_t target = {
            .entity = r->entity, .output = graph->output, .levels = levels,
            .generation = generation
        };
        for (l = 0; l < levels; l ++) {
            target.nodes[l] = nodes[l];
            box_mg_plan_need(plan, nodes[l]);
        }

        *ecs_vec_append_t(NULL, &plan->targets, box_mg_target_t) = target;
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
    return box_mesh_graph_evaluate_level(world, node, 0, error);
}

box_mg_value_t* box_mesh_graph_evaluate_level(
    ecs_world_t *world,
    ecs_entity_t node,
    int32_t level,
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
    box_mg_walk_t rc = box_mg_plan_walk(world, g, &plan, name, node, false,
        level > 0 ? level : 0, NULL, &index, &problem);
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
