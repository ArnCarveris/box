/**
 * @file texture/graph.c
 * @brief Node kinds of texture graphs and the build of a TextureGraph into a compiled graph.
 *
 * A change of a TextureGraph or of a node it reached marks it; the build at the end of the frame
 * walks it from its output, checks it and compiles it into the registry entry of the texture,
 * which the render extract sends to the backend. The backend evaluates it into the image.
 */

#include "texture.h"
#include "../render/render.h"
#include <math.h>
#include <stdarg.h>

/** Most nodes of a graph */
#define BOX_TEXTURE_GRAPH_NODES (1024)

/** Most texels the window of a graph on DomainWorld is grown by on each side */
#define BOX_TEXTURE_GRAPH_APRON (1024)

/** Offset basis of the 64 bit FNV-1a hash of the compiled graphs */
#define BOX_TEXTURE_GRAPH_FNV_BASIS (14695981039346656037ull)

/** Prime of the 64 bit FNV-1a hash of the compiled graphs */
#define BOX_TEXTURE_GRAPH_FNV_PRIME (1099511628211ull)

/** State of the build of one graph */
typedef struct box_texture_graph_build_t {
    ecs_world_t *world;         /**< The world or a stage */
    box_textures_t *t;          /**< The registry */
    ecs_entity_t graph;         /**< The texture entity with the TextureGraph */
    char *name;                 /**< Path of the graph */
    ecs_map_t index;            /**< Node entity to 0 while it is visited, index + 1 after */
    ecs_vec_t nodes;            /**< box_texture_graph_node_t in topological order */
    ecs_vec_t params;           /**< uint8_t, parameter bytes of the nodes */
    ecs_vec_t *reached;         /**< ecs_entity_t, the entities the walk reached */
    ecs_vec_t textures;         /**< box_texture_graph_texture_t, the textures the nodes sample
                                 *   in node order */
    TextureGraphDomain domain;  /**< Coordinates of the texels of the graph */
    double origin[2];           /**< World x and z of the first texel on DomainWorld */
    float spacing[2];           /**< Meters between neighbouring texels on DomainWorld */
    ecs_vec_t window;           /**< uint8_t, window elements of the nodes on DomainWorld, which
                                 *   follow the parameter bytes of every node */
    char *error;                /**< Why the build failed, NULL while it did not */
    bool wait;                  /**< Wait for a node without a node component */
    bool waiting;               /**< The walk found a node without a node component, or a
                                 *   sampled entity without a texture component */
    bool held;                  /**< A sampled texture has no image yet */
} box_texture_graph_build_t;

/** Add bytes to a 64 bit FNV-1a hash.
 *
 * @param hash The hash so far
 * @param data The bytes
 * @param size Number of bytes
 * @return The hash with the bytes
 */
static uint64_t box_texture_graph_hash(
    uint64_t hash,
    const void *data,
    int32_t size)
{
    const uint8_t *bytes = data;
    int32_t i;
    for (i = 0; i < size; i ++) {
        hash ^= bytes[i];
        hash *= BOX_TEXTURE_GRAPH_FNV_PRIME;
    }

    return hash;
}

box_texture_node_kind_t* box_texture_node_kind(
    const box_textures_t *t,
    int32_t id)
{
    box_texture_node_kind_t *result = NULL;
    if (t && id > 0 && id <= ecs_vec_count(&t->node_kinds)) {
        result = *ecs_vec_get_t(&t->node_kinds, box_texture_node_kind_t*, id - 1);
    }

    return result;
}

/** Free the strings a node kind owns.
 *
 * @param kind The kind
 */
static void box_texture_node_kind_strings_free(
    box_texture_node_kind_t *kind)
{
    int32_t i;
    for (i = 0; i < kind->input_count; i ++) {
        ecs_os_free(ECS_CONST_CAST(char*, kind->inputs[i].name));
        ecs_os_free(ECS_CONST_CAST(char*, kind->inputs[i].value));
    }

    for (i = 0; i < kind->param_count; i ++) {
        ecs_os_free(ECS_CONST_CAST(char*, kind->params[i].name));
    }

    ecs_os_free(ECS_CONST_CAST(char*, kind->array.name));

    ecs_os_free(kind->function);
    ecs_os_free(kind->source);
    ecs_os_free(kind->texture);
    ecs_os_free(kind->linear);
}

/** Free a compiled graph.
 *
 * @param graph The graph, may be NULL
 */
static void box_texture_graph_free(
    box_texture_graph_t *graph)
{
    if (graph) {
        ecs_os_free(graph->nodes);
        ecs_os_free(graph->params);
        ecs_os_free(graph->textures);
        ecs_os_free(graph);
    }
}

void box_texture_graph_clear(
    box_texture_t *tex)
{
    box_texture_graph_free(tex->graph);
    tex->graph = NULL;
    ecs_vec_fini_t(NULL, &tex->reached, ecs_entity_t);
    tex->graph_dirty = false;
    tex->graph_waiting = false;
    tex->graph_held = false;
    tex->graph_sent = false;
    tex->graph_pending = false;
}

void box_texture_graphs_fini(
    box_textures_t *t)
{
    int32_t i, count = ecs_vec_count(&t->node_kinds);
    for (i = 0; i < count; i ++) {
        box_texture_node_kind_t *kind = *ecs_vec_get_t(
            &t->node_kinds, box_texture_node_kind_t*, i);
        box_texture_node_kind_strings_free(kind);
        ecs_os_free(kind);
    }

    ecs_vec_fini_t(NULL, &t->node_kinds, box_texture_node_kind_t*);
    ecs_vec_fini_t(NULL, &t->graph_queued, ecs_entity_t);
    ecs_vec_fini_t(NULL, &t->graph_released, uint32_t);
    ecs_vec_fini_t(NULL, &t->reload, ecs_entity_t);
}

void box_texture_graph_request(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e)
{
    box_texture_t *tex = box_texture_entry(t, e);
    tex->generation ++;
    tex->graph_dirty = true;
    box_texture_status(world, e, TextureLoading, box_texture_revision(world, e), NULL);
}

