/**
 * @file TransitionBoxShadow.c
 * @brief Tests that the shadow of a 2D shape (BoxShadow) follows the transform, opacity and
 * tint transitions of its shape and of the ancestors of its shape with the GPU backend: it
 * renders where and as strong as the shape renders, not at the target the game thread sees.
 *
 * The world advances a quarter second per frame and the transitions take one second, Linear.
 * Each case compares the frame halfway through with a scene set up in that state without a
 * transition; the worlds are made one after the other, so no two render at once.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge in pixels of the images */
#define TBS_SIZE (64)

/** Seconds every frame advances the world */
#define TBS_DELTA (0.25f)

/** Largest difference of a channel between a frame in mid transition and the scene set up in
 * that state: the edges of the shape and the blur of the shadow may round apart */
#define TBS_TOLERANCE (4)

/** Largest difference of the linear darkness of the shadow from the opacity times its darkness
 * at opacity 1: a channel of 8 bits rounds by up to 0.01 in the light of the background */
#define TBS_DARK_TOLERANCE (0.02f)

/** Captures after a change that render at progress 1/2 */
#define TBS_MID (3)

/** Captures after a change that render at the target */
#define TBS_END (5)

/** The pose of an entity */
typedef struct tbs_pose_t {
    float x;        /**< Position x */
    float y;        /**< Position y */
    float scale;    /**< Scale of x and y */
    float angle;    /**< Rotation about z */
} tbs_pose_t;

/** The shadow every case draws: black, offset down and to the right, blurred */
static const BoxShadow tbs_shadow = {
    .offset_x = 6, .offset_y = -6, .blur = 4, .color = {0, 0, 0, 200}};

/** Create a headless world with an ortho camera of one unit per pixel and a white clear in a
 * 64x64 image: world x = 0 lands on column 32, and y grows up while the rows grow down.
 *
 * @return The world
 */
static ecs_world_t* tbs_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {255, 255, 255, 255}}});

    return world;
}

/** Set the pose of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param pose The pose
 */
static void tbs_pose(
    ecs_world_t *world,
    ecs_entity_t e,
    tbs_pose_t pose)
{
    ecs_set(world, e, Position, {pose.x, pose.y, 0});
    ecs_set(world, e, Scale, {pose.scale, pose.scale, 1});
    ecs_set(world, e, Rotation, {0, 0, pose.angle});
}

/** Add a red 16x16 rect, with the shadow of the cases or without one.
 *
 * @param world The world
 * @param parent The parent, 0 for none
 * @param pose Its pose
 * @param shadowed Whether it has a BoxShadow
 * @return The rect
 */
static ecs_entity_t tbs_rect(
    ecs_world_t *world,
    ecs_entity_t parent,
    tbs_pose_t pose,
    bool shadowed)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    tbs_pose(world, e, pose);
    ecs_set(world, e, Rect, {16, 16});
    ecs_set(world, e, Rgb, {255, 0, 0});
    if (shadowed) {
        ecs_set_ptr(world, e, BoxShadow, &tbs_shadow);
    }

    return e;
}

/** Add a parent without a shape.
 *
 * @param world The world
 * @param pose Its pose
 * @return The parent
 */
static ecs_entity_t tbs_parent(
    ecs_world_t *world,
    tbs_pose_t pose)
{
    ecs_entity_t e = ecs_new(world);
    tbs_pose(world, e, pose);

    return e;
}

/** Give an entity a one second Linear transition of a component.
 *
 * @param world The world
 * @param e The entity
 * @param component The component
 */
static void tbs_transition(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component)
{
    box_transition_set(world, e, component, &(TransitionSpec){ .duration = 1.0f });
}

/** Give an entity one second Linear transitions of Position, Scale and Rotation.
 *
 * @param world The world
 * @param e The entity
 */
static void tbs_transition_pose(
    ecs_world_t *world,
    ecs_entity_t e)
{
    tbs_transition(world, e, ecs_id(Position));
    tbs_transition(world, e, ecs_id(Scale));
    tbs_transition(world, e, ecs_id(Rotation));
}

