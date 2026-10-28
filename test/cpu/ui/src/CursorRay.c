/**
 * @file CursorRay.c
 * @brief box_view_cursor_ray: the world ray through the cursor in a view, whatever it picks.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** Distance within which a component of a ray matches */
#define RAY_EPS (2e-3f)

/** Tangent of half the default vertical field of view of 60 degrees */
#define RAY_TAN (0.57735027f)

/** Aspect ratio of the default surface of 1280 x 720 points */
#define RAY_ASPECT (1280.0f / 720.0f)

/** Modules the scenes use */
#define RAY_PRELUDE \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.picking\n" \
    "using box.input\n"

/** A camera at 5, 10, 10 pitched down 45 degrees, so the center of the surface looks at the
 * ground point 5, 0, 0, and a box between the camera and that point */
#define RAY_GROUND \
    "camera {\n" \
    "  Position: {5, 10, 10}\n" \
    "  Rotation: {-0.78539816, 0, 0}\n" \
    "  Camera: {}\n" \
    "}\n" \
    "view {\n" \
    "  View: {camera: camera}\n" \
    "}\n" \
    "crate {\n" \
    "  Position: {5, 2, 2}\n" \
    "  Box: {2, 2, 2}\n" \
    "}\n"

/** Create a world with the prelude and a scene, and run two frames.
 *
 * @param scene Script code after the prelude
 * @return The world
 */