void box_texture_graphs_touch(
    box_textures_t *t,
    ecs_entity_t e)
{
    int32_t i;
    ecs_map_iter_t it = ecs_map_iter(&t->textures);
    while (ecs_map_next(&it)) {
        box_texture_t *tex = ecs_map_ptr(&it);
        const ecs_entity_t *reached = ecs_vec_first_t(&tex->reached, ecs_entity_t);
        int32_t count = ecs_vec_count(&tex->reached);
        if (!e && count) {
            tex->graph_dirty = true;
            continue;
        }

        for (i = 0; i < count; i ++) {
            if (reached[i] == e) {
                tex->graph_dirty = true;
                break;
            }
        }
    }
}

/** Build again the graphs that reached a node whose node component was set or removed, also
 * when the node is deleted.
 *
 * @param it The iterator of the observer
 */
static void BoxTextureNodeChanged(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    int32_t i;
    if (!t || ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_texture_graphs_touch(t, it->entities[i]);
    }

done:
    return;
}

/** Copy the description of a node kind into a kind of the registry, checking it.
 *
 * @param kind The kind, its strings receive copies
 * @param desc The description
 * @return 0 on success, -1 when the description is invalid
 */
static int box_texture_node_kind_fill(
    box_texture_node_kind_t *kind,
    const box_texture_node_desc_t *desc)
{
    int32_t i, offset = 0, result = 0;
    bool any = false;
    kind->component = desc->component;
    kind->output = desc->output;
    kind->output_type = desc->output_type;
    kind->flags = desc->flags;
    kind->passes = desc->passes > 1 ? desc->passes : 1;
    if (kind->passes > 1) {
        kind->flags |= BOX_TEXTURE_NODE_NEIGHBOURHOOD;
    }

    if (kind->passes > BOX_TEXTURE_NODE_PASSES) {
        result = -1;
    }

    kind->function = ecs_os_strdup(desc->function);
    kind->source = ecs_os_strdup(desc->source);
    kind->flags &= ~(BOX_TEXTURE_NODE_SAMPLES | BOX_TEXTURE_NODE_WINDOW);
    kind->window = desc->window;
    kind->window_size = desc->window ? desc->window_size : 0;
    kind->reach = desc->reach;
    if (desc->window) {
        kind->flags |= BOX_TEXTURE_NODE_WINDOW;
        if (desc->window_size < 1 || desc->window_size > BOX_TEXTURE_NODE_WINDOW_MAX) {
            result = -1;
        }
    }

    if (desc->texture) {
        kind->flags |= BOX_TEXTURE_NODE_SAMPLES;
        kind->texture = ecs_os_strdup(desc->texture);
        kind->texture_offset = desc->texture_offset;
        kind->linear = desc->linear ? ecs_os_strdup(desc->linear) : NULL;
        kind->linear_offset = desc->linear_offset;
    }

    for (i = 0; i < BOX_TEXTURE_NODE_INPUTS && desc->inputs[i].name; i ++) {
        kind->inputs[i] = desc->inputs[i];
        kind->inputs[i].name = ecs_os_strdup(desc->inputs[i].name);
        kind->inputs[i].value = desc->inputs[i].value
            ? ecs_os_strdup(desc->inputs[i].value)
            : NULL;
        kind->input_count ++;
        any = any || desc->inputs[i].type == TexAny;
    }

    for (i = 0; i < BOX_TEXTURE_NODE_PARAMS && desc->params[i].name; i ++) {
        bool flag = desc->params[i].kind == BOX_TEXTURE_PARAM_BOOL;
        ecs_size_t size = flag ? ECS_SIZEOF(uint32_t) : desc->params[i].size;
        int32_t align = size <= 4 ? 4 : size <= 8 ? 8 : 16;
        kind->params[i] = desc->params[i];
        kind->params[i].name = ecs_os_strdup(desc->params[i].name);
        kind->param_count ++;
        if (flag ? desc->params[i].size != ECS_SIZEOF(bool)
            : (size < 4 || size > 16 || size % 4))
        {
            result = -1;
        }

        offset = (offset + align - 1) / align * align;
        kind->param_offsets[i] = offset;
        offset += size;
    }

    kind->types_offset = -1;
    if (any) {
        kind->types_offset = (offset + 3) / 4 * 4;
        offset = kind->types_offset + ECS_SIZEOF(uint32_t);
    }

    kind->param_size = (offset + 15) / 16 * 16;
    kind->array_header = -1;
    if (desc->array.name) {
        kind->array = desc->array;
        kind->array.name = ecs_os_strdup(desc->array.name);
        kind->array_header = kind->param_size;
        kind->param_size += 16;
        if (desc->array.max < 1 || desc->array.max > BOX_TEXTURE_NODE_ARRAY_MAX ||
            (!desc->array.pack && (desc->array.size < 4 || desc->array.size > 16)))
        {
            result = -1;
        }
    }

    kind->node_size = kind->param_size + kind->input_count * 16 +
        (kind->array.name ? kind->array.max * 16 : 0);
    if (kind->param_size > BOX_TEXTURE_NODE_PARAM_BYTES) {
        result = -1;
    }

    return result;
}

/** Describe a node kind in the reflected TextureNodeKind of its component.
 *
 * @param world The world
 * @param kind The kind
 */
static void box_texture_node_kind_reflect(
    ecs_world_t *world,
    const box_texture_node_kind_t *kind)
{
    TextureNodeKind value = {
        .id = kind->id,
        .output = kind->output,
        .input_count = kind->input_count,
        .param_count = kind->param_count,
        .param_size = kind->param_size,
        .array = kind->array.name,
        .array_max = kind->array.name ? kind->array.max : 0,
        .array_header = kind->array_header,
        .types_offset = kind->types_offset,
        .function = kind->function,
        .neighbourhood = (kind->flags & BOX_TEXTURE_NODE_NEIGHBOURHOOD) != 0,
        .animated = (kind->flags & BOX_TEXTURE_NODE_ANIMATED) != 0,
        .plane = (kind->flags & BOX_TEXTURE_NODE_PLANE) != 0,
        .passes = kind->passes,
        .texture = kind->texture
    };
    int32_t i;
    for (i = 0; i < kind->input_count; i ++) {
        value.inputs[i].name = kind->inputs[i].name;
        value.inputs[i].type = kind->inputs[i].type;
        value.inputs[i].fallback = kind->inputs[i].value != NULL;
        value.inputs[i].value = kind->inputs[i].value;
        value.inputs[i].coords = kind->inputs[i].coords;
    }

    for (i = 0; i < kind->param_count; i ++) {
        value.params[i].name = kind->params[i].name;
        value.params[i].offset = kind->param_offsets[i];
        value.params[i].size = kind->params[i].kind == BOX_TEXTURE_PARAM_BOOL
            ? ECS_SIZEOF(uint32_t)
            : kind->params[i].size;
    }

    ecs_set_ptr(world, kind->component, TextureNodeKind, &value);
}

