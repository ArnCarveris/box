/**
 * @file render/draw.c
 * @brief Tracks which shapes changed and keeps them in their instanced batch.
 *
 * Observers collect changed entities and the roots of changed subtrees with the streams their
 * change writes (box_draw_kind_t), so an extract only visits what changed and writes what it
 * changed: a moved shape its transform, a recolored one its shading.
 *
 * Static streams are written once per change, the transforms of DynamicTransform and the
 * shading of DynamicMaterial shapes per frame. A mesh instance references a slot of the
 * material table (materials.c), so a material change packs a slot, not the instances.
 */

#include "draw.h"

ECS_COMPONENT_DECLARE(DrawSlot);

/** Construct DrawSlot values that are not in a batch.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_draw_slot_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    DrawSlot *slots = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        slots[i] = (DrawSlot){
            .batch = -1, .slot = -1, .fade = 1.0f, .tint = { .keep = 1.0f }
        };
    }
}

/** Copy DrawSlot values: a copy is a new entity that has no slot yet.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_draw_slot_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)src;
    box_draw_slot_ctor(dst, count, ti);
}

/** Move DrawSlot values with the entity they belong to.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_draw_slot_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
}

/** Drop a reference to the state, freeing it with the last one.
 *
 * @param ctx The state
 */
static void box_draws_free(
    void *ctx)
{
    box_render_draws_t *d = ctx;
    if (-- d->refs) {
        goto done;
    }

    box_draws_batches_fini(d);
    ecs_map_fini(&d->meshes);
    ecs_vec_fini_t(NULL, &d->released, uint32_t);
    ecs_vec_fini_t(NULL, &d->blended, int32_t);
    ecs_vec_fini_t(NULL, &d->sorted, box_draw_sorted_t);
    ecs_vec_fini_t(NULL, &d->shadow_regions, box_render_shadow_region_t);
    ecs_vec_fini_t(NULL, &d->shadow_materials, uint32_t);
    ecs_vec_fini_t(NULL, &d->dirty, ecs_entity_t);
    ecs_vec_fini_t(NULL, &d->moved, box_draw_moved_t);
    ecs_vec_fini_t(NULL, &d->walk, ecs_entity_t);
    box_draws_order_fini(d);
    box_draws_clips_fini(d);
    box_draws_materials_fini(d);
    ecs_os_free(d);
done:
    return;
}

void box_draws_mark(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    uint8_t kinds)
{
    DrawSlot *s = ecs_get_mut(world, e, DrawSlot);
    if (!s) {
        goto done;
    }

    s->changed |= kinds;
    if (!s->dirty) {
        s->dirty = true;
        *ecs_vec_append_t(NULL, &d->dirty, ecs_entity_t) = e;
    }

done:
    return;
}

/** Mark the entities of an observer event as changed.
 *
 * @param it The iterator of the observer
 * @param kinds box_draw_kind_t, the streams the change writes
 */
static void box_draws_on_change(
    ecs_iter_t *it,
    uint8_t kinds)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_draws_mark(it->world, d, it->entities[i], kinds);
    }

done:
    return;
}

/** Mark the entities of an observer event as changed in every stream: a change of the shape or
 * of its batch key.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnChange(
    ecs_iter_t *it)
{
    box_draws_on_change(it, BOX_DRAW_ALL);
}

/** Mark the entities of an observer event as changed in their shading: a material change.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnShading(
    ecs_iter_t *it)
{
    box_draws_on_change(it, BOX_DRAW_SHADING);
}

/** Remember the entities of an observer event as roots of changed subtrees.
 *
 * @param it The iterator of the observer
 * @param kinds box_draw_kind_t, the streams the change writes for the entities of the subtrees
 */
static void box_draws_on_move(
    ecs_iter_t *it,
    uint8_t kinds)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        *ecs_vec_append_t(NULL, &d->moved, box_draw_moved_t) = (box_draw_moved_t){
            .root = it->entities[i], .kinds = kinds
        };
    }

done:
    return;
}

