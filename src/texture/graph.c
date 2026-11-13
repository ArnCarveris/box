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
#include <stdarg.h>

/** Most nodes of a graph */
#define BOX_TEXTURE_GRAPH_NODES (1024)

/** Offset basis of the 64 bit FNV-1a hash of the compiled graphs */
#define BOX_TEXTURE_GRAPH_FNV_BASIS (14695981039346656037ull)

/** Prime of the 64 bit FNV-1a hash of the compiled graphs */
#define BOX_TEXTURE_GRAPH_FNV_PRIME (1099511628211ull)

/** WGSL of the TexConstant node: its value */
static const char *box_texture_constant_wgsl =
    "fn box_tex_constant(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    return bitcast<vec4f>(p[0]);\n"
    "}\n";

/** WGSL of the TexCoords node: the coordinates times its scale */
static const char *box_texture_coords_wgsl =
    "fn box_tex_coords(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let scale = bitcast<f32>(p[0].x);\n"
    "    return vec4f(coords.xyz * select(scale, 1.0, scale == 0.0), coords.w);\n"
    "}\n";

/** WGSL of the TexMath node: the operation of its op on a, b and c */
static const char *box_texture_math_wgsl =
    "fn box_tex_math(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let a = i[0];\n"
    "    let b = i[1];\n"
    "    let c = i[2];\n"
    "    switch bitcast<i32>(p[0].x) {\n"
    "        case 1: { return a - b; }\n"
    "        case 2: { return a * b; }\n"
    "        case 3: { return min(a, b); }\n"
    "        case 4: { return max(a, b); }\n"
    "        case 5: { return mix(a, b, c); }\n"
    "        case 6: { return clamp(a, b, c); }\n"
    "        default: { return a + b; }\n"
    "    }\n"
    "}\n";

/** WGSL of the TexNoise node: fbm, ridged or billow gradient noise per channel.
 *
 * The lattice is four dimensional, with 32 gradients of three components +-1 and one 0. A point
 * with w 0 needs only the eight corners at w 0, so without evolve the noise is three dimensional.
 *
 * At w 0 that noise is the evolved noise, so it continues into it. On the plane x and y wrap
 * with the whole number of cells across of each octave. */
