/**
 * @file TextSpans.c
 * @brief TextSpans: glyphs drawn in the colors of their spans in the batches of the Text.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Channel difference a pixel of solid ink may have from the color it is drawn in */
#define SPANS_TOLERANCE (6)

/** Create a headless world with box imported.
 *
 * @return The world
 */
static ecs_world_t* spans_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    return world;
}

/** Create an orthographic camera with one logical pixel per unit and a view cleared to black.
 *
 * @param world The world
 */
static void spans_view(
    ecs_world_t *world)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
}

/** Create a surface that writes its frames to a file of the temporary folder.
 *
 * @param world The world
 * @param width Width in pixels
 * @param height Height in pixels
 * @param file File name
 */
static void spans_surface(
    ecs_world_t *world,
    int32_t width,
    int32_t height,
    const char *file)
{
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = width, .height = height, .write_to_file = (char*)file,
        .resolution_scale = 1});
}

/** Create a white Text at the origin.
 *
 * @param world The world
 * @param parent Parent of the Text, 0 for none
 * @param value The string
 * @param size Em size
 * @param width Width the lines wrap at, 0 for none
 * @return The entity
 */
static ecs_entity_t spans_text(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *value,
    float size,
    float width)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Text, {.value = (char*)value, .size = size, .width = width,
        .line_height = size * 1.25f, .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, e, Rgb, {255, 255, 255});

    return e;
}

/** Replace the TextSpans of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param spans The spans
 * @param count Number of spans
 */
static void spans_set(
    ecs_world_t *world,
    ecs_entity_t e,
    const TextSpan *spans,
    int32_t count)
{
    TextSpans *ts = ecs_ensure(world, e, TextSpans);
    int32_t i;
    ecs_vec_clear(&ts->spans);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &ts->spans, TextSpan) = spans[i];
    }

    ecs_modified(world, e, TextSpans);
}

/** Run frames, wait for the renderer and load the frame a surface wrote.
 *
 * @param world The world
 * @param file File name of the surface
 * @param frames Frames to run
 * @return RGBA8 pixels, freed with stbi_image_free
 */
static unsigned char* spans_frame(
    ecs_world_t *world,
    const char *file,
    int32_t frames)
{
    int32_t i;
    int w = 0, h = 0, n = 0;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    ecs_os_free(path);

    return result;
}

/** Count the pixels of an area drawn in a color.
 *
 * @param px The pixels
 * @param width Width of the image
 * @param x0 Left column
 * @param x1 Column after the right one
 * @param y0 Top row
 * @param y1 Row after the bottom one
 * @param r Red
 * @param g Green
 * @param b Blue
 * @return The pixels within SPANS_TOLERANCE of the color
 */
static int32_t spans_ink(
    const unsigned char *px,
    int32_t width,
    int32_t x0,
    int32_t x1,
    int32_t y0,
    int32_t y1,
    int r,
    int g,
    int b)
{
    int32_t x, y, result = 0;
    for (y = y0; y < y1; y ++) {
        for (x = x0; x < x1; x ++) {
            const unsigned char *c = &px[(y * width + x) * 4];
            result += abs(c[0] - r) <= SPANS_TOLERANCE && abs(c[1] - g) <= SPANS_TOLERANCE &&
                abs(c[2] - b) <= SPANS_TOLERANCE;
        }
    }

    return result;
}

/** Get the brightest red channel of an area.
 *
 * @param px The pixels
 * @param width Width of the image
 * @param x0 Left column
 * @param x1 Column after the right one
 * @param y Row
 * @return The value
 */
static int spans_peak(
    const unsigned char *px,
    int32_t width,
    int32_t x0,
    int32_t x1,
    int32_t y)
{
    int32_t x;
    int result = 0;
    for (x = x0; x < x1; x ++) {
        int v = px[(y * width + x) * 4];
        result = v > result ? v : result;
    }

    return result;
}

void TextSpans_two_words_two_colors(void) {
    ecs_world_t *world = spans_world();
    spans_view(world);
    ecs_entity_t e = spans_text(world, 0, "II II", 60, 0);
    spans_set(world, e, (TextSpan[]){
        { 0, 2, { 255, 0, 0, 255 } },
        { 3, 5, { 0, 255, 0, 255 } }
    }, 2);
    spans_surface(world, 128, 64, "spans.png");

    unsigned char *px = spans_frame(world, "spans.png", 3);
    test_assert(spans_ink(px, 128, 0, 64, 28, 36, 255, 0, 0) >= 6);
    test_int(spans_ink(px, 128, 64, 128, 0, 64, 255, 0, 0), 0);
    test_assert(spans_ink(px, 128, 64, 128, 28, 36, 0, 255, 0) >= 6);
    test_int(spans_ink(px, 128, 0, 64, 0, 64, 0, 255, 0), 0);
    test_int(spans_ink(px, 128, 0, 128, 0, 64, 255, 255, 255), 0);
    stbi_image_free(px);

    ecs_fini(world);
}

