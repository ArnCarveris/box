/**
 * @file AreaBake.c
 * @brief Tests of the irradiance grids of areas that a mover carries, with the GPU backend: a
 * ship with a room and a door to its deck bakes the room once, keeps its light while it sails and
 * turns, blends it at the door and keeps it inside the walls.
 *
 * The ship is at the absolute X of AB_X, its images are 96x64.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width in pixels of the images */
#define AB_WIDTH (96)

/** Height in pixels of the images */
#define AB_HEIGHT (64)

/** Seconds of a frame */
#define AB_DELTA (1.0f / 60.0f)

/** X of the ship at the dock */
#define AB_X (3000.0f)

/** Frames of a voyage */
#define AB_VOYAGE (24)

/** Largest channel difference between two images of the same lighting placed differently */
#define AB_CLOSE_DIFF (4)

/** Pixels of two images of the room that may differ by more than AB_CLOSE_DIFF, a hundredth of
 * them: those an edge passes within the rounding of its place far from the origin */
#define AB_EDGE_PIXELS (AB_WIDTH * AB_HEIGHT / 100)

/** Largest difference of the means of the channels of two images of the room */
#define AB_EDGE_MEAN (0.1)

/** Largest step of the light between two neighbouring pixels of the floor seen through the door,
 * about what its gradient takes and far below the step the ambient of the room leaves there
 * without the grid */
#define AB_DOOR_STEP (6.0)

/** First row of the floor in the images through the door, the deck beyond it */
#define AB_DOOR_FLOOR (20)

/** Where the camera of the ship looks from */
typedef enum ab_camera_t {
    AB_INSIDE,  /**< In the room, at its back wall and the panel */
    AB_DOOR,    /**< In the room, through the door at the deck */
    AB_OUTSIDE  /**< Outside the ship, at the outer side of its right wall */
} ab_camera_t;

/** A ship with a room, a door to its deck and a light, seen by a camera aboard */
typedef struct ab_ship_t {
    ecs_world_t *world;     /**< The world */
    ecs_entity_t ship;      /**< The mover */
    ecs_entity_t room;      /**< The VisArea with the LightBake */
    ecs_entity_t light;     /**< The point light of the room */
} ab_ship_t;

/** Add a white box to the ship.
 *
 * @param world The world
 * @param ship The ship
 * @param p Position of its center on the ship
 * @param size Its size
 * @return The box
 */
static ecs_entity_t ab_box(
    ecs_world_t *world,
    ecs_entity_t ship,
    Position p,
    Box size)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, ship);
    ecs_set_ptr(world, e, Position, &p);
    ecs_set_ptr(world, e, Box, &size);
    ecs_set(world, e, Rgb, {200, 200, 200});
    ecs_set(world, e, PbrMaterial, {.roughness = 1});

    return e;
}

/** Create a headless world with a ship at AB_X: a room of 6 by 6 by 3 with walls that reach
 * into it, so they are members of the room, a floor that goes on through the door in its front
 * wall as a deck, an emissive panel, a point light and a camera aboard, and an ambient light.
 *
 * @param bake Whether the room has LightBake
 * @param mode LightBakeMode of the light
 * @param camera Where the camera looks from
 * @return The ship
 */
