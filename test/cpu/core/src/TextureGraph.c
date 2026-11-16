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

/** Coordinates turned, scaled and warped by TexTransform and TexWarp */
#define GRAPH_COORDS "test/assets/textures/graph/coords.flecs"

/** A graph whose output node gets its node component after the graph */
#define GRAPH_DECLARED_AFTER "test/assets/textures/graph/declared_after.flecs"

/** Billow noise at coordinates warped by a vector of noise */
#define GRAPH_NOISE "test/assets/textures/graph/noise.flecs"

/** A rocky world and a gas giant whose maps are graphs on the sphere domain, as templates */
#define GRAPH_PLANET "test/assets/textures/graph/planet.flecs"

/** The nebula of the background of the planet scene, a static graph on the sphere domain */
#define GRAPH_NEBULA "test/assets/textures/graph/nebula.flecs"

/** Node literals that leave members out */
#define GRAPH_DEFAULTS "test/assets/textures/graph/defaults.flecs"

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
    ecs_set(world, *math, TexMultiply, { .a = *uv, .b = *constant });
    ecs_set(world, *tex, TextureGraph, { .output = *math, .width = 32, .height = 16 });
}

void TextureGraph_builtin_kinds(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    int32_t constant = kind_id(world, ecs_id(TexConstant));
    int32_t coords = kind_id(world, ecs_id(TexCoords));
    int32_t math = kind_id(world, ecs_id(TexMix));
    test_assert(constant > 0 && coords > 0 && math > 0);
    test_assert(constant != coords && coords != math && constant != math);

    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexMix), TextureNodeKind);
    test_int(kind->output, TexAny);
    test_int(kind->input_count, 3);
    test_str(kind->inputs[0].name, "a");
    test_str(kind->inputs[1].name, "b");
    test_str(kind->inputs[2].name, "factor");
    test_int(kind->inputs[0].type, TexAny);
    test_bool(kind->inputs[0].fallback, true);
    test_str(kind->inputs[0].value, "a_value");
    test_str(kind->inputs[2].value, "factor_value");
    test_int(kind->param_count, 0);
    test_int(kind->types_offset, 0);
    test_int(kind->param_size, 16);
    test_str(kind->function, "box_tex_mix");
    test_bool(kind->neighbourhood, false);
    test_bool(kind->animated, false);

    kind = ecs_get(world, ecs_id(TexRemap), TextureNodeKind);
    test_int(kind->input_count, 1);
    test_str(kind->inputs[0].name, "input");
    test_str(kind->inputs[0].value, "input_value");
    test_int(kind->param_count, 5);
    test_str(kind->params[0].name, "from_min");
    test_int(kind->params[0].offset, 0);
    test_int(kind->params[0].size, 4);
    test_str(kind->params[4].name, "clamp");
    test_int(kind->params[4].offset, 16);
    test_int(kind->params[4].size, 4);
    test_int(kind->types_offset, 20);
    test_int(kind->param_size, 32);
    test_str(kind->function, "box_tex_remap");

    kind = ecs_get(world, ecs_id(TexConstant), TextureNodeKind);
    test_int(kind->input_count, 0);
    test_str(kind->inputs[0].value, NULL);
    test_int(kind->params[0].size, 16);
    test_str(kind->function, "box_tex_constant");

    box_textures_t *t = box_textures_get(world);
    const box_texture_node_kind_t *k = box_texture_node_kind(t, math);
    test_assert(k != NULL);
    test_assert(k->component == ecs_id(TexMix));
    test_int(k->node_size, 16 + 3 * 16);
    test_assert(strstr(k->source, "fn box_tex_mix(") != NULL);
    test_assert(box_texture_node_kind(t, 0) == NULL);
    test_assert(box_texture_node_kind(t, 1000) == NULL);

    ecs_fini(world);
}

void TextureGraph_normal_slope_kinds(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexNormal), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexVector);
    test_int(kind->input_count, 1);
    test_str(kind->inputs[0].name, "height");
    test_int(kind->inputs[0].type, TexScalar);
    test_bool(kind->inputs[0].fallback, true);
    test_int(kind->param_count, 1);
    test_str(kind->params[0].name, "strength");
    test_int(kind->params[0].size, 4);
    test_str(kind->function, "box_tex_normal");
    test_bool(kind->neighbourhood, true);
    test_bool(kind->animated, false);

    kind = ecs_get(world, ecs_id(TexSlope), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexScalar);
    test_int(kind->input_count, 1);
    test_int(kind->inputs[0].type, TexScalar);
    test_str(kind->params[0].name, "strength");
    test_str(kind->function, "box_tex_slope");
    test_bool(kind->neighbourhood, true);

    ecs_entity_t tex = ecs_entity(world, { .name = "relief" });
    ecs_entity_t uv = ecs_entity(world, { .name = "uv" });
    ecs_entity_t height = ecs_entity(world, { .name = "height" });
    ecs_entity_t normal = ecs_entity(world, { .name = "normal" });
    ecs_set(world, uv, TexCoords, { .scale = 0 });
    ecs_set(world, height, TexSplit, { .input = uv, .channel = ChannelG });
    ecs_set(world, normal, TexNormal, { .height = height, .strength = 2 });
    ecs_set(world, tex, TextureGraph, { .output = normal, .width = 32, .height = 16,
        .format = GraphLinear });
    ecs_progress(world, 0);
    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_assert(status != NULL);
    test_assert(status->state != TextureFailed);
    const box_texture_graph_t *graph = graph_of(world, tex);
    test_assert(graph != NULL);
    test_int(graph->node_count, 3);
    test_int(graph->nodes[2].kind, kind_id(world, ecs_id(TexNormal)));
    test_int(graph->nodes[2].inputs[0], 1);

    ecs_entity_t wrong = ecs_entity(world, { .name = "wrong" });
    ecs_set(world, wrong, TexSlope, { .height = uv });
    ecs_set(world, tex, TextureGraph, { .output = wrong, .width = 32, .height = 16 });
    ecs_progress(world, 0);
    status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureFailed);
    test_assert(strstr(status->error, "expects Scalar") != NULL);

    ecs_fini(world);
}

/** TexDistance is a built in kind of a Scalar input and a range, a neighbourhood kind of
 * BOX_TEX_DISTANCE_PASSES passes, and a script names it nodes.Distance. */
void TextureGraph_distance_kind(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexDistance), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexScalar);
    test_int(kind->input_count, 1);
    test_str(kind->inputs[0].name, "input");
    test_int(kind->inputs[0].type, TexScalar);
    test_bool(kind->inputs[0].fallback, true);
    test_str(kind->inputs[0].value, "input_value");
    test_int(kind->param_count, 1);
    test_str(kind->params[0].name, "range");
    test_int(kind->params[0].offset, 0);
    test_int(kind->params[0].size, 4);
    test_int(kind->param_size, 16);
    test_str(kind->function, "box_tex_distance");
    test_bool(kind->neighbourhood, true);
    test_bool(kind->animated, false);
    test_bool(kind->plane, false);
    test_int(kind->passes, BOX_TEX_DISTANCE_PASSES);
    test_assert(BOX_TEX_DISTANCE_PASSES <= BOX_TEXTURE_NODE_PASSES);

    const char *code =
        "using box.texture\n"
        "coast {\n"
        "  uv { nodes.Coords: {} }\n"
        "  u { nodes.Split: {input: uv, channel: ChannelR} }\n"
        "  dist { nodes.Distance: {input: u, range: 8} }\n"
        "  Graph: {output: dist, width: 32, height: 16, domain: DomainSphere}\n"
        "}\n";
    test_assert(ecs_script_run(world, "distance", code, NULL) == 0);
    ecs_entity_t tex = ecs_lookup(world, "coast");
    ecs_entity_t dist = ecs_lookup(world, "coast.dist");
    test_assert(tex != 0 && dist != 0);
    const TexDistance *d = ecs_get(world, dist, TexDistance);
    test_assert(d != NULL);
    test_assert(d->input == ecs_lookup(world, "coast.u"));
    test_flt(d->range, 8);
    test_flt(d->input_value, 0);
    test_int(frame_status(world, tex)->state, TextureReady);
    const box_texture_graph_t *graph = graph_of(world, tex);
    test_assert(graph != NULL);
    test_int(graph->node_count, 3);
    test_int(graph->nodes[2].kind, kind_id(world, ecs_id(TexDistance)));
    test_int(graph->nodes[2].inputs[0], 1);

    /* A zero range stays zero. */
    ecs_entity_t zero = ecs_new(world);
    ecs_add(world, zero, TexDistance);
    test_flt(ecs_get(world, zero, TexDistance)->range, 0);

    /* The input is a Scalar. */
    ecs_entity_t wrong = ecs_entity(world, { .name = "wrong" });
    ecs_set(world, wrong, TexDistance, { .input = ecs_lookup(world, "coast.uv"), .range = 4 });
    ecs_set(world, tex, TextureGraph, { .output = wrong, .width = 32, .height = 16 });
    ecs_progress(world, 0);
    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureFailed);
    test_assert(strstr(status->error, "expects Scalar") != NULL);

    ecs_fini(world);
}

void TextureGraph_kind_reflected_json(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexMultiply), TextureNodeKind);
    char *json = ecs_ptr_to_json(world, ecs_id(TextureNodeKind), kind);
    test_assert(json != NULL);
    test_assert(strstr(json, "\"box_tex_multiply\"") != NULL);
    test_assert(strstr(json, "\"name\":\"a_value\"") == NULL);
    test_assert(strstr(json, "\"value\":\"a_value\"") != NULL);
    test_assert(strstr(json, "\"Any\"") != NULL);
    test_assert(strstr(json, "\"b\"") != NULL);
    ecs_os_free(json);

    ecs_fini(world);
}

/** Check that a path names an entity, and the given one when it is not 0.
 *
 * @param world The world
 * @param path The path
 * @param expect The entity, 0 for any
 */
static void test_path(
    ecs_world_t *world,
    const char *path,
    ecs_entity_t expect)
{
    ecs_entity_t e = ecs_lookup(world, path);
    if (!e || (expect && e != expect)) {
        printf("path: %s\n", path);
    }

    test_assert(e != 0);
    if (expect) {
        test_assert(e == expect);
    }
}

