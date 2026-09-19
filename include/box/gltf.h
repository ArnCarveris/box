/**
 * @file box/gltf.h
 * @brief Gltf component: setting it loads a glTF file into a tree of child entities.
 */

#ifndef BOX_GLTF_H
#define BOX_GLTF_H

#include "types.h"

#ifdef BOX_GLTF_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** glTF file instantiated as children of the entity that carries the component.
 *
 * Setting the component parses the file and builds one child entity per node of
 * its scene, with Position, Rotation and Scale from the node transform, and one
 * grandchild per mesh primitive with a Mesh3 that the mesh cache shares between
 * primitives with the same data. A primitive carries the material of its glTF
 * material as Rgb, Opacity, PbrMaterial, Emissive and PbrTextures, whose slots
 * point at texture entities the loader creates under a `textures` child.
 *
 * Everything the loader creates is a child of the entity, so deleting it takes
 * the model with it. Setting the component again replaces the tree and removing
 * it takes the tree away, which leaves children the application added alone.
 */
ECS_STRUCT(Gltf, {
    char *path;  /**< The .gltf or .glb file, relative to the working directory */
    char *scene; /**< Scene of the file to instantiate, empty for the default one */
    float scale; /**< Uniform scale applied to the root nodes, 0 for one */
});

/** Marks every entity the loader created, so replacing the file removes them */
extern ECS_TAG_DECLARE(GltfNode);

/** Register the box.gltf module.
 *
 * @param world The world
 */
void BoxGltfImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
