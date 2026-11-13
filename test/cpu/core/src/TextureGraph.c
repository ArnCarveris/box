/**
 * @file TextureGraph.c
 * @brief Tests for TextureGraph: node kinds, the build of graphs, their validation, their hashes
 * and the compiled graphs the null backend receives.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "texture.h"
#include "render.h"

/** The authoring example of TextureGraph */
#define GRAPH_PLATING "test/assets/textures/graph/plating.flecs"

/** A graph whose output node gets its node component after the graph */
#define GRAPH_DECLARED_AFTER "test/assets/textures/graph/declared_after.flecs"

/** Node of a scalar kind registered by the tests, as an application registers its own */
typedef struct TestScalar {
    float value;        /**< Parameter */
    ecs_entity_t in;    /**< Scalar input node */
    float in_value;     /**< Value of in without a node */
} TestScalar;

/** WGSL of TestScalar */
static const char *test_scalar_wgsl =
    "fn test_scalar(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return vec4f(bitcast<f32>(p[0].x) * i[0].x, 0.0, 0.0, 0.0);\n"
    "}\n";

/** Create a headless world.
 *
 * @return The world
 */
static ecs_world_t* world_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    return world;
}

/** Create a headless world that renders with a null backend.
 *
 * @param null Receives the null backend, freed by the caller after ecs_fini
 * @return The world
 */
static ecs_world_t* world_null(
    box_render_null_t **null)
{
    ecs_world_t *world = world_new();
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);

    return world;
}

/** Register TestScalar and its node kind.
 *
 * @param world The world
 * @return The id of the kind
 */
static int32_t register_scalar(
    ecs_world_t *world)
{
    ecs_entity_t c = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "TestScalar" }),
        .type.size = ECS_SIZEOF(TestScalar),
        .type.alignment = ECS_ALIGNOF(TestScalar)
    });

    return box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .output = TexScalar,
        .inputs = {
            { "in", offsetof(TestScalar, in), TexScalar, "in_value",
              offsetof(TestScalar, in_value) }
        },
        .params = {{ "value", offsetof(TestScalar, value), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "test_scalar",
        .source = test_scalar_wgsl,
        .flags = BOX_TEXTURE_NODE_ANIMATED
    });
}

/** Set a TestScalar on an entity.
 *
 * @param world The world
 * @param e The entity
 * @param value Its parameter
 * @param in Its input node, 0 for none
 */
static void set_scalar(
    ecs_world_t *world,
    ecs_entity_t e,
    float value,
    ecs_entity_t in)
{
    ecs_entity_t c = ecs_lookup(world, "TestScalar");
    TestScalar s = { .value = value, .in = in, .in_value = 3 };
    ecs_set_id(world, e, c, ECS_SIZEOF(TestScalar), &s);
}

/** Load a script from the repository.
 *
 * @param world The world
 * @param file The script, from the repository root
 */
static void run_file(
    ecs_world_t *world,
    const char *file)
{
    char *path = box_app_locate(file);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
}

/** Get the registry entry of a texture entity.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The entry
 */
static const box_texture_t* entry_of(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    box_textures_t *t = box_textures_get(world);
    test_assert(t != NULL);
    box_texture_t *entry = ecs_map_get_deref(&t->textures, box_texture_t, tex);
    test_assert(entry != NULL);

    return entry;
}

/** Get the compiled graph of a texture entity.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The compiled graph
 */
static const box_texture_graph_t* graph_of(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    const box_texture_graph_t *graph = entry_of(world, tex)->graph;
    test_assert(graph != NULL);

    return graph;
}

/** Get the status of a texture entity after a frame.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The status
 */
static const TextureStatus* frame_status(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    ecs_progress(world, 0);
    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_assert(status != NULL);

    return status;
}

/** Check that a texture failed with an error that contains two strings.
 *
 * @param world The world
 * @param tex The texture entity
 * @param a The first string
 * @param b The second string
 */
