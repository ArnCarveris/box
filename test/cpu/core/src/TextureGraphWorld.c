/**
 * @file TextureGraphWorld.c
 * @brief Tests for TextureGraph on DomainWorld built on the main thread: the window of a graph,
 * the window elements of its nodes computed in double precision, the apron of its neighbourhood
 * nodes, the kinds that fail on it, the float formats and what the null backend receives.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "texture.h"
#include "render.h"

/** Create a headless world.
 *
 * @return The world
 */
static ecs_world_t* tw_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    return world;
}

/** Get the registry entry of a texture entity.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The entry
 */
static const box_texture_t* tw_entry(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    box_textures_t *t = box_textures_get(world);
    test_assert(t != NULL);
    box_texture_t *entry = ecs_map_get_deref(&t->textures, box_texture_t, tex);
    test_assert(entry != NULL);

    return entry;
}

/** Get the compiled graph of a texture entity after a frame.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The compiled graph
 */
static const box_texture_graph_t* tw_graph(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    ecs_progress(world, 0);
    const box_texture_graph_t *graph = tw_entry(world, tex)->graph;
    if (!graph) {
        const TextureStatus *status = ecs_get(world, tex, TextureStatus);
        printf("error: %s\n", status && status->error ? status->error : "(none)");
    }

    test_assert(graph != NULL);

    return graph;
}

/** Check that a texture failed after a frame with an error that contains two strings.
 *
 * @param world The world
 * @param tex The texture entity
 * @param a The first string
 * @param b The second string
 */
static void tw_failed(
    ecs_world_t *world,
    ecs_entity_t tex,
    const char *a,
    const char *b)
{
    ecs_progress(world, 0);
    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_assert(status != NULL);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    if (!strstr(status->error, a) || !strstr(status->error, b)) {
        printf("error: %s\n", status->error);
    }

    test_assert(strstr(status->error, a) != NULL);
    test_assert(strstr(status->error, b) != NULL);
}

/** Get the node of a compiled graph whose entity is a node entity.
 *
 * @param graph The compiled graph
 * @param e The node entity
 * @return The node
 */
static const box_texture_graph_node_t* tw_node(
    const box_texture_graph_t *graph,
    ecs_entity_t e)
{
    int32_t i;
    for (i = 0; i < graph->node_count; i ++) {
        if (graph->nodes[i].entity == e) {
            return &graph->nodes[i];
        }
    }

    test_assert(false);

    return NULL;
}

/** Get the internal kind of a node component.
 *
 * @param world The world
 * @param component The component
 * @return The kind
 */
static const box_texture_node_kind_t* tw_kind(
    ecs_world_t *world,
    ecs_entity_t component)
{
    const TextureNodeKind *reflected = ecs_get(world, component, TextureNodeKind);
    test_assert(reflected != NULL);
    const box_texture_node_kind_t *kind = box_texture_node_kind(box_textures_get(world),
        reflected->id);
    test_assert(kind != NULL);

    return kind;
}

/** Get a word of the window elements of a node.
 *
 * @param graph The compiled graph
 * @param node The node
 * @param element The vec4 element
 * @param word The word of the element
 * @return The word
 */
static uint32_t tw_word(
    const box_texture_graph_t *graph,
    const box_texture_graph_node_t *node,
    int32_t element,
    int32_t word)
{
    uint32_t result;
    test_assert(node->window > 0);
    test_assert(node->window + (element + 1) * 16 <= graph->param_size);
    ecs_os_memcpy(&result, graph->params + node->window + element * 16 + word * 4,
        ECS_SIZEOF(uint32_t));

    return result;
}

/** Get a float word of the window elements of a node.
 *
 * @param graph The compiled graph
 * @param node The node
 * @param element The vec4 element
 * @param word The word of the element
 * @return The float
 */
static float tw_float(
    const box_texture_graph_t *graph,
    const box_texture_graph_node_t *node,
    int32_t element,
    int32_t word)
{
    uint32_t bits = tw_word(graph, node, element, word);
    float result;
    ecs_os_memcpy(&result, &bits, ECS_SIZEOF(float));

    return result;
}

