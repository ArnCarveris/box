/**
 * @file TransitionBorder.c
 * @brief Tests of the TransitionBorder suite: transitions of Border, its width, radius and color,
 * with the null backend: the values of the resolver, the material words of the 2D shading it
 * patches and the shadow of the shape that follows it, compared with a world set up at the same
 * value without a transition.
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
#include "../../../geometry_assets.h"

/** Difference a patched float may have from the one of a static world */
#define TR_BORDER_EPS (1e-5f)

/** Difference a tinted border or shape color may have from the one of a static world, whose
 * tints are composed in another order */
#define TR_BORDER_COLOR_EPS (2e-3f)

/** Difference a patched shadow color channel may have from the one of a static world: two
 * units of unorm16 */
#define TR_BORDER_SHADOW_COLOR_EPS (2.0f / 65535.0f + 1e-7f)

/** Frames after a change that render at progress 1/2 */
#define TR_BORDER_MID (3)

/** Frames after a change that render at the target */
#define TR_BORDER_END (5)

/** Width and height of the rectangle of a case: half its smaller side is 10 */
#define TR_BORDER_W (20.0f)
#define TR_BORDER_H (40.0f)

/** Decode a half float.
 *
 * @param bits The 16 bits
 * @return The value
 */
static float tr_half(
    uint32_t bits)
{
    uint32_t sign = (bits >> 15) & 1u, exp = (bits >> 10) & 0x1Fu, mant = bits & 0x3FFu;
    float result;
    if (!exp) {
        result = ldexpf((float)mant, -24);
    } else if (exp == 31) {
        result = mant ? NAN : INFINITY;
    } else {
        result = ldexpf((float)(mant | 0x400u), (int)exp - 25);
    }

    return sign ? -result : result;
}

/** Get a half of a material word of a 2D shading.
 *
 * @param shading The shading
 * @param word The word, 2 or 3
 * @param high Whether the high half
 * @return The value
 */
static float tr_border_half(
    const box_render_shading_t *shading,
    int32_t word,
    bool high)
{
    uint32_t bits;
    ecs_os_memcpy(&bits, &shading->material[word], 4);

    return tr_half(high ? bits >> 16 : bits & 0xFFFFu);
}

/** Create a 2D rectangle with a Border.
 *
 * @param world The world
 * @param parent ChildOf parent, 0 for none
 * @param border The border
 * @return The rectangle
 */
static ecs_entity_t tr_border_rect(
    ecs_world_t *world,
    ecs_entity_t parent,
    const Border *border)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rect, {TR_BORDER_W, TR_BORDER_H});
    ecs_set(world, e, Rgb, {200, 100, 50});
    ecs_set_ptr(world, e, Border, border);

    return e;
}

/** Give an entity a one second transition of a component.
 *
 * @param world The world
 * @param e The entity
 * @param component The component
 */
static void tr_border_transition(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component)
{
    box_transition_set(world, e, component, &(TransitionSpec){ .duration = 1.0f });
}

/** Read the shading of the 2D shape of an entity as the backend got it in the last packet: the
 * patched bytes of the run upload of a dynamic_material batch, else the static slot.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param out Receives the shading
 */
static void tr_border_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_shading_t *out)
{
    box_draw_batch_t *b = tr_batch(world, e);
    int32_t slot = ecs_get(world, e, DrawSlot)->slot;
    box_render_sync(world);
    *out = *box_draws_slot_shading(b, slot);
    tr_slot_rendered(world, null, b, slot, BOX_RENDER_STREAM_SHADING,
        (uint32_t)sizeof(box_render_shading_t), out);
}

/** Encode the shading of the 2D shape of an entity with another Border, as the resolver does:
 * its shape with the Border applied by box_shape2d_border.
 *
 * @param world The world
 * @param e The entity
 * @param border The border
 * @param out Receives the shading
 */
