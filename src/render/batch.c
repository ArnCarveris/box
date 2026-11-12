/**
 * @file render/batch.c
 * @brief Persistent instanced batches and what a frame sends of them to the renderer.
 *
 * Every batch owns a run of slots of an instance page (store.c) with transform, shading and,
 * for a mesh batch, surface and frame streams. Changed slots upload through a segment of the
 * change buffer per page and stream; a batch that outgrows its run moves and the GPU copies.
 *
 * Mesh and 2D batches take their runs from stores of their own. A batch with dynamic_transform
 * uploads its transform stream every frame, one with dynamic_material its shading stream.
 */

#include "draw.h"
#include <math.h>

/** Most slots of one blended draw, so the ordered cull pass spreads a long run over jobs */
#define BOX_DRAWS_BLENDED_RUN (512)

/** Bytes the smallest change buffer holds */
#define BOX_DRAWS_MIN_CHANGES (64 * 1024)

/** Bytes the change buffer holds at most, below the 256 MiB buffer size every WebGPU device
 * supports; a batch whose segment does not fit any more uploads all its instances instead */
#define BOX_DRAWS_MAX_CHANGES ((uint64_t)128 << 20)

_Static_assert(sizeof(box_render_transform_t) == 48, "InstanceTransform of instance.wgsl");
_Static_assert(sizeof(box_render_shading_t) == 64, "Shape2DShading of shape2d.wgsl");
_Static_assert(sizeof(box_render_mesh_shading_t) == 16, "MeshShading of instance.wgsl");
_Static_assert(sizeof(box_render_material_t) == 176, "Material of material.wgsl");
_Static_assert(BOX_RENDER_MATERIALS_MAX <= BOX_RENDER_MESH_MATERIAL_MASK,
    "the material slots fit below the planet light slot of a mesh shading");

/** Bytes of the header of a scatter segment */
#define BOX_DRAWS_HEADER ((uint32_t)(BOX_RENDER_SCATTER_HEADER * sizeof(uint32_t)))

/** Hash a batch key.
 *
 * @param key The key
 * @return The hash
 */
static uint64_t box_draws_hash(
    const box_draw_key_t *key)
{
    uint64_t h = key->scene * 0x9E3779B97F4A7C15ull;
    int32_t i;
    h ^= ((uint64_t)key->mesh + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full;
    h ^= (((uint64_t)key->pipeline * 2 + key->dynamic_transform) * 2 + key->dynamic_material) *
        0x165667B19E3779F9ull;
    h ^= (((((uint64_t)key->blend * 2 + key->back_face) * 4 + key->wrap) * 2 + key->cutout) * 2 +
        key->transmissive + 1) * 0x27D4EB2F165667C5ull;
    h ^= ((uint64_t)key->surface + (uint64_t)key->water * 2 + (uint64_t)key->frame * 4) *
        0x85EBCA77C2B2AE63ull;
    h ^= ((uint64_t)key->foam + 1) * 0x9FB21C651E98DF25ull;
    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        h = (h ^ (h >> 31)) * 0x9E3779B97F4A7C15ull + key->textures[i];
    }

    h ^= (((((((uint64_t)key->shadow * 2 + key->bake) * 2 + key->box_shadow) * 2 + key->slice) *
        2 + key->texture_blend) * 2 + key->polygon) * 2 + key->fixture) * 0x94D049BB133111EBull;
    h ^= ((uint64_t)key->atlas + 1) * 0xD6E8FEB86659FD93ull;
    h ^= ((uint64_t)key->features + 1) * 0xA0761D6478BD642Full;
    h ^= ((uint64_t)key->planet + 1) * 0xE7037ED1A0B428DBull;

    return h ^ (h >> 29);
}

/** Compare two batch keys.
 *
 * @param a The first key
 * @param b The second key
 * @return Whether the keys are equal
 */
static bool box_draws_key_eq(
    const box_draw_key_t *a,
    const box_draw_key_t *b)
{
    return a->scene == b->scene && a->mesh == b->mesh && a->pipeline == b->pipeline &&
        a->blend == b->blend && a->cutout == b->cutout && a->back_face == b->back_face &&
        a->transmissive == b->transmissive && a->water == b->water && a->foam == b->foam &&
        a->dynamic_transform == b->dynamic_transform &&
        a->dynamic_material == b->dynamic_material &&
        a->wrap == b->wrap && !ecs_os_memcmp(a->textures, b->textures, ECS_SIZEOF(a->textures)) &&
        a->shadow == b->shadow && a->fixture == b->fixture && a->bake == b->bake &&
        a->atlas == b->atlas && a->features == b->features && a->planet == b->planet &&
        a->surface == b->surface && a->frame == b->frame &&
        a->box_shadow == b->box_shadow && a->slice == b->slice &&
        a->texture_blend == b->texture_blend && a->polygon == b->polygon;
}

box_draw_batch_t* box_draws_batch(
    box_render_draws_t *d,
    int32_t index)
{
    return ecs_vec_get_t(&d->batches, box_draw_batch_t, index);
}

box_render_transform_t* box_draws_slot_transform(
    box_draw_batch_t *b,
    int32_t slot)
{
    return ecs_vec_get_t(&b->transforms, box_render_transform_t, slot);
}

box_render_shading_t* box_draws_slot_shading(
    box_draw_batch_t *b,
    int32_t slot)
{
    return ecs_vec_get_t(&b->shadings, box_render_shading_t, slot);
}

box_render_mesh_shading_t* box_draws_slot_mesh_shading(
    box_draw_batch_t *b,
    int32_t slot)
{
    return ecs_vec_get_t(&b->mesh_shadings, box_render_mesh_shading_t, slot);
}

/** Check whether a batch is drawn by the 2D shape pipeline, whose pages hold the whole shading.
 *
 * @param b The batch
 * @return Whether its key has the 2D shape pipeline
 */
static bool box_draws_is_2d(
    const box_draw_batch_t *b)
{
    return b->key.pipeline == BOX_RENDER_SHADER_SHAPE2D;
}

/** Check whether a batch has the surface stream: a mesh batch with a parametric surface, or a
 * frame batch, whose pages have every stream.
 *
 * @param b The batch
 * @return Whether its pages are of the surface or the frame store
 */
static bool box_draws_has_surface(
    const box_draw_batch_t *b)
{
    return (b->key.surface || b->key.frame) && !box_draws_is_2d(b);
}

/** Get the store of the batches of a key: 2D, frame, surface or plain mesh.
 *
 * @param d The state
 * @param key The key
 * @return The store
 */
static box_draw_store_t* box_draws_key_store(
    box_render_draws_t *d,
    const box_draw_key_t *key)
{
    int32_t store = BOX_DRAWS_STORE_MESH;
    if (key->pipeline == BOX_RENDER_SHADER_SHAPE2D) {
        store = BOX_DRAWS_STORE_2D;
    } else if (key->frame) {
        store = BOX_DRAWS_STORE_FRAME;
    } else if (key->surface) {
        store = BOX_DRAWS_STORE_SURFACE;
    }

    return &d->stores[store];
}

box_draw_store_t* box_draws_batch_store(
    box_render_draws_t *d,
    const box_draw_batch_t *b)
{
    return box_draws_key_store(d, &b->key);
}

uint8_t box_draws_batch_kinds(
    const box_draw_batch_t *b)
{
    return b->key.frame ? BOX_DRAW_ALL
        : box_draws_has_surface(b) ? (uint8_t)(BOX_DRAW_BOTH | BOX_DRAW_SURFACE)
        : BOX_DRAW_BOTH;
}

box_render_surface_t* box_draws_slot_surface(
    box_draw_batch_t *b,
    int32_t slot)
{
    return box_draws_has_surface(b)
        ? ecs_vec_get_t(&b->surfaces, box_render_surface_t, slot)
        : NULL;
}


/** Get the instance data of a stream of a batch.
 *
 * @param b The batch
 * @param stream The stream, one of the layout of the store of the batch
 * @return The vector of the stream, one element per slot
 */
static ecs_vec_t* box_draws_stream(
    box_draw_batch_t *b,
    int32_t stream)
{
    ecs_vec_t *result = &b->surfaces;
    if (stream == BOX_RENDER_STREAM_FRAME) {
        result = &b->frames;
    } else if (stream == BOX_RENDER_STREAM_TRANSFORM) {
        result = &b->transforms;
    } else if (stream == BOX_RENDER_STREAM_SHADING) {
        result = box_draws_is_2d(b) ? &b->shadings : &b->mesh_shadings;
    }

    return result;
}

/** Add bytes uploaded into a stream to the counters.
 *
 * @param d The state
 * @param stream The stream
 * @param bytes The bytes
 */
static void box_draws_count_bytes(
    box_render_draws_t *d,
    int32_t stream,
    int64_t bytes)
{
    if (stream == BOX_RENDER_STREAM_TRANSFORM) {
        d->stats.transform_bytes += bytes;
    } else if (stream == BOX_RENDER_STREAM_SHADING) {
        d->stats.shading_bytes += bytes;
    } else if (stream == BOX_RENDER_STREAM_FRAME) {
        d->stats.frame_bytes += bytes;
    } else {
        d->stats.surface_bytes += bytes;
    }

    d->stats.upload_bytes += bytes;
}

