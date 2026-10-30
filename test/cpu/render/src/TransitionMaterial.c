/**
 * @file TransitionMaterial.c
 * @brief Tests of the TransitionMaterial suite: material transitions: the records of the material
 * table and the 2D shading the resolver patches, read from the last packet of the null backend.
 *
 * The cases step the world with a fixed delta and a Linear transition of one second, so the
 * four frames after a change render at progress 0, 1/4, 1/2 and 3/4 and the fifth at the target.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"
#include "transition_test.h"

/** A material component, the value a case starts from and the one it transitions to */
typedef struct tr_material_case_t {
    ecs_entity_t component;                 /**< Component id */
    box_render_transition_component_t c;    /**< Transitionable component */
    ecs_size_t size;                        /**< Size of the component */
    const void *from;                       /**< First value, set without a transition */
    const void *to;                         /**< Value the case transitions to */
} tr_material_case_t;

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
    case BOX_RENDER_TRANSITION_RGB: m->color = value; break;
    case BOX_RENDER_TRANSITION_ALPHA_CUTOFF: m->cutoff = value; break;
    case BOX_RENDER_TRANSITION_PBR_MATERIAL: m->pbr = value; break;
    case BOX_RENDER_TRANSITION_EMISSIVE: m->emissive = value; break;
    case BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM: m->uv = value; break;
    case BOX_RENDER_TRANSITION_TRANSMISSION: m->transmission = value; break;
    default: test_assert(false); break;
    }
}

/** Compute the value of a case at a progress, as the resolver does with Linear timing.
 *
 * @param mc The case
 * @param t Progress
 * @param out Receives the value, BOX_RENDER_TRANSITION_VALUE bytes
 */
static void tr_material_mix(
    const tr_material_case_t *mc,
    float t,
    float *out)
{
    float from[BOX_RENDER_TRANSITION_VALUE / 4], to[BOX_RENDER_TRANSITION_VALUE / 4];
    box_render_transition_pack(mc->c, mc->from, from);
    box_render_transition_pack(mc->c, mc->to, to);
    box_render_transition_mix(mc->c, from, to, t, out);
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

/** Read a shading slot of a 2D batch as the backend got it in the last packet: the patched
 * bytes of the run upload of a dynamic_material batch, else the static slot.
 *
 * @param world The world
 * @param null The backend
 * @param b The batch
 * @param slot The slot
 * @param out Receives the shading
 */
static void tr_shading_slot(
    ecs_world_t *world,
    const box_render_null_t *null,
    box_draw_batch_t *b,
    int32_t slot,
    box_render_shading_t *out)
{
    box_render_sync(world);
    *out = *box_draws_slot_shading(b, slot);
    tr_slot_rendered(world, null, b, slot, BOX_RENDER_STREAM_SHADING,
        (uint32_t)sizeof(box_render_shading_t), out);
}

/** Read the shading of the 2D shape of an entity as the backend got it in the last packet.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param out Receives the shading
 */
static void tr_shading_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_shading_t *out)
{
    tr_shading_slot(world, null, tr_batch(world, e), ecs_get(world, e, DrawSlot)->slot, out);
}

/** Get the packed color word of the material table.
 *
 * @param r Red
 * @param g Green
 * @param b Blue
 * @return The word
 */
static uint32_t tr_color_word(
    uint32_t r,
    uint32_t g,
    uint32_t b)
{
    return r | (g << 8) | (b << 16);
}

/** The Rgb case: every channel steps by an integer each quarter */
static const Rgb tr_rgb_from = {0, 0, 0}, tr_rgb_to = {200, 100, 40};

/** The Emissive case */
static const Emissive tr_emissive_from = { .strength = 0, .color = {0, 0, 0} };
static const Emissive tr_emissive_to = { .strength = 4, .color = {200, 100, 40} };

/** The PbrMaterial case */
static const PbrMaterial tr_pbr_from = {0, 1}, tr_pbr_to = {1, 0};

/** The TextureTransform case */
static const TextureTransform tr_uv_from = {1, 1, 0, 0}, tr_uv_to = {2, 3, 1, -1};

/** The AlphaCutoff case */
static const AlphaCutoff tr_cutoff_from = {0.25f}, tr_cutoff_to = {0.75f};

/** The Transmission case: every float and color channel steps by an exact amount each quarter */
static const Transmission tr_transmission_from = {
    .transmission_factor = 0, .ior = 1, .thickness_factor = 0, .attenuation_distance = 1,
    .attenuation_color = {0, 0, 0}
};
static const Transmission tr_transmission_to = {
    .transmission_factor = 1, .ior = 2, .thickness_factor = 4, .attenuation_distance = 5,
    .attenuation_color = {200, 100, 40}
};