void TextureGraph_names(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    test_path(world, "box.texture.Texture", ecs_id(Texture));
    test_path(world, "box.texture.Path", ecs_id(TexturePath));
    test_path(world, "box.texture.Gen", ecs_id(TextureGen));
    test_path(world, "box.texture.Graph", ecs_id(TextureGraph));
    test_path(world, "box.texture.State", ecs_id(TextureState));
    test_path(world, "box.texture.Status", ecs_id(TextureStatus));
    test_path(world, "box.texture.Registry", ecs_id(TextureRegistry));
    test_path(world, "box.texture.Pixels", ecs_id(TexturePixels));
    test_path(world, "box.texture.gen.Pattern", ecs_id(TextureGenPattern));
    test_path(world, "box.texture.gen.Op", ecs_id(TextureGenOp));
    test_path(world, "box.texture.gen.Distance", ecs_id(TextureGenDistance));
    test_path(world, "box.texture.gen.ToneOrder", ecs_id(TextureGenToneOrder));
    test_path(world, "box.texture.gen.Output", ecs_id(TextureGenOutput));
    test_path(world, "box.texture.gen.Layer", ecs_id(TextureGenLayer));
    test_path(world, "box.texture.gen.Key", ecs_id(TextureGenKey));
    test_path(world, "box.texture.graph.Domain", ecs_id(TextureGraphDomain));
    test_path(world, "box.texture.graph.Format", ecs_id(TextureGraphFormat));
    test_path(world, "box.texture.graph.ValueType", ecs_id(TextureValueType));
    test_path(world, "box.texture.graph.Cache", ecs_id(TextureGraphCache));
    test_path(world, "box.texture.graph.Stats", ecs_id(TextureGraphStats));
    test_path(world, "box.texture.graph.NodeKind", ecs_id(TextureNodeKind));
    test_path(world, "box.texture.graph.Port", ecs_id(TextureNodePort));
    test_path(world, "box.texture.graph.Param", ecs_id(TextureNodeParam));
    test_path(world, "box.texture.graph.Preview", ecs_id(TextureNodePreview));
    test_path(world, "box.texture.graph.PreviewStatus", ecs_id(TextureNodePreviewStatus));
    test_path(world, "box.texture.nodes.Constant", ecs_id(TexConstant));
    test_path(world, "box.texture.nodes.Coords", ecs_id(TexCoords));
    test_path(world, "box.texture.nodes.Time", ecs_id(TexTime));
    test_path(world, "box.texture.nodes.Splat", ecs_id(TexSplat));
    test_path(world, "box.texture.nodes.Add", ecs_id(TexAdd));
    test_path(world, "box.texture.nodes.Multiply", ecs_id(TexMultiply));
    test_path(world, "box.texture.nodes.Remap", ecs_id(TexRemap));
    test_path(world, "box.texture.nodes.Split", ecs_id(TexSplit));
    test_path(world, "box.texture.nodes.Combine", ecs_id(TexCombine));
    test_path(world, "box.texture.nodes.Warp", ecs_id(TexWarp));
    test_path(world, "box.texture.nodes.Transform", ecs_id(TexTransform));
    test_path(world, "box.texture.nodes.Ramp", ecs_id(TexRamp));
    test_path(world, "box.texture.nodes.Curve", ecs_id(TexCurve));
    test_path(world, "box.texture.nodes.Blocks", ecs_id(TexBlocks));
    test_path(world, "box.texture.nodes.Cells", ecs_id(TexCells));
    test_path(world, "box.texture.nodes.Blur", ecs_id(TexBlur));
    test_path(world, "box.texture.nodes.Distance", ecs_id(TexDistance));
    test_path(world, "box.texture.nodes.Noise", ecs_id(TexNoise));
    test_path(world, "box.texture.nodes.Image", ecs_id(TexImage));
    test_path(world, "box.texture.nodes.Lut", ecs_id(TexLut));
    test_path(world, "box.texture.nodes.Normal", ecs_id(TexNormal));
    test_path(world, "box.texture.nodes.Slope", ecs_id(TexSlope));
    test_path(world, "box.texture.nodes.Channel", ecs_id(TextureChannel));
    test_path(world, "box.texture.nodes.CellsDistance", ecs_id(TextureCellsDistance));
    test_path(world, "box.texture.nodes.NoiseFractal", ecs_id(TextureNoiseFractal));
    test_path(world, "box.texture.nodes.Address", ecs_id(TextureAddress));

    test_path(world, "box.texture.graph.ValueType.Scalar", 0);
    test_path(world, "box.texture.graph.ValueType.Vector", 0);
    test_path(world, "box.texture.graph.ValueType.Any", 0);
    test_path(world, "box.texture.graph.Domain.DomainPlane", 0);
    test_path(world, "box.texture.graph.Format.GraphColor", 0);
    test_path(world, "box.texture.gen.Pattern.GenNoise", 0);
    test_path(world, "box.texture.gen.ToneOrder.GenToneChecker", 0);
    test_path(world, "box.texture.nodes.Channel.ChannelR", 0);
    test_path(world, "box.texture.nodes.Address.AddressWrap", 0);
    test_path(world, "box.texture.nodes.CellsDistance.CellsNearest", 0);
    test_path(world, "box.texture.nodes.NoiseFractal.NoiseFbm", 0);
    test_path(world, "box.texture.State.Ready", 0);

    test_assert(ecs_lookup(world, "box.texture.TexturePath") == 0);
    test_assert(ecs_lookup(world, "box.texture.TexNoise") == 0);
    test_assert(ecs_lookup(world, "box.texture.nodes.TexNoise") == 0);
    test_assert(ecs_lookup(world, "box.texture.nodes.TexAdd") == 0);
    test_assert(ecs_lookup(world, "box.texture.nodes.Math") == 0);
    test_assert(ecs_lookup(world, "box.texture.nodes.MathOp") == 0);
    test_assert(ecs_lookup(world, "box.texture.graph.ValueType.TexScalar") == 0);

    ecs_fini(world);
}

void TextureGraph_system_names(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    test_path(world, "box.texture.OnSet", 0);
    test_path(world, "box.texture.OnRemove", 0);
    test_path(world, "box.texture.Surfaces", 0);
    test_path(world, "box.texture.Update", 0);
    test_path(world, "box.texture.PixelsOnSet", 0);
    test_path(world, "box.texture.PixelsOnRemove", 0);
    test_path(world, "box.texture.gen.OnSet", 0);
    test_path(world, "box.texture.gen.OnRemove", 0);
    test_path(world, "box.texture.graph.Build", 0);
    test_path(world, "box.texture.graph.OnSet", 0);
    test_path(world, "box.texture.graph.OnRemove", 0);

    test_assert(ecs_has_id(world,
        ecs_lookup(world, "box.texture.Update"), EcsSystem));
    test_assert(ecs_has_id(world,
        ecs_lookup(world, "box.texture.graph.Build"), EcsSystem));
    test_assert(ecs_has_id(world,
        ecs_lookup(world, "box.texture.gen.OnSet"), EcsObserver));

    test_assert(ecs_lookup(world, "box.texture.TextureOnSet") == 0);
    test_assert(ecs_lookup(world, "box.texture.TextureUpdate") == 0);
    test_assert(ecs_lookup(world, "box.texture.TextureGenOnSet") == 0);
    test_assert(ecs_lookup(world, "box.texture.GenOnSet") == 0);
    test_assert(ecs_lookup(world, "box.texture.TextureGraphBuild") == 0);
    test_assert(ecs_lookup(world, "box.texture.GraphBuild") == 0);
    test_assert(ecs_lookup(world, "box.texture.GraphOnRemove") == 0);

    ecs_fini(world);
}

void TextureGraph_names_script(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    register_scalar(world);
    const char *code =
        "using box.texture\n"
        "qualified {\n"
        "  uv { nodes.Coords: {} }\n"
        "  noise { nodes.Noise: {coords: uv, frequency: 4, fractal: NoiseRidged} }\n"
        "  Graph: {output: noise, width: 16, domain: DomainPlane}\n"
        "}\n";
    test_assert(ecs_script_run(world, "names", code, NULL) == 0);
    code =
        "using box.texture\n"
        "using box.texture.nodes\n"
        "unqualified {\n"
        "  red { Constant: {value: {1, 0, 0, 1}} }\n"
        "  r { Split: {input: red, channel: ChannelG} }\n"
        "  out { Combine: {r: r} }\n"
        "  Graph: {output: out, width: 16}\n"
        "}\n";
    test_assert(ecs_script_run(world, "names_using", code, NULL) == 0);

    ecs_entity_t qualified = ecs_lookup(world, "qualified");
    ecs_entity_t noise = ecs_lookup(world, "qualified.noise");
    test_assert(qualified && noise);
    test_assert(ecs_get(world, qualified, TextureGraph)->output == noise);
    test_int(ecs_get(world, noise, TexNoise)->fractal, NoiseRidged);
    test_assert(ecs_get(world, noise, TexNoise)->coords == ecs_lookup(world, "qualified.uv"));

    ecs_entity_t r = ecs_lookup(world, "unqualified.r");
    test_assert(r != 0);
    test_int(ecs_get(world, r, TexSplit)->channel, ChannelG);
    test_assert(ecs_get(world, r, TexSplit)->input == ecs_lookup(world, "unqualified.red"));

    /* A kind an application registers keeps the scope of its component. */
    ecs_entity_t c = ecs_lookup(world, "TestScalar");
    test_assert(c != 0);
    test_assert(ecs_get(world, c, TextureNodeKind) != NULL);
    test_assert(ecs_get_parent(world, c) == 0);

    test_int(frame_status(world, qualified)->state, TextureReady);
    test_int(frame_status(world, ecs_lookup(world, "unqualified"))->state, TextureReady);

    ecs_fini(world);
}

/** TexCells is a built in kind: a coords input that takes the coordinates without a node, an
 * evolve input with a constant, five parameters, and full jitter when it is added. */