int32_t box_texture_node_register(
    ecs_world_t *world,
    const box_texture_node_desc_t *desc)
{
    box_textures_t *t = box_textures_get(world);
    box_texture_node_kind_t *kind = NULL, *existing = NULL;
    int32_t result = 0, i, count;
    if (!t || !desc || !desc->component || !desc->function || !desc->source) {
        ecs_err("box_texture_node_register: invalid node kind description");
        goto done;
    }

    count = ecs_vec_count(&t->node_kinds);
    for (i = 0; i < count; i ++) {
        box_texture_node_kind_t *k = *ecs_vec_get_t(&t->node_kinds, box_texture_node_kind_t*, i);
        if (k->component == desc->component) {
            existing = k;
        }
    }

    kind = ecs_os_calloc_t(box_texture_node_kind_t);
    if (box_texture_node_kind_fill(kind, desc)) {
        char *name = ecs_get_path(world, desc->component);
        ecs_err("box_texture_node_register: node kind '%s' has a parameter of an invalid size, "
            "more than %d parameter bytes, more than %d passes, an invalid array or an invalid "
            "window size", name, BOX_TEXTURE_NODE_PARAM_BYTES, BOX_TEXTURE_NODE_PASSES);
        ecs_os_free(name);
        box_texture_node_kind_strings_free(kind);
        ecs_os_free(kind);
        goto done;
    }

    if (existing) {
        kind->id = existing->id;
        box_texture_node_kind_strings_free(existing);
        *existing = *kind;
        ecs_os_free(kind);
        kind = existing;
    } else {
        kind->id = count + 1;
        *ecs_vec_append_t(NULL, &t->node_kinds, box_texture_node_kind_t*) = kind;
        ecs_observer(world, {
            .query.terms = {{ .id = desc->component }},
            .events = { EcsOnSet, EcsOnRemove },
            .callback = BoxTextureNodeChanged
        });
    }

    kind->sent = false;
    result = kind->id;
    box_texture_node_kind_reflect(world, kind);
    box_texture_graphs_touch(t, 0);
done:
    return result;
}

/** Fail a build with an error that names the graph.
 *
 * @param b The build
 * @param fmt Format of the problem, after the name of the graph
 * @param ... Arguments of the format
 */
static void box_texture_graph_error(
    box_texture_graph_build_t *b,
    const char *fmt,
    ...)
{
    va_list args;
    if (b->error) {
        goto done;
    }

    va_start(args, fmt);
    char *problem = flecs_vasprintf(fmt, args);
    va_end(args);
    b->error = flecs_asprintf("texture graph '%s': %s", b->name, problem);
    ecs_os_free(problem);
done:
    return;
}

/** Get the path of an entity for an error, its id when it is not alive.
 *
 * @param world The world or a stage
 * @param e The entity
 * @return The path, freed by the caller
 */
static char* box_texture_graph_name(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    char *result = ecs_is_alive(world, e)
        ? ecs_get_path(world, e)
        : flecs_asprintf("#%u", (uint32_t)e);

    return result;
}

/** Find the node kind of an entity: the kind of the one node component it has.
 *
 * @param b The build
 * @param e The entity
 * @param count Receives the number of node components it has
 * @return The kind, NULL without one
 */
static box_texture_node_kind_t* box_texture_graph_kind_of(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    int32_t *count)
{
    box_texture_node_kind_t *result = NULL;
    int32_t i, kinds = ecs_vec_count(&b->t->node_kinds);
    *count = 0;
    for (i = 0; i < kinds; i ++) {
        box_texture_node_kind_t *kind = *ecs_vec_get_t(
            &b->t->node_kinds, box_texture_node_kind_t*, i);
        if (ecs_has_id(b->world, e, kind->component)) {
            result = result ? result : kind;
            (*count) ++;
        }
    }

    return result;
}

/** Record an entity the walk reached, once.
 *
 * @param b The build
 * @param e The entity
 */
static void box_texture_graph_reach(
    box_texture_graph_build_t *b,
    ecs_entity_t e)
{
    int32_t i, count = ecs_vec_count(b->reached);
    const ecs_entity_t *reached = ecs_vec_first_t(b->reached, ecs_entity_t);
    bool found = false;
    for (i = 0; i < count; i ++) {
        found = found || reached[i] == e;
    }

    if (!found) {
        *ecs_vec_append_t(NULL, b->reached, ecs_entity_t) = e;
    }
}

/** Write the array parameter of a node: its header and its elements after the constants of
 * its inputs, failing the build with more elements than the kind takes or invalid ones.
 *
 * @param b The build
 * @param e The node entity
 * @param kind Its kind, with an array parameter
 * @param ptr Its node component
 * @param params Its parameter bytes, zeroed
 * @param offset Offset of its parameter bytes in the graph parameters
 */
static void box_texture_graph_array(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    const box_texture_node_kind_t *kind,
    const uint8_t *ptr,
    uint8_t *params,
    int32_t offset)
{
    const ecs_vec_t *vec = (const ecs_vec_t*)(ptr + kind->array.offset);
    int32_t i, count = ecs_vec_count(vec);
    int32_t first = kind->param_size + kind->input_count * 16;
    uint32_t *header = (uint32_t*)(params + kind->array_header);
    uint32_t *elements = (uint32_t*)(params + first);
    char *problem = NULL;
    if (count > kind->array.max) {
        char *name = box_texture_graph_name(b->world, e);
        box_texture_graph_error(b, "node '%s' has %d elements in %s, more than %d", name,
            count, kind->array.name, kind->array.max);
        ecs_os_free(name);
        goto done;
    }

    header[0] = (uint32_t)((offset + first) / 16);
    header[1] = (uint32_t)count;
    if (kind->array.pack) {
        problem = kind->array.pack(ptr, count, elements);
    } else {
        const uint8_t *data = ecs_vec_first(vec);
        for (i = 0; i < count; i ++) {
            ecs_os_memcpy(&elements[i * 4], data + i * kind->array.size, kind->array.size);
        }
    }

    if (problem) {
        char *name = box_texture_graph_name(b->world, e);
        box_texture_graph_error(b, "node '%s': %s", name, problem);
        ecs_os_free(name);
        ecs_os_free(problem);
    }

done:
    return;
}

