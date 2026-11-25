/**
 * @file mesh_graph/mesh_graph.c
 * @brief The box.mesh module: the MeshGraph shape, the node kind registry and the systems that
 * build and wait for the evaluations.
 */

#define BOX_MESH_GRAPH_IMPL
#include "mesh_graph.h"
#include "../geometry/mesh_cache.h"
#include "../jobs/jobs.h"

/** Singleton that owns the state of the mesh graphs of a world */
typedef struct MeshGraphRegistry {
    box_mesh_graphs_t *graphs; /**< The state */
} MeshGraphRegistry;

/** Component id of MeshGraphRegistry */
static ECS_COMPONENT_DECLARE(MeshGraphRegistry);

box_mesh_graphs_t* box_mesh_graphs_get(
    const ecs_world_t *world)
{
    const MeshGraphRegistry *r = ecs_id(MeshGraphRegistry)
        ? ecs_singleton_get(world, MeshGraphRegistry)
        : NULL;

    return r ? r->graphs : NULL;
}

box_mg_kind_t* box_mesh_kind(
    const box_mesh_graphs_t *g,
    int32_t id)
{
    box_mg_kind_t *result = NULL;
    if (g && id > 0 && id <= ecs_vec_count(&g->kinds)) {
        result = *ecs_vec_get_t(&g->kinds, box_mg_kind_t*, id - 1);
    }

    return result;
}

box_mg_kind_t* box_mesh_kind_of(
    const box_mesh_graphs_t *g,
    ecs_entity_t component)
{
    ecs_map_val_t *id = ecs_map_get(&g->kind_index, component);

    return id ? box_mesh_kind(g, (int32_t)*id) : NULL;
}

void box_mesh_graphs_touch(
    box_mesh_graphs_t *g,
    ecs_entity_t e)
{
    int32_t i;
    if (e && !ecs_map_get(&g->reached, e)) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&g->records);
    while (ecs_map_next(&it)) {
        box_mg_record_t *r = ecs_map_ptr(&it);
        const ecs_entity_t *reached = ecs_vec_first_t(&r->reached, ecs_entity_t);
        int32_t count = ecs_vec_count(&r->reached);
        if (!e) {
            r->dirty = true;
            continue;
        }

        for (i = 0; i < count && !r->dirty; i ++) {
            r->dirty = reached[i] == e;
        }
    }

done:
    return;
}

/** Build the graphs again that reached an entity whose node component or source component was
 * set or removed, also when it is deleted.
 *
 * @param it The iterator of the observer
 */
static void BoxMeshNodeChanged(
    ecs_iter_t *it)
{
    box_mesh_graphs_t *g = box_mesh_graphs_get(it->world);
    int32_t i;
    if (!g || ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_mesh_graphs_touch(g, it->entities[i]);
    }

done:
    return;
}

/** Free the names a kind owns.
 *
 * @param kind The kind
 */
static void box_mesh_kind_strings_free(
    box_mg_kind_t *kind)
{
    int32_t i;
    for (i = 0; i < kind->input_count; i ++) {
        ecs_os_free(ECS_CONST_CAST(char*, kind->inputs[i].name));
    }

    for (i = 0; i < kind->param_count; i ++) {
        ecs_os_free(ECS_CONST_CAST(char*, kind->params[i].name));
    }
}

/** Copy the description of a node kind into a kind, checking it.
 *
 * @param world The world
 * @param kind The kind, its names receive copies
 * @param desc The description
 * @return 0 on success, -1 when the description is invalid
 */
