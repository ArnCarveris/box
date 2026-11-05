/**
 * @file TransitionAddRemove.c
 * @brief Tests of the TransitionAddRemove suite: on_add and on_remove of TransitionSpec, which
 * animate an added component in from its neutral value and a removed one out to it: the mesh,
 * 2D, material table and shadow records the resolver patches, read from the last packet of the
 * null backend.
 *
 * The cases step the world with a fixed delta and a Linear transition of one second, so the
 * four frames after a change render at progress 0, 1/4, 1/2 and 3/4 and the fifth at the target.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "draw.h"
#include "transition_test.h"

/** Difference a patched alpha or color may have from the expected one */
#define TR_FLOAT_EPS (1e-4f)

/** Difference a patched float of a transform may have from the expected one */
#define TR_TRANSFORM_EPS (1e-4f)

/** Difference a patched shadow color channel may have from the one of a static world: two
 * units of unorm16 */
#define TR_SHADOW_COLOR_EPS (2.0f / 65535.0f + 1e-7f)

/** The Tint the cases add and remove: its neutral value is {0, 0, 255, 0} */
static const Tint tr_tint = {0, 0, 255, 200};

/** Error messages logged since the collector was installed */
static ecs_strbuf_t tr_errors = ECS_STRBUF_INIT;

/** Log function the collector passes messages on to */
static ecs_os_api_log_t tr_log = NULL;

/** Count of errors logged since the collector was installed */
static int32_t tr_error_count = 0;

/** Collect logged errors without printing them.
 *
 * @param level Level of the message
 * @param file File that logged it
 * @param line Line that logged it
 * @param msg The message
 */
static void tr_collect_errors(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level == -3) {
        ecs_strbuf_appendstr(&tr_errors, msg);
        ecs_strbuf_appendch(&tr_errors, '\n');
        tr_error_count ++;
    }
}

/** Start collecting errors */
static void tr_capture_begin(void) {
    ecs_strbuf_reset(&tr_errors);
    tr_error_count = 0;
    tr_log = ecs_os_api.log_;
    ecs_os_api.log_ = tr_collect_errors;
}

/** Stop collecting errors.
 *
 * @return The collected errors, to free with ecs_os_free, NULL when none
 */
static char* tr_capture_end(void) {
    ecs_os_api.log_ = tr_log;

    return ecs_strbuf_get(&tr_errors);
}

/** A one second Linear transition that animates adding the component */
static const TransitionSpec tr_add = { .duration = 1.0f, .on_add = true };

/** A one second Linear transition that animates removing the component */
static const TransitionSpec tr_remove = { .duration = 1.0f, .on_remove = true };

/** A one second Linear transition that animates adding and removing the component */
static const TransitionSpec tr_both = { .duration = 1.0f, .on_add = true, .on_remove = true };

/** Get the component id of a transitionable component.
 *
 * @param c The component
 * @return The id
 */
static ecs_entity_t tr_id(
    box_render_transition_component_t c)
{
    ecs_entity_t result = 0;
    switch (c) {
    case BOX_RENDER_TRANSITION_OPACITY: result = ecs_id(Opacity); break;
    case BOX_RENDER_TRANSITION_TINT: result = ecs_id(Tint); break;
    case BOX_RENDER_TRANSITION_PBR_MATERIAL: result = ecs_id(PbrMaterial); break;
    case BOX_RENDER_TRANSITION_EMISSIVE: result = ecs_id(Emissive); break;
    case BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM: result = ecs_id(TextureTransform); break;
    case BOX_RENDER_TRANSITION_CLEAR_COAT: result = ecs_id(ClearCoat); break;
    case BOX_RENDER_TRANSITION_ANISOTROPY: result = ecs_id(SpecularAnisotropy); break;
    case BOX_RENDER_TRANSITION_SHEEN: result = ecs_id(Sheen); break;
    case BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION: result = ecs_id(DiffuseTransmission); break;
    default: test_assert(false); break;
    }

    return result;
}

/** Compute the value a transition renders at a frame of a case, as the resolver does with
 * Linear timing.
 *
 * @param c The component
 * @param from The value at the start, packed
 * @param to The target, packed
 * @param i The frame after the change, 0 to 4
 * @param out Receives the value, BOX_RENDER_TRANSITION_VALUE bytes
 */
static void tr_at(
    box_render_transition_component_t c,
    const void *from,
    const void *to,
    int32_t i,
    void *out)
{
    box_render_transition_mix(c, from, to, (float)i / 4.0f, out);
}

/** Get the Tint a transition renders at a frame of a case.
 *
 * @param from The value at the start
 * @param to The target
 * @param i The frame after the change, 0 to 4
 * @return The value
 */
static Tint tr_tint_at(
    Tint from,
    Tint to,
    int32_t i)
{
    float a[BOX_RENDER_TRANSITION_VALUE / 4], b[BOX_RENDER_TRANSITION_VALUE / 4];
    float out[BOX_RENDER_TRANSITION_VALUE / 4];
    Tint result;
    box_render_transition_pack(BOX_RENDER_TRANSITION_TINT, &from, a);
    box_render_transition_pack(BOX_RENDER_TRANSITION_TINT, &to, b);
    tr_at(BOX_RENDER_TRANSITION_TINT, a, b, i, out);
    ecs_os_memcpy(&result, out, ECS_SIZEOF(Tint));

    return result;
}

