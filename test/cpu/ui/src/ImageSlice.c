/**
 * @file ImageSlice.c
 * @brief ImageSlice: the nine parts of a sliced Image for an element size, scale and fill, the
 * texture coordinates the shader maps a point to, and the component in script and prefabs.
 */

#include <box.h>
#include <bake_test.h>
#include "draw.h"

/** Texels of the side of the frame texture the tests slice, as test/integration/visual/assets/
 * frame.png, with insets of FRAME_INSET */
#define FRAME (48.0f)

/** Inset of the corners of the frame texture */
#define FRAME_INSET (16.0f)

/** Tolerance of the coordinates */
#define SLICE_EPS (1e-4f)

/** Check two floats within SLICE_EPS.
 *
 * @param a The value
 * @param b The expected value
 */
static void slice_near(
    float a,
    float b)
{
    if (fabsf(a - b) > SLICE_EPS) {
        test_flt(a, b);
    }
}

/** Resolve a frame of the whole 48 texel texture with insets of 16 on an element.
 *
 * @param slice The ImageSlice
 * @param width Width of the element
 * @param height Height of the element
 * @param out Receives the parts
 */
static void slice_frame(
    const ImageSlice *slice,
    float width,
    float height,
    box_image_slice_t *out)
{
    Image image = { .u1 = 1.0f, .v1 = 1.0f };
    test_bool(box_image_slice_resolve(&image, slice, FRAME, FRAME, width, height, out), true);
}

/** An ImageSlice with insets of 16 texels.
 *
 * @param scale The scale
 * @return The value
 */
static ImageSlice slice_insets(
    float scale)
{
    return (ImageSlice){ .left = FRAME_INSET, .top = FRAME_INSET, .right = FRAME_INSET,
        .bottom = FRAME_INSET, .scale = scale };
}

void ImageSlice_corners_at_texel_size(void) {
    ImageSlice slice = slice_insets(1.0f);
    box_image_slice_t parts;
    slice_frame(&slice, 200, 100, &parts);

    /* The corners are 16 by 16, the edges and the center fill the rest */
    slice_near(parts.x[0], 0);
    slice_near(parts.x[1], 16);
    slice_near(parts.x[2], 184);
    slice_near(parts.x[3], 200);
    slice_near(parts.y[0], 0);
    slice_near(parts.y[1], 16);
    slice_near(parts.y[2], 84);
    slice_near(parts.y[3], 100);
    slice_near(parts.u[0], 0);
    slice_near(parts.u[1], 1.0f / 3.0f);
    slice_near(parts.u[2], 2.0f / 3.0f);
    slice_near(parts.u[3], 1);
    slice_near(parts.v[1], 1.0f / 3.0f);
    slice_near(parts.v[2], 2.0f / 3.0f);
    slice_near(parts.scale, 1);
    test_bool(parts.hollow, false);

    /* Stretched parts have no period */
    int32_t i;
    for (i = 0; i < 4; i ++) {
        slice_near(parts.period[i], 0);
    }
}

void ImageSlice_scale(void) {
    ImageSlice slice = slice_insets(2.0f);
    box_image_slice_t parts;
    slice_frame(&slice, 200, 100, &parts);
    slice_near(parts.x[1], 32);
    slice_near(parts.x[2], 168);
    slice_near(parts.y[1], 32);
    slice_near(parts.y[2], 68);
    slice_near(parts.scale, 2);

    /* The texture coordinates of the slices do not depend on the scale */
    slice_near(parts.u[1], 1.0f / 3.0f);
    slice_near(parts.v[2], 2.0f / 3.0f);
}

void ImageSlice_invalid_scale_is_one(void) {
    ImageSlice slice = slice_insets(0.0f);
    box_image_slice_t parts;
    slice_frame(&slice, 200, 100, &parts);
    slice_near(parts.x[1], 16);
    slice_near(parts.scale, 1);
    slice.scale = -3;
    slice_frame(&slice, 200, 100, &parts);
    slice_near(parts.x[1], 16);
    slice.scale = NAN;
    slice_frame(&slice, 200, 100, &parts);
    slice_near(parts.x[1], 16);
}