uint8_t box_draws_dynamic_kinds(
    const box_draw_batch_t *b)
{
    return (uint8_t)((b->key.dynamic_transform ? BOX_DRAW_TRANSFORM : 0) |
        (b->key.dynamic_transform && b->key.frame ? BOX_DRAW_FRAME : 0) |
        (b->key.dynamic_material ? BOX_DRAW_SHADING : 0));
}

/** Forget the changed slots of every stream of a batch.
 *
 * @param b The batch
 */
static void box_draws_changed_clear(
    box_draw_batch_t *b)
{
    int32_t s;
    for (s = 0; s < BOX_DRAWS_STREAMS; s ++) {
        ecs_vec_clear(&b->changed[s]);
    }
}

/** Get the instances a batch of a store holds before its key continues in another batch: the
 * chunk of the store, and a batch never spans pages, so at most the slots of a page.
 *
 * @param store The store of the batch
 * @return The instances
 */
static uint32_t box_draws_limit(
    const box_draw_store_t *store)
{
    return store->chunk < store->slots ? store->chunk : store->slots;
}

uint32_t box_draws_key_limit(
    box_render_draws_t *d,
    const box_draw_key_t *key)
{
    return box_draws_limit(box_draws_key_store(d, key));
}

int32_t box_draws_batch_ensure(
    box_render_draws_t *d,
    const box_draw_key_t *key)
{
    uint64_t hash = box_draws_hash(key);
    uint64_t *head = ecs_map_get(&d->lookup, hash);
    int32_t result = head ? (int32_t)*head - 1 : -1;
    uint32_t limit = box_draws_key_limit(d, key);
    box_draw_batch_t *b;
    while (result != -1) {
        b = box_draws_batch(d, result);
        if (box_draws_key_eq(&b->key, key) &&
            (uint32_t)ecs_vec_count(&b->entities) < limit)
        {
            goto done;
        }

        result = b->next;
    }

    if (ecs_vec_count(&d->free_batches)) {
        result = *ecs_vec_last_t(&d->free_batches, int32_t);
        ecs_vec_remove_last(&d->free_batches);
        b = box_draws_batch(d, result);
    } else {
        result = ecs_vec_count(&d->batches);
        b = ecs_vec_append_t(NULL, &d->batches, box_draw_batch_t);
        ecs_os_memset_t(b, 0, box_draw_batch_t);
        ecs_vec_init_t(NULL, &b->entities, ecs_entity_t, 0);
        ecs_vec_init_t(NULL, &b->glyphs, int32_t, 0);
        ecs_vec_init_t(NULL, &b->transforms, box_render_transform_t, 0);
        ecs_vec_init_t(NULL, &b->shadings, box_render_shading_t, 0);
        ecs_vec_init_t(NULL, &b->mesh_shadings, box_render_mesh_shading_t, 0);
        ecs_vec_init_t(NULL, &b->surfaces, box_render_surface_t, 0);
        ecs_vec_init_t(NULL, &b->frames, box_render_transform_t, 0);
        for (int32_t s = 0; s < BOX_DRAWS_STREAMS; s ++) {
            ecs_vec_init_t(NULL, &b->changed[s], int32_t, 0);
        }
    }

    b->key = *key;
    b->alive = true;
    b->page = -1;
    b->first = 0;
    b->capacity = 0;
    b->full = box_draws_batch_kinds(b);
    b->sent = 0;
    b->from_count = 0;
    b->bounds_dirty = false;
    b->bounded = false;
    b->world_dirty = true;
    b->world_valid = false;
    b->was_ready = false;
    b->transform_frame = -1;
    b->shading_frame = -1;
    b->record_frame = -1;
    head = ecs_map_ensure(&d->lookup, hash);
    b->next = *head ? (int32_t)*head - 1 : -1;
    *head = (uint64_t)result + 1;

done:
    return result;
}

/** Take a batch out of use, freeing its run of slots.
 *
 * @param d The state
 * @param p The packet
 * @param index Index of the batch
 */
static void box_draws_batch_free(
    box_render_draws_t *d,
    box_render_packet_t *p,
    int32_t index)
{
    box_draw_batch_t *b = box_draws_batch(d, index);
    uint64_t hash = box_draws_hash(&b->key);
    uint64_t *head = ecs_map_get(&d->lookup, hash);
    int32_t prev = -1, cur = head ? (int32_t)*head - 1 : -1;
    while (cur != -1 && cur != index) {
        prev = cur;
        cur = box_draws_batch(d, cur)->next;
    }

    if (prev != -1) {
        box_draws_batch(d, prev)->next = b->next;
    } else if (b->next != -1) {
        *head = (uint64_t)b->next + 1;
    } else if (head) {
        ecs_map_remove(&d->lookup, hash);
    }

    if (b->page >= 0) {
        box_draws_store_free(box_draws_batch_store(d, b), p, b->page, b->first, b->capacity);
    }

    ecs_vec_clear(&b->entities);
    ecs_vec_clear(&b->glyphs);
    ecs_vec_clear(&b->transforms);
    ecs_vec_clear(&b->shadings);
    ecs_vec_clear(&b->mesh_shadings);
    ecs_vec_clear(&b->surfaces);
    ecs_vec_clear(&b->frames);
    box_draws_changed_clear(b);
    b->alive = false;
    b->page = -1;
    b->first = 0;
    b->capacity = 0;
    b->sent = 0;
    b->from_count = 0;
    *ecs_vec_append_t(NULL, &d->free_batches, int32_t) = index;
}

void box_draws_slot_changed(
    box_draw_batch_t *b,
    int32_t slot,
    uint8_t kinds)
{
    int32_t s;
    kinds &= box_draws_batch_kinds(b);
    if (kinds & (BOX_DRAW_TRANSFORM | BOX_DRAW_SURFACE)) {
        b->world_dirty = true;
    }

    if ((kinds & BOX_DRAW_SURFACE) &&
        (b->bounded || box_draws_slot_surface(b, slot)->p[0][0] != 0.0f))
    {
        b->bounds_dirty = true;
    }

    kinds &= (uint8_t)~(box_draws_dynamic_kinds(b) | b->full);
    for (s = 0; kinds && s < BOX_DRAWS_STREAMS; s ++) {
        ecs_vec_t *changed = &b->changed[s];
        if (!(kinds & (1 << s))) {
            continue;
        }

        if (ecs_vec_count(changed) >= ecs_vec_count(&b->entities)) {
            b->full |= (uint8_t)(1 << s);
            ecs_vec_clear(changed);
            continue;
        }

        *ecs_vec_append_t(NULL, changed, int32_t) = slot;
    }
}

int32_t box_draws_slot_add(
    box_render_draws_t *d,
    int32_t index,
    ecs_entity_t e)
{
    box_draw_batch_t *b = box_draws_batch(d, index);
    int32_t result = ecs_vec_count(&b->entities);
    if (box_draws_is_2d(b)) {
        box_draws_order_slot(d, e);
        ecs_os_memset_t(ecs_vec_append_t(NULL, &b->shadings, box_render_shading_t), 0,
            box_render_shading_t);
    } else {
        ecs_os_memset_t(ecs_vec_append_t(NULL, &b->mesh_shadings, box_render_mesh_shading_t),
            0, box_render_mesh_shading_t);
    }

    if (box_draws_has_surface(b)) {
        ecs_os_memset_t(ecs_vec_append_t(NULL, &b->surfaces, box_render_surface_t), 0,
            box_render_surface_t);
    }

    if (b->key.frame) {
        ecs_os_memset_t(ecs_vec_append_t(NULL, &b->frames, box_render_transform_t), 0,
            box_render_transform_t);
    }

    *ecs_vec_append_t(NULL, &b->entities, ecs_entity_t) = e;
    ecs_os_memset_t(ecs_vec_append_t(NULL, &b->transforms, box_render_transform_t), 0,
        box_render_transform_t);
    b->world_dirty = true;

    return result;
}

int32_t box_draws_glyph_add(
    box_render_draws_t *d,
    int32_t index,
    ecs_entity_t e,
    int32_t glyph)
{
    int32_t result = box_draws_slot_add(d, index, e);
    *ecs_vec_append_t(NULL, &box_draws_batch(d, index)->glyphs, int32_t) = glyph;

    return result;
}

/** Point the slot entry of a moved glyph at its new slot.
 *
 * @param world The world
 * @param b The glyph batch
 * @param moved The entity of the moved glyph
 * @param from The slot the glyph was in
 * @param to The slot the glyph moves to
 */
static void box_draws_glyph_moved(
    ecs_world_t *world,
    box_draw_batch_t *b,
    ecs_entity_t moved,
    int32_t from,
    int32_t to)
{
    int32_t glyph = *ecs_vec_get_t(&b->glyphs, int32_t, from);
    TextSlots *slots = ecs_get_mut(world, moved, TextSlots);
    *ecs_vec_get_t(&b->glyphs, int32_t, to) = glyph;
    if (slots && glyph < ecs_vec_count(&slots->slots)) {
        ecs_vec_get_t(&slots->slots, box_draw_glyph_slot_t, glyph)->slot = to;
    }
}