/** Point a material at the value of a component.
 *
 * @param m The material
 * @param c The component
 * @param value The value
 */
static void tr_material_set(
    box_draw_material_t *m,
    box_render_transition_component_t c,
    const void *value)
{
    switch (c) {
    case BOX_RENDER_TRANSITION_PBR_MATERIAL: m->pbr = value; break;
    case BOX_RENDER_TRANSITION_EMISSIVE: m->emissive = value; break;
    case BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM: m->uv = value; break;
    case BOX_RENDER_TRANSITION_CLEAR_COAT: m->clear_coat = value; break;
    case BOX_RENDER_TRANSITION_ANISOTROPY: m->anisotropy = value; break;
    case BOX_RENDER_TRANSITION_SHEEN: m->sheen = value; break;
    case BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION: m->diffuse_transmission = value; break;
    default: test_assert(false); break;
    }
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

/** Read the shading slot of an entity as the backend got it in the last packet: the patched
 * bytes of the run upload of a dynamic_material batch, else the static slot.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param out Receives the shading, a box_render_shading_t for a 2D shape and a
 *        box_render_mesh_shading_t for a mesh
 */
static void tr_shading_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    void *out)
{
    box_draw_batch_t *b = tr_batch(world, e);
    int32_t slot = ecs_get(world, e, DrawSlot)->slot;
    bool flat = b->key.pipeline == BOX_RENDER_SHADER_SHAPE2D;
    uint32_t stride = flat ? (uint32_t)sizeof(box_render_shading_t)
        : (uint32_t)sizeof(box_render_mesh_shading_t);
    box_render_sync(world);
    if (flat) {
        ecs_os_memcpy(out, box_draws_slot_shading(b, slot), (ecs_size_t)stride);
    } else {
        ecs_os_memcpy(out, box_draws_slot_mesh_shading(b, slot), (ecs_size_t)stride);
    }

    tr_slot_rendered(world, null, b, slot, BOX_RENDER_STREAM_SHADING, stride, out);
}

/** Get a channel of the unorm16x4 color of a 2D shading.
 *
 * @param shading The shading
 * @param c The channel, r, g, b, a
 * @return The channel from 0 to 1
 */
static float tr_channel(
    const box_render_shading_t *shading,
    int32_t c)
{
    float color[4];
    box_draws_color_unpack(color, shading->color);

    return color[c];
}

/** Check the 2D shading of a rect rendered in the last packet against the one of its material
 * with another value of a component.
 *
 * @param world The world
 * @param null The backend
 * @param e The rect
 * @param c The component, BOX_RENDER_TRANSITION_TINT or a material component
 * @param value The value it renders, NULL for none
 */
static void tr_check_shape(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_transition_component_t c,
    const void *value)
{
    box_render_shading_t rendered, expected;
    box_draw_material_t m;
    int32_t i;
    tr_shading_rendered(world, null, e, &rendered);
    box_draws_shape_material(world, e, ecs_get(world, e, DrawSlot), &m);
    if (c == BOX_RENDER_TRANSITION_TINT) {
        m.tint = value;
    } else {
        tr_material_set(&m, c, value);
    }

    box_draws_material(&expected, &m);
    for (i = 0; i < 4; i ++) {
        test_assert(fabsf(tr_channel(&rendered, i) - tr_channel(&expected, i)) < TR_FLOAT_EPS);
        test_assert(fabsf(rendered.emissive[i] - expected.emissive[i]) < TR_FLOAT_EPS);
        test_assert(fabsf(rendered.uv[i] - expected.uv[i]) < TR_FLOAT_EPS);
    }

    test_assert(!ecs_os_memcmp(rendered.ids, expected.ids, ECS_SIZEOF(rendered.ids)));
}

/** Check that the 2D shading of a rect is the one of its target material, byte for byte, in a
 * static batch.
 *
 * @param world The world
 * @param null The backend
 * @param e The rect
 */
static void tr_check_shape_target(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e)
{
    box_render_shading_t rendered, expected;
    box_draw_material_t m;
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    test_assert(!tr_batch(world, e)->key.dynamic_material);
    box_draws_shape_material(world, e, ecs_get(world, e, DrawSlot), &m);
    box_draws_material(&expected, &m);
    tr_shading_rendered(world, null, e, &rendered);
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
}

/** Read a record of the material table as the backend got it in the last packet.
 *
 * @param world The world
 * @param null The backend
 * @param slot The slot
 * @param out Receives the record
 * @return Whether the last packet uploaded the slot
 */
static bool tr_material_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    uint32_t slot,
    box_render_material_t *out)
{
    uint32_t size = (uint32_t)sizeof(box_render_material_t);
    uint32_t at = slot * size;
    bool result = false;
    int32_t i;
    box_render_sync(world);
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == BOX_RENDER_BUFFER_MATERIALS && at >= u->offset &&
            at + size <= u->offset + u->size)
        {
            ecs_os_memcpy(out, box_render_packet_data(&null->last, u->data + at - u->offset),
                (ecs_size_t)size);
            result = true;
        }
    }

    return result;
}