TextureValueType box_texture_graph_follow(
    const TextureValueType *inputs,
    uint32_t used)
{
    TextureValueType result = TexVector;
    bool scalar = false, vector = false;
    int32_t i;
    for (i = 0; i < BOX_TEXTURE_NODE_INPUTS; i ++) {
        if (used & (1u << i)) {
            scalar = scalar || inputs[i] == TexScalar;
            vector = vector || inputs[i] == TexVector;
        }
    }

    if (scalar && !vector) {
        result = TexScalar;
    }

    return result;
}

/** Get the type of the value of a node.
 *
 * @param kind Its kind
 * @param ptr Its component
 * @param inputs Type of the node of each input, TexAny for none
 * @return The type, TexScalar or TexVector
 */
static TextureValueType box_texture_graph_output_type(
    const box_texture_node_kind_t *kind,
    const void *ptr,
    const TextureValueType *inputs)
{
    TextureValueType result = kind->output;
    if (result == TexAny && kind->output_type) {
        result = kind->output_type(ptr, inputs);
    }

    if (result == TexAny) {
        result = box_texture_graph_follow(inputs, (1u << kind->input_count) - 1);
    }

    return result;
}

/** Append the compiled node of an entity whose inputs are compiled, with its parameters and
 * the constants of its inputs without a node.
 *
 * @param b The build
 * @param e The node entity
 * @param kind Its kind
 * @param inputs Index of the node of each input, -1 for none
 * @param texture Index of the texture it samples, -1 for none
 * @return Index of the node
 */
static int32_t box_texture_graph_append(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    const box_texture_node_kind_t *kind,
    const int32_t *inputs,
    int32_t texture)
{
    const uint8_t *ptr = ecs_get_id(b->world, e, kind->component);
    int32_t i, offset = ecs_vec_count(&b->params);
    int32_t result = ecs_vec_count(&b->nodes);
    TextureValueType types[BOX_TEXTURE_NODE_INPUTS] = { TexAny, TexAny, TexAny, TexAny };
    uint32_t word = 0;
    for (i = 0; i < kind->input_count; i ++) {
        if (inputs[i] >= 0) {
            types[i] = ecs_vec_get_t(&b->nodes, box_texture_graph_node_t, inputs[i])->output;
            word |= types[i] == TexScalar ? (1u << i) : (1u << (4 + i));
        }
    }

    uint8_t *params = ecs_vec_grow_t(NULL, &b->params, uint8_t, kind->node_size);
    ecs_os_memset(params, 0, kind->node_size);
    for (i = 0; i < kind->param_count; i ++) {
        if (kind->params[i].kind == BOX_TEXTURE_PARAM_BOOL) {
            uint32_t flag = *(const bool*)(ptr + kind->params[i].offset) ? 1u : 0u;
            ecs_os_memcpy(params + kind->param_offsets[i], &flag, ECS_SIZEOF(uint32_t));
        } else {
            ecs_os_memcpy(params + kind->param_offsets[i], ptr + kind->params[i].offset,
                kind->params[i].size);
        }
    }

    for (i = 0; i < kind->input_count; i ++) {
        const box_texture_node_input_t *in = &kind->inputs[i];
        if (inputs[i] < 0 && in->value) {
            ecs_os_memcpy(params + kind->param_size + i * 16, ptr + in->value_offset,
                in->type == TexScalar ? ECS_SIZEOF(float) : 4 * ECS_SIZEOF(float));
        }
    }

    if (kind->types_offset >= 0) {
        ecs_os_memcpy(params + kind->types_offset, &word, ECS_SIZEOF(uint32_t));
    }

    if (kind->array.name) {
        box_texture_graph_array(b, e, kind, ptr, params, offset);
    }

    int32_t window = -1, reach = 0;
    if (b->domain == DomainWorld && kind->window) {
        window = ecs_vec_count(&b->window);
        uint8_t *elements = ecs_vec_grow_t(NULL, &b->window, uint8_t, kind->window_size * 16);
        ecs_os_memset(elements, 0, kind->window_size * 16);
        kind->window(ptr, b->origin, (uint32_t*)elements);
    }

    if (b->domain == DomainWorld && (kind->flags & BOX_TEXTURE_NODE_NEIGHBOURHOOD)) {
        float texel = glm_min(b->spacing[0], b->spacing[1]);
        float texels = kind->reach ? kind->reach(ptr, texel) : (float)kind->passes;
        if (!(texels >= 0)) {
            char *name = box_texture_graph_name(b->world, e);
            char *component = ecs_get_path(b->world, kind->component);
            box_texture_graph_error(b, "node '%s' (%s) does not work on DomainWorld", name,
                component);
            ecs_os_free(component);
            ecs_os_free(name);
        }

        reach = texels >= 0 ? (int32_t)ceilf(glm_min(texels, 65536.0f)) : 0;
    }

    box_texture_graph_node_t *node = ecs_vec_append_t(NULL, &b->nodes,
        box_texture_graph_node_t);
    ecs_os_memset_t(node, 0, box_texture_graph_node_t);
    node->entity = e;
    node->window = window;
    node->reach = reach;
    node->kind = kind->id;
    node->output = box_texture_graph_output_type(kind, ptr, types);
    node->params = offset;
    node->param_size = kind->node_size;
    node->texture = texture;
    for (i = 0; i < BOX_TEXTURE_NODE_INPUTS; i ++) {
        node->inputs[i] = i < kind->input_count ? inputs[i] : -1;
    }

    *ecs_map_ensure(&b->index, e) = (ecs_map_val_t)(result + 1);

    return result;
}

/** Check that a node outputs the type an input expects.
 *
 * @param b The build
 * @param e The node entity
 * @param output Type of the node
 * @param from Node of the input, 0 for the output of the graph
 * @param input Name of the input
 * @param expected Type the input expects, TexAny for either
 */
static void box_texture_graph_check_type(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    TextureValueType output,
    ecs_entity_t from,
    const char *input,
    TextureValueType expected)
{
    if (from && expected != TexAny && output != expected) {
        char *name = box_texture_graph_name(b->world, e);
        char *from_name = box_texture_graph_name(b->world, from);
        box_texture_graph_error(b, "node '%s' outputs %s but input '%s' of node '%s' expects "
            "%s", name, output == TexScalar ? "Scalar" : "Vector", input, from_name,
            expected == TexScalar ? "Scalar" : "Vector");
        ecs_os_free(from_name);
        ecs_os_free(name);
    }
}

