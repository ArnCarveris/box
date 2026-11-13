/**
 * @file geometry/mesh_cache.c
 * @brief Reference counted cache of shape meshes keyed by shape and mesh key.
 */

#include "mesh_cache.h"

/** Cached mesh */
typedef struct box_mesh_entry_t {
    box_mesh_t mesh;          /**< Mesh data */
    const box_shape_t *shape; /**< Shape that generated the mesh, NULL for a free slot */
    box_shape_key_t key;      /**< Key the mesh was generated from */
    uint64_t hash;            /**< Hash of shape and key */
    int32_t refs;             /**< Reference count */
    int32_t holds;            /**< References of modules that follow the mesh, part of refs */
    uint32_t next;            /**< Next handle with the same hash, 0 for none */
    void *value;              /**< Copy of the first shape value that acquired the mesh, to
                               *   generate its levels of detail from, NULL for none */
    uint32_t lod[BOX_LOD_LEVELS_MAX]; /**< Handle of each level of detail, 0 when not yet
                                       *   requested; lod[0] is not used */
} box_mesh_entry_t;

/** Mesh cache and shape registry */
struct box_mesh_cache_t {
    ecs_vec_t entries;   /**< box_mesh_entry_t, handle is index + 1 */
    ecs_vec_t free_list; /**< uint32_t handles of free entries */
    ecs_map_t lookup;    /**< Hash to the first handle with that hash */
    ecs_vec_t shapes;    /**< box_shape_t* registered shapes */
    int32_t alive;       /**< Number of live meshes */
};

/** Get the entry of a handle.
 *
 * @param cache The cache
 * @param handle The handle
 * @return The entry, NULL when the handle is out of range
 */
static box_mesh_entry_t* box_mesh_cache_entry(
    const box_mesh_cache_t *cache,
    uint32_t handle)
{
    box_mesh_entry_t *result = NULL;
    if (handle && (int32_t)handle <= ecs_vec_count(&cache->entries)) {
        result = ecs_vec_get_t(&cache->entries, box_mesh_entry_t, (int32_t)handle - 1);
    }

    return result;
}

/** Hash a shape and a key.
 *
 * @param shape The shape
 * @param key The key
 * @return The hash
 */
static uint64_t box_mesh_cache_hash(
    const box_shape_t *shape,
    const box_shape_key_t *key)
{
    uint64_t hash = 14695981039346656037ull;
    uintptr_t ptr = (uintptr_t)shape;
    const uint8_t *bytes = (const uint8_t*)&ptr;
    for (size_t i = 0; i < sizeof(ptr); i ++) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }

    bytes = (const uint8_t*)key;
    for (size_t i = 0; i < sizeof(box_shape_key_t); i ++) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }

    return hash;
}

/** Free the copy of the shape value of an entry.
 *
 * @param entry The entry
 */
static void box_mesh_cache_value_fini(
    box_mesh_entry_t *entry)
{
    if (entry->value) {
        if (entry->shape->value_fini) {
            entry->shape->value_fini(entry->value);
        }

        ecs_os_free(entry->value);
        entry->value = NULL;
    }
}

box_mesh_cache_t* box_mesh_cache_new(void)
{
    box_mesh_cache_t *cache = ecs_os_calloc_t(box_mesh_cache_t);
    ecs_vec_init_t(NULL, &cache->entries, box_mesh_entry_t, 0);
    ecs_vec_init_t(NULL, &cache->free_list, uint32_t, 0);
    ecs_vec_init_t(NULL, &cache->shapes, box_shape_t*, 0);
    ecs_map_init(&cache->lookup, NULL);

    return cache;
}

void box_mesh_cache_free(
    void *ctx)
{
    box_mesh_cache_t *cache = ctx;
    box_mesh_entry_t *entries = ecs_vec_first(&cache->entries);
    for (int32_t i = 0; i < ecs_vec_count(&cache->entries); i ++) {
        if (entries[i].shape) {
            box_mesh_cache_value_fini(&entries[i]);
        }

        box_mesh_fini(&entries[i].mesh);
    }

    box_shape_t **shapes = ecs_vec_first(&cache->shapes);
    for (int32_t i = 0; i < ecs_vec_count(&cache->shapes); i ++) {
        ecs_os_free(shapes[i]);
    }

    ecs_vec_fini_t(NULL, &cache->entries, box_mesh_entry_t);
    ecs_vec_fini_t(NULL, &cache->free_list, uint32_t);
    ecs_vec_fini_t(NULL, &cache->shapes, box_shape_t*);
    ecs_map_fini(&cache->lookup);
    ecs_os_free(cache);
}