static void tr_border_expected(
    ecs_world_t *world,
    ecs_entity_t e,
    const Border *border,
    box_render_shading_t *out)
{
    box_draw_material_t m;
    Shape2D shape;
    box_draws_shape_material(world, e, ecs_get(world, e, DrawSlot), &m);
    shape = *m.shape2d;
    if (m.border_radius) {
        shape.radius = 0.0f;
    }

    box_shape2d_border(&shape, border);
    m.shape2d = &shape;
    box_draws_material(out, &m);
}

/** Render a world with a bordered rectangle set up without a transition and read its shading.
 *
 * @param border The border
 * @param out Receives the shading
 */
static void tr_border_reference(
    const Border *border,
    box_render_shading_t *out)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_border_rect(world, 0, border);
    tr_frames(world, 2);
    tr_border_rendered(world, null, e, out);
    tr_fini(world, null);
}

/** Change the Border of an entity and check every frame of the transition: the shading of its
 * shape is that of its shape at the Border the resolver renders, in a dynamic material batch,
 * and at the end the target in its static batch.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity, with a running node of its Border
 * @param to The target
 * @param mid Receives the shading half way
 */
static void tr_border_run(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    const Border *to,
    box_render_shading_t *mid)
{
    box_render_shading_t rendered, expected;
    Border v;
    int32_t i;
    test_assert(!tr_batch(world, e)->key.dynamic_material);
    ecs_set_ptr(world, e, Border, to);
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_assert(ecs_has_id(world, e, TransitionMaterial));
        test_assert(tr_batch(world, e)->key.dynamic_material);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 1);
        test_assert(box_render_transition_test_value(world, e, ecs_id(Border), &v));
        tr_border_expected(world, e, &v, &expected);
        tr_border_rendered(world, null, e, &rendered);
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
        if (i == TR_BORDER_MID - 1) {
            *mid = rendered;
        }
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    test_assert(!tr_batch(world, e)->key.dynamic_material);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADOW_BOX), 0);
}

void TransitionBorder_values(void) {
    // ui/border
    const Border from = { .width = 1, .radius = 0, .color = {0, 0, 0, 255} };
    const Border to = { .width = 3, .radius = 8, .color = {200, 100, 40, 55} };
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_border_rect(world, 0, &from);
    Border v;
    tr_border_transition(world, e, ecs_id(Border));
    tr_frame(world);
    test_assert(box_render_transition_test_value(world, e, ecs_id(Border), &v));
    test_assert(!ecs_os_memcmp_t(&v, &from, Border));

    ecs_set_ptr(world, e, Border, &to);
    tr_frames(world, TR_BORDER_MID);
    test_assert(box_render_transition_test_value(world, e, ecs_id(Border), &v));
    test_flt(v.width, 2);
    test_flt(v.radius, 4);
    test_uint(v.color.r, 100);
    test_uint(v.color.g, 50);
    test_uint(v.color.b, 20);
    test_uint(v.color.a, 155);
    test_flt(ecs_get(world, e, Border)->width, 3);

    tr_frames(world, TR_BORDER_END - TR_BORDER_MID);
    test_assert(box_render_transition_test_value(world, e, ecs_id(Border), &v));
    test_assert(!ecs_os_memcmp_t(&v, &to, Border));
    test_int(box_render_transition_test_active(world), 0);
    tr_fini(world, null);
}

void TransitionBorder_layout(void) {
    // ui/border
    const box_render_transition_layout_t *layout =
        box_render_transition_layout(BOX_RENDER_TRANSITION_BORDER);
    box_render_transition_component_t c;
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    test_assert(box_render_transition_component(ecs_id(Border), &c));
    test_int(c, BOX_RENDER_TRANSITION_BORDER);
    tr_fini(world, null);
    test_int(layout->size, ECS_SIZEOF(Border));
    test_int(layout->count, 6);
    test_assert(layout->count <= BOX_RENDER_TRANSITION_FIELDS);
    test_assert(layout->size <= BOX_RENDER_TRANSITION_VALUE);
    test_int(layout->fields[0].kind, BOX_RENDER_TRANSITION_F32);
    test_int(layout->fields[1].kind, BOX_RENDER_TRANSITION_F32);
    test_int(layout->fields[2].kind, BOX_RENDER_TRANSITION_U8);
    test_int(layout->fields[5].kind, BOX_RENDER_TRANSITION_U8);
}

