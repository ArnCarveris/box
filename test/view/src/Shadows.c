/**
 * @file Shadows.c
 * @brief Tests for cascaded shadows: splits, cascade fit, casters and the static cache.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/shadow.h"

static ecs_world_t *w;
static box_packet_t packet;

void Shadows_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    packet.clear[3] = 1.0f;
}

void Shadows_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Shadows_splits_are_uniform_at_lambda_zero(void)
{
    // views/shadows
    float splits[4];
    box_render_shadow_splits(1.0f, 101.0f, 0.0f, 4, splits);

    test_flt(splits[0], 26.0f);
    test_flt(splits[1], 51.0f);
    test_flt(splits[2], 76.0f);
    test_flt(splits[3], 101.0f);
}

void Shadows_splits_are_logarithmic_at_lambda_one(void)
{
    // views/shadows
    float splits[4];
    box_render_shadow_splits(1.0f, 16.0f, 1.0f, 4, splits);

    test_flt(splits[0], 2.0f);
    test_flt(splits[1], 4.0f);
    test_flt(splits[2], 8.0f);
    test_flt(splits[3], 16.0f);
}

void Shadows_default_splits_blend_both(void)
{
    // views/shadows
    float splits[4];
    float uniform[4];
    float logarithmic[4];
    box_render_shadow_splits(1.0f, 16.0f, BOX_SHADOW_LAMBDA, 4, splits);
    box_render_shadow_splits(1.0f, 16.0f, 0.0f, 4, uniform);
    box_render_shadow_splits(1.0f, 16.0f, 1.0f, 4, logarithmic);

    int i;
    for (i = 0; i < 3; i ++) {
        test_assert(splits[i] > logarithmic[i]);
        test_assert(splits[i] < uniform[i]);
        test_flt(splits[i], BOX_SHADOW_LAMBDA * logarithmic[i] +
            (1.0f - BOX_SHADOW_LAMBDA) * uniform[i]);
    }

    test_flt(splits[3], 16.0f);
}

void Shadows_cascades_cover_the_camera_range(void)
{
    // views/shadows
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->shadow_cascades, BOX_SHADOW_CASCADES);
    test_int(v->shadow_resolution, BOX_SHADOW_RESOLUTION);
    test_int(v->shadow_filter, BOX_SHADOW_FILTER);
    test_flt(v->shadow_distance, BOX_SHADOW_DISTANCE);
    test_flt(v->shadow_split[3], BOX_SHADOW_DISTANCE);
    test_assert(v->shadow_split[0] < v->shadow_split[1]);
    test_assert(v->shadow_split[1] < v->shadow_split[2]);
    test_assert(v->shadow_offset[0] < v->shadow_offset[3]);
    test_assert(v->shadow_bounds[3] < v->shadow_bounds[15]);
}

void Shadows_cascade_holds_the_slice_corners(void)
{
    // views/shadows
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    uint32_t c;
    for (c = 0; c < v->shadow_cascades; c ++) {
        vec3 corners[8];
        float near_ = c ? v->shadow_split[c - 1] : v->cluster.near_;
        box_render_shadow_corners(v, near_, v->shadow_split[c], corners);
        mat4 m;
        memcpy(m.v, v->shadow_matrix + c * 16, sizeof(m.v));
        int i;
        for (i = 0; i < 8; i ++) {
            vec3 clip = mat4_transform_point(&m, corners[i]);
            test_assert(clip.x >= -1.0001f && clip.x <= 1.0001f);
            test_assert(clip.y >= -1.0001f && clip.y <= 1.0001f);
            test_assert(clip.z >= -0.0001f && clip.z <= 1.0001f);
        }
    }
}

void Shadows_texel_snapping_keeps_the_matrix(void)
{
    // views/shadows
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    ecs_set(w, view, ShadowSettings, {.cascades = 1, .resolution = 256, .max_distance = 200});
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    float before[16];
    memcpy(before, v->shadow_matrix, sizeof(before));
    float texel = 2.0f * v->shadow_bounds[3] / 256.0f;
    test_assert(texel > 0.1f);

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_set(w, cam, Position, {texel * 0.001f, 4, -12});
    box_render_extract(w, &packet);
    v = box_vec_at(&packet.views, 0);
    test_bool(box_shadow_matrix_equal(before, v->shadow_matrix), true);

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_set(w, cam, Position, {texel * 40.0f, 4, -12});
    box_render_extract(w, &packet);
    v = box_vec_at(&packet.views, 0);
    test_bool(box_shadow_matrix_equal(before, v->shadow_matrix), false);
}

void Shadows_casters_exclude_no_shadow(void)
{
    // views/shadows
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    ecs_entity_t caster = ecs_new(w);
    ecs_set(w, caster, Position, {0, 0, 0});
    ecs_set(w, caster, Box, {1, 1, 1});
    ecs_entity_t hidden = ecs_new(w);
    ecs_set(w, hidden, Position, {2, 0, 0});
    ecs_set(w, hidden, Box, {1, 1, 1});
    ecs_add_id(w, hidden, NoShadow);
    ecs_entity_t child = ecs_entity(w, { .parent = hidden });
    ecs_set(w, child, Position, {0, 1, 0});
    ecs_set(w, child, Box, {1, 1, 1});
    box_render_extract(w, &packet);

    test_int(packet.static_updates.count, 3);
    uint32_t i;
    for (i = 0; i < packet.static_updates.count; i ++) {
        box_static_update_t *u = box_vec_at(&packet.static_updates, i);
        uint32_t slot = ecs_get(w, caster, RenderInstance)->slot;
        test_bool(box_instance_caster(&u->data), u->slot == slot);
    }
}

void Shadows_casters_exclude_transparent(void)
{
    // views/shadows
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    ecs_entity_t blended = ecs_new(w);
    ecs_set(w, blended, Position, {0, 0, 0});
    ecs_set(w, blended, Box, {1, 1, 1});
    ecs_set(w, blended, BlendMode, {AlphaBlend});
    ecs_entity_t two_sided = ecs_new(w);
    ecs_set(w, two_sided, Position, {2, 0, 0});
    ecs_set(w, two_sided, Box, {1, 1, 1});
    ecs_set(w, two_sided, BackFaceMode, {BackFaceTextured});
    box_render_extract(w, &packet);

    test_int(packet.transparent.count, 2);
    uint32_t i;
    for (i = 0; i < packet.transparent.count; i ++) {
        box_transparent_t *entry = box_vec_at(&packet.transparent, i);
        test_bool(box_instance_caster(&entry->data), entry->blend == BOX_BLEND_OPAQUE);
    }
}

void Shadows_casters_respect_shadow_layers(void)
{
    // views/shadows
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    ecs_entity_t member = ecs_new(w);
    ecs_set(w, member, Position, {0, 0, 0});
    ecs_set(w, member, Box, {1, 1, 1});
    ecs_add_pair(w, member, Layer, a);
    ecs_entity_t other = ecs_new(w);
    ecs_set(w, other, Position, {2, 0, 0});
    ecs_set(w, other, Box, {1, 1, 1});
    ecs_add_pair(w, other, Layer, b);
    View *settings = ecs_get_mut(w, view, View);
    settings->shadow_layers[0] = a;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->caster_mask, 1u);
    test_bool(box_view_casts(v, ecs_get(w, member, RenderInstance)->layer_mask), true);
    test_bool(box_view_casts(v, ecs_get(w, other, RenderInstance)->layer_mask), false);
    test_bool(box_view_casts(v, 0), true);
}

void Shadows_settings_belong_to_the_view(void)
{
    // views/shadows
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->shadows.cascades = 2;
    settings->shadows.max_distance = 50;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->shadow_cascades, 2);
    test_flt(v->shadow_distance, 50.0f);
    test_int(v->shadow_resolution, BOX_SHADOW_RESOLUTION);

    settings = ecs_get_mut(w, view, View);
    settings->shadows.cascades = 3;
    settings->shadows.resolution = 512;
    settings->shadows.max_distance = BOX_SHADOW_DISTANCE;
    ecs_modified(w, view, View);
    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(w, &packet);
    v = box_vec_at(&packet.views, 0);
    test_int(v->shadow_cascades, 3);
    test_int(v->shadow_resolution, 512);
    test_flt(v->shadow_distance, BOX_SHADOW_DISTANCE);
}

void Shadows_settings_are_clamped(void)
{
    // views/shadows
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->shadows.cascades = 9;
    settings->shadows.resolution = 0;
    settings->shadows.filter_size = 99;
    settings->shadows.split_lambda = 5.0f;
    settings->shadows.fade = -1.0f;
    settings->shadows.depth_bias = -2.0f;
    settings->shadows.max_distance = 0;
    ecs_modified(w, view, View);

    ShadowSettings s;
    box_render_shadow_settings(w, view, &s);
    test_int(s.cascades, BOX_SHADOW_CASCADE_MAX);
    test_int(s.resolution, BOX_SHADOW_RESOLUTION);
    test_int(s.filter_size, BOX_SHADOW_FILTER_MAX);
    test_flt(s.split_lambda, 1.0f);
    test_flt(s.fade, 0.0f);
    test_flt(s.depth_bias, 0.0f);
    test_flt(s.max_distance, BOX_SHADOW_DISTANCE);
}

void Shadows_view_without_shadows_has_no_cascades(void)
{
    // views/shadows
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->shadows.enabled = false;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->shadows, 0);
    test_int(v->shadow_cascades, 0);
}

void Shadows_world_without_a_sun_has_no_cascades(void)
{
    // views/shadows
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->shadows, 1);
    test_int(v->shadow_cascades, 0);
}

void Shadows_static_delta_inside_a_cascade_marks_it_dirty(void)
{
    // views/shadows
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    box_shadow_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    box_shadow_cache_view(&cache, v);
    test_bool(cache.dirty[0], true);
    test_uint(box_shadow_cache_pick(&cache, v), 0xFu);

    uint32_t i;
    for (i = 0; i < v->shadow_cascades; i ++) {
        box_shadow_cache_done(&cache, v, i);
    }

    test_uint(box_shadow_cache_pick(&cache, v), 0);
    float inside[4] = { v->shadow_bounds[0], v->shadow_bounds[1], v->shadow_bounds[2], 0.5f };
    box_shadow_cache_delta(&cache, v, inside);
    test_bool(cache.dirty[0], true);
    test_uint(box_shadow_cache_pick(&cache, v) & 1u, 1u);
}

void Shadows_static_delta_outside_a_cascade_keeps_it(void)
{
    // views/shadows
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    box_shadow_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    box_shadow_cache_view(&cache, v);
    uint32_t i;
    for (i = 0; i < v->shadow_cascades; i ++) {
        box_shadow_cache_done(&cache, v, i);
    }

    float radius = v->shadow_bounds[3];
    float outside[4] = {
        v->shadow_bounds[0] + 100.0f * radius,
        v->shadow_bounds[1],
        v->shadow_bounds[2] + 100.0f * radius,
        0.5f
    };
    box_shadow_cache_delta(&cache, v, outside);
    test_bool(cache.dirty[0], false);
    test_bool(box_shadow_in_cascade(v, 0, outside), false);
    test_uint(box_shadow_cache_pick(&cache, v), 0);
}

void Shadows_camera_rotation_marks_every_cascade(void)
{
    // views/shadows
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    box_shadow_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    box_shadow_cache_view(&cache, v);
    uint32_t i;
    for (i = 0; i < v->shadow_cascades; i ++) {
        box_shadow_cache_done(&cache, v, i);
    }

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_set(w, cam, Rotation, {0.2f, 0.8f, 0});
    box_render_extract(w, &packet);
    v = box_vec_at(&packet.views, 0);
    box_shadow_cache_view(&cache, v);

    for (i = 0; i < v->shadow_cascades; i ++) {
        test_bool(cache.dirty[i], true);
    }

    test_uint(box_shadow_cache_pick(&cache, v), 3u);
}

void Shadows_held_cascades_take_turns(void)
{
    // views/shadows
    box_view_t v;
    memset(&v, 0, sizeof(v));
    v.shadow_cascades = 4;
    box_shadow_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    uint32_t i;
    for (i = 0; i < 4; i ++) {
        cache.dirty[i] = true;
        cache.valid[i] = true;
    }

    test_uint(box_shadow_cache_pick(&cache, &v), 0x3u);
    box_shadow_cache_done(&cache, &v, 0);
    box_shadow_cache_done(&cache, &v, 1);
    for (i = 0; i < 4; i ++) {
        cache.dirty[i] = true;
    }

    test_uint(box_shadow_cache_pick(&cache, &v), 0x5u);
    box_shadow_cache_done(&cache, &v, 0);
    box_shadow_cache_done(&cache, &v, 2);
    for (i = 0; i < 4; i ++) {
        cache.dirty[i] = true;
    }

    test_uint(box_shadow_cache_pick(&cache, &v), 0x9u);
    box_shadow_cache_done(&cache, &v, 0);
    box_shadow_cache_done(&cache, &v, 3);
    for (i = 0; i < 4; i ++) {
        cache.dirty[i] = true;
    }

    test_uint(box_shadow_cache_pick(&cache, &v), 0x3u);
    cache.dirty[0] = false;
    cache.dirty[1] = false;
    test_uint(box_shadow_cache_pick(&cache, &v), 0xCu);
    cache.dirty[3] = false;
    test_uint(box_shadow_cache_pick(&cache, &v), 0x4u);
}

void Shadows_unheld_cascades_render_at_once(void)
{
    // views/shadows
    box_view_t v;
    memset(&v, 0, sizeof(v));
    v.shadow_cascades = 4;
    box_shadow_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    uint32_t i;
    for (i = 0; i < 4; i ++) {
        cache.dirty[i] = true;
    }

    cache.valid[0] = true;
    cache.valid[1] = true;
    cache.next = 1;
    test_uint(box_shadow_cache_pick(&cache, &v), 0xFu);
    cache.dirty[1] = false;
    test_uint(box_shadow_cache_pick(&cache, &v), 0xDu);
}

void Shadows_held_cascade_keeps_its_matrix(void)
{
    // views/shadows
    box_view_t v;
    memset(&v, 0, sizeof(v));
    v.shadow_cascades = 3;
    v.shadow_matrix[0] = 1;
    v.shadow_matrix[16] = 2;
    v.shadow_matrix[32] = 3;
    box_shadow_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    test_assert(box_shadow_cache_matrix(&cache, &v, 0, 0) == v.shadow_matrix);
    box_shadow_cache_done(&cache, &v, 0);
    box_shadow_cache_done(&cache, &v, 1);
    box_shadow_cache_done(&cache, &v, 2);
    v.shadow_matrix[0] = 5;
    v.shadow_matrix[16] = 6;
    v.shadow_matrix[32] = 7;
    box_shadow_cache_view(&cache, &v);
    uint32_t i;
    for (i = 0; i < 3; i ++) {
        test_bool(cache.dirty[i], true);
    }

    uint32_t rebuild = box_shadow_cache_pick(&cache, &v);
    test_uint(rebuild, 0x3u);
    test_assert(box_shadow_cache_matrix(&cache, &v, 0, rebuild) == v.shadow_matrix);
    test_assert(box_shadow_cache_matrix(&cache, &v, 1, rebuild) == v.shadow_matrix + 16);
    const float *held = box_shadow_cache_matrix(&cache, &v, 2, rebuild);
    test_assert(held == cache.matrix[2]);
    test_assert(held[0] == 3);
}
