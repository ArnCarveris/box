/**
 * @file Grass.c
 * @brief Tests for the Grass component of box.grass: its defaults, the resolution of its values,
 * the terrain it makes sampled on the GPU and what the render extract appends to the packet.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "grass.h"

/** The Grass entities of the last packet a test backend received */
static ecs_vec_t grass_packet;

/** Copy the Grass entities of a packet.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void grass_render(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    ecs_vec_clear(&grass_packet);
    int32_t i, count = ecs_vec_count(&packet->grasses);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &grass_packet, box_render_grass_t) =
            *ecs_vec_get_t(&packet->grasses, box_render_grass_t, i);
    }
}

/** Create a world that renders on the main thread into a backend that keeps the Grass entities
 * of the last packet.
 *
 * @return The world
 */
static ecs_world_t* grass_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_vec_init_t(NULL, &grass_packet, box_render_grass_t, 0);
    box_render_backend_t backend = { .render = grass_render };
    box_render_set_backend(world, &backend);

    return world;
}

/** Run a frame and wait for the backend.
 *
 * @param world The world
 */
static void grass_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Find a Grass entity in the last packet.
 *
 * @param e The entity
 * @return Its record, NULL when the packet does not have it
 */
static const box_render_grass_t* grass_find(
    ecs_entity_t e)
{
    const box_render_grass_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&grass_packet); i ++) {
        const box_render_grass_t *g = ecs_vec_get_t(&grass_packet, box_render_grass_t, i);
        if (g->grass == e) {
            result = g;
        }
    }

    return result;
}

/** Linear value of a display encoded channel.
 *
 * @param v The channel
 * @return The linear value
 */
static float grass_linear(
    uint8_t v)
{
    float c = (float)v / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

void Grass_registered(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_id(Grass) != 0);
    test_assert(ecs_lookup(world, "box.grass.Grass") == ecs_id(Grass));
    ecs_fini(world);
}

void Grass_defaults(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Grass);
    const Grass *g = ecs_get(world, e, Grass);
    test_assert(g != NULL);
    test_uint(g->terrain, 0);
    test_int(g->mask, -1);
    test_flt(g->density, BOX_GRASS_DENSITY);
    test_flt(g->min_height, 0.25f);
    test_flt(g->max_height, 0.6f);
    test_flt(g->fold_height, BOX_GRASS_FOLD_HEIGHT);
    test_flt(g->min_width, 0.03f);
    test_flt(g->max_width, 0.06f);
    test_int(g->base_color.r, 38);
    test_int(g->base_color.g, 66);
    test_int(g->base_color.b, 18);
    test_int(g->tip_color.r, 150);
    test_int(g->tip_color.g, 175);
    test_int(g->tip_color.b, 70);
    test_flt(g->lod[0], 20);
    test_flt(g->lod[1], 60);
    test_flt(g->lod[2], 200);
    test_flt(g->clump_size, BOX_GRASS_CLUMP_SIZE);
    test_flt(g->clump_follow, BOX_GRASS_CLUMP_FOLLOW);
    test_flt(g->variation, BOX_GRASS_VARIATION);
    test_uint(g->seed, 0);
    ecs_fini(world);
}

void Grass_script(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_int(ecs_script_run(world, "grass",
        "using box.grass\n"
        "meadow { Grass: {density: 20, mask: 2, lod: [10, 30, 90], seed: 7, clump_size: 2.5,\n"
        "  clump_follow: 0.9, variation: 0.25, fold_height: 0.3} }\n", NULL), 0);
    ecs_entity_t meadow = ecs_lookup(world, "meadow");
    test_assert(meadow != 0);
    const Grass *g = ecs_get(world, meadow, Grass);
    test_assert(g != NULL);
    test_flt(g->density, 20);
    test_int(g->mask, 2);
    test_flt(g->lod[0], 10);
    test_flt(g->lod[1], 30);
    test_flt(g->lod[2], 90);
    test_uint(g->seed, 7);
    test_flt(g->clump_size, 2.5f);
    test_flt(g->clump_follow, 0.9f);
    test_flt(g->variation, 0.25f);
    test_flt(g->fold_height, 0.3f);
    test_flt(g->min_height, 0.25f);
    test_int(g->base_color.g, 66);
    ecs_fini(world);
}

