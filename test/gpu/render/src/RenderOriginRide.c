/**
 * @file RenderOriginRide.c
 * @brief Tests that the shapes of every kind under a moving mover draw with its pose of the same
 * frame, with the GPU backend: a mover that a system moves and turns every frame in EcsOnUpdate
 * carries a camera, a 3D box (a mover batch, placed on the GPU from the mover table), a 2D rect
 * and a text (per-entity dynamic slots). Seen from the camera, which rides the mover, nothing
 * moves, so every frame shows each layer where a frame of the mover standing still does.
 *
 * A layer placed with the pose of an earlier frame than the mover table lags behind by one step
 * of the mover, so it stands off its place against the box.
 *
 * The frames compared render back to back on the render thread, each extracted while the one
 * before it renders, as a windowed run does, each into its own 96x96 image, unlit, with the
 * render origin off and following the camera (WorldOrigin.render).
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

/* The capture queue of the surface module (src/surface/surface.h) */
int box_surface_capture_frame(
    ecs_world_t *world,
    ecs_entity_t surface,
    const char *path);

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width and height in pixels of the images */
#define RORI_SIZE (96)

/** Seconds of a frame */
#define RORI_DELTA (1.0f / 60.0f)

/** Distance the mover travels along X every frame, in units */
#define RORI_STEP (0.35f)

/** Angle the mover turns about Y every frame, in radians */
#define RORI_TURN (0.04f)

/** Frames the mover moves, each rendered and waited for, before the frames compared */
#define RORI_WARMUP (3)

/** Frames rendered back to back and compared with the first of them */
#define RORI_FRAMES (6)

/** Largest distance in pixels the center of a layer may be from where the still frame shows it */
#define RORI_MAX_SHIFT (0.35)

/** Pixels a layer covers at least */
#define RORI_CONTENT (40)

/** The layers of the scene, told apart by their colors */
typedef enum rori_layer_t {
    RoriBox,    /**< The red 3D box, a mover batch */
    RoriRect,   /**< The green 2D rect */
    RoriText,   /**< The white text */
    RoriLayers  /**< Count of layers */
} rori_layer_t;

/** Names of the layers, for the messages */
static const char *rori_names[RoriLayers] = { "box", "rect", "text" };

/** The center of a layer in an image */
typedef struct rori_center_t {
    double x;      /**< Mean column of its pixels */
    double y;      /**< Mean row of its pixels */
    int32_t count; /**< Its pixels */
} rori_center_t;

/** Move and turn the mover, the entity of the system context, every frame.
 *
 * @param it The system iterator
 */
static void rori_move(
    ecs_iter_t *it)
{
    ecs_entity_t mover = (ecs_entity_t)(uintptr_t)it->ctx;
    const Position *p = ecs_get(it->world, mover, Position);
    const Rotation *r = ecs_get(it->world, mover, Rotation);
    ecs_set(it->world, mover, Position, {p->x + RORI_STEP, p->y, p->z});
    ecs_set(it->world, mover, Rotation, {r->x, r->y + RORI_TURN, r->z});
}

/** Create a headless world that renders on the render thread without lighting, with a mover that
 * moves every frame carrying a camera, a red box, a green rect and a white text in front of it.
 *
 * @param render Whether the render origin follows the camera (WorldOrigin.render)
 * @return The world
 */
static ecs_world_t* rori_world(
    bool render)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = false});
    ecs_singleton_set(world, WorldOrigin, {.enabled = false, .render = render});

    ecs_entity_t mover = ecs_entity(world, { .name = "mover" });
    ecs_set(world, mover, Position, {10, 0, 0});
    ecs_set(world, mover, Rotation, {0, 0, 0});
    ecs_add(world, mover, DynamicTransform);

    ecs_entity_t box = ecs_new_w_pair(world, EcsChildOf, mover);
    ecs_set(world, box, Position, {-0.6f, 0, -4});
    ecs_set(world, box, Box, {1, 1, 1});
    ecs_set(world, box, Rgb, {255, 0, 0});

    ecs_entity_t rect = ecs_new_w_pair(world, EcsChildOf, mover);
    ecs_set(world, rect, Position, {0.6f, 0.3f, -3.5f});
    ecs_set(world, rect, Rect, {0.8f, 0.5f});
    ecs_set(world, rect, Rgb, {0, 255, 0});

    ecs_entity_t label = ecs_new_w_pair(world, EcsChildOf, mover);
    ecs_set(world, label, Position, {0.6f, -0.5f, -3.5f});
    ecs_set(world, label, Text, {.value = "HHH", .size = 0.35f,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, label, Rgb, {255, 255, 255});

    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, mover);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->lighting.enabled = false;
    ecs_modified(world, view, View);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RoriMove" }),
        .phase = EcsOnUpdate,
        .callback = rori_move,
        .ctx = (void*)(uintptr_t)mover
    });

    return world;
}

