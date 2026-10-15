/**
 * @file RenderPacket.c
 * @brief Tests for the render packet ring, the render thread and the null backend.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

static ecs_world_t *unpause_world = NULL;
static bool unpaused = false;
static int32_t custom_renders = 0;
static int32_t custom_finis = 0;

static void ExtractContents(ecs_iter_t *it) {
    box_render_packet_t *p = box_render_packet(it->world);
    box_render_batch_t *batch = box_render_packet_batch(p);
    batch->pipeline = 3;
    batch->mesh = 7;
    batch->instances = 11;
    batch->first_instance = 2;
    batch->instance_count = 4;

    uint8_t *bytes = box_render_packet_upload(p, 11, 32, 64);
    int32_t i;
    for (i = 0; i < 64; i ++) {
        bytes[i] = (uint8_t)i;
    }

    uint32_t *desc = box_render_packet_resource(
        p, BOX_RENDER_MESH, BOX_RENDER_CREATE, 7, 2 * sizeof(uint32_t));
    desc[0] = 24;
    desc[1] = 36;
}

/** Bytes of packet data ExtractLarge appends per frame, above BOX_RENDER_DATA_KEEP */
#define RENDER_PACKET_LARGE (20 * 1024 * 1024)

/** Bytes ExtractLarge appends: RENDER_PACKET_LARGE, or 64 to let the workload drop */
static uint32_t extract_large_size = RENDER_PACKET_LARGE;

static void ExtractLarge(ecs_iter_t *it) {
    box_render_packet_t *p = box_render_packet(it->world);
    uint8_t *bytes = box_render_packet_upload(p, 11, 0, extract_large_size);
    uint32_t i;
    for (i = 0; i < extract_large_size; i += 4096) {
        bytes[i] = (uint8_t)i;
    }
}

static void* UnpauseLater(void *arg) {
    (void)arg;
    ecs_sleepf(0.05);
    unpaused = true;
    box_render_pause(unpause_world, false);

    return NULL;
}

static int FailInit(void *ctx) {
    (void)ctx;

    return 1;
}

static void CountRender(void *ctx, const box_render_packet_t *packet) {
    (void)ctx;
    (void)packet;
    custom_renders ++;
}

static void CountFini(void *ctx) {
    (void)ctx;
    custom_finis ++;
}

void RenderPacket_defaults(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const RenderSettings *settings = ecs_singleton_get(world, RenderSettings);
    test_assert(settings != NULL);
    test_int(settings->in_flight, 2);
    test_bool(settings->main_thread, false);

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_assert(status != NULL);
    test_int(status->submitted, 0);
    test_int(status->rendered, 0);
    test_int(status->in_flight, 0);
    test_int(status->stalls, 0);

    ecs_fini(world);
}

void RenderPacket_packet_per_frame(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    box_render_sync(world);

    test_int(null->inits, 1);
    test_int(null->packets, 3);
    test_int(null->gaps, 0);
    test_flt(null->delta, 0.5f);
    test_flt(null->time, 1.5);

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->submitted, 3);
    test_bool(status->threaded, true);

    ecs_fini(world);
    test_int(null->finis, 1);
    box_render_null_free(null);
}

void RenderPacket_render_thread_owns_backend(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    ecs_fini(world);

    test_int(null->inits, 1);
    test_int(null->packets, 10);
    test_int(null->finis, 1);
    test_int(null->wrong_thread, 0);
    test_assert(null->thread != ecs_os_thread_self());
    box_render_null_free(null);
}

void RenderPacket_main_thread_consumes(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, RenderSettings, { .in_flight = 2, .main_thread = true });

    ecs_progress(world, 0);
    test_int(null->packets, 1);
    ecs_progress(world, 0);
    test_int(null->packets, 2);
    test_assert(null->thread == ecs_os_thread_self());

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->submitted, 2);
    test_int(status->rendered, 2);
    test_int(status->in_flight, 0);
    test_bool(status->threaded, false);

    ecs_fini(world);
    test_int(null->finis, 1);
    test_int(null->wrong_thread, 0);
    box_render_null_free(null);
}