static void test_failed(
    ecs_world_t *world,
    ecs_entity_t tex,
    const char *a,
    const char *b)
{
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

/** Get the kind id of a node component.
 *
 * @param world The world
 * @param component The component
 * @return The id of its kind
 */
static int32_t kind_id(
    ecs_world_t *world,
    ecs_entity_t component)
{
    const TextureNodeKind *kind = ecs_get(world, component, TextureNodeKind);
    test_assert(kind != NULL);

    return kind->id;
}

/** Create a graph of a constant times the coordinates.
 *
 * @param world The world
 * @param tex Receives the texture entity
 * @param uv Receives the coordinates node
 * @param constant Receives the constant node
 * @param math Receives the multiply node, the output
 */
static void make_graph(
    ecs_world_t *world,
    ecs_entity_t *tex,
    ecs_entity_t *uv,
    ecs_entity_t *constant,
    ecs_entity_t *math)
{
    *tex = ecs_entity(world, { .name = "graph" });
    *uv = ecs_entity(world, { .name = "uv" });
    *constant = ecs_entity(world, { .name = "constant" });
    *math = ecs_entity(world, { .name = "math" });
    ecs_set(world, *uv, TexCoords, { .scale = 0 });
    ecs_set(world, *constant, TexConstant, { .value = { 0.25f, 0.5f, 0.75f, 1 } });
    ecs_set(world, *math, TexMath, { .op = MathMultiply, .a = *uv, .b = *constant });
    ecs_set(world, *tex, TextureGraph, { .output = *math, .width = 32, .height = 16 });
}

void TextureGraph_builtin_kinds(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    int32_t constant = kind_id(world, ecs_id(TexConstant));
    int32_t coords = kind_id(world, ecs_id(TexCoords));
    int32_t math = kind_id(world, ecs_id(TexMath));
    test_assert(constant > 0 && coords > 0 && math > 0);
    test_assert(constant != coords && coords != math && constant != math);

    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexMath), TextureNodeKind);
    test_int(kind->output, TexVector);
    test_int(kind->input_count, 3);
    test_str(kind->inputs[0].name, "a");
    test_str(kind->inputs[1].name, "b");
    test_str(kind->inputs[2].name, "c");
    test_int(kind->inputs[0].type, TexVector);
    test_bool(kind->inputs[0].fallback, true);
    test_int(kind->param_count, 1);
    test_str(kind->params[0].name, "op");
    test_int(kind->params[0].offset, 0);
    test_int(kind->params[0].size, 4);
    test_int(kind->param_size, 16);
    test_str(kind->function, "box_tex_math");
    test_bool(kind->neighbourhood, false);
    test_bool(kind->animated, false);

    kind = ecs_get(world, ecs_id(TexConstant), TextureNodeKind);
    test_int(kind->input_count, 0);
    test_int(kind->params[0].size, 16);
    test_str(kind->function, "box_tex_constant");

    box_textures_t *t = box_textures_get(world);
    const box_texture_node_kind_t *k = box_texture_node_kind(t, math);
    test_assert(k != NULL);
    test_assert(k->component == ecs_id(TexMath));
    test_int(k->node_size, 16 + 3 * 16);
    test_assert(strstr(k->source, "fn box_tex_math(") != NULL);
    test_assert(box_texture_node_kind(t, 0) == NULL);
    test_assert(box_texture_node_kind(t, 1000) == NULL);

    ecs_fini(world);
}

void TextureGraph_kind_reflected_json(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexMath), TextureNodeKind);
    char *json = ecs_ptr_to_json(world, ecs_id(TextureNodeKind), kind);
    test_assert(json != NULL);
    test_assert(strstr(json, "\"box_tex_math\"") != NULL);
    test_assert(strstr(json, "\"a_value\"") == NULL);
    test_assert(strstr(json, "\"TexVector\"") != NULL);
    test_assert(strstr(json, "\"b\"") != NULL);
    ecs_os_free(json);

    ecs_fini(world);
}

void TextureGraph_script(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    run_file(world, GRAPH_PLATING);
    ecs_entity_t tex = ecs_lookup(world, "plating");
    ecs_entity_t uv = ecs_lookup(world, "plating.uv");
    ecs_entity_t bright = ecs_lookup(world, "plating.bright");
    ecs_entity_t tinted = ecs_lookup(world, "plating.tinted");
    test_assert(tex && uv && bright && tinted);
    test_assert(ecs_get(world, tex, TextureGraph)->output == tinted);

    const TextureStatus *status = frame_status(world, tex);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);
    test_assert(status->error == NULL);
    test_int(ecs_get(world, tex, Texture)->width, 1024);
    test_int(ecs_get(world, tex, Texture)->height, 1024);

    const box_texture_graph_t *graph = graph_of(world, tex);
    test_int(graph->node_count, 3);
    test_int(graph->width, 1024);
    test_int(graph->height, 1024);
    test_int(graph->domain, DomainPlane);
    test_int(graph->format, GraphColor);
    test_assert(graph->nodes[0].entity == uv);
    test_assert(graph->nodes[1].entity == bright);
    test_assert(graph->nodes[2].entity == tinted);
    test_int(graph->nodes[0].kind, kind_id(world, ecs_id(TexCoords)));
    test_int(graph->nodes[1].kind, kind_id(world, ecs_id(TexMath)));
    test_int(graph->nodes[1].inputs[0], 0);
    test_int(graph->nodes[1].inputs[1], -1);
    test_int(graph->nodes[1].inputs[2], -1);
    test_int(graph->nodes[2].inputs[0], 1);
    test_int(graph->nodes[2].inputs[1], -1);

    const box_texture_graph_node_t *node = &graph->nodes[2];
    test_int(node->param_size, 64);
    test_int(graph->param_size, 16 + 64 + 64);
    int32_t op;
    float b[4];
    ecs_os_memcpy(&op, graph->params + node->params, 4);
    ecs_os_memcpy(b, graph->params + node->params + 16 + 16, 16);
    test_int(op, MathAdd);
    test_flt(b[0], 0.2f);
    test_flt(b[1], 0.1f);
    test_flt(b[2], 0.05f);
    test_flt(b[3], 0.0f);
    ecs_os_memcpy(&op, graph->params + graph->nodes[1].params, 4);
    test_int(op, MathMultiply);

    ecs_fini(world);
}