/** Render a frame that advances the world by TBS_DELTA into an image file and load it.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @return RGBA pixels of a TBS_SIZE square image, freed with stbi_image_free
 */
static unsigned char* tbs_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = TBS_SIZE, .height = TBS_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, TBS_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, TBS_SIZE);
    test_int(h, TBS_SIZE);
    ecs_os_free(path);

    return px;
}

/** Render a number of frames and keep the last.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @param count Frames to render, at least 1
 * @return The pixels of the last frame, freed with stbi_image_free
 */
static unsigned char* tbs_capture_n(
    ecs_world_t *world,
    const char *file,
    int32_t count)
{
    int32_t i;
    for (i = 1; i < count; i ++) {
        stbi_image_free(tbs_capture(world, file));
    }

    return tbs_capture(world, file);
}

/** Get a pixel of a capture.
 *
 * @param px The pixels
 * @param x The column
 * @param y The row, 0 at the top
 * @return The RGBA of the pixel
 */
static const unsigned char* tbs_at(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return &px[(y * TBS_SIZE + x) * 4];
}

/** Get the largest difference of a channel between two captures in an area.
 *
 * @param a The first capture
 * @param b The second capture
 * @param x0 First column
 * @param y0 First row
 * @param x1 Last column
 * @param y1 Last row
 * @return The difference
 */
static int32_t tbs_diff_area(
    const unsigned char *a,
    const unsigned char *b,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1)
{
    int32_t x, y, i, result = 0;
    for (y = y0; y <= y1; y ++) {
        for (x = x0; x <= x1; x ++) {
            for (i = 0; i < 3; i ++) {
                int32_t d = abs((int32_t)tbs_at(a, x, y)[i] - (int32_t)tbs_at(b, x, y)[i]);
                result = d > result ? d : result;
            }
        }
    }

    return result;
}

/** Get the largest difference of a channel between two captures.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The difference
 */
static int32_t tbs_diff(
    const unsigned char *a,
    const unsigned char *b)
{
    return tbs_diff_area(a, b, 0, 0, TBS_SIZE - 1, TBS_SIZE - 1);
}

/** Get how much the shadow darkens the white background at a pixel, in linear light, from its
 * green channel: the shadow blends in linear light, so this scales with its alpha.
 *
 * @param px The capture
 * @param x The column
 * @param y The row
 * @return 1 minus the linear green channel
 */
static float tbs_dark(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    float c = (float)tbs_at(px, x, y)[1] / 255.0f;
    float linear = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);

    return 1.0f - linear;
}

/** Check that a world has no device errors and free it.
 *
 * @param world The world
 */
static void tbs_fini(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    ecs_fini(world);
}

/** Render a scene of one shadowed rect without a transition, under a parent when it has one.
 *
 * @param file Image file in the temporary folder
 * @param parent Pose of the parent, NULL for none
 * @param pose Pose of the rect
 * @param opacity Opacity of the parent when it has one, else of the rect, negative for none
 * @param tint Tint of the parent, NULL for none
 * @param shadowed Whether the rect has a BoxShadow
 * @return The pixels, freed with stbi_image_free
 */
static unsigned char* tbs_reference(
    const char *file,
    const tbs_pose_t *parent,
    tbs_pose_t pose,
    float opacity,
    const Tint *tint,
    bool shadowed)
{
    ecs_world_t *world = tbs_world();
    ecs_entity_t up = parent ? tbs_parent(world, *parent) : 0;
    ecs_entity_t e = tbs_rect(world, up, pose, shadowed);
    if (opacity >= 0) {
        ecs_set(world, up ? up : e, Opacity, {opacity});
    }

    if (tint) {
        test_assert(up != 0);
        ecs_set_ptr(world, up, Tint, tint);
    }

    unsigned char *result = tbs_capture_n(world, file, 2);
    tbs_fini(world);

    return result;
}

/** A shadowed rect moves, grows and turns: halfway through, its shadow is where the shadow of a
 * rect set up at the halfway pose is, not at the target, and the frame after the end matches a
 * scene set up at the target.
 */
