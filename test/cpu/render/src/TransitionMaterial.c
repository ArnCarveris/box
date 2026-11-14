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
#include <math.h>

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
    case BOX_RENDER_TRANSITION_CLEAR_COAT: m->clear_coat = value; break;
    case BOX_RENDER_TRANSITION_ANISOTROPY: m->anisotropy = value; break;
    case BOX_RENDER_TRANSITION_SHEEN: m->sheen = value; break;
    case BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION: m->diffuse_transmission = value; break;
    case BOX_RENDER_TRANSITION_FRESNEL: m->fresnel = value; break;
    case BOX_RENDER_TRANSITION_DISTORTION: m->distortion = value; break;
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

/** The ClearCoat case: both fields rise, so both unorm16 halves of the packed word do */
static const ClearCoat tr_coat_from = { .strength = 0.25f, .roughness = 0 };
static const ClearCoat tr_coat_to = { .strength = 1, .roughness = 1 };

/** The SpecularAnisotropy case: the rotation turns twice, 0 to 4 pi, which a shortest path or a
 * wrap of the moving value would not, so the packed rotation is pi, 2 pi and 3 pi in between */
static const SpecularAnisotropy tr_aniso_from = { .strength = 0.25f, .rotation = 0 };
static const SpecularAnisotropy tr_aniso_to = { .strength = 1, .rotation = 4.0f * GLM_PIf };

/** The Sheen case: every color channel steps by an integer each quarter */
static const Sheen tr_sheen_from = { .color = {40, 20, 8}, .roughness = 0 };
static const Sheen tr_sheen_to = { .color = {200, 100, 40}, .roughness = 1 };

/** The DiffuseTransmission case: every float and color channel steps by an exact amount each
 * quarter */
static const DiffuseTransmission tr_diffuse_from = {
    .factor = 0, .color = {0, 0, 0}, .thickness = 0, .blur = 0
};
static const DiffuseTransmission tr_diffuse_to = {
    .factor = 1, .color = {200, 100, 40}, .thickness = 2, .blur = 0.5f
};

/** The Fresnel case: every color channel steps by an integer and every float by an exact amount
 * each quarter */
static const Fresnel tr_fresnel_from = {
    .color = {40, 20, 8}, .strength = 0, .power = 1, .bias = 0, .opacity = 0
};
static const Fresnel tr_fresnel_to = {
    .color = {200, 100, 40}, .strength = 4, .power = 5, .bias = 0.5f, .opacity = 1
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
    case BOX_RENDER_TRANSITION_CLEAR_COAT:
        result.component = ecs_id(ClearCoat);
        result.size = ECS_SIZEOF(ClearCoat);
        result.from = &tr_coat_from;
        result.to = &tr_coat_to;
        break;
    case BOX_RENDER_TRANSITION_ANISOTROPY:
        result.component = ecs_id(SpecularAnisotropy);
        result.size = ECS_SIZEOF(SpecularAnisotropy);
        result.from = &tr_aniso_from;
        result.to = &tr_aniso_to;
        break;
    case BOX_RENDER_TRANSITION_SHEEN:
        result.component = ecs_id(Sheen);
        result.size = ECS_SIZEOF(Sheen);
        result.from = &tr_sheen_from;
        result.to = &tr_sheen_to;
        break;
    case BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION:
        result.component = ecs_id(DiffuseTransmission);
        result.size = ECS_SIZEOF(DiffuseTransmission);
        result.from = &tr_diffuse_from;
        result.to = &tr_diffuse_to;
        break;
    case BOX_RENDER_TRANSITION_FRESNEL:
        result.component = ecs_id(Fresnel);
        result.size = ECS_SIZEOF(Fresnel);
        result.from = &tr_fresnel_from;
        result.to = &tr_fresnel_to;
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
        box_draws_material_of(world, e, 0, &m);
        tr_material_set(&m, mc->c, mid);
        box_draws_material_pack(&expected, &m);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_material_t));
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    box_draws_material_of(world, e, 0, &m);
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
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
        .punctual_shadows = true, .cookies = true, .environment = true,
        .aerial_perspective = true, .alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
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
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
        .punctual_shadows = true, .cookies = true, .environment = true,
        .aerial_perspective = true, .alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
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

/** Get the unorm16 of a value as the material table packs it.
 *
 * @param value The value
 * @return The unorm16
 */
