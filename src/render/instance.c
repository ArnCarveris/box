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

    box_render_lod_touch(world, state, e);
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

/** Mark one child and its descendants for extraction.
 *
 * @param world The world
 * @param e The child
 * @param ctx Unused
 */
static void box_render_mark_child(
    ecs_world_t *world,
    ecs_entity_t e,
    void *ctx)
{
    (void)ctx;

    box_render_mark_subtree(world, e);
}

void box_render_mark_subtree(
    ecs_world_t *world,
    ecs_entity_t e)
{
    if (ecs_has(world, e, RenderMesh)) {
        box_render_mark_dirty(world, e);
    }

    box_hierarchy_children(world, e, box_render_mark_child, NULL);
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

/** An inherited switch was added or removed: re-evaluate the subtree.
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

/** A NoShadow tag exists, so extract resolves the caster flag of changed entities.
 *
 * @param it Iterator over the entities that gained the tag
 */
static void box_render_on_shadowless(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (state) {
        state->shadowless = true;
    }
}

/** A Hidden tag exists, so extract resolves the visibility of changed entities.
 *
 * @param it Iterator over the entities that gained the tag
 */
static void box_render_on_hidden(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (state) {
        state->hide_used = true;
    }
}

/** A Wear exists, so extract packs the layer amounts of changed entities.
 *
 * @param it Iterator over the entities that gained the component
 */
static void box_render_on_worn(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (state) {
        state->worn = true;
    }
}

/** A 2D shape exists, so extract resolves the shape words of changed entities.
 *
 * @param it Iterator over the entities that gained the component
 */
static void box_render_on_shaped(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (state) {
        state->shaped = true;
    }
}

/** A clip rectangle exists, so extract resolves the clip words of instances.
 *
 * @param it Iterator over the entities that gained a clip rectangle
 */
static void box_render_on_clipped(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (state) {
        state->clipped = true;
    }
}

/** A transition exists, so extract resolves the displayed values of instances.
 *
 * @param it Iterator over the entities that gained the state
 */
static void box_render_on_transitioned(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (state) {
        state->transitioned = true;
    }
}

/** An opacity exists, so extract resolves the alpha of changed entities.
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
 * The tint mixes the color toward its own by its strength, so the alpha of the
 * instance is the opacity alone.
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
        float a = (float)tint->a / 255.0f;
        out[0] += ((float)tint->r / 255.0f - out[0]) * a;
        out[1] += ((float)tint->g / 255.0f - out[1]) * a;
        out[2] += ((float)tint->b / 255.0f - out[2]) * a;
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

/** Write the packed 2D shape parameters of an entity into instance data.
 *
 * The radii and widths of the shape are lengths in the local space of the
 * rectangle, which the fragment stage never sees; it reads them as ratios of
 * the smaller half extent of the instance, the same length the rounded corner
 * of the shape is bound by. The half extents themselves travel as ratios of the
 * larger one, which is all the fragment stage needs to keep a corner round.
 *
 * @param world The world
 * @param e The entity
 * @param mesh Its render mesh
 * @param m Its instance matrix, the world transform times the shape scale
 * @param out The instance to fill
 */
static void box_render_fill_shape(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    const mat4 *m,
    box_instance_t *out)
{
    const RenderShape2D *shape = ecs_get(world, e, RenderShape2D);
    if (!shape) {
        goto done;
    }

    vec3 extent = vec3_sub(mesh->bounds_max, mesh->bounds_min);
    float span[2] = { extent.x, extent.y };
    float half[2];
    int32_t axis;
    for (axis = 0; axis < 2; axis ++) {
        vec3 col = vec3_new(m->v[axis * 4], m->v[axis * 4 + 1], m->v[axis * 4 + 2]);

        half[axis] = vec3_length(col) * span[axis] * 0.5f;
    }

    float small = half[0] < half[1] ? half[0] : half[1];
    float large = half[0] > half[1] ? half[0] : half[1];
    if (small <= 0.0f || large <= 0.0f) {
        goto done;
    }

    float radii[4];
    float widths[4];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        radii[i] = shape->radii[i] / small;
        widths[i] = shape->widths[i] / small;
    }

    rgba color = shape->color;
    if (!color.a) {
        const Rgb *base = ecs_get(world, e, Rgb);

        color.r = base ? base->r : 255;
        color.g = base ? base->g : 255;
        color.b = base ? base->b : 255;
        color.a = 255;
    }

    out->shape[0] = box_shape_pack4(radii);
    out->shape[1] = box_shape_pack4(widths);
    out->shape[2] = (uint32_t)color.r | ((uint32_t)color.g << 8) |
        ((uint32_t)color.b << 16) | ((uint32_t)color.a << 24);
    out->shape[3] = box_shape_pack2(half[0] / large, half[1] / large);
    box_instance_set_shape(out, true);
    box_instance_set_soft(out, ecs_has_id(world, e, RenderSoftShape));
done:
    return;
}