bool box_draws_caster(
    const box_draw_batch_t *b)
{
    return box_render_is_mesh(b->key.pipeline) && b->key.shadow &&
        b->key.blend == Opaque && !b->key.atlas;
}

bool box_draws_shadow_bounds(
    const ecs_world_t *world,
    const box_draw_batch_t *b,
    const box_render_transform_t *lo,
    const box_render_transform_t *hi,
    const box_render_surface_t *surface,
    box_render_shadow_region_t *out)
{
    const box_mesh_cache_t *cache = NULL;
    const box_mesh_t *mesh = NULL;
    vec3 min, max;
    int32_t r, k;
    bool result = false;
    if (surface && surface->p[0][0] != 0.0f) {
        for (r = 0; r < 3; r ++) {
            min[r] = surface->p[22][r] - surface->p[23][r];
            max[r] = surface->p[22][r] + surface->p[23][r];
        }
    } else {
        cache = box_mesh_cache_of(world);
        mesh = cache ? box_mesh_cache_get(cache, b->key.mesh) : NULL;
        if (!mesh || !mesh->vertex_count) {
            goto done;
        }

        glm_vec3_copy(ECS_CONST_CAST(float*, mesh->min), min);
        glm_vec3_copy(ECS_CONST_CAST(float*, mesh->max), max);
    }

    out->scene = b->key.scene;
    for (r = 0; r < 3; r ++) {
        const float *row_lo = lo->rows[r];
        const float *row_hi = hi->rows[r];
        float world_lo = 0.0f, world_hi = 0.0f;
        for (k = 0; k < 3; k ++) {
            float p0 = row_lo[k] * min[k], p1 = row_lo[k] * max[k];
            float p2 = row_hi[k] * min[k], p3 = row_hi[k] * max[k];
            world_lo += glm_min(glm_min(p0, p1), glm_min(p2, p3));
            world_hi += glm_max(glm_max(p0, p1), glm_max(p2, p3));
        }

        out->min[r] = world_lo + row_lo[3];
        out->max[r] = world_hi + row_hi[3];
    }

    result = true;
done:
    return result;
}

void box_draws_shadow_region(
    const ecs_world_t *world,
    box_render_draws_t *d,
    const box_draw_batch_t *b,
    const box_render_transform_t *transform,
    const box_render_surface_t *surface)
{
    box_render_shadow_region_t region;
    if (d->shadow_record && box_draws_caster(b) &&
        box_draws_shadow_bounds(world, b, transform, transform, surface, &region))
    {
        *ecs_vec_append_t(NULL, &d->shadow_regions, box_render_shadow_region_t) = region;
    }
}

/** Record the regions of every slot of a caster batch, which the tiles did not draw while the
 * backend lacked its mesh, or drew with another albedo image of an alpha tested batch.
 *
 * @param world The world
 * @param d The state
 * @param b The batch
 */
static void box_draws_shadow_batch(
    const ecs_world_t *world,
    box_render_draws_t *d,
    box_draw_batch_t *b)
{
    int32_t i;
    for (i = 0; d->shadow_record && box_draws_caster(b) &&
        i < ecs_vec_count(&b->entities); i ++)
    {
        box_draws_shadow_region(world, d, b, box_draws_slot_transform(b, i),
            box_draws_slot_surface(b, i));
    }
}

/** Record the regions of the slots of the alpha tested caster batches that render with a slot
 * of the material table whose cutoff or UV transform changed, and forget those slots.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_shadow_materials(
    const ecs_world_t *world,
    box_render_draws_t *d)
{
    const uint32_t *changed = ecs_vec_first_t(&d->shadow_materials, uint32_t);
    int32_t i, j, k, count = ecs_vec_count(&d->shadow_materials);
    for (i = 0; count && i < ecs_vec_count(&d->batches); i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        if (!b->alive || !b->key.cutout || !box_draws_caster(b)) {
            continue;
        }

        for (j = 0; j < ecs_vec_count(&b->entities); j ++) {
            uint32_t material = box_draws_slot_mesh_shading(b, j)->material &
                BOX_RENDER_MESH_MATERIAL_MASK;
            bool hit = false;
            for (k = 0; k < count && !hit; k ++) {
                hit = changed[k] == material;
            }

            if (hit) {
                box_draws_shadow_region(world, d, b, box_draws_slot_transform(b, j),
                    box_draws_slot_surface(b, j));
            }
        }
    }

    ecs_vec_clear(&d->shadow_materials);
}

/** Get the revision of the albedo image an alpha tested caster batch renders its tiles with,
 * for a batch with a TextureBlend the sum of the revisions of its mask and of the albedo of
 * each layer, which only grow, so a new image of any of them changes it.
 *
 * @param d The state
 * @param b The batch
 * @return The revision, 0 for a batch that is no alpha tested caster
 */
static uint32_t box_draws_shadow_texture(
    const box_render_draws_t *d,
    const box_draw_batch_t *b)
{
    const TextureBlend *blend;
    uint32_t result = 0;
    int32_t k;
    if (!b->key.cutout || !box_draws_caster(b)) {
        goto done;
    }

    if (!b->key.texture_blend) {
        result = box_textures_revision(d->textures, b->key.textures[0]);
        goto done;
    }

    blend = d->world ? ecs_get(d->world, b->key.textures[0], TextureBlend) : NULL;
    if (!blend) {
        goto done;
    }

    result = box_textures_revision(d->textures, blend->mask);
    for (k = 0; k < BOX_RENDER_BLEND_LAYERS; k ++) {
        ecs_entity_t layer = blend->layers[k];
        const PbrTextures *textures = layer && ecs_is_alive(d->world, layer)
            ? ecs_get(d->world, layer, PbrTextures) : NULL;
        if (textures) {
            result += box_textures_revision(d->textures, textures->albedo);
        }
    }

done:
    return result;
}

void box_draws_slot_remove(
    ecs_world_t *world,
    box_render_draws_t *d,
    int32_t index,
    int32_t slot)
{
    box_draw_batch_t *b = box_draws_batch(d, index);
    int32_t last = ecs_vec_count(&b->entities) - 1;
    bool flat = box_draws_is_2d(b);
    box_draws_shadow_region(world, d, b, box_draws_slot_transform(b, slot),
        box_draws_slot_surface(b, slot));
    if (flat) {
        box_draws_order_slot(d, 0);
    }

    if (slot != last) {
        ecs_entity_t moved = *ecs_vec_get_t(&b->entities, ecs_entity_t, last);
        bool shadow = flat && !b->key.atlas &&
            box_draws_box_shadow_moved(world, moved, index, last, slot);
        DrawSlot *s = b->key.atlas || shadow ? NULL : ecs_get_mut(world, moved, DrawSlot);
        if (b->key.atlas) {
            box_draws_glyph_moved(world, b, moved, last, slot);
        }

        *ecs_vec_get_t(&b->entities, ecs_entity_t, slot) = moved;
        *box_draws_slot_transform(b, slot) = *box_draws_slot_transform(b, last);
        if (flat) {
            *box_draws_slot_shading(b, slot) = *box_draws_slot_shading(b, last);
        } else {
            *box_draws_slot_mesh_shading(b, slot) = *box_draws_slot_mesh_shading(b, last);
        }

        if (box_draws_has_surface(b)) {
            *box_draws_slot_surface(b, slot) = *box_draws_slot_surface(b, last);
        }

        if (b->key.frame) {
            *box_draws_slot_frame(b, slot) = *box_draws_slot_frame(b, last);
        }

        if (s) {
            s->slot = slot;
        }

        box_draws_slot_changed(b, slot, box_draws_batch_kinds(b));
    }

    if (b->bounded) {
        b->bounds_dirty = true;
    }

    b->world_dirty = true;
    ecs_vec_remove_last(&b->entities);
    ecs_vec_remove_last(&b->transforms);
    ecs_vec_remove_last(flat ? &b->shadings : &b->mesh_shadings);
    if (box_draws_has_surface(b)) {
        ecs_vec_remove_last(&b->surfaces);
    }

    if (b->key.frame) {
        ecs_vec_remove_last(&b->frames);
    }

    if (b->key.atlas) {
        ecs_vec_remove_last(&b->glyphs);
    }
}


/** Send a mesh to the backend unless it has it, taking a cache reference to it.
 *
 * @param p The packet
 * @param d The state
 * @param cache The mesh cache
 * @param handle The mesh handle
 * @return Whether the backend has the mesh
 */