static uint32_t tr_unorm16(
    float value)
{
    return (uint32_t)(glm_clamp(value, 0.0f, 1.0f) * 65535.0f + 0.5f);
}

/** Get the packed clear_coat word of a ClearCoat.
 *
 * @param coat The coat
 * @return The word
 */
static uint32_t tr_coat_word(
    const ClearCoat *coat)
{
    return tr_unorm16(coat->strength) | (tr_unorm16(coat->roughness) << 16);
}

/** Check the clear_coat word of a record of the material table against the ClearCoat case at a
 * progress of k quarters, and that both halves lie strictly between the two values in between.
 *
 * @param r The record
 * @param k Quarters, 0 to 4
 */
static void tr_coat_check(
    const box_render_material_t *r,
    uint32_t k)
{
    float t = (float)k / 4.0f;
    ClearCoat expected = {
        .strength = tr_coat_from.strength + (tr_coat_to.strength - tr_coat_from.strength) * t,
        .roughness = tr_coat_from.roughness + (tr_coat_to.roughness - tr_coat_from.roughness) * t
    };
    test_uint(r->clear_coat, tr_coat_word(&expected));
    if (k && k < 4) {
        uint32_t from = tr_coat_word(&tr_coat_from), to = tr_coat_word(&tr_coat_to);
        test_assert((r->clear_coat & 0xFFFFu) > (from & 0xFFFFu));
        test_assert((r->clear_coat & 0xFFFFu) < (to & 0xFFFFu));
        test_assert((r->clear_coat >> 16) > (from >> 16));
        test_assert((r->clear_coat >> 16) < (to >> 16));
    }
}

void TransitionMaterial_clear_coat_mesh(void) {
    // engine_core/transition
    tr_test_mesh_material(BOX_RENDER_TRANSITION_CLEAR_COAT);

    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered;
    int32_t i;
    ecs_set_ptr(world, e, ClearCoat, &tr_coat_from);
    box_transition_set(world, e, ecs_id(ClearCoat), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    test_assert(slot != 0 && slot != UINT32_MAX);

    ecs_set_ptr(world, e, ClearCoat, &tr_coat_to);
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        tr_coat_check(&rendered, (uint32_t)i);
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    tr_coat_check(&rendered, 4);
    test_uint(rendered.clear_coat, tr_coat_word(&tr_coat_to));
    tr_fini(world, null);
}

void TransitionMaterial_clear_coat_prefab(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    box_render_transitions_t *s = box_render_transitions_get(world);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t instances[8];
    box_render_material_t rendered;
    const ClearCoat own = { .strength = 0.5f, .roughness = 0.25f };
    int32_t i;
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set_ptr(world, prefab, ClearCoat, &tr_coat_from);
    box_transition_set(world, prefab, ecs_id(ClearCoat), &(TransitionSpec){ .duration = 1.0f });
    for (i = 0; i < 8; i ++) {
        instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, instances[i], Position, {(float)i, 0, 0});
    }

    ecs_entity_t over = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, over, Position, {0, 2, 0});
    ecs_set_ptr(world, over, ClearCoat, &own);
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
        test_uint(box_draws_slot_mesh_shading(tr_batch(world, instances[i]), ds->slot)->material,
            slot_p);
    }

    ecs_set_ptr(world, prefab, ClearCoat, &tr_coat_to);
    for (i = 0; i < 4; i ++) {
        int32_t j;
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_int(box_render_transitions_material_visited(s), 2);
        test_assert(tr_material_rendered(world, null, slot_p, &rendered));
        tr_coat_check(&rendered, (uint32_t)i);
        if (tr_material_rendered(world, null, slot_o, &rendered)) {
            test_uint(rendered.clear_coat, tr_coat_word(&own));
        }

        for (j = 0; j < 8; j ++) {
            const DrawSlot *ds = ecs_get(world, instances[j], DrawSlot);
            test_assert(!ecs_has_id(world, instances[j], TransitionMaterial));
            test_assert(!ecs_os_memcmp_t(ecs_get(world, instances[j], ClearCoat),
                &tr_coat_to, ClearCoat));
            test_int(ds->batch, batch);
            test_uint(box_draws_slot_mesh_shading(tr_batch(world, instances[j]),
                ds->slot)->material, slot_p);
        }
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot_p, &rendered));
    tr_coat_check(&rendered, 4);
    tr_fini(world, null);
}

