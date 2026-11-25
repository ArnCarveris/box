/**
 * @file render/captures.c
 * @brief One-off captures of a camera, requested on the main thread and written by the renderer.
 *
 * A capture becomes an offscreen target that exists for one packet: the packet creates it,
 * renders the view of the capture into it and writes the PNG, the next packet destroys it.
 *
 * A camera that a view renders is captured as on screen, with the lighting, shadows, culling,
 * effects and debug drawing of the view; another camera is captured lit, without shadows,
 * effects or debug drawing and with the default culling.
 */

#include "render.h"
#include <math.h>

/** Number of target handles captures cycle through */
#define BOX_RENDER_CAPTURE_HANDLES (0x10000000u)

/** Largest width or height of a capture, in pixels */
#define BOX_RENDER_CAPTURE_MAX_SIZE (8192)

/** Capture queue of a world */
typedef struct box_render_captures_t {
    ecs_vec_t pending;      /**< box_render_capture_desc_t with an owned path */
    ecs_vec_t sent;         /**< uint32_t, targets the previous packet created */
    uint32_t next;          /**< Counter of capture target handles */
} box_render_captures_t;

/** Singleton that owns the capture queue of a world */
typedef struct RenderCaptures {
    box_render_captures_t *captures; /**< The queue */
} RenderCaptures;

ECS_COMPONENT_DECLARE(RenderCaptures);

/** Free the capture queues of the singletons.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_captures_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RenderCaptures *c = ptr;
    int32_t i, j;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_render_captures_t *q = c[i].captures;
        if (!q) {
            continue;
        }

        for (j = 0; j < ecs_vec_count(&q->pending); j ++) {
            box_render_capture_desc_t *d = ecs_vec_get_t(
                &q->pending, box_render_capture_desc_t, j);
            ecs_os_free(ECS_CONST_CAST(char*, d->path));
        }

        ecs_vec_fini_t(NULL, &q->pending, box_render_capture_desc_t);
        ecs_vec_fini_t(NULL, &q->sent, uint32_t);
        ecs_os_free(q);
        c[i].captures = NULL;
    }
}

/** Move capture queues to other singletons.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_captures_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RenderCaptures *d = dst;
    RenderCaptures *s = src;
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (d[i].captures != s[i].captures) {
            box_render_captures_dtor(&d[i], 1, ti);
        }

        d[i].captures = s[i].captures;
        s[i].captures = NULL;
    }
}

/** Get the capture queue of a world.
 *
 * @param world The world or a stage
 * @return The queue
 */
