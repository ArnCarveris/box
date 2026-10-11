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