void TextureGraphWorld_kinds_window(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    const box_texture_node_kind_t *k = tw_kind(world, ecs_id(TexNoise));
    test_assert(k->flags & BOX_TEXTURE_NODE_WINDOW);
    test_int(k->window_size, BOX_TEXTURE_NODE_WINDOW_MAX);
    k = tw_kind(world, ecs_id(TexCells));
    test_assert(k->flags & BOX_TEXTURE_NODE_WINDOW);
    test_int(k->window_size, 2);
    k = tw_kind(world, ecs_id(TexCoords));
    test_assert(k->flags & BOX_TEXTURE_NODE_WINDOW);
    test_int(k->window_size, 1);
    k = tw_kind(world, ecs_id(TexTransform));
    test_assert(k->flags & BOX_TEXTURE_NODE_WINDOW);
    test_int(k->window_size, 1);
    k = tw_kind(world, ecs_id(TexAdd));
    test_assert(!(k->flags & BOX_TEXTURE_NODE_WINDOW));
    test_int(k->window_size, 0);
    k = tw_kind(world, ecs_id(TexBlur));
    test_assert(k->reach != NULL);
    k = tw_kind(world, ecs_id(TexFlow));
    test_assert(k->reach != NULL);

    ecs_fini(world);
}

void TextureGraphWorld_plane_keeps_layout(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t noise = ecs_new(world), tex = ecs_new(world);
    ecs_set(world, noise, TexNoise, {.frequency = 4, .octaves = 3, .lacunarity = 2,
        .gain = 0.5f, .channels = 1});
    ecs_set(world, tex, TextureGraph, {.output = noise, .width = 16});
    const box_texture_graph_t *g = tw_graph(world, tex);
    test_int(g->domain, DomainPlane);
    test_int(g->nodes[0].window, 0);
    test_int(g->nodes[0].reach, 0);
    test_int(g->param_size, g->nodes[0].param_size);
    test_int(g->apron, 0);
    test_flt(g->spacing[0], 0);

    ecs_fini(world);
}

void TextureGraphWorld_noise_window_elements(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t noise = ecs_new(world), tex = ecs_new(world);
    ecs_set(world, noise, TexNoise, {.frequency = 0.5f, .octaves = 3, .lacunarity = 2,
        .gain = 0.5f, .channels = 1});
    ecs_set(world, tex, TextureGraph, {.output = noise, .width = 33, .domain = DomainWorld,
        .format = GraphFloat, .window = {.x = 1e6, .z = -249999.75, .width = 32}});
    const box_texture_graph_t *g = tw_graph(world, tex);
    const box_texture_graph_node_t *n = tw_node(g, noise);
    test_int(g->domain, DomainWorld);
    test_assert(n->window >= n->params + n->param_size);
    test_int(n->window % 16, 0);
    test_int(g->param_size, n->window + BOX_TEXTURE_NODE_WINDOW_MAX * 16);

    /* Octave 0 at 0.5 cells per meter: x 500000 cells, z -124999.875 */
    test_int((int32_t)tw_word(g, n, 0, 0), 500000);
    test_int((int32_t)tw_word(g, n, 0, 1), -125000);
    test_flt(tw_float(g, n, 0, 2), 0);
    test_flt(tw_float(g, n, 0, 3), 0.125f);

    /* Octave 1 at 1 cell per meter */
    test_int((int32_t)tw_word(g, n, 1, 0), 1000000);
    test_int((int32_t)tw_word(g, n, 1, 1), -250000);
    test_flt(tw_float(g, n, 1, 3), 0.25f);

    /* Octave 2 at 2 cells per meter */
    test_int((int32_t)tw_word(g, n, 2, 0), 2000000);
    test_int((int32_t)tw_word(g, n, 2, 1), -500000);
    test_flt(tw_float(g, n, 2, 3), 0.5f);

    /* Octaves past the last are zero */
    test_int(tw_word(g, n, 3, 0), 0);

    ecs_fini(world);
}

void TextureGraphWorld_cells_window_elements(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t cells = ecs_new(world), tex = ecs_new(world);
    ecs_set(world, cells, TexCells, {.count = 64, .jitter = 1, .seed = 3, .size = 4});
    ecs_set(world, tex, TextureGraph, {.output = cells, .width = 9, .domain = DomainWorld,
        .format = GraphFloat4, .window = {.x = -10, .z = 4000001, .width = 8}});
    const box_texture_graph_t *g = tw_graph(world, tex);
    const box_texture_graph_node_t *n = tw_node(g, cells);
    test_int((int32_t)tw_word(g, n, 0, 0), -3);
    test_int((int32_t)tw_word(g, n, 0, 1), 1000000);
    test_flt(tw_float(g, n, 0, 2), 0.5f);
    test_flt(tw_float(g, n, 0, 3), 0.25f);
    test_flt(tw_float(g, n, 1, 0), 0.25f);

    ecs_fini(world);
}

