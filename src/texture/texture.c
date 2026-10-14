/**
 * @file texture/texture.c
 * @brief Texture components.
 */

#define BOX_TEXTURE_IMPL
#include <box.h>

void BoxTextureImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTexture);

    ECS_META_COMPONENT(world, Texture);
    ECS_META_COMPONENT(world, TexturePath);
    box_owned_hooks(world, TexturePath, BOX_OWNED_STR(TexturePath, path));
}