void ImageSlice_corners_do_not_fit_width(void) {
    /* 24 is less than the 32 of both corners: they scale by 0.75 and meet in the middle */
    ImageSlice slice = slice_insets(1.0f);
    box_image_slice_t parts;
    slice_frame(&slice, 24, 100, &parts);
    slice_near(parts.scale, 0.75f);
    slice_near(parts.x[1], 12);
    slice_near(parts.x[2], 12);
    slice_near(parts.x[3], 24);

    /* The rows scale with them, so the corners keep their aspect */
    slice_near(parts.y[1], 12);
    slice_near(parts.y[2], 88);
}

void ImageSlice_corners_do_not_fit_height(void) {
    ImageSlice slice = slice_insets(2.0f);
    box_image_slice_t parts;
    slice_frame(&slice, 300, 32, &parts);

    /* 64 of scaled corners in a height of 32: the scale halves to 1 */
    slice_near(parts.scale, 1);
    slice_near(parts.y[1], 16);
    slice_near(parts.y[2], 16);
    slice_near(parts.x[1], 16);
    slice_near(parts.x[2], 284);
}

void ImageSlice_corners_do_not_fit_both(void) {
    /* The smaller of the two fits wins: 32 / 64 across, 48 / 64 down */
    ImageSlice slice = slice_insets(2.0f);
    box_image_slice_t parts;
    slice_frame(&slice, 32, 48, &parts);
    slice_near(parts.scale, 1);
    slice_near(parts.x[1], 16);
    slice_near(parts.x[2], 16);
    slice_near(parts.y[1], 16);
    slice_near(parts.y[2], 32);
}

void ImageSlice_insets_clamped(void) {
    ImageSlice slice = {
        .left = 40, .right = 40, .top = -5, .bottom = 100, .scale = 1
    };
    box_image_slice_t parts;
    slice_frame(&slice, 400, 400, &parts);

    /* The left inset keeps its 40 texels, the right one the 8 left of the area */
    slice_near(parts.u[1], 40.0f / 48.0f);
    slice_near(parts.u[2], 40.0f / 48.0f);
    slice_near(parts.x[1], 40);
    slice_near(parts.x[2], 392);

    /* A negative inset is 0, one past the area the whole area */
    slice_near(parts.v[1], 0);
    slice_near(parts.v[2], 0);
    slice_near(parts.y[1], 0);
    slice_near(parts.y[2], 352);
}

void ImageSlice_image_area(void) {
    /* The insets count from the area of the Image, a 48 texel square of a 96 by 192 texture */
    Image image = { .u0 = 0.25f, .v0 = 0.5f, .u1 = 0.75f, .v1 = 0.75f };
    ImageSlice slice = slice_insets(1.0f);
    box_image_slice_t parts;
    test_bool(box_image_slice_resolve(&image, &slice, 96, 192, 100, 100, &parts), true);
    slice_near(parts.u[0], 0.25f);
    slice_near(parts.u[1], 0.25f + 16.0f / 96.0f);
    slice_near(parts.u[2], 0.75f - 16.0f / 96.0f);
    slice_near(parts.u[3], 0.75f);
    slice_near(parts.v[0], 0.5f);
    slice_near(parts.v[1], 0.5f + 16.0f / 192.0f);
    slice_near(parts.v[2], 0.75f - 16.0f / 192.0f);
    slice_near(parts.v[3], 0.75f);
    slice_near(parts.x[1], 16);
    slice_near(parts.y[2], 84);
}

