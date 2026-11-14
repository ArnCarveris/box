/**
 * @file TextureMappingFrame.c
 * @brief Tests for MappingFrame texture mapping: shapes under one TextureFrame show one
 * continuous texture that moves and turns with the frame.
 *
 * Each case renders a scene script headless and compares renders. A camera that is a child of
 * the frame moves with it, so a texture fixed to the frame renders the same. The textures are
 * emissive on a black, rough metal, so the light and shadows do not take part, except in the
 * alpha cutout case, whose shadow is what it tests.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"
#include "preprocess.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Side of the square surface in pixels */
#define MAPPING_FRAME_PX 128

/** Height of the ortho views in world units: 16 pixels per unit */
#define MAPPING_FRAME_HEIGHT 8.0f

/** Largest channel delta and pixel delta of tools/integration.py: a comparison fails when the
 * largest delta is above MAPPING_FRAME_MAX_DELTA and more than MAPPING_FRAME_MAX_PIXELS pixels
 * differ by more than MAPPING_FRAME_PIXEL_DELTA */
#define MAPPING_FRAME_MAX_DELTA 24
#define MAPPING_FRAME_PIXEL_DELTA 16
#define MAPPING_FRAME_MAX_PIXELS 200

/** Pixels that differ above which two renders show different textures */
#define MAPPING_FRAME_DIFFERENT_PIXELS 1000

/** Modules and materials of the scripts, formatted with the mapping mode */
#define MAPPING_FRAME_PRELUDE \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.material\n" \
    "using box.texture\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.light\n" \
    "prefab Hull {\n" \
    "  Rgb: {0, 0, 0}\n" \
    "  PbrMaterial: {metallic: 1, roughness: 1}\n" \
    "  Emissive: {strength: 1, color: {255, 255, 255}}\n" \
    "  PbrTextures: {emissive: checker_tex}\n" \
    "  texture.Mapping: {mode: %s, size: 3}\n" \
    "}\n"

/** The view of the scripts, after the scene, formatted with the camera */
#define MAPPING_FRAME_VIEW \
    "main_view {\n" \
    "  View: {camera: %s, clear: {enabled: true, color: {0, 0, 0, 255}}, " \
    "shadows: {enabled: true}}\n" \
    "}\n"

/** The camera of the ortho views from above, a child of the frame */
#define MAPPING_FRAME_TOP_CAMERA \
    "  camera {\n" \
    "    Position: {0, 10, 0}\n" \
    "    Rotation: {-1.5707963, 0, 0}\n" \
    "    Camera: {ortho: true, ortho_height: 8, near_: 0.1, far_: 100}\n" \
    "  }\n"

/** Where the frame of a render is put after the script ran */
typedef struct mapping_frame_move_t {
    float x;        /**< Position x */
    float z;        /**< Position z */
    float yaw;      /**< Rotation around y */
} mapping_frame_move_t;

/** Run a frame until a texture finished loading.
 *
 * @param world The world
 * @param tex The texture
 */
static void mapping_frame_wait(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus) &&
        ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
}

/** Render a scene script and load the render.
 *
 * The script has the prelude with the mapping mode, the scene, whose frame is the entity ship,
 * and the view through the camera. The texture entities checker_tex and fence_tex exist before
 * the script runs.
 *
 * @param mode The mapping mode of the Hull prefab, MappingFrame or MappingWorld
 * @param camera Path of the camera of the view
 * @param scene The scene
 * @param move Where the frame is put, NULL to leave it where the script puts it
 * @param file The file name of the render in the temporary folder
 * @return The RGBA8 pixels, MAPPING_FRAME_PX square, freed with stbi_image_free
 */
