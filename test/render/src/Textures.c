/**
 * @file Textures.c
 * @brief Tests for texture loading, the resource commands and the PBR texture slots.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/texture/texture_internal.h"

int box_image_write_png(
    const char *path,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    const void *pixels);

static ecs_world_t *w;
static box_packet_t packet;
static int32_t texture_warnings;
static int32_t texture_errors;
static ecs_os_api_log_t texture_log_prev;

/** Log callback that counts warnings and errors instead of printing them.
 *
 * @param level Log level, -2 for a warning and -3 for an error
 * @param file Source file of the message
 * @param line Source line of the message
 * @param msg The message
 */
static void texture_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    (void)msg;
    texture_warnings += level == -2;
    texture_errors += level == -3;
}

void Textures_setup(void)
{
    texture_warnings = 0;
    texture_errors = 0;
    w = ecs_init();
    texture_log_prev = ecs_os_api.log_;
    ecs_os_api.log_ = texture_log;
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Textures_teardown(void)
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
    ecs_os_api.log_ = texture_log_prev;
}

void Textures_png_loads_into_a_create_command(void)
{
    // textures/textures
    const char *path = "/tmp/box_test_tex_load.png";
    static uint8_t pixels[64 * 64 * 4];
    int32_t i;
    for (i = 0; i < 64 * 64; i ++) {
        pixels[i * 4 + 0] = 200;
        pixels[i * 4 + 1] = 100;
        pixels[i * 4 + 2] = 50;
        pixels[i * 4 + 3] = 255;
    }

    test_int(box_image_write_png(path, 64, 64, 64 * 4, pixels), 0);
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path) });
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 256);
    test_int(cmd->height, 256);
    test_int(cmd->mips, 9);
    test_int(cmd->srgb, 1);
    test_int(box_texture_class(cmd->handle), 0);
    test_int(box_texture_layer(cmd->handle), 0);
    const RenderTexture *rt = ecs_get(w, t, RenderTexture);
    test_assert(rt != NULL);
    test_int(rt->handle, cmd->handle);
    test_int(rt->width, 256);
    test_int(rt->height, 256);
    test_int(texture_errors, 0);
}

void Textures_requested_size_selects_the_class(void)
{
    // textures/textures
    const char *path = "/tmp/box_test_tex_size.png";
    static uint8_t pixels[64 * 64 * 4];
    memset(pixels, 180, sizeof(pixels));
    test_int(box_image_write_png(path, 64, 64, 64 * 4, pixels), 0);
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path), .width = 1024, .height = 1024 });
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 1024);
    test_int(box_texture_class(cmd->handle), 2);
    test_int(cmd->mips, 11);
}

void Textures_source_size_picks_the_nearest_class(void)
{
    // textures/textures
    const char *path = "/tmp/box_test_tex_source.png";
    static uint8_t pixels[700 * 8 * 4];
    memset(pixels, 90, sizeof(pixels));
    test_int(box_image_write_png(path, 700, 8, 700 * 4, pixels), 0);
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path) });
    box_render_extract(w, &packet);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 512);
    test_int(box_texture_class(cmd->handle), 1);
}

void Textures_quality_caps_the_class(void)
{
    // textures/textures
    const char *path = "/tmp/box_test_tex_quality.png";
    static uint8_t pixels[64 * 64 * 4];
    memset(pixels, 120, sizeof(pixels));
    test_int(box_image_write_png(path, 64, 64, 64 * 4, pixels), 0);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->texture_quality = TextureQualityLow;
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path), .width = 2048, .height = 2048 });
    box_render_extract(w, &packet);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->width, 512);
    test_int(box_texture_class(cmd->handle), 1);
}

void Textures_new_path_updates_the_same_handle(void)
{
    // textures/textures
    const char *first = "/tmp/box_test_tex_first.png";
    const char *second = "/tmp/box_test_tex_second.png";
    static uint8_t pixels[64 * 64 * 4];
    memset(pixels, 60, sizeof(pixels));
    test_int(box_image_write_png(first, 64, 64, 64 * 4, pixels), 0);
    memset(pixels, 220, sizeof(pixels));
    test_int(box_image_write_png(second, 64, 64, 64 * 4, pixels), 0);

    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(first) });
    box_render_extract(w, &packet);
    uint32_t handle = ((box_texture_cmd_t*)box_vec_at(&packet.texture_create, 0))->handle;
    free(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, 0))->pixels);
    box_packet_reset(&packet);

    ecs_set(w, t, Texture, { .path = ecs_os_strdup(second) });
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 1);
    test_int(packet.texture_destroy.count, 0);
    box_texture_cmd_t *cmd = box_vec_at(&packet.texture_create, 0);
    test_int(cmd->handle, handle);
    test_int(cmd->pixels[0], 220);
    test_int(ecs_get(w, t, RenderTexture)->handle, handle);
}