static int box_mesh_kind_fill(
    const ecs_world_t *world,
    box_mg_kind_t *kind,
    const box_mesh_node_desc_t *desc)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, desc->component);
    int32_t i, result = 0;
    bool seed = false;
    kind->component = desc->component;
    kind->size = ti ? ti->size : 0;
    kind->output = desc->output;
    kind->capture = desc->capture;
    kind->eval = desc->eval;
    kind->lod = desc->lod;
    kind->flags = desc->flags;
    for (i = 0; i < BOX_MESH_NODE_INPUTS && desc->inputs[i].name; i ++) {
        kind->inputs[i] = desc->inputs[i];
        kind->inputs[i].name = ecs_os_strdup(desc->inputs[i].name);
        kind->input_count ++;
        if (desc->inputs[i].offset < 0 || desc->inputs[i].offset >= kind->size) {
            result = -1;
        }
    }

    for (i = 0; i < BOX_MESH_NODE_PARAMS && desc->params[i].name; i ++) {
        kind->params[i] = desc->params[i];
        kind->params[i].name = ecs_os_strdup(desc->params[i].name);
        kind->param_count ++;
        seed = seed || !ecs_os_strcmp(desc->params[i].name, "seed");
        if (desc->params[i].offset < 0 || desc->params[i].size <= 0 ||
            desc->params[i].offset + desc->params[i].size > kind->size)
        {
            result = -1;
        }
    }

    if (!ti || (!desc->capture == !desc->eval) ||
        ((desc->flags & BOX_MESH_NODE_RANDOM) && !seed))
    {
        result = -1;
    }

    return result;
}

/** Describe a node kind in the reflected MeshNodeKind of its component.
 *
 * @param world The world
 * @param kind The kind
 */
static void box_mesh_kind_reflect(
    ecs_world_t *world,
    const box_mg_kind_t *kind)
{
    MeshNodeKind value = {
        .id = kind->id,
        .output = kind->output,
        .input_count = kind->input_count,
        .param_count = kind->param_count,
        .source = kind->capture != NULL,
        .random = (kind->flags & BOX_MESH_NODE_RANDOM) != 0,
        .detail = kind->lod != NULL || (kind->flags & BOX_MESH_NODE_DETAIL) != 0
    };
    int32_t i;
    for (i = 0; i < kind->input_count; i ++) {
        value.inputs[i].name = kind->inputs[i].name;
        value.inputs[i].type = kind->inputs[i].type;
        value.inputs[i].list = kind->inputs[i].list;
        value.inputs[i].optional = kind->inputs[i].optional;
    }

    for (i = 0; i < kind->param_count; i ++) {
        value.params[i].name = kind->params[i].name;
        value.params[i].offset = kind->params[i].offset;
        value.params[i].size = kind->params[i].size;
    }

    ecs_set_ptr(world, kind->component, MeshNodeKind, &value);
}

int32_t box_mesh_node_register(
    ecs_world_t *world,
    const box_mesh_node_desc_t *desc)
{
    box_mesh_graphs_t *g = box_mesh_graphs_get(world);
    box_mg_kind_t *kind = NULL, *existing = NULL;
    int32_t result = 0;
    if (!g || !desc || !desc->component) {
        ecs_err("box_mesh_node_register: invalid node kind description");
        goto done;
    }

    existing = box_mesh_kind_of(g, desc->component);
    kind = ecs_os_calloc_t(box_mg_kind_t);
    if (box_mesh_kind_fill(world, kind, desc)) {
        char *name = ecs_get_path(world, desc->component);
        ecs_err("box_mesh_node_register: node kind '%s' needs capture or eval, members in its "
            "component and a seed when it is random", name);
        ecs_os_free(name);
        box_mesh_kind_strings_free(kind);
        ecs_os_free(kind);
        goto done;
    }

    if (existing) {
        kind->id = existing->id;
        box_mesh_kind_strings_free(existing);
        *existing = *kind;
        ecs_os_free(kind);
        kind = existing;
    } else {
        kind->id = ecs_vec_count(&g->kinds) + 1;
        *ecs_vec_append_t(NULL, &g->kinds, box_mg_kind_t*) = kind;
        ecs_map_insert(&g->kind_index, desc->component, (ecs_map_val_t)kind->id);
        ecs_observer(world, {
            .query.terms = {{ .id = desc->component }},
            .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
            .events = { EcsOnSet, EcsOnRemove },
            .callback = BoxMeshNodeChanged
        });
    }

    result = kind->id;
    box_mesh_kind_reflect(world, kind);
    box_mesh_graphs_touch(g, 0);
done:
    return result;
}

