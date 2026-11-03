/**
 * @file TransitionBoxShadow.c
 * @brief Tests of the TransitionBoxShadow suite: transitions of BoxShadow itself, its offset,
 * blur, spread and color, with the null backend: the values of the resolver, the dynamic batch of
 * the shadow slot while one runs, and the transform and shading the resolver patches, compared
 * byte for byte with a world set up at the same value without a transition.
 *
 * The cases step the world with a fixed delta and a Linear transition of one second, so the
 * four frames after a change render at progress 0, 1/4, 1/2 and 3/4 and the fifth at the target.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "draw.h"
#include "transition_test.h"

/** Difference a patched float of a transform may have from the one of a static world */
#define TR_SHADOW_EPS (1e-5f)

/** Difference a patched color channel may have from the one of a static world: two units of
 * unorm16 */
#define TR_SHADOW_COLOR_EPS (2.0f / 65535.0f + 1e-7f)

/** Frames after a change that render at progress 1/2 */
#define TR_SHADOW_MID (3)

/** Frames after a change that render at the target */
#define TR_SHADOW_END (5)

/** The shadow a case starts at: close under the rect, sharp */
static const BoxShadow tr_shadow_from = {
    .offset_x = 1, .offset_y = -1, .blur = 2, .spread = 0, .color = {0, 0, 0, 200}};

/** The shadow a case changes to: lifted, soft and grown */
static const BoxShadow tr_shadow_to = {
    .offset_x = 5, .offset_y = -9, .blur = 10, .spread = 2, .color = {0, 0, 255, 100}};

/** Halfway between them, the channels rounded as the resolver rounds them */
static const BoxShadow tr_shadow_mid = {
    .offset_x = 3, .offset_y = -5, .blur = 6, .spread = 1, .color = {0, 0, 128, 150}};

/** Create a 2D rectangle with a BoxShadow.
 *
 * @param world The world
 * @param shadow The shadow
 * @return The rectangle
 */
static ecs_entity_t tr_shadow_rect(
    ecs_world_t *world,
    const BoxShadow *shadow)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rect, {10, 20});
    ecs_set(world, e, Rgb, {200, 100, 50});
    ecs_set_ptr(world, e, BoxShadow, shadow);

    return e;
}

/** Give an entity a one second Linear transition of a component.
 *
 * @param world The world
 * @param e The entity
 * @param component The component
 */
static void tr_shadow_transition(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component)
{
    box_transition_set(world, e, component, &(TransitionSpec){ .duration = 1.0f });
}

/** Get the batch of the shadow of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The batch, NULL when the shadow has no slot
 */
static box_draw_batch_t* tr_shadow_batch(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_render_draws_t *d = box_draws_state(world);
    const BoxShadowSlot *bs = ecs_get(world, e, BoxShadowSlot);
    test_assert(d != NULL);

    return bs && bs->batch >= 0 ? box_draws_batch(d, bs->batch) : NULL;
}

/** Read the transform and the shading of the shadow of an entity as the backend got them in the
 * last packet: the patched bytes of the run uploads of a dynamic batch, else the static slot.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param transform Receives the transform
 * @param shading Receives the shading
 */
static void tr_shadow_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_transform_t *transform,
    box_render_shading_t *shading)
{
    box_draw_batch_t *b = tr_shadow_batch(world, e);
    int32_t slot;
    test_assert(b != NULL);
    slot = ecs_get(world, e, BoxShadowSlot)->slot;
    box_render_sync(world);
    *transform = *box_draws_slot_transform(b, slot);
    *shading = *box_draws_slot_shading(b, slot);
    tr_slot_rendered(world, null, b, slot, BOX_RENDER_STREAM_TRANSFORM,
        (uint32_t)sizeof(box_render_transform_t), transform);
    tr_slot_rendered(world, null, b, slot, BOX_RENDER_STREAM_SHADING,
        (uint32_t)sizeof(box_render_shading_t), shading);
}