void TransitionBorder_color(void) {
    // ui/border
    const Border from = { .width = 2, .radius = 4, .color = {0, 0, 0, 255} };
    const Border to = { .width = 2, .radius = 4, .color = {200, 100, 40, 255} };
    const Border half = { .width = 2, .radius = 4, .color = {100, 50, 20, 255} };
    box_render_null_t *null;
    box_render_shading_t mid, end, expected;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_border_rect(world, 0, &from);
    tr_border_transition(world, e, ecs_id(Border));
    tr_frames(world, 2);
    tr_border_run(world, null, e, &to, &mid);
    tr_border_rendered(world, null, e, &end);
    tr_fini(world, null);

    tr_border_reference(&half, &expected);
    test_assert(!ecs_os_memcmp(&mid.material[2], &expected.material[2], 2 * ECS_SIZEOF(float)));
    test_assert(!ecs_os_memcmp(&mid.material[0], &expected.material[0], 2 * ECS_SIZEOF(float)));
    tr_border_reference(&to, &expected);
    test_assert(!ecs_os_memcmp_t(&end, &expected, box_render_shading_t));
}

void TransitionBorder_width_radius(void) {
    // ui/border
    const Border from = { .width = 1, .radius = 0, .color = {40, 110, 240, 255} };
    const Border to = { .width = 3, .radius = 8, .color = {40, 110, 240, 255} };
    const Border half = { .width = 2, .radius = 4, .color = {40, 110, 240, 255} };
    box_render_null_t *null;
    box_render_shading_t mid, end, expected;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_border_rect(world, 0, &from);
    tr_border_transition(world, e, ecs_id(Border));
    tr_frames(world, 2);
    tr_border_run(world, null, e, &to, &mid);
    tr_border_rendered(world, null, e, &end);
    tr_fini(world, null);

    test_flt(mid.material[0], 0.4f);
    test_flt(mid.material[1], 0.2f);
    tr_border_reference(&half, &expected);
    test_assert(!ecs_os_memcmp(mid.material, expected.material, 4 * ECS_SIZEOF(float)));
    tr_border_reference(&to, &expected);
    test_flt(end.material[0], 0.8f);
    test_flt(end.material[1], 0.3f);
    test_assert(!ecs_os_memcmp_t(&end, &expected, box_render_shading_t));
}

void TransitionBorder_appear(void) {
    // ui/border
    const Border from = { .width = 0, .radius = 0, .color = {40, 110, 240, 255} };
    const Border to = { .width = 2, .radius = 0, .color = {40, 110, 240, 255} };
    box_render_null_t *null;
    box_render_shading_t mid, end, expected;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_border_rect(world, 0, &from);
    tr_border_transition(world, e, ecs_id(Border));
    tr_frames(world, 2);
    tr_border_rendered(world, null, e, &end);
    test_flt(end.material[1], 0);
    tr_border_run(world, null, e, &to, &mid);
    tr_border_rendered(world, null, e, &end);
    tr_fini(world, null);

    test_flt(mid.material[1], 0.1f);
    tr_border_reference(&to, &expected);
    test_assert(!ecs_os_memcmp_t(&end, &expected, box_render_shading_t));
}

/** A curve whose output overshoots past 1 takes the width of a shrinking border below 0, which
 * draws as no border, never a negative one. */