/** Get the case of a component, once the world registered the components.
 *
 * @param c The component
 * @return The case
 */
static tr_material_case_t tr_case(
    box_render_transition_component_t c)
{
    tr_material_case_t result = { .c = c };
    switch (c) {
    case BOX_RENDER_TRANSITION_RGB:
        result.component = ecs_id(Rgb);
        result.size = ECS_SIZEOF(Rgb);
        result.from = &tr_rgb_from;
        result.to = &tr_rgb_to;
        break;
    case BOX_RENDER_TRANSITION_EMISSIVE:
        result.component = ecs_id(Emissive);
        result.size = ECS_SIZEOF(Emissive);
        result.from = &tr_emissive_from;
        result.to = &tr_emissive_to;
        break;
    case BOX_RENDER_TRANSITION_PBR_MATERIAL:
        result.component = ecs_id(PbrMaterial);
        result.size = ECS_SIZEOF(PbrMaterial);
        result.from = &tr_pbr_from;
        result.to = &tr_pbr_to;
        break;
    case BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM:
        result.component = ecs_id(TextureTransform);
        result.size = ECS_SIZEOF(TextureTransform);
        result.from = &tr_uv_from;
        result.to = &tr_uv_to;
        break;
    case BOX_RENDER_TRANSITION_ALPHA_CUTOFF:
        result.component = ecs_id(AlphaCutoff);
        result.size = ECS_SIZEOF(AlphaCutoff);
        result.from = &tr_cutoff_from;
        result.to = &tr_cutoff_to;
        break;
    case BOX_RENDER_TRANSITION_TRANSMISSION:
        result.component = ecs_id(Transmission);
        result.size = ECS_SIZEOF(Transmission);
        result.from = &tr_transmission_from;
        result.to = &tr_transmission_to;
        break;
    default:
        test_assert(false);
        break;
    }

    return result;
}

/** Run a material transition of a mesh shape: the record of its slot of the material table
 * at the start, midpoint and end, the batch it keeps and the target on the game thread.
 *
 * @param c The component
 */
static void tr_test_mesh_material(
    box_render_transition_component_t c)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    tr_material_case_t case_ = tr_case(c), *mc = &case_;
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered, expected;
    box_draw_material_t m;
    float mid[BOX_RENDER_TRANSITION_VALUE / 4];
    int32_t i;
    ecs_set_id(world, e, mc->component, (size_t)mc->size, mc->from);
    box_transition_set(world, e, mc->component, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    bool cutout = tr_batch(world, e)->key.cutout;
    test_assert(slot != 0 && slot != UINT32_MAX);

    ecs_set_id(world, e, mc->component, (size_t)mc->size, mc->to);
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_assert(!ecs_os_memcmp(ecs_get_id(world, e, mc->component), mc->to, mc->size));
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
        test_assert(!ecs_has_id(world, e, TransitionMaterial));
        test_int(ecs_get(world, e, DrawSlot)->batch, batch);
        test_bool(tr_batch(world, e)->key.cutout, cutout);
        tr_material_mix(mc, (float)i / 4.0f, mid);
        box_draws_material_of(world, e, &m);
        tr_material_set(&m, mc->c, mid);
        box_draws_material_pack(&expected, &m);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_material_t));
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    box_draws_material_of(world, e, &m);
    box_draws_material_pack(&expected, &m);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_material_t));
    const box_draw_materials_t *table = &box_draws_state(world)->materials;
    test_assert(!ecs_os_memcmp_t(&rendered, ecs_vec_get_t(&table->records,
        box_render_material_t, (int32_t)slot), box_render_material_t));

    tr_frame(world);
    test_assert(!tr_material_rendered(world, null, slot, &rendered));
    tr_fini(world, null);
}

/** Run a material transition of a 2D shape: its shading at the start, midpoint and end. The
 * components a 2D shape does not render (PbrMaterial, AlphaCutoff) leave it static.
 *
 * @param c The component
 * @param patched Whether the component changes the shading of a 2D shape
 */