/** Drop a user of an output record, freeing it with the last.
 *
 * @param g The state
 * @param node The output node
 */
static void box_mesh_graph_output_release(
    box_mesh_graphs_t *g,
    ecs_entity_t node)
{
    box_mg_output_t *out = ecs_map_get_deref(&g->outputs, box_mg_output_t, node);
    if (out && -- out->users <= 0) {
        box_mesh_graph_output_clear(out);
        ecs_map_remove_free(&g->outputs, node);
    }
}

/** Track the MeshGraph shapes that are set: a new graph or another output builds again, the
 * new output showing the mesh of the old one until it has its own. A set with the same output
 * changes nothing, as a mesh delivered to the shape sets it.
 *
 * @param it The iterator of the observer
 */
static void BoxMeshGraphOnSet(
    ecs_iter_t *it)
{
    box_mesh_graphs_t *g = box_mesh_graphs_get(it->world);
    const MeshGraph *graphs = ecs_field(it, MeshGraph, 0);
    int32_t i;
    if (!g) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        box_mg_record_t *r = ecs_map_get_deref(&g->records, box_mg_record_t, e);
        bool added = !r;
        if (!r) {
            r = ecs_map_insert_alloc_t(&g->records, box_mg_record_t, e);
            ecs_os_memset_t(r, 0, box_mg_record_t);
            r->entity = e;
            ecs_vec_init_t(NULL, &r->reached, ecs_entity_t, 0);
        }

        int32_t levels = graphs[i].levels > 1 ? graphs[i].levels : 1;
        levels = levels < BOX_LOD_LEVELS_MAX ? levels : BOX_LOD_LEVELS_MAX;
        if (!added && r->output == graphs[i].output) {
            r->dirty = r->dirty || r->levels != levels;
            r->levels = levels;
            continue;
        }

        r->levels = levels;
        box_mg_output_t *out = box_mesh_graph_output(g, graphs[i].output);
        out->users ++;
        const box_mg_output_t *old = r->output
            ? ecs_map_get_deref(&g->outputs, box_mg_output_t, r->output)
            : NULL;
        if (old && old->value && !out->value) {
            box_mesh_graph_output_copy(out, old);
        }

        if (r->output) {
            box_mesh_graph_output_release(g, r->output);
        }

        r->output = graphs[i].output;
        r->dirty = true;
    }

done:
    return;
}

/** Forget the MeshGraph shapes that lose their MeshGraph or are deleted.
 *
 * @param it The iterator of the observer
 */
static void BoxMeshGraphOnRemove(
    ecs_iter_t *it)
{
    box_mesh_graphs_t *g = box_mesh_graphs_get(it->world);
    bool live = it->other_table && ecs_table_get_type(it->other_table)->count;
    int32_t i;
    if (!g || ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        box_mg_record_t *r = ecs_map_get_deref(&g->records, box_mg_record_t, e);
        if (!r) {
            continue;
        }

        box_mesh_graph_unreach(g, r);
        ecs_vec_fini_t(NULL, &r->reached, ecs_entity_t);
        if (r->output) {
            box_mesh_graph_output_release(g, r->output);
        }

        ecs_map_remove_free(&g->records, e);
        if (live) {
            ecs_remove(it->stage, e, MeshGraphStatus);
        }
    }

done:
    return;
}

/** Find the output a MeshGraph names.
 *
 * @param world The world
 * @param graph The MeshGraph
 * @return The output, NULL for none
 */
static const box_mg_output_t* box_mesh_graph_output_of(
    const ecs_world_t *world,
    const MeshGraph *graph)
{
    const box_mesh_graphs_t *g = box_mesh_graphs_get(world);

    return g && graph->output
        ? ecs_map_get_deref(&g->outputs, box_mg_output_t, graph->output)
        : NULL;
}

