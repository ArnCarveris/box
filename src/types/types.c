/**
 * @file types/types.c
 * @brief Registers the reflected math types of the box.types module.
 */

#define BOX_TYPES_IMPL
#include <box.h>

void BoxTypesImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTypes);

    ECS_META_COMPONENT(world, vec2);
    ECS_META_COMPONENT(world, vec3);
    ECS_META_COMPONENT(world, vec4);
    ECS_META_COMPONENT(world, mat4);
    ECS_META_COMPONENT(world, rgb);
    ECS_META_COMPONENT(world, rgba);
}