static ab_ship_t ab_ship(
    bool bake,
    LightBakeMode mode,
    ab_camera_t camera)
{
    ab_ship_t s = {0};
    ecs_world_t *world = s.world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, Scene, {.baked = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {120, 130, 150}, .ground = {80, 75, 70},
        .intensity = 0.5f});

    test_int(ecs_script_run(world, "ship",
        "using box.transform\n"
        "using box.portal\n"
        "ship {\n"
        "  DynamicTransform\n"
        "  Position: {3000, 0, 0}\n"
        "  Rotation: {0, 0, 0}\n"
        "  room {\n"
        "    VisArea: {shape: [{-3, -3}, {3, -3}, {3, 3}, {-3, 3}], y: 0, height: 3,\n"
        "      ambient: {0.01, 0.01, 0.01}}\n"
        "  }\n"
        "  door {\n"
        "    Position: {0, 1.2, 3}\n"
        "    VisPortal: {kind: Door, size: {2, 2.4}, normal: {0, 0, 1}}\n"
        "  }\n"
        "}\n", NULL), 0);
    s.ship = ecs_lookup(world, "ship");
    s.room = ecs_lookup(world, "ship.room");
    test_assert(s.ship != 0 && s.room != 0);
    if (bake) {
        ecs_set(world, s.room, LightBake, {.probe_spacing = 1, .bounces = 1,
            .capture_size = 8, .budget_ms = 8, .compute = true});
    }

    ab_box(world, s.ship, (Position){0, -0.05f, 2}, (Box){6.4f, 0.2f, 10.4f});
    ab_box(world, s.ship, (Position){0, 3.05f, 0}, (Box){6.4f, 0.2f, 6.4f});
    ab_box(world, s.ship, (Position){0, 1.5f, -3}, (Box){6.4f, 3, 0.2f});
    ab_box(world, s.ship, (Position){-3, 1.5f, 0}, (Box){0.2f, 3, 6.4f});
    ab_box(world, s.ship, (Position){3, 1.5f, 0}, (Box){0.2f, 3, 6.4f});
    ab_box(world, s.ship, (Position){-2.05f, 1.5f, 3}, (Box){2.1f, 3, 0.2f});
    ab_box(world, s.ship, (Position){2.05f, 1.5f, 3}, (Box){2.1f, 3, 0.2f});
    ab_box(world, s.ship, (Position){0, 2.7f, 3}, (Box){2, 0.6f, 0.2f});
    ecs_entity_t panel = ab_box(world, s.ship, (Position){-1.5f, 1.5f, -2.85f},
        (Box){1, 1, 0.1f});
    ecs_set(world, panel, Emissive, {.strength = 2, .color = {255, 180, 90}});

    s.light = ecs_new_w_pair(world, EcsChildOf, s.ship);
    ecs_set(world, s.light, Position, {1.5f, 2.2f, 0});
    ecs_set(world, s.light, Rgb, {255, 240, 220});
    ecs_set(world, s.light, PointLight, {.intensity = 6, .range = 8, .bake = mode});

    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, s.ship);
    if (camera == AB_INSIDE) {
        ecs_set(world, c, Position, {0, 1.6f, 2.6f});
        ecs_set(world, c, Rotation, {-0.15f, 0, 0});
    } else if (camera == AB_DOOR) {
        ecs_set(world, c, Position, {0, 1.4f, 0.5f});
        ecs_set(world, c, Rotation, {-0.5f, GLM_PIf, 0});
    } else {
        ecs_set(world, c, Position, {9, 1.5f, -1});
        ecs_set(world, c, Rotation, {0, GLM_PIf * 0.5f, 0});
    }

    ecs_set(world, c, Camera, {.fov = camera == AB_OUTSIDE ? 40.0f : 70.0f, .near_ = 0.05f,
        .far_ = 200});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = c;
    v->clear = (ClearSettings){.enabled = true, .color = {30, 40, 60, 255}};
    v->lighting.enabled = true;
    v->lighting.baked = true;
    ecs_modified(world, view, View);
    ecs_entity_t surface = ecs_entity(world, { .name = "capture" });
    ecs_set(world, surface, Surface, {.width = AB_WIDTH, .height = AB_HEIGHT,
        .resolution_scale = 1});

    return s;
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param s The ship
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* ab_frame(
    const ab_ship_t *s,
    const char *file)
{
    ecs_world_t *world = s->world;
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = AB_WIDTH, .height = AB_HEIGHT,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, AB_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, AB_WIDTH);
    test_int(h, AB_HEIGHT);
    ecs_os_free(path);

    return result;
}

/** Get the status of the bake of the room.
 *
 * @param s The ship
 * @return The status
 */