static bool box_draws_mesh(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_mesh_cache_t *cache,
    uint32_t handle)
{
    bool result = true;
    const box_mesh_t *mesh;
    if (ecs_map_get(&d->meshes, handle)) {
        goto done;
    }

    mesh = box_mesh_cache_get(cache, handle);
    if (!mesh || !mesh->vertex_count || !mesh->index_count) {
        result = false;
        goto done;
    }

    uint64_t total_size = sizeof(box_render_mesh_desc_t) +
        (uint64_t)mesh->vertex_count * sizeof(box_vertex_t) +
        (uint64_t)mesh->index_count * sizeof(uint32_t) +
        (uint64_t)mesh->surface_asset_count * sizeof(vec4);
    if (total_size + (uint64_t)ecs_vec_count(&p->data) + BOX_RENDER_DATA_ALIGN > INT32_MAX) {
        ecs_err("surface render packet capacity exceeded");
        result = false;
        goto done;
    }

    uint32_t vertex_size = (uint32_t)mesh->vertex_count * (uint32_t)sizeof(box_vertex_t);
    uint32_t index_size = (uint32_t)mesh->index_count * (uint32_t)sizeof(uint32_t);
    box_render_mesh_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_MESH,
        BOX_RENDER_CREATE, handle,
        (uint32_t)sizeof(box_render_mesh_desc_t) + vertex_size + index_size +
        (uint32_t)mesh->surface_asset_count * sizeof(vec4));
    uint8_t *data = (uint8_t*)(desc + 1);
    desc->surface_asset_count = (uint32_t)mesh->surface_asset_count;
    desc->vertex_count = (uint32_t)mesh->vertex_count;
    desc->index_count = (uint32_t)mesh->index_count;
    ecs_os_memcpy(data, mesh->vertices, (ecs_size_t)vertex_size);
    ecs_os_memcpy(data + vertex_size, mesh->indices, (ecs_size_t)index_size);
    if (mesh->surface_asset_count) {
        ecs_os_memcpy(data + vertex_size + index_size, mesh->surface_assets,
            mesh->surface_asset_count * sizeof(vec4));
    }

    box_mesh_cache_hold(cache, handle);
    ecs_map_insert(&d->meshes, handle, 1);
done:
    return result;
}

/** Destroy the meshes no shape uses any more and drop the hold of the renderer on them.
 *
 * @param p The packet
 * @param d The state
 * @param cache The mesh cache
 */
static void box_draws_release(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_mesh_cache_t *cache)
{
    int32_t i;
    ecs_vec_clear(&d->released);
    ecs_map_iter_t it = ecs_map_iter(&d->meshes);
    while (ecs_map_next(&it)) {
        uint32_t handle = (uint32_t)ecs_map_key(&it);
        if (!box_mesh_cache_users(cache, handle)) {
            *ecs_vec_append_t(NULL, &d->released, uint32_t) = handle;
        }
    }

    for (i = 0; i < ecs_vec_count(&d->released); i ++) {
        uint32_t handle = *ecs_vec_get_t(&d->released, uint32_t, i);
        box_render_packet_resource(p, BOX_RENDER_MESH, BOX_RENDER_DESTROY, handle, 0);
        box_mesh_cache_unhold(cache, handle);
        ecs_map_remove(&d->meshes, handle);
    }
}

void box_draws_reset_backend(
    box_render_draws_t *d,
    box_mesh_cache_t *cache)
{
    int32_t i;
    ecs_map_iter_t it = ecs_map_iter(&d->meshes);
    while (ecs_map_next(&it)) {
        box_mesh_cache_unhold(cache, (uint32_t)ecs_map_key(&it));
    }

    ecs_map_clear(&d->meshes);
    for (i = 0; i < ecs_vec_count(&d->batches); i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        b->page = -1;
        b->first = 0;
        b->capacity = 0;
        b->full = box_draws_batch_kinds(b);
        b->sent = 0;
        b->from_count = 0;
        box_draws_changed_clear(b);
    }

    for (i = 0; i < BOX_DRAWS_STORES; i ++) {
        box_draws_store_reset(&d->stores[i]);
    }

    d->changes_size = 0;
    box_draws_clips_reset(d);
    box_draws_materials_reset(d);
}

/** Give a batch a run that holds all its instances. A new batch gets its count rounded up to
 * BOX_DRAWS_GROW_ALIGN slots, at least BOX_DRAWS_MIN_CAPACITY; one that outgrew its run takes
 * the growth step at or above its count (box_draws_grow), so headroom only comes with growth.
 *
 * A run at the top of its page extends in place; otherwise the batch moves and its run is freed,
 * and the send copies the instances the GPU holds into the new run and writes the queued changes
 * over them (box_draws_send_batch). A batch the GPU holds none of uploads every instance.
 *
 * @param p The packet
 * @param d The state
 * @param b The batch
 */
static void box_draws_reserve(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_draw_batch_t *b)
{
    box_draw_store_t *store = box_draws_batch_store(d, b);
    uint32_t count = (uint32_t)ecs_vec_count(&b->entities);
    uint32_t limit = box_draws_limit(store);
    uint32_t capacity, first;
    int32_t page;
    if (b->page >= 0 && count <= b->capacity) {
        goto done;
    }

    if (b->page < 0) {
        capacity = box_draws_grow(count, count);
    } else {
        capacity = box_draws_grow(0, count);
    }

    if (capacity > limit && count <= limit) {
        capacity = limit;
    }

    if (b->page >= 0 &&
        box_draws_store_extend(store, b->page, b->first, b->capacity, capacity))
    {
        b->capacity = capacity;
        goto done;
    }

    box_draws_store_alloc(store, capacity, &page, &first);
    b->from_count = 0;
    if (b->page >= 0) {
        b->from_page = b->page;
        b->from_first = b->first;
        b->from_count = b->sent < b->capacity ? b->sent : b->capacity;
        b->from_count = b->from_count < count ? b->from_count : count;
        b->from_serial = box_draws_store_page(store, b->page)->serial;
        box_draws_store_free(store, p, b->page, b->first, b->capacity);
    }

    b->page = page;
    b->first = first;
    b->capacity = capacity;
    if (!b->from_count) {
        b->full = box_draws_batch_kinds(b);
        box_draws_changed_clear(b);
    }
done:
    return;
}

/** Make the buffers of every page hold their runs. A buffer created again keeps the instances
 * of the buffer it replaces through a GPU copy, so no batch of the page uploads for it.
 *
 * @param p The packet
 * @param d The state
 */
static void box_draws_pages(
    box_render_packet_t *p,
    box_render_draws_t *d)
{
    int32_t pg, st, s;
    for (st = 0; st < BOX_DRAWS_STORES; st ++) {
        box_draw_store_t *store = &d->stores[st];
        for (pg = 0; pg < ecs_vec_count(&store->pages); pg ++) {
            uint32_t kept = box_draws_store_page(store, pg)->capacity;
            if (!box_draws_store_reserve(store, p, pg) || !kept) {
                continue;
            }

            for (s = 0; s < store->layout.streams; s ++) {
                d->stats.kept_pages ++;
                d->stats.kept_bytes += (int64_t)kept * store->layout.stride[s];
            }
        }
    }
}

/** Upload the instances of a stream of a batch from a slot to the end into its run; the
 * changed slots of the stream stay queued.
 *
 * @param p The packet
 * @param d The state
 * @param b The batch
 * @param stream The stream
 * @param from First slot uploaded, 0 for every instance
 */
static void box_draws_upload_from(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_draw_batch_t *b,
    int32_t stream,
    uint32_t from)
{
    box_draw_store_t *store = box_draws_batch_store(d, b);
    ecs_vec_t *data = box_draws_stream(b, stream);
    uint32_t stride = store->layout.stride[stream];
    uint32_t count = (uint32_t)ecs_vec_count(data);
    uint32_t size = from < count ? (count - from) * stride : 0;
    uint32_t offset = (b->first + from) * stride;
    if (size) {
        void *dst = box_render_packet_upload(p,
            box_draws_store_handle(store, b->page, stream), offset, size);
        ecs_os_memcpy(dst, ECS_OFFSET(ecs_vec_first(data), from * stride), (ecs_size_t)size);
        box_draws_count_bytes(d, stream, size);
        if (!from && stream == BOX_RENDER_STREAM_TRANSFORM) {
            b->transform_frame = p->frame;
            b->transform_data = ecs_vec_last_t(&p->uploads, box_render_upload_t)->data;
        } else if (!from && stream == BOX_RENDER_STREAM_SHADING) {
            b->shading_frame = p->frame;
            b->shading_data = ecs_vec_last_t(&p->uploads, box_render_upload_t)->data;
        }
    }
}

/** Upload every instance of a stream of a batch into its run.
 *
 * @param p The packet
 * @param d The state
 * @param b The batch
 * @param stream The stream
 */
static void box_draws_upload_all(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_draw_batch_t *b,
    int32_t stream)
{
    box_draws_upload_from(p, d, b, stream, 0);
    b->full &= (uint8_t)~(1 << stream);
    ecs_vec_clear(&b->changed[stream]);
}

/** Copy the first slots of a stream of a moved batch from its old run into its new run on the
 * GPU.
 *
 * @param p The packet
 * @param d The state
 * @param b The batch
 * @param stream The stream
 * @param count Slots copied
 */
