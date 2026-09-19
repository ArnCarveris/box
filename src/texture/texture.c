/**
 * @file texture/texture.c
 * @brief Registers the box.texture module and loads images when a Texture is set.
 */

#define BOX_TEXTURE_IMPL
#include "texture_internal.h"

ECS_COMPONENT_DECLARE(TextureImpl);
ECS_COMPONENT_DECLARE(TextureRefs);

/** TextureImpl destructor releasing the kept pixels.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_impl_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    TextureImpl *impl = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_os_free(impl[i].pixels);
    }
}

/** TextureRefs destructor releasing the referrer lists.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_refs_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    TextureRefs *refs = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_map_iter_t mit = ecs_map_iter(&refs[i].map);
        while (ecs_map_next(&mit)) {
            ecs_vec_t *list = ecs_map_ptr(&mit);
            ecs_vec_fini_t(NULL, list, ecs_entity_t);
            ecs_os_free(list);
        }

        ecs_map_fini(&refs[i].map);
    }
}

void box_texture_settings(
    ecs_world_t *world,
    uint32_t *cap,
    bool *keep)
{
    *cap = BOX_TEXTURE_MAX_SIZE;
    *keep = false;
    const Surface *s = ecs_singleton_get(world, Surface);
    if (s) {
        uint32_t quality = (uint32_t)s->texture_quality;
        *cap = quality && quality < BOX_TEXTURE_MAX_SIZE ? quality : BOX_TEXTURE_MAX_SIZE;
        *keep = s->keep_source_textures;
    }
}

/** Keep or release the CPU copy of a loaded texture.
 *
 * @param world The world
 * @param e The texture entity
 * @param img The loaded image
 * @param keep Keep the pixels of level 0 in memory
 */
static void box_texture_keep(
    ecs_world_t *world,
    ecs_entity_t e,
    const box_image_t *img,
    bool keep)
{
    TextureImpl *impl = ecs_get_mut(world, e, TextureImpl);
    ecs_assert(impl != NULL, ECS_INTERNAL_ERROR, "Texture without TextureImpl");
    ecs_os_free(impl->pixels);
    impl->pixels = NULL;
    impl->source_width = img->source_width;
    impl->source_height = img->source_height;
    if (keep) {
        ecs_size_t bytes = (ecs_size_t)(img->size * img->size * 4);
        impl->pixels = ecs_os_malloc(bytes);
        ecs_os_memcpy(impl->pixels, img->pixels, bytes);
    }
}

void box_texture_publish(
    ecs_world_t *world,
    ecs_entity_t e,
    const box_image_t *img,
    bool keep)
{
    RenderTexture *rt = ecs_get_mut(world, e, RenderTexture);
    ecs_assert(rt != NULL, ECS_INTERNAL_ERROR, "texture source without RenderTexture");
    uint32_t reuse = 0;
    if (rt->handle) {
        reuse = (uint32_t)rt->width == img->size ? rt->handle : 0;
        if (!reuse) {
            box_render_texture_destroy(world, rt->handle);
        }
    }

    box_texture_keep(world, e, img, keep);
    rt->handle = box_render_texture_create(world, reuse, img->size, img->pixels, true);
    rt->width = (int32_t)img->size;
    rt->height = (int32_t)img->size;
    ecs_modified(world, e, RenderTexture);
    box_texture_refs_mark(world, e);
}

bool box_texture_claim(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t id)
{
    TextureImpl *impl = ecs_get_mut(world, e, TextureImpl);
    ecs_assert(impl != NULL, ECS_INTERNAL_ERROR, "texture source without TextureImpl");
    bool result = true;
    if (!impl->source) {
        impl->source = id;
    } else if (impl->source != id) {
        result = false;
        if (!impl->conflict) {
            impl->conflict = true;
            const char *name = ecs_get_name(world, e);
            ecs_err("texture '%s' has both a Texture and an Svg, '%s' is ignored",
                name ? name : "<unnamed>", ecs_get_name(world, id));
        }
    }

    return result;
}

void box_texture_release(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t e,
    ecs_entity_t id,
    bool deleting)
{
    const TextureImpl *impl = ecs_get(world, e, TextureImpl);
    if (impl && impl->source && impl->source != id) {
        goto done;
    }

    box_texture_refs_unbind(world, e);
    if (!deleting) {
        ecs_remove(stage, e, RenderTexture);
        ecs_remove(stage, e, TextureImpl);
    }
done:
    return;
}

