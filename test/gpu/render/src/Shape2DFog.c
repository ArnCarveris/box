/**
 * @file Shape2DFog.c
 * @brief Tests that world 2D shapes are seen through the fog of the area they are in, as the
 * meshes next to them are, with the GPU backend.
 *
 * A camera in a fogged VisArea looks at four quads at the same distance: on the left two thin
 * mesh boxes, on the right two Rects. The top pair is black and unlit, no light and no ambient
 * reaching it, so its pixels are the light the fog scatters in alone. The bottom pair is white,
 * the box emissive over a black albedo, so its pixels also show how much the fog takes away.
 * Each Rect matches the box beside it.
 *
 * The scenes are seen into a 96x64 image.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width and height in pixels of the images */
#define S2F_WIDTH (96)
#define S2F_HEIGHT (64)

/** Seconds of a frame */
#define S2F_DELTA (1.0f / 60.0f)

/** Frames rendered before an image is checked */
#define S2F_FRAMES (3)

/** Local X and Z of the far scene, whole units so its f32 positions are exact */
#define S2F_FAR_X (100000.0f)
#define S2F_FAR_Z (-100000.0f)

/** Distance of the quads in front of the camera */
#define S2F_DISTANCE (8.0f)

/** X and Y of the centers of the quads off the line of sight: the meshes left, the Rects
 * right, the black ones up and the white ones down */
#define S2F_SIDE (1.5f)
#define S2F_ROW (1.25f)

/** Size of the quads */
#define S2F_QUAD (2.0f)

/** Fog of the room: color and density */
#define S2F_FOG_COLOR (0.5f)
#define S2F_FOG_DENSITY (0.1f)

/** Largest channel difference between a Rect and the mesh beside it */
#define S2F_CLOSE (3)

/** Least channel value of the black Rect through the fog: it scatters about 144 in */
#define S2F_INSCATTER (60)

/** Largest channel value of the white Rect through the fog: it keeps about 223 */
#define S2F_EXTINCT (245)

/** Where a quad of the scene is */
typedef enum s2f_quad_t {
    S2F_MESH_BLACK,  /**< The black mesh box, top left */
    S2F_RECT_BLACK,  /**< The black Rect, top right */
    S2F_MESH_WHITE,  /**< The emissive mesh box, bottom left */
    S2F_RECT_WHITE   /**< The white Rect, bottom right */
} s2f_quad_t;

/** Add a quad to a scene.
 *
 * @param world The world
 * @param x X of the scene
 * @param z Z of the scene
 * @param quad Which quad
 */
static void s2f_quad(
    ecs_world_t *world,
    float x,
    float z,
    s2f_quad_t quad)
{
    bool rect = quad == S2F_RECT_BLACK || quad == S2F_RECT_WHITE;
    bool white = quad == S2F_MESH_WHITE || quad == S2F_RECT_WHITE;
    float side = rect ? S2F_SIDE : -S2F_SIDE;
    float row = white ? -S2F_ROW : S2F_ROW;
    // The Rect lies where the front face of the box is, so both are as far from the eye
    float depth = rect ? S2F_DISTANCE : S2F_DISTANCE + 0.01f;
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x + side, row, z - depth});
    if (rect) {
        ecs_set(world, e, Rect, {S2F_QUAD, S2F_QUAD});
        uint8_t level = white ? 255 : 0;
        ecs_set(world, e, Rgb, {level, level, level});
    } else {
        ecs_set(world, e, Box, {S2F_QUAD, S2F_QUAD, 0.02f});
        ecs_set(world, e, Rgb, {0, 0, 0});
        ecs_set(world, e, PbrMaterial, {.roughness = 1});
        if (white) {
            ecs_set(world, e, Emissive, {.strength = 1, .color = {255, 255, 255}});
        }
    }
}

/** Create a headless world that renders on the main thread: a fogged VisArea without ambient
 * around a camera, the four quads in front of it and a lit view of black.
 *
 * @param x X of the scene
 * @param z Z of the scene
 * @return The world
 */
static ecs_world_t* s2f_world(
    float x,
    float z)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});

    // The outline of the room is in absolute coordinates, so it is written out at the scene
    char *script = flecs_asprintf(
        "using box.portal\n"
        "room {\n"
        "  VisArea: {shape: [{%f, %f}, {%f, %f}, {%f, %f}, {%f, %f}], y: -5, height: 10,\n"
        "    ambient: {0, 0, 0}, fog: {%f, %f, %f, %f}}\n"
        "}\n",
        (double)(x - 10), (double)(z - 12), (double)(x + 10), (double)(z - 12),
        (double)(x + 10), (double)(z + 4), (double)(x - 10), (double)(z + 4),
        (double)S2F_FOG_COLOR, (double)S2F_FOG_COLOR, (double)S2F_FOG_COLOR,
        (double)S2F_FOG_DENSITY);
    test_int(ecs_script_run(world, "room", script, NULL), 0);
    ecs_os_free(script);

    s2f_quad(world, x, z, S2F_MESH_BLACK);
    s2f_quad(world, x, z, S2F_RECT_BLACK);
    s2f_quad(world, x, z, S2F_MESH_WHITE);
    s2f_quad(world, x, z, S2F_RECT_WHITE);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {x, 0, z});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->lighting.enabled = true;
    ecs_modified(world, view, View);

    return world;
}

/** Add a HUD over a world: a scene of its own with an orthographic camera and a lit-less view
 * drawn after the others, and a white 10x10 Rect in the middle of the image.
 *
 * @param world The world
 */
