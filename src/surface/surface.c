/**
 * @file surface/surface.c
 * @brief Creates windows and renderers from Surface components and submits frames.
 */

#define BOX_SURFACE_IMPL
#include <box.h>
#include <string.h>
#include "surface_internal.h"
#include "../shader/shader_internal.h"

ECS_COMPONENT_DECLARE(SurfaceImpl);

/** Create the window and renderer for a surface that has none yet.
 *
 * @param world The world
 * @param e The surface entity
 * @param s The surface settings
 * @param impl The runtime objects to fill
 */
static void box_surface_open(
    ecs_world_t *world,
    ecs_entity_t e,
    const Surface *s,
    SurfaceImpl *impl)
{
    const App *app = ecs_singleton_get(world, App);
    ecs_assert(app != NULL, ECS_INVALID_OPERATION, "import box.core before box.surface");

    box_renderer_desc_t desc = {
        .width = (uint32_t)s->width,
        .height = (uint32_t)s->height,
        .vsync = s->vsync,
        .anisotropy = s->anisotropy ? (uint32_t)s->anisotropy : BOX_ANISOTROPY_DEFAULT,
#ifndef BOX_PLATFORM_WEB
        .threaded = true
#endif
    };

    if (!app->headless) {
        box_window_desc_t window_desc = {
            .title = s->title ? s->title : "box",
            .width = s->width,
            .height = s->height
        };
        impl->window = box_window_create(&window_desc);
        if (impl->window) {
            int32_t pt_width, pt_height;
            box_window_size(impl->window, (int32_t*)&desc.width, (int32_t*)&desc.height,
                &pt_width, &pt_height);
            desc.metal_layer = box_window_metal_layer(impl->window);
            desc.canvas_selector = box_window_canvas(impl->window);
        }
    }

    impl->renderer = box_renderer_create(&desc);
    if (!impl->renderer) {
        ecs_err("failed to create renderer for surface %s", ecs_get_name(world, e));
    }
}

/** Apply settings that changed on an existing surface.
 *
 * @param s The surface settings
 * @param impl The runtime objects
 */
static void box_surface_apply(
    const Surface *s,
    SurfaceImpl *impl)
{
    if (!impl->window) {
        goto done;
    }

    if (s->width != impl->width || s->height != impl->height) {
        box_window_set_size(impl->window, s->width, s->height);
    }

    const char *title = s->title ? s->title : "box";
    if (!impl->title || strcmp(title, impl->title)) {
        box_window_set_title(impl->window, title);
        ecs_os_free(impl->title);
        impl->title = ecs_os_strdup(title);
    }
done:
    return;
}

/** Surface on_set hook: open or reconfigure the window and renderer.
 *
 * @param it Iterator over the changed entities
 */
static void box_surface_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    Surface *s = ecs_field(it, Surface, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        SurfaceImpl *impl = ecs_ensure(world, e, SurfaceImpl);
        if (!impl->renderer) {
            box_surface_open(world, e, &s[i], impl);
        }

        box_surface_apply(&s[i], impl);
        impl->width = s[i].width;
        impl->height = s[i].height;
    }
}

/** SurfaceImpl destructor: stop the renderer and close the window.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_surface_impl_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    SurfaceImpl *impl = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (impl[i].renderer) {
            box_renderer_destroy(impl[i].renderer);
        }

        if (impl[i].window) {
            box_window_destroy(impl[i].window);
        }

        ecs_os_free(impl[i].title);
    }
}

/** Poll window events and publish the actual size to the Surface.
 *
 * @param it Iterator over surfaces
 */
static void BoxSurfacePoll(
    ecs_iter_t *it)
{
    Surface *s = ecs_field(it, Surface, 0);
    SurfaceImpl *impl = ecs_field(it, SurfaceImpl, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (!impl[i].window) {
            s[i].actual_width = s[i].width;
            s[i].actual_height = s[i].height;
            s[i].point_width = s[i].width;
            s[i].point_height = s[i].height;
            continue;
        }

        box_window_poll(impl[i].window);
        box_window_size(impl[i].window, &s[i].actual_width, &s[i].actual_height,
            &s[i].point_width, &s[i].point_height);
        if (box_window_should_close(impl[i].window)) {
            ecs_quit(it->world);
        }
    }
}

