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

uint32_t box_mesh_cache_acquire(
    box_mesh_cache_t *cache,
    const box_shape_t *shape,
    const void *value)
{
    box_shape_key_t key;
    ecs_os_memset_t(&key, 0, box_shape_key_t);
    shape->desc.key(value, &key);
    uint64_t hash = box_mesh_cache_hash(shape, &key);
    uint32_t handle = box_mesh_cache_find(cache, shape, &key, hash);
    if (!handle) {
        box_mesh_t mesh = {0};
        shape->desc.generate(value, &key, &mesh);
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
        box_mesh_cache_unlink(cache, handle);
        box_mesh_fini(&entry->mesh);
        entry->shape = NULL;
        entry->next = 0;
        *ecs_vec_append_t(NULL, &cache->free_list, uint32_t) = handle;
        cache->alive --;
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