static ecs_world_t* ray_world(
    const char *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *code = flecs_asprintf("%s%s", RAY_PRELUDE, scene);
    test_assert(ecs_script(world, { .code = code }) != 0);
    ecs_os_free(code);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

/** Move the pointer and run a frame.
 *
 * @param world The world
 * @param x Cursor x in window points
 * @param y Cursor y in window points
 */
static void ray_cursor(
    ecs_world_t *world,
    float x,
    float y)
{
    box_input_mouse_move(world, x, y, false);
    ecs_progress(world, 0);
}

/** Get the ray of a named view, which must exist.
 *
 * @param world The world
 * @param name Name of the view
 * @param origin Receives the origin
 * @param dir Receives the direction
 */
static void ray_get(
    const ecs_world_t *world,
    const char *name,
    vec3 origin,
    vec3 dir)
{
    ecs_entity_t view = ecs_lookup(world, name);
    test_assert(view != 0);
    test_bool(box_view_cursor_ray(world, view, origin, dir), true);
    test_assert(fabsf(glm_vec3_norm(dir) - 1.0f) < RAY_EPS);
}

/** Check a vector against expected components.
 *
 * @param v The vector
 * @param x Expected x
 * @param y Expected y
 * @param z Expected z
 */
static void ray_expect(
    const vec3 v,
    float x,
    float y,
    float z)
{
    test_assert(fabsf(v[0] - x) < RAY_EPS);
    test_assert(fabsf(v[1] - y) < RAY_EPS);
    test_assert(fabsf(v[2] - z) < RAY_EPS);
}

/** Check the ray of a camera at 0, 0, 10 looking down -Z with the default projection against
 * the corner of the surface the direction (sx * tan * aspect, sy * tan, -1) points at.
 *
 * @param origin Origin of the ray
 * @param dir Direction of the ray
 * @param sx Sign of the corner along x, 1 for right
 * @param sy Sign of the corner along y, 1 for up
 */
static void ray_expect_corner(
    const vec3 origin,
    const vec3 dir,
    float sx,
    float sy)
{
    vec3 v = { sx * RAY_TAN * RAY_ASPECT, sy * RAY_TAN, -1.0f };
    vec3 n;
    glm_vec3_normalize_to(v, n);
    ray_expect(dir, n[0], n[1], n[2]);
    ray_expect(origin, v[0] * 0.1f, v[1] * 0.1f, 10.0f - 0.1f);
}

/** Intersect a ray with the ground plane y = 0.
 *
 * @param origin Origin of the ray
 * @param dir Direction of the ray
 * @param out Receives the ground point
 */
static void ray_ground(
    const vec3 origin,
    const vec3 dir,
    vec3 out)
{
    test_assert(dir[1] < 0);
    float t = -origin[1] / dir[1];
    glm_vec3_scale(ECS_CONST_CAST(float*, dir), t, out);
    glm_vec3_add(out, ECS_CONST_CAST(float*, origin), out);
}

void CursorRay_perspective_center(void) {
    // ui/picking
    ecs_world_t *world = ray_world(
        "camera {\n"
        "  Position: {0, 10, 10}\n"
        "  Rotation: {-0.78539816, 0, 0}\n"
        "  Camera: {}\n"
        "}\n"
        "view {\n"
        "  View: {camera: camera}\n"
        "}\n");
    vec3 origin, dir;
    ray_cursor(world, 640, 360);
    ray_get(world, "view", origin, dir);
    ray_expect(dir, 0, -0.70710678f, -0.70710678f);
    ray_expect(origin, 0, 10.0f - 0.070710678f, 10.0f - 0.070710678f);

    ecs_fini(world);
}

void CursorRay_perspective_corner(void) {
    // ui/picking
    ecs_world_t *world = ray_world(
        "camera {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "view {\n"
        "  View: {camera: camera}\n"
        "}\n");
    vec3 origin, dir;
    ray_cursor(world, 1280, 720);
    ray_get(world, "view", origin, dir);
    ray_expect_corner(origin, dir, 1, -1);

    ray_cursor(world, 0, 0);
    ray_get(world, "view", origin, dir);
    ray_expect_corner(origin, dir, -1, 1);

    ecs_fini(world);
}

void CursorRay_orthographic(void) {
    // ui/picking
    ecs_world_t *world = ray_world(
        "camera {\n"
        "  Position: {0, 0, 100}\n"
        "  Camera: {ortho: true, ortho_scale: 1}\n"
        "}\n"
        "view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "top {\n"
        "  Position: {0, 50, 0}\n"
        "  Rotation: {-1.57079633, 0, 0}\n"
        "  Camera: {ortho: true, ortho_height: 72}\n"
        "}\n"
        "top_view {\n"
        "  View: {camera: top}\n"
        "}\n");
    vec3 origin, dir;
    ray_cursor(world, 640, 360);
    ray_get(world, "view", origin, dir);
    ray_expect(dir, 0, 0, -1);
    ray_expect(origin, 0, 0, 99.9f);
    ray_get(world, "top_view", origin, dir);
    ray_expect(dir, 0, -1, 0);
    ray_expect(origin, 0, 49.9f, 0);

    ray_cursor(world, 740, 310);
    ray_get(world, "view", origin, dir);
    ray_expect(dir, 0, 0, -1);
    ray_expect(origin, 100, 50, 99.9f);
    ray_get(world, "top_view", origin, dir);
    ray_expect(dir, 0, -1, 0);
    ray_expect(origin, 10, 49.9f, -5);

    ecs_fini(world);
}

void CursorRay_ground_behind_shape(void) {
    // ui/picking
    ecs_world_t *world = ray_world(RAY_GROUND);
    vec3 origin, dir, ground;
    ray_cursor(world, 640, 360);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->hit, ecs_lookup(world, "crate"));
    test_uint(status->hit_view, ecs_lookup(world, "view"));

    ray_get(world, "view", origin, dir);
    ray_expect(dir, 0, -0.70710678f, -0.70710678f);
    ray_ground(origin, dir, ground);
    ray_expect(ground, 5, 0, 0);

    ecs_fini(world);
}

void CursorRay_ui_view_over(void) {
    // ui/picking
    ecs_world_t *world = ray_world(RAY_GROUND
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  panel {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {300, 200}\n"
        "    InputBlocker\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n");
    vec3 origin, dir, ground;
    ray_cursor(world, 640, 360);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->hit, ecs_lookup(world, "hud.panel"));
    test_uint(status->hit_view, ecs_lookup(world, "hud_view"));
    test_uint(status->handler, ecs_lookup(world, "hud.panel"));

    ray_get(world, "view", origin, dir);
    ray_expect(dir, 0, -0.70710678f, -0.70710678f);
    ray_ground(origin, dir, ground);
    ray_expect(ground, 5, 0, 0);

    ray_get(world, "hud_view", origin, dir);
    ray_expect(dir, 0, 0, -1);
    ray_expect(origin, 0, 0, 99.9f);

    ecs_fini(world);
}

void CursorRay_no_ray(void) {
    // ui/picking
    ecs_world_t *world = ray_world(RAY_GROUND
        "bare_view {\n"
        "  View: {}\n"
        "}\n");
    ecs_entity_t view = ecs_lookup(world, "view");
    ecs_entity_t bare = ecs_lookup(world, "bare_view");
    vec3 origin = { 1, 2, 3 }, dir = { 4, 5, 6 };
    test_bool(box_view_cursor_ray(world, view, origin, dir), false);
    ray_expect(origin, 1, 2, 3);
    ray_expect(dir, 4, 5, 6);

    ray_cursor(world, 640, 360);
    test_bool(box_view_cursor_ray(world, view, origin, dir), true);
    test_bool(box_view_cursor_ray(world, bare, origin, dir), false);
    test_bool(box_view_cursor_ray(world, ecs_lookup(world, "camera"), origin, dir), false);
    test_bool(box_view_cursor_ray(world, ecs_lookup(world, "crate"), origin, dir), false);
    test_bool(box_view_cursor_ray(world, 0, origin, dir), false);

    ray_cursor(world, 1400, 360);
    test_bool(box_view_cursor_ray(world, view, origin, dir), false);
    ray_cursor(world, 640, -10);
    test_bool(box_view_cursor_ray(world, view, origin, dir), false);

    ray_cursor(world, 640, 360);
    test_bool(box_view_cursor_ray(world, view, origin, dir), true);
    ecs_delete(world, ecs_lookup(world, "camera"));
    ecs_progress(world, 0);
    test_bool(box_view_cursor_ray(world, view, origin, dir), false);

    ecs_delete(world, view);
    test_bool(box_view_cursor_ray(world, view, origin, dir), false);

    ecs_fini(world);
}