void TextureGraph_script_declared_after(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    run_file(world, GRAPH_DECLARED_AFTER);
    ecs_entity_t tex = ecs_lookup(world, "panel");
    test_assert(tex != 0);
    const TextureStatus *status = frame_status(world, tex);
    test_int(status->state, TextureReady);
    test_int(ecs_get(world, tex, Texture)->width, 64);
    test_int(ecs_get(world, tex, Texture)->height, 32);
    const box_texture_graph_t *graph = graph_of(world, tex);
    test_int(graph->node_count, 2);
    test_int(graph->format, GraphLinear);
    test_bool(entry_of(world, tex)->single_channel, false);

    float c[4];
    ecs_os_memcpy(c, graph->params + graph->nodes[1].params + 16 + 2 * 16, 16);
    test_flt(c[0], 0.5f);
    test_flt(c[3], 0.5f);
    float scale;
    ecs_os_memcpy(&scale, graph->params + graph->nodes[0].params, 4);
    test_flt(scale, 2.0f);

    ecs_fini(world);
}

void TextureGraph_node_set_next_frame(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t node = ecs_entity(world, { .name = "later" });
    ecs_entity_t tex = ecs_entity(world, { .name = "waits" });
    ecs_set(world, tex, TextureGraph, { .output = node, .width = 8 });
    test_int(frame_status(world, tex)->state, TextureLoading);
    test_assert(entry_of(world, tex)->graph == NULL);

    ecs_set(world, node, TexConstant, { .value = { 1, 0, 0, 1 } });
    const TextureStatus *status = frame_status(world, tex);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);
    test_int(graph_of(world, tex)->node_count, 1);

    ecs_fini(world);
}

void TextureGraph_default_size(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, { .value = { 1, 1, 1, 1 } });
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGraph, { .output = node });
    test_int(frame_status(world, tex)->state, TextureReady);
    test_int(ecs_get(world, tex, Texture)->width, 512);
    test_int(ecs_get(world, tex, Texture)->height, 512);

    ecs_entity_t wide = ecs_new(world);
    ecs_set(world, wide, TextureGraph, { .output = node, .width = 64 });
    test_int(frame_status(world, wide)->state, TextureReady);
    test_int(ecs_get(world, wide, Texture)->height, 64);

    ecs_fini(world);
}

void TextureGraph_capped_by_quality(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {
        .width = 64, .height = 64, .texture_quality = TextureQualityLow });
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, { .value = { 1, 1, 1, 1 } });
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGraph, { .output = node, .width = 1024, .height = 256 });
    test_int(frame_status(world, tex)->state, TextureReady);
    test_int(ecs_get(world, tex, Texture)->width, 512);
    test_int(ecs_get(world, tex, Texture)->height, 128);
    test_int(graph_of(world, tex)->width, 512);

    ecs_fini(world);
    box_render_null_free(null);
}

void TextureGraph_single_format(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, { .value = { 1, 1, 1, 1 } });
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGraph, {
        .output = node, .width = 8, .format = GraphSingle, .domain = DomainSphere });
    test_int(frame_status(world, tex)->state, TextureReady);
    test_bool(entry_of(world, tex)->single_channel, true);
    test_int(graph_of(world, tex)->domain, DomainSphere);
    test_int(graph_of(world, tex)->format, GraphSingle);

    ecs_fini(world);
}

void TextureGraph_rebuild_on_graph_change(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    test_int(frame_status(world, tex)->revision, 1);
    uint64_t structure = graph_of(world, tex)->structure_hash;

    ecs_set(world, tex, TextureGraph, { .output = math, .width = 64, .height = 16 });
    const TextureStatus *status = frame_status(world, tex);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_int(ecs_get(world, tex, Texture)->width, 64);
    test_assert(graph_of(world, tex)->structure_hash == structure);

    ecs_set(world, tex, TextureGraph, { .output = constant, .width = 64, .height = 16 });
    test_int(frame_status(world, tex)->revision, 3);
    test_int(graph_of(world, tex)->node_count, 1);
    test_assert(graph_of(world, tex)->structure_hash != structure);

    ecs_fini(world);
}

void TextureGraph_unchanged_rebuild_keeps_revision(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    test_int(frame_status(world, tex)->revision, 1);

    ecs_set(world, tex, TextureGraph, { .output = math, .width = 32, .height = 16 });
    test_int(frame_status(world, tex)->revision, 1);
    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);

    ecs_set(world, constant, TexConstant, { .value = { 0.25f, 0.5f, 0.75f, 1 } });
    test_int(frame_status(world, tex)->revision, 1);

    ecs_fini(world);
}

void TextureGraph_param_edit_keeps_structure(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    test_int(frame_status(world, tex)->revision, 1);
    uint64_t structure = graph_of(world, tex)->structure_hash;
    uint64_t params = graph_of(world, tex)->param_hash;

    ecs_set(world, math, TexMath, {
        .op = MathAdd, .a = uv, .b = constant, .c_value = { 1, 2, 3, 4 } });
    test_int(frame_status(world, tex)->revision, 2);
    test_assert(graph_of(world, tex)->structure_hash == structure);
    test_assert(graph_of(world, tex)->param_hash != params);
    params = graph_of(world, tex)->param_hash;

    ecs_set(world, uv, TexCoords, { .scale = 4 });
    test_int(frame_status(world, tex)->revision, 3);
    test_assert(graph_of(world, tex)->structure_hash == structure);
    test_assert(graph_of(world, tex)->param_hash != params);

    ecs_fini(world);
}

