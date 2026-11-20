/**
 * @file PortalCctv.c
 * @brief Tests for the CCTV of test/assets/portal/cctv_screen.flecs: the camera list, selecting a
 * camera with a click, an offline camera, and the SurfaceSchedule of the feed that renders only
 * while its screen is seen.
 */

#include "screen_test.h"
#include <string.h>

/** A window, two security cameras (b offline), the CCTV screen 1.35 m up facing +z at the
 * origin, its feed, and a camera looking at the screen from 2 m */
#define CCTV_SCENE \
    "using box.transform\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.surface\n" \
    "window { Surface: {width: 1280, height: 720} }\n" \
    "state { PortalCctvState: {selected: 0} }\n" \
    "cameras {\n" \
    "  PortalSecurityCamera a(label: \"CAM-A\", title: \"HALL\", place: \"NORTH\", yaw: 1) {\n" \
    "    Position: {0, 2, 5}\n" \
    "  }\n" \
    "  PortalSecurityCamera b(label: \"CAM-B\", title: \"YARD\", place: \"SOUTH\", offline: true) {\n" \
    "    Position: {0, 2, -5}\n" \
    "  }\n" \
    "}\n" \
    "feed { texture {} }\n" \
    "console {\n" \
    "  PortalCctvScreen screen(\n" \
    "    width: 1.92, height: 0.72, cameras: [cameras.a, cameras.b], state: state,\n" \
    "    picture: feed.texture)\n" \
    "  {\n" \
    "    Position: {0, 1.35, 0}\n" \
    "  }\n" \
    "}\n" \
    "feed {\n" \
    "  PortalCctvFeed: {cameras: [cameras.a, cameras.b], state: state, seen: console.screen}\n" \
    "}\n" \
    "camera {\n" \
    "  Position: {0, 1.35, 2}\n" \
    "  Rotation: {0, 0, 0}\n" \
    "  Camera: {}\n" \
    "}\n" \
    "view {\n" \
    "  View: {camera: camera}\n" \
    "}\n"

/** Path of the list of cameras */
#define CCTV_LIST "console.screen.body.list"

/** Path of the frame of the picture */
#define CCTV_FRAME "console.screen.body.feed.frame"

/** Create the world of a scene with the widgets and the screen templates and run 20 frames.
 *
 * @param scene The scene
 * @return The world
 */
static ecs_world_t* cctv_world(
    const char *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    screen_load(world, "test/assets/ui/widgets.flecs", "box.ui.widgets");
    screen_load(world, "test/assets/portal/door.flecs", NULL);
    screen_load(world, "test/assets/portal/screen.flecs", NULL);
    screen_load(world, "test/assets/portal/cctv_screen.flecs", NULL);
    test_assert(ecs_script(world, { .code = scene }) != 0);
    screen_run(world, 20);

    return world;
}

/** Get the frames the feed rendered.
 *
 * @param world The world
 * @return SurfaceScheduleStatus.renders of the feed
 */
static int32_t cctv_renders(
    ecs_world_t *world)
{
    const SurfaceScheduleStatus *status = ecs_get(
        world, screen_e(world, "feed"), SurfaceScheduleStatus);
    test_assert(status != NULL);

    return status->renders;
}

/** Get the camera of the view of the feed.
 *
 * @param world The world
 * @return View.camera of the feed's view
 */
static ecs_entity_t cctv_camera(
    ecs_world_t *world)
{
    const View *view = ecs_get(world, screen_e(world, "feed.views.view"), View);
    test_assert(view != NULL);

    return view->camera;
}