void TextSpans_entity_color_outside_spans(void) {
    ecs_world_t *world = spans_world();
    spans_view(world);
    ecs_entity_t e = spans_text(world, 0, "I I I", 60, 0);
    ecs_set(world, e, Rgb, {255, 128, 0});
    spans_set(world, e, (TextSpan[]){{ 2, 3, { 0, 0, 255, 255 } }}, 1);
    spans_surface(world, 128, 64, "outside.png");

    unsigned char *px = spans_frame(world, "outside.png", 3);
    test_assert(spans_ink(px, 128, 0, 50, 28, 36, 255, 128, 0) >= 3);
    test_assert(spans_ink(px, 128, 78, 128, 28, 36, 255, 128, 0) >= 3);
    test_assert(spans_ink(px, 128, 50, 78, 28, 36, 0, 0, 255) >= 3);
    test_int(spans_ink(px, 128, 50, 78, 0, 64, 255, 128, 0), 0);
    test_int(spans_ink(px, 128, 0, 50, 0, 64, 0, 0, 255), 0);
    test_int(spans_ink(px, 128, 78, 128, 0, 64, 0, 0, 255), 0);
    stbi_image_free(px);

    ecs_fini(world);
}

void TextSpans_span_alpha(void) {
    ecs_world_t *world = spans_world();
    spans_view(world);
    ecs_entity_t e = spans_text(world, 0, "I", 60, 0);
    spans_surface(world, 64, 64, "alpha.png");

    unsigned char *px = spans_frame(world, "alpha.png", 3);
    test_assert(spans_peak(px, 64, 0, 64, 32) > 250);
    stbi_image_free(px);

    spans_set(world, e, (TextSpan[]){{ 0, 1, { 255, 255, 255, 128 } }}, 1);
    px = spans_frame(world, "alpha.png", 1);
    int half = spans_peak(px, 64, 0, 64, 32);
    test_assert(half > 60);
    test_assert(half < 230);
    stbi_image_free(px);

    ecs_set(world, e, Opacity, {0.5f});
    px = spans_frame(world, "alpha.png", 1);
    int quarter = spans_peak(px, 64, 0, 64, 32);
    test_assert(quarter > 10);
    test_assert(quarter < half - 10);
    stbi_image_free(px);

    ecs_fini(world);
}

void TextSpans_span_change_recolors_next_frame(void) {
    ecs_world_t *world = spans_world();
    spans_view(world);
    ecs_entity_t e = spans_text(world, 0, "II", 60, 0);
    spans_set(world, e, (TextSpan[]){{ 0, 2, { 255, 0, 0, 255 } }}, 1);
    spans_surface(world, 64, 64, "change.png");

    unsigned char *px = spans_frame(world, "change.png", 3);
    test_assert(spans_ink(px, 64, 0, 64, 28, 36, 255, 0, 0) >= 6);
    stbi_image_free(px);

    spans_set(world, e, (TextSpan[]){{ 0, 2, { 0, 0, 255, 255 } }}, 1);
    px = spans_frame(world, "change.png", 1);
    test_assert(spans_ink(px, 64, 0, 64, 28, 36, 0, 0, 255) >= 6);
    test_int(spans_ink(px, 64, 0, 64, 0, 64, 255, 0, 0), 0);
    stbi_image_free(px);

    spans_set(world, e, (TextSpan[]){{ 1, 2, { 0, 255, 0, 255 } }}, 1);
    px = spans_frame(world, "change.png", 1);
    test_assert(spans_ink(px, 64, 0, 32, 28, 36, 255, 255, 255) >= 3);
    test_assert(spans_ink(px, 64, 32, 64, 28, 36, 0, 255, 0) >= 3);
    test_int(spans_ink(px, 64, 0, 64, 0, 64, 0, 0, 255), 0);
    stbi_image_free(px);

    ecs_fini(world);
}

