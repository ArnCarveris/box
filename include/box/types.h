/**
 * @file box/types.h
 * @brief Reflected math types shared by every box module.
 *
 * The types are registered by the box.types module so scripts can use them.
 */

#ifndef BOX_TYPES_H
#define BOX_TYPES_H

#include <flecs.h>

#ifdef BOX_TYPES_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Two component vector */
ECS_STRUCT(vec2, {
    float x; /**< X component */
    float y; /**< Y component */
});

/** Three component vector */
ECS_STRUCT(vec3, {
    float x; /**< X component */
    float y; /**< Y component */
    float z; /**< Z component */
});

/** Four component vector */
ECS_STRUCT(vec4, {
    float x; /**< X component */
    float y; /**< Y component */
    float z; /**< Z component */
    float w; /**< W component */
});

/** Column-major 4x4 matrix */
ECS_STRUCT(mat4, {
    float v[16]; /**< Matrix elements, column major */
});

/** Display encoded color with 8 bit channels */
ECS_STRUCT(rgb, {
    uint8_t r; /**< Red */
    uint8_t g; /**< Green */
    uint8_t b; /**< Blue */
});

/** Register the box.types module.
 *
 * @param world The world
 */
void box_typesImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
