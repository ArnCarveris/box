/**
 * @file Polygon2D.c
 * @brief Tests for Polygon2D: concave outlines, holes, curves, strokes, borders, images, clips,
 * shadows, materials, transitions and changes, rendered with the GPU backend.
 *
 * The scenes are drawn by an orthographic camera of one unit per pixel into a 64x64 image on a
 * black clear, so the pixel of a point x, y of the plane is column 32 + x, row 32 - y.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"
#include "draw.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Modules of the scenes */
#define POLY_PRELUDE \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.material\n" \
    "using box.texture\n" \
    "using box.layout\n"

/** An L shaped concave outline in a 4 by 4 square without its top right quarter; scaled by 10
 * and offset by -20 it covers -20..20 on both axes but the quarter x 0..20, y 0..20 */
#define POLY_L \
    "l_path {\n" \
    "  Path2D: {start: {0, 0}, closed: true, segments: [\n" \
    "    {kind: Line, end: {4, 0}},\n" \
    "    {kind: Line, end: {4, 2}},\n" \
    "    {kind: Line, end: {2, 2}},\n" \
    "    {kind: Line, end: {2, 4}},\n" \
    "    {kind: Line, end: {0, 4}}\n" \
    "  ]}\n" \
    "}\n"

/** A circle of radius 20 from four cubic Bezier quarters */
#define POLY_CIRCLE \
    "circle_path {\n" \
    "  Path2D: {start: {20, 0}, closed: true, segments: [\n" \
    "  {kind: CubicBezier, control1: {20, 11.046}, control2: {11.046, 20}, end: {0, 20}},\n" \
    "  {kind: CubicBezier, control1: {-11.046, 20}, control2: {-20, 11.046}, end: {-20, 0}},\n" \
    "  {kind: CubicBezier, control1: {-20, -11.046}, control2: {-11.046, -20}, end: {0, -20}},\n" \
    "  {kind: CubicBezier, control1: {11.046, -20}, control2: {20, -11.046}, end: {20, 0}}\n" \
    "  ]}\n" \
    "}\n"

/** Create a headless world with an orthographic camera of one unit per pixel, a view that
 * clears to a color, a 64x64 surface that writes a file, and the entities of a script.
 *
 * @param file The file of the surface
 * @param clear The clear color
 * @param code The script, after POLY_PRELUDE, NULL for none
 * @return The world
 */
static ecs_world_t* poly_world_clear(
    const char *file,
    rgba clear,
    const char *code)
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
    ecs_entity_t s = ecs_entity(world, { .name = "capture" });
    ecs_set(world, s, Surface, {.width = 64, .height = 64,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    if (code) {
        char *script = flecs_asprintf("%s%s", POLY_PRELUDE, code);
        test_assert(ecs_script_run(world, "polygon", script, NULL) == 0);
        ecs_os_free(script);
    }

    return world;
}

/** Create a world as poly_world_clear does, with a black clear.
 *
 * @param file The file of the surface
 * @param code The script, after POLY_PRELUDE, NULL for none
 * @return The world
 */
static ecs_world_t* poly_world(
    const char *file,
    const char *code)
{
    return poly_world_clear(file, (rgba){0, 0, 0, 255}, code);
}

/** Render frames and load the image the surface wrote.
 *
 * @param world The world
 * @param file The file of the surface
 * @param frames Frames to render
 * @param delta Seconds each frame advances the world
 * @return The pixels, RGBA, free with stbi_image_free
 */
static unsigned char* poly_render_delta(
    ecs_world_t *world,
    const char *file,
    int32_t frames,
    float delta)
{
    int32_t i;
    int w = 0, h = 0, n = 0;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, delta);
    }

    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 64);

    return px;
}

/** Render frames that do not advance the world and load the image the surface wrote.
 *
 * @param world The world
 * @param file The file of the surface
 * @param frames Frames to render
 * @return The pixels, RGBA, free with stbi_image_free
 */
static unsigned char* poly_render(
    ecs_world_t *world,
    const char *file,
    int32_t frames)
{
    return poly_render_delta(world, file, frames, 0);
}

/** Get the pixel of a point of the plane.
 *
 * @param px The pixels
 * @param x X of the point
 * @param y Y of the point
 * @return The pixel
 */
static const unsigned char* poly_at(
    const unsigned char *px,
    float x,
    float y)
{
    int32_t col = (int32_t)floorf(x + 32.0f), row = (int32_t)floorf(32.0f - y);
    test_assert(col >= 0 && col < 64 && row >= 0 && row < 64);

    return &px[(row * 64 + col) * 4];
}

/** Check whether the pixel of a point is white.
 *
 * @param px The pixels
 * @param x X of the point
 * @param y Y of the point
 * @return Whether every channel is above 245
 */