void TextSpans_remove_spans_restores_entity_color(void) {
    ecs_world_t *world = spans_world();
    spans_view(world);
    ecs_entity_t e = spans_text(world, 0, "II", 60, 0);
    ecs_set(world, e, Rgb, {255, 128, 0});
    spans_set(world, e, (TextSpan[]){{ 0, 2, { 0, 255, 0, 128 } }}, 1);
    spans_surface(world, 64, 64, "restore.png");

    unsigned char *px = spans_frame(world, "restore.png", 3);
    test_int(spans_ink(px, 64, 0, 64, 0, 64, 255, 128, 0), 0);
    stbi_image_free(px);

    ecs_remove(world, e, TextSpans);
    px = spans_frame(world, "restore.png", 1);
    test_assert(spans_ink(px, 64, 0, 64, 28, 36, 255, 128, 0) >= 6);
    stbi_image_free(px);

    ecs_fini(world);
}

void TextSpans_wrapped_paragraph_keeps_colors(void) {
    ecs_world_t *world = spans_world();
    spans_view(world);
    ecs_entity_t e = spans_text(world, 0, "II II", 40, 40);
    spans_set(world, e, (TextSpan[]){
        { 0, 2, { 255, 0, 0, 255 } },
        { 3, 5, { 0, 255, 0, 255 } }
    }, 2);
    spans_surface(world, 128, 128, "wrapped.png");

    unsigned char *px = spans_frame(world, "wrapped.png", 3);
    test_int(box_text_line(world, e, 0, NULL, NULL), 2);
    test_assert(spans_ink(px, 128, 0, 128, 0, 64, 255, 0, 0) >= 6);
    test_int(spans_ink(px, 128, 0, 128, 64, 128, 255, 0, 0), 0);
    test_assert(spans_ink(px, 128, 0, 128, 64, 128, 0, 255, 0) >= 6);
    test_int(spans_ink(px, 128, 0, 128, 0, 64, 0, 255, 0), 0);
    stbi_image_free(px);

    ecs_fini(world);
}

void TextSpans_world_space(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 2});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {0, 0, 0});
    ecs_set(world, panel, Scale, {0.01f, 0.01f, 0.01f});
    ecs_entity_t e = spans_text(world, panel, "II II", 60, 0);
    spans_set(world, e, (TextSpan[]){
        { 0, 2, { 255, 0, 0, 255 } },
        { 3, 5, { 0, 255, 0, 255 } }
    }, 2);
    spans_surface(world, 128, 128, "world.png");

    unsigned char *px = spans_frame(world, "world.png", 3);
    test_assert(spans_ink(px, 128, 0, 64, 56, 72, 255, 0, 0) >= 6);
    test_int(spans_ink(px, 128, 64, 128, 0, 128, 255, 0, 0), 0);
    test_assert(spans_ink(px, 128, 64, 128, 56, 72, 0, 255, 0) >= 6);
    test_int(spans_ink(px, 128, 0, 64, 0, 128, 0, 255, 0), 0);
    stbi_image_free(px);

    ecs_fini(world);
}

