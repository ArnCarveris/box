/**
 * @file TransitionTintOpacity.c
 * @brief Tests of the TransitionTintOpacity suite: Tint and Opacity transitions, composed down
 * the hierarchy: the mesh, 2D and glyph shading the resolver patches, read from the last packet
 * of the null backend, and the blended batch of a shape during an Opacity transition.
 *
 * The cases step the world with a fixed delta and a Linear transition of one second, so the
 * four frames after a change render at progress 0, 1/4, 1/2 and 3/4 and the fifth at the target.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"
#include "draw.h"

/** Seconds every frame advances the world, a power of two so the clock is exact */
#define TR_DELTA (0.25f)

/** Units of unorm16 a patched tint word may differ by from the expected one: the resolver
 * composes the tints in another order than box_draws_fade */
#define TR_UNORM_EPS (2)

/** Difference a patched alpha or color may have from the expected one */
#define TR_FLOAT_EPS (1e-4f)

/** A value of an entity that a case renders instead of the target of the entity */
typedef struct tr_override_t {
    ecs_entity_t entity;            /**< The entity, 0 for none */
    const Tint *tint;               /**< Its rendered Tint, NULL for its target */
    const Opacity *opacity;         /**< Its rendered Opacity, NULL for its target */
} tr_override_t;

/** Most overrides of a case */
#define TR_OVERRIDES (2)

/** Create a world that renders into the null backend with a view, so the batches are sent.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* tr_scene(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 20});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    return world;
}

/** Delete a world and its null backend.
 *
 * @param world The world
 * @param null The backend
 */
static void tr_fini(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_fini(world);
    box_render_null_free(null);
}

/** Run one frame and wait until the render thread resolved its packet.
 *
 * @param world The world
 */
static void tr_frame(
    ecs_world_t *world)
{
    ecs_progress(world, TR_DELTA);
    box_render_sync(world);
}

/** Count the records of an op in the last packet, once the render thread resolved it.
 *
 * @param world The world
 * @param null The backend
 * @param op The op
 * @return The records
 */
static int32_t tr_ops(
    ecs_world_t *world,
    const box_render_null_t *null,
    box_render_transition_op_t op)
{
    int32_t i, result = 0;
    box_render_sync(world);
    for (i = 0; i < ecs_vec_count(&null->last.transitions); i ++) {
        const box_render_transition_record_t *r = ecs_vec_get_t(
            &null->last.transitions, box_render_transition_record_t, i);
        result += r->op == op;
    }

    return result;
}

/** Create a box shape.
 *
 * @param world The world
 * @param parent ChildOf parent, 0 for none
 * @return The shape
 */
static ecs_entity_t tr_shape(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 2, 3});
    ecs_set(world, e, Rgb, {200, 100, 50});

    return e;
}

/** Create a 2D rectangle.
 *
 * @param world The world
 * @param parent ChildOf parent, 0 for none
 * @return The rectangle
 */
static ecs_entity_t tr_rect(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rect, {10, 20});
    ecs_set(world, e, Rgb, {200, 100, 50});

    return e;
}

/** Create an entity without a shape, the parent of the shapes of a case.
 *
 * @param world The world
 * @return The entity
 */
static ecs_entity_t tr_group(
    ecs_world_t *world)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});

    return e;
}

/** Get the batch of the shape of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The batch
 */
static box_draw_batch_t* tr_batch(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_render_draws_t *d = box_draws_state(world);
    const DrawSlot *ds = ecs_get(world, e, DrawSlot);
    test_assert(d != NULL);
    test_assert(ds != NULL);
    test_assert(ds->batch >= 0);

    return box_draws_batch(d, ds->batch);
}

/** Read the shading slot of an entity as the backend got it in the last packet: the patched
 * bytes of the run upload of a dynamic_material batch, else the static slot.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param out Receives the shading, a box_render_shading_t for a 2D shape and a
 *        box_render_mesh_shading_t for a mesh
 */
static void tr_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    void *out)
{
    box_render_draws_t *d = box_draws_state(world);
    box_draw_batch_t *b = tr_batch(world, e);
    int32_t slot = ecs_get(world, e, DrawSlot)->slot;
    bool flat = b->key.pipeline == BOX_RENDER_SHADER_SHAPE2D;
    uint32_t stride = flat ? (uint32_t)sizeof(box_render_shading_t)
        : (uint32_t)sizeof(box_render_mesh_shading_t);
    bool found = false;
    int32_t i;
    box_render_sync(world);
    if (flat) {
        ecs_os_memcpy(out, box_draws_slot_shading(b, slot), (ecs_size_t)stride);
    } else {
        ecs_os_memcpy(out, box_draws_slot_mesh_shading(b, slot), (ecs_size_t)stride);
    }

    if (!b->key.dynamic_material) {
        goto done;
    }

    uint32_t handle = box_draws_store_handle(box_draws_batch_store(d, b), b->page,
        BOX_RENDER_STREAM_SHADING);
    uint32_t offset = b->first * stride;
    for (i = 0; i < ecs_vec_count(&null->last.uploads) && !found; i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == handle && u->offset == offset) {
            ecs_os_memcpy(out, box_render_packet_data(&null->last,
                u->data + (uint32_t)slot * stride), (ecs_size_t)stride);
            found = true;
        }
    }

    test_assert(found);
done:
    return;
}