/** Remember the entities of an observer event as roots of subtrees whose key may have changed.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnMove(
    ecs_iter_t *it)
{
    box_draws_on_move(it, BOX_DRAW_ALL);
}

/** Remember the entities of an observer event as roots of subtrees whose shading changed: the
 * Opacity and the Tint of the ancestors.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnShadingMove(
    ecs_iter_t *it)
{
    box_draws_on_move(it, BOX_DRAW_SHADING);
}

/** Mark the static entities whose WorldTransform was set, in their transform; the descendants
 * of a ManualTransform entity arrive through box_transform_on_moved once their static transform
 * is computed.
 *
 * The DynamicTransform filter runs in the callback, because a Not term on an inherited tag
 * allocates for every event.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnStaticWorld(
    ecs_iter_t *it)
{
    if (!box_transform_dynamic(it->world, it->table)) {
        box_draws_on_change(it, BOX_DRAW_TRANSFORM);
    }
}

/** Mark the entities whose static transform was computed, in their transform.
 *
 * @param world The world
 * @param entities The entities
 * @param count Number of entities
 * @param ctx The state
 */
static void box_draws_on_moved(
    ecs_world_t *world,
    const ecs_entity_t *entities,
    int32_t count,
    void *ctx)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_draws_mark(world, ctx, entities[i], BOX_DRAW_TRANSFORM);
    }
}

/** Remember the entities whose parent changed as roots of changed subtrees, and make the 2D
 * draw order again. A reparented entity writes its transform and its shading: its layer,
 * clip, opacity and tints follow the new ancestors.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnHierarchy(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    d->order_dirty = true;
    box_draws_on_move(it, BOX_DRAW_BOTH);
}

/** Follow an element that got or lost ClipChildren: its descendants take the clip index of
 * their nearest clip again, which their shading holds.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnClip(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        if (it->event == EcsOnAdd) {
            box_draws_clip_added(d);
        } else {
            box_draws_clip_removed(d, it->entities[i]);
        }
    }

    box_draws_on_move(it, BOX_DRAW_SHADING);
done:
    return;
}

/** Make the 2D draw order again when the z it was made from changed.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnPositionZ(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    for (i = 0; !ecs_is_fini(it->world) && i < it->count; i ++) {
        box_draws_order_position(it->world, d, it->entities[i]);
    }
}

/** Make the 2D draw order again after a parent got or lost OrderedChildren.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnOrdered(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    d->order_dirty = true;
}

/** Free the slots of entities that lose their shape, and the slot of the material table a mesh
 * shape owns.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnRemove(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        DrawSlot *s = ecs_get_mut(it->world, it->entities[i], DrawSlot);
        if (s && s->batch >= 0) {
            box_draws_slot_remove(it->world, d, s->batch, s->slot);
            s->batch = -1;
            s->slot = -1;
        }

        box_draws_material_release(it->world, d, it->entities[i]);
    }

done:
    return;
}

/** Free the glyph slots of entities that lose their Text.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnTextRemove(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_draws_text_remove(it->world, d, it->entities[i]);
    }

done:
    return;
}

/** Mark every entity of the changed subtrees, the roots included.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_walk(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&d->moved); i ++) {
        box_draw_moved_t moved = *ecs_vec_get_t(&d->moved, box_draw_moved_t, i);
        if (!ecs_is_alive(world, moved.root)) {
            continue;
        }

        *ecs_vec_append_t(NULL, &d->walk, ecs_entity_t) = moved.root;
        while (ecs_vec_count(&d->walk)) {
            ecs_entity_t e = *ecs_vec_last_t(&d->walk, ecs_entity_t);
            ecs_vec_remove_last(&d->walk);
            box_draws_mark(world, d, e, moved.kinds);
            ecs_iter_t it = ecs_children(world, e);
            while (ecs_children_next(&it)) {
                int32_t c;
                for (c = 0; c < it.count; c ++) {
                    *ecs_vec_append_t(NULL, &d->walk, ecs_entity_t) = it.entities[c];
                }
            }
        }
    }

    ecs_vec_clear(&d->moved);
}

float box_draws_layer(
    const ecs_world_t *world,
    ecs_entity_t e,
    bool glyph)
{
    float result = glyph ? 1.0f : 0.0f;
    bool floating = ecs_has(world, e, LayoutFloat);
    ecs_entity_t cur = ecs_get_parent(world, e);
    while (cur) {
        result += 2.0f;
        floating |= ecs_has(world, cur, LayoutFloat);
        cur = ecs_get_parent(world, cur);
    }

    return floating ? result + BOX_DRAWS_FLOAT_LAYERS : result;
}

float box_draws_fade(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_draw_tint_t *tint)
{
    float result = 1.0f;
    ecs_entity_t cur = ecs_get_parent(world, e);
    *tint = (box_draw_tint_t){ .keep = 1.0f };
    while (cur) {
        const Opacity *opacity = ecs_get(world, cur, Opacity);
        const Tint *t = ecs_get(world, cur, Tint);
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

/** Get the scene of a changed shape, looked up once per table while the changes are processed.
 *
 * Every entity of a table without Scene and without a non fragmenting parent has the scene of
 * the table, and the hierarchy does not change while the changes are processed.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @return The scene, 0 for the default scene
 */