void PortalCctv_lists_cameras(void) {
    ecs_world_t *world = cctv_world(CCTV_SCENE);

    test_str(screen_text(world, CCTV_LIST ".cam_0.pick.caption"), "CAM-A");
    test_str(screen_text(world, CCTV_LIST ".cam_1.pick.caption"), "CAM-B  OFFLINE");
    test_str(screen_text(world, "console.screen.header.band.caption"), "SECURITY // CCTV");
    test_str(screen_text(world, "console.screen.header.band.state"), "1/2 ONLINE");
    test_str(screen_text(world, CCTV_FRAME ".label"), "CAM-A  HALL");
    test_str(screen_text(world, CCTV_FRAME ".where"), "REC  NORTH");
    test_assert(ecs_lookup(world, CCTV_FRAME ".image") != 0);
    test_assert(ecs_lookup(world, CCTV_FRAME ".signal") == 0);

    test_uint(cctv_camera(world), screen_e(world, "cameras.a"));
    test_assert(!ecs_get(world, screen_e(world, "feed"), SurfaceSchedule)->paused);
    test_uint(
        ecs_get(world, screen_e(world, "feed"), Surface)->texture,
        screen_e(world, "feed.texture"));

    ecs_fini(world);
}

void PortalCctv_camera_pans(void) {
    ecs_world_t *world = cctv_world(CCTV_SCENE);
    ecs_entity_t a = screen_e(world, "cameras.a");
    float low = 100, high = -100;
    int32_t i;

    for (i = 0; i < 300; i ++) {
        screen_run(world, 1);

        float y = ecs_get(world, a, Rotation)->y;
        low = y < low ? y : low;
        high = y > high ? y : high;
    }

    test_assert(low < 0.7f);
    test_assert(low >= 0.65f - 1e-4f);
    test_assert(high > 1.3f);
    test_assert(high <= 1.35f + 1e-4f);

    ecs_fini(world);
}

void PortalCctv_click_selects_camera(void) {
    ecs_world_t *world = cctv_world(CCTV_SCENE);

    screen_click(world, CCTV_LIST ".cam_1.pick", 1);
    test_int(screen_get(world, "state", "PortalCctvState", "selected"), 1);
    test_uint(cctv_camera(world), screen_e(world, "cameras.b"));
    test_assert(ecs_get(world, screen_e(world, "feed"), SurfaceSchedule)->paused);
    test_str(screen_text(world, CCTV_FRAME ".signal"), "NO SIGNAL");
    test_str(screen_text(world, CCTV_FRAME ".which"), "CAM-B  YARD");
    test_assert(ecs_lookup(world, CCTV_FRAME ".image") == 0);

    screen_click(world, CCTV_LIST ".cam_0.pick", 1);
    test_int(screen_get(world, "state", "PortalCctvState", "selected"), 0);
    test_uint(cctv_camera(world), screen_e(world, "cameras.a"));
    test_assert(!ecs_get(world, screen_e(world, "feed"), SurfaceSchedule)->paused);
    test_assert(ecs_lookup(world, CCTV_FRAME ".image") != 0);

    ecs_fini(world);
}

void PortalCctv_feed_renders_while_seen(void) {
    ecs_world_t *world = cctv_world(CCTV_SCENE);
    ecs_entity_t feed = screen_e(world, "feed");
    ecs_entity_t camera = screen_e(world, "camera");
    int32_t before = cctv_renders(world);

    test_assert(ecs_get(world, feed, SurfaceScheduleStatus)->seen);
    test_assert(before > 10);

    screen_run(world, 10);
    test_int(cctv_renders(world), before + 10);

    ecs_set(world, camera, Rotation, {0, 3.14159265f, 0});
    screen_run(world, 2);
    test_assert(!ecs_get(world, feed, SurfaceScheduleStatus)->seen);
    before = cctv_renders(world);
    screen_run(world, 10);
    test_int(cctv_renders(world), before);

    ecs_set(world, camera, Rotation, {0, 0, 0});
    ecs_set(world, camera, Position, {0, 1.35f, 20});
    screen_run(world, 2);
    test_assert(!ecs_get(world, feed, SurfaceScheduleStatus)->seen);
    before = cctv_renders(world);
    screen_run(world, 10);
    test_int(cctv_renders(world), before);

    ecs_set(world, camera, Position, {0, 1.35f, 2});
    screen_run(world, 2);
    test_assert(ecs_get(world, feed, SurfaceScheduleStatus)->seen);
    test_assert(cctv_renders(world) > before);

    ecs_fini(world);
}