void TextureGraph_edge_edit_changes_structure(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    test_int(frame_status(world, tex)->revision, 1);
    uint64_t structure = graph_of(world, tex)->structure_hash;

    ecs_set(world, math, TexMath, {
        .op = MathMultiply, .a = uv, .b_value = { 0.25f, 0.5f, 0.75f, 1 } });
    test_int(frame_status(world, tex)->revision, 2);
    test_int(graph_of(world, tex)->node_count, 2);
    test_assert(graph_of(world, tex)->structure_hash != structure);

    ecs_set(world, math, TexMath, {
        .op = MathMultiply, .a = uv, .b = constant, .c = constant });
    test_int(frame_status(world, tex)->revision, 3);
    test_int(graph_of(world, tex)->node_count, 3);
    test_assert(graph_of(world, tex)->structure_hash != structure);

    ecs_fini(world);
}

void TextureGraph_same_structure_same_hash(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_entity_t uv2 = ecs_new(world);
    ecs_entity_t constant2 = ecs_new(world);
    ecs_entity_t math2 = ecs_new(world);
    ecs_entity_t tex2 = ecs_new(world);
    ecs_set(world, uv2, TexCoords, { .scale = 3 });
    ecs_set(world, constant2, TexConstant, { .value = { 1, 1, 1, 1 } });
    ecs_set(world, math2, TexMath, { .op = MathMax, .a = uv2, .b = constant2 });
    ecs_set(world, tex2, TextureGraph, { .output = math2, .width = 8 });
    ecs_progress(world, 0);

    const box_texture_graph_t *a = graph_of(world, tex);
    const box_texture_graph_t *b = graph_of(world, tex2);
    test_assert(a->structure_hash == b->structure_hash);
    test_assert(a->param_hash != b->param_hash);

    ecs_fini(world);
}

void TextureGraph_nested_change(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_entity_t outer = ecs_new(world);
    ecs_set(world, outer, TexMath, { .op = MathAdd, .a = math, .b_value = { 1, 1, 1, 0 } });
    ecs_set(world, tex, TextureGraph, { .output = outer, .width = 8 });
    test_int(frame_status(world, tex)->revision, 1);
    test_int(graph_of(world, tex)->node_count, 4);
    uint64_t params = graph_of(world, tex)->param_hash;

    ecs_set(world, constant, TexConstant, { .value = { 0, 0, 0, 1 } });
    test_int(frame_status(world, tex)->revision, 2);
    test_assert(graph_of(world, tex)->param_hash != params);

    TexConstant *c = ecs_ensure(world, constant, TexConstant);
    c->value[0] = 0.5f;
    ecs_modified(world, constant, TexConstant);
    test_int(frame_status(world, tex)->revision, 3);

    ecs_fini(world);
}

void TextureGraph_unreached_change(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, TexConstant, { .value = { 1, 1, 1, 1 } });
    test_int(frame_status(world, tex)->revision, 1);

    ecs_set(world, other, TexConstant, { .value = { 0, 1, 1, 1 } });
    test_int(frame_status(world, tex)->revision, 1);
    test_assert(entry_of(world, tex)->graph_dirty == false);

    ecs_fini(world);
}

void TextureGraph_shared_node(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t math = ecs_new(world);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, uv, TexCoords, { .scale = 1 });
    ecs_set(world, math, TexMath, { .op = MathMix, .a = uv, .b = uv, .c = uv });
    ecs_set(world, tex, TextureGraph, { .output = math, .width = 8 });
    test_int(frame_status(world, tex)->state, TextureReady);
    const box_texture_graph_t *graph = graph_of(world, tex);
    test_int(graph->node_count, 2);
    test_int(graph->nodes[1].inputs[0], 0);
    test_int(graph->nodes[1].inputs[1], 0);
    test_int(graph->nodes[1].inputs[2], 0);
    test_int(graph->nodes[1].inputs[3], -1);

    ecs_fini(world);
}

void TextureGraph_node_replaced(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    test_int(frame_status(world, tex)->revision, 1);
    uint64_t structure = graph_of(world, tex)->structure_hash;

    ecs_remove(world, constant, TexConstant);
    ecs_set(world, constant, TexCoords, { .scale = 2 });
    const TextureStatus *status = frame_status(world, tex);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_int(graph_of(world, tex)->nodes[1].kind, kind_id(world, ecs_id(TexCoords)));
    test_assert(graph_of(world, tex)->structure_hash != structure);

    ecs_fini(world);
}

void TextureGraph_node_removed(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    test_int(frame_status(world, tex)->revision, 1);

    ecs_log_set_level(-4);
    ecs_remove(world, constant, TexConstant);
    test_int(frame_status(world, tex)->state, TextureLoading);
    ecs_progress(world, 0);
    test_failed(world, tex, "graph", "no node component");
    test_failed(world, tex, "constant", "graph");
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);
    test_assert(graph_of(world, tex)->node_count == 3);

    ecs_set(world, constant, TexConstant, { .value = { 1, 1, 1, 1 } });
    const TextureStatus *status = frame_status(world, tex);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_assert(status->error == NULL);

    ecs_fini(world);
}