void TransitionBoxShadow_transform_mid_transition(void) {
    // ui/box_shadow
    tbs_pose_t from = {-12, 4, 1.0f, 0.0f}, to = {12, -4, 1.5f, 0.6f};
    tbs_pose_t mid = {0, 0, 1.25f, 0.3f};
    ecs_world_t *world = tbs_world();
    ecs_entity_t e = tbs_rect(world, 0, from, true);
    tbs_transition_pose(world, e);
    stbi_image_free(tbs_capture(world, "tbs_transform.png"));
    tbs_pose(world, e, to);
    unsigned char *moving = tbs_capture_n(world, "tbs_transform.png", TBS_MID);
    test_flt(ecs_get(world, e, Position)->x, 12);
    unsigned char *end = tbs_capture_n(world, "tbs_transform.png", TBS_END - TBS_MID);
    tbs_fini(world);

    unsigned char *at_mid = tbs_reference("tbs_transform_mid.png", NULL, mid, -1, NULL, true);
    unsigned char *at_end = tbs_reference("tbs_transform_end.png", NULL, to, -1, NULL, true);
    test_assert(tbs_diff(moving, at_end) > 60);
    test_assert(tbs_diff(moving, at_mid) <= TBS_TOLERANCE);
    test_int(tbs_diff(end, at_end), 0);
    stbi_image_free(moving);
    stbi_image_free(end);
    stbi_image_free(at_mid);
    stbi_image_free(at_end);
}

/** The parent of a shadowed rect moves, grows and turns: halfway through, the shadow of its
 * child is where it is under a parent set up at the halfway pose, and at the end where it is
 * under a parent at the target.
 */
void TransitionBoxShadow_ancestor_transform_mid_transition(void) {
    // ui/box_shadow
    tbs_pose_t from = {-10, 4, 1.0f, 0.0f}, to = {10, -4, 1.4f, -0.5f};
    tbs_pose_t mid = {0, 0, 1.2f, -0.25f}, child = {3, 2, 1.0f, 0.0f};
    ecs_world_t *world = tbs_world();
    ecs_entity_t parent = tbs_parent(world, from);
    tbs_rect(world, parent, child, true);
    tbs_transition_pose(world, parent);
    stbi_image_free(tbs_capture(world, "tbs_parent.png"));
    tbs_pose(world, parent, to);
    unsigned char *moving = tbs_capture_n(world, "tbs_parent.png", TBS_MID);
    unsigned char *end = tbs_capture_n(world, "tbs_parent.png", TBS_END - TBS_MID);
    tbs_fini(world);

    unsigned char *at_mid = tbs_reference("tbs_parent_mid.png", &mid, child, -1, NULL, true);
    unsigned char *at_end = tbs_reference("tbs_parent_end.png", &to, child, -1, NULL, true);
    test_assert(tbs_diff(moving, at_end) > 60);
    test_assert(tbs_diff(moving, at_mid) <= TBS_TOLERANCE);
    test_int(tbs_diff(end, at_end), 0);
    stbi_image_free(moving);
    stbi_image_free(end);
    stbi_image_free(at_mid);
    stbi_image_free(at_end);
}

/** Check a capture of a shadowed rect at the center that fades: under the rect the pixels are
 * those of the rect without a shadow, as the rect cuts its shadow out by its alpha, and outside
 * it the shadow darkens the background, in linear light, by opacity times its darkness at 1.
 *
 * @param px The capture
 * @param plain The rect without a shadow at the same opacity
 * @param shadowed The rect with its shadow at the same opacity, without a transition
 * @param full The rect with its shadow at opacity 1
 * @param opacity The opacity
 */
static void tbs_check_fade(
    const unsigned char *px,
    const unsigned char *plain,
    const unsigned char *shadowed,
    const unsigned char *full,
    float opacity)
{
    int32_t i;
    test_assert(tbs_diff_area(px, plain, 25, 25, 38, 38) <= TBS_TOLERANCE);
    test_assert(tbs_diff(px, shadowed) <= TBS_TOLERANCE);
    for (i = 0; i < 3; i ++) {
        int32_t x = 42 + i, y = 42 - i;
        float dark = tbs_dark(full, x, y);
        test_assert(dark > 0.4f);
        test_assert(fabsf(tbs_dark(px, x, y) - dark * opacity) <= TBS_DARK_TOLERANCE);
    }
}