static ecs_entity_t box_draws_scene(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    const ecs_table_t *table = ecs_get_table(world, e);
    ecs_entity_t result = d->scene_of;
    if (!table || table != d->scene_table) {
        result = box_render_scene_of(world, e);
        bool shared = table && !ecs_table_has_id(world, table, ecs_id(Scene)) &&
            !ecs_table_has_id(world, table, ecs_id(EcsParent));
        d->scene_table = shared ? table : NULL;
        d->scene_of = result;
    }

    return result;
}

/** Check whether a scene bakes its lighting or reflections, so captures tell its static shapes
 * apart.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @return Whether the scene root, or for the default scene the singleton, has LightBake or
 *         ReflectionBake
 */
static bool box_draws_baked(
    const ecs_world_t *world,
    ecs_entity_t scene)
{
    bool light = ecs_has(world, scene ? scene : ecs_id(LightBake), LightBake);

    return light || ecs_has(world, scene ? scene : ecs_id(ReflectionBake), ReflectionBake);
}

/** Mark every shape in the batches of the scene of the entities of an observer event, after
 * the scene got or lost LightBake or ReflectionBake, which changes the bake flag of their keys.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnLightBake(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i, b, s;
    for (i = 0; !ecs_is_fini(it->world) && i < it->count; i ++) {
        ecs_entity_t scene = box_render_scene_of(it->world, it->entities[i]);
        for (b = 0; b < ecs_vec_count(&d->batches); b ++) {
            box_draw_batch_t *batch = box_draws_batch(d, b);
            if (!batch->alive || batch->key.scene != scene || batch->key.atlas ||
                batch->key.pipeline != BOX_RENDER_SHADER_MESH)
            {
                continue;
            }

            for (s = 0; s < ecs_vec_count(&batch->entities); s ++) {
                box_draws_mark(it->world, d, *ecs_vec_get_t(&batch->entities, ecs_entity_t, s),
                    BOX_DRAW_ALL);
            }
        }
    }
}

/** Compute the batch key of a shape.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param shape Its mesh
 * @param alpha Resolved alpha of the shape
 * @param key Receives the key
 */