void ImageSlice_flipped_area(void) {
    /* An area from right to left slices from its start, which is the right of the texture */
    Image image = { .u0 = 1, .v0 = 0, .u1 = 0, .v1 = 1 };
    ImageSlice slice = slice_insets(1.0f);
    slice.right = 8;
    box_image_slice_t parts;
    test_bool(box_image_slice_resolve(&image, &slice, 48, 48, 100, 100, &parts), true);
    slice_near(parts.u[0], 1);
    slice_near(parts.u[1], 1.0f - 16.0f / 48.0f);
    slice_near(parts.u[2], 8.0f / 48.0f);
    slice_near(parts.u[3], 0);
    slice_near(parts.x[2], 92);
}

void ImageSlice_tile_periods(void) {
    ImageSlice slice = slice_insets(2.0f);
    slice.edges = SliceTile;
    box_image_slice_t parts;
    slice_frame(&slice, 200, 100, &parts);

    /* The middle of the texture is 16 texels, 32 units at a scale of 2 */
    slice_near(parts.period[0], 32);
    slice_near(parts.period[1], 32);
    slice_near(parts.period[2], 0);
    slice_near(parts.period[3], 0);

    slice.edges = SliceStretch;
    slice.center = SliceTile;
    slice_frame(&slice, 200, 100, &parts);
    slice_near(parts.period[0], 0);
    slice_near(parts.period[1], 0);
    slice_near(parts.period[2], 32);
    slice_near(parts.period[3], 32);
}

void ImageSlice_tile_periods_follow_fit(void) {
    /* Corners that do not fit scale the tiles with them */
    ImageSlice slice = slice_insets(1.0f);
    slice.edges = SliceTile;
    slice.center = SliceTile;
    box_image_slice_t parts;
    slice_frame(&slice, 100, 16, &parts);
    slice_near(parts.scale, 0.5f);
    slice_near(parts.period[0], 8);
    slice_near(parts.period[3], 8);
}

void ImageSlice_map_corners(void) {
    ImageSlice slice = slice_insets(2.0f);
    box_image_slice_t parts;
    float uv[2];
    slice_frame(&slice, 200, 100, &parts);

    /* A corner maps at its scale from the corner of the element, whatever its size */
    test_bool(box_image_slice_map(&parts, 10, 6, uv), true);
    slice_near(uv[0], 5.0f / 48.0f);
    slice_near(uv[1], 3.0f / 48.0f);
    test_bool(box_image_slice_map(&parts, 190, 94, uv), true);
    slice_near(uv[0], 1.0f - 5.0f / 48.0f);
    slice_near(uv[1], 1.0f - 3.0f / 48.0f);
    slice_frame(&slice, 400, 300, &parts);
    test_bool(box_image_slice_map(&parts, 10, 6, uv), true);
    slice_near(uv[0], 5.0f / 48.0f);
    slice_near(uv[1], 3.0f / 48.0f);
    test_bool(box_image_slice_map(&parts, 390, 294, uv), true);
    slice_near(uv[0], 1.0f - 5.0f / 48.0f);
    slice_near(uv[1], 1.0f - 3.0f / 48.0f);
}

void ImageSlice_map_stretch(void) {
    ImageSlice slice = slice_insets(1.0f);
    box_image_slice_t parts;
    float uv[2];
    slice_frame(&slice, 200, 100, &parts);

    /* The top edge spans 16..184: its middle is the middle of the texture */
    test_bool(box_image_slice_map(&parts, 100, 8, uv), true);
    slice_near(uv[0], 0.5f);
    slice_near(uv[1], 8.0f / 48.0f);

    /* A quarter along it is a quarter along the middle of the texture */
    test_bool(box_image_slice_map(&parts, 16 + 42, 8, uv), true);
    slice_near(uv[0], 1.0f / 3.0f + 0.25f / 3.0f);

    /* The left edge spans 16..84 down */
    test_bool(box_image_slice_map(&parts, 8, 50, uv), true);
    slice_near(uv[0], 8.0f / 48.0f);
    slice_near(uv[1], 0.5f);

    /* The center stretches both ways */
    test_bool(box_image_slice_map(&parts, 100, 50, uv), true);
    slice_near(uv[0], 0.5f);
    slice_near(uv[1], 0.5f);
}