/** Check the record of the material table of a mesh in the last packet against the one of its
 * material with another value of a component.
 *
 * @param world The world
 * @param null The backend
 * @param e The mesh
 * @param c The material component
 * @param value The value it renders
 */
static void tr_check_record(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_transition_component_t c,
    const void *value)
{
    box_render_material_t rendered, expected;
    box_draw_material_t m;
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    test_assert(slot != 0 && slot != UINT32_MAX);
    box_draws_material_of(world, e, &m);
    tr_material_set(&m, c, value);
    box_draws_material_pack(&expected, &m);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_material_t));
}

/** Check that the record of the material table of a mesh is the one of its target material,
 * byte for byte, in the table and in the last packet when it uploaded it.
 *
 * @param world The world
 * @param null The backend
 * @param e The mesh
 */
static void tr_check_record_target(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e)
{
    box_render_material_t rendered, expected;
    box_draw_material_t m;
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    const box_draw_materials_t *table = &box_draws_state(world)->materials;
    box_draws_material_of(world, e, &m);
    box_draws_material_pack(&expected, &m);
    if (tr_material_rendered(world, null, slot, &rendered)) {
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_material_t));
    }

    test_assert(!ecs_os_memcmp_t(&expected, ecs_vec_get_t(&table->records,
        box_render_material_t, (int32_t)slot), box_render_material_t));
}

/** Add or remove a material component of a mesh with on_add or on_remove: the record of its
 * slot of the material table at every frame of the transition and at the end.
 *
 * @param c The component
 * @param value The value added, or removed
 * @param remove Whether the case removes the component, else adds it
 * @param bare Whether the mesh has no material component before the add, so the add also adds
 *        its MaterialId
 */
static void tr_test_mesh(
    box_render_transition_component_t c,
    const void *value,
    bool remove,
    bool bare)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t id = tr_id(c);
    ecs_size_t size = box_render_transition_layout(c)->size;
    ecs_entity_t e = ecs_new(world);
    float target[BOX_RENDER_TRANSITION_VALUE / 4], neutral[BOX_RENDER_TRANSITION_VALUE / 4];
    float mid[BOX_RENDER_TRANSITION_VALUE / 4];
    int32_t i;
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 2, 3});
    if (!bare) {
        ecs_set(world, e, Rgb, {200, 100, 50});
    }

    box_transition_set(world, e, id, remove ? &tr_remove : &tr_add);
    if (remove) {
        ecs_set_id(world, e, id, (size_t)size, value);
    }

    tr_frame(world);
    tr_frame(world);
    test_bool(ecs_has(world, e, MaterialId), !bare || remove);
    box_render_transition_pack(c, value, target);
    test_assert(box_render_transition_neutral(c, target, neutral));

    if (remove) {
        ecs_remove_id(world, e, id);
    } else {
        ecs_set_id(world, e, id, (size_t)size, value);
    }

    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_bool(ecs_has_id(world, e, id), !remove);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        tr_at(c, remove ? target : neutral, remove ? neutral : target, i, mid);
        tr_check_record(world, null, e, c, mid);
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_START), 0);
    tr_check_record_target(world, null, e);

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    tr_check_record_target(world, null, e);
    tr_fini(world, null);
}

/** Add or remove a material component of a 2D rect with on_add or on_remove: its shading at
 * every frame of the transition and at the end.
 *
 * @param c The component
 * @param value The value added, or removed
 * @param remove Whether the case removes the component, else adds it
 */
static void tr_test_shape(
    box_render_transition_component_t c,
    const void *value,
    bool remove)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t id = tr_id(c);
    ecs_size_t size = box_render_transition_layout(c)->size;
    ecs_entity_t e = tr_rect(world, 0);
    float target[BOX_RENDER_TRANSITION_VALUE / 4], neutral[BOX_RENDER_TRANSITION_VALUE / 4];
    float mid[BOX_RENDER_TRANSITION_VALUE / 4];
    int32_t i;
    box_transition_set(world, e, id, remove ? &tr_remove : &tr_add);
    if (remove) {
        ecs_set_id(world, e, id, (size_t)size, value);
    }

    tr_frame(world);
    tr_frame(world);
    box_render_transition_pack(c, value, target);
    test_assert(box_render_transition_neutral(c, target, neutral));

    if (remove) {
        ecs_remove_id(world, e, id);
    } else {
        ecs_set_id(world, e, id, (size_t)size, value);
    }

    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_bool(ecs_has_id(world, e, id), !remove);
        test_assert(ecs_has_id(world, e, TransitionMaterial));
        test_assert(tr_batch(world, e)->key.dynamic_material);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 1);
        tr_at(c, remove ? target : neutral, remove ? neutral : target, i, mid);
        tr_check_shape(world, null, e, c, mid);
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    tr_check_shape_target(world, null, e);
    tr_fini(world, null);
}

/** The Emissive the cases add and remove */
static const Emissive tr_emissive = { .strength = 4, .color = {200, 100, 40} };