void TextureGraph_node_deleted(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    test_int(frame_status(world, tex)->revision, 1);

    ecs_log_set_level(-4);
    ecs_delete(world, constant);
    frame_status(world, tex);
    test_failed(world, tex, "graph", "missing node");
    test_failed(world, tex, "'math'", "input 'b'");

    ecs_set(world, math, TexMath, { .op = MathMultiply, .a = uv });
    const TextureStatus *status = frame_status(world, tex);
    test_int(status->state, TextureReady);
    test_int(graph_of(world, tex)->node_count, 2);

    ecs_fini(world);
}

void TextureGraph_output_deleted(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    test_int(frame_status(world, tex)->revision, 1);

    ecs_log_set_level(-4);
    ecs_delete(world, math);
    frame_status(world, tex);
    test_failed(world, tex, "graph", "output is a missing node");

    ecs_fini(world);
}

void TextureGraph_error_no_output(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_entity(world, { .name = "empty" });
    ecs_log_set_level(-4);
    ecs_set(world, tex, TextureGraph, { .width = 8 });
    frame_status(world, tex);
    test_failed(world, tex, "'empty'", "no output node");
    test_assert(ecs_get(world, tex, Texture) == NULL);

    ecs_fini(world);
}

void TextureGraph_error_no_node_component(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t plain = ecs_entity(world, { .name = "plain" });
    ecs_entity_t math = ecs_entity(world, { .name = "math" });
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_set(world, math, TexMath, { .op = MathAdd, .a = plain });
    ecs_set(world, tex, TextureGraph, { .output = math, .width = 8 });
    ecs_log_set_level(-4);
    test_int(frame_status(world, tex)->state, TextureLoading);
    frame_status(world, tex);
    test_failed(world, tex, "'graph'", "node 'plain' has no node component");

    ecs_fini(world);
}

void TextureGraph_error_two_node_components(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t both = ecs_entity(world, { .name = "both" });
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_set(world, both, TexConstant, { .value = { 1, 1, 1, 1 } });
    ecs_set(world, both, TexCoords, { .scale = 1 });
    ecs_set(world, tex, TextureGraph, { .output = both, .width = 8 });
    ecs_log_set_level(-4);
    frame_status(world, tex);
    test_failed(world, tex, "'both'", "more than one node component");

    ecs_fini(world);
}

void TextureGraph_error_cycle(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_set(world, a, TexMath, { .op = MathAdd, .a = b });
    ecs_set(world, b, TexMath, { .op = MathAdd, .b = a });
    ecs_set(world, tex, TextureGraph, { .output = a, .width = 8 });
    ecs_log_set_level(-4);
    frame_status(world, tex);
    test_failed(world, tex, "'graph'", "cycle");
    test_failed(world, tex, "node 'a'", "cycle");

    ecs_set(world, b, TexMath, { .op = MathAdd });
    test_int(frame_status(world, tex)->state, TextureReady);
    test_int(graph_of(world, tex)->node_count, 2);

    ecs_fini(world);
}

void TextureGraph_error_self_cycle(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_set(world, a, TexMath, { .op = MathAdd, .a = a });
    ecs_set(world, tex, TextureGraph, { .output = a, .width = 8 });
    ecs_log_set_level(-4);
    frame_status(world, tex);
    test_failed(world, tex, "'graph'", "cycle");

    ecs_fini(world);
}

void TextureGraph_error_type_mismatch(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    test_assert(register_scalar(world) > 0);
    ecs_entity_t scalar = ecs_entity(world, { .name = "scalar" });
    ecs_entity_t math = ecs_entity(world, { .name = "math" });
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    set_scalar(world, scalar, 2, 0);
    ecs_set(world, math, TexMath, { .op = MathAdd, .a = scalar });
    ecs_set(world, tex, TextureGraph, { .output = math, .width = 8 });
    ecs_log_set_level(-4);
    frame_status(world, tex);
    test_failed(world, tex, "'graph'", "node 'scalar' outputs Scalar");
    test_failed(world, tex, "input 'a' of node 'math'", "expects Vector");

    ecs_entity_t uv = ecs_entity(world, { .name = "uv" });
    ecs_set(world, uv, TexCoords, { .scale = 1 });
    set_scalar(world, scalar, 2, uv);
    ecs_set(world, tex, TextureGraph, { .output = scalar, .width = 8 });
    frame_status(world, tex);
    test_failed(world, tex, "node 'uv' outputs Vector", "input 'in' of node 'scalar'");

    ecs_fini(world);
}