void ImageSlice_map_tile(void) {
    ImageSlice slice = slice_insets(1.0f);
    slice.edges = SliceTile;
    box_image_slice_t parts;
    float uv[2];
    slice_frame(&slice, 200, 100, &parts);

    /* The top edge repeats every 16 units from 16: 36 is a quarter into the second tile */
    test_bool(box_image_slice_map(&parts, 36, 8, uv), true);
    slice_near(uv[0], 1.0f / 3.0f + 0.25f / 3.0f);
    test_bool(box_image_slice_map(&parts, 36 + 16 * 5, 8, uv), true);
    slice_near(uv[0], 1.0f / 3.0f + 0.25f / 3.0f);

    /* The last tile is cut: 184 is 168 from the start, half of the eleventh tile */
    test_bool(box_image_slice_map(&parts, 183.9f, 8, uv), true);
    test_assert(uv[0] < 1.0f / 3.0f + 0.5f / 3.0f + 0.01f);
    test_assert(uv[0] > 1.0f / 3.0f + 0.5f / 3.0f - 0.01f);

    /* The left edge repeats down */
    test_bool(box_image_slice_map(&parts, 8, 16 + 16 + 8, uv), true);
    slice_near(uv[1], 0.5f);

    /* The center stretches */
    test_bool(box_image_slice_map(&parts, 100, 50, uv), true);
    slice_near(uv[0], 0.5f);
    slice_near(uv[1], 0.5f);
}

void ImageSlice_map_center_tile(void) {
    ImageSlice slice = slice_insets(1.0f);
    slice.center = SliceTile;
    box_image_slice_t parts;
    float uv[2];
    slice_frame(&slice, 200, 100, &parts);

    /* 16 + 16 * 3 + 4 is a quarter into a tile of the center along x */
    test_bool(box_image_slice_map(&parts, 68, 16 + 16 + 8, uv), true);
    slice_near(uv[0], 1.0f / 3.0f + 0.25f / 3.0f);
    slice_near(uv[1], 0.5f);

    /* The edges stretch */
    test_bool(box_image_slice_map(&parts, 100, 8, uv), true);
    slice_near(uv[0], 0.5f);
}

void ImageSlice_map_hollow(void) {
    ImageSlice slice = slice_insets(1.0f);
    slice.hollow = true;
    box_image_slice_t parts;
    float uv[2];
    slice_frame(&slice, 200, 100, &parts);
    test_bool(parts.hollow, true);
    test_bool(box_image_slice_map(&parts, 100, 50, uv), false);
    test_bool(box_image_slice_map(&parts, 100, 8, uv), true);
    test_bool(box_image_slice_map(&parts, 8, 50, uv), true);
    test_bool(box_image_slice_map(&parts, 8, 8, uv), true);
}

void ImageSlice_no_texture_is_plain(void) {
    Image image = { .u0 = 0.25f, .v0 = 0, .u1 = 0.75f, .v1 = 1 };
    ImageSlice slice = slice_insets(1.0f);
    slice.hollow = true;
    box_image_slice_t parts;
    float uv[2];
    test_bool(box_image_slice_resolve(&image, &slice, 0, 0, 200, 100, &parts), false);

    /* Without a texture size the element maps to the area as a plain Image, center included */
    test_bool(box_image_slice_map(&parts, 0, 0, uv), true);
    slice_near(uv[0], 0.25f);
    slice_near(uv[1], 0);
    test_bool(box_image_slice_map(&parts, 100, 50, uv), true);
    slice_near(uv[0], 0.5f);
    slice_near(uv[1], 0.5f);
    test_bool(box_image_slice_map(&parts, 200, 100, uv), true);
    slice_near(uv[0], 0.75f);
    slice_near(uv[1], 1);
}

/** A script with a frame texture entity, a sliced Rect, a sliced Circle and a prefab with an
 * ImageSlice and its instance */