void TransitionBorder_overshoot(void) {
    // ui/border
    const Border from = { .width = 2, .radius = 0, .color = {40, 110, 240, 255} };
    const Border to = { .width = 0, .radius = 0, .color = {40, 110, 240, 255} };
    box_render_null_t *null;
    box_render_shading_t rendered, expected;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_border_rect(world, 0, &from);
    bool negative = false;
    Border v;
    int32_t i;
    box_transition_set(world, e, ecs_id(Border), &(TransitionSpec){
        .duration = 1.0f, .timing_function = CubicBezier,
        .bezier = { 1.0f / 3.0f, 2.0f, 2.0f / 3.0f, 2.0f } });
    tr_frames(world, 2);
    ecs_set_ptr(world, e, Border, &to);
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_assert(box_render_transition_test_value(world, e, ecs_id(Border), &v));
        tr_border_rendered(world, null, e, &rendered);
        test_assert(rendered.material[1] >= 0.0f);
        if (v.width < 0.0f) {
            negative = true;
            test_flt(rendered.material[1], 0);
        }

        tr_border_expected(world, e, &v, &expected);
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
    }

    test_assert(negative);
    tr_frame(world);
    tr_border_rendered(world, null, e, &rendered);
    test_flt(rendered.material[1], 0);
    tr_fini(world, null);
}

/** Create a filled square Polygon2D with a Border.
 *
 * @param world The world
 * @param border The border
 * @return The polygon
 */
static ecs_entity_t tr_border_polygon(
    ecs_world_t *world,
    const Border *border)
{
    const vec2 square[] = { {-5, -5}, {5, -5}, {5, 5}, {-5, 5} };
    ecs_entity_t path = test_path2(world, square, 4, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Polygon2D, {.path = path, .scale = {1, 1}});
    ecs_set(world, e, Rgb, {200, 100, 50});
    ecs_set_ptr(world, e, Border, border);

    return e;
}

void TransitionBorder_polygon(void) {
    // ui/border
    const Border from = { .width = 1, .radius = 0, .color = {0, 0, 0, 255} };
    const Border to = { .width = 3, .radius = 0, .color = {200, 100, 40, 255} };
    box_render_null_t *null;
    box_render_shading_t mid, end, expected;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_border_polygon(world, &from);
    tr_border_transition(world, e, ecs_id(Border));
    tr_frames(world, 2);
    test_int(ecs_get(world, e, Shape2D)->kind, BOX_SHAPE2D_POLYGON);
    tr_border_run(world, null, e, &to, &mid);
    tr_border_rendered(world, null, e, &end);
    test_flt(mid.material[1], 2);
    test_flt(end.material[1], 3);
    tr_fini(world, null);

    world = tr_scene(&null);
    e = tr_border_polygon(world, &to);
    tr_frames(world, 2);
    tr_border_rendered(world, null, e, &expected);
    tr_fini(world, null);
    test_assert(!ecs_os_memcmp_t(&end, &expected, box_render_shading_t));
}

/** A Border transition of a shape with a Tint transition of its parent and an Opacity
 * transition of its own: the border color is tinted with the rendered tints. */
