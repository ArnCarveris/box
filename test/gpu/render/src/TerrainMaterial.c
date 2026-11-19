/**
 * @file TerrainMaterial.c
 * @brief Tests for the masks of a Terrain on the GPU backend: the masks of a tile hold the values
 * of their graph, each layer of the TextureBlend shows where its channel is 1, tiles of adjacent
 * levels meet without a seam in the material, a triplanar 70 degree slope keeps the texel
 * density of a flat area, and the sample terrain of test/assets/terrain/materials.flecs renders.
 *
 * The scenes are made in C over flat or ramp heights, so the expected images are known; the
 * layers are flat colors or a checker made by texture graphs.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"
#include "terrain.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width in pixels of the images */
#define TM_WIDTH (128)

/** Height in pixels of the images */
#define TM_HEIGHT (96)

/** Seconds of a frame */
#define TM_DELTA (1.0f / 60.0f)

/** Frames a test waits at most for the tiles of a terrain */
#define TM_FRAMES (300)

/** Edge of a tile of level 0 in the scenes made in C */
#define TM_TILE (16.0f)

/** Half of the range of x and z the linear masks of mask_matches_graph map to 0..1 */
#define TM_SPAN (1000.0)

/** Tangent of 70 degrees, the slope of the face of triplanar_slope_density */
#define TM_TAN70 (2.7474774)

/** Height of the face of triplanar_slope_density in meters */
#define TM_FACE (60.0)

/** Create a headless world with the texture_blend, triplanar and texture_layers features, a
 * camera that is the focus, a view, ambient light and a sun from above, rendering on the main
 * thread into a capture surface.
 *
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* tm_world(
    ecs_entity_t *camera)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, Scene, {.texture_blend = true, .triplanar = true,
        .texture_layers = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.5f});
    *camera = ecs_entity(world, { .name = "camera" });
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 4000});
    ecs_add_id(world, *camera, WorldFocus);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = *camera,
        .clear = {.enabled = true, .color = {255, 0, 255, 255}}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 2.0f});
    ecs_entity_t capture = ecs_entity(world, { .name = "capture" });
    ecs_set(world, capture, Surface, {.width = TM_WIDTH, .height = TM_HEIGHT,
        .resolution_scale = 1});

    return world;
}

/** Make a node of a constant.
 *
 * @param world The world
 * @param r First channel
 * @param g Second channel
 * @param b Third channel
 * @param a Fourth channel
 * @return The node
 */
static ecs_entity_t tm_constant(
    ecs_world_t *world,
    float r,
    float g,
    float b,
    float a)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, TexConstant, {.value = {r, g, b, a}});

    return result;
}

/** Make a node of a channel of the world coordinates of a graph: 0 for x, 1 for z. TexCoords
 * on DomainWorld is scale times the meters from the origin of the window plus (scale - 1) times
 * that origin, so the coordinates of scale 2 minus those of scale 1 are the world position.
 *
 * @param world The world
 * @param channel The channel
 * @return The scalar node
 */
static ecs_entity_t tm_axis(
    ecs_world_t *world,
    TextureChannel channel)
{
    ecs_entity_t local = ecs_new(world), twice = ecs_new(world), coords = ecs_new(world);
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, local, TexCoords, {.scale = 1});
    ecs_set(world, twice, TexCoords, {.scale = 2});
    ecs_set(world, coords, TexSubtract, {.a = twice, .b = local});
    ecs_set(world, result, TexSplit, {.input = coords, .channel = channel});

    return result;
}

/** Make a node of the product of two nodes.
 *
 * @param world The world
 * @param a The first node
 * @param b The second node
 * @return The node
 */
static ecs_entity_t tm_mul(
    ecs_world_t *world,
    ecs_entity_t a,
    ecs_entity_t b)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, TexMultiply, {.a = a, .b = b});

    return result;
}

/** Make a node of one minus a node.
 *
 * @param world The world
 * @param a The node
 * @return The node
 */
static ecs_entity_t tm_invert(
    ecs_world_t *world,
    ecs_entity_t a)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, TexSubtract, {.a_value = {1, 1, 1, 1}, .b = a});

    return result;
}

