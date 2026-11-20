/**
 * @file mesh_graph/plan.c
 * @brief Plans of mesh graphs: the walk of a graph into hashed node snapshots, their evaluation
 * and the value cache.
 */

#include "mesh_graph.h"
#include <stdarg.h>

/** Mark of a node whose walk did not complete: it waits or holds */
#define BOX_MG_INCOMPLETE (UINT64_MAX)

/** Seed of the content hashes of the nodes */
#define BOX_MG_HASH_SEED (14695981039346656037ull)

/** State of the walk of one graph */
typedef struct box_mg_walk_ctx_t {
    const ecs_world_t *world;    /**< The world */
    box_mesh_graphs_t *g;        /**< The state */
    box_mg_plan_t *plan;         /**< The plan the nodes go to */
    const char *graph;           /**< Name of the graph */
    ecs_map_t visit;             /**< Node entity to 0 while visited, index + 1 after */
    ecs_vec_t *reached;          /**< ecs_entity_t, the entities reached, NULL for none */
    bool wait;                   /**< Wait for a node without a node component */
    int32_t count;               /**< Nodes visited */
    box_mg_walk_t state;         /**< Outcome so far */
    char *error;                 /**< Why the walk failed, NULL while it did not */
} box_mg_walk_ctx_t;

void box_mg_plan_init(
    box_mg_plan_t *plan)
{
    ecs_os_memset_t(plan, 0, box_mg_plan_t);
    ecs_vec_init_t(NULL, &plan->nodes, box_mg_node_t, 0);
    ecs_vec_init_t(NULL, &plan->links, int32_t, 0);
    ecs_vec_init_t(NULL, &plan->targets, box_mg_target_t, 0);
    ecs_map_init(&plan->by_hash, NULL);
}

void box_mg_plan_fini(
    box_mg_plan_t *plan)
{
    int32_t i, count = ecs_vec_count(&plan->nodes);
    box_mg_node_t *nodes = ecs_vec_first_t(&plan->nodes, box_mg_node_t);
    for (i = 0; i < count; i ++) {
        ecs_os_free(nodes[i].params);
        ecs_os_free(nodes[i].name);
        ecs_os_free(nodes[i].error);
        box_mg_value_release(nodes[i].value);
    }

    ecs_vec_fini_t(NULL, &plan->nodes, box_mg_node_t);
    ecs_vec_fini_t(NULL, &plan->links, int32_t);
    ecs_vec_fini_t(NULL, &plan->targets, box_mg_target_t);
    ecs_map_fini(&plan->by_hash);
    if (plan->sync && !-- plan->sync->refs) {
        ecs_os_mutex_free(plan->sync->lock);
        ecs_os_free(plan->sync);
    }

    plan->sync = NULL;
}

/** Fail a walk with an error that names the graph; the first error is kept.
 *
 * @param w The walk
 * @param fmt Format of the problem
 * @param ... Arguments of the format
 */
static void box_mg_walk_error(
    box_mg_walk_ctx_t *w,
    const char *fmt,
    ...)
{
    va_list args;
    if (!w->error) {
        va_start(args, fmt);
        char *problem = flecs_vasprintf(fmt, args);
        va_end(args);
        w->error = flecs_asprintf("mesh graph '%s': %s", w->graph, problem);
        ecs_os_free(problem);
    }

    w->state = BOX_MG_WALK_ERROR;
}

/** Note an entity the walk reached, so its changes build the graph again.
 *
 * @param w The walk
 * @param e The entity
 */
static void box_mg_walk_reach(
    box_mg_walk_ctx_t *w,
    ecs_entity_t e)
{
    if (w->reached && e) {
        *ecs_vec_append_t(NULL, w->reached, ecs_entity_t) = e;
    }
}

/** Find the node kind of an entity.
 *
 * @param w The walk
 * @param e The node entity
 * @param name Name of the node in errors
 * @return The kind, NULL for none or more than one, which fails the walk
 */
static box_mg_kind_t* box_mg_walk_kind(
    box_mg_walk_ctx_t *w,
    ecs_entity_t e,
    const char *name)
{
    box_mg_kind_t *result = NULL;
    const ecs_type_t *type = ecs_get_type(w->world, e);
    int32_t i, found = 0;
    for (i = 0; type && i < type->count; i ++) {
        box_mg_kind_t *kind = box_mesh_kind_of(w->g, type->array[i]);
        if (kind) {
            result = result ? result : kind;
            found ++;
        }
    }

    if (found > 1) {
        box_mg_walk_error(w, "'%s' has more than one node component", name);
        result = NULL;
    }

    return result;
}

