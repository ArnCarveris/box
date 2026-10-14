/**
 * @file surface/surface.c
 * @brief Registers the box.surface module and turns Surface values into render targets.
 *
 * The main thread opens windows, measures surfaces and describes them as target resources in
 * the render packet; the backend creates the GPU objects on the thread that renders.
 */

#define BOX_SURFACE_IMPL
#include "surface.h"
#include "../render/render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Largest width or height of a surface and of its render size, in pixels */
#define BOX_SURFACE_MAX_SIZE (8192)

/** A released target whose window closes once the packet that releases it was rendered */
typedef struct box_surface_retired_t {
    uint32_t handle;        /**< Handle of the target */
    box_window_t *window;   /**< The window, NULL for an offscreen target */
    int32_t packet;         /**< Packet that releases the target, 0 until it is extracted */
} box_surface_retired_t;

/** State of the box.surface module shared by its hooks and systems */
typedef struct box_surface_state_t {
    uint32_t last_handle;   /**< Last handle given to a target */
    ecs_vec_t retired;      /**< box_surface_retired_t */
} box_surface_state_t;

/** Engine side of a surface: its target, its window and what the backend was told */
typedef struct SurfaceImpl {
    uint32_t handle;                /**< Handle of the target, 0 before it was created */
    box_window_t *window;           /**< The window, NULL for an offscreen target */
    int32_t window_width;           /**< Width in points the window was given */
    int32_t window_height;          /**< Height in points the window was given */
    box_render_target_desc_t desc;  /**< Description the backend last received */
    char *write_to_file;            /**< Surface.write_to_file the capture path was made from */
    char *capture;                  /**< Path the frames are written to */
} SurfaceImpl;

ECS_COMPONENT_DECLARE(SurfaceImpl);

/** Hand the target of a surface to the retired list and empty the surface.
 *
 * @param state The module state
 * @param impl The engine side of the surface
 */
static void box_surface_retire(
    box_surface_state_t *state,
    SurfaceImpl *impl)
{
    if (impl->handle) {
        box_surface_retired_t *r = ecs_vec_append_t(
            NULL, &state->retired, box_surface_retired_t);
        r->handle = impl->handle;
        r->window = impl->window;
        r->packet = 0;
    }

    ecs_os_free(impl->write_to_file);
    ecs_os_free(impl->capture);
    ecs_os_memset_t(impl, 0, SurfaceImpl);
}

/** Retire the targets of destroyed surfaces.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info, whose hook context is the module state
 */
static void box_surface_impl_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SurfaceImpl *impl = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_surface_retire(ti->hooks.ctx, &impl[i]);
    }
}

/** Move the engine side of surfaces, retiring what the destination held.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info, whose hook context is the module state
 */
static void box_surface_impl_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SurfaceImpl *d = dst;
    SurfaceImpl *s = src;
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (&d[i] != &s[i]) {
            box_surface_retire(ti->hooks.ctx, &d[i]);
            d[i] = s[i];
            ecs_os_memset_t(&s[i], 0, SurfaceImpl);
        }
    }
}

/** Copy surfaces: a copy gets a target of its own, so the destination starts empty.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info, whose hook context is the module state
 */
static void box_surface_impl_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SurfaceImpl *d = dst;
    int32_t i;
    (void)src;
    for (i = 0; i < count; i ++) {
        box_surface_retire(ti->hooks.ctx, &d[i]);
    }
}

/** Close the windows that are left and free the module state.
 *
 * The state is the hook context of SurfaceImpl, which the world frees after it destroyed every
 * component, so every SurfaceImpl dtor has retired its target by then and the render thread
 * and backend are gone.
 *
 * @param ctx The module state
 */
static void box_surface_state_free(
    void *ctx)
{
    box_surface_state_t *state = ctx;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->retired); i ++) {
        box_surface_retired_t *r = ecs_vec_get_t(&state->retired, box_surface_retired_t, i);
        if (r->window) {
            box_window_free(r->window);
        }
    }

    ecs_vec_fini_t(NULL, &state->retired, box_surface_retired_t);
    ecs_os_free(state);
}

/** Check whether a surface renders offscreen.
 *
 * @param world The world
 * @param s The surface
 * @return Whether the run is headless or a test, or the surface renders to a texture
 */
static bool box_surface_offscreen(
    const ecs_world_t *world,
    const Surface *s)
{
    const App *app = ecs_singleton_get(world, App);
    const char *test = getenv(BOX_TEST_ENV);
    bool is_test = test && test[0] && strcmp(test, "0") != 0;

    return !app || app->headless || s->texture || is_test;
}