static box_render_captures_t* box_render_captures_get(
    const ecs_world_t *world)
{
    const RenderCaptures *c = ecs_singleton_get(world, RenderCaptures);
    ecs_assert(c != NULL, ECS_INVALID_OPERATION, "box.render is not imported");

    return c->captures;
}

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static float box_render_capture_linear(
    uint8_t value)
{
    float c = (float)value / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Find the view of the packet that renders a camera, the first in render order.
 *
 * @param p The packet, whose views are sorted by View.order, then by creation
 * @param camera The camera
 * @return The view, NULL when no view renders the camera
 */
static const box_render_view_t* box_render_capture_view_of(
    const box_render_packet_t *p,
    ecs_entity_t camera)
{
    const box_render_view_t *result = NULL;
    int32_t i, count = ecs_vec_count(&p->views);
    for (i = 0; i < count && !result; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        if (v->view && !v->target && v->camera == camera) {
            result = v;
        }
    }

    return result;
}

/** Fill the view of a capture from its camera entity or its description.
 *
 * A camera that a view renders is captured with the settings of that view: its lighting,
 * shadows and effects, which the later extracts resolve from the view entity, and its culling
 * and debug drawing, copied together so that a WireframeOnly capture also skips occlusion.
 *
 * A camera without a view, or the camera of a description, draws no debug pass and has the
 * features of its scene.
 *
 * @param world The world
 * @param p The packet
 * @param d The capture
 * @param v The zeroed view
 * @return 0 when the view was filled, -1 when the camera entity has no Camera
 */
static int box_render_capture_camera(
    ecs_world_t *world,
    const box_render_packet_t *p,
    const box_render_capture_desc_t *d,
    box_render_view_t *v)
{
    const box_render_view_t *source = NULL;
    int result = -1;
    const Camera *data = NULL;
    WorldTransform buf;
    const WorldTransform *transform = NULL;
    if (!d->camera) {
        v->camera_data = d->camera_data;
        v->scene = d->scene;
        v->cull.enabled = true;
        v->cull.min_dimension = BOX_RENDER_CULL_MIN_DIMENSION;
        v->features = box_render_view_features(world, v->scene, NULL);
        v->scene_features = box_render_scene_features(world, v->scene);
        WorldTransform t;
        box_transform_from_mat4(d->transform, NULL, &t);
        box_transform_mat4(&t, p->origin, v->camera_transform);
        result = 0;
        goto done;
    }

    data = ecs_is_alive(world, d->camera) ? ecs_get(world, d->camera, Camera) : NULL;
    if (!data) {
        goto done;
    }

    source = box_render_capture_view_of(p, d->camera);
    if (source) {
        v->view = source->view;
        v->cull = source->cull;
        v->debug = source->debug;
        v->debug_box_first = source->debug_box_first;
        v->debug_box_count = source->debug_box_count;
        v->debug_pose_first = source->debug_pose_first;
        v->debug_pose_count = source->debug_pose_count;
        v->features = source->features;
        v->scene_features = source->scene_features;
    } else {
        v->cull.enabled = true;
        v->cull.min_dimension = BOX_RENDER_CULL_MIN_DIMENSION;
    }

    transform = box_transform_world(world, d->camera, &buf);
    v->camera = d->camera;
    v->camera_data = *data;
    v->scene = box_render_scene_of(world, d->camera);
    if (!source) {
        v->features = box_render_view_features(world, v->scene, NULL);
        v->scene_features = box_render_scene_features(world, v->scene);
    }

    if (transform) {
        box_transform_mat4(transform, p->origin, v->camera_transform);
    } else {
        glm_mat4_identity(v->camera_transform);
    }

    result = 0;
done:
    return result;
}

/** Add a capture to the packet: its target, the view that renders into it and the PNG path.
 *
 * @param world The world
 * @param q The capture queue
 * @param p The packet
 * @param d The capture
 */
static void box_render_capture_send(
    ecs_world_t *world,
    box_render_captures_t *q,
    box_render_packet_t *p,
    const box_render_capture_desc_t *d)
{
    box_render_view_t view = {0};
    if (box_render_capture_camera(world, p, d, &view)) {
        ecs_err("capture '%s' skipped: its camera is gone", d->path);
        goto done;
    }

    uint32_t handle = BOX_RENDER_CAPTURE_TARGET_FIRST + q->next;
    q->next = (q->next + 1) % BOX_RENDER_CAPTURE_HANDLES;
    box_render_target_desc_t *desc = box_render_packet_resource(
        p, BOX_RENDER_TARGET, BOX_RENDER_CREATE, handle, sizeof(box_render_target_desc_t));
    ecs_os_memset_t(desc, 0, box_render_target_desc_t);
    desc->width = desc->render_width = d->width;
    desc->height = desc->render_height = d->height;

    glm_mat4_inv(view.camera_transform, view.view_matrix);
    view.clear = d->clear;
    view.target = handle;
    box_render_view_t *v = box_render_packet_view(p);
    *v = view;

    box_render_target_t *target = box_render_packet_target(p, handle, d->path);
    target->exclusive = true;
    target->clear[3] = 1.0f;
    if (d->clear.enabled) {
        target->clear[0] = box_render_capture_linear(d->clear.color.r);
        target->clear[1] = box_render_capture_linear(d->clear.color.g);
        target->clear[2] = box_render_capture_linear(d->clear.color.b);
        target->clear[3] = (float)d->clear.color.a / 255.0f;
    }

    ecs_vec_append_t(NULL, &q->sent, uint32_t)[0] = handle;
done:
    return;
}

/** Destroy the targets of the previous captures and add the queued captures to the packet.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractCaptures(
    ecs_iter_t *it)
{
    box_render_captures_t *q = box_render_captures_get(it->world);
    box_render_packet_t *p = NULL;
    int32_t i;
    if (!ecs_vec_count(&q->sent) && !ecs_vec_count(&q->pending)) {
        goto done;
    }

    p = box_render_packet(it->world);
    for (i = 0; i < ecs_vec_count(&q->sent); i ++) {
        uint32_t handle = *ecs_vec_get_t(&q->sent, uint32_t, i);
        box_render_packet_resource(p, BOX_RENDER_TARGET, BOX_RENDER_DESTROY, handle, 0);
    }

    ecs_vec_clear(&q->sent);
    for (i = 0; i < ecs_vec_count(&q->pending); i ++) {
        box_render_capture_desc_t *d = ecs_vec_get_t(
            &q->pending, box_render_capture_desc_t, i);
        box_render_capture_send(it->world, q, p, d);
        ecs_os_free(ECS_CONST_CAST(char*, d->path));
    }

    ecs_vec_clear(&q->pending);
done:
    return;
}

int box_render_capture(
    ecs_world_t *world,
    const box_render_capture_desc_t *desc)
{
    box_render_captures_t *q = box_render_captures_get(world);
    int result = -1;
    if (!desc->path || !desc->path[0]) {
        ecs_err("a capture needs a path");
        goto done;
    }

    if (desc->width <= 0 || desc->height <= 0 ||
        desc->width > BOX_RENDER_CAPTURE_MAX_SIZE || desc->height > BOX_RENDER_CAPTURE_MAX_SIZE)
    {
        ecs_err("invalid capture size %dx%d", desc->width, desc->height);
        goto done;
    }

    box_render_capture_desc_t *d = ecs_vec_append_t(
        NULL, &q->pending, box_render_capture_desc_t);
    *d = *desc;
    d->path = ecs_os_strdup(desc->path);
    box_render_use_gpu(world);
    result = 0;
done:
    return result;
}

int32_t box_render_captures_pending(
    const ecs_world_t *world)
{
    return ecs_vec_count(&box_render_captures_get(world)->pending);
}

void box_render_captures_init(
    ecs_world_t *world)
{
    ecs_set_name_prefix(world, "Render");
    ECS_COMPONENT_DEFINE(world, RenderCaptures);
    ecs_set_name_prefix(world, NULL);
    ecs_set_hooks(world, RenderCaptures, {
        .ctor = flecs_default_ctor,
        .dtor = box_render_captures_dtor,
        .move = box_render_captures_move
    });

    box_render_captures_t *q = ecs_os_calloc_t(box_render_captures_t);
    ecs_vec_init_t(NULL, &q->pending, box_render_capture_desc_t, 0);
    ecs_vec_init_t(NULL, &q->sent, uint32_t, 0);
    ecs_singleton_set(world, RenderCaptures, { .captures = q });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.Captures" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractCaptures
    });
}