static const LightBakeStatus* ab_status(
    const ab_ship_t *s)
{
    const LightBakeStatus *result = ecs_get(s->world, s->room, LightBakeStatus);
    test_assert(result != NULL);

    return result;
}

/** Run frames until the bake of the room converged.
 *
 * @param s The ship
 */
static void ab_converge(
    const ab_ship_t *s)
{
    int32_t frames = 0;
    while (!ab_status(s)->converged && frames < 600) {
        ecs_progress(s->world, AB_DELTA);
        box_render_sync(s->world);
        frames ++;
    }

    test_bool(ab_status(s)->converged, true);
    test_assert(ab_status(s)->error == NULL);
}

/** Place the ship on frame i of a voyage of AB_VOYAGE frames: it sails 500 units along X and
 * 120 along Z and turns by a quarter turn.
 *
 * @param s The ship
 * @param i The frame, 0 at the dock
 */
static void ab_sail(
    const ab_ship_t *s,
    int32_t i)
{
    float t = (float)i / (float)AB_VOYAGE;
    ecs_set(s->world, s->ship, Position, {AB_X + 500.0f * t, 0, 120.0f * t});
    ecs_set(s->world, s->ship, Rotation, {0, GLM_PIf * 0.5f * t, 0});
}

/** Make the render origin follow the ship.
 *
 * @param s The ship
 */
static void ab_follow(
    const ab_ship_t *s)
{
    ecs_singleton_set(s->world, WorldOrigin, {.enabled = false, .render = true,
        .render_follow = s->ship});
}

/** Get the largest channel difference between two images.
 *
 * @param a The first image
 * @param b The second image
 * @return The difference, 0 to 255
 */
static int32_t ab_worst(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, i;
    for (i = 0; i < AB_WIDTH * AB_HEIGHT * 4; i ++) {
        int32_t d = abs((int)a[i] - (int)b[i]);
        worst = (i & 3) != 3 && d > worst ? d : worst;
    }

    return worst;
}

/** Check that two images of the room match: every pixel to within AB_CLOSE_DIFF in every
 * channel but at most AB_EDGE_PIXELS, where an edge passes the center of a pixel within the
 * rounding of its place, and the mean of the channels to within AB_EDGE_MEAN.
 *
 * @param a The first image
 * @param b The second image
 */
static void ab_expect_edges(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t edges = 0, i, c;
    double sum = 0;
    for (i = 0; i < AB_WIDTH * AB_HEIGHT; i ++) {
        int32_t worst = 0;
        for (c = 0; c < 3; c ++) {
            int32_t d = (int)a[i * 4 + c] - (int)b[i * 4 + c];
            worst = abs(d) > worst ? abs(d) : worst;
            sum += d;
        }

        edges += worst > AB_CLOSE_DIFF;
    }

    double mean = fabs(sum) / (AB_WIDTH * AB_HEIGHT * 3);
    if (edges > AB_EDGE_PIXELS || mean > AB_EDGE_MEAN) {
        printf("%d pixels differ, mean difference %.3f\n", edges, mean);
    }

    test_assert(edges <= AB_EDGE_PIXELS);
    test_assert(mean <= AB_EDGE_MEAN);
}

/** Get the mean of the channels of a rectangle of an image.
 *
 * @param px The image
 * @param x0 First column
 * @param y0 First row
 * @param x1 Column past the last
 * @param y1 Row past the last
 * @return The mean, 0 to 255
 */
static double ab_mean(
    const unsigned char *px,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1)
{
    double sum = 0;
    int32_t x, y, c;
    for (y = y0; y < y1; y ++) {
        for (x = x0; x < x1; x ++) {
            for (c = 0; c < 3; c ++) {
                sum += px[(y * AB_WIDTH + x) * 4 + c];
            }
        }
    }

    return sum / (double)((x1 - x0) * (y1 - y0) * 3);
}

