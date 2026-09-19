/**
 * @file render/texture.c
 * @brief Texture handles, the pager that spreads a size class over pages, and its commands.
 *
 * An array texture holds at most the array layer limit of the device, which is
 * the whole capacity of a size class when the class is one array texture. A
 * class is a list of pages instead: the pager hands a texture the first page of
 * its class with a free layer and opens a new page when none has room, so a
 * class spans as many array textures as it needs. The layer of a handle counts
 * across the pages of the pager, which is what puts the page in the high bits
 * of the packed instance slot.
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

/** Page of a size class with a free layer, opening one when none has room.
 *
 * Pages are opened in order and never closed, so the first page that was not
 * opened yet comes after every page that holds a class.
 *
 * @param state The registry
 * @param cls The size class
 * @return The page, NULL when every page of the pager is taken
 */
static box_texture_page_t* box_render_texture_page_of(
    RenderState *state,
    uint32_t cls)
{
    box_texture_page_t *result = NULL;
    uint32_t i;
    for (i = 0; i < BOX_TEXTURE_PAGES; i ++) {
        box_texture_page_t *page = &state->pages[i];
        if (!page->used) {
            page->used = true;
            page->cls = cls;
            result = page;
            break;
        }

        if (page->cls != cls) {
            continue;
        }

        if (ecs_vec_count(&page->free_layers) || page->count < BOX_TEXTURE_PAGE_LAYERS) {
            result = page;
            break;
        }
    }

    return result;
}

/** Take a layer of a size class from a page of the pager.
 *
 * @param state The registry
 * @param cls The size class
 * @return The layer, counted across the pages, or the layer limit when none is left
 */
static uint32_t box_render_texture_layer_alloc(
    RenderState *state,
    uint32_t cls)
{
    uint32_t result = BOX_TEXTURE_LAYER_MAX + 1;
    box_texture_page_t *page = box_render_texture_page_of(state, cls);
    if (!page) {
        if (!state->pages_full) {
            state->pages_full = true;
            ecs_err("texture pager: every page is taken, %d pages of %d layers; "
                "textures past this point render with the color of their material",
                BOX_TEXTURE_PAGES, BOX_TEXTURE_PAGE_LAYERS);
        }

        goto done;
    }

    int32_t count = ecs_vec_count(&page->free_layers);
    uint32_t layer = page->count;
    if (count) {
        layer = ecs_vec_get_t(&page->free_layers, uint32_t, count - 1)[0];
        ecs_vec_remove_last(&page->free_layers);
    } else {
        page->count ++;
    }

    result = (uint32_t)(page - state->pages) * BOX_TEXTURE_PAGE_LAYERS + layer;
done:
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
        uint32_t layer = box_render_texture_layer_alloc(state, cls);
        if (layer > BOX_TEXTURE_LAYER_MAX) {
            free(pixels);
            goto done;
        }

        handle = box_texture_handle(cls, layer);
    }

    state->textured = true;

    box_texture_cmd_t *cmd = ecs_vec_append_t(NULL, &state->pending_textures, box_texture_cmd_t);
    cmd->handle = handle;
    cmd->width = size;
    cmd->height = size;
    cmd->mips = box_texture_mips(size);
    cmd->srgb = srgb;
    cmd->pixels = pixels;
done:
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

    ecs_assert(box_texture_class(handle) < BOX_TEXTURE_CLASSES, ECS_INVALID_PARAMETER,
        "bad texture handle");
    uint32_t page = box_texture_page(handle);
    ecs_assert(page < BOX_TEXTURE_PAGES, ECS_INVALID_PARAMETER, "bad texture handle");
    ecs_vec_append_t(NULL, &state->pages[page].free_layers, uint32_t)[0] =
        box_texture_page_layer(handle);
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