/** Compose the capture path for the current frame, empty when nothing to write.
 *
 * @param s The surface settings
 * @param frame The frame number
 * @param out Buffer receiving the path
 * @param size Buffer size
 */
static void box_surface_capture_path(
    const Surface *s,
    int32_t frame,
    char *out,
    size_t size)
{
    out[0] = '\0';
    if (!s->write_to_file) {
        goto done;
    }

    if (s->write_nth <= 0) {
        ecs_os_snprintf(out, (ecs_size_t)size, "%s", s->write_to_file);
        goto done;
    }

    if (frame % s->write_nth) {
        goto done;
    }

    const char *ext = strrchr(s->write_to_file, '.');
    int32_t base_len = ext ? (int32_t)(ext - s->write_to_file) : (int32_t)strlen(s->write_to_file);
    ecs_os_snprintf(out, (ecs_size_t)size, "%.*s_%05d%s",
        base_len, s->write_to_file, frame, ext ? ext : "");
done:
    return;
}

/** Acquire the packet of the frame and fill its surface parameters.
 *
 * @param it Iterator over surfaces
 */
static void BoxSurfaceAcquire(
    ecs_iter_t *it)
{
    const App *app = ecs_singleton_get(it->world, App);
    Surface *s = ecs_field(it, Surface, 0);
    SurfaceImpl *impl = ecs_field(it, SurfaceImpl, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        impl[i].packet = NULL;
        if (!impl[i].renderer) {
            continue;
        }

        box_packet_t *p = box_renderer_acquire(impl[i].renderer);
        if (!p) {
            continue;
        }

        p->frame = (uint32_t)app->frame;
        p->time = ecs_get_world_info(it->world)->world_time_total;
        p->delta = ecs_get_world_info(it->world)->delta_time;
        p->width = (uint32_t)s[i].actual_width;
        p->height = (uint32_t)s[i].actual_height;
        p->clear[0] = 0.02f;
        p->clear[1] = 0.02f;
        p->clear[2] = 0.03f;
        p->clear[3] = 1.0f;
        box_surface_capture_path(&s[i], app->frame, p->capture_path, sizeof(p->capture_path));
        impl[i].packet = p;
    }
}

/** Submit the packet acquired at the start of the frame.
 *
 * @param it Iterator over surfaces
 */
static void BoxSurfaceSubmit(
    ecs_iter_t *it)
{
    SurfaceImpl *impl = ecs_field(it, SurfaceImpl, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (impl[i].packet) {
            box_shader_extract(it->world, impl[i].packet);
            box_renderer_submit(impl[i].renderer, impl[i].packet);
            impl[i].packet = NULL;
        }
    }
}

void BoxSurfaceImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxSurface);
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxShader);

    ECS_META_COMPONENT(world, Anisotropy);
    ECS_META_COMPONENT(world, TextureQuality);
    ECS_META_COMPONENT(world, TextureCompression);
    ECS_META_COMPONENT(world, TextureCacheMode);
    ECS_META_COMPONENT(world, Surface);
    ECS_COMPONENT_DEFINE(world, SurfaceImpl);

    ecs_set_hooks(world, Surface, { .on_set = box_surface_on_set });
    ecs_set_hooks(world, SurfaceImpl, {
        .ctor = flecs_default_ctor,
        .dtor = box_surface_impl_dtor
    });
    ecs_add_pair(world, ecs_id(SurfaceImpl), EcsOnInstantiate, EcsDontInherit);

    ECS_SYSTEM(world, BoxSurfacePoll, EcsOnLoad, Surface, SurfaceImpl);
    ECS_SYSTEM(world, BoxSurfaceAcquire, EcsPreStore, Surface, SurfaceImpl);
    ECS_SYSTEM(world, BoxSurfaceSubmit, EcsOnStore, Surface, SurfaceImpl);
}