void Grass_resolve(void) {
    Grass in = {
        .mask = 5,
        .density = -1,
        .min_height = 0,
        .max_height = 0.1f,
        .min_width = NAN,
        .max_width = 0.01f,
        .lod = { 50, 10, 900 }
    };
    Grass out;
    box_grass_resolve(&in, &out);
    test_int(out.mask, -1);
    test_flt(out.density, BOX_GRASS_DENSITY);
    test_flt(out.min_height, 0.25f);
    test_flt(out.max_height, 0.25f);
    test_flt(out.min_width, 0.03f);
    test_flt(out.max_width, 0.03f);
    test_flt(out.lod[0], 50);
    test_flt(out.lod[1], 50);
    test_flt(out.lod[2], BOX_GRASS_DISTANCE_MAX);

    in = (Grass){ .mask = 3, .density = 1000, .lod = { 0, 0, 0 } };
    box_grass_resolve(&in, &out);
    test_int(out.mask, 3);
    test_flt(out.density, BOX_GRASS_DENSITY_MAX);
    test_flt(out.lod[0], 20);
    test_flt(out.lod[1], 60);
    test_flt(out.lod[2], 200);
}

void Grass_resolve_clumps(void) {
    Grass in = { .clump_size = 0, .clump_follow = 0, .variation = 0 };
    Grass out;
    box_grass_resolve(&in, &out);
    test_flt(out.clump_size, BOX_GRASS_CLUMP_SIZE);
    test_flt(out.clump_follow, 0);
    test_flt(out.variation, 0);

    in = (Grass){ .clump_size = -3, .clump_follow = 1.5f, .variation = -0.5f };
    box_grass_resolve(&in, &out);
    test_flt(out.clump_size, BOX_GRASS_CLUMP_SIZE);
    test_flt(out.clump_follow, 1);
    test_flt(out.variation, 0);

    in = (Grass){ .clump_size = INFINITY, .clump_follow = NAN, .variation = NAN };
    box_grass_resolve(&in, &out);
    test_flt(out.clump_size, BOX_GRASS_CLUMP_SIZE);
    test_flt(out.clump_follow, BOX_GRASS_CLUMP_FOLLOW);
    test_flt(out.variation, BOX_GRASS_VARIATION);

    in = (Grass){ .clump_size = 3.5f, .clump_follow = 0.4f, .variation = 2 };
    box_grass_resolve(&in, &out);
    test_flt(out.clump_size, 3.5f);
    test_flt(out.clump_follow, 0.4f);
    test_flt(out.variation, 1);
}

void Grass_resolve_fold(void) {
    Grass in = { .fold_height = 0 };
    Grass out;
    box_grass_resolve(&in, &out);
    test_flt(out.fold_height, 0);

    in = (Grass){ .fold_height = -0.5f };
    box_grass_resolve(&in, &out);
    test_flt(out.fold_height, 0);

    in = (Grass){ .fold_height = NAN };
    box_grass_resolve(&in, &out);
    test_flt(out.fold_height, BOX_GRASS_FOLD_HEIGHT);

    in = (Grass){ .fold_height = INFINITY };
    box_grass_resolve(&in, &out);
    test_flt(out.fold_height, BOX_GRASS_FOLD_HEIGHT);

    in = (Grass){ .fold_height = 0.42f };
    box_grass_resolve(&in, &out);
    test_flt(out.fold_height, 0.42f);
}

void Grass_terrain_sampled(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, {.value = {5, 5, 5, 5}});
    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Terrain, {.height = node, .tile_size = 16, .resolution = 17,
        .levels = 2});
    test_bool(ecs_has_id(world, terrain, TerrainSampled), false);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Grass, {.terrain = terrain, .mask = -1});
    test_bool(ecs_has_id(world, terrain, TerrainSampled), true);

    // A second Grass shares the terrain, a Grass on a plane samples nothing
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Grass, {.terrain = terrain, .mask = 1});
    test_bool(ecs_has_id(world, terrain, TerrainSampled), true);
    ecs_entity_t plane = ecs_new(world);
    ecs_set(world, plane, Grass, {.mask = -1});
    test_bool(ecs_has_id(world, plane, TerrainSampled), false);
    ecs_fini(world);
}