void TextSpans_render_to_texture(void) {
    ecs_world_t *world = spans_world();
    spans_surface(world, 128, 64, "main.png");
    ecs_entity_t texture = ecs_new(world);
    spans_view(world);
    ecs_entity_t image = ecs_new(world);
    ecs_set(world, image, Position, {0, 0, 0});
    ecs_set(world, image, Rect, {128, 64});
    ecs_set(world, image, Image, {.texture = texture, .u1 = 1, .v1 = 1});

    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t ui_camera = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, ui_camera, Position, {0, 0, 100});
    ecs_set(world, ui_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t e = spans_text(world, ui, "II II", 60, 0);
    spans_set(world, e, (TextSpan[]){
        { 0, 2, { 255, 0, 0, 255 } },
        { 3, 5, { 0, 255, 0, 255 } }
    }, 2);
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t ui_view = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, ui_view, View, {.camera = ui_camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t screen = ecs_new(world);
    ecs_set(world, screen, Surface, {.width = 128, .height = 64, .texture = texture,
        .view_root = root, .resolution_scale = 1});

    unsigned char *px = spans_frame(world, "main.png", 3);
    test_assert(spans_ink(px, 128, 0, 64, 28, 36, 255, 0, 0) >= 3);
    test_int(spans_ink(px, 128, 64, 128, 0, 64, 255, 0, 0), 0);
    test_assert(spans_ink(px, 128, 64, 128, 28, 36, 0, 255, 0) >= 3);
    test_int(spans_ink(px, 128, 0, 64, 0, 64, 0, 255, 0), 0);
    stbi_image_free(px);

    ecs_fini(world);
}

void TextSpans_no_extra_batches(void) {
    ecs_world_t *world = spans_world();
    spans_view(world);
    ecs_entity_t e = spans_text(world, 0, "II II", 60, 0);
    spans_surface(world, 128, 64, "batches.png");

    unsigned char *px = spans_frame(world, "batches.png", 3);
    stbi_image_free(px);
    box_render_draws_stats_t plain = {0}, colored = {0};
    box_render_draws_stats(world, &plain);
    test_int(plain.instances, 4);

    spans_set(world, e, (TextSpan[]){
        { 0, 1, { 255, 0, 0, 255 } },
        { 1, 2, { 0, 255, 0, 255 } },
        { 3, 4, { 0, 0, 255, 255 } }
    }, 3);
    px = spans_frame(world, "batches.png", 1);
    stbi_image_free(px);
    box_render_draws_stats(world, &colored);
    test_int(colored.batches, plain.batches);
    test_int(colored.instances, plain.instances);
    test_int(colored.dynamic, plain.dynamic);

    spans_set(world, e, (TextSpan[]){{ 0, 2, { 255, 0, 0, 100 } }}, 1);
    px = spans_frame(world, "batches.png", 1);
    stbi_image_free(px);
    box_render_draws_stats(world, &colored);
    test_int(colored.batches, plain.batches);
    test_int(colored.instances, plain.instances);

    ecs_fini(world);
}

/** Font file of the tests of styled spans, in the test assets */
#define SPANS_FONT "test/assets/fonts/B612-Regular.ttf"

/** Channel value a pixel passes to count as ink */
#define SPANS_LIT (128)

/** Create a white Text at the origin in a font of the test assets.
 *
 * @param world The world
 * @param parent Parent of the Text, 0 for none
 * @param value The string
 * @param size Em size
 * @return The entity
 */
static ecs_entity_t spans_styled(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *value,
    float size)
{
    ecs_entity_t font = ecs_lookup(world, "spans_font");
    if (!font) {
        font = ecs_entity(world, { .name = "spans_font" });
        ecs_set(world, font, Font, { .path = SPANS_FONT, .atlas_pages = 4 });
    }

    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Text, {.value = (char*)value, .size = size,
        .line_height = size * 1.25f, .font = font});
    ecs_set(world, e, Rgb, {255, 255, 255});

    return e;
}

/** Count the pixels of an image whose red channel passes SPANS_LIT.
 *
 * @param px The pixels
 * @param width Width of the image
 * @param height Height of the image
 * @return The pixels
 */
static int32_t spans_lit(
    const unsigned char *px,
    int32_t width,
    int32_t height)
{
    int32_t i, result = 0;
    for (i = 0; i < width * height; i ++) {
        result += px[i * 4] >= SPANS_LIT;
    }

    return result;
}

/** Get how far the ink at the top of an image is right of the ink at its bottom: the mean x of
 * the lit pixels of the top rows of the ink minus that of its bottom rows.
 *
 * @param px The pixels
 * @param width Width of the image
 * @param height Height of the image
 * @return The lean in pixels, positive to the right at the top
 */
static float spans_lean(
    const unsigned char *px,
    int32_t width,
    int32_t height)
{
    int32_t x, y, top = -1, bottom = -1;
    double sum[2] = {0}, count[2] = {0};
    for (y = 0; y < height; y ++) {
        for (x = 0; x < width; x ++) {
            if (px[(y * width + x) * 4] >= SPANS_LIT) {
                top = top < 0 ? y : top;
                bottom = y;
            }
        }
    }

    test_assert(top >= 0);
    test_assert(bottom - top > 12);
    for (y = top; y <= bottom; y ++) {
        int32_t band = y < top + 5 ? 0 : (y > bottom - 5 ? 1 : -1);
        for (x = 0; band >= 0 && x < width; x ++) {
            if (px[(y * width + x) * 4] >= SPANS_LIT) {
                sum[band] += x;
                count[band] ++;
            }
        }
    }

    test_assert(count[0] > 0 && count[1] > 0);

    return (float)(sum[0] / count[0] - sum[1] / count[1]);
}

void TextSpans_bold_more_coverage(void) {
    float sizes[3] = { 20, 36, 60 };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_world_t *world = spans_world();
        spans_view(world);
        ecs_entity_t e = spans_styled(world, 0, "Hello", sizes[i]);
        spans_surface(world, 256, 96, "bold.png");

        unsigned char *px = spans_frame(world, "bold.png", 3);
        int32_t regular = spans_lit(px, 256, 96);
        stbi_image_free(px);
        test_assert(regular > 0);

        spans_set(world, e, (TextSpan[]){
            { 0, 5, {0}, .bold = true, .keep_color = true }}, 1);
        px = spans_frame(world, "bold.png", 1);
        int32_t bold = spans_lit(px, 256, 96);
        stbi_image_free(px);
        test_assert(bold > regular + regular / 10);

        ecs_fini(world);
    }
}