#define SLICE_SCENE \
    "using box.geometry\n" \
    "frame {}\n" \
    "panel {\n" \
    "  Rect: {200, 100}\n" \
    "  Image: {texture: frame}\n" \
    "  ImageSlice: {left: 16, top: 16, right: 16, bottom: 16, scale: 2, edges: SliceTile, " \
    "hollow: true}\n" \
    "}\n" \
    "disc {\n" \
    "  Circle: {40}\n" \
    "  Image: {texture: frame}\n" \
    "  ImageSlice: {left: 16, top: 16, right: 16, bottom: 16}\n" \
    "}\n" \
    "prefab Framed {\n" \
    "  Rect: {100, 100}\n" \
    "  Image: {texture: frame}\n" \
    "  ImageSlice: {left: 8, top: 8, right: 8, bottom: 8, center: SliceTile}\n" \
    "}\n" \
    "framed : Framed {}\n"

/** Create a world that runs the slice scene.
 *
 * @return The world
 */
static ecs_world_t* slice_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "slice", SLICE_SCENE, NULL) == 0);
    ecs_progress(world, 0);

    return world;
}

void ImageSlice_script(void) {
    ecs_world_t *world = slice_world();
    ecs_entity_t panel = ecs_lookup(world, "panel");
    test_assert(panel != 0);
    const ImageSlice *slice = ecs_get(world, panel, ImageSlice);
    test_assert(slice != NULL);
    test_flt(slice->left, 16);
    test_flt(slice->bottom, 16);
    test_flt(slice->scale, 2);
    test_int(slice->edges, SliceTile);
    test_int(slice->center, SliceStretch);
    test_bool(slice->hollow, true);
    const Shape2D *shape = ecs_get(world, panel, Shape2D);
    test_assert(shape != NULL);
    test_bool(shape->sliced, true);

    /* A slice without a scale in script keeps the default of 1 */
    slice = ecs_get(world, ecs_lookup(world, "disc"), ImageSlice);
    test_assert(slice != NULL);
    test_flt(slice->scale, 1);
    ecs_fini(world);
}

void ImageSlice_circle_not_sliced(void) {
    ecs_world_t *world = slice_world();
    const Shape2D *shape = ecs_get(world, ecs_lookup(world, "disc"), Shape2D);
    test_assert(shape != NULL);
    test_bool(shape->sliced, false);
    ecs_fini(world);
}

void ImageSlice_remove(void) {
    ecs_world_t *world = slice_world();
    ecs_entity_t panel = ecs_lookup(world, "panel");
    ecs_remove(world, panel, ImageSlice);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, panel, Shape2D)->sliced, false);

    /* An ImageSlice without an Image slices nothing */
    ecs_remove(world, panel, Image);
    ecs_set(world, panel, ImageSlice, {.left = 4, .scale = 1});
    ecs_progress(world, 0);
    test_bool(ecs_get(world, panel, Shape2D)->sliced, false);
    ecs_set(world, panel, Image, {.texture = ecs_lookup(world, "frame"), .u1 = 1, .v1 = 1});
    ecs_progress(world, 0);
    test_bool(ecs_get(world, panel, Shape2D)->sliced, true);
    ecs_fini(world);
}

void ImageSlice_prefab(void) {
    ecs_world_t *world = slice_world();
    ecs_entity_t framed = ecs_lookup(world, "framed");
    test_assert(framed != 0);
    const ImageSlice *slice = ecs_get(world, framed, ImageSlice);
    test_assert(slice != NULL);
    test_flt(slice->left, 8);
    test_int(slice->center, SliceTile);
    test_bool(ecs_get(world, framed, Shape2D)->sliced, true);
    ecs_fini(world);
}