static bool box_texture_graph_node_samples_back(
    box_texture_graph_build_t *b,
    ecs_entity_t node,
    ecs_map_t *textures,
    ecs_map_t *nodes);

/** Check whether the TextureGraph of a texture samples the graph of a build, through the
 * textures its nodes sample and their graphs in turn.
 *
 * @param b The build
 * @param texture The texture entity
 * @param textures Texture entities visited
 * @param nodes Node entities visited
 * @return Whether it samples the graph
 */
static bool box_texture_graph_samples_back(
    box_texture_graph_build_t *b,
    ecs_entity_t texture,
    ecs_map_t *textures,
    ecs_map_t *nodes)
{
    const TextureGraph *graph = NULL;
    bool result = false;
    if (!ecs_is_alive(b->world, texture) || ecs_map_get(textures, texture)) {
        goto done;
    }

    ecs_map_insert(textures, texture, 1);
    graph = ecs_get(b->world, texture, TextureGraph);
    if (graph && graph->output) {
        result = box_texture_graph_node_samples_back(b, graph->output, textures, nodes);
    }

done:
    return result;
}

/** Check whether a node or a node before it samples the graph of a build, directly or
 * through the graph of a texture it samples.
 *
 * @param b The build
 * @param node The node entity
 * @param textures Texture entities visited
 * @param nodes Node entities visited
 * @return Whether it samples the graph
 */
static bool box_texture_graph_node_samples_back(
    box_texture_graph_build_t *b,
    ecs_entity_t node,
    ecs_map_t *textures,
    ecs_map_t *nodes)
{
    const box_texture_node_kind_t *kind;
    const uint8_t *ptr;
    bool result = false;
    int32_t i, kinds;
    if (!ecs_is_alive(b->world, node) || ecs_map_get(nodes, node)) {
        goto done;
    }

    ecs_map_insert(nodes, node, 1);
    kind = box_texture_graph_kind_of(b, node, &kinds);
    if (!kind) {
        goto done;
    }

    ptr = ecs_get_id(b->world, node, kind->component);
    if (kind->texture) {
        ecs_entity_t texture = *(const ecs_entity_t*)(ptr + kind->texture_offset);
        result = texture == b->graph ||
            (texture && box_texture_graph_samples_back(b, texture, textures, nodes));
    }

    for (i = 0; !result && i < kind->input_count; i ++) {
        ecs_entity_t in = *(const ecs_entity_t*)(ptr + kind->inputs[i].offset);
        result = in && box_texture_graph_node_samples_back(b, in, textures, nodes);
    }

done:
    return result;
}

/** Check whether an entity has an image to sample: a TexturePath, a TextureGen, a TextureGraph,
 * an image made by the engine, or it is the texture of a Surface.
 *
 * @param b The build
 * @param e The entity
 * @return Whether it is a texture
 */
