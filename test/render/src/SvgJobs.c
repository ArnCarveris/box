/**
 * @file SvgJobs.c
 * @brief Tests for incremental SVG rasterization on the job system.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/texture/svg_internal.h"

static const char *jobs_svg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\">"
    "<circle cx=\"32\" cy=\"32\" r=\"30\" fill=\"#ff0000\"/></svg>";

static ecs_world_t *w;
static box_packet_t packet;

void SvgJobs_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void SvgJobs_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);
        free(cmd->vertices);
        free(cmd->indices);
    }

    for (i = 0; i < packet.texture_create.count; i ++) {
        free(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, i))->pixels);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void SvgJobs_queues_a_job_instead_of_rasterizing(void)
{
    // responsiveness/svg_jobs
    JobSettings *js = ecs_singleton_ensure(w, JobSettings);
    js->workers = -1;
    ecs_singleton_modified(w, JobSettings);
    box_job_set_eye(w, vec3_new(0, 0, 0));

    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(jobs_svg), .width = 256, .height = 256 });
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 0);
    const RenderTexture *rt = ecs_get(w, t, RenderTexture);
    test_assert(rt != NULL);
    test_int(rt->handle, 0);
    const TextureImpl *impl = ecs_get(w, t, TextureImpl);
    test_assert(impl != NULL);
    test_bool(impl->pending, true);

    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 256);
    test_int(ecs_get(w, t, RenderTexture)->handle, cmd->handle);
    test_bool(ecs_get(w, t, TextureImpl)->pending, false);
}

void SvgJobs_previous_image_stays_while_resizing(void)
{
    // responsiveness/svg_jobs
    JobSettings *js = ecs_singleton_ensure(w, JobSettings);
    js->workers = -1;
    ecs_singleton_modified(w, JobSettings);
    box_job_set_eye(w, vec3_new(0, 0, 0));

    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(jobs_svg), .width = 256, .height = 256 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    uint32_t handle = cmd->handle;
    free(cmd->pixels);
    box_packet_reset(&packet);

    ecs_set(w, t, Svg, { .data = ecs_os_strdup(jobs_svg), .width = 512, .height = 512 });
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 0);
    test_int(packet.texture_destroy.count, 0);
    const RenderTexture *rt = ecs_get(w, t, RenderTexture);
    test_int(rt->handle, handle);
    test_int(rt->width, 256);
    test_bool(ecs_get(w, t, TextureImpl)->pending, true);

    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    test_int(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, 0))->width, 512);
    test_int(ecs_get(w, t, RenderTexture)->width, 512);
}

void SvgJobs_completion_uploads_on_the_main_thread(void)
{
    // responsiveness/svg_jobs
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(jobs_svg), .width = 256, .height = 256 });
    ecs_os_sleep(0, 200000000);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 0);
    test_int(ecs_get(w, t, RenderTexture)->handle, 0);

    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    uint8_t *center = cmd->pixels + ((size_t)128 * 256 + 128) * 4;
    test_int(center[0], 255);
    test_int(center[3], 255);
}

void SvgJobs_new_set_supersedes_the_queued_job(void)
{
    // responsiveness/svg_jobs
    JobSettings *js = ecs_singleton_ensure(w, JobSettings);
    js->workers = -1;
    ecs_singleton_modified(w, JobSettings);
    box_job_set_eye(w, vec3_new(0, 0, 0));

    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(jobs_svg), .width = 256, .height = 256 });
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(jobs_svg), .width = 512, .height = 512 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    test_int(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, 0))->width, 512);
    const JobStats *stats = ecs_singleton_get(w, JobStats);
    test_int(stats->dropped, 1);
    test_int(ecs_get(w, t, RenderTexture)->width, 512);
}

void SvgJobs_deleting_the_entity_cancels_the_job(void)
{
    // responsiveness/svg_jobs
    JobSettings *js = ecs_singleton_ensure(w, JobSettings);
    js->workers = -1;
    ecs_singleton_modified(w, JobSettings);
    box_job_set_eye(w, vec3_new(0, 0, 0));

    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(jobs_svg), .width = 256, .height = 256 });
    ecs_delete(w, t);
    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 0);
    const JobStats *stats = ecs_singleton_get(w, JobStats);
    test_int(stats->dropped, 1);
    test_int(stats->completed, 0);
}

void SvgJobs_bands_split_a_large_image(void)
{
    // responsiveness/svg_jobs
    int32_t small[2] = {256, 256};
    int32_t large[2] = {1024, 1024};
    test_int(box_svg_band_count(small), 1);
    test_int(box_svg_band_count(large), 4);

    JobSettings *js = ecs_singleton_ensure(w, JobSettings);
    js->workers = -1;
    ecs_singleton_modified(w, JobSettings);
    box_job_set_eye(w, vec3_new(0, 0, 0));

    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(jobs_svg), .width = 1024, .height = 1024 });
    box_job_flush(w);
    const JobStats *stats = ecs_singleton_get(w, JobStats);
    test_int(stats->completed, 4);

    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 1024);
    uint8_t *center = cmd->pixels + ((size_t)512 * 1024 + 512) * 4;
    test_int(center[0], 255);
    test_int(center[3], 255);
    uint8_t *seam = cmd->pixels + ((size_t)256 * 1024 + 512) * 4;
    test_int(seam[0], 255);
    test_int(seam[3], 255);
}

void SvgJobs_nearest_texture_rasterizes_first(void)
{
    // responsiveness/svg_jobs
    JobSettings *js = ecs_singleton_ensure(w, JobSettings);
    js->workers = -1;
    ecs_singleton_modified(w, JobSettings);

    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, 0});
    ecs_set(w, camera, Camera, { .fov = 50 });
    ecs_entity_t far = ecs_new(w);
    ecs_entity_t near = ecs_new(w);
    ecs_entity_t far_quad = ecs_new(w);
    ecs_set(w, far_quad, Position, {0, 0, 100});
    ecs_set(w, far_quad, Quad, {1, 1});
    ecs_set(w, far_quad, PbrTextures, { .albedo = far });
    ecs_entity_t near_quad = ecs_new(w);
    ecs_set(w, near_quad, Position, {0, 0, 2});
    ecs_set(w, near_quad, Quad, {1, 1});
    ecs_set(w, near_quad, PbrTextures, { .albedo = near });
    box_render_extract(w, &packet);
    box_packet_reset(&packet);

    ecs_set(w, far, Svg, { .data = ecs_os_strdup(jobs_svg), .width = 256, .height = 256 });
    ecs_set(w, near, Svg, { .data = ecs_os_strdup(jobs_svg), .width = 256, .height = 256 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 2);
    box_texture_cmd_t *first = box_vec_at(&packet.texture_create, 0);
    test_int(first->handle, ecs_get(w, near, RenderTexture)->handle);
    box_texture_cmd_t *second = box_vec_at(&packet.texture_create, 1);
    test_int(second->handle, ecs_get(w, far, RenderTexture)->handle);
}

/** OnSet observer on RenderTexture that queues a structural change, like a
 * reactive UI script does.
 *
 * @param it Iterator over the textures that were set
 */
static void SvgJobs_on_render_texture(
    ecs_iter_t *it)
{
    ecs_entity_t tag = (ecs_entity_t)(uintptr_t)it->ctx;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_add_id(it->world, it->entities[i], tag);
    }
}

void SvgJobs_publish_from_progress_with_observer(void)
{
    // textures/svg_publish_readonly
    JobSettings *js = ecs_singleton_ensure(w, JobSettings);
    js->workers = -1;
    js->budget_ms = 0;
    ecs_singleton_modified(w, JobSettings);

    ecs_entity_t tag = ecs_new(w);
    ecs_observer(w, {
        .query.terms = {{ .id = ecs_id(RenderTexture) }},
        .events = { EcsOnSet },
        .callback = SvgJobs_on_render_texture,
        .ctx = (void*)(uintptr_t)tag
    });

    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(jobs_svg), .width = 64, .height = 64 });
    test_int(ecs_get(w, t, RenderTexture)->handle, 0);

    ecs_progress(w, 0.016f);
    test_assert(ecs_get(w, t, RenderTexture)->handle != 0);
    test_assert(ecs_has_id(w, t, tag));
    test_bool(ecs_get(w, t, TextureImpl)->pending, false);
}