void RenderPacket_backpressure_full_ring(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    box_render_pause(world, true);

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->submitted, 2);
    test_int(status->rendered, 0);
    test_int(status->in_flight, 2);
    test_int(status->stalls, 0);
    test_int(null->packets, 0);

    box_render_pause(world, false);
    box_render_sync(world);
    test_int(null->packets, 2);

    ecs_progress(world, 0);
    status = ecs_singleton_get(world, RenderStatus);
    test_int(status->stalls, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void RenderPacket_backpressure_stalls_main_thread(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    box_render_pause(world, true);

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    unpause_world = world;
    unpaused = false;
    ecs_os_thread_t thread = ecs_os_thread_new(UnpauseLater, NULL);
    ecs_progress(world, 0);
    test_bool(unpaused, true);
    ecs_os_thread_join(thread);

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->submitted, 3);
    test_int(status->stalls, 1);
    test_assert(status->in_flight <= 2);

    box_render_sync(world);
    test_int(null->packets, 3);
    test_int(null->gaps, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void RenderPacket_in_flight_setting(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, RenderSettings, { .in_flight = 3 });
    box_render_pause(world, true);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->in_flight, 3);
    test_int(status->stalls, 0);

    box_render_pause(world, false);
    box_render_sync(world);
    test_int(null->packets, 3);

    ecs_fini(world);
    box_render_null_free(null);
}

void RenderPacket_settings_change_drains(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    box_render_pause(world, true);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_singleton_set(world, RenderSettings, { .in_flight = 4 });
    ecs_progress(world, 0);

    test_int(null->packets, 2);
    test_int(null->inits, 1);
    test_int(null->finis, 0);

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->in_flight, 1);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    status = ecs_singleton_get(world, RenderStatus);
    test_int(status->in_flight, 4);
    test_int(status->stalls, 0);

    box_render_pause(world, false);
    box_render_sync(world);
    test_int(null->packets, 6);
    test_int(null->gaps, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void RenderPacket_switch_to_main_thread(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_singleton_set(world, RenderSettings, { .in_flight = 2, .main_thread = true });
    ecs_progress(world, 0);

    test_int(null->packets, 3);
    test_int(null->inits, 2);
    test_int(null->finis, 1);
    test_int(null->gaps, 0);
    test_assert(null->thread == ecs_os_thread_self());

    ecs_fini(world);
    test_int(null->finis, 2);
    box_render_null_free(null);
}

void RenderPacket_no_alloc_after_warmup(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ExtractContents" }),
        .phase = RenderExtract,
        .callback = ExtractContents
    });

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, { .fov = 60, .near_ = 0.1f });
    ecs_set(world, camera, Position, { 1, 2, 3 });
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, { .camera = camera });

    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    for (i = 0; i < 100; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    test_int(after - before, 0);

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->submitted, 110);
    test_int(status->rendered, 110);

    ecs_fini(world);
}

void RenderPacket_no_alloc_after_warmup_main_thread(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, RenderSettings, { .in_flight = 2, .main_thread = true });
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ExtractContents" }),
        .phase = RenderExtract,
        .callback = ExtractContents
    });

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, { .fov = 60, .near_ = 0.1f });
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, { .camera = camera });

    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    for (i = 0; i < 100; i ++) {
        ecs_progress(world, 0);
    }

    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    test_int(after - before, 0);

    ecs_fini(world);
}

void RenderPacket_shutdown_with_packets_in_flight(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    box_render_pause(world, true);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->in_flight, 2);
    test_int(null->packets, 0);

    ecs_fini(world);

    test_int(null->packets, 2);
    test_int(null->gaps, 0);
    test_int(null->finis, 1);
    test_int(null->wrong_thread, 0);
    box_render_null_free(null);
}

void RenderPacket_shutdown_without_frames(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_fini(world);

    test_int(null->inits, 0);
    test_int(null->packets, 0);
    test_int(null->finis, 0);
    box_render_null_free(null);
}