/** Load the image of a texture entity and hand it to the renderer.
 *
 * @param world The world
 * @param e The texture entity
 * @param path The image file
 * @param request Requested edge length in pixels, 0 keeps the source size
 */
static void box_texture_reload(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *path,
    int32_t request)
{
    if (!path || !path[0]) {
        goto done;
    }

    if (!box_texture_claim(world, e, ecs_id(Texture))) {
        goto done;
    }

    uint32_t cap;
    bool keep;
    box_texture_settings(world, &cap, &keep);

    box_image_t img;
    if (box_texture_load(path, request, cap, &img)) {
        goto done;
    }

    box_texture_publish(world, e, &img, keep);
done:
    return;
}

void box_texture_set_memory(
    ecs_world_t *world,
    ecs_entity_t e,
    const uint8_t *data,
    int32_t size)
{
    if (!ecs_has(world, e, Texture)) {
        ecs_set(world, e, Texture, { .path = ecs_os_strdup("") });
    }

    if (!box_texture_claim(world, e, ecs_id(Texture))) {
        goto done;
    }

    uint32_t cap;
    bool keep;
    box_texture_settings(world, &cap, &keep);

    box_image_t img;
    if (box_texture_load_memory(data, size, 0, cap, &img)) {
        goto done;
    }

    box_texture_publish(world, e, &img, keep);
done:
    return;
}

void box_texture_set_image(
    ecs_world_t *world,
    ecs_entity_t e,
    const uint8_t *pixels,
    int32_t width,
    int32_t height)
{
    if (!ecs_has(world, e, Texture)) {
        ecs_set(world, e, Texture, { .path = ecs_os_strdup("") });
    }

    if (!box_texture_claim(world, e, ecs_id(Texture))) {
        goto done;
    }

    uint32_t cap;
    bool keep;
    box_texture_settings(world, &cap, &keep);

    box_image_t img;
    int32_t request = width > height ? width : height;
    box_texture_image(pixels, width, height, request, cap, &img);
    box_texture_publish(world, e, &img, keep);
done:
    return;
}

/** Texture was set: load the image file and upload it.
 *
 * @param it Iterator over the changed textures
 */
static void box_texture_on_set(
    ecs_iter_t *it)
{
    Texture *t = ecs_field(it, Texture, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        int32_t request = t[i].width > t[i].height ? t[i].width : t[i].height;
        box_texture_reload(it->world, it->entities[i], t[i].path, request);
    }
}

/** Texture was removed: unbind the materials that still reference it.
 *
 * Nothing is unbound once the world is quitting or being deleted, so tearing a
 * world down does not warn about every material that outlives its textures.
 *
 * @param it Iterator over the entities that lost their texture
 */
static void box_texture_on_remove(
    ecs_iter_t *it)
{
    if (ecs_is_fini(it->world) || ecs_should_quit(it->world)) {
        goto done;
    }

    bool deleting = !it->other_table || !ecs_table_get_type(it->other_table)->count;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_texture_release(it->world, it->stage, it->entities[i], ecs_id(Texture), deleting);
    }
done:
    return;
}

void BoxTextureImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTexture);
    ECS_IMPORT(world, BoxRender);

    ECS_META_COMPONENT(world, Texture);
    ECS_META_COMPONENT(world, Svg);
    ECS_COMPONENT_DEFINE(world, TextureImpl);
    ECS_COMPONENT_DEFINE(world, TextureRefs);

    ecs_set_hooks(world, TextureImpl, {
        .ctor = flecs_default_ctor,
        .dtor = box_texture_impl_dtor
    });
    ecs_set_hooks(world, TextureRefs, {
        .ctor = flecs_default_ctor,
        .dtor = box_texture_refs_dtor
    });

    ecs_add_pair(world, ecs_id(Texture), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(TextureImpl), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(Texture), EcsWith, ecs_id(TextureImpl));
    ecs_add_pair(world, ecs_id(Texture), EcsWith, ecs_id(RenderTexture));

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Texture) }},
        .events = { EcsOnSet },
        .callback = box_texture_on_set
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Texture) }},
        .events = { EcsOnRemove },
        .callback = box_texture_on_remove
    });

    box_svg_init(world);
    box_texture_refs_init(world);
}
