/**
 * @file HiZ.c
 * @brief Tests for the depth pyramid rule, the screen bounds and the occlusion test.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/hiz.h"

static ecs_world_t *w;
static box_packet_t packet;

void HiZ_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    packet.clear[3] = 1.0f;
}

void HiZ_teardown(void)
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

void HiZ_levels_of_a_view(void)
{
    // culling/hi_z
    test_uint(box_hiz_levels(960, 540, 0), 10);
    test_uint(box_hiz_levels(1, 1, 0), 1);
    test_uint(box_hiz_levels(960, 540, 4), 4);
    test_uint(box_hiz_levels(960, 540, 99), 10);
    test_uint(box_hiz_extent(960, 0), 960);
    test_uint(box_hiz_extent(960, 5), 30);
    test_uint(box_hiz_extent(960, 20), 1);
}

void HiZ_reduction_of_a_four_by_four_block(void)
{
    // culling/hi_z
    float src[16] = {
        0.90f, 0.80f, 0.40f, 0.30f,
        0.70f, 0.60f, 0.35f, 0.25f,
        0.50f, 0.45f, 0.20f, 0.15f,
        0.55f, 0.65f, 0.10f, 0.05f
    };
    test_flt(box_hiz_reduce(src, 4, 4, 0, 0), 0.60f);
    test_flt(box_hiz_reduce(src, 4, 4, 1, 0), 0.25f);
    test_flt(box_hiz_reduce(src, 4, 4, 0, 1), 0.45f);
    test_flt(box_hiz_reduce(src, 4, 4, 1, 1), 0.05f);
}

void HiZ_reduction_covers_an_odd_edge(void)
{
    // culling/hi_z
    float src[9] = {
        0.90f, 0.80f, 0.70f,
        0.85f, 0.75f, 0.65f,
        0.60f, 0.50f, 0.02f
    };
    test_flt(box_hiz_reduce(src, 3, 3, 0, 0), 0.02f);

    float wide[6] = { 0.90f, 0.80f, 0.70f, 0.60f, 0.50f, 0.03f };
    test_flt(box_hiz_reduce(wide, 3, 2, 0, 0), 0.03f);
}

void HiZ_sphere_on_the_axis_projects_to_the_center(void)
{
    // culling/hi_z
    mat4 view = mat4_look_at(vec3_new(0, 0, 0), vec3_new(0, 0, 1), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(3.14159265f * 0.5f, 1.0f, 0.1f);
    float clip[16];
    box_frustum_mul(proj.v, view.v, clip);

    float center[3] = {0, 0, 10};
    box_hiz_rect_t rect;
    test_bool(box_hiz_project(clip, center, 1.0f, &rect), true);
    test_bool(rect.clamped, false);
    test_assert(rect.min[0] < 0.5f);
    test_assert(rect.max[0] > 0.5f);
    test_assert(fabsf((rect.min[0] + rect.max[0]) * 0.5f - 0.5f) < 1e-4f);
    test_assert(fabsf((rect.min[1] + rect.max[1]) * 0.5f - 0.5f) < 1e-4f);
    test_assert(fabsf(rect.depth - 0.1f / 9.0f) < 1e-5f);
}

void HiZ_sphere_off_the_axis_projects_beside_it(void)
{
    // culling/hi_z
    mat4 view = mat4_look_at(vec3_new(0, 0, 0), vec3_new(0, 0, 1), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(3.14159265f * 0.5f, 1.0f, 0.1f);
    float clip[16];
    box_frustum_mul(proj.v, view.v, clip);

    float right[3] = {3, 0, 10};
    box_hiz_rect_t rect;
    test_bool(box_hiz_project(clip, right, 0.5f, &rect), true);
    test_assert(rect.min[0] > 0.5f);
    test_assert(rect.max[0] < 1.0f);

    float above[3] = {0, 3, 10};
    box_hiz_rect_t high;
    test_bool(box_hiz_project(clip, above, 0.5f, &high), true);
    test_assert(high.max[1] < 0.5f);
    test_assert(high.min[1] > 0.0f);
}

void HiZ_sphere_across_the_near_plane_takes_the_view(void)
{
    // culling/hi_z
    mat4 view = mat4_look_at(vec3_new(0, 0, 0), vec3_new(0, 0, 1), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(3.14159265f * 0.5f, 1.0f, 0.1f);
    float clip[16];
    box_frustum_mul(proj.v, view.v, clip);

    float center[3] = {0, 0, 0.5f};
    box_hiz_rect_t rect;
    test_bool(box_hiz_project(clip, center, 1.0f, &rect), false);
    test_bool(rect.clamped, true);
    test_flt(rect.min[0], 0);
    test_flt(rect.min[1], 0);
    test_flt(rect.max[0], 1.0f);
    test_flt(rect.max[1], 1.0f);
    test_flt(rect.depth, 1.0f);
}

void HiZ_mip_holds_the_rectangle_in_two_texels(void)
{
    // culling/hi_z
    box_hiz_rect_t rect = { .min = {0.25f, 0.25f}, .max = {0.3125f, 0.3125f} };
    test_uint(box_hiz_mip(&rect, 64, 64, 7), 2);

    box_hiz_rect_t tiny = { .min = {0.5f, 0.5f}, .max = {0.5f, 0.5f} };
    test_uint(box_hiz_mip(&tiny, 64, 64, 7), 0);

    box_hiz_rect_t all = { .min = {0.0f, 0.0f}, .max = {1.0f, 1.0f} };
    test_uint(box_hiz_mip(&all, 64, 64, 7), 6);
    test_uint(box_hiz_mip(&all, 64, 64, 3), 2);
}

void HiZ_pyramid_hides_a_sphere_behind_a_wall(void)
{
    // culling/hi_z
    float level0[16];
    float level1[4];
    float level2[1];
    uint32_t i;
    for (i = 0; i < 16; i ++) {
        level0[i] = 0.5f;
    }

    for (i = 0; i < 4; i ++) {
        level1[i] = 0.5f;
    }

    level2[0] = 0.5f;
    box_hiz_pyramid_t p = {
        .levels = { level0, level1, level2 },
        .width = 4, .height = 4, .count = 3
    };

    box_hiz_rect_t behind = { .min = {0.05f, 0.05f}, .max = {0.45f, 0.45f}, .depth = 0.3f };
    test_bool(box_hiz_visible(&p, &behind), false);
}

void HiZ_pyramid_keeps_a_sphere_in_front_of_a_wall(void)
{
    // culling/hi_z
    float level0[16];
    float level1[4];
    float level2[1];
    uint32_t i;
    for (i = 0; i < 16; i ++) {
        level0[i] = 0.5f;
    }

    for (i = 0; i < 4; i ++) {
        level1[i] = 0.5f;
    }

    level2[0] = 0.5f;
    box_hiz_pyramid_t p = {
        .levels = { level0, level1, level2 },
        .width = 4, .height = 4, .count = 3
    };

    box_hiz_rect_t front = { .min = {0.05f, 0.05f}, .max = {0.45f, 0.45f}, .depth = 0.7f };
    test_bool(box_hiz_visible(&p, &front), true);

    box_hiz_rect_t touching = { .min = {0.05f, 0.05f}, .max = {0.45f, 0.45f}, .depth = 0.5f };
    test_bool(box_hiz_visible(&p, &touching), true);
}

void HiZ_pyramid_keeps_a_sphere_that_straddles_a_wall(void)
{
    // culling/hi_z
    float level0[16];
    float level1[4] = { 0.5f, 0.0f, 0.5f, 0.0f };
    float level2[1] = { 0.0f };
    uint32_t i;
    for (i = 0; i < 16; i ++) {
        level0[i] = (i % 4) < 2 ? 0.5f : 0.0f;
    }

    box_hiz_pyramid_t p = {
        .levels = { level0, level1, level2 },
        .width = 4, .height = 4, .count = 3
    };

    box_hiz_rect_t across = { .min = {0.25f, 0.05f}, .max = {0.75f, 0.45f}, .depth = 0.3f };
    test_bool(box_hiz_visible(&p, &across), true);

    box_hiz_rect_t inside = { .min = {0.05f, 0.05f}, .max = {0.45f, 0.45f}, .depth = 0.3f };
    test_bool(box_hiz_visible(&p, &inside), false);
}

void HiZ_a_clamped_rectangle_is_never_hidden(void)
{
    // culling/hi_z
    float level0[16];
    float level1[4];
    float level2[1] = { 1.0f };
    uint32_t i;
    for (i = 0; i < 16; i ++) {
        level0[i] = 1.0f;
    }

    for (i = 0; i < 4; i ++) {
        level1[i] = 1.0f;
    }

    box_hiz_pyramid_t p = {
        .levels = { level0, level1, level2 },
        .width = 4, .height = 4, .count = 3
    };

    box_hiz_rect_t near_plane = {
        .min = {0, 0}, .max = {1.0f, 1.0f}, .depth = 0.0f, .clamped = true
    };
    test_bool(box_hiz_visible(&p, &near_plane), true);
}

void HiZ_settings_fall_back_to_the_singleton(void)
{
    // culling/hi_z
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    ecs_get_mut(w, e, View)->camera = cam;
    ecs_modified(w, e, View);
    ecs_singleton_set(w, CullSettings, {.enabled = true, .occlusion = false, .hiz_levels = 5});
    box_render_extract(w, &packet);

    test_int(packet.views.count, 1);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->occlusion, 0);
    test_uint(v->hiz_levels, 5);
}

void HiZ_view_without_settings_occludes_by_default(void)
{
    // culling/hi_z
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    box_render_extract(w, &packet);

    test_int(packet.views.count, 1);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->occlusion, 1);
    test_uint(v->hiz_levels, BOX_CULL_HIZ_LEVELS);
}

void HiZ_disabled_culling_disables_occlusion(void)
{
    // culling/hi_z
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, CullSettings, {.enabled = false, .occlusion = true});
    box_render_extract(w, &packet);

    test_int(packet.views.count, 1);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->cull_enabled, 0);
    test_uint(v->occlusion, 0);
}