/** Find the override of an entity.
 *
 * @param overrides The overrides, TR_OVERRIDES of them
 * @param e The entity
 * @return The override, NULL for none
 */
static const tr_override_t* tr_override(
    const tr_override_t *overrides,
    ecs_entity_t e)
{
    const tr_override_t *result = NULL;
    int32_t i;
    for (i = 0; overrides && i < TR_OVERRIDES && !result; i ++) {
        if (overrides[i].entity == e) {
            result = &overrides[i];
        }
    }

    return result;
}

/** Get the Tint an entity renders in a case.
 *
 * @param world The world
 * @param overrides The overrides
 * @param e The entity
 * @return The Tint, NULL for none
 */
static const Tint* tr_tint(
    const ecs_world_t *world,
    const tr_override_t *overrides,
    ecs_entity_t e)
{
    const tr_override_t *o = tr_override(overrides, e);

    return o && o->tint ? o->tint : ecs_get(world, e, Tint);
}

/** Get the Opacity an entity renders in a case.
 *
 * @param world The world
 * @param overrides The overrides
 * @param e The entity
 * @return The Opacity, NULL for none
 */
static const Opacity* tr_opacity(
    const ecs_world_t *world,
    const tr_override_t *overrides,
    ecs_entity_t e)
{
    const tr_override_t *o = tr_override(overrides, e);

    return o && o->opacity ? o->opacity : ecs_get(world, e, Opacity);
}

/** Compose the Tint and Opacity of the ancestors of an entity as box_draws_fade does, with the
 * rendered values of the overrides.
 *
 * @param world The world
 * @param overrides The overrides
 * @param e The entity
 * @param tint Receives the composed tints
 * @return The product of the opacities
 */
static float tr_fade(
    const ecs_world_t *world,
    const tr_override_t *overrides,
    ecs_entity_t e,
    box_draw_tint_t *tint)
{
    float result = 1.0f;
    ecs_entity_t cur = ecs_get_parent(world, e);
    *tint = (box_draw_tint_t){ .keep = 1.0f };
    while (cur) {
        const Opacity *opacity = tr_opacity(world, overrides, cur);
        const Tint *t = tr_tint(world, overrides, cur);
        if (opacity) {
            result *= glm_clamp(opacity->value, 0.0f, 1.0f);
        }

        if (t) {
            float a = (float)t->a / 255.0f;
            float weight = a * tint->keep / 255.0f;
            tint->add[0] += (float)t->r * weight;
            tint->add[1] += (float)t->g * weight;
            tint->add[2] += (float)t->b * weight;
            tint->keep *= 1.0f - a;
        }

        cur = ecs_get_parent(world, cur);
    }

    return result;
}

/** Get the material an entity renders with in a case.
 *
 * @param world The world
 * @param overrides The overrides
 * @param e The entity
 * @param tint Receives the composed tints of the ancestors
 * @param out Receives the material
 */
static void tr_material(
    ecs_world_t *world,
    const tr_override_t *overrides,
    ecs_entity_t e,
    box_draw_tint_t *tint,
    box_draw_material_t *out)
{
    const DrawSlot *ds = ecs_get(world, e, DrawSlot);
    test_assert(ds != NULL);
    box_draws_shape_material(world, e, ds, out);
    out->fade = tr_fade(world, overrides, e, tint);
    out->inherited = tint;
    out->tint = tr_tint(world, overrides, e);
    out->opacity = tr_opacity(world, overrides, e);
}

/** Check that a unorm16 half of a tint word is near the expected one.
 *
 * @param a The rendered word
 * @param b The expected word
 * @param shift 0 for the low half, 16 for the high half
 */
static void tr_unorm_near(
    uint32_t a,
    uint32_t b,
    int32_t shift)
{
    int32_t x = (int32_t)((a >> shift) & 0xffffu);
    int32_t y = (int32_t)((b >> shift) & 0xffffu);
    test_assert(abs(x - y) <= TR_UNORM_EPS);
}

/** Check the mesh shading an entity rendered with in the last packet against the one of a case.
 *
 * @param world The world
 * @param null The backend
 * @param overrides The overrides
 * @param e The entity
 * @return The rendered shading
 */
static box_render_mesh_shading_t tr_check_mesh(
    ecs_world_t *world,
    const box_render_null_t *null,
    const tr_override_t *overrides,
    ecs_entity_t e)
{
    box_render_mesh_shading_t rendered, expected;
    box_draw_material_t m;
    box_draw_tint_t tint;
    tr_rendered(world, null, e, &rendered);
    tr_material(world, overrides, e, &tint, &m);
    box_draws_mesh_shading(&expected, rendered.material, &m);
    test_uint(rendered.material, expected.material);
    tr_unorm_near(rendered.tint[0], expected.tint[0], 0);
    tr_unorm_near(rendered.tint[0], expected.tint[0], 16);
    tr_unorm_near(rendered.tint[1], expected.tint[1], 0);
    tr_unorm_near(rendered.tint[1], expected.tint[1], 16);
    test_assert(fabsf(rendered.alpha - expected.alpha) < TR_FLOAT_EPS);

    return rendered;
}

/** Check that the mesh shading of an entity in its static slot is the one of its targets,
 * byte for byte.
 *
 * @param world The world
 * @param e The entity
 */