/** Write the world space clip rectangle of an entity into instance data.
 *
 * @param world The world
 * @param e The entity
 * @param out The instance to fill
 */
static void box_render_fill_clip(
    ecs_world_t *world,
    ecs_entity_t e,
    box_instance_t *out)
{
    const RenderClip *clip = ecs_get(world, e, RenderClip);
    if (!clip) {
        goto done;
    }

    box_instance_set_clip(out, clip->min[0], clip->min[1],
        clip->max[0], clip->max[1]);
done:
    return;
}

void box_render_set_clip(
    ecs_world_t *world,
    ecs_entity_t e,
    const float *min,
    const float *max)
{
    RenderClip *clip = ecs_get_mut(world, e, RenderClip);
    if (!clip) {
        ecs_set(world, e, RenderClip, {
            .min = { min[0], min[1] }, .max = { max[0], max[1] }
        });
        goto done;
    }

    if (clip->min[0] == min[0] && clip->min[1] == min[1] &&
        clip->max[0] == max[0] && clip->max[1] == max[1])
    {
        goto done;
    }

    clip->min[0] = min[0];
    clip->min[1] = min[1];
    clip->max[0] = max[0];
    clip->max[1] = max[1];
    ecs_modified(world, e, RenderClip);
done:
    return;
}