void TextureGraph_cells_kind(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    int32_t cells_id = kind_id(world, ecs_id(TexCells));
    test_assert(cells_id > 0);

    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexCells), TextureNodeKind);
    test_int(kind->output, TexVector);
    test_int(kind->input_count, 2);
    test_str(kind->inputs[0].name, "coords");
    test_int(kind->inputs[0].type, TexVector);
    test_bool(kind->inputs[0].fallback, false);
    test_bool(kind->inputs[0].coords, true);
    test_str(kind->inputs[1].name, "evolve");
    test_int(kind->inputs[1].type, TexScalar);
    test_bool(kind->inputs[1].fallback, true);
    test_bool(kind->inputs[1].coords, false);
    test_int(kind->param_count, 5);
    test_int(kind->param_size, 32);
    test_str(kind->function, "box_tex_cells");
    test_bool(kind->neighbourhood, false);

    ecs_entity_t node = ecs_new(world);
    ecs_add(world, node, TexCells);
    const TexCells *cells = ecs_get(world, node, TexCells);
    test_flt(cells->jitter, 1);
    test_int(cells->count, 64);
    test_int(cells->distance, CellsNearest);

    TexCells *mut = ecs_get_mut(world, node, TexCells);
    mut->count = 40;
    mut->seed = 6;
    mut->distance = CellsEdge;
    mut->evolve_value = 2.5f;
    ecs_modified(world, node, TexCells);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TextureGraph, {.output = node, .width = 16, .height = 16});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_texture_graph_t *graph = graph_of(world, tex);
    test_int(graph->node_count, 1);
    test_int(graph->nodes[0].kind, cells_id);
    test_int(graph->nodes[0].param_size, 32 + 2 * 16);
    int32_t count, seed, distance;
    float jitter, evolve;
    ecs_os_memcpy(&count, graph->params + kind->params[0].offset, 4);
    ecs_os_memcpy(&jitter, graph->params + kind->params[1].offset, 4);
    ecs_os_memcpy(&seed, graph->params + kind->params[2].offset, 4);
    ecs_os_memcpy(&distance, graph->params + kind->params[3].offset, 4);
    ecs_os_memcpy(&evolve, graph->params + 32 + 16, 4);
    test_int(count, 40);
    test_flt(jitter, 1);
    test_int(seed, 6);
    test_int(distance, CellsEdge);
    test_flt(evolve, 2.5f);

    const box_render_texture_node_kind_t *k = box_render_null_texture_node_kind(null,
        (uint32_t)cells_id);
    test_assert(k != NULL);
    test_int(k->input_count, 2);
    test_int(k->coords_inputs, 1);
    test_assert(strstr(box_render_texture_node_kind_source(k), "fn box_tex_cells(") != NULL);

    ecs_fini(world);
    box_render_null_free(null);
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
    test_int(graph->nodes[1].kind, kind_id(world, ecs_id(TexMultiply)));
    test_int(graph->nodes[2].kind, kind_id(world, ecs_id(TexAdd)));
    test_int(graph->nodes[1].inputs[0], 0);
    test_int(graph->nodes[1].inputs[1], -1);
    test_int(graph->nodes[1].inputs[2], -1);
    test_int(graph->nodes[2].inputs[0], 1);
    test_int(graph->nodes[2].inputs[1], -1);

    const box_texture_graph_node_t *node = &graph->nodes[2];
    test_int(node->param_size, 48);
    test_int(graph->param_size, 16 + 48 + 48);
    uint32_t types;
    float b[4];
    ecs_os_memcpy(&types, graph->params + node->params, 4);
    ecs_os_memcpy(b, graph->params + node->params + 16 + 16, 16);
    test_uint(types, 1u << 4);
    test_flt(b[0], 0.2f);
    test_flt(b[1], 0.1f);
    test_flt(b[2], 0.05f);
    test_flt(b[3], 0.0f);
    ecs_os_memcpy(&types, graph->params + graph->nodes[1].params, 4);
    test_uint(types, 1u << 4);

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

    ecs_set(world, constant, TexConstant, { .value = { 1, 2, 3, 4 } });
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

    ecs_set(world, math, TexMultiply, {
        .a = uv, .b_value = { 0.25f, 0.5f, 0.75f, 1 } });
    test_int(frame_status(world, tex)->revision, 2);
    test_int(graph_of(world, tex)->node_count, 2);
    test_assert(graph_of(world, tex)->structure_hash != structure);

    ecs_set(world, math, TexMultiply, { .a = constant, .b = uv });
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
    ecs_set(world, math2, TexMultiply, { .a = uv2, .b = constant2 });
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
    ecs_set(world, outer, TexAdd, { .a = math, .b_value = { 1, 1, 1, 0 } });
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
    ecs_set(world, math, TexMix, { .a = uv, .b = uv, .factor = uv });
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

    ecs_set(world, math, TexMultiply, { .a = uv });
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
    ecs_set(world, math, TexAdd, { .a = plain });
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
    ecs_set(world, a, TexAdd, { .a = b });
    ecs_set(world, b, TexAdd, { .b = a });
    ecs_set(world, tex, TextureGraph, { .output = a, .width = 8 });
    ecs_log_set_level(-4);
    frame_status(world, tex);
    test_failed(world, tex, "'graph'", "cycle");
    test_failed(world, tex, "node 'a'", "cycle");

    ecs_set(world, b, TexAdd, { .a = 0 });
    test_int(frame_status(world, tex)->state, TextureReady);
    test_int(graph_of(world, tex)->node_count, 2);

    ecs_fini(world);
}

void TextureGraph_error_self_cycle(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_set(world, a, TexAdd, { .a = a });
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
    ecs_entity_t split = ecs_entity(world, { .name = "split" });
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    set_scalar(world, scalar, 2, 0);
    ecs_set(world, split, TexSplit, { .input = scalar });
    ecs_set(world, tex, TextureGraph, { .output = split, .width = 8 });
    ecs_log_set_level(-4);
    frame_status(world, tex);
    test_failed(world, tex, "'graph'", "node 'scalar' outputs Scalar");
    test_failed(world, tex, "input 'input' of node 'split'", "expects Vector");

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
    test_assert(id > kind_id(world, ecs_id(TexRemap)));
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
    test_failed(world, tex, "'both'", "a Graph");
    test_failed(world, tex, "a Path", "a Graph");

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
    test_failed(world, tex, "a Gen", "a Graph");

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
    test_int(null->texture_node_kinds, 44);

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
    test_int(nodes[2].output, TexVector);
    test_int(nodes[2].kind, kind_id(world, ecs_id(TexMultiply)));
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
        (uint32_t)kind_id(world, ecs_id(TexMultiply)));
    test_assert(k != NULL);
    test_int(k->output, TexAny);
    test_int(k->input_count, 2);
    test_int(k->param_size, 16);
    test_str(box_render_texture_node_kind_function(k), "box_tex_multiply");
    test_assert(strstr(box_render_texture_node_kind_source(k), "fn box_tex_multiply(") != NULL);
    test_int(k->passes, 1);

    k = box_render_null_texture_node_kind(null, (uint32_t)kind_id(world, ecs_id(TexBlur)));
    test_assert(k != NULL);
    test_int(k->passes, 2);
    test_str(box_render_texture_node_kind_function(k), "box_tex_blur");

    k = box_render_null_texture_node_kind(null, (uint32_t)kind_id(world, ecs_id(TexDistance)));
    test_assert(k != NULL);
    test_int(k->passes, BOX_TEX_DISTANCE_PASSES);
    test_str(box_render_texture_node_kind_function(k), "box_tex_distance");

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->texture_graphs, 1);
    test_int(null->texture_node_kinds, 44);

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
    ecs_set(world, math, TexMultiply, { .a = math });
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
    test_int(second->texture_node_kinds, 44);
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
    test_int(null->texture_node_kinds, 45);
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

/** Build a graph whose output is a new math node and get the compiled node of it.
 *
 * @param world The world
 * @param tex The texture entity
 * @param kind The component of the node, such as TexAdd
 * @param value The value of the component
 * @return The compiled node, the last
 */
static const box_texture_graph_node_t* math_node(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t kind,
    const void *value)
{
    ecs_entity_t node = ecs_new(world);
    const ecs_type_info_t *ti = ecs_get_type_info(world, kind);
    ecs_set_id(world, node, kind, (size_t)ti->size, value);
    ecs_set(world, tex, TextureGraph, { .output = node, .width = 8 });
    test_int(frame_status(world, tex)->state, TextureReady);
    const box_texture_graph_t *graph = graph_of(world, tex);

    return &graph->nodes[graph->node_count - 1];
}

/** Read the types word of a compiled math node.
 *
 * @param world The world
 * @param tex The texture entity
 * @param node The compiled node
 * @return The word
 */
static uint32_t math_types(
    ecs_world_t *world,
    ecs_entity_t tex,
    const box_texture_graph_node_t *node)
{
    const box_texture_node_kind_t *kind = box_texture_node_kind(box_textures_get(world),
        node->kind);
    uint32_t result;
    test_assert(kind->types_offset >= 0);
    ecs_os_memcpy(&result, graph_of(world, tex)->params + node->params + kind->types_offset, 4);

    return result;
}

void TextureGraph_math_types(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_entity_t uv = ecs_entity(world, { .name = "uv" });
    ecs_entity_t split = ecs_entity(world, { .name = "split" });
    ecs_set(world, uv, TexCoords, { .scale = 0 });
    ecs_set(world, split, TexSplit, { .input = uv, .channel = ChannelG });
    const box_texture_graph_node_t *node;

    node = math_node(world, tex, ecs_id(TexAdd), &(TexAdd){ .a = uv, .b = uv });
    test_int(node->output, TexVector);
    test_uint(math_types(world, tex, node), (1u << 4) | (1u << 5));

    node = math_node(world, tex, ecs_id(TexAdd), &(TexAdd){ .a = split,
        .b_value = { 1, 2, 3, 4 } });
    test_int(node->output, TexScalar);
    test_uint(math_types(world, tex, node), 1u);

    node = math_node(world, tex, ecs_id(TexMultiply), &(TexMultiply){ .a = split, .b = uv });
    test_int(node->output, TexVector);
    test_uint(math_types(world, tex, node), 1u | (1u << 5));

    node = math_node(world, tex, ecs_id(TexMix), &(TexMix){ .a = split, .b = split,
        .factor = uv });
    test_int(node->output, TexVector);
    test_uint(math_types(world, tex, node), 3u | (1u << 6));

    node = math_node(world, tex, ecs_id(TexClamp), &(TexClamp){ .input = split, .min = split,
        .max = split });
    test_int(node->output, TexScalar);
    test_uint(math_types(world, tex, node), 7u);

    node = math_node(world, tex, ecs_id(TexAbs), &(TexAbs){ .input = split });
    test_int(node->output, TexScalar);
    test_uint(math_types(world, tex, node), 1u);

    node = math_node(world, tex, ecs_id(TexSubtract), &(TexSubtract){
        .a_value = { 1, 1, 1, 1 } });
    test_int(node->output, TexVector);
    test_uint(math_types(world, tex, node), 0);

    node = math_node(world, tex, ecs_id(TexDot), &(TexDot){ .a = uv, .b = uv });
    test_int(node->output, TexScalar);

    node = math_node(world, tex, ecs_id(TexLength), &(TexLength){ .input = uv });
    test_int(node->output, TexScalar);

    node = math_node(world, tex, ecs_id(TexNormalize), &(TexNormalize){ .input = split });
    test_int(node->output, TexScalar);

    node = math_node(world, tex, ecs_id(TexCross), &(TexCross){ .a = split, .b = split });
    test_int(node->output, TexVector);

    ecs_fini(world);
}

void TextureGraph_math_remap_params(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t uv = ecs_new(world);
    ecs_set(world, uv, TexCoords, { .scale = 0 });
    const box_texture_graph_node_t *node = math_node(world, tex, ecs_id(TexRemap), &(TexRemap){
        .input = uv, .from_min = -1, .from_max = 1, .to_min = 2, .to_max = 4, .clamp = true });
    float range[4];
    uint32_t clamp;
    test_int(node->param_size, 48);
    ecs_os_memcpy(range, graph_of(world, tex)->params + node->params, 16);
    ecs_os_memcpy(&clamp, graph_of(world, tex)->params + node->params + 16, 4);
    test_flt(range[0], -1);
    test_flt(range[1], 1);
    test_flt(range[2], 2);
    test_flt(range[3], 4);
    test_uint(clamp, 1);
    test_uint(math_types(world, tex, node), 1u << 4);

    node = math_node(world, tex, ecs_id(TexRemap), &(TexRemap){
        .input = uv, .from_min = -1, .from_max = 1, .to_min = 2, .to_max = 4 });
    ecs_os_memcpy(&clamp, graph_of(world, tex)->params + node->params + 16, 4);
    test_uint(clamp, 0);

    ecs_fini(world);
}

