/**
 * @file types/types.c
 * @brief Registers the reflected math types of the box.types module.
 */

#define BOX_TYPES_IMPL
#include <box.h>

ECS_COMPONENT_DECLARE(vec2);
ECS_COMPONENT_DECLARE(vec3);
ECS_COMPONENT_DECLARE(vec4);
ECS_COMPONENT_DECLARE(mat4);

void BoxTypesImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTypes);

    ECS_COMPONENT_DEFINE(world, vec2);
    ECS_COMPONENT_DEFINE(world, vec3);
    ECS_COMPONENT_DEFINE(world, vec4);
    ECS_COMPONENT_DEFINE(world, mat4);

    ecs_struct(world, {
        .entity = ecs_id(vec2),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(vec3),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(vec4),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
            { .name = "w", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(mat4),
        .members = {
            { .name = "v", .type = ecs_id(ecs_f32_t), .count = 16 }
        }
    });

    ECS_META_COMPONENT(world, rgb);
    ECS_META_COMPONENT(world, rgba);
}