void Grass_extract_plane(void) {
    ecs_world_t *world = grass_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {3, 2, -4});
    ecs_set(world, e, Grass, {.mask = 1, .density = 30, .min_height = 0.2f,
        .max_height = 0.5f, .fold_height = 0.3f, .min_width = 0.02f, .max_width = 0.04f,
        .base_color = {40, 70, 20}, .tip_color = {140, 170, 70}, .clump_size = 2,
        .clump_follow = 0.6f, .variation = 0.8f, .lod = {15, 45, 150}, .seed = 9});
    grass_frame(world);
    grass_frame(world);

    const box_render_grass_t *g = grass_find(e);
    test_assert(g != NULL);
    test_uint(g->terrain, 0);
    test_uint(g->scene, 0);
    test_flt(g->plane, 2);
    test_flt(g->ground[0], 2);
    test_flt(g->ground[1], 2);
    test_int(g->mask, 1);
    test_flt(g->density, 30);
    test_flt(g->height[0], 0.2f);
    test_flt(g->height[1], 0.5f);
    test_flt(g->fold, 0.3f);
    test_flt(g->width[0], 0.02f);
    test_flt(g->width[1], 0.04f);
    test_flt(g->base[0], grass_linear(40));
    test_flt(g->base[1], grass_linear(70));
    test_flt(g->tip[2], grass_linear(70));
    test_flt(g->clump[0], 2);
    test_flt(g->clump[1], 0.6f);
    test_flt(g->variation, 0.8f);
    test_flt(g->lod[0], 15);
    test_flt(g->lod[1], 45);
    test_flt(g->lod[2], 150);
    test_uint(g->seed, 9);

    // Removing the Grass removes it from the packet
    ecs_remove(world, e, Grass);
    grass_frame(world);
    test_assert(grass_find(e) == NULL);

    ecs_fini(world);
    ecs_vec_fini_t(NULL, &grass_packet, box_render_grass_t);
}

void Grass_extract_terrain(void) {
    ecs_world_t *world = grass_world();
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, {.value = {5, 5, 5, 5}});
    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Position, {0, 5, 0});
    ecs_set(world, terrain, Terrain, {.height = node, .tile_size = 16, .resolution = 17,
        .levels = 2, .min_height = -10, .max_height = 60});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Grass, {.terrain = terrain, .mask = 0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Grass, {.terrain = terrain, .mask = 3});
    ecs_entity_t lost = ecs_new(world);
    ecs_set(world, lost, Grass, {.terrain = ecs_new(world), .mask = -1});
    grass_frame(world);
    grass_frame(world);

    const box_render_grass_t *ga = grass_find(a);
    const box_render_grass_t *gb = grass_find(b);
    test_assert(ga != NULL);
    test_assert(gb != NULL);
    test_uint(ga->terrain, terrain);
    test_uint(gb->terrain, terrain);
    test_int(ga->mask, 0);
    test_int(gb->mask, 3);
    test_flt(ga->ground[0], -5);
    test_flt(ga->ground[1], 65);

    // A Grass whose terrain is not a Terrain grows nowhere
    test_assert(grass_find(lost) == NULL);

    ecs_fini(world);
    ecs_vec_fini_t(NULL, &grass_packet, box_render_grass_t);
}

void Grass_extract_scene(void) {
    ecs_world_t *world = grass_world();
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, e, Grass, {.mask = -1});
    grass_frame(world);
    grass_frame(world);

    const box_render_grass_t *g = grass_find(e);
    test_assert(g != NULL);
    test_uint(g->scene, scene);
    test_flt(g->fold, 0);

    ecs_fini(world);
    ecs_vec_fini_t(NULL, &grass_packet, box_render_grass_t);
}

