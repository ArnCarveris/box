/**
 * @file Bake.c
 * @brief Tests for static light classification, bake settings, emitters and the probe math.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Bake_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
}

void Bake_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Bake_static_light_is_static(void)
{
    // lighting/light_baking
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, PointLight, {.intensity = 2, .range = 10});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
    box_light_t *light = box_vec_at(&packet.lights, 0);
    test_int(light->is_static, 1);
}

void Bake_dynamic_light_is_not_static(void)
{
    // lighting/light_baking
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, DynamicTransform);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, PointLight, {.intensity = 2, .range = 10});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
    box_light_t *light = box_vec_at(&packet.lights, 0);
    test_int(light->is_static, 0);
}

void Bake_light_under_dynamic_parent_is_dynamic(void)
{
    // lighting/light_baking
    ecs_entity_t parent = ecs_new(w);
    ecs_add(w, parent, DynamicTransform);
    ecs_set(w, parent, Position, {0, 0, 0});
    ecs_entity_t e = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, PointLight, {.intensity = 2, .range = 10});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
    box_light_t *light = box_vec_at(&packet.lights, 0);
    test_int(light->is_static, 0);
}

void Bake_static_lights_are_listed_unculled(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t behind = ecs_new(w);
    ecs_set(w, behind, Position, {0, 0, -20});
    ecs_set(w, behind, PointLight, {.intensity = 2, .range = 2});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 0);
    test_int(packet.bake_lights.count, 1);
    box_light_t *light = box_vec_at(&packet.bake_lights, 0);
    test_flt(light->position[2], -20);
    test_int(light->is_static, 1);
}

void Bake_dynamic_light_not_in_bake_lights(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, DynamicTransform);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, PointLight, {.intensity = 2, .range = 10});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
    test_int(packet.bake_lights.count, 0);
}

void Bake_bake_lights_empty_when_disabled(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = false});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, PointLight, {.intensity = 2, .range = 10});
    box_render_extract(w, &packet);
    test_int(packet.bake.enabled, 0);
    test_int(packet.lights.count, 1);
    test_int(packet.bake_lights.count, 0);
}

void Bake_settings_defaults(void)
{
    // lighting/bake_resolution
    box_render_extract(w, &packet);
    test_int(packet.bake.enabled, 1);
    test_int(packet.bake.budget, BOX_BAKE_BUDGET);
    test_flt(packet.bake.spacing, BOX_BAKE_SPACING);
    test_int(packet.bake.probe_max, BOX_BAKE_PROBE_MAX);
    test_int(packet.bake.x, BOX_BAKE_GRID_X);
    test_int(packet.bake.y, BOX_BAKE_GRID_Y);
    test_int(packet.bake.z, BOX_BAKE_GRID_Z);
    test_int(packet.bake.steps, BOX_BAKE_STEPS);
}

void Bake_settings_override(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {
        .enabled = true, .budget = 8, .x = 16, .y = 4, .z = 16, .steps = 12});
    box_render_extract(w, &packet);
    test_int(packet.bake.enabled, 1);
    test_int(packet.bake.budget, 8);
    test_int(packet.bake.x, 16);
    test_int(packet.bake.y, 4);
    test_int(packet.bake.z, 16);
    test_int(packet.bake.steps, 12);
}

void Bake_emissive_static_is_an_emitter(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 2, 3});
    ecs_set(w, e, Box, {2, 2, 2});
    ecs_set(w, e, Emissive, {.strength = 4, .color = {255, 0, 0}});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.emitters.count, 1);
    box_emitter_t *emitter = box_vec_at(&packet.emitters, 0);
    test_flt(emitter->position[0], 1);
    test_flt(emitter->position[1], 2);
    test_flt(emitter->position[2], 3);
    test_assert(fabsf(emitter->radius - sqrtf(3.0f)) < 1e-4f);
    test_flt(emitter->color[0], 4);
    test_flt(emitter->color[1], 0);
    test_assert(emitter->range > emitter->radius);
}

void Bake_emissive_dynamic_is_not_an_emitter(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, DynamicTransform);
    ecs_set(w, e, Position, {1, 2, 3});
    ecs_set(w, e, Box, {2, 2, 2});
    ecs_set(w, e, Emissive, {.strength = 4, .color = {255, 0, 0}});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.emitters.count, 0);
}

void Bake_emitters_empty_when_disabled(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = false});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 2, 3});
    ecs_set(w, e, Box, {2, 2, 2});
    ecs_set(w, e, Emissive, {.strength = 4, .color = {255, 0, 0}});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.emitters.count, 0);
}

void Bake_static_instance_is_flagged_baked(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_entity_t moving = ecs_new(w);
    ecs_add(w, moving, DynamicTransform);
    ecs_set(w, moving, Position, {2, 0, 5});
    ecs_set(w, moving, Box, {1, 1, 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    test_int(packet.dynamic_instances.count, 1);
    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    box_instance_t *dynamic = box_vec_at(&packet.dynamic_instances, 0);
    test_flt(update->data.params[2], 1);
    test_flt(dynamic->params[2], 0);
}

void Bake_toggle_requeues_every_instance(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = false});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    test_flt(((box_static_update_t*)box_vec_at(&packet.static_updates, 0))->data.params[2], 0);

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);

    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    test_flt(((box_static_update_t*)box_vec_at(&packet.static_updates, 0))->data.params[2], 1);
}

void Bake_sh_reconstructs_the_light_direction(void)
{
    // lighting/light_baking
    box_probe_t probe;
    memset(&probe, 0, sizeof(box_probe_t));
    float dir[3] = {0, 1, 0};
    float value[3] = {1, 1, 1};
    box_sh_add(&probe, dir, value);

    float up[3] = {0, 1, 0};
    float down[3] = {0, -1, 0};
    float side[3] = {1, 0, 0};
    float lit[3];
    float dark[3];
    float flat[3];
    box_sh_eval(&probe, up, lit);
    box_sh_eval(&probe, down, dark);
    box_sh_eval(&probe, side, flat);
    test_assert(fabsf(lit[0] - 0.75f) < 1e-3f);
    test_flt(dark[0], 0);
    test_assert(fabsf(flat[0] - 0.25f) < 1e-3f);
    test_assert(lit[1] == lit[0] && lit[2] == lit[0]);
}

void Bake_voxel_grid_marks_static_geometry(void)
{
    // lighting/light_baking
    float min[3] = {-4, -1, -4};
    float max[3] = {4, 3, 4};
    uint32_t dim[3] = {32, 16, 32};
    box_voxel_grid_t grid;
    box_voxel_grid_build(min, max, dim, &grid);
    test_int(box_voxel_words(&grid), 32 * 16 * 32 / 32);

    box_probe_desc_t wall = {{0, 1, 0, 2.0f}, {3, 1, 0.25f, 1}};
    uint32_t *bits = ecs_os_calloc_n(uint32_t, box_voxel_words(&grid));
    box_bake_grid_build(&grid, &wall, 1, bits);

    float inside[3] = {0, 1, 0};
    float outside[3] = {0, 1, 3};
    test_assert(box_voxel_solid(&grid, bits, inside));
    test_assert(!box_voxel_solid(&grid, bits, outside));
    ecs_os_free(bits);
}

void Bake_probe_behind_a_wall_is_darker(void)
{
    // lighting/light_baking
    box_probe_desc_t descs[3] = {
        {{0, 1, 0, 3.0f}, {4, 2, 0.2f, 1}},
        {{0, 1, -3, 0.9f}, {0.5f, 0.5f, 0.5f, 1}},
        {{0, 1, 3, 0.9f}, {0.5f, 0.5f, 0.5f, 1}}
    };
    float min[3] = {-5, -2, -5};
    float max[3] = {5, 4, 5};
    uint32_t dim[3] = {64, 32, 64};
    box_voxel_grid_t grid;
    box_voxel_grid_build(min, max, dim, &grid);
    uint32_t *bits = ecs_os_calloc_n(uint32_t, box_voxel_words(&grid));
    box_bake_grid_build(&grid, descs, 3, bits);

    box_light_t light;
    memset(&light, 0, sizeof(box_light_t));
    light.position[2] = -6;
    light.position[1] = 1;
    light.range = 20;
    light.intensity = 10;
    light.color[0] = 1;
    light.color[1] = 1;
    light.color[2] = 1;
    light.cos_inner = 1;
    light.cos_outer = -1;
    light.is_static = 1;

    box_bake_scene_t scene = {
        .probes = descs, .probe_count = 3, .lights = &light, .light_count = 1,
        .occupancy = bits, .grid = grid, .steps = 64
    };
    box_probe_t front;
    box_probe_t behind;
    box_bake_probe_cpu(&scene, 1, &front);
    box_bake_probe_cpu(&scene, 2, &behind);

    float normal[3] = {0, 0, -1};
    float lit[3];
    float shadowed[3];
    box_sh_eval(&front, normal, lit);
    box_sh_eval(&behind, normal, shadowed);
    test_assert(lit[0] > 0.05f);
    test_flt(shadowed[0], 0);
    ecs_os_free(bits);
}

void Bake_probe_is_not_occluded_by_its_own_bounds(void)
{
    // lighting/light_baking
    box_probe_desc_t desc = {{0, 0, 0, 3.5f}, {2, 2, 2, 1}};
    float min[3] = {-4, -4, -4};
    float max[3] = {4, 12, 4};
    uint32_t dim[3] = {32, 64, 32};
    box_voxel_grid_t grid;
    box_voxel_grid_build(min, max, dim, &grid);
    uint32_t *bits = ecs_os_calloc_n(uint32_t, box_voxel_words(&grid));
    box_bake_grid_build(&grid, &desc, 1, bits);

    float center[3] = {0, 0, 0};
    test_assert(box_voxel_solid(&grid, bits, center));

    box_light_t light;
    memset(&light, 0, sizeof(box_light_t));
    light.position[1] = 8;
    light.range = 20;
    light.intensity = 10;
    light.color[0] = 1;
    light.color[1] = 1;
    light.color[2] = 1;
    light.cos_inner = 1;
    light.cos_outer = -1;

    box_bake_scene_t scene = {
        .probes = &desc, .probe_count = 1, .lights = &light, .light_count = 1,
        .occupancy = bits, .grid = grid, .steps = 32
    };
    box_probe_t probe;
    box_bake_probe_cpu(&scene, 0, &probe);

    float up[3] = {0, 1, 0};
    float out[3];
    box_sh_eval(&probe, up, out);
    test_assert(out[0] > 0.05f);
    ecs_os_free(bits);
}

void Bake_emitter_lights_a_probe(void)
{
    // lighting/light_baking
    box_probe_desc_t descs[2] = {
        {{0, 0, 0, 0.9f}, {0.5f, 0.5f, 0.5f, 1}},
        {{0, 0, 3, 0.9f}, {0.5f, 0.5f, 0.5f, 1}}
    };
    float min[3] = {-4, -4, -4};
    float max[3] = {4, 4, 8};
    uint32_t dim[3] = {32, 32, 48};
    box_voxel_grid_t grid;
    box_voxel_grid_build(min, max, dim, &grid);
    uint32_t *bits = ecs_os_calloc_n(uint32_t, box_voxel_words(&grid));
    box_bake_grid_build(&grid, descs, 2, bits);

    float color[3] = {4, 2, 1};
    box_emitter_t emitter;
    memset(&emitter, 0, sizeof(box_emitter_t));
    emitter.position[2] = 3;
    emitter.radius = 0.9f;
    emitter.color[0] = color[0];
    emitter.color[1] = color[1];
    emitter.color[2] = color[2];
    emitter.range = box_bake_emitter_range(color, emitter.radius);

    box_bake_scene_t scene = {
        .probes = descs, .probe_count = 2, .emitters = &emitter, .emitter_count = 1,
        .occupancy = bits, .grid = grid, .steps = 32
    };
    box_probe_t probe;
    box_bake_probe_cpu(&scene, 0, &probe);

    float normal[3] = {0, 0, 1};
    float out[3];
    box_sh_eval(&probe, normal, out);
    test_assert(out[0] > 0.01f);
    test_assert(out[0] > out[1]);
    test_assert(out[1] > out[2]);
    ecs_os_free(bits);
}

void Bake_moved_instance_marks_its_probe(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);
    test_int(state.probe_count, 1);
    test_int(state.dirty.count, 1);
    test_int(box_bake_state_take(&state, 16), 1);
    test_int(state.dirty.count, 0);

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_set(w, e, Position, {0.5f, 0, 5});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    box_bake_state_apply(&state, &packet);
    test_int(state.dirty.count, 1);
    box_bake_state_fini(&state);
}

void Bake_moved_instance_marks_only_its_neighbourhood(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t light = ecs_new(w);
    ecs_set(w, light, Position, {0, 0, 0});
    ecs_set(w, light, PointLight, {.intensity = 5, .range = 8});

    int32_t i;
    ecs_entity_t moved = 0;
    for (i = 0; i < 400; i ++) {
        ecs_entity_t e = ecs_new(w);
        ecs_set(w, e, Position, {(float)i - 200.0f, 0, 5});
        ecs_set(w, e, Box, {0.8f, 0.8f, 0.8f});
        moved = i == 200 ? e : moved;
    }

    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);
    test_int(state.probe_count, 400);
    test_int(box_bake_state_take(&state, 1024), 400);

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_set(w, moved, Position, {0.2f, 0, 5});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    box_bake_state_apply(&state, &packet);
    test_assert(state.dirty.count >= 1);
    test_assert(state.dirty.count < 40);
    box_bake_state_fini(&state);
}

void Bake_moved_light_marks_the_probes_in_range(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t light = ecs_new(w);
    ecs_set(w, light, Position, {0, 0, 5});
    ecs_set(w, light, PointLight, {.intensity = 5, .range = 6});

    int32_t i;
    for (i = 0; i < 40; i ++) {
        ecs_entity_t e = ecs_new(w);
        ecs_set(w, e, Position, {(float)i - 20.0f, 0, 5});
        ecs_set(w, e, Box, {0.8f, 0.8f, 0.8f});
    }

    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);
    test_int(box_bake_state_take(&state, 1024), 40);

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_set(w, light, Position, {1, 0, 5});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    box_bake_state_apply(&state, &packet);
    test_assert(state.dirty.count >= 8);
    test_assert(state.dirty.count < 40);
    box_bake_state_fini(&state);
}

void Bake_budget_limits_the_probes_per_frame(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    int32_t i;
    for (i = 0; i < 20; i ++) {
        ecs_entity_t e = ecs_new(w);
        ecs_set(w, e, Position, {(float)i, 0, 5});
        ecs_set(w, e, Box, {1, 1, 1});
    }

    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);
    test_int(state.dirty.count, 20);
    test_int(box_bake_state_take(&state, 5), 5);
    test_int(state.dirty.count, 15);
    test_int(box_bake_state_take(&state, 5), 5);
    test_int(state.dirty.count, 10);
    test_int(box_bake_state_take(&state, 100), 10);
    test_int(state.dirty.count, 0);
    box_bake_state_fini(&state);
}

void Bake_nearest_probes_are_baked_first(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t near = ecs_new(w);
    ecs_set(w, near, Position, {0, 0, 4});
    ecs_set(w, near, Box, {1, 1, 1});
    ecs_entity_t far = ecs_new(w);
    ecs_set(w, far, Position, {0, 0, 40});
    ecs_set(w, far, Box, {1, 1, 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);
    test_int(state.dirty.count, 2);
    test_int(box_bake_state_take(&state, 1), 1);

    const RenderInstance *inst = ecs_get(w, near, RenderInstance);
    box_bake_mesh_t *mesh = box_bake_state_mesh(&state, inst->mesh);
    test_int(mesh->jobs.count, 1);
    test_int(*(uint32_t*)box_vec_at(&mesh->jobs, 0), inst->slot);
    box_bake_state_fini(&state);
}

void Bake_disabled_settings_drop_the_probes(void)
{
    // lighting/light_baking
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);
    test_int(state.probe_count, 1);
    test_assert(state.enabled);

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_singleton_set(w, BakeSettings, {.enabled = false});
    box_render_extract(w, &packet);
    box_bake_state_apply(&state, &packet);
    test_assert(!state.enabled);
    test_int(state.probe_count, 0);
    test_int(state.dirty.count, 0);
    box_bake_state_fini(&state);
}

void Bake_baking_is_enabled_by_default(void)
{
    // lighting/bake_resolution
    const BakeSettings *s = ecs_singleton_get(w, BakeSettings);
    test_assert(s != NULL);
    test_bool(s->enabled, true);
    test_int(s->budget, BOX_BAKE_BUDGET);
    test_flt(s->probe_spacing, BOX_BAKE_SPACING);
    test_int(s->probe_max, BOX_BAKE_PROBE_MAX);
}

void Bake_grid_dims_from_bounds_and_spacing(void)
{
    // lighting/bake_resolution
    uint32_t dim[3];
    float small[3] = {0.5f, 0.5f, 0.5f};
    box_probe_dims(small, 1.0f, 16, dim);
    test_int(dim[0], 1);
    test_int(dim[1], 1);
    test_int(dim[2], 1);

    float tile[3] = {1.0f, 0.2f, 1.0f};
    box_probe_dims(tile, 1.0f, 16, dim);
    test_int(dim[0], 2);
    test_int(dim[1], 1);
    test_int(dim[2], 2);

    float floor_[3] = {5.0f, 0.1f, 5.0f};
    box_probe_dims(floor_, 1.0f, 16, dim);
    test_int(dim[0], 10);
    test_int(dim[1], 1);
    test_int(dim[2], 10);

    box_probe_dims(floor_, 2.5f, 16, dim);
    test_int(dim[0], 4);
    test_int(dim[1], 1);
    test_int(dim[2], 4);
}

void Bake_grid_dims_clamped_to_the_maximum(void)
{
    // lighting/bake_resolution
    uint32_t dim[3];
    float ground[3] = {45.0f, 0.25f, 45.0f};
    box_probe_dims(ground, 1.0f, 8, dim);
    test_int(dim[0], 8);
    test_int(dim[1], 1);
    test_int(dim[2], 8);

    box_probe_dims(ground, 1.0f, 32, dim);
    test_int(dim[0], 32);
    test_int(dim[2], 32);

    box_probe_dims(ground, 1.0f, 0, dim);
    test_int(dim[0], 1);
    test_int(dim[2], 1);

    float zero[3] = {0, 0, 0};
    box_probe_dims(zero, 1.0f, 8, dim);
    test_int(dim[0], 1);
    test_int(dim[1], 1);
    test_int(dim[2], 1);
}

void Bake_large_instance_gets_a_probe_grid(void)
{
    // lighting/bake_resolution
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, Box, {4, 0.5f, 4});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);
    test_int(state.probe_count, 4 * 1 * 4);
    test_int(state.dirty.count, 1);

    const RenderInstance *inst = ecs_get(w, e, RenderInstance);
    box_bake_mesh_t *mesh = box_bake_state_mesh(&state, inst->mesh);
    const box_probe_desc_t *desc = box_vec_at(&mesh->descs, inst->slot);
    test_int(box_probe_dim(desc, 0), 4);
    test_int(box_probe_dim(desc, 1), 1);
    test_int(box_probe_dim(desc, 2), 4);
    test_int(box_probe_count(desc), 16);

    test_int(box_bake_state_take(&state, 64), 16);
    test_int(mesh->jobs.count, 16);
    box_bake_slot_t *job = box_vec_at(&mesh->jobs, 5);
    test_int(job->slot, inst->slot);
    test_int(job->index, 5);
    box_bake_state_fini(&state);
}

void Bake_pool_reuses_the_probes_of_a_removed_instance(void)
{
    // lighting/bake_resolution
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t big = ecs_new(w);
    ecs_set(w, big, Position, {0, 0, 5});
    ecs_set(w, big, Box, {4, 0.5f, 4});
    ecs_entity_t small = ecs_new(w);
    ecs_set(w, small, Position, {12, 0, 5});
    ecs_set(w, small, Box, {1, 1, 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);
    test_int(state.probe_count, 17);

    const RenderInstance *inst = ecs_get(w, small, RenderInstance);
    uint32_t mesh_handle = inst->mesh;
    box_bake_mesh_t *mesh = box_bake_state_mesh(&state, mesh_handle);
    const box_probe_desc_t *desc = box_vec_at(&mesh->descs, inst->slot);
    test_int(desc->grid[0], 16);

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_delete(w, big);
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    box_bake_state_apply(&state, &packet);
    test_int(state.probe_count, 1);

    inst = ecs_get(w, small, RenderInstance);
    mesh = box_bake_state_mesh(&state, mesh_handle);
    test_int(mesh->descs.count, 1);
    desc = box_vec_at(&mesh->descs, inst->slot);
    test_int(desc->grid[0], 0);
    test_int(box_probe_count(desc), 1);
    box_bake_state_fini(&state);
}

void Bake_grid_probe_sits_on_the_bounds(void)
{
    // lighting/bake_resolution
    box_probe_desc_t desc;
    memset(&desc, 0, sizeof(box_probe_desc_t));
    desc.position[0] = 1;
    desc.position[1] = 2;
    desc.position[2] = 3;
    desc.extent[0] = 5;
    desc.extent[1] = 0.5f;
    desc.extent[2] = 4;
    desc.extent[3] = 1;
    desc.span[0][0] = 10;
    desc.span[1][1] = 1;
    desc.span[2][2] = 8;
    desc.grid[0] = 7;
    desc.grid[1] = 3;
    desc.grid[2] = 1;
    desc.grid[3] = 2;
    test_int(box_probe_count(&desc), 6);

    float p[3];
    box_probe_position(&desc, 0, p);
    test_flt(p[0], -4);
    test_flt(p[1], 2);
    test_flt(p[2], -1);

    box_probe_position(&desc, 2, p);
    test_flt(p[0], 6);
    test_flt(p[2], -1);

    box_probe_position(&desc, 4, p);
    test_flt(p[0], 1);
    test_flt(p[2], 7);

    float own[3];
    box_probe_own(&desc, own);
    test_assert(fabsf(own[0] - 5.0f / 3.0f) < 1e-5f);
    test_flt(own[1], 0.5f);
    test_flt(own[2], 2);

    float coord[3];
    box_probe_position(&desc, 5, p);
    box_probe_coord(&desc, p, coord);
    test_flt(coord[0], 2);
    test_flt(coord[1], 0);
    test_flt(coord[2], 1);
}

void Bake_grid_probe_follows_the_instance_scale(void)
{
    // lighting/bake_resolution
    ecs_singleton_set(w, BakeSettings, {.enabled = true});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {2, 0, 6});
    ecs_set(w, e, Box, {4, 0.5f, 2});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);

    const RenderInstance *inst = ecs_get(w, e, RenderInstance);
    box_bake_mesh_t *mesh = box_bake_state_mesh(&state, inst->mesh);
    const box_probe_desc_t *desc = box_vec_at(&mesh->descs, inst->slot);
    test_int(box_probe_dim(desc, 0), 4);
    test_int(box_probe_dim(desc, 1), 1);
    test_int(box_probe_dim(desc, 2), 2);

    float p[3];
    box_probe_position(desc, 0, p);
    test_flt(p[0], 0);
    test_flt(p[1], 0);
    test_flt(p[2], 5);

    box_probe_position(desc, 3, p);
    test_flt(p[0], 4);
    test_flt(p[2], 5);

    box_probe_position(desc, 7, p);
    test_flt(p[0], 4);
    test_flt(p[2], 7);

    float coord[3];
    float middle[3] = {2, 0, 6};
    box_probe_coord(desc, middle, coord);
    test_flt(coord[0], 1.5f);
    test_flt(coord[1], 0);
    test_flt(coord[2], 0.5f);
    box_bake_state_fini(&state);
}

void Bake_trilinear_weights_at_the_corners(void)
{
    // lighting/bake_resolution
    box_probe_desc_t desc;
    memset(&desc, 0, sizeof(box_probe_desc_t));
    desc.extent[3] = 1;
    desc.span[0][0] = 2;
    desc.span[1][1] = 2;
    desc.span[2][2] = 2;
    desc.grid[0] = 5;
    desc.grid[1] = 2;
    desc.grid[2] = 2;
    desc.grid[3] = 2;

    uint32_t index[8];
    float weight[8];
    float low[3] = {0, 0, 0};
    box_probe_weights(&desc, low, index, weight);
    test_int(index[0], 5);
    test_flt(weight[0], 1);
    int i;
    for (i = 1; i < 8; i ++) {
        test_flt(weight[i], 0);
    }

    float high[3] = {1, 1, 1};
    box_probe_weights(&desc, high, index, weight);
    test_int(index[0], 12);
    test_flt(weight[0], 1);
    for (i = 1; i < 8; i ++) {
        test_int(index[i], 12);
        test_flt(weight[i], 0);
    }

    float outside[3] = {-4, 9, 3};
    box_probe_weights(&desc, outside, index, weight);
    test_int(index[0], 5 + 2 + 4);
    test_flt(weight[0], 1);
    test_int(index[1], 5 + 1 + 2 + 4);
    test_flt(weight[1], 0);
}

void Bake_trilinear_weights_at_the_center(void)
{
    // lighting/bake_resolution
    box_probe_desc_t desc;
    memset(&desc, 0, sizeof(box_probe_desc_t));
    desc.extent[3] = 1;
    desc.span[0][0] = 2;
    desc.span[1][1] = 2;
    desc.span[2][2] = 2;
    desc.grid[0] = 0;
    desc.grid[1] = 2;
    desc.grid[2] = 2;
    desc.grid[3] = 2;

    uint32_t index[8];
    float weight[8];
    float center[3] = {0.5f, 0.5f, 0.5f};
    box_probe_weights(&desc, center, index, weight);
    float sum = 0;
    int i;
    for (i = 0; i < 8; i ++) {
        test_flt(weight[i], 0.125f);
        test_int(index[i], (uint32_t)i);
        sum += weight[i];
    }

    test_flt(sum, 1);

    float edge[3] = {0.25f, 0, 0};
    box_probe_weights(&desc, edge, index, weight);
    test_flt(weight[0], 0.75f);
    test_flt(weight[1], 0.25f);
    test_flt(weight[2], 0);
}

void Bake_trilinear_sample_mixes_the_corners(void)
{
    // lighting/bake_resolution
    box_probe_desc_t desc;
    memset(&desc, 0, sizeof(box_probe_desc_t));
    desc.extent[3] = 1;
    desc.span[0][0] = 2;
    desc.span[1][1] = 2;
    desc.span[2][2] = 2;
    desc.grid[1] = 2;
    desc.grid[2] = 1;
    desc.grid[3] = 1;

    box_probe_t pool[2];
    memset(pool, 0, sizeof(pool));
    float dir[3] = {0, 1, 0};
    float dark[3] = {0, 0, 0};
    float bright[3] = {4, 4, 4};
    box_sh_add(&pool[0], dir, dark);
    box_sh_add(&pool[1], dir, bright);

    box_probe_t out;
    float up[3] = {0, 1, 0};
    float value[3];
    float at_dark[3] = {0, 0, 0};
    box_probe_sample(&desc, pool, at_dark, &out);
    box_sh_eval(&out, up, value);
    test_flt(value[0], 0);

    float middle[3] = {0.5f, 0, 0};
    box_probe_sample(&desc, pool, middle, &out);
    box_sh_eval(&out, up, value);
    test_assert(fabsf(value[0] - 1.5f) < 1e-3f);

    float at_bright[3] = {1, 0, 0};
    box_probe_sample(&desc, pool, at_bright, &out);
    box_sh_eval(&out, up, value);
    test_assert(fabsf(value[0] - 3.0f) < 1e-3f);
}
