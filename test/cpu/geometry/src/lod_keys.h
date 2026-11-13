/**
 * @file lod_keys.h
 * @brief Lookups of the levels of detail of the mesh of an entity, shared by the shape suites.
 */

#ifndef TEST_GEOMETRY_LOD_KEYS_H
#define TEST_GEOMETRY_LOD_KEYS_H

#include <box.h>
#include <bake_test.h>
#include "mesh_cache.h"

/** Get the mesh of a level of detail of the shape of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param level The level
 * @return The mesh handle
 */
static uint32_t lod_mesh(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t level)
{
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(shape != NULL);
    test_assert(shape->mesh != 0);

    return box_mesh_cache_lod(cache, shape->mesh, level);
}

/** Get the key of a level of detail of the shape of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param level The level
 * @return The key
 */
static const box_shape_key_t* lod_key(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t level)
{
    const box_shape_key_t *key = box_mesh_cache_key(box_mesh_cache_of(world),
        lod_mesh(world, e, level));
    test_assert(key != NULL);

    return key;
}

/** Check that a level of detail of the shape of an entity has fewer indices than level 0.
 *
 * @param world The world
 * @param e The entity
 * @param level The level
 */
static void lod_test_fewer_indices(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t level)
{
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    /* Get the variant first, generating it can move the cache entries */
    const box_mesh_t *variant = box_mesh_cache_get(cache, lod_mesh(world, e, level));
    const box_mesh_t *base = box_mesh_cache_get(cache, lod_mesh(world, e, 0));
    test_assert(base != NULL);
    test_assert(variant != NULL);
    test_assert(variant->index_count > 0);
    test_assert(variant->index_count < base->index_count);
}

#endif
