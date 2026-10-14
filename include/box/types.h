/**
 * @file box/types.h
 * @brief Reflected math types shared by every box module.
 *
 * The types are registered by the box.types module so scripts can use them.
 */

#ifndef BOX_TYPES_H
#define BOX_TYPES_H

#include <flecs.h>
#include <cglm/cglm.h>

#ifdef BOX_TYPES_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern ECS_COMPONENT_DECLARE(vec2);
extern ECS_COMPONENT_DECLARE(vec3);
extern ECS_COMPONENT_DECLARE(vec4);
extern ECS_COMPONENT_DECLARE(mat4);

/** Display encoded color with 8 bit channels */
ECS_STRUCT(rgb, {
    uint8_t r; /**< Red */
    uint8_t g; /**< Green */
    uint8_t b; /**< Blue */
});

/** Display encoded color with 8 bit channels and an 8 bit alpha channel */
ECS_STRUCT(rgba, {
    uint8_t r; /**< Red */
    uint8_t g; /**< Green */
    uint8_t b; /**< Blue */
    uint8_t a; /**< Alpha */
});

/** Member of a component that owns heap memory. */
typedef struct box_owned_t {
    ecs_size_t offset;      /**< Offset of the member in the component */
    ecs_size_t elem_size;   /**< Element size of an ecs_vec_t member, 0 for a char* member */
} box_owned_t;

/** Owned char* member of component T */
#define BOX_OWNED_STR(T, member) { (ecs_size_t)offsetof(T, member), 0 }

/** Owned ecs_vec_t member of component T with trivially copyable elements of type E */
#define BOX_OWNED_VEC(T, member, E) { (ecs_size_t)offsetof(T, member), ECS_SIZEOF(E) }

/** Install the lifecycle hooks of reflected component T from its owned members.
 *
 * Flecs generates no dtor, copy or move for C structs registered with ECS_META_COMPONENT,
 * so components with BOX_OWNED_STR or BOX_OWNED_VEC members call this right after it.
 */
#define box_owned_hooks(world, T, ...)\
    box_types_owned_hooks(world, ecs_id(T), (const box_owned_t[]){ __VA_ARGS__ },\
        (int32_t)(sizeof((const box_owned_t[]){ __VA_ARGS__ }) / sizeof(box_owned_t)))

/** Install ctor, dtor, copy and move hooks that free, deep copy and hand over owned members.
 *
 * Must be called before the component is used.
 *
 * @param world The world
 * @param component The component
 * @param members The members that own heap memory
 * @param count Number of members
 */
void box_types_owned_hooks(
    ecs_world_t *world,
    ecs_entity_t component,
    const box_owned_t *members,
    int32_t count);

/** Register the box.types module.
 *
 * @param world The world
 */
void BoxTypesImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
