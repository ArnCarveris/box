/**
 * @file material/material.c
 * @brief Registers the components of the box.material module.
 */

#define BOX_MATERIAL_IMPL
#include <box.h>

ECS_TAG_DECLARE(DynamicMaterial);
ECS_TAG_DECLARE(NoShadow);

void BoxMaterialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxMaterial);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Rgb);
    ECS_META_COMPONENT(world, Opacity);
    ECS_META_COMPONENT(world, Tint);
    ECS_META_COMPONENT(world, PbrMaterial);
    ECS_META_COMPONENT(world, Emissive);
    ECS_META_COMPONENT(world, BlendMode);
    ECS_META_COMPONENT(world, BackFaceMode);
    ECS_META_COMPONENT(world, TextureWrap);
    ECS_META_COMPONENT(world, TextureTransform);
    ECS_META_COMPONENT(world, PbrTextures);

    ECS_TAG_DEFINE(world, DynamicMaterial);
    ECS_TAG_DEFINE(world, NoShadow);
}