/** Fade a shadowed rect, or its parent when it has one, from opacity 1 to 0.5 and check the
 * frame halfway through, at 0.75, and the frame at the end.
 *
 * @param under Whether the rect has a parent that fades
 */
static void tbs_opacity(
    bool under)
{
    tbs_pose_t center = {0, 0, 1.0f, 0.0f};
    ecs_world_t *world = tbs_world();
    ecs_entity_t parent = under ? tbs_parent(world, center) : 0;
    ecs_entity_t e = tbs_rect(world, parent, center, true);
    ecs_entity_t fading = under ? parent : e;
    ecs_set(world, fading, Opacity, {1.0f});
    tbs_transition(world, fading, ecs_id(Opacity));
    unsigned char *full = tbs_capture(world, "tbs_opacity.png");
    ecs_set(world, fading, Opacity, {0.5f});
    unsigned char *mid = tbs_capture_n(world, "tbs_opacity.png", TBS_MID);
    test_flt(ecs_get(world, fading, Opacity)->value, 0.5f);
    unsigned char *end = tbs_capture_n(world, "tbs_opacity.png", TBS_END - TBS_MID);
    tbs_fini(world);

    const tbs_pose_t *up = under ? &center : NULL;
    unsigned char *plain_mid = tbs_reference("tbs_opacity_ref.png", up, center, 0.75f, NULL,
        false);
    unsigned char *shadowed_mid = tbs_reference("tbs_opacity_ref.png", up, center, 0.75f, NULL,
        true);
    unsigned char *plain_end = tbs_reference("tbs_opacity_ref.png", up, center, 0.5f, NULL,
        false);
    unsigned char *shadowed_end = tbs_reference("tbs_opacity_ref.png", up, center, 0.5f, NULL,
        true);
    test_assert(tbs_diff(mid, shadowed_end) > 20);
    tbs_check_fade(mid, plain_mid, shadowed_mid, full, 0.75f);
    tbs_check_fade(end, plain_end, shadowed_end, full, 0.5f);
    test_int(tbs_diff(end, shadowed_end), 0);
    stbi_image_free(full);
    stbi_image_free(mid);
    stbi_image_free(end);
    stbi_image_free(plain_mid);
    stbi_image_free(shadowed_mid);
    stbi_image_free(plain_end);
    stbi_image_free(shadowed_end);
}

/** A shadowed rect fades from opacity 1 to 0.5: halfway through its shadow is cut out under it
 * by the alpha it renders at and fades with it, with no seam along its edge.
 */
void TransitionBoxShadow_opacity_mid_transition(void) {
    // ui/box_shadow
    tbs_opacity(false);
}

/** The parent of a shadowed rect fades from opacity 1 to 0.5: the shadow of the child fades
 * with the opacity the parent renders at.
 */
void TransitionBoxShadow_ancestor_opacity_mid_transition(void) {
    // ui/box_shadow
    tbs_opacity(true);
}

/** A shadowed rect turns from red to blue: halfway through, its shadow is the shadow it has at
 * either end, as the color of a shape does not change its shadow.
 */
void TransitionBoxShadow_rgb_keeps_shadow(void) {
    // ui/box_shadow
    tbs_pose_t center = {0, 0, 1.0f, 0.0f};
    ecs_world_t *world = tbs_world();
    ecs_entity_t e = tbs_rect(world, 0, center, true);
    tbs_transition(world, e, ecs_id(Rgb));
    unsigned char *before = tbs_capture(world, "tbs_rgb.png");
    ecs_set(world, e, Rgb, {0, 0, 255});
    unsigned char *mid = tbs_capture_n(world, "tbs_rgb.png", TBS_MID);
    tbs_fini(world);

    const unsigned char *c = tbs_at(mid, 32, 32);
    test_assert(c[0] > 60 && c[2] > 60);
    test_int(tbs_diff_area(mid, before, 41, 41, 50, 50), 0);
    test_int(tbs_diff_area(mid, before, 0, 0, 63, 22), 0);
    stbi_image_free(before);
    stbi_image_free(mid);
}