/** Load an image file of the temporary folder.
 *
 * @param world The world
 * @param file The image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* rori_load(
    ecs_world_t *world,
    const char *file)
{
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, RORI_SIZE);
    test_int(h, RORI_SIZE);
    ecs_os_free(path);

    return result;
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* rori_frame(
    ecs_world_t *world)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = RORI_SIZE, .height = RORI_SIZE,
        .write_to_file = "rori.png", .resolution_scale = 1});
    ecs_progress(world, RORI_DELTA);
    box_render_sync(world);

    return rori_load(world, "rori.png");
}

/** Find the layer of a pixel from its color.
 *
 * @param c The RGBA pixel
 * @return The layer, RoriLayers for the background or an edge
 */
static rori_layer_t rori_layer_of(
    const unsigned char *c)
{
    rori_layer_t result = RoriLayers;
    if (c[0] > 200 && c[1] > 200 && c[2] > 200) {
        result = RoriText;
    } else if (c[0] > 150 && c[1] < 60 && c[2] < 60) {
        result = RoriBox;
    } else if (c[1] > 150 && c[0] < 60 && c[2] < 60) {
        result = RoriRect;
    }

    return result;
}

/** Find the center of every layer in an image.
 *
 * @param px The image
 * @param out Receives the centers, RoriLayers of them
 */
static void rori_centers(
    const unsigned char *px,
    rori_center_t *out)
{
    int32_t x, y, l;
    ecs_os_memset_n(out, 0, rori_center_t, RoriLayers);
    for (y = 0; y < RORI_SIZE; y ++) {
        for (x = 0; x < RORI_SIZE; x ++) {
            rori_layer_t layer = rori_layer_of(&px[(y * RORI_SIZE + x) * 4]);
            if (layer != RoriLayers) {
                out[layer].x += x;
                out[layer].y += y;
                out[layer].count ++;
            }
        }
    }

    for (l = 0; l < RoriLayers; l ++) {
        if (out[l].count) {
            out[l].x /= out[l].count;
            out[l].y /= out[l].count;
        }
    }
}

/** Render a frame of the mover standing still, then move the mover for some frames and render
 * RORI_FRAMES frames back to back, the render thread working on a frame while the next one is
 * extracted, each into its own image: every layer stays where the still frame shows it, within
 * RORI_MAX_SHIFT pixels.
 *
 * @param render Whether the render origin follows the camera
 */
static void rori_expect_steady(
    bool render)
{
    ecs_world_t *world = rori_world(render);
    ecs_entity_t move = ecs_lookup(world, "RoriMove");
    rori_center_t still[RoriLayers], cur[RoriLayers];
    char files[RORI_FRAMES][32];
    int32_t i, l;
    bool steady = true;
    ecs_enable(world, move, false);
    stbi_image_free(rori_frame(world));
    unsigned char *px = rori_frame(world);
    rori_centers(px, still);
    stbi_image_free(px);
    for (l = 0; l < RoriLayers; l ++) {
        if (still[l].count < RORI_CONTENT) {
            printf("%s covers %d pixels\n", rori_names[l], still[l].count);
        }

        test_assert(still[l].count >= RORI_CONTENT);
    }

    ecs_enable(world, move, true);
    for (i = 0; i < RORI_WARMUP; i ++) {
        stbi_image_free(rori_frame(world));
    }

    ecs_entity_t surface = ecs_lookup(world, "capture");
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    for (i = 0; i < RORI_FRAMES; i ++) {
        ecs_os_snprintf(files[i], 32, "rori_%d.png", i);
        char *path = flecs_asprintf("%s/%s", tmp, files[i]);
        test_int(box_surface_capture_frame(world, surface, path), 0);
        ecs_os_free(path);
    }

    for (i = 0; i < RORI_FRAMES; i ++) {
        ecs_progress(world, RORI_DELTA);
    }

    box_render_sync(world);
    for (i = 0; i < RORI_FRAMES; i ++) {
        px = rori_load(world, files[i]);
        rori_centers(px, cur);
        stbi_image_free(px);
        for (l = 0; l < RoriLayers; l ++) {
            double dx = cur[l].x - still[l].x, dy = cur[l].y - still[l].y;
            double shift = sqrt(dx * dx + dy * dy);
            if (cur[l].count < RORI_CONTENT || shift > RORI_MAX_SHIFT) {
                printf("frame %d: %s moved %.2f pixels (%.2f, %.2f), %d pixels\n", i,
                    rori_names[l], shift, dx, dy, cur[l].count);
                steady = false;
            }
        }
    }

    test_assert(steady);
    ecs_fini(world);
}

void RenderOriginRide_layers_steady(void) {
    // render/world_origin
    /* A camera rides a mover that moves and turns every frame, with the render origin off: the
     * 3D box, the 2D rect and the text in front of it stay put in the image. */
    rori_expect_steady(false);
}

void RenderOriginRide_layers_steady_render_origin(void) {
    // render/world_origin
    /* The same with the render origin following the camera (WorldOrigin.render): the layers
     * stay put. */
    rori_expect_steady(true);
}