static void box_draws_key(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const ShapeMesh *shape,
    float alpha,
    box_draw_key_t *key)
{
    const BackFaceMode *back_face = ecs_get(world, e, BackFaceMode);
    const PbrTextures *textures = ecs_get(world, e, PbrTextures);
    const TextureWrap *wrap = ecs_get(world, e, TextureWrap);
    const Shape2D *shape2d = ecs_get(world, e, Shape2D);
    ecs_os_memset_t(key, 0, box_draw_key_t);
    key->cutout = box_draws_cutout(ecs_get(world, e, AlphaCutoff), shape2d);
    key->blend = box_draws_blend(ecs_get(world, e, BlendMode), alpha, key->cutout);
    key->back_face = back_face ? (uint8_t)*back_face : (uint8_t)BackFaceCull;
    key->wrap = wrap ? (uint8_t)*wrap : (uint8_t)WrapRepeat;
    if (textures) {
        key->textures[0] = textures->albedo;
        key->textures[1] = textures->emissive;
        key->textures[2] = textures->orm;
        key->textures[3] = textures->normal;
    }

    key->scene = box_draws_scene(world, d, e);
    key->mesh = shape->render_mesh ? shape->render_mesh : shape->mesh;
    key->pipeline = shape2d ? BOX_RENDER_SHADER_SHAPE2D : BOX_RENDER_SHADER_MESH;
    if (shape2d && shape2d->texture) {
        key->textures[0] = shape2d->texture;
    }

    key->dynamic_transform = box_transform_dynamic(world, ecs_get_table(world, e));
    key->dynamic_material = ecs_has_id(world, e, DynamicMaterial) ||
        ecs_get_target_for_id(world, e, EcsChildOf, DynamicMaterial) != 0;
    key->shadow = !shape2d && !ecs_has_id(world, e, NoShadow) &&
        !ecs_get_target_for_id(world, e, EcsChildOf, NoShadow);
    key->bake = !shape2d && key->blend == Opaque && !ecs_has_id(world, e, DynamicTransform) &&
        !ecs_has_id(world, e, ManualTransform) && !ecs_has_id(world, e, NoBake) &&
        !box_projection_anchored(world, e) && box_draws_baked(world, key->scene);
}

/** Record the regions of a caster slot whose instance was written, as bake_change.c records
 * the bounds of changed static shapes; the light extract renders the tiles they reach into.
 *
 * They are the bounds it had and has when its transform or surface changed, or when an alpha
 * tested caster changed its shading, which its alpha test reads; for a new slot the bounds it has.
 *
 * @param world The world
 * @param d The state
 * @param b The batch of the slot
 * @param slot The slot
 * @param was_transform The transform the slot had, NULL for a new slot
 * @param was_surface The surface the slot had, NULL for a new slot
 * @param kinds box_draw_kind_t, the streams the change wrote
 */
static void box_draws_shadow_update(
    const ecs_world_t *world,
    box_render_draws_t *d,
    const box_draw_batch_t *b,
    int32_t slot,
    const box_render_transform_t *was_transform,
    const box_render_surface_t *was_surface,
    uint8_t kinds)
{
    const box_render_transform_t *transform = box_draws_slot_transform(
        ECS_CONST_CAST(box_draw_batch_t*, b), slot);
    const box_render_surface_t *surface = box_draws_slot_surface(
        ECS_CONST_CAST(box_draw_batch_t*, b), slot);
    if (!d->shadow_record || !box_draws_caster(b)) {
        goto done;
    }

    if (!was_transform) {
        box_draws_shadow_region(world, d, b, transform, surface);
    } else if (ecs_os_memcmp_t(was_transform, transform, box_render_transform_t) ||
        ecs_os_memcmp_t(was_surface, surface, box_render_surface_t) ||
        (b->key.cutout && (kinds & BOX_DRAW_SHADING)))
    {
        box_draws_shadow_region(world, d, b, was_transform, was_surface);
        box_draws_shadow_region(world, d, b, transform, surface);
    }

done:
    return;
}

/** Bring the shape of a changed entity into the batch of its key and write its instance.
 *
 * A shape that keeps its slot writes the streams of its change, plus its shading when the clip
 * index of a 2D shape changed; a shape that gets a slot writes every stream.
 *
 * The shading of a mesh shape is the slot of the material table it renders with, its tint and
 * its alpha; a mesh shape that owns a MaterialId gets a slot of its own there, and gives it back
 * when it is no longer drawn by the mesh pipeline.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param s Its DrawSlot
 * @param kinds box_draw_kind_t, the streams the change writes
 */
