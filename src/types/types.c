/**
 * @file types/types.c
 * @brief Registers the reflected math types of the box.types module.
 */

#define BOX_TYPES_IMPL
#include <box.h>

void box_typesImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, box_types);

    ECS_META_COMPONENT(world, vec2);
    ECS_META_COMPONENT(world, vec3);
    ECS_META_COMPONENT(world, vec4);
    ECS_META_COMPONENT(world, mat4);
    ECS_META_COMPONENT(world, rgb);
}
