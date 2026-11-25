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

    if (material->mapping) {
        out->mapped = true;
        out->mapping = *material->mapping;
    }

    if (material->scroll) {
        out->scrolled = true;
        out->scroll = *material->scroll;
    }

    if (material->fresnel) {
        out->present |= 1u << BOX_RENDER_TRANSITION_FRESNEL;
        out->fresnel = *material->fresnel;
    }

    if (material->distortion) {
        out->present |= 1u << BOX_RENDER_TRANSITION_DISTORTION;
        out->distortion = *material->distortion;
    }

    if (material->water) {
        out->watered = true;
        out->water = *material->water;
        out->water_waves = material->water_waves;
        if (material->flow) {
            out->flowed = true;
            out->flow = *material->flow;
        }

        if (material->caustics) {
            out->causticked = true;
            out->caustics = *material->caustics;
            out->caustics_frame[0] = material->caustics_frame[0];
            out->caustics_frame[1] = material->caustics_frame[1];
        }
    }

    if (material->sway) {
        out->swayed = true;
        out->sway = *material->sway;
        out->wind = material->wind;
    }

    if (material->reveal) {
        out->revealed = true;
        out->reveal = *material->reveal;
    }

    if (material->occlusion) {
        out->occluded = true;
        out->occlusion = *material->occlusion;
    }

    out->time = material->time;
    out->origin[0] = material->origin[0];
    out->origin[1] = material->origin[1];
    out->origin[2] = material->origin[2];

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

    if (material->shape2d && material->border && material->shape2d->kind != BOX_SHAPE2D_LINE &&
        (kind == BOX_DRAW_TRANSITION_SHAPE || kind == BOX_DRAW_TRANSITION_BOX_SHADOW))
    {
        out->present |= 1u << BOX_RENDER_TRANSITION_BORDER;
        out->border = *material->border;
        out->border_radius = material->border_radius;
    }
}

void box_draws_transition_shape(
    const box_draw_transition_material_t *material,
    Shape2D *out)
{
    *out = material->shape2d;
    if (material->present & (1u << BOX_RENDER_TRANSITION_BORDER)) {
        if (material->border_radius) {
            out->radius = 0.0f;
        }

        box_shape2d_border(out, &material->border);
    }
}

