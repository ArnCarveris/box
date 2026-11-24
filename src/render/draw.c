/**
 * @file render/draw.c
 * @brief Tracks which shapes changed and keeps them in their instanced batch.
 *
 * Observers collect changed entities and the roots of changed subtrees with the streams their
 * change writes (box_draw_kind_t), so an extract only visits what changed and writes what it
 * changed: a moved shape its transform, a recolored one its shading.
 *
 * Static streams are written once per change, the transforms of DynamicTransform and the
 * shading of DynamicMaterial shapes per frame. A mesh instance references a slot of the material
 * table (materials.c), so a material change packs a slot or moves its owner to another one.
 */

#include "draw.h"
#include "../terrain/terrain.h"

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
    ecs_vec_fini_t(NULL, &d->swaying, ecs_entity_t);
    ecs_vec_fini_t(NULL, &d->shadow_materials, uint32_t);
    ecs_vec_fini_t(NULL, &d->dirty, ecs_entity_t);
    ecs_vec_fini_t(NULL, &d->moved, box_draw_moved_t);
    ecs_vec_fini_t(NULL, &d->walk, box_draw_walk_t);
    ecs_vec_fini_t(NULL, &d->fade_links, box_draw_fade_link_t);
    ecs_vec_fini_t(NULL, &d->planets, ecs_entity_t);
    box_draws_order_fini(d);
    box_draws_clips_fini(d);
    box_draws_materials_fini(d);
    box_draws_movers_fini(d);
    box_draws_cards_fini(d);
    ecs_os_free(d);
done:
    return;
}

/** Add a shape to the changed entities, as box_draws_mark.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param kinds box_draw_kind_t, the streams the change writes
 * @return Its DrawSlot, NULL when it has none
 */
static DrawSlot* box_draws_mark_slot(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    uint8_t kinds)
{
    DrawSlot *s = ecs_get_mut(world, e, DrawSlot);
    box_draws_card_mark(d, e, kinds);
    if (!s) {
        goto done;
    }

    s->changed |= (kinds & BOX_DRAW_FADE) ? kinds : (uint8_t)(kinds | BOX_DRAW_KEY);
    if (!s->dirty) {
        s->dirty = true;
        *ecs_vec_append_t(NULL, &d->dirty, ecs_entity_t) = e;
    }

done:
    return s;
}

