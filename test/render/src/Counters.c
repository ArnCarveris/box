/**
 * @file Counters.c
 * @brief Tests for the per frame counts of the renderer and the resources it tracks.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/renderer_internal.h"

/** Frames a test renders, enough for the read back of a counted frame to arrive */
#define COUNTERS_FRAMES (8)

/** Edge of the offscreen target the tests render into */
#define COUNTERS_SIZE (128)

void Counters_triangles_follow_the_topology(void)
{
    // monitoring/stats_command
    test_int(box_count_triangles(BOX_COUNT_TRIANGLES, 36, 2), 24);
    test_int(box_count_triangles(BOX_COUNT_TRIANGLES, 3, 1), 1);
    test_int(box_count_triangles(BOX_COUNT_STRIP, 4, 10), 20);
    test_int(box_count_triangles(BOX_COUNT_STRIP, 2, 10), 0);
    test_int(box_count_triangles(BOX_COUNT_LINES, 200, 1), 0);
}

void Counters_passes_with_one_name_share_an_entry(void)
{
    // monitoring/stats_command
    box_counters_t *c = calloc(1, sizeof(box_counters_t));
    test_int(box_count_pass(c, "shadow", false), 0);
    test_int(box_count_pass(c, "opaque", false), 1);
    test_int(box_count_pass(c, "shadow", false), 0);
    test_int(box_count_pass(c, "shadow", true), 2);
    test_int(c->pass_count, 3);
    test_str(c->passes[1].name, "opaque");
    test_bool(c->passes[2].compute, true);
    free(c);
}

void Counters_a_full_pass_table_takes_no_more(void)
{
    // monitoring/stats_command
    box_counters_t *c = calloc(1, sizeof(box_counters_t));
    int32_t i;
    for (i = 0; i < BOX_COUNT_PASS_MAX; i ++) {
        char name[16];
        ecs_os_snprintf(name, sizeof(name), "pass_%d", i);
        test_int(box_count_pass(c, name, false), i);
    }

    test_int(box_count_pass(c, "one_too_many", false), -1);
    test_int(box_count_pass(c, "pass_3", false), 3);
    test_int(c->pass_count, BOX_COUNT_PASS_MAX);
    free(c);
}

void Counters_a_long_name_is_truncated(void)
{
    // monitoring/stats_command
    box_counters_t *c = calloc(1, sizeof(box_counters_t));
    box_count_pass(c, "a_pass_name_that_does_not_fit_in_the_field", false);
    test_int(ecs_os_strlen(c->passes[0].name), BOX_COUNT_NAME_MAX - 1);
    char name[BOX_COUNT_NAME_MAX];
    box_count_name(name, "opaque_pass", 6);
    test_str(name, "opaque");
    free(c);
}

void Counters_texture_bytes_cover_every_mip_and_layer(void)
{
    // monitoring/stats_command
    WGPUTextureDescriptor desc = {
        .dimension = WGPUTextureDimension_2D,
        .size = { 960, 540, 1 },
        .format = WGPUTextureFormat_BGRA8Unorm,
        .mipLevelCount = 5,
        .sampleCount = 1
    };
    test_int(box_count_texture_bytes(&desc),
        (960 * 540 + 480 * 270 + 240 * 135 + 120 * 67 + 60 * 33) * 4);

    desc.size = (WGPUExtent3D){ 2048, 2048, 4 };
    desc.format = WGPUTextureFormat_Depth32Float;
    desc.mipLevelCount = 1;
    test_int(box_count_texture_bytes(&desc), 2048ull * 2048 * 4 * 4);

    desc.size = (WGPUExtent3D){ 16, 16, 16 };
    desc.dimension = WGPUTextureDimension_3D;
    desc.format = WGPUTextureFormat_RGBA16Float;
    desc.mipLevelCount = 2;
    test_int(box_count_texture_bytes(&desc), (16 * 16 * 16 + 8 * 8 * 8) * 8);
}