void TextureGraph_custom_kind(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    int32_t id = register_scalar(world);
    test_assert(id > kind_id(world, ecs_id(TexMath)));
    ecs_entity_t c = ecs_lookup(world, "TestScalar");
    const TextureNodeKind *kind = ecs_get(world, c, TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->id, id);
    test_int(kind->output, TexScalar);
    test_int(kind->input_count, 1);
    test_str(kind->inputs[0].name, "in");
    test_int(kind->inputs[0].type, TexScalar);
    test_bool(kind->animated, true);
    test_str(kind->function, "test_scalar");

    ecs_entity_t leaf = ecs_new(world);
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t tex = ecs_new(world);
    set_scalar(world, leaf, 5, 0);
    set_scalar(world, root, 7, leaf);
    ecs_set(world, tex, TextureGraph, { .output = root, .width = 8, .format = GraphSingle });
    test_int(frame_status(world, tex)->state, TextureReady);
    const box_texture_graph_t *graph = graph_of(world, tex);
    test_int(graph->node_count, 2);
    test_int(graph->nodes[0].kind, id);
    test_int(graph->nodes[0].output, TexScalar);
    test_int(graph->nodes[0].inputs[0], -1);
    test_int(graph->nodes[1].inputs[0], 0);
    test_int(graph->nodes[0].param_size, 32);

    float v[4];
    ecs_os_memcpy(v, graph->params + graph->nodes[0].params, 4);
    test_flt(v[0], 5);
    ecs_os_memcpy(v, graph->params + graph->nodes[0].params + 16, 16);
    test_flt(v[0], 3);
    test_flt(v[1], 0);
    ecs_os_memcpy(v, graph->params + graph->nodes[1].params + 16, 16);
    test_flt(v[0], 0);

    ecs_fini(world);
}

void TextureGraph_register_replaces(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    int32_t id = register_scalar(world);
    ecs_entity_t node = ecs_new(world);
    ecs_entity_t tex = ecs_new(world);
    set_scalar(world, node, 1, 0);
    ecs_set(world, tex, TextureGraph, { .output = node, .width = 8 });
    test_int(frame_status(world, tex)->revision, 1);
    test_int(graph_of(world, tex)->nodes[0].param_size, 32);

    ecs_entity_t c = ecs_lookup(world, "TestScalar");
    int32_t again = box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .output = TexScalar,
        .params = {{ "value", offsetof(TestScalar, value), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "test_scalar",
        .source = test_scalar_wgsl
    });
    test_int(again, id);
    test_int(ecs_get(world, c, TextureNodeKind)->input_count, 0);
    test_int(frame_status(world, tex)->revision, 2);
    test_int(graph_of(world, tex)->nodes[0].param_size, 16);

    ecs_fini(world);
}

void TextureGraph_register_invalid(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t c = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "TestScalar" }),
        .type.size = ECS_SIZEOF(TestScalar),
        .type.alignment = ECS_ALIGNOF(TestScalar)
    });
    ecs_log_set_level(-4);
    test_int(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .params = {{ "value", 0, 6, BOX_TEXTURE_PARAM_F32 }},
        .function = "f",
        .source = "fn f() {}"
    }), 0);
    test_int(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .function = "f"
    }), 0);
    test_int(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .params = {
            { "a", 0, 16, BOX_TEXTURE_PARAM_F32 }, { "b", 0, 16, BOX_TEXTURE_PARAM_F32 },
            { "c", 0, 16, BOX_TEXTURE_PARAM_F32 }, { "d", 0, 16, BOX_TEXTURE_PARAM_F32 },
            { "e", 0, 4, BOX_TEXTURE_PARAM_F32 }
        },
        .function = "f",
        .source = "fn f() {}"
    }), 0);
    test_assert(ecs_get(world, c, TextureNodeKind) == NULL);

    ecs_fini(world);
}

void TextureGraph_param_layout(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t c = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "TestScalar" }),
        .type.size = ECS_SIZEOF(TestScalar),
        .type.alignment = ECS_ALIGNOF(TestScalar)
    });
    test_assert(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .params = {
            { "a", 0, 4, BOX_TEXTURE_PARAM_F32 }, { "b", 0, 8, BOX_TEXTURE_PARAM_F32 },
            { "c", 0, 12, BOX_TEXTURE_PARAM_F32 }, { "d", 0, 4, BOX_TEXTURE_PARAM_U32 }
        },
        .function = "f",
        .source = "fn f() {}"
    }) > 0);
    const TextureNodeKind *kind = ecs_get(world, c, TextureNodeKind);
    test_int(kind->param_count, 4);
    test_int(kind->params[0].offset, 0);
    test_int(kind->params[1].offset, 8);
    test_int(kind->params[2].offset, 16);
    test_int(kind->params[3].offset, 28);
    test_int(kind->param_size, 32);

    ecs_fini(world);
}

void TextureGraph_with_path_error(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, { .value = { 1, 1, 1, 1 } });
    ecs_entity_t tex = ecs_entity(world, { .name = "both" });
    ecs_log_set_level(-4);
    ecs_set(world, tex, TexturePath, { .path = "test/assets/textures/gray.png" });
    ecs_set(world, tex, TextureGraph, { .output = node, .width = 8 });
    frame_status(world, tex);
    test_failed(world, tex, "'both'", "TextureGraph");
    test_failed(world, tex, "TexturePath", "TextureGraph");

    ecs_remove(world, tex, TexturePath);
    const TextureStatus *status = frame_status(world, tex);
    test_int(status->state, TextureReady);
    test_int(ecs_get(world, tex, Texture)->width, 8);
    test_assert(graph_of(world, tex)->node_count == 1);

    ecs_fini(world);
}

