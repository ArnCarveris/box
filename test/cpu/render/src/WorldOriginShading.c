/**
 * @file WorldOriginShading.c
 * @brief Tests for the absolute position of the local origin (WorldOrigin) as the records of the
 * world space patterns pack it: wrapped in double precision to the period of each pattern, 0 at
 * an origin of 0, and continuous across a shift by whole steps. The material records do not
 * hold it: the shaders add the projection of the origin themselves.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "draw.h"

/** Distance in repeats between two phases modulo 1, 0..0.5.
 *
 * @param a The first phase
 * @param b The second phase
 * @return The distance
 */
static double wos_phase_distance(
    double a,
    double b)
{
    double d = fmod(a - b, 1.0);
    d = d < 0 ? d + 1.0 : d;

    return d > 0.5 ? 1.0 - d : d;
}

void WorldOriginShading_wrap(void) {
    // render/world_origin_shading
    test_flt(box_render_origin_wrap(7, 3), 1);
    test_flt(box_render_origin_wrap(-1, 3), 2);
    test_flt(box_render_origin_wrap(-6, 3), 0);
    test_flt(box_render_origin_wrap(NAN, 3), 0);
    test_flt(box_render_origin_wrap(5, 0), 0);
    test_assert(box_render_origin_wrap_float(-1e-12, 1.0) < 1.0f);
    test_assert(box_render_origin_wrap_float(-1e-12, 1.0) >= 0.0f);
}

void WorldOriginShading_origin_of_world(void) {
    // render/world_origin_shading
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    double origin[3];
    box_render_origin(world, origin);
    test_assert(origin[0] == 0 && origin[1] == 0 && origin[2] == 0);
    ecs_singleton_set(world, WorldOrigin, {.x = 1e6, .y = -256, .z = 2.5e9, .enabled = false});
    box_render_origin(world, origin);
    test_assert(origin[0] == 1e6);
    test_assert(origin[1] == -256);
    test_assert(origin[2] == 2.5e9);
    ecs_fini(world);
}

void WorldOriginShading_mapping_pack(void) {
    // render/world_origin_shading
    float out[4];

    /* At an origin of 0 the offset is 0 */
    const double zero[3] = {0, 0, 0};
    box_render_origin_mapping(zero, 3, 2, 0.5f, out);
    test_flt(out[0], 0);
    test_flt(out[1], 0);
    test_flt(out[2], 0);
    test_flt(out[3], 0);

    /* The origin projected as mapping_object does, wrapped to whole repeats */
    const double far[3] = {1e6 + 1, 4, -2.5};
    box_render_origin_mapping(far, 3, 2, 0.5f, out);
    test_assert(wos_phase_distance(out[0], (1e6 + 1) / 3 * 2) < 1e-6);
    test_assert(wos_phase_distance(out[1], 4.0 / 3 * 0.5) < 1e-6);
    test_assert(wos_phase_distance(out[2], -2.5 / 3 * 2) < 1e-6);
    test_assert(wos_phase_distance(out[3], (1e6 + 1) / 3 * 0.5) < 1e-6);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        test_assert(out[i] >= 0 && out[i] < 1);
    }

    /* A size that is not above 0 maps as a size of 1 */
    box_render_origin_mapping(far, 0, 1, 1, out);
    test_assert(wos_phase_distance(out[1], 4) < 1e-6);
    test_assert(wos_phase_distance(out[2], -2.5) < 1e-6);
}

void WorldOriginShading_mapping_record(void) {
    // render/world_origin_shading
    /* The shaders add the projection of the origin: the record of a world mapped material is
     * the same at any origin */
    TextureMapping mapping = {.mode = MappingWorld, .size = 3};
    TextureTransform uv = {.scale_x = 2, .scale_y = 0.5f};
    box_render_material_t at_zero, far;
    box_draw_material_t m = {.fade = 1, .mapping = &mapping, .uv = &uv};
    ecs_os_memset_t(&at_zero, 0, box_render_material_t);
    ecs_os_memset_t(&far, 0, box_render_material_t);
    box_draws_material_pack(&at_zero, &m);
    m.origin[0] = 1e6 + 1;
    m.origin[1] = 4;
    m.origin[2] = -2.5;
    box_draws_material_pack(&far, &m);
    test_int(ecs_os_memcmp(&at_zero, &far, ECS_SIZEOF(box_render_material_t)), 0);
}

void WorldOriginShading_water_ripple_pack(void) {
    // render/world_origin_shading
    TextureMapping mapping = {.mode = MappingWorld, .size = 4};
    Water water = {.depth = 1, .visibility = 1, .ripple_scale = 1.5f};
    box_render_material_t at_zero, far;
    box_draw_material_t m = {.fade = 1, .mapping = &mapping, .water = &water};
    box_draws_material_pack(&at_zero, &m);
    test_flt(at_zero.water_ripple_offset[0], 0);
    test_flt(at_zero.water_ripple_offset[1], 0);

    /* The second normal sample takes the whole periods of BOX_RENDER_WATER_ORIGIN_PERIOD the
     * shaders leave out of the projection, times the ripple scale */
    m.origin[0] = 4.0 * (BOX_RENDER_WATER_ORIGIN_PERIOD + 0.25);
    m.origin[2] = 4.0 * (3 * BOX_RENDER_WATER_ORIGIN_PERIOD + 0.5);
    box_draws_material_pack(&far, &m);
    test_assert(wos_phase_distance(far.water_ripple_offset[0],
        1.5 * 3 * BOX_RENDER_WATER_ORIGIN_PERIOD) < 1e-5);
    test_assert(wos_phase_distance(far.water_ripple_offset[1],
        -1.5 * BOX_RENDER_WATER_ORIGIN_PERIOD) < 1e-5);

    /* Without a world projection it does not */
    m.mapping = NULL;
    box_draws_material_pack(&far, &m);
    test_flt(far.water_ripple_offset[0], 0);
    test_flt(far.water_ripple_offset[1], 0);
}

