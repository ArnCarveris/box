/**
 * @file render/instance.c
 * @brief Tracks which entities need extraction and manages static instance slots.
 */

#include "render_internal.h"

void box_render_mark_dirty(
    ecs_world_t *world,
    ecs_entity_t e)
{
    RenderState *state = ecs_get_mut(world, ecs_id(RenderState), RenderState);
    if (!state) {
        goto done;
    }

    RenderInstance *inst = ecs_get_mut(world, e, RenderInstance);
    ecs_assert(inst != NULL, ECS_INTERNAL_ERROR, "RenderMesh entity without RenderInstance");
    if (!inst->dirty) {
        inst->dirty = true;
        ecs_vec_append_t(NULL, &state->dirty, ecs_entity_t)[0] = e;
    }
done:
    return;
}

void box_render_mark_all(
    ecs_world_t *world,
    RenderState *state)
{
    ecs_iter_t it = ecs_query_iter(world, state->instances);
    while (ecs_query_next(&it)) {
        RenderInstance *inst = ecs_field(&it, RenderInstance, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!inst[i].dirty) {
                inst[i].dirty = true;
                ecs_vec_append_t(NULL, &state->dirty, ecs_entity_t)[0] = it.entities[i];
            }
        }
    }
}

void box_render_mark_subtree(
    ecs_world_t *world,
    ecs_entity_t e)
{
    if (ecs_has(world, e, RenderMesh)) {
        box_render_mark_dirty(world, e);
    }

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_render_mark_subtree(world, it.entities[i]);
        }
    }
}

/** A component that feeds instance data was set.
 *
 * @param it Iterator over the changed entities
 */
static void box_render_on_change(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_render_mark_dirty(it->world, it->entities[i]);
    }
}

/** A dynamic tag was added or removed: re-evaluate the subtree.
 *
 * @param it Iterator over the changed entities
 */
static void box_render_on_dynamic(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_render_mark_subtree(it->world, it->entities[i]);
    }
}

/** An instance record is being removed: free its static slot.
 *
 * @param it Iterator over the removed entities
 */
static void box_render_on_remove(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    RenderInstance *inst = ecs_field(it, RenderInstance, 0);
    int32_t i;
    for (i = 0; state && i < it->count; i ++) {
        if (box_render_has_slot(state, it->entities[i], &inst[i])) {
            box_render_release_slot(it->world, state, it->entities[i], &inst[i]);
        }

        box_render_set_transparent(it->world, state, it->entities[i], &inst[i], false);
    }
}

/** A component that derives instance uvs exists, so extract reads them.
 *
 * @param it Iterator over the entities that gained the component
 */
static void box_render_on_tiling(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (state) {
        state->tiled = true;
    }
}

/** A blend or back face mode exists, so extract reads it from changed entities.
 *
 * @param it Iterator over the entities that gained the component
 */
static void box_render_on_blended(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (state) {
        state->blended = true;
    }
}

/** An opacity or tint exists, so extract resolves the alpha of changed entities.
 *
 * @param it Iterator over the entities that gained the component
 */
static void box_render_on_translucent(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (state) {
        state->translucent = true;
    }
}

/** Register an OnAdd observer that flags the world as holding tiling components.
 *
 * Prefabs are matched too, because a tiling component often lives on a prefab
 * and is only inherited by the instances that carry the mesh.
 *
 * @param world The world
 * @param component The component to watch
 */
static void box_render_watch_tiling(
    ecs_world_t *world,
    ecs_entity_t component)
{
    ecs_observer(world, {
        .query.terms = {{ .id = component }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnAdd },
        .callback = box_render_on_tiling
    });
}

/** Register an OnAdd observer that sets a registry flag, prefabs included.
 *
 * @param world The world
 * @param component The component to watch
 * @param callback The observer body
 */
static void box_render_watch_flag(
    ecs_world_t *world,
    ecs_entity_t component,
    ecs_iter_action_t callback)
{
    ecs_observer(world, {
        .query.terms = {{ .id = component }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnAdd },
        .callback = callback
    });
}

/** Register an OnSet observer for a component on entities with a mesh.
 *
 * @param world The world
 * @param component The component to watch
 */
static void box_render_watch(
    ecs_world_t *world,
    ecs_entity_t component)
{
    ecs_observer(world, {
        .query.terms = {
            { .id = component },
            { .id = ecs_id(RenderMesh), .inout = EcsInOutNone }
        },
        .events = { EcsOnSet },
        .callback = box_render_on_change
    });
}

/** Convert a color and tint to linear rgba.
 *
 * @param color The base color, NULL for white
 * @param tint The resolved tint, NULL for none
 * @param opacity The opacity
 * @param out Four floats receiving the result
 */
static void box_render_color(
    const Rgb *color,
    const ActualTint *tint,
    float opacity,
    float *out)
{
    out[0] = color ? box_srgb_to_linear(color->r) : 1.0f;
    out[1] = color ? box_srgb_to_linear(color->g) : 1.0f;
    out[2] = color ? box_srgb_to_linear(color->b) : 1.0f;
    out[3] = opacity;
    if (tint) {
        out[0] *= (float)tint->r / 255.0f;
        out[1] *= (float)tint->g / 255.0f;
        out[2] *= (float)tint->b / 255.0f;
        out[3] *= (float)tint->a / 255.0f;
    }
}

/** Resolve a texture slot entity to the handle of its uploaded texture.
 *
 * @param world The world
 * @param slot The texture entity, 0 for none
 * @return The texture handle, 0 when the slot binds no texture
 */
