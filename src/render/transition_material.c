/**
 * @file render/transition_material.c
 * @brief The target material of a material transition and the records it encodes again, and
 * the fade records of the nodes of Tint and Opacity transitions.
 *
 * The game thread copies the material components a record was encoded from into the target of
 * a transition; the resolver encodes the record again from it, with the values its nodes
 * render, through the same functions, so a component at its target gives the same bytes.
 */

#include "draw.h"
#include <stddef.h>

void box_draws_transition_target(
    box_draw_transition_material_t *out,
    const box_draw_material_t *material,
    box_draw_transition_kind_t kind)
{
    ecs_os_memset_t(out, 0, box_draw_transition_material_t);
    out->kind = (uint8_t)kind;
    out->fade = material->fade;
    out->fade_rel.keep = 1.0f;
    out->fade_alpha = 1.0f;
    out->opacity_scale = 1.0f;
    if (material->color) {
        out->present |= 1u << BOX_RENDER_TRANSITION_RGB;
        out->color = *material->color;
    }

    if (material->cutoff) {
        out->present |= 1u << BOX_RENDER_TRANSITION_ALPHA_CUTOFF;
        out->cutoff = *material->cutoff;
    }

    if (material->pbr) {
        out->present |= 1u << BOX_RENDER_TRANSITION_PBR_MATERIAL;
        out->pbr = *material->pbr;
    }

    if (material->emissive) {
        out->present |= 1u << BOX_RENDER_TRANSITION_EMISSIVE;
        out->emissive = *material->emissive;
    }

    if (material->uv) {
        out->present |= 1u << BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM;
        out->uv = *material->uv;
    }

    if (material->transmission) {
        out->present |= 1u << BOX_RENDER_TRANSITION_TRANSMISSION;
        out->transmission = *material->transmission;
    }

    if (material->clear_coat) {
        out->present |= 1u << BOX_RENDER_TRANSITION_CLEAR_COAT;
        out->clear_coat = *material->clear_coat;
    }

    if (material->anisotropy) {
        out->present |= 1u << BOX_RENDER_TRANSITION_ANISOTROPY;
        out->anisotropy = *material->anisotropy;
    }

    if (material->sheen) {
        out->present |= 1u << BOX_RENDER_TRANSITION_SHEEN;
        out->sheen = *material->sheen;
    }

    if (material->diffuse_transmission) {
        out->present |= 1u << BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION;
        out->diffuse_transmission = *material->diffuse_transmission;
    }

    if (material->opacity) {
        out->present |= 1u << BOX_RENDER_TRANSITION_OPACITY;
        out->opacity = *material->opacity;
    }

    if (material->tint) {
        out->present |= 1u << BOX_RENDER_TRANSITION_TINT;
        out->tint = *material->tint;
    }

    if (material->inherited) {
        out->tinted = true;
        out->inherited = *material->inherited;
    }

    if (material->shape2d && kind == BOX_DRAW_TRANSITION_SHAPE) {
        out->shape2d = *material->shape2d;
    }
}

/** Get a component of a material transition, NULL when the material does not have it.
 *
 * @param material The material
 * @param component The component
 * @param value The value of the component in the material
 * @return value or NULL
 */
static const void* box_draws_transition_part(
    const box_draw_transition_material_t *material,
    box_render_transition_component_t component,
    const void *value)
{
    return (material->present & (1u << component)) ? value : NULL;
}

