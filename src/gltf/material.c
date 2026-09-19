/**
 * @file gltf/material.c
 * @brief Creates the texture entities of a glTF file and maps its materials onto components.
 *
 * Every image of the file becomes one texture entity under a `textures` child
 * of the owner, whether its pixels come from a file next to the glTF, from a
 * data uri or from a buffer view of a glb. A material turns into the Rgb,
 * Opacity, PbrMaterial, Emissive and PbrTextures of the entity of a primitive.
 */

#include "gltf_internal.h"
#include "../texture/texture_internal.h"
#include <string.h>

uint8_t box_gltf_srgb(
    float value)
{
    float v = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    float s = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;

    return (uint8_t)(s * 255.0f + 0.5f);
}

/** Load the pixels of an image that a data uri carries.
 *
 * @param world The world
 * @param e The texture entity
 * @param uri The data uri of the image
 */
static void box_gltf_image_uri(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *uri)
{
    const char *comma = strchr(uri, ',');
    if (!comma) {
        goto done;
    }

    const char *base64 = comma + 1;
    cgltf_size length = (cgltf_size)strlen(base64);
    cgltf_size size = length / 4 * 3;
    void *bytes = NULL;
    cgltf_options options = {0};

    while (size && length && base64[length - 1] == '=') {
        size --;
        length --;
    }

    if (cgltf_load_buffer_base64(&options, size, base64, &bytes) == cgltf_result_success) {
        box_texture_set_memory(world, e, bytes, (int32_t)size);
        free(bytes);
    }
done:
    return;
}

/** Point a texture entity at an image file that sits next to the glTF file.
 *
 * @param ctx The load
 * @param e The texture entity
 * @param uri The uri of the image, relative to the glTF file
 */
static void box_gltf_image_file(
    box_gltf_ctx_t *ctx,
    ecs_entity_t e,
    const char *uri)
{
    char *decoded = ecs_os_strdup(uri);

    cgltf_decode_uri(decoded);

    char *path = flecs_asprintf("%s%s", ctx->dir, decoded);
    ecs_set(ctx->world, e, Texture, { .path = path });
    ecs_os_free(decoded);
}

/** Create the texture entity of one image of the file and load its pixels.
 *
 * @param ctx The load
 * @param image The glTF image
 * @param index Index of the image in the file
 * @return The texture entity
 */
static ecs_entity_t box_gltf_image(
    box_gltf_ctx_t *ctx,
    const cgltf_image *image,
    int32_t index)
{
    char fallback[32];
    char name[64];

    ecs_os_snprintf(fallback, sizeof(fallback), "image%d", index);
    box_gltf_name(ctx->world, ctx->textures, image->name, fallback, name);

    ecs_entity_t e = ecs_entity(ctx->world, { .name = name, .parent = ctx->textures });
    ecs_add(ctx->world, e, GltfNode);

    if (image->buffer_view) {
        box_texture_set_memory(ctx->world, e, cgltf_buffer_view_data(image->buffer_view),
            (int32_t)image->buffer_view->size);
    } else if (!image->uri) {
        ecs_warn("box.gltf: image '%s' holds no pixels", name);
    } else if (!strncmp(image->uri, "data:", 5)) {
        box_gltf_image_uri(ctx->world, e, image->uri);
    } else {
        box_gltf_image_file(ctx, e, image->uri);
    }

    return e;
}

void box_gltf_images(
    box_gltf_ctx_t *ctx)
{
    cgltf_size i;

    if (!ctx->data->images_count) {
        goto done;
    }

    ctx->images = ecs_os_calloc_n(ecs_entity_t, (int32_t)ctx->data->images_count);
    ctx->textures = ecs_entity(ctx->world, {
        .name = BOX_GLTF_TEXTURE_SCOPE,
        .parent = ctx->owner
    });
    ecs_add(ctx->world, ctx->textures, GltfNode);
    ecs_add(ctx->world, ctx->textures, GltfTextures);

    for (i = 0; i < ctx->data->images_count; i ++) {
        ctx->images[i] = box_gltf_image(ctx, &ctx->data->images[i], (int32_t)i);
    }
done:
    return;
}

/** The texture entity a material slot binds.
 *
 * @param ctx The load
 * @param view The texture view of the slot
 * @return The texture entity, 0 when the slot is empty
 */
static ecs_entity_t box_gltf_slot(
    box_gltf_ctx_t *ctx,
    const cgltf_texture_view *view)
{
    ecs_entity_t result = 0;
    const cgltf_image *image = view->texture ? view->texture->image : NULL;

    if (image && ctx->images) {
        result = ctx->images[cgltf_image_index(ctx->data, image)];
    }

    return result;
}

/** Give an entity the Emissive of a material when it emits any light.
 *
 * @param ctx The load
 * @param e The entity of the primitive
 * @param material The glTF material
 * @param textures The texture slots, its emissive slot is filled in
 */
static void box_gltf_emissive(
    box_gltf_ctx_t *ctx,
    ecs_entity_t e,
    const cgltf_material *material,
    PbrTextures *textures)
{
    const float *color = material->emissive_factor;
    float strength = material->has_emissive_strength ?
        material->emissive_strength.emissive_strength : 1.0f;

    textures->emissive = box_gltf_slot(ctx, &material->emissive_texture);
    if (!textures->emissive && color[0] <= 0 && color[1] <= 0 && color[2] <= 0) {
        goto done;
    }

    ecs_set(ctx->world, e, Emissive, {
        .strength = strength,
        .color = {
            box_gltf_srgb(color[0]), box_gltf_srgb(color[1]), box_gltf_srgb(color[2])
        }
    });
done:
    return;
}

void box_gltf_material(
    box_gltf_ctx_t *ctx,
    ecs_entity_t e,
    const cgltf_material *material)
{
    float base[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic = 1.0f;
    float roughness = 1.0f;
    PbrTextures textures = {0};

    if (material && material->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness *pbr = &material->pbr_metallic_roughness;

        memcpy(base, pbr->base_color_factor, sizeof(base));
        metallic = pbr->metallic_factor;
        roughness = pbr->roughness_factor;
        textures.albedo = box_gltf_slot(ctx, &pbr->base_color_texture);
        textures.roughness = box_gltf_slot(ctx, &pbr->metallic_roughness_texture);
    }

    ecs_set(ctx->world, e, Rgb, {
        box_gltf_srgb(base[0]), box_gltf_srgb(base[1]), box_gltf_srgb(base[2])
    });
    ecs_set(ctx->world, e, PbrMaterial, { .metallic = metallic, .roughness = roughness });

    if (base[3] < 1.0f) {
        ecs_set(ctx->world, e, Opacity, { base[3] });
    }

    if (material) {
        textures.normal = box_gltf_slot(ctx, &material->normal_texture);
        box_gltf_emissive(ctx, e, material, &textures);
        if (material->alpha_mode == cgltf_alpha_mode_blend) {
            ecs_set(ctx->world, e, BlendMode, { AlphaBlend });
        }

        if (material->double_sided) {
            ecs_set(ctx->world, e, BackFaceMode, { BackFaceTextured });
        }
    }

    if (textures.albedo || textures.roughness || textures.normal || textures.emissive) {
        ecs_set_ptr(ctx->world, e, PbrTextures, &textures);
    }
}
