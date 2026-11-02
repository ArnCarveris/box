/**
 * @file render/transition_material.c
 * @brief The target material of a material transition and the records it encodes again.
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
        out->anisotropic = true;
        out->anisotropy = *material->anisotropy;
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
        .anisotropy = material->anisotropic ? &material->anisotropy : NULL,
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
        ecs_os_memcpy(ECS_OFFSET(dst, offsetof(box_render_shading_t, uv)), shading.uv,
            ECS_SIZEOF(vec4));
    }
done:
    return;
}