void WorldOriginShading_mapping_continuous(void) {
    // render/world_origin_shading
    /* A point at local p before a shift by 256 is at p - 256 after it: the projection of the
     * local position plus the offset is the same modulo whole repeats */
    double origin[3] = {999936, 0, -512};
    float before[4], after[4];
    box_render_origin_mapping(origin, 3, 1, 1, before);
    origin[0] += 256;
    origin[2] += 256;
    box_render_origin_mapping(origin, 3, 1, 1, after);
    double p = 100.25;
    test_assert(wos_phase_distance(p / 3 + before[0], (p - 256) / 3 + after[0]) < 1e-5);
    test_assert(wos_phase_distance(p / 3 + before[2], (p - 256) / 3 + after[2]) < 1e-5);
}

void WorldOriginShading_layer_pack(void) {
    // render/world_origin_shading
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_texture_layer_t v;
    ecs_entity_t layer = ecs_new(world);
    ecs_add(world, layer, TextureLayer);
    ecs_set(world, layer, TextureMapping, {.mode = MappingWorld, .size = 3});
    box_draws_layer_pack(world, layer, 0.0, &v);
    test_flt(v.origin[0], 0);
    test_flt(v.origin[1], 0);
    test_flt(v.origin[2], 0);
    test_flt(v.origin[3], 0);

    ecs_singleton_set(world, WorldOrigin, {.x = 1000, .y = 2, .z = -7, .enabled = false});
    box_draws_layer_pack(world, layer, 0.0, &v);
    test_assert(wos_phase_distance(v.origin[0], 1000.0 / 3) < 1e-6);
    test_assert(wos_phase_distance(v.origin[1], 2.0 / 3) < 1e-6);
    test_assert(wos_phase_distance(v.origin[2], -7.0 / 3) < 1e-6);
    test_assert(wos_phase_distance(v.origin[3], 1000.0 / 3) < 1e-6);

    /* A layer on the UVs of the mesh ignores the origin */
    ecs_set(world, layer, TextureMapping, {.mode = MappingUv, .size = 3});
    box_draws_layer_pack(world, layer, 0.0, &v);
    test_flt(v.origin[0], 0);
    test_flt(v.origin[2], 0);
    ecs_fini(world);
}

void WorldOriginShading_wind_frame(void) {
    // render/world_origin_shading
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 4, .gust = 0.5f,
        .gust_scale = 5});
    WindStatus *s = ecs_ensure(world, ecs_id(Wind), WindStatus);
    test_assert(s != NULL);
    s->offset_x = 12.5;
    s->offset_z = 3;
    s->sway = 0.25;
    s->clock = 1234.5;
    ecs_modified(world, ecs_id(Wind), WindStatus);

    box_render_wind_t at_zero, far;
    box_render_wind_frame(world, &at_zero);
    test_flt(at_zero.offset[0], 12.5f);
    test_flt(at_zero.offset[1], 3);
    test_flt(at_zero.sway, 0.25f);
    test_assert(at_zero.origin_y == 0);

    /* The gust field moves back by the origin, wrapped to its period of 32 patches of 5 */
    ecs_singleton_set(world, WorldOrigin, {.x = 1000, .y = 50, .z = -300, .enabled = false});
    box_render_wind_frame(world, &far);
    test_flt(far.offset[0], (float)box_render_origin_wrap(12.5 - 1000, 160));
    test_flt(far.offset[1], (float)box_render_origin_wrap(3 + 300, 160));

    /* The clock of the flutter and the branches does not move with the origin */
    test_assert(at_zero.clock == 1234.5);
    test_assert(far.clock == at_zero.clock);
    test_assert(far.offset[0] >= 0 && far.offset[0] < 160);
    test_assert(far.origin_y == 50);

    /* The wave of the sway, along the wind: half a radian per gust patch */
    test_assert(wos_phase_distance(far.sway,
        0.25 - 0.5 * 1000.0 * 0.2 / (2.0 * GLM_PI)) < 1e-5);
    ecs_fini(world);
}

void WorldOriginShading_wind_pack_base(void) {
    // render/world_origin_shading
    WindSway sway = {.bend = 1, .height = 2, .base = 3};
    box_render_wind_t wind = {.direction = {1, 0}, .speed = 2, .frequency = 0.05f,
        .origin_y = 100};
    box_render_material_t record;
    ecs_os_memset_t(&record, 0, box_render_material_t);
    box_render_wind_pack(&record, &sway, &wind);
    test_flt(record.wind_base, -97);

    wind.origin_y = 0;
    box_render_wind_pack(&record, &sway, &wind);
    test_flt(record.wind_base, 3);
}