static void tr_test_shape_material(
    box_render_transition_component_t c,
    bool patched)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    tr_material_case_t case_ = tr_case(c), *mc = &case_;
    ecs_entity_t e = ecs_new(world);
    box_render_shading_t rendered, expected;
    box_draw_material_t m;
    float mid[BOX_RENDER_TRANSITION_VALUE / 4];
    int32_t i;
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rect, {10, 20});
    ecs_set_id(world, e, mc->component, (size_t)mc->size, mc->from);
    box_transition_set(world, e, mc->component, &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    test_assert(!tr_batch(world, e)->key.dynamic_material);

    ecs_set_id(world, e, mc->component, (size_t)mc->size, mc->to);
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_assert(!ecs_os_memcmp(ecs_get_id(world, e, mc->component), mc->to, mc->size));
        test_bool(ecs_has_id(world, e, TransitionMaterial), patched);
        test_bool(tr_batch(world, e)->key.dynamic_material, patched);
        test_assert(!tr_batch(world, e)->key.cutout);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), patched ? 1 : 0);
        tr_material_mix(mc, (float)i / 4.0f, mid);
        box_draws_shape_material(world, e, ecs_get(world, e, DrawSlot), &m);
        tr_material_set(&m, mc->c, mid);
        box_draws_material(&expected, &m);
        tr_shading_rendered(world, null, e, &rendered);
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    test_assert(!tr_batch(world, e)->key.dynamic_material);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    box_draws_shape_material(world, e, ecs_get(world, e, DrawSlot), &m);
    box_draws_material(&expected, &m);
    tr_shading_rendered(world, null, e, &rendered);
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
    tr_fini(world, null);
}

void TransitionMaterial_rgb_mesh(void) {
    // engine_core/transition
    tr_test_mesh_material(BOX_RENDER_TRANSITION_RGB);
}

void TransitionMaterial_emissive_mesh(void) {
    // engine_core/transition
    tr_test_mesh_material(BOX_RENDER_TRANSITION_EMISSIVE);
}

void TransitionMaterial_pbr_mesh(void) {
    // engine_core/transition
    tr_test_mesh_material(BOX_RENDER_TRANSITION_PBR_MATERIAL);
}

/** Check the Transmission words of a record of the material table against the Transmission
 * case at a progress of k quarters.
 *
 * @param r The record
 * @param k Quarters, 0 to 4
 */
static void tr_transmission_check(
    const box_render_material_t *r,
    uint32_t k)
{
    float t = (float)k / 4.0f;
    test_flt(r->transmission_factor, t);
    test_flt(r->ior, 1.0f + t);
    test_flt(r->thickness_factor, 4.0f * t);
    test_flt(r->attenuation_distance, 1.0f + 4.0f * t);
    test_uint(r->attenuation_color, tr_color_word(50 * k, 25 * k, 10 * k));
}

/** Check that every Transmission word of a record of the material table lies strictly between
 * the two values of the Transmission case.
 *
 * @param r The record
 */
static void tr_transmission_between(
    const box_render_material_t *r)
{
    const Transmission *a = &tr_transmission_from, *b = &tr_transmission_to;
    test_assert(r->transmission_factor > a->transmission_factor);
    test_assert(r->transmission_factor < b->transmission_factor);
    test_assert(r->ior > a->ior && r->ior < b->ior);
    test_assert(r->thickness_factor > a->thickness_factor);
    test_assert(r->thickness_factor < b->thickness_factor);
    test_assert(r->attenuation_distance > a->attenuation_distance);
    test_assert(r->attenuation_distance < b->attenuation_distance);
    test_assert((r->attenuation_color & 0xFF) > a->attenuation_color.r);
    test_assert((r->attenuation_color & 0xFF) < b->attenuation_color.r);
    test_assert(((r->attenuation_color >> 8) & 0xFF) > a->attenuation_color.g);
    test_assert(((r->attenuation_color >> 8) & 0xFF) < b->attenuation_color.g);
    test_assert(((r->attenuation_color >> 16) & 0xFF) > a->attenuation_color.b);
    test_assert(((r->attenuation_color >> 16) & 0xFF) < b->attenuation_color.b);
}

