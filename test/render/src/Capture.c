/**
 * @file Capture.c
 * @brief Tests for the Capture component consumed by the extract system.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Capture_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 64;
    packet.height = 32;
}

void Capture_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Capture_component_consumed_by_extract(void)
{
    // command/capture
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_set(w, cam, Capture, {.path = ecs_os_strdup("/tmp/box_test_capture.png")});
    box_render_extract(w, &packet);
    test_str(packet.capture_path, "/tmp/box_test_capture.png");
    test_assert(!ecs_has(w, cam, Capture));
}

void Capture_explicit_view_uses_capture_camera(void)
{
    // command/capture
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, -5});
    ecs_set(w, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {0, 0, 9});
    ecs_set(w, b, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = b, .enabled = true});
    ecs_set(w, b, Capture, {.path = ecs_os_strdup("/tmp/box_test_capture_b.png")});
    box_render_extract(w, &packet);
    test_bool(packet.has_view, true);
    test_flt(packet.view.eye[2], 9);
    test_str(packet.capture_path, "/tmp/box_test_capture_b.png");
}

void Capture_two_cameras_over_two_frames(void)
{
    // command/capture
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {-3, 0, 0});
    ecs_set(w, a, Camera, {.fov = 60});
    ecs_entity_t view_a = ecs_new(w);
    ecs_set(w, view_a, View, {.camera = a, .enabled = true});
    ecs_set(w, a, Capture, {.path = ecs_os_strdup("/tmp/box_test_capture_a.png")});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {7, 0, 0});
    ecs_set(w, b, Camera, {.fov = 60});
    ecs_entity_t view_b = ecs_new(w);
    ecs_set(w, view_b, View, {.camera = b, .enabled = true});
    ecs_set(w, b, Capture, {.path = ecs_os_strdup("/tmp/box_test_capture_b.png")});
    box_render_extract(w, &packet);
    test_str(packet.capture_path, "/tmp/box_test_capture_a.png");
    test_flt(packet.view.eye[0], -3);
    test_uint(packet.capture_view, 0);
    test_assert(!ecs_has(w, a, Capture));
    test_assert(ecs_has(w, b, Capture));

    box_packet_reset(&packet);
    packet.width = 64;
    packet.height = 32;
    box_render_extract(w, &packet);
    test_str(packet.capture_path, "/tmp/box_test_capture_b.png");
    test_flt(packet.view.eye[0], -3);
    test_uint(packet.capture_view, 1);
    test_assert(!ecs_has(w, b, Capture));
}

void Capture_overrides_write_to_file(void)
{
    // command/capture
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_set(w, cam, Capture, {.path = ecs_os_strdup("/tmp/box_test_capture_cam.png")});
    ecs_os_snprintf(packet.capture_path, BOX_CAPTURE_PATH_MAX, "%s",
        "/tmp/box_test_capture_surface.png");
    box_render_extract(w, &packet);
    test_str(packet.capture_path, "/tmp/box_test_capture_cam.png");
}

void Capture_write_to_file_kept_without_component(void)
{
    // command/capture
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_os_snprintf(packet.capture_path, BOX_CAPTURE_PATH_MAX, "%s",
        "/tmp/box_test_capture_surface.png");
    box_render_extract(w, &packet);
    test_str(packet.capture_path, "/tmp/box_test_capture_surface.png");
}

void Capture_active_camera_is_the_first_camera(void)
{
    // command/capture
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, -5});
    ecs_set(w, a, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = a, .enabled = true});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {0, 0, -9});
    ecs_set(w, b, Camera, {.fov = 60});
    test_assert(box_render_active_camera(w) == a);
    box_render_extract(w, &packet);
    test_flt(packet.view.eye[2], -5);
    test_assert(packet.capture_path[0] == '\0');
}

void Capture_active_camera_without_camera_entity(void)
{
    // command/capture
    test_assert(box_render_active_camera(w) == 0);
    box_render_extract(w, &packet);
    test_bool(packet.has_view, false);
}

void Capture_removed_after_a_deferred_extract(void)
{
    // command/capture
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_set(w, cam, Capture, {.path = ecs_os_strdup("/tmp/box_test_capture_deferred.png")});
    ecs_readonly_begin(w, false);
    box_render_extract(ecs_get_stage(w, 0), &packet);
    test_assert(ecs_has(w, cam, Capture));
    ecs_readonly_end(w);
    test_assert(!ecs_has(w, cam, Capture));
    test_str(packet.capture_path, "/tmp/box_test_capture_deferred.png");
}