void TextureGraph_math_kind_changes_structure(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t uv = ecs_new(world);
    ecs_set(world, uv, TexCoords, { .scale = 0 });
    math_node(world, tex, ecs_id(TexAdd), &(TexAdd){ .a = uv, .b = uv });
    uint64_t structure = graph_of(world, tex)->structure_hash;
    uint64_t params = graph_of(world, tex)->param_hash;

    math_node(world, tex, ecs_id(TexAdd), &(TexAdd){ .a = uv, .b = uv });
    test_assert(graph_of(world, tex)->structure_hash == structure);
    test_assert(graph_of(world, tex)->param_hash == params);

    math_node(world, tex, ecs_id(TexMultiply), &(TexMultiply){ .a = uv, .b = uv });
    test_assert(graph_of(world, tex)->structure_hash != structure);

    math_node(world, tex, ecs_id(TexDot), &(TexDot){ .a = uv, .b = uv });
    test_assert(graph_of(world, tex)->structure_hash != structure);

    ecs_fini(world);
}

/** A math node kind as the math_kinds test expects it */
typedef struct math_kind_t {
    const char *path;           /**< Path of its component */
    ecs_entity_t component;     /**< Its component */
    TextureValueType output;    /**< Type of its value */
    const char *function;       /**< Name of its WGSL function */
    const char *inputs[4];      /**< Names of its inputs, NULL ends the list */
} math_kind_t;

void TextureGraph_math_kinds(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const math_kind_t expect[] = {
        { "Add", ecs_id(TexAdd), TexAny, "box_tex_add", { "a", "b" } },
        { "Subtract", ecs_id(TexSubtract), TexAny, "box_tex_subtract", { "a", "b" } },
        { "Multiply", ecs_id(TexMultiply), TexAny, "box_tex_multiply", { "a", "b" } },
        { "Divide", ecs_id(TexDivide), TexAny, "box_tex_divide", { "a", "b" } },
        { "Min", ecs_id(TexMin), TexAny, "box_tex_min", { "a", "b" } },
        { "Max", ecs_id(TexMax), TexAny, "box_tex_max", { "a", "b" } },
        { "Power", ecs_id(TexPower), TexAny, "box_tex_power", { "a", "b" } },
        { "Dot", ecs_id(TexDot), TexScalar, "box_tex_dot", { "a", "b" } },
        { "Cross", ecs_id(TexCross), TexVector, "box_tex_cross", { "a", "b" } },
        { "Mix", ecs_id(TexMix), TexAny, "box_tex_mix", { "a", "b", "factor" } },
        { "Clamp", ecs_id(TexClamp), TexAny, "box_tex_clamp", { "input", "min", "max" } },
        { "Step", ecs_id(TexStep), TexAny, "box_tex_step", { "input", "edge" } },
        { "Smoothstep", ecs_id(TexSmoothstep), TexAny, "box_tex_smoothstep",
            { "input", "edge0", "edge1" } },
        { "Abs", ecs_id(TexAbs), TexAny, "box_tex_abs", { "input" } },
        { "Negate", ecs_id(TexNegate), TexAny, "box_tex_negate", { "input" } },
        { "Floor", ecs_id(TexFloor), TexAny, "box_tex_floor", { "input" } },
        { "Fract", ecs_id(TexFract), TexAny, "box_tex_fract", { "input" } },
        { "Sin", ecs_id(TexSin), TexAny, "box_tex_sin", { "input" } },
        { "Cos", ecs_id(TexCos), TexAny, "box_tex_cos", { "input" } },
        { "Length", ecs_id(TexLength), TexScalar, "box_tex_length", { "input" } },
        { "Normalize", ecs_id(TexNormalize), TexAny, "box_tex_normalize", { "input" } },
        { "Remap", ecs_id(TexRemap), TexAny, "box_tex_remap", { "input" } }
    };
    box_textures_t *t = box_textures_get(world);
    int32_t i, j, count = (int32_t)(sizeof(expect) / sizeof(expect[0]));
    for (i = 0; i < count; i ++) {
        char *path = flecs_asprintf("box.texture.nodes.%s", expect[i].path);
        test_path(world, path, expect[i].component);
        ecs_os_free(path);

        const TextureNodeKind *kind = ecs_get(world, expect[i].component, TextureNodeKind);
        test_assert(kind != NULL);
        test_assert(kind->id > 0);
        test_int(kind->output, expect[i].output);
        test_str(kind->function, expect[i].function);
        test_int(kind->param_count, i == count - 1 ? 5 : 0);
        test_bool(kind->neighbourhood, false);
        test_bool(kind->animated, false);
        test_int(kind->passes, 1);
        for (j = 0; j < 4 && expect[i].inputs[j]; j ++) {
            char *value = flecs_asprintf("%s_value", expect[i].inputs[j]);
            test_str(kind->inputs[j].name, expect[i].inputs[j]);
            test_int(kind->inputs[j].type, TexAny);
            test_bool(kind->inputs[j].fallback, true);
            test_str(kind->inputs[j].value, value);
            test_bool(kind->inputs[j].coords, false);
            ecs_os_free(value);
        }

        test_int(kind->input_count, j);
        test_int(kind->types_offset, i == count - 1 ? 20 : 0);

        const box_texture_node_kind_t *k = box_texture_node_kind(t, kind->id);
        test_assert(k != NULL);
        test_assert(k->component == expect[i].component);
        char *fn = flecs_asprintf("fn %s(", expect[i].function);
        test_assert(strstr(k->source, fn) != NULL);
        ecs_os_free(fn);
    }

    ecs_fini(world);
}

void TextureGraph_split_combine_kinds(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexSplit), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexScalar);
    test_int(kind->input_count, 1);
    test_str(kind->inputs[0].name, "input");
    test_int(kind->inputs[0].type, TexVector);
    test_bool(kind->inputs[0].fallback, true);
    test_int(kind->param_count, 1);
    test_str(kind->params[0].name, "channel");
    test_int(kind->types_offset, -1);
    test_str(kind->function, "box_tex_split");

    kind = ecs_get(world, ecs_id(TexCombine), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexVector);
    test_int(kind->input_count, 4);
    test_str(kind->inputs[0].name, "r");
    test_str(kind->inputs[3].name, "a");
    test_int(kind->inputs[2].type, TexScalar);
    test_bool(kind->inputs[3].fallback, true);
    test_int(kind->param_count, 0);
    test_int(kind->param_size, 0);
    test_str(kind->function, "box_tex_combine");

    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t uv = ecs_new(world);
    ecs_entity_t split = ecs_new(world);
    ecs_entity_t combine = ecs_new(world);
    ecs_set(world, uv, TexCoords, { .scale = 0 });
    ecs_set(world, split, TexSplit, { .input = uv, .channel = ChannelB });
    ecs_set(world, combine, TexCombine, { .g = split, .r_value = 0.25f, .b_value = 0.5f,
        .a_value = 1 });
    ecs_set(world, tex, TextureGraph, { .output = combine, .width = 8 });
    test_int(frame_status(world, tex)->state, TextureReady);
    const box_texture_graph_t *graph = graph_of(world, tex);
    test_int(graph->node_count, 3);
    test_int(graph->nodes[1].output, TexScalar);
    test_int(graph->nodes[1].param_size, 32);
    test_int(graph->nodes[2].output, TexVector);
    test_int(graph->nodes[2].param_size, 64);
    test_int(graph->nodes[2].inputs[0], -1);
    test_int(graph->nodes[2].inputs[1], 1);

    int32_t channel;
    float v[4];
    ecs_os_memcpy(&channel, graph->params + graph->nodes[1].params, 4);
    test_int(channel, ChannelB);
    ecs_os_memcpy(v, graph->params + graph->nodes[2].params, 16);
    test_flt(v[0], 0.25f);
    test_flt(v[1], 0);
    ecs_os_memcpy(v, graph->params + graph->nodes[2].params + 2 * 16, 16);
    test_flt(v[0], 0.5f);
    ecs_os_memcpy(v, graph->params + graph->nodes[2].params + 3 * 16, 16);
    test_flt(v[0], 1);

    ecs_fini(world);
}

void TextureGraph_coord_kinds(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexWarp), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexVector);
    test_int(kind->input_count, 3);
    test_str(kind->inputs[0].name, "coords");
    test_str(kind->inputs[1].name, "offset");
    test_str(kind->inputs[2].name, "amount");
    test_int(kind->inputs[0].type, TexVector);
    test_int(kind->inputs[1].type, TexVector);
    test_int(kind->inputs[2].type, TexScalar);
    test_bool(kind->inputs[0].fallback, false);
    test_bool(kind->inputs[1].fallback, true);
    test_bool(kind->inputs[2].fallback, true);
    test_int(kind->param_count, 0);
    test_int(kind->param_size, 0);
    test_str(kind->function, "box_tex_warp");
    test_bool(kind->neighbourhood, false);
    test_bool(kind->animated, false);

    kind = ecs_get(world, ecs_id(TexTransform), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexVector);
    test_int(kind->input_count, 2);
    test_str(kind->inputs[0].name, "coords");
    test_str(kind->inputs[1].name, "angle");
    test_int(kind->inputs[1].type, TexScalar);
    test_bool(kind->inputs[0].fallback, false);
    test_bool(kind->inputs[1].fallback, true);
    test_int(kind->param_count, 3);
    test_str(kind->params[0].name, "axis");
    test_int(kind->params[0].offset, 0);
    test_int(kind->params[1].offset, 16);
    test_int(kind->params[2].offset, 32);
    test_int(kind->param_size, 48);
    test_str(kind->function, "box_tex_transform");
    test_bool(kind->neighbourhood, false);

    ecs_fini(world);
}

