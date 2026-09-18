/**
 * @file Svg.c
 * @brief Benchmarks for the cost of rasterizing an SVG icon into a texture.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/job/job_internal.h"

/** Edge length the icon is rasterized at */
#define BENCH_SVG_SIZE (256)

/** A gear icon with a stroked ring, a gradient badge and a notched outline */
static const char *bench_svg_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
    "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"64\">"
    "<stop offset=\"0\" stop-color=\"#8ecbff\"/><stop offset=\"1\" stop-color=\"#1b64a8\"/>"
    "</linearGradient></defs>"
    "<path fill=\"url(#g)\" d=\"M28 4h8l1.6 7.2 5.2 2.2 6-4.2 5.6 5.6-4.2 6 2.2 5.2L60 28v8"
    "l-7.2 1.6-2.2 5.2 4.2 6-5.6 5.6-6-4.2-5.2 2.2L36 60h-8l-1.6-7.2-5.2-2.2-6 4.2-5.6-5.6"
    "4.2-6-2.2-5.2L4 36v-8l7.2-1.6 2.2-5.2-4.2-6 5.6-5.6 6 4.2 5.2-2.2z\"/>"
    "<circle cx=\"32\" cy=\"32\" r=\"11\" fill=\"#0b1a2b\"/>"
    "<circle cx=\"32\" cy=\"32\" r=\"15\" fill=\"none\" stroke=\"#ffffff\" stroke-width=\"2\"/>"
    "</svg>";

void Svg_rasterize_256(bench_t *b)
{
    // textures/svg
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxTexture);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t e = ecs_new(world);
    int32_t i;
    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        box_render_extract(world, &packet);
        for (i = 0; i < (int32_t)packet.texture_create.count; i ++) {
            free(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, i))->pixels);
        }

        box_packet_reset(&packet);
        bench_resume(b);
        ecs_set(world, e, Svg, { .data = ecs_os_strdup(bench_svg_icon),
            .width = BENCH_SVG_SIZE, .height = BENCH_SVG_SIZE });
        box_job_flush(world);
        bench_keep(ecs_get(world, e, RenderTexture)->handle);
    }

    box_render_extract(world, &packet);
    for (i = 0; i < (int32_t)packet.texture_create.count; i ++) {
        free(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, i))->pixels);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}
