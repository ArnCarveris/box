/**
 * @file ImageSlice.c
 * @brief ImageSlice rendered: the corners of test/integration/visual/assets/frame.png keep their
 * size on elements of any size, the edges stretch or repeat their stripes and a hollow frame
 * shows what is behind its center. The texture of an offscreen Surface slices at its size in
 * pixels too.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** The frame texture, from the repository root */
#define SLICE_FRAME "test/integration/visual/assets/frame.png"

/** Side of the surface in pixels, one point per pixel */
#define SLICE_SIZE (192)

/** Texels of the side of the frame texture */
#define SLICE_FRAME_TEXELS (48)

/** Inset of the corners of the frame texture in texels */
#define SLICE_INSET (16)

/** Texels of a stripe of an edge of the frame texture, which repeats every two */
#define SLICE_STRIPE (4)

/** Create a headless world with an orthographic camera of one point per pixel, a view that
 * clears to a color and a square surface that writes a file every frame.
 *
 * @param file The file of the surface
 * @param clear The clear color
 * @return The world
 */
static ecs_world_t* slice_world(
    const char *file,
    rgba clear)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true, .color = clear}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = SLICE_SIZE, .height = SLICE_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    return world;
}

/** Create the frame texture and wait until it is loaded.
 *
 * @param world The world
 * @return The texture entity
 */
static ecs_entity_t slice_texture(
    ecs_world_t *world)
{
    ecs_entity_t tex = ecs_new(world);
    int32_t frames = 0;
    ecs_set(world, tex, TexturePath, {.path = SLICE_FRAME});
    ecs_progress(world, 0);
    while (frames < 100 && ecs_get(world, tex, TextureStatus)->state == TextureLoading) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, tex, Texture)->width, SLICE_FRAME_TEXELS);
    test_int(ecs_get(world, tex, Texture)->height, SLICE_FRAME_TEXELS);

    return tex;
}

/** Create a sliced frame at the origin.
 *
 * @param world The world
 * @param tex The frame texture
 * @param width Width of the element
 * @param height Height of the element
 * @param slice The ImageSlice
 * @return The element
 */
static ecs_entity_t slice_panel(
    ecs_world_t *world,
    ecs_entity_t tex,
    float width,
    float height,
    const ImageSlice *slice)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 1});
    ecs_set(world, e, Rect, {width, height});
    ecs_set(world, e, Image, {.texture = tex, .u1 = 1, .v1 = 1});
    ecs_set_ptr(world, e, ImageSlice, slice);

    return e;
}

/** Render frames and load the image a surface wrote.
 *
 * @param world The world
 * @param file The file of the surface
 * @return The pixels, RGBA, free with stbi_image_free
 */
static unsigned char* slice_render(
    ecs_world_t *world,
    const char *file)
{
    int32_t i;
    int w = 0, h = 0, n = 0;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, SLICE_SIZE);
    test_int(h, SLICE_SIZE);

    return px;
}

/** Get a pixel of a rendered image.
 *
 * @param px The pixels
 * @param x The column
 * @param y The row, from the top
 * @return The pixel
 */
static const unsigned char* slice_at(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return &px[(y * SLICE_SIZE + x) * 4];
}

/** Check whether two colors are within a tolerance on every channel.
 *
 * @param a The first color
 * @param b The second color
 * @param tolerance Largest difference of a channel
 * @return Whether they match
 */
static bool slice_same(
    const unsigned char *a,
    const unsigned char *b,
    int32_t tolerance)
{
    int32_t i;
    bool result = true;
    for (i = 0; i < 3; i ++) {
        result = result && abs((int32_t)a[i] - (int32_t)b[i]) <= tolerance;
    }

    return result;
}

/** Get the pixel at an offset into a corner of a centered element.
 *
 * @param px The pixels
 * @param width Width of the element
 * @param height Height of the element
 * @param corner 0 top left, 1 top right, 2 bottom left, 3 bottom right
 * @param dx Pixels in from the side of the corner
 * @param dy Pixels in from the top or bottom of the corner
 * @return The pixel
 */
