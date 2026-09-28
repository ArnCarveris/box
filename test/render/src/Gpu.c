/**
 * @file Gpu.c
 * @brief Tests that the renderer times the passes of a frame when it is asked to.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/renderer_internal.h"
#include <ctype.h>
#include <string.h>

/** Frames a test renders, enough for a resolve to arrive */
#define GPU_FRAMES (8)

/** Edge of the offscreen target the tests render into */
#define GPU_SIZE (128)

void Gpu_timings_are_off_until_they_are_asked_for(void)
{
    // monitoring/gpu_timings
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 55});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0.5f, 0});
    ecs_set(w, e, Box, {1, 1, 1});

    box_renderer_desc_t desc = { .width = GPU_SIZE, .height = GPU_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);

    int32_t i;
    for (i = 0; i < GPU_FRAMES; i ++) {
        box_packet_t *p = box_renderer_acquire(r);
        test_assert(p != NULL);
        p->width = GPU_SIZE;
        p->height = GPU_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_renderer_gpu_t gpu;
    box_renderer_gpu_timings(r, &gpu);
    test_int(gpu.count, 0);
    test_flt(gpu.frame_ms, 0.0);
    test_int(box_renderer_errors(r), 0);
    box_renderer_destroy(r);
    ecs_fini(w);
}

void Gpu_enabled_timings_report_the_passes_of_a_frame(void)
{
    // monitoring/gpu_timings
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 55});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0.5f, 0});
    ecs_set(w, e, Box, {1, 1, 1});

    box_renderer_desc_t desc = { .width = GPU_SIZE, .height = GPU_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_gpu_timings(r, true);

    int32_t i;
    for (i = 0; i < GPU_FRAMES; i ++) {
        box_packet_t *p = box_renderer_acquire(r);
        test_assert(p != NULL);
        p->width = GPU_SIZE;
        p->height = GPU_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_renderer_gpu_t gpu;
    box_renderer_gpu_timings(r, &gpu);
    test_int(box_renderer_errors(r), 0);
    if (gpu.supported) {
        test_assert(gpu.count > 0);
        test_assert(gpu.frame_ms >= 0.0);
        bool opaque = false;
        uint32_t pass;
        for (pass = 0; pass < gpu.count; pass ++) {
            opaque |= !ecs_os_strcmp(gpu.passes[pass].name, "opaque");
        }

        test_bool(opaque, true);
    } else {
        test_int(gpu.count, 0);
    }

    box_renderer_destroy(r);
    ecs_fini(w);
}

void Gpu_disabled_timings_stop_the_queries(void)
{
    // monitoring/gpu_timings
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 55});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0.5f, 0});
    ecs_set(w, e, Box, {1, 1, 1});

    box_renderer_desc_t desc = { .width = GPU_SIZE, .height = GPU_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_gpu_timings(r, true);

    int32_t i;
    for (i = 0; i < GPU_FRAMES * 2; i ++) {
        if (i == GPU_FRAMES) {
            box_renderer_set_gpu_timings(r, false);
        }

        box_packet_t *p = box_renderer_acquire(r);
        test_assert(p != NULL);
        p->width = GPU_SIZE;
        p->height = GPU_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    test_int(box_renderer_frames(r), GPU_FRAMES * 2);
    test_int(box_renderer_errors(r), 0);
    box_renderer_destroy(r);
    ecs_fini(w);
}

/** Copy the device name of the adapter a renderer picked, in lower case.
 *
 * @param r The renderer
 * @param out Receives the name
 * @param size Size of out
 */
static void gpu_adapter_name(
    box_renderer_t *r,
    char *out,
    size_t size)
{
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    size_t i, len = 0;
    out[0] = '\0';
    if (wgpuAdapterGetInfo(r->adapter, &info) == WGPUStatus_Success && info.device.data) {
        len = info.device.length == WGPU_STRLEN ? strlen(info.device.data) : info.device.length;
    }

    for (i = 0; i < len && i + 1 < size; i ++) {
        out[i] = (char)tolower((unsigned char)info.device.data[i]);
    }

    out[i] = '\0';
    wgpuAdapterInfoFreeMembers(info);
}

void Gpu_a_power_preference_picks_an_adapter(void)
{
    // launcher/gpu_selection
    box_renderer_desc_t desc = { .width = GPU_SIZE, .height = GPU_SIZE, .gpu = "high-performance" };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    test_assert(r->adapter != NULL);
    box_renderer_destroy(r);

    desc.gpu = "low-power";
    r = box_renderer_create(&desc);
    test_assert(r != NULL);
    test_assert(r->adapter != NULL);
    box_renderer_destroy(r);
}

void Gpu_part_of_an_adapter_name_picks_that_adapter(void)
{
    // launcher/gpu_selection
    char name[256], picked[256];
    box_renderer_desc_t desc = { .width = GPU_SIZE, .height = GPU_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    gpu_adapter_name(r, name, sizeof(name));
    box_renderer_destroy(r);
    test_assert(name[0] != '\0');

    desc.gpu = name;
    r = box_renderer_create(&desc);
    test_assert(r != NULL);
    gpu_adapter_name(r, picked, sizeof(picked));
    test_str(picked, name);
    box_renderer_destroy(r);
}

void Gpu_a_name_no_adapter_has_fails(void)
{
    // launcher/gpu_selection
    box_renderer_desc_t desc = {
        .width = GPU_SIZE, .height = GPU_SIZE, .gpu = "no adapter is called this"
    };
    test_assert(box_renderer_create(&desc) == NULL);
}