void TransitionMaterial_clear_coat_keeps_batch(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered;
    int32_t i;
    ecs_set(world, e, ClearCoat, { .strength = 0, .roughness = 0.5f });
    box_transition_set(world, e, ecs_id(ClearCoat), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    int32_t at = ecs_get(world, e, DrawSlot)->slot;
    test_uint(ecs_vec_get_t(&box_draws_state(world)->materials.records,
        box_render_material_t, (int32_t)slot)->clear_coat, 0);

    ecs_set(world, e, ClearCoat, { .strength = 1, .roughness = 0.5f });
    for (i = 0; i < 4; i ++) {
        ClearCoat expected = { .strength = 0.25f * (float)i, .roughness = 0.5f };
        tr_frame(world);
        test_int(ecs_get(world, e, DrawSlot)->batch, batch);
        test_int(ecs_get(world, e, DrawSlot)->slot, at);
        test_assert(!ecs_has_id(world, e, TransitionMaterial));
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_uint(rendered.clear_coat, i ? tr_coat_word(&expected) : 0u);
    }

    tr_frame(world);
    test_int(ecs_get(world, e, DrawSlot)->batch, batch);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_uint(rendered.clear_coat, tr_coat_word(&(ClearCoat){ 1, 0.5f }));

    ecs_set(world, e, ClearCoat, { .strength = 0, .roughness = 0.5f });
    for (i = 0; i < 5; i ++) {
        ClearCoat expected = { .strength = 1.0f - 0.25f * (float)i, .roughness = 0.5f };
        tr_frame(world);
        test_int(ecs_get(world, e, DrawSlot)->batch, batch);
        test_int(ecs_get(world, e, DrawSlot)->slot, at);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_uint(rendered.clear_coat, i < 4 ? tr_coat_word(&expected) : 0u);
    }

    tr_fini(world, null);
}

void TransitionMaterial_clear_coat_2d(void) {
    // engine_core/transition
    tr_test_shape_material(BOX_RENDER_TRANSITION_CLEAR_COAT, false);
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
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
        .punctual_shadows = true, .cookies = true, .environment = true,
        .aerial_perspective = true, .alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
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
        box_draws_material_of(world, others[i], 0, &m);
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
    box_draws_material_of(world, a, 0, &m);
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

/** Run a material transition of a prefab with mesh instances and one instance that owns its
 * own value: the slot of the prefab is patched at every quarter with the mixed value, the slot
 * of the owner keeps its own, and no instance changes its batch or slot.
 *
 * @param c The component
 * @param own The value of the instance that owns one
 */
static void tr_test_prefab_material(
    box_render_transition_component_t c,
    const void *own)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    box_render_transitions_t *s = box_render_transitions_get(world);
    tr_material_case_t case_ = tr_case(c), *mc = &case_;
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t instances[8];
    box_render_material_t rendered, expected;
    box_draw_material_t m;
    float mid[BOX_RENDER_TRANSITION_VALUE / 4];
    int32_t i;
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set_id(world, prefab, mc->component, (size_t)mc->size, mc->from);
    box_transition_set(world, prefab, mc->component, &(TransitionSpec){ .duration = 1.0f });
    for (i = 0; i < 8; i ++) {
        instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, instances[i], Position, {(float)i, 0, 0});
    }

    ecs_entity_t over = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, over, Position, {0, 2, 0});
    ecs_set_id(world, over, mc->component, (size_t)mc->size, own);
    tr_frame(world);
    tr_frame(world);
    uint32_t slot_p = ecs_get(world, prefab, MaterialId)->value;
    uint32_t slot_o = ecs_get(world, over, MaterialId)->value;
    test_assert(ecs_owns(world, over, MaterialId));
    test_assert(slot_o != slot_p);
    int32_t batch = ecs_get(world, instances[0], DrawSlot)->batch;
    int32_t batch_o = ecs_get(world, over, DrawSlot)->batch;
    box_render_material_t own_record;
    box_draws_material_of(world, over, 0, &m);
    box_draws_material_pack(&own_record, &m);

    ecs_set_id(world, prefab, mc->component, (size_t)mc->size, mc->to);
    for (i = 0; i < 4; i ++) {
        int32_t j;
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_int(box_render_transitions_material_visited(s), 2);
        tr_material_mix(mc, (float)i / 4.0f, mid);
        box_draws_material_of(world, prefab, 0, &m);
        tr_material_set(&m, mc->c, mid);
        box_draws_material_pack(&expected, &m);
        test_assert(tr_material_rendered(world, null, slot_p, &rendered));
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_material_t));
        if (tr_material_rendered(world, null, slot_o, &rendered)) {
            test_assert(!ecs_os_memcmp_t(&rendered, &own_record, box_render_material_t));
        }

        test_int(ecs_get(world, over, DrawSlot)->batch, batch_o);
        for (j = 0; j < 8; j ++) {
            const DrawSlot *ds = ecs_get(world, instances[j], DrawSlot);
            test_assert(!ecs_has_id(world, instances[j], TransitionMaterial));
            test_assert(!ecs_os_memcmp(ecs_get_id(world, instances[j], mc->component), mc->to,
                mc->size));
            test_int(ds->batch, batch);
            test_uint(box_draws_slot_mesh_shading(tr_batch(world, instances[j]),
                ds->slot)->material, slot_p);
        }
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    box_draws_material_of(world, prefab, 0, &m);
    box_draws_material_pack(&expected, &m);
    test_assert(tr_material_rendered(world, null, slot_p, &rendered));
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_material_t));
    tr_fini(world, null);
}