box_mesh_cache_t* box_mesh_cache_of(
    const ecs_world_t *world)
{
    box_mesh_cache_t *result = NULL;
    const ecs_type_info_t *ti = NULL;
    if (ecs_id(ShapeMesh)) {
        ti = ecs_get_type_info(world, ecs_id(ShapeMesh));
    }

    if (ti) {
        result = ti->hooks.ctx;
    }

    return result;
}

/** Find the handle of a cached mesh.
 *
 * @param cache The cache
 * @param shape The shape
 * @param key The key
 * @param hash The hash of shape and key
 * @return The handle, 0 when the mesh is not cached
 */
static uint32_t box_mesh_cache_find(
    const box_mesh_cache_t *cache,
    const box_shape_t *shape,
    const box_shape_key_t *key,
    uint64_t hash)
{
    uint32_t result = 0;
    ecs_map_val_t *head = ecs_map_get(&cache->lookup, hash);
    uint32_t handle = head ? (uint32_t)*head : 0;
    while (handle) {
        box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
        if (entry->shape == shape && !ecs_os_memcmp_t(&entry->key, key, box_shape_key_t)) {
            result = handle;
            break;
        }

        handle = entry->next;
    }

    return result;
}

/** Look up or generate the mesh of a key and take a reference to it.
 *
 * @param cache The cache
 * @param shape The shape
 * @param value The shape value to generate the mesh from
 * @param key_ptr The key
 * @return The mesh handle
 */
static uint32_t box_mesh_cache_insert(
    box_mesh_cache_t *cache,
    const box_shape_t *shape,
    const void *value,
    const box_shape_key_t *key_ptr)
{
    box_shape_key_t key = *key_ptr;
    uint64_t hash = box_mesh_cache_hash(shape, &key);
    uint32_t handle = box_mesh_cache_find(cache, shape, &key, hash);
    if (!handle) {
        box_mesh_t mesh = {0};
        if (shape->desc.generate_world) {
            shape->desc.generate_world(shape->world, value, &key, &mesh);
        } else {
            shape->desc.generate(value, &key, &mesh);
        }

        box_mesh_bounds(&mesh);
        if (ecs_vec_count(&cache->free_list)) {
            handle = *ecs_vec_last_t(&cache->free_list, uint32_t);
            ecs_vec_remove_last(&cache->free_list);
        } else {
            ecs_vec_append_t(NULL, &cache->entries, box_mesh_entry_t);
            handle = (uint32_t)ecs_vec_count(&cache->entries);
        }

        ecs_map_val_t *head = ecs_map_get(&cache->lookup, hash);
        box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
        *entry = (box_mesh_entry_t){
            .mesh = mesh,
            .shape = shape,
            .key = key,
            .hash = hash,
            .next = head ? (uint32_t)*head : 0
        };
        if (head) {
            *head = handle;
        } else {
            ecs_map_insert(&cache->lookup, hash, handle);
        }

        cache->alive ++;
    }

    box_mesh_cache_entry(cache, handle)->refs ++;

    return handle;
}

uint32_t box_mesh_cache_acquire(
    box_mesh_cache_t *cache,
    const box_shape_t *shape,
    const void *value)
{
    box_shape_key_t key;
    ecs_os_memset_t(&key, 0, box_shape_key_t);
    if (shape->desc.key_world) {
        shape->desc.key_world(shape->world, value, &key);
    } else {
        shape->desc.key(value, &key);
    }

    uint32_t handle = box_mesh_cache_insert(cache, shape, value, &key);
    box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
    if (!entry->value && (shape->desc.lod || shape->desc.lod_world) && shape->size > 0) {
        entry->value = ecs_os_malloc(shape->size);
        if (shape->value_copy) {
            shape->value_copy(entry->value, value);
        } else {
            ecs_os_memcpy(entry->value, value, shape->size);
        }
    }

    return handle;
}

uint32_t box_mesh_cache_lod(
    box_mesh_cache_t *cache,
    uint32_t handle,
    int32_t level)
{
    uint32_t result = handle;
    box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
    if (!entry || !entry->shape || !entry->value || level < 1) {
        goto done;
    }

    level = level < BOX_LOD_LEVELS_MAX ? level : BOX_LOD_LEVELS_MAX - 1;
    if (entry->lod[level]) {
        result = entry->lod[level];
        goto done;
    }

    uint32_t previous = box_mesh_cache_lod(cache, handle, level - 1);
    entry = box_mesh_cache_entry(cache, handle);
    const box_shape_t *shape = entry->shape;
    void *value = entry->value;
    box_shape_key_t key;
    ecs_os_memset_t(&key, 0, box_shape_key_t);
    if (shape->desc.lod_world) {
        shape->desc.lod_world(shape->world, value, level, &key);
    } else {
        shape->desc.lod(value, level, &key);
    }

    result = previous;
    if (ecs_os_memcmp_t(&box_mesh_cache_entry(cache, previous)->key, &key, box_shape_key_t)) {
        result = box_mesh_cache_insert(cache, shape, value, &key);
        if (result == handle) {
            box_mesh_cache_release(cache, result);
        }
    }

    box_mesh_cache_entry(cache, handle)->lod[level] = result;
done:
    return result;
}

