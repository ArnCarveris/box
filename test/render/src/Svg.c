/**
 * @file Svg.c
 * @brief Tests for SVG rasterization into the texture upload path.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/texture/texture_internal.h"
#include "../../../src/job/job_internal.h"

static const char *svg_halves =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
    "<rect x=\"0\" y=\"0\" width=\"64\" height=\"32\" fill=\"#ff0000\"/>"
    "<rect x=\"0\" y=\"32\" width=\"64\" height=\"32\" fill=\"#0000ff\"/></svg>";

static const char *svg_circle =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\">"
    "<circle cx=\"32\" cy=\"32\" r=\"30\" fill=\"#ff0000\"/></svg>";

static const char *svg_text =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
    "<rect width=\"64\" height=\"64\" fill=\"#000000\"/>"
    "<text x=\"32\" y=\"48\" font-family=\"B612\" font-size=\"64\" fill=\"#ffffff\""
    " text-anchor=\"middle\">I</text></svg>";

static const char *svg_mask =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
    "<defs><mask id=\"m\"><rect width=\"64\" height=\"32\" fill=\"#ffffff\"/></mask></defs>"
    "<rect width=\"64\" height=\"64\" fill=\"#00ff00\" mask=\"url(#m)\"/></svg>";

static const char *svg_clip =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
    "<defs><clipPath id=\"c\"><rect width=\"32\" height=\"64\"/></clipPath></defs>"
    "<rect width=\"64\" height=\"64\" fill=\"#ff0000\" clip-path=\"url(#c)\"/></svg>";

static ecs_world_t *w;
static box_packet_t packet;
static int32_t svg_errors;
static ecs_os_api_log_t svg_log_prev;

/** Log callback that counts errors instead of printing them.
 *
 * @param level Log level, -3 for an error
 * @param file Source file of the message
 * @param line Source line of the message
 * @param msg The message
 */
static void svg_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    (void)msg;
    svg_errors += level == -3;
}

void Svg_setup(void)
{
    svg_errors = 0;
    w = ecs_init();
    svg_log_prev = ecs_os_api.log_;
    ecs_os_api.log_ = svg_log;
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxTexture);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Svg_teardown(void)
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
    ecs_os_api.log_ = svg_log_prev;
}

void Svg_inline_data_rasterizes(void)
{
    // textures/svg
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(svg_circle), .width = 64, .height = 64 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 256);
    test_int(cmd->height, 256);
    test_int(cmd->mips, 9);
    test_int(cmd->srgb, 1);
    const RenderTexture *rt = ecs_get(w, t, RenderTexture);
    test_assert(rt != NULL);
    test_int(rt->handle, cmd->handle);
    test_int(rt->width, 256);
    test_int(rt->height, 256);
    uint8_t *center = cmd->pixels + ((size_t)128 * 256 + 128) * 4;
    test_int(center[0], 255);
    test_int(center[1], 0);
    test_int(center[2], 0);
    test_int(center[3], 255);
    test_int(cmd->pixels[3], 0);
    test_int(svg_errors, 0);
}

void Svg_file_rasterizes(void)
{
    // textures/svg
    const char *path = "/tmp/box_test_svg_file.svg";
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs(svg_circle, f);
    fclose(f);

    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .file = ecs_os_strdup(path), .width = 256, .height = 256 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 256);
    test_int(cmd->height, 256);
    uint8_t *center = cmd->pixels + ((size_t)128 * 256 + 128) * 4;
    test_int(center[0], 255);
    test_int(center[3], 255);
    test_int(cmd->pixels[3], 0);
    test_int(svg_errors, 0);
}

void Svg_zero_height_follows_the_width(void)
{
    // textures/svg
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(svg_circle), .width = 1024 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 1024);
    test_int(cmd->height, 1024);
    const TextureImpl *impl = ecs_get(w, t, TextureImpl);
    test_assert(impl != NULL);
    test_int(impl->source_width, 1024);
    test_int(impl->source_height, 1024);
    test_int(svg_errors, 0);
}

void Svg_resize_rerasterizes(void)
{
    // textures/svg
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(svg_circle), .width = 64, .height = 64 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 256);
    uint32_t handle = cmd->handle;
    free(cmd->pixels);
    box_packet_reset(&packet);

    ecs_set(w, t, Svg, { .data = ecs_os_strdup(svg_circle), .width = 1024, .height = 1024 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 1024);
    test_int(cmd->height, 1024);
    test_int(cmd->mips, 11);
    test_assert(cmd->handle != handle);
    test_int(packet.texture_destroy.count, 1);
    test_int(*(uint32_t*)box_vec_at(&packet.texture_destroy, 0), handle);
    const RenderTexture *rt = ecs_get(w, t, RenderTexture);
    test_int(rt->handle, cmd->handle);
    test_int(rt->width, 1024);
    uint8_t *center = cmd->pixels + ((size_t)512 * 1024 + 512) * 4;
    test_int(center[0], 255);
    test_int(center[3], 255);
}