static const char *box_texture_noise_wgsl =
    "// Gradient of a corner of the noise lattice dotted with the offset d from it\n"
    "fn box_tex_noise_grad(h: u32, d: vec4f) -> f32 {\n"
    "    let s = vec3f(select(1.0, -1.0, (h & 1u) != 0u), select(1.0, -1.0, (h & 2u) != 0u),\n"
    "        select(1.0, -1.0, (h & 4u) != 0u));\n"
    "    switch (h >> 3u) & 3u {\n"
    "        case 0u: { return dot(s, d.yzw); }\n"
    "        case 1u: { return dot(s, d.xzw); }\n"
    "        case 2u: { return dot(s, d.xyw); }\n"
    "        default: { return dot(s, d.xyz); }\n"
    "    }\n"
    "}\n"
    "\n"
    "// Lattice index wrapped into 0..period - 1, as it is without a period\n"
    "fn box_tex_noise_wrap(i: i32, period: i32) -> u32 {\n"
    "    if (period <= 0) {\n"
    "        return bitcast<u32>(i);\n"
    "    }\n"
    "    return bitcast<u32>(((i % period) + period) % period);\n"
    "}\n"
    "\n"
    "// Hash of a corner of the lattice, x and y wrapped\n"
    "fn box_tex_noise_hash(seed: u32, c: vec4i, period: vec2i) -> u32 {\n"
    "    let x = box_tex_noise_wrap(c.x, period.x);\n"
    "    let y = box_tex_noise_wrap(c.y, period.y);\n"
    "    return box_texgraph_hash(seed ^ box_texgraph_hash(x ^ box_texgraph_hash(y ^\n"
    "        box_texgraph_hash(bitcast<u32>(c.z) ^ box_texgraph_hash(bitcast<u32>(c.w))))));\n"
    "}\n"
    "\n"
    "// Noise of the eight corners of the cell i at the layer iw, d.w the offset from the layer\n"
    "fn box_tex_noise_layer(seed: u32, i: vec3i, f: vec3f, u: vec3f, iw: i32, dw: f32,\n"
    "    period: vec2i) -> f32 {\n"
    "    var v: array<f32, 8>;\n"
    "    for (var c = 0; c < 8; c ++) {\n"
    "        let o = vec3i(c & 1, (c >> 1) & 1, (c >> 2) & 1);\n"
    "        let h = box_tex_noise_hash(seed, vec4i(i + o, iw), period);\n"
    "        v[c] = box_tex_noise_grad(h, vec4f(f - vec3f(o), dw));\n"
    "    }\n"
    "    let y0 = mix(mix(v[0], v[1], u.x), mix(v[2], v[3], u.x), u.y);\n"
    "    let y1 = mix(mix(v[4], v[5], u.x), mix(v[6], v[7], u.x), u.y);\n"
    "    return mix(y0, y1, u.z);\n"
    "}\n"
    "\n"
    "// Gradient noise at a point, about -1..1, three dimensional where w is 0\n"
    "fn box_tex_noise_gradient(seed: u32, p: vec4f, period: vec2i) -> f32 {\n"
    "    let b = floor(p.xyz);\n"
    "    let i = vec3i(b);\n"
    "    let f = p.xyz - b;\n"
    "    let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
    "    if (p.w == 0.0) {\n"
    "        return box_tex_noise_layer(seed, i, f, u, 0, 0.0, period);\n"
    "    }\n"
    "    let bw = floor(p.w);\n"
    "    let iw = i32(bw);\n"
    "    let fw = p.w - bw;\n"
    "    let uw = fw * fw * fw * (fw * (fw * 6.0 - 15.0) + 10.0);\n"
    "    return mix(box_tex_noise_layer(seed, i, f, u, iw, fw, period),\n"
    "        box_tex_noise_layer(seed, i, f, u, iw + 1, fw - 1.0, period), uw);\n"
    "}\n"
    "\n"
    "// Fractal of the octaves of one channel, 0..1\n"
    "fn box_tex_noise_fractal(p: array<vec4u, 4>, seed: u32, coords: vec4f, evolve: f32)\n"
    "    -> f32 {\n"
    "    let frequency = bitcast<f32>(p[0].x);\n"
    "    let octaves = clamp(bitcast<i32>(p[0].y), 1, 16);\n"
    "    let lacunarity = select(bitcast<f32>(p[0].z), 2.0, bitcast<f32>(p[0].z) <= 0.0);\n"
    "    let gain = select(bitcast<f32>(p[0].w), 0.5, bitcast<f32>(p[0].w) <= 0.0);\n"
    "    let fractal = bitcast<i32>(p[1].y);\n"
    "    let offset = select(bitcast<f32>(p[1].z), 1.0, bitcast<f32>(p[1].z) == 0.0);\n"
    "    let plane = box_texgraph_stage.domain == 0u;\n"
    "    let size = vec2f(box_texgraph_stage.size);\n"
    "    var f = select(frequency, 4.0, frequency <= 0.0);\n"
    "    var time = evolve;\n"
    "    var amp = 1.0;\n"
    "    var norm = 0.0;\n"
    "    var sum = 0.0;\n"
    "    var weight = 1.0;\n"
    "    for (var o = 0; o < 16; o ++) {\n"
    "        if (o >= octaves) {\n"
    "            break;\n"
    "        }\n"
    "        var q = coords.xyz * f;\n"
    "        var period = vec2i(0);\n"
    "        if (plane) {\n"
    "            let px = clamp(i32(floor(f + 0.5)), 1, 4096);\n"
    "            let py = clamp(i32(floor(f32(px) * size.y / size.x + 0.5)), 1, 4096);\n"
    "            period = vec2i(px, py);\n"
    "            q = coords.xyz * vec3f(f32(px), f32(py), f32(px));\n"
    "        }\n"
    "        let s = box_texgraph_hash(seed ^ box_texgraph_hash(u32(o) + 0x68e31da4u));\n"
    "        let n = clamp(box_tex_noise_gradient(s, vec4f(q, time), period), -1.0, 1.0);\n"
    "        switch fractal {\n"
    "            case 1: {\n"
    "                var signal = offset - abs(n);\n"
    "                signal = signal * signal * weight;\n"
    "                weight = clamp(signal, 0.0, 1.0);\n"
    "                sum += amp * signal;\n"
    "            }\n"
    "            case 2: {\n"
    "                sum += amp * abs(n);\n"
    "            }\n"
    "            default: {\n"
    "                sum += amp * n;\n"
    "            }\n"
    "        }\n"
    "        norm += amp;\n"
    "        amp *= gain;\n"
    "        f *= lacunarity;\n"
    "        time *= lacunarity;\n"
    "    }\n"
    "    let v = sum / norm;\n"
    "    return clamp(select(v, 0.5 + 0.5 * v, fractal != 1 && fractal != 2), 0.0, 1.0);\n"
    "}\n"
    "\n"
    "fn box_tex_noise(p: array<vec4u, 4>, i: array<vec4f, 4>, coords: vec4f) -> vec4f {\n"
    "    let channels = bitcast<i32>(p[1].w);\n"
    "    if (channels <= 1) {\n"
    "        let n = box_tex_noise_fractal(p, box_texgraph_hash(p[1].x), i[0], i[1].x);\n"
    "        return vec4f(n, n, n, 1.0);\n"
    "    }\n"
    "    var r = vec4f(0.0, 0.0, 0.0, 1.0);\n"
    "    for (var c = 0; c < 4; c ++) {\n"
    "        if (c >= channels) {\n"
    "            break;\n"
    "        }\n"
    "        r[c] = box_tex_noise_fractal(p, box_texgraph_hash(p[1].x + u32(c) * 0x9e3779b9u),\n"
    "            i[0], i[1].x);\n"
    "    }\n"
    "    return r;\n"
    "}\n";