static void box_draws_update_shape(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    DrawSlot *s,
    uint8_t kinds)
{
    box_draw_key_t key;
    box_draw_batch_t *b;
    const WorldTransform *transform;
    box_draw_material_t material;
    box_render_transform_t was_transform;
    box_render_surface_t was_surface;
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    bool drawn = shape && shape->mesh && !ecs_has_id(world, e, EcsPrefab);
    bool kept = false, recorded = false;
    s->dirty = false;
    d->stats.processed ++;
    if (drawn) {
        s->fade = box_draws_fade(world, e, &s->tint);
        s->tinted = s->tint.keep < 1.0f;
        material = (box_draw_material_t){
            .color = ecs_get(world, e, Rgb),
            .opacity = ecs_get(world, e, Opacity),
            .cutoff = ecs_get(world, e, AlphaCutoff),
            .pbr = ecs_get(world, e, PbrMaterial),
            .emissive = ecs_get(world, e, Emissive),
            .uv = ecs_get(world, e, TextureTransform),
            .shape2d = ecs_get(world, e, Shape2D),
            .tint = ecs_get(world, e, Tint),
            .inherited = s->tinted ? &s->tint : NULL,
            .fade = s->fade
        };
        box_draws_key(world, d, e, shape, box_draws_alpha(&material), &key);
    }

    if (s->batch >= 0) {
        b = box_draws_batch(d, s->batch);
        if (drawn && !ecs_os_memcmp_t(&b->key, &key, box_draw_key_t)) {
            kept = true;
            goto write;
        }

        box_draws_slot_remove(world, d, s->batch, s->slot);
        s->batch = -1;
        s->slot = -1;
    }

    if (!drawn) {
        box_draws_material_release(world, d, e);
        goto done;
    }

    s->batch = box_draws_batch_ensure(d, &key);
    s->slot = box_draws_slot_add(d, s->batch, e);
    b = box_draws_batch(d, s->batch);
    kinds = BOX_DRAW_ALL;
write:
    if (kept && d->shadow_record && box_draws_caster(b)) {
        was_transform = *box_draws_slot_transform(b, s->slot);
        was_surface = *box_draws_slot_surface(b, s->slot);
        recorded = true;
    }

    if (material.shape2d) {
        int32_t clip = box_draws_clip_of(world, d, e);
        if (clip != s->clip) {
            kinds |= BOX_DRAW_SHADING;
        }

        s->layer = box_draws_layer(world, e, false);
        s->clip = clip;
        material.clip = clip;
    }

    if (kinds & BOX_DRAW_TRANSFORM) {
        box_render_transform_t *out = box_draws_slot_transform(b, s->slot);
        transform = ecs_get(world, e, WorldTransform);
        box_draws_transform(out, transform, shape);
        if (material.shape2d) {
            box_draws_lift(out, transform, s->layer);
        }
    }

    if ((kinds & BOX_DRAW_SHADING) && material.shape2d) {
        box_draws_material_release(world, d, e);
        box_draws_material(box_draws_slot_shading(b, s->slot), &material);
    } else if (kinds & BOX_DRAW_SHADING) {
        box_draws_mesh_shading(box_draws_slot_mesh_shading(b, s->slot),
            box_draws_material_id(world, d, e, true), &material);
    }

    if (kinds & BOX_DRAW_SURFACE) {
        box_draws_surface(box_draws_slot_surface(b, s->slot), shape);
    }

    box_draws_shadow_update(world, d, b, s->slot, recorded ? &was_transform : NULL,
        recorded ? &was_surface : NULL, kinds);
    box_draws_slot_changed(b, s->slot, kinds);
done:
    return;
}

/** Bring a changed entity into the batches of its shape and text and write their instances.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 */
static void box_draws_update(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    DrawSlot *s = ecs_is_alive(world, e) ? ecs_get_mut(world, e, DrawSlot) : NULL;
    uint8_t kinds;
    if (!s) {
        goto done;
    }

    kinds = s->changed;
    s->changed = 0;
    box_draws_update_shape(world, d, e, s, kinds);
    if (ecs_has(world, e, TextSlots)) {
        box_draws_text_update(world, d, e, kinds);
    }

done:
    return;
}