static void s2f_hud(
    ecs_world_t *world)
{
    ecs_entity_t hud = ecs_new(world);
    ecs_set(world, hud, Scene, {0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .order = 1});
    ecs_entity_t rect = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {10, 10});
    ecs_set(world, rect, Rgb, {255, 255, 255});
}

/** Render S2F_FRAMES frames into an image file of the temporary folder and load the last one.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* s2f_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_entity(world, { .name = "capture" });
    ecs_set(world, e, Surface, {.width = S2F_WIDTH, .height = S2F_HEIGHT,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    for (int32_t i = 0; i < S2F_FRAMES; i ++) {
        ecs_progress(world, S2F_DELTA);
        box_render_sync(world);
    }

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, S2F_WIDTH);
    test_int(h, S2F_HEIGHT);
    ecs_os_free(path);

    return result;
}

/** Get the green channel at the center of a quad in an image, from the vertical field of view
 * of 60 degrees of the camera.
 *
 * @param px The image
 * @param quad Which quad
 * @return The channel, 0 to 255
 */
static int32_t s2f_at(
    const unsigned char *px,
    s2f_quad_t quad)
{
    bool rect = quad == S2F_RECT_BLACK || quad == S2F_RECT_WHITE;
    bool white = quad == S2F_MESH_WHITE || quad == S2F_RECT_WHITE;
    float scale = (S2F_HEIGHT * 0.5f) / (tanf(glm_rad(30.0f)) * S2F_DISTANCE);
    int32_t x = (int32_t)(S2F_WIDTH * 0.5f + (rect ? S2F_SIDE : -S2F_SIDE) * scale);
    int32_t y = (int32_t)(S2F_HEIGHT * 0.5f + (white ? S2F_ROW : -S2F_ROW) * scale);

    return px[(y * S2F_WIDTH + x) * 4 + 1];
}

/** Check that the Rects of an image are fogged as the meshes beside them: the black one shows
 * the light scattered in, the white one is dimmed, and each is within S2F_CLOSE of its mesh.
 *
 * @param px The image
 */
static void s2f_expect_fogged(
    const unsigned char *px)
{
    int32_t mesh_black = s2f_at(px, S2F_MESH_BLACK);
    int32_t rect_black = s2f_at(px, S2F_RECT_BLACK);
    int32_t mesh_white = s2f_at(px, S2F_MESH_WHITE);
    int32_t rect_white = s2f_at(px, S2F_RECT_WHITE);
    printf("black: mesh %d, rect %d; white: mesh %d, rect %d\n", mesh_black, rect_black,
        mesh_white, rect_white);

    test_assert(mesh_black >= S2F_INSCATTER);
    test_assert(mesh_white <= S2F_EXTINCT);
    test_assert(rect_black >= S2F_INSCATTER);
    test_assert(rect_white <= S2F_EXTINCT);
    test_assert(abs(rect_black - mesh_black) <= S2F_CLOSE);
    test_assert(abs(rect_white - mesh_white) <= S2F_CLOSE);
}

void Shape2DFog_area_fog_like_mesh(void) {
    // ui/shapes_2d
    /* A black and a white Rect in a fogged room look as the black and the emissive white mesh
     * beside them at the same distance: the fog of the room scatters light in front of both
     * and dims what is behind it alike. */
    ecs_world_t *world = s2f_world(0, 0);
    unsigned char *px = s2f_frame(world, "s2f_near.png");
    s2f_expect_fogged(px);
    stbi_image_free(px);
    ecs_fini(world);
}

void Shape2DFog_area_fog_render_origin(void) {
    // render/world_origin
    /* The same room a hundred thousand units from the local origin, the render origin
     * following the camera: the Rects are fogged as the meshes, and as at the local origin. */
    ecs_world_t *near = s2f_world(0, 0);
    ecs_world_t *far = s2f_world(S2F_FAR_X, S2F_FAR_Z);
    ecs_singleton_set(far, WorldOrigin, {.enabled = false, .render = true});
    unsigned char *a = s2f_frame(near, "s2f_near.png");
    unsigned char *b = s2f_frame(far, "s2f_far.png");
    s2f_expect_fogged(b);
    test_assert(abs(s2f_at(a, S2F_RECT_BLACK) - s2f_at(b, S2F_RECT_BLACK)) <= S2F_CLOSE);
    test_assert(abs(s2f_at(a, S2F_RECT_WHITE) - s2f_at(b, S2F_RECT_WHITE)) <= S2F_CLOSE);
    stbi_image_free(a);
    stbi_image_free(b);
    ecs_fini(near);
    ecs_fini(far);
}

void Shape2DFog_hud_unfogged(void) {
    // ui/shapes_2d
    /* A white Rect of a HUD, a scene of its own seen by an orthographic view, stays white over
     * the fogged room: the fog of the areas belongs to the view of the room alone. */
    ecs_world_t *world = s2f_world(0, 0);
    s2f_hud(world);
    unsigned char *px = s2f_frame(world, "s2f_hud.png");
    const unsigned char *center = &px[((S2F_HEIGHT / 2) * S2F_WIDTH + S2F_WIDTH / 2) * 4];
    printf("hud: %d %d %d\n", center[0], center[1], center[2]);
    test_int(center[0], 255);
    test_int(center[1], 255);
    test_int(center[2], 255);
    stbi_image_free(px);
    ecs_fini(world);
}
