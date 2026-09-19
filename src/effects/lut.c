/**
 * @file effects/lut.c
 * @brief Loads the display transform lookup cube a tone mapping pass samples.
 *
 * The cube is an engine asset next to the shaders, so it is found through the
 * shader search path and embedded in the wasm build like every other one. It is
 * read the first time a pass asks for it and travels to the renderer as one
 * resource command, which is why a scene that never selects the operator pays
 * neither the file nor the texture.
 */

#include "effects_internal.h"
#include <string.h>

/** Cube the engine ships, resolved against the shader search path */
#define BOX_LUT_PATH "etc/shaders/lut/tony_mc_mapface.lut"

/** Bytes of the header of a cube file */
#define BOX_LUT_HEADER (16)

/** Magic a cube file starts with */
#define BOX_LUT_MAGIC "BOXLUT3D"

/** Bytes of the magic */
#define BOX_LUT_MAGIC_LEN (8)

/** Read a little endian word out of a cube file.
 *
 * @param bytes First byte of the word
 * @return The word
 */
static uint32_t box_lut_word(
    const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/** Read the cube file and hand its texels to the packet.
 *
 * @param p The packet
 * @return true when the cube reached the packet
 */
static bool box_lut_load(
    box_packet_t *p)
{
    bool result = false;
    char resolved[BOX_SHADER_PATH_MAX];
    if (!box_shader_resolve(BOX_LUT_PATH, resolved, sizeof(resolved))) {
        ecs_err("box.effects: %s: file not found", BOX_LUT_PATH);
        goto done;
    }

    size_t size = 0;
    unsigned char *bytes = box_shader_read_bytes(resolved, &size);
    if (!bytes) {
        ecs_err("box.effects: %s: cannot be read", BOX_LUT_PATH);
        goto done;
    }

    uint32_t dim = 0;
    if (size > BOX_LUT_HEADER && !memcmp(bytes, BOX_LUT_MAGIC, BOX_LUT_MAGIC_LEN)) {
        dim = box_lut_word(bytes + BOX_LUT_MAGIC_LEN);
    }

    size_t texels = (size_t)dim * dim * dim;
    if (!dim || size != BOX_LUT_HEADER + texels * sizeof(uint32_t)) {
        ecs_err("box.effects: %s: not a lookup cube", BOX_LUT_PATH);
        ecs_os_free(bytes);
        goto done;
    }

    box_lut_cmd_t *cmd = box_vec_push(&p->lut_create);
    cmd->dim = dim;
    cmd->texels = malloc(texels * sizeof(uint32_t));
    size_t i;
    for (i = 0; i < texels; i ++) {
        cmd->texels[i] = box_lut_word(bytes + BOX_LUT_HEADER + i * sizeof(uint32_t));
    }

    ecs_os_free(bytes);
    result = true;
done:
    return result;
}

bool box_effect_lut(
    EffectState *state,
    box_packet_t *p)
{
    if (state->lut_failed) {
        goto done;
    }

    if (!state->lut_sent) {
        state->lut_sent = box_lut_load(p);
        state->lut_failed = !state->lut_sent;
    }

done:
    return state->lut_sent;
}