static void tr_check_mesh_target(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_draw_batch_t *b = tr_batch(world, e);
    const DrawSlot *ds = ecs_get(world, e, DrawSlot);
    box_render_mesh_shading_t expected, *slot = box_draws_slot_mesh_shading(b, ds->slot);
    box_draw_material_t m;
    box_draws_shape_material(world, e, ds, &m);
    box_draws_mesh_shading(&expected, slot->material, &m);
    test_assert(!b->key.dynamic_material);
    test_assert(!ecs_os_memcmp_t(slot, &expected, box_render_mesh_shading_t));
}

/** Check the 2D shading an entity rendered with in the last packet against the one of a case.
 *
 * @param world The world
 * @param null The backend
 * @param overrides The overrides
 * @param e The entity
 */
static void tr_check_shape(
    ecs_world_t *world,
    const box_render_null_t *null,
    const tr_override_t *overrides,
    ecs_entity_t e)
{
    box_render_shading_t rendered, expected;
    box_draw_material_t m;
    box_draw_tint_t tint;
    int32_t i;
    tr_rendered(world, null, e, &rendered);
    tr_material(world, overrides, e, &tint, &m);
    box_draws_material(&expected, &m);
    for (i = 0; i < 4; i ++) {
        test_assert(fabsf(rendered.color[i] - expected.color[i]) < TR_FLOAT_EPS);
        test_assert(fabsf(rendered.uv[i] - expected.uv[i]) < TR_FLOAT_EPS);
    }

    test_assert(!ecs_os_memcmp(rendered.ids, expected.ids, ECS_SIZEOF(rendered.ids)));
}

/** Mix two values of a component at a progress, as the resolver does with Linear timing.
 *
 * @param c The component
 * @param from The value at the start
 * @param to The target
 * @param t Progress
 * @param out Receives the value, BOX_RENDER_TRANSITION_VALUE bytes
 */
static void tr_mix(
    box_render_transition_component_t c,
    const void *from,
    const void *to,
    float t,
    void *out)
{
    uint8_t a[BOX_RENDER_TRANSITION_VALUE], b[BOX_RENDER_TRANSITION_VALUE];
    box_render_transition_pack(c, from, a);
    box_render_transition_pack(c, to, b);
    box_render_transition_mix(c, a, b, t, out);
}

/** Get the Opacity of an Opacity transition at a frame of a case.
 *
 * @param from The value at the start
 * @param to The target
 * @param i The frame after the change, 0 to 3
 * @return The value
 */
static Opacity tr_opacity_at(
    float from,
    float to,
    int32_t i)
{
    Opacity a = {from}, b = {to};
    uint8_t out[BOX_RENDER_TRANSITION_VALUE];
    Opacity result;
    tr_mix(BOX_RENDER_TRANSITION_OPACITY, &a, &b, (float)i / 4.0f, out);
    ecs_os_memcpy(&result, out, ECS_SIZEOF(Opacity));

    return result;
}

/** Get the Tint of a Tint transition at a frame of a case.
 *
 * @param from The value at the start
 * @param to The target
 * @param i The frame after the change, 0 to 3
 * @return The value
 */
static Tint tr_tint_at(
    const Tint *from,
    const Tint *to,
    int32_t i)
{
    uint8_t out[BOX_RENDER_TRANSITION_VALUE];
    Tint result;
    tr_mix(BOX_RENDER_TRANSITION_TINT, from, to, (float)i / 4.0f, out);
    ecs_os_memcpy(&result, out, ECS_SIZEOF(Tint));

    return result;
}

/** Add a Linear transition of a second to an entity.
 *
 * @param world The world
 * @param e The entity
 * @param component The component
 */
static void tr_transition(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component)
{
    box_transition_set(world, e, component, &(TransitionSpec){ .duration = 1.0f });
}

void TransitionTintOpacity_tint_mesh(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0);
    const Tint from = {0, 0, 255, 0}, to = {0, 0, 255, 200};
    int32_t i;
    ecs_set_ptr(world, e, Tint, &from);
    tr_transition(world, e, ecs_id(Tint));
    tr_frame(world);
    tr_frame(world);
    box_draw_key_t key = tr_batch(world, e)->key;
    test_assert(!key.dynamic_material);

    ecs_set_ptr(world, e, Tint, &to);
    for (i = 0; i < 4; i ++) {
        Tint mid = tr_tint_at(&from, &to, i);
        tr_override_t o[TR_OVERRIDES] = {{ .entity = e, .tint = &mid }};
        tr_frame(world);
        test_int(ecs_get(world, e, Tint)->a, 200);
        test_assert(ecs_has_id(world, e, TransitionMaterial));
        test_assert(!ecs_has_id(world, e, TransitionOpacity));
        test_assert(tr_batch(world, e)->key.dynamic_material);
        test_int(tr_batch(world, e)->key.blend, Opaque);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 1);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 1);
        tr_check_mesh(world, null, o, e);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    test_assert(!ecs_os_memcmp_t(&tr_batch(world, e)->key, &key, box_draw_key_t));
    tr_check_mesh_target(world, e);
    tr_fini(world, null);
}

