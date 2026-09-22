/**
 * @file Shapes2D.c
 * @brief Tests for the 2D shape words extract packs into an instance.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Shapes2D_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Shapes2D_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);

        free(cmd->vertices);
        free(cmd->indices);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Shapes2D_a_plain_instance_has_no_shape(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Rect, {4, 4});
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_bool(box_instance_shape(&u->data), false);
    test_uint(u->data.shape[0], 0);
    test_uint(u->data.shape[1], 0);
    test_uint(u->data.shape[2], 0);
    test_uint(u->data.shape[3], 0);
}

void Shapes2D_a_border_sets_the_shape_bit(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Rect, {4, 4});
    ecs_set(w, e, Border, {.radius = 1, .width = 0.5f, .color = {10, 20, 30, 255}});
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_bool(box_instance_shape(&u->data), true);
    test_int(box_instance_layers(&u->data), 0);
}

void Shapes2D_radii_are_ratios_of_the_short_side(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Rect, {8, 4});
    ecs_set(w, e, Border, {.radius = 1, .radius_top_left = 2});
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_uint(u->data.shape[0] & 0xFF, 255);
    test_uint((u->data.shape[0] >> 8) & 0xFF, 128);
    test_uint((u->data.shape[0] >> 16) & 0xFF, 128);
    test_uint((u->data.shape[0] >> 24) & 0xFF, 128);
}

void Shapes2D_widths_are_ratios_of_the_short_side(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Rect, {8, 4});
    ecs_set(w, e, Border, {.width = 0.5f, .left_width = 1});
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_uint(u->data.shape[1] & 0xFF, 64);
    test_uint((u->data.shape[1] >> 24) & 0xFF, 128);
}

void Shapes2D_the_half_extents_carry_the_aspect(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Rect, {8, 4});
    ecs_set(w, e, Border, {.radius = 1});
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_uint(u->data.shape[3] & 0xFFFF, 65535);
    test_uint(u->data.shape[3] >> 16, 32768);
}

void Shapes2D_a_scale_moves_the_ratios(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Scale, {2, 2, 2});
    ecs_set(w, e, Rect, {4, 4});
    ecs_set(w, e, Border, {.radius = 2});
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_uint(u->data.shape[0] & 0xFF, 128);
}

void Shapes2D_the_border_color_reaches_the_instance(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Rect, {4, 4});
    ecs_set(w, e, Border, {.width = 0.5f, .color = {10, 20, 30, 200}});
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_uint(u->data.shape[2] & 0xFF, 10);
    test_uint((u->data.shape[2] >> 8) & 0xFF, 20);
    test_uint((u->data.shape[2] >> 16) & 0xFF, 30);
    test_uint((u->data.shape[2] >> 24) & 0xFF, 200);
}

void Shapes2D_a_colorless_border_takes_the_rgb(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Rect, {4, 4});
    ecs_set(w, e, Rgb, {40, 50, 60});
    ecs_set(w, e, Border, {.width = 0.5f});
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_uint(u->data.shape[2] & 0xFF, 40);
    test_uint((u->data.shape[2] >> 8) & 0xFF, 50);
    test_uint((u->data.shape[2] >> 16) & 0xFF, 60);
    test_uint((u->data.shape[2] >> 24) & 0xFF, 255);
}

void Shapes2D_a_radius_change_re_extracts_the_instance(void)
{
    // shapes_2d/primitives
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Rect, {4, 4});
    ecs_set(w, e, Border, {.radius = 1});
    box_render_extract(w, &packet);
    test_uint(((box_static_update_t*)box_vec_at(&packet.static_updates, 0))->data.shape[0]
        & 0xFF, 128);

    uint32_t seen = packet.static_updates.count;
    ecs_set(w, e, Border, {.radius = 2});
    box_render_extract(w, &packet);
    test_assert(packet.static_updates.count > seen);

    box_static_update_t *u = box_vec_at(&packet.static_updates, packet.static_updates.count - 1);
    test_uint(u->data.shape[0] & 0xFF, 255);
}

void Shapes2D_a_border_survives_the_sorted_pass(void)
{
    // shapes_2d/primitives
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Rect, {4, 4});
    ecs_set(w, e, Border, {.radius = 1, .width = 0.5f});
    ecs_set(w, e, BlendMode, {AlphaBlend});
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 1);

    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_bool(box_instance_shape(&entry->data), true);
    test_uint(entry->data.shape[0] & 0xFF, 128);
}