void box_render_clear_clip(
    ecs_world_t *world,
    ecs_entity_t e)
{
    if (ecs_has(world, e, RenderClip)) {
        ecs_remove(world, e, RenderClip);
        box_render_mark_dirty(world, e);
    }
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

bool box_render_casts_shadow(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return !box_hierarchy_ancestor_has(world, e, NoShadow);
}

bool box_render_is_hidden(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const RenderState *state = ecs_id(RenderState) ?
        ecs_singleton_get(world, RenderState) : NULL;

    return state != NULL && state->hide_used &&
        box_hierarchy_ancestor_has(world, e, Hidden);
}

void box_render_set_depth(
    ecs_world_t *world,
    ecs_entity_t e,
    float depth)
{
    RenderInstance *inst = ecs_get_mut(world, e, RenderInstance);
    if (!inst || inst->depth == depth) {
        goto done;
    }

    inst->depth = depth;
    box_render_state(world)->depth_used = true;
    box_render_mark_dirty(world, e);
done:
    return;
}

const TransitionState* box_render_transition(
    const ecs_world_t *world,
    const RenderState *state,
    ecs_entity_t e)
{
    return state->transitioned ? ecs_get(world, e, TransitionState) : NULL;
}

/** Read a component value, replaced by the value a transition displays.
 *
 * @param transition The transitions of the entity, NULL when it has none
 * @param component The component
 * @param time The time the transitions are displayed at
 * @param value The authored value, NULL when the entity has no such component
 * @param buffer Scratch of the size of the component
 * @param size Size of the component
 * @return The value to read, which is the buffer while a transition moves it
 */
static const void* box_render_displayed(
    const TransitionState *transition,
    ecs_entity_t component,
    double time,
    const void *value,
    void *buffer,
    size_t size)
{
    const void *result = value;
    if (!transition || !value) {
        goto done;
    }

    memcpy(buffer, value, size);
    if (box_transition_display(transition, component, time, buffer)) {
        result = buffer;
    }

done:
    return result;
}

/** Compose the world transform an entity displays from its transitioned pose.
 *
 * The pose components keep the values the author assigned, so the matrix the
 * transform module wrote is not the one a view shows while a transition moves
 * one of them: the local pose is composed again from what is displayed and put
 * back under the world transform of the parent.
 *
 * @param world The world
 * @param e The entity
 * @param transition The transitions of the entity
 * @param time The time the transitions are displayed at
 * @param out Receives the displayed world transform
 * @return true when a transition moves the pose of the entity
 */
static bool box_render_display_transform(
    ecs_world_t *world,
    ecs_entity_t e,
    const TransitionState *transition,
    double time,
    WorldTransform *out)
{
    bool result = false;
    if (ecs_has_id(world, e, ManualTransform)) {
        goto done;
    }

    Position position;
    Rotation rotation;
    Scale scale;
    const Position *p = ecs_get(world, e, Position);
    const Rotation *r = ecs_get(world, e, Rotation);
    const Scale *s = ecs_get(world, e, Scale);
    const Orientation *o = ecs_get(world, e, Orientation);

    p = box_render_displayed(transition, ecs_id(Position), time, p,
        &position, sizeof(Position));
    r = box_render_displayed(transition, ecs_id(Rotation), time, r,
        &rotation, sizeof(Rotation));
    s = box_render_displayed(transition, ecs_id(Scale), time, s,
        &scale, sizeof(Scale));
    result = p == &position || r == &rotation || s == &scale;
    if (!result) {
        goto done;
    }

    mat4 parent = mat4_identity();
    ecs_entity_t ancestor = box_hierarchy_parent(world, e);
    const WorldTransform *above = ancestor ?
        ecs_get(world, ancestor, WorldTransform) : NULL;
    if (above) {
        parent = above->m;
    }

    mat4 local = box_transform_compose(p, r, s, o);

    out->m = mat4_mul(&parent, &local);
done:
    return result;
}

/** Pack the per entity layer amounts of an entity into the wear words.
 *
 * The two components are read on their own, so an entity that only says how
 * burnt it is keeps the noise of its layers where the scene put it and the
 * other way around.
 *
 * @param world The world
 * @param e The entity
 * @param transition The transitions of the entity, NULL when it has none
 * @param time The time the transitions are displayed at
 * @param out The instance to fill
 */
static void box_render_fill_wear(
    ecs_world_t *world,
    ecs_entity_t e,
    const TransitionState *transition,
    double time,
    box_instance_t *out)
{
    Wear wear_value;
    WearDetail detail_value;
    const Wear *wear = box_render_displayed(transition, ecs_id(Wear), time,
        ecs_get(world, e, Wear), &wear_value, sizeof(Wear));
    const WearDetail *detail = box_render_displayed(transition, ecs_id(WearDetail),
        time, ecs_get(world, e, WearDetail), &detail_value, sizeof(WearDetail));
    if (!wear && !detail) {
        goto done;
    }

    float amounts[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float seed = 0.0f;
    float scale = 1.0f;
    if (wear) {
        amounts[0] = wear->dust;
        amounts[1] = wear->grime;
        amounts[2] = wear->edge;
        amounts[3] = wear->wet;
    }

    if (detail) {
        amounts[4] = detail->burn;
        amounts[5] = detail->scratch;
        seed = detail->seed;
        scale = detail->scale;
    }

    box_instance_set_wear(out, amounts, seed, scale);
done:
    return;
}

void box_render_fill_instance(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    const WorldTransform *transform,
    bool baked,
    bool textured,
    bool tiled,
    bool shaped,
    bool worn,
    bool clipped,
    const RenderState *layers,
    const TransitionState *transition,
    double time,
    float depth,
    box_instance_t *out)
{
    WorldTransform displayed;
    if (transition && box_render_display_transform(
        world, e, transition, time, &displayed))
    {
        transform = &displayed;
    }

    mat4 scale = mat4_scaling(mesh->scale);
    mat4 m = mat4_mul(&transform->m, &scale);
    m.v[14] += depth;

    int row;
    for (row = 0; row < 3; row ++) {
        out->transform[row * 4 + 0] = m.v[0 + row];
        out->transform[row * 4 + 1] = m.v[4 + row];
        out->transform[row * 4 + 2] = m.v[8 + row];
        out->transform[row * 4 + 3] = m.v[12 + row];
    }

    Opacity opacity_value;
    Rgb rgb_value;
    ActualTint tint_value;
    Emissive emissive_value;
    const Opacity *opacity = box_render_displayed(transition, ecs_id(Opacity),
        time, ecs_get(world, e, Opacity), &opacity_value, sizeof(Opacity));
    const Rgb *rgb = box_render_displayed(transition, ecs_id(Rgb),
        time, ecs_get(world, e, Rgb), &rgb_value, sizeof(Rgb));
    const ActualTint *tint = box_render_displayed(transition, ecs_id(ActualTint),
        time, ecs_get(world, e, ActualTint), &tint_value, sizeof(ActualTint));

    float alpha = opacity ? opacity->value : 1.0f;
    if (opacity && ecs_has_id(world, e, RenderGrouped)) {
        alpha = 1.0f;
    }

    box_render_color(rgb, tint, alpha, out->color);

    const Emissive *emissive = box_render_displayed(transition, ecs_id(Emissive),
        time, ecs_get(world, e, Emissive), &emissive_value, sizeof(Emissive));
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
    memset(out->shape, 0, sizeof(out->shape));
    if (shaped) {
        box_render_fill_shape(world, e, mesh, &m, out);
    }

    memset(out->wear, 0, sizeof(out->wear));
    if (worn) {
        box_render_fill_wear(world, e, transition, time, out);
    }

    memset(out->clip, 0, sizeof(out->clip));
    if (clipped) {
        box_render_fill_clip(world, e, out);
    }

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
    box_render_watch(world, ecs_id(RenderShape2D));
    box_render_watch(world, ecs_id(RenderClip));
    box_render_watch(world, ecs_id(Wear));
    box_render_watch(world, ecs_id(WearDetail));
    box_render_watch_flag(world, ecs_id(TransitionState), box_render_on_transitioned);
    box_render_watch_tiling(world, ecs_id(TextureTransform));
    box_render_watch_tiling(world, ecs_id(TextureTiling));
    box_render_watch_tiling(world, ecs_id(TextureWrap));
    box_render_watch_flag(world, ecs_id(BlendMode), box_render_on_blended);
    box_render_watch_flag(world, ecs_id(BackFaceMode), box_render_on_blended);
    box_render_watch_flag(world, ecs_id(Opacity), box_render_on_translucent);
    box_render_watch_flag(world, NoShadow, box_render_on_shadowless);
    box_render_watch_flag(world, Hidden, box_render_on_hidden);
    box_render_watch_flag(world, ecs_id(RenderShape2D), box_render_on_shaped);
    box_render_watch_flag(world, ecs_id(RenderClip), box_render_on_clipped);
    box_render_watch_flag(world, ecs_id(Wear), box_render_on_worn);
    box_render_watch_flag(world, ecs_id(WearDetail), box_render_on_worn);

    ecs_observer(world, {
        .query.terms = {{ .id = NoShadow }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_render_on_dynamic
    });

    ecs_observer(world, {
        .query.terms = {{ .id = Hidden }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_render_on_dynamic
    });

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

    ecs_add_id(world, ecs_id(Wear), Transitionable);
    ecs_add_id(world, ecs_id(WearDetail), Transitionable);
}
