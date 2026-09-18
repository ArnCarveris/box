/**
 * @file material/layers.c
 * @brief Registers the built in material layers under box.material.layers.
 *
 * Each layer is an entity with the WGSL body of its effect and the scene global
 * parameters that body reads, which a script changes at runtime.
 */

#include "material_internal.h"

/** Body of the edge wear layer, which reveals bare metal along the bounds edges.
 *
 * `a.rgb` is the color of the exposed metal and `a.w` how much of it shows.
 * `b.x` is the width of the worn band in bounds units, `b.y` the roughness the
 * metal takes and `b.z` the noise frequency in cells per world unit.
 */
static const char *box_layer_edge_wear_wgsl =
    "let d = min((*s).local, vec3(1.0) - (*s).local);\n"
    "let lo = min(min(d.x, d.y), d.z);\n"
    "let hi = max(max(d.x, d.y), d.z);\n"
    "let mid = d.x + d.y + d.z - lo - hi;\n"
    "let edge = 1.0 - smoothstep(0.0, max(p.b.x, 0.0001), mid);\n"
    "let k = saturate(edge * p.a.w * box_noise((*s).world_pos * p.b.z));\n"
    "(*s).albedo = mix((*s).albedo, p.a.rgb, k);\n"
    "(*s).metallic = mix((*s).metallic, 1.0, k);\n"
    "(*s).roughness = mix((*s).roughness, p.b.y, k);\n";

/** Body of the dust layer, which settles on the surfaces that face up.
 *
 * `a.rgb` is the dust color and `a.w` how much of it settles. `b.x` is the
 * noise frequency in cells per world unit, `b.y` the roughness the dust takes
 * and `b.z` how sharply it clings to the surfaces that face up.
 */
static const char *box_layer_dust_wgsl =
    "let n = (*s).geo_normal;\n"
    "let up = pow(saturate(n.y), max(p.b.z, 0.001));\n"
    "let k = saturate(p.a.w * up * box_noise((*s).world_pos * p.b.x));\n"
    "(*s).albedo = mix((*s).albedo, p.a.rgb, k);\n"
    "(*s).roughness = mix((*s).roughness, p.b.y, k);\n";

/** Body of the grime layer, which gathers where a surface faces down or sits low.
 *
 * `a.rgb` multiplies the albedo where the grime sits and `a.w` is its amount.
 * `b.x` is the noise frequency in cells per world unit and `b.y` the roughness
 * the grime takes.
 */
static const char *box_layer_grime_wgsl =
    "let n = (*s).geo_normal;\n"
    "let low = saturate(-n.y + (1.0 - (*s).local.y));\n"
    "let k = saturate(p.a.w * low * box_noise((*s).world_pos * p.b.x));\n"
    "(*s).albedo = mix((*s).albedo, (*s).albedo * p.a.rgb, k);\n"
    "(*s).roughness = mix((*s).roughness, p.b.y, k);\n";

/** Body of the wetness layer, which darkens a surface and makes it glossy.
 *
 * `a.x` is the roughness the wet film reaches and `a.w` the wetness amount.
 * `b.x` is the noise frequency in cells per world unit, `b.y` how much the
 * water pools on the surfaces that face up and `b.z` how much noise breaks it up.
 */
static const char *box_layer_wetness_wgsl =
    "let n = (*s).geo_normal;\n"
    "let pool = mix(1.0, saturate(n.y), p.b.y);\n"
    "let noise = mix(1.0, box_noise((*s).world_pos * p.b.x), p.b.z);\n"
    "let k = saturate(p.a.w * pool * noise);\n"
    "(*s).albedo = (*s).albedo * (1.0 - 0.5 * k);\n"
    "(*s).roughness = mix((*s).roughness, p.a.x, k);\n";

/** Create one layer entity with its source, order and default parameters.
 *
 * @param world The world
 * @param parent The layers scope
 * @param name Name of the layer entity
 * @param wgsl Body of its layer function, copied into the component
 * @param order Application order, the lowest runs first
 * @param a First parameter vector
 * @param b Second parameter vector
 * @return The layer entity
 */
static ecs_entity_t box_layer_add(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const char *wgsl,
    int32_t order,
    vec4 a,
    vec4 b)
{
    ecs_entity_t e = ecs_entity(world, { .name = name, .parent = parent });
    ecs_set(world, e, MaterialLayer, { .wgsl = ecs_os_strdup(wgsl), .order = order });
    ecs_set(world, e, LayerParams, { .a = a, .b = b });

    return e;
}

void box_material_layers_init(
    ecs_world_t *world)
{
    ecs_entity_t parent = ecs_entity(world, { .name = "layers" });

    box_layer_add(world, parent, "edge_wear", box_layer_edge_wear_wgsl, 0,
        (vec4){ 0.82f, 0.80f, 0.76f, 1.0f }, (vec4){ 0.14f, 0.2f, 6.0f, 0.0f });
    box_layer_add(world, parent, "dust", box_layer_dust_wgsl, 10,
        (vec4){ 0.72f, 0.68f, 0.58f, 0.85f }, (vec4){ 6.0f, 0.95f, 1.5f, 0.0f });
    box_layer_add(world, parent, "grime", box_layer_grime_wgsl, 20,
        (vec4){ 0.18f, 0.16f, 0.12f, 0.9f }, (vec4){ 4.0f, 0.85f, 0.0f, 0.0f });
    box_layer_add(world, parent, "wetness", box_layer_wetness_wgsl, 30,
        (vec4){ 0.05f, 0.0f, 0.0f, 0.9f }, (vec4){ 3.0f, 0.0f, 0.35f, 0.0f });
}
