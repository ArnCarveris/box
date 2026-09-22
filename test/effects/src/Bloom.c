#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/renderer_internal.h"

static ecs_world_t *w;
static box_renderer_t *renderer;
static ecs_entity_t bloom;
static ecs_entity_t bloom_params;

static void bloom_set(
    float temporal)
{
    void *ptr = ecs_ensure_id(w, bloom, bloom_params, (size_t)ecs_get_type_info(w, bloom_params)->size);
    ecs_value_t value = { .type = bloom_params, .ptr = ptr };
    char expr[256];
    snprintf(expr, sizeof(expr),
        "{intensity: 0.3, low_frequency_boost: 0.7, "
        "low_frequency_boost_curvature: 0.95, high_pass_frequency: 1, "
        "prefilter: {threshold: 1, threshold_softness: 0}, "
        "max_mip_dimension: 512, firefly_clamp: 12, temporal: %g, "
        "karis: true, full_res_first_mip: true}", temporal);
    test_assert(ecs_expr_run(w, expr, &value, NULL) != NULL);
    ecs_modified_id(w, bloom, bloom_params);
}

void Bloom_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    test_assert(box_script_load(w, BOX_EFFECTS_BUILTINS) != 0);
    ecs_entity_t cam = ecs_entity(w, { .name = "camera" });
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    bloom = ecs_lookup(w, "box.effects.stack.bloom");
    bloom_params = ecs_lookup(w, "box.effects.Bloom");
    ecs_get_mut(w, bloom, Effect)->enabled = true;
    ecs_modified(w, bloom, Effect);
    bloom_set(0.8f);
    renderer = box_renderer_create(&(box_renderer_desc_t){ .width = 64, .height = 32 });
    test_assert(renderer != NULL);
    test_assert(box_renderer_ready(renderer));
}

void Bloom_teardown(void)
{
    box_renderer_destroy(renderer);
    ecs_fini(w);
}

static box_gpu_view_t *bloom_frame(uint32_t frame, uint32_t width, uint32_t height)
{
    box_packet_t *p = box_renderer_acquire(renderer);
    test_assert(p != NULL);
    p->width = width;
    p->height = height;
    box_render_extract(w, p);
    p->frame = frame;
    box_renderer_submit(renderer, p);
    test_assert(renderer->views.count > 0);
    return box_vec_at(&renderer->views, 0);
}

void Bloom_history_survives_frames_and_resizes(void)
{
    box_gpu_view_t *v = bloom_frame(1, 64, 32);
    test_bool(v->setup_mip_history_valid, true);
    test_uint(v->setup_mip_width, 32);
    test_uint(v->setup_mip_height, 16);
    uint32_t index = v->setup_mip_history_index;
    v = bloom_frame(2, 64, 32);
    test_uint(v->setup_mip_history_frame, 2);
    test_uint(v->setup_mip_history_index, 1 - index);
    v = bloom_frame(3, 96, 64);
    test_bool(v->setup_mip_history_valid, true);
    test_uint(v->setup_mip_width, 48);
    test_uint(v->setup_mip_height, 32);
    test_uint(v->setup_mip_history_frame, 3);
    test_uint(v->setup_mip_history_index, 1);
    bloom_set(0);
    v = bloom_frame(4, 96, 64);
    test_bool(v->setup_mip_history_valid, false);
    test_assert(v->setup_mip_history[0] == NULL);
    test_assert(v->setup_mip_history[1] == NULL);
    bloom_set(0.8f);
    v = bloom_frame(5, 96, 64);
    test_bool(v->setup_mip_history_valid, true);
    test_uint(v->setup_mip_history_frame, 5);
}

void Bloom_growing_effect_stack_rebinds_uniforms(void)
{
    box_gpu_view_t *v = bloom_frame(1, 64, 32);
    test_assert(v->setup_mip_uniform == renderer->effect_uniform);
    ecs_entity_t tonemap = ecs_lookup(w, "box.effects.stack.tonemap");
    ecs_get_mut(w, tonemap, Effect)->enabled = true;
    ecs_modified(w, tonemap, Effect);
    v = bloom_frame(2, 64, 32);
    test_assert(renderer->effect_uniform_slots >= 2);
    test_assert(v->setup_mip_uniform == renderer->effect_uniform);
    test_bool(v->setup_mip_history_valid, true);
    test_uint(v->setup_mip_history_frame, 2);
}

void Bloom_history_is_separate_for_each_view(void)
{
    ecs_entity_t cam = ecs_entity(w, { .name = "second_camera" });
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t first_view = ecs_new(w);
    ecs_add(w, first_view, View);
    ecs_get_mut(w, first_view, View)->camera = ecs_lookup(w, "camera");
    ecs_modified(w, first_view, View);
    ecs_entity_t second_view = ecs_new(w);
    ecs_add(w, second_view, View);
    ecs_get_mut(w, second_view, View)->camera = cam;
    ecs_modified(w, second_view, View);
    bloom_frame(1, 64, 32);
    test_int(renderer->views.count, 2);
    box_gpu_view_t *first = box_vec_at(&renderer->views, 0);
    box_gpu_view_t *second = box_vec_at(&renderer->views, 1);
    test_bool(first->setup_mip_history_valid, true);
    test_bool(second->setup_mip_history_valid, true);
    test_assert(first->setup_mip_history[0] != second->setup_mip_history[0]);
    test_assert(first->setup_mip_history[1] != second->setup_mip_history[1]);
    bloom_frame(2, 64, 32);
    test_uint(first->setup_mip_history_frame, 2);
    test_uint(second->setup_mip_history_frame, 2);
}
