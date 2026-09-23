/**
 * @file SvgJobs.c
 * @brief Benchmark for the frame cost of rasterizing a storm of SVGs on the job system.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/job/job_internal.h"

/** SVG textures the storm rasterizes per frame */
#define BENCH_SVG_STORM (1000)

/** Edge length every icon of the storm is rasterized at */
#define BENCH_SVG_STORM_SIZE (64)

/** A small badge icon with a gradient, a rounded rectangle and a star */
static const char *bench_svg_badge =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
    "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"64\" y2=\"64\">"
    "<stop offset=\"0\" stop-color=\"#5ee7ff\"/><stop offset=\"1\" stop-color=\"#2a6df4\"/>"
    "</linearGradient></defs>"
    "<rect x=\"4\" y=\"4\" width=\"56\" height=\"56\" rx=\"14\" fill=\"url(#g)\"/>"
    "<path fill=\"#ffffff\" d=\"M32 16l5 11 12 1-9 8 3 12-11-6-11 6 3-12-9-8 12-1z\"/></svg>";

/** Release the texture uploads a frame produced.
 *
 * @param p The packet
 */
static void bench_svg_drain(
    box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->texture_create.count; i ++) {
        free(((box_texture_cmd_t*)box_vec_at(&p->texture_create, i))->pixels);
    }

    box_packet_reset(p);
}

void SvgJobs_rasterize_1000_while_rendering(
    bench_t *b)
{
    // responsiveness/svg_jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t textures[BENCH_SVG_STORM];
    int32_t i;
    for (i = 0; i < BENCH_SVG_STORM; i ++) {
        textures[i] = ecs_new(world);
        ecs_entity_t quad = ecs_new(world);
        ecs_set(world, quad, Position, {(float)(i % 40), (float)(i / 40), 0});
        ecs_set(world, quad, Quad, {1, 1});
        ecs_set(world, quad, PbrTextures, { .albedo = textures[i] });
    }

    box_render_extract(world, &packet);
    bench_svg_drain(&packet);
    bench_set_items(b, BENCH_SVG_STORM);

    while (bench_iter(b)) {
        bench_pause(b);
        for (i = 0; i < BENCH_SVG_STORM; i ++) {
            ecs_set(world, textures[i], Svg, { .data = ecs_os_strdup(bench_svg_badge),
                .width = BENCH_SVG_STORM_SIZE, .height = BENCH_SVG_STORM_SIZE });
        }

        bench_resume(b);
        ecs_progress(world, 1.0f / 60.0f);
        box_render_extract(world, &packet);
        bench_pause(b);
        bench_keep(packet.texture_create.count);
        bench_svg_drain(&packet);
        box_job_flush(world);
        box_render_extract(world, &packet);
        bench_svg_drain(&packet);
        bench_resume(b);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}