/** Get the type of the value of a TexNoise node: a Scalar with one channel, else a Vector.
 *
 * @param node The TexNoise
 * @return The type
 */
static TextureValueType box_texture_noise_output(
    const void *node)
{
    const TexNoise *noise = node;

    return noise->channels > 1 ? TexVector : TexScalar;
}

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
    char *error;                /**< Why the build failed, NULL while it did not */
    bool wait;                  /**< Wait for a node without a node component */
    bool waiting;               /**< The walk found a node without a node component */
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

    ecs_os_free(kind->function);
    ecs_os_free(kind->source);
}

void box_texture_graph_clear(
    box_texture_t *tex)
{
    if (tex->graph) {
        ecs_os_free(tex->graph->nodes);
        ecs_os_free(tex->graph->params);
        ecs_os_free(tex->graph);
        tex->graph = NULL;
    }

    ecs_vec_fini_t(NULL, &tex->reached, ecs_entity_t);
    tex->graph_dirty = false;
    tex->graph_waiting = false;
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

/** Mark every graph dirty that reached an entity in its last build, or every graph.
 *
 * @param t The registry
 * @param e The entity, 0 for every graph
 */
static void box_texture_graphs_touch(
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
    kind->component = desc->component;
    kind->output = desc->output;
    kind->flags = desc->flags;
    kind->output_of = desc->output_of;
    kind->function = ecs_os_strdup(desc->function);
    kind->source = ecs_os_strdup(desc->source);
    for (i = 0; i < BOX_TEXTURE_NODE_INPUTS && desc->inputs[i].name; i ++) {
        kind->inputs[i] = desc->inputs[i];
        kind->inputs[i].name = ecs_os_strdup(desc->inputs[i].name);
        kind->inputs[i].value = desc->inputs[i].value
            ? ecs_os_strdup(desc->inputs[i].value)
            : NULL;
        kind->input_count ++;
    }

    for (i = 0; i < BOX_TEXTURE_NODE_PARAMS && desc->params[i].name; i ++) {
        ecs_size_t size = desc->params[i].size;
        int32_t align = size <= 4 ? 4 : size <= 8 ? 8 : 16;
        kind->params[i] = desc->params[i];
        kind->params[i].name = ecs_os_strdup(desc->params[i].name);
        kind->param_count ++;
        if (size < 4 || size > 16 || size % 4) {
            result = -1;
        }

        offset = (offset + align - 1) / align * align;
        kind->param_offsets[i] = offset;
        offset += size;
    }

    kind->param_size = (offset + 15) / 16 * 16;
    kind->node_size = kind->param_size + kind->input_count * 16;
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
        .function = kind->function,
        .neighbourhood = (kind->flags & BOX_TEXTURE_NODE_NEIGHBOURHOOD) != 0,
        .animated = (kind->flags & BOX_TEXTURE_NODE_ANIMATED) != 0
    };
    int32_t i;
    for (i = 0; i < kind->input_count; i ++) {
        value.inputs[i].name = kind->inputs[i].name;
        value.inputs[i].type = kind->inputs[i].type;
        value.inputs[i].fallback = kind->inputs[i].value != NULL;
    }

    for (i = 0; i < kind->param_count; i ++) {
        value.params[i].name = kind->params[i].name;
        value.params[i].offset = kind->param_offsets[i];
        value.params[i].size = kind->params[i].size;
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
        ecs_err("box_texture_node_register: node kind '%s' has a parameter of an invalid size "
            "or more than %d parameter bytes", name, BOX_TEXTURE_NODE_PARAM_BYTES);
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

/** Get the type of the value of a node.
 *
 * @param kind The kind of the node
 * @param ptr Its component
 * @return The type of its kind, or the type its members give
 */
static TextureValueType box_texture_graph_output(
    const box_texture_node_kind_t *kind,
    const void *ptr)
{
    return kind->output_of && ptr ? kind->output_of(ptr) : kind->output;
}

/** Append the compiled node of an entity whose inputs are compiled, with its parameters and
 * the constants of its inputs without a node.
 *
 * @param b The build
 * @param e The node entity
 * @param kind Its kind
 * @param inputs Index of the node of each input, -1 for none
 * @return Index of the node
 */
static int32_t box_texture_graph_append(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    const box_texture_node_kind_t *kind,
    const int32_t *inputs)
{
    const uint8_t *ptr = ecs_get_id(b->world, e, kind->component);
    int32_t i, offset = ecs_vec_count(&b->params);
    int32_t result = ecs_vec_count(&b->nodes);
    uint8_t *params = ecs_vec_grow_t(NULL, &b->params, uint8_t, kind->node_size);
    ecs_os_memset(params, 0, kind->node_size);
    for (i = 0; i < kind->param_count; i ++) {
        ecs_os_memcpy(params + kind->param_offsets[i], ptr + kind->params[i].offset,
            kind->params[i].size);
    }

    for (i = 0; i < kind->input_count; i ++) {
        const box_texture_node_input_t *in = &kind->inputs[i];
        if (inputs[i] < 0 && in->value) {
            ecs_os_memcpy(params + kind->param_size + i * 16, ptr + in->value_offset,
                in->type == TexScalar ? ECS_SIZEOF(float) : 4 * ECS_SIZEOF(float));
        }
    }

    box_texture_graph_node_t *node = ecs_vec_append_t(NULL, &b->nodes,
        box_texture_graph_node_t);
    ecs_os_memset_t(node, 0, box_texture_graph_node_t);
    node->entity = e;
    node->kind = kind->id;
    node->output = box_texture_graph_output(kind, ptr);
    node->params = offset;
    node->param_size = kind->node_size;
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
 * @param expected Type the input expects
 */
static void box_texture_graph_check_type(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    TextureValueType output,
    ecs_entity_t from,
    const char *input,
    TextureValueType expected)
{
    if (from && output != expected) {
        char *name = box_texture_graph_name(b->world, e);
        char *from_name = box_texture_graph_name(b->world, from);
        box_texture_graph_error(b, "node '%s' outputs %s but input '%s' of node '%s' expects "
            "%s", name, output == TexScalar ? "Scalar" : "Vector", input, from_name,
            expected == TexScalar ? "Scalar" : "Vector");
        ecs_os_free(from_name);
        ecs_os_free(name);
    }
}

/** Visit a node, compiling its inputs before it.
 *
 * @param b The build
 * @param e The node entity
 * @param from Node whose input it is, 0 for the output of the graph
 * @param input Name of that input
 * @param expected Type that input expects
 * @return Index of the node, -1 when the build failed or waits
 */
static int32_t box_texture_graph_visit(
    box_texture_graph_build_t *b,
    ecs_entity_t e,
    ecs_entity_t from,
    const char *input,
    TextureValueType expected)
{
    int32_t result = -1, i, kinds = 0;
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
        box_texture_graph_check_type(b, e, ecs_vec_get_t(&b->nodes, box_texture_graph_node_t,
            result)->output, from, input, expected);
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

    box_texture_graph_check_type(b, e, box_texture_graph_output(kind,
        ecs_get_id(b->world, e, kind->component)), from, input, expected);
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

    if (!b->error && !b->waiting) {
        result = box_texture_graph_append(b, e, kind, inputs);
    }

done:
    ecs_os_free(name);

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
    int32_t i;
    box_texture_capped_size(b->world, b->t, graph->width, graph->height, &result->width,
        &result->height);
    result->domain = graph->domain;
    result->format = graph->format;
    result->node_count = ecs_vec_count(&b->nodes);
    result->nodes = ecs_os_memdup(ecs_vec_first(&b->nodes),
        result->node_count * ECS_SIZEOF(box_texture_graph_node_t));
    result->param_size = ecs_vec_count(&b->params);
    result->params = result->param_size
        ? ecs_os_memdup(ecs_vec_first(&b->params), result->param_size)
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
    }

    result->param_hash = box_texture_graph_hash(BOX_TEXTURE_GRAPH_FNV_BASIS, result->params,
        result->param_size);

    return result;
}

/** Check whether two compiled graphs make the same image.
 *
 * @param a The first graph
 * @param b The second graph
 * @return Whether their size, domain, format, structure and parameters are the same
 */
static bool box_texture_graph_same(
    const box_texture_graph_t *a,
    const box_texture_graph_t *b)
{
    return a->width == b->width && a->height == b->height && a->domain == b->domain &&
        a->format == b->format && a->structure_hash == b->structure_hash &&
        a->param_hash == b->param_hash && a->node_count == b->node_count &&
        a->param_size == b->param_size &&
        (!a->param_size || !ecs_os_memcmp(a->params, b->params, a->param_size));
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
        ecs_os_free(graph->nodes);
        ecs_os_free(graph->params);
        ecs_os_free(graph);
        box_texture_status(world, e, tex->graph_pending ? TextureLoading : TextureReady,
            revision, NULL);
        goto done;
    }

    if (tex->graph) {
        ecs_os_free(tex->graph->nodes);
        ecs_os_free(tex->graph->params);
        ecs_os_free(tex->graph);
    }

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
        .world = world, .t = t, .graph = e, .reached = &tex->reached, .wait = wait
    };
    tex->graph_dirty = false;
    tex->graph_waiting = false;
    if (ecs_has(world, e, TexturePath) || ecs_has(world, e, TextureGen)) {
        box_texture_load(world, t, e);
        goto done;
    }

    b.name = ecs_get_path(world, e);
    ecs_map_init(&b.index, NULL);
    ecs_vec_init_t(NULL, &b.nodes, box_texture_graph_node_t, 0);
    ecs_vec_init_t(NULL, &b.params, uint8_t, 0);
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
    } else {
        box_texture_graph_accept(world, t, e, box_texture_graph_compile(&b, graph));
    }

    ecs_os_free(b.error);
    ecs_os_free(b.name);
    ecs_map_fini(&b.index);
    ecs_vec_fini_t(NULL, &b.nodes, box_texture_graph_node_t);
    ecs_vec_fini_t(NULL, &b.params, uint8_t);