void TransitionMaterial_transmission_mesh(void) {
    // engine_core/transition
    tr_test_mesh_material(BOX_RENDER_TRANSITION_TRANSMISSION);

    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered;
    int32_t i;
    ecs_set_ptr(world, e, Transmission, &tr_transmission_from);
    box_transition_set(world, e, ecs_id(Transmission), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    test_assert(slot != 0 && slot != UINT32_MAX);

    ecs_set_ptr(world, e, Transmission, &tr_transmission_to);
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        tr_transmission_check(&rendered, (uint32_t)i);
        if (i) {
            tr_transmission_between(&rendered);
        }
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    tr_transmission_check(&rendered, 4);
    tr_fini(world, null);
}

void TransitionMaterial_transmission_prefab(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    box_render_transitions_t *s = box_render_transitions_get(world);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t instances[8];
    box_render_material_t rendered;
    const Transmission own = {
        .transmission_factor = 0.5f, .ior = 1.25f, .thickness_factor = 2,
        .attenuation_distance = 3, .attenuation_color = {10, 20, 30}
    };
    int32_t i;
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set_ptr(world, prefab, Transmission, &tr_transmission_from);
    box_transition_set(world, prefab, ecs_id(Transmission),
        &(TransitionSpec){ .duration = 1.0f });
    for (i = 0; i < 8; i ++) {
        instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, instances[i], Position, {(float)i, 0, 0});
    }

    ecs_entity_t over = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, over, Position, {0, 2, 0});
    ecs_set_ptr(world, over, Transmission, &own);
    tr_frame(world);
    tr_frame(world);
    uint32_t slot_p = ecs_get(world, prefab, MaterialId)->value;
    uint32_t slot_o = ecs_get(world, over, MaterialId)->value;
    test_assert(ecs_owns(world, over, MaterialId));
    test_assert(slot_o != slot_p);
    int32_t batch = ecs_get(world, instances[0], DrawSlot)->batch;
    for (i = 0; i < 8; i ++) {
        const DrawSlot *ds = ecs_get(world, instances[i], DrawSlot);
        test_assert(!ecs_owns(world, instances[i], MaterialId));
        test_assert(tr_batch(world, instances[i])->key.transmissive);
        test_uint(box_draws_slot_mesh_shading(tr_batch(world, instances[i]), ds->slot)->material,
            slot_p);
    }

    ecs_set_ptr(world, prefab, Transmission, &tr_transmission_to);
    for (i = 0; i < 4; i ++) {
        int32_t j;
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_int(box_render_transitions_material_visited(s), 2);
        test_assert(tr_material_rendered(world, null, slot_p, &rendered));
        tr_transmission_check(&rendered, (uint32_t)i);
        if (i) {
            tr_transmission_between(&rendered);
        }

        if (tr_material_rendered(world, null, slot_o, &rendered)) {
            test_flt(rendered.transmission_factor, own.transmission_factor);
            test_flt(rendered.ior, own.ior);
            test_uint(rendered.attenuation_color, tr_color_word(10, 20, 30));
        }

        for (j = 0; j < 8; j ++) {
            const DrawSlot *ds = ecs_get(world, instances[j], DrawSlot);
            test_assert(!ecs_has_id(world, instances[j], TransitionMaterial));
            test_assert(!ecs_os_memcmp_t(ecs_get(world, instances[j], Transmission),
                &tr_transmission_to, Transmission));
            test_int(ds->batch, batch);
            test_uint(box_draws_slot_mesh_shading(tr_batch(world, instances[j]),
                ds->slot)->material, slot_p);
        }
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot_p, &rendered));
    tr_transmission_check(&rendered, 4);
    tr_fini(world, null);
}

void TransitionMaterial_transmission_keeps_batch(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered;
    int32_t i;
    ecs_set(world, e, Transmission, { .transmission_factor = 0, .ior = 1.5f });
    box_transition_set(world, e, ecs_id(Transmission), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    int32_t at = ecs_get(world, e, DrawSlot)->slot;
    test_assert(tr_batch(world, e)->key.transmissive);

    ecs_set(world, e, Transmission, { .transmission_factor = 1, .ior = 1.5f });
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_int(ecs_get(world, e, DrawSlot)->batch, batch);
        test_int(ecs_get(world, e, DrawSlot)->slot, at);
        test_assert(tr_batch(world, e)->key.transmissive);
        test_assert(!ecs_has_id(world, e, TransitionMaterial));
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_flt(rendered.transmission_factor, 0.25f * (float)i);
    }

    tr_frame(world);
    test_int(ecs_get(world, e, DrawSlot)->batch, batch);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_flt(rendered.transmission_factor, 1.0f);

    ecs_set(world, e, Transmission, { .transmission_factor = 0, .ior = 1.5f });
    for (i = 0; i < 5; i ++) {
        tr_frame(world);
        test_int(ecs_get(world, e, DrawSlot)->batch, batch);
        test_int(ecs_get(world, e, DrawSlot)->slot, at);
        test_assert(tr_batch(world, e)->key.transmissive);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_flt(rendered.transmission_factor, 1.0f - 0.25f * (float)i);
    }

    tr_fini(world, null);
}

