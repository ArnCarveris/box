/**
 * @file Target.c
 * @brief Tests for the render targets a view draws into and their packet commands.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Target_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Target_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Target_surface_view_has_no_target(void)
{
    // views/view
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    ecs_get_mut(w, e, View)->camera = cam;
    ecs_modified(w, e, View);
    box_render_extract(w, &packet);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->target, 0);
    test_int(v->width, 640);
    test_int(v->height, 480);
    test_int(packet.target_create.count, 0);
}

void Target_target_reaches_the_packet_as_a_create_command(void)
{
    // views/view
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, RenderTarget, {320, 240});
    box_render_extract(w, &packet);
    test_int(packet.target_create.count, 1);
    box_target_cmd_t *cmd = box_vec_at(&packet.target_create, 0);
    test_int(cmd->width, 320);
    test_int(cmd->height, 240);
    test_assert(cmd->handle != 0);
    test_uint(ecs_get(w, target, RenderTargetImpl)->handle, cmd->handle);
}

void Target_view_names_the_target_handle_and_size(void)
{
    // views/view
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, RenderTarget, {256, 128});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    View *v = ecs_get_mut(w, e, View);
    v->camera = cam;
    v->target = target;
    ecs_modified(w, e, View);
    box_render_extract(w, &packet);
    box_view_t *out = box_vec_at(&packet.views, 0);
    box_target_cmd_t *cmd = box_vec_at(&packet.target_create, 0);
    test_int(out->target, cmd->handle);
    test_int(out->width, 256);
    test_int(out->height, 128);
    test_assert(out->proj[0] > out->proj[5] * 0.4f);
}

void Target_resize_resends_the_create_command(void)
{
    // views/view
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, RenderTarget, {256, 256});
    box_render_extract(w, &packet);
    uint32_t handle = ((box_target_cmd_t*)box_vec_at(&packet.target_create, 0))->handle;

    ecs_set(w, target, RenderTarget, {512, 512});
    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(w, &packet);
    test_int(packet.target_create.count, 1);
    box_target_cmd_t *cmd = box_vec_at(&packet.target_create, 0);
    test_int(cmd->handle, handle);
    test_int(cmd->width, 512);
    test_int(cmd->height, 512);
}

void Target_unchanged_target_is_sent_once(void)
{
    // views/view
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, RenderTarget, {256, 256});
    box_render_extract(w, &packet);
    test_int(packet.target_create.count, 1);

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(w, &packet);
    test_int(packet.target_create.count, 0);
}

void Target_deleted_target_is_destroyed(void)
{
    // views/view
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, RenderTarget, {256, 256});
    box_render_extract(w, &packet);
    uint32_t handle = ((box_target_cmd_t*)box_vec_at(&packet.target_create, 0))->handle;

    ecs_delete(w, target);
    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(w, &packet);
    test_int(packet.target_destroy.count, 1);
    test_uint(*(uint32_t*)box_vec_at(&packet.target_destroy, 0), handle);
    test_int(packet.target_create.count, 0);
}

void Target_two_targets_get_two_handles(void)
{
    // views/view
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, RenderTarget, {256, 256});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, RenderTarget, {512, 256});
    box_render_extract(w, &packet);
    test_int(packet.target_create.count, 2);
    uint32_t first = ecs_get(w, a, RenderTargetImpl)->handle;
    uint32_t second = ecs_get(w, b, RenderTargetImpl)->handle;
    test_assert(first != 0);
    test_assert(second != 0);
    test_assert(first != second);
}
