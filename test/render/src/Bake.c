#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/renderer.h"

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t view;

static void Bake_set_settings(
    BakeSettings value)
{
    View *settings = ecs_get_mut(w, view, View);
    settings->bake = value;
    ecs_modified(w, view, View);
}

void Bake_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
}

void Bake_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Bake_static_light_is_static(void)
{
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
    Bake_set_settings((BakeSettings){.enabled = true});
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
    Bake_set_settings((BakeSettings){.enabled = true});
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
    Bake_set_settings((BakeSettings){.enabled = false});
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
    Bake_set_settings((BakeSettings){
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
    Bake_set_settings((BakeSettings){.enabled = true});
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
    Bake_set_settings((BakeSettings){.enabled = true});
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
    Bake_set_settings((BakeSettings){.enabled = false});
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
    Bake_set_settings((BakeSettings){.enabled = true});
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
    Bake_set_settings((BakeSettings){.enabled = false});
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

    Bake_set_settings((BakeSettings){.enabled = true});
    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    test_flt(((box_static_update_t*)box_vec_at(&packet.static_updates, 0))->data.params[2], 1);
}

void Bake_sh_reconstructs_the_light_direction(void)
{
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

static ecs_entity_t Bake_box(float x, float y, float z, float sx, float sy, float sz)
{
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {x, y, z});
    ecs_set(w, e, Box, {sx, sy, sz});
    return e;
}

static void Bake_extract(void)
{
    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
}

static void Bake_light(void)
{
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 2, 0});
    ecs_set(w, e, PointLight, {.intensity = 1, .range = 1000000});
}

void Bake_details_share_the_grid(void)
{
    Bake_light();
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    uint32_t probes = s.probe_count;
    test_int(probes, 64);
    for (int i = 0; i < 1000; i ++) Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_int(s.probe_count, probes);
    test_int(s.occluders.count, 0);
    box_bake_state_fini(&s);
}

void Bake_grid_is_shared_across_meshes(void)
{
    Bake_light();
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {2, 2, 2});
    ecs_set(w, e, Sphere, {.radius = 0.1f});
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    test_int(s.chunk_count, 1);
    test_int(s.probe_count, 64);
    box_bake_state_fini(&s);
}