void Svg_removing_the_component_destroys_the_handle(void)
{
    // textures/svg
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(svg_circle), .width = 64, .height = 64 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    uint32_t handle = cmd->handle;
    free(cmd->pixels);
    box_packet_reset(&packet);

    ecs_remove(w, t, Svg);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 0);
    test_int(packet.texture_destroy.count, 1);
    test_int(*(uint32_t*)box_vec_at(&packet.texture_destroy, 0), handle);
    test_assert(!ecs_has(w, t, RenderTexture));
    test_assert(!ecs_has(w, t, TextureImpl));
}

void Svg_handle_reaches_a_pbr_slot(void)
{
    // textures/svg
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(svg_circle), .width = 256, .height = 256 });
    box_job_flush(w);
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Quad, {1, 1});
    ecs_set(w, e, PbrTextures, { .albedo = t });
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    const RenderTexture *rt = ecs_get(w, t, RenderTexture);
    test_assert(rt->handle != 0);
    test_int(box_instance_texture(&u->data, 0), rt->handle);
    test_int(box_instance_texture(&u->data, 1), 0);
    test_int(svg_errors, 0);
}

void Svg_texture_and_svg_is_an_error(void)
{
    // textures/svg
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(svg_circle), .width = 64, .height = 64 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    uint32_t handle = cmd->handle;
    free(cmd->pixels);
    box_packet_reset(&packet);
    test_int(svg_errors, 0);

    ecs_set(w, t, Texture, { .path = ecs_os_strdup("/tmp/box_test_svg_conflict.png") });
    test_int(svg_errors, 1);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 0);
    test_int(packet.texture_destroy.count, 0);
    test_int(ecs_get(w, t, RenderTexture)->handle, handle);

    ecs_set(w, t, Texture, { .path = ecs_os_strdup("/tmp/box_test_svg_conflict.png") });
    test_int(svg_errors, 1);
    test_int(ecs_get(w, t, RenderTexture)->handle, handle);
}

void Svg_row_zero_is_the_bottom_of_the_image(void)
{
    // textures/uv_orientation
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(svg_halves), .width = 2, .height = 2 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 256);
    test_int(cmd->pixels[0], 0);
    test_int(cmd->pixels[1], 0);
    test_int(cmd->pixels[2], 255);
    const uint8_t *top = cmd->pixels + ((size_t)255 * 256) * 4;
    test_int(top[0], 255);
    test_int(top[1], 0);
    test_int(top[2], 0);
    test_int(svg_errors, 0);
}

void Svg_text_rasterizes(void)
{
    // textures/svg_resvg
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(svg_text), .width = 256, .height = 256 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    const uint8_t *stem = cmd->pixels + ((size_t)127 * 256 + 128) * 4;
    test_int(stem[0], 255);
    test_int(stem[1], 255);
    test_int(stem[2], 255);
    test_int(stem[3], 255);
    const uint8_t *corner = cmd->pixels + ((size_t)8 * 256 + 8) * 4;
    test_int(corner[0], 0);
    test_int(corner[3], 255);

    int32_t bright = 0;
    int32_t i;
    for (i = 0; i < 256 * 256; i ++) {
        bright += cmd->pixels[(size_t)i * 4] > 200;
    }

    test_assert(bright > 2000);
    test_assert(bright < 20000);
    test_int(svg_errors, 0);
}

void Svg_mask_hides_half(void)
{
    // textures/svg_resvg
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(svg_mask), .width = 256, .height = 256 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    const uint8_t *shown = cmd->pixels + ((size_t)200 * 256 + 128) * 4;
    test_int(shown[0], 0);
    test_int(shown[1], 255);
    test_int(shown[2], 0);
    test_int(shown[3], 255);
    const uint8_t *hidden = cmd->pixels + ((size_t)40 * 256 + 128) * 4;
    test_int(hidden[3], 0);
    test_int(svg_errors, 0);
}

void Svg_clip_path_keeps_the_clipped_half(void)
{
    // textures/svg_resvg
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Svg, { .data = ecs_os_strdup(svg_clip), .width = 256, .height = 256 });
    box_job_flush(w);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    const uint8_t *kept = cmd->pixels + ((size_t)128 * 256 + 64) * 4;
    test_int(kept[0], 255);
    test_int(kept[1], 0);
    test_int(kept[2], 0);
    test_int(kept[3], 255);
    const uint8_t *clipped = cmd->pixels + ((size_t)128 * 256 + 192) * 4;
    test_int(clipped[3], 0);
    test_int(svg_errors, 0);
}
