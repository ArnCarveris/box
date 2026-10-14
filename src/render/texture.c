/**
 * @file render/texture.c
 * @brief Sends the images box.texture decoded to the backend and picks the sampler anisotropy.
 *
 * A texture gets a backend handle with its first image; a removed texture releases it. The
 * pixels are freed once they are in a packet, so a new backend loads every texture again.
 */

#include "render.h"
#include "../texture/texture.h"

/** State of the texture extract */
typedef struct box_render_textures_t {
    const void *backend;    /**< Context of the backend the textures were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
} box_render_textures_t;

/** Get the anisotropic samples of the samplers: the most any surface asks for.
 *
 * @param world The world
 * @param t The registry
 * @return Samples from 1 to 16
 */
static uint32_t box_render_anisotropy(
    const ecs_world_t *world,
    const box_textures_t *t)
{
    uint32_t result = 0;
    int32_t i;
    ecs_iter_t it = ecs_query_iter(world, t->surfaces);
    while (ecs_query_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (i = 0; i < it.count; i ++) {
            uint32_t a = s[i].anisotropy == AnisotropyDefault
                ? BOX_RENDER_ANISOTROPY_DEFAULT
                : (uint32_t)s[i].anisotropy;
            result = a > result ? a : result;
        }
    }

    if (!result) {
        result = BOX_RENDER_ANISOTROPY_DEFAULT;
    }

    return result > 16 ? 16 : result;
}

/** Send the image of a texture.
 *
 * @param p The packet
 * @param t The registry
 * @param tex The texture
 * @return Bytes of the image
 */
static uint32_t box_render_texture_send(
    box_render_packet_t *p,
    box_textures_t *t,
    box_texture_t *tex)
{
    uint32_t size = (uint32_t)tex->width * (uint32_t)tex->height * 4;
    if (!tex->handle && ecs_vec_count(&t->free_handles)) {
        tex->handle = *ecs_vec_last_t(&t->free_handles, uint32_t);
        ecs_vec_remove_last(&t->free_handles);
    } else if (!tex->handle) {
        tex->handle = t->next_handle ++;
    }

    box_render_texture_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_TEXTURE,
        BOX_RENDER_CREATE, tex->handle, (uint32_t)sizeof(box_render_texture_desc_t) + size);
    desc->width = (uint32_t)tex->width;
    desc->height = (uint32_t)tex->height;
    ecs_os_memcpy(desc + 1, tex->pixels, (ecs_size_t)size);
    ecs_os_free(tex->pixels);
    tex->pixels = NULL;
    tex->sent = true;

    return size;
}

/** Send the queued images, at most a budget of bytes per packet unless it is the first one.
 *
 * @param p The packet
 * @param t The registry
 */
static void box_render_textures_send(
    box_render_packet_t *p,
    box_textures_t *t)
{
    int32_t i, kept = 0, count = ecs_vec_count(&t->queued);
    ecs_entity_t *queued = ecs_vec_first(&t->queued);
    uint64_t bytes = 0;
    for (i = 0; i < count; i ++) {
        box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, queued[i]);
        if (!tex || !tex->pixels) {
            if (tex) {
                tex->queued = false;
            }

            continue;
        }

        if (bytes && bytes + (uint64_t)tex->width * (uint64_t)tex->height * 4 >
            BOX_RENDER_TEXTURE_BUDGET)
        {
            queued[kept ++] = queued[i];
            continue;
        }

        bytes += box_render_texture_send(p, t, tex);
        tex->queued = false;
    }

    ecs_vec_set_count_t(NULL, &t->queued, ecs_entity_t, kept);
}

/** Release removed textures, send loaded images and set the sampler anisotropy.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractTextures(
    ecs_iter_t *it)
{
    box_render_textures_t *state = it->ctx;
    ecs_world_t *world = it->world;
    box_textures_t *t = box_textures_get(world);
    box_render_packet_t *p = box_render_packet(world);
    const box_render_backend_t *backend = box_render_get_backend(world);
    int32_t i;
    p->anisotropy = BOX_RENDER_ANISOTROPY_DEFAULT;
    if (!t) {
        goto done;
    }

    if (backend->ctx != state->backend || backend->render != state->render) {
        if (state->backend || state->render) {
            for (i = 0; i < ecs_vec_count(&t->released); i ++) {
                *ecs_vec_append_t(NULL, &t->free_handles, uint32_t) =
                    *ecs_vec_get_t(&t->released, uint32_t, i);
            }

            ecs_vec_clear(&t->released);
            box_textures_reload(world, t, true);
        }

        state->backend = backend->ctx;
        state->render = backend->render;
    }

    for (i = 0; i < ecs_vec_count(&t->released); i ++) {
        uint32_t handle = *ecs_vec_get_t(&t->released, uint32_t, i);
        box_render_packet_resource(p, BOX_RENDER_TEXTURE, BOX_RENDER_DESTROY, handle, 0);
        *ecs_vec_append_t(NULL, &t->free_handles, uint32_t) = handle;
    }

    ecs_vec_clear(&t->released);
    box_render_textures_send(p, t);
    p->anisotropy = box_render_anisotropy(world, t);
done:
    return;
}

void box_render_textures_init(
    ecs_world_t *world)
{
    box_render_textures_t *state = ecs_os_calloc_t(box_render_textures_t);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractTextures" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractTextures,
        .ctx = state,
        .ctx_free = ecs_os_api.free_
    });
}