static unsigned char* mapping_frame_render(
    const char *mode,
    const char *camera,
    const char *scene,
    const mapping_frame_move_t *move,
    const char *file)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;

    char *checker = box_shader_locate("test/integration/visual/assets/checker.png");
    char *fence = box_shader_locate("test/integration/visual/assets/fence.png");
    test_assert(checker != NULL);
    test_assert(fence != NULL);
    ecs_entity_t checker_tex = ecs_entity(world, { .name = "checker_tex" });
    ecs_set(world, checker_tex, TexturePath, {.path = checker});
    ecs_entity_t fence_tex = ecs_entity(world, { .name = "fence_tex" });
    ecs_set(world, fence_tex, TexturePath, {.path = fence});

    char *prelude = flecs_asprintf(MAPPING_FRAME_PRELUDE, mode);
    char *view = flecs_asprintf(MAPPING_FRAME_VIEW, camera);
    char *code = flecs_asprintf("%s%s%s", prelude, scene, view);
    test_assert(ecs_script(world, { .code = code }) != 0);
    ecs_entity_t ship = ecs_lookup(world, "ship");
    test_assert(ship != 0);
    if (move) {
        ecs_set(world, ship, Position, {move->x, 0, move->z});
        ecs_set(world, ship, Rotation, {0, move->yaw, 0});
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = MAPPING_FRAME_PX, .height = MAPPING_FRAME_PX,
        .write_to_file = (char*)file, .resolution_scale = 1});

    ecs_progress(world, 0);
    mapping_frame_wait(world, checker_tex);
    mapping_frame_wait(world, fence_tex);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", tmp, file);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_int(w, MAPPING_FRAME_PX);
    test_int(h, MAPPING_FRAME_PX);

    ecs_os_free(path);
    ecs_os_free(code);
    ecs_os_free(view);
    ecs_os_free(prelude);
    ecs_os_free(checker);
    ecs_os_free(fence);
    ecs_fini(world);

    return img;
}

/** Pixel of a world position on the ground plane of the ortho view from above a frame at the
 * origin: x to the right, z down the image.
 *
 * @param world The world x or z
 * @return The pixel column or row
 */
static int32_t mapping_frame_pixel(
    float world)
{
    float per_unit = (float)MAPPING_FRAME_PX / MAPPING_FRAME_HEIGHT;

    return (int32_t)lroundf((float)MAPPING_FRAME_PX * 0.5f + world * per_unit);
}

/** Compare a rectangle of pixels of two renders.
 *
 * @param a The first render
 * @param b The second render
 * @param x0 First column
 * @param y0 First row
 * @param x1 Column past the last
 * @param y1 Row past the last
 * @param max Raised to the largest channel delta
 * @param count Incremented per pixel with a channel off by more than MAPPING_FRAME_PIXEL_DELTA
 */
static void mapping_frame_compare(
    const unsigned char *a,
    const unsigned char *b,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t *max,
    int32_t *count)
{
    int32_t x, y, c;
    for (y = y0; y < y1; y ++) {
        for (x = x0; x < x1; x ++) {
            int32_t i = (y * MAPPING_FRAME_PX + x) * 4, off = 0;
            for (c = 0; c < 3; c ++) {
                int32_t d = abs((int32_t)a[i + c] - (int32_t)b[i + c]);
                if (d > *max) {
                    *max = d;
                }

                if (d > off) {
                    off = d;
                }
            }

            if (off > MAPPING_FRAME_PIXEL_DELTA) {
                (*count) ++;
            }
        }
    }
}

/** Check that a render shows the checker: its green spans dark and light squares.
 *
 * @param img The render
 * @param x0 First column
 * @param y0 First row
 * @param x1 Column past the last
 * @param y1 Row past the last
 */