static uint32_t box_render_texture_of(
    ecs_world_t *world,
    ecs_entity_t slot)
{
    uint32_t result = 0;
    if (slot && ecs_is_alive(world, slot)) {
        const RenderTexture *rt = ecs_get(world, slot, RenderTexture);
        result = rt ? rt->handle : 0;
    }

    return result;
}

/** Write the texture handles of the PBR slots into instance data.
 *
 * @param world The world
 * @param e The entity
 * @param out The instance to fill
 */
static void box_render_fill_textures(
    ecs_world_t *world,
    ecs_entity_t e,
    box_instance_t *out)
{
    const PbrTextures *tex = ecs_get(world, e, PbrTextures);
    if (!tex) {
        goto done;
    }

    box_instance_set_texture(out, 0, box_render_texture_of(world, tex->albedo));
    box_instance_set_texture(out, 1, box_render_texture_of(world, tex->emissive));
    box_instance_set_texture(out, 2, box_render_texture_of(world, tex->roughness));
    box_instance_set_texture(out, 3, box_render_texture_of(world, tex->normal));
done:
    return;
}

void box_render_instance_bounds(
    const RenderMesh *mesh,
    const mat4 *m,
    float *out)
{
    vec3 center = vec3_scale(vec3_add(mesh->bounds_min, mesh->bounds_max), 0.5f);
    vec3 extent = vec3_scale(vec3_sub(mesh->bounds_max, mesh->bounds_min), 0.5f);
    float radius = 0;
    int axis;
    for (axis = 0; axis < 3; axis ++) {
        float x = m->v[axis];
        float y = m->v[4 + axis];
        float z = m->v[8 + axis];
        float e = fabsf(x) * extent.x + fabsf(y) * extent.y + fabsf(z) * extent.z;
        radius += e * e;
        out[axis] = x * center.x + y * center.y + z * center.z + m->v[12 + axis];
    }

    out[3] = sqrtf(radius);
}

void box_render_fill_instance(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    const WorldTransform *transform,
    bool baked,
    bool textured,
    bool tiled,
    const RenderState *layers,
    box_instance_t *out)
{
    mat4 scale = mat4_scaling(mesh->scale);
    mat4 m = mat4_mul(&transform->m, &scale);
    int row;
    for (row = 0; row < 3; row ++) {
        out->transform[row * 4 + 0] = m.v[0 + row];
        out->transform[row * 4 + 1] = m.v[4 + row];
        out->transform[row * 4 + 2] = m.v[8 + row];
        out->transform[row * 4 + 3] = m.v[12 + row];
    }

    const Opacity *opacity = ecs_get(world, e, Opacity);
    box_render_color(ecs_get(world, e, Rgb), ecs_get(world, e, ActualTint),
        opacity ? opacity->value : 1.0f, out->color);

    const Emissive *emissive = ecs_get(world, e, Emissive);
    float strength = emissive ? emissive->strength : 0.0f;
    out->emissive[0] = emissive ? box_srgb_to_linear(emissive->color.r) * strength : 0.0f;
    out->emissive[1] = emissive ? box_srgb_to_linear(emissive->color.g) * strength : 0.0f;
    out->emissive[2] = emissive ? box_srgb_to_linear(emissive->color.b) * strength : 0.0f;
    out->emissive[3] = strength;

    const PbrMaterial *pbr = ecs_get(world, e, PbrMaterial);
    out->params[0] = pbr ? pbr->metallic : 0.0f;
    out->params[1] = pbr ? pbr->roughness : 0.5f;
    out->params[2] = baked ? 1.0f : 0.0f;
    out->params[3] = 0.0f;

    memset(out->textures, 0, sizeof(out->textures));
    if (textured) {
        box_render_fill_textures(world, e, out);
    }

    box_render_fill_uv(world, e, mesh, &m, tiled, out);
    if (layers) {
        box_instance_set_layers(out, box_render_layer_mask(world, layers, e));
    }

    box_render_instance_bounds(mesh, &m, out->bounds);
}

void box_render_instance_init(
    ecs_world_t *world)
{
    box_render_watch(world, ecs_id(RenderMesh));
    box_render_watch(world, ecs_id(WorldTransform));
    box_render_watch(world, ecs_id(Rgb));
    box_render_watch(world, ecs_id(Opacity));
    box_render_watch(world, ecs_id(ActualTint));
    box_render_watch(world, ecs_id(PbrMaterial));
    box_render_watch(world, ecs_id(Emissive));
    box_render_watch(world, ecs_id(PbrTextures));
    box_render_watch(world, ecs_id(BlendMode));
    box_render_watch(world, ecs_id(BackFaceMode));
    box_render_watch(world, ecs_id(TextureTransform));
    box_render_watch(world, ecs_id(TextureTiling));
    box_render_watch(world, ecs_id(TextureWrap));
    box_render_watch_tiling(world, ecs_id(TextureTransform));
    box_render_watch_tiling(world, ecs_id(TextureTiling));
    box_render_watch_tiling(world, ecs_id(TextureWrap));
    box_render_watch_flag(world, ecs_id(BlendMode), box_render_on_blended);
    box_render_watch_flag(world, ecs_id(BackFaceMode), box_render_on_blended);
    box_render_watch_flag(world, ecs_id(Opacity), box_render_on_translucent);
    box_render_watch_flag(world, ecs_id(Tint), box_render_on_translucent);
    box_render_watch_flag(world, ecs_id(ActualTint), box_render_on_translucent);

    ecs_observer(world, {
        .query.terms = {{ .id = DynamicTransform }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_render_on_dynamic
    });

    ecs_observer(world, {
        .query.terms = {{ .id = DynamicMaterial }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_render_on_dynamic
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(RenderInstance) }},
        .events = { EcsOnRemove },
        .callback = box_render_on_remove
    });
}