void TransitionMaterial_uv_mesh(void) {
    // engine_core/transition
    tr_test_mesh_material(BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM);
}

void TransitionMaterial_cutoff_mesh(void) {
    // engine_core/transition
    tr_test_mesh_material(BOX_RENDER_TRANSITION_ALPHA_CUTOFF);
}

void TransitionMaterial_rgb_2d(void) {
    // engine_core/transition
    tr_test_shape_material(BOX_RENDER_TRANSITION_RGB, true);
}

void TransitionMaterial_emissive_2d(void) {
    // engine_core/transition
    tr_test_shape_material(BOX_RENDER_TRANSITION_EMISSIVE, true);
}

void TransitionMaterial_pbr_2d(void) {
    // engine_core/transition
    tr_test_shape_material(BOX_RENDER_TRANSITION_PBR_MATERIAL, false);
}

void TransitionMaterial_uv_2d(void) {
    // engine_core/transition
    tr_test_shape_material(BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM, true);
}

void TransitionMaterial_cutoff_2d(void) {
    // engine_core/transition
    tr_test_shape_material(BOX_RENDER_TRANSITION_ALPHA_CUTOFF, false);
}

void TransitionMaterial_cutoff_batch(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered;
    int32_t i;
    ecs_set(world, e, AlphaCutoff, {0.25f});
    box_transition_set(world, e, ecs_id(AlphaCutoff), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    test_assert(tr_batch(world, e)->key.cutout);

    ecs_set(world, e, AlphaCutoff, {0.75f});
    for (i = 0; i < 2; i ++) {
        tr_frame(world);
        test_assert(tr_batch(world, e)->key.cutout);
        test_int(ecs_get(world, e, DrawSlot)->batch, batch);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_flt(rendered.cutoff, 0.25f + 0.125f * (float)i);
    }

    ecs_remove(world, e, AlphaCutoff);
    tr_frame(world);
    test_assert(!tr_batch(world, e)->key.cutout);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_flt(rendered.cutoff, 0.0f);
    tr_fini(world, null);
}

void TransitionMaterial_prefab_one_slot(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t instances[32];
    box_render_material_t rendered;
    int32_t i;
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, Rgb, {0, 0, 0});
    box_transition_set(world, prefab, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    for (i = 0; i < 32; i ++) {
        instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, instances[i], Position, {(float)i, 0, 0});
    }

    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, prefab, MaterialId)->value;
    for (i = 0; i < 32; i ++) {
        const DrawSlot *ds = ecs_get(world, instances[i], DrawSlot);
        test_assert(!ecs_owns(world, instances[i], MaterialId));
        test_uint(box_draws_slot_mesh_shading(tr_batch(world, instances[i]), ds->slot)->material,
            slot);
    }

    ecs_set(world, prefab, Rgb, {200, 100, 40});
    for (i = 0; i < 4; i ++) {
        uint32_t k = (uint32_t)i;
        int32_t j;
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
        test_int(box_render_transitions_material_visited(box_render_transitions_get(world)), 1);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_uint(rendered.color, tr_color_word(50 * k, 25 * k, 10 * k));
        for (j = 0; j < 32; j ++) {
            const Rgb *rgb = ecs_get(world, instances[j], Rgb);
            test_assert(!ecs_has_id(world, instances[j], TransitionMaterial));
            test_int(rgb->r, 200);
            test_int(rgb->g, 100);
            test_int(rgb->b, 40);
        }
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_uint(rendered.color, tr_color_word(200, 100, 40));
    tr_fini(world, null);
}