void TextureGraph_combine_rejects_vector(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_entity_t uv = ecs_entity(world, { .name = "uv" });
    ecs_entity_t split = ecs_entity(world, { .name = "split" });
    ecs_entity_t math = ecs_entity(world, { .name = "math" });
    ecs_entity_t combine = ecs_entity(world, { .name = "combine" });
    ecs_set(world, uv, TexCoords, { .scale = 0 });
    ecs_set(world, split, TexSplit, { .input = uv });
    ecs_set(world, math, TexSin, { .input = split });
    ecs_set(world, combine, TexCombine, { .r = math, .g = split });
    ecs_set(world, tex, TextureGraph, { .output = combine, .width = 8 });
    test_int(frame_status(world, tex)->state, TextureReady);

    ecs_set(world, math, TexSin, { .input = uv });
    ecs_log_set_level(-4);
    frame_status(world, tex);
    test_failed(world, tex, "node 'math' outputs Vector", "input 'r' of node 'combine'");

    ecs_fini(world);
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

void TextureGraph_coord_kinds_sent(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    run_file(world, GRAPH_COORDS);
    ecs_progress(world, 0);
    box_render_sync(world);

    /* A coords input without a node is the domain coordinates, the others their constants */
    const box_render_texture_node_kind_t *k = box_render_null_texture_node_kind(null,
        (uint32_t)kind_id(world, ecs_id(TexWarp)));
    test_assert(k != NULL);
    test_int(k->coords_inputs, 1);
    k = box_render_null_texture_node_kind(null, (uint32_t)kind_id(world, ecs_id(TexTransform)));
    test_assert(k != NULL);
    test_int(k->coords_inputs, 1);
    k = box_render_null_texture_node_kind(null, (uint32_t)kind_id(world, ecs_id(TexMultiply)));
    test_assert(k != NULL);
    test_int(k->coords_inputs, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void TextureGraph_time_into_vector_fails(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t time = ecs_entity(world, { .name = "time" });
    ecs_entity_t split = ecs_entity(world, { .name = "split" });
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_set(world, time, TexTime, { .scale = 1 });
    ecs_set(world, split, TexSplit, { .input = time });
    ecs_set(world, tex, TextureGraph, { .output = split, .width = 8 });
    ecs_log_set_level(-4);
    frame_status(world, tex);
    ecs_log_set_level(-1);
    test_failed(world, tex, "node 'time' outputs Scalar", "input 'input' of node 'split'");

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

void TextureGraph_coord_nodes_script(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    run_file(world, GRAPH_COORDS);
    ecs_entity_t tex = ecs_lookup(world, "graph_coords");
    ecs_entity_t uv = ecs_lookup(world, "graph_coords.uv");
    ecs_entity_t turned = ecs_lookup(world, "graph_coords.turned");
    ecs_entity_t warped = ecs_lookup(world, "graph_coords.warped");
    test_assert(tex && uv && turned && warped);
    test_assert(ecs_get(world, turned, TexTransform)->coords == uv);
    test_assert(ecs_get(world, warped, TexWarp)->coords == turned);

    const TextureStatus *status = frame_status(world, tex);
    test_int(status->state, TextureReady);
    test_assert(status->error == NULL);

    const box_texture_graph_t *graph = graph_of(world, tex);
    test_int(graph->node_count, 3);
    test_assert(graph->nodes[0].entity == uv);
    test_assert(graph->nodes[1].entity == turned);
    test_assert(graph->nodes[2].entity == warped);
    test_int(graph->nodes[1].kind, kind_id(world, ecs_id(TexTransform)));
    test_int(graph->nodes[2].kind, kind_id(world, ecs_id(TexWarp)));
    test_int(graph->nodes[1].inputs[0], 0);
    test_int(graph->nodes[1].inputs[1], -1);
    test_int(graph->nodes[2].inputs[0], 1);
    test_int(graph->nodes[2].inputs[1], -1);
    test_int(graph->nodes[2].inputs[2], -1);

    /* The transform: its scale, then the angle without a node */
    const uint8_t *p = graph->params + graph->nodes[1].params;
    float value;
    test_int(graph->nodes[1].param_size, 48 + 2 * 16);
    ecs_os_memcpy(&value, p + 32, ECS_SIZEOF(float));
    test_flt(value, 2);
    ecs_os_memcpy(&value, p + 48 + 16, ECS_SIZEOF(float));
    test_flt(value, 90);

    /* The warp: no parameters, then the offset and the amount without a node */
    p = graph->params + graph->nodes[2].params;
    test_int(graph->nodes[2].param_size, 3 * 16);
    ecs_os_memcpy(&value, p + 16, ECS_SIZEOF(float));
    test_flt(value, 0.7f);
    ecs_os_memcpy(&value, p + 32, ECS_SIZEOF(float));
    test_flt(value, 0.5f);

    ecs_fini(world);
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

/** Get the parameter bytes of the node of an entity in the compiled graph of a texture.
 *
 * @param world The world
 * @param tex The texture entity
 * @param e The node entity
 * @param size Receives the bytes of the node
 * @return Its bytes
 */
static const uint8_t* node_params(
    ecs_world_t *world,
    ecs_entity_t tex,
    ecs_entity_t e,
    int32_t *size)
{
    const box_texture_graph_t *g = graph_of(world, tex);
    const uint8_t *result = NULL;
    int32_t i;
    for (i = 0; i < g->node_count; i ++) {
        if (g->nodes[i].entity == e) {
            result = g->params + g->nodes[i].params;
            *size = g->nodes[i].param_size;
        }
    }

    test_assert(result != NULL);

    return result;
}

void TextureGraph_ramp_curve_kinds(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexRamp), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexVector);
    test_int(kind->input_count, 1);
    test_str(kind->inputs[0].name, "input");
    test_int(kind->inputs[0].type, TexScalar);
    test_bool(kind->inputs[0].fallback, true);
    test_int(kind->param_count, 0);
    test_str(kind->array, "keys");
    test_int(kind->array_max, BOX_TEXTURE_RAMP_KEYS);
    test_int(kind->array_header, 0);
    test_int(kind->param_size, 16);
    test_str(kind->function, "box_tex_ramp");

    kind = ecs_get(world, ecs_id(TexCurve), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexScalar);
    test_int(kind->inputs[0].type, TexScalar);
    test_str(kind->array, "curve.keys");
    test_int(kind->array_max, BOX_TEXTURE_CURVE_KEYS);
    test_str(kind->function, "box_tex_curve");

    kind = ecs_get(world, ecs_id(TexMultiply), TextureNodeKind);
    test_assert(kind->array == NULL);
    test_int(kind->array_max, 0);
    test_int(kind->array_header, -1);

    box_textures_t *t = box_textures_get(world);
    const box_texture_node_kind_t *k = box_texture_node_kind(t,
        kind_id(world, ecs_id(TexRamp)));
    test_int(k->node_size, 16 + 16 + BOX_TEXTURE_RAMP_KEYS * 16);

    ecs_fini(world);
}

void TextureGraph_ramp_keys_layout(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_entity_t uv = ecs_entity(world, { .name = "uv" });
    ecs_entity_t ramp = ecs_entity(world, { .name = "ramp" });
    ecs_entity_t math = ecs_entity(world, { .name = "math" });
    ecs_set(world, uv, TexCoords, { .scale = 0 });
    ecs_set(world, ramp, TexRamp, { .input_value = 0.25f,
        .keys = BOX_VEC_VIEW(TextureGenKey, {0.2f, {255, 0, 51}}) });
    ecs_set(world, math, TexMultiply, { .a = uv, .b = ramp });
    ecs_set(world, tex, TextureGraph, { .output = math, .width = 8 });
    test_int(frame_status(world, tex)->state, TextureReady);
    uint64_t structure = graph_of(world, tex)->structure_hash;
    uint64_t params = graph_of(world, tex)->param_hash;
    int32_t total = graph_of(world, tex)->param_size, size = 0;
    const uint8_t *bytes = node_params(world, tex, ramp, &size);
    const box_texture_graph_t *g = graph_of(world, tex);
    int32_t offset = (int32_t)(bytes - g->params);
    const uint32_t *header = (const uint32_t*)bytes;
    const float *input = (const float*)(bytes + 16);
    const float *keys = (const float*)(bytes + 32);
    test_int(size, 32 + BOX_TEXTURE_RAMP_KEYS * 16);
    test_int(header[0], (offset + 32) / 16);
    test_int(header[1], 1);
    test_flt(input[0], 0.25f);
    test_flt(keys[0], 0.2f);
    test_flt(keys[1], 1.0f);
    test_flt(keys[2], 0.0f);
    test_flt(keys[3], 0.2f);
    test_flt(keys[4], 0.0f);

    ecs_set(world, ramp, TexRamp, { .input_value = 0.25f,
        .keys = BOX_VEC_VIEW(TextureGenKey, {0.2f, {255, 0, 51}}, {0.8f, {0, 255, 0}}) });
    test_int(frame_status(world, tex)->revision, 2);
    test_assert(graph_of(world, tex)->structure_hash == structure);
    test_assert(graph_of(world, tex)->param_hash != params);
    test_int(graph_of(world, tex)->param_size, total);
    bytes = node_params(world, tex, ramp, &size);
    header = (const uint32_t*)bytes;
    keys = (const float*)(bytes + 32);
    test_int(header[1], 2);
    test_flt(keys[4], 0.8f);
    test_flt(keys[6], 1.0f);
    params = graph_of(world, tex)->param_hash;

    ecs_set(world, ramp, TexRamp, { .input_value = 0.25f });
    test_int(frame_status(world, tex)->revision, 3);
    test_assert(graph_of(world, tex)->structure_hash == structure);
    test_assert(graph_of(world, tex)->param_hash != params);
    bytes = node_params(world, tex, ramp, &size);
    test_int(((const uint32_t*)bytes)[1], 0);
    test_flt(((const float*)(bytes + 32))[0], 0.0f);

    ecs_fini(world);
}

void TextureGraph_ramp_too_many_keys(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_entity_t ramp = ecs_entity(world, { .name = "ramp" });
    TexRamp *value = ecs_ensure(world, ramp, TexRamp);
    int32_t i;
    ecs_vec_init_if_t(&value->keys, TextureGenKey);
    for (i = 0; i <= BOX_TEXTURE_RAMP_KEYS; i ++) {
        *ecs_vec_append_t(NULL, &value->keys, TextureGenKey) = (TextureGenKey){
            (float)i / BOX_TEXTURE_RAMP_KEYS, {0, 0, 0}};
    }

    ecs_modified(world, ramp, TexRamp);
    ecs_set(world, tex, TextureGraph, { .output = ramp });
    frame_status(world, tex);
    test_failed(world, tex, "ramp", "33 elements in keys, more than 32");

    value = ecs_get_mut(world, ramp, TexRamp);
    ecs_vec_remove_last(&value->keys);
    ecs_modified(world, ramp, TexRamp);
    test_int(frame_status(world, tex)->state, TextureReady);

    ecs_fini(world);
}

void TextureGraph_curve_keys_layout(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_entity_t curve = ecs_entity(world, { .name = "curve" });
    ecs_entity_t ramp = ecs_entity(world, { .name = "ramp" });
    ecs_set(world, curve, TexCurve, { .input_value = 0.5f, .curve = { .kind = CurveCatmullRom,
        .keys = BOX_CURVE_KEYS({0, 0.2f}, {0.4f, 1}, {1, 0.5f}) } });
    ecs_set(world, ramp, TexRamp, { .input = curve });
    ecs_set(world, tex, TextureGraph, { .output = ramp });
    test_int(frame_status(world, tex)->state, TextureReady);
    uint64_t structure = graph_of(world, tex)->structure_hash;
    uint64_t params = graph_of(world, tex)->param_hash;
    int32_t size = 0, i;
    const uint8_t *bytes = node_params(world, tex, curve, &size);
    const Curve *c = &ecs_get(world, curve, TexCurve)->curve;
    const float *keys = (const float*)(bytes + 32);
    test_int(size, 32 + BOX_TEXTURE_CURVE_KEYS * 16);
    test_int(((const uint32_t*)bytes)[1], 3);
    for (i = 0; i < 2; i ++) {
        float c1, c2;
        box_curve_span_controls(c, i, &c1, &c2);
        test_flt(keys[i * 4], ecs_vec_get_t(&c->keys, CurveKey, i)->time);
        test_flt(keys[i * 4 + 1], ecs_vec_get_t(&c->keys, CurveKey, i)->value);
        test_flt(keys[i * 4 + 2], c1);
        test_flt(keys[i * 4 + 3], c2);
    }

    test_flt(keys[8], 1.0f);
    test_flt(keys[9], 0.5f);

    ecs_set(world, curve, TexCurve, { .input_value = 0.5f, .curve = { .kind = CurveBezier,
        .keys = BOX_CURVE_KEYS({0, 0.2f, 0, 1}, {0.4f, 1, -1, 0}, {0.7f, 0.1f}, {1, 0.5f}) } });
    test_int(frame_status(world, tex)->revision, 2);
    test_assert(graph_of(world, tex)->structure_hash == structure);
    test_assert(graph_of(world, tex)->param_hash != params);
    bytes = node_params(world, tex, curve, &size);
    test_int(((const uint32_t*)bytes)[1], 4);

    ecs_fini(world);
}

void TextureGraph_curve_invalid(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_entity_t curve = ecs_entity(world, { .name = "curve" });
    ecs_entity_t ramp = ecs_entity(world, { .name = "ramp" });
    ecs_set(world, curve, TexCurve, { .curve = {
        .keys = BOX_CURVE_KEYS({0.5f, 0}, {0.5f, 1}) } });
    ecs_set(world, ramp, TexRamp, { .input = curve });
    ecs_set(world, tex, TextureGraph, { .output = ramp });
    frame_status(world, tex);
    test_failed(world, tex, "'curve'", "key 1");

    ecs_fini(world);
}

void TextureGraph_array_register_invalid(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t c = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "TestArray" }),
        .type.size = ECS_SIZEOF(TexRamp),
        .type.alignment = ECS_ALIGNOF(TexRamp)
    });
    ecs_log_set_level(-4);
    test_int(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .array = { "keys", offsetof(TexRamp, keys), 8, 0 },
        .function = "f",
        .source = "fn f() {}"
    }), 0);
    test_int(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .array = { "keys", offsetof(TexRamp, keys), 20, 4 },
        .function = "f",
        .source = "fn f() {}"
    }), 0);
    test_int(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .params = {
            { "a", 0, 16, BOX_TEXTURE_PARAM_F32 }, { "b", 0, 16, BOX_TEXTURE_PARAM_F32 },
            { "c", 0, 16, BOX_TEXTURE_PARAM_F32 }, { "d", 0, 16, BOX_TEXTURE_PARAM_F32 }
        },
        .array = { "keys", offsetof(TexRamp, keys), 8, 4 },
        .function = "f",
        .source = "fn f() {}"
    }), 0);
    ecs_log_set_level(-1);
    test_assert(box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = c,
        .params = {{ "a", 0, 4, BOX_TEXTURE_PARAM_F32 }},
        .array = { "keys", offsetof(TexRamp, keys), 8, 4 },
        .function = "f",
        .source = "fn f() {}"
    }) > 0);
    const TextureNodeKind *kind = ecs_get(world, c, TextureNodeKind);
    test_int(kind->array_header, 16);
    test_int(kind->param_size, 32);
    test_int(kind->array_max, 4);

    ecs_fini(world);
}