static const unsigned char* slice_corner(
    const unsigned char *px,
    int32_t width,
    int32_t height,
    int32_t corner,
    int32_t dx,
    int32_t dy)
{
    int32_t left = SLICE_SIZE / 2 - width / 2, top = SLICE_SIZE / 2 - height / 2;
    int32_t x = (corner & 1) ? left + width - 1 - dx : left + dx;
    int32_t y = (corner & 2) ? top + height - 1 - dy : top + dy;

    return slice_at(px, x, y);
}

/** Measure the runs of the stripes along a row of the top edge of a centered element: the
 * pixels of the stripe color (low green) and of the white between them.
 *
 * The texture is filtered in linear light, so a pixel is of the stripe below the display green
 * of half way between the two in linear light (188), which splits the filtered seams evenly.
 *
 * @param px The pixels
 * @param width Width of the element
 * @param height Height of the element
 * @param corner Width of a corner in pixels
 * @param runs Receives the lengths of the runs, at most 64
 * @return The number of runs
 */
static int32_t slice_runs(
    const unsigned char *px,
    int32_t width,
    int32_t height,
    int32_t corner,
    int32_t *runs)
{
    int32_t left = SLICE_SIZE / 2 - width / 2 + corner;
    int32_t right = SLICE_SIZE / 2 + width / 2 - corner;
    int32_t y = SLICE_SIZE / 2 - height / 2 + corner / 2;
    int32_t x, count = 0;
    bool on = false;
    for (x = left; x < right; x ++) {
        bool stripe = slice_at(px, x, y)[1] < 188;
        if (x == left || stripe != on) {
            test_assert(count < 64);
            runs[count ++] = 0;
            on = stripe;
        }

        runs[count - 1] ++;
    }

    return count;
}

/** An ImageSlice of the frame texture.
 *
 * @param scale The scale
 * @param edges The fill of the edges
 * @return The value
 */
static ImageSlice slice_frame(
    float scale,
    SliceFill edges)
{
    return (ImageSlice){ .left = SLICE_INSET, .top = SLICE_INSET, .right = SLICE_INSET,
        .bottom = SLICE_INSET, .scale = scale, .edges = edges };
}

void ImageSlice_corners_keep_size(void) {
    // ui/image_slice
    ImageSlice slice = slice_frame(1, SliceStretch);
    int32_t sizes[2][2] = {{64, 64}, {160, 96}};
    unsigned char *px[2];
    int32_t i, c, dx, dy;
    int fw = 0, fh = 0, fn = 0;
    unsigned char *frame = NULL;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = slice_world("slice_corners.png", (rgba){0, 0, 0, 255});
        if (!frame) {
            /* box_app_locate allocates with the OS API of flecs, set up by a world */
            char *file = box_app_locate(SLICE_FRAME);
            frame = stbi_load(file, &fw, &fh, &fn, 4);
            ecs_os_free(file);
            test_assert(frame != NULL);
            test_int(fw, SLICE_FRAME_TEXELS);
        }

        slice_panel(world, slice_texture(world), (float)sizes[i][0], (float)sizes[i][1],
            &slice);
        px[i] = slice_render(world, "slice_corners.png");
        ecs_fini(world);
    }

    /* Inside each corner, away from the filtered seam with the edges, the pixels are the same
     * at both sizes and are the texels of the corner of the frame at a scale of 1 */
    for (c = 0; c < 4; c ++) {
        for (dy = 1; dy < SLICE_INSET - 2; dy ++) {
            for (dx = 1; dx < SLICE_INSET - 2; dx ++) {
                const unsigned char *a = slice_corner(px[0], sizes[0][0], sizes[0][1], c, dx, dy);
                const unsigned char *b = slice_corner(px[1], sizes[1][0], sizes[1][1], c, dx, dy);
                int32_t tx = (c & 1) ? SLICE_FRAME_TEXELS - 1 - dx : dx;
                int32_t ty = (c & 2) ? SLICE_FRAME_TEXELS - 1 - dy : dy;
                const unsigned char *t = &frame[(ty * SLICE_FRAME_TEXELS + tx) * 4];
                test_assert(slice_same(a, b, 1));
                test_assert(slice_same(a, t, 3));
            }
        }
    }

    stbi_image_free(frame);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