static void box_draws_copy(
    box_render_packet_t *p,
    box_render_draws_t *d,
    const box_draw_batch_t *b,
    int32_t stream,
    uint32_t count)
{
    box_draw_store_t *store = box_draws_batch_store(d, b);
    uint32_t stride = store->layout.stride[stream];
    box_render_copy_t *c = box_render_packet_buffer_copy(p);
    c->src = box_draws_store_handle(store, b->from_page, stream);
    c->src_offset = b->from_first * stride;
    c->dst = box_draws_store_handle(store, b->page, stream);
    c->dst_offset = b->first * stride;
    c->size = count * stride;
    d->stats.copies ++;
    d->stats.copy_bytes += c->size;
}

/** Check whether the run a batch moved out of still holds its instances on the GPU: its page
 * was not released in the extract, which applies before the copies.
 *
 * @param d The state
 * @param b The batch
 * @return Whether the instances can be copied from the old run
 */
static bool box_draws_from_held(
    box_render_draws_t *d,
    const box_draw_batch_t *b)
{
    box_draw_store_t *store = box_draws_batch_store(d, b);
    const box_draw_page_t *pg;
    bool result = false;
    if (!b->from_count || b->from_page >= ecs_vec_count(&store->pages)) {
        goto done;
    }

    pg = box_draws_store_page(store, b->from_page);
    result = pg->alive && pg->serial == b->from_serial;
done:
    return result;
}

/** Get the bytes of one scatter entry of a stream: the slot and the instance data.
 *
 * @param s The store of the pages of the stream
 * @param stream The stream
 * @return The bytes
 */
static uint32_t box_draws_entry_size(
    const box_draw_store_t *s,
    int32_t stream)
{
    return (uint32_t)sizeof(uint32_t) + s->layout.stride[stream];
}

/** Get the bytes of a scatter segment, aligned for the next segment.
 *
 * @param s The store of the pages of the stream
 * @param stream The stream of the segment
 * @param entries Entries of the segment
 * @return The bytes, 0 for a segment without entries
 */
static uint64_t box_draws_segment_size(
    const box_draw_store_t *s,
    int32_t stream,
    uint32_t entries)
{
    uint64_t size = BOX_DRAWS_HEADER + (uint64_t)entries * box_draws_entry_size(s, stream);

    return entries
        ? (size + BOX_RENDER_SCATTER_ALIGN - 1) & ~(uint64_t)(BOX_RENDER_SCATTER_ALIGN - 1)
        : 0;
}

void box_draws_batch_textures(
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    box_render_batch_t *out)
{
    int32_t i;
    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        ecs_entity_t e = b->key.textures[i];
        out->textures[i] = e ? box_textures_handle(d->textures, e) : 0;
        if (e && !out->textures[i] && d->world) {
            out->textures[i] = box_render_texture_target_handle(d->world, e);
        }
    }

    if (b->key.atlas) {
        out->textures[2] = b->key.atlas;
    }

    out->foam = b->key.foam ? box_textures_handle(d->textures, b->key.foam) : 0;
    if (b->key.foam && !out->foam && d->world) {
        out->foam = box_render_texture_target_handle(d->world, b->key.foam);
    }

    out->wrap = b->key.wrap;
}

void box_draws_batch_blend(
    box_render_packet_t *p,
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    box_render_batch_t *out)
{
    const TileTerrain *terrain = b->key.texture_blend && d->world
        ? ecs_get(d->world, b->key.textures[0], TileTerrain)
        : NULL;
    const TextureBlend *blend = b->key.texture_blend && d->world && !terrain
        ? ecs_get(d->world, b->key.textures[0], TextureBlend)
        : NULL;
    uint32_t offset = 0;
    int32_t k;
    if (!b->key.texture_blend) {
        goto done;
    }

    box_render_texture_blend_t *r = box_render_packet_reserve(p,
        (uint32_t)sizeof(box_render_texture_blend_t), &offset);
    ecs_os_memset_t(r, 0, box_render_texture_blend_t);
    r->count = 1;
    r->quality = d->textures && d->textures->quality
        ? (uint32_t)d->textures->quality : BOX_TEXTURE_QUALITY_DEFAULT;
    for (k = 0; k < BOX_RENDER_BLEND_LAYERS; k ++) {
        r->transforms[k][0] = 1.0f;
        r->transforms[k][1] = 1.0f;
    }

    if (terrain) {
        box_draws_batch_tiles(d, b->key.textures[0], terrain, r);
    } else if (blend) {
        float contrast = isfinite(blend->contrast) && blend->contrast > 0.0f
            ? blend->contrast : 0.0f;
        r->mask = box_draws_texture_handle(d, blend->mask);
        r->contrast = contrast;
        for (k = 0; k < BOX_RENDER_BLEND_LAYERS; k ++) {
            ecs_entity_t layer = blend->layers[k];
            bool alive = layer && ecs_is_alive(d->world, layer);
            const PbrTextures *textures = alive ? ecs_get(d->world, layer, PbrTextures) : NULL;
            const TextureTransform *uv = alive
                ? ecs_get(d->world, layer, TextureTransform) : NULL;
            if (!layer) {
                continue;
            }

            r->count = (uint32_t)k + 1;
            if (contrast > 0.0f) {
                r->layers[k][BOX_RENDER_BLEND_HEIGHT] = box_draws_texture_handle(d,
                    blend->heights[k]);
            }

            if (textures) {
                r->layers[k][BOX_RENDER_BLEND_ALBEDO] = box_draws_texture_handle(d,
                    textures->albedo);
                r->layers[k][BOX_RENDER_BLEND_ORM] = box_draws_texture_handle(d, textures->orm);
                r->layers[k][BOX_RENDER_BLEND_NORMAL] = box_draws_texture_handle(d,
                    textures->normal);
            }

            if (uv) {
                r->transforms[k][0] = uv->scale_x;
                r->transforms[k][1] = uv->scale_y;
                r->transforms[k][2] = uv->offset_x;
                r->transforms[k][3] = uv->offset_y;
            }
        }
    }

    out->texture_blend = true;
    out->blend_layers = offset;
    out->textures[0] = 0;
    out->textures[2] = 0;
    out->textures[3] = 0;
done:
    return;
}

/** Write the instance page, the run and the bounds of a batch into its draw.
 *
 * @param d The state
 * @param b The batch
 * @param out The draw
 */
static void box_draws_batch_page(
    box_render_draws_t *d,
    const box_draw_batch_t *b,
    box_render_batch_t *out)
{
    out->instances = box_draws_store_handle(box_draws_batch_store(d, b), b->page,
        BOX_RENDER_STREAM_TRANSFORM);
    out->first_instance = b->first;
    out->bounded = b->bounded;
    if (b->bounded) {
        glm_vec3_copy(ECS_CONST_CAST(float*, b->center), out->center);
        glm_vec3_copy(ECS_CONST_CAST(float*, b->extent), out->extent);
    }

    out->bounds = b->world_valid && !b->world_dirty;
    if (out->bounds) {
        glm_vec3_copy(ECS_CONST_CAST(float*, b->world_min), out->min);
        glm_vec3_copy(ECS_CONST_CAST(float*, b->world_max), out->max);
        out->radius = b->world_radius;
    }
}

/** Send a stream of a batch that moved to a new run.
 *
 * The GPU copies the instances of the old run into the new run, and the slots past them upload.
 * Changed copied slots go into the scatter segment, unless that is not smaller than the rest of
 * the copy or outgrows the change buffer; then the copy stops at the first changed slot.
 *
 * A stream whose copy would make the copies of the frame outgrow BOX_RENDER_COPY_MAX uploads its
 * run.
 *
 * @param p The packet
 * @param d The state
 * @param b The batch
 * @param stream The stream
 * @param changes Bytes the scatter segments of the frame take, updated
 * @param copies Bytes the copies of the frame move, updated
 */
static void box_draws_send_moved(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_draw_batch_t *b,
    int32_t stream,
    uint64_t *changes,
    uint64_t *copies)
{
    box_draw_store_t *store = box_draws_batch_store(d, b);
    box_draw_page_t *pg = box_draws_store_page(store, b->page);
    ecs_vec_t *changed = &b->changed[stream];
    int32_t *slots = ecs_vec_first(changed);
    int32_t i, count = ecs_vec_count(changed);
    uint32_t stride = store->layout.stride[stream];
    uint32_t held = b->from_count, first = held, entries = 0;
    if (*copies + (uint64_t)held * stride > BOX_RENDER_COPY_MAX) {
        box_draws_upload_all(p, d, b, stream);
        goto done;
    }

    for (i = 0; i < count; i ++) {
        if ((uint32_t)slots[i] < held) {
            first = (uint32_t)slots[i] < first ? (uint32_t)slots[i] : first;
            slots[entries ++] = slots[i];
        }
    }

    ecs_vec_set_count_t(NULL, changed, int32_t, (int32_t)entries);
    if (entries) {
        uint64_t grow = box_draws_segment_size(store, stream, pg->entries[stream] + entries) -
            box_draws_segment_size(store, stream, pg->entries[stream]);
        if ((uint64_t)entries * box_draws_entry_size(store, stream) >=
            (uint64_t)(held - first) * stride || *changes + grow > BOX_DRAWS_MAX_CHANGES)
        {
            held = first;
            ecs_vec_clear(changed);
        } else {
            pg->entries[stream] += entries;
            *changes += grow;
        }
    }

    if (held) {
        box_draws_copy(p, d, b, stream, held);
        *copies += (uint64_t)held * stride;
    }

    box_draws_upload_from(p, d, b, stream, held);
done:
    return;
}