static int32_t box_mg_walk_visit(
    box_mg_walk_ctx_t *w,
    ecs_entity_t e,
    ecs_entity_t from,
    const char *port);

/** Visit the input nodes of a port of a node.
 *
 * @param w The walk
 * @param e The node entity
 * @param name Name of the node
 * @param ptr The node component
 * @param input The port
 * @param children Receives the indices of the input nodes, -1 for an incomplete one
 * @return The number of input nodes
 */
static int32_t box_mg_walk_port(
    box_mg_walk_ctx_t *w,
    ecs_entity_t e,
    const char *name,
    const void *ptr,
    const box_mesh_node_input_t *input,
    ecs_vec_t *children)
{
    const ecs_entity_t *entities = NULL;
    int32_t i, count = 0;
    if (input->list) {
        const ecs_vec_t *list = ECS_OFFSET(ptr, input->offset);
        entities = ecs_vec_first_t(list, ecs_entity_t);
        count = ecs_vec_count(list);
    } else {
        entities = ECS_OFFSET(ptr, input->offset);
        count = entities[0] ? 1 : 0;
    }

    if (!count && !input->optional) {
        box_mg_walk_error(w, "input '%s' of '%s' has no node", input->name, name);
    }

    for (i = 0; i < count && w->state != BOX_MG_WALK_ERROR; i ++) {
        *ecs_vec_append_t(NULL, children, int32_t) =
            box_mg_walk_visit(w, entities[i], e, input->name);
    }

    return count;
}

/** Check the types of the input nodes of a node and find the type of its value.
 *
 * @param w The walk
 * @param kind The kind
 * @param name Name of the node
 * @param counts Input nodes of each port
 * @param children Indices of the input nodes, all complete
 * @return The type of the value of the node
 */
static MeshValueType box_mg_walk_types(
    box_mg_walk_ctx_t *w,
    const box_mg_kind_t *kind,
    const char *name,
    const int32_t *counts,
    const int32_t *children)
{
    MeshValueType result = kind->output;
    bool any = false;
    int32_t p, i, link = 0;
    const box_mg_node_t *nodes = ecs_vec_first_t(&w->plan->nodes, box_mg_node_t);
    for (p = 0; p < kind->input_count; p ++) {
        MeshValueType want = kind->inputs[p].type;
        for (i = 0; i < counts[p]; i ++) {
            const box_mg_node_t *child = &nodes[children[link ++]];
            if (want != MeshValueAny && child->type != want) {
                box_mg_walk_error(w, "input '%s' of '%s' takes %s, '%s' outputs %s",
                    kind->inputs[p].name, name, box_mg_type_name(want), child->name,
                    box_mg_type_name(child->type));
            } else if (want == MeshValueAny && kind->output == MeshValueAny) {
                if (any && child->type != result) {
                    box_mg_walk_error(w, "inputs of '%s' output %s and %s", name,
                        box_mg_type_name(result), box_mg_type_name(child->type));
                }

                result = any ? result : child->type;
                any = true;
            }
        }
    }

    if (result == MeshValueAny) {
        box_mg_walk_error(w, "'%s' has no input to take its type from", name);
    }

    return result;
}

/** Snapshot the parameters of a node: a copy of its component with only the parameters.
 *
 * @param kind The kind
 * @param ptr The node component
 * @return The snapshot
 */
static void* box_mg_walk_params(
    const box_mg_kind_t *kind,
    const void *ptr)
{
    void *result = ecs_os_calloc(kind->size > 0 ? kind->size : 1);
    int32_t i;
    for (i = 0; i < kind->param_count; i ++) {
        const box_mesh_node_param_t *param = &kind->params[i];
        ecs_os_memcpy(ECS_OFFSET(result, param->offset), ECS_OFFSET(ptr, param->offset),
            param->size);
    }

    return result;
}

/** Read the value of a source node.
 *
 * @param w The walk
 * @param kind The kind
 * @param e The node entity
 * @param name Name of the node
 * @param ptr The node component
 * @param type The type of the value
 * @return The value, NULL when the walk waits, holds or failed
 */