void Grass_extract_wind(void) {
    ecs_world_t *world = grass_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Grass, {.mask = -1});
    grass_frame(world);
    grass_frame(world);

    // Without a Wind the blades stand in still air
    const box_render_grass_t *g = grass_find(e);
    test_assert(g != NULL);
    test_flt(g->wind_direction[0], 1);
    test_flt(g->wind_direction[1], 0);
    test_flt(g->wind_speed, 0);
    test_flt(g->wind_phase, 0);

    // With one they read its gust field and phases as the WindSway records do
    ecs_singleton_set(world, Wind, {.direction = {0, 2}, .speed = 6, .gust = 0.4f,
        .gust_scale = 10});
    WindStatus *s = ecs_ensure(world, ecs_id(Wind), WindStatus);
    s->clock = 5.3;
    ecs_modified(world, ecs_id(Wind), WindStatus);
    grass_frame(world);

    box_render_wind_t w;
    box_render_wind_frame(world, &w);
    g = grass_find(e);
    test_assert(g != NULL);
    test_flt(g->wind_direction[0], 0);
    test_flt(g->wind_direction[1], 1);
    test_flt(g->wind_offset[0], w.offset[0]);
    test_flt(g->wind_offset[1], w.offset[1]);
    test_flt(g->wind_speed, 6);
    test_flt(g->wind_gust, 0.4f);
    test_flt(g->wind_frequency, 0.1f);
    test_flt(g->wind_sway, w.sway);
    const WindStatus *status = ecs_get(world, ecs_id(Wind), WindStatus);
    test_assert(status != NULL);
    test_assert(status->clock >= 5.3);
    double phase = fmod(status->clock * BOX_RENDER_GRASS_WIND_RATE, 1.0);
    test_assert(fabs((double)g->wind_phase - phase) < 1e-5);
    test_assert(g->wind_phase >= 0 && g->wind_phase < 1);

    ecs_fini(world);
    ecs_vec_fini_t(NULL, &grass_packet, box_render_grass_t);
}

/** A Grass of the packet with the defaults of a new Grass resolved.
 *
 * @return The Grass
 */
static box_render_grass_t grass_record(void)
{
    box_render_grass_t g = {0};
    g.mask = -1;
    g.density = 48;
    g.height[0] = 0.25f;
    g.height[1] = 0.6f;
    g.width[0] = 0.03f;
    g.width[1] = 0.06f;
    g.base[0] = 0.02f;
    g.base[1] = 0.05f;
    g.base[2] = 0.01f;
    g.tip[0] = 0.3f;
    g.tip[1] = 0.43f;
    g.tip[2] = 0.06f;
    g.variation = 0.5f;
    g.lod[0] = 20;
    g.lod[1] = 60;
    g.lod[2] = 200;
    g.wind_direction[0] = 1;

    return g;
}

void Grass_far_range(void) {
    box_render_grass_t g = grass_record();
    box_render_grass_far_t far;
    box_render_grass_far(&g, &far);

    // The far field fades in over the distances where the blades thin out and is full where
    // they start to shrink away, before they end
    test_flt(far.range[0], 20);
    test_flt(far.range[1], 200 * BOX_RENDER_GRASS_FAR_FULL);
    test_flt(far.range[2], 200);
    test_flt(far.range[3], 0.6f);
    test_assert(far.range[0] < far.range[1]);
    test_assert(far.range[1] < far.range[2]);

    // It starts where level 0 ends
    g.lod[0] = 50;
    g.lod[1] = 60;
    g.lod[2] = 80;
    box_render_grass_far(&g, &far);
    test_flt(far.range[0], 50);
    test_flt(far.range[1], 80 * BOX_RENDER_GRASS_FAR_FULL);

    // And never after it is full
    g.lod[0] = 100;
    g.lod[1] = 100;
    g.lod[2] = 100;
    box_render_grass_far(&g, &far);
    test_flt(far.range[0], 100 * BOX_RENDER_GRASS_FAR_FULL);
    test_flt(far.range[1], 100 * BOX_RENDER_GRASS_FAR_FULL);
}

void Grass_far_cover(void) {
    box_render_grass_t g = grass_record();
    box_render_grass_far_t far;
    box_render_grass_far(&g, &far);
    float area = 0.045f * 0.425f * BOX_RENDER_GRASS_FAR_AREA;
    test_assert(fabsf(far.tip[3] - (1.0f - expf(-48.0f * area))) < 1e-5f);
    float cover = far.tip[3];

    // A denser field covers more of the ground
    g.density = 96;
    box_render_grass_far(&g, &far);
    test_assert(far.tip[3] > cover);

    // The cover stays within its bounds
    g.density = 0.01f;
    box_render_grass_far(&g, &far);
    test_flt(far.tip[3], BOX_RENDER_GRASS_FAR_COVER_MIN);
    g.density = 256;
    g.width[1] = 0.5f;
    g.height[1] = 2;
    box_render_grass_far(&g, &far);
    test_flt(far.tip[3], BOX_RENDER_GRASS_FAR_COVER_MAX);
}