void TextureGraph_blocks_kind(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    test_assert(kind_id(world, ecs_id(TexBlocks)) > 0);
    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexBlocks), TextureNodeKind);
    test_int(kind->output, TexVector);
    test_int(kind->input_count, 1);
    test_str(kind->inputs[0].name, "coords");
    test_int(kind->inputs[0].type, TexVector);
    test_bool(kind->inputs[0].fallback, false);
    test_int(kind->param_count, 10);
    test_str(kind->params[0].name, "courses");
    test_str(kind->params[9].name, "layer_seed");
    test_int(kind->params[9].offset, 36);
    test_int(kind->params[9].size, 4);
    test_int(kind->param_size, 48);
    test_str(kind->function, "box_tex_blocks");
    test_bool(kind->plane, true);
    test_bool(kind->neighbourhood, false);

    ecs_fini(world);
}

void TextureGraph_blocks_plane(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_entity_t blocks = ecs_entity(world, { .name = "blocks" });
    ecs_set(world, blocks, TexBlocks, { .courses = 4, .per_course = 3, .gap = 0.02f });
    ecs_set(world, tex, TextureGraph, { .output = blocks, .width = 32, .height = 32 });
    const TextureStatus *status = frame_status(world, tex);
    test_int(status->state, TextureReady);
    test_assert(graph_of(world, tex)->node_count == 1);

    ecs_fini(world);
}

void TextureGraph_blocks_sphere_error(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_entity_t blocks = ecs_entity(world, { .name = "blocks" });
    ecs_set(world, blocks, TexBlocks, { .courses = 4, .per_course = 3, .gap = 0.02f });
    ecs_log_set_level(-4);
    ecs_set(world, tex, TextureGraph, { .output = blocks, .width = 32, .height = 16,
        .domain = DomainSphere });
    frame_status(world, tex);
    test_failed(world, tex, "'blocks'", "DomainPlane");
    test_failed(world, tex, "box.texture.nodes.Blocks", "'graph'");

    ecs_set(world, tex, TextureGraph, { .output = blocks, .width = 32, .height = 16,
        .domain = DomainPlane });
    test_int(frame_status(world, tex)->state, TextureReady);

    ecs_fini(world);
}