void TransitionBorder_with_tint_opacity(void) {
    // ui/border
    const Border from = { .width = 2, .radius = 4, .color = {0, 0, 0, 255} };
    const Border to = { .width = 2, .radius = 4, .color = {200, 200, 200, 255} };
    const Border half = { .width = 2, .radius = 4, .color = {100, 100, 100, 255} };
    box_render_null_t *null;
    box_render_shading_t rendered, expected;
    int32_t i;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Tint, {0, 0, 0, 0});
    tr_border_transition(world, parent, ecs_id(Tint));
    ecs_entity_t e = tr_border_rect(world, parent, &from);
    ecs_set(world, e, Opacity, {1.0f});
    tr_border_transition(world, e, ecs_id(Border));
    tr_border_transition(world, e, ecs_id(Opacity));
    tr_frames(world, 2);

    ecs_set(world, parent, Tint, {200, 100, 40, 200});
    ecs_set(world, e, Opacity, {0.5f});
    ecs_set_ptr(world, e, Border, &to);
    tr_frames(world, TR_BORDER_MID);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_SHADING), 1);
    test_assert(tr_records(null, BOX_RENDER_TRANSITION_FADE) >= 1);
    test_assert(tr_batch(world, e)->key.dynamic_material);
    tr_border_rendered(world, null, e, &rendered);
    tr_fini(world, null);

    world = tr_scene(&null);
    parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Tint, {100, 50, 20, 100});
    e = tr_border_rect(world, parent, &half);
    ecs_set(world, e, Opacity, {0.75f});
    tr_frames(world, 2);
    tr_border_rendered(world, null, e, &expected);
    tr_fini(world, null);

    test_assert(!ecs_os_memcmp(rendered.material, expected.material, 2 * ECS_SIZEOF(float)));
    for (i = 2; i < 4; i ++) {
        test_assert(fabsf(tr_border_half(&rendered, i, false) -
            tr_border_half(&expected, i, false)) <= TR_BORDER_COLOR_EPS);
        test_assert(fabsf(tr_border_half(&rendered, i, true) -
            tr_border_half(&expected, i, true)) <= TR_BORDER_COLOR_EPS);
    }

    float ca[4], cb[4];
    box_draws_color_unpack(ca, rendered.color);
    box_draws_color_unpack(cb, expected.color);
    for (i = 0; i < 4; i ++) {
        test_assert(fabsf(ca[i] - cb[i]) <= TR_BORDER_COLOR_EPS);
    }
}

/** Read the transform and the shading of the shadow of an entity as the backend got them in the
 * last packet.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param transform Receives the transform
 * @param shading Receives the shading
 */
static void tr_border_shadow_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_transform_t *transform,
    box_render_shading_t *shading)
{
    box_render_draws_t *d = box_draws_state(world);
    const BoxShadowSlot *bs = ecs_get(world, e, BoxShadowSlot);
    test_assert(bs != NULL);
    test_assert(bs->batch >= 0);
    box_draw_batch_t *b = box_draws_batch(d, bs->batch);
    box_render_sync(world);
    *transform = *box_draws_slot_transform(b, bs->slot);
    *shading = *box_draws_slot_shading(b, bs->slot);
    tr_slot_rendered(world, null, b, bs->slot, BOX_RENDER_STREAM_TRANSFORM,
        (uint32_t)sizeof(box_render_transform_t), transform);
    tr_slot_rendered(world, null, b, bs->slot, BOX_RENDER_STREAM_SHADING,
        (uint32_t)sizeof(box_render_shading_t), shading);
}

/** The shadow of the cases */
static const BoxShadow tr_border_shadow = {
    .offset_x = 2, .offset_y = -3, .blur = 4, .spread = 1, .color = {0, 0, 0, 160}};

/** Render a world with a bordered and shadowed rectangle set up without a transition and read
 * its shadow.
 *
 * @param border The border
 * @param transform Receives the transform of the shadow
 * @param shading Receives the shading of the shadow
 */
static void tr_border_shadow_reference(
    const Border *border,
    box_render_transform_t *transform,
    box_render_shading_t *shading)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_border_rect(world, 0, border);
    ecs_set_ptr(world, e, BoxShadow, &tr_border_shadow);
    tr_frames(world, 2);
    tr_border_shadow_rendered(world, null, e, transform, shading);
    tr_fini(world, null);
}

/** Run a Border transition of a shadowed rectangle and check its shadow half way against a
 * static one at the half way Border, and at the end byte for byte against the target.
 *
 * @param from The border it starts at
 * @param to The border it changes to
 * @param half The border half way
 */