/** Key of a MeshGraph: the content hash of the levels delivered for its output and the size of
 * the mesh of level 0, all zero before the first, and the levels of detail of the graph in f[1],
 * as the mesh cache keeps the MeshGraph of the first shape of a key to make its levels from.
 *
 * @param world The world
 * @param value The MeshGraph
 * @param key The key
 */
static void box_mesh_graph_key(
    const ecs_world_t *world,
    const void *value,
    box_shape_key_t *key)
{
    const MeshGraph *graph = value;
    const box_mg_output_t *out = box_mesh_graph_output_of(world, graph);
    if (out && out->value) {
        int32_t levels = graph->levels > 1 ? graph->levels : 1;
        key->i[0] = (int32_t)(uint32_t)(out->hash & 0xFFFFFFFFu);
        key->i[1] = (int32_t)(uint32_t)(out->hash >> 32);
        key->i[2] = out->value->mesh.vertex_count;
        key->i[3] = out->value->mesh.index_count;
        key->f[1] = (float)(levels < BOX_LOD_LEVELS_MAX ? levels : BOX_LOD_LEVELS_MAX);
    }
}

/** Key of a level of detail of a MeshGraph: the content hash and the size of the mesh of the
 * level, and the level in f[0]. A level the output does not have, or whose mesh is the mesh of
 * the level before, has the key of the level before.
 *
 * @param world The world
 * @param value The MeshGraph
 * @param level The level, from 1
 * @param key The key
 */
static void box_mesh_graph_lod(
    const ecs_world_t *world,
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    const MeshGraph *graph = value;
    const box_mg_output_t *out = box_mesh_graph_output_of(world, graph);
    int32_t levels = graph->levels > 1 ? graph->levels : 1;
    levels = out && out->levels < levels ? out->levels : levels;
    level = level < levels ? level : levels - 1;
    while (out && level > 0 && (!out->lods[level] ||
        out->lod_hashes[level] == out->lod_hashes[level - 1]))
    {
        level --;
    }

    if (!out || level <= 0) {
        box_mesh_graph_key(world, value, key);
        goto done;
    }

    uint64_t hash = out->lod_hashes[level];
    key->i[0] = (int32_t)(uint32_t)(hash & 0xFFFFFFFFu);
    key->i[1] = (int32_t)(uint32_t)(hash >> 32);
    key->i[2] = out->lods[level]->mesh.vertex_count;
    key->i[3] = out->lods[level]->mesh.index_count;
    key->f[0] = (float)level;
done:
    return;
}

/** Give a generated mesh the per vertex data of the data.0 to data.15 attributes of a Mesh
 * value, when it has one of them: data.8 to data.15 as its upper data, when it has one of
 * those.
 *
 * @param m The Mesh value
 * @param mesh The generated mesh, with its vertices
 */
static void box_mesh_graph_data(
    const box_mg_mesh_t *m,
    box_mesh_t *mesh)
{
    const float *channels[16];
    bool any = false, upper = false;
    int32_t i, k;
    for (k = 0; k < 16; k ++) {
        char name[BOX_MESH_ATTR_NAME];
        ecs_os_snprintf(name, ECS_SIZEOF(name), "%s%d", BOX_MESH_DATA_ATTR, k);
        channels[k] = box_mg_attr_find(m->attrs, m->attr_count, name);
        any = any || channels[k];
        upper = upper || (k >= 8 && channels[k]);
    }

    for (i = 0; any && i < m->vertex_count; i ++) {
        vec4 d[4];
        for (k = 0; k < 16; k ++) {
            d[k / 4][k % 4] = channels[k] ? channels[k][i] : 0.0f;
        }

        box_mesh_vertex_data(mesh, i, d[0], d[1]);
        if (upper) {
            box_mesh_vertex_data_upper(mesh, i, d[2], d[3]);
        }
    }
}

