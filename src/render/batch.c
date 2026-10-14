/**
 * @file render/batch.c
 * @brief Persistent instanced batches and what a frame sends of them to the renderer.
 *
 * Static batches upload all instances when their buffer is created and afterwards only the
 * changed slots, through a segment of the change buffer that the scatter pass copies.
 */

#include "draw.h"

/** Color of a shape without Rgb, display encoded */
static const uint8_t box_draws_default_color[3] = { 204, 204, 204 };

/** Instances the smallest instance buffer holds */
#define BOX_DRAWS_MIN_CAPACITY (64)

/** Bytes the smallest change buffer holds */
#define BOX_DRAWS_MIN_CHANGES (64 * 1024)

/** Words of one instance */
#define BOX_DRAWS_WORDS ((uint32_t)(sizeof(box_render_instance_t) / sizeof(uint32_t)))

/** Bytes of one scatter entry: the slot and the instance */
#define BOX_DRAWS_ENTRY ((uint32_t)sizeof(uint32_t) + (uint32_t)sizeof(box_render_instance_t))

/** Bytes of the header of a scatter segment */
#define BOX_DRAWS_HEADER ((uint32_t)(BOX_RENDER_SCATTER_HEADER * sizeof(uint32_t)))

/** Linear light of every display encoded channel value */
static float box_draws_srgb[256];

void box_draws_tables_init(void)
{
    int32_t i;
    for (i = 0; i < 256; i ++) {
        float c = (float)i / 255.0f;
        box_draws_srgb[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    }
}

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static float box_draws_linear(
    uint8_t value)
{
    return box_draws_srgb[value];
}

/** Convert a display encoded channel from 0 to 1 to linear light.
 *
 * Interpolates the table of the byte values, which stays within 1e-5 of the exact curve.
 *
 * @param value The channel
 * @return The linear value
 */
static float box_draws_linear_f(
    float value)
{
    float x = glm_clamp(value, 0.0f, 1.0f) * 255.0f;
    int32_t i = (int32_t)x;
    if (i > 254) {
        i = 254;
    }

    float f = x - (float)i;

    return box_draws_srgb[i] + (box_draws_srgb[i + 1] - box_draws_srgb[i]) * f;
}

/** Write the base color of a shape, mixed toward its tints, as linear light.
 *
 * @param out Three floats receiving the color
 * @param color The base color, display encoded
 * @param material Material components of the entity
 */
static void box_draws_tinted(
    float *out,
    const uint8_t *color,
    const box_draw_material_t *material)
{
    const box_draw_tint_t *inherited = material->inherited;
    const Tint *tint = material->tint;
    int32_t i;
    if ((!inherited || inherited->keep >= 1.0f) && (!tint || !tint->a)) {
        out[0] = box_draws_linear(color[0]);
        out[1] = box_draws_linear(color[1]);
        out[2] = box_draws_linear(color[2]);
        goto done;
    }

    const uint8_t *own = tint ? &tint->r : NULL;
    float a = tint ? (float)tint->a / 255.0f : 0.0f;
    for (i = 0; i < 3; i ++) {
        float c = (float)color[i] / 255.0f;
        if (inherited) {
            c = c * inherited->keep + inherited->add[i];
        }

        if (own) {
            c += ((float)own[i] / 255.0f - c) * a;
        }

        out[i] = box_draws_linear_f(c);
    }

done:
    return;
}

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
    h ^= ((uint64_t)key->pipeline * 2 + key->dynamic) * 0x165667B19E3779F9ull;
    h ^= (((uint64_t)key->blend * 2 + key->back_face) * 4 + key->wrap + 1) *
        0x27D4EB2F165667C5ull;
    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        h = (h ^ (h >> 31)) * 0x9E3779B97F4A7C15ull + key->textures[i];
    }

    h ^= (uint64_t)key->shadow * 0x94D049BB133111EBull;

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
        a->blend == b->blend && a->back_face == b->back_face && a->dynamic == b->dynamic &&
        a->wrap == b->wrap && !ecs_os_memcmp(a->textures, b->textures, ECS_SIZEOF(a->textures)) &&
        a->shadow == b->shadow;
}

box_draw_batch_t* box_draws_batch(
    box_render_draws_t *d,
    int32_t index)
{
    return ecs_vec_get_t(&d->batches, box_draw_batch_t, index);
}