void PortalCctv_feed_keeps_interval(void) {
    ecs_world_t *world = cctv_world(CCTV_SCENE);
    ecs_entity_t feed = screen_e(world, "feed");
    SurfaceSchedule *schedule = ecs_get_mut(world, feed, SurfaceSchedule);
    int32_t before;

    schedule->interval = 0.25f;
    ecs_modified(world, feed, SurfaceSchedule);
    screen_run(world, 3);
    before = cctv_renders(world);
    screen_run(world, 30);

    test_assert(cctv_renders(world) - before >= 9);
    test_assert(cctv_renders(world) - before <= 11);

    ecs_fini(world);
}

void PortalCctv_offline_feed_renders_nothing(void) {
    ecs_world_t *world = cctv_world(CCTV_SCENE);
    int32_t before;

    screen_set(world, "state", "PortalCctvState", "selected", 1);
    screen_run(world, 2);
    test_assert(ecs_get(world, screen_e(world, "feed"), SurfaceSchedule)->paused);
    before = cctv_renders(world);
    screen_run(world, 10);
    test_int(cctv_renders(world), before);

    ecs_fini(world);
}

/** A room whose door at z -10 opens south to the outdoors, the CCTV screen inside it at z -15
 * facing the door, and a camera outdoors at z -5 looking at it through the door */
#define CCTV_ROOM_SCENE \
    "using box.transform\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.portal\n" \
    "using box.surface\n" \
    "window { Surface: {width: 1280, height: 720} }\n" \
    "room {\n" \
    "  VisArea: {shape: [{-5, -20}, {5, -20}, {5, -10}, {-5, -10}], y: 0, height: 3}\n" \
    "}\n" \
    "door {\n" \
    "  Position: {0, 1.2, -10}\n" \
    "  VisPortal: {kind: Door, size: {2, 2.4}, normal: {0, 0, 1}, open: 1}\n" \
    "}\n" \
    "state { PortalCctvState: {selected: 0} }\n" \
    "cameras {\n" \
    "  PortalSecurityCamera a(label: \"CAM-A\") { Position: {0, 2, -18} }\n" \
    "}\n" \
    "feed { texture {} }\n" \
    "console {\n" \
    "  PortalCctvScreen screen(cameras: [cameras.a], state: state, picture: feed.texture) {\n" \
    "    Position: {0, 1.35, -15}\n" \
    "  }\n" \
    "}\n" \
    "feed {\n" \
    "  PortalCctvFeed: {cameras: [cameras.a], state: state, seen: console.screen}\n" \
    "}\n" \
    "camera {\n" \
    "  Position: {0, 1.35, -5}\n" \
    "  Rotation: {0, 0, 0}\n" \
    "  Camera: {}\n" \
    "}\n" \
    "view {\n" \
    "  View: {camera: camera}\n" \
    "}\n"

void PortalCctv_closed_door_hides_screen(void) {
    ecs_world_t *world = cctv_world(CCTV_ROOM_SCENE);
    ecs_entity_t feed = screen_e(world, "feed");
    ecs_entity_t door = screen_e(world, "door");
    int32_t before;

    test_assert(ecs_get(world, feed, SurfaceScheduleStatus)->seen);

    ecs_get_mut(world, door, VisPortal)->open = 0;
    ecs_modified(world, door, VisPortal);
    screen_run(world, 2);
    test_assert(!ecs_get(world, feed, SurfaceScheduleStatus)->seen);
    before = cctv_renders(world);
    screen_run(world, 10);
    test_int(cctv_renders(world), before);

    ecs_get_mut(world, door, VisPortal)->open = 1;
    ecs_modified(world, door, VisPortal);
    screen_run(world, 2);
    test_assert(ecs_get(world, feed, SurfaceScheduleStatus)->seen);

    ecs_fini(world);
}