void box_draws_transition_encode(
    const box_draw_transition_material_t *material,
    void *dst)
{
    box_draw_material_t m = {
        .color = box_draws_transition_part(material, BOX_RENDER_TRANSITION_RGB,
            &material->color),
        .opacity = box_draws_transition_part(material, BOX_RENDER_TRANSITION_OPACITY,
            &material->opacity),
        .cutoff = box_draws_transition_part(material, BOX_RENDER_TRANSITION_ALPHA_CUTOFF,
            &material->cutoff),
        .pbr = box_draws_transition_part(material, BOX_RENDER_TRANSITION_PBR_MATERIAL,
            &material->pbr),
        .emissive = box_draws_transition_part(material, BOX_RENDER_TRANSITION_EMISSIVE,
            &material->emissive),
        .uv = box_draws_transition_part(material, BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM,
            &material->uv),
        .transmission = box_draws_transition_part(material, BOX_RENDER_TRANSITION_TRANSMISSION,
            &material->transmission),
        .clear_coat = box_draws_transition_part(material, BOX_RENDER_TRANSITION_CLEAR_COAT,
            &material->clear_coat),
        .anisotropy = box_draws_transition_part(material, BOX_RENDER_TRANSITION_ANISOTROPY,
            &material->anisotropy),
        .sheen = box_draws_transition_part(material, BOX_RENDER_TRANSITION_SHEEN,
            &material->sheen),
        .diffuse_transmission = box_draws_transition_part(material,
            BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION, &material->diffuse_transmission),
        .tint = box_draws_transition_part(material, BOX_RENDER_TRANSITION_TINT,
            &material->tint),
        .inherited = material->tinted ? &material->inherited : NULL,
        .shape2d = material->kind == BOX_DRAW_TRANSITION_SHAPE ? &material->shape2d : NULL,
        .fade = material->fade
    };
    box_render_material_t record;
    box_render_shading_t shading;
    box_render_mesh_shading_t mesh;
    if (material->kind == BOX_DRAW_TRANSITION_TABLE) {
        box_draws_material_pack(&record, &m);
        ecs_os_memcpy(dst, &record, ECS_SIZEOF(record));
        goto done;
    }

    if (material->kind == BOX_DRAW_TRANSITION_MESH) {
        ecs_os_memcpy(&mesh, dst, ECS_SIZEOF(mesh));
        box_draws_mesh_shading(&mesh, mesh.material, &m);
        ecs_os_memcpy(dst, &mesh, ECS_SIZEOF(mesh));
        goto done;
    }

    if (material->kind == BOX_DRAW_TRANSITION_BOX_SHADOW) {
        box_draws_box_shadow_encode(&m, material, dst);
        goto done;
    }

    box_draws_material(&shading, &m);
    ecs_os_memcpy(ECS_OFFSET(dst, offsetof(box_render_shading_t, color)), shading.color,
        ECS_SIZEOF(shading.color));
    ecs_os_memcpy(ECS_OFFSET(dst, offsetof(box_render_shading_t, emissive)), shading.emissive,
        ECS_SIZEOF(float) * 3);
    if (material->kind == BOX_DRAW_TRANSITION_SHAPE) {
        ecs_os_memcpy(ECS_OFFSET(dst, offsetof(box_render_shading_t, emissive)),
            shading.emissive, ECS_SIZEOF(vec4));
        ecs_os_memcpy(ECS_OFFSET(dst, offsetof(box_render_shading_t, material)),
            shading.material, ECS_SIZEOF(vec4));
        if (!material->shape2d.sliced) {
            ecs_os_memcpy(ECS_OFFSET(dst, offsetof(box_render_shading_t, uv)), shading.uv,
                ECS_SIZEOF(vec4));
        }
    }
done:
    return;
}

void box_draws_transition_fade_own(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_draw_tint_t *tint,
    float *alpha)
{
    const Tint *own_tint = ecs_get(world, e, Tint);
    const Opacity *opacity = ecs_get(world, e, Opacity);
    int32_t i;
    if (own_tint) {
        float a = (float)own_tint->a / 255.0f;
        const uint8_t *own = &own_tint->r;
        for (i = 0; i < 3; i ++) {
            tint->add[i] = tint->add[i] * (1.0f - a) + (float)own[i] / 255.0f * a;
        }

        tint->keep *= 1.0f - a;
    }

    if (opacity) {
        *alpha *= glm_clamp(opacity->value, 0.0f, 1.0f);
    }
}

void box_draws_transition_send_fade(
    const ecs_world_t *world,
    box_render_packet_t *p,
    ecs_entity_t e,
    uint32_t node,
    uint32_t parent,
    const box_draw_tint_t *tint,
    float alpha)
{
    box_render_transition_fade_t fade = {0};
    const Tint *own_tint = ecs_get(world, e, Tint);
    const Opacity *opacity = ecs_get(world, e, Opacity);
    box_draw_tint_t ancestors;
    uint32_t offset = 0;
    if (tint) {
        glm_vec3_copy(ECS_CONST_CAST(float*, tint->add), fade.add);
        fade.keep = tint->keep;
        fade.alpha = alpha;
    } else {
        fade.alpha = box_draws_fade(world, e, &ancestors);
        glm_vec3_copy(ancestors.add, fade.add);
        fade.keep = ancestors.keep;
    }

    if (own_tint) {
        fade.present |= 1u << BOX_RENDER_TRANSITION_TINT;
        fade.tint[0] = own_tint->r;
        fade.tint[1] = own_tint->g;
        fade.tint[2] = own_tint->b;
        fade.tint[3] = own_tint->a;
    }

    if (opacity) {
        fade.present |= 1u << BOX_RENDER_TRANSITION_OPACITY;
        fade.opacity = opacity->value;
    }

    void *dst = box_render_packet_reserve(p, (uint32_t)sizeof(fade), &offset);
    ecs_os_memcpy(dst, &fade, ECS_SIZEOF(fade));
    box_render_transition_record_t *r = box_render_packet_transition(
        p, BOX_RENDER_TRANSITION_FADE, node);
    r->parent = parent;
    r->data = offset;
}