void Textures_removing_the_component_destroys_the_handle(void)
{
    // textures/textures
    const char *path = "/tmp/box_test_tex_remove.png";
    static uint8_t pixels[64 * 64 * 4];
    memset(pixels, 30, sizeof(pixels));
    test_int(box_image_write_png(path, 64, 64, 64 * 4, pixels), 0);
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path) });
    box_render_extract(w, &packet);
    uint32_t handle = ((box_texture_cmd_t*)box_vec_at(&packet.texture_create, 0))->handle;
    free(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, 0))->pixels);
    box_packet_reset(&packet);

    ecs_remove(w, t, Texture);
    box_render_extract(w, &packet);
    test_int(packet.texture_destroy.count, 1);
    test_int(*(uint32_t*)box_vec_at(&packet.texture_destroy, 0), handle);
    test_assert(!ecs_has(w, t, RenderTexture));
}

void Textures_albedo_slot_reaches_the_instance(void)
{
    // textures/textures
    const char *path = "/tmp/box_test_tex_albedo.png";
    static uint8_t pixels[64 * 64 * 4];
    memset(pixels, 200, sizeof(pixels));
    test_int(box_image_write_png(path, 64, 64, 64 * 4, pixels), 0);
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path) });
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, PbrTextures, { .albedo = t });
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_instance_texture(&u->data, 0), ecs_get(w, t, RenderTexture)->handle);
    test_int(box_instance_texture(&u->data, 1), 0);
    test_int(box_instance_texture(&u->data, 2), 0);
    test_int(box_instance_texture(&u->data, 3), 0);
}

void Textures_every_slot_reaches_the_instance(void)
{
    // textures/textures
    const char *path = "/tmp/box_test_tex_slots.png";
    static uint8_t pixels[64 * 64 * 4];
    memset(pixels, 140, sizeof(pixels));
    test_int(box_image_write_png(path, 64, 64, 64 * 4, pixels), 0);
    ecs_entity_t slots[4];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        slots[i] = ecs_new(w);
        ecs_set(w, slots[i], Texture, { .path = ecs_os_strdup(path) });
    }

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, PbrTextures, { .albedo = slots[0], .emissive = slots[1],
        .roughness = slots[2], .normal = slots[3] });
    box_render_extract(w, &packet);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    for (i = 0; i < 4; i ++) {
        uint32_t handle = box_instance_texture(&u->data, (uint32_t)i);
        test_int(handle, ecs_get(w, slots[i], RenderTexture)->handle);
        test_int(box_texture_layer(handle), (uint32_t)i);
    }
}

void Textures_prefab_slots_are_inherited(void)
{
    // textures/textures
    const char *path = "/tmp/box_test_tex_prefab.png";
    static uint8_t pixels[64 * 64 * 4];
    memset(pixels, 170, sizeof(pixels));
    test_int(box_image_write_png(path, 64, 64, 64 * 4, pixels), 0);
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path) });
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    ecs_set(w, prefab, PbrTextures, { .albedo = t });
    ecs_entity_t e = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_instance_texture(&u->data, 0), ecs_get(w, t, RenderTexture)->handle);
}

void Textures_reload_reextracts_dependents(void)
{
    // textures/textures
    const char *path = "/tmp/box_test_tex_reload.png";
    static uint8_t pixels[64 * 64 * 4];
    memset(pixels, 40, sizeof(pixels));
    test_int(box_image_write_png(path, 64, 64, 64 * 4, pixels), 0);
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path) });
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    ecs_set(w, prefab, PbrTextures, { .albedo = t });
    ecs_entity_t e = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    free(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, 0))->pixels);
    free(((box_mesh_cmd_t*)box_vec_at(&packet.mesh_create, 0))->vertices);
    free(((box_mesh_cmd_t*)box_vec_at(&packet.mesh_create, 0))->indices);
    box_packet_reset(&packet);

    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    box_packet_reset(&packet);

    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path) });
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_instance_texture(&u->data, 0), ecs_get(w, t, RenderTexture)->handle);
}

void Textures_deleting_a_texture_unbinds_and_warns(void)
{
    // textures/textures
    const char *path = "/tmp/box_test_tex_delete.png";
    static uint8_t pixels[64 * 64 * 4];
    memset(pixels, 210, sizeof(pixels));
    test_int(box_image_write_png(path, 64, 64, 64 * 4, pixels), 0);
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path) });
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, PbrTextures, { .albedo = t });
    box_render_extract(w, &packet);
    free(((box_texture_cmd_t*)box_vec_at(&packet.texture_create, 0))->pixels);
    free(((box_mesh_cmd_t*)box_vec_at(&packet.mesh_create, 0))->vertices);
    free(((box_mesh_cmd_t*)box_vec_at(&packet.mesh_create, 0))->indices);
    box_packet_reset(&packet);

    ecs_delete(w, t);
    test_int(texture_warnings, 1);
    test_int(ecs_get(w, e, PbrTextures)->albedo, 0);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_instance_texture(&u->data, 0), 0);
    test_int(packet.texture_destroy.count, 1);
}