/** Generate the mesh of a MeshGraph from the value delivered for its output.
 *
 * @param world The world
 * @param value The MeshGraph
 * @param key The key
 * @param mesh The mesh
 */
static void box_mesh_graph_generate(
    const ecs_world_t *world,
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    const box_mg_output_t *out = box_mesh_graph_output_of(world, value);
    int32_t level = (int32_t)key->f[0], i;
    level = level > 0 && level < BOX_LOD_LEVELS_MAX ? level : 0;
    const box_mg_value_t *v = out ? (level ? out->lods[level] : out->value) : NULL;
    uint64_t hash = out ? (level ? out->lod_hashes[level] : out->hash) : 0;
    const box_mg_mesh_t *m = v ? &v->mesh : NULL;
    if (!m || key->i[0] != (int32_t)(uint32_t)(hash & 0xFFFFFFFFu) ||
        key->i[1] != (int32_t)(uint32_t)(hash >> 32) || !m->vertex_count ||
        !m->index_count)
    {
        goto done;
    }

    mesh->vertices = ecs_os_malloc_n(box_vertex_t, m->vertex_count);
    mesh->indices = ecs_os_malloc_n(uint32_t, m->index_count);
    mesh->vertex_count = mesh->vertex_capacity = m->vertex_count;
    mesh->index_count = mesh->index_capacity = m->index_count;
    for (i = 0; i < m->vertex_count; i ++) {
        glm_vec3_copy(m->positions[i], mesh->vertices[i].position);
        glm_vec3_copy(m->normals[i], mesh->vertices[i].normal);
        glm_vec2_copy(m->uvs[i], mesh->vertices[i].uv);
    }

    ecs_os_memcpy_n(mesh->indices, m->indices, uint32_t, m->index_count);
    box_mesh_graph_data(m, mesh);
done:
    return;
}

/** Surface of a MeshGraph: none, so that a MeshGraph without a mesh resolves to no mesh and
 * draws nothing.
 *
 * @param world The world
 * @param value The MeshGraph
 * @param data The surface parameters, left empty
 * @param min The minimum of the bounds, left as is
 * @param max The maximum of the bounds, left as is
 */
static void box_mesh_graph_surface(
    const ecs_world_t *world,
    const void *value,
    box_surface_data_t *data,
    vec3 min,
    vec3 max)
{
    (void)world;
    (void)value;
    (void)data;
    (void)min;
    (void)max;
}

/** Build the graphs that changed, or wait for a node or a source, at the end of the frame.
 *
 * @param it The iterator of the system
 */
static void BoxMeshGraphBuild(
    ecs_iter_t *it)
{
    box_mesh_graphs_t *g = box_mesh_graphs_get(it->world);
    if (g) {
        box_mesh_graphs_build(it->stage, g);
    }
}

/** In a headless run, wait for the evaluations in flight before the frame, so that their
 * completion in the frame gives the shapes their meshes as a frame of a window would later.
 *
 * @param it The iterator of the system
 */
static void BoxMeshGraphWait(
    ecs_iter_t *it)
{
    box_mesh_graphs_t *g = box_mesh_graphs_get(it->world);
    const App *app = ecs_singleton_get(it->world, App);
    ecs_time_t start = {0};
    if (!g || !app || !app->headless || app->async_loads) {
        goto done;
    }

    ecs_time_measure(&start);
    for (;;) {
        ecs_os_mutex_lock(g->sync->lock);
        int32_t working = g->sync->working;
        ecs_os_mutex_unlock(g->sync->lock);
        ecs_time_t now = start;
        double waited = ecs_time_measure(&now);
        if (!working) {
            break;
        }

        if (waited > BOX_MESH_HEADLESS_WAIT) {
            ecs_warn("box.mesh: %d mesh graph evaluations still running after %.0f seconds",
                working, waited);
            break;
        }

        ecs_os_sleep(0, 1000 * 1000);
    }

done:
    return;
}

