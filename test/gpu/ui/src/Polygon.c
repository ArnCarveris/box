/**
 * @file Polygon.c
 * @brief Picking of Polygon2D shapes: a click hits a polygon inside its outline or its stroke,
 * not its bounds, and not where a ClipChildren ancestor cuts it.
 *
 * The scenes are screen UI of one point per pixel on the 1280x720 window, so the screen pixel of
 * a point x, y of the plane is 640 + x, 360 - y.
 */

#include <box.h>
#include <bake_test.h>

/** A probe that counts the clicks on its entity, an L shaped polygon in front of a Rect, a
 * stroked road and a clipped disc */
#define POLYGON_SCENE \
    "using flecs.script\n" \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.layout\n" \
    "using box.picking\n" \
    "template Probe {\n" \
    "  mut clicks: i32 = 0\n" \
    "  async {\n" \
    "    while true {\n" \
    "      await on.click(this)\n" \
    "      clicks = clicks + 1\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "l_path {\n" \
    "  Path2D: {start: {0, 0}, closed: true, segments: [\n" \
    "    {kind: Line, end: {4, 0}},\n" \
    "    {kind: Line, end: {4, 2}},\n" \
    "    {kind: Line, end: {2, 2}},\n" \
    "    {kind: Line, end: {2, 4}},\n" \
    "    {kind: Line, end: {0, 4}}\n" \
    "  ]}\n" \
    "}\n" \
    "road_path {\n" \
    "  Path2D: {start: {-100, -100}, segments: [\n" \
    "    {kind: Line, end: {100, -100}},\n" \
    "    {kind: CatmullRom, end: {150, -50}},\n" \
    "    {kind: CatmullRom, end: {150, 50}}\n" \
    "  ]}\n" \
    "}\n" \
    "hud {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "  behind {\n" \
    "    Position: {0, 0, 0}\n" \
    "    Rect: {80, 80}\n" \
    "    Interactive\n" \
    "    Probe: {}\n" \
    "  }\n" \
    "  l_shape {\n" \
    "    Position: {0, 0, 1}\n" \
    "    Polygon2D: {path: l_path, scale: {10, 10}, offset: {-20, -20}}\n" \
    "    Interactive\n" \
    "    Probe: {}\n" \
    "  }\n" \
    "  road {\n" \
    "    Position: {0, 0, 1}\n" \
    "    Polygon2D: {path: road_path, width: 10}\n" \
    "    Interactive\n" \
    "    Probe: {}\n" \
    "  }\n" \
    "  panel {\n" \
    "    Position: {-200, 100, 0}\n" \
    "    Rect: {40, 40}\n" \
    "    ClipChildren\n" \
    "    disc {\n" \
    "      Position: {20, 0, 1}\n" \
    "      Polygon2D: {path: l_path, scale: {10, 10}, offset: {-20, -20}}\n" \
    "      Interactive\n" \
    "      Probe: {}\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "hud_view {\n" \
    "  View: {camera: hud.camera}\n" \
    "}\n"

/** The clicks of the probe of an entity */
typedef struct { int32_t clicks; } PolygonMut;

/** Click a screen pixel and let the click reach the probes.
 *
 * @param world The world
 * @param s The shell session
 * @param x Column of the pixel
 * @param y Row of the pixel
 */
static void polygon_click(
    ecs_world_t *world,
    ecs_entity_t s,
    int32_t x,
    int32_t y)
{
    char *cmd = flecs_asprintf("mouse_click left [%d, %d]", x, y);
    ecs_os_free(box_command_run(world, s, cmd, NULL));
    ecs_os_free(cmd);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

/** Get the clicks of the probe of an entity.
 *
 * @param world The world
 * @param name The path of the entity
 * @return The clicks
 */
static int32_t polygon_clicks(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t e = ecs_lookup(world, name);
    test_assert(mut != 0);
    test_assert(e != 0);

    return ((const PolygonMut*)ecs_get_id(world, e, mut))->clicks;
}

/** A click hits the L inside its outline; in its notch, inside its bounds, the Rect behind it. */
void Polygon_picked_inside_outline(void) {
    // ui/polygon_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .code = POLYGON_SCENE }) != 0);
    ecs_progress(world, 0);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    polygon_click(world, s, 630, 370);
    test_int(polygon_clicks(world, "hud.l_shape"), 1);
    test_int(polygon_clicks(world, "hud.behind"), 0);

    polygon_click(world, s, 650, 350);
    test_int(polygon_clicks(world, "hud.l_shape"), 1);
    test_int(polygon_clicks(world, "hud.behind"), 1);

    polygon_click(world, s, 630, 345);
    test_int(polygon_clicks(world, "hud.l_shape"), 2);
    test_int(polygon_clicks(world, "hud.behind"), 1);

    polygon_click(world, s, 670, 360);
    test_int(polygon_clicks(world, "hud.l_shape"), 2);
    test_int(polygon_clicks(world, "hud.behind"), 2);

    ecs_fini(world);
}

/** A click hits the road within half its width of its path, on its straight part and its curved
 * end, not beside it nor inside its bounds away from it.
 */
void Polygon_picked_on_stroke(void) {
    // ui/polygon_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .code = POLYGON_SCENE }) != 0);
    ecs_progress(world, 0);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    polygon_click(world, s, 640, 464);
    test_int(polygon_clicks(world, "hud.road"), 1);

    polygon_click(world, s, 640, 470);
    test_int(polygon_clicks(world, "hud.road"), 1);

    polygon_click(world, s, 740, 360);
    test_int(polygon_clicks(world, "hud.road"), 1);

    polygon_click(world, s, 790, 310);
    test_int(polygon_clicks(world, "hud.road"), 2);

    ecs_fini(world);
}

/** A click hits the L where its ClipChildren ancestor shows it, not where the panel cuts it. */
void Polygon_picked_inside_clip(void) {
    // ui/polygon_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .code = POLYGON_SCENE }) != 0);
    ecs_progress(world, 0);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    polygon_click(world, s, 450, 265);
    test_int(polygon_clicks(world, "hud.panel.disc"), 1);

    polygon_click(world, s, 470, 265);
    test_int(polygon_clicks(world, "hud.panel.disc"), 1);

    ecs_fini(world);
}