void TransitionTintOpacity_opacity_mesh(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0);
    int32_t i;
    ecs_set(world, e, Opacity, {0.2f});
    tr_transition(world, e, ecs_id(Opacity));
    tr_frame(world);
    tr_frame(world);
    test_int(tr_batch(world, e)->key.blend, AlphaBlend);

    ecs_set(world, e, Opacity, {0.6f});
    for (i = 0; i < 4; i ++) {
        Opacity mid = tr_opacity_at(0.2f, 0.6f, i);
        tr_override_t o[TR_OVERRIDES] = {{ .entity = e, .opacity = &mid }};
        tr_frame(world);
        test_flt(ecs_get(world, e, Opacity)->value, 0.6f);
        test_assert(ecs_has_id(world, e, TransitionOpacity));
        test_assert(tr_batch(world, e)->key.dynamic_material);
        test_int(tr_batch(world, e)->key.blend, AlphaBlend);
        box_render_mesh_shading_t r = tr_check_mesh(world, null, o, e);
        test_assert(fabsf(r.alpha - mid.value) < TR_FLOAT_EPS);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionOpacity));
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    test_int(tr_batch(world, e)->key.blend, AlphaBlend);
    tr_check_mesh_target(world, e);
    tr_fini(world, null);
}

void TransitionTintOpacity_tint_2d(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    const Tint from = {0, 255, 0, 0}, to = {0, 255, 0, 255};
    box_render_shading_t rendered, expected;
    box_draw_material_t m;
    int32_t i;
    ecs_set_ptr(world, e, Tint, &from);
    tr_transition(world, e, ecs_id(Tint));
    tr_frame(world);
    tr_frame(world);

    ecs_set_ptr(world, e, Tint, &to);
    for (i = 0; i < 4; i ++) {
        Tint mid = tr_tint_at(&from, &to, i);
        tr_override_t o[TR_OVERRIDES] = {{ .entity = e, .tint = &mid }};
        tr_frame(world);
        test_assert(ecs_has_id(world, e, TransitionMaterial));
        test_assert(tr_batch(world, e)->key.dynamic_material);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 1);
        tr_check_shape(world, null, o, e);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    test_assert(!tr_batch(world, e)->key.dynamic_material);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    box_draws_shape_material(world, e, ecs_get(world, e, DrawSlot), &m);
    box_draws_material(&expected, &m);
    tr_rendered(world, null, e, &rendered);
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
    tr_fini(world, null);
}

void TransitionTintOpacity_opacity_2d(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    box_render_shading_t rendered, expected;
    box_draw_material_t m;
    int32_t i;
    ecs_set(world, e, Opacity, {0.2f});
    tr_transition(world, e, ecs_id(Opacity));
    tr_frame(world);
    tr_frame(world);

    ecs_set(world, e, Opacity, {0.6f});
    for (i = 0; i < 4; i ++) {
        Opacity mid = tr_opacity_at(0.2f, 0.6f, i);
        tr_override_t o[TR_OVERRIDES] = {{ .entity = e, .opacity = &mid }};
        tr_frame(world);
        test_assert(ecs_has_id(world, e, TransitionOpacity));
        test_int(tr_batch(world, e)->key.blend, AlphaBlend);
        tr_check_shape(world, null, o, e);
        tr_rendered(world, null, e, &rendered);
        test_assert(fabsf(rendered.color[3] - mid.value) < TR_FLOAT_EPS);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionOpacity));
    test_assert(!tr_batch(world, e)->key.dynamic_material);
    box_draws_shape_material(world, e, ecs_get(world, e, DrawSlot), &m);
    box_draws_material(&expected, &m);
    tr_rendered(world, null, e, &rendered);
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
    tr_fini(world, null);
}

void TransitionTintOpacity_child_own_tint(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_group(world);
    ecs_entity_t child = tr_shape(world, parent);
    ecs_entity_t rect = tr_rect(world, parent);
    const Tint from = {255, 0, 0, 0}, to = {255, 0, 0, 255};
    int32_t i;
    ecs_set(world, child, Tint, {0, 0, 255, 128});
    ecs_set(world, rect, Tint, {0, 0, 255, 64});
    ecs_set_ptr(world, parent, Tint, &from);
    tr_transition(world, parent, ecs_id(Tint));
    tr_frame(world);
    tr_frame(world);

    ecs_set_ptr(world, parent, Tint, &to);
    for (i = 0; i < 4; i ++) {
        Tint mid = tr_tint_at(&from, &to, i);
        tr_override_t o[TR_OVERRIDES] = {{ .entity = parent, .tint = &mid }};
        tr_frame(world);
        test_assert(!ecs_has_id(world, parent, TransitionMaterial));
        test_assert(ecs_has_id(world, child, TransitionMaterial));
        test_assert(ecs_has_id(world, rect, TransitionMaterial));
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 1);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 2);
        tr_check_mesh(world, null, o, child);
        tr_check_shape(world, null, o, rect);
        test_int(ecs_get(world, child, Tint)->a, 128);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, child, TransitionMaterial));
    test_assert(!ecs_has_id(world, rect, TransitionMaterial));
    tr_check_mesh_target(world, child);
    tr_fini(world, null);
}