int32_t box_draws_batch_ensure(
    box_render_draws_t *d,
    const box_draw_key_t *key)
{
    uint64_t hash = box_draws_hash(key);
    uint64_t *head = ecs_map_get(&d->lookup, hash);
    int32_t result = head ? (int32_t)*head - 1 : -1;
    box_draw_batch_t *b;
    while (result != -1) {
        b = box_draws_batch(d, result);
        if (box_draws_key_eq(&b->key, key)) {
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
        ecs_vec_init_t(NULL, &b->instances, box_render_instance_t, 0);
        ecs_vec_init_t(NULL, &b->changed, int32_t, 0);
    }

    b->key = *key;
    b->alive = true;
    b->buffer = 0;
    b->capacity = 0;
    b->full = true;
    head = ecs_map_ensure(&d->lookup, hash);
    b->next = *head ? (int32_t)*head - 1 : -1;
    *head = (uint64_t)result + 1;

done:
    return result;
}

/** Take a batch out of use, releasing its instance buffer in the packet.
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

    if (b->buffer) {
        box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY, b->buffer, 0);
        *ecs_vec_append_t(NULL, &d->free_buffers, uint32_t) = b->buffer;
    }

    ecs_vec_clear(&b->entities);
    ecs_vec_clear(&b->instances);
    ecs_vec_clear(&b->changed);
    b->alive = false;
    b->buffer = 0;
    b->capacity = 0;
    *ecs_vec_append_t(NULL, &d->free_batches, int32_t) = index;
}

void box_draws_slot_changed(
    box_draw_batch_t *b,
    int32_t slot)
{
    if (b->key.dynamic || b->full) {
        goto done;
    }

    if (ecs_vec_count(&b->changed) >= ecs_vec_count(&b->entities)) {
        b->full = true;
        ecs_vec_clear(&b->changed);
        goto done;
    }

    *ecs_vec_append_t(NULL, &b->changed, int32_t) = slot;
done:
    return;
}

int32_t box_draws_slot_add(
    box_render_draws_t *d,
    int32_t index,
    ecs_entity_t e)
{
    box_draw_batch_t *b = box_draws_batch(d, index);
    int32_t result = ecs_vec_count(&b->entities);
    *ecs_vec_append_t(NULL, &b->entities, ecs_entity_t) = e;
    ecs_vec_append_t(NULL, &b->instances, box_render_instance_t);

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
    if (slot != last) {
        ecs_entity_t moved = *ecs_vec_get_t(&b->entities, ecs_entity_t, last);
        DrawSlot *s = ecs_get_mut(world, moved, DrawSlot);
        *ecs_vec_get_t(&b->entities, ecs_entity_t, slot) = moved;
        *ecs_vec_get_t(&b->instances, box_render_instance_t, slot) =
            *ecs_vec_get_t(&b->instances, box_render_instance_t, last);
        if (s) {
            s->slot = slot;
        }

        box_draws_slot_changed(b, slot);
    }

    ecs_vec_remove_last(&b->entities);
    ecs_vec_remove_last(&b->instances);
}

float box_draws_alpha(
    const box_draw_material_t *material)
{
    float alpha = material->opacity ? material->opacity->value : 1.0f;

    return glm_clamp(alpha, 0.0f, 1.0f) * material->fade;
}

uint8_t box_draws_blend(
    const BlendMode *mode,
    float alpha)
{
    uint8_t result = mode ? (uint8_t)*mode : (uint8_t)Opaque;
    if (result == Opaque && alpha < 1.0f) {
        result = AlphaBlend;
    }

    return result;
}

void box_draws_material(
    box_render_instance_t *out,
    const box_draw_material_t *material)
{
    const Rgb *color = material->color;
    const Emissive *emissive = material->emissive;
    const PbrMaterial *pbr = material->pbr;
    const TextureTransform *uv = material->uv;
    box_draws_tinted(out->color, color ? &color->r : box_draws_default_color, material);
    out->color[3] = box_draws_alpha(material);
    float strength = emissive && emissive->strength > 0.0f ? emissive->strength : 0.0f;
    out->emissive[0] = emissive ? box_draws_linear(emissive->color.r) * strength : 0.0f;
    out->emissive[1] = emissive ? box_draws_linear(emissive->color.g) * strength : 0.0f;
    out->emissive[2] = emissive ? box_draws_linear(emissive->color.b) * strength : 0.0f;
    out->emissive[3] = 0.0f;
    out->material[0] = pbr ? glm_clamp(pbr->metallic, 0.0f, 1.0f) : 0.0f;
    out->material[1] = pbr ? glm_clamp(pbr->roughness, 0.0f, 1.0f) : 0.5f;
    out->material[2] = 0.0f;
    out->material[3] = 0.0f;
    out->uv[0] = uv ? uv->scale_x : 1.0f;
    out->uv[1] = uv ? uv->scale_y : 1.0f;
    out->uv[2] = uv ? uv->offset_x : 0.0f;
    out->uv[3] = uv ? uv->offset_y : 0.0f;
}

void box_draws_transform(
    box_render_instance_t *out,
    const WorldTransform *transform,
    const ShapeMesh *shape)
{
    if (transform) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, transform->m), out->model);
    } else {
        glm_mat4_identity(out->model);
    }

    glm_vec4_scale(out->model[0], shape->scale[0], out->model[0]);
    glm_vec4_scale(out->model[1], shape->scale[1], out->model[1]);
    glm_vec4_scale(out->model[2], shape->scale[2], out->model[2]);
}

void box_draws_instance(
    box_render_instance_t *out,
    const WorldTransform *transform,
    const ShapeMesh *shape,
    const box_draw_material_t *material)
{
    box_draws_transform(out, transform, shape);
    box_draws_material(out, material);
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

    uint32_t vertex_size = (uint32_t)mesh->vertex_count * (uint32_t)sizeof(box_vertex_t);
    uint32_t index_size = (uint32_t)mesh->index_count * (uint32_t)sizeof(uint32_t);
    box_render_mesh_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_MESH,
        BOX_RENDER_CREATE, handle,
        (uint32_t)sizeof(box_render_mesh_desc_t) + vertex_size + index_size);
    uint8_t *data = (uint8_t*)(desc + 1);
    desc->vertex_count = (uint32_t)mesh->vertex_count;
    desc->index_count = (uint32_t)mesh->index_count;
    ecs_os_memcpy(data, mesh->vertices, (ecs_size_t)vertex_size);
    ecs_os_memcpy(data + vertex_size, mesh->indices, (ecs_size_t)index_size);
    box_mesh_cache_keep(cache, handle);
    ecs_map_insert(&d->meshes, handle, 1);
done:
    return result;
}

/** Destroy the meshes only the renderer still references.
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
        if (box_mesh_cache_refs(cache, handle) <= 1) {
            *ecs_vec_append_t(NULL, &d->released, uint32_t) = handle;
        }
    }

    for (i = 0; i < ecs_vec_count(&d->released); i ++) {
        uint32_t handle = *ecs_vec_get_t(&d->released, uint32_t, i);
        box_render_packet_resource(p, BOX_RENDER_MESH, BOX_RENDER_DESTROY, handle, 0);
        box_mesh_cache_release(cache, handle);
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
        box_mesh_cache_release(cache, (uint32_t)ecs_map_key(&it));
    }

    ecs_map_clear(&d->meshes);
    for (i = 0; i < ecs_vec_count(&d->batches); i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        b->buffer = 0;
        b->capacity = 0;
        b->full = true;
        ecs_vec_clear(&b->changed);
    }

    ecs_vec_clear(&d->free_buffers);
    d->next_buffer = BOX_RENDER_BUFFER_FIRST;
    d->changes_size = 0;
}

/** Give a batch an instance buffer that holds all its instances.
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
    uint32_t count = (uint32_t)ecs_vec_count(&b->entities);
    uint32_t capacity = b->capacity ? b->capacity : BOX_DRAWS_MIN_CAPACITY;
    if (b->buffer && count <= b->capacity) {
        goto done;
    }

    while (capacity < count) {
        capacity *= 2;
    }

    if (!b->buffer && ecs_vec_count(&d->free_buffers)) {
        b->buffer = *ecs_vec_last_t(&d->free_buffers, uint32_t);
        ecs_vec_remove_last(&d->free_buffers);
    } else if (!b->buffer) {
        b->buffer = d->next_buffer ++;
    }

    box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
        BOX_RENDER_CREATE, b->buffer, sizeof(box_render_buffer_desc_t));
    desc->size = capacity * (uint32_t)sizeof(box_render_instance_t);
    desc->usage = BOX_RENDER_BUFFER_INSTANCE;
    if (!b->key.dynamic) {
        desc->usage |= BOX_RENDER_BUFFER_STORAGE;
    }

    b->capacity = capacity;
    b->full = true;
    ecs_vec_clear(&b->changed);
done:
    return;
}

/** Upload every instance of a batch.
 *
 * @param p The packet
 * @param d The state
 * @param b The batch
 */
static void box_draws_upload_all(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_draw_batch_t *b)
{
    uint32_t size = (uint32_t)ecs_vec_count(&b->instances) *
        (uint32_t)sizeof(box_render_instance_t);
    void *dst = box_render_packet_upload(p, b->buffer, 0, size);
    ecs_os_memcpy(dst, ecs_vec_first(&b->instances), (ecs_size_t)size);
    d->stats.upload_bytes += size;
    b->full = false;
    ecs_vec_clear(&b->changed);
}

/** Get the bytes of the scatter segment of a static batch.
 *
 * @param b The batch
 * @return The bytes, aligned for the next segment
 */
static uint32_t box_draws_segment_size(
    const box_draw_batch_t *b)
{
    uint32_t size = BOX_DRAWS_HEADER + (uint32_t)ecs_vec_count(&b->changed) * BOX_DRAWS_ENTRY;

    return (size + BOX_RENDER_SCATTER_ALIGN - 1) & ~(uint32_t)(BOX_RENDER_SCATTER_ALIGN - 1);
}

/** Write the texture handles and the sampler wrap of a batch into its draw.
 *
 * @param d The state
 * @param b The batch
 * @param out The draw
 */
static void box_draws_batch_textures(
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    box_render_batch_t *out)
{
    int32_t i;
    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        ecs_entity_t e = b->key.textures[i];
        out->textures[i] = e ? box_textures_handle(d->textures, e) : 0;
    }

    out->wrap = b->key.wrap;
}