const box_shape_key_t* box_mesh_cache_key(
    const box_mesh_cache_t *cache,
    uint32_t handle)
{
    const box_shape_key_t *result = NULL;
    box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
    if (entry && entry->shape) {
        result = &entry->key;
    }

    return result;
}

void box_mesh_cache_keep(
    box_mesh_cache_t *cache,
    uint32_t handle)
{
    box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
    if (entry && entry->shape) {
        entry->refs ++;
    }
}

void box_mesh_cache_hold(
    box_mesh_cache_t *cache,
    uint32_t handle)
{
    box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
    if (entry && entry->shape) {
        entry->refs ++;
        entry->holds ++;
    }
}

void box_mesh_cache_unhold(
    box_mesh_cache_t *cache,
    uint32_t handle)
{
    box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
    if (entry && entry->shape && entry->holds > 0) {
        entry->holds --;
        box_mesh_cache_release(cache, handle);
    }
}

int32_t box_mesh_cache_users(
    const box_mesh_cache_t *cache,
    uint32_t handle)
{
    int32_t result = 0;
    box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
    if (entry && entry->shape) {
        result = entry->refs - entry->holds;
    }

    return result;
}

/** Remove an entry from the hash chain it is part of.
 *
 * @param cache The cache
 * @param handle The handle of the entry
 */
static void box_mesh_cache_unlink(
    box_mesh_cache_t *cache,
    uint32_t handle)
{
    box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
    ecs_map_val_t *head = ecs_map_get(&cache->lookup, entry->hash);
    if (head && (uint32_t)*head == handle) {
        if (entry->next) {
            *head = entry->next;
        } else {
            ecs_map_remove(&cache->lookup, entry->hash);
        }
    } else if (head) {
        box_mesh_entry_t *prev = box_mesh_cache_entry(cache, (uint32_t)*head);
        while (prev && prev->next != handle) {
            prev = box_mesh_cache_entry(cache, prev->next);
        }

        if (prev) {
            prev->next = entry->next;
        }
    }
}

void box_mesh_cache_release(
    box_mesh_cache_t *cache,
    uint32_t handle)
{
    box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
    if (entry && entry->shape && !(-- entry->refs)) {
        uint32_t lod[BOX_LOD_LEVELS_MAX];
        ecs_os_memcpy_n(lod, entry->lod, uint32_t, BOX_LOD_LEVELS_MAX);
        box_mesh_cache_unlink(cache, handle);
        box_mesh_cache_value_fini(entry);
        box_mesh_fini(&entry->mesh);
        entry->shape = NULL;
        entry->next = 0;
        ecs_os_memset_n(entry->lod, 0, uint32_t, BOX_LOD_LEVELS_MAX);
        *ecs_vec_append_t(NULL, &cache->free_list, uint32_t) = handle;
        cache->alive --;

        lod[0] = handle;
        for (int32_t i = 1; i < BOX_LOD_LEVELS_MAX && lod[i]; i ++) {
            if (lod[i] != handle && lod[i] != lod[i - 1]) {
                box_mesh_cache_release(cache, lod[i]);
            }
        }
    }
}

const box_mesh_t* box_mesh_cache_get(
    const box_mesh_cache_t *cache,
    uint32_t handle)
{
    const box_mesh_t *result = NULL;
    box_mesh_entry_t *entry = box_mesh_cache_entry(cache, handle);
    if (entry && entry->shape) {
        result = &entry->mesh;
    }

    return result;
}

int32_t box_mesh_cache_count(
    const box_mesh_cache_t *cache)
{
    return cache->alive;
}

box_shape_t* box_mesh_cache_add_shape(
    box_mesh_cache_t *cache,
    const box_shape_desc_t *desc)
{
    box_shape_t *result = NULL;
    box_shape_t **shapes = ecs_vec_first(&cache->shapes);
    for (int32_t i = 0; i < ecs_vec_count(&cache->shapes); i ++) {
        if (shapes[i]->desc.component == desc->component) {
            goto done;
        }
    }

    result = ecs_os_calloc_t(box_shape_t);
    result->desc = *desc;
    result->cache = cache;
    *ecs_vec_append_t(NULL, &cache->shapes, box_shape_t*) = result;
done:
    return result;
}

