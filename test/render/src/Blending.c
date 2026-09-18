/**
 * @file Blending.c
 * @brief Tests for pass assignment, the sorted draw list, blend and back face modes.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/blur.h"

static ecs_world_t *w;
static box_packet_t packet;

void Blending_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Blending_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);
        free(cmd->vertices);
        free(cmd->indices);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Blending_opaque_entity_stays_in_its_batch(void)
{
    // materials/blending
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 0);
    test_int(packet.static_updates.count, 1);
    test_int(packet.static_counts.count, 1);
    test_int(((box_static_count_t*)box_vec_at(&packet.static_counts, 0))->count, 1);
    const RenderInstance *inst = ecs_get(w, e, RenderInstance);
    test_bool(inst->transparent, false);
}

void Blending_blend_mode_moves_to_the_sorted_list(void)
{
    // materials/blending
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, BlendMode, {Additive});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    test_int(packet.transparent.count, 1);
    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_int(entry->blend, Additive);
    test_int(entry->back, BackFaceCull);
    const RenderInstance *inst = ecs_get(w, e, RenderInstance);
    test_bool(inst->transparent, true);
}

void Blending_opacity_moves_to_the_sorted_list(void)
{
    // materials/blending
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Opacity, {0.25f});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    test_int(packet.transparent.count, 1);
    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_flt(entry->data.color[3], 0.25f);
}

void Blending_full_opacity_stays_in_its_batch(void)
{
    // materials/blending
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Opacity, {1.0f});
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 0);
    test_int(packet.static_updates.count, 1);
}

void Blending_tint_alpha_moves_to_the_sorted_list(void)
{
    // materials/blending
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Tint, {255, 255, 255, 128});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    test_int(packet.transparent.count, 1);
    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_assert(fabsf(entry->data.color[3] - 128.0f / 255.0f) < 1e-5f);
}

void Blending_translucent_opaque_mode_blends_as_alpha(void)
{
    // materials/blending
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Opacity, {0.5f});
    ecs_set(w, e, BlendMode, {Opaque});
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 1);
    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_int(entry->blend, AlphaBlend);
    test_int(box_instance_blend(&entry->data), AlphaBlend);
}

void Blending_back_face_mode_moves_to_the_sorted_list(void)
{
    // materials/blending
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, BackFaceMode, {BackFacePlain});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    test_int(packet.transparent.count, 1);
    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_int(entry->blend, Opaque);
    test_int(entry->back, BackFacePlain);
}

void Blending_back_face_modes_reach_the_packet(void)
{
    // materials/blending
    int32_t mode;
    for (mode = 0; mode < 3; mode ++) {
        ecs_entity_t e = ecs_new(w);
        ecs_set(w, e, Position, {0, 0, (float)mode});
        ecs_set(w, e, Box, {1, 1, 1});
        ecs_set(w, e, Opacity, {0.5f});
        ecs_set_id(w, e, ecs_id(BackFaceMode), sizeof(BackFaceMode), &mode);
    }

    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 3);
    uint32_t seen = 0;
    uint32_t i;
    for (i = 0; i < packet.transparent.count; i ++) {
        box_transparent_t *entry = box_vec_at(&packet.transparent, i);
        test_int(entry->back, box_instance_back(&entry->data));
        seen |= 1u << entry->back;
    }

    test_int(seen, 7);
}

void Blending_every_blend_mode_reaches_the_packet(void)
{
    // materials/blending
    int32_t mode;
    for (mode = 1; mode < BOX_BLEND_COUNT; mode ++) {
        ecs_entity_t e = ecs_new(w);
        ecs_set(w, e, Position, {0, 0, (float)mode});
        ecs_set(w, e, Box, {1, 1, 1});
        ecs_set_id(w, e, ecs_id(BlendMode), sizeof(BlendMode), &mode);
    }

    ecs_entity_t opaque = ecs_new(w);
    ecs_set(w, opaque, Position, {0, 0, 0});
    ecs_set(w, opaque, Box, {1, 1, 1});
    ecs_set(w, opaque, BackFaceMode, {BackFaceTextured});

    box_render_extract(w, &packet);
    test_int(packet.transparent.count, BOX_BLEND_COUNT);
    uint32_t seen = 0;
    uint32_t i;
    for (i = 0; i < packet.transparent.count; i ++) {
        box_transparent_t *entry = box_vec_at(&packet.transparent, i);
        test_int(entry->blend, box_instance_blend(&entry->data));
        seen |= 1u << entry->blend;
    }

    test_int(seen, (1u << BOX_BLEND_COUNT) - 1);
}

void Blending_sorted_back_to_front(void)
{
    // materials/blending
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, LookAt, {0, 0, 0});
    ecs_set(w, camera, Camera, {.fov = 60});

    ecs_entity_t near = ecs_new(w);
    ecs_set(w, near, Position, {0, 0, -4});
    ecs_set(w, near, Box, {1, 1, 1});
    ecs_set(w, near, Opacity, {0.5f});

    ecs_entity_t far = ecs_new(w);
    ecs_set(w, far, Position, {0, 0, 8});
    ecs_set(w, far, Box, {1, 1, 1});
    ecs_set(w, far, Opacity, {0.5f});

    ecs_entity_t middle = ecs_new(w);
    ecs_set(w, middle, Position, {0, 0, 2});
    ecs_set(w, middle, Box, {1, 1, 1});
    ecs_set(w, middle, Opacity, {0.5f});

    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 3);
    box_transparent_t *first = box_vec_at(&packet.transparent, 0);
    box_transparent_t *second = box_vec_at(&packet.transparent, 1);
    box_transparent_t *third = box_vec_at(&packet.transparent, 2);
    test_assert(first->depth > second->depth);
    test_assert(second->depth > third->depth);
    test_flt(first->data.transform[11], 8);
    test_flt(second->data.transform[11], 2);
    test_flt(third->data.transform[11], -4);
}

void Blending_static_entity_leaves_and_regains_its_slot(void)
{
    // materials/blending
    ecs_entity_t keep = ecs_new(w);
    ecs_set(w, keep, Position, {2, 0, 0});
    ecs_set(w, keep, Box, {1, 1, 1});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 2);
    test_int(((box_static_count_t*)box_vec_at(&packet.static_counts, 0))->count, 2);

    box_packet_reset(&packet);
    ecs_set(w, e, BlendMode, {AlphaBlend});
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 1);
    test_int(packet.static_counts.count, 1);
    test_int(((box_static_count_t*)box_vec_at(&packet.static_counts, 0))->count, 1);
    const RenderInstance *inst = ecs_get(w, e, RenderInstance);
    test_bool(inst->transparent, true);

    box_packet_reset(&packet);
    ecs_set(w, e, BlendMode, {Opaque});
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 0);
    test_int(packet.static_updates.count, 1);
    test_int(((box_static_count_t*)box_vec_at(&packet.static_counts, 0))->count, 2);
    inst = ecs_get(w, e, RenderInstance);
    test_bool(inst->transparent, false);
    test_int(inst->slot, 1);
}

void Blending_sorted_entity_extracted_every_frame(void)
{
    // materials/blending
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Opacity, {0.5f});
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 1);

    box_packet_reset(&packet);
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 1);
    test_int(packet.static_updates.count, 0);
}

void Blending_dynamic_transparent_leaves_the_dynamic_batches(void)
{
    // materials/blending
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add(w, e, DynamicTransform);
    ecs_set(w, e, BlendMode, {Multiply});
    box_render_extract(w, &packet);
    test_int(packet.dynamic_instances.count, 0);
    test_int(packet.dynamic_batches.count, 0);
    test_int(packet.transparent.count, 1);
    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_int(entry->blend, Multiply);
}

void Blending_deleted_entity_leaves_the_sorted_list(void)
{
    // materials/blending
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_set(w, a, Opacity, {0.5f});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {2, 0, 0});
    ecs_set(w, b, Box, {1, 1, 1});
    ecs_set(w, b, Opacity, {0.5f});
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 2);

    box_packet_reset(&packet);
    ecs_delete(w, a);
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 1);
    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_flt(entry->data.transform[3], 2);
}

void Blending_prefab_blend_mode_is_inherited(void)
{
    // materials/blending
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    ecs_set(w, prefab, BlendMode, {Screen});
    ecs_entity_t e = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 1);
    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_int(entry->blend, Screen);
}

void Blending_opaque_scene_packet_is_unchanged(void)
{
    // materials/blending
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_set(w, a, Rgb, {10, 20, 30});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {2, 0, 0});
    ecs_set(w, b, Sphere, {.segments = 8, .radius = 1});
    ecs_add(w, b, DynamicTransform);
    box_render_extract(w, &packet);

    test_int(packet.transparent.count, 0);
    test_int(packet.static_updates.count, 1);
    test_int(packet.static_counts.count, 1);
    test_int(packet.dynamic_batches.count, 1);
    test_int(packet.dynamic_instances.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(u->data.uv_mode, 0);
    test_flt(u->data.color[3], 1.0f);
    box_instance_t *dyn = box_vec_at(&packet.dynamic_instances, 0);
    test_int(dyn->uv_mode, 0);
}

void Blending_blur_level_of_zero_roughness_is_the_first_level(void)
{
    // materials/glass_roughness
    test_flt(box_blur_level(0.0f, 5), 0.0f);
    test_flt(box_blur_level(0.0f, 9), 0.0f);
}

void Blending_blur_level_of_full_roughness_is_the_last_level(void)
{
    // materials/glass_roughness
    test_flt(box_blur_level(1.0f, 5), 4.0f);
    test_flt(box_blur_level(1.0f, 9), 8.0f);
}

void Blending_blur_level_grows_with_roughness(void)
{
    // materials/glass_roughness
    float previous = -1.0f;
    int32_t i;
    for (i = 0; i <= 20; i ++) {
        float level = box_blur_level((float)i / 20.0f, 5);
        test_assert(level > previous);
        test_assert(level <= 4.0f);
        previous = level;
    }
}

void Blending_blur_level_of_a_quarter_roughness_is_half_the_chain(void)
{
    // materials/glass_roughness
    test_flt(box_blur_level(0.25f, 5), 2.0f);
    test_flt(box_blur_level(0.25f, 9), 4.0f);
}

void Blending_blur_level_of_half_roughness_follows_the_curve(void)
{
    // materials/glass_roughness
    test_assert(fabsf(box_blur_level(0.5f, 5) - sqrtf(0.5f) * 4.0f) < 1e-5f);
    test_assert(fabsf(box_blur_level(0.5f, 9) - sqrtf(0.5f) * 8.0f) < 1e-5f);
}

void Blending_blur_level_clamps_roughness_to_the_chain(void)
{
    // materials/glass_roughness
    test_flt(box_blur_level(-1.0f, 5), 0.0f);
    test_flt(box_blur_level(4.0f, 5), 4.0f);
}

void Blending_blur_chain_keeps_its_coarsest_level_large(void)
{
    // materials/glass_roughness
    uint32_t levels = box_blur_levels(960, 540);
    test_int(levels, 5);
    test_assert((540u >> (levels - 1)) >= BOX_BLUR_MIN_SIZE);
    test_assert((540u >> levels) < BOX_BLUR_MIN_SIZE);
    test_int(box_blur_levels(1280, 720), 5);
    test_int(box_blur_levels(32, 32), 1);
    test_assert(box_blur_levels(8192, 8192) <= BOX_BLUR_LEVELS_MAX);
}

void Blending_opaque_view_does_not_request_the_blur_chain(void)
{
    // materials/glass_roughness
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    test_int(packet.view.transparent_count, 0);
    test_int(packet.view.translucent_count, 0);
}

void Blending_alpha_blended_instance_requests_the_blur_chain(void)
{
    // materials/glass_roughness
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Opacity, {0.5f});
    box_render_extract(w, &packet);
    test_int(packet.view.transparent_count, 1);
    test_int(packet.view.translucent_count, 1);
}

void Blending_premultiplied_instance_requests_the_blur_chain(void)
{
    // materials/glass_roughness
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, BlendMode, {PremultipliedAlpha});
    box_render_extract(w, &packet);
    test_int(packet.view.translucent_count, 1);
}

void Blending_additive_instance_does_not_request_the_blur_chain(void)
{
    // materials/glass_roughness
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, BlendMode, {Additive});
    box_render_extract(w, &packet);
    test_int(packet.view.transparent_count, 1);
    test_int(packet.view.translucent_count, 0);
}

void Blending_translucent_count_covers_the_alpha_blended_instances(void)
{
    // materials/glass_roughness
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_set(w, a, Opacity, {0.5f});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {2, 0, 0});
    ecs_set(w, b, Box, {1, 1, 1});
    ecs_set(w, b, BlendMode, {AlphaBlend});
    ecs_entity_t c = ecs_new(w);
    ecs_set(w, c, Position, {4, 0, 0});
    ecs_set(w, c, Box, {1, 1, 1});
    ecs_set(w, c, BlendMode, {Screen});
    box_render_extract(w, &packet);
    test_int(packet.view.transparent_count, 3);
    test_int(packet.view.translucent_count, 2);
}