void ImageSlice_edges_stretch(void) {
    // ui/image_slice
    ImageSlice slice = slice_frame(2, SliceStretch);
    int32_t widths[2] = {96, 160}, runs[64];
    int32_t i, r;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = slice_world("slice_stretch.png", (rgba){0, 0, 0, 255});
        slice_panel(world, slice_texture(world), (float)widths[i], 96, &slice);
        unsigned char *px = slice_render(world, "slice_stretch.png");

        /* The 16 texels of the edge are two stripes and two gaps at any width: four runs of a
         * quarter of the span each */
        int32_t span = widths[i] - 4 * SLICE_INSET;
        int32_t count = slice_runs(px, widths[i], 96, 2 * SLICE_INSET, runs);
        test_int(count, 4);
        for (r = 0; r < count; r ++) {
            test_assert(abs(runs[r] - span / 4) <= 1);
        }

        stbi_image_free(px);
        ecs_fini(world);
    }
}

void ImageSlice_edges_tile(void) {
    // ui/image_slice
    ImageSlice slice = slice_frame(2, SliceTile);
    int32_t widths[2] = {96, 160}, runs[64];
    int32_t i, r;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = slice_world("slice_tile.png", (rgba){0, 0, 0, 255});
        slice_panel(world, slice_texture(world), (float)widths[i], 96, &slice);
        unsigned char *px = slice_render(world, "slice_tile.png");

        /* The stripes keep their 4 texels at a scale of 2, 8 pixels, at any width: the span of
         * 32 or 96 pixels holds 4 or 12 runs */
        int32_t span = widths[i] - 4 * SLICE_INSET;
        int32_t count = slice_runs(px, widths[i], 96, 2 * SLICE_INSET, runs);
        test_int(count, span / (2 * SLICE_STRIPE));
        for (r = 0; r < count; r ++) {
            test_assert(abs(runs[r] - 2 * SLICE_STRIPE) <= 1);
        }

        stbi_image_free(px);
        ecs_fini(world);
    }
}

void ImageSlice_hollow_center(void) {
    // ui/image_slice
    rgba clear = {0, 160, 0, 255};
    ImageSlice slice = slice_frame(1, SliceStretch);
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = slice_world("slice_hollow.png", clear);
        slice.hollow = i == 1;
        slice_panel(world, slice_texture(world), 96, 96, &slice);
        unsigned char *px = slice_render(world, "slice_hollow.png");
        const unsigned char *center = slice_at(px, SLICE_SIZE / 2, SLICE_SIZE / 2);
        const unsigned char *inside = slice_at(px, SLICE_SIZE / 2 - 20, SLICE_SIZE / 2 + 15);
        const unsigned char *edge = slice_at(px, SLICE_SIZE / 2, SLICE_SIZE / 2 - 48 + 8);
        const unsigned char *corner = slice_at(px, SLICE_SIZE / 2 - 48 + 8,
            SLICE_SIZE / 2 - 48 + 8);
        unsigned char green[3] = {clear.r, clear.g, clear.b};
        if (slice.hollow) {
            /* The background shows through the center */
            test_assert(slice_same(center, green, 3));
            test_assert(slice_same(inside, green, 3));
        } else {
            /* The gray checker of the center covers it */
            test_assert(center[0] > 60 && abs(center[0] - center[1]) < 4);
            test_assert(abs(inside[0] - inside[1]) < 4);
        }

        /* The frame itself is drawn either way: the red corner and the edge */
        test_assert(corner[0] > 180 && corner[1] < 80 && corner[2] < 80);
        test_assert(!slice_same(edge, green, 20));
        stbi_image_free(px);
        ecs_fini(world);
    }
}

/** Side of the offscreen surface of the surface tests in points and pixels */
#define SLICE_SURFACE (48)

/** Side of the solid corners the offscreen surface renders */
#define SLICE_SURFACE_CORNER (16)

/** The colors of the corners of the offscreen surface: top left, top right, bottom left and
 * bottom right, as slice_corner numbers them */
static const unsigned char slice_surface_colors[4][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}
};