box_shape_t* box_mesh_cache_shape(
    const box_mesh_cache_t *cache,
    ecs_entity_t component)
{
    box_shape_t *result = NULL;
    box_shape_t **shapes = ecs_vec_first(&cache->shapes);
    for (int32_t i = 0; i < ecs_vec_count(&cache->shapes); i ++) {
        if (shapes[i]->desc.component == component) {
            result = shapes[i];
            break;
        }
    }

    return result;
}

/** Shape value used to cache a source mesh resolved against a parametric surface */
typedef struct box_resolved_mesh_t {
    const ShapeMesh *shape;   /**< Shape with the surface parameters */
    const box_mesh_t *mesh;   /**< Source grid mesh */
    uint64_t source_hash;     /**< Cache hash of the source mesh */
} box_resolved_mesh_t;

/** Compute the cache key of a resolved mesh from the source hash and surface parameters.
 *
 * @param value The box_resolved_mesh_t value.
 * @param key Output cache key.
 */
static void box_resolved_mesh_key(
    const void *value,
    box_shape_key_t *key)
{
    const box_resolved_mesh_t *resolved = value;
    uint64_t hash = 14695981039346656037ull;
    const uint8_t *bytes = (const uint8_t*)resolved->shape->surface.p;
    for (size_t i = 0; i < 20 * sizeof(vec4); i ++) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }

    ecs_os_memcpy(key->i, &resolved->source_hash, sizeof(uint64_t));
    ecs_os_memcpy(key->f, &hash, sizeof(hash));
}

/** Generate a mesh by sampling the surface at each (u, v, region) vertex of the source mesh.
 *
 * @param value The box_resolved_mesh_t value.
 * @param key The cache key (unused).
 * @param mesh Output mesh, left empty when allocation fails.
 */
static void box_resolved_mesh_generate(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)key;
    const box_resolved_mesh_t *resolved = value;
    const box_mesh_t *source = resolved->mesh;
    int32_t count = source->vertex_count;
    int32_t regions = (count + 3) / 4;
    mesh->vertices = ecs_os_malloc_n(box_vertex_t, count);
    mesh->indices = ecs_os_memdup_n(source->indices, uint32_t, source->index_count);
    mesh->surface_assets = ecs_os_calloc_n(vec4, regions);
    if (!mesh->vertices || !mesh->indices || !mesh->surface_assets) {
        box_mesh_fini(mesh);
        goto done;
    }

    mesh->vertex_count = mesh->vertex_capacity = count;
    mesh->index_count = mesh->index_capacity = source->index_count;
    mesh->surface_asset_count = regions;
    for (int32_t i = 0; i < count; i ++) {
        const box_vertex_t *input = &source->vertices[i];
        box_vertex_t *output = &mesh->vertices[i];
        int region = (int)input->position[2];
        box_surface_sample_t sample;
        box_surface_sample(&resolved->shape->surface, region,
            input->position[0], input->position[1], &sample);
        glm_vec3_copy(sample.position, output->position);
        glm_vec3_copy(sample.normal, output->normal);
        glm_vec2_copy(ECS_CONST_CAST(float*, input->uv), output->uv);
        int material = 2;
        if (region == 0 || region == 1) {
            material = region;
        } else if (region == 2 || region == 3 || region == 8 || region == 9) {
            material = 3;
        }

        mesh->surface_assets[i / 4][i % 4] = (float)material;
    }

done:
    return;
}

uint32_t box_mesh_cache_resolve(
    box_mesh_cache_t *cache,
    const ShapeMesh *shape)
{
    static const box_shape_t resolved_shape = {
        .desc = {.key = box_resolved_mesh_key, .generate = box_resolved_mesh_generate}
    };
    uint32_t handle = 0;
    int kind = (int)shape->surface.p[0][0];
    box_mesh_entry_t *source = box_mesh_cache_entry(cache, shape->mesh);
    if (kind < 1 || kind > 6 || !source || !source->shape ||
        !source->mesh.vertex_count || !source->mesh.index_count)
    {
        goto done;
    }

    box_resolved_mesh_t value = {shape, &source->mesh, source->hash};
    handle = box_mesh_cache_acquire(cache, &resolved_shape, &value);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, handle);
    if (!mesh || !mesh->vertex_count || !mesh->index_count) {
        box_mesh_cache_release(cache, handle);
        handle = 0;
    }

done:
    return handle;
}

void box_shape_mesh_release(
    box_mesh_cache_t *cache,
    const ShapeMesh *shape)
{
    box_mesh_cache_release(cache, shape->mesh);
    box_mesh_cache_release(cache, shape->render_mesh);
}