done:
    return;
}

/** Load the textures whose source component was removed, then build the graphs that changed
 * and fail the ones that waited a frame for a node without a node component.
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
        if (tex->graph_dirty || tex->graph_waiting) {
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
            continue;
        }

        box_texture_graph_build(world, t, e, wait);
    }

    ecs_vec_fini_t(NULL, &build, ecs_entity_t);
done:
    return;
}

/** Register the built in node kinds: TexConstant, TexCoords, TexMath and TexNoise.
 *
 * @param world The world
 */
static void box_texture_nodes_register(
    ecs_world_t *world)
{
    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexConstant),
        .output = TexVector,
        .params = {{ "value", offsetof(TexConstant, value), 16, BOX_TEXTURE_PARAM_F32 }},
        .function = "box_tex_constant",
        .source = box_texture_constant_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexCoords),
        .output = TexVector,
        .params = {{ "scale", offsetof(TexCoords, scale), 4, BOX_TEXTURE_PARAM_F32 }},
        .function = "box_tex_coords",
        .source = box_texture_coords_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexMath),
        .output = TexVector,
        .inputs = {
            { "a", offsetof(TexMath, a), TexVector, "a_value", offsetof(TexMath, a_value) },
            { "b", offsetof(TexMath, b), TexVector, "b_value", offsetof(TexMath, b_value) },
            { "c", offsetof(TexMath, c), TexVector, "c_value", offsetof(TexMath, c_value) }
        },
        .params = {{ "op", offsetof(TexMath, op), 4, BOX_TEXTURE_PARAM_I32 }},
        .function = "box_tex_math",
        .source = box_texture_math_wgsl
    });

    box_texture_node_register(world, &(box_texture_node_desc_t){
        .component = ecs_id(TexNoise),
        .output = TexScalar,
        .inputs = {
            { "coords", offsetof(TexNoise, coords), TexVector },
            { "evolve", offsetof(TexNoise, evolve), TexScalar }
        },
        .params = {
            { "frequency", offsetof(TexNoise, frequency), 4, BOX_TEXTURE_PARAM_F32 },
            { "octaves", offsetof(TexNoise, octaves), 4, BOX_TEXTURE_PARAM_I32 },
            { "lacunarity", offsetof(TexNoise, lacunarity), 4, BOX_TEXTURE_PARAM_F32 },
            { "gain", offsetof(TexNoise, gain), 4, BOX_TEXTURE_PARAM_F32 },
            { "seed", offsetof(TexNoise, seed), 4, BOX_TEXTURE_PARAM_I32 },
            { "fractal", offsetof(TexNoise, fractal), 4, BOX_TEXTURE_PARAM_I32 },
            { "offset", offsetof(TexNoise, offset), 4, BOX_TEXTURE_PARAM_F32 },
            { "channels", offsetof(TexNoise, channels), 4, BOX_TEXTURE_PARAM_I32 }
        },
        .function = "box_tex_noise",
        .source = box_texture_noise_wgsl,
        .output_of = box_texture_noise_output
    });
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
        .entity = ecs_entity(world, { .name = "TextureGraphBuild" }),
        .phase = EcsOnStore,
        .run = BoxTextureGraphBuild
    });
}