/** Upload the instances of a batch it needs this frame and add its draw.
 *
 * @param p The packet
 * @param d The state
 * @param b The batch
 * @return Bytes the scatter segment of the batch needs, 0 when it does not scatter
 */
static uint32_t box_draws_send_batch(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_draw_batch_t *b)
{
    uint32_t result = 0;
    uint32_t count = (uint32_t)ecs_vec_count(&b->entities);
    box_draws_reserve(p, d, b);
    if (b->key.dynamic || b->full) {
        box_draws_upload_all(p, d, b);
    } else if (ecs_vec_count(&b->changed)) {
        result = box_draws_segment_size(b);
        if ((uint64_t)result >= (uint64_t)count * sizeof(box_render_instance_t)) {
            box_draws_upload_all(p, d, b);
            result = 0;
        }
    }

    b->ready = true;
    if (b->key.blend != Opaque) {
        goto done;
    }

    box_render_batch_t *out = box_render_packet_batch(p);
    out->scene = b->key.scene;
    out->pipeline = b->key.pipeline;
    out->blend = b->key.blend;
    out->back_face = b->key.back_face;
    out->shadow = b->key.shadow;
    out->mesh = b->key.mesh;
    box_draws_batch_textures(d, b, out);
    out->instances = b->buffer;
    out->first_instance = 0;
    out->instance_count = count;
done:
    return result;
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
        int32_t count = ecs_vec_count(&b->instances);
        const box_render_instance_t *inst = ecs_vec_first(&b->instances);
        if (b->key.scene != v->scene) {
            continue;
        }

        box_draw_sorted_t *out = ecs_vec_grow_t(NULL, &d->sorted, box_draw_sorted_t, count);
        for (s = 0; s < count; s ++) {
            const float *pos = inst[s].model[3];
            out[s].depth = m[0][2] * pos[0] + m[1][2] * pos[1] + m[2][2] * pos[2] + m[3][2];
            out[s].batch = index;
            out[s].slot = s;
        }
    }
}