void box_draws_mark(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    uint8_t kinds)
{
    box_draws_mark_slot(world, d, e, kinds);
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
    box_draws_on_move(it, BOX_DRAW_SHADING | BOX_DRAW_FADE);
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
 * draw order again. A reparented entity writes its transform and its shading: its clip,
 * opacity and tints follow the new ancestors, its rank the new order.
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

/** Remember the entities that got or lost TextureFrame as roots of subtrees whose instances
 * project from another frame: the MappingFrame shapes below write their frame stream, the
 * MappingObject shapes below join or leave a frame batch.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnTextureFrame(
    ecs_iter_t *it)
{
    box_draws_on_move(it, BOX_DRAW_FRAME);
}

/** Follow an element that got or lost LayoutFloat, or whose clip or hide_clipped changed.
 *
 * A float leaves the clips of its ancestors and draws after the rest, a clipped overlay keeps the
 * clips and draws after its siblings, and a float with hide_clipped gives its subtree its own
 * clip index. The subtree takes its clip index again and the order is made again.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnLayoutFloat(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        const LayoutFloat *fl = it->event == EcsOnRemove ? NULL :
            ecs_get(it->world, e, LayoutFloat);
        uint64_t flags = fl ? (fl->clip ? 1u : (fl->hide_clipped ? 2u : 0u)) : 0u;
        const uint64_t *found = ecs_map_get(&d->overlays, e);
        uint64_t was = found ? *found : 0;
        if (it->event == EcsOnSet && flags == was) {
            continue;
        }

        if (flags) {
            ecs_map_ensure(&d->overlays, e)[0] = flags;
        } else {
            ecs_map_remove(&d->overlays, e);
        }

        d->order_dirty |= it->event != EcsOnSet || (flags & 1u) != (was & 1u);
        *ecs_vec_append_t(NULL, &d->moved, box_draw_moved_t) = (box_draw_moved_t){
            .root = e, .kinds = BOX_DRAW_BOTH
        };
    }

done:
    return;
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

/** Free the slots of entities that lose their shape, the slot of their shadow, and the slot of
 * the material table a mesh shape owns.
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
        box_draws_box_shadow_remove(it->world, d, it->entities[i]);
        box_draws_slice_release(d, it->entities[i]);
        box_draws_polygon_release(d, it->entities[i]);
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

void box_draws_fade_step(
    const Opacity *opacity,
    const Tint *t,
    float *fade,
    box_draw_tint_t *tint)
{
    if (opacity) {
        *fade *= glm_clamp(opacity->value, 0.0f, 1.0f);
    }

    if (t) {
        float a = (float)t->a / 255.0f;
        float weight = a * tint->keep / 255.0f;
        tint->add[0] += (float)t->r * weight;
        tint->add[1] += (float)t->g * weight;
        tint->add[2] += (float)t->b * weight;
        tint->keep *= 1.0f - a;
    }
}

/** Compose the fade of an entity from the chain of its ancestors with an Opacity or a Tint, in
 * the order of box_draws_fade, so the result is the same.
 *
 * @param d The state
 * @param link Index of the first box_draw_fade_link_t, -1 for none
 * @param tint Receives the tints of the ancestors
 * @return The product of the Opacity of the ancestors
 */
static float box_draws_fade_chain(
    const box_render_draws_t *d,
    int32_t link,
    box_draw_tint_t *tint)
{
    float result = 1.0f;
    *tint = (box_draw_tint_t){ .keep = 1.0f };
    while (link >= 0) {
        const box_draw_fade_link_t *l = ecs_vec_get_t(&d->fade_links, box_draw_fade_link_t, link);
        box_draws_fade_step(l->has_opacity ? &l->opacity : NULL, l->has_tint ? &l->tint : NULL,
            &result, tint);
        link = l->next;
    }

    return result;
}

/** Add an entity in front of a chain of ancestors when it has an Opacity or a Tint.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param next Index of the chain of its ancestors, -1 for none
 * @return Index of the chain of the children of the entity
 */
static int32_t box_draws_fade_link(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    int32_t next)
{
    const Opacity *opacity = ecs_get(world, e, Opacity);
    const Tint *t = ecs_get(world, e, Tint);
    int32_t result = next;
    if (opacity || t) {
        box_draw_fade_link_t *l = ecs_vec_append_t(NULL, &d->fade_links, box_draw_fade_link_t);
        *l = (box_draw_fade_link_t){
            .opacity = opacity ? *opacity : (Opacity){0},
            .tint = t ? *t : (Tint){0},
            .has_opacity = opacity != NULL,
            .has_tint = t != NULL,
            .next = next
        };
        result = ecs_vec_count(&d->fade_links) - 1;
    }

    return result;
}

/** Make the chain of the ancestors of a subtree root with an Opacity or a Tint, from the
 * nearest one up.
 *
 * @param world The world
 * @param d The state
 * @param e The root
 * @return Index of the first link, -1 for none
 */
static int32_t box_draws_fade_root(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    int32_t result = -1, last = -1;
    ecs_entity_t cur = ecs_get_parent(world, e);
    while (cur) {
        int32_t link = box_draws_fade_link(world, d, cur, -1);
        if (link >= 0) {
            if (last >= 0) {
                ecs_vec_get_t(&d->fade_links, box_draw_fade_link_t, last)->next = link;
            } else {
                result = link;
            }

            last = link;
        }

        cur = ecs_get_parent(world, cur);
    }

    return result;
}

/** Mark every entity of the changed subtrees, the roots included.
 *
 * The walk carries the Opacity and Tint of the ancestors down, so each shape gets its fade
 * (BOX_DRAW_FADE in DrawSlot.changed) without walking up the hierarchy again.
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

        ecs_vec_clear(&d->fade_links);
        *ecs_vec_append_t(NULL, &d->walk, box_draw_walk_t) = (box_draw_walk_t){
            .e = moved.root, .fade = box_draws_fade_root(world, d, moved.root)
        };
        while (ecs_vec_count(&d->walk)) {
            box_draw_walk_t cur = *ecs_vec_last_t(&d->walk, box_draw_walk_t);
            int32_t children = -2;
            ecs_vec_remove_last(&d->walk);
            DrawSlot *s = box_draws_mark_slot(world, d, cur.e, moved.kinds);
            if (s) {
                s->fade = box_draws_fade_chain(d, cur.fade, &s->tint);
                s->changed |= BOX_DRAW_FADE;
            }

            ecs_iter_t it = ecs_children(world, cur.e);
            while (ecs_children_next(&it)) {
                int32_t c;
                if (it.count && children == -2) {
                    children = box_draws_fade_link(world, d, cur.e, cur.fade);
                }

                for (c = 0; c < it.count; c ++) {
                    *ecs_vec_append_t(NULL, &d->walk, box_draw_walk_t) = (box_draw_walk_t){
                        .e = it.entities[c], .fade = children
                    };
                }
            }
        }
    }

    ecs_vec_clear(&d->moved);
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
        box_draws_fade_step(ecs_get(world, cur, Opacity), ecs_get(world, cur, Tint), &result,
            tint);
        cur = ecs_get_parent(world, cur);
    }

    return result;
}

uint32_t box_draws_planet_find(
    const box_render_draws_t *d,
    ecs_entity_t planet)
{
    uint32_t result = 0;
    int32_t i;
    for (i = 0; planet && i < ecs_vec_count(&d->planets); i ++) {
        if (*ecs_vec_get_t(&d->planets, ecs_entity_t, i) == planet) {
            result = (uint32_t)i + 1u;
            break;
        }
    }

    return result;
}

uint32_t box_draws_planet_slot(
    box_render_draws_t *d,
    ecs_entity_t planet)
{
    uint32_t result = box_draws_planet_find(d, planet);
    int32_t i;
    if (result || !planet) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&d->planets); i ++) {
        ecs_entity_t *slot = ecs_vec_get_t(&d->planets, ecs_entity_t, i);
        if (!*slot) {
            *slot = planet;
            result = (uint32_t)i + 1u;
            goto done;
        }
    }

    if (ecs_vec_count(&d->planets) < BOX_RENDER_PLANET_LIGHT_MAX - 1) {
        *ecs_vec_append_t(NULL, &d->planets, ecs_entity_t) = planet;
        result = (uint32_t)ecs_vec_count(&d->planets);
    }

done:
    return result;
}

ecs_entity_t box_draws_planet_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    ecs_entity_t cur = e;
    if (!ecs_id(PlanetAtmosphere) || !ecs_id_in_use(world, ecs_id(PlanetAtmosphere))) {
        goto done;
    }

    while (cur) {
        if (ecs_has_id(world, cur, ecs_id(PlanetAtmosphere))) {
            result = cur;
            break;
        }

        cur = ecs_get_parent(world, cur);
    }

done:
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

/** Set the bake flag of a key, so captures tell static shapes apart: an opaque shape that
 * doesn't move, in a scene whose root (or the singleton) has LightBake or ReflectionBake.
 *
 * @param world The world
 * @param e The entity
 * @param allowed Whether the shape can bake at all, false for 2D shapes
 * @param key The key, its scene and blend computed
 */
static void box_draws_key_bake(
    ecs_world_t *world,
    ecs_entity_t e,
    bool allowed,
    box_draw_key_t *key)
{
    key->bake = allowed && key->blend == Opaque && !ecs_has_id(world, e, DynamicTransform) &&
        !ecs_has_id(world, e, TransitionTransform) && !ecs_has_id(world, e, ManualTransform) &&
        !ecs_has_id(world, e, NoBake) && !box_projection_anchored(world, e) &&
        (ecs_has(world, key->scene ? key->scene : ecs_id(LightBake), LightBake) ||
         ecs_has(world, key->scene ? key->scene : ecs_id(ReflectionBake), ReflectionBake));
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
                !box_render_is_mesh(batch->key.pipeline))
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

/** Mark every shape in the mesh batches of a scene whose Scene was set, which changes the
 * features, the pipeline and the fallbacks of their keys, and their material records (TextureScroll
 * and Water follow their flags). The singleton is the Scene of the default scene.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnSceneFeatures(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i, b, s;
    for (i = 0; !ecs_is_fini(it->world) && i < it->count; i ++) {
        ecs_entity_t scene = it->entities[i] == ecs_id(Scene) ? 0 : it->entities[i];
        for (b = 0; b < ecs_vec_count(&d->batches); b ++) {
            box_draw_batch_t *batch = box_draws_batch(d, b);
            if (!batch->alive || batch->key.scene != scene || batch->key.atlas ||
                !box_render_is_mesh(batch->key.pipeline))
            {
                continue;
            }

            for (s = 0; s < ecs_vec_count(&batch->entities); s ++) {
                ecs_entity_t e = *ecs_vec_get_t(&batch->entities, ecs_entity_t, s);
                box_draws_material_changed(d, e);
                box_draws_mark(it->world, d, e, BOX_DRAW_ALL);
            }
        }
    }
}

ecs_entity_t box_draws_blend_owner(
    const ecs_world_t *world,
    ecs_entity_t e,
    const box_draw_key_t *key)
{
    ecs_entity_t result = 0;
    if (box_render_is_mesh(key->pipeline) && ecs_id_in_use(world, ecs_id(TileTerrain)) &&
        ecs_owns(world, e, TileTerrain))
    {
        result = e;
        goto done;
    }

    if (!box_render_is_mesh(key->pipeline) || !ecs_id_in_use(world, ecs_id(TextureBlend)) ||
        !ecs_get(world, e, TextureBlend))
    {
        goto done;
    }

    const TerrainTile *tile = ecs_id(TerrainTile) ? ecs_get(world, e, TerrainTile) : NULL;
    result = tile && tile->terrain ? tile->terrain
        : ecs_get_target_for_id(world, e, EcsIsA, ecs_id(TextureBlend));
done:
    return result;
}

/** Compute the batch key of a shape.
 *
 * A shape under an active Opacity transition (TransitionOpacity) is keyed as if its alpha were
 * below 1. A 3D shape draws with the mesh shader box_draws_shader picks, with the features
 * box_draws_features gives; the Scene features that are off are ignored.
 *
 * A 3D shape that has or inherits Transmission, Distortion (box_draws_distorted), or Water
 * with Scene.water (without its TextureBlend), is transmissive: it draws in the sorted blended
 * pass, is not alpha tested and casts no shadow, whatever BlendMode, Opacity and AlphaCutoff.
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
    bool fading = ecs_has_id(world, e, TransitionOpacity);
    ecs_os_memset_t(key, 0, box_draw_key_t);
    key->scene = box_draws_scene(world, d, e);
    key->features = shape2d ? 0u : box_draws_features(world, e, key->scene);
    key->cutout = box_draws_scene_cutout(ecs_get(world, e, AlphaCutoff),
        ecs_get(world, e, VertexReveal), shape2d, key->features);
    key->blend = box_draws_scene_blend(ecs_get(world, e, BlendMode), fading ? 0.0f : alpha,
        key->cutout, shape2d, key->features);
    key->back_face = back_face ? (uint8_t)*back_face : (uint8_t)BackFaceCull;
    key->wrap = wrap ? (uint8_t)*wrap : (uint8_t)WrapRepeat;
    if (textures) {
        key->textures[0] = textures->albedo;
        key->textures[1] = textures->emissive;
        key->textures[2] = textures->orm;
        key->textures[3] = textures->normal;
    }

    key->mesh = shape->render_mesh ? shape->render_mesh : shape->mesh;
    key->pipeline = shape2d ? BOX_RENDER_SHADER_SHAPE2D : BOX_RENDER_SHADER_MESH;
    key->surface = shape->surface.p[0][0] != 0.0f;
    if (!shape2d) {
        const TextureMapping *mapping = ecs_get(world, e, TextureMapping);
        key->frame = mapping && (mapping->mode == MappingFrame ||
            (mapping->mode == MappingObject && box_render_texture_frame(world, e)));
    }

    if (shape2d && shape2d->texture) {
        key->textures[0] = shape2d->texture;
        key->slice = shape2d->sliced;
    }

    key->polygon = shape2d && shape2d->kind == BOX_SHAPE2D_POLYGON;

    const ecs_table_t *table = ecs_get_table(world, e);
    bool transition = ecs_has_id(world, e, TransitionTransform);
    key->mover = !shape2d && box_draws_mover_table(world, table, false,
        ecs_pair(EcsIsA, DynamicTransformSpread));
    key->dynamic_transform = !key->mover &&
        (box_transform_dynamic(world, table) || transition);
    key->dynamic_material = ecs_has_id(world, e, DynamicMaterial) ||
        ecs_has_id(world, e, TransitionMaterial) || fading ||
        ecs_get_target_for_id(world, e, EcsChildOf, DynamicMaterial) != 0;
    key->shadow = !shape2d && !ecs_has_id(world, e, NoShadow) &&
        !ecs_get_target_for_id(world, e, EcsChildOf, NoShadow);
    key->fixture = key->shadow && (ecs_has_id(world, e, LightFixture) ||
        ecs_get_target_for_id(world, e, EcsChildOf, LightFixture) != 0);
    key->wind = !shape2d && (key->features & BOX_RENDER_FEATURE_WIND) &&
        ecs_get(world, e, WindSway) != NULL;
    if (!shape2d) {
        box_draws_range_of(world, d, e, key->range);
    }

    key->transmissive = !shape2d && (key->features & BOX_RENDER_FEATURE_TRANSMISSION) &&
        ecs_get(world, e, Transmission) != NULL;
    key->transmissive = key->transmissive ||
        (!shape2d && box_draws_distorted(world, e, key->features));
    const Water *water = shape2d || !(key->features & BOX_RENDER_FEATURE_WATER)
        ? NULL : ecs_get(world, e, Water);
    if (water) {
        key->water = true;
        key->transmissive = true;
        key->foam = water->foam_texture;
    }

    if (key->transmissive) {
        key->blend = AlphaBlend;
        key->cutout = false;
        key->shadow = false;
        key->fixture = false;
    }

    ecs_entity_t blend = shape2d || key->water ||
        !(key->features & BOX_RENDER_FEATURE_TEXTURE_BLEND)
        ? 0 : box_draws_blend_owner(world, e, key);
    if (blend) {
        key->texture_blend = true;
        key->textures[0] = blend;
        key->textures[2] = 0;
        key->textures[3] = 0;
    }

    if (!shape2d) {
        key->pipeline = box_draws_shader(world, d, e, key);
    }

    const TextureLayers *layers = shape2d || (key->transmissive && !key->water) ||
        !box_render_is_mesh(key->pipeline) || !(key->features & BOX_RENDER_FEATURE_TEXTURE_LAYERS)
        ? NULL : ecs_get(world, e, TextureLayers);
    if (layers) {
        ecs_os_memcpy_n(key->layers, layers->layers, ecs_entity_t, BOX_RENDER_LAYERS);
    }

    bool planet_light = key->features & BOX_RENDER_FEATURE_PLANET_LIGHT;
    if (!shape2d && (planet_light || key->blend != Opaque)) {
        ecs_entity_t planet = box_draws_planet_of(world, e);
        key->planet = planet && planet_light ? box_draws_planet_slot(d, planet) : 0u;
        key->atmosphere = key->blend != Opaque ? planet : 0;
    }

    box_draws_key_bake(world, e, !shape2d, key);
}

/** Update the fields of the key of a 3D shape that follow its blend and its alpha test, for a
 * change that keeps the rest of its key (BOX_DRAW_FADE): as box_draws_key computes them.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param material Its material
 * @param key The key of its batch, updated
 */
static void box_draws_key_blend(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const box_draw_material_t *material,
    box_draw_key_t *key)
{
    bool fading = ecs_has_id(world, e, TransitionOpacity);
    bool planet_light = key->features & BOX_RENDER_FEATURE_PLANET_LIGHT;
    key->cutout = box_draws_scene_cutout(material->cutoff, material->reveal, NULL,
        key->features);
    key->blend = box_draws_scene_blend(ecs_get(world, e, BlendMode),
        fading ? 0.0f : box_draws_alpha(material), key->cutout, NULL, key->features);
    key->planet = 0;
    key->atmosphere = 0;
    if (planet_light || key->blend != Opaque) {
        ecs_entity_t planet = box_draws_planet_of(world, e);
        key->planet = planet && planet_light ? box_draws_planet_slot(d, planet) : 0u;
        key->atmosphere = key->blend != Opaque ? planet : 0;
    }

    box_draws_key_bake(world, e, true, key);
}

/** Check whether a shape whose change is its material slot, or the Opacity or Tint of an
 * ancestor, keeps the key of its batch: the blend and the alpha test of the key, which
 * box_draws_key resolves from its alpha and AlphaCutoff, are the only fields they change.
 *
 * @param world The world
 * @param e The entity
 * @param key The key of its batch
 * @param material Its material, with the fade of this extract
 * @return Whether the key is unchanged
 */
static bool box_draws_blend_kept(
    const ecs_world_t *world,
    ecs_entity_t e,
    const box_draw_key_t *key,
    const box_draw_material_t *material)
{
    bool result = key->transmissive;
    if (!result) {
        bool fading = ecs_has_id(world, e, TransitionOpacity);
        bool cutout = box_draws_scene_cutout(material->cutoff, material->reveal,
            material->shape2d, key->features);
        result = cutout == key->cutout && box_draws_scene_blend(ecs_get(world, e, BlendMode),
            fading ? 0.0f : box_draws_alpha(material), cutout, material->shape2d,
            key->features) == key->blend;
    }

    return result;
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
 * @param was_surface The surface the slot had, NULL for a new slot or a batch without the
 * surface stream
 * @param kinds box_draw_kind_t, the streams the change wrote
 */
static void box_draws_shadow_update(
    const ecs_world_t *world,
    box_render_draws_t *d,
    box_draw_batch_t *b,
    int32_t slot,
    const box_render_transform_t *was_transform,
    const box_render_surface_t *was_surface,
    uint8_t kinds)
{
    const box_render_transform_t *transform = box_draws_slot_transform(b, slot);
    const box_render_surface_t *surface = box_draws_slot_surface(b, slot);
    box_render_transform_t was_world, now_world;
    const uint32_t *mover = box_draws_slot_mover(b, slot);
    if (!d->shadow_record || !box_draws_caster(b)) {
        goto done;
    }

    if (!was_transform) {
        box_draws_shadow_slot(world, d, b, slot);
    } else if (ecs_os_memcmp_t(was_transform, transform, box_render_transform_t) ||
        (surface && ecs_os_memcmp_t(was_surface, surface, box_render_surface_t)) ||
        (b->key.cutout && (kinds & BOX_DRAW_SHADING)))
    {
        if (mover) {
            const box_render_transform_t *pose =
                &box_draws_mover(d, *mover & BOX_RENDER_MOVER_INDEX)->pose;
            box_draws_compose(&was_world, pose, was_transform);
            box_draws_compose(&now_world, pose, transform);
            was_transform = &was_world;
            transform = &now_world;
        }

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
 * The shading of a mesh shape is its slot of the material table, its tint and its alpha. A mesh
 * shape that owns a MaterialId takes its slot here, private in a DynamicMaterial batch, and gives
 * it back when it is no longer drawn by the mesh pipeline.
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
    box_draw_material_t material;
    box_render_transform_t was_transform;
    box_render_surface_t was_surface;
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    WorldTransform transform_buf;
    bool drawn = shape && shape->mesh && ecs_has(world, e, WorldTransform) &&
        !ecs_has_id(world, e, EcsPrefab) &&
        !ecs_has_id(world, e, EcsDisabled);
    bool kept = false, recorded = false, surfaced = false, keyed = kinds & BOX_DRAW_KEY;
    bool faded = false;
    s->dirty = false;
    d->stats.processed ++;
    if (drawn) {
        if (!(kinds & BOX_DRAW_FADE)) {
            s->fade = box_draws_fade(world, e, &s->tint);
        }

        s->tinted = s->tint.keep < 1.0f;
        if (!(kinds & BOX_DRAW_KEY) && s->batch >= 0 &&
            box_render_is_mesh(box_draws_batch(d, s->batch)->key.pipeline))
        {
            b = box_draws_batch(d, s->batch);
            faded = (kinds & (BOX_DRAW_ALL | BOX_DRAW_SLOT)) == BOX_DRAW_SHADING;
            material = (box_draw_material_t){
                .opacity = faded ? ecs_get(world, e, Opacity) : NULL,
                .cutoff = faded ? ecs_get(world, e, AlphaCutoff) : NULL,
                .reveal = faded ? ecs_get(world, e, VertexReveal) : NULL,
                .tint = faded ? ecs_get(world, e, Tint) : NULL,
                .inherited = s->tinted ? &s->tint : NULL, .fade = s->fade
            };
            kept = (kinds & BOX_DRAW_ALL) == BOX_DRAW_TRANSFORM ||
                (faded && box_draws_blend_kept(world, e, &b->key, &material));
            if (kept) {
                goto write;
            }

            faded = false;
        }

        box_draws_shape_material(world, e, s, &material);
        if (!(kinds & BOX_DRAW_KEY) && s->batch >= 0) {
            b = box_draws_batch(d, s->batch);
            if (box_draws_blend_kept(world, e, &b->key, &material)) {
                kept = true;
                goto write;
            }
        }

        if (!(kinds & BOX_DRAW_KEY) && s->batch >= 0 && !material.shape2d &&
            box_render_is_mesh(box_draws_batch(d, s->batch)->key.pipeline) &&
            !box_draws_batch(d, s->batch)->key.transmissive)
        {
            key = box_draws_batch(d, s->batch)->key;
            box_draws_key_blend(world, d, e, &material, &key);
        } else {
            box_draws_key(world, d, e, shape, box_draws_alpha(&material), &key);
        }
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
    kinds &= BOX_DRAW_ALL;
    if ((kinds & BOX_DRAW_TRANSFORM) || keyed) {
        kinds |= BOX_DRAW_FRAME;
    }

    if (kinds & (BOX_DRAW_TRANSFORM | BOX_DRAW_FRAME)) {
        kinds |= BOX_DRAW_MOVER;
    }

    kinds &= box_draws_batch_kinds(b);
    if (kept && d->shadow_record && box_draws_caster(b)) {
        const box_render_surface_t *surface = box_draws_slot_surface(b, s->slot);
        was_transform = *box_draws_slot_transform(b, s->slot);
        if (surface) {
            was_surface = *surface;
        }

        recorded = true;
        surfaced = surface != NULL;
    }

    if (material.shape2d) {
        int32_t clip = box_draws_clip_of(world, d, e);
        if (clip != s->clip) {
            kinds |= BOX_DRAW_SHADING;
        }

        s->clip = clip;
        material.clip = clip;
        material.rank = s->rank;
        bool polygon = material.shape2d->kind == BOX_SHAPE2D_POLYGON;
        int32_t slice = material.shape2d->sliced ? box_draws_slice_of(d, e) :
            polygon ? box_draws_polygon_of(world, d, e, kinds & BOX_DRAW_SHADING) : 0;
        if (slice != s->slice) {
            kinds |= BOX_DRAW_SHADING;
        }

        if (s->slice && !material.shape2d->sliced) {
            box_draws_slice_release(d, e);
        }

        if (s->slice && !polygon) {
            box_draws_polygon_release(d, e);
        }

        s->slice = slice;
        material.slice = slice;
    }

    box_render_transform_t model;
    if (kinds & (BOX_DRAW_TRANSFORM | BOX_DRAW_FRAME)) {
        box_draws_transform(&model, box_transform_world(world, e, &transform_buf), shape);
    }

    if (kinds & BOX_DRAW_TRANSFORM) {
        box_render_transform_t *out = box_draws_slot_transform(b, s->slot);
        uint32_t *rec = box_draws_slot_mover(b, s->slot);
        *out = model;
        if (rec) {
            ecs_entity_t mover = box_draws_mover_of(world, e);
            uint32_t index = box_draws_mover_ref(world, d, mover);
            box_draws_mover_unref(d, *rec & BOX_RENDER_MOVER_INDEX);
            *rec = (*rec & BOX_RENDER_MOVER_FRAME) | index;
            WorldTransform mover_buf;
            box_draws_relative(out, box_transform_world(world, mover, &mover_buf), &model);
        }
    }

    if ((kinds & BOX_DRAW_SHADING) && material.shape2d) {
        box_draws_material_release(world, d, e);
        box_draws_material(box_draws_slot_shading(b, s->slot), &material);
    } else if ((kinds & BOX_DRAW_SHADING) && faded) {
        box_render_mesh_shading_t *out = box_draws_slot_mesh_shading(b, s->slot);
        box_draws_mesh_shading(out, out->material, &material);
    } else if (kinds & BOX_DRAW_SHADING) {
        box_render_mesh_shading_t *out = box_draws_slot_mesh_shading(b, s->slot);
        box_draws_mesh_shading(out, box_draws_planet_material(
            box_draws_material_id(world, d, e, true, b->key.dynamic_material),
            b->key.planet), &material);
    }

    if (kinds & BOX_DRAW_SURFACE) {
        box_draws_surface(box_draws_slot_surface(b, s->slot), shape);
    }

    if (kinds & BOX_DRAW_FRAME) {
        box_draws_frame_write(world, d, e, b, s->slot, &model, keyed);
    }

    box_draws_shadow_update(world, d, b, s->slot, recorded ? &was_transform : NULL,
        surfaced ? &was_surface : NULL, kinds);
    box_draws_slot_changed(b, s->slot, kinds);
done:
    return;
}

void box_draws_shape_material(
    const ecs_world_t *world,
    ecs_entity_t e,
    const DrawSlot *s,
    box_draw_material_t *out)
{
    *out = (box_draw_material_t){
        .color = ecs_get(world, e, Rgb),
        .opacity = ecs_get(world, e, Opacity),
        .cutoff = ecs_get(world, e, AlphaCutoff),
        .pbr = ecs_get(world, e, PbrMaterial),
        .emissive = ecs_get(world, e, Emissive),
        .uv = ecs_get(world, e, TextureTransform),
        .transmission = ecs_get(world, e, Transmission),
        .shape2d = ecs_get(world, e, Shape2D),
        .tint = ecs_get(world, e, Tint), .occlusion = ecs_get(world, e, VertexOcclusion),
        .inherited = s->tinted ? &s->tint : NULL,
        .fade = s->fade
    };
    if (!out->shape2d) {
        out->reveal = ecs_get(world, e, VertexReveal);
    }

    if (out->shape2d) {
        out->border = ecs_get(world, e, Border);
        out->border_radius = out->border && out->shape2d->kind == BOX_SHAPE2D_RECT &&
            ecs_has(world, e, Rect);
        out->clip = s->clip;
        out->slice = s->slice;
        out->rank = s->rank;
    }
}

/** Bring a changed entity into the batches of its shape, shadow and text and write their
 * instances.
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
    if (ecs_has(world, e, BoxShadow)) {
        box_draws_box_shadow_update(world, d, e, s);
    }

    if (ecs_has(world, e, TextSlots)) {
        box_draws_text_update(world, d, e, kinds & BOX_DRAW_ALL);
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
    box_draws_cards_update(world, d);
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

    d->tick ++;
    d->stats.processed = 0;
    d->stats.scattered = 0;
    d->stats.scatters = 0;
    d->stats.upload_bytes = 0;
    d->stats.copies = 0;
    d->stats.copy_bytes = 0;
    d->stats.kept_pages = 0;
    d->stats.kept_bytes = 0;
    d->stats.transform_bytes = 0;
    d->stats.shading_bytes = 0;
    d->stats.surface_bytes = 0;
    d->stats.frame_bytes = 0;
    d->stats.material_bytes = 0;
    box_draws_materials_resolve(world, d);
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

/** Remember the entities that got or lost a PlanetAtmosphere as roots of subtrees whose planet
 * light slot changed, and give back the slot of an entity that lost it.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnPlanetAtmosphere(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (it->event == EcsOnRemove) {
        for (i = 0; i < it->count; i ++) {
            uint32_t slot = box_draws_planet_find(d, it->entities[i]);
            if (slot) {
                *ecs_vec_get_t(&d->planets, ecs_entity_t, (int32_t)slot - 1) = 0;
            }
        }
    }

    box_draws_on_move(it, BOX_DRAW_ALL);
}

void box_render_planet_observers(
    ecs_world_t *world)
{
    box_render_draws_t *d = box_draws_state(world);
    if (!d || !ecs_id(PlanetAtmosphere)) {
        goto done;
    }

    box_draws_observer(world, d, "DrawsOnPlanetAtmosphere", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(PlanetAtmosphere) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnPlanetAtmosphere);
done:
    return;
}

uint32_t box_render_planet_light_slot(
    const ecs_world_t *world,
    ecs_entity_t planet)
{
    const box_render_draws_t *d = box_draws_state(world);

    return d ? box_draws_planet_find(d, planet) : 0u;
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

    box_draws_observer(world, d, "DrawsOnTextureBlend", (ecs_observer_desc_t){
        .query.terms = {{
            .id = ecs_id(TextureBlend),
            .src.id = EcsSelf | EcsUp,
            .trav = EcsIsA
        }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnShading);

    box_draws_observer(world, d, "DrawsOnTextureLayers", (ecs_observer_desc_t){
        .query.terms = {{
            .id = ecs_id(TextureLayers),
            .src.id = EcsSelf | EcsUp,
            .trav = EcsIsA
        }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnShading);

    box_draws_observer(world, d, "DrawsOnTileTerrain", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(TileTerrain) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnShading);

    box_draws_observer(world, d, "DrawsOnOpacity", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Opacity) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnShadingMove);
    box_draws_observer(world, d, "DrawsOnTint", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Tint) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnShadingMove);
    box_draws_observer(world, d, "DrawsOnDrawRange", (ecs_observer_desc_t){
        .query.terms = {{
            .id = ecs_id(DrawRange),
            .src.id = EcsSelf | EcsUp,
            .trav = EcsIsA
        }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnMove);
}

/** Register the observers that collect changed entities and changed subtrees.
 *
 * The observers that free slots match disabled entities too: a shape or a text deleted while
 * disabled would otherwise leave its dead entity in its batch.
 *
 * The other observers skip disabled entities, so a shape that gets or loses EcsDisabled is
 * written again: a disabled shape gives back its slots, an enabled one takes them with what
 * it has now, also what changed while it was disabled.
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
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnRemove } }, BoxDrawsOnRemove);
    box_draws_observer(world, d, "DrawsOnDisabled", (ecs_observer_desc_t){
        .query.terms = {{ .id = EcsDisabled }},
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnShape2D", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Shape2D) }},
        .events = { EcsOnSet } }, BoxDrawsOnShading);
    box_draws_observer(world, d, "DrawsOnTextGlyphs", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(TextGlyphs) }},
        .events = { EcsOnSet } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnTextRemove", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Text) }},
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnRemove } }, BoxDrawsOnTextRemove);
    box_draws_observer(world, d, "DrawsOnTextSpans", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(TextSpans) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnShading);
    box_draws_box_shadow_observers(world, d);
    box_draws_material_observers(world, d);
    d->refs ++;
    box_transform_on_dynamic(world, BoxDrawsOnChange, d, box_draws_free);
    box_draws_observer(world, d, "DrawsOnWorldTransform", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet } }, BoxDrawsOnStaticWorld);
    box_draws_observer(world, d, "DrawsOnWorldTransformAdd", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnDynamicMaterial", (ecs_observer_desc_t){
        .query.terms = {{ .id = DynamicMaterial }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnMove);
    box_draws_observer(world, d, "DrawsOnTextureFrame", (ecs_observer_desc_t){
        .query.terms = {{ .id = TextureFrame }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnTextureFrame);
    box_draws_observer(world, d, "DrawsOnNoBake", (ecs_observer_desc_t){
        .query.terms = {{ .id = NoBake }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnManualTransform", (ecs_observer_desc_t){
        .query.terms = {{ .id = ManualTransform }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnTransitionTransform", (ecs_observer_desc_t){
        .query.terms = {{ .id = TransitionTransform }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnTransitionMaterial", (ecs_observer_desc_t){
        .query.terms = {{ .id = TransitionMaterial }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnTransitionOpacity", (ecs_observer_desc_t){
        .query.terms = {{ .id = TransitionOpacity }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnTransitionBoxShadow", (ecs_observer_desc_t){
        .query.terms = {{ .id = TransitionBoxShadow }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnLightBake", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(LightBake) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnLightBake);
    box_draws_observer(world, d, "DrawsOnSceneFeatures", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Scene) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnSceneFeatures);
    box_draws_observer(world, d, "DrawsOnReflectionBake", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(ReflectionBake) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnLightBake);
    box_draws_observer(world, d, "DrawsOnNoShadow", (ecs_observer_desc_t){
        .query.terms = {{ .id = NoShadow }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnMove);
    box_draws_observer(world, d, "DrawsOnLightFixture", (ecs_observer_desc_t){
        .query.terms = {{ .id = LightFixture }},
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
        .events = { EcsOnAdd, EcsOnSet, EcsOnRemove } }, BoxDrawsOnLayoutFloat);
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
        .entity = ecs_entity(world, { .name = "dynamic.Transforms" }),
        .terms = {
            { .id = ecs_id(ShapeMesh), .inout = EcsIn },
            { .id = ecs_id(DrawSlot), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
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
        .entity = ecs_entity(world, { .name = "dynamic.Materials" }),
        .terms = {
            { .id = ecs_id(ShapeMesh), .inout = EcsIn },
            { .id = ecs_id(DrawSlot), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
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

    d->extended = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ExtendedMaterials" }),
        .terms = {
            { .id = ecs_id(ShapeMesh), .inout = EcsInOutNone },
            {
                .id = ecs_id(Sheen),
                .src.id = EcsSelf | EcsUp,
                .trav = EcsIsA,
                .inout = EcsInOutNone,
                .oper = EcsOr
            },
            {
                .id = ecs_id(ClearCoat),
                .src.id = EcsSelf | EcsUp,
                .trav = EcsIsA,
                .inout = EcsInOutNone,
                .oper = EcsOr
            },
            {
                .id = ecs_id(SpecularAnisotropy),
                .src.id = EcsSelf | EcsUp,
                .trav = EcsIsA,
                .inout = EcsInOutNone,
                .oper = EcsOr
            },
            {
                .id = ecs_id(DiffuseTransmission),
                .src.id = EcsSelf | EcsUp,
                .trav = EcsIsA,
                .inout = EcsInOutNone,
                .oper = EcsOr
            },
            {
                .id = ecs_id(Fresnel),
                .src.id = EcsSelf | EcsUp,
                .trav = EcsIsA,
                .inout = EcsInOutNone
            }
        },
        .cache_kind = EcsQueryCacheAuto
    });
}

void box_render_draws_init(
    ecs_world_t *world)
{
    box_render_draws_t *d = ecs_os_calloc_t(box_render_draws_t);
    const int32_t plain[4] = {
        BOX_DRAWS_STORE_MESH, BOX_DRAWS_STORE_SURFACE, BOX_DRAWS_STORE_FRAME,
        BOX_DRAWS_STORE_FRAME_SURFACE
    };
    int32_t i;
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
    ecs_vec_init_t(NULL, &d->walk, box_draw_walk_t, 0);
    ecs_vec_init_t(NULL, &d->fade_links, box_draw_fade_link_t, 0);
    ecs_vec_init_t(NULL, &d->released, uint32_t, 0);
    ecs_vec_init_t(NULL, &d->blended, int32_t, 0);
    ecs_vec_init_t(NULL, &d->sorted, box_draw_sorted_t, 0);
    ecs_vec_init_t(NULL, &d->shadow_regions, box_render_shadow_region_t, 0);
    ecs_vec_init_t(NULL, &d->swaying, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &d->shadow_materials, uint32_t, 0);
    ecs_vec_init_t(NULL, &d->planets, ecs_entity_t, 0);
    ecs_map_init(&d->lookup, NULL);
    ecs_map_init(&d->meshes, NULL);
    box_draws_order_init(d);
    box_draws_clips_init(d);
    box_draws_materials_init(d);
    box_draws_movers_init(d);
    box_draws_cards_init(d);
    box_draws_store_init(&d->stores[BOX_DRAWS_STORE_MESH], &(box_draw_layout_t){
        .streams = BOX_RENDER_INSTANCE_STREAMS_PLAIN,
        .stride = {
            [BOX_RENDER_STREAM_TRANSFORM] = (uint32_t)sizeof(box_render_transform_t),
            [BOX_RENDER_STREAM_SHADING] = (uint32_t)sizeof(box_render_mesh_shading_t)
        }
    }, BOX_RENDER_BUFFER_FIRST);
    box_draws_store_init(&d->stores[BOX_DRAWS_STORE_SURFACE], &(box_draw_layout_t){
        .streams = BOX_RENDER_INSTANCE_STREAMS_SURFACE,
        .stride = {
            [BOX_RENDER_STREAM_TRANSFORM] = (uint32_t)sizeof(box_render_transform_t),
            [BOX_RENDER_STREAM_SHADING] = (uint32_t)sizeof(box_render_mesh_shading_t),
            [BOX_RENDER_STREAM_SURFACE] = (uint32_t)sizeof(box_render_surface_t)
        }
    }, BOX_RENDER_BUFFER_FIRST_SURFACE);
    box_draws_store_init(&d->stores[BOX_DRAWS_STORE_FRAME], &(box_draw_layout_t){
        .streams = BOX_DRAWS_STREAMS,
        .stride = {
            [BOX_RENDER_STREAM_TRANSFORM] = (uint32_t)sizeof(box_render_transform_t),
            [BOX_RENDER_STREAM_SHADING] = (uint32_t)sizeof(box_render_mesh_shading_t),
            [BOX_RENDER_STREAM_FRAME] = (uint32_t)sizeof(box_render_transform_t)
        }
    }, BOX_RENDER_BUFFER_FIRST_FRAME);
    box_draws_store_init(&d->stores[BOX_DRAWS_STORE_FRAME_SURFACE], &(box_draw_layout_t){
        .streams = BOX_DRAWS_STREAMS,
        .stride = {
            [BOX_RENDER_STREAM_TRANSFORM] = (uint32_t)sizeof(box_render_transform_t),
            [BOX_RENDER_STREAM_SHADING] = (uint32_t)sizeof(box_render_mesh_shading_t),
            [BOX_RENDER_STREAM_SURFACE] = (uint32_t)sizeof(box_render_surface_t),
            [BOX_RENDER_STREAM_FRAME] = (uint32_t)sizeof(box_render_transform_t)
        }
    }, BOX_RENDER_BUFFER_FIRST_FRAME_SURFACE);
    box_draws_store_init(&d->stores[BOX_DRAWS_STORE_2D], &(box_draw_layout_t){
        .streams = BOX_RENDER_INSTANCE_STREAMS_2D,
        .stride = {
            [BOX_RENDER_STREAM_TRANSFORM] = (uint32_t)sizeof(box_render_transform_t),
            [BOX_RENDER_STREAM_SHADING] = (uint32_t)sizeof(box_render_shading_t)
        }
    }, BOX_RENDER_BUFFER_FIRST_2D);
    for (i = 0; i < 4; i ++) {
        box_draw_layout_t layout = d->stores[plain[i]].layout;
        layout.streams = BOX_DRAWS_STREAMS;
        layout.stride[BOX_RENDER_STREAM_MOVER] = (uint32_t)sizeof(uint32_t);
        box_draws_store_init(&d->stores[BOX_DRAWS_STORE_MOVER + i], &layout,
            BOX_RENDER_BUFFER_FIRST_MOVER + (uint32_t)i * BOX_RENDER_INSTANCE_STREAMS *
                BOX_RENDER_PAGE_RANGE);
    }

    d->refs = 1;
    box_draws_queries(world, d);
    box_draws_text_init(world, d);
    box_draws_box_shadow_init(world);
    box_draws_observers(world, d);
    box_draws_transform_observers(world, d);
    box_draws_mover_observers(world, d);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.Draws" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractDraws,
        .ctx = d,
        .ctx_free = box_draws_free
    });
}

box_render_draws_t* box_draws_state(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.Draws");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;

    return s ? s->ctx : NULL;
}

ecs_vec_t* box_render_draws_shadow_regions(
    const ecs_world_t *world)
{
    box_render_draws_t *d = box_draws_state(world);

    return d ? &d->shadow_regions : NULL;
}

bool box_render_draws_swaying(
    const ecs_world_t *world,
    ecs_entity_t scene)
{
    box_render_draws_t *d = box_draws_state(world);
    int32_t i, count = d ? ecs_vec_count(&d->swaying) : 0;
    bool result = false;
    for (i = 0; i < count && !result; i ++) {
        result = *ecs_vec_get_t(&d->swaying, ecs_entity_t, i) == scene;
    }

    return result;
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
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.Draws");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    ecs_os_memset_t(stats, 0, box_render_draws_stats_t);
    if (s && s->ctx) {
        *stats = ((const box_render_draws_t*)s->ctx)->stats;
    }
}