void TextureGraph_noise_kind(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    const TextureNodeKind *kind = ecs_get(world, ecs_id(TexNoise), TextureNodeKind);
    test_assert(kind != NULL);
    test_int(kind->output, TexAny);
    test_int(kind->input_count, 2);
    test_str(kind->inputs[0].name, "coords");
    test_int(kind->inputs[0].type, TexVector);
    test_bool(kind->inputs[0].fallback, false);
    test_bool(kind->inputs[0].coords, true);
    test_str(kind->inputs[1].name, "evolve");
    test_int(kind->inputs[1].type, TexScalar);
    test_bool(kind->inputs[1].fallback, false);
    test_int(kind->param_count, 8);
    test_str(kind->params[1].name, "octaves");
    test_int(kind->param_size, 32);
    test_str(kind->function, "box_tex_noise");
    test_bool(kind->neighbourhood, false);
    test_bool(kind->animated, false);

    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_entity_t noise = ecs_entity(world, { .name = "noise" });
    ecs_set(world, noise, TexNoise, { .frequency = 4, .octaves = 3 });
    ecs_set(world, tex, TextureGraph, { .output = noise, .width = 8 });
    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_texture_node_kind_t *k = box_render_null_texture_node_kind(null,
        (uint32_t)kind->id);
    test_assert(k != NULL);
    test_int(k->coords_inputs, 1);
    test_assert(strstr(box_render_texture_node_kind_source(k), "fn box_tex_noise(") != NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void TextureGraph_noise_channels_type(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_entity_t noise = ecs_entity(world, { .name = "noise" });
    ecs_entity_t split = ecs_entity(world, { .name = "split" });
    ecs_set(world, noise, TexNoise, { .channels = 1 });
    ecs_set(world, split, TexSplit, { .input = noise });
    ecs_set(world, tex, TextureGraph, { .output = noise, .width = 8 });
    test_int(frame_status(world, tex)->state, TextureReady);
    test_int(graph_of(world, tex)->nodes[0].output, TexScalar);
    uint64_t structure = graph_of(world, tex)->structure_hash;

    ecs_set(world, tex, TextureGraph, { .output = split, .width = 8 });
    ecs_log_set_level(-4);
    frame_status(world, tex);
    test_failed(world, tex, "node 'noise' outputs Scalar", "input 'input' of node 'split'");

    ecs_set(world, noise, TexNoise, { .channels = 3 });
    test_int(frame_status(world, tex)->state, TextureReady);
    test_int(graph_of(world, tex)->nodes[0].output, TexVector);

    ecs_set(world, noise, TexNoise, { .channels = 4 });
    test_int(frame_status(world, tex)->state, TextureReady);
    test_int(graph_of(world, tex)->nodes[0].output, TexVector);

    ecs_set(world, tex, TextureGraph, { .output = noise, .width = 8 });
    test_int(frame_status(world, tex)->state, TextureReady);
    test_assert(graph_of(world, tex)->structure_hash != structure);

    ecs_fini(world);
}

void TextureGraph_noise_params_keep_structure(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_entity(world, { .name = "graph" });
    ecs_entity_t noise = ecs_entity(world, { .name = "noise" });
    ecs_set(world, noise, TexNoise, { .frequency = 4, .octaves = 1 });
    ecs_set(world, tex, TextureGraph, { .output = noise, .width = 8 });
    test_int(frame_status(world, tex)->revision, 1);
    uint64_t structure = graph_of(world, tex)->structure_hash;
    uint64_t params = graph_of(world, tex)->param_hash;

    ecs_set(world, noise, TexNoise, { .frequency = 4, .octaves = 7 });
    test_int(frame_status(world, tex)->revision, 2);
    test_assert(graph_of(world, tex)->structure_hash == structure);
    test_assert(graph_of(world, tex)->param_hash != params);
    params = graph_of(world, tex)->param_hash;

    ecs_set(world, noise, TexNoise, { .frequency = 9, .octaves = 7, .seed = 3,
        .fractal = NoiseRidged, .offset = 0.8f, .lacunarity = 2.5f, .gain = 0.4f });
    test_int(frame_status(world, tex)->revision, 3);
    test_assert(graph_of(world, tex)->structure_hash == structure);
    test_assert(graph_of(world, tex)->param_hash != params);

    ecs_fini(world);
}

void TextureGraph_noise_script(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    run_file(world, GRAPH_NOISE);
    ecs_entity_t tex = ecs_lookup(world, "clouds");
    ecs_entity_t warp = ecs_lookup(world, "clouds.warp");
    ecs_entity_t moved = ecs_lookup(world, "clouds.moved");
    ecs_entity_t noise = ecs_lookup(world, "clouds.noise");
    test_assert(tex && warp && moved && noise);
    const TexNoise *n = ecs_get(world, noise, TexNoise);
    test_assert(n != NULL);
    test_assert(n->coords == moved);
    test_assert(n->evolve == 0);
    test_int(n->octaves, 5);
    test_int(n->seed, 3);
    test_int(n->fractal, NoiseBillow);
    test_flt(n->lacunarity, 2.5f);

    test_int(frame_status(world, tex)->state, TextureReady);
    const box_texture_graph_t *graph = graph_of(world, tex);
    test_int(graph->node_count, 4);
    test_assert(graph->nodes[3].entity == noise);
    test_int(graph->nodes[3].output, TexScalar);
    test_int(graph->nodes[3].inputs[1], -1);
    int32_t i, warp_index = -1;
    for (i = 0; i < 3; i ++) {
        if (graph->nodes[i].entity == warp) {
            warp_index = i;
        }
    }

    test_assert(warp_index >= 0);
    test_int(graph->nodes[warp_index].output, TexVector);

    int32_t octaves, fractal;
    float frequency;
    const uint8_t *params = graph->params + graph->nodes[3].params;
    ecs_os_memcpy(&frequency, params, 4);
    ecs_os_memcpy(&octaves, params + 4, 4);
    ecs_os_memcpy(&fractal, params + 20, 4);
    test_flt(frequency, 6.0f);
    test_int(octaves, 5);
    test_int(fractal, NoiseBillow);
    test_int(graph->nodes[3].param_size, 32 + 2 * 16);

    ecs_fini(world);
}

/** Load the planet templates, declare a World and a Giant and run frames until the graphs that
 * sample the biome table are built.
 *
 * @param world The world
 */
static void planet_load(
    ecs_world_t *world)
{
    run_file(world, GRAPH_PLANET);
    test_int(ecs_script_run(world, "planets",
        "planet_graph.World earth(seed: 7, water: 0.6, temp: 0.5, bio: 0.8, size: 64)\n"
        "planet_graph.Giant giant(seed: 5, size: 64)\n", NULL), 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

/** Get the ready texture of an entity of the planets by path.
 *
 * @param world The world
 * @param path The path of the texture entity
 * @return The texture entity
 */
static ecs_entity_t planet_map(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t e = ecs_lookup(world, path);
    test_assert(e != 0);
    const TextureStatus *status = ecs_get(world, e, TextureStatus);
    test_assert(status != NULL);
    if (status->state != TextureReady) {
        printf("%s: %s\n", path, status->error ? status->error : "not ready");
    }

    test_int(status->state, TextureReady);

    return e;
}

/** Get the index of the first node of a kind in a compiled graph.
 *
 * @param world The world
 * @param graph The graph
 * @param component The node component of the kind
 * @return The index, -1 when no node has the kind
 */
static int32_t planet_node(
    ecs_world_t *world,
    const box_texture_graph_t *graph,
    ecs_entity_t component)
{
    int32_t i, result = -1, id = kind_id(world, component);
    for (i = graph->node_count - 1; i >= 0; i --) {
        if (graph->nodes[i].kind == id) {
            result = i;
        }
    }

    return result;
}

/** Get the index of the node of an entity in a compiled graph.
 *
 * @param graph The graph
 * @param node The node entity
 * @return The index, -1 when the graph does not reach it
 */
static int32_t planet_entity(
    const box_texture_graph_t *graph,
    ecs_entity_t node)
{
    int32_t i, result = -1;
    for (i = 0; i < graph->node_count && result < 0; i ++) {
        if (graph->nodes[i].entity == node) {
            result = i;
        }
    }

    return result;
}

void TextureGraph_planet_script(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    planet_load(world);
    ecs_entity_t table = planet_map(world, "earth.table");
    ecs_entity_t albedo = planet_map(world, "earth.albedo");
    ecs_entity_t normals = planet_map(world, "earth.normals");
    ecs_entity_t clouds = planet_map(world, "earth.clouds");
    ecs_entity_t giant = planet_map(world, "giant.albedo");
    ecs_entity_t bands = planet_map(world, "giant.bands");
    ecs_entity_t relief = ecs_lookup(world, "earth.relief");
    test_assert(relief != 0);

    const box_texture_graph_t *g = graph_of(world, table);
    test_int(g->domain, DomainPlane);
    test_int(g->width, 32);
    test_int(g->height, 32);
    test_assert(planet_node(world, g, ecs_id(TexCurve)) >= 0);

    g = graph_of(world, albedo);
    test_int(g->domain, DomainSphere);
    test_int(g->width, 64);
    test_int(g->height, 32);
    test_int(g->format, GraphColor);
    test_assert(g->rate == 0);
    int32_t lut = planet_node(world, g, ecs_id(TexLut));
    test_assert(lut >= 0);
    test_assert(g->nodes[lut].texture >= 0);
    test_assert(g->textures[g->nodes[lut].texture].entity == table);
    test_assert(planet_node(world, g, ecs_id(TexSlope)) >= 0);
    test_assert(planet_node(world, g, ecs_id(TexBlur)) >= 0);
    test_assert(planet_node(world, g, ecs_id(TexWarp)) >= 0);
    test_assert(planet_node(world, g, ecs_id(TexTime)) < 0);
    test_assert(planet_entity(g, relief) >= 0);

    g = graph_of(world, normals);
    test_int(g->domain, DomainSphere);
    test_int(g->format, GraphLinear);
    test_int(g->nodes[g->node_count - 1].kind, kind_id(world, ecs_id(TexNormal)));
    test_assert(planet_entity(g, relief) >= 0);

    g = graph_of(world, clouds);
    test_int(g->domain, DomainSphere);
    test_flt(g->rate, 3);
    int32_t time = planet_node(world, g, ecs_id(TexTime));
    test_assert(time >= 0);
    int32_t i, evolving = 0, noise_id = kind_id(world, ecs_id(TexNoise));
    for (i = 0; i < g->node_count; i ++) {
        if (g->nodes[i].kind == noise_id && g->nodes[i].inputs[1] == time) {
            evolving ++;
        }
    }

    test_int(evolving, 1);
    test_assert(planet_node(world, g, ecs_id(TexCurve)) >= 0);

    g = graph_of(world, bands);
    test_int(g->domain, DomainSphere);
    test_assert(g->rate == 0);
    test_assert(planet_node(world, g, ecs_id(TexTime)) < 0);
    test_assert(planet_node(world, g, ecs_id(TexCells)) >= 0);
    test_assert(planet_node(world, g, ecs_id(TexRamp)) >= 0);

    g = graph_of(world, giant);
    test_int(g->domain, DomainSphere);
    test_flt(g->rate, 12);
    test_assert(planet_node(world, g, ecs_id(TexTime)) >= 0);
    test_assert(planet_node(world, g, ecs_id(TexTransform)) >= 0);
    int32_t image = planet_node(world, g, ecs_id(TexImage));
    test_assert(image >= 0);
    test_assert(g->nodes[image].texture >= 0);
    test_assert(g->textures[g->nodes[image].texture].entity == bands);

    ecs_fini(world);
}

void TextureGraph_planet_props_keep_structure(void) {
    // materials/textures
    const char *paths[] = {"earth.table", "earth.albedo", "earth.normals", "earth.clouds"};
    uint64_t structure[4], params[4];
    int32_t i;
    ecs_world_t *world = world_new();
    planet_load(world);
    for (i = 0; i < 4; i ++) {
        const box_texture_graph_t *g = graph_of(world, planet_map(world, paths[i]));
        structure[i] = g->structure_hash;
        params[i] = g->param_hash;
    }

    ecs_entity_t earth = ecs_lookup(world, "earth");
    ecs_entity_t tmpl = ecs_lookup(world, "planet_graph.World");
    ecs_entity_t relief = ecs_lookup(world, "earth.relief");
    test_assert(earth != 0 && tmpl != 0 && relief != 0);
    const ecs_type_info_t *ti = ecs_get_type_info(world, tmpl);
    test_assert(ti != NULL);
    void *ptr = ecs_os_malloc(ti->size);
    ecs_os_memcpy(ptr, ecs_get_id(world, earth, tmpl), ti->size);
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, tmpl, ptr);
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_member(&cur, "seed"), 0);
    test_int(ecs_meta_set_int(&cur, 12), 0);
    test_int(ecs_meta_member(&cur, "water"), 0);
    test_int(ecs_meta_set_float(&cur, 0.3), 0);
    test_int(ecs_meta_member(&cur, "temp"), 0);
    test_int(ecs_meta_set_float(&cur, 0.7), 0);
    test_int(ecs_meta_member(&cur, "bio"), 0);
    test_int(ecs_meta_set_float(&cur, 0.4), 0);
    ecs_set_id(world, earth, tmpl, ti->size, ptr);
    ecs_os_free(ptr);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    for (i = 0; i < 4; i ++) {
        const box_texture_graph_t *g = graph_of(world, planet_map(world, paths[i]));
        test_assert(g->structure_hash == structure[i]);
        test_assert(g->param_hash != params[i]);
    }

    test_assert(ecs_is_alive(world, relief));
    test_assert(ecs_lookup(world, "earth.relief") == relief);

    ecs_fini(world);
}

/** Load the nebula template, declare a Nebula and run frames until its graph is built.
 *
 * @param world The world
 */
static void nebula_load(
    ecs_world_t *world)
{
    run_file(world, GRAPH_NEBULA);
    test_int(ecs_script_run(world, "nebula",
        "nebula_graph.Nebula sky(seed: 5, size: 64)\n", NULL), 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

void TextureGraph_nebula_script(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    nebula_load(world);
    const box_texture_graph_t *g = graph_of(world, planet_map(world, "sky.neb"));
    test_int(g->domain, DomainSphere);
    test_int(g->format, GraphHdr);
    test_int(g->width, 64);
    test_int(g->height, 32);
    test_assert(g->rate == 0);
    test_assert(planet_node(world, g, ecs_id(TexTime)) < 0);
    test_assert(planet_node(world, g, ecs_id(TexWarp)) >= 0);
    test_assert(planet_node(world, g, ecs_id(TexRamp)) >= 0);
    test_assert(planet_node(world, g, ecs_id(TexSmoothstep)) >= 0);
    test_assert(planet_node(world, g, ecs_id(TexCoords)) >= 0);
    test_assert(planet_node(world, g, ecs_id(TexSplit)) >= 0);
    test_int(g->nodes[g->node_count - 1].kind, kind_id(world, ecs_id(TexAdd)));

    ecs_fini(world);
}

void TextureGraph_nebula_props_keep_structure(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    nebula_load(world);
    const box_texture_graph_t *g = graph_of(world, planet_map(world, "sky.neb"));
    uint64_t structure = g->structure_hash, params = g->param_hash;
    ecs_entity_t gas = ecs_lookup(world, "sky.neb.n_gas");
    test_assert(gas != 0);

    ecs_entity_t sky = ecs_lookup(world, "sky");
    ecs_entity_t tmpl = ecs_lookup(world, "nebula_graph.Nebula");
    test_assert(sky != 0 && tmpl != 0);
    const ecs_type_info_t *ti = ecs_get_type_info(world, tmpl);
    test_assert(ti != NULL);
    void *ptr = ecs_os_malloc(ti->size);
    ecs_os_memcpy(ptr, ecs_get_id(world, sky, tmpl), ti->size);
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, tmpl, ptr);
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_member(&cur, "seed"), 0);
    test_int(ecs_meta_set_int(&cur, 12), 0);
    test_int(ecs_meta_member(&cur, "frequency"), 0);
    test_int(ecs_meta_set_float(&cur, 2.2), 0);
    test_int(ecs_meta_member(&cur, "warp"), 0);
    test_int(ecs_meta_set_float(&cur, 1.1), 0);
    test_int(ecs_meta_member(&cur, "cover"), 0);
    test_int(ecs_meta_set_float(&cur, 0.8), 0);
    test_int(ecs_meta_member(&cur, "band"), 0);
    test_int(ecs_meta_set_float(&cur, 0.1), 0);
    test_int(ecs_meta_member(&cur, "band_width"), 0);
    test_int(ecs_meta_set_float(&cur, 20), 0);
    ecs_set_id(world, sky, tmpl, ti->size, ptr);
    ecs_os_free(ptr);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    g = graph_of(world, planet_map(world, "sky.neb"));
    test_assert(g->structure_hash == structure);
    test_assert(g->param_hash != params);
    test_assert(ecs_is_alive(world, gas));
    test_assert(ecs_lookup(world, "sky.neb.n_gas") == gas);

    ecs_fini(world);
}

void TextureGraph_node_ctor_defaults(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, TexCoords);
    ecs_add(world, e, TexTime);
    ecs_add(world, e, TexTransform);
    ecs_add(world, e, TexNormal);
    ecs_add(world, e, TexSlope);
    ecs_add(world, e, TexNoise);
    ecs_add(world, e, TexCells);

    test_flt(ecs_get(world, e, TexCoords)->scale, 1);
    test_flt(ecs_get(world, e, TexTime)->scale, 1);
    test_flt(ecs_get(world, e, TexTime)->offset, 0);
    const TexTransform *transform = ecs_get(world, e, TexTransform);
    test_flt(transform->scale[0], 1);
    test_flt(transform->scale[1], 1);
    test_flt(transform->scale[2], 1);
    test_flt(ecs_get(world, e, TexNormal)->strength, 1);
    test_flt(ecs_get(world, e, TexSlope)->strength, 1);
    const TexNoise *noise = ecs_get(world, e, TexNoise);
    test_flt(noise->frequency, 4);
    test_int(noise->octaves, 1);
    test_flt(noise->lacunarity, 2);
    test_flt(noise->gain, 0.5f);
    test_flt(noise->offset, 1);
    const TexCells *cells = ecs_get(world, e, TexCells);
    test_int(cells->count, 64);
    test_flt(cells->jitter, 1);
    test_flt(cells->edge, 0.25f);

    ecs_fini(world);
}

void TextureGraph_script_member_default(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    run_file(world, GRAPH_DEFAULTS);
    ecs_entity_t uv = ecs_lookup(world, "defaults.uv");
    ecs_entity_t time = ecs_lookup(world, "defaults.time");
    ecs_entity_t moved = ecs_lookup(world, "defaults.moved");
    ecs_entity_t noise = ecs_lookup(world, "defaults.noise");
    ecs_entity_t cells = ecs_lookup(world, "defaults.cells");
    ecs_entity_t normal = ecs_lookup(world, "defaults.normal");
    ecs_entity_t slope = ecs_lookup(world, "defaults.slope");
    ecs_entity_t gradient = ecs_lookup(world, "defaults.gradient");
    test_assert(uv && time && moved && noise && cells && normal && slope && gradient);

    test_flt(ecs_get(world, uv, TexCoords)->scale, 1);
    test_flt(ecs_get(world, time, TexTime)->scale, 1);
    test_flt(ecs_get(world, time, TexTime)->offset, 3);
    const TexTransform *transform = ecs_get(world, moved, TexTransform);
    test_assert(transform->coords == uv);
    test_flt(transform->scale[0], 1);
    test_flt(transform->scale[1], 1);
    test_flt(transform->scale[2], 1);
    const TexNoise *n = ecs_get(world, noise, TexNoise);
    test_int(n->seed, 2);
    test_flt(n->frequency, 4);
    test_flt(n->lacunarity, 2);
    test_flt(n->gain, 0.5f);
    test_flt(n->offset, 1);
    const TexCells *c = ecs_get(world, cells, TexCells);
    test_int(c->seed, 4);
    test_int(c->count, 64);
    test_flt(c->jitter, 1);
    test_flt(c->edge, 0.25f);
    test_flt(ecs_get(world, normal, TexNormal)->strength, 1);
    test_flt(ecs_get(world, slope, TexSlope)->strength, 1);
    test_flt(ecs_get(world, gradient, TexGradient)->strength, 1);

    ecs_fini(world);
}

void TextureGraph_region_members_reflect(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    TextureGraph graph = {0};
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, ecs_id(TextureGraph), &graph);
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_member(&cur, "longitude_start"), 0);
    test_int(ecs_meta_set_float(&cur, 0.25), 0);
    test_int(ecs_meta_member(&cur, "longitude_end"), 0);
    test_int(ecs_meta_set_float(&cur, 1.0), 0);
    test_int(ecs_meta_member(&cur, "latitude_start"), 0);
    test_int(ecs_meta_set_float(&cur, -0.5), 0);
    test_int(ecs_meta_member(&cur, "latitude_end"), 0);
    test_int(ecs_meta_set_float(&cur, 0.75), 0);
    test_int(ecs_meta_pop(&cur), 0);
    test_flt(graph.longitude_start, 0.25f);
    test_flt(graph.longitude_end, 1.0f);
    test_flt(graph.latitude_start, -0.5f);
    test_flt(graph.latitude_end, 0.75f);

    char *json = ecs_ptr_to_json(world, ecs_id(TextureGraph), &graph);
    test_assert(json != NULL);
    test_assert(strstr(json, "\"longitude_start\"") != NULL);
    test_assert(strstr(json, "\"latitude_end\"") != NULL);
    ecs_os_free(json);

    ecs_fini(world);
}

