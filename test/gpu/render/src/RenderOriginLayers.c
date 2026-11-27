/**
 * @file RenderOriginLayers.c
 * @brief Tests that the coplanar layers of a 2D tree far from the local origin keep their order,
 * with the GPU backend: a mover several hundred units out carries a screen, a dark background
 * Rect with a band Rect over it in its plane, and a camera that rides the mover, seen at a
 * grazing angle from a short distance. The mover, heeled and trimmed a little, sails a step and
 * rolls a little every frame, and the camera heaves against it by a fraction of a millimetre, as
 * a walker on a ship does.
 *
 * The layers of a tree are told apart only by their rank (ranked_depth of shape2d.wgsl). A layer
 * placed from its own f32 world transform out there stands off the layer under it by the rounding
 * of that transform along the normal, tens of micrometres, more than the rank lift at a grazing
 * angle, so the background wins over the band, or over a wedge of it where the planes cross. The
 * layers of a group are placed on the plane of its anchor (render/plane.c) to keep their order.
 *
 * Every frame is compared with the same scene at the local origin: no pixel the band covers
 * there shows the background far out. The scenes are seen into 128x128 images, unlit, with the
 * narrow field of view that gives the pixel size of a full window, with the render origin off
 * and following the camera (WorldOrigin.render).
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
#define ROL_SIZE (128)

/** Seconds of a frame */
#define ROL_DELTA (1.0f / 60.0f)

/** Position of the mover of the far scene, some 460 units from the local origin, where f32
 * positions are 1/32768 of a unit apart */
#define ROL_FAR_X (380.37f)
#define ROL_FAR_Y (1.71f)
#define ROL_FAR_Z (-263.93f)

/** Heading of the mover about Y, its pitch and its roll, in radians: a ship that heels and trims
 * a little, so the screen faces no axis and its world transform rounds along its normal */
#define ROL_HEADING (0.7f)
#define ROL_PITCH (0.035f)
#define ROL_ROLL (0.05f)

/** Roll the mover gains every frame, in radians, so the rounding changes from frame to frame */
#define ROL_ROLLING (0.0003f)

/** Distance the mover sails along X every frame, in units */
#define ROL_SAIL (0.05f)

/** Width and height of the background of the screen and the height of its band, in units, as
 * PortalScreen and PortalScreenHeader lay them out */
#define ROL_SCREEN_WIDTH (1.92f)
#define ROL_SCREEN_HEIGHT (0.72f)
#define ROL_BAND_HEIGHT (0.068f)

/** Distance of the camera from the center of the band and its angle from the normal of the
 * screen, in radians (65 degrees) */
#define ROL_DISTANCE (1.5f)
#define ROL_GRAZE (1.1345f)

/** Vertical field of view of the camera in degrees, which gives a 128 pixel image the pixel
 * size of a 70 degree view over 1300 pixels */
#define ROL_FOV (6.0f)

/** Frames of a test */
#define ROL_FRAMES (32)

/** Pixels of the band that may show the background in a frame, the anti-aliased edge */
#define ROL_MAX_LOST (2)

/** Pixels the band covers at least */
#define ROL_CONTENT (1500)

/** Worlds a test renders side by side, the near scene and the far one */
#define ROL_WORLDS (2)

/** The pixels of an image, told apart by their colors */
typedef enum rol_layer_t {
    RolBand,       /**< The green band */
    RolBackground, /**< The blue background */
    RolOther       /**< The clear color or an edge */
} rol_layer_t;

/** The scene of a world */
typedef struct rol_scene_t {
    ecs_world_t *world;  /**< The world */
    ecs_entity_t mover;  /**< The mover that carries the screen and the camera */
    ecs_entity_t camera; /**< The camera */
    float x;             /**< Position of the mover along X at the first frame */
    float y;             /**< Position of the mover along Y */
    float z;             /**< Position of the mover along Z */
} rol_scene_t;

/** Create a headless world that renders on the main thread without lighting, with a mover that
 * carries a screen, a blue background Rect with a green band Rect over it in its plane, and a
 * camera that looks at the band from ROL_DISTANCE at ROL_GRAZE from its normal.
 *
 * @param render Whether the render origin follows the camera (WorldOrigin.render)
 * @param x Position of the mover along X
 * @param y Position of the mover along Y
 * @param z Position of the mover along Z
 * @return The scene
 */
static rol_scene_t rol_world(
    bool render,
    float x,
    float y,
    float z)
{
    rol_scene_t result = { .x = x, .y = y, .z = z };
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, WorldOrigin, {.enabled = false, .render = render});

    ecs_entity_t mover = ecs_entity(world, { .name = "mover" });
    ecs_set(world, mover, Position, {x, y, z});
    ecs_set(world, mover, Rotation, {ROL_PITCH, ROL_HEADING, ROL_ROLL});
    ecs_add(world, mover, DynamicTransform);

    ecs_entity_t screen = ecs_entity(world, { .name = "screen", .parent = mover });
    ecs_set(world, screen, Position, {0.4f, 1.35f, 0.21f});
    ecs_set(world, screen, Rect, {ROL_SCREEN_WIDTH, ROL_SCREEN_HEIGHT});
    ecs_set(world, screen, Rgb, {0, 0, 255});

    float band_y = (ROL_SCREEN_HEIGHT - ROL_BAND_HEIGHT) * 0.5f;
    ecs_entity_t band = ecs_entity(world, { .name = "band", .parent = screen });
    ecs_set(world, band, Position, {0, band_y, 0});
    ecs_set(world, band, Rect, {ROL_SCREEN_WIDTH, ROL_BAND_HEIGHT});
    ecs_set(world, band, Rgb, {0, 255, 0});

    float s = sinf(ROL_GRAZE), c = cosf(ROL_GRAZE);
    ecs_entity_t camera = ecs_entity(world, { .name = "camera", .parent = mover });
    ecs_set(world, camera, Position, {0.4f + ROL_DISTANCE * s, 1.35f + band_y,
        0.21f + ROL_DISTANCE * c});
    ecs_set(world, camera, Rotation, {0, ROL_GRAZE, 0});
    ecs_set(world, camera, Camera, {.fov = ROL_FOV, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->lighting.enabled = false;
    ecs_modified(world, view, View);

    result.world = world;
    result.mover = mover;
    result.camera = camera;

    return result;
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* rol_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = ROL_SIZE, .height = ROL_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, ROL_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, ROL_SIZE);
    test_int(h, ROL_SIZE);
    ecs_os_free(path);

    return result;
}