void TransitionTintOpacity_nested(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_group(world);
    ecs_entity_t child = tr_shape(world, parent);
    ecs_entity_t grandchild = tr_shape(world, child);
    const Tint from = {0, 255, 0, 0}, to = {0, 255, 0, 255};
    int32_t i;
    ecs_set(world, grandchild, Tint, {255, 0, 0, 100});
    ecs_set_ptr(world, parent, Tint, &from);
    ecs_set(world, parent, Opacity, {1.0f});
    ecs_set(world, child, Opacity, {1.0f});
    tr_transition(world, parent, ecs_id(Tint));
    tr_transition(world, parent, ecs_id(Opacity));
    tr_transition(world, child, ecs_id(Opacity));
    tr_frame(world);
    tr_frame(world);

    ecs_set_ptr(world, parent, Tint, &to);
    ecs_set(world, parent, Opacity, {0.5f});
    ecs_set(world, child, Opacity, {0.5f});
    for (i = 0; i < 4; i ++) {
        Tint tint = tr_tint_at(&from, &to, i);
        Opacity opacity = tr_opacity_at(1.0f, 0.5f, i);
        tr_override_t o[TR_OVERRIDES] = {
            { .entity = parent, .tint = &tint, .opacity = &opacity },
            { .entity = child, .opacity = &opacity }
        };
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 2);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 2);
        test_assert(ecs_has_id(world, child, TransitionOpacity));
        test_assert(ecs_has_id(world, grandchild, TransitionOpacity));
        box_render_mesh_shading_t c = tr_check_mesh(world, null, o, child);
        box_render_mesh_shading_t g = tr_check_mesh(world, null, o, grandchild);
        test_assert(fabsf(c.alpha - opacity.value * opacity.value) < TR_FLOAT_EPS);
        test_assert(fabsf(g.alpha - opacity.value * opacity.value) < TR_FLOAT_EPS);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, child, TransitionOpacity));
    test_assert(!ecs_has_id(world, grandchild, TransitionOpacity));
    tr_check_mesh_target(world, child);
    tr_check_mesh_target(world, grandchild);
    test_flt(box_draws_slot_mesh_shading(tr_batch(world, grandchild),
        ecs_get(world, grandchild, DrawSlot)->slot)->alpha, 0.25f);
    tr_fini(world, null);
}

void TransitionTintOpacity_opacity_target_zero(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_group(world);
    ecs_entity_t child = tr_shape(world, parent);
    int32_t i;
    ecs_set(world, parent, Opacity, {1.0f});
    tr_transition(world, parent, ecs_id(Opacity));
    tr_frame(world);
    tr_frame(world);

    ecs_set(world, parent, Opacity, {0.0f});
    for (i = 0; i < 4; i ++) {
        Opacity mid = tr_opacity_at(1.0f, 0.0f, i);
        tr_override_t o[TR_OVERRIDES] = {{ .entity = parent, .opacity = &mid }};
        tr_frame(world);
        test_flt(ecs_get(world, parent, Opacity)->value, 0);
        box_render_mesh_shading_t r = tr_check_mesh(world, null, o, child);
        test_assert(fabsf(r.alpha - (1.0f - (float)i / 4.0f)) < TR_FLOAT_EPS);
        test_int(tr_batch(world, child)->key.blend, AlphaBlend);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, child, TransitionOpacity));
    test_int(tr_batch(world, child)->key.blend, AlphaBlend);
    tr_check_mesh_target(world, child);
    test_flt(box_draws_slot_mesh_shading(tr_batch(world, child),
        ecs_get(world, child, DrawSlot)->slot)->alpha, 0);
    tr_fini(world, null);
}

void TransitionTintOpacity_opacity_start_zero(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_group(world);
    ecs_entity_t child = tr_shape(world, parent);
    int32_t i;
    ecs_set(world, parent, Opacity, {0.0f});
    tr_transition(world, parent, ecs_id(Opacity));
    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    test_assert(!ecs_has_id(world, child, TransitionOpacity));
    tr_frame(world);
    test_flt(box_draws_slot_mesh_shading(tr_batch(world, child),
        ecs_get(world, child, DrawSlot)->slot)->alpha, 0);

    ecs_set(world, parent, Opacity, {1.0f});
    for (i = 0; i < 4; i ++) {
        Opacity mid = tr_opacity_at(0.0f, 1.0f, i);
        tr_override_t o[TR_OVERRIDES] = {{ .entity = parent, .opacity = &mid }};
        tr_frame(world);
        box_render_mesh_shading_t r = tr_check_mesh(world, null, o, child);
        test_assert(fabsf(r.alpha - (float)i / 4.0f) < TR_FLOAT_EPS);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, child, TransitionOpacity));
    test_int(tr_batch(world, child)->key.blend, Opaque);
    tr_check_mesh_target(world, child);
    tr_fini(world, null);
}

void TransitionTintOpacity_fade_in_batch(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0);
    ecs_entity_t reference = tr_shape(world, 0);
    int32_t i;
    ecs_set(world, e, Opacity, {0.0f});
    tr_transition(world, e, ecs_id(Opacity));
    tr_frame(world);
    tr_frame(world);
    test_int(tr_batch(world, e)->key.blend, AlphaBlend);
    test_assert(!box_draws_caster(tr_batch(world, e)));
    test_assert(box_draws_caster(tr_batch(world, reference)));

    ecs_set(world, e, Opacity, {1.0f});
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        box_draw_batch_t *b = tr_batch(world, e);
        test_assert(ecs_has_id(world, e, TransitionOpacity));
        test_int(b->key.blend, AlphaBlend);
        test_assert(b->key.dynamic_material);
        test_assert(!b->key.bake);
        test_assert(!box_draws_caster(b));
        test_assert(ecs_get(world, e, DrawSlot)->batch !=
            ecs_get(world, reference, DrawSlot)->batch);
    }

    tr_frame(world);
    box_draw_batch_t *b = tr_batch(world, e);
    test_assert(!ecs_has_id(world, e, TransitionOpacity));
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    test_int(b->key.blend, Opaque);
    test_assert(!b->key.dynamic_material);
    test_assert(box_draws_caster(b));
    test_int(ecs_get(world, e, DrawSlot)->batch, ecs_get(world, reference, DrawSlot)->batch);
    tr_check_mesh_target(world, e);
    test_flt(box_draws_slot_mesh_shading(b, ecs_get(world, e, DrawSlot)->slot)->alpha, 1);
    tr_fini(world, null);
}