static box_mg_value_t* box_mg_walk_capture(
    box_mg_walk_ctx_t *w,
    const box_mg_kind_t *kind,
    ecs_entity_t e,
    const char *name,
    const void *ptr,
    MeshValueType type)
{
    box_mg_value_t *result = box_mg_value_new(type);
    ecs_entity_t source = 0;
    char *error = NULL;
    int rc = kind->capture(w->world, e, ptr, result, &source, &error);
    box_mg_walk_reach(w, source);
    if (rc == BOX_MESH_CAPTURE_WAIT && w->wait) {
        w->state = w->state == BOX_MG_WALK_OK ? BOX_MG_WALK_WAIT : w->state;
    } else if (rc == BOX_MESH_CAPTURE_WAIT) {
        box_mg_walk_error(w, "'%s': %s", name, error ? error : "its source is not ready");
    } else if (rc == BOX_MESH_CAPTURE_HOLD) {
        w->state = w->state != BOX_MG_WALK_ERROR ? BOX_MG_WALK_HOLD : w->state;
    } else if (rc != BOX_MESH_CAPTURE_OK) {
        box_mg_walk_error(w, "'%s': %s", name, error ? error : "its source is invalid");
    }

    if (rc != BOX_MESH_CAPTURE_OK) {
        box_mg_value_release(result);
        result = NULL;
    }

    ecs_os_free(error);

    return result;
}

/** Add a node to the plan, or find the node of the plan with its hash, and look its value up
 * in the cache.
 *
 * @param w The walk
 * @param node The node, the plan takes what it owns
 * @param children Indices of its input nodes
 * @param link_count Number of input nodes
 * @return The index of the node in the plan
 */
static int32_t box_mg_walk_add(
    box_mg_walk_ctx_t *w,
    box_mg_node_t *node,
    const int32_t *children,
    int32_t link_count)
{
    box_mg_plan_t *plan = w->plan;
    ecs_map_val_t *found = ecs_map_get(&plan->by_hash, node->hash);
    int32_t result;
    if (found) {
        result = (int32_t)*found - 1;
        ecs_os_free(node->params);
        ecs_os_free(node->name);
        box_mg_value_release(node->value);
    } else {
        box_mg_entry_t *entry = ecs_map_get_deref(&w->g->cache, box_mg_entry_t, node->hash);
        if (entry && !node->value) {
            node->value = box_mg_value_keep(entry->value);
            entry->used = ++ w->g->clock;
            w->g->stats.hits ++;
        }

        node->link = ecs_vec_count(&plan->links);
        if (link_count) {
            ecs_os_memcpy_n(ecs_vec_grow_t(NULL, &plan->links, int32_t, link_count),
                children, int32_t, link_count);
        }

        result = ecs_vec_count(&plan->nodes);
        *ecs_vec_append_t(NULL, &plan->nodes, box_mg_node_t) = *node;
        ecs_map_insert(&plan->by_hash, node->hash, (ecs_map_val_t)(result + 1));
    }

    return result;
}

/** Visit a node entity with its kind: its inputs, its snapshot and its hash.
 *
 * @param w The walk
 * @param e The node entity
 * @param kind The kind
 * @param name Name of the node, the node takes it when it is added
 * @return The index of the node in the plan, -1 when it is incomplete or the walk failed
 */
static int32_t box_mg_walk_node(
    box_mg_walk_ctx_t *w,
    ecs_entity_t e,
    const box_mg_kind_t *kind,
    char *name)
{
    const void *ptr = ecs_get_id(w->world, e, kind->component);
    box_mg_node_t node = { .entity = e, .name = name, .kind = kind->id, .eval = kind->eval };
    ecs_vec_t children;
    int32_t p, i, result = -1;
    bool complete = true;
    ecs_vec_init_t(NULL, &children, int32_t, 0);
    for (p = 0; p < kind->input_count && w->state != BOX_MG_WALK_ERROR; p ++) {
        node.counts[p] = box_mg_walk_port(w, e, name, ptr, &kind->inputs[p], &children);
    }

    const int32_t *links = ecs_vec_first_t(&children, int32_t);
    int32_t link_count = ecs_vec_count(&children);
    for (i = 0; i < link_count; i ++) {
        complete = complete && links[i] >= 0;
    }

    if (w->state != BOX_MG_WALK_ERROR && complete) {
        node.type = box_mg_walk_types(w, kind, name, node.counts, links);
    }

    if (w->state == BOX_MG_WALK_ERROR || !complete) {
        ecs_os_free(name);
        goto done;
    }

    node.params = box_mg_walk_params(kind, ptr);
    node.hash = box_mg_hash(BOX_MG_HASH_SEED, &kind->id, ECS_SIZEOF(int32_t));
    node.hash = box_mg_hash(node.hash, node.params, kind->size);
    node.hash = box_mg_hash(node.hash, node.counts, ECS_SIZEOF(node.counts));
    if (kind->capture) {
        node.value = box_mg_walk_capture(w, kind, e, name, ptr, node.type);
        if (!node.value) {
            ecs_os_free(node.params);
            ecs_os_free(name);
            goto done;
        }

        node.hash = box_mg_value_hash(node.hash, node.value);
    }

    const box_mg_node_t *nodes = ecs_vec_first_t(&w->plan->nodes, box_mg_node_t);
    for (i = 0; i < link_count; i ++) {
        node.hash = box_mg_hash(node.hash, &nodes[links[i]].hash, ECS_SIZEOF(uint64_t));
    }

    node.hash = node.hash ? node.hash : 1;
    result = box_mg_walk_add(w, &node, links, link_count);
done:
    ecs_vec_fini_t(NULL, &children, int32_t);

    return result;
}