void TextureGraphWorld_windows_share_structure(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t noise = ecs_new(world), a = ecs_new(world), b = ecs_new(world);
    ecs_set(world, noise, TexNoise, {.frequency = 0.1f, .octaves = 4, .lacunarity = 2,
        .gain = 0.5f, .channels = 1});
    ecs_set(world, a, TextureGraph, {.output = noise, .width = 129, .domain = DomainWorld,
        .format = GraphFloat, .window = {.x = 0, .z = 0, .width = 128}});
    ecs_set(world, b, TextureGraph, {.output = noise, .width = 129, .domain = DomainWorld,
        .format = GraphFloat, .window = {.x = 128, .z = 0, .width = 128}});
    const box_texture_graph_t *ga = tw_graph(world, a);
    const box_texture_graph_t *gb = tw_entry(world, b)->graph;
    test_assert(gb != NULL);
    test_assert(ga->structure_hash == gb->structure_hash);
    test_assert(ga->param_hash != gb->param_hash);
    test_assert(ga->origin[0] == 0);
    test_assert(gb->origin[0] == 128);
    test_flt(ga->spacing[0], 1);
    test_flt(ga->spacing[1], 1);

    /* The same window makes the same graph */
    ecs_set(world, b, TextureGraph, {.output = noise, .width = 129, .domain = DomainWorld,
        .format = GraphFloat, .window = {.x = 0, .z = 0, .width = 128}});
    ga = tw_graph(world, a);
    gb = tw_entry(world, b)->graph;
    test_assert(ga->param_hash == gb->param_hash);

    ecs_fini(world);
}

void TextureGraphWorld_spacing(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t coords = ecs_new(world), tex = ecs_new(world);
    ecs_set(world, coords, TexCoords, {.scale = 1});
    ecs_set(world, tex, TextureGraph, {.output = coords, .width = 129, .height = 65,
        .domain = DomainWorld, .format = GraphFloat4});
    const box_texture_graph_t *g = tw_graph(world, tex);
    test_flt(g->spacing[0], 1);
    test_flt(g->spacing[1], 1);

    ecs_set(world, tex, TextureGraph, {.output = coords, .width = 129, .height = 65,
        .domain = DomainWorld, .format = GraphFloat4, .window = {.width = 256}});
    g = tw_graph(world, tex);
    test_flt(g->spacing[0], 2);
    test_flt(g->spacing[1], 2);

    ecs_set(world, tex, TextureGraph, {.output = coords, .width = 129, .height = 65,
        .domain = DomainWorld, .format = GraphFloat4, .window = {.width = 256, .depth = 32}});
    g = tw_graph(world, tex);
    test_flt(g->spacing[0], 2);
    test_flt(g->spacing[1], 0.5f);

    ecs_fini(world);
}

void TextureGraphWorld_apron(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t noise = ecs_new(world), blur = ecs_new(world), slope = ecs_new(world);
    ecs_entity_t sum = ecs_new(world), tex = ecs_new(world);
    ecs_set(world, noise, TexNoise, {.frequency = 0.1f, .octaves = 2, .lacunarity = 2,
        .gain = 0.5f, .channels = 1});
    ecs_set(world, blur, TexBlur, {.input = noise, .radius = 8});
    ecs_set(world, slope, TexSlope, {.height = blur, .strength = 1});
    ecs_set(world, sum, TexAdd, {.a = slope, .b = noise});
    ecs_set(world, tex, TextureGraph, {.output = sum, .width = 65, .domain = DomainWorld,
        .format = GraphFloat, .window = {.width = 64}});
    const box_texture_graph_t *g = tw_graph(world, tex);
    test_int(tw_node(g, blur)->reach, 9);
    test_int(tw_node(g, slope)->reach, 1);
    test_int(tw_node(g, noise)->reach, 0);
    test_int(g->apron, 10);

    /* Two meters per texel halve the reach of the blur in texels */
    ecs_set(world, tex, TextureGraph, {.output = sum, .width = 65, .domain = DomainWorld,
        .format = GraphFloat, .window = {.width = 128}});
    g = tw_graph(world, tex);
    test_int(tw_node(g, blur)->reach, 5);
    test_int(g->apron, 6);

    /* The plane has no apron */
    ecs_set(world, tex, TextureGraph, {.output = sum, .width = 65, .format = GraphSingle});
    g = tw_graph(world, tex);
    test_int(g->apron, 0);
    test_int(tw_node(g, blur)->reach, 0);

    ecs_fini(world);
}