/** Free the state of the mesh graphs of a world.
 *
 * @param world The world
 * @param ctx The state
 */
static void box_mesh_graphs_fini(
    ecs_world_t *world,
    void *ctx)
{
    box_mesh_graphs_t *g = ctx;
    int32_t i, count = ecs_vec_count(&g->kinds);
    (void)world;
    for (i = 0; i < count; i ++) {
        box_mg_kind_t *kind = *ecs_vec_get_t(&g->kinds, box_mg_kind_t*, i);
        box_mesh_kind_strings_free(kind);
        ecs_os_free(kind);
    }

    ecs_vec_fini_t(NULL, &g->kinds, box_mg_kind_t*);
    ecs_map_fini(&g->kind_index);
    ecs_map_iter_t it = ecs_map_iter(&g->records);
    while (ecs_map_next(&it)) {
        box_mg_record_t *r = ecs_map_ptr(&it);
        ecs_vec_fini_t(NULL, &r->reached, ecs_entity_t);
        ecs_os_free(r);
    }

    ecs_map_fini(&g->records);
    it = ecs_map_iter(&g->outputs);
    while (ecs_map_next(&it)) {
        box_mg_output_t *out = ecs_map_ptr(&it);
        box_mesh_graph_output_clear(out);
        ecs_os_free(out);
    }

    ecs_map_fini(&g->outputs);
    ecs_map_fini(&g->reached);
    box_mg_cache_fini(g);
    if (!-- g->sync->refs) {
        ecs_os_mutex_free(g->sync->lock);
        ecs_os_free(g->sync);
    }

    ecs_os_free(g);
}

/* Registers a component under its C type and names it after, as box.texture does. */
#define BOX_MESH_COMPONENT(world, T, name)\
    ECS_META_COMPONENT(world, T);\
    ecs_set_name(world, ecs_id(T), name)

/** Register the components of box.mesh: box.mesh.Graph, box.mesh.Status, box.mesh.NodeKind and
 * the nodes in box.mesh.nodes, such as box.mesh.nodes.Tube.
 *
 * @param world The world, in the scope of box.mesh
 */