void Textures_missing_file_leaves_no_handle(void)
{
    // textures/textures
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup("/tmp/box_test_tex_absent.png") });
    test_int(texture_errors, 1);
    test_int(ecs_get(w, t, RenderTexture)->handle, 0);
    box_render_extract(w, &packet);
    test_int(packet.texture_create.count, 0);
}

void Textures_kept_source_pixels_survive_the_upload(void)
{
    // textures/textures
    const char *path = "/tmp/box_test_tex_keep.png";
    static uint8_t pixels[64 * 64 * 4];
    memset(pixels, 77, sizeof(pixels));
    test_int(box_image_write_png(path, 64, 64, 64 * 4, pixels), 0);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->keep_source_textures = true;
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path) });
    const TextureImpl *impl = ecs_get(w, t, TextureImpl);
    test_assert(impl != NULL);
    test_assert(impl->pixels != NULL);
    test_int(impl->pixels[0], 77);
    test_int(impl->source_width, 64);
    test_int(impl->source_height, 64);
}

void Textures_row_zero_is_the_bottom_of_the_file(void)
{
    // textures/uv_orientation
    const char *path = "/tmp/box_test_tex_orientation.png";
    static uint8_t pixels[2 * 2 * 4];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 0;
        pixels[i * 4 + 2] = 0;
        pixels[i * 4 + 3] = 255;
        pixels[(2 + i) * 4 + 0] = 0;
        pixels[(2 + i) * 4 + 1] = 0;
        pixels[(2 + i) * 4 + 2] = 255;
        pixels[(2 + i) * 4 + 3] = 255;
    }

    test_int(box_image_write_png(path, 2, 2, 2 * 4, pixels), 0);
    ecs_entity_t t = ecs_new(w);
    ecs_set(w, t, Texture, { .path = ecs_os_strdup(path) });
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
    test_int(texture_errors, 0);
}

void Textures_pager_spreads_a_class_over_pages(void)
{
    // textures/layer_cap
    static uint32_t handles[600];
    int32_t i;
    for (i = 0; i < 600; i ++) {
        handles[i] = box_render_texture_create(w, 0, 256, NULL, true);
        test_assert(handles[i] != 0);
        test_int(box_texture_class(handles[i]), 0);
        test_int(box_texture_layer(handles[i]), (uint32_t)i);
        test_int(box_texture_page(handles[i]), (uint32_t)i / BOX_TEXTURE_PAGE_LAYERS);
        test_int(box_texture_page_layer(handles[i]), (uint32_t)i % BOX_TEXTURE_PAGE_LAYERS);
    }

    const RenderState *state = ecs_singleton_get(w, RenderState);
    test_assert(state != NULL);
    int32_t used = 0;
    for (i = 0; i < BOX_TEXTURE_PAGES; i ++) {
        used += state->pages[i].used;
    }

    test_int(used, 3);
    test_int(state->pages[0].count, BOX_TEXTURE_PAGE_LAYERS);
    test_int(state->pages[1].count, BOX_TEXTURE_PAGE_LAYERS);
    test_int(state->pages[2].count, 600 - 2 * BOX_TEXTURE_PAGE_LAYERS);

    box_instance_t inst;
    memset(&inst, 0, sizeof(box_instance_t));
    box_instance_set_texture(&inst, 0, handles[599]);
    test_int(box_instance_texture(&inst, 0), handles[599]);
    test_int(texture_errors, 0);
}

void Textures_pager_reuses_a_released_layer(void)
{
    // textures/layer_cap
    static uint32_t handles[600];
    int32_t i;
    for (i = 0; i < 600; i ++) {
        handles[i] = box_render_texture_create(w, 0, 256, NULL, true);
    }

    box_render_texture_destroy(w, handles[5]);
    box_render_texture_destroy(w, handles[300]);
    test_int(box_render_texture_create(w, 0, 256, NULL, true), handles[5]);
    test_int(box_render_texture_create(w, 0, 256, NULL, true), handles[300]);
    test_int(box_render_texture_create(w, 0, 256, NULL, true),
        box_texture_handle(0, 600));

    const RenderState *state = ecs_singleton_get(w, RenderState);
    int32_t used = 0;
    for (i = 0; i < BOX_TEXTURE_PAGES; i ++) {
        used += state->pages[i].used;
    }

    test_int(used, 3);
    test_int(texture_errors, 0);
}

void Textures_pager_out_of_pages_leaves_no_handle(void)
{
    // textures/layer_cap
    int32_t i;
    for (i = 0; i < BOX_TEXTURE_PAGES * BOX_TEXTURE_PAGE_LAYERS; i ++) {
        test_assert(box_render_texture_create(w, 0, 256, NULL, true) != 0);
    }

    test_int(box_render_texture_create(w, 0, 256, NULL, true), 0);
    test_int(box_render_texture_create(w, 0, 512, NULL, true), 0);
    test_int(texture_errors, 1);
}