/** Find the layer of a pixel from its color.
 *
 * @param px The image
 * @param i The pixel
 * @return The layer, RolOther for the clear color or an edge
 */
static rol_layer_t rol_layer_of(
    const unsigned char *px,
    int32_t i)
{
    const unsigned char *c = &px[i * 4];
    rol_layer_t result = RolOther;
    if (c[1] > 200 && c[0] < 40 && c[2] < 40) {
        result = RolBand;
    } else if (c[2] > 200 && c[0] < 40 && c[1] < 40) {
        result = RolBackground;
    }

    return result;
}

/** Heave of the camera against the mover at a frame, between 0.2 and 1.7 millimetres up or down
 * from the frame before, as a walker on a ship heaves.
 *
 * @param frame The frame
 * @return The offset of the camera along Y from its place, in units
 */
static float rol_heave(
    int32_t frame)
{
    float result = 0;
    int32_t i;
    for (i = 1; i <= frame; i ++) {
        float step = 0.0002f + 0.0015f * (float)((i * 7) % 11) / 10.0f;
        if (i % 3 == 0) {
            step = -step;
        }

        result += step;
    }

    return result;
}

/** Sail and roll the mover of a scene to a frame and heave its camera.
 *
 * @param scene The scene
 * @param frame The frame
 * @param camera The place of the camera on the mover
 */
static void rol_move(
    rol_scene_t *scene,
    int32_t frame,
    const Position *camera)
{
    ecs_set(scene->world, scene->mover, Position, {
        scene->x + ROL_SAIL * (float)frame, scene->y, scene->z});
    ecs_set(scene->world, scene->mover, Rotation, {
        ROL_PITCH, ROL_HEADING, ROL_ROLL + ROL_ROLLING * (float)frame});
    ecs_set(scene->world, scene->camera, Position, {
        camera->x, camera->y + rol_heave(frame), camera->z});
}

/** Render ROL_FRAMES frames of the screen near the local origin and far from it, the mover
 * sailing and the camera heaving: in every frame, at most ROL_MAX_LOST pixels that show the band
 * near the origin show the background far from it.
 *
 * @param render Whether the render origin follows the camera
 */
static void rol_expect_steady(
    bool render)
{
    static const char *files[ROL_WORLDS] = { "rol0.png", "rol1.png" };
    rol_scene_t scenes[ROL_WORLDS] = {
        rol_world(render, 0.37f, ROL_FAR_Y, 0.07f),
        rol_world(render, ROL_FAR_X, ROL_FAR_Y, ROL_FAR_Z)
    };
    Position camera = *ecs_get(scenes[0].world, scenes[0].camera, Position);
    int32_t frame, w, i, worst = 0, failed = 0;
    for (frame = 0; frame < ROL_FRAMES; frame ++) {
        unsigned char *px[ROL_WORLDS];
        for (w = 0; w < ROL_WORLDS; w ++) {
            rol_move(&scenes[w], frame, &camera);
            px[w] = rol_frame(scenes[w].world, files[w]);
        }

        int32_t band = 0, lost = 0;
        for (i = 0; i < ROL_SIZE * ROL_SIZE; i ++) {
            if (rol_layer_of(px[0], i) == RolBand) {
                band ++;
                lost += rol_layer_of(px[1], i) == RolBackground;
            }
        }

        if (band < ROL_CONTENT) {
            printf("frame %d: the band covers %d pixels\n", frame, band);
        }

        test_assert(band >= ROL_CONTENT);
        if (lost > ROL_MAX_LOST) {
            printf("frame %d: %d of %d band pixels show the background\n", frame, lost, band);
            failed ++;
        }

        worst = lost > worst ? lost : worst;
        for (w = 0; w < ROL_WORLDS; w ++) {
            stbi_image_free(px[w]);
        }
    }

    if (failed) {
        printf("%d of %d frames lost band pixels, %d at most\n", failed, ROL_FRAMES, worst);
    }

    test_int(failed, 0);
    for (w = 0; w < ROL_WORLDS; w ++) {
        ecs_fini(scenes[w].world);
    }
}

void RenderOriginLayers_coplanar_steady(void) {
    // render/world_origin
    /* A screen of a background Rect and a band Rect in its plane rides a mover 460 units from
     * the local origin, seen at a grazing angle by a camera that rides the mover and heaves
     * against it, with the render origin off: the band stays over the background in every
     * frame. */
    rol_expect_steady(false);
}

void RenderOriginLayers_coplanar_steady_render_origin(void) {
    // render/world_origin
    /* The same with the render origin following the camera (WorldOrigin.render): the band
     * stays over the background. */
    rol_expect_steady(true);
}