void Grass_far_values(void) {
    box_render_grass_t g = grass_record();
    g.mask = 2;
    g.variation = 0.8f;
    g.wind_direction[0] = 0;
    g.wind_direction[1] = 1;
    g.wind_offset[0] = 3;
    g.wind_offset[1] = -4;
    g.wind_speed = 6;
    g.wind_gust = 0.4f;
    g.wind_frequency = 0.1f;
    g.wind_sway = 0.25f;
    g.wind_phase = 0.75f;
    box_render_grass_far_t far;
    box_render_grass_far(&g, &far);

    // The colors and variation of the blades
    test_flt(far.base[0], 0.02f);
    test_flt(far.base[1], 0.05f);
    test_flt(far.base[2], 0.01f);
    test_flt(far.base[3], 0.8f);
    test_flt(far.tip[0], 0.3f);
    test_flt(far.tip[1], 0.43f);
    test_flt(far.tip[2], 0.06f);

    // The wind the blades bend with
    test_flt(far.wind_field[0], 0);
    test_flt(far.wind_field[1], 1);
    test_flt(far.wind_field[2], 3);
    test_flt(far.wind_field[3], -4);
    test_flt(far.wind_strength[0], 6);
    test_flt(far.wind_strength[1], 0.4f);
    test_flt(far.wind_strength[2], 0.1f);
    test_flt(far.wind_strength[3], 0.25f);
    test_flt(far.wind_time[0], 0.75f);
    test_flt(far.wind_time[1], 0);

    // The mask channel plus one, and that there is a Grass
    test_uint(far.config[0], 3);
    test_uint(far.config[1], 1);
    test_uint(far.config[2], 0);
    test_uint(far.config[3], 0);

    // Without a mask channel the grass grows everywhere
    g.mask = -1;
    box_render_grass_far(&g, &far);
    test_uint(far.config[0], 0);
    test_uint(far.config[1], 1);
}

void Grass_far_of_terrain(void) {
    ecs_os_set_api_defaults();
    box_render_packet_t p;
    ecs_os_memset_t(&p, 0, box_render_packet_t);
    ecs_vec_init_t(NULL, &p.terrain_samples, box_render_terrain_sample_t, 0);
    ecs_vec_init_t(NULL, &p.grasses, box_render_grass_t, 0);
    box_render_terrain_sample_t *s = ecs_vec_append_t(NULL, &p.terrain_samples,
        box_render_terrain_sample_t);
    ecs_os_memset_t(s, 0, box_render_terrain_sample_t);
    s->terrain = 1000;
    s->mesh = 7;
    s = ecs_vec_append_t(NULL, &p.terrain_samples, box_render_terrain_sample_t);
    ecs_os_memset_t(s, 0, box_render_terrain_sample_t);
    s->terrain = 2000;
    s->mesh = 9;
    box_render_grass_t g = grass_record();
    g.grass = 10;
    g.terrain = 0;
    g.mask = 0;
    *ecs_vec_append_t(NULL, &p.grasses, box_render_grass_t) = g;
    g.grass = 11;
    g.terrain = 1000;
    g.mask = 1;
    *ecs_vec_append_t(NULL, &p.grasses, box_render_grass_t) = g;
    g.grass = 12;
    g.terrain = 1000;
    g.mask = 2;
    *ecs_vec_append_t(NULL, &p.grasses, box_render_grass_t) = g;
    box_render_grass_far_t far;

    // The tiles of a terrain draw the far field of the first Grass on it
    test_bool(box_render_grass_far_of(&p, 7, &far), true);
    test_uint(far.config[0], 2);
    test_uint(far.config[1], 1);

    // A terrain without a Grass draws as before
    far.config[1] = 5;
    test_bool(box_render_grass_far_of(&p, 9, &far), false);
    test_uint(far.config[1], 0);
    test_flt(far.range[1], 0);

    // As do the blends of meshes that are no terrain
    test_bool(box_render_grass_far_of(&p, 0, &far), false);
    test_uint(far.config[1], 0);
    test_bool(box_render_grass_far_of(&p, 8, &far), false);
    test_uint(far.config[1], 0);

    ecs_vec_fini_t(NULL, &p.terrain_samples, box_render_terrain_sample_t);
    ecs_vec_fini_t(NULL, &p.grasses, box_render_grass_t);
}
