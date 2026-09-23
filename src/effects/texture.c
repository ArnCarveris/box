#include "effects_internal.h"
#include <string.h>

#define BOX_EFFECT_TEXTURE_HEADER (16)
#define BOX_EFFECT_TEXTURE_MAGIC "BOXLUT3D"
#define BOX_EFFECT_TEXTURE_MAGIC_LEN (8)

static uint32_t box_effect_texture_word(
    const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static bool box_effect_texture_load(
    const char *path,
    uint32_t handle,
    box_packet_t *p)
{
    bool result = false;
    char resolved[BOX_SHADER_PATH_MAX];
    if (!path || !box_locate(path, resolved, sizeof(resolved))) {
        ecs_err("box.effects: %s: file not found", path ? path : "");
        goto done;
    }

    size_t size = 0;
    unsigned char *bytes = box_shader_read_bytes(resolved, &size);
    if (!bytes) {
        ecs_err("box.effects: %s: cannot be read", path);
        goto done;
    }

    uint32_t dim = 0;
    if (size > BOX_EFFECT_TEXTURE_HEADER &&
        !memcmp(bytes, BOX_EFFECT_TEXTURE_MAGIC, BOX_EFFECT_TEXTURE_MAGIC_LEN))
    {
        dim = box_effect_texture_word(bytes + BOX_EFFECT_TEXTURE_MAGIC_LEN);
    }

    size_t texels = (size_t)dim * dim * dim;
    if (!dim || size != BOX_EFFECT_TEXTURE_HEADER + texels * sizeof(uint32_t)) {
        ecs_err("box.effects: %s: unsupported texture", path);
        ecs_os_free(bytes);
        goto done;
    }

    box_effect_texture_cmd_t *cmd = box_vec_push(&p->effect_texture_create);
    cmd->handle = handle;
    cmd->dim = dim;
    cmd->texels = malloc(texels * sizeof(uint32_t));
    size_t i;
    for (i = 0; i < texels; i ++) {
        cmd->texels[i] = box_effect_texture_word(
            bytes + BOX_EFFECT_TEXTURE_HEADER + i * sizeof(uint32_t));
    }

    ecs_os_free(bytes);
    result = true;
done:
    return result;
}

int32_t box_effect_texture(
    ecs_world_t *world,
    EffectState *state,
    ecs_entity_t texture,
    box_packet_t *p)
{
    int32_t handle;
    for (handle = 0; handle < state->texture_count; handle ++) {
        if (state->texture_entity[handle] == texture) {
            break;
        }
    }

    if (handle == state->texture_count) {
        if (handle >= BOX_EFFECTS_MAX) {
            return -1;
        }

        state->texture_entity[handle] = texture;
        state->texture_count ++;
    }

    if (state->texture_failed[handle]) {
        return -1;
    }

    if (!state->texture_sent[handle]) {
        const EffectTexture *desc = ecs_get(world, texture, EffectTexture);
        state->texture_sent[handle] = desc && box_effect_texture_load(
            desc->path, (uint32_t)handle, p);
        state->texture_failed[handle] = !state->texture_sent[handle];
    }

    return state->texture_sent[handle] ? handle : -1;
}
