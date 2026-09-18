/**
 * @file render/texture.c
 * @brief Texture handles, their per size class layer free lists and resource commands.
 */

#include "render_internal.h"

/** Size class a square texture of an edge length belongs to.
 *
 * @param size Edge length in pixels
 * @return The size class
 */
static uint32_t box_render_texture_class_of(
    uint32_t size)
{
    ecs_assert(size >= BOX_TEXTURE_MIN_SIZE, ECS_INVALID_PARAMETER, "texture below the min class");
    ecs_assert(size <= BOX_TEXTURE_MAX_SIZE, ECS_INVALID_PARAMETER, "texture above the max class");
    uint32_t result = 0;
    while (box_texture_class_size(result) < size) {
        result ++;
    }

    return result;
}

/** Take a layer of a size class from its free list or hand out a new one.
 *
 * @param state The registry
 * @param cls The size class
 * @return The layer
 */
static uint32_t box_render_texture_layer_alloc(
    RenderState *state,
    uint32_t cls)
{
    ecs_vec_t *free_layers = &state->free_layers[cls];
    int32_t count = ecs_vec_count(free_layers);
    uint32_t result = state->layer_count[cls];
    if (count) {
        result = ecs_vec_get_t(free_layers, uint32_t, count - 1)[0];
        ecs_vec_remove_last(free_layers);
    } else {
        state->layer_count[cls] ++;
    }

    return result;
}

uint32_t box_render_texture_create(
    ecs_world_t *world,
    uint32_t handle,
    uint32_t size,
    uint8_t *pixels,
    bool srgb)
{
    RenderState *state = box_render_state(world);
    uint32_t cls = box_render_texture_class_of(size);
    ecs_assert(!handle || box_texture_class(handle) == cls, ECS_INVALID_PARAMETER,
        "replaced texture handle is of another size class");

    if (!handle) {
        handle = box_texture_handle(cls, box_render_texture_layer_alloc(state, cls));
    }

    box_texture_cmd_t *cmd = ecs_vec_append_t(NULL, &state->pending_textures, box_texture_cmd_t);
    cmd->handle = handle;
    cmd->width = size;
    cmd->height = size;
    cmd->mips = box_texture_mips(size);
    cmd->srgb = srgb;
    cmd->pixels = pixels;

    return handle;
}

void box_render_texture_destroy(
    ecs_world_t *world,
    uint32_t handle)
{
    RenderState *state = ecs_get_mut(world, ecs_id(RenderState), RenderState);
    if (!state || !handle) {
        goto done;
    }

    uint32_t cls = box_texture_class(handle);
    ecs_assert(cls < BOX_TEXTURE_CLASSES, ECS_INVALID_PARAMETER, "bad texture handle");
    ecs_vec_append_t(NULL, &state->free_layers[cls], uint32_t)[0] = box_texture_layer(handle);
    ecs_vec_append_t(NULL, &state->dead_textures, uint32_t)[0] = handle;
done:
    return;
}

void box_render_extract_textures(
    RenderState *state,
    box_packet_t *p)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->dead_textures); i ++) {
        uint32_t *handle = box_vec_push(&p->texture_destroy);
        *handle = ecs_vec_get_t(&state->dead_textures, uint32_t, i)[0];
    }

    for (i = 0; i < ecs_vec_count(&state->pending_textures); i ++) {
        box_texture_cmd_t *cmd = box_vec_push(&p->texture_create);
        *cmd = ecs_vec_get_t(&state->pending_textures, box_texture_cmd_t, i)[0];
    }

    ecs_vec_clear(&state->dead_textures);
    ecs_vec_clear(&state->pending_textures);
}

/** A RenderTexture was removed: give its handle back to the free list.
 *
 * @param it Iterator over the entities that lost their texture
 */
static void box_render_texture_on_remove(
    ecs_iter_t *it)
{
    RenderTexture *rt = ecs_field(it, RenderTexture, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_render_texture_destroy(it->world, rt[i].handle);
    }
}

void box_render_texture_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(RenderTexture) }},
        .events = { EcsOnRemove },
        .callback = box_render_texture_on_remove
    });
}