/** Add the blended instances of every view, sorted back to front, after the opaque batches.
 *
 * Consecutive slots of one batch become one draw. The sort reuses the storage of earlier
 * frames, so a frame that sorts no more instances than before does not allocate.
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
        box_draws_collect(d, view);
        int32_t count = ecs_vec_count(&d->sorted);
        box_draw_sorted_t *sorted = ecs_vec_first(&d->sorted);
        if (count > 1) {
            qsort(sorted, (size_t)count, sizeof(box_draw_sorted_t), box_draws_sorted_cmp);
        }

        for (i = 0; i < count; i ++) {
            box_draw_batch_t *b = box_draws_batch(d, sorted[i].batch);
            if (out && out->instances == b->buffer &&
                out->first_instance + out->instance_count == (uint32_t)sorted[i].slot)
            {
                out->instance_count ++;
                continue;
            }

            out = box_render_packet_batch(p);
            out->view = v;
            out->scene = b->key.scene;
            out->pipeline = b->key.pipeline;
            out->blend = b->key.blend;
            out->back_face = b->key.back_face;
            out->shadow = b->key.shadow;
            out->mesh = b->key.mesh;
            box_draws_batch_textures(d, b, out);
            out->instances = b->buffer;
            out->first_instance = (uint32_t)sorted[i].slot;
            out->instance_count = 1;
        }
    }
}

/** Write the scatter segment of a batch and add its scatter.
 *
 * @param p The packet
 * @param d The state
 * @param b The batch
 * @param dst Start of the segment
 * @param offset Byte offset of the segment in the change buffer
 * @return Bytes of the segment
 */