/** Check that two transforms are the same within TR_SHADOW_EPS.
 *
 * @param a The first transform
 * @param b The second transform
 */
static void tr_shadow_transform_near(
    const box_render_transform_t *a,
    const box_render_transform_t *b)
{
    int32_t r, c;
    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 4; c ++) {
            test_assert(fabsf(a->rows[r][c] - b->rows[r][c]) <= TR_SHADOW_EPS);
        }
    }
}

/** Check that two shadings are the same: the words of the shadow box within TR_SHADOW_EPS, the
 * color within TR_SHADOW_COLOR_EPS, as a tint of no strength may round apart in the last bit,
 * and the ids exactly.
 *
 * @param a The first shading
 * @param b The second shading
 */
static void tr_shadow_shading_near(
    const box_render_shading_t *a,
    const box_render_shading_t *b)
{
    float ca[4], cb[4];
    int32_t i;
    box_draws_color_unpack(ca, a->color);
    box_draws_color_unpack(cb, b->color);
    for (i = 0; i < 4; i ++) {
        test_assert(fabsf(a->emissive[i] - b->emissive[i]) <= TR_SHADOW_EPS);
        test_assert(fabsf(a->material[i] - b->material[i]) <= TR_SHADOW_EPS);
        test_assert(fabsf(a->uv[i] - b->uv[i]) <= TR_SHADOW_EPS);
        test_assert(fabsf(ca[i] - cb[i]) <= TR_SHADOW_COLOR_EPS);
    }

    test_uint(a->ids[0], b->ids[0]);
    test_uint(a->ids[1], b->ids[1]);
}

/** Render a world with a shadowed rect set up without a transition and read its shadow.
 *
 * @param shadow The shadow
 * @param position Position of the rect
 * @param opacity Opacity of the rect, negative for none
 * @param transform Receives the transform of the shadow
 * @param shading Receives the shading of the shadow
 */
static void tr_shadow_reference(
    const BoxShadow *shadow,
    Position position,
    float opacity,
    box_render_transform_t *transform,
    box_render_shading_t *shading)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shadow_rect(world, shadow);
    ecs_set_ptr(world, e, Position, &position);
    if (opacity >= 0) {
        ecs_set(world, e, Opacity, {opacity});
    }

    tr_frames(world, 2);
    tr_shadow_rendered(world, null, e, transform, shading);
    tr_fini(world, null);
}

void TransitionBoxShadow_values(void) {
    // ui/box_shadow
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shadow_rect(world, &tr_shadow_from);
    BoxShadow v;
    tr_shadow_transition(world, e, ecs_id(BoxShadow));
    tr_frame(world);
    test_assert(box_render_transition_test_value(world, e, ecs_id(BoxShadow), &v));
    test_assert(!ecs_os_memcmp_t(&v, &tr_shadow_from, BoxShadow));

    ecs_set_ptr(world, e, BoxShadow, &tr_shadow_to);
    tr_frames(world, TR_SHADOW_MID);
    test_assert(box_render_transition_test_value(world, e, ecs_id(BoxShadow), &v));
    test_flt(v.offset_x, tr_shadow_mid.offset_x);
    test_flt(v.offset_y, tr_shadow_mid.offset_y);
    test_flt(v.blur, tr_shadow_mid.blur);
    test_flt(v.spread, tr_shadow_mid.spread);
    test_uint(v.color.r, tr_shadow_mid.color.r);
    test_uint(v.color.g, tr_shadow_mid.color.g);
    test_uint(v.color.b, tr_shadow_mid.color.b);
    test_uint(v.color.a, tr_shadow_mid.color.a);
    test_flt(ecs_get(world, e, BoxShadow)->offset_y, tr_shadow_to.offset_y);

    tr_frames(world, TR_SHADOW_END - TR_SHADOW_MID);
    test_assert(box_render_transition_test_value(world, e, ecs_id(BoxShadow), &v));
    test_assert(!ecs_os_memcmp_t(&v, &tr_shadow_to, BoxShadow));
    test_int(box_render_transition_test_active(world), 0);
    tr_fini(world, null);
}