void TransitionAddRemove_tint_add(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    const Tint neutral = {0, 0, 255, 0};
    int32_t i;
    box_transition_set(world, e, ecs_id(Tint), &tr_add);
    tr_frames(world, 2);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);

    ecs_set_ptr(world, e, Tint, &tr_tint);
    for (i = 0; i < 4; i ++) {
        Tint mid = tr_tint_at(neutral, tr_tint, i);
        tr_frame(world);
        test_int(ecs_get(world, e, Tint)->a, 200);
        test_assert(ecs_has_id(world, e, TransitionMaterial));
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 1);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 1);
        tr_check_shape(world, null, e, BOX_RENDER_TRANSITION_TINT, &mid);
        if (!i) {
            tr_check_shape(world, null, e, BOX_RENDER_TRANSITION_TINT, NULL);
        }

        if (i == 2) {
            test_int(mid.a, 100);
            test_int(mid.b, 255);
        }
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    tr_check_shape_target(world, null, e);
    tr_fini(world, null);
}

void TransitionAddRemove_tint_remove(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    const Tint neutral = {0, 0, 255, 0};
    box_render_shading_t rendered, expected;
    box_draw_material_t m;
    float alpha = 256.0f;
    int32_t i;
    ecs_set_ptr(world, e, Tint, &tr_tint);
    box_transition_set(world, e, ecs_id(Tint), &tr_remove);
    tr_frames(world, 2);

    ecs_remove(world, e, Tint);
    for (i = 0; i < 4; i ++) {
        Tint mid = tr_tint_at(tr_tint, neutral, i);
        tr_frame(world);
        test_assert(ecs_get(world, e, Tint) == NULL);
        test_assert(ecs_has_id(world, e, TransitionMaterial));
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 1);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 1);
        tr_check_shape(world, null, e, BOX_RENDER_TRANSITION_TINT, &mid);
        test_assert((float)mid.a < alpha);
        alpha = (float)mid.a;
    }

    tr_frame(world);
    test_assert(ecs_get(world, e, Tint) == NULL);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_START), 0);
    tr_check_shape_target(world, null, e);

    tr_frame(world);
    test_int(ecs_vec_count(&null->last.transitions), 0);
    box_draws_shape_material(world, e, ecs_get(world, e, DrawSlot), &m);
    test_assert(m.tint == NULL);
    box_draws_material(&expected, &m);
    tr_shading_rendered(world, null, e, &rendered);
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
    tr_fini(world, null);
}

void TransitionAddRemove_tint_add_snaps(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    box_transition_set(world, e, ecs_id(Tint), &(TransitionSpec){ .duration = 1.0f });
    tr_frames(world, 2);

    ecs_set_ptr(world, e, Tint, &tr_tint);
    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    tr_check_shape_target(world, null, e);
    tr_fini(world, null);
}

void TransitionAddRemove_tint_remove_snaps(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    ecs_set_ptr(world, e, Tint, &tr_tint);
    box_transition_set(world, e, ecs_id(Tint), &(TransitionSpec){ .duration = 1.0f });
    tr_frames(world, 2);

    ecs_remove(world, e, Tint);
    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    tr_check_shape_target(world, null, e);
    tr_fini(world, null);
}

void TransitionAddRemove_emissive_add_2d(void) {
    // engine_core/transition
    tr_test_shape(BOX_RENDER_TRANSITION_EMISSIVE, &tr_emissive, false);
}

void TransitionAddRemove_emissive_remove_2d(void) {
    // engine_core/transition
    tr_test_shape(BOX_RENDER_TRANSITION_EMISSIVE, &tr_emissive, true);
}

void TransitionAddRemove_emissive_add_mesh(void) {
    // engine_core/transition
    tr_test_mesh(BOX_RENDER_TRANSITION_EMISSIVE, &tr_emissive, false, false);
}

void TransitionAddRemove_emissive_remove_mesh(void) {
    // engine_core/transition
    tr_test_mesh(BOX_RENDER_TRANSITION_EMISSIVE, &tr_emissive, true, false);
}

void TransitionAddRemove_emissive_add_mesh_new_material(void) {
    // engine_core/transition
    tr_test_mesh(BOX_RENDER_TRANSITION_EMISSIVE, &tr_emissive, false, true);
}

void TransitionAddRemove_opacity_add_mesh(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_mesh_shading_t rendered;
    int32_t i;
    box_transition_set(world, e, ecs_id(Opacity), &tr_add);
    tr_frames(world, 2);
    test_int(tr_batch(world, e)->key.blend, Opaque);

    ecs_set(world, e, Opacity, {0.2f});
    for (i = 0; i < 4; i ++) {
        float mid = 1.0f + (0.2f - 1.0f) * (float)i / 4.0f;
        tr_frame(world);
        test_flt(ecs_get(world, e, Opacity)->value, 0.2f);
        test_assert(ecs_has_id(world, e, TransitionOpacity));
        test_assert(tr_batch(world, e)->key.dynamic_material);
        test_int(tr_batch(world, e)->key.blend, AlphaBlend);
        tr_shading_rendered(world, null, e, &rendered);
        test_assert(fabsf(rendered.alpha - mid) < TR_FLOAT_EPS);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionOpacity));
    test_int(tr_batch(world, e)->key.blend, AlphaBlend);
    tr_shading_rendered(world, null, e, &rendered);
    test_assert(fabsf(rendered.alpha - 0.2f) < TR_FLOAT_EPS);
    tr_fini(world, null);
}