void TransitionMaterial_unrelated_owners(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    box_render_transitions_t *s = box_render_transitions_get(world);
    ecs_entity_t others[64];
    box_render_material_t rendered, expected[64];
    box_draw_material_t m;
    int32_t i;
    for (i = 0; i < 64; i ++) {
        others[i] = tr_shape(world, 0, (float)(i % 8));
        ecs_set(world, others[i], Rgb, {(uint8_t)i, 10, 20});
    }

    ecs_entity_t e = tr_shape(world, 0, 0);
    ecs_set(world, e, Rgb, {0, 0, 0});
    box_transition_set(world, e, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    for (i = 0; i < 64; i ++) {
        uint32_t other = ecs_get(world, others[i], MaterialId)->value;
        test_assert(other != slot);
        box_draws_material_of(world, others[i], &m);
        box_draws_material_pack(&expected[i], &m);
    }

    test_int(box_render_transitions_material_visited(s), 0);

    ecs_set(world, e, Rgb, {200, 100, 40});
    for (i = 0; i < 4; i ++) {
        uint32_t k = (uint32_t)i;
        int32_t j;
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_int(box_render_transitions_material_visited(s), 1);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_uint(rendered.color, tr_color_word(50 * k, 25 * k, 10 * k));
        for (j = 0; j < 64; j ++) {
            uint32_t other = ecs_get(world, others[j], MaterialId)->value;
            if (tr_material_rendered(world, null, other, &rendered)) {
                test_assert(!ecs_os_memcmp(&rendered, &expected[j], ECS_SIZEOF(rendered)));
            }
        }
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_int(box_render_transitions_material_visited(s), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_uint(rendered.color, tr_color_word(200, 100, 40));
    tr_fini(world, null);
}

void TransitionMaterial_instance_override(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    box_render_material_t rendered;
    int32_t i;
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, Rgb, {0, 0, 0});
    box_transition_set(world, prefab, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, a, Rgb, {0, 0, 0});
    tr_frame(world);
    tr_frame(world);
    uint32_t slot_p = ecs_get(world, prefab, MaterialId)->value;
    uint32_t slot_a = ecs_get(world, a, MaterialId)->value;
    test_assert(ecs_owns(world, a, MaterialId));
    test_assert(slot_a != slot_p);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);

    ecs_set(world, a, Rgb, {200, 100, 40});
    for (i = 0; i < 4; i ++) {
        uint32_t k = (uint32_t)i;
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_assert(tr_material_rendered(world, null, slot_a, &rendered));
        test_uint(rendered.color, tr_color_word(50 * k, 25 * k, 10 * k));
        if (tr_material_rendered(world, null, slot_p, &rendered)) {
            test_uint(rendered.color, 0);
        }
    }

    tr_frame(world);
    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);

    ecs_set(world, prefab, Rgb, {100, 0, 0});
    for (i = 0; i < 4; i ++) {
        uint32_t k = (uint32_t)i;
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_assert(tr_material_rendered(world, null, slot_p, &rendered));
        test_uint(rendered.color, tr_color_word(25 * k, 0, 0));
        if (tr_material_rendered(world, null, slot_a, &rendered)) {
            test_uint(rendered.color, tr_color_word(200, 100, 40));
        }

        test_uint(box_draws_slot_mesh_shading(tr_batch(world, b),
            ecs_get(world, b, DrawSlot)->slot)->material, slot_p);
    }

    tr_frame(world);
    test_assert(tr_material_rendered(world, null, slot_p, &rendered));
    test_uint(rendered.color, tr_color_word(100, 0, 0));
    tr_fini(world, null);
}

void TransitionMaterial_prefab_nested_instance(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    box_render_transitions_t *s = box_render_transitions_get(world);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    box_render_material_t rendered;
    int32_t i;
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, Rgb, {0, 0, 0});
    box_transition_set(world, prefab, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });

    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, a);
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, b, Emissive, {.strength = 1, .color = {10, 20, 30}});
    ecs_entity_t others[16];
    for (i = 0; i < 16; i ++) {
        others[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, others[i], Position, {(float)i, 2, 0});
    }

    tr_frame(world);
    tr_frame(world);
    uint32_t slot_p = ecs_get(world, prefab, MaterialId)->value;
    uint32_t slot_b = ecs_get(world, b, MaterialId)->value;
    test_assert(!ecs_owns(world, a, MaterialId));
    test_assert(ecs_owns(world, b, MaterialId));
    test_assert(slot_b != slot_p);

    ecs_set(world, prefab, Rgb, {200, 100, 40});
    for (i = 0; i < 4; i ++) {
        uint32_t k = (uint32_t)i;
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 2);
        test_int(box_render_transitions_material_visited(s), 3);
        test_assert(tr_material_rendered(world, null, slot_p, &rendered));
        test_uint(rendered.color, tr_color_word(50 * k, 25 * k, 10 * k));
        test_assert(tr_material_rendered(world, null, slot_b, &rendered));
        test_uint(rendered.color, tr_color_word(50 * k, 25 * k, 10 * k));
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot_b, &rendered));
    test_uint(rendered.color, tr_color_word(200, 100, 40));
    tr_fini(world, null);
}