static bool box_texture_graph_is_texture(
    box_texture_graph_build_t *b,
    ecs_entity_t e)
{
    bool result = ecs_has(b->world, e, TexturePath) || ecs_has(b->world, e, TextureGen) ||
        ecs_has(b->world, e, TextureGraph) || ecs_has(b->world, e, TexturePixels);
    int32_t i;
    ecs_iter_t it = ecs_query_iter(b->world, b->t->surfaces);
    while (!result && ecs_query_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (i = 0; i < it.count; i ++) {
            result = result || s[i].texture == e;
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Check the texture a node of a sampling kind samples and give it the next texture of the
 * graph: it fails the build when it is missing, part of a cycle of graphs or failed, waits a
 * frame for an entity without a texture component and holds the graph while it has no image.
 *
 * @param b The build
 * @param e The node entity
 * @param kind Its kind, BOX_TEXTURE_NODE_SAMPLES
 * @return Index of the texture, -1 when the build failed or waits
 */
static int32_t box_texture_graph_sample(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    const box_texture_node_kind_t *kind)
{
    const uint8_t *ptr = ecs_get_id(b->world, e, kind->component);
    ecs_entity_t texture = *(const ecs_entity_t*)(ptr + kind->texture_offset);
    bool linear = kind->linear && *(const bool*)(ptr + kind->linear_offset);
    char *name = ecs_get_path(b->world, e), *texture_name = NULL;
    const TextureStatus *status;
    int32_t result = -1, width, height;
    ecs_map_t textures, nodes;
    bool cycle;
    if (!texture) {
        box_texture_graph_error(b, "node '%s' samples no texture", name);
        goto done;
    }

    box_texture_graph_reach(b, texture);
    if (!ecs_is_alive(b->world, texture)) {
        box_texture_graph_error(b, "node '%s' samples a missing texture #%u", name,
            (uint32_t)texture);
        goto done;
    }

    texture_name = ecs_get_path(b->world, texture);
    ecs_map_init(&textures, NULL);
    ecs_map_init(&nodes, NULL);
    cycle = texture == b->graph ||
        box_texture_graph_samples_back(b, texture, &textures, &nodes);
    ecs_map_fini(&textures);
    ecs_map_fini(&nodes);
    if (cycle) {
        box_texture_graph_error(b, "node '%s' samples texture '%s', which samples this graph: "
            "a cycle of graphs", name, texture_name);
        goto done;
    }

    if (box_surface_texture_size(b->world, texture, &width, &height)) {
        goto bind;
    }

    if (!box_texture_graph_is_texture(b, texture)) {
        if (b->wait) {
            b->waiting = true;
        } else {
            box_texture_graph_error(b, "node '%s' samples '%s', which is not a texture", name,
                texture_name);
        }

        goto done;
    }

    status = ecs_get(b->world, texture, TextureStatus);
    if (status && status->state == TextureFailed) {
        box_texture_graph_error(b, "node '%s' samples texture '%s', which failed: %s", name,
            texture_name, status->error ? status->error : "no image");
        goto done;
    }

    if (!status || (status->state != TextureReady && !status->revision)) {
        b->held = true;
    }

bind:
    result = ecs_vec_count(&b->textures);
    *ecs_vec_append_t(NULL, &b->textures, box_texture_graph_texture_t) =
        (box_texture_graph_texture_t){ .entity = texture, .linear = linear };
done:
    ecs_os_free(texture_name);
    ecs_os_free(name);

    return result;
}

/** Visit a node, compiling its inputs before it.
 *
 * @param b The build
 * @param e The node entity
 * @param from Node whose input it is, 0 for the output of the graph
 * @param input Name of that input
 * @param expected Type that input expects, TexAny for either
 * @return Index of the node, -1 when the build failed or waits
 */
static int32_t box_texture_graph_visit(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    ecs_entity_t from,
    const char *input,
    TextureValueType expected)
{
    int32_t result = -1, i, kinds = 0, texture = -1;
    int32_t inputs[BOX_TEXTURE_NODE_INPUTS] = { -1, -1, -1, -1 };
    char *name = NULL;
    box_texture_node_kind_t *kind = NULL;
    const ecs_map_val_t *visited = NULL;
    box_texture_graph_reach(b, e);
    if (!ecs_is_alive(b->world, e)) {
        char *from_name = from ? box_texture_graph_name(b->world, from) : NULL;
        if (from_name) {
            box_texture_graph_error(b, "input '%s' of node '%s' is a missing node #%u", input,
                from_name, (uint32_t)e);
        } else {
            box_texture_graph_error(b, "the output is a missing node #%u", (uint32_t)e);
        }

        ecs_os_free(from_name);
        goto done;
    }

    name = ecs_get_path(b->world, e);
    visited = ecs_map_get(&b->index, e);
    if (visited && !*visited) {
        box_texture_graph_error(b, "node '%s' is part of a cycle", name);
        goto done;
    }

    kind = box_texture_graph_kind_of(b, e, &kinds);
    if (visited) {
        result = (int32_t)*visited - 1;
        box_texture_graph_check_type(b, e,
            ecs_vec_get_t(&b->nodes, box_texture_graph_node_t, result)->output, from, input,
            expected);
        goto done;
    }

    if (kinds > 1) {
        box_texture_graph_error(b, "node '%s' has more than one node component", name);
        goto done;
    }

    if (!kind && b->wait) {
        b->waiting = true;
        goto done;
    }

    if (!kind) {
        box_texture_graph_error(b, "node '%s' has no node component", name);
        goto done;
    }

    if ((kind->flags & BOX_TEXTURE_NODE_PLANE) && b->domain != DomainPlane) {
        char *component = ecs_get_path(b->world, kind->component);
        box_texture_graph_error(b, "node '%s' (%s) works on DomainPlane only, the graph is on "
            "%s", name, component, b->domain == DomainWorld ? "DomainWorld" : "DomainSphere");
        ecs_os_free(component);
    }

    if (ecs_vec_count(&b->nodes) >= BOX_TEXTURE_GRAPH_NODES) {
        box_texture_graph_error(b, "more than %d nodes", BOX_TEXTURE_GRAPH_NODES);
    }

    if (b->error) {
        goto done;
    }

    *ecs_map_ensure(&b->index, e) = 0;
    for (i = 0; i < kind->input_count && !b->error && !b->waiting; i ++) {
        const uint8_t *ptr = ecs_get_id(b->world, e, kind->component);
        ecs_entity_t in = *(const ecs_entity_t*)(ptr + kind->inputs[i].offset);
        if (in) {
            inputs[i] = box_texture_graph_visit(b, in, e, kind->inputs[i].name,
                kind->inputs[i].type);
        }
    }

    if (!b->error && !b->waiting && (kind->flags & BOX_TEXTURE_NODE_SAMPLES)) {
        texture = box_texture_graph_sample(b, e, kind);
    }

    if (!b->error && !b->waiting) {
        result = box_texture_graph_append(b, e, kind, inputs, texture);
        box_texture_graph_check_type(b, e,
            ecs_vec_get_t(&b->nodes, box_texture_graph_node_t, result)->output, from, input,
            expected);
    }

done:
    ecs_os_free(name);

    return result;
}

/** Resolve the longitude and latitude range of a graph on the sphere: an empty range is the
 * whole of it, a longitude range is at most a turn and the latitudes are clamped to the poles.
 *
 * @param graph The TextureGraph
 * @param out Receives the longitude start and end, the latitude start and end in radians
 */
static void box_texture_graph_region(
    const TextureGraph *graph,
    float out[4])
{
    const float turn = 2.0f * (float)GLM_PI;
    const float pole = 0.5f * (float)GLM_PI;
    float lon0 = graph->longitude_start, lon1 = graph->longitude_end;
    float lat0 = glm_clamp(graph->latitude_start, -pole, pole);
    float lat1 = glm_clamp(graph->latitude_end, -pole, pole);
    if (!isfinite(lon0) || !isfinite(lon1) || lon0 == lon1) {
        lon0 = 0;
        lon1 = turn;
    } else if (fabsf(lon1 - lon0) > turn) {
        lon1 = lon0 + (lon1 > lon0 ? turn : -turn);
    }

    if (!isfinite(lat0) || !isfinite(lat1) || lat0 == lat1) {
        lat0 = -pole;
        lat1 = pole;
    }

    out[0] = lon0;
    out[1] = lon1;
    out[2] = lat0;
    out[3] = lat1;
}

/** Resolve the window of a graph on DomainWorld: its origin and the meters between its texels.
 *
 * @param graph The TextureGraph
 * @param width Width of the image, capped
 * @param height Height of the image, capped
 * @param origin Receives the world x and z of the first texel
 * @param spacing Receives the meters between neighbouring texels along x and z
 */
static void box_texture_graph_window(
    const TextureGraph *graph,
    int32_t width,
    int32_t height,
    double origin[2],
    float spacing[2])
{
    const TextureGraphWindow *w = &graph->window;
    float sx = 1, sz;
    origin[0] = isfinite(w->x) ? w->x : 0;
    origin[1] = isfinite(w->z) ? w->z : 0;
    if (isfinite(w->width) && w->width > 0) {
        sx = w->width / (float)(width > 1 ? width - 1 : 1);
    }

    sz = sx;
    if (isfinite(w->depth) && w->depth > 0) {
        sz = w->depth / (float)(height > 1 ? height - 1 : 1);
    }

    spacing[0] = sx;
    spacing[1] = sz;
}

/** Get the texels the window of a graph on DomainWorld is grown by on each side: the largest sum
 * of the reaches of the neighbourhood nodes along a path from the output, so every node reads
 * its inputs where they were evaluated from the window itself.
 *
 * @param b The build, its nodes in topological order
 * @return The texels
 */
static int32_t box_texture_graph_apron(
    box_texture_graph_build_t *b)
{
    int32_t n = ecs_vec_count(&b->nodes), i, j, result = 0;
    const box_texture_graph_node_t *nodes = ecs_vec_first(&b->nodes);
    int32_t *sum = ecs_os_calloc_n(int32_t, n > 0 ? n : 1);
    for (i = 0; i < n; i ++) {
        int32_t deepest = 0;
        for (j = 0; j < BOX_TEXTURE_NODE_INPUTS; j ++) {
            int32_t in = nodes[i].inputs[j];
            if (in >= 0 && sum[in] > deepest) {
                deepest = sum[in];
            }
        }

        sum[i] = nodes[i].reach + deepest;
        if (sum[i] > BOX_TEXTURE_GRAPH_APRON) {
            sum[i] = BOX_TEXTURE_GRAPH_APRON;
        }
    }

    result = n > 0 ? sum[n - 1] : 0;
    ecs_os_free(sum);

    return result;
}

/** Compile the walk of a build into a graph with its hashes.
 *
 * @param b The build
 * @param graph The TextureGraph
 * @return The compiled graph, owned by the caller
 */
static box_texture_graph_t* box_texture_graph_compile(
    box_texture_graph_build_t *b,
    const TextureGraph *graph)
{
    box_texture_graph_t *result = ecs_os_calloc_t(box_texture_graph_t);
    int32_t i, node_bytes = ecs_vec_count(&b->params);
    box_texture_capped_size(b->world, b->t, graph->width, graph->height, &result->width,
        &result->height);
    result->domain = graph->domain;
    if (graph->domain == DomainSphere) {
        box_texture_graph_region(graph, result->region);
    }

    if (graph->domain == DomainWorld) {
        result->origin[0] = b->origin[0];
        result->origin[1] = b->origin[1];
        result->spacing[0] = b->spacing[0];
        result->spacing[1] = b->spacing[1];
        result->apron = box_texture_graph_apron(b);
        if (ecs_vec_count(&b->window)) {
            ecs_os_memcpy(ecs_vec_grow_t(NULL, &b->params, uint8_t,
                ecs_vec_count(&b->window)), ecs_vec_first(&b->window),
                ecs_vec_count(&b->window));
        }
    }

    for (i = 0; i < ecs_vec_count(&b->nodes); i ++) {
        box_texture_graph_node_t *node = ecs_vec_get_t(&b->nodes, box_texture_graph_node_t, i);
        node->window = node->window >= 0 ? node_bytes + node->window : 0;
    }

    result->format = graph->format;
    result->rate = graph->rate > 0 ? graph->rate : 0;
    result->interpolate = graph->interpolate;
    result->node_count = ecs_vec_count(&b->nodes);
    result->nodes = ecs_os_memdup(ecs_vec_first(&b->nodes),
        result->node_count * ECS_SIZEOF(box_texture_graph_node_t));
    result->param_size = ecs_vec_count(&b->params);
    result->params = result->param_size
        ? ecs_os_memdup(ecs_vec_first(&b->params), result->param_size)
        : NULL;
    result->texture_count = ecs_vec_count(&b->textures);
    result->textures = result->texture_count
        ? ecs_os_memdup(ecs_vec_first(&b->textures),
            result->texture_count * ECS_SIZEOF(box_texture_graph_texture_t))
        : NULL;
    result->structure_hash = box_texture_graph_hash(BOX_TEXTURE_GRAPH_FNV_BASIS,
        &result->node_count, ECS_SIZEOF(int32_t));
    for (i = 0; i < result->node_count; i ++) {
        const box_texture_graph_node_t *node = &result->nodes[i];
        result->structure_hash = box_texture_graph_hash(result->structure_hash, &node->kind,
            ECS_SIZEOF(int32_t));
        result->structure_hash = box_texture_graph_hash(result->structure_hash, &node->output,
            ECS_SIZEOF(node->output));
        result->structure_hash = box_texture_graph_hash(result->structure_hash, node->inputs,
            ECS_SIZEOF(node->inputs));
        result->structure_hash = box_texture_graph_hash(result->structure_hash, &node->texture,
            ECS_SIZEOF(int32_t));
        result->structure_hash = box_texture_graph_hash(result->structure_hash, &node->output,
            ECS_SIZEOF(TextureValueType));
        result->structure_hash = box_texture_graph_hash(result->structure_hash, &node->window,
            ECS_SIZEOF(int32_t));
    }

    result->param_hash = box_texture_graph_hash(BOX_TEXTURE_GRAPH_FNV_BASIS, result->params,
        result->param_size);

    return result;
}

/** Check whether two compiled graphs make the same image.
 *
 * @param a The first graph
 * @param b The second graph
 * @return Whether their size, domain, range, format, rate, interpolation, structure,
 * parameters and sampled textures are the same
 */
static bool box_texture_graph_same(
    const box_texture_graph_t *a,
    const box_texture_graph_t *b)
{
    bool result = a->width == b->width && a->height == b->height && a->domain == b->domain &&
        !ecs_os_memcmp(a->region, b->region, ECS_SIZEOF(a->region)) &&
        a->origin[0] == b->origin[0] && a->origin[1] == b->origin[1] &&
        a->spacing[0] == b->spacing[0] && a->spacing[1] == b->spacing[1] &&
        a->apron == b->apron &&
        a->format == b->format && a->rate == b->rate &&
        a->interpolate == b->interpolate && a->structure_hash == b->structure_hash &&
        a->param_hash == b->param_hash && a->node_count == b->node_count &&
        a->param_size == b->param_size && a->texture_count == b->texture_count &&
        (!a->param_size || !ecs_os_memcmp(a->params, b->params, a->param_size));
    int32_t i;
    for (i = 0; result && i < a->texture_count; i ++) {
        result = a->textures[i].entity == b->textures[i].entity &&
            a->textures[i].linear == b->textures[i].linear;
    }

    return result;
}

/** Check whether the backend of a world evaluates the graphs and reports their outcome, so
 * a graph is ready once its image was written: the GPU backend, unless its device failed.
 *
 * @param world The world or a stage
 * @return Whether Texture and TextureStatus wait for the outcome of the backend
 */
static bool box_texture_graph_deferred(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;

    return box_render_gpu_stats(world, &stats) && !stats.failed;
}

/** Take a compiled graph into the entry of its texture and queue it for the backend, unless
 * it makes the image the backend already has.
 *
 * With a backend that evaluates the graphs the texture stays loading until the outcome of its
 * evaluation arrives from the render extract, else it is ready at once.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 * @param graph The compiled graph, takes ownership
 */
static void box_texture_graph_accept(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    box_texture_graph_t *graph)
{
    box_texture_t *tex = box_texture_entry(t, e);
    int32_t revision = box_texture_revision(world, e);
    bool deferred = box_texture_graph_deferred(world);
    if (tex->graph && box_texture_graph_same(tex->graph, graph)) {
        box_texture_graph_free(graph);
        box_texture_status(world, e, tex->graph_pending ? TextureLoading : TextureReady,
            revision, NULL);
        goto done;
    }

    box_texture_graph_free(tex->graph);
    tex->graph = graph;
    tex->loaded = true;
    tex->width = graph->width;
    tex->height = graph->height;
    tex->single_channel = graph->format == GraphSingle;
    tex->graph_serial ++;
    tex->graph_pending = deferred;
    if (!tex->graph_queued) {
        tex->graph_queued = true;
        *ecs_vec_append_t(NULL, &t->graph_queued, ecs_entity_t) = e;
    }

    if (deferred) {
        box_texture_status(world, e, TextureLoading, revision, NULL);
        goto done;
    }

    ecs_set(world, e, Texture, { .width = graph->width, .height = graph->height });
    box_texture_status(world, e, TextureReady, revision + 1, NULL);
done:
    return;
}

/** Build the TextureGraph of a texture: walk it from its output, check it and compile it.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 * @param wait Wait a frame for a node without a node component instead of failing
 */
static void box_texture_graph_build(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    bool wait)
{
    const TextureGraph *graph = ecs_get(world, e, TextureGraph);
    box_texture_t *tex = box_texture_entry(t, e);
    box_texture_graph_build_t b = {
        .world = world, .t = t, .graph = e, .reached = &tex->reached, .wait = wait,
        .domain = graph->domain
    };
    tex->graph_dirty = false;
    tex->graph_waiting = false;
    tex->graph_held = false;
    if (ecs_has(world, e, TexturePath) || ecs_has(world, e, TextureGen)) {
        box_texture_load(world, t, e);
        goto done;
    }

    if (graph->domain == DomainWorld) {
        int32_t width, height;
        box_texture_capped_size(world, t, graph->width, graph->height, &width, &height);
        box_texture_graph_window(graph, width, height, b.origin, b.spacing);
    }

    b.name = ecs_get_path(world, e);
    ecs_map_init(&b.index, NULL);
    ecs_vec_init_t(NULL, &b.window, uint8_t, 0);
    ecs_vec_init_t(NULL, &b.nodes, box_texture_graph_node_t, 0);
    ecs_vec_init_t(NULL, &b.params, uint8_t, 0);
    ecs_vec_init_t(NULL, &b.textures, box_texture_graph_texture_t, 0);
    ecs_vec_clear(&tex->reached);
    if (!graph->output) {
        box_texture_graph_error(&b, "no output node");
    } else {
        box_texture_graph_visit(&b, graph->output, 0, NULL, TexVector);
    }

    if (b.error) {
        box_texture_fail(world, t, e, b.error);
    } else if (b.waiting) {
        tex->graph_waiting = true;
        box_texture_status(world, e, TextureLoading, box_texture_revision(world, e), NULL);
    } else if (b.held) {
        tex->graph_held = true;
        box_texture_status(world, e, TextureLoading, box_texture_revision(world, e), NULL);
    } else {
        box_texture_graph_accept(world, t, e, box_texture_graph_compile(&b, graph));
    }

    ecs_os_free(b.error);
    ecs_os_free(b.name);
    ecs_map_fini(&b.index);
    ecs_vec_fini_t(NULL, &b.nodes, box_texture_graph_node_t);
    ecs_vec_fini_t(NULL, &b.params, uint8_t);
    ecs_vec_fini_t(NULL, &b.textures, box_texture_graph_texture_t);
    ecs_vec_fini_t(NULL, &b.window, uint8_t);
done:
    return;
}

/** Load the textures whose source component was removed, then build the graphs that changed
 * or wait for a sampled texture to get an image, and fail the ones that waited a frame for a
 * node without a node component.
 *
 * @param it The iterator of the system
 */
static void BoxTextureGraphBuild(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->stage;
    box_textures_t *t = box_textures_get(world);
    ecs_vec_t build;
    int32_t i, count;
    if (!t) {
        goto done;
    }

    ecs_vec_init_t(NULL, &build, ecs_entity_t, 0);
    count = ecs_vec_count(&t->reload);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &build, ecs_entity_t) =
            *ecs_vec_get_t(&t->reload, ecs_entity_t, i);
    }

    ecs_vec_clear(&t->reload);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&build, ecs_entity_t, i);
        if (ecs_is_alive(world, e)) {
            box_texture_load(world, t, e);
        }
    }

    ecs_vec_clear(&build);
    ecs_map_iter_t mit = ecs_map_iter(&t->textures);
    while (ecs_map_next(&mit)) {
        box_texture_t *tex = ecs_map_ptr(&mit);
        if (tex->graph_dirty || tex->graph_waiting || tex->graph_held) {
            *ecs_vec_append_t(NULL, &build, ecs_entity_t) = tex->entity;
        }
    }

    count = ecs_vec_count(&build);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&build, ecs_entity_t, i);
        box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, e);
        if (!tex) {
            continue;
        }

        bool wait = !tex->graph_waiting;
        if (!ecs_is_alive(world, e) || !ecs_has(world, e, TextureGraph)) {
            tex->graph_dirty = false;
            tex->graph_waiting = false;
            tex->graph_held = false;
            continue;
        }

        box_texture_graph_build(world, t, e, wait);
    }

    ecs_vec_fini_t(NULL, &build, ecs_entity_t);
done:
    return;
}

void box_texture_graphs_init(
    ecs_world_t *world,
    box_textures_t *t)
{
    ecs_vec_init_t(NULL, &t->node_kinds, box_texture_node_kind_t*, 0);
    ecs_vec_init_t(NULL, &t->graph_queued, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &t->graph_released, uint32_t, 0);
    ecs_vec_init_t(NULL, &t->reload, ecs_entity_t, 0);
    box_texture_nodes_register(world);
    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "Build",
            .parent = ecs_lookup_child(world, ecs_get_scope(world), "graph")
        }),
        .phase = EcsOnStore,
        .run = BoxTextureGraphBuild
    });
}