/** Bring the changed entities into their batches.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_process(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    int32_t i;
    box_draws_text_dynamic(world, d);
    box_draws_walk(world, d);
    d->scene_table = NULL;
    for (i = 0; i < ecs_vec_count(&d->dirty); i ++) {
        box_draws_update(world, d, *ecs_vec_get_t(&d->dirty, ecs_entity_t, i));
    }

    d->scene_table = NULL;
    ecs_vec_clear(&d->dirty);
}

/** Update the batches with what changed and send what the views of the frame draw.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractDraws(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    ecs_world_t *world = it->world;
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    box_render_packet_t *p = box_render_packet(world);
    const box_render_backend_t *backend = box_render_get_backend(world);
    if (!cache) {
        goto done;
    }

    if (backend->ctx != d->backend || backend->render != d->render ||
        backend->generation != d->generation)
    {
        box_draws_reset_backend(d, cache);
        d->backend = backend->ctx;
        d->render = backend->render;
        d->generation = backend->generation;
    }

    d->stats.processed = 0;
    d->stats.scattered = 0;
    d->stats.scatters = 0;
    d->stats.upload_bytes = 0;
    d->stats.transform_bytes = 0;
    d->stats.shading_bytes = 0;
    d->stats.surface_bytes = 0;
    d->stats.material_bytes = 0;
    box_draws_process(world, d);
    box_draws_clips_send(world, d, p);
    box_draws_order_update(world, d);
    box_draws_send(world, d, p);
    RenderStatus *status = ecs_singleton_get_mut(world, RenderStatus);
    if (status) {
        status->upload_bytes = d->stats.upload_bytes + d->stats.material_bytes;
    }

done:
    return;
}

void box_draws_observer(
    ecs_world_t *world,
    box_render_draws_t *d,
    const char *name,
    ecs_observer_desc_t desc,
    ecs_iter_action_t callback)
{
    desc.entity = ecs_entity(world, { .name = name });
    desc.callback = callback;
    desc.ctx = d;
    desc.ctx_free = box_draws_free;
    d->refs ++;
    ecs_observer_init(world, &desc);
}

/** Register the observers of the material state outside the material table, which write the
 * shading or the batch key of a shape; a new key moves it to a new slot, writing every stream.
 * The material components are followed by box_draws_materials_observers.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_material_observers(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    ecs_entity_t components[3] = {
        ecs_id(BlendMode), ecs_id(BackFaceMode), ecs_id(TextureWrap)
    };
    const char *names[3] = {
        "DrawsOnBlendMode", "DrawsOnBackFaceMode", "DrawsOnTextureWrap"
    };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        box_draws_observer(world, d, names[i], (ecs_observer_desc_t){
            .query.terms = {{ .id = components[i] }},
            .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnShading);
    }

    box_draws_materials_observers(world, d);

    box_draws_observer(world, d, "DrawsOnPbrTextures", (ecs_observer_desc_t){
        .query.terms = {{
            .id = ecs_id(PbrTextures),
            .src.id = EcsSelf | EcsUp,
            .trav = EcsIsA
        }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnShading);

    box_draws_observer(world, d, "DrawsOnOpacity", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Opacity) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnShadingMove);
    box_draws_observer(world, d, "DrawsOnTint", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Tint) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnShadingMove);
}

/** Register the observers that collect changed entities and changed subtrees.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_observers(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    box_draws_observer(world, d, "DrawsOnShape", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(ShapeMesh) }},
        .events = { EcsOnSet } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnShapeRemove", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(ShapeMesh) }},
        .events = { EcsOnRemove } }, BoxDrawsOnRemove);
    box_draws_observer(world, d, "DrawsOnShape2D", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Shape2D) }},
        .events = { EcsOnSet } }, BoxDrawsOnShading);
    box_draws_observer(world, d, "DrawsOnTextGlyphs", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(TextGlyphs) }},
        .events = { EcsOnSet } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnTextRemove", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Text) }},
        .events = { EcsOnRemove } }, BoxDrawsOnTextRemove);
    box_draws_observer(world, d, "DrawsOnTextSpans", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(TextSpans) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnShading);
    box_draws_material_observers(world, d);
    d->refs ++;
    box_transform_on_dynamic(world, BoxDrawsOnChange, d, box_draws_free);
    box_draws_observer(world, d, "DrawsOnWorldTransform", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet } }, BoxDrawsOnStaticWorld);
    box_draws_observer(world, d, "DrawsOnDynamicMaterial", (ecs_observer_desc_t){
        .query.terms = {{ .id = DynamicMaterial }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnMove);
    box_draws_observer(world, d, "DrawsOnNoBake", (ecs_observer_desc_t){
        .query.terms = {{ .id = NoBake }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnManualTransform", (ecs_observer_desc_t){
        .query.terms = {{ .id = ManualTransform }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnLightBake", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(LightBake) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnLightBake);
    box_draws_observer(world, d, "DrawsOnReflectionBake", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(ReflectionBake) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnLightBake);
    box_draws_observer(world, d, "DrawsOnNoShadow", (ecs_observer_desc_t){
        .query.terms = {{ .id = NoShadow }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnMove);
    box_draws_observer(world, d, "DrawsOnScene", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Scene) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnMove);
    box_draws_observer(world, d, "DrawsOnChildOf", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_pair(EcsChildOf, EcsWildcard) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnHierarchy);
    box_draws_observer(world, d, "DrawsOnParent", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(EcsParent) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnHierarchy);
    box_draws_observer(world, d, "DrawsOnClipChildren", (ecs_observer_desc_t){
        .query.terms = {{ .id = ClipChildren }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnClip);
    box_draws_observer(world, d, "DrawsOnLayoutFloat", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(LayoutFloat) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnHierarchy);
    box_draws_observer(world, d, "DrawsOnOrderedChildren", (ecs_observer_desc_t){
        .query.terms = {{ .id = EcsOrderedChildren }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnOrdered);
}

/** Follow static transforms: the 2D draw order on Position changes, and the entities whose
 * static transform was computed through box_transform_on_moved.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_transform_observers(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    box_draws_observer(world, d, "DrawsOnPositionZ", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Position) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnPositionZ);
    d->refs ++;
    box_transform_on_moved(world, box_draws_on_moved, d, box_draws_free);
}

/** Create the queries of the dynamic shapes.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_queries(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    d->dynamic_transform = ecs_query(world, {
        .terms = {
            { .id = ecs_id(ShapeMesh), .inout = EcsIn },
            { .id = ecs_id(DrawSlot), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Rgb), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Opacity), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(PbrMaterial), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Emissive), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(BlendMode), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(TextureTransform), .inout = EcsIn, .oper = EcsOptional },
            { .id = DynamicTransform, .inout = EcsInOutNone },
            {
                .id = DynamicMaterial,
                .src.id = EcsSelf | EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone,
                .oper = EcsOptional
            },
            { .id = ecs_id(Shape2D), .src.id = EcsSelf, .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(AlphaCutoff), .inout = EcsIn, .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    d->dynamic_material = ecs_query(world, {
        .terms = {
            { .id = ecs_id(ShapeMesh), .inout = EcsIn },
            { .id = ecs_id(DrawSlot), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Rgb), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Opacity), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(PbrMaterial), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Emissive), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(BlendMode), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(TextureTransform), .inout = EcsIn, .oper = EcsOptional },
            {
                .id = DynamicMaterial,
                .src.id = EcsSelf | EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone
            },
            {
                .id = DynamicTransform,
                .src.id = EcsSelf,
                .inout = EcsInOutNone,
                .oper = EcsNot
            },
            { .id = ecs_id(Shape2D), .src.id = EcsSelf, .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(AlphaCutoff), .inout = EcsIn, .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });
}

void box_render_draws_init(
    ecs_world_t *world)
{
    box_render_draws_t *d = ecs_os_calloc_t(box_render_draws_t);
    box_draws_tables_init();
    ECS_COMPONENT_DEFINE(world, DrawSlot);
    ecs_set_hooks(world, DrawSlot, {
        .ctor = box_draw_slot_ctor,
        .copy = box_draw_slot_copy,
        .move = box_draw_slot_move
    });
    ecs_add_pair(world, ecs_id(ShapeMesh), EcsWith, ecs_id(DrawSlot));

    ecs_vec_init_t(NULL, &d->batches, box_draw_batch_t, 0);
    ecs_vec_init_t(NULL, &d->free_batches, int32_t, 0);
    ecs_vec_init_t(NULL, &d->dirty, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &d->moved, box_draw_moved_t, 0);
    ecs_vec_init_t(NULL, &d->walk, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &d->released, uint32_t, 0);
    ecs_vec_init_t(NULL, &d->blended, int32_t, 0);
    ecs_vec_init_t(NULL, &d->sorted, box_draw_sorted_t, 0);
    ecs_vec_init_t(NULL, &d->shadow_regions, box_render_shadow_region_t, 0);
    ecs_vec_init_t(NULL, &d->shadow_materials, uint32_t, 0);
    ecs_map_init(&d->lookup, NULL);
    ecs_map_init(&d->meshes, NULL);
    box_draws_order_init(d);
    box_draws_clips_init(d);
    box_draws_materials_init(d);
    box_draws_store_init(&d->stores[BOX_DRAWS_STORE_MESH], &(box_draw_layout_t){
        .streams = BOX_DRAWS_STREAMS,
        .stride = {
            [BOX_RENDER_STREAM_TRANSFORM] = (uint32_t)sizeof(box_render_transform_t),
            [BOX_RENDER_STREAM_SHADING] = (uint32_t)sizeof(box_render_mesh_shading_t),
            [BOX_RENDER_STREAM_SURFACE] = (uint32_t)sizeof(box_render_surface_t)
        }
    }, BOX_RENDER_BUFFER_FIRST, BOX_DRAWS_CHUNK);
    box_draws_store_init(&d->stores[BOX_DRAWS_STORE_2D], &(box_draw_layout_t){
        .streams = BOX_DRAWS_STREAMS,
        .stride = {
            [BOX_RENDER_STREAM_TRANSFORM] = (uint32_t)sizeof(box_render_transform_t),
            [BOX_RENDER_STREAM_SHADING] = (uint32_t)sizeof(box_render_shading_t),
            [BOX_RENDER_STREAM_SURFACE] = (uint32_t)sizeof(box_render_surface_t)
        }
    }, BOX_RENDER_BUFFER_FIRST_2D, BOX_DRAWS_CHUNK);
    d->chunk = BOX_DRAWS_CHUNK;
    d->refs = 1;
    box_draws_queries(world, d);
    box_draws_text_init(world, d);
    box_draws_observers(world, d);
    box_draws_transform_observers(world, d);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractDraws" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractDraws,
        .ctx = d,
        .ctx_free = box_draws_free
    });
}

/** Get the state of the draw extract of a world.
 *
 * @param world The world
 * @return The state, NULL without the draw extract
 */
static box_render_draws_t* box_draws_state(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;

    return s ? s->ctx : NULL;
}

ecs_vec_t* box_render_draws_shadow_regions(
    const ecs_world_t *world)
{
    box_render_draws_t *d = box_draws_state(world);

    return d ? &d->shadow_regions : NULL;
}

void box_render_draws_shadow_record(
    const ecs_world_t *world,
    bool record)
{
    box_render_draws_t *d = box_draws_state(world);
    if (d) {
        d->shadow_record = record;
        if (!record) {
            ecs_vec_clear(&d->shadow_regions);
            ecs_vec_clear(&d->shadow_materials);
        }
    }
}

void box_render_draws_stats(
    const ecs_world_t *world,
    box_render_draws_stats_t *stats)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    ecs_os_memset_t(stats, 0, box_render_draws_stats_t);
    if (s && s->ctx) {
        *stats = ((const box_render_draws_t*)s->ctx)->stats;
    }
}