static void tr_border_test_shadow(
    const Border *from,
    const Border *to,
    const Border *half)
{
    box_render_null_t *null;
    box_render_transform_t transform, end_transform, expected_transform;
    box_render_shading_t shading, end_shading, expected_shading;
    int32_t i;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_border_rect(world, 0, from);
    ecs_set_ptr(world, e, BoxShadow, &tr_border_shadow);
    tr_border_transition(world, e, ecs_id(Border));
    tr_frames(world, 2);
    test_assert(!ecs_has_id(world, e, TransitionBoxShadow));

    ecs_set_ptr(world, e, Border, to);
    tr_frames(world, TR_BORDER_MID);
    test_assert(ecs_has_id(world, e, TransitionBoxShadow));
    test_assert(ecs_has_id(world, e, TransitionMaterial));
    test_int(tr_records(null, BOX_RENDER_TRANSITION_SHADOW_BOX), 1);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_SHADING), 1);
    tr_border_shadow_rendered(world, null, e, &transform, &shading);
    tr_frames(world, TR_BORDER_END - TR_BORDER_MID);
    test_assert(!ecs_has_id(world, e, TransitionBoxShadow));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADOW_BOX), 0);
    tr_border_shadow_rendered(world, null, e, &end_transform, &end_shading);
    tr_fini(world, null);

    tr_border_shadow_reference(half, &expected_transform, &expected_shading);
    for (i = 0; i < 4; i ++) {
        test_assert(fabsf(shading.material[i] - expected_shading.material[i]) <= TR_BORDER_EPS);
        test_assert(fabsf(shading.uv[i] - expected_shading.uv[i]) <= TR_BORDER_EPS);
    }

    float ca[4], cb[4];
    box_draws_color_unpack(ca, shading.color);
    box_draws_color_unpack(cb, expected_shading.color);
    for (i = 0; i < 4; i ++) {
        test_assert(fabsf(ca[i] - cb[i]) <= TR_BORDER_SHADOW_COLOR_EPS);
    }

    tr_border_shadow_reference(to, &expected_transform, &expected_shading);
    test_assert(!ecs_os_memcmp_t(&end_transform, &expected_transform, box_render_transform_t));
    test_assert(!ecs_os_memcmp_t(&end_shading, &expected_shading, box_render_shading_t));
}

void TransitionBorder_box_shadow_radius(void) {
    // ui/border
    const Border from = { .width = 1, .radius = 0, .color = {40, 110, 240, 255} };
    const Border to = { .width = 1, .radius = 8, .color = {40, 110, 240, 255} };
    const Border half = { .width = 1, .radius = 4, .color = {40, 110, 240, 255} };
    tr_border_test_shadow(&from, &to, &half);
}

void TransitionBorder_box_shadow_cover(void) {
    // ui/border
    const Border from = { .width = 2, .radius = 4, .color = {40, 110, 240, 255} };
    const Border to = { .width = 2, .radius = 4, .color = {40, 110, 240, 55} };
    const Border half = { .width = 2, .radius = 4, .color = {40, 110, 240, 155} };
    tr_border_test_shadow(&from, &to, &half);
}

/** Instances of a prefab that declares the transition own a copy of its Border: a change of the
 * copy of one patches that instance alone, and a change of the prefab visits the prefab alone. */
void TransitionBorder_prefab_instances(void) {
    // ui/border
    const Border from = { .width = 1, .radius = 0, .color = {0, 0, 0, 255} };
    const Border to = { .width = 3, .radius = 8, .color = {200, 100, 40, 255} };
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t instances[32];
    box_render_transitions_t *s;
    int32_t i, j;
    ecs_set(world, prefab, Rect, {TR_BORDER_W, TR_BORDER_H});
    ecs_set(world, prefab, Rgb, {200, 100, 50});
    ecs_set_ptr(world, prefab, Border, &from);
    tr_border_transition(world, prefab, ecs_id(Border));
    for (i = 0; i < 32; i ++) {
        instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, instances[i], Position, {(float)i * 30.0f, 0, 0});
    }

    tr_frames(world, 2);
    s = box_render_transitions_get(world);
    test_assert(ecs_owns(world, instances[0], Border));
    ecs_set_ptr(world, instances[0], Border, &to);
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 1);
        test_int(box_render_transitions_material_visited(s), 1);
        test_assert(ecs_has_id(world, instances[0], TransitionMaterial));
        for (j = 1; j < 32; j ++) {
            test_assert(!ecs_has_id(world, instances[j], TransitionMaterial));
        }
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    test_assert(!ecs_has_id(world, instances[0], TransitionMaterial));

    ecs_set_ptr(world, prefab, Border, &to);
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
        test_int(box_render_transitions_material_visited(s), 1);
        for (j = 0; j < 32; j ++) {
            test_assert(!ecs_has_id(world, instances[j], TransitionMaterial));
            test_flt(ecs_get(world, instances[j], Border)->width, j ? 1 : 3);
        }
    }

    tr_fini(world, null);
}