void TransitionBoxShadow_dynamic_batch(void) {
    // ui/box_shadow
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shadow_rect(world, &tr_shadow_from);
    tr_shadow_transition(world, e, ecs_id(BoxShadow));
    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionBoxShadow));
    test_assert(!tr_shadow_batch(world, e)->key.dynamic_transform);
    test_assert(!tr_shadow_batch(world, e)->key.dynamic_material);

    ecs_set_ptr(world, e, BoxShadow, &tr_shadow_to);
    tr_frame(world);
    box_draw_batch_t *b = tr_shadow_batch(world, e);
    test_assert(ecs_has_id(world, e, TransitionBoxShadow));
    test_assert(b != NULL);
    test_assert(b->key.box_shadow);
    test_assert(b->key.dynamic_transform);
    test_assert(b->key.dynamic_material);
    test_assert(!tr_batch(world, e)->key.dynamic_transform);
    test_assert(!tr_batch(world, e)->key.dynamic_material);
    test_assert(!ecs_has_id(world, e, TransitionTransform));
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADOW_BOX), 1);

    tr_frames(world, TR_SHADOW_END - 1);
    b = tr_shadow_batch(world, e);
    test_assert(!ecs_has_id(world, e, TransitionBoxShadow));
    test_assert(b != NULL);
    test_assert(!b->key.dynamic_transform);
    test_assert(!b->key.dynamic_material);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADOW_BOX), 0);
    tr_fini(world, null);
}

void TransitionBoxShadow_mid_matches_static(void) {
    // ui/box_shadow
    box_render_null_t *null;
    box_render_transform_t transform, expected_transform;
    box_render_shading_t shading, expected_shading;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shadow_rect(world, &tr_shadow_from);
    tr_shadow_transition(world, e, ecs_id(BoxShadow));
    tr_frame(world);
    ecs_set_ptr(world, e, BoxShadow, &tr_shadow_to);
    tr_frames(world, TR_SHADOW_MID);
    tr_shadow_rendered(world, null, e, &transform, &shading);
    tr_fini(world, null);

    tr_shadow_reference(&tr_shadow_mid, (Position){0, 0, 0}, -1, &expected_transform,
        &expected_shading);
    tr_shadow_transform_near(&transform, &expected_transform);
    tr_shadow_shading_near(&shading, &expected_shading);
}

void TransitionBoxShadow_end_matches_static(void) {
    // ui/box_shadow
    box_render_null_t *null;
    box_render_transform_t transform, expected_transform;
    box_render_shading_t shading, expected_shading;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shadow_rect(world, &tr_shadow_from);
    tr_shadow_transition(world, e, ecs_id(BoxShadow));
    tr_frame(world);
    ecs_set_ptr(world, e, BoxShadow, &tr_shadow_to);
    tr_frames(world, TR_SHADOW_END);
    tr_shadow_rendered(world, null, e, &transform, &shading);
    tr_fini(world, null);

    tr_shadow_reference(&tr_shadow_to, (Position){0, 0, 0}, -1, &expected_transform,
        &expected_shading);
    test_assert(!ecs_os_memcmp_t(&transform, &expected_transform, box_render_transform_t));
    test_assert(!ecs_os_memcmp_t(&shading, &expected_shading, box_render_shading_t));
}

void TransitionBoxShadow_with_opacity(void) {
    // ui/box_shadow
    box_render_null_t *null;
    box_render_transform_t transform, expected_transform;
    box_render_shading_t shading, expected_shading;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shadow_rect(world, &tr_shadow_from);
    ecs_set(world, e, Opacity, {1.0f});
    tr_shadow_transition(world, e, ecs_id(BoxShadow));
    tr_shadow_transition(world, e, ecs_id(Opacity));
    tr_frame(world);
    ecs_set_ptr(world, e, BoxShadow, &tr_shadow_to);
    ecs_set(world, e, Opacity, {0.5f});
    tr_frames(world, TR_SHADOW_MID);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_SHADOW_BOX), 1);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_FADE), 1);
    tr_shadow_rendered(world, null, e, &transform, &shading);
    tr_fini(world, null);

    tr_shadow_reference(&tr_shadow_mid, (Position){0, 0, 0}, 0.75f, &expected_transform,
        &expected_shading);
    tr_shadow_transform_near(&transform, &expected_transform);
    tr_shadow_shading_near(&shading, &expected_shading);
}