/** Get the largest step of the luminance between two neighbouring pixels of a column of the
 * middle of an image, between two rows.
 *
 * @param px The image
 * @param y0 First row
 * @param y1 Last row
 * @return The step
 */
static double ab_column_step(
    const unsigned char *px,
    int32_t y0,
    int32_t y1)
{
    double result = 0, last = -1;
    int32_t y;
    for (y = y0; y <= y1; y ++) {
        double v = ab_mean(px, AB_WIDTH / 2 - 2, y, AB_WIDTH / 2 + 2, y + 1);
        if (last >= 0) {
            result = fmax(result, fabs(v - last));
        }

        last = v;
    }

    return result;
}

/** Free an image and the world of a ship.
 *
 * @param s The ship
 * @param px The image, NULL for none
 */
static void ab_free(
    ab_ship_t *s,
    unsigned char *px)
{
    stbi_image_free(px);
    ecs_fini(s->world);
}

void AreaBake_ship_same_lighting_after_sailing(void) {
    // baked_lighting/area_grid
    /* The room of a ship is lit by a Baked light, which only its probes hold. The render origin
     * follows the ship while it sails 500 units and turns by a quarter turn: the room looks as
     * it did at the dock and no probe is captured again. */
    ab_ship_t s = ab_ship(true, LightBakeModeBaked, AB_INSIDE);
    ab_follow(&s);
    ab_converge(&s);
    int32_t captures = ab_status(&s)->captures;
    test_assert(captures > 0);
    test_assert(ab_status(&s)->probes > 0);
    unsigned char *dock = ab_frame(&s, "area_dock.png");
    unsigned char *px = NULL;
    for (int32_t i = 1; i <= AB_VOYAGE; i ++) {
        ab_sail(&s, i);
        stbi_image_free(px);
        px = ab_frame(&s, "area_sail.png");
    }

    stbi_image_free(px);
    px = ab_frame(&s, "area_sail.png");
    int32_t worst = ab_worst(dock, px);
    printf("worst channel difference after sailing %d, wall %.1f\n", worst,
        ab_mean(dock, 30, 10, 66, 40));
    test_assert(worst <= AB_CLOSE_DIFF);
    test_assert(ab_mean(dock, 30, 10, 66, 40) > 20);
    test_int(ab_status(&s)->captures, captures);
    test_bool(ab_status(&s)->converged, true);
    test_assert(ecs_get(s.world, s.ship, WorldTransform)->m[3][0] > AB_X + 400);
    stbi_image_free(dock);
    ab_free(&s, px);
}

void AreaBake_door_blend_continuous(void) {
    // baked_lighting/area_grid
    /* Looking down at the door, the floor of the room, lit by its probes, goes over into the
     * deck outside, lit by the ambient light, without a step: the grid blends over a probe
     * spacing across the door. Without the grid the ambient of the room leaves a step there.
     * The ship sails and turns and the view stays the same. */
    ab_ship_t s = ab_ship(true, LightBakeModeBaked, AB_DOOR);
    ab_ship_t plain = ab_ship(false, LightBakeModeBaked, AB_DOOR);
    ab_follow(&s);
    ab_converge(&s);
    unsigned char *dock = ab_frame(&s, "area_door.png");
    unsigned char *flat = ab_frame(&plain, "area_door_plain.png");
    double step = ab_column_step(dock, AB_DOOR_FLOOR, AB_HEIGHT - 1);
    double seam = ab_column_step(flat, AB_DOOR_FLOOR, AB_HEIGHT - 1);
    double inside = ab_mean(dock, AB_WIDTH / 2 - 2, AB_HEIGHT - 4, AB_WIDTH / 2 + 2, AB_HEIGHT);
    double deck = ab_mean(dock, AB_WIDTH / 2 - 2, AB_DOOR_FLOOR, AB_WIDTH / 2 + 2,
        AB_DOOR_FLOOR + 4);
    printf("door: largest step %.1f, without the grid %.1f, inside %.1f, deck %.1f\n", step,
        seam, inside, deck);
    test_assert(step <= AB_DOOR_STEP);
    test_assert(seam > 3 * AB_DOOR_STEP);
    test_assert(inside - deck > 3 * AB_DOOR_STEP);

    unsigned char *px = NULL;
    for (int32_t i = 1; i <= AB_VOYAGE; i ++) {
        ab_sail(&s, i);
        stbi_image_free(px);
        px = ab_frame(&s, "area_door.png");
    }

    test_assert(ab_column_step(px, AB_DOOR_FLOOR, AB_HEIGHT - 1) <= AB_DOOR_STEP);
    ab_expect_edges(dock, px);
    stbi_image_free(dock);
    stbi_image_free(flat);
    ab_free(&plain, NULL);
    ab_free(&s, px);
}