/** The shape is in a dynamic material batch only while its Border transitions, and an idle node
 * sends no records. */
void TransitionBorder_dynamic_batch(void) {
    // ui/border
    const Border from = { .width = 1, .radius = 0, .color = {0, 0, 0, 255} };
    const Border to = { .width = 3, .radius = 8, .color = {200, 100, 40, 255} };
    box_render_null_t *null;
    box_render_shading_t mid, rendered, expected;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_border_rect(world, 0, &from);
    int32_t i;
    tr_border_transition(world, e, ecs_id(Border));
    for (i = 0; i < 3; i ++) {
        tr_frame(world);
        if (i) {
            test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
            test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_START), 0);
        }

        test_assert(!ecs_has_id(world, e, TransitionMaterial));
        test_assert(!tr_batch(world, e)->key.dynamic_material);
    }

    tr_border_run(world, null, e, &to, &mid);
    for (i = 0; i < 2; i ++) {
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_START), 0);
        test_assert(!ecs_has_id(world, e, TransitionMaterial));
        test_assert(!tr_batch(world, e)->key.dynamic_material);
    }

    tr_border_rendered(world, null, e, &rendered);
    tr_border_expected(world, e, &to, &expected);
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
    tr_fini(world, null);
}

/** A Line draws no border and an entity without a 2D shape has no 2D shading: a Border
 * transition of either sends nothing. */
void TransitionBorder_no_shape(void) {
    // ui/border
    const Border from = { .width = 1, .radius = 0, .color = {0, 0, 0, 255} };
    const Border to = { .width = 3, .radius = 8, .color = {200, 100, 40, 255} };
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t line = ecs_new(world);
    ecs_entity_t mesh = tr_shape(world, 0, 4);
    int32_t i;
    ecs_set(world, line, Position, {0, 0, 0});
    ecs_set(world, line, Line, {.x1 = -2, .y1 = 0, .x2 = 2, .y2 = 1, .width = 1});
    ecs_set_ptr(world, line, Border, &from);
    ecs_set_ptr(world, mesh, Border, &from);
    tr_border_transition(world, line, ecs_id(Border));
    tr_border_transition(world, mesh, ecs_id(Border));
    tr_frames(world, 2);

    ecs_set_ptr(world, line, Border, &to);
    ecs_set_ptr(world, mesh, Border, &to);
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
        test_assert(!ecs_has_id(world, line, TransitionMaterial));
        test_assert(!ecs_has_id(world, mesh, TransitionMaterial));
        test_assert(!tr_batch(world, line)->key.dynamic_material);
    }

    tr_fini(world, null);
}

/** present holds a bit for each of the 17 components: a target with every bit set encodes the
 * shading and the material record as the game thread does, with the Border of the target
 * applied to its shape. */