void ImageSlice_template(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "slice",
        "using box.geometry\n"
        "frame {}\n"
        "template Frame {\n"
        "  prop s: f32 = 1\n"
        "  Rect: {100, 60}\n"
        "  Image: {texture: frame}\n"
        "  ImageSlice: {left: 12, top: 12, right: 12, bottom: 12, scale: s}\n"
        "}\n"
        "Frame big(s: 3)\n", NULL) == 0);
    ecs_progress(world, 0);
    ecs_entity_t big = ecs_lookup(world, "big");
    test_assert(big != 0);
    const ImageSlice *slice = ecs_get(world, big, ImageSlice);
    test_assert(slice != NULL);
    test_flt(slice->left, 12);
    test_flt(slice->scale, 3);
    test_bool(ecs_get(world, big, Shape2D)->sliced, true);
    ecs_fini(world);
}

void ImageSlice_record_waits_for_texture(void) {
    ecs_world_t *world = slice_world();
    ecs_entity_t panel = ecs_lookup(world, "panel");
    box_render_slice_t record;

    /* Before the texture has a size the record is the plain Image, its center drawn */
    test_bool(box_draws_slice_record(world, panel, &record), true);
    slice_near(record.x[0], 0);
    slice_near(record.x[1], 0);
    slice_near(record.x[2], 1);
    slice_near(record.x[3], 1);
    slice_near(record.u[1], 0);
    slice_near(record.u[2], 1);
    slice_near(record.period[0], 0);
    slice_near(record.flags[0], 0);

    /* With a Texture of 48 texels the corners are 32 units of 200 by 100 at a scale of 2 */
    ecs_set(world, ecs_lookup(world, "frame"), Texture, {48, 48});
    test_bool(box_draws_slice_record(world, panel, &record), true);
    slice_near(record.x[1], 32.0f / 200.0f);
    slice_near(record.x[2], 168.0f / 200.0f);
    slice_near(record.x[3], 1);
    slice_near(record.y[1], 32.0f / 100.0f);
    slice_near(record.y[2], 68.0f / 100.0f);
    slice_near(record.u[1], 1.0f / 3.0f);
    slice_near(record.v[2], 2.0f / 3.0f);

    /* Tiled edges repeat every 32 units, the center stretches, the record is hollow */
    slice_near(record.period[0], 32.0f / 200.0f);
    slice_near(record.period[1], 32.0f / 100.0f);
    slice_near(record.period[2], 0);
    slice_near(record.flags[0], 1);

    /* A Circle has no record */
    test_bool(box_draws_slice_record(world, ecs_lookup(world, "disc"), &record), false);
    ecs_fini(world);
}

/** Get the slice record of a sliced shape in the clip records of the draws.
 *
 * @param world The world
 * @param d The draws
 * @param e The shape
 * @return The record
 */
static const box_render_slice_t* slice_record_of(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    int32_t index = ecs_get(world, e, DrawSlot)->slice;
    test_assert(index > 0);

    return (const box_render_slice_t*)ecs_vec_get_t(
        &d->clips.records, box_render_clip_t, index);
}

void ImageSlice_record_lifecycle(void) {
    // ui/image_slice
    ecs_world_t *world = slice_world();
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    test_assert(system != 0);
    box_render_draws_t *d = ecs_system_get(world, system)->ctx;
    ecs_entity_t panel = ecs_lookup(world, "panel");

    /* The panel and the instance of the prefab have a record, the plain Image until the texture
     * has a size, the Circle has none */
    test_int(ecs_map_count(&d->clips.slices), 2);
    test_int(ecs_get(world, ecs_lookup(world, "disc"), DrawSlot)->slice, 0);
    int32_t index = ecs_get(world, panel, DrawSlot)->slice;
    slice_near(slice_record_of(world, d, panel)->x[1], 0);

    /* The texture finishing its load slices the panel without a change of the panel */
    ecs_set(world, ecs_lookup(world, "frame"), Texture, {48, 48});
    ecs_progress(world, 0);
    slice_near(slice_record_of(world, d, panel)->x[1], 32.0f / 200.0f);

    /* A resized panel gets its record computed again, at the same index */
    ecs_set(world, panel, Rect, {400, 100});
    ecs_progress(world, 0);
    test_int(ecs_get(world, panel, DrawSlot)->slice, index);
    slice_near(slice_record_of(world, d, panel)->x[1], 32.0f / 400.0f);

    /* Without its ImageSlice the panel frees its record, which the next sliced shape reuses */
    ecs_remove(world, panel, ImageSlice);
    ecs_progress(world, 0);
    test_int(ecs_get(world, panel, DrawSlot)->slice, 0);
    test_int(ecs_map_count(&d->clips.slices), 1);
    ecs_set(world, panel, ImageSlice, {.left = 8, .top = 8, .right = 8, .bottom = 8,
        .scale = 1});
    ecs_progress(world, 0);
    test_int(ecs_get(world, panel, DrawSlot)->slice, index);
    slice_near(slice_record_of(world, d, panel)->x[1], 8.0f / 400.0f);

    /* A deleted shape frees its record */
    ecs_delete(world, panel);
    ecs_progress(world, 0);
    test_int(ecs_map_count(&d->clips.slices), 1);
    ecs_fini(world);
}

