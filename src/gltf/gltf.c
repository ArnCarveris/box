/**
 * @file gltf/gltf.c
 * @brief Registers the box.gltf module and loads a file when a Gltf is set.
 */

#define BOX_GLTF_IMPL
#include "gltf_internal.h"

ECS_TAG_DECLARE(GltfNode);
ECS_TAG_DECLARE(GltfTextures);

/** Gltf was set: replace the entities of the previous file with those of this one.
 *
 * @param it Iterator over the changed components
 */
static void box_gltf_on_set(
    ecs_iter_t *it)
{
    Gltf *gltf = ecs_field(it, Gltf, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_gltf_load(it->world, it->entities[i], &gltf[i]);
    }
}

/** Gltf was removed: take the entities the file created away again.
 *
 * An entity that is being deleted has already lost its children when this runs,
 * because flecs resolves the ChildOf cleanup before it empties the entity, so
 * the same unload covers both a removed component and a deleted owner.
 *
 * @param it Iterator over the entities that lost the component
 */
static void box_gltf_on_remove(
    ecs_iter_t *it)
{
    if (ecs_is_fini(it->world) || ecs_should_quit(it->world)) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_gltf_unload(it->world, it->stage, it->entities[i]);
    }
done:
    return;
}

void BoxGltfImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxGltf);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxTexture);

    ECS_META_COMPONENT(world, Gltf);
    ECS_TAG_DEFINE(world, GltfNode);
    ECS_TAG_DEFINE(world, GltfTextures);

    ecs_add_pair(world, ecs_id(Gltf), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, GltfNode, EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, GltfTextures, EcsOnInstantiate, EcsDontInherit);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Gltf) }},
        .events = { EcsOnSet },
        .callback = box_gltf_on_set
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Gltf) }},
        .events = { EcsOnRemove },
        .callback = box_gltf_on_remove
    });
}