void TransitionTintOpacity_fade_out_batch(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0);
    ecs_entity_t reference = tr_shape(world, 0);
    int32_t i;
    ecs_set(world, e, Opacity, {1.0f});
    tr_transition(world, e, ecs_id(Opacity));
    tr_frame(world);
    tr_frame(world);
    test_int(tr_batch(world, e)->key.blend, Opaque);
    test_int(ecs_get(world, e, DrawSlot)->batch, ecs_get(world, reference, DrawSlot)->batch);

    ecs_set(world, e, Opacity, {0.0f});
    for (i = 0; i < 4; i ++) {
        Opacity mid = tr_opacity_at(1.0f, 0.0f, i);
        tr_override_t o[TR_OVERRIDES] = {{ .entity = e, .opacity = &mid }};
        tr_frame(world);
        test_int(tr_batch(world, e)->key.blend, AlphaBlend);
        test_assert(!box_draws_caster(tr_batch(world, e)));
        tr_check_mesh(world, null, o, e);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionOpacity));
    test_int(tr_batch(world, e)->key.blend, AlphaBlend);
    test_assert(!tr_batch(world, e)->key.dynamic_material);
    tr_check_mesh_target(world, e);
    tr_fini(world, null);
}

void TransitionTintOpacity_blend_mode_kept(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0);
    int32_t i;
    ecs_set(world, e, BlendMode, {Additive});
    ecs_set(world, e, Opacity, {1.0f});
    tr_transition(world, e, ecs_id(Opacity));
    tr_frame(world);
    tr_frame(world);

    ecs_set(world, e, Opacity, {0.5f});
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_assert(ecs_has_id(world, e, TransitionOpacity));
        test_int(tr_batch(world, e)->key.blend, Additive);
    }

    tr_frame(world);
    test_int(tr_batch(world, e)->key.blend, Additive);
    tr_fini(world, null);
}

void TransitionTintOpacity_light_no_effect(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 2, 0});
    ecs_set(world, light, PointLight, {.intensity = 1, .range = 10});
    ecs_set(world, light, Opacity, {1.0f});
    ecs_set(world, light, Tint, {0, 0, 0, 0});
    tr_transition(world, light, ecs_id(Opacity));
    tr_transition(world, light, ecs_id(Tint));
    tr_frame(world);
    tr_frame(world);

    ecs_set(world, light, Opacity, {0.0f});
    ecs_set(world, light, Tint, {255, 0, 0, 255});
    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    test_assert(!ecs_has_id(world, light, TransitionOpacity));
    test_assert(!ecs_has_id(world, light, TransitionMaterial));
    test_assert(!ecs_has(world, light, DrawSlot));

    ecs_entity_t child = tr_shape(world, light);
    tr_frame(world);
    tr_frame(world);
    test_assert(ecs_has_id(world, child, TransitionOpacity));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 1);
    tr_fini(world, null);
}

void TransitionTintOpacity_parent_relationship(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_group(world);
    ecs_entity_t child = tr_shape(world, 0);
    int32_t i;
    ecs_set(world, child, EcsParent, {parent});
    ecs_set(world, parent, Opacity, {1.0f});
    tr_transition(world, parent, ecs_id(Opacity));
    tr_frame(world);
    tr_frame(world);

    ecs_set(world, parent, Opacity, {0.5f});
    for (i = 0; i < 4; i ++) {
        Opacity mid = tr_opacity_at(1.0f, 0.5f, i);
        tr_override_t o[TR_OVERRIDES] = {{ .entity = parent, .opacity = &mid }};
        tr_frame(world);
        test_assert(ecs_has_id(world, child, TransitionOpacity));
        tr_check_mesh(world, null, o, child);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, child, TransitionOpacity));
    tr_check_mesh_target(world, child);
    tr_fini(world, null);
}

void TransitionTintOpacity_targets_on_game_thread(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0);
    int32_t i;
    ecs_set(world, e, Opacity, {1.0f});
    ecs_set(world, e, Tint, {0, 0, 0, 0});
    tr_transition(world, e, ecs_id(Opacity));
    tr_transition(world, e, ecs_id(Tint));
    tr_frame(world);

    ecs_set(world, e, Opacity, {0.0f});
    ecs_set(world, e, Tint, {200, 100, 40, 200});
    for (i = 0; i < 4; i ++) {
        Opacity opacity = {0};
        Tint tint = {0};
        tr_frame(world);
        test_flt(ecs_get(world, e, Opacity)->value, 0);
        test_int(ecs_get(world, e, Tint)->r, 200);
        test_int(ecs_get(world, e, Tint)->a, 200);
        test_assert(box_render_transition_test_value(world, e, ecs_id(Opacity), &opacity));
        test_assert(box_render_transition_test_value(world, e, ecs_id(Tint), &tint));
        test_assert(fabsf(opacity.value - (1.0f - (float)i / 4.0f)) < TR_FLOAT_EPS);
        test_int(tint.a, 50 * i);
    }

    tr_fini(world, null);
}