void* box_draws_transition_value(
    box_draw_transition_material_t *material,
    box_render_transition_component_t component)
{
    void *result = NULL;
    switch (component) {
    case BOX_RENDER_TRANSITION_RGB: result = &material->color; break;
    case BOX_RENDER_TRANSITION_ALPHA_CUTOFF: result = &material->cutoff; break;
    case BOX_RENDER_TRANSITION_PBR_MATERIAL: result = &material->pbr; break;
    case BOX_RENDER_TRANSITION_EMISSIVE: result = &material->emissive; break;
    case BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM: result = &material->uv; break;
    case BOX_RENDER_TRANSITION_TRANSMISSION: result = &material->transmission; break;
    case BOX_RENDER_TRANSITION_CLEAR_COAT: result = &material->clear_coat; break;
    case BOX_RENDER_TRANSITION_ANISOTROPY: result = &material->anisotropy; break;
    case BOX_RENDER_TRANSITION_SHEEN: result = &material->sheen; break;
    case BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION:
        result = &material->diffuse_transmission;
        break;
    case BOX_RENDER_TRANSITION_FRESNEL: result = &material->fresnel; break;
    case BOX_RENDER_TRANSITION_DISTORTION: result = &material->distortion; break;
    case BOX_RENDER_TRANSITION_OPACITY: result = &material->opacity; break;
    case BOX_RENDER_TRANSITION_TINT: result = &material->tint; break;
    case BOX_RENDER_TRANSITION_BORDER: result = &material->border; break;
    case BOX_RENDER_TRANSITION_POSITION:
    case BOX_RENDER_TRANSITION_ROTATION:
    case BOX_RENDER_TRANSITION_SCALE:
    case BOX_RENDER_TRANSITION_BOX_SHADOW:
    case BOX_RENDER_TRANSITION_POINT_LIGHT:
    case BOX_RENDER_TRANSITION_SPOT_LIGHT:
    case BOX_RENDER_TRANSITION_COMPONENTS:
        break;
    }

    return result;
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

/** Point the material components of a record at the values of its target material.
 *
 * @param material The target material
 * @param shape Receives the 2D shape of a shape record with its Border applied
 *        (box_draws_transition_shape), which shape2d points at
 * @param out Receives the material components
 */
static void box_draws_transition_parts(
    const box_draw_transition_material_t *material,
    Shape2D *shape,
    box_draw_material_t *out)
{
    if (material->kind == BOX_DRAW_TRANSITION_SHAPE) {
        box_draws_transition_shape(material, shape);
    }

    *out = (box_draw_material_t){
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
        .mapping = material->mapped ? &material->mapping : NULL,
        .scroll = material->scrolled ? &material->scroll : NULL,
        .time = material->time,
        .origin = { material->origin[0], material->origin[1], material->origin[2] },
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
        .fresnel = box_draws_transition_part(material, BOX_RENDER_TRANSITION_FRESNEL,
            &material->fresnel),
        .distortion = box_draws_transition_part(material, BOX_RENDER_TRANSITION_DISTORTION,
            &material->distortion),
        .water = material->watered ? &material->water : NULL,
        .water_waves = material->watered ? material->water_waves : 0u,
        .flow = material->watered && material->flowed ? &material->flow : NULL,
        .caustics = material->watered && material->causticked ? &material->caustics : NULL,
        .caustics_frame = { material->caustics_frame[0], material->caustics_frame[1] },
        .sway = material->swayed ? &material->sway : NULL,
        .wind = material->wind,
        .reveal = material->revealed ? &material->reveal : NULL,
        .occlusion = material->occluded ? &material->occlusion : NULL,
        .tint = box_draws_transition_part(material, BOX_RENDER_TRANSITION_TINT,
            &material->tint),
        .inherited = material->tinted ? &material->inherited : NULL,
        .shape2d = material->kind == BOX_DRAW_TRANSITION_SHAPE ? shape : NULL,
        .fade = material->fade
    };
}

void box_draws_transition_encode(
    const box_draw_transition_material_t *material,
    void *dst)
{
    box_draw_material_t m;
    box_render_material_t record;
    box_render_shading_t shading;
    box_render_mesh_shading_t mesh;
    Shape2D shape;
    box_draws_transition_parts(material, &shape, &m);
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
        if (material->shape2d.kind != BOX_SHAPE2D_POLYGON) {
            ecs_os_memcpy(ECS_OFFSET(dst, offsetof(box_render_shading_t, emissive)),
                shading.emissive, ECS_SIZEOF(vec4));
        }

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

void box_draws_transition_box_shadow(
    const box_draw_transition_box_shadow_t *record,
    const BoxShadow *shadow,
    mat4 world,
    const double *origin,
    void *transform,
    void *shading)
{
    box_draw_transition_box_shadow_t patched = *record;
    box_draw_material_t m;
    Shape2D shape;
    box_draws_transition_parts(&record->material, &shape, &m);
    if (record->material.present & (1u << BOX_RENDER_TRANSITION_BORDER)) {
        box_draws_transition_shape(&record->material, &patched.material.shape2d);
        patched.material.shadow_cover = box_draws_box_shadow_cover(
            &patched.material.shape2d, record->material.shadow_blend, 1.0f);
    }

    box_draws_box_shadow_patch(&m, &patched, shadow, world, origin, transform, shading);
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
    float alpha,
    const Tint *leaving_tint,
    const Opacity *leaving_opacity)
{
    box_render_transition_fade_t fade = {0};
    const Tint *own_tint = ecs_get(world, e, Tint);
    const Opacity *opacity = ecs_get(world, e, Opacity);
    own_tint = own_tint ? own_tint : leaving_tint;
    opacity = opacity ? opacity : leaving_opacity;
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