/** Upload the instances of a batch it needs this frame and add its draw.
 *
 * A stream the batch uploads every frame, or every instance of, uploads its run. Any other
 * stream of a batch that moved to a new run copies the instances of its old run on the GPU
 * (box_draws_send_moved).
 *
 * The changed slots of any other stream go into the scatter segment of its page and stream,
 * unless they are not smaller than its run or the segments of the frame would outgrow the
 * change buffer; the stream uploads its run then.
 *
 * @param p The packet
 * @param d The state
 * @param b The batch
 * @param changes Bytes the scatter segments of the frame take, updated
 * @param copies Bytes the copies of the frame move, updated
 */
static void box_draws_send_batch(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_draw_batch_t *b,
    uint64_t *changes,
    uint64_t *copies)
{
    box_draw_store_t *store = box_draws_batch_store(d, b);
    uint32_t count = (uint32_t)ecs_vec_count(&b->entities);
    uint8_t dynamic = box_draws_dynamic_kinds(b);
    bool moved = box_draws_from_held(d, b);
    int32_t s;
    if (b->from_count && !moved) {
        b->full = box_draws_batch_kinds(b);
    }

    for (s = 0; s < store->layout.streams; s ++) {
        uint32_t entries = (uint32_t)ecs_vec_count(&b->changed[s]);
        if ((dynamic | b->full) & (1 << s)) {
            box_draws_upload_all(p, d, b, s);
        } else if (moved) {
            box_draws_send_moved(p, d, b, s, changes, copies);
        } else if (entries) {
            box_draw_page_t *pg = box_draws_store_page(store, b->page);
            uint64_t grow = box_draws_segment_size(store, s, pg->entries[s] + entries) -
                box_draws_segment_size(store, s, pg->entries[s]);
            uint64_t run = (uint64_t)count * store->layout.stride[s];
            if ((uint64_t)entries * box_draws_entry_size(store, s) >= run ||
                *changes + grow > BOX_DRAWS_MAX_CHANGES)
            {
                box_draws_upload_all(p, d, b, s);
            } else {
                pg->entries[s] += entries;
                *changes += grow;
            }
        }
    }

    b->from_count = 0;
    b->sent = count;
    b->ready = true;
    if (b->key.blend != Opaque || b->key.pipeline == BOX_RENDER_SHADER_SHAPE2D) {
        goto done;
    }

    box_render_batch_t *out = box_render_packet_batch(p);
    b->record_frame = p->frame;
    b->record = ecs_vec_count(&p->batches) - 1;
    out->scene = b->key.scene;
    out->pipeline = b->key.pipeline;
    out->features = b->key.features;
    out->blend = b->key.blend;
    out->back_face = b->key.back_face;
    out->cutout = b->key.cutout;
    out->transmissive = b->key.transmissive;
    out->water = b->key.water;
    out->shadow = b->key.shadow;
    out->fixture = b->key.fixture;
    out->bake = b->key.bake;
    out->surface = b->key.surface;
    out->mesh = b->key.mesh;
    box_draws_batch_textures(d, b, out);
    box_draws_batch_blend(p, d, b, out);
    box_draws_batch_page(d, b, out);
    out->instance_count = count;
done:
    return;
}

/** Order blended instances back to front, then by batch and slot for equal depths.
 *
 * @param a The first instance
 * @param b The second instance
 * @return Negative when a draws first
 */
static int box_draws_sorted_cmp(
    const void *a,
    const void *b)
{
    const box_draw_sorted_t *sa = a, *sb = b;
    int result = (sa->depth > sb->depth) - (sa->depth < sb->depth);
    if (!result) {
        result = (sa->batch > sb->batch) - (sa->batch < sb->batch);
    }

    if (!result) {
        result = (sa->slot > sb->slot) - (sa->slot < sb->slot);
    }

    return result;
}

/** Collect the blended instances a view draws with their view depth.
 *
 * @param d The state
 * @param v The view
 */
static void box_draws_collect(
    box_render_draws_t *d,
    const box_render_view_t *v)
{
    int32_t i, s;
    const vec4 *m = v->view_matrix;
    ecs_vec_clear(&d->sorted);
    for (i = 0; i < ecs_vec_count(&d->blended); i ++) {
        int32_t index = *ecs_vec_get_t(&d->blended, int32_t, i);
        box_draw_batch_t *b = box_draws_batch(d, index);
        int32_t count = ecs_vec_count(&b->transforms);
        const box_render_transform_t *t = ecs_vec_first(&b->transforms);
        if (b->key.scene != v->scene) {
            continue;
        }

        box_draw_sorted_t *out = ecs_vec_grow_t(NULL, &d->sorted, box_draw_sorted_t, count);
        for (s = 0; s < count; s ++) {
            float x = t[s].rows[0][3], y = t[s].rows[1][3], z = t[s].rows[2][3];
            out[s].depth = m[0][2] * x + m[1][2] * y + m[2][2] * z + m[3][2];
            out[s].batch = index;
            out[s].slot = s;
        }
    }
}

/** Add the blended instances of every view, sorted back to front, after the opaque batches.
 *
 * The sorted page slots of a view go into the packet data; a run of one batch in that order is
 * one draw listing its slots, split every BOX_DRAWS_BLENDED_RUN slots. Sort and packet storage
 * is reused, so a frame that sorts no more instances than before does not allocate.
 *
 * @param p The packet
 * @param d The state
 */
static void box_draws_send_blended(
    box_render_packet_t *p,
    box_render_draws_t *d)
{
    int32_t v, i;
    for (v = 0; v < ecs_vec_count(&p->views); v ++) {
        box_render_view_t *view = ecs_vec_get_t(&p->views, box_render_view_t, v);
        box_render_batch_t *out = NULL;
        uint32_t offset = 0;
        box_draws_collect(d, view);
        int32_t count = ecs_vec_count(&d->sorted);
        box_draw_sorted_t *sorted = ecs_vec_first(&d->sorted);
        if (count > 1) {
            qsort(sorted, (size_t)count, sizeof(box_draw_sorted_t), box_draws_sorted_cmp);
        }

        uint32_t *slots = box_render_packet_reserve(p,
            (uint32_t)count * (uint32_t)sizeof(uint32_t), &offset);
        for (i = 0; i < count; i ++) {
            slots[i] = box_draws_batch(d, sorted[i].batch)->first + (uint32_t)sorted[i].slot;
        }

        for (i = 0; i < count; i ++) {
            box_draw_batch_t *b = box_draws_batch(d, sorted[i].batch);
            if (out && sorted[i].batch == sorted[i - 1].batch &&
                out->instance_count < BOX_DRAWS_BLENDED_RUN)
            {
                out->instance_count ++;
                continue;
            }

            out = box_render_packet_batch(p);
            out->view = v;
            out->scene = b->key.scene;
            out->pipeline = b->key.pipeline;
            out->features = b->key.features;
            out->blend = b->key.blend;
            out->back_face = b->key.back_face;
            out->cutout = b->key.cutout;
            out->transmissive = b->key.transmissive;
    out->water = b->key.water;
            out->shadow = b->key.shadow;
            out->fixture = b->key.fixture;
            out->surface = b->key.surface;
            out->mesh = b->key.mesh;
            box_draws_batch_textures(d, b, out);
            box_draws_batch_blend(p, d, b, out);
            box_draws_batch_page(d, b, out);
            out->first_instance = 0;
            out->listed = true;
            out->slots = offset + (uint32_t)i * (uint32_t)sizeof(uint32_t);
            out->instance_count = 1;
        }
    }
}

/** Write the changed slots of a stream of a batch into the scatter segment of its page and
 * stream.
 *
 * @param d The state
 * @param b The batch
 * @param stream The stream
 * @param data Start of the change buffer upload
 */
static void box_draws_scatter(
    box_render_draws_t *d,
    box_draw_batch_t *b,
    int32_t stream,
    uint8_t *data)
{
    box_draw_store_t *store = box_draws_batch_store(d, b);
    box_draw_page_t *pg = box_draws_store_page(store, b->page);
    const ecs_vec_t *changed = &b->changed[stream];
    const uint8_t *src = ecs_vec_first(box_draws_stream(b, stream));
    uint32_t stride = store->layout.stride[stream];
    uint32_t size = box_draws_entry_size(store, stream);
    uint8_t *entry = data + pg->offset[stream] + BOX_DRAWS_HEADER + pg->written[stream] * size;
    int32_t i, count = ecs_vec_count(changed), slots = ecs_vec_count(&b->entities);
    for (i = 0; i < count; i ++) {
        int32_t slot = *ecs_vec_get_t(changed, int32_t, i);
        uint32_t index = b->first + (uint32_t)slot;
        if (slot >= slots) {
            continue;
        }

        ecs_os_memcpy(entry, &index, ECS_SIZEOF(uint32_t));
        ecs_os_memcpy(entry + sizeof(uint32_t), src + (size_t)slot * stride, (ecs_size_t)stride);
        entry += size;
        pg->written[stream] ++;
    }

    ecs_vec_clear(&b->changed[stream]);
}