/** Make a layer of a TextureBlend whose albedo is a flat color.
 *
 * @param world The world
 * @param r Red, linear 0..1
 * @param g Green
 * @param b Blue
 * @return The layer
 */
static ecs_entity_t tm_color_layer(
    ecs_world_t *world,
    float r,
    float g,
    float b)
{
    ecs_entity_t tex = ecs_new(world), layer = ecs_new(world);
    ecs_set(world, tex, TextureGraph, {.output = tm_constant(world, r, g, b, 1), .width = 4});
    ecs_set(world, layer, PbrTextures, {.albedo = tex});

    return layer;
}

/** Make a terrain of a height and a masks node.
 *
 * @param world The world
 * @param height The height node
 * @param masks The masks node
 * @param resolution Vertices along an edge of a tile
 * @return The terrain
 */
static ecs_entity_t tm_terrain(
    ecs_world_t *world,
    ecs_entity_t height,
    ecs_entity_t masks,
    int32_t resolution)
{
    ecs_entity_t result = ecs_entity(world, { .name = "terrain" });
    ecs_set(world, result, Terrain, {.height = height, .masks = masks, .tile_size = TM_TILE,
        .resolution = resolution, .levels = 4, .lod_distance = 48, .min_height = -10,
        .max_height = 80, .budget = 256});
    ecs_set(world, result, Rgb, {255, 255, 255});
    ecs_set(world, result, PbrMaterial, {.roughness = 1});

    return result;
}

/** Run frames until a terrain has no pending tile and draws at least one, then a few more so
 * the last tiles draw.
 *
 * @param world The world
 * @param terrain The terrain entity
 */
static void tm_settle(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    const TerrainStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TM_FRAMES; i ++) {
        ecs_progress(world, TM_DELTA);
        box_render_sync(world);
        status = ecs_get(world, terrain, TerrainStatus);
        if (status && !status->pending && status->drawn) {
            break;
        }
    }

    test_assert(status != NULL);
    if (status->pending || !status->drawn) {
        printf("pending %d, drawn %d\n", status->pending, status->drawn);
    }

    test_int(status->pending, 0);
    test_assert(status->drawn > 0);
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, TM_DELTA);
        box_render_sync(world);
    }
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* tm_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    ecs_set(world, e, Surface, {.width = TM_WIDTH, .height = TM_HEIGHT,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, TM_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, TM_WIDTH);
    test_int(h, TM_HEIGHT);
    ecs_os_free(path);

    return result;
}

/** Check that the backend logged no error.
 *
 * @param world The world
 */
static void tm_expect_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.errors, 0);
}

/** Get a pixel of an image.
 *
 * @param px The image
 * @param x Column
 * @param y Row
 * @return The pixel
 */
static const unsigned char* tm_px(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return &px[(y * TM_WIDTH + x) * 4];
}

/** Classify a pixel as one of the colors of layers_show_channels.
 *
 * @param p The pixel
 * @return 0 red, 1 green, 2 blue, 3 yellow, -1 none
 */
static int32_t tm_color_of(
    const unsigned char *p)
{
    int32_t r = p[0], g = p[1], b = p[2], result = -1;
    if (r > g + 60 && r > b + 60) {
        result = 0;
    } else if (g > r + 60 && g > b + 60) {
        result = 1;
    } else if (b > r + 60 && b > g + 60) {
        result = 2;
    } else if (r > b + 60 && g > b + 60 && abs(r - g) < 60) {
        result = 3;
    }

    return result;
}

/** Count the changes between dark and bright pixels along a line of an image, thresholded at
 * the mean luminance of the line.
 *
 * @param px The image
 * @param x0 Column of the first pixel
 * @param y0 Row of the first pixel
 * @param dx Column step
 * @param dy Row step
 * @param count Pixels along the line
 * @return The changes
 */