void AreaBake_no_leak_through_wall(void) {
    // baked_lighting/area_grid
    /* The outer side of a wall of the room, right by its probes, looks as it does without the
     * bake: the grid lights the room and the opening of its door, not what lies outside. */
    ab_ship_t s = ab_ship(true, LightBakeModeMixed, AB_OUTSIDE);
    ab_ship_t plain = ab_ship(false, LightBakeModeMixed, AB_OUTSIDE);
    ab_converge(&s);
    unsigned char *baked = ab_frame(&s, "area_wall.png");
    unsigned char *flat = ab_frame(&plain, "area_wall_plain.png");
    int32_t worst = ab_worst(baked, flat);
    printf("outer wall: worst channel difference %d, wall %.1f\n", worst,
        ab_mean(baked, 20, 10, 76, 54));
    test_assert(worst <= AB_CLOSE_DIFF);
    test_assert(ab_mean(baked, 20, 10, 76, 54) > 10);
    stbi_image_free(flat);
    ab_free(&plain, NULL);
    ab_free(&s, baked);
}

void AreaBake_render_origin_same_image(void) {
    // baked_lighting/area_grid
    /* The room drawn relative to a render origin that follows the ship matches the room drawn
     * without one, frame by frame, while the ship sails and turns. */
    ab_ship_t s[2] = {
        ab_ship(true, LightBakeModeBaked, AB_INSIDE), ab_ship(true, LightBakeModeBaked, AB_INSIDE)
    };
    unsigned char *px[2] = {0};
    ab_follow(&s[1]);
    ab_converge(&s[0]);
    ab_converge(&s[1]);
    for (int32_t i = 0; i <= AB_VOYAGE; i ++) {
        for (int32_t w = 0; w < 2; w ++) {
            ab_sail(&s[w], i);
            stbi_image_free(px[w]);
            px[w] = ab_frame(&s[w], w ? "area_follow1.png" : "area_follow0.png");
        }

        ab_expect_edges(px[0], px[1]);
    }

    ab_free(&s[0], px[0]);
    ab_free(&s[1], px[1]);
}

void AreaBake_ship_light_no_rebake(void) {
    // baked_lighting/area_grid
    /* A Mixed light the ship carries moves and turns with it: the probes keep their capture
     * while it sails, and a change of the intensity of the light bakes them again. */
    ab_ship_t s = ab_ship(true, LightBakeModeMixed, AB_INSIDE);
    ab_converge(&s);
    int32_t captures = ab_status(&s)->captures;
    test_assert(captures > 0);
    for (int32_t i = 1; i <= AB_VOYAGE; i ++) {
        ab_sail(&s, i);
        ecs_progress(s.world, AB_DELTA);
        box_render_sync(s.world);
        test_int(ab_status(&s)->captures, captures);
        test_bool(ab_status(&s)->converged, true);
    }

    ecs_set(s.world, s.light, PointLight, {.intensity = 12, .range = 8,
        .bake = LightBakeModeMixed});
    ab_converge(&s);
    ecs_progress(s.world, AB_DELTA);
    box_render_sync(s.world);
    ab_converge(&s);
    test_assert(ab_status(&s)->captures > captures);
    ab_free(&s, NULL);
}