void TextureGraphWorld_plane_kinds_fail(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t blocks = ecs_entity(world, { .name = "blocks" });
    ecs_entity_t ripples = ecs_entity(world, { .name = "ripples" });
    ecs_entity_t shadow = ecs_entity(world, { .name = "shadow" });
    ecs_entity_t tex = ecs_entity(world, { .name = "terrain" });
    ecs_set(world, blocks, TexBlocks, {.courses = 4, .per_course = 2});
    ecs_set(world, ripples, TexRipples, {.count = 16, .speed = 1, .lifetime = 1,
        .width = 0.15f, .density_value = 1});
    ecs_set(world, shadow, TexTransmittance, {.density_value = 0.5f, .absorption = 1,
        .range = 8, .steps = 16});

    ecs_set(world, tex, TextureGraph, {.output = blocks, .width = 16, .domain = DomainWorld});
    tw_failed(world, tex, "blocks", "DomainWorld");
    ecs_set(world, tex, TextureGraph, {.output = ripples, .width = 16, .domain = DomainWorld});
    tw_failed(world, tex, "ripples", "DomainWorld");
    ecs_set(world, tex, TextureGraph, {.output = shadow, .width = 16, .domain = DomainWorld});
    tw_failed(world, tex, "shadow", "does not work on DomainWorld");

    /* The sphere keeps its message */
    ecs_set(world, tex, TextureGraph, {.output = blocks, .width = 16, .domain = DomainSphere});
    tw_failed(world, tex, "blocks", "the graph is on DomainSphere");

    ecs_fini(world);
}

void TextureGraphWorld_float_formats(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t coords = ecs_new(world), tex = ecs_new(world);
    ecs_set(world, coords, TexCoords, {.scale = 1});
    ecs_set(world, tex, TextureGraph, {.output = coords, .width = 8, .domain = DomainWorld,
        .format = GraphFloat});
    const box_texture_graph_t *g = tw_graph(world, tex);
    test_int(g->format, GraphFloat);
    test_bool(tw_entry(world, tex)->single_channel, false);

    ecs_set(world, tex, TextureGraph, {.output = coords, .width = 8, .format = GraphFloat4});
    g = tw_graph(world, tex);
    test_int(g->format, GraphFloat4);
    test_int(g->domain, DomainPlane);

    ecs_fini(world);
}

void TextureGraphWorld_window_reflected(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGraph, {.domain = DomainWorld,
        .window = {.x = 1e6, .z = -2.5, .width = 128, .depth = 64}, .format = GraphFloat});
    char *json = ecs_ptr_to_json(world, ecs_id(TextureGraph), ecs_get(world, tex, TextureGraph));
    test_assert(json != NULL);
    test_assert(strstr(json, "\"domain\":\"DomainWorld\"") != NULL);
    test_assert(strstr(json, "\"window\":{\"x\":\"1e6\", \"z\":-2.5") != NULL);
    test_assert(strstr(json, "\"format\":\"GraphFloat\"") != NULL);
    ecs_os_free(json);

    ecs_fini(world);
}

void TextureGraphWorld_null_backend_window(void) {
    // materials/textures
    ecs_world_t *world = tw_world();
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t noise = ecs_new(world), blur = ecs_new(world), tex = ecs_new(world);
    ecs_set(world, noise, TexNoise, {.frequency = 0.1f, .octaves = 2, .lacunarity = 2,
        .gain = 0.5f, .channels = 1});
    ecs_set(world, blur, TexBlur, {.input = noise, .radius = 4});
    ecs_set(world, tex, TextureGraph, {.output = blur, .width = 33, .domain = DomainWorld,
        .format = GraphFloat, .window = {.x = 1e6, .z = 2e6, .width = 64}});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_texture_t *entry = tw_entry(world, tex);
    const box_render_texture_graph_t *g = box_render_null_texture_graph(null, entry->handle);
    test_assert(g != NULL);
    test_int(g->domain, DomainWorld);
    test_int(g->format, GraphFloat);
    test_assert(g->origin[0] == 1e6);
    test_assert(g->origin[1] == 2e6);
    test_flt(g->spacing[0], 2);
    test_flt(g->spacing[1], 2);
    test_int(g->apron, 3);
    test_int(g->param_size, entry->graph->param_size);

    const box_render_texture_graph_node_t *nodes = box_render_texture_graph_nodes(g);
    test_int(nodes[0].window, entry->graph->nodes[0].window);
    test_assert(nodes[0].window > 0);
    test_int(nodes[1].window, 0);

    const box_render_texture_node_kind_t *k = box_render_null_texture_node_kind(null,
        (uint32_t)nodes[0].kind);
    test_assert(k != NULL);
    test_assert(k->flags & BOX_TEXTURE_NODE_WINDOW);

    ecs_fini(world);
    box_render_null_free(null);
}