void TransitionAddRemove_opacity_remove_mesh(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_mesh_shading_t rendered;
    int32_t i;
    ecs_set(world, e, Opacity, {0.0f});
    box_transition_set(world, e, ecs_id(Opacity), &tr_remove);
    tr_frames(world, 2);
    test_int(tr_batch(world, e)->key.blend, AlphaBlend);

    ecs_remove(world, e, Opacity);
    for (i = 0; i < 4; i ++) {
        float mid = (float)i / 4.0f;
        tr_frame(world);
        test_assert(!ecs_has(world, e, Opacity));
        test_assert(ecs_has_id(world, e, TransitionOpacity));
        test_int(tr_batch(world, e)->key.blend, AlphaBlend);
        test_assert(!box_draws_caster(tr_batch(world, e)));
        tr_shading_rendered(world, null, e, &rendered);
        test_assert(fabsf(rendered.alpha - mid) < TR_FLOAT_EPS);
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionOpacity));
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    test_int(tr_batch(world, e)->key.blend, Opaque);
    test_assert(box_draws_caster(tr_batch(world, e)));
    tr_shading_rendered(world, null, e, &rendered);
    test_assert(fabsf(rendered.alpha - 1.0f) < TR_FLOAT_EPS);
    tr_fini(world, null);
}

void TransitionAddRemove_scale_remove_child(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Scale, {2, 2, 2});
    ecs_entity_t child = tr_shape(world, parent, 0);
    ecs_set(world, child, Position, {0, 1, 0});
    box_transition_set(world, parent, ecs_id(Scale), &tr_remove);
    tr_frames(world, 2);

    ecs_remove(world, parent, Scale);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        float s = 2.0f - (float)i / 4.0f;
        box_render_transform_t rendered, expected;
        WorldTransform wt;
        mat4 up, local;
        int32_t r, c;
        tr_frame(world);
        test_assert(!ecs_has(world, parent, Scale));
        test_assert(ecs_has_id(world, child, TransitionTransform));
        box_transform_local(&(Position){0, 0, 0}, NULL, &(Scale){s, s, s}, up);
        box_transform_local(&(Position){0, 1, 0}, NULL, NULL, local);
        glm_mul(up, local, wt.m);
        box_draws_transform(&expected, &wt, ecs_get(world, child, ShapeMesh));
        test_assert(tr_rendered(world, null, child, &rendered));
        for (r = 0; r < 3; r ++) {
            for (c = 0; c < 4; c ++) {
                test_assert(fabsf(rendered.rows[r][c] - expected.rows[r][c]) <
                    TR_TRANSFORM_EPS);
            }
        }
    }

    tr_frame(world);
    tr_test_static(world, null, child);
    tr_fini(world, null);
}

void TransitionAddRemove_clear_coat_add(void) {
    // engine_core/transition
    const ClearCoat value = { .strength = 1, .roughness = 0.5f };
    tr_test_mesh(BOX_RENDER_TRANSITION_CLEAR_COAT, &value, false, false);
}

void TransitionAddRemove_sheen_remove(void) {
    // engine_core/transition
    const Sheen value = { .color = {200, 100, 40}, .roughness = 1 };
    tr_test_mesh(BOX_RENDER_TRANSITION_SHEEN, &value, true, false);
}

void TransitionAddRemove_anisotropy_add(void) {
    // engine_core/transition
    const SpecularAnisotropy value = { .strength = 1, .rotation = 1 };
    tr_test_mesh(BOX_RENDER_TRANSITION_ANISOTROPY, &value, false, false);
}

void TransitionAddRemove_diffuse_transmission_remove(void) {
    // engine_core/transition
    const DiffuseTransmission value = {
        .factor = 1, .color = {200, 100, 40}, .thickness = 2, .blur = 0.5f };
    tr_test_mesh(BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION, &value, true, false);
}

void TransitionAddRemove_pbr_add(void) {
    // engine_core/transition
    const PbrMaterial value = { .metallic = 1, .roughness = 0 };
    tr_test_mesh(BOX_RENDER_TRANSITION_PBR_MATERIAL, &value, false, false);
}

void TransitionAddRemove_uv_remove(void) {
    // engine_core/transition
    const TextureTransform value = {2, 3, 1, -1};
    tr_test_mesh(BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM, &value, true, false);
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

    return bs && bs->batch >= 0 ? box_draws_batch(d, bs->batch) : NULL;
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

/** Render a world with a shadowed rect set up without a transition and read its shadow.
 *
 * @param shadow The shadow
 * @param transform Receives the transform of the shadow
 * @param shading Receives the shading of the shadow
 */
static void tr_shadow_reference(
    const BoxShadow *shadow,
    box_render_transform_t *transform,
    box_render_shading_t *shading)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    ecs_set_ptr(world, e, BoxShadow, shadow);
    tr_frames(world, 2);
    tr_shadow_rendered(world, null, e, transform, shading);
    tr_fini(world, null);
}

