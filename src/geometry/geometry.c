/**
 * @file geometry/geometry.c
 * @brief Registers the shape components of the box.geometry module.
 */

#define BOX_GEOMETRY_IMPL
#include <box.h>

void BoxGeometryImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxGeometry);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Mesh3);
    ECS_META_COMPONENT(world, Box);
    ECS_META_COMPONENT(world, Quad);
    ECS_META_COMPONENT(world, Triangle);
    ECS_META_COMPONENT(world, RightTriangle);
    ECS_META_COMPONENT(world, TrianglePrism);
    ECS_META_COMPONENT(world, RightTrianglePrism);
    ECS_META_COMPONENT(world, NGon);
    ECS_META_COMPONENT(world, Sphere);
    ECS_META_COMPONENT(world, HemiSphere);
    ECS_META_COMPONENT(world, IcoSphere);
    ECS_META_COMPONENT(world, Cone);
    ECS_META_COMPONENT(world, Cylinder);
    ECS_META_COMPONENT(world, Torus);
    ECS_META_COMPONENT(world, Capsule);
    ECS_META_COMPONENT(world, Tube);
    ECS_META_COMPONENT(world, Frustum);
    ECS_META_COMPONENT(world, RoundedBox);
    ECS_META_COMPONENT(world, Star);
    ECS_META_COMPONENT(world, Rect);
    ECS_META_COMPONENT(world, Circle);
}