/** Clamp a size in points to the sizes a surface supports.
 *
 * @param value The size of the Surface
 * @return The size, between 1 and BOX_SURFACE_MAX_SIZE
 */
static int32_t box_surface_points(
    int32_t value)
{
    int32_t result = value > 1 ? value : 1;

    return result < BOX_SURFACE_MAX_SIZE ? result : BOX_SURFACE_MAX_SIZE;
}

/** Divide a size in pixels by the resolution scale.
 *
 * @param pixels The size
 * @param scale The resolution scale, a scale of 0 or less renders at the largest size
 * @return The render size, between 1 and BOX_SURFACE_MAX_SIZE
 */
static int32_t box_surface_scaled(
    int32_t pixels,
    float scale)
{
    float value = scale > 0 ? (float)pixels / scale : (float)BOX_SURFACE_MAX_SIZE;
    int32_t result = value < (float)BOX_SURFACE_MAX_SIZE
        ? (int32_t)(value + 0.5f)
        : BOX_SURFACE_MAX_SIZE;
    if (result < 1) {
        result = 1;
    }

    return result;
}

/** Construct Surface values with the documented defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_surface_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Surface *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Surface){
            .width = BOX_SURFACE_WIDTH,
            .height = BOX_SURFACE_HEIGHT,
            .resolution_scale = 1.0f,
            .pixel_ratio = 1.0f
        };
    }
}

/** Give a surface its target handle and, unless it renders offscreen, its window.
 *
 * @param world The world
 * @param s The surface
 * @param impl The engine side of the surface
 * @param state The module state
 */
static void box_surface_open(
    ecs_world_t *world,
    const Surface *s,
    SurfaceImpl *impl,
    box_surface_state_t *state)
{
    impl->handle = ++ state->last_handle;
    if (box_surface_offscreen(world, s)) {
        goto done;
    }

    impl->window_width = box_surface_points(s->width);
    impl->window_height = box_surface_points(s->height);
    impl->window = box_window_new(world, impl->window_width, impl->window_height, "box");
    if (!impl->window) {
        ecs_warn("cannot open a window, the surface renders offscreen");
    }

done:
    return;
}

/** Describe the target of a surface: its size in pixels and points, render size and window
 * layer. Offscreen a point is pixel_ratio pixels; a window reports both, so a retina window has two
 * pixels per point.
 *
 * @param s The surface
 * @param impl The engine side of the surface, whose window follows the requested size
 * @param desc The description, zeroed first so it can be compared bytewise
 */
static void box_surface_measure(
    const Surface *s,
    SurfaceImpl *impl,
    box_render_target_desc_t *desc)
{
    int32_t width = box_surface_points(s->width);
    int32_t height = box_surface_points(s->height);
    float ratio = s->pixel_ratio > 0 ? s->pixel_ratio : 1.0f;
    ecs_os_memset_t(desc, 0, box_render_target_desc_t);
    desc->width = box_surface_points((int32_t)((float)width * ratio + 0.5f));
    desc->height = box_surface_points((int32_t)((float)height * ratio + 0.5f));
    desc->point_width = width;
    desc->point_height = height;
    desc->vsync = s->vsync;
    desc->texture = s->texture != 0;
    if (impl->window) {
        if (width != impl->window_width || height != impl->window_height) {
            box_window_resize(impl->window, width, height);
            impl->window_width = width;
            impl->window_height = height;
        }

        box_window_size(impl->window, &desc->width, &desc->height);
        box_window_points(impl->window, &desc->point_width, &desc->point_height);
        desc->layer = box_window_layer(impl->window);
        desc->refresh = s->vsync ? box_window_refresh(impl->window) : 0;
    }

    if (desc->width > 0 && desc->height > 0) {
        desc->render_width = box_surface_scaled(desc->width, s->resolution_scale);
        desc->render_height = box_surface_scaled(desc->height, s->resolution_scale);
    }
}

/** Get the file the frames of a surface are written to.
 *
 * @param world The world
 * @param s The surface
 * @param impl The engine side of the surface, which keeps the path
 * @return The path, NULL when the surface is not captured
 */