void TransitionAddRemove_box_shadow_add(void) {
    // ui/box_shadow
    const BoxShadow to = {
        .offset_x = 5, .offset_y = -9, .blur = 10, .spread = 2, .color = {0, 0, 255, 200}};
    const BoxShadow mid = {
        .offset_x = 5, .offset_y = -9, .blur = 10, .spread = 2, .color = {0, 0, 255, 100}};
    box_render_transform_t transform, expected_transform;
    box_render_shading_t shading, expected_shading, first;
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    float color[4];
    int32_t i, r, c;
    box_transition_set(world, e, ecs_id(BoxShadow), &tr_add);
    tr_frames(world, 2);

    ecs_set_ptr(world, e, BoxShadow, &to);
    tr_frame(world);
    test_assert(ecs_has_id(world, e, TransitionBoxShadow));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADOW_BOX), 1);
    tr_shadow_rendered(world, null, e, &transform, &first);
    box_draws_color_unpack(color, first.color);
    test_assert(fabsf(color[3]) <= TR_SHADOW_COLOR_EPS);

    tr_frames(world, 2);
    tr_shadow_rendered(world, null, e, &transform, &shading);
    tr_fini(world, null);

    tr_shadow_reference(&mid, &expected_transform, &expected_shading);
    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 4; c ++) {
            test_assert(fabsf(transform.rows[r][c] - expected_transform.rows[r][c]) <
                TR_TRANSFORM_EPS);
        }
    }

    for (i = 0; i < 4; i ++) {
        float a[4], b[4];
        box_draws_color_unpack(a, shading.color);
        box_draws_color_unpack(b, expected_shading.color);
        test_assert(fabsf(a[i] - b[i]) <= TR_SHADOW_COLOR_EPS);
        test_assert(fabsf(shading.material[i] - expected_shading.material[i]) <
            TR_TRANSFORM_EPS);
    }

    world = tr_scene(&null);
    e = tr_rect(world, 0);
    box_transition_set(world, e, ecs_id(BoxShadow), &tr_add);
    tr_frames(world, 2);
    ecs_set_ptr(world, e, BoxShadow, &to);
    tr_frames(world, 5);
    test_assert(!ecs_has_id(world, e, TransitionBoxShadow));
    tr_shadow_rendered(world, null, e, &transform, &shading);
    tr_fini(world, null);

    tr_shadow_reference(&to, &expected_transform, &expected_shading);
    test_assert(!ecs_os_memcmp_t(&transform, &expected_transform, box_render_transform_t));
    test_assert(!ecs_os_memcmp_t(&shading, &expected_shading, box_render_shading_t));
}

void TransitionAddRemove_not_add_spawn(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    ecs_set_ptr(world, e, Tint, &tr_tint);
    box_transition_set(world, e, ecs_id(Tint), &tr_both);
    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_CREATE), 1);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    tr_check_shape_target(world, null, e);
    tr_fini(world, null);
}

void TransitionAddRemove_not_add_transition_added(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    ecs_set_ptr(world, e, Tint, &tr_tint);
    tr_frames(world, 2);

    box_transition_set(world, e, ecs_id(Tint), &tr_both);
    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_CREATE), 1);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    tr_check_shape_target(world, null, e);
    tr_fini(world, null);
}

void TransitionAddRemove_not_add_override_inherited(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set_ptr(world, prefab, Emissive, &tr_emissive);
    box_transition_set(world, prefab, ecs_id(Emissive), &tr_both);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst, Position, {0, 0, 0});
    ecs_set(world, inst, Rect, {10, 20});
    tr_frames(world, 2);
    test_assert(!ecs_owns(world, inst, Emissive));

    ecs_set(world, inst, Emissive, {2, {10, 20, 30}});
    tr_frame(world);
    test_assert(ecs_owns(world, inst, Emissive));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    tr_check_shape_target(world, null, inst);
    tr_fini(world, null);
}

void TransitionAddRemove_not_add_reenable(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    box_transition_set(world, e, ecs_id(Tint), &tr_both);
    tr_frames(world, 2);

    ecs_enable(world, e, false);
    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_DESTROY), 1);
    ecs_set_ptr(world, e, Tint, &tr_tint);
    ecs_enable(world, e, true);
    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_CREATE), 1);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    tr_check_shape_target(world, null, e);
    tr_fini(world, null);
}

void TransitionAddRemove_remove_inherited_snaps(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Emissive, {1, {255, 0, 0}});
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst, Position, {0, 0, 0});
    ecs_set(world, inst, Rect, {10, 20});
    ecs_set_ptr(world, inst, Emissive, &tr_emissive);
    box_transition_set(world, inst, ecs_id(Emissive), &tr_both);
    tr_frames(world, 2);

    ecs_remove(world, inst, Emissive);
    tr_frame(world);
    test_assert(!ecs_owns(world, inst, Emissive));
    test_flt(ecs_get(world, inst, Emissive)->strength, 1.0f);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    tr_check_shape_target(world, null, inst);

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    tr_check_shape_target(world, null, inst);
    tr_fini(world, null);
}