/** Check that a mesh shape with a material component and a Transition of it pays nothing
 * while the component does not change, and that a shape without a Transition pays nothing when
 * it changes: no transition records, no TransitionMaterial and the slot is packed directly.
 *
 * @param c The component
 */
static void tr_test_material_idle(
    box_render_transition_component_t c)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    box_render_transitions_t *s = box_render_transitions_get(world);
    tr_material_case_t case_ = tr_case(c), *mc = &case_;
    ecs_entity_t e = tr_shape(world, 0, 0);
    ecs_entity_t plain = tr_shape(world, 0, 2);
    box_render_material_t rendered, expected;
    box_draw_material_t m;
    int32_t i;
    ecs_set_id(world, e, mc->component, (size_t)mc->size, mc->from);
    box_transition_set(world, e, mc->component, &(TransitionSpec){ .duration = 1.0f });
    ecs_set_id(world, plain, mc->component, (size_t)mc->size, mc->from);
    tr_frame(world);
    tr_frame(world);
    for (i = 0; i < 3; i ++) {
        tr_frame(world);
        test_int(ecs_vec_count(&null->last.transitions), 0);
        test_int(box_render_transitions_material_visited(s), 0);
        test_assert(!ecs_has_id(world, e, TransitionMaterial));
    }

    int32_t batch = ecs_get(world, plain, DrawSlot)->batch;
    ecs_set_id(world, plain, mc->component, (size_t)mc->size, mc->to);
    tr_frame(world);
    uint32_t slot = ecs_get(world, plain, MaterialId)->value;
    test_assert(slot != ecs_get(world, e, MaterialId)->value);
    test_int(ecs_vec_count(&null->last.transitions), 0);
    test_int(box_render_transitions_material_visited(s), 0);
    test_assert(!ecs_has_id(world, plain, TransitionMaterial));
    test_int(ecs_get(world, plain, DrawSlot)->batch, batch);
    box_draws_material_of(world, plain, 0, &m);
    box_draws_material_pack(&expected, &m);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_material_t));
    tr_fini(world, null);
}

/** Check that the final record of a transition equals the record of a shape that had the target
 * set directly, without a transition: once it ends the shape shares the slot of that shape, in
 * the same batch.
 *
 * @param c The component
 */
static void tr_test_material_final(
    box_render_transition_component_t c)
{
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    tr_material_case_t case_ = tr_case(c), *mc = &case_;
    ecs_entity_t e = tr_shape(world, 0, 0);
    ecs_entity_t direct = tr_shape(world, 0, 2);
    box_render_material_t packed, still;
    box_draw_material_t m;
    int32_t i;
    ecs_set_id(world, e, mc->component, (size_t)mc->size, mc->from);
    box_transition_set(world, e, mc->component, &(TransitionSpec){ .duration = 1.0f });
    ecs_set_id(world, direct, mc->component, (size_t)mc->size, mc->to);
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    uint32_t slot_d = ecs_get(world, direct, MaterialId)->value;
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    test_assert(slot != slot_d);

    ecs_set_id(world, e, mc->component, (size_t)mc->size, mc->to);
    for (i = 0; i < 5; i ++) {
        tr_frame(world);
        test_int(ecs_get(world, e, DrawSlot)->batch, batch);
        test_assert(!ecs_has_id(world, e, TransitionMaterial));
    }

    const DrawSlot *ds = ecs_get(world, e, DrawSlot);
    test_uint(ecs_get(world, e, MaterialId)->value, slot_d);
    test_uint(box_draws_slot_mesh_shading(tr_batch(world, e), ds->slot)->material, slot_d);
    box_draws_material_of(world, e, 0, &m);
    box_draws_material_pack(&packed, &m);
    still = *ecs_vec_get_t(&box_draws_state(world)->materials.records, box_render_material_t,
        (int32_t)slot_d);
    test_assert(!ecs_os_memcmp_t(&packed, &still, box_render_material_t));
    test_int(ds->batch, ecs_get(world, direct, DrawSlot)->batch);
    tr_fini(world, null);
}