static int32_t tm_transitions(
    const unsigned char *px,
    int32_t x0,
    int32_t y0,
    int32_t dx,
    int32_t dy,
    int32_t count)
{
    int32_t i, result = 0;
    double mean = 0.0;
    for (i = 0; i < count; i ++) {
        const unsigned char *p = tm_px(px, x0 + i * dx, y0 + i * dy);
        mean += (double)(p[0] + p[1] + p[2]);
    }

    mean /= (double)count;
    bool last = false;
    for (i = 0; i < count; i ++) {
        const unsigned char *p = tm_px(px, x0 + i * dx, y0 + i * dy);
        bool bright = (double)(p[0] + p[1] + p[2]) > mean;
        result += i > 0 && bright != last;
        last = bright;
    }

    return result;
}

/** Run frames until the CPU copy of a texture is ready.
 *
 * @param world The world
 * @param tex The texture entity, with TextureReadback
 */
static void tm_readback_wait(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    const TextureReadbackStatus *status = NULL;
    int32_t i;
    for (i = 0; i < TM_FRAMES; i ++) {
        status = ecs_get(world, tex, TextureReadbackStatus);
        if (status && status->ready) {
            break;
        }

        ecs_progress(world, TM_DELTA);
        box_render_sync(world);
    }

    test_assert(status != NULL);
    test_bool(status->ready, true);
}

/** Compare the CPU copy of a masks image of a tile with the linear masks of
 * mask_matches_graph: red (x + TM_SPAN) / (2 TM_SPAN), green the same of z, blue 0.25, alpha 1.
 *
 * @param world The world
 * @param tex The texture entity
 * @param tolerance Largest difference of a channel
 * @return The largest difference
 */
static float tm_compare_linear(
    ecs_world_t *world,
    ecs_entity_t tex,
    float tolerance)
{
    const TextureGraph *graph = ecs_get(world, tex, TextureGraph);
    test_assert(graph != NULL);
    int32_t i, j, w = graph->width, h = graph->height;
    float worst = 0.0f;
    for (j = 0; j < h; j ++) {
        for (i = 0; i < w; i ++) {
            float v[4];
            test_bool(box_texture_readback_texel(world, tex, i, j, v), true);
            double x = graph->window.x + (double)i * graph->window.width / (double)(w - 1);
            double z = graph->window.z + (double)j * graph->window.depth / (double)(h - 1);
            float expect[4] = { (float)((x + TM_SPAN) / (2.0 * TM_SPAN)),
                (float)((z + TM_SPAN) / (2.0 * TM_SPAN)), 0.25f, 1.0f };
            int32_t c;
            for (c = 0; c < 4; c ++) {
                float d = fabsf(v[c] - expect[c]);
                worst = d > worst ? d : worst;
            }
        }
    }

    if (worst > tolerance) {
        printf("masks differ from the graph by %f\n", (double)worst);
    }

    test_assert(worst <= tolerance);

    return worst;
}