static void mapping_frame_textured(
    const unsigned char *img,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1)
{
    int32_t x, y, lo = 255, hi = 0;
    for (y = y0; y < y1; y ++) {
        for (x = x0; x < x1; x ++) {
            int32_t v = img[(y * MAPPING_FRAME_PX + x) * 4 + 1];
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
    }

    test_assert(hi - lo > 64);
}

/** Two touching boxes under a moved and turned frame, the second turned by 90 degrees and with
 * an origin of its own, render as one box of their total size under the same frame: the pixels
 * across the seam match, so neither box projects from its own origin. */
void TextureMappingFrame_touching_boxes_continuous(void) {
    // materials/texture_mapping_frame
    const char *pair =
        "ship {\n"
        "  texture.Frame\n"
        "  Position: {0.7, 0, 0.3}\n"
        "  Rotation: {0, 0.4, 0}\n"
        "  left : Hull { Position: {-1.5, 0.5, 0}\n Box: {3, 1, 6} }\n"
        "  right : Hull { Position: {1.5, 0.5, 0}\n Rotation: {0, 1.5707963, 0}\n"
        "    Box: {6, 1, 3} }\n"
        "}\n"
        "top_camera {\n"
        "  Position: {0, 10, 0}\n"
        "  Rotation: {-1.5707963, 0, 0}\n"
        "  Camera: {ortho: true, ortho_height: 8, near_: 0.1, far_: 100}\n"
        "}\n";
    const char *single =
        "ship {\n"
        "  texture.Frame\n"
        "  Position: {0.7, 0, 0.3}\n"
        "  Rotation: {0, 0.4, 0}\n"
        "  whole : Hull { Position: {0, 0.5, 0}\n Box: {6, 1, 6} }\n"
        "}\n"
        "top_camera {\n"
        "  Position: {0, 10, 0}\n"
        "  Rotation: {-1.5707963, 0, 0}\n"
        "  Camera: {ortho: true, ortho_height: 8, near_: 0.1, far_: 100}\n"
        "}\n";
    unsigned char *a = mapping_frame_render("MappingFrame", "top_camera", pair, NULL,
        "mapping_frame_pair.png");
    unsigned char *b = mapping_frame_render("MappingFrame", "top_camera", single, NULL,
        "mapping_frame_single.png");

    /* A square of 2 units around the center of the frame, inside the turned 6 x 6 footprint */
    int32_t x0 = mapping_frame_pixel(0.7f - 2.0f), x1 = mapping_frame_pixel(0.7f + 2.0f);
    int32_t y0 = mapping_frame_pixel(0.3f - 2.0f), y1 = mapping_frame_pixel(0.3f + 2.0f);
    int32_t max = 0, count = 0;
    mapping_frame_textured(b, x0, y0, x1, y1);
    mapping_frame_compare(a, b, x0, y0, x1, y1, &max, &count);
    test_assert(max <= MAPPING_FRAME_MAX_DELTA || count <= MAPPING_FRAME_MAX_PIXELS);

    stbi_image_free(a);
    stbi_image_free(b);
}

/** A frame with touching boxes and the camera as children renders the same after the frame is
 * moved and turned: the texture moves and turns with the frame. The same scene with MappingWorld
 * renders another texture once moved, so the comparison sees the mapping. */
void TextureMappingFrame_moved_frame_matches(void) {
    // materials/texture_mapping_frame
    const char *scene =
        "ship {\n"
        "  texture.Frame\n"
        MAPPING_FRAME_TOP_CAMERA
        "  a : Hull { Position: {-1.5, 0.5, -1}\n Box: {3, 1, 4} }\n"
        "  b : Hull { Position: {1.5, 0.5, -1}\n Rotation: {0, 1.5707963, 0}\n"
        "    Box: {4, 1, 3} }\n"
        "  c : Hull { Position: {0, 0.5, 2}\n Rotation: {0, 3.1415927, 0}\n"
        "    Box: {6, 1, 2} }\n"
        "}\n";
    mapping_frame_move_t moved = { .x = 2.3f, .z = -1.1f, .yaw = 0.7f };
    unsigned char *still = mapping_frame_render("MappingFrame", "ship.camera", scene, NULL,
        "mapping_frame_still.png");
    unsigned char *turned = mapping_frame_render("MappingFrame", "ship.camera", scene,
        &moved, "mapping_frame_turned.png");
    unsigned char *world = mapping_frame_render("MappingWorld", "ship.camera", scene,
        &moved, "mapping_frame_world_turned.png");

    int32_t first = mapping_frame_pixel(-3) + 1, last = mapping_frame_pixel(3) - 1;
    int32_t max = 0, count = 0;
    mapping_frame_textured(still, first, first, last, last);
    mapping_frame_compare(still, turned, 0, 0, MAPPING_FRAME_PX, MAPPING_FRAME_PX,
        &max, &count);
    test_assert(max <= MAPPING_FRAME_MAX_DELTA || count <= MAPPING_FRAME_MAX_PIXELS);

    max = 0;
    count = 0;
    mapping_frame_compare(still, world, first, first, last, last, &max, &count);
    test_assert(count > MAPPING_FRAME_DIFFERENT_PIXELS);

    stbi_image_free(still);
    stbi_image_free(turned);
    stbi_image_free(world);
}

/** Parametric surfaces project from the frame too: a wedge (ProfileExtrusion) and a Loft under a
 * frame with the camera render the same after the frame is moved and turned. */
void TextureMappingFrame_surface_moved_frame_matches(void) {
    // materials/texture_mapping_frame
    const char *scene =
        "wedge_shape { Path2D: {start: {0, 0}, segments: [{kind: Line, end: {3, 0}}, "
        "{kind: Line, end: {0, 3}}], closed: true} }\n"
        "wedge_profile { Profile2D: {outer: wedge_shape} }\n"
        "square_shape { Path2D: {start: {-1, -1}, segments: [{kind: Line, end: {1, -1}}, "
        "{kind: Line, end: {1, 1}}, {kind: Line, end: {-1, 1}}], closed: true} }\n"
        "square_profile { Profile2D: {outer: square_shape} }\n"
        "ship {\n"
        "  texture.Frame\n"
        "  camera {\n"
        "    Position: {0, 7, 7}\n"
        "    Rotation: {-0.7853982, 0, 0}\n"
        "    Camera: {ortho: true, ortho_height: 8, near_: 0.1, far_: 100}\n"
        "  }\n"
        "  wedge : Hull { Position: {-3, 0, -1}\n"
        "    ProfileExtrusion: {profile: wedge_profile, depth: 1.5, cap_start: true, "
        "cap_end: true} }\n"
        "  loft : Hull { Position: {1.5, 0, 0}\n"
        "    Loft: {sections: [{profile: square_profile, offset: {0, 0, 0}, "
        "scale: {1, 1}}, {profile: square_profile, offset: {0, 2, 0}, "
        "scale: {0.6, 0.6}}], "
        "cap_start: true, cap_end: true} }\n"
        "}\n";
    mapping_frame_move_t moved = { .x = -1.7f, .z = 2.4f, .yaw = -0.9f };
    unsigned char *still = mapping_frame_render("MappingFrame", "ship.camera", scene, NULL,
        "mapping_frame_surface_still.png");
    unsigned char *turned = mapping_frame_render("MappingFrame", "ship.camera", scene,
        &moved, "mapping_frame_surface_turned.png");

    int32_t max = 0, count = 0;
    mapping_frame_textured(still, 0, 0, MAPPING_FRAME_PX, MAPPING_FRAME_PX);
    mapping_frame_compare(still, turned, 0, 0, MAPPING_FRAME_PX, MAPPING_FRAME_PX,
        &max, &count);
    test_assert(max <= MAPPING_FRAME_MAX_DELTA || count <= MAPPING_FRAME_MAX_PIXELS);

    stbi_image_free(still);
    stbi_image_free(turned);
}

/** An alpha tested lattice panel with MappingFrame casts the holes it shows onto a floor: with
 * the panel, the floor and the camera under the frame and the sun fixed in the world, the
 * render is the same after the frame moves, holes of the shadow included, while the same scene
 * with MappingWorld shows other holes once moved. */
void TextureMappingFrame_cutout_shadow_follows_frame(void) {
    // materials/texture_mapping_frame
    const char *scene =
        "$ { Scene: {alpha_cutout: true} }\n"
        "sun {\n"
        "  Rotation: {0.7, -0.4, 0}\n"
        "  DirectionalLight: {intensity: 1.5 * flecs.script.math.PI}\n"
        "  Rgb: {255, 245, 230}\n"
        "}\n"
        "ship {\n"
        "  texture.Frame\n"
        "  camera {\n"
        "    Position: {0, 2.8, 7.5}\n"
        "    Rotation: {-0.27, 0, 0}\n"
        "    Camera: {fov: 50}\n"
        "  }\n"
        "  floor { Position: {0, -0.1, 0}\n Box: {16, 0.2, 10}\n Rgb: {176, 180, 166} }\n"
        "  panel {\n"
        "    Position: {0, 0.8, -0.8}\n"
        "    Box: {3, 1.6, 0.06}\n"
        "    Rgb: {235, 215, 190}\n"
        "    PbrTextures: {albedo: fence_tex}\n"
        "    texture.Mapping: {mode: %s, size: 1.6}\n"
        "    AlphaCutoff: {value: 0.5}\n"
        "  }\n"
        "}\n";
    mapping_frame_move_t moved = { .x = 0.55f, .z = -0.35f, .yaw = 0 };
    char frame_scene[2048], world_scene[2048];
    snprintf(frame_scene, sizeof(frame_scene), scene, "MappingFrame");
    snprintf(world_scene, sizeof(world_scene), scene, "MappingWorld");
    unsigned char *still = mapping_frame_render("MappingFrame", "ship.camera", frame_scene,
        NULL, "mapping_frame_cutout_still.png");
    unsigned char *moved_frame = mapping_frame_render("MappingFrame", "ship.camera",
        frame_scene, &moved, "mapping_frame_cutout_moved.png");
    unsigned char *moved_world = mapping_frame_render("MappingFrame", "ship.camera",
        world_scene, &moved, "mapping_frame_cutout_world.png");

    int32_t max = 0, count = 0;
    mapping_frame_compare(still, moved_frame, 0, 0, MAPPING_FRAME_PX, MAPPING_FRAME_PX,
        &max, &count);
    test_assert(max <= MAPPING_FRAME_MAX_DELTA || count <= MAPPING_FRAME_MAX_PIXELS);

    max = 0;
    count = 0;
    mapping_frame_compare(still, moved_world, 0, 0, MAPPING_FRAME_PX, MAPPING_FRAME_PX,
        &max, &count);
    test_assert(count > MAPPING_FRAME_MAX_PIXELS);

    stbi_image_free(still);
    stbi_image_free(moved_frame);
    stbi_image_free(moved_world);
}

/** A frame scaled by 10 with a MappingFrame box, a MappingFrame plate under an extra scale and a
 * turned MappingObject box renders, from an ortho view 10 times as high, as the unscaled frame:
 * the textures scale with the frame. The same scene with MappingWorld renders another texture
 * once scaled, so the comparison sees the scale. */
void TextureMappingFrame_scaled_frame_matches(void) {
    // materials/texture_mapping_frame
    const char *scene =
        "ship {\n"
        "  texture.Frame\n"
        "  Scale: {%g, %g, %g}\n"
        "  a : Hull { Position: {-1.5, 0.5, -1}\n Box: {3, 1, 4} }\n"
        "  stretched {\n"
        "    Position: {1.5, 0, -1}\n"
        "    Scale: {1.5, 1, 1}\n"
        "    b : Hull { Position: {0, 0.5, 0}\n Box: {2, 1, 4} }\n"
        "  }\n"
        "  ring {\n"
        "    Position: {0, 0.5, 2}\n"
        "    Rotation: {0, 0.6, 0}\n"
        "    Box: {4, 1, 1.5}\n"
        "    Rgb: {0, 0, 0}\n"
        "    PbrMaterial: {metallic: 1, roughness: 1}\n"
        "    Emissive: {strength: 1, color: {255, 255, 255}}\n"
        "    PbrTextures: {emissive: checker_tex}\n"
        "    texture.Mapping: {mode: MappingObject, size: 1.5}\n"
        "  }\n"
        "}\n"
        "top_camera {\n"
        "  Position: {0, %g, 0}\n"
        "  Rotation: {-1.5707963, 0, 0}\n"
        "  Camera: {ortho: true, ortho_height: %g, near_: 0.1, far_: %g}\n"
        "}\n";
    char unscaled[2048], scaled[2048];
    snprintf(unscaled, sizeof(unscaled), scene, 1.0, 1.0, 1.0, 10.0, 8.0, 100.0);
    snprintf(scaled, sizeof(scaled), scene, 10.0, 10.0, 10.0, 100.0, 80.0, 1000.0);
    unsigned char *still = mapping_frame_render("MappingFrame", "top_camera", unscaled, NULL,
        "mapping_frame_unscaled.png");
    unsigned char *large = mapping_frame_render("MappingFrame", "top_camera", scaled, NULL,
        "mapping_frame_scaled.png");
    unsigned char *world_still = mapping_frame_render("MappingWorld", "top_camera", unscaled,
        NULL, "mapping_frame_world_unscaled.png");
    unsigned char *world_large = mapping_frame_render("MappingWorld", "top_camera", scaled,
        NULL, "mapping_frame_world_scaled.png");

    int32_t first = mapping_frame_pixel(-3) + 1, last = mapping_frame_pixel(3) - 1;
    int32_t max = 0, count = 0;
    mapping_frame_textured(still, first, first, last, last);
    mapping_frame_compare(still, large, 0, 0, MAPPING_FRAME_PX, MAPPING_FRAME_PX,
        &max, &count);
    test_assert(max <= MAPPING_FRAME_MAX_DELTA || count <= MAPPING_FRAME_MAX_PIXELS);

    max = 0;
    count = 0;
    mapping_frame_compare(world_still, world_large, first, first, last, last, &max, &count);
    test_assert(count > MAPPING_FRAME_DIFFERENT_PIXELS);

    stbi_image_free(still);
    stbi_image_free(large);
    stbi_image_free(world_still);
    stbi_image_free(world_large);
}