/** Upload the change buffer with the segment of every page and stream with changes and add one
 * scatter per page and stream, holding the changed slots of that stream of every batch in the
 * page.
 *
 * @param p The packet
 * @param d The state
 * @param total Bytes of all segments
 */
static void box_draws_send_changes(
    box_render_packet_t *p,
    box_render_draws_t *d,
    uint64_t total)
{
    uint32_t offset = 0;
    int32_t i, s, st, pages, count = ecs_vec_count(&d->batches);
    if (total > d->changes_size) {
        uint64_t size = d->changes_size ? d->changes_size : BOX_DRAWS_MIN_CHANGES;
        while (size < total) {
            size *= 2;
        }

        if (size > BOX_DRAWS_MAX_CHANGES) {
            size = BOX_DRAWS_MAX_CHANGES;
        }

        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, BOX_RENDER_BUFFER_CHANGES, sizeof(box_render_buffer_desc_t));
        desc->size = size;
        desc->usage = BOX_RENDER_BUFFER_STORAGE;
        d->changes_size = (uint32_t)size;
    }

    uint8_t *data = box_render_packet_upload(p, BOX_RENDER_BUFFER_CHANGES, 0, (uint32_t)total);
    for (st = 0; st < BOX_DRAWS_STORES; st ++) {
        box_draw_store_t *store = &d->stores[st];
        pages = ecs_vec_count(&store->pages);
        for (i = 0; i < pages; i ++) {
            box_draw_page_t *pg = box_draws_store_page(store, i);
            for (s = 0; s < store->layout.streams; s ++) {
                uint64_t size = box_draws_segment_size(store, s, pg->entries[s]);
                pg->offset[s] = offset;
                pg->written[s] = 0;
                offset += (uint32_t)size;
                box_draws_count_bytes(d, s, (int64_t)size);
            }
        }
    }

    for (i = 0; i < count; i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        for (s = 0; b->alive && b->ready && s < BOX_DRAWS_STREAMS; s ++) {
            if (ecs_vec_count(&b->changed[s])) {
                box_draws_scatter(d, b, s, data);
            }
        }
    }

    for (st = 0; st < BOX_DRAWS_STORES; st ++) {
        box_draw_store_t *store = &d->stores[st];
        pages = ecs_vec_count(&store->pages);
        for (i = 0; i < pages; i ++) {
            box_draw_page_t *pg = box_draws_store_page(store, i);
            for (s = 0; s < store->layout.streams; s ++) {
                uint32_t *header = (uint32_t*)(data + pg->offset[s]);
                if (!pg->entries[s]) {
                    continue;
                }

                header[0] = pg->written[s];
                header[1] = store->layout.stride[s] / (uint32_t)sizeof(uint32_t);
                header[2] = 0;
                header[3] = 0;
                pg->entries[s] = 0;
                if (pg->written[s]) {
                    box_render_scatter_t *sc = box_render_packet_scatter(p);
                    sc->buffer = box_draws_store_handle(store, i, s);
                    sc->changes = BOX_RENDER_BUFFER_CHANGES;
                    sc->offset = pg->offset[s];
                    sc->size = BOX_DRAWS_HEADER + pg->written[s] * box_draws_entry_size(store, s);
                    sc->count = pg->written[s];
                    d->stats.scattered += (int32_t)pg->written[s];
                    d->stats.scatters ++;
                }
            }
        }
    }
}

/** Get the step between the rows of an optional field, 0 when its value is inherited.
 *
 * @param it The query result
 * @param ptr The field
 * @param size Size of an element
 * @param index The field index
 * @return The step in bytes
 */
static ecs_size_t box_draws_stride(
    const ecs_iter_t *it,
    const void *ptr,
    ecs_size_t size,
    int8_t index)
{
    return ptr && ecs_field_is_self(it, index) ? size : 0;
}

/** Get the value of an optional field for a row.
 *
 * @param ptr The field
 * @param stride Step between rows
 * @param row The row
 * @return The value, NULL when the field is not set
 */
static const void* box_draws_at(
    const void *ptr,
    ecs_size_t stride,
    int32_t row)
{
    return ptr ? ECS_OFFSET(ptr, stride * row) : NULL;
}

/** Write the shading of the dynamic material shapes of a query result into their instances.
 *
 * A mesh shape writes the slot of its material, its tint and its alpha, and packs the slot it
 * owns again; a shape that renders with the slot of its prefab leaves the slot as it is. A 2D
 * shape writes its whole material.
 *
 * A shape whose resolved blend mode or alpha test no longer matches its batch is queued to
 * move, unless it is transmissive, which ignores both.
 *
 * @param it The query result
 * @param d The state
 * @param walk Whether the opacity and tint of ancestors are read every frame
 */
static void box_draws_fill_material(
    ecs_iter_t *it,
    box_render_draws_t *d,
    bool walk)
{
    const DrawSlot *slots = ecs_field(it, DrawSlot, 1);
    const void *colors = ecs_field(it, Rgb, 3);
    const void *opacities = ecs_field(it, Opacity, 4);
    const void *pbrs = ecs_field(it, PbrMaterial, 5);
    const void *emissives = ecs_field(it, Emissive, 6);
    const void *modes = ecs_field(it, BlendMode, 7);
    const void *uvs = ecs_field(it, TextureTransform, 8);
    const Shape2D *shapes2d = ecs_field(it, Shape2D, 11);
    const void *cutoffs = ecs_field(it, AlphaCutoff, 12);
    const Tint *tints = ecs_table_get_id(it->world, it->table, ecs_id(Tint), it->offset);
    const BoxShadowSlot *shadows = shapes2d ? box_draws_box_shadows(it) : NULL;
    ecs_size_t color_step = box_draws_stride(it, colors, ECS_SIZEOF(Rgb), 3);
    ecs_size_t opacity_step = box_draws_stride(it, opacities, ECS_SIZEOF(Opacity), 4);
    ecs_size_t pbr_step = box_draws_stride(it, pbrs, ECS_SIZEOF(PbrMaterial), 5);
    ecs_size_t emissive_step = box_draws_stride(it, emissives, ECS_SIZEOF(Emissive), 6);
    ecs_size_t mode_step = box_draws_stride(it, modes, ECS_SIZEOF(BlendMode), 7);
    ecs_size_t uv_step = box_draws_stride(it, uvs, ECS_SIZEOF(TextureTransform), 8);
    ecs_size_t cutoff_step = box_draws_stride(it, cutoffs, ECS_SIZEOF(AlphaCutoff), 12);
    box_draw_tint_t inherited;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (slots[i].batch < 0) {
            continue;
        }

        box_draw_batch_t *b = box_draws_batch(d, slots[i].batch);
        if (!b->key.dynamic_material || slots[i].slot >= ecs_vec_count(&b->entities)) {
            continue;
        }

        box_draw_material_t material = {
            .color = box_draws_at(colors, color_step, i),
            .opacity = box_draws_at(opacities, opacity_step, i),
            .cutoff = box_draws_at(cutoffs, cutoff_step, i),
            .pbr = box_draws_at(pbrs, pbr_step, i),
            .emissive = box_draws_at(emissives, emissive_step, i),
            .uv = box_draws_at(uvs, uv_step, i),
            .shape2d = shapes2d ? &shapes2d[i] : NULL,
            .tint = tints ? &tints[i] : NULL,
            .inherited = slots[i].tinted ? &slots[i].tint : NULL,
            .fade = slots[i].fade,
            .clip = shapes2d ? slots[i].clip : 0,
            .slice = shapes2d ? slots[i].slice : 0,
            .rank = shapes2d ? slots[i].rank : 0
        };
        if (walk) {
            material.fade = box_draws_fade(it->world, it->entities[i], &inherited);
            material.inherited = &inherited;
        }

        float alpha;
        if (box_draws_is_2d(b)) {
            box_render_shading_t *out = box_draws_slot_shading(b, slots[i].slot);
            box_draws_material(out, &material);
            alpha = box_draws_alpha(&material);
            if (shadows) {
                box_draws_box_shadow_write(it->world, d, it->entities[i], &shadows[i],
                    &slots[i], NULL, &material, BOX_DRAW_SHADING);
            }
        } else {
            uint32_t id = box_draws_material_id(it->world, d, it->entities[i], true, true);
            box_render_mesh_shading_t *out = box_draws_slot_mesh_shading(b, slots[i].slot);
            box_render_mesh_shading_t was = *out;
            box_draws_material_changed(d, it->entities[i]);
            box_draws_mesh_shading(out, box_draws_planet_material(id, b->key.planet),
                &material);
            alpha = box_draws_alpha(&material);
            if (b->key.cutout && (was.material != out->material || was.alpha != out->alpha)) {
                box_render_transform_t *transform = box_draws_slot_transform(b, slots[i].slot);
                box_draws_shadow_region(it->world, d, b, transform,
                    box_draws_slot_surface(b, slots[i].slot));
            }
        }

        bool cutout = box_draws_scene_cutout(material.cutoff, material.shape2d,
            b->key.features);
        uint8_t blend = box_draws_scene_blend(box_draws_at(modes, mode_step, i), alpha, cutout,
            material.shape2d, b->key.features);
        if (!b->key.transmissive && (blend != b->key.blend || cutout != b->key.cutout)) {
            box_draws_mark(it->world, d, it->entities[i], BOX_DRAW_ALL);
        }
    }
}