void TransitionTintOpacity_no_transition_no_records(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = tr_group(world);
    ecs_entity_t child = tr_shape(world, parent);
    tr_frame(world);
    ecs_set(world, parent, Opacity, {0.5f});
    ecs_set(world, parent, Tint, {1, 2, 3, 4});
    tr_frame(world);
    test_int(ecs_vec_count(&null->last.transitions), 0);
    test_assert(!ecs_has_id(world, child, TransitionOpacity));
    test_assert(!ecs_has_id(world, child, TransitionMaterial));
    test_assert(!tr_batch(world, child)->key.dynamic_material);
    tr_fini(world, null);
}

void TransitionTintOpacity_dynamic_material_fade_in(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0);
    ecs_entity_t reference = tr_shape(world, 0);
    box_render_mesh_shading_t rendered, expected;
    int32_t i;
    ecs_add(world, e, DynamicMaterial);
    ecs_add(world, reference, DynamicMaterial);
    ecs_set(world, e, Opacity, {0.0f});
    ecs_set(world, reference, Opacity, {1.0f});
    tr_transition(world, e, ecs_id(Opacity));
    tr_frame(world);
    tr_frame(world);
    box_draw_batch_t *b = tr_batch(world, e);
    test_int(b->key.blend, AlphaBlend);
    test_assert(b->key.dynamic_material);
    test_assert(!box_draws_caster(b));
    DrawSlot start = *ecs_get(world, e, DrawSlot);

    ecs_set(world, e, Opacity, {1.0f});
    for (i = 0; i < 4; i ++) {
        Opacity mid = tr_opacity_at(0.0f, 1.0f, i);
        tr_override_t o[TR_OVERRIDES] = {{ .entity = e, .opacity = &mid }};
        tr_frame(world);
        const DrawSlot *ds = ecs_get(world, e, DrawSlot);
        test_flt(ecs_get(world, e, Opacity)->value, 1.0f);
        test_assert(ecs_has_id(world, e, TransitionOpacity));
        test_int(ds->batch, start.batch);
        test_int(ds->slot, start.slot);
        b = tr_batch(world, e);
        test_int(b->key.blend, AlphaBlend);
        test_assert(b->key.dynamic_material);
        test_assert(!b->key.bake);
        test_assert(!box_draws_caster(b));
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 1);
        rendered = tr_check_mesh(world, null, o, e);
        test_assert(fabsf(rendered.alpha - mid.value) < TR_FLOAT_EPS);
        test_assert(fabsf(rendered.alpha - (float)i / 4.0f) < TR_FLOAT_EPS);
    }

    tr_frame(world);
    b = tr_batch(world, e);
    test_assert(!ecs_has_id(world, e, TransitionOpacity));
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    test_int(b->key.blend, Opaque);
    test_assert(b->key.dynamic_material);
    test_assert(box_draws_caster(b));
    test_int(ecs_get(world, e, DrawSlot)->batch, ecs_get(world, reference, DrawSlot)->batch);
    rendered = tr_check_mesh(world, null, NULL, e);
    tr_rendered(world, null, reference, &expected);
    test_assert(!ecs_os_memcmp(rendered.tint, expected.tint, ECS_SIZEOF(rendered.tint)));
    test_assert(!ecs_os_memcmp(&rendered.alpha, &expected.alpha, ECS_SIZEOF(float)));
    test_flt(rendered.alpha, 1);
    tr_fini(world, null);
}

/** The string of the text cases, one glyph per byte */
#define TR_TEXT "fade"

/** Create a Text with the font of the test assets, under a parent, colored by one span over
 * the whole string.
 *
 * @param world The world
 * @param parent ChildOf parent
 * @param font The font
 * @param color Display encoded color of the span
 * @return The text
 */
static ecs_entity_t tr_text(
    ecs_world_t *world,
    ecs_entity_t parent,
    ecs_entity_t font,
    rgba color)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, e, Position, {0, 1, 0});
    ecs_set(world, e, Text, { .value = TR_TEXT, .size = 16, .font = font });
    ecs_set(world, e, Rgb, {20, 200, 40});
    TextSpans *spans = ecs_ensure(world, e, TextSpans);
    *ecs_vec_append_t(NULL, &spans->spans, TextSpan) = (TextSpan){
        .start = 0, .end = (int32_t)(sizeof(TR_TEXT) - 1), .color = color
    };
    ecs_modified(world, e, TextSpans);

    return e;
}

/** Read the shading of a glyph slot as the backend got it in the last packet: the patched
 * bytes of the run upload of a dynamic_material batch, else the static slot.
 *
 * @param world The world
 * @param null The backend
 * @param at The glyph slot
 * @param out Receives the shading
 */
static void tr_glyph_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    const box_draw_glyph_slot_t *at,
    box_render_shading_t *out)
{
    box_render_draws_t *d = box_draws_state(world);
    box_draw_batch_t *b = box_draws_batch(d, at->batch);
    uint32_t stride = (uint32_t)sizeof(box_render_shading_t);
    bool found = false;
    int32_t i;
    box_render_sync(world);
    *out = *box_draws_slot_shading(b, at->slot);
    if (!b->key.dynamic_material) {
        goto done;
    }

    uint32_t handle = box_draws_store_handle(box_draws_batch_store(d, b), b->page,
        BOX_RENDER_STREAM_SHADING);
    uint32_t offset = b->first * stride;
    for (i = 0; i < ecs_vec_count(&null->last.uploads) && !found; i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == handle && u->offset == offset) {
            ecs_os_memcpy(out, box_render_packet_data(&null->last,
                u->data + (uint32_t)at->slot * stride), (ecs_size_t)stride);
            found = true;
        }
    }

    test_assert(found);