static const char* box_surface_capture(
    const ecs_world_t *world,
    const Surface *s,
    SurfaceImpl *impl)
{
    const char *file = s->write_to_file;
    const AppStatus *status;
    if (!file || !file[0]) {
        goto done;
    }

    if (impl->write_to_file && !strcmp(impl->write_to_file, file)) {
        goto done;
    }

    ecs_os_free(impl->write_to_file);
    ecs_os_free(impl->capture);
    impl->write_to_file = ecs_os_strdup(file);
    status = ecs_singleton_get(world, AppStatus);
    if (file[0] != '/' && status && status->tmp_dir) {
        impl->capture = flecs_asprintf("%s/%s", status->tmp_dir, file);
    } else {
        impl->capture = ecs_os_strdup(file);
    }

done:
    return file && file[0] ? impl->capture : NULL;
}

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static float box_surface_linear(
    uint8_t value)
{
    float c = (float)value / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Find the clear color of a surface: the color of its first view that clears, skipping the
 * views of captures and, without a view_root, the views other surfaces claim.
 *
 * @param world The world
 * @param s The surface
 * @param p The packet with the views of the frame
 * @param out Receives the linear color, opaque black when no view clears
 */
static void box_surface_clear(
    const ecs_world_t *world,
    const Surface *s,
    const box_render_packet_t *p,
    float *out)
{
    rgba color = { 0, 0, 0, 255 };
    int32_t i;
    for (i = 0; i < ecs_vec_count(&p->views); i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        if (v->target) {
            continue;
        }

        if (s->view_root ? v->parent != s->view_root : v->claimed) {
            continue;
        }

        if (v->clear.enabled) {
            color = v->clear.color;
            break;
        }
    }

    out[0] = box_surface_linear(color.r);
    out[1] = box_surface_linear(color.g);
    out[2] = box_surface_linear(color.b);
    out[3] = (float)color.a / 255.0f;
}

/** Bring the target of a surface up to date and add it to the packet.
 *
 * @param world The stage of the system
 * @param e The surface entity
 * @param s The surface
 * @param impl The engine side of the surface
 * @param state The module state
 */
static void box_surface_frame(
    ecs_world_t *world,
    ecs_entity_t e,
    const Surface *s,
    SurfaceImpl *impl,
    box_surface_state_t *state)
{
    box_render_packet_t *p = box_render_packet(world);
    box_render_resource_op_t op = BOX_RENDER_UPDATE;
    box_render_target_desc_t desc;
    float clear[4];
    if (!impl->handle) {
        box_surface_open(world, s, impl, state);
        op = BOX_RENDER_CREATE;
    }

    box_surface_measure(s, impl, &desc);
    if (!desc.render_width) {
        goto done;
    }

    if (op == BOX_RENDER_CREATE || memcmp(&desc, &impl->desc, sizeof(desc))) {
        void *dst = box_render_packet_resource(
            p, BOX_RENDER_TARGET, op, impl->handle, sizeof(desc));
        ecs_os_memcpy_t(dst, &desc, box_render_target_desc_t);
        impl->desc = desc;
        ecs_set(world, e, SurfaceStatus, {
            .width = desc.width,
            .height = desc.height,
            .point_width = desc.point_width,
            .point_height = desc.point_height,
            .render_width = desc.render_width,
            .render_height = desc.render_height,
            .window = impl->window != NULL
        });
    }

    if (s->vsync && !box_surface_offscreen(world, s)) {
        p->vsync = true;
        p->refresh = p->refresh > 0 ? p->refresh : desc.refresh;
    }

    box_surface_clear(world, s, p, clear);
    box_render_target_t *target = box_render_packet_target(
        p, impl->handle, box_surface_capture(world, s, impl));
    ecs_os_memcpy_n(target->clear, clear, float, 4);
    target->view_root = s->view_root;
    if (s->texture) {
        box_render_texture_target(world, s->texture, impl->handle);
    }

done:
    return;
}

/** Release the targets of removed surfaces and close their windows once that rendered.
 *
 * @param world The world
 * @param state The module state
 */
static void box_surface_release(
    ecs_world_t *world,
    box_surface_state_t *state)
{
    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    box_render_packet_t *p = box_render_packet(world);
    int32_t i;
    for (i = ecs_vec_count(&state->retired) - 1; i >= 0; i --) {
        box_surface_retired_t *r = ecs_vec_get_t(&state->retired, box_surface_retired_t, i);
        if (!r->packet) {
            box_render_packet_resource(p, BOX_RENDER_TARGET, BOX_RENDER_DESTROY, r->handle, 0);
            r->packet = status->submitted + 1;
        } else if (status->rendered >= r->packet) {
            if (r->window) {
                box_window_free(r->window);
            }

            ecs_vec_remove_t(&state->retired, box_surface_retired_t, i);
        }
    }
}

/** Mark the views of the packet whose parent is the view_root of a surface, which surfaces
 * without a view_root then skip.
 *
 * @param world The world
 * @param p The packet
 */
static void box_surface_claim(
    ecs_world_t *world,
    box_render_packet_t *p)
{
    int32_t i, v, count = ecs_vec_count(&p->views);
    ecs_iter_t it = ecs_each(world, Surface);
    while (ecs_each_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (i = 0; i < it.count; i ++) {
            for (v = 0; s[i].view_root && v < count; v ++) {
                box_render_view_t *view = ecs_vec_get_t(&p->views, box_render_view_t, v);
                view->claimed |= view->parent == s[i].view_root;
            }
        }
    }
}

/** Describe every surface in the packet of the frame and release removed ones.
 *
 * @param it The iterator of the system
 */
static void BoxSurfaceExtract(
    ecs_iter_t *it)
{
    box_surface_state_t *state = it->ctx;
    box_surface_release(it->world, state);
    box_render_texture_targets_clear(it->world);
    box_surface_claim(it->world, box_render_packet(it->world));
    while (ecs_iter_next(it)) {
        const Surface *s = ecs_field(it, Surface, 0);
        SurfaceImpl *impl = ecs_field(it, SurfaceImpl, 1);
        int32_t i;
        for (i = 0; i < it->count; i ++) {
            box_surface_frame(it->stage, it->entities[i], &s[i], &impl[i], state);
        }
    }
}

/** Process window events and stop the world when a window is closed.
 *
 * @param it The iterator of the system
 */
static void BoxSurfacePoll(
    ecs_iter_t *it)
{
    box_window_poll();
    while (ecs_iter_next(it)) {
        const SurfaceImpl *impl = ecs_field(it, SurfaceImpl, 0);
        int32_t i;
        for (i = 0; i < it->count; i ++) {
            if (impl[i].window && box_window_closed(impl[i].window)) {
                ecs_quit(it->world);
            }
        }
    }
}

/** Give a set surface its engine side and make the world render with the GPU.
 *
 * @param it The iterator of the observer
 */
static void BoxSurfaceOnSet(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_add(it->world, it->entities[i], SurfaceImpl);
    }

    box_render_use_gpu(ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world)));
}