void TextureGraph_with_gen_error(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, { .value = { 1, 1, 1, 1 } });
    ecs_entity_t tex = ecs_entity(world, { .name = "both" });
    ecs_log_set_level(-4);
    ecs_set(world, tex, TextureGraph, { .output = node, .width = 8 });
    ecs_set(world, tex, TextureGen, { .width = 16 });
    frame_status(world, tex);
    test_failed(world, tex, "TextureGen", "TextureGraph");

    ecs_remove(world, tex, TextureGraph);
    int32_t frames = 0;
    while (frame_status(world, tex)->state != TextureReady && frames < 100) {
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, tex, Texture)->width, 16);
    test_assert(entry_of(world, tex)->graph == NULL);

    ecs_fini(world);
}

void TextureGraph_remove(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    test_int(frame_status(world, tex)->state, TextureReady);
    ecs_remove(world, tex, TextureGraph);
    test_assert(ecs_get(world, tex, Texture) == NULL);
    test_assert(ecs_get(world, tex, TextureStatus) == NULL);
    box_textures_t *t = box_textures_get(world);
    test_assert(ecs_map_get(&t->textures, tex) == NULL);

    ecs_set(world, constant, TexConstant, { .value = { 0, 0, 0, 0 } });
    ecs_progress(world, 0);
    test_assert(ecs_get(world, tex, TextureStatus) == NULL);

    ecs_fini(world);
}

void TextureGraph_two_graphs_share_node(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, TextureGraph, { .output = constant, .width = 8 });
    ecs_progress(world, 0);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);
    test_int(ecs_get(world, other, TextureStatus)->revision, 1);

    ecs_set(world, constant, TexConstant, { .value = { 0, 0, 0, 0 } });
    ecs_progress(world, 0);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);
    test_int(ecs_get(world, other, TextureStatus)->revision, 2);

    ecs_fini(world);
}

void TextureGraph_null_backend_records(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_texture_t *entry = entry_of(world, tex);
    test_assert(entry->handle != 0);
    test_bool(entry->graph_sent, true);
    test_int(null->texture_graphs, 1);
    test_int(null->texture_node_kinds, 7);

    const box_render_texture_graph_t *g = box_render_null_texture_graph(null, entry->handle);
    test_assert(g != NULL);
    test_int(g->width, 32);
    test_int(g->height, 16);
    test_int(g->node_count, 3);
    test_int(g->format, GraphColor);
    test_int(g->domain, DomainPlane);
    test_assert(g->structure_hash == entry->graph->structure_hash);
    test_assert(g->param_hash == entry->graph->param_hash);
    test_int(g->param_size, entry->graph->param_size);

    const box_render_texture_graph_node_t *nodes = box_render_texture_graph_nodes(g);
    test_int(nodes[2].kind, kind_id(world, ecs_id(TexMath)));
    test_int(nodes[2].inputs[0], 0);
    test_int(nodes[2].inputs[1], 1);
    test_int(nodes[2].inputs[2], -1);
    test_int(nodes[2].inputs[3], -1);
    test_int(nodes[1].kind, kind_id(world, ecs_id(TexConstant)));
    float value[4];
    ecs_os_memcpy(value, box_render_texture_graph_params(g) + nodes[1].params, 16);
    test_flt(value[0], 0.25f);
    test_flt(value[2], 0.75f);

    const box_render_texture_node_kind_t *k = box_render_null_texture_node_kind(null,
        (uint32_t)kind_id(world, ecs_id(TexMath)));
    test_assert(k != NULL);
    test_int(k->output, TexVector);
    test_int(k->input_count, 3);
    test_int(k->param_size, 16);
    test_str(box_render_texture_node_kind_function(k), "box_tex_math");
    test_assert(strstr(box_render_texture_node_kind_source(k), "fn box_tex_math(") != NULL);

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->texture_graphs, 1);
    test_int(null->texture_node_kinds, 7);

    ecs_fini(world);
    box_render_null_free(null);
}

void TextureGraph_null_backend_param_edit(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_progress(world, 0);
    box_render_sync(world);
    uint32_t handle = entry_of(world, tex)->handle;
    const box_render_texture_graph_t *g = box_render_null_texture_graph(null, handle);
    test_assert(g != NULL);
    uint64_t structure = g->structure_hash, params = g->param_hash;

    ecs_set(world, constant, TexConstant, { .value = { 1, 0, 0, 1 } });
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->texture_graphs, 2);
    test_int(entry_of(world, tex)->handle, handle);
    g = box_render_null_texture_graph(null, handle);
    test_assert(g->structure_hash == structure);
    test_assert(g->param_hash != params);

    ecs_fini(world);
    box_render_null_free(null);
}