/** The parent of a shadowed rect tints toward blue, from a strength of 0 to 255: halfway
 * through, the black shadow is tinted as under a parent set up at a strength of 128.
 */
void TransitionBoxShadow_ancestor_tint_mid_transition(void) {
    // ui/box_shadow
    tbs_pose_t center = {0, 0, 1.0f, 0.0f};
    Tint half = {0, 0, 255, 128}, full = {0, 0, 255, 255};
    ecs_world_t *world = tbs_world();
    ecs_entity_t parent = tbs_parent(world, center);
    tbs_rect(world, parent, center, true);
    ecs_set(world, parent, Tint, {0, 0, 255, 0});
    tbs_transition(world, parent, ecs_id(Tint));
    stbi_image_free(tbs_capture(world, "tbs_tint.png"));
    ecs_set_ptr(world, parent, Tint, &full);
    unsigned char *mid = tbs_capture_n(world, "tbs_tint.png", TBS_MID);
    unsigned char *end = tbs_capture_n(world, "tbs_tint.png", TBS_END - TBS_MID);
    tbs_fini(world);

    unsigned char *at_mid = tbs_reference("tbs_tint_ref.png", &center, center, -1, &half, true);
    unsigned char *at_end = tbs_reference("tbs_tint_ref.png", &center, center, -1, &full, true);
    test_assert(tbs_diff_area(mid, at_end, 41, 41, 50, 50) > 20);
    test_assert(tbs_diff(mid, at_mid) <= TBS_TOLERANCE);
    test_int(tbs_diff(end, at_end), 0);
    stbi_image_free(mid);
    stbi_image_free(end);
    stbi_image_free(at_mid);
    stbi_image_free(at_end);
}

/** Two shadowed rects move, the shadow of the first in the slot before that of the second.
 *
 * The first loses its BoxShadow halfway: its slot is freed and the shadow of the second moves
 * into it. The transition still moves the first rect but no longer patches that slot, so the
 * second shadow stays with its own rect.
 */
void TransitionBoxShadow_shadow_removed_mid_transition(void) {
    // ui/box_shadow
    tbs_pose_t a_from = {-12, 14, 1.0f, 0.0f}, a_to = {12, 14, 1.0f, 0.0f};
    tbs_pose_t b_from = {12, -14, 1.0f, 0.0f}, b_to = {-12, -14, 1.0f, 0.0f};
    tbs_pose_t a_mid = {0, 14, 1.0f, 0.0f}, b_mid = {0, -14, 1.0f, 0.0f};
    ecs_world_t *world = tbs_world();
    ecs_entity_t a = tbs_rect(world, 0, a_from, true);
    ecs_entity_t b = tbs_rect(world, 0, b_from, true);
    tbs_transition(world, a, ecs_id(Position));
    tbs_transition(world, b, ecs_id(Position));
    stbi_image_free(tbs_capture(world, "tbs_removed.png"));
    tbs_pose(world, a, a_to);
    tbs_pose(world, b, b_to);
    stbi_image_free(tbs_capture_n(world, "tbs_removed.png", TBS_MID - 1));
    ecs_remove(world, a, BoxShadow);
    unsigned char *mid = tbs_capture(world, "tbs_removed.png");
    unsigned char *end = tbs_capture_n(world, "tbs_removed.png", TBS_END - TBS_MID);
    tbs_fini(world);

    ecs_world_t *ref = tbs_world();
    ecs_entity_t ra = tbs_rect(ref, 0, a_mid, false);
    ecs_entity_t rb = tbs_rect(ref, 0, b_mid, true);
    unsigned char *at_mid = tbs_capture_n(ref, "tbs_removed_ref.png", 2);
    tbs_pose(ref, ra, a_to);
    tbs_pose(ref, rb, b_to);
    unsigned char *at_end = tbs_capture_n(ref, "tbs_removed_ref.png", 2);
    tbs_fini(ref);

    test_assert(tbs_diff(mid, at_mid) <= TBS_TOLERANCE);
    test_int(tbs_diff(end, at_end), 0);
    stbi_image_free(mid);
    stbi_image_free(end);
    stbi_image_free(at_mid);
    stbi_image_free(at_end);
}