static void box_mesh_register(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);
    ecs_entity_t nodes = ecs_entity(world, { .name = "nodes" });
    ecs_add_id(world, nodes, EcsModule);

    ecs_set_name_prefix(world, "MeshValue");
    BOX_MESH_COMPONENT(world, MeshValueType, "ValueType");
    ecs_set_name_prefix(world, "MeshGraph");
    BOX_MESH_COMPONENT(world, MeshGraphState, "State");
    ecs_set_name_prefix(world, "MeshBranch");
    BOX_MESH_COMPONENT(world, MeshBranchDistribution, "BranchDistribution");
    ecs_set_name_prefix(world, "MeshEnvelope");
    BOX_MESH_COMPONENT(world, MeshEnvelopeShape, "EnvelopeShape");
    ecs_set_name_prefix(world, "MeshLeaf");
    BOX_MESH_COMPONENT(world, MeshLeafPlacement, "LeafPlacement");
    BOX_MESH_COMPONENT(world, MeshLeafOrientation, "LeafOrientation");
    ecs_set_name_prefix(world, "MeshCard");
    BOX_MESH_COMPONENT(world, MeshCardShape, "CardShape");
    ecs_set_name_prefix(world, "MeshCrown");
    BOX_MESH_COMPONENT(world, MeshCrownShape, "CrownShape");
    ecs_set_name_prefix(world, "MeshNeedle");
    BOX_MESH_COMPONENT(world, MeshNeedleStyle, "NeedleStyle");
    ecs_set_name_prefix(world, NULL);
    BOX_MESH_COMPONENT(world, MeshGraph, "Graph");
    BOX_MESH_COMPONENT(world, MeshGraphStatus, "Status");
    BOX_MESH_COMPONENT(world, MeshGraphCache, "Cache");
    BOX_MESH_COMPONENT(world, MeshNodePort, "Port");
    BOX_MESH_COMPONENT(world, MeshNodeParam, "Param");
    BOX_MESH_COMPONENT(world, MeshNodeKind, "NodeKind");

    ecs_set_scope(world, nodes);
    ecs_set_name_prefix(world, "Mesh");
    ECS_META_COMPONENT(world, MeshShape);
    ECS_META_COMPONENT(world, MeshMesh3);
    ECS_META_COMPONENT(world, MeshPath);
    ECS_META_COMPONENT(world, MeshTransform);
    ECS_META_COMPONENT(world, MeshMerge);
    ECS_META_COMPONENT(world, MeshDetail);
    ECS_META_COMPONENT(world, MeshTube);
    ECS_META_COMPONENT(world, MeshPointsOnCurves);
    ECS_META_COMPONENT(world, MeshPointsOnMesh);
    ECS_META_COMPONENT(world, MeshInstanceOnPoints);
    ECS_META_COMPONENT(world, MeshDisplace);
    ECS_META_COMPONENT(world, MeshNormals);
    ECS_META_COMPONENT(world, MeshStem);
    ECS_META_COMPONENT(world, MeshBranches);
    ECS_META_COMPONENT(world, MeshPrune);
    ECS_META_COMPONENT(world, MeshPipeModel);
    ECS_META_COMPONENT(world, MeshCut);
    ECS_META_COMPONENT(world, MeshStrata);
    ECS_META_COMPONENT(world, MeshJitter);
    ECS_META_COMPONENT(world, MeshLeaves);
    ECS_META_COMPONENT(world, MeshNeedles);
    ECS_META_COMPONENT(world, MeshCards);
    ECS_META_COMPONENT(world, MeshCrownNormals);
    ECS_META_COMPONENT(world, MeshCrownOcclusion);
    ecs_set_name_prefix(world, NULL);
    ecs_set_scope(world, module);

    box_owned_hooks(world, MeshGraphStatus, BOX_OWNED_STR(MeshGraphStatus, error));
    box_owned_hooks(world, MeshMerge, BOX_OWNED_VEC(MeshMerge, inputs, ecs_entity_t));
    box_owned_hooks(world, MeshDetail, BOX_OWNED_VEC(MeshDetail, levels, ecs_entity_t));
    box_owned_hooks(world, MeshNodePort, BOX_OWNED_STR(MeshNodePort, name));
    box_owned_hooks(world, MeshNodeParam, BOX_OWNED_STR(MeshNodeParam, name));
    box_owned_hooks(world, MeshNodeKind,
        BOX_OWNED_STR(MeshNodeKind, inputs[0].name),
        BOX_OWNED_STR(MeshNodeKind, inputs[1].name),
        BOX_OWNED_STR(MeshNodeKind, inputs[2].name),
        BOX_OWNED_STR(MeshNodeKind, inputs[3].name),
        BOX_OWNED_STR(MeshNodeKind, params[0].name),
        BOX_OWNED_STR(MeshNodeKind, params[1].name),
        BOX_OWNED_STR(MeshNodeKind, params[2].name),
        BOX_OWNED_STR(MeshNodeKind, params[3].name),
        BOX_OWNED_STR(MeshNodeKind, params[4].name),
        BOX_OWNED_STR(MeshNodeKind, params[5].name),
        BOX_OWNED_STR(MeshNodeKind, params[6].name),
        BOX_OWNED_STR(MeshNodeKind, params[7].name),
        BOX_OWNED_STR(MeshNodeKind, params[8].name),
        BOX_OWNED_STR(MeshNodeKind, params[9].name),
        BOX_OWNED_STR(MeshNodeKind, params[10].name),
        BOX_OWNED_STR(MeshNodeKind, params[11].name),
        BOX_OWNED_STR(MeshNodeKind, params[12].name),
        BOX_OWNED_STR(MeshNodeKind, params[13].name),
        BOX_OWNED_STR(MeshNodeKind, params[14].name),
        BOX_OWNED_STR(MeshNodeKind, params[15].name),
        BOX_OWNED_STR(MeshNodeKind, params[16].name),
        BOX_OWNED_STR(MeshNodeKind, params[17].name),
        BOX_OWNED_STR(MeshNodeKind, params[18].name),
        BOX_OWNED_STR(MeshNodeKind, params[19].name),
        BOX_OWNED_STR(MeshNodeKind, params[20].name),
        BOX_OWNED_STR(MeshNodeKind, params[21].name),
        BOX_OWNED_STR(MeshNodeKind, params[22].name),
        BOX_OWNED_STR(MeshNodeKind, params[23].name),
        BOX_OWNED_STR(MeshNodeKind, params[24].name),
        BOX_OWNED_STR(MeshNodeKind, params[25].name),
        BOX_OWNED_STR(MeshNodeKind, params[26].name),
        BOX_OWNED_STR(MeshNodeKind, params[27].name),
        BOX_OWNED_STR(MeshNodeKind, params[28].name),
        BOX_OWNED_STR(MeshNodeKind, params[29].name),
        BOX_OWNED_STR(MeshNodeKind, params[30].name),
        BOX_OWNED_STR(MeshNodeKind, params[31].name));
}

