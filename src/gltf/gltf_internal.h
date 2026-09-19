/**
 * @file gltf/gltf_internal.h
 * @brief Private state a glTF load shares between its node, mesh and material steps.
 */

#ifndef BOX_GLTF_INTERNAL_H
#define BOX_GLTF_INTERNAL_H

#include <box.h>
#include <box/gltf.h>
#include <cgltf.h>

/** Name of the child entity the texture entities of a file are created under */
#define BOX_GLTF_TEXTURE_SCOPE "textures"

/** Marks the child entity the texture entities live under, which is unloaded last */
extern ECS_TAG_DECLARE(GltfTextures);

/** Everything one load of one file needs, handed to every step of the load */
typedef struct box_gltf_ctx_t {
    ecs_world_t *world;    /**< The world */
    ecs_entity_t owner;    /**< Entity that carries the Gltf component */
    ecs_entity_t textures; /**< Child entity the texture entities live under */
    cgltf_data *data;      /**< The parsed file */
    char *dir;             /**< Directory of the file, prefix of a relative image uri */
    ecs_entity_t *images;  /**< Texture entity per image of the file, 0 when it failed */
    float scale;           /**< Uniform scale applied to the root nodes */
} box_gltf_ctx_t;

/** Parse a glTF file and build the entity tree of its scene under an entity.
 *
 * @param world The world
 * @param e The entity that carries the component
 * @param gltf The component
 */
void box_gltf_load(
    ecs_world_t *world,
    ecs_entity_t e,
    const Gltf *gltf);

/** Delete the entities a previous load created under an entity.
 *
 * @param world The world, which the children are read from
 * @param stage The world the deletes go through, the stage inside an observer
 * @param e The entity that carries the component
 */
void box_gltf_unload(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t e);

/** Create one texture entity per image of the file, loading its pixels.
 *
 * @param ctx The load
 */
void box_gltf_images(
    box_gltf_ctx_t *ctx);

/** Give an entity the box material components of a glTF material.
 *
 * @param ctx The load
 * @param e The entity of the primitive
 * @param material The glTF material, NULL for the glTF default material
 */
void box_gltf_material(
    box_gltf_ctx_t *ctx,
    ecs_entity_t e,
    const cgltf_material *material);

/** Give an entity the Mesh3 of a glTF primitive, converted to box axes.
 *
 * @param world The world
 * @param e The entity of the primitive
 * @param prim The glTF primitive
 * @return True when the primitive holds a triangle mesh that could be read
 */
bool box_gltf_mesh(
    ecs_world_t *world,
    ecs_entity_t e,
    const cgltf_primitive *prim);

/** Turn a glTF name into a name that is unique among the children of an entity.
 *
 * @param world The world
 * @param parent The parent entity
 * @param name The glTF name, NULL or empty for the fallback
 * @param fallback Name to use when the glTF name is missing or taken
 * @param out Buffer of at least 64 characters receiving the name
 */
void box_gltf_name(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const char *fallback,
    char *out);

#endif