void TransitionAddRemove_prefab_remove_emissive(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    float target[BOX_RENDER_TRANSITION_VALUE / 4], neutral[BOX_RENDER_TRANSITION_VALUE / 4];
    float mid[BOX_RENDER_TRANSITION_VALUE / 4];
    int32_t i, k;
    ecs_set_ptr(world, prefab, Emissive, &tr_emissive);
    box_transition_set(world, prefab, ecs_id(Emissive), &tr_remove);
    ecs_entity_t insts[2];
    for (k = 0; k < 2; k ++) {
        insts[k] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, insts[k], Position, {(float)k * 20, 0, 0});
        ecs_set(world, insts[k], Rect, {10, 20});
    }

    tr_frames(world, 2);
    box_render_transition_pack(BOX_RENDER_TRANSITION_EMISSIVE, &tr_emissive, target);
    box_render_transition_neutral(BOX_RENDER_TRANSITION_EMISSIVE, target, neutral);

    ecs_remove(world, prefab, Emissive);
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        tr_at(BOX_RENDER_TRANSITION_EMISSIVE, target, neutral, i, mid);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 2);
        for (k = 0; k < 2; k ++) {
            test_assert(ecs_get(world, insts[k], Emissive) == NULL);
            test_assert(ecs_has_id(world, insts[k], TransitionMaterial));
            tr_check_shape(world, null, insts[k], BOX_RENDER_TRANSITION_EMISSIVE, mid);
        }
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    for (k = 0; k < 2; k ++) {
        tr_check_shape_target(world, null, insts[k]);
    }

    tr_fini(world, null);
}

/** A remove three frames into an add starts from what the add renders at that time, 3/4 of
 * the way, so no frame jumps. */
void TransitionAddRemove_remove_during_add(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    const Tint neutral = {0, 0, 255, 0};
    int32_t i;
    box_transition_set(world, e, ecs_id(Tint), &tr_both);
    tr_frames(world, 2);

    ecs_set_ptr(world, e, Tint, &tr_tint);
    for (i = 0; i < 3; i ++) {
        Tint mid = tr_tint_at(neutral, tr_tint, i);
        tr_frame(world);
        tr_check_shape(world, null, e, BOX_RENDER_TRANSITION_TINT, &mid);
    }

    Tint from = tr_tint_at(neutral, tr_tint, 3);
    ecs_remove(world, e, Tint);
    for (i = 0; i < 4; i ++) {
        Tint mid = tr_tint_at(from, neutral, i);
        tr_frame(world);
        test_assert(!ecs_has(world, e, Tint));
        tr_check_shape(world, null, e, BOX_RENDER_TRANSITION_TINT, &mid);
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    tr_check_shape_target(world, null, e);
    tr_fini(world, null);
}

/** An add three frames into a remove starts from what the remove renders at that time, 3/4 of
 * the way, so no frame jumps. */
void TransitionAddRemove_add_during_remove(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    const Tint neutral = {0, 0, 255, 0};
    int32_t i;
    ecs_set_ptr(world, e, Tint, &tr_tint);
    box_transition_set(world, e, ecs_id(Tint), &tr_both);
    tr_frames(world, 2);

    ecs_remove(world, e, Tint);
    for (i = 0; i < 3; i ++) {
        Tint mid = tr_tint_at(tr_tint, neutral, i);
        tr_frame(world);
        tr_check_shape(world, null, e, BOX_RENDER_TRANSITION_TINT, &mid);
    }

    Tint from = tr_tint_at(tr_tint, neutral, 3);
    ecs_set_ptr(world, e, Tint, &tr_tint);
    for (i = 0; i < 4; i ++) {
        Tint mid = tr_tint_at(from, tr_tint, i);
        tr_frame(world);
        test_assert(ecs_has(world, e, Tint));
        tr_check_shape(world, null, e, BOX_RENDER_TRANSITION_TINT, &mid);
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    tr_check_shape_target(world, null, e);
    tr_fini(world, null);
}

void TransitionAddRemove_delete_during_remove(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    ecs_entity_t other = tr_rect(world, 0);
    ecs_set_ptr(world, e, Tint, &tr_tint);
    box_transition_set(world, e, ecs_id(Tint), &tr_both);
    tr_frames(world, 2);

    ecs_remove(world, e, Tint);
    tr_frames(world, 2);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 1);

    ecs_delete(world, e);
    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_DESTROY), 1);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_FADE), 0);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    tr_check_shape_target(world, null, other);

    tr_frames(world, 4);
    test_int(ecs_vec_count(&null->last.transitions), 0);
    tr_check_shape_target(world, null, other);
    tr_fini(world, null);
}