/** An offscreen surface of 48 by 48 pixels that renders into surface_texture: four solid 16 by
 * 16 squares in its corners, red, green, blue and yellow, on white */
#define SLICE_SURFACE_SCENE \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.material\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.surface\n" \
    "surface_texture {}\n" \
    "surface_ui {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "  top_left {\n" \
    "    Position: {-16, 16, 0}\n" \
    "    Rect: {16, 16}\n" \
    "    Rgb: {255, 0, 0}\n" \
    "  }\n" \
    "  top_right {\n" \
    "    Position: {16, 16, 0}\n" \
    "    Rect: {16, 16}\n" \
    "    Rgb: {0, 255, 0}\n" \
    "  }\n" \
    "  bottom_left {\n" \
    "    Position: {-16, -16, 0}\n" \
    "    Rect: {16, 16}\n" \
    "    Rgb: {0, 0, 255}\n" \
    "  }\n" \
    "  bottom_right {\n" \
    "    Position: {16, -16, 0}\n" \
    "    Rect: {16, 16}\n" \
    "    Rgb: {255, 255, 0}\n" \
    "  }\n" \
    "}\n" \
    "surface_views {\n" \
    "  view {\n" \
    "    View: {camera: surface_ui.camera, lighting: {enabled: false},\n" \
    "      culling: {enabled: false},\n" \
    "      clear: {enabled: true, color: {255, 255, 255, 255}}}\n" \
    "  }\n" \
    "}\n" \
    "corner_surface {\n" \
    "  Surface: {width: 48, height: 48, texture: surface_texture, view_root: surface_views}\n" \
    "}\n"

void ImageSlice_surface_corners_keep_size(void) {
    // ui/image_slice
    ImageSlice slice = { .left = SLICE_SURFACE_CORNER, .top = SLICE_SURFACE_CORNER,
        .right = SLICE_SURFACE_CORNER, .bottom = SLICE_SURFACE_CORNER, .scale = 1 };
    int32_t sizes[2][2] = {{64, 64}, {160, 96}};
    unsigned char white[3] = {255, 255, 255};
    int32_t i, c, dx, dy;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = slice_world("slice_surface.png", (rgba){0, 0, 0, 255});
        test_assert(ecs_script_run(world, "surface", SLICE_SURFACE_SCENE, NULL) == 0);
        ecs_entity_t tex = ecs_lookup(world, "surface_texture");
        test_assert(tex != 0);
        test_assert(!ecs_has(world, tex, Texture));
        slice_panel(world, tex, (float)sizes[i][0], (float)sizes[i][1], &slice);
        unsigned char *px = slice_render(world, "slice_surface.png");
        int32_t w = sizes[i][0], h = sizes[i][1];

        /* The offscreen surface rendered at its size in pixels, which the slicing counts in */
        const SurfaceStatus *st = ecs_get(world, ecs_lookup(world, "corner_surface"),
            SurfaceStatus);
        test_assert(st != NULL);
        test_int(st->width, SLICE_SURFACE);
        test_int(st->height, SLICE_SURFACE);

        /* Inside each corner, away from the filtered seam with the edges, the pixels are the
         * solid color of the corner of the surface at its size of 16 pixels */
        for (c = 0; c < 4; c ++) {
            for (dy = 1; dy < SLICE_SURFACE_CORNER - 2; dy ++) {
                for (dx = 1; dx < SLICE_SURFACE_CORNER - 2; dx ++) {
                    test_assert(slice_same(slice_corner(px, w, h, c, dx, dy),
                        slice_surface_colors[c], 3));
                }
            }
        }

        /* Past the corners the edges show the white between the squares: stretched as a plain
         * Image the red square would cover 160 / 3 pixels of the top and 96 / 3 of the side */
        for (c = 0; c < 4; c ++) {
            test_assert(slice_same(slice_corner(px, w, h, c, SLICE_SURFACE_CORNER + 8, 8),
                white, 3));
            test_assert(slice_same(slice_corner(px, w, h, c, 8, SLICE_SURFACE_CORNER + 8),
                white, 3));
        }

        stbi_image_free(px);
        ecs_fini(world);
    }
}