void TransitionBorder_present_bits(void) {
    // ui/border
    const Rgb color = {200, 100, 50};
    const Opacity opacity = {0.5f};
    const AlphaCutoff cutoff = {0.5f};
    const PbrMaterial pbr = {0.25f, 0.75f};
    const Emissive emissive = { .strength = 2, .color = {10, 20, 30} };
    const TextureTransform uv = {2, 3, 0.5f, 0.25f};
    const Transmission transmission = { .transmission_factor = 0.5f, .ior = 1.5f };
    const ClearCoat coat = {0.5f, 0.25f};
    const SpecularAnisotropy aniso = {0.5f, 1.0f};
    const Sheen sheen = { .color = {10, 20, 30}, .roughness = 0.5f };
    const DiffuseTransmission diffuse = { .factor = 0.5f, .color = {10, 20, 30} };
    const Fresnel fresnel = {
        .color = {10, 20, 30}, .strength = 2, .power = 3, .bias = 0.25f, .opacity = 0.5f };
    const Tint tint = {40, 80, 120, 100};
    const Border border = { .width = 2, .radius = 0, .color = {40, 110, 240, 200} };
    const Border other = { .width = 3, .radius = 6, .color = {240, 110, 40, 100} };
    box_draw_tint_t inherited = { .add = {0.1f, 0.2f, 0.3f}, .keep = 0.5f };
    Shape2D shape = { .kind = BOX_SHAPE2D_RECT, .size = {20, 40}, .uv = {1, 1, 0, 0} };
    Shape2D patched;
    box_draw_transition_material_t target;
    box_render_shading_t rendered, expected;
    box_render_material_t record, record_expected;
    box_shape2d_border(&shape, &border);
    box_draw_material_t m = {
        .color = &color, .opacity = &opacity, .cutoff = &cutoff, .pbr = &pbr,
        .emissive = &emissive, .uv = &uv, .transmission = &transmission, .clear_coat = &coat,
        .anisotropy = &aniso, .sheen = &sheen, .diffuse_transmission = &diffuse,
        .fresnel = &fresnel, .tint = &tint, .inherited = &inherited, .shape2d = &shape,
        .border = &border, .border_radius = true, .fade = 0.75f, .rank = 3
    };
    uint32_t all = (uint32_t)((1ull << BOX_RENDER_TRANSITION_COMPONENTS) - 1);

    test_int(BOX_RENDER_TRANSITION_COMPONENTS, 18);
    test_assert(ECS_SIZEOF(target.present) * 8 >= BOX_RENDER_TRANSITION_COMPONENTS);

    box_draws_transition_target(&target, &m, BOX_DRAW_TRANSITION_SHAPE);
    test_assert(target.present & (1u << BOX_RENDER_TRANSITION_BORDER));
    test_assert(target.present & (1u << BOX_RENDER_TRANSITION_TINT));
    test_assert(target.present & (1u << BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION));
    test_assert(target.present & (1u << BOX_RENDER_TRANSITION_FRESNEL));
    target.present = all;
    box_draws_material(&expected, &m);
    rendered = expected;
    ecs_os_memset(rendered.material, 0, ECS_SIZEOF(rendered.material));
    ecs_os_memset(rendered.color, 0, ECS_SIZEOF(rendered.color));
    box_draws_transition_encode(&target, &rendered);
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));

    target.border = other;
    patched = shape;
    patched.radius = 0;
    box_shape2d_border(&patched, &other);
    m.shape2d = &patched;
    box_draws_material(&expected, &m);
    box_draws_transition_encode(&target, &rendered);
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
    test_flt(rendered.material[0], 0.6f);
    test_flt(rendered.material[1], 0.3f);

    m.shape2d = NULL;
    m.border = NULL;
    box_draws_transition_target(&target, &m, BOX_DRAW_TRANSITION_TABLE);
    test_assert(!(target.present & (1u << BOX_RENDER_TRANSITION_BORDER)));
    target.present = all;
    box_draws_material_pack(&record_expected, &m);
    ecs_os_memset_t(&record, 0, box_render_material_t);
    box_draws_transition_encode(&target, &record);
    test_assert(!ecs_os_memcmp_t(&record, &record_expected, box_render_material_t));
}