void Counters_block_compressed_bytes_round_up_to_blocks(void)
{
    // monitoring/stats_command
    WGPUTextureDescriptor desc = {
        .dimension = WGPUTextureDimension_2D,
        .size = { 6, 6, 1 },
        .format = WGPUTextureFormat_BC1RGBAUnorm,
        .mipLevelCount = 2,
        .sampleCount = 4
    };
    test_int(box_count_texture_bytes(&desc), (2 * 2 * 8 + 1 * 1 * 8) * 4);
}

void Counters_nothing_is_counted_until_asked(void)
{
    // monitoring/stats_command
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

    box_renderer_desc_t desc = { .width = COUNTERS_SIZE, .height = COUNTERS_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);

    int32_t i;
    for (i = 0; i < COUNTERS_FRAMES; i ++) {
        box_packet_t *p = box_renderer_acquire(r);
        p->width = COUNTERS_SIZE;
        p->height = COUNTERS_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_counters_t *c = ecs_os_calloc_t(box_counters_t);
    test_bool(box_renderer_counters(r, c), false);
    test_int(c->draws, 0);
    test_bool(r->count.recording, false);
    test_assert(r->count.state.indirect == NULL);
    test_int(box_renderer_errors(r), 0);
    ecs_os_free(c);
    box_renderer_destroy(r);
    ecs_fini(w);
}

void Counters_one_mesh_draws_its_instances_at_once(void)
{
    // monitoring/stats_command
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 55});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {-1, 0.5f, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {1, 0.5f, 0});
    ecs_set(w, b, Box, {1, 1, 1});

    box_renderer_desc_t desc = { .width = COUNTERS_SIZE, .height = COUNTERS_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_counters(r, BOX_COUNT_FRAME);

    int32_t i;
    for (i = 0; i < COUNTERS_FRAMES; i ++) {
        box_packet_t *p = box_renderer_acquire(r);
        p->width = COUNTERS_SIZE;
        p->height = COUNTERS_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_counters_t *c = ecs_os_calloc_t(box_counters_t);
    test_bool(box_renderer_counters(r, c), true);
    test_int(c->frame, COUNTERS_FRAMES - 1);
    test_int(c->flags, BOX_COUNT_FRAME);
    test_bool(c->indirect_resolved, true);
    test_int(c->draws, 1);
    test_int(c->indirect_draws, 0);
    test_int(c->instances, 2);
    test_int(c->triangles, 24);
    test_int(c->render_passes, 1);
    test_int(c->compute_passes, 0);
    test_int(c->dispatches, 0);
    test_assert(c->pipeline_switches >= 1);
    test_assert(c->pipeline_sets >= c->pipeline_switches);
    test_assert(c->bind_changes >= 1);
    test_assert(c->bind_sets >= c->bind_changes);
    test_assert(c->buffer_uploads >= 1);
    test_assert(c->buffer_upload_bytes >= c->buffer_uploads);
    test_int(c->pass_count, 1);
    test_str(c->passes[0].name, "opaque");
    test_int(c->passes[0].draws, 1);
    test_int(c->pipeline_count, 0);
    test_int(box_renderer_errors(r), 0);
    ecs_os_free(c);
    box_renderer_destroy(r);
    ecs_fini(w);
}

void Counters_live_resources_match_the_device_report(void)
{
    // monitoring/stats_command
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

    box_renderer_desc_t desc = { .width = COUNTERS_SIZE, .height = COUNTERS_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_counters(r, BOX_COUNT_FRAME);

    int32_t i;
    for (i = 0; i < COUNTERS_FRAMES; i ++) {
        box_packet_t *p = box_renderer_acquire(r);
        p->width = COUNTERS_SIZE;
        p->height = COUNTERS_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_counters_t *c = ecs_os_calloc_t(box_counters_t);
    test_bool(box_renderer_counters(r, c), true);
    WGPUGlobalReport report;
    wgpuGenerateReport(r->instance, &report);
    test_int(c->buffers, report.hub.buffers.numKeptFromUser);
    test_int(c->textures, report.hub.textures.numKeptFromUser);
    test_assert(c->buffer_memory > 0);
    test_assert(c->texture_memory >= (uint64_t)COUNTERS_SIZE * COUNTERS_SIZE * 4);
    uint64_t usage = 0;
    for (i = 0; i < BOX_COUNT_USAGE_COUNT; i ++) {
        usage += c->usages[i].bytes;
    }

    test_int(usage, c->buffer_memory);
    test_int(box_renderer_errors(r), 0);
    ecs_os_free(c);
    box_renderer_destroy(r);
    ecs_fini(w);
}

void Counters_indirect_draws_are_resolved_from_their_arguments(void)
{
    // monitoring/stats_command
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 20, -1});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    int32_t side = 80;
    for (int32_t i = 0; i < side * side; i ++) {
        ecs_entity_t e = ecs_new(w);
        ecs_set(w, e, Position, {(float)(i % side) * 2.0f - (float)side,
            0, (float)(i / side) * 2.0f - (float)side});
        ecs_set(w, e, Box, {1, 1, 1});
    }

    box_renderer_desc_t desc = { .width = COUNTERS_SIZE, .height = COUNTERS_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_counters(r, BOX_COUNT_FRAME | BOX_COUNT_PASSES);

    int32_t i;
    for (i = 0; i < COUNTERS_FRAMES; i ++) {
        box_packet_t *p = box_renderer_acquire(r);
        p->width = COUNTERS_SIZE;
        p->height = COUNTERS_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_counters_t *c = ecs_os_calloc_t(box_counters_t);
    test_bool(box_renderer_counters(r, c), true);
    box_renderer_stats_t stats;
    box_renderer_stats(r, &stats);
    test_assert(c->indirect_draws > 0);
    test_bool(c->indirect_resolved, true);
    test_assert(c->dispatches > 0);
    test_assert(c->compute_passes > 0);
    test_assert(c->instances > 0);
    uint64_t opaque = 0;
    uint64_t fullscreen = 0;
    for (i = 0; i < (int32_t)c->pass_count; i ++) {
        const box_count_pass_t *pass = &c->passes[i];
        if (!ecs_os_strcmp(pass->name, "opaque") || !ecs_os_strcmp(pass->name, "uncovered")) {
            opaque += pass->instances;
            test_int(pass->triangles, pass->instances * 12);
        } else {
            fullscreen += pass->instances;
        }
    }

    test_int(opaque, stats.drawn);
    test_int(c->instances, opaque + fullscreen);
    test_int(box_renderer_errors(r), 0);
    ecs_os_free(c);
    box_renderer_destroy(r);
    ecs_fini(w);
}

void Counters_counting_again_forgets_the_last_frame(void)
{
    // monitoring/stats_command
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

    box_renderer_desc_t desc = { .width = COUNTERS_SIZE, .height = COUNTERS_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_counters(r, BOX_COUNT_FRAME);

    int32_t i;
    for (i = 0; i < COUNTERS_FRAMES * 2; i ++) {
        if (i == COUNTERS_FRAMES) {
            box_renderer_set_counters(r, 0);
        }

        box_packet_t *p = box_renderer_acquire(r);
        p->width = COUNTERS_SIZE;
        p->height = COUNTERS_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_counters_t *c = ecs_os_calloc_t(box_counters_t);
    test_bool(box_renderer_counters(r, c), true);
    test_int(c->frame, COUNTERS_FRAMES - 1);
    test_bool(r->count.recording, false);

    box_renderer_set_counters(r, BOX_COUNT_FRAME);
    test_bool(box_renderer_counters(r, c), false);

    box_packet_t *p = box_renderer_acquire(r);
    p->width = COUNTERS_SIZE;
    p->height = COUNTERS_SIZE;
    box_render_extract(w, p);
    box_renderer_submit(r, p);
    test_bool(box_renderer_counters(r, c), true);
    test_int(c->frame, COUNTERS_FRAMES * 2);
    test_int(box_renderer_errors(r), 0);
    ecs_os_free(c);
    box_renderer_destroy(r);
    ecs_fini(w);
}

void Counters_details_are_only_counted_when_asked(void)
{
    // monitoring/stats_details
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

    box_renderer_desc_t desc = { .width = COUNTERS_SIZE, .height = COUNTERS_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_counters(r, BOX_COUNT_FRAME);

    int32_t i;
    for (i = 0; i < COUNTERS_FRAMES; i ++) {
        box_packet_t *p = box_renderer_acquire(r);
        p->width = COUNTERS_SIZE;
        p->height = COUNTERS_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_counters_t *c = ecs_os_calloc_t(box_counters_t);
    test_bool(box_renderer_counters(r, c), true);
    test_assert(c->pipeline_sets > 0);
    test_assert(c->bind_sets > 0);
    test_assert(c->buffer_uploads > 0);
    test_int(c->pipeline_count, 0);
    test_int(c->bind_count, 0);
    for (i = 0; i < BOX_COUNT_USAGE_COUNT; i ++) {
        test_int(c->usages[i].uploads, 0);
    }

    ecs_os_free(c);
    box_renderer_destroy(r);
    ecs_fini(w);
}

void Counters_details_add_up_to_the_totals(void)
{
    // monitoring/stats_details
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
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {-1, 0.5f, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {1, 0.5f, 0});
    ecs_set(w, b, Sphere, {.radius = 0.5f, .segments = 16});

    box_renderer_desc_t desc = { .width = COUNTERS_SIZE, .height = COUNTERS_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_counters(r, BOX_COUNT_FRAME | BOX_COUNT_PASSES | BOX_COUNT_PIPELINES |
        BOX_COUNT_BIND_GROUPS | BOX_COUNT_BUFFERS | BOX_COUNT_COMPUTE);

    int32_t i;
    for (i = 0; i < COUNTERS_FRAMES; i ++) {
        box_packet_t *p = box_renderer_acquire(r);
        p->width = COUNTERS_SIZE;
        p->height = COUNTERS_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_counters_t *c = ecs_os_calloc_t(box_counters_t);
    test_bool(box_renderer_counters(r, c), true);
    uint64_t draws = 0, triangles = 0, switches = 0, sets = 0, changes = 0, recorded = 0;
    for (i = 0; i < (int32_t)c->pass_count; i ++) {
        draws += c->passes[i].draws;
        triangles += c->passes[i].triangles;
        switches += c->passes[i].pipeline_switches;
        sets += c->passes[i].bind_sets;
        changes += c->passes[i].bind_changes;
        recorded += c->passes[i].recorded;
    }

    test_int(draws, c->draws);
    test_int(triangles, c->triangles);
    test_int(switches, c->pipeline_switches);
    test_int(sets, c->bind_sets);
    test_int(changes, c->bind_changes);
    test_int(recorded, c->render_passes + c->compute_passes);

    uint64_t pipeline_sets = 0, pipeline_switches = 0, pipeline_draws = 0;
    bool mesh = false;
    for (i = 0; i < (int32_t)c->pipeline_count; i ++) {
        pipeline_sets += c->pipelines[i].sets;
        pipeline_switches += c->pipelines[i].switches;
        pipeline_draws += c->pipelines[i].draws;
        mesh |= !ecs_os_strncmp(c->pipelines[i].name, "mesh", 4);
    }

    test_bool(mesh, true);
    test_int(pipeline_sets, c->pipeline_sets);
    test_int(pipeline_switches, c->pipeline_switches);
    test_int(pipeline_draws, c->draws);

    uint64_t bind_sets = 0, bind_changes = 0;
    for (i = 0; i < (int32_t)c->bind_count; i ++) {
        test_assert(c->binds[i].pass < c->pass_count);
        test_assert(c->binds[i].changes <= c->binds[i].sets);
        bind_sets += c->binds[i].sets;
        bind_changes += c->binds[i].changes;
    }

    test_int(bind_sets, c->bind_sets);
    test_int(bind_changes, c->bind_changes);

    uint64_t uploads = 0, upload_bytes = 0, buffers = 0, bytes = 0;
    for (i = 0; i < BOX_COUNT_USAGE_COUNT; i ++) {
        uploads += c->usages[i].uploads;
        upload_bytes += c->usages[i].upload_bytes;
        buffers += c->usages[i].buffers;
        bytes += c->usages[i].bytes;
    }

    test_int(uploads, c->buffer_uploads);
    test_int(upload_bytes, c->buffer_upload_bytes);
    test_int(buffers, c->buffers);
    test_int(bytes, c->buffer_memory);
    test_assert(c->usages[BOX_COUNT_USAGE_UNIFORM].uploads > 0);
    test_int(box_renderer_errors(r), 0);
    ecs_os_free(c);
    box_renderer_destroy(r);
    ecs_fini(w);
}

void Counters_redundant_bind_group_sets_are_no_changes(void)
{
    // monitoring/stats_details
    box_counters_t *c = calloc(1, sizeof(box_counters_t));
    int32_t pass = box_count_pass(c, "opaque", false);
    test_int(box_count_bind(c, (uint32_t)pass, 3), 0);
    test_int(box_count_bind(c, (uint32_t)pass, 0), 1);
    test_int(box_count_bind(c, (uint32_t)pass, 3), 0);
    test_int(c->bind_count, 2);
    test_int(c->binds[1].slot, 0);
    int32_t other = box_count_pass(c, "sorted", false);
    test_int(box_count_bind(c, (uint32_t)other, 3), 2);
    free(c);
}

void Counters_compute_passes_count_their_workgroups(void)
{
    // monitoring/stats_details
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 160, -1});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    int32_t side = 80;
    for (int32_t i = 0; i < side * side; i ++) {
        ecs_entity_t e = ecs_new(w);
        ecs_set(w, e, Position, {(float)(i % side) * 2.0f - (float)side,
            0, (float)(i / side) * 2.0f - (float)side});
        ecs_set(w, e, Box, {1, 1, 1});
    }

    box_renderer_desc_t desc = { .width = COUNTERS_SIZE, .height = COUNTERS_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_counters(r, BOX_COUNT_FRAME | BOX_COUNT_COMPUTE | BOX_COUNT_PIPELINES);

    int32_t i;
    for (i = 0; i < COUNTERS_FRAMES; i ++) {
        box_packet_t *p = box_renderer_acquire(r);
        p->width = COUNTERS_SIZE;
        p->height = COUNTERS_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_counters_t *c = ecs_os_calloc_t(box_counters_t);
    test_bool(box_renderer_counters(r, c), true);
    uint64_t dispatches = 0, workgroups = 0, compute = 0, pipeline_dispatches = 0;
    for (i = 0; i < (int32_t)c->pass_count; i ++) {
        const box_count_pass_t *pass = &c->passes[i];
        if (pass->compute) {
            compute += pass->recorded;
            test_int(pass->draws, 0);
        } else {
            test_int(pass->dispatches, 0);
        }

        dispatches += pass->dispatches;
        workgroups += pass->workgroups;
    }

    for (i = 0; i < (int32_t)c->pipeline_count; i ++) {
        pipeline_dispatches += c->pipelines[i].dispatches;
        if (c->pipelines[i].compute) {
            test_int(c->pipelines[i].draws, 0);
        } else {
            test_int(c->pipelines[i].dispatches, 0);
        }
    }

    test_assert(c->dispatches > 0);
    test_int(dispatches, c->dispatches);
    test_int(pipeline_dispatches, c->dispatches);
    test_int(workgroups, c->workgroups);
    test_int(compute, c->compute_passes);
    test_assert(c->workgroups >= (uint64_t)side * side / 64);
    test_int(box_renderer_errors(r), 0);
    ecs_os_free(c);
    box_renderer_destroy(r);
    ecs_fini(w);
}