/** Get the packed anisotropy word of a SpecularAnisotropy, the rotation wrapped to 0..2 pi.
 *
 * @param a The anisotropy
 * @return The word
 */
static uint32_t tr_aniso_word(
    const SpecularAnisotropy *a)
{
    float turn = 2.0f * GLM_PIf;
    float rotation = fmodf(a->rotation, turn);
    if (rotation < 0.0f) {
        rotation += turn;
    }

    return tr_unorm16(a->strength) | (tr_unorm16(rotation / turn) << 16);
}

/* The rotation renders 0, pi, 2 pi and 3 pi in the frames after the change: packed as none, half
 * a turn, none and half a turn, which neither a shortest path nor a wrap of the moving value
 * gives. */
void TransitionMaterial_specular_anisotropy_mesh(void) {
    // engine_core/transition
    tr_test_mesh_material(BOX_RENDER_TRANSITION_ANISOTROPY);

    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered;
    int32_t i;
    ecs_set_ptr(world, e, SpecularAnisotropy, &tr_aniso_from);
    box_transition_set(world, e, ecs_id(SpecularAnisotropy),
        &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    test_assert(slot != 0 && slot != UINT32_MAX);

    ecs_set_ptr(world, e, SpecularAnisotropy, &tr_aniso_to);
    for (i = 0; i < 4; i ++) {
        SpecularAnisotropy expected = {
            .strength = 0.25f + 0.75f * 0.25f * (float)i,
            .rotation = GLM_PIf * (float)i
        };
        uint32_t half;
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_uint(rendered.anisotropy & 0xFFFFu, tr_unorm16(expected.strength));
        half = rendered.anisotropy >> 16;
        if (i == 1 || i == 3) {
            test_assert(half >= 32766u && half <= 32769u);
        } else {
            test_assert(half <= 1u || half >= 65534u);
        }
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_uint(rendered.anisotropy, tr_aniso_word(&tr_aniso_to));
    tr_fini(world, null);
}

void TransitionMaterial_specular_anisotropy_prefab(void) {
    // engine_core/transition
    tr_test_prefab_material(BOX_RENDER_TRANSITION_ANISOTROPY,
        &(SpecularAnisotropy){ .strength = 0.5f, .rotation = 1.0f });
}

void TransitionMaterial_specular_anisotropy_final(void) {
    // engine_core/transition
    tr_test_material_final(BOX_RENDER_TRANSITION_ANISOTROPY);
}

void TransitionMaterial_specular_anisotropy_idle(void) {
    // engine_core/transition
    tr_test_material_idle(BOX_RENDER_TRANSITION_ANISOTROPY);
}

void TransitionMaterial_specular_anisotropy_2d(void) {
    // engine_core/transition
    tr_test_shape_material(BOX_RENDER_TRANSITION_ANISOTROPY, false);
}

void TransitionMaterial_sheen_mesh(void) {
    // engine_core/transition
    tr_test_mesh_material(BOX_RENDER_TRANSITION_SHEEN);

    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered;
    int32_t i;
    ecs_set_ptr(world, e, Sheen, &tr_sheen_from);
    box_transition_set(world, e, ecs_id(Sheen), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    test_assert(slot != 0 && slot != UINT32_MAX);

    ecs_set_ptr(world, e, Sheen, &tr_sheen_to);
    for (i = 0; i < 4; i ++) {
        uint32_t k = (uint32_t)i;
        uint32_t r = (uint32_t)(0.25f * (float)i * 255.0f + 0.5f);
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_uint(rendered.sheen & 0xFFFFFFu,
            tr_color_word(40 + 40 * k, 20 + 20 * k, 8 + 8 * k));
        test_uint(rendered.sheen >> 24, r);
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_uint(rendered.sheen, tr_color_word(200, 100, 40) | (255u << 24));
    tr_fini(world, null);
}

void TransitionMaterial_sheen_prefab(void) {
    // engine_core/transition
    tr_test_prefab_material(BOX_RENDER_TRANSITION_SHEEN,
        &(Sheen){ .color = {10, 200, 30}, .roughness = 0.25f });
}

void TransitionMaterial_sheen_final(void) {
    // engine_core/transition
    tr_test_material_final(BOX_RENDER_TRANSITION_SHEEN);
}

void TransitionMaterial_sheen_idle(void) {
    // engine_core/transition
    tr_test_material_idle(BOX_RENDER_TRANSITION_SHEEN);
}

void TransitionMaterial_sheen_2d(void) {
    // engine_core/transition
    tr_test_shape_material(BOX_RENDER_TRANSITION_SHEEN, false);
}

void TransitionMaterial_diffuse_transmission_mesh(void) {
    // engine_core/transition
    tr_test_mesh_material(BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION);

    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered;
    int32_t i;
    ecs_set_ptr(world, e, DiffuseTransmission, &tr_diffuse_from);
    box_transition_set(world, e, ecs_id(DiffuseTransmission),
        &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    test_assert(slot != 0 && slot != UINT32_MAX);

    ecs_set_ptr(world, e, DiffuseTransmission, &tr_diffuse_to);
    for (i = 0; i < 4; i ++) {
        uint32_t k = (uint32_t)i;
        float t = 0.25f * (float)i;
        uint32_t f = (uint32_t)(t * 255.0f + 0.5f);
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_uint(rendered.diffuse_transmission & 0xFFFFFFu,
            tr_color_word(50 * k, 25 * k, 10 * k));
        test_uint(rendered.diffuse_transmission >> 24, f);
        test_flt(rendered.thickness, 2.0f * t);
        test_flt(rendered.diffuse_blur, 0.5f * t);
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_uint(rendered.diffuse_transmission, tr_color_word(200, 100, 40) | (255u << 24));
    test_flt(rendered.thickness, 2.0f);
    test_flt(rendered.diffuse_blur, 0.5f);
    tr_fini(world, null);
}

void TransitionMaterial_diffuse_transmission_prefab(void) {
    // engine_core/transition
    tr_test_prefab_material(BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION,
        &(DiffuseTransmission){ .factor = 0.75f, .color = {10, 200, 30}, .thickness = 1 });
}

void TransitionMaterial_diffuse_transmission_final(void) {
    // engine_core/transition
    tr_test_material_final(BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION);
}

void TransitionMaterial_diffuse_transmission_idle(void) {
    // engine_core/transition
    tr_test_material_idle(BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION);
}

void TransitionMaterial_diffuse_transmission_2d(void) {
    // engine_core/transition
    tr_test_shape_material(BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION, false);
}

/** Check the fresnel words of a record of the material table against the ones a Fresnel packs.
 *
 * @param r The record
 * @param fresnel The Fresnel
 */
static void tr_fresnel_check(
    const box_render_material_t *r,
    const Fresnel *fresnel)
{
    box_render_material_t expected;
    box_draw_material_t m = { .fresnel = fresnel };
    box_draws_material_pack(&expected, &m);
    test_uint(r->fresnel_color[0], expected.fresnel_color[0]);
    test_uint(r->fresnel_color[1], expected.fresnel_color[1]);
    test_flt(r->fresnel_strength, fresnel->strength);
    test_flt(r->fresnel_power, fresnel->power);
    test_flt(r->fresnel_bias, fresnel->bias);
    test_flt(r->fresnel_opacity, fresnel->opacity);
}

/** Get the Fresnel of the Fresnel case at a progress of k quarters.
 *
 * @param k Quarters, 0 to 4
 * @return The value
 */
static Fresnel tr_fresnel_at(
    uint32_t k)
{
    float t = (float)k / 4.0f;
    return (Fresnel){
        .color = {(uint8_t)(40 + 40 * k), (uint8_t)(20 + 20 * k), (uint8_t)(8 + 8 * k)},
        .strength = 4.0f * t,
        .power = 1.0f + 4.0f * t,
        .bias = 0.5f * t,
        .opacity = t
    };
}

void TransitionMaterial_fresnel_mesh(void) {
    // engine_core/transition
    tr_test_mesh_material(BOX_RENDER_TRANSITION_FRESNEL);

    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered;
    int32_t i;
    ecs_set_ptr(world, e, Fresnel, &tr_fresnel_from);
    box_transition_set(world, e, ecs_id(Fresnel), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    test_assert(slot != 0 && slot != UINT32_MAX);

    ecs_set_ptr(world, e, Fresnel, &tr_fresnel_to);
    for (i = 0; i < 4; i ++) {
        Fresnel expected = tr_fresnel_at((uint32_t)i);
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        tr_fresnel_check(&rendered, &expected);
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    tr_fresnel_check(&rendered, &tr_fresnel_to);
    tr_fini(world, null);
}

void TransitionMaterial_fresnel_prefab(void) {
    // engine_core/transition
    tr_test_prefab_material(BOX_RENDER_TRANSITION_FRESNEL,
        &(Fresnel){ .color = {10, 200, 30}, .strength = 2, .power = 3, .bias = 0.25f,
            .opacity = 0.5f });
}

void TransitionMaterial_fresnel_final(void) {
    // engine_core/transition
    tr_test_material_final(BOX_RENDER_TRANSITION_FRESNEL);
}

void TransitionMaterial_fresnel_idle(void) {
    // engine_core/transition
    tr_test_material_idle(BOX_RENDER_TRANSITION_FRESNEL);
}

void TransitionMaterial_fresnel_2d(void) {
    // engine_core/transition
    tr_test_shape_material(BOX_RENDER_TRANSITION_FRESNEL, false);
}

void TransitionMaterial_fresnel_keeps_batch(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_singleton_set(world, Scene, {.fresnel = true});
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered;
    int32_t i;
    ecs_set_ptr(world, e, Fresnel, &tr_fresnel_from);
    box_transition_set(world, e, ecs_id(Fresnel), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    int32_t at = ecs_get(world, e, DrawSlot)->slot;
    box_draw_key_t key = tr_batch(world, e)->key;
    test_int(key.pipeline, BOX_RENDER_SHADER_MESH_EXTENDED);

    ecs_set_ptr(world, e, Fresnel, &tr_fresnel_to);
    for (i = 0; i < 4; i ++) {
        Fresnel expected = tr_fresnel_at((uint32_t)i);
        tr_frame(world);
        test_int(ecs_get(world, e, DrawSlot)->batch, batch);
        test_int(ecs_get(world, e, DrawSlot)->slot, at);
        test_int(tr_batch(world, e)->key.pipeline, key.pipeline);
        test_uint(tr_batch(world, e)->key.features, key.features);
        test_assert(!ecs_has_id(world, e, TransitionMaterial));
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        tr_fresnel_check(&rendered, &expected);
    }

    tr_frame(world);
    test_int(ecs_get(world, e, DrawSlot)->batch, batch);
    test_int(tr_batch(world, e)->key.pipeline, key.pipeline);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    tr_fresnel_check(&rendered, &tr_fresnel_to);

    ecs_set_ptr(world, e, Fresnel, &tr_fresnel_from);
    for (i = 0; i < 5; i ++) {
        Fresnel expected = tr_fresnel_at((uint32_t)(4 - i));
        tr_frame(world);
        test_int(ecs_get(world, e, DrawSlot)->batch, batch);
        test_int(ecs_get(world, e, DrawSlot)->slot, at);
        test_int(tr_batch(world, e)->key.pipeline, key.pipeline);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        tr_fresnel_check(&rendered, &expected);
    }

    tr_fini(world, null);
}

/* A change back to the first value three frames into a transition starts from what the
 * transition renders at that time, 3/4 of the way, so no frame jumps. */
void TransitionMaterial_fresnel_interrupt(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    tr_material_case_t case_ = tr_case(BOX_RENDER_TRANSITION_FRESNEL), *mc = &case_;
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered, expected;
    box_draw_material_t m;
    float start[BOX_RENDER_TRANSITION_VALUE / 4], from[BOX_RENDER_TRANSITION_VALUE / 4];
    float mid[BOX_RENDER_TRANSITION_VALUE / 4];
    int32_t i;
    ecs_set_ptr(world, e, Fresnel, &tr_fresnel_from);
    box_transition_set(world, e, ecs_id(Fresnel), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;

    ecs_set_ptr(world, e, Fresnel, &tr_fresnel_to);
    for (i = 0; i < 3; i ++) {
        Fresnel at = tr_fresnel_at((uint32_t)i);
        tr_frame(world);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        tr_fresnel_check(&rendered, &at);
    }

    tr_material_mix(mc, 0.75f, start);
    box_render_transition_pack(BOX_RENDER_TRANSITION_FRESNEL, &tr_fresnel_from, from);
    ecs_set_ptr(world, e, Fresnel, &tr_fresnel_from);
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        test_int(ecs_get(world, e, DrawSlot)->batch, batch);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        box_render_transition_mix(BOX_RENDER_TRANSITION_FRESNEL, start, from,
            (float)i / 4.0f, mid);
        box_draws_material_of(world, e, 0, &m);
        tr_material_set(&m, mc->c, mid);
        box_draws_material_pack(&expected, &m);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_material_t));
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    tr_fresnel_check(&rendered, &tr_fresnel_from);
    tr_fini(world, null);
}

/** The Distortion case: every float steps by an exact amount each quarter, without speed, so
 * the phase stays 0 */
static const Distortion tr_distortion_from = { .strength = 0, .scale = 1, .speed = 0 };
static const Distortion tr_distortion_to = { .strength = 0.008f, .scale = 3, .speed = 0 };

/** A Distortion transitions its three floats linearly in the record of its slot, keeping the
 * transmissive batch of the shape. */
void TransitionMaterial_distortion_mesh(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_singleton_set(world, Scene, {.distortion = true});
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_render_material_t rendered, expected;
    box_draw_material_t m;
    int32_t i;
    ecs_set_ptr(world, e, Distortion, &tr_distortion_from);
    box_transition_set(world, e, ecs_id(Distortion), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);
    tr_frame(world);
    uint32_t slot = ecs_get(world, e, MaterialId)->value;
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    test_assert(slot != 0 && slot != UINT32_MAX);
    test_bool(tr_batch(world, e)->key.transmissive, true);

    ecs_set_ptr(world, e, Distortion, &tr_distortion_to);
    for (i = 0; i < 4; i ++) {
        float from[BOX_RENDER_TRANSITION_VALUE / 4], to[BOX_RENDER_TRANSITION_VALUE / 4];
        float value[BOX_RENDER_TRANSITION_VALUE / 4];
        Distortion mid;
        box_render_transition_pack(BOX_RENDER_TRANSITION_DISTORTION, &tr_distortion_from, from);
        box_render_transition_pack(BOX_RENDER_TRANSITION_DISTORTION, &tr_distortion_to, to);
        box_render_transition_mix(BOX_RENDER_TRANSITION_DISTORTION, from, to, (float)i / 4.0f,
            value);
        ecs_os_memcpy(&mid, value, ECS_SIZEOF(Distortion));
        test_assert(fabsf(mid.strength - 0.002f * (float)i) < 1e-6f);
        test_assert(fabsf(mid.scale - (1.0f + 0.5f * (float)i)) < 1e-6f);
        tr_frame(world);
        test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 1);
        test_int(ecs_get(world, e, DrawSlot)->batch, batch);
        test_bool(tr_batch(world, e)->key.transmissive, true);
        test_assert(tr_material_rendered(world, null, slot, &rendered));
        test_assert(fabsf(rendered.distortion_strength - mid.strength) < 1e-6f);
        test_assert(fabsf(rendered.distortion_frequency - 1.0f / mid.scale) < 1e-5f);
        test_flt(rendered.distortion_phase, 0);
        test_uint(rendered.distortion_flags,
            BOX_RENDER_DISTORTION_PRESENT | BOX_RENDER_DISTORTION_SHARP);
        box_draws_material_of(world, e, 0, &m);
        m.distortion = &mid;
        box_draws_material_pack(&expected, &m);
        test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_material_t));
    }

    tr_frame(world);
    test_int(tr_ops(world, null, BOX_RENDER_TRANSITION_MATERIAL), 0);
    test_assert(tr_material_rendered(world, null, slot, &rendered));
    test_flt(rendered.distortion_strength, 0.008f);
    test_assert(fabsf(rendered.distortion_frequency - 1.0f / 3.0f) < 1e-6f);
    tr_fini(world, null);
}