/** Release the target of a surface removed from a live entity.
 *
 * @param it The iterator of the observer
 */
static void BoxSurfaceOnRemove(
    ecs_iter_t *it)
{
    int32_t i;
    if (it->other_table && ecs_table_get_type(it->other_table)->count) {
        for (i = 0; i < it->count; i ++) {
            ecs_remove(it->stage, it->entities[i], SurfaceImpl);
        }
    }
}

void BoxSurfaceImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxSurface);
    ECS_IMPORT(world, BoxApp);
    ECS_IMPORT(world, BoxRender);

    ECS_META_COMPONENT(world, Anisotropy);
    ECS_META_COMPONENT(world, TextureQuality);
    ECS_META_COMPONENT(world, Surface);
    box_owned_hooks(world, Surface, BOX_OWNED_STR(Surface, write_to_file));
    ecs_set_hooks(world, Surface, { .ctor = box_surface_ctor });
    ECS_META_COMPONENT(world, SurfaceStatus);
    ECS_COMPONENT_DEFINE(world, SurfaceImpl);

    box_surface_state_t *state = ecs_os_calloc_t(box_surface_state_t);
    ecs_vec_init_t(NULL, &state->retired, box_surface_retired_t, 0);

    ecs_set_hooks(world, SurfaceImpl, {
        .ctor = flecs_default_ctor,
        .dtor = box_surface_impl_dtor,
        .move = box_surface_impl_move,
        .copy = box_surface_impl_copy,
        .ctx = state,
        .ctx_free = box_surface_state_free
    });
    ecs_add_pair(world, ecs_id(SurfaceImpl), EcsOnInstantiate, EcsDontInherit);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "SurfaceOnSet" }),
        .query.terms = {{ .id = ecs_id(Surface) }},
        .events = { EcsOnSet },
        .callback = BoxSurfaceOnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "SurfaceOnRemove" }),
        .query.terms = {{ .id = ecs_id(Surface) }},
        .events = { EcsOnRemove },
        .callback = BoxSurfaceOnRemove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "SurfacePoll" }),
        .phase = EcsOnLoad,
        .query.terms = {{ .id = ecs_id(SurfaceImpl), .inout = EcsIn }},
        .run = BoxSurfacePoll
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "SurfaceExtract" }),
        .phase = RenderExtract,
        .query.terms = {
            { .id = ecs_id(Surface), .inout = EcsIn },
            { .id = ecs_id(SurfaceImpl) }
        },
        .run = BoxSurfaceExtract,
        .ctx = state
    });
}