void TerrainMaterial_mask_matches_graph(void) {
    // terrain/terrain_material
    ecs_entity_t camera;
    ecs_world_t *world = tm_world(&camera);
    ecs_set(world, camera, Position, {0, 60, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_entity_t r = ecs_new(world), g = ecs_new(world), masks = ecs_new(world);
    ecs_set(world, r, TexRemap, {.input = tm_axis(world, ChannelR), .from_min = (float)-TM_SPAN,
        .from_max = (float)TM_SPAN, .to_min = 0, .to_max = 1});
    ecs_set(world, g, TexRemap, {.input = tm_axis(world, ChannelG), .from_min = (float)-TM_SPAN,
        .from_max = (float)TM_SPAN, .to_min = 0, .to_max = 1});
    ecs_set(world, masks, TexCombine, {.r = r, .g = g, .b_value = 0.25f, .a_value = 1});
    ecs_entity_t terrain = tm_terrain(world, tm_constant(world, 0, 0, 0, 0), masks, 17);
    ecs_progress(world, TM_DELTA);
    box_render_sync(world);

    int32_t count, i, checked = 0;
    box_terrain_tile_t *const *tiles = box_terrain_tiles(world, terrain, &count);
    test_assert(count > 0);
    for (i = 0; i < count; i ++) {
        test_assert(tiles[i]->masks != 0);
        ecs_add(world, tiles[i]->masks, TextureReadback);
        ecs_add(world, tiles[i]->coarse, TextureReadback);
    }

    tm_settle(world, terrain);
    tiles = box_terrain_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        const box_terrain_tile_t *t = tiles[i];
        if (t->level != 0 || !ecs_has(world, t->masks, TextureReadback)) {
            continue;
        }

        // Level 0 reads the node as it is; the coarse masks are a blur of a linear field,
        // which keeps it
        tm_readback_wait(world, t->masks);
        tm_readback_wait(world, t->coarse);
        tm_compare_linear(world, t->masks, 1.5f / 255.0f);
        tm_compare_linear(world, t->coarse, 3.0f / 255.0f);
        checked ++;
    }

    test_assert(checked > 0);
    tm_expect_no_errors(world);
    ecs_fini(world);
}

void TerrainMaterial_layers_show_channels(void) {
    // terrain/terrain_material
    ecs_entity_t camera;
    ecs_world_t *world = tm_world(&camera);
    ecs_set(world, camera, Position, {0, 120, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});

    // A quadrant per channel: red x < 0 and z < 0, green x >= 0 and z < 0, blue x < 0 and
    // z >= 0, alpha x >= 0 and z >= 0
    ecs_entity_t sx = ecs_new(world), sz = ecs_new(world), masks = ecs_new(world);
    ecs_set(world, sx, TexStep, {.input = tm_axis(world, ChannelR)});
    ecs_set(world, sz, TexStep, {.input = tm_axis(world, ChannelG)});
    ecs_entity_t nx = tm_invert(world, sx), nz = tm_invert(world, sz);
    ecs_set(world, masks, TexCombine, {.r = tm_mul(world, nx, nz), .g = tm_mul(world, sx, nz),
        .b = tm_mul(world, nx, sz), .a = tm_mul(world, sx, sz)});
    ecs_entity_t terrain = tm_terrain(world, tm_constant(world, 0, 0, 0, 0), masks, 33);
    ecs_set(world, terrain, TextureBlend, {.layers = {
        tm_color_layer(world, 1, 0, 0), tm_color_layer(world, 0, 1, 0),
        tm_color_layer(world, 0, 0, 1), tm_color_layer(world, 1, 1, 0)}});
    tm_settle(world, terrain);
    unsigned char *px = tm_frame(world, "terrain_material_channels.png");

    // The quadrants of the image show the four layers, the red and yellow ones diagonal, as
    // are the green and blue ones, whatever the orientation of the image
    int32_t tl = tm_color_of(tm_px(px, TM_WIDTH / 4, TM_HEIGHT / 4));
    int32_t tr = tm_color_of(tm_px(px, TM_WIDTH * 3 / 4, TM_HEIGHT / 4));
    int32_t bl = tm_color_of(tm_px(px, TM_WIDTH / 4, TM_HEIGHT * 3 / 4));
    int32_t br = tm_color_of(tm_px(px, TM_WIDTH * 3 / 4, TM_HEIGHT * 3 / 4));
    if (tl < 0 || tr < 0 || bl < 0 || br < 0) {
        printf("quadrants %d %d %d %d\n", tl, tr, bl, br);
    }

    test_assert(tl >= 0 && tr >= 0 && bl >= 0 && br >= 0);
    test_int((1 << tl) | (1 << tr) | (1 << bl) | (1 << br), 15);
    test_int((1 << tl) | (1 << br), tl == 0 || tl == 3 ? 9 : 6);

    // The tiles share one batch
    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    test_assert(status->drawn > 1);
    tm_expect_no_errors(world);
    stbi_image_free(px);
    ecs_fini(world);
}

void TerrainMaterial_levels_seamless(void) {
    // terrain/terrain_material
    ecs_entity_t camera;
    ecs_world_t *world = tm_world(&camera);
    ecs_set(world, camera, Position, {0, 150, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 90, .near_ = 0.5f, .far_ = 4000});

    // Smooth noise weighs a red and a green layer, linearly
    ecs_entity_t noise = ecs_new(world), masks = ecs_new(world);
    ecs_set(world, noise, TexNoise, {.frequency = 0.01f, .octaves = 1, .lacunarity = 2,
        .gain = 0.5f, .seed = 7});
    ecs_set(world, masks, TexCombine, {.r = noise, .g = tm_invert(world, noise)});
    ecs_entity_t terrain = tm_terrain(world, tm_constant(world, 0, 0, 0, 0), masks, 17);
    ecs_set(world, terrain, TextureBlend, {.layers = {
        tm_color_layer(world, 1, 0, 0), tm_color_layer(world, 0, 1, 0)}});
    tm_settle(world, terrain);
    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    int32_t levels = 0, i, x, y;
    for (i = 0; i < BOX_TERRAIN_LEVELS; i ++) {
        levels += status->tiles[i] > 0;
    }

    test_assert(levels >= 3);
    unsigned char *px = tm_frame(world, "terrain_material_levels.png");

    // No jump in the color between neighbouring pixels: the levels meet without a seam
    int32_t worst = 0, wx = 0, wy = 0;
    for (y = 0; y < TM_HEIGHT; y ++) {
        for (x = 0; x < TM_WIDTH; x ++) {
            const unsigned char *p = tm_px(px, x, y);
            test_assert(!(p[0] > 200 && p[1] < 80 && p[2] > 200));
            if (x + 1 < TM_WIDTH) {
                const unsigned char *q = tm_px(px, x + 1, y);
                int32_t d = abs(p[0] - q[0]) + abs(p[1] - q[1]);
                if (d > worst) {
                    worst = d;
                    wx = x;
                    wy = y;
                }
            }

            if (y + 1 < TM_HEIGHT) {
                const unsigned char *q = tm_px(px, x, y + 1);
                int32_t d = abs(p[0] - q[0]) + abs(p[1] - q[1]);
                if (d > worst) {
                    worst = d;
                    wx = x;
                    wy = y;
                }
            }
        }
    }

    if (worst > 48) {
        printf("color jumps by %d at %d, %d\n", worst, wx, wy);
    }

    test_assert(worst <= 48);
    tm_expect_no_errors(world);
    stbi_image_free(px);
    ecs_fini(world);
}

void TerrainMaterial_triplanar_slope_density(void) {
    // terrain/terrain_material
    ecs_entity_t camera;
    ecs_world_t *world = tm_world(&camera);

    // A face of 70 degrees rising from 0 to TM_FACE meters along +x from x 0, flat on both
    // sides
    ecs_entity_t height = ecs_new(world);
    ecs_set(world, height, TexRemap, {.input = tm_axis(world, ChannelR), .from_min = 0,
        .from_max = (float)(TM_FACE / TM_TAN70), .to_min = 0, .to_max = (float)TM_FACE,
        .clamp = true});

    // A checker of 8 by 8 squares over the image
    ecs_entity_t uv = ecs_new(world), scaled = ecs_new(world), cells = ecs_new(world);
    ecs_entity_t u = ecs_new(world), v = ecs_new(world), sum = ecs_new(world);
    ecs_entity_t half = ecs_new(world), parity = ecs_new(world), checker = ecs_new(world);
    ecs_entity_t color = ecs_new(world), tex = ecs_new(world), layer = ecs_new(world);
    ecs_set(world, uv, TexCoords, {.scale = 1});
    ecs_set(world, scaled, TexMultiply, {.a = uv, .b_value = {8, 8, 8, 8}});
    ecs_set(world, cells, TexFloor, {.input = scaled});
    ecs_set(world, u, TexSplit, {.input = cells, .channel = ChannelR});
    ecs_set(world, v, TexSplit, {.input = cells, .channel = ChannelG});
    ecs_set(world, sum, TexAdd, {.a = u, .b = v});
    ecs_set(world, half, TexMultiply, {.a = sum, .b_value = {0.5f, 0.5f, 0.5f, 0.5f}});
    ecs_set(world, parity, TexFract, {.input = half});
    ecs_set(world, checker, TexStep, {.input = parity,
        .edge_value = {0.25f, 0.25f, 0.25f, 0.25f}});
    ecs_set(world, color, TexMix, {.a_value = {0.05f, 0.05f, 0.05f, 1},
        .b_value = {0.9f, 0.9f, 0.9f, 1}, .factor = checker});
    ecs_set(world, tex, TextureGraph, {.output = color, .width = 256});
    ecs_set(world, layer, PbrTextures, {.albedo = tex});

    ecs_entity_t terrain = tm_terrain(world, height, tm_constant(world, 1, 0, 0, 0), 65);
    ecs_set(world, terrain, TextureBlend, {.layers = {layer}});
    ecs_set(world, terrain, TextureMapping, {.mode = MappingWorld, .size = 8, .blend = 0.3f});

    // Facing the middle of the face horizontally from 31 m
    ecs_set(world, camera, Position, {(float)(30.0 / TM_TAN70 - 31.0), 30, 0});
    ecs_set(world, camera, Rotation, {0, -(float)GLM_PI_2, 0});
    ecs_set(world, camera, Camera, {.fov = 20, .near_ = 0.5f, .far_ = 4000});
    tm_settle(world, terrain);
    unsigned char *face = tm_frame(world, "terrain_material_face.png");
    int32_t face_h = tm_transitions(face, TM_WIDTH / 4, TM_HEIGHT / 2, 1, 0, 64);
    int32_t face_v = tm_transitions(face, TM_WIDTH / 2, TM_HEIGHT / 2 - 32, 0, 1, 64);

    // Looking down on the flat ground west of the face from 31 m
    ecs_set(world, camera, Position, {-40, 31, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    tm_settle(world, terrain);
    unsigned char *flat = tm_frame(world, "terrain_material_flat.png");
    int32_t flat_h = tm_transitions(flat, TM_WIDTH / 4, TM_HEIGHT / 2, 1, 0, 64);
    int32_t flat_v = tm_transitions(flat, TM_WIDTH / 2, TM_HEIGHT / 2 - 32, 0, 1, 64);
    printf("transitions: face %d x %d, flat %d x %d\n", face_h, face_v, flat_h, flat_v);

    // The squares of the face are as large as those of the flat ground, and not stretched up
    // the face
    test_assert(face_h >= 3 && face_v >= 3 && flat_h >= 3 && flat_v >= 3);
    test_assert(face_v * 10 >= face_h * 7 && face_v * 7 <= face_h * 10);
    test_assert((face_h + face_v) * 10 >= (flat_h + flat_v) * 7);
    test_assert((face_h + face_v) * 7 <= (flat_h + flat_v) * 10);
    tm_expect_no_errors(world);
    stbi_image_free(face);
    stbi_image_free(flat);
    ecs_fini(world);
}

void TerrainMaterial_sample_renders(void) {
    // terrain/terrain_material
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t capture = ecs_entity(world, { .name = "capture" });
    ecs_set(world, capture, Surface, {.width = TM_WIDTH, .height = TM_HEIGHT,
        .resolution_scale = 1});
    char *path = box_app_locate("test/assets/terrain/materials.flecs");
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t terrain = ecs_lookup(world, "terrain_materials.land");
    test_assert(terrain != 0);
    tm_settle(world, terrain);
    unsigned char *px = tm_frame(world, "terrain_material_sample.png");

    // Grass, rock and something else show on the ground
    int32_t green = 0, gray = 0, ground = 0, i;
    for (i = 0; i < TM_WIDTH * TM_HEIGHT; i ++) {
        const unsigned char *p = &px[i * 4];
        bool sky = abs((int)p[0] - 20) <= 6 && abs((int)p[1] - 30) <= 6 &&
            abs((int)p[2] - 60) <= 6;
        ground += !sky;
        green += !sky && p[1] > p[0] + 12 && p[1] > p[2] + 12;
        gray += !sky && abs((int)p[0] - (int)p[1]) < 12 && abs((int)p[1] - (int)p[2]) < 12 &&
            p[0] > 40;
    }

    printf("ground %d, green %d, gray %d\n", ground, green, gray);
    test_assert(ground > TM_WIDTH * TM_HEIGHT / 3);
    test_assert(green * 50 > ground);
    test_assert(gray * 50 > ground);
    tm_expect_no_errors(world);
    stbi_image_free(px);
    ecs_fini(world);
}