static uint32_t box_draws_scatter(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_draw_batch_t *b,
    uint8_t *dst,
    uint32_t offset)
{
    uint32_t *header = (uint32_t*)dst;
    uint32_t *entry = header + BOX_RENDER_SCATTER_HEADER;
    uint32_t written = 0, size = box_draws_segment_size(b);
    int32_t i, count = ecs_vec_count(&b->changed), slots = ecs_vec_count(&b->entities);
    for (i = 0; i < count; i ++) {
        int32_t slot = *ecs_vec_get_t(&b->changed, int32_t, i);
        if (slot >= slots) {
            continue;
        }

        entry[0] = (uint32_t)slot;
        ecs_os_memcpy(&entry[1], ecs_vec_get_t(&b->instances, box_render_instance_t, slot),
            ECS_SIZEOF(box_render_instance_t));
        entry += 1 + BOX_DRAWS_WORDS;
        written ++;
    }

    header[0] = written;
    header[1] = BOX_DRAWS_WORDS;
    header[2] = 0;
    header[3] = 0;
    ecs_vec_clear(&b->changed);
    if (written) {
        box_render_scatter_t *sc = box_render_packet_scatter(p);
        sc->buffer = b->buffer;
        sc->changes = BOX_RENDER_BUFFER_CHANGES;
        sc->offset = offset;
        sc->size = BOX_DRAWS_HEADER + written * BOX_DRAWS_ENTRY;
        sc->count = written;
        d->stats.scattered += (int32_t)written;
    }

    return size;
}

/** Upload the change buffer with the segments of the scattering batches.
 *
 * @param p The packet
 * @param d The state
 * @param total Bytes of all segments
 */