void TextureGraph_region_default_whole_sphere(void) {
    // materials/textures
    const float pi = (float)GLM_PI;
    ecs_world_t *world = world_new();
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    test_int(frame_status(world, tex)->revision, 1);
    const box_texture_graph_t *g = graph_of(world, tex);
    test_flt(g->region[0], 0);
    test_flt(g->region[1], 0);
    test_flt(g->region[2], 0);
    test_flt(g->region[3], 0);

    ecs_set(world, tex, TextureGraph, {
        .output = math, .width = 32, .height = 16, .domain = DomainSphere });
    test_int(frame_status(world, tex)->revision, 2);
    g = graph_of(world, tex);
    test_flt(g->region[0], 0);
    test_flt(g->region[1], 2 * pi);
    test_flt(g->region[2], -pi / 2);
    test_flt(g->region[3], pi / 2);

    /* The full range spelled out is the default */
    ecs_set(world, tex, TextureGraph, {
        .output = math, .width = 32, .height = 16, .domain = DomainSphere,
        .longitude_start = 0, .longitude_end = 2 * pi,
        .latitude_start = -pi / 2, .latitude_end = pi / 2 });
    test_int(frame_status(world, tex)->revision, 2);

    /* Equal ranges anywhere are the whole of them */
    ecs_set(world, tex, TextureGraph, {
        .output = math, .width = 32, .height = 16, .domain = DomainSphere,
        .longitude_start = 1, .longitude_end = 1,
        .latitude_start = 0.5f, .latitude_end = 0.5f });
    test_int(frame_status(world, tex)->revision, 2);

    /* A longitude range is at most a turn, the latitudes stop at the poles */
    ecs_set(world, tex, TextureGraph, {
        .output = math, .width = 32, .height = 16, .domain = DomainSphere,
        .longitude_start = 1, .longitude_end = 10,
        .latitude_start = -3, .latitude_end = 0.25f });
    test_int(frame_status(world, tex)->revision, 3);
    g = graph_of(world, tex);
    test_flt(g->region[0], 1);
    test_flt(g->region[1], 1 + 2 * pi);
    test_flt(g->region[2], -pi / 2);
    test_flt(g->region[3], 0.25f);

    ecs_fini(world);
}

void TextureGraph_region_edit_keeps_structure(void) {
    // materials/textures
    box_render_null_t *null = NULL;
    ecs_world_t *world = world_null(&null);
    ecs_entity_t tex, uv, constant, math;
    make_graph(world, &tex, &uv, &constant, &math);
    ecs_set(world, tex, TextureGraph, {
        .output = math, .width = 32, .height = 16, .domain = DomainSphere });
    ecs_progress(world, 0);
    box_render_sync(world);
    uint64_t structure = graph_of(world, tex)->structure_hash;
    uint64_t params = graph_of(world, tex)->param_hash;
    uint32_t serial = entry_of(world, tex)->graph_serial;

    ecs_set(world, tex, TextureGraph, {
        .output = math, .width = 32, .height = 16, .domain = DomainSphere,
        .longitude_start = 0.5f, .longitude_end = 1.5f,
        .latitude_start = -0.25f, .latitude_end = 0.5f });
    ecs_progress(world, 0);
    box_render_sync(world);
    const box_texture_graph_t *g = graph_of(world, tex);
    test_assert(g->structure_hash == structure);
    test_assert(g->param_hash == params);
    test_assert(entry_of(world, tex)->graph_serial != serial);
    test_flt(g->region[0], 0.5f);
    test_flt(g->region[1], 1.5f);
    test_flt(g->region[2], -0.25f);
    test_flt(g->region[3], 0.5f);

    const box_render_texture_graph_t *desc = box_render_null_texture_graph(null,
        entry_of(world, tex)->handle);
    test_assert(desc != NULL);
    test_assert(desc->structure_hash == structure);
    test_flt(desc->region[0], 0.5f);
    test_flt(desc->region[1], 1.5f);
    test_flt(desc->region[2], -0.25f);
    test_flt(desc->region[3], 0.5f);

    /* On the plane the ranges are ignored */
    ecs_set(world, tex, TextureGraph, { .output = math, .width = 32, .height = 16 });
    ecs_progress(world, 0);
    serial = entry_of(world, tex)->graph_serial;
    ecs_set(world, tex, TextureGraph, {
        .output = math, .width = 32, .height = 16,
        .longitude_start = 0.5f, .longitude_end = 1.5f });
    ecs_progress(world, 0);
    test_int(entry_of(world, tex)->graph_serial, serial);
    test_flt(graph_of(world, tex)->region[1], 0);

    ecs_fini(world);
    box_render_null_free(null);
}