void TransitionBoxShadow_with_position(void) {
    // ui/box_shadow
    box_render_null_t *null;
    box_render_transform_t transform, expected_transform;
    box_render_shading_t shading, expected_shading;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shadow_rect(world, &tr_shadow_from);
    ecs_set(world, e, Position, {-4, 2, 0});
    tr_shadow_transition(world, e, ecs_id(BoxShadow));
    tr_shadow_transition(world, e, ecs_id(Position));
    tr_frame(world);
    ecs_set_ptr(world, e, BoxShadow, &tr_shadow_to);
    ecs_set(world, e, Position, {4, -2, 0});
    tr_frames(world, TR_SHADOW_MID);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_SHADOW_BOX), 1);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_POSE), 1);
    tr_shadow_rendered(world, null, e, &transform, &shading);
    tr_fini(world, null);

    tr_shadow_reference(&tr_shadow_mid, (Position){0, 0, 0}, -1, &expected_transform,
        &expected_shading);
    tr_shadow_transform_near(&transform, &expected_transform);
    tr_shadow_shading_near(&shading, &expected_shading);
}

void TransitionBoxShadow_removed_mid_transition(void) {
    // ui/box_shadow
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t a = tr_shadow_rect(world, &tr_shadow_from);
    ecs_entity_t b = tr_shadow_rect(world, &tr_shadow_from);
    ecs_set(world, b, Position, {12, 0, 0});
    tr_shadow_transition(world, a, ecs_id(BoxShadow));
    tr_shadow_transition(world, b, ecs_id(BoxShadow));
    tr_frame(world);
    ecs_set_ptr(world, a, BoxShadow, &tr_shadow_to);
    ecs_set_ptr(world, b, BoxShadow, &tr_shadow_to);
    tr_frames(world, TR_SHADOW_MID - 1);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADOW_BOX), 2);

    ecs_remove(world, a, BoxShadow);
    tr_frame(world);
    test_int(ecs_get(world, a, BoxShadowSlot)->batch, -1);
    test_assert(!ecs_has_id(world, a, TransitionBoxShadow));
    test_assert(ecs_has_id(world, b, TransitionBoxShadow));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADOW_BOX), 1);

    box_render_transform_t transform, expected_transform;
    box_render_shading_t shading, expected_shading;
    tr_shadow_rendered(world, null, b, &transform, &shading);
    tr_frames(world, TR_SHADOW_END - TR_SHADOW_MID);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADOW_BOX), 0);
    test_assert(!ecs_has_id(world, b, TransitionBoxShadow));
    tr_fini(world, null);

    tr_shadow_reference(&tr_shadow_mid, (Position){12, 0, 0}, -1, &expected_transform,
        &expected_shading);
    tr_shadow_transform_near(&transform, &expected_transform);
    tr_shadow_shading_near(&shading, &expected_shading);
}

