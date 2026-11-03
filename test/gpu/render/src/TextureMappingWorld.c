/**
 * @file TextureMappingWorld.c
 * @brief Tests for MappingWorld texture mapping: touching meshes show one continuous texture.
 *
 * A 3 x 3 grid of touching boxes of different sizes, some turned by 90 degrees around y, with
 * MappingWorld and a checker texture, seen from above in an ortho view, renders the seams
 * between the boxes as a single box of the same total size does.
 *
 * The texture is emissive on a black, rough metal so the light and shadows do not take part.
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
#define MAPPING_WORLD_PX 128

/** Height of the ortho view in world units: 16 pixels per unit */
#define MAPPING_WORLD_HEIGHT 8.0f

/** Largest channel delta and pixel delta of tools/integration.py: a comparison fails when the
 * largest delta is above MAPPING_WORLD_MAX_DELTA and more than MAPPING_WORLD_MAX_PIXELS pixels
 * differ by more than MAPPING_WORLD_PIXEL_DELTA */
#define MAPPING_WORLD_MAX_DELTA 24
#define MAPPING_WORLD_PIXEL_DELTA 16
#define MAPPING_WORLD_MAX_PIXELS 200

/** Pixels on each side of a seam that are compared */
#define MAPPING_WORLD_BAND 4

/** Widths of the columns (x) and depths of the rows (z) of the grid, 6 x 6 units in total,
 * starting at -3 */
static const float mapping_world_widths[3] = { 1, 2, 3 };
static const float mapping_world_depths[3] = { 3, 1, 2 };

/** Create a box with MappingWorld and the emissive checker.
 *
 * @param world The world
 * @param tex The checker texture
 * @param x The x of its center
 * @param z The z of its center
 * @param width Its extent along x in the world
 * @param depth Its extent along z in the world
 * @param turned Whether it is turned by 90 degrees around y, its Box swapping width and depth
 * @return The box
 */
static ecs_entity_t mapping_world_box(
    ecs_world_t *world,
    ecs_entity_t tex,
    float x,
    float z,
    float width,
    float depth,
    bool turned)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0.5f, z});
    if (turned) {
        ecs_set(world, e, Rotation, {0, (float)M_PI * 0.5f, 0});
        ecs_set(world, e, Box, {depth, 1, width});
    } else {
        ecs_set(world, e, Box, {width, 1, depth});
    }

    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, e, PbrTextures, {.emissive = tex});
    ecs_set(world, e, TextureMapping, {.mode = MappingWorld, .size = 3});
    ecs_set(world, e, TextureWrap, {WrapRepeat});

    return e;
}

/** Render the grid of touching boxes, or a single box of the same total size, from above in an
 * ortho view, and load the render.
 *
 * @param grid Whether to render the grid, else the single box
 * @param file The file name of the render in the temporary folder
 * @return The RGBA8 pixels, MAPPING_WORLD_PX square, freed with stbi_image_free
 */
static unsigned char* mapping_world_render(
    bool grid,
    const char *file)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;

    char *checker = box_shader_locate("test/integration/visual/assets/checker.png");
    test_assert(checker != NULL);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = checker});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)M_PI * 0.5f, 0, 0});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = MAPPING_WORLD_HEIGHT,
        .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});

    if (grid) {
        int32_t row, col, k = 0;
        float z = -3;
        for (row = 0; row < 3; row ++) {
            float x = -3;
            float depth = mapping_world_depths[row];
            for (col = 0; col < 3; col ++) {
                float width = mapping_world_widths[col];
                mapping_world_box(world, tex, x + width * 0.5f, z + depth * 0.5f, width, depth,
                    (k % 2) == 1);
                x += width;
                k ++;
            }

            z += depth;
        }
    } else {
        mapping_world_box(world, tex, 0, 0, 6, 6, false);
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = MAPPING_WORLD_PX, .height = MAPPING_WORLD_PX,
        .write_to_file = (char*)file, .resolution_scale = 1});

    int32_t frames = 0;
    ecs_progress(world, 0);
    while (ecs_get(world, tex, TextureStatus) &&
        ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", tmp, file);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_int(w, MAPPING_WORLD_PX);
    test_int(h, MAPPING_WORLD_PX);

    ecs_os_free(path);
    ecs_os_free(checker);
    ecs_fini(world);

    return img;
}

/** Pixel of a world position on the ground plane of the ortho view from above: x to the right,
 * z down the image.
 *
 * @param world The world x or z
 * @return The pixel column or row
 */
static int32_t mapping_world_pixel(
    float world)
{
    float per_unit = (float)MAPPING_WORLD_PX / MAPPING_WORLD_HEIGHT;

    return (int32_t)lroundf((float)MAPPING_WORLD_PX * 0.5f + world * per_unit);
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
 * @param count Incremented per pixel with a channel off by more than MAPPING_WORLD_PIXEL_DELTA
 */
static void mapping_world_compare(
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
            int32_t i = (y * MAPPING_WORLD_PX + x) * 4, off = 0;
            for (c = 0; c < 3; c ++) {
                int32_t d = abs((int32_t)a[i + c] - (int32_t)b[i + c]);
                if (d > *max) {
                    *max = d;
                }

                if (d > off) {
                    off = d;
                }
            }

            if (off > MAPPING_WORLD_PIXEL_DELTA) {
                (*count) ++;
            }
        }
    }
}

/** The pixels on both sides of every seam of the grid match the single box, and so does the
 * whole footprint, so no box shows a texture of its own. The checker shows on the single box:
 * its red and its light squares, far apart in green, both inside its footprint. */
void TextureMappingWorld_grid_matches_single_box(void) {
    // materials/texture_mapping_world
    unsigned char *grid = mapping_world_render(true, "mapping_world_grid.png");
    unsigned char *single = mapping_world_render(false, "mapping_world_single.png");

    int32_t x, y, lo = 255, hi = 0;
    int32_t first = mapping_world_pixel(-3) + 1, last = mapping_world_pixel(3) - 1;
    for (y = first; y < last; y ++) {
        for (x = first; x < last; x ++) {
            int32_t v = single[(y * MAPPING_WORLD_PX + x) * 4 + 1];
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
    }

    test_assert(hi - lo > 64);

    int32_t max = 0, count = 0, k;
    float seam_x = -3, seam_z = -3;
    for (k = 0; k < 2; k ++) {
        seam_x += mapping_world_widths[k];
        seam_z += mapping_world_depths[k];
        int32_t sx = mapping_world_pixel(seam_x);
        int32_t sz = mapping_world_pixel(seam_z);
        mapping_world_compare(grid, single, sx - MAPPING_WORLD_BAND, first,
            sx + MAPPING_WORLD_BAND, last, &max, &count);
        mapping_world_compare(grid, single, first, sz - MAPPING_WORLD_BAND,
            last, sz + MAPPING_WORLD_BAND, &max, &count);
    }

    test_assert(max <= MAPPING_WORLD_MAX_DELTA || count <= MAPPING_WORLD_MAX_PIXELS);

    max = 0;
    count = 0;
    mapping_world_compare(grid, single, first, first, last, last, &max, &count);
    test_assert(max <= MAPPING_WORLD_MAX_DELTA || count <= MAPPING_WORLD_MAX_PIXELS);

    stbi_image_free(grid);
    stbi_image_free(single);
}