void RenderPacket_set_backend_replaces(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *first = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(first);
    box_render_set_backend(world, &backend);
    box_render_pause(world, true);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    box_render_null_t *second = box_render_null_new();
    backend = box_render_null_backend(second);
    box_render_set_backend(world, &backend);

    test_int(first->packets, 2);
    test_int(first->finis, 1);
    test_int(second->inits, 0);

    box_render_pause(world, false);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(first->packets, 2);
    test_int(second->inits, 1);
    test_int(second->packets, 1);

    ecs_fini(world);
    test_int(second->finis, 1);
    box_render_null_free(first);
    box_render_null_free(second);
}

void RenderPacket_backend_init_fails(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    custom_renders = 0;
    custom_finis = 0;
    box_render_backend_t backend = {
        .init = FailInit,
        .render = CountRender,
        .fini = CountFini
    };
    box_render_set_backend(world, &backend);

    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    test_int(status->submitted, 3);
    test_int(status->rendered, 3);

    ecs_fini(world);
    test_int(custom_renders, 0);
    test_int(custom_finis, 0);
}

void RenderPacket_batch_upload_resource_contents(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ExtractContents" }),
        .phase = RenderExtract,
        .callback = ExtractContents
    });

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(null->batches, 2);
    test_int(null->uploads, 2);
    test_int(null->resources, 2);
    test_int(null->bytes, 128);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->batches), 1);
    box_render_batch_t *batch = ecs_vec_first_t(&p->batches, box_render_batch_t);
    test_uint(batch->pipeline, 3);
    test_uint(batch->mesh, 7);
    test_uint(batch->instances, 11);
    test_uint(batch->first_instance, 2);
    test_uint(batch->instance_count, 4);

    test_int(ecs_vec_count(&p->uploads), 1);
    box_render_upload_t *upload = ecs_vec_first_t(&p->uploads, box_render_upload_t);
    test_uint(upload->buffer, 11);
    test_uint(upload->offset, 32);
    test_uint(upload->size, 64);
    const uint8_t *bytes = box_render_packet_data(p, upload->data);
    test_int(bytes[0], 0);
    test_int(bytes[63], 63);

    test_int(ecs_vec_count(&p->resources), 1);
    box_render_resource_t *res = ecs_vec_first_t(&p->resources, box_render_resource_t);
    test_int(res->kind, BOX_RENDER_MESH);
    test_int(res->op, BOX_RENDER_CREATE);
    test_uint(res->handle, 7);
    test_uint(res->size, 8);
    const uint32_t *desc = box_render_packet_data(p, res->data);
    test_uint(desc[0], 24);
    test_uint(desc[1], 36);

    ecs_fini(world);
    box_render_null_free(null);
}

void RenderPacket_headless_fixed_delta(void) {
    // engine_core/render_thread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, App, {
        .headless = true,
        .frames = 5,
        .fps = 50,
        .cpu_budget = 100,
        .unguarded = true,
        .unpaced = true
    });

    test_int(box_app_run(world), 0);
    box_render_sync(world);

    test_int(null->packets, 5);
    test_int(null->gaps, 0);
    test_int(null->frame, 4);
    test_flt(null->delta, 0.02f);
    test_flt(null->time, 0.1);

    ecs_fini(world);
    box_render_null_free(null);
}

void RenderPacket_no_alloc_after_warmup_large(void) {
    // render_fixes/packet_data_trim
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    extract_large_size = RENDER_PACKET_LARGE;
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ExtractLarge" }),
        .phase = RenderExtract,
        .callback = ExtractLarge
    });

    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    for (i = 0; i < 100; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    test_int(after - before, 0);

    ecs_fini(world);
}

void RenderPacket_large_data_released_after_drop(void) {
    // render_fixes/packet_data_trim
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    extract_large_size = RENDER_PACKET_LARGE;
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ExtractLarge" }),
        .phase = RenderExtract,
        .callback = ExtractLarge
    });

    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    extract_large_size = 64;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    extract_large_size = RENDER_PACKET_LARGE;
    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    test_int(after - before, 0);

    extract_large_size = 64;
    for (i = 0; i < 1000; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    extract_large_size = RENDER_PACKET_LARGE;
    before = ecs_os_api_malloc_count + ecs_os_api_calloc_count + ecs_os_api_realloc_count;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    after = ecs_os_api_malloc_count + ecs_os_api_calloc_count + ecs_os_api_realloc_count;
    test_assert(after - before > 0);

    ecs_fini(world);
}