/** Visit a node entity: check it, then add it and its inputs to the plan.
 *
 * @param w The walk
 * @param e The node entity
 * @param from The node that reads it, 0 for the output
 * @param port The input of from that reads it, NULL for the output
 * @return The index of the node in the plan, -1 when it is incomplete or the walk failed
 */
static int32_t box_mg_walk_visit(
    box_mg_walk_ctx_t *w,
    ecs_entity_t e,
    ecs_entity_t from,
    const char *port)
{
    int32_t result = -1;
    char *from_name = from ? ecs_get_path(w->world, from) : NULL;
    ecs_map_val_t *mark;
    if (w->state == BOX_MG_WALK_ERROR) {
        goto done;
    }

    if (!e || !ecs_is_alive(w->world, e)) {
        if (from) {
            box_mg_walk_error(w, "input '%s' of '%s' is a missing node", port, from_name);
        } else {
            box_mg_walk_error(w, "output is a missing node");
        }

        goto done;
    }

    mark = ecs_map_get(&w->visit, e);
    if (mark) {
        if (!*mark) {
            char *name = ecs_get_path(w->world, e);
            box_mg_walk_error(w, "'%s' is part of a cycle", name);
            ecs_os_free(name);
        } else if (*mark != BOX_MG_INCOMPLETE) {
            result = (int32_t)*mark - 1;
        }

        goto done;
    }

    if (++ w->count > BOX_MESH_GRAPH_NODES) {
        box_mg_walk_error(w, "more than %d nodes", BOX_MESH_GRAPH_NODES);
        goto done;
    }

    box_mg_walk_reach(w, e);
    ecs_map_insert(&w->visit, e, 0);
    char *name = ecs_get_path(w->world, e);
    box_mg_kind_t *kind = box_mg_walk_kind(w, e, name);
    if (kind) {
        result = box_mg_walk_node(w, e, kind, name);
    } else {
        if (w->state != BOX_MG_WALK_ERROR && w->wait) {
            w->state = w->state == BOX_MG_WALK_OK ? BOX_MG_WALK_WAIT : w->state;
        } else {
            box_mg_walk_error(w, "'%s' has no node component", name);
        }

        ecs_os_free(name);
    }

    ecs_map_ensure(&w->visit, e)[0] = result >= 0
        ? (ecs_map_val_t)(result + 1)
        : BOX_MG_INCOMPLETE;
done:
    ecs_os_free(from_name);

    return result;
}

box_mg_walk_t box_mg_plan_walk(
    const ecs_world_t *world,
    box_mesh_graphs_t *g,
    box_mg_plan_t *plan,
    const char *graph,
    ecs_entity_t output,
    bool wait,
    ecs_vec_t *reached,
    int32_t *node,
    char **error)
{
    box_mg_walk_ctx_t w = {
        .world = world, .g = g, .plan = plan, .graph = graph, .reached = reached,
        .wait = wait
    };
    ecs_map_init(&w.visit, NULL);
    *node = box_mg_walk_visit(&w, output, 0, NULL);
    if (w.state == BOX_MG_WALK_OK && *node < 0) {
        box_mg_walk_error(&w, "output is incomplete");
    }

    ecs_map_fini(&w.visit);
    *error = w.error;

    return w.state;
}

void box_mg_plan_need(
    box_mg_plan_t *plan,
    int32_t node)
{
    box_mg_node_t *n = ecs_vec_get_t(&plan->nodes, box_mg_node_t, node);
    int32_t i, links = 0;
    if (n->needed) {
        goto done;
    }

    n->needed = true;
    if (n->value) {
        goto done;
    }

    for (i = 0; i < BOX_MESH_NODE_INPUTS; i ++) {
        links += n->counts[i];
    }

    for (i = 0; i < links; i ++) {
        box_mg_plan_need(plan, *ecs_vec_get_t(&plan->links, int32_t, n->link + i));
    }

done:
    return;
}

/** Evaluate one node whose inputs have values or errors.
 *
 * @param plan The plan
 * @param n The node
 */