static void box_draws_send_changes(
    box_render_packet_t *p,
    box_render_draws_t *d,
    uint32_t total)
{
    uint32_t offset = 0;
    int32_t i, count = ecs_vec_count(&d->batches);
    if (total > d->changes_size) {
        uint32_t size = d->changes_size ? d->changes_size : BOX_DRAWS_MIN_CHANGES;
        while (size < total) {
            size *= 2;
        }

        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, BOX_RENDER_BUFFER_CHANGES, sizeof(box_render_buffer_desc_t));
        desc->size = size;
        desc->usage = BOX_RENDER_BUFFER_STORAGE;
        d->changes_size = size;
    }

    uint8_t *data = box_render_packet_upload(p, BOX_RENDER_BUFFER_CHANGES, 0, total);
    d->stats.upload_bytes += total;
    for (i = 0; i < count; i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        if (b->alive && !b->key.dynamic && ecs_vec_count(&b->changed)) {
            offset += box_draws_scatter(p, d, b, data + offset, offset);
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

/** Write the material of the dynamic shapes of a query result into their instances.
 *
 * A shape whose resolved blend mode no longer matches its batch is queued to move.
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
    const void *tints = ecs_field(it, Tint, 11);
    ecs_size_t color_step = box_draws_stride(it, colors, ECS_SIZEOF(Rgb), 3);
    ecs_size_t opacity_step = box_draws_stride(it, opacities, ECS_SIZEOF(Opacity), 4);
    ecs_size_t pbr_step = box_draws_stride(it, pbrs, ECS_SIZEOF(PbrMaterial), 5);
    ecs_size_t emissive_step = box_draws_stride(it, emissives, ECS_SIZEOF(Emissive), 6);
    ecs_size_t mode_step = box_draws_stride(it, modes, ECS_SIZEOF(BlendMode), 7);
    ecs_size_t uv_step = box_draws_stride(it, uvs, ECS_SIZEOF(TextureTransform), 8);
    ecs_size_t tint_step = box_draws_stride(it, tints, ECS_SIZEOF(Tint), 11);
    box_draw_tint_t inherited;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (slots[i].batch < 0) {
            continue;
        }

        box_draw_batch_t *b = box_draws_batch(d, slots[i].batch);
        if (!b->key.dynamic || slots[i].slot >= ecs_vec_count(&b->instances)) {
            continue;
        }

        box_draw_material_t material = {
            .color = box_draws_at(colors, color_step, i),
            .opacity = box_draws_at(opacities, opacity_step, i),
            .pbr = box_draws_at(pbrs, pbr_step, i),
            .emissive = box_draws_at(emissives, emissive_step, i),
            .uv = box_draws_at(uvs, uv_step, i),
            .tint = box_draws_at(tints, tint_step, i),
            .inherited = &slots[i].tint,
            .fade = slots[i].fade
        };
        if (walk) {
            material.fade = box_draws_fade(it->world, it->entities[i], &inherited);
            material.inherited = &inherited;
        }

        box_render_instance_t *inst = ecs_vec_get_t(
            &b->instances, box_render_instance_t, slots[i].slot);
        box_draws_material(inst, &material);
        if (box_draws_blend(box_draws_at(modes, mode_step, i), inst->color[3]) != b->key.blend) {
            box_draws_mark(it->world, d, it->entities[i]);
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
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (slots[i].batch < 0) {
            continue;
        }

        box_draw_batch_t *b = box_draws_batch(d, slots[i].batch);
        if (!b->key.dynamic || slots[i].slot >= ecs_vec_count(&b->instances)) {
            continue;
        }

        box_draws_transform(ecs_vec_get_t(&b->instances, box_render_instance_t, slots[i].slot),
            transforms ? &transforms[i] : NULL, &shapes[i]);
    }
}

/** Write the instances of every dynamic shape.
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
        box_draws_fill_transform(&it, d);
        box_draws_fill_material(&it, d, !ecs_field_is_self(&it, 9));
    }
}

void box_draws_send(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_render_packet_t *p)
{
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    int32_t i, count = ecs_vec_count(&d->batches);
    uint32_t changes = 0;
    bool views = ecs_vec_count(&p->views) != 0;
    d->textures = box_textures_get(world);
    d->stats.batches = 0;
    d->stats.instances = 0;
    d->stats.dynamic = 0;
    for (i = 0; i < count; i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        if (b->alive && !ecs_vec_count(&b->entities)) {
            box_draws_batch_free(d, p, i);
        }

        if (!b->alive) {
            continue;
        }

        d->stats.batches ++;
        if (b->key.dynamic) {
            d->stats.dynamic += ecs_vec_count(&b->entities);
        } else {
            d->stats.instances += ecs_vec_count(&b->entities);
        }
    }

    box_draws_release(p, d, cache);
    if (!views) {
        goto done;
    }

    box_draws_dynamic(world, d);
    ecs_vec_clear(&d->blended);
    for (i = 0; i < count; i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        b->ready = false;
        if (b->alive && box_draws_mesh(p, d, cache, b->key.mesh)) {
            changes += box_draws_send_batch(p, d, b);
        }

        if (b->ready && b->key.blend != Opaque) {
            *ecs_vec_append_t(NULL, &d->blended, int32_t) = i;
        }
    }

    if (ecs_vec_count(&d->blended)) {
        box_draws_send_blended(p, d);
    }

    if (changes) {
        box_draws_send_changes(p, d, changes);
    }

done:
    return;
}

void box_draws_batches_fini(
    box_render_draws_t *d)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&d->batches); i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        ecs_vec_fini_t(NULL, &b->entities, ecs_entity_t);
        ecs_vec_fini_t(NULL, &b->instances, box_render_instance_t);
        ecs_vec_fini_t(NULL, &b->changed, int32_t);
    }

    ecs_vec_fini_t(NULL, &d->batches, box_draw_batch_t);
    ecs_vec_fini_t(NULL, &d->free_batches, int32_t);
    ecs_vec_fini_t(NULL, &d->free_buffers, uint32_t);
    ecs_map_fini(&d->lookup);
}
