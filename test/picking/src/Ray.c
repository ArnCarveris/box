/**
 * @file Ray.c
 * @brief Tests for the world ray a view builds from a normalized screen position.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

static ecs_world_t *w;

void Ray_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxPicking);
}

void Ray_teardown(void)
{
    ecs_fini(w);
}

void Ray_center_of_the_default_camera_looks_forward(void)
{
    // picking/picking
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_progress(w, 0);

    vec3 origin, dir;
    test_bool(box_picking_ray(w, 0, (vec2){0, 0}, &origin, &dir), true);
    test_flt(origin.x, 0);
    test_flt(origin.y, 0);
    test_flt(origin.z, -10);
    test_flt(dir.x, 0);
    test_flt(dir.y, 0);
    test_flt(dir.z, 1);
}

void Ray_without_a_camera_there_is_no_ray(void)
{
    // picking/picking
    ecs_progress(w, 0);

    vec3 origin, dir;
    test_bool(box_picking_ray(w, 0, (vec2){0, 0}, &origin, &dir), false);
}

void Ray_follows_the_cursor_to_the_right(void)
{
    // picking/picking
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_progress(w, 0);

    vec3 origin, dir;
    test_bool(box_picking_ray(w, 0, (vec2){1, 0}, &origin, &dir), true);
    test_assert(dir.x > 0);
    test_flt(dir.y, 0);
    test_assert(dir.z > 0);
    test_flt(dir.x / dir.z, tanf(30.0f * 3.14159265f / 180.0f));
}

void Ray_follows_the_cursor_up(void)
{
    // picking/picking
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_progress(w, 0);

    vec3 origin, dir;
    test_bool(box_picking_ray(w, 0, (vec2){0, 1}, &origin, &dir), true);
    test_flt(dir.x, 0);
    test_assert(dir.y > 0);
    test_flt(dir.y / dir.z, tanf(30.0f * 3.14159265f / 180.0f));
}

void Ray_widens_with_the_aspect_of_the_surface(void)
{
    // picking/picking
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 800;
    s->height = 400;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_progress(w, 0);

    vec3 origin, dir;
    test_bool(box_picking_ray(w, 0, (vec2){1, 0}, &origin, &dir), true);
    test_flt(dir.x / dir.z, 2.0f * tanf(30.0f * 3.14159265f / 180.0f));
}

void Ray_camera_aspect_ratio_overrides_the_surface(void)
{
    // picking/picking
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 800;
    s->height = 400;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60, .aspect_ratio = 1});
    ecs_progress(w, 0);

    vec3 origin, dir;
    test_bool(box_picking_ray(w, 0, (vec2){1, 0}, &origin, &dir), true);
    test_flt(dir.x / dir.z, tanf(30.0f * 3.14159265f / 180.0f));
}

void Ray_turns_with_the_camera(void)
{
    // picking/picking
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, 0});
    ecs_set(w, cam, Rotation, {0, 3.14159265f / 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_progress(w, 0);

    vec3 origin, dir;
    test_bool(box_picking_ray(w, 0, (vec2){0, 0}, &origin, &dir), true);
    test_flt(dir.x, 1);
    test_flt(dir.y, 0);
    test_flt(dir.z, 0);
}

void Ray_of_an_orthographic_camera_is_parallel(void)
{
    // picking/picking
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.orthographic = true, .ortho_height = 10});
    ecs_progress(w, 0);

    vec3 origin, dir;
    test_bool(box_picking_ray(w, 0, (vec2){1, 0}, &origin, &dir), true);
    test_flt(origin.x, 5);
    test_flt(origin.y, 0);
    test_flt(origin.z, -10);
    test_flt(dir.x, 0);
    test_flt(dir.z, 1);
}

void Ray_a_view_builds_its_ray_from_its_own_camera(void)
{
    // picking/picking
    ecs_entity_t first = ecs_new(w);
    ecs_set(w, first, Position, {0, 0, -10});
    ecs_set(w, first, Camera, {.fov = 60});
    ecs_entity_t second = ecs_new(w);
    ecs_set(w, second, Position, {20, 0, 0});
    ecs_set(w, second, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = second});
    ecs_progress(w, 0);

    vec3 origin, dir;
    test_bool(box_picking_ray(w, view, (vec2){0, 0}, &origin, &dir), true);
    test_flt(origin.x, 20);
    test_flt(origin.z, 0);
}

void Ray_a_view_without_a_camera_has_no_ray(void)
{
    // picking/picking
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = 0});
    ecs_progress(w, 0);

    vec3 origin, dir;
    test_bool(box_picking_ray(w, view, (vec2){0, 0}, &origin, &dir), false);
}

void Ray_aspect_of_a_render_target_comes_from_the_target(void)
{
    // picking/picking
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 100;
    s->height = 100;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t target = ecs_new(w);
    ecs_set(w, target, RenderTarget, {.width = 800, .height = 400});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .target = target});
    ecs_progress(w, 0);

    vec3 origin, dir;
    test_bool(box_picking_ray(w, view, (vec2){1, 0}, &origin, &dir), true);
    test_flt(dir.x / dir.z, 2.0f * tanf(30.0f * 3.14159265f / 180.0f));
}