void TransitionAddRemove_no_neutral_logs(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_rect(world, 0);
    const TransitionSpec add = { .duration = 1.0f, .on_add = true };
    int32_t i;
    tr_capture_begin();
    box_transition_set(world, e, ecs_id(Position), &add);
    char *errors = tr_capture_end();
    test_int(tr_error_count, 1);
    test_assert(strstr(errors, "Position") != NULL);
    ecs_os_free(errors);

    tr_capture_begin();
    box_transition_set(world, e, ecs_id(Rgb), &add);
    errors = tr_capture_end();
    test_assert(errors != NULL);
    test_assert(strstr(errors, "Rgb") != NULL);
    ecs_os_free(errors);

    tr_capture_begin();
    box_transition_set(world, e, ecs_id(AlphaCutoff), &add);
    errors = tr_capture_end();
    test_assert(errors != NULL);
    test_assert(strstr(errors, "AlphaCutoff") != NULL);
    ecs_os_free(errors);

    tr_capture_begin();
    box_transition_set(world, e, ecs_id(Transmission), &add);
    errors = tr_capture_end();
    test_assert(errors != NULL);
    test_assert(strstr(errors, "Transmission") != NULL);
    ecs_os_free(errors);

    ecs_remove(world, e, Transition);
    tr_capture_begin();
    box_transition_set(world, e, ecs_id(BoxShadow),
        &(TransitionSpec){ .duration = 1.0f, .on_remove = true });
    errors = tr_capture_end();
    test_int(tr_error_count, 1);
    test_assert(strstr(errors, "BoxShadow") != NULL);
    test_assert(strstr(errors, "on_remove") != NULL);
    ecs_os_free(errors);

    tr_capture_begin();
    box_transition_set(world, e, ecs_id(Rgb), &add);
    errors = tr_capture_end();
    ecs_os_free(errors);
    tr_frames(world, 2);

    ecs_set(world, e, Rgb, {0, 0, 0});
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_assert(ecs_has_id(world, e, TransitionMaterial));
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 1);
    }

    tr_frame(world);
    tr_check_shape_target(world, null, e);
    tr_fini(world, null);
}

void TransitionAddRemove_neutral_values(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    float value[BOX_RENDER_TRANSITION_VALUE / 4], out[BOX_RENDER_TRANSITION_VALUE / 4];

    Tint tint = {10, 20, 30, 200}, tint_n;
    box_render_transition_pack(BOX_RENDER_TRANSITION_TINT, &tint, value);
    test_assert(box_render_transition_neutral(BOX_RENDER_TRANSITION_TINT, value, out));
    ecs_os_memcpy(&tint_n, out, ECS_SIZEOF(Tint));
    test_int(tint_n.r, 10);
    test_int(tint_n.g, 20);
    test_int(tint_n.b, 30);
    test_int(tint_n.a, 0);

    Scale scale = {2, 3, 4}, scale_n;
    box_render_transition_pack(BOX_RENDER_TRANSITION_SCALE, &scale, value);
    test_assert(box_render_transition_neutral(BOX_RENDER_TRANSITION_SCALE, value, out));
    ecs_os_memcpy(&scale_n, out, ECS_SIZEOF(Scale));
    test_flt(scale_n.x, 1);
    test_flt(scale_n.y, 1);
    test_flt(scale_n.z, 1);

    Opacity opacity = {0.25f}, opacity_n;
    box_render_transition_pack(BOX_RENDER_TRANSITION_OPACITY, &opacity, value);
    test_assert(box_render_transition_neutral(BOX_RENDER_TRANSITION_OPACITY, value, out));
    ecs_os_memcpy(&opacity_n, out, ECS_SIZEOF(Opacity));
    test_flt(opacity_n.value, 1);

    PbrMaterial pbr = {1, 0}, pbr_n;
    box_render_transition_pack(BOX_RENDER_TRANSITION_PBR_MATERIAL, &pbr, value);
    test_assert(box_render_transition_neutral(BOX_RENDER_TRANSITION_PBR_MATERIAL, value, out));
    ecs_os_memcpy(&pbr_n, out, ECS_SIZEOF(PbrMaterial));
    test_flt(pbr_n.metallic, BOX_MATERIAL_METALLIC);
    test_flt(pbr_n.roughness, BOX_MATERIAL_ROUGHNESS);

    Sheen sheen = {{1, 2, 3}, 0.75f}, sheen_n;
    box_render_transition_pack(BOX_RENDER_TRANSITION_SHEEN, &sheen, value);
    test_assert(box_render_transition_neutral(BOX_RENDER_TRANSITION_SHEEN, value, out));
    ecs_os_memcpy(&sheen_n, out, ECS_SIZEOF(Sheen));
    test_int(sheen_n.color.r, 0);
    test_int(sheen_n.color.g, 0);
    test_int(sheen_n.color.b, 0);
    test_flt(sheen_n.roughness, 0.75f);

    BoxShadow shadow = {1, 2, 3, 4, {5, 6, 7, 8}}, shadow_n;
    box_render_transition_pack(BOX_RENDER_TRANSITION_BOX_SHADOW, &shadow, value);
    test_assert(box_render_transition_neutral(BOX_RENDER_TRANSITION_BOX_SHADOW, value, out));
    ecs_os_memcpy(&shadow_n, out, ECS_SIZEOF(BoxShadow));
    test_flt(shadow_n.offset_x, 1);
    test_flt(shadow_n.spread, 4);
    test_int(shadow_n.color.b, 7);
    test_int(shadow_n.color.a, 0);

    Position position = {1, 2, 3};
    box_render_transition_pack(BOX_RENDER_TRANSITION_POSITION, &position, value);
    test_assert(!box_render_transition_neutral(BOX_RENDER_TRANSITION_POSITION, value, out));
    test_assert(!box_render_transition_neutral(BOX_RENDER_TRANSITION_RGB, value, out));
    test_assert(!box_render_transition_neutral(BOX_RENDER_TRANSITION_ALPHA_CUTOFF, value, out));
    test_assert(!box_render_transition_neutral(BOX_RENDER_TRANSITION_TRANSMISSION, value, out));

    ecs_fini(world);
}