void ImageSlice_record_surface_texture(void) {
    // ui/image_slice
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    test_assert(ecs_script_run(world, "slice", SLICE_SCENE, NULL) == 0);
    ecs_entity_t frame = ecs_lookup(world, "frame");
    ecs_entity_t panel = ecs_lookup(world, "panel");
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    /* An offscreen surface of 24 points at 2 pixels per point renders into the frame: an image
     * of 48 pixels, the size the insets count in, not the 24 points */
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 24, .height = 24, .pixel_ratio = 2,
        .resolution_scale = 1, .texture = frame});
    int32_t i, size[2];
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    test_assert(!ecs_has(world, frame, Texture));
    test_bool(box_surface_texture_size(world, frame, &size[0], &size[1]), true);
    test_int(size[0], 48);
    test_int(size[1], 48);
    test_bool(box_surface_texture_size(world, panel, &size[0], &size[1]), false);
    test_int(size[0], 0);
    test_int(size[1], 0);

    /* The corners are 16 texels of 48 at a scale of 2, 32 units of 200 by 100 */
    box_render_slice_t record;
    test_bool(box_draws_slice_record(world, panel, &record), true);
    slice_near(record.x[1], 32.0f / 200.0f);
    slice_near(record.y[1], 32.0f / 100.0f);
    slice_near(record.u[1], 1.0f / 3.0f);
    slice_near(record.v[2], 2.0f / 3.0f);

    /* The record in the clip buffer follows a resize of the surface without a change of the
     * panel: 96 pixels make the insets a sixth of the image */
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    test_assert(system != 0);
    box_render_draws_t *d = ecs_system_get(world, system)->ctx;
    slice_near(slice_record_of(world, d, panel)->u[1], 1.0f / 3.0f);
    ecs_set(world, surface, Surface, {.width = 48, .height = 48, .pixel_ratio = 2,
        .resolution_scale = 1, .texture = frame});
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    test_bool(box_surface_texture_size(world, frame, &size[0], &size[1]), true);
    test_int(size[0], 96);
    slice_near(slice_record_of(world, d, panel)->x[1], 32.0f / 200.0f);
    slice_near(slice_record_of(world, d, panel)->u[1], 1.0f / 6.0f);
    slice_near(slice_record_of(world, d, panel)->v[2], 5.0f / 6.0f);

    /* resolution_scale only changes the size the views render at, not the image */
    ecs_set(world, surface, Surface, {.width = 48, .height = 48, .pixel_ratio = 2,
        .resolution_scale = 2, .texture = frame});
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    test_bool(box_surface_texture_size(world, frame, &size[0], &size[1]), true);
    test_int(size[0], 96);
    test_int(size[1], 96);
    test_int(ecs_get(world, surface, SurfaceStatus)->render_width, 48);
    slice_near(slice_record_of(world, d, panel)->u[1], 1.0f / 6.0f);

    ecs_fini(world);
    box_render_null_free(null);
}