/** Write the transforms of the dynamic shapes of a query result into their instances.
 *
 * @param it The query result
 * @param d The state
 */
static void box_draws_fill_transform(
    ecs_iter_t *it,
    box_render_draws_t *d)
{
    const ShapeMesh *shapes = ecs_field(it, ShapeMesh, 0);
    const DrawSlot *slots = ecs_field(it, DrawSlot, 1);
    const WorldTransform *transforms = ecs_field(it, WorldTransform, 2);
    const BoxShadowSlot *shadows = box_draws_box_shadows(it);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (slots[i].batch < 0) {
            continue;
        }

        box_draw_batch_t *b = box_draws_batch(d, slots[i].batch);
        if (!b->key.dynamic_transform || slots[i].slot >= ecs_vec_count(&b->transforms)) {
            continue;
        }

        box_render_transform_t *out = box_draws_slot_transform(b, slots[i].slot);
        box_render_transform_t was = *out;
        box_draws_transform(out, &transforms[i], &shapes[i]);
        if (b->key.frame) {
            box_draws_frame_of_shape(it->world, it->entities[i],
                box_draws_slot_frame(b, slots[i].slot), out);
        }

        if (shadows) {
            box_draws_box_shadow_write(it->world, d, it->entities[i], &shadows[i], &slots[i],
                &transforms[i], NULL, BOX_DRAW_TRANSFORM);
        }

        if (!box_draws_is_2d(b) && d->shadow_record &&
            ecs_os_memcmp_t(&was, out, box_render_transform_t))
        {
            const box_render_surface_t *surface = box_draws_slot_surface(b, slots[i].slot);
            box_draws_shadow_region(it->world, d, b, &was, surface);
            box_draws_shadow_region(it->world, d, b, out, surface);
        }
    }
}

/** Write the streams of every dynamic shape that upload every frame: the transforms of the
 * DynamicTransform shapes and the shading of the DynamicMaterial shapes. A DynamicMaterial
 * shape without DynamicTransform keeps its static transform.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_dynamic(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    ecs_iter_t it = ecs_query_iter(world, d->dynamic_transform);
    while (ecs_query_next(&it)) {
        box_draws_fill_transform(&it, d);
        if (ecs_field_is_set(&it, 10)) {
            box_draws_fill_material(&it, d, !ecs_field_is_self(&it, 10));
        }
    }

    it = ecs_query_iter(world, d->dynamic_material);
    while (ecs_query_next(&it)) {
        if (!box_transform_dynamic(world, it.table)) {
            box_draws_fill_material(&it, d, !ecs_field_is_self(&it, 9));
        }
    }
}

void box_draws_send(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_render_packet_t *p)
{
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    int32_t i, count = ecs_vec_count(&d->batches);
    uint64_t changes = 0, copies = 0;
    bool views = ecs_vec_count(&p->views) != 0, bounds = false;
    d->textures = box_textures_get(world);
    d->world = world;
    d->stats.batches = 0;
    d->stats.box_shadows = d->box_shadows;
    d->stats.instances = 0;
    d->stats.dynamic = 0;
    d->stats.scatters = 0;
    for (i = 0; i < count; i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        if (b->alive && !ecs_vec_count(&b->entities)) {
            box_draws_batch_free(d, p, i);
        }

        if (!b->alive) {
            continue;
        }

        d->stats.batches ++;
        if (b->key.dynamic_transform || b->key.dynamic_material) {
            d->stats.dynamic += ecs_vec_count(&b->entities);
        } else {
            d->stats.instances += ecs_vec_count(&b->entities);
        }
    }

    box_draws_release(p, d, cache);
    if (!views) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&p->views); i ++) {
        bounds |= ecs_vec_get_t(&p->views, box_render_view_t, i)->cull.bounds;
    }

    box_draws_dynamic(world, d);
    box_draws_materials_send(world, d, p);
    box_draws_shadow_materials(world, d);
    ecs_vec_clear(&d->blended);
    for (i = 0; i < count; i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        b->ready = b->alive && box_draws_mesh(p, d, cache, b->key.mesh);
        if (b->ready) {
            box_draws_reserve(p, d, b);
        }

        uint32_t texture = box_draws_shadow_texture(d, b);
        if (b->ready && (!b->was_ready || texture != b->shadow_texture)) {
            box_draws_shadow_batch(world, d, b);
        }

        b->was_ready = b->ready;
        b->shadow_texture = texture;
    }

    box_draws_pages(p, d);
    for (i = 0; i < count; i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        if (b->ready && b->bounds_dirty) {
            box_draws_bounds(b, cache);
        }

        if (b->ready) {
            box_draws_world_bounds(b, cache, bounds);
            box_draws_send_batch(p, d, b, &changes, &copies);
        }

        if (b->ready && b->key.blend != Opaque &&
            b->key.pipeline != BOX_RENDER_SHADER_SHAPE2D)
        {
            *ecs_vec_append_t(NULL, &d->blended, int32_t) = i;
        }
    }

    box_draws_send_ordered(p, d);
    if (ecs_vec_count(&d->blended)) {
        box_draws_send_blended(p, d);
    }

    if (changes) {
        box_draws_send_changes(p, d, changes);
    }

done:
    d->stats.pages = 0;
    d->stats.page_slots = 0;
    int64_t bytes[BOX_DRAWS_STREAMS] = {0};
    for (i = 0; i < BOX_DRAWS_STORES; i ++) {
        d->stats.pages += d->stores[i].alive;
        box_draws_store_memory(&d->stores[i], &d->stats.page_slots, bytes);
    }

    d->stats.page_transform_bytes = bytes[BOX_RENDER_STREAM_TRANSFORM];
    d->stats.page_shading_bytes = bytes[BOX_RENDER_STREAM_SHADING];
    d->stats.page_surface_bytes = bytes[BOX_RENDER_STREAM_SURFACE];
    d->stats.page_frame_bytes = bytes[BOX_RENDER_STREAM_FRAME];
    d->stats.page_bytes = 0;
    for (i = 0; i < BOX_DRAWS_STREAMS; i ++) {
        d->stats.page_bytes += bytes[i];
    }

    d->stats.materials = d->materials.alive;

    return;
}

void box_draws_batches_fini(
    box_render_draws_t *d)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&d->batches); i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        ecs_vec_fini_t(NULL, &b->entities, ecs_entity_t);
        ecs_vec_fini_t(NULL, &b->glyphs, int32_t);
        ecs_vec_fini_t(NULL, &b->transforms, box_render_transform_t);
        ecs_vec_fini_t(NULL, &b->shadings, box_render_shading_t);
        ecs_vec_fini_t(NULL, &b->mesh_shadings, box_render_mesh_shading_t);
        ecs_vec_fini_t(NULL, &b->surfaces, box_render_surface_t);
        ecs_vec_fini_t(NULL, &b->frames, box_render_transform_t);
        for (int32_t s = 0; s < BOX_DRAWS_STREAMS; s ++) {
            ecs_vec_fini_t(NULL, &b->changed[s], int32_t);
        }
    }

    ecs_vec_fini_t(NULL, &d->batches, box_draw_batch_t);
    ecs_vec_fini_t(NULL, &d->free_batches, int32_t);
    for (i = 0; i < BOX_DRAWS_STORES; i ++) {
        box_draws_store_fini(&d->stores[i]);
    }

    ecs_map_fini(&d->lookup);
}

void box_render_draws_page(
    ecs_world_t *world,
    int32_t slots)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    if (s && s->ctx && slots > 0) {
        box_render_draws_t *d = s->ctx;
        for (int32_t i = 0; i < BOX_DRAWS_STORES; i ++) {
            d->stores[i].slots = (uint32_t)slots < d->stores[i].max
                ? (uint32_t)slots : d->stores[i].max;
        }
    }
}

void box_render_draws_chunk(
    ecs_world_t *world,
    int32_t instances)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    if (s && s->ctx && instances > 0) {
        box_render_draws_t *d = s->ctx;
        for (int32_t i = 0; i < BOX_DRAWS_STORES; i ++) {
            d->stores[i].chunk = (uint32_t)instances < d->stores[i].max
                ? (uint32_t)instances : d->stores[i].max;
        }
    }
}