static bool poly_white(
    const unsigned char *px,
    float x,
    float y)
{
    const unsigned char *c = poly_at(px, x, y);

    return c[0] > 245 && c[1] > 245 && c[2] > 245;
}

/** Check whether the pixel of a point is black.
 *
 * @param px The pixels
 * @param x X of the point
 * @param y Y of the point
 * @return Whether every channel is below 10
 */
static bool poly_black(
    const unsigned char *px,
    float x,
    float y)
{
    const unsigned char *c = poly_at(px, x, y);

    return c[0] < 10 && c[1] < 10 && c[2] < 10;
}

/** Check that a world has no device errors.
 *
 * @param world The world
 */
static void poly_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
}

/** The white L of POLY_L at the origin */
#define POLY_L_SHAPE \
    POLY_L \
    "l_shape {\n" \
    "  Position: {0, 0, 0}\n" \
    "  Polygon2D: {path: l_path, scale: {10, 10}, offset: {-20, -20}}\n" \
    "  Rgb: {255, 255, 255}\n" \
    "}\n"

/** The concave L fills its arms and leaves its notch and the outside of its bounds empty. */
void Polygon2D_concave(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_concave.png", POLY_L_SHAPE);
    unsigned char *px = poly_render(world, "polygon_concave.png", 1);
    test_assert(poly_white(px, -10.5f, 10.5f));
    test_assert(poly_white(px, -10.5f, -10.5f));
    test_assert(poly_white(px, 10.5f, -10.5f));
    test_assert(poly_white(px, -19.5f, 0.5f));
    test_assert(poly_white(px, 19.5f, -19.5f));
    test_assert(poly_white(px, -0.5f, 19.5f));
    test_assert(poly_black(px, 10.5f, 10.5f));
    test_assert(poly_black(px, 0.5f, 0.5f));
    test_assert(poly_black(px, 19.5f, 19.5f));
    test_assert(poly_black(px, -20.5f, 0.5f));
    test_assert(poly_black(px, 0.5f, -20.5f));
    test_assert(poly_black(px, 25.5f, -10.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** A diagonal edge is anti-aliased over about a pixel, while the edges along the pixel grid
 * stay crisp.
 */
void Polygon2D_edges_antialiased(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_aa.png",
        "tri_path {\n"
        "  Path2D: {start: {-20, -20}, closed: true, segments: [\n"
        "    {kind: Line, end: {20.5, -20}},\n"
        "    {kind: Line, end: {-20, 20}}\n"
        "  ]}\n"
        "}\n"
        "tri {\n"
        "  Position: {0, 0, 0}\n"
        "  Polygon2D: {path: tri_path}\n"
        "  Rgb: {255, 255, 255}\n"
        "}\n");
    unsigned char *px = poly_render(world, "polygon_aa.png", 1);
    int32_t row, col, partial = 0;
    for (row = 16; row < 48; row ++) {
        for (col = 16; col < 48; col ++) {
            int32_t v = px[(row * 64 + col) * 4];
            partial += v > 20 && v < 235;
        }
    }

    test_assert(partial >= 16);
    test_assert(poly_white(px, -10.5f, -10.5f));
    test_assert(poly_black(px, 10.5f, 10.5f));
    test_assert(poly_white(px, 0.5f, -19.5f));
    test_assert(poly_black(px, 0.5f, -20.5f));
    test_assert(poly_white(px, -19.5f, 0.5f));
    test_assert(poly_black(px, -20.5f, 0.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** The hole of a Profile2D stays empty, the ring around it is filled. */
void Polygon2D_hole(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_hole.png",
        "outer_path {\n"
        "  Path2D: {start: {-20, -20}, closed: true, segments: [\n"
        "    {kind: Line, end: {20, -20}},\n"
        "    {kind: Line, end: {20, 20}},\n"
        "    {kind: Line, end: {-20, 20}}\n"
        "  ]}\n"
        "}\n"
        "hole_path {\n"
        "  Path2D: {start: {-8, -8}, closed: true, segments: [\n"
        "    {kind: Line, end: {8, -8}},\n"
        "    {kind: Line, end: {8, 8}},\n"
        "    {kind: Line, end: {-8, 8}}\n"
        "  ]}\n"
        "}\n"
        "frame_profile {\n"
        "  Profile2D: {outer: outer_path, holes: [hole_path]}\n"
        "}\n"
        "frame {\n"
        "  Position: {0, 0, 0}\n"
        "  Polygon2D: {path: frame_profile}\n"
        "  Rgb: {255, 255, 255}\n"
        "}\n");
    unsigned char *px = poly_render(world, "polygon_hole.png", 1);
    test_assert(poly_black(px, 0.5f, 0.5f));
    test_assert(poly_black(px, -7.5f, 7.5f));
    test_assert(poly_black(px, 7.5f, -7.5f));
    test_assert(poly_white(px, 8.5f, 0.5f));
    test_assert(poly_white(px, 0.5f, 8.5f));
    test_assert(poly_white(px, -14.5f, 14.5f));
    test_assert(poly_white(px, 19.5f, -19.5f));
    test_assert(poly_black(px, 20.5f, 0.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** A circle from four cubic Bezier segments is round: no corner of its bounds is drawn. */
void Polygon2D_curved(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_curved.png",
        POLY_CIRCLE
        "disc {\n"
        "  Position: {0, 0, 0}\n"
        "  Polygon2D: {path: circle_path}\n"
        "  Rgb: {255, 255, 255}\n"
        "}\n");
    unsigned char *px = poly_render(world, "polygon_curved.png", 1);
    test_assert(poly_white(px, 0.5f, 0.5f));
    test_assert(poly_white(px, 18.5f, 0.5f));
    test_assert(poly_white(px, -0.5f, -18.5f));
    test_assert(poly_white(px, 12.5f, 12.5f));
    test_assert(poly_white(px, -12.5f, -12.5f));
    test_assert(poly_black(px, 21.5f, 0.5f));
    test_assert(poly_black(px, 15.5f, 15.5f));
    test_assert(poly_black(px, -15.5f, 15.5f));
    test_assert(poly_black(px, 19.5f, 19.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** The Border lies inside every edge of the outline, also along the inner corner of the notch. */
void Polygon2D_border(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_border.png",
        POLY_L
        "l_shape {\n"
        "  Position: {0, 0, 0}\n"
        "  Polygon2D: {path: l_path, scale: {10, 10}, offset: {-20, -20}}\n"
        "  Rgb: {255, 255, 255}\n"
        "  Border: {width: 3, color: {255, 0, 0, 255}}\n"
        "}\n");
    unsigned char *px = poly_render(world, "polygon_border.png", 1);
    const unsigned char *c = poly_at(px, -18.5f, -10.5f);
    test_assert(c[0] > 245 && c[1] < 10 && c[2] < 10);
    c = poly_at(px, 10.5f, -1.5f);
    test_assert(c[0] > 245 && c[1] < 10 && c[2] < 10);
    c = poly_at(px, -1.5f, 10.5f);
    test_assert(c[0] > 245 && c[1] < 10 && c[2] < 10);
    test_assert(poly_white(px, -10.5f, -10.5f));
    test_assert(poly_white(px, 10.5f, -10.5f));
    test_assert(poly_black(px, 10.5f, 10.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** An open path draws a stroke of its width, with a round end. */
void Polygon2D_stroke(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_stroke.png",
        "road_path {\n"
        "  Path2D: {start: {-20, -10}, segments: [\n"
        "    {kind: Line, end: {20, -10}},\n"
        "    {kind: Line, end: {20, 20}}\n"
        "  ]}\n"
        "}\n"
        "road {\n"
        "  Position: {0, 0, 0}\n"
        "  Polygon2D: {path: road_path, width: 4}\n"
        "  Rgb: {255, 255, 255}\n"
        "}\n");
    unsigned char *px = poly_render(world, "polygon_stroke.png", 1);
    test_assert(poly_white(px, 0.5f, -10.5f));
    test_assert(poly_white(px, 0.5f, -8.5f));
    test_assert(poly_white(px, 20.5f, 5.5f));
    test_assert(poly_white(px, 20.5f, -10.5f));
    test_assert(poly_black(px, 0.5f, -13.5f));
    test_assert(poly_black(px, 0.5f, -6.5f));
    test_assert(poly_black(px, 0.5f, 0.5f));
    test_assert(poly_black(px, 16.5f, 5.5f));
    test_assert(poly_white(px, -20.5f, -10.5f));
    test_assert(poly_black(px, -23.5f, -10.5f));
    test_assert(poly_black(px, -21.5f, -12.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** A stroke thinner than a pixel is widened to one and faded, so no column misses it. */
void Polygon2D_thin_stroke_stays_continuous(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_thin.png",
        "hair_path {\n"
        "  Path2D: {start: {-25, -20}, segments: [{kind: Line, end: {25, 15}}]}\n"
        "}\n"
        "hair {\n"
        "  Position: {0, 0, 0}\n"
        "  Polygon2D: {path: hair_path, width: 0.3}\n"
        "  Rgb: {255, 255, 255}\n"
        "}\n");
    unsigned char *px = poly_render(world, "polygon_thin.png", 1);
    int32_t col, row, gaps = 0;
    for (col = 10; col < 54; col ++) {
        int32_t lit = 0;
        for (row = 0; row < 64; row ++) {
            lit += px[(row * 64 + col) * 4] > 8;
        }

        gaps += lit == 0;
    }

    test_int(gaps, 0);
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** A ClipChildren ancestor cuts the polygon; one entirely outside it draws nothing, and the test
 * of whether it is outside uses the bounds of its outline, which need not hold its origin.
 */
void Polygon2D_clipped(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_clip.png",
        POLY_CIRCLE
        "panel {\n"
        "  Position: {-10, 0, 0}\n"
        "  Rect: {20, 20}\n"
        "  Rgb: {0, 0, 0}\n"
        "  ClipChildren\n"
        "  disc {\n"
        "    Position: {10, 0, 0}\n"
        "    Polygon2D: {path: circle_path}\n"
        "    Rgb: {0, 255, 0}\n"
        "  }\n"
        "}\n");
    unsigned char *px = poly_render(world, "polygon_clip.png", 1);
    const unsigned char *c = poly_at(px, -5.5f, 5.5f);
    test_assert(c[1] > 245 && c[0] < 10);
    c = poly_at(px, -1.5f, -8.5f);
    test_assert(c[1] > 245 && c[0] < 10);
    test_assert(poly_black(px, 5.5f, 0.5f));
    test_assert(poly_black(px, -5.5f, 12.5f));
    test_assert(poly_black(px, -5.5f, -12.5f));
    stbi_image_free(px);

    ecs_entity_t disc = ecs_lookup(world, "panel.disc");
    ecs_set(world, disc, Position, {60, 0, 0});
    px = poly_render(world, "polygon_clip.png", 1);
    test_assert(poly_black(px, -5.5f, 5.5f));
    stbi_image_free(px);

    ecs_set(world, disc, Polygon2D, {.path = ecs_lookup(world, "circle_path"),
        .scale = {1, 1}, .offset = {-60, 0}, .width = 1});
    px = poly_render(world, "polygon_clip.png", 1);
    c = poly_at(px, -5.5f, 5.5f);
    test_assert(c[1] > 245 && c[0] < 10);
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** The Image spans the bounds of the outline: red top left, blue bottom left, white bottom right,
 * and the green top right quarter is the notch of the L.
 */
void Polygon2D_image(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_image.png", NULL);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/polygon_quadrants.png", tmp);
    uint8_t texels[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        bool right = (i % 4) >= 2;
        bool bottom = (i / 4) >= 2;
        texels[i * 4 + 0] = (uint8_t)(right == bottom ? 255 : 0);
        texels[i * 4 + 1] = (uint8_t)(right ? 255 : 0);
        texels[i * 4 + 2] = (uint8_t)(bottom ? 255 : 0);
        texels[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, texels, 4 * 4) != 0);
    char *code = flecs_asprintf(POLY_PRELUDE POLY_L
        "quadrants { TexturePath: {path: \"%s\"} }\n"
        "l_shape {\n"
        "  Position: {0, 0, 0}\n"
        "  Polygon2D: {path: l_path, scale: {10, 10}, offset: {-20, -20}}\n"
        "  Image: {texture: quadrants}\n"
        "}\n", file);
    test_assert(ecs_script_run(world, "image", code, NULL) == 0);
    ecs_os_free(code);
    unsigned char *px = poly_render(world, "polygon_image.png", 2);
    const unsigned char *c = poly_at(px, -10.5f, 10.5f);
    test_assert(c[0] > 230 && c[1] < 30 && c[2] < 30);
    c = poly_at(px, -10.5f, -10.5f);
    test_assert(c[0] < 30 && c[1] < 30 && c[2] > 230);
    c = poly_at(px, 10.5f, -10.5f);
    test_assert(c[0] > 230 && c[1] > 230 && c[2] > 230);
    test_assert(poly_black(px, 10.5f, 10.5f));
    stbi_image_free(px);
    ecs_os_free(file);
    poly_no_errors(world);
    ecs_fini(world);
}

/** Opacity blends the polygon and a Tint replaces its color. */
void Polygon2D_opacity_and_tint(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_material.png",
        POLY_L
        "l_shape {\n"
        "  Position: {0, 0, 0}\n"
        "  Polygon2D: {path: l_path, scale: {10, 10}, offset: {-20, -20}}\n"
        "  Rgb: {255, 0, 0}\n"
        "  Opacity: {0.5}\n"
        "}\n");
    unsigned char *px = poly_render(world, "polygon_material.png", 1);
    const unsigned char *c = poly_at(px, -10.5f, -10.5f);
    test_assert(c[0] > 120 && c[0] < 230 && c[1] < 10 && c[2] < 10);
    test_assert(poly_black(px, 10.5f, 10.5f));
    stbi_image_free(px);

    ecs_entity_t e = ecs_lookup(world, "l_shape");
    ecs_remove(world, e, Opacity);
    ecs_set(world, e, Tint, {0, 0, 255, 255});
    px = poly_render(world, "polygon_material.png", 1);
    c = poly_at(px, -10.5f, -10.5f);
    test_assert(c[0] < 10 && c[1] < 10 && c[2] > 245);
    test_assert(poly_black(px, 10.5f, 10.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** The BoxShadow is the outline moved by the offset: dark right of the lower arm and in the notch
 * of the L over the shadow of the upper arm, light in the notch of the shadow, which a shadow of
 * the bounds would cover, and the shape draws over its shadow.
 */
void Polygon2D_box_shadow_follows_outline(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world_clear("polygon_shadow.png", (rgba){255, 255, 255, 255},
        POLY_L
        "l_shape {\n"
        "  Position: {0, 0, 0}\n"
        "  Polygon2D: {path: l_path, scale: {10, 10}, offset: {-20, -20}}\n"
        "  Rgb: {120, 160, 255}\n"
        "  BoxShadow: {offset_x: 6, offset_y: -6, color: {0, 0, 0, 255}}\n"
        "}\n");
    unsigned char *px = poly_render(world, "polygon_shadow.png", 1);
    const unsigned char *c = poly_at(px, 23.5f, -10.5f);
    test_assert(c[0] < 40 && c[1] < 40 && c[2] < 40);
    c = poly_at(px, 3.5f, 3.5f);
    test_assert(c[0] < 40 && c[1] < 40 && c[2] < 40);
    test_assert(poly_white(px, 12.5f, 10.5f));
    c = poly_at(px, -10.5f, -10.5f);
    test_assert(c[0] > 100 && c[0] < 140 && c[2] > 245);
    test_assert(poly_white(px, -24.5f, 0.5f));
    test_assert(poly_white(px, 0.5f, 24.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** The shadow of a disc of radius 10 grown by a spread of 4 falls off around 14, and goes away
 * with its BoxShadow.
 */
void Polygon2D_box_shadow_blur_and_spread(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world_clear("polygon_shadow_blur.png",
        (rgba){255, 255, 255, 255},
        POLY_CIRCLE
        "disc {\n"
        "  Position: {0, 0, 0}\n"
        "  Polygon2D: {path: circle_path, scale: {0.5, 0.5}}\n"
        "  Rgb: {255, 255, 255}\n"
        "  BoxShadow: {blur: 8, spread: 4, color: {0, 0, 0, 255}}\n"
        "}\n");
    unsigned char *px = poly_render(world, "polygon_shadow_blur.png", 1);
    int32_t near = poly_at(px, 12.5f, 0.5f)[0];
    int32_t edge = poly_at(px, 14.5f, 0.5f)[0];
    int32_t far = poly_at(px, 18.5f, 0.5f)[0];
    test_assert(near < edge);
    test_assert(edge < far);
    test_assert(far < 250);
    test_assert(poly_white(px, 30.5f, 0.5f));
    test_assert(poly_white(px, 0.5f, 0.5f));
    stbi_image_free(px);

    ecs_remove(world, ecs_lookup(world, "disc"), BoxShadow);
    px = poly_render(world, "polygon_shadow_blur.png", 1);
    test_assert(poly_white(px, 12.5f, 0.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** Render the white L of POLY_L_SHAPE over a white clear with a shadow, without a transition.
 *
 * @param file The file of the surface
 * @param shadow The shadow
 * @return The pixels, RGBA, free with stbi_image_free
 */
static unsigned char* poly_shadow_reference(
    const char *file,
    const BoxShadow *shadow)
{
    ecs_world_t *world = poly_world_clear(file, (rgba){255, 255, 255, 255}, POLY_L_SHAPE);
    ecs_entity_t e = ecs_lookup(world, "l_shape");
    ecs_set(world, e, Rgb, {120, 160, 255});
    ecs_set_ptr(world, e, BoxShadow, shadow);
    unsigned char *px = poly_render(world, file, 2);
    poly_no_errors(world);
    ecs_fini(world);

    return px;
}

/** Get the largest difference of a channel between two images.
 *
 * @param a The first image
 * @param b The second image
 * @return The difference
 */
static int32_t poly_diff(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t i, result = 0;
    for (i = 0; i < 64 * 64 * 4; i ++) {
        if (i % 4 != 3) {
            int32_t d = abs((int32_t)a[i] - (int32_t)b[i]);
            result = d > result ? d : result;
        }
    }

    return result;
}

/** A transition of the BoxShadow of a polygon lifts its shadow: halfway through it is the shadow
 * of a polygon set up at the halfway offset, blur and spread, following the outline, and at the
 * end that of the target.
 */
void Polygon2D_box_shadow_transition(void) {
    // ui/polygon_2d
    BoxShadow rest = {.offset_x = 2, .offset_y = -2, .blur = 2, .color = {0, 0, 0, 200}};
    BoxShadow lifted = {.offset_x = 8, .offset_y = -8, .blur = 10, .spread = 2,
        .color = {0, 0, 0, 200}};
    BoxShadow halfway = {.offset_x = 5, .offset_y = -5, .blur = 6, .spread = 1,
        .color = {0, 0, 0, 200}};
    ecs_world_t *world = poly_world_clear("polygon_shadow_lift.png", (rgba){255, 255, 255, 255},
        POLY_L_SHAPE);
    ecs_entity_t e = ecs_lookup(world, "l_shape");
    ecs_set(world, e, Rgb, {120, 160, 255});
    ecs_set_ptr(world, e, BoxShadow, &rest);
    box_transition_set(world, e, ecs_id(BoxShadow), &(TransitionSpec){ .duration = 1.0f });
    stbi_image_free(poly_render_delta(world, "polygon_shadow_lift.png", 1, 0.25f));
    ecs_set_ptr(world, e, BoxShadow, &lifted);
    unsigned char *mid = poly_render_delta(world, "polygon_shadow_lift.png", 3, 0.25f);
    unsigned char *end = poly_render_delta(world, "polygon_shadow_lift.png", 2, 0.25f);
    poly_no_errors(world);
    ecs_fini(world);

    unsigned char *at_mid = poly_shadow_reference("polygon_shadow_mid.png", &halfway);
    unsigned char *at_end = poly_shadow_reference("polygon_shadow_end.png", &lifted);
    test_assert(poly_diff(mid, at_end) > 20);
    test_assert(poly_diff(mid, at_mid) <= 4);
    test_int(poly_diff(end, at_end), 0);
    test_assert(poly_white(mid, 10.5f, 10.5f));
    stbi_image_free(mid);
    stbi_image_free(end);
    stbi_image_free(at_mid);
    stbi_image_free(at_end);
}

/** A polygon in a UI rendered to a texture: the L shows through an Image of the texture at twice
 * its size, its notch the clear color of the texture.
 */
void Polygon2D_render_to_texture(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_texture.png", POLY_L);
    ecs_entity_t texture = ecs_new(world);
    ecs_entity_t image = ecs_new(world);
    ecs_set(world, image, Position, {0, 0, 0});
    ecs_set(world, image, Rect, {64, 64});
    ecs_set(world, image, Image, {.texture = texture, .u1 = 1, .v1 = 1});

    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t ui_camera = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, ui_camera, Position, {0, 0, 100});
    ecs_set(world, ui_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t l = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, l, Position, {0, 0, 0});
    ecs_set(world, l, Polygon2D, {.path = ecs_lookup(world, "l_path"), .scale = {3, 3},
        .offset = {-6, -6}, .width = 1});
    ecs_set(world, l, Rgb, {0, 255, 0});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t ui_view = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, ui_view, View, {.camera = ui_camera,
        .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t screen = ecs_new(world);
    ecs_set(world, screen, Surface, {.width = 32, .height = 32, .texture = texture,
        .view_root = root, .resolution_scale = 1});
    unsigned char *px = poly_render(world, "polygon_texture.png", 3);
    const unsigned char *c = poly_at(px, -6.5f, -6.5f);
    test_assert(c[1] > 245 && c[0] < 10 && c[2] < 10);
    c = poly_at(px, 6.5f, -6.5f);
    test_assert(c[1] > 245 && c[0] < 10 && c[2] < 10);
    c = poly_at(px, -6.5f, 6.5f);
    test_assert(c[1] > 245 && c[0] < 10 && c[2] < 10);
    c = poly_at(px, 6.5f, 6.5f);
    test_assert(c[2] > 245 && c[0] < 10 && c[1] < 10);
    c = poly_at(px, 20.5f, -20.5f);
    test_assert(c[2] > 245 && c[0] < 10 && c[1] < 10);
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** An Rgb transition renders the polygon in between, and the record index of the polygon survives
 * it: the notch stays open.
 */
void Polygon2D_rgb_transition(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_transition.png", POLY_L_SHAPE);
    ecs_entity_t e = ecs_lookup(world, "l_shape");
    ecs_set(world, e, Rgb, {255, 0, 0});
    box_transition_set(world, e, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    stbi_image_free(poly_render_delta(world, "polygon_transition.png", 1, 0.25f));
    ecs_set(world, e, Rgb, {0, 0, 255});
    stbi_image_free(poly_render_delta(world, "polygon_transition.png", 1, 0.25f));
    unsigned char *px = poly_render_delta(world, "polygon_transition.png", 1, 0.25f);
    const unsigned char *c = poly_at(px, -10.5f, -10.5f);
    test_assert(c[0] > 40 && c[2] > 40);
    test_assert(poly_black(px, 10.5f, 10.5f));
    test_assert(poly_black(px, -20.5f, 0.5f));
    stbi_image_free(px);
    px = poly_render_delta(world, "polygon_transition.png", 4, 0.25f);
    c = poly_at(px, -10.5f, -10.5f);
    test_assert(c[0] < 10 && c[2] > 245);
    test_assert(poly_black(px, 10.5f, 10.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** The polygon follows its path: the L becomes a square, then a circle that takes more records
 * than the square, and without Polygon2D nothing is drawn.
 */
void Polygon2D_path_change_updates(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_change.png", POLY_L_SHAPE);
    unsigned char *px = poly_render(world, "polygon_change.png", 1);
    test_assert(poly_black(px, 10.5f, 10.5f));
    stbi_image_free(px);

    ecs_entity_t path = ecs_lookup(world, "l_path");
    test_assert(ecs_script_run(world, "change", POLY_PRELUDE
        "l_path {\n"
        "  Path2D: {start: {0, 0}, closed: true, segments: [\n"
        "    {kind: Line, end: {4, 0}},\n"
        "    {kind: Line, end: {4, 4}},\n"
        "    {kind: Line, end: {0, 4}}\n"
        "  ]}\n"
        "}\n", NULL) == 0);
    test_assert(ecs_lookup(world, "l_path") == path);
    px = poly_render(world, "polygon_change.png", 1);
    test_assert(poly_white(px, 10.5f, 10.5f));
    stbi_image_free(px);

    test_assert(ecs_script_run(world, "circle", POLY_PRELUDE POLY_CIRCLE, NULL) == 0);
    ecs_entity_t e = ecs_lookup(world, "l_shape");
    ecs_set(world, e, Polygon2D, {.path = ecs_lookup(world, "circle_path"), .scale = {1, 1},
        .width = 1});
    px = poly_render(world, "polygon_change.png", 1);
    test_assert(poly_white(px, 0.5f, 0.5f));
    test_assert(poly_black(px, 19.5f, 19.5f));
    stbi_image_free(px);

    ecs_remove(world, e, Polygon2D);
    px = poly_render(world, "polygon_change.png", 1);
    test_assert(poly_black(px, 0.5f, 0.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** The freed record of a deleted polygon is taken by a new polygon, the others keep theirs. */
void Polygon2D_records_reused(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_reuse.png", POLY_L POLY_CIRCLE);
    ecs_entity_t l_path = ecs_lookup(world, "l_path");
    ecs_entity_t circle = ecs_lookup(world, "circle_path");
    ecs_entity_t a = ecs_new(world), b = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Polygon2D, {.path = circle, .scale = {0.25f, 0.25f},
        .offset = {-15, 15}, .width = 1});
    ecs_set(world, a, Rgb, {255, 255, 255});
    ecs_set(world, b, Position, {0, 0, 0});
    ecs_set(world, b, Polygon2D, {.path = l_path, .scale = {2, 2}, .offset = {10, -20},
        .width = 1});
    ecs_set(world, b, Rgb, {255, 255, 255});
    unsigned char *px = poly_render(world, "polygon_reuse.png", 1);
    test_assert(poly_white(px, -15.5f, 15.5f));
    test_assert(poly_white(px, 11.5f, -18.5f));
    stbi_image_free(px);

    ecs_delete(world, a);
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, Position, {0, 0, 0});
    ecs_set(world, c, Polygon2D, {.path = l_path, .scale = {2, 2}, .offset = {-20, 10},
        .width = 1});
    ecs_set(world, c, Rgb, {255, 255, 255});
    px = poly_render(world, "polygon_reuse.png", 1);
    test_assert(poly_black(px, -15.5f, 15.5f));
    test_assert(poly_white(px, -18.5f, 11.5f));
    test_assert(poly_white(px, 11.5f, -18.5f));
    test_assert(poly_black(px, 17.5f, -12.5f));
    test_assert(poly_black(px, -12.5f, 17.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** A polygon that moves keeps its record and uploads nothing to the clip buffer; one whose path
 * changes uploads its own records, not those of the other polygons. */
void Polygon2D_records_upload_changed(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_upload.png", POLY_L POLY_CIRCLE);
    ecs_entity_t l_path = ecs_lookup(world, "l_path");
    ecs_entity_t circle = ecs_lookup(world, "circle_path");
    ecs_entity_t a = ecs_new(world), b = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Polygon2D, {.path = circle, .scale = {0.5f, 0.5f}, .width = 1});
    ecs_set(world, a, Rgb, {255, 255, 255});
    ecs_set(world, b, Position, {0, 0, 0});
    ecs_set(world, b, Polygon2D, {.path = l_path, .scale = {2, 2}, .offset = {10, -20},
        .width = 1});
    ecs_set(world, b, Rgb, {255, 255, 255});
    stbi_image_free(poly_render(world, "polygon_upload.png", 2));

    box_render_draws_stats_t stats = {0};
    ecs_set(world, a, Position, {-12, 12, 0});
    unsigned char *px = poly_render(world, "polygon_upload.png", 1);
    box_render_draws_stats(world, &stats);
    test_int(stats.clip_bytes, 0);
    test_assert(poly_white(px, -12.5f, 12.5f));
    test_assert(poly_black(px, 0.5f, 0.5f));
    stbi_image_free(px);

    ecs_set(world, b, Polygon2D, {.path = l_path, .scale = {2, 2}, .offset = {10, -10},
        .width = 1});
    px = poly_render(world, "polygon_upload.png", 1);
    box_render_draws_stats(world, &stats);
    test_int(stats.clip_bytes, BOX_RENDER_POLYGON_RECORDS(6) *
        (int64_t)sizeof(box_render_clip_t));
    test_assert(poly_white(px, 11.5f, -8.5f));
    test_assert(poly_white(px, -12.5f, 12.5f));
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** 1024 L shapes of 2 by 2 units over the image light three of the four pixels of each. */
void Polygon2D_many_polygons(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_many.png", POLY_L);
    ecs_entity_t l_path = ecs_lookup(world, "l_path");
    int32_t x, y;
    for (y = 0; y < 32; y ++) {
        for (x = 0; x < 32; x ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)(x * 2 - 32), (float)(y * 2 - 32), 0});
            ecs_set(world, e, Polygon2D, {.path = l_path, .scale = {0.5f, 0.5f},
                .width = 1});
            ecs_set(world, e, Rgb, {255, 255, 255});
        }
    }

    unsigned char *px = poly_render(world, "polygon_many.png", 2);
    int32_t lit = 0, i;
    for (i = 0; i < 64 * 64; i ++) {
        lit += px[i * 4] > 245;
    }

    test_assert(lit >= 3 * 1024 - 64);
    test_assert(lit <= 3 * 1024 + 64);
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}

/** Polygons of different outlines share a batch, their outlines are in their records. */
void Polygon2D_one_batch(void) {
    // ui/polygon_2d
    ecs_world_t *world = poly_world("polygon_batch.png", POLY_L POLY_CIRCLE);
    ecs_entity_t l_path = ecs_lookup(world, "l_path");
    ecs_entity_t circle = ecs_lookup(world, "circle_path");
    ecs_entity_t a = ecs_new(world), b = ecs_new(world);
    ecs_set(world, a, Polygon2D, {.path = l_path, .scale = {1, 1}, .width = 1});
    ecs_set(world, b, Polygon2D, {.path = circle, .scale = {1, 1}, .width = 1});
    ecs_progress(world, 0);
    const DrawSlot *sa = ecs_get(world, a, DrawSlot);
    const DrawSlot *sb = ecs_get(world, b, DrawSlot);
    test_assert(sa != NULL && sb != NULL);
    test_assert(sa->batch >= 0);
    test_int(sa->batch, sb->batch);
    test_assert(sa->slice != 0);
    test_assert(sb->slice != 0);
    test_assert(sa->slice != sb->slice);
    poly_no_errors(world);
    ecs_fini(world);
}

/** A tilted disc in a perspective view is filled and its border runs all the way round it. */
void Polygon2D_perspective(void) {
    // ui/polygon_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64,
        .write_to_file = "polygon_perspective.png", .resolution_scale = 1});
    test_assert(ecs_script_run(world, "world", POLY_PRELUDE POLY_CIRCLE
        "sign {\n"
        "  Position: {0, 0, 0}\n"
        "  Rotation: {-0.7, 0.3, 0}\n"
        "  Polygon2D: {path: circle_path, scale: {0.1, 0.1}}\n"
        "  Rgb: {255, 255, 255}\n"
        "  Border: {width: 0.3, color: {255, 0, 0, 255}}\n"
        "}\n", NULL) == 0);
    unsigned char *px = poly_render(world, "polygon_perspective.png", 1);
    const unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 245 && c[1] > 245);
    c = &px[(2 * 64 + 2) * 4];
    test_assert(c[0] < 10 && c[1] < 10);
    int32_t i, red = 0;
    for (i = 0; i < 64 * 64; i ++) {
        red += px[i * 4] > 200 && px[i * 4 + 1] < 60;
    }

    test_assert(red > 40);
    stbi_image_free(px);
    poly_no_errors(world);
    ecs_fini(world);
}