done:
    return;
}

void TransitionTintOpacity_text_spans_fade(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const rgba color = {255, 64, 0, 128};
    const Tint from = {0, 0, 255, 0}, to = {0, 0, 255, 200};
    ecs_entity_t font = ecs_entity(world, { .name = "tr_font" });
    ecs_set(world, font, Font, { .path = "test/assets/fonts/B612-Regular.ttf" });
    ecs_entity_t parent = tr_group(world);
    ecs_entity_t reference_parent = tr_group(world);
    ecs_entity_t text = tr_text(world, parent, font, color);
    ecs_entity_t reference = tr_text(world, reference_parent, font, color);
    int32_t i, j, count = (int32_t)(sizeof(TR_TEXT) - 1);
    ecs_set(world, parent, Opacity, {0.4f});
    ecs_set_ptr(world, parent, Tint, &from);
    ecs_set(world, reference_parent, Opacity, {1.0f});
    ecs_set_ptr(world, reference_parent, Tint, &to);
    tr_transition(world, parent, ecs_id(Opacity));
    tr_transition(world, parent, ecs_id(Tint));
    tr_frame(world);
    tr_frame(world);
    test_int(ecs_vec_count(&ecs_get(world, text, TextSlots)->slots), count);
    test_int(ecs_vec_count(&ecs_get(world, reference, TextSlots)->slots), count);
    test_assert(!ecs_has_id(world, text, TransitionOpacity));

    ecs_set(world, parent, Opacity, {1.0f});
    ecs_set_ptr(world, parent, Tint, &to);
    for (i = 0; i < 4; i ++) {
        Opacity opacity = tr_opacity_at(0.4f, 1.0f, i);
        Tint tint = tr_tint_at(&from, &to, i);
        tr_override_t o[TR_OVERRIDES] = {{ .entity = parent, .tint = &tint, .opacity = &opacity }};
        box_draw_material_t m, span, target;
        box_draw_tint_t inherited, target_inherited;
        box_render_shading_t rendered, expected, target_shading;
        Rgb rgb, target_rgb;
        Opacity span_opacity, target_opacity;
        tr_frame(world);
        test_assert(ecs_has_id(world, text, TransitionOpacity));
        test_assert(ecs_has_id(world, text, TransitionMaterial));
        test_assert(!ecs_has_id(world, parent, TransitionOpacity));

        box_draws_text_material(world, text, &target_inherited, &target);
        box_draws_glyph_span_material(&target, &color, &span, &target_rgb, &target_opacity);
        box_draws_material(&target_shading, &span);
        box_draws_text_material(world, text, &inherited, &m);
        m.fade = tr_fade(world, o, text, &inherited);
        m.inherited = &inherited;
        box_draws_glyph_span_material(&m, &color, &span, &rgb, &span_opacity);
        box_draws_material(&expected, &span);

        const TextSlots *ts = ecs_get(world, text, TextSlots);
        test_int(ecs_vec_count(&ts->slots), count);
        for (j = 0; j < count; j ++) {
            const box_draw_glyph_slot_t *at = ecs_vec_get_t(
                &ts->slots, box_draw_glyph_slot_t, j);
            box_draw_batch_t *b = box_draws_batch(box_draws_state(world), at->batch);
            int32_t c;
            test_int(b->key.pipeline, BOX_RENDER_SHADER_SHAPE2D);
            test_int(b->key.blend, AlphaBlend);
            test_assert(b->key.dynamic_material);
            tr_glyph_rendered(world, null, at, &rendered);
            test_assert(fabsf(rendered.color[3] -
                (float)color.a / 255.0f * opacity.value) < TR_FLOAT_EPS);
            for (c = 0; c < 4; c ++) {
                test_assert(fabsf(rendered.color[c] - expected.color[c]) < TR_FLOAT_EPS);
            }

            if (i == 2) {
                test_assert(fabsf(rendered.color[2] - target_shading.color[2]) > TR_FLOAT_EPS);
                test_assert(fabsf(rendered.color[3] - target_shading.color[3]) > TR_FLOAT_EPS);
            }
        }
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, text, TransitionOpacity));
    test_assert(!ecs_has_id(world, text, TransitionMaterial));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    const TextSlots *ts = ecs_get(world, text, TextSlots);
    const TextSlots *rs = ecs_get(world, reference, TextSlots);
    test_int(ecs_vec_count(&ts->slots), count);
    test_int(ecs_vec_count(&rs->slots), count);
    for (j = 0; j < count; j ++) {
        box_render_shading_t rendered, expected;
        const box_draw_glyph_slot_t *at = ecs_vec_get_t(&ts->slots, box_draw_glyph_slot_t, j);
        const box_draw_glyph_slot_t *ref = ecs_vec_get_t(&rs->slots, box_draw_glyph_slot_t, j);
        test_assert(!box_draws_batch(box_draws_state(world), at->batch)->key.dynamic_material);
        test_int(at->batch, ref->batch);
        tr_glyph_rendered(world, null, at, &rendered);
        tr_glyph_rendered(world, null, ref, &expected);
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
    }

    tr_fini(world, null);
}