void TransitionBoxShadow_no_area(void) {
    // ui/box_shadow
    BoxShadow none = tr_shadow_from, some = tr_shadow_from, half = tr_shadow_from;
    box_render_null_t *null;
    box_render_transform_t transform, expected_transform;
    box_render_shading_t shading, expected_shading;
    none.spread = -6;
    some.spread = 2;
    half.spread = -2;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shadow_rect(world, &none);
    tr_shadow_transition(world, e, ecs_id(BoxShadow));
    tr_frame(world);
    test_int(ecs_get(world, e, BoxShadowSlot)->batch, -1);

    ecs_set_ptr(world, e, BoxShadow, &some);
    tr_frame(world);
    test_assert(ecs_get(world, e, BoxShadowSlot)->batch >= 0);
    tr_shadow_rendered(world, null, e, &transform, &shading);
    test_uint(shading.color[0], 0);
    test_uint(shading.color[1], 0);
    test_flt(transform.rows[0][0], 0);
    test_flt(transform.rows[1][1], 0);

    tr_frames(world, TR_SHADOW_MID - 1);
    tr_shadow_rendered(world, null, e, &transform, &shading);
    tr_shadow_reference(&half, (Position){0, 0, 0}, -1, &expected_transform,
        &expected_shading);
    tr_shadow_transform_near(&transform, &expected_transform);
    tr_shadow_shading_near(&shading, &expected_shading);

    tr_frames(world, TR_SHADOW_END - TR_SHADOW_MID);
    test_assert(!ecs_has_id(world, e, TransitionBoxShadow));
    test_assert(ecs_get(world, e, BoxShadowSlot)->batch >= 0);

    ecs_set_ptr(world, e, BoxShadow, &none);
    tr_frames(world, TR_SHADOW_MID);
    test_assert(ecs_has_id(world, e, TransitionBoxShadow));
    test_assert(ecs_get(world, e, BoxShadowSlot)->batch >= 0);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_SHADOW_BOX), 1);
    tr_shadow_rendered(world, null, e, &transform, &shading);
    tr_shadow_transform_near(&transform, &expected_transform);
    tr_shadow_shading_near(&shading, &expected_shading);

    tr_frames(world, TR_SHADOW_END - TR_SHADOW_MID);
    test_assert(!ecs_has_id(world, e, TransitionBoxShadow));
    test_int(ecs_get(world, e, BoxShadowSlot)->batch, -1);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADOW_BOX), 0);
    tr_fini(world, null);
}

void TransitionBoxShadow_no_transition_snaps(void) {
    // ui/box_shadow
    box_render_null_t *null;
    box_render_transform_t transform, expected_transform;
    box_render_shading_t shading, expected_shading;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shadow_rect(world, &tr_shadow_from);
    tr_shadow_transition(world, e, ecs_id(Position));
    tr_frame(world);
    ecs_set_ptr(world, e, BoxShadow, &tr_shadow_to);
    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionBoxShadow));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADOW_BOX), 0);
    test_assert(!tr_shadow_batch(world, e)->key.dynamic_material);
    tr_shadow_rendered(world, null, e, &transform, &shading);
    tr_fini(world, null);

    tr_shadow_reference(&tr_shadow_to, (Position){0, 0, 0}, -1, &expected_transform,
        &expected_shading);
    test_assert(!ecs_os_memcmp_t(&transform, &expected_transform, box_render_transform_t));
    test_assert(!ecs_os_memcmp_t(&shading, &expected_shading, box_render_shading_t));
}

void TransitionBoxShadow_layout(void) {
    // ui/box_shadow
    const box_render_transition_layout_t *layout =
        box_render_transition_layout(BOX_RENDER_TRANSITION_BOX_SHADOW);
    box_render_transition_component_t c;
    float lo[4] = {0, 0, 0, 0}, hi[4] = {2, 4, 8, 16};
    test_assert(box_render_transition_component(ecs_id(BoxShadow), &c));
    test_int(c, BOX_RENDER_TRANSITION_BOX_SHADOW);
    test_int(layout->size, ECS_SIZEOF(BoxShadow));
    test_int(layout->count, 8);
    test_assert(layout->count <= BOX_RENDER_TRANSITION_FIELDS);

    box_render_transition_widen(BOX_RENDER_TRANSITION_BOX_SHADOW, &(TransitionSpec){
        .duration = 1, .timing_function = CubicBezier, .bezier = {0.5f, 1.5f, 0.5f, 1.0f}},
        lo, hi);
    test_flt(lo[0], -1);
    test_flt(hi[0], 3);
    test_flt(lo[3], -8);
    test_flt(hi[3], 24);
}