void TransitionMaterial_prefab_instances_no_node(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t instances[64];
    box_render_material_t rendered;
    int32_t i;
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, Rgb, {0, 0, 0});
    box_transition_set(world, prefab, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    for (i = 0; i < 64; i ++) {
        instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, instances[i], Position, {(float)(i % 8), (float)(i / 8), 0});
    }

    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, prefab, MaterialId)->value;
    test_assert(box_render_transition_test_node(world, prefab) != 0);
    for (i = 0; i < 64; i ++) {
        test_int(box_render_transition_test_node(world, instances[i]), 0);
    }

    ecs_set(world, prefab, Rgb, {200, 100, 40});
    for (i = 0; i < 4; i ++) {
        uint32_t k = (uint32_t)i;
        int32_t j;
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_uint(rendered.color, tr_color_word(50 * k, 25 * k, 10 * k));
        for (j = 0; j < 64; j ++) {
            test_int(box_render_transition_test_node(world, instances[j]), 0);
        }
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_uint(rendered.color, tr_color_word(200, 100, 40));
    tr_fini(world, null);
}

void TransitionMaterial_prefab_instance_override_node(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    box_render_material_t rendered, expected;
    box_draw_material_t m;
    int32_t i;
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, Rgb, {200, 100, 40});
    box_transition_set(world, prefab, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, b, Position, {2, 0, 0});
    tr_frame(world);
    tr_frame(world);
    test_int(box_render_transition_test_node(world, a), 0);
    test_int(box_render_transition_test_node(world, b), 0);

    ecs_set(world, a, Rgb, {0, 0, 0});
    tr_frame(world);
    test_assert(box_render_transition_test_node(world, a) != 0);
    test_int(box_render_transition_test_node(world, b), 0);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(ecs_owns(world, a, MaterialId));
    uint32_t slot_a = ecs_get(world, a, MaterialId)->value;
    test_assert(slot_a != ecs_get(world, prefab, MaterialId)->value);
    if (tr_material_rendered(world, null, slot_a, &rendered)) {
        test_uint(rendered.color, tr_color_word(0, 0, 0));
    }

    ecs_set(world, a, Rgb, {200, 100, 40});
    for (i = 0; i < 4; i ++) {
        uint32_t k = (uint32_t)i;
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_assert(tr_material_rendered(world, null, slot_a, &rendered));
        test_uint(rendered.color, tr_color_word(50 * k, 25 * k, 10 * k));
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot_a, &rendered));
    test_uint(rendered.color, tr_color_word(200, 100, 40));

    ecs_set(world, a, Rgb, {0, 0, 0});
    tr_frame(world);
    ecs_remove(world, a, Rgb);
    tr_frame(world);
    test_int(box_render_transition_test_node(world, a), 0);
    tr_frame(world);
    test_int(box_render_transition_test_node(world, a), 0);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    const DrawSlot *ds = ecs_get(world, a, DrawSlot);
    uint32_t slot = box_draws_slot_mesh_shading(tr_batch(world, a), ds->slot)->material;
    box_draws_material_of(world, a, &m);
    box_draws_material_pack(&expected, &m);
    test_uint(expected.color, tr_color_word(200, 100, 40));
    const box_draw_materials_t *table = &box_draws_state(world)->materials;
    test_assert(!ecs_os_memcmp_t(&expected, ecs_vec_get_t(&table->records,
        box_render_material_t, (int32_t)slot), box_render_material_t));
    if (tr_material_rendered(world, null, slot, &rendered)) {
        test_uint(rendered.color, tr_color_word(200, 100, 40));
    }

    tr_fini(world, null);
}

void TransitionMaterial_prefab_inherited_2d(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    box_render_shading_t rendered, expected;
    box_draw_material_t m;
    int32_t i;
    ecs_set(world, prefab, Rect, {10, 20});
    ecs_set(world, prefab, Rgb, {0, 0, 0});
    box_transition_set(world, prefab, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, b, Position, {40, 0, 0});
    ecs_set(world, b, Rgb, {0, 0, 0});
    tr_frame(world);
    tr_frame(world);

    ecs_set(world, prefab, Rgb, {200, 100, 40});
    for (i = 0; i < 4; i ++) {
        uint8_t k = (uint8_t)i;
        Rgb mid = { (uint8_t)(50 * k), (uint8_t)(25 * k), (uint8_t)(10 * k) };
        tr_frame(world);
        test_assert(ecs_has_id(world, a, TransitionMaterial));
        test_assert(!ecs_has_id(world, b, TransitionMaterial));
        test_assert(!ecs_has_id(world, prefab, TransitionMaterial));
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 1);
        box_draws_shape_material(world, a, ecs_get(world, a, DrawSlot), &m);
        m.color = &mid;
        box_draws_material(&expected, &m);
        tr_shading_rendered(world, null, a, &rendered);
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));

        box_draws_shape_material(world, b, ecs_get(world, b, DrawSlot), &m);
        box_draws_material(&expected, &m);
        tr_shading_rendered(world, null, b, &rendered);
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, a, TransitionMaterial));
    box_draws_shape_material(world, a, ecs_get(world, a, DrawSlot), &m);
    box_draws_material(&expected, &m);
    tr_shading_rendered(world, null, a, &rendered);
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_shading_t));
    tr_fini(world, null);
}