void Bake_distant_geometry_keeps_the_grid_sparse(void)
{
    Bake_light();
    Bake_box(-2, -2, -2, 0.1f, 0.1f, 0.1f);
    Bake_box(100002, 100002, 100002, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    test_int(s.chunk_count, 2);
    int32_t negative[3] = {-1, -1, -1}, empty[3] = {0, 0, 0};
    test_assert(box_bake_find_chunk(&s, negative) != BOX_BAKE_NONE);
    test_int(box_bake_find_chunk(&s, empty), BOX_BAKE_NONE);
    box_bake_state_fini(&s);
}

void Bake_removal_reclaims_chunks(void)
{
    Bake_light();
    ecs_entity_t a = Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    ecs_entity_t b = Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    ecs_delete(w, a);
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_int(s.chunk_count, 1);
    ecs_delete(w, b);
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_int(s.chunk_count, 0);
    Bake_box(102, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_int(s.chunks.count, 1);
    test_int(s.chunk_count, 1);
    test_assert(((box_bake_chunk_t*)s.chunks.data)->fresh);
    box_bake_state_fini(&s);
}

void Bake_budget_splits_a_chunk(void)
{
    Bake_light();
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    test_int(box_bake_state_take(&s, 0), 0);
    test_int(box_bake_state_take(&s, 7), 7);
    test_int(((uint32_t*)s.jobs.data)[0], 0);
    test_int(box_bake_state_take(&s, 7), 7);
    test_int(((uint32_t*)s.jobs.data)[0], 7);
    test_int(box_bake_state_take(&s, 100), 50);
    test_int(s.dirty.count, 0);
    box_bake_state_fini(&s);
}

void Bake_nearest_chunk_is_baked_first(void)
{
    Bake_light();
    Bake_box(102, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    test_int(box_bake_state_take(&s, 1), 1);
    uint32_t id = ((uint32_t*)s.jobs.data)[0] / 64;
    box_bake_chunk_t *c = box_vec_at(&s.chunks, id);
    test_int(c->coord[0], 0);
    box_bake_state_fini(&s);
}

void Bake_walls_block_both_sides(void)
{
    Bake_light();
    Bake_box(0, 2, 0, 0.1f, 8, 8);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    float a[3] = {-2, 2, 0}, b[3] = {2, 2, 0}, c[3] = {-1, 2, 0};
    test_assert(!box_bake_visible_sparse(&s, a, b));
    test_assert(!box_bake_visible_sparse(&s, b, a));
    test_assert(box_bake_visible_sparse(&s, a, c));
    float inside[3] = {0, 2, 0};
    test_assert(!box_bake_visible_sparse(&s, a, inside));
    float high[3] = {-2, 10, 0}, far[3] = {2, 10, 0};
    test_assert(box_bake_visible_sparse(&s, high, far));
    box_bake_state_fini(&s);
}

void Bake_rotated_wall_uses_its_oriented_bounds(void)
{
    Bake_light();
    ecs_entity_t e = Bake_box(0, 0, 0, 0.1f, 4, 8);
    ecs_set(w, e, Rotation, {0, 0.785398f, 0});
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    float a[3] = {-4, 0, 0}, b[3] = {4, 0, 0};
    test_assert(!box_bake_visible_sparse(&s, a, b));
    float c[3] = {2.5f, 0, -2.5f}, d[3] = {2.5f, 1, -2.5f};
    test_assert(box_bake_visible_sparse(&s, c, d));
    box_bake_state_fini(&s);
}

void Bake_occluder_override_is_extracted_and_updated(void)
{
    Bake_light();
    ecs_entity_t wall = Bake_box(0, 2, 0, 0.1f, 8, 8);
    ecs_set(w, wall, BakeOccluder, {false});
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    float a[3] = {-2, 2, 0}, b[3] = {2, 2, 0};
    test_assert(box_bake_visible_sparse(&s, a, b));
    ecs_set(w, wall, BakeOccluder, {true});
    Bake_extract();
    test_int(packet.static_updates.count, 1);
    box_bake_state_apply(&s, &packet);
    test_assert(!box_bake_visible_sparse(&s, a, b));
    ecs_remove(w, wall, BakeOccluder);
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_assert(!box_bake_visible_sparse(&s, a, b));
    box_bake_state_fini(&s);
}

void Bake_thin_details_do_not_occlude(void)
{
    Bake_light();
    ecs_entity_t wire = Bake_box(0, 2, 0, 0.05f, 0.05f, 8);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    float a[3] = {-2, 2, 0}, b[3] = {2, 2, 0};
    test_assert(box_bake_visible_sparse(&s, a, b));
    ecs_set(w, wire, BakeOccluder, {true});
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_assert(!box_bake_visible_sparse(&s, a, b));
    box_bake_state_fini(&s);
}

void Bake_local_lists_exclude_distant_sources(void)
{
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_box(102, 2, 2, 0.1f, 0.1f, 0.1f);
    for (int i = 0; i < 2; i ++) {
        ecs_entity_t e = Bake_box(2 + 100 * i, 3, 2, 0.1f, 0.1f, 0.1f);
        ecs_set(w, e, Emissive, {.strength = 1, .color = {255, 255, 255}});
        ecs_entity_t l = ecs_new(w);
        ecs_set(w, l, Position, {2 + 100 * i, 3, 2});
        ecs_set(w, l, PointLight, {.intensity = 1, .range = 4});
    }
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    int32_t coord[3] = {0, 0, 0};
    uint32_t id = box_bake_find_chunk(&s, coord);
    box_bake_chunk_t *c = box_vec_at(&s.chunks, id);
    test_int(c->lights.count, 1);
    test_int(c->emitters.count, 1);
    box_probe_t probe;
    box_bake_sparse_probe(&s, id, 21, &probe);
    test_assert(probe.r[0] > 0);
    box_bake_pack(&s);
    uint32_t *data = s.data.data;
    test_int(data[data[2] + id * 12 + 5], 1);
    test_int(data[data[2] + id * 12 + 7], 1);
    box_bake_state_fini(&s);
}

void Bake_wall_shadows_shared_probes(void)
{
    Bake_box(0, 2, 0, 0.1f, 8, 8);
    ecs_entity_t light = ecs_new(w);
    ecs_set(w, light, Position, {-2, 2, 0});
    ecs_set(w, light, PointLight, {.intensity = 10, .range = 20});
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    int32_t front[3] = {-1, 0, 0}, back[3] = {0, 0, 0};
    box_probe_t lit, dark;
    box_bake_sparse_probe(&s, box_bake_find_chunk(&s, front), 7, &lit);
    box_bake_sparse_probe(&s, box_bake_find_chunk(&s, back), 4, &dark);
    test_assert(lit.r[0] > 0);
    test_flt(dark.r[0], 0);
    box_bake_state_fini(&s);
}

void Bake_removed_light_invalidates_old_region(void)
{
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_box(102, 2, 2, 0.1f, 0.1f, 0.1f);
    ecs_entity_t light = ecs_new(w);
    ecs_set(w, light, Position, {2, 2, 2});
    ecs_set(w, light, PointLight, {.intensity = 10, .range = 4});
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    box_bake_state_take(&s, UINT32_MAX);
    ecs_delete(w, light);
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_int(s.chunk_count, 0);
    test_int(s.probe_count, 0);
    test_assert(s.data_dirty);
    box_bake_state_fini(&s);
}

void Bake_moved_wall_invalidates_old_and_new_regions(void)
{
    ecs_entity_t wall = Bake_box(0, 2, 0, 0.1f, 8, 8);
    ecs_entity_t light = ecs_new(w);
    ecs_set(w, light, Position, {-2, 2, 0});
    ecs_set(w, light, PointLight, {.intensity = 10, .range = 20});
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    box_bake_state_take(&s, UINT32_MAX);
    ecs_set(w, wall, Position, {4, 2, 0});
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_assert(s.dirty.count > 0);
    float a[3] = {-2, 2, 0}, b[3] = {2, 2, 0}, c[3] = {6, 2, 0};
    test_assert(box_bake_visible_sparse(&s, a, b));
    test_assert(!box_bake_visible_sparse(&s, b, c));
    box_bake_state_fini(&s);
}

void Bake_unchanged_packet_does_no_work(void)
{
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    box_bake_state_take(&s, UINT32_MAX);
    s.data_dirty = false;
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_int(s.dirty.count, 0);
    test_assert(!s.data_dirty);
    box_bake_state_fini(&s);
}

void Bake_respace_rebuilds_from_existing_geometry(void)
{
    Bake_light();
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    Bake_set_settings((BakeSettings){.enabled = true, .probe_spacing = 0.5f});
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_flt(s.spacing, 0.5f);
    test_int(s.chunk_count, 8);
    test_int(s.dirty.count, 8);
    box_bake_state_fini(&s);
}

void Bake_toggle_restores_mesh_bounds(void)
{
    Bake_light();
    Bake_box(2, 2, 2, 1, 1, 1);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    uint32_t count = s.probe_count;
    Bake_set_settings((BakeSettings){.enabled = false});
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_int(s.probe_count, 0);
    Bake_set_settings((BakeSettings){.enabled = true});
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_int(s.probe_count, count);
    test_assert(s.occluders.count > 0);
    box_bake_state_fini(&s);
}

void Bake_large_unlit_surface_allocates_no_probes(void)
{
    Bake_box(0, -1, 0, 100000, 2, 100000);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    test_int(s.probe_count, 0);
    ecs_entity_t l = ecs_new(w);
    ecs_set(w, l, Position, {0, 2, 0});
    ecs_set(w, l, PointLight, {.intensity = 5, .range = 4});
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_assert(s.probe_count > 0);
    test_assert(s.chunk_count < 100);
    float a[3] = {0, 2, 0}, b[3] = {0, -3, 0};
    test_assert(!box_bake_visible_sparse(&s, a, b));
    box_bake_state_fini(&s);
}

void Bake_removed_override_restores_automatic_occlusion(void)
{
    Bake_light();
    ecs_entity_t e = Bake_box(0, 2, 0, 0.1f, 8, 8);
    ecs_set(w, e, BakeOccluder, {false});
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    ecs_remove(w, e, BakeOccluder);
    Bake_extract();
    test_int(packet.static_updates.count, 1);
    box_bake_state_apply(&s, &packet);
    float a[3] = {-2, 2, 0}, b[3] = {2, 2, 0};
    test_assert(!box_bake_visible_sparse(&s, a, b));
    box_bake_state_fini(&s);
}

void Bake_transition_preserves_occluder_override(void)
{
    Bake_light();
    ecs_entity_t e = Bake_box(0, 2, 0, 0.1f, 8, 8);
    ecs_set(w, e, BakeOccluder, {false});
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    u->bake_occluder = 2;
    u->data.transform[3] = 1;
    box_bake_state_apply(&s, &packet);
    float a[3] = {-2, 2, 0}, b[3] = {2, 2, 0};
    test_assert(box_bake_visible_sparse(&s, a, b));
    box_bake_state_fini(&s);
}

void Bake_moved_light_allocates_and_reclaims_regions(void)
{
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_box(102, 2, 2, 0.1f, 0.1f, 0.1f);
    ecs_entity_t light = ecs_new(w);
    ecs_set(w, light, Position, {2, 2, 2});
    ecs_set(w, light, PointLight, {.intensity = 10, .range = 4});
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    test_int(s.chunk_count, 1);
    int32_t a[3] = {0, 0, 0}, b[3] = {25, 0, 0};
    test_assert(box_bake_find_chunk(&s, a) != BOX_BAKE_NONE);
    ecs_set(w, light, Position, {102, 2, 2});
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    test_int(s.chunk_count, 1);
    test_int(box_bake_find_chunk(&s, a), BOX_BAKE_NONE);
    test_assert(box_bake_find_chunk(&s, b) != BOX_BAKE_NONE);
    box_bake_state_fini(&s);
}

void Bake_local_lists_have_no_fixed_source_limit(void)
{
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    for (int i = 0; i < 100; i ++) {
        ecs_entity_t light = ecs_new(w);
        ecs_set(w, light, Position, {2, 3, 2});
        ecs_set(w, light, PointLight, {.intensity = 1, .range = 4});
    }
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    int32_t coord[3] = {0, 0, 0};
    uint32_t id = box_bake_find_chunk(&s, coord);
    box_bake_chunk_t *c = box_vec_at(&s.chunks, id);
    test_int(c->lights.count, 100);
    box_bake_state_fini(&s);
}

void Bake_large_emitter_does_not_skip_a_nearby_wall(void)
{
    Bake_box(0, 2, 0, 0.1f, 8, 8);
    ecs_entity_t emitter = Bake_box(1, 2, 0, 0.4f, 4, 4);
    ecs_set(w, emitter, Emissive, {.strength = 3, .color = {255, 100, 20}});
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    int32_t front[3] = {0, 0, 0}, back[3] = {-1, 0, 0};
    box_probe_t lit, dark;
    box_bake_sparse_probe(&s, box_bake_find_chunk(&s, front), 5, &lit);
    box_bake_sparse_probe(&s, box_bake_find_chunk(&s, back), 7, &dark);
    test_assert(lit.r[0] > 0);
    test_flt(dark.r[0], 0);
    box_bake_state_fini(&s);
}

void Bake_occluder_threshold_reextracts_geometry(void)
{
    Bake_light();
    Bake_box(0, 2, 0, 0.1f, 2, 0.3f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    float a[3] = {-2, 2, 0}, b[3] = {2, 2, 0};
    test_assert(box_bake_visible_sparse(&s, a, b));
    Bake_set_settings((BakeSettings){.enabled = true, .occluder_min_size = 0.2f});
    Bake_extract();
    test_int(packet.static_updates.count, 1);
    box_bake_state_apply(&s, &packet);
    test_assert(!box_bake_visible_sparse(&s, a, b));
    box_bake_state_fini(&s);
}


void Bake_directory_preserves_negative_coords_and_holes(void)
{
    Bake_light();
    Bake_box(-6, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_box(6, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    box_bake_pack(&s);
    uint32_t *data = s.data.data;
    uint32_t directory = data[6];
    test_assert(directory != 0);
    test_int((int32_t)data[directory], -2);
    test_int(data[directory + 1], 0);
    test_int(data[directory + 2], 0);
    test_int(data[directory + 3], 4);
    test_int(data[directory + 4], 1);
    test_int(data[directory + 5], 1);
    int32_t a[3] = {-2, 0, 0}, b[3] = {1, 0, 0};
    test_int(data[directory + 6], box_bake_find_chunk(&s, a));
    test_int(data[directory + 7], BOX_BAKE_NONE);
    test_int(data[directory + 8], BOX_BAKE_NONE);
    test_int(data[directory + 9], box_bake_find_chunk(&s, b));
    box_bake_state_fini(&s);
}

void Bake_directory_falls_back_for_distant_regions(void)
{
    Bake_light();
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_box(65538, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    box_bake_pack(&s);
    uint32_t *data = s.data.data;
    test_int(s.chunk_count, 2);
    test_int(data[6], 0);
    test_assert(data[1] > 0);
    int32_t a[3] = {0, 0, 0}, b[3] = {16384, 0, 0};
    test_assert(box_bake_find_chunk(&s, a) != BOX_BAKE_NONE);
    test_assert(box_bake_find_chunk(&s, b) != BOX_BAKE_NONE);
    box_bake_state_fini(&s);
}

void Bake_sampling_neighbors_follow_chunk_removal(void)
{
    Bake_light();
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    ecs_entity_t neighbor = Bake_box(6, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    box_bake_pack(&s);
    int32_t a[3] = {0, 0, 0}, b[3] = {1, 0, 0}, c[3] = {2, 0, 0};
    uint32_t id = box_bake_find_chunk(&s, a);
    uint32_t *data = s.data.data;
    test_int(data[data[5] + id * 8], id);
    test_int(data[data[5] + id * 8 + 1], box_bake_find_chunk(&s, b));
    ecs_set(w, neighbor, Position, {10, 2, 2});
    Bake_extract();
    box_bake_state_apply(&s, &packet);
    box_bake_pack(&s);
    data = s.data.data;
    test_int(data[data[5] + id * 8 + 1], BOX_BAKE_NONE);
    uint32_t directory = data[6];
    test_assert(directory != 0);
    test_int(data[directory + 7], BOX_BAKE_NONE);
    test_int(data[directory + 8], box_bake_find_chunk(&s, c));
    box_bake_state_fini(&s);
}

void Bake_sampling_cells_include_neighboring_occluders(void)
{
    Bake_light();
    Bake_box(4.25f, 2, 2, 0.1f, 2, 2);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    box_bake_pack(&s);
    int32_t coord[3] = {0, 0, 0};
    uint32_t id = box_bake_find_chunk(&s, coord);
    test_assert(id != BOX_BAKE_NONE);
    const box_bake_chunk_t *chunk = box_vec_at(&s.chunks, id);
    test_int(chunk->occluders.count, 0);
    uint32_t *data = s.data.data;
    uint32_t cells = data[data[2] + id * 12 + 11];
    test_int(data[cells + 23 * 2 + 1], 1);
    test_int(data[cells + 20 * 2 + 1], 0);
    uint32_t occluder = data[data[cells + 23 * 2]];
    const box_bake_occluder_t *o = box_vec_at(&s.occluders, occluder);
    test_assert(o->bounds_min[0] > 4);
    test_assert(o->bounds_max[0] < 4.5f);
    box_bake_state_fini(&s);
}


void Bake_ui_emitter_changes_do_not_rebake_scene(void)
{
    Bake_light();
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    ecs_entity_t layer = ecs_new(w);
    box_render_add_ui_layer(w, layer);
    ecs_entity_t glyph = Bake_box(2, 2, 2, 0.5f, 0.5f, 0.1f);
    ecs_add_pair(w, glyph, Layer, layer);
    ecs_set(w, glyph, Emissive, {.strength = 1, .color = {255, 255, 255}});
    Bake_extract();
    test_int(packet.emitters.count, 0);
    const RenderInstance *instance = ecs_get(w, glyph, RenderInstance);
    uint32_t mesh = instance->mesh, slot = instance->slot;
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    box_bake_state_take(&s, s.probe_count);
    s.data_dirty = false;
    box_bake_mesh_t *m = box_bake_state_mesh(&s, mesh);
    if (slot < m->instances.count) {
        test_bool(((box_bake_instance_t*)box_vec_at(&m->instances, slot))->alive, false);
    }
    ecs_set(w, glyph, Position, {2.5f, 2, 2});
    ecs_set(w, glyph, Emissive, {.strength = 1, .color = {100, 200, 255}});
    Bake_extract();
    test_int(packet.emitters.count, 0);
    box_bake_state_apply(&s, &packet);
    test_int(box_bake_state_take(&s, s.probe_count), 0);
    test_bool(s.data_dirty, false);
    box_bake_state_fini(&s);
}

void Bake_ui_view_does_not_override_bake_settings(void)
{
    Bake_set_settings((BakeSettings){.enabled = true, .probe_spacing = 2});
    Bake_light();
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    ecs_entity_t layer = ecs_new(w);
    box_render_add_ui_layer(w, layer);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.orthographic = true, .ortho_height = 10});
    ecs_entity_t ui_view = ecs_new(w);
    ecs_set(w, ui_view, View, {.camera = cam, .layers = {layer},
        .priority = -1, .enabled = true, .strict_layers = true});
    ecs_entity_t ui_light = ecs_new(w);
    ecs_set(w, ui_light, Position, {0, 2, 0});
    ecs_set(w, ui_light, PointLight, {.intensity = 1, .range = 5});
    ecs_add_pair(w, ui_light, Layer, layer);
    Bake_extract();
    test_int(packet.views.count, 2);
    test_bool(packet.bake.enabled, true);
    test_flt(packet.bake.spacing, 2);
    test_int(packet.bake_lights.count, 1);
    ecs_set(w, ui_view, View, {.camera = cam, .layers = {layer},
        .priority = 1, .enabled = true, .strict_layers = true});
    Bake_extract();
    test_bool(packet.bake.enabled, true);
    test_int(packet.static_updates.count, 0);
}


void Bake_probes_inside_occluders_are_invalid(void)
{
    Bake_light();
    Bake_box(1.5f, 1.5f, 1.5f, 0.5f, 0.5f, 0.5f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    int32_t coord[3] = {0, 0, 0};
    uint32_t chunk = box_bake_find_chunk(&s, coord);
    test_assert(chunk != BOX_BAKE_NONE);
    box_probe_t inside, outside;
    box_bake_sparse_probe(&s, chunk, 21, &inside);
    box_bake_sparse_probe(&s, chunk, 0, &outside);
    test_flt(inside.r[0], -1);
    test_assert(outside.r[0] > 0);
    box_bake_state_fini(&s);
}

void Bake_dark_probes_remain_valid(void)
{
    ecs_entity_t light = ecs_new(w);
    ecs_set(w, light, Position, {2, 2, 2});
    ecs_set(w, light, PointLight, {.intensity = 1, .range = 10});
    ecs_set(w, light, Rgb, {0, 0, 0});
    Bake_box(2, 2, 2, 0.1f, 0.1f, 0.1f);
    Bake_extract();
    box_bake_state_t s;
    box_bake_state_init(&s);
    box_bake_state_apply(&s, &packet);
    int32_t coord[3] = {0, 0, 0};
    uint32_t chunk = box_bake_find_chunk(&s, coord);
    test_assert(chunk != BOX_BAKE_NONE);
    box_probe_t probe;
    box_bake_sparse_probe(&s, chunk, 21, &probe);
    test_flt(probe.r[0], 0);
    test_flt(probe.g[0], 0);
    test_flt(probe.b[0], 0);
    box_bake_state_fini(&s);
}