static void box_mg_plan_eval_node(
    box_mg_plan_t *plan,
    box_mg_node_t *n)
{
    box_mg_node_t *nodes = ecs_vec_first_t(&plan->nodes, box_mg_node_t);
    int32_t i, count = 0;
    for (i = 0; i < BOX_MESH_NODE_INPUTS; i ++) {
        count += n->counts[i];
    }

    const int32_t *links = count ? ecs_vec_get_t(&plan->links, int32_t, n->link) : NULL;

    const box_mg_value_t **inputs = ecs_os_calloc_n(const box_mg_value_t*, count + 1);
    for (i = 0; i < count && !n->error; i ++) {
        const box_mg_node_t *child = &nodes[links[i]];
        inputs[i] = child->value;
        if (!child->value) {
            n->error = ecs_os_strdup(child->error ? child->error : "an input has no value");
        }
    }

    if (!n->error && n->eval) {
        box_mg_value_t *value = box_mg_value_new(n->type);
        char *error = NULL;
        if (n->eval(n->params, inputs, n->counts, value, &error)) {
            n->error = flecs_asprintf("node '%s': %s", n->name,
                error ? error : "evaluation failed");
            box_mg_value_release(value);
        } else {
            n->value = value;
            n->computed = true;
        }

        ecs_os_free(error);
    } else if (!n->error) {
        n->error = flecs_asprintf("node '%s' has no value", n->name);
    }

    ecs_os_free(ECS_CONST_CAST(void*, inputs));
}

void box_mg_plan_eval(
    box_mg_plan_t *plan)
{
    int32_t i, count = ecs_vec_count(&plan->nodes);
    box_mg_node_t *nodes = ecs_vec_first_t(&plan->nodes, box_mg_node_t);
    for (i = 0; i < count; i ++) {
        box_mg_node_t *n = &nodes[i];
        if (n->needed && !n->value && !n->error) {
            box_mg_plan_eval_node(plan, n);
        }
    }
}

void box_mg_plan_cache(
    const ecs_world_t *world,
    box_mesh_graphs_t *g,
    box_mg_plan_t *plan)
{
    int32_t i, count = ecs_vec_count(&plan->nodes);
    box_mg_node_t *nodes = ecs_vec_first_t(&plan->nodes, box_mg_node_t);
    for (i = 0; i < count; i ++) {
        box_mg_node_t *n = &nodes[i];
        if (!n->computed || !n->value) {
            continue;
        }

        n->computed = false;
        g->stats.evaluated ++;
        box_mg_entry_t *entry = ecs_map_get_deref(&g->cache, box_mg_entry_t, n->hash);
        if (entry) {
            box_mg_value_release(n->value);
            n->value = box_mg_value_keep(entry->value);
        } else {
            entry = ecs_map_insert_alloc_t(&g->cache, box_mg_entry_t, n->hash);
            entry->hash = n->hash;
            entry->value = box_mg_value_keep(n->value);
            entry->bytes = box_mg_value_bytes(n->value);
            g->cache_bytes += entry->bytes;
            g->stats.values ++;
        }

        entry->used = ++ g->clock;
    }

    box_mg_cache_trim(world, g);
}

void box_mg_cache_trim(
    const ecs_world_t *world,
    box_mesh_graphs_t *g)
{
    const MeshGraphCache *settings = ecs_singleton_get(world, MeshGraphCache);
    int64_t budget = settings && settings->budget > 0 ? settings->budget : BOX_MESH_CACHE_BUDGET;
    while (g->cache_bytes > budget) {
        box_mg_entry_t *oldest = NULL;
        ecs_map_iter_t it = ecs_map_iter(&g->cache);
        while (ecs_map_next(&it)) {
            box_mg_entry_t *entry = ecs_map_ptr(&it);
            if (entry->value->refs == 1 && (!oldest || entry->used < oldest->used)) {
                oldest = entry;
            }
        }

        if (!oldest) {
            break;
        }

        uint64_t hash = oldest->hash;
        g->cache_bytes -= oldest->bytes;
        g->stats.values --;
        g->stats.evicted ++;
        box_mg_value_release(oldest->value);
        ecs_map_remove_free(&g->cache, hash);
    }

    g->stats.bytes = g->cache_bytes;
}

void box_mg_cache_fini(
    box_mesh_graphs_t *g)
{
    ecs_map_iter_t it = ecs_map_iter(&g->cache);
    while (ecs_map_next(&it)) {
        box_mg_entry_t *entry = ecs_map_ptr(&it);
        box_mg_value_release(entry->value);
    }

    ecs_map_fini(&g->cache);
    g->cache_bytes = 0;
}