void box_mesh_graph_shape_init(
    ecs_world_t *world)
{
    ecs_add_pair(world, ecs_id(MeshGraph), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(MeshGraphStatus), EcsOnInstantiate, EcsDontInherit);
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OnSet" }),
        .query.terms = {{ .id = ecs_id(MeshGraph), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet },
        .callback = BoxMeshGraphOnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OnRemove" }),
        .query.terms = {{ .id = ecs_id(MeshGraph), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnRemove },
        .callback = BoxMeshGraphOnRemove
    });

    box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(MeshGraph),
        .key_world = box_mesh_graph_key,
        .generate_world = box_mesh_graph_generate,
        .surface_world = box_mesh_graph_surface,
        .lod_world = box_mesh_graph_lod
    });

    ecs_entity_t sources[] = {
        ecs_id(ShapeMesh), ecs_id(Mesh3), ecs_id(Path3D), ecs_id(Path2D)
    };
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = sources[i], .src.id = EcsSelf }},
            .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
            .events = { EcsOnSet, EcsOnRemove },
            .callback = BoxMeshNodeChanged
        });
    }
}

void BoxMeshImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxMesh);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxApp);
    ECS_IMPORT(world, BoxJobs);
    ECS_IMPORT(world, BoxGeometry);

    box_mesh_register(world);
    ECS_COMPONENT_DEFINE(world, MeshGraphRegistry);
    box_mesh_graphs_t *g = ecs_os_calloc_t(box_mesh_graphs_t);
    ecs_vec_init_t(NULL, &g->kinds, box_mg_kind_t*, 0);
    ecs_map_init(&g->kind_index, NULL);
    ecs_map_init(&g->records, NULL);
    ecs_map_init(&g->outputs, NULL);
    ecs_map_init(&g->reached, NULL);
    ecs_map_init(&g->cache, NULL);
    g->sync = ecs_os_calloc_t(box_mg_sync_t);
    g->sync->lock = ecs_os_mutex_new();
    g->sync->refs = 1;
    ecs_singleton_set(world, MeshGraphRegistry, { .graphs = g });
    ecs_atfini(world, box_mesh_graphs_fini, g);

    box_mesh_graph_shape_init(world);
    box_mesh_nodes_source_register(world);
    box_mesh_nodes_mesh_register(world);
    box_mesh_nodes_curves_register(world);
    box_mesh_nodes_branch_register(world);
    box_mesh_nodes_foliage_register(world);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Build" }),
        .phase = EcsOnStore,
        .run = BoxMeshGraphBuild
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Wait" }),
        .phase = EcsPreFrame,
        .run = BoxMeshGraphWait
    });
}