void TextureGraph_null_backend_remove(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_progress(world, 0);
    box_render_sync(world);
    uint32_t handle = entry_of(world, tex)->handle;
    test_assert(box_render_null_texture_graph(null, handle) != NULL);

    ecs_delete(world, tex);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->texture_graph_destroys, 1);
    test_assert(box_render_null_texture_graph(null, handle) == NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void TextureGraph_null_backend_failed_keeps_graph(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_progress(world, 0);
    box_render_sync(world);
    uint32_t handle = entry_of(world, tex)->handle;

    ecs_log_set_level(-4);
    ecs_set(world, math, TexMath, { .op = MathAdd, .a = math });
    ecs_progress(world, 0);
    box_render_sync(world);
    test_failed(world, tex, "'graph'", "cycle");
    test_int(null->texture_graphs, 1);
    test_int(null->texture_graph_destroys, 0);
    test_assert(box_render_null_texture_graph(null, handle) != NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void TextureGraph_null_backend_restart(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_progress(world, 0);
    box_render_sync(world);
    uint32_t handle = entry_of(world, tex)->handle;

    box_render_null_t *second = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(second);
    box_render_set_backend(world, &backend);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(second->texture_node_kinds, 7);
    test_int(second->texture_graphs, 1);
    test_assert(box_render_null_texture_graph(second, handle) != NULL);
    test_int(box_render_null_texture_graph(second, handle)->node_count, 3);

    ecs_fini(world);
    box_render_null_free(second);
    box_render_null_free(null);
}

void TextureGraph_custom_kind_sent(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->texture_node_kinds, 0);

    int32_t id = register_scalar(world);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->texture_node_kinds, 0);

    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->texture_node_kinds, 8);
    const box_render_texture_node_kind_t *k = box_render_null_texture_node_kind(null,
        (uint32_t)id);
    test_assert(k != NULL);
    test_int(k->output, TexScalar);
    test_int(k->input_types[0], TexScalar);
    test_int(k->flags, BOX_TEXTURE_NODE_ANIMATED);
    test_str(box_render_texture_node_kind_function(k), "test_scalar");
    test_str(box_render_texture_node_kind_source(k), test_scalar_wgsl);

    ecs_fini(world);
    box_render_null_free(null);
}

void TextureGraph_builtin_time_splat(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexTime), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexScalar);
    test_int(kind->input_count, 0);
    test_int(kind->param_count, 2);
    test_str(kind->params[0].name, "scale");
    test_int(kind->params[0].offset, 0);
    test_str(kind->params[1].name, "offset");
    test_int(kind->params[1].offset, 4);
    test_int(kind->param_size, 16);
    test_str(kind->function, "box_tex_time");
    test_bool(kind->animated, true);
    test_bool(kind->neighbourhood, false);

    kind = ecs_get(world, ecs_id(TexSplat), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexVector);
    test_int(kind->input_count, 1);
    test_str(kind->inputs[0].name, "in");
    test_int(kind->inputs[0].type, TexScalar);
    test_bool(kind->inputs[0].fallback, true);
    test_str(kind->function, "box_tex_splat");
    test_bool(kind->animated, false);

    ecs_entity_t time = ecs_entity(world, { .name = "time" });
    ecs_entity_t splat = ecs_entity(world, { .name = "splat" });
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_set(world, time, TexTime, { .scale = 2, .offset = 0.5f });
    ecs_set(world, splat, TexSplat, { .in = time });
    ecs_set(world, tex, TextureGraph, { .output = splat, .width = 8 });
    ecs_progress(world, 0);
    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_assert(status != NULL);
    test_int(status->state, TextureReady);

    const box_texture_t *entry = entry_of(world, tex);
    test_int(entry->graph->node_count, 2);
    test_int(entry->graph->nodes[0].kind, kind_id(world, ecs_id(TexTime)));
    test_int(entry->graph->nodes[1].inputs[0], 0);
    float params[2];
    ecs_os_memcpy(params, entry->graph->params + entry->graph->nodes[0].params, 8);
    test_flt(params[0], 2);
    test_flt(params[1], 0.5f);

    ecs_fini(world);
}

void TextureGraph_time_into_vector_fails(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t time = ecs_entity(world, { .name = "time" });
    ecs_entity_t math = ecs_entity(world, { .name = "math" });
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_set(world, time, TexTime, { .scale = 1 });
    ecs_set(world, math, TexMath, { .op = MathAdd, .a = time });
    ecs_set(world, tex, TextureGraph, { .output = math, .width = 8 });
    ecs_log_set_level(-4);
    frame_status(world, tex);
    ecs_log_set_level(-1);
    test_failed(world, tex, "node 'time' outputs Scalar", "input 'a' of node 'math'");

    ecs_fini(world);
}

void TextureGraph_null_backend_rate(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_progress(world, 0);
    box_render_sync(world);
    uint32_t handle = entry_of(world, tex)->handle;
    const box_render_texture_graph_t *g = box_render_null_texture_graph(null, handle);
    test_assert(g != NULL);
    test_flt(g->rate, 0);
    test_int(null->texture_graphs, 1);
    uint64_t structure = g->structure_hash, params = g->param_hash;

    TextureGraph *graph = ecs_get_mut(world, tex, TextureGraph);
    graph->rate = 12;
    ecs_modified(world, tex, TextureGraph);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->texture_graphs, 2);
    g = box_render_null_texture_graph(null, handle);
    test_flt(g->rate, 12);
    test_assert(g->structure_hash == structure);
    test_assert(g->param_hash == params);

    graph = ecs_get_mut(world, tex, TextureGraph);
    graph->rate = -1;
    ecs_modified(world, tex, TextureGraph);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->texture_graphs, 3);
    test_flt(box_render_null_texture_graph(null, handle)->rate, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void TextureGraph_cache_budget_packet(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(null->last.texture_graph_budget == 0);

    ecs_singleton_set(world, TextureGraphCache, { .budget = 4096 });
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(null->last.texture_graph_budget == 4096);

    ecs_fini(world);
    box_render_null_free(null);
}
