/**
 * @file texture/refs.c
 * @brief Reverse map from a texture entity to the materials that bind it.
 */

#include "texture_internal.h"

/** The referrer list of a texture, or NULL when nothing binds it.
 *
 * @param world The world
 * @param texture The texture entity
 * @param create Create the list when the texture has none
 * @return The list of referring entities, or NULL
 */
static ecs_vec_t* box_texture_refs_list(
    ecs_world_t *world,
    ecs_entity_t texture,
    bool create)
{
    TextureRefs *refs = ecs_get_mut(world, ecs_id(TextureRefs), TextureRefs);
    ecs_assert(refs != NULL, ECS_INVALID_OPERATION, "box.texture is not imported");
    ecs_vec_t *result = ecs_map_get_deref(&refs->map, ecs_vec_t, texture);
    if (!result && create) {
        result = ecs_map_ensure_alloc_t(&refs->map, ecs_vec_t, texture);
        ecs_vec_init_t(NULL, result, ecs_entity_t, 0);
    }

    return result;
}

/** Queue an entity and everything that inherits from it for re-extraction.
 *
 * @param world The world
 * @param e The entity
 */
static void box_texture_mark_entity(
    ecs_world_t *world,
    ecs_entity_t e)
{
    if (ecs_has(world, e, RenderMesh)) {
        box_render_mark_dirty(world, e);
    }

    ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, e));
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_texture_mark_entity(world, it.entities[i]);
        }
    }
}

/** Drop the entries of a list that no longer bind the texture.
 *
 * @param world The world
 * @param list The referrer list
 * @param texture The texture entity
 * @return The number of live referrers
 */
static int32_t box_texture_refs_compact(
    ecs_world_t *world,
    ecs_vec_t *list,
    ecs_entity_t texture)
{
    int32_t i = 0;
    while (i < ecs_vec_count(list)) {
        ecs_entity_t ref = ecs_vec_get_t(list, ecs_entity_t, i)[0];
        const PbrTextures *t = ecs_is_alive(world, ref) ? ecs_get(world, ref, PbrTextures) : NULL;
        bool live = t && (t->albedo == texture || t->emissive == texture ||
            t->roughness == texture || t->normal == texture);
        if (live) {
            i ++;
            continue;
        }

        int32_t last = ecs_vec_count(list) - 1;
        ecs_vec_get_t(list, ecs_entity_t, i)[0] = ecs_vec_get_t(list, ecs_entity_t, last)[0];
        ecs_vec_remove_last(list);
    }

    return ecs_vec_count(list);
}

/** Register a material as a referrer of a texture, ignoring duplicates.
 *
 * @param world The world
 * @param texture The texture entity, 0 for an empty slot
 * @param ref The material entity
 */
static void box_texture_refs_add(
    ecs_world_t *world,
    ecs_entity_t texture,
    ecs_entity_t ref)
{
    if (!texture) {
        goto done;
    }

    ecs_vec_t *list = box_texture_refs_list(world, texture, true);
    int32_t i;
    for (i = 0; i < ecs_vec_count(list); i ++) {
        if (ecs_vec_get_t(list, ecs_entity_t, i)[0] == ref) {
            goto done;
        }
    }

    ecs_vec_append_t(NULL, list, ecs_entity_t)[0] = ref;
done:
    return;
}

void box_texture_refs_mark(
    ecs_world_t *world,
    ecs_entity_t texture)
{
    ecs_vec_t *list = box_texture_refs_list(world, texture, false);
    if (!list) {
        goto done;
    }

    box_texture_refs_compact(world, list, texture);
    int32_t i;
    for (i = 0; i < ecs_vec_count(list); i ++) {
        box_texture_mark_entity(world, ecs_vec_get_t(list, ecs_entity_t, i)[0]);
    }
done:
    return;
}

void box_texture_refs_unbind(
    ecs_world_t *world,
    ecs_entity_t texture)
{
    ecs_vec_t *list = box_texture_refs_list(world, texture, false);
    if (!list) {
        goto done;
    }

    int32_t count = box_texture_refs_compact(world, list, texture);
    if (count) {
        const char *name = ecs_get_name(world, texture);
        ecs_warn("texture '%s' is unloaded while %d material(s) bind it, unbinding them",
            name ? name : "<unnamed>", count);
    }

    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_entity_t ref = ecs_vec_get_t(list, ecs_entity_t, i)[0];
        PbrTextures *t = ecs_get_mut(world, ref, PbrTextures);
        ecs_assert(t != NULL, ECS_INTERNAL_ERROR, NULL);
        t->albedo = t->albedo == texture ? 0 : t->albedo;
        t->emissive = t->emissive == texture ? 0 : t->emissive;
        t->roughness = t->roughness == texture ? 0 : t->roughness;
        t->normal = t->normal == texture ? 0 : t->normal;
        box_texture_mark_entity(world, ref);
    }

    TextureRefs *refs = ecs_get_mut(world, ecs_id(TextureRefs), TextureRefs);
    ecs_vec_fini_t(NULL, list, ecs_entity_t);
    ecs_map_remove_free(&refs->map, texture);
done:
    return;
}

/** A material bound its texture slots: record it as a referrer of each one.
 *
 * @param it Iterator over the changed materials
 */
static void box_texture_on_pbr_textures(
    ecs_iter_t *it)
{
    PbrTextures *t = ecs_field(it, PbrTextures, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        box_texture_refs_add(it->world, t[i].albedo, e);
        box_texture_refs_add(it->world, t[i].emissive, e);
        box_texture_refs_add(it->world, t[i].roughness, e);
        box_texture_refs_add(it->world, t[i].normal, e);
    }
}

void box_texture_refs_init(
    ecs_world_t *world)
{
    TextureRefs *refs = ecs_singleton_ensure(world, TextureRefs);
    ecs_map_init(&refs->map, NULL);
    ecs_singleton_modified(world, TextureRefs);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(PbrTextures), .src.id = EcsSelf }},
        .events = { EcsOnSet },
        .callback = box_texture_on_pbr_textures
    });
}