void TransitionMaterial_text_rgb(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    App *app = ecs_singleton_get_mut(world, App);
    box_render_shading_t rendered, expected;
    box_draw_material_t m;
    box_draw_tint_t tint;
    int32_t i, j;
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t text = ecs_new(world);
    ecs_set(world, text, Position, {0, 1, 0});
    ecs_set(world, text, Text, {
        .value = "tint", .size = 16, .font = ecs_lookup(world, "box.text.DefaultFont")
    });
    ecs_set(world, text, Rgb, {0, 0, 0});
    box_transition_set(world, text, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    const TextSlots *ts = ecs_get(world, text, TextSlots);
    test_int(ecs_vec_count(&ts->slots), 4);

    ecs_set(world, text, Rgb, {200, 100, 40});
    for (i = 0; i < 4; i ++) {
        uint8_t k = (uint8_t)i;
        Rgb mid = { (uint8_t)(50 * k), (uint8_t)(25 * k), (uint8_t)(10 * k) };
        tr_frame(world);
        test_assert(ecs_has_id(world, text, TransitionMaterial));
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 4);
        box_draws_text_material(world, text, &tint, &m);
        m.color = &mid;
        box_draws_material(&expected, &m);
        ts = ecs_get(world, text, TextSlots);
        for (j = 0; j < 4; j ++) {
            const box_draw_glyph_slot_t *at = ecs_vec_get_t(
                &ts->slots, box_draw_glyph_slot_t, j);
            box_draw_batch_t *b = box_draws_batch(box_draws_state(world), at->batch);
            test_assert(b->key.dynamic_material);
            tr_shading_slot(world, null, b, at->slot, &rendered);
            test_assert(!ecs_os_memcmp(rendered.color, expected.color, ECS_SIZEOF(vec4)));
            test_assert(!ecs_os_memcmp(rendered.emissive, expected.emissive,
                ECS_SIZEOF(float) * 3));
        }
    }

    tr_frame(world);
    test_assert(!ecs_has_id(world, text, TransitionMaterial));
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_SHADING), 0);
    tr_fini(world, null);
}

void TransitionMaterial_target_on_game_thread(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    int32_t i;
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, Emissive, { .strength = 0, .color = {0, 0, 0} });
    box_transition_set(world, e, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    box_transition_set(world, e, ecs_id(Emissive), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Rgb, {200, 100, 40});
    ecs_set(world, e, Emissive, { .strength = 2, .color = {10, 20, 30} });
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        const Rgb *rgb = ecs_get(world, e, Rgb);
        const Emissive *emissive = ecs_get(world, e, Emissive);
        test_int(rgb->r, 200);
        test_int(rgb->g, 100);
        test_int(rgb->b, 40);
        test_flt(emissive->strength, 2);
        test_int(emissive->color.b, 30);
        test_int(tr_rgb(world, e).r, 50 * i);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
    }

    tr_fini(world, null);
}

void TransitionMaterial_no_transition_no_records(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {10, 20});
    tr_frame(world);
    ecs_set(world, e, Rgb, {1, 2, 3});
    ecs_set(world, rect, Rgb, {1, 2, 3});
    tr_frame(world);
    test_int(ecs_vec_count(&null->last.transitions), 0);
    test_assert(!ecs_has_id(world, rect, TransitionMaterial));
    test_assert(!tr_batch(world, rect)->key.dynamic_material);
    tr_fini(world, null);
}

void TransitionMaterial_transitions_get_no_lookup(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_world(&null);
    const RenderTransitionState *t = ecs_singleton_get(world, RenderTransitionState);
    test_assert(t != NULL);
    test_assert(t->state != NULL);
    test_assert(box_render_transitions_get(world) == t->state);
    ecs_entity_t q = ecs_lookup(world, "box.render.RenderTransitions");
    test_assert(q != ecs_id(RenderTransitionState));
    test_assert(ecs_query_get(world, q) != NULL);
    tr_fini(world, null);
}