void TextSpans_italic_leans(void) {
    ecs_world_t *world = spans_world();
    spans_view(world);
    ecs_entity_t e = spans_styled(world, 0, "I", 80);
    spans_surface(world, 128, 128, "italic.png");

    unsigned char *px = spans_frame(world, "italic.png", 3);
    float regular = spans_lean(px, 128, 128);
    int32_t ink = spans_lit(px, 128, 128);
    stbi_image_free(px);
    test_assert(fabsf(regular) < 1.5f);

    spans_set(world, e, (TextSpan[]){{ 0, 1, {0}, .italic = true, .keep_color = true }}, 1);
    px = spans_frame(world, "italic.png", 1);
    float italic = spans_lean(px, 128, 128);
    int32_t leaning = spans_lit(px, 128, 128);
    stbi_image_free(px);

    test_assert(italic > regular + 6.0f);
    test_assert(leaning > ink * 9 / 10);
    test_assert(leaning < ink * 11 / 10);

    ecs_fini(world);
}

void TextSpans_regular_unchanged(void) {
    ecs_world_t *world = spans_world();
    spans_view(world);
    ecs_entity_t e = spans_styled(world, 0, "Hello", 36);
    spans_surface(world, 256, 96, "regular.png");

    unsigned char *px = spans_frame(world, "regular.png", 3);
    unsigned char *none = ecs_os_malloc(256 * 96 * 4);
    ecs_os_memcpy(none, px, 256 * 96 * 4);
    stbi_image_free(px);

    spans_set(world, e, (TextSpan[]){
        { 0, 5, {0}, .bold = false, .italic = false, .keep_color = true }}, 1);
    px = spans_frame(world, "regular.png", 1);
    test_assert(!memcmp(px, none, 256 * 96 * 4));
    stbi_image_free(px);

    spans_set(world, e, (TextSpan[]){{ 0, 5, { 255, 255, 255, 255 } }}, 1);
    px = spans_frame(world, "regular.png", 1);
    test_assert(!memcmp(px, none, 256 * 96 * 4));
    stbi_image_free(px);

    ecs_os_free(none);
    ecs_fini(world);
}

void TextSpans_same_batches(void) {
    ecs_world_t *world = spans_world();
    spans_view(world);
    ecs_entity_t e = spans_styled(world, 0, "II II", 60);
    spans_surface(world, 128, 64, "styled_batches.png");

    unsigned char *px = spans_frame(world, "styled_batches.png", 3);
    stbi_image_free(px);
    box_render_draws_stats_t plain = {0}, styled = {0};
    box_render_draws_stats(world, &plain);
    test_int(plain.instances, 4);

    spans_set(world, e, (TextSpan[]){
        { 0, 1, {0}, .bold = true, .keep_color = true },
        { 1, 2, {0}, .italic = true, .keep_color = true },
        { 3, 5, { 255, 0, 0, 255 }, .bold = true, .italic = true }
    }, 3);
    px = spans_frame(world, "styled_batches.png", 1);
    stbi_image_free(px);
    box_render_draws_stats(world, &styled);
    test_int(styled.batches, plain.batches);
    test_int(styled.instances, plain.instances);
    test_int(styled.dynamic, plain.dynamic);

    ecs_fini(world);
}

void TextSpans_world_text_styled(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 2});
    ecs_set(world, camera, Rotation, {0, 0, 0.2f});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {0, 0, 0});
    ecs_set(world, panel, Scale, {0.01f, 0.01f, 0.01f});
    ecs_entity_t e = spans_styled(world, panel, "I", 100);
    spans_surface(world, 128, 128, "world_styled.png");

    unsigned char *px = spans_frame(world, "world_styled.png", 3);
    float upright = spans_lean(px, 128, 128);
    int32_t regular = spans_lit(px, 128, 128);
    stbi_image_free(px);

    spans_set(world, e, (TextSpan[]){
        { 0, 1, {0}, .italic = true, .keep_color = true }}, 1);
    px = spans_frame(world, "world_styled.png", 1);
    float italic = spans_lean(px, 128, 128);
    stbi_image_free(px);
    test_assert(italic > upright + 3.0f);

    spans_set(world, e, (TextSpan[]){
        { 0, 1, {0}, .bold = true, .keep_color = true }}, 1);
    px = spans_frame(world, "world_styled.png", 1);
    int32_t bold = spans_lit(px, 128, 128);
    stbi_image_free(px);
    test_assert(bold > regular + regular / 10);

    ecs_fini(world);
}
