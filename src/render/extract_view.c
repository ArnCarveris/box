/**
 * @file render/extract_view.c
 * @brief Builds the view array of a packet: one entry per view that renders.
 *
 * A world without View entities gets one view from the active camera, so the
 * packet a scene produces is the same as before views existed.
 */

#include "render_internal.h"
#include "../effects/effects_internal.h"
#include "../sky/sky_internal.h"
#include <stdlib.h>

/** Degrees in one radian, the unit the camera field of view is given in */
#define BOX_VIEW_RADIANS (3.14159265f / 180.0f)

/** Field of view a camera without one renders with */
#define BOX_VIEW_FOV (60.0f)

/** Near plane a view without a camera renders with */
#define BOX_VIEW_NEAR (0.1f)

/** Far plane a view without a camera renders with */
#define BOX_VIEW_FAR (1000.0f)

/** Visible height an orthographic camera without one renders with */
#define BOX_VIEW_ORTHO_HEIGHT (10.0f)

const void* box_render_view_setting(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t id)
{
    const void *result = NULL;
    if (e) {
        result = ecs_get_id(world, e, id);
    }

    if (!result) {
        result = ecs_get_id(world, id, id);
    }

    return result;
}

/** Check whether a view draws a UI tree, which it does by listing its layer.
 *
 * @param world The world
 * @param state The registry
 * @param e The View entity, 0 for the view a world without views gets
 * @return True when the view lists the layer of a UI tree
 */
static bool box_view_is_ui(
    ecs_world_t *world,
    const RenderState *state,
    ecs_entity_t e)
{
    bool result = false;
    if (!state->ui_mask || !e) {
        goto done;
    }

    const View *view = ecs_get(world, e, View);
    if (!view || !view->layers[0]) {
        goto done;
    }

    result = (box_render_view_layers(state, view->layers) & state->ui_mask) != 0;
done:
    return result;
}

/** Order two views by priority, then by entity so the order never wobbles.
 *
 * @param a The first view
 * @param b The second view
 * @return Negative when a renders first, positive when b does
 */
static int box_view_compare(
    const void *a,
    const void *b)
{
    const box_view_src_t *x = a;
    const box_view_src_t *y = b;
    int result = (x->priority > y->priority) - (x->priority < y->priority);
    if (!result) {
        result = (x->entity > y->entity) - (x->entity < y->entity);
    }

    return result;
}

/** Take the capture request of the first camera that has one.
 *
 * The path of the request replaces the one the surface wrote and the component
 * is removed, so a next frame honors the request of another camera. The
 * component owns its path, which is released with the request.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 * @return The camera that requested the capture, 0 when no camera did
 */
static ecs_entity_t box_view_capture(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_query_iter(world, state->captures);
    if (ecs_query_next(&it)) {
        Capture *capture = ecs_field(&it, Capture, 0);
        result = it.entities[0];
        ecs_os_snprintf(p->capture_path, BOX_CAPTURE_PATH_MAX, "%s",
            capture->path ? capture->path : "");
        ecs_os_free(capture->path);
        capture->path = NULL;
        ecs_iter_fini(&it);
    }

    if (result) {
        ecs_remove(world, result, Capture);
    }

    return result;
}

/** Collect the enabled views that have a camera, sorted by priority.
 *
 * @param world The world
 * @param state The registry
 */
static void box_view_collect(
    ecs_world_t *world,
    RenderState *state)
{
    ecs_iter_t it = ecs_query_iter(world, state->views);
    while (ecs_query_next(&it)) {
        View *view = ecs_field(&it, View, 0);
        ViewImpl *impl = ecs_field(&it, ViewImpl, 1);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            impl[i].extracted = false;
            if (!view[i].enabled) {
                continue;
            }

            bool has_camera = view[i].camera && ecs_is_alive(world, view[i].camera) &&
                ecs_has(world, view[i].camera, Camera);
            if (!has_camera) {
                if (!impl[i].warned) {
                    ecs_warn("view %s has no camera and is skipped",
                        ecs_get_name(world, it.entities[i]));
                    impl[i].warned = true;
                }

                continue;
            }

            impl[i].warned = false;
            box_view_src_t *src = ecs_vec_append_t(NULL, &state->view_list, box_view_src_t);
            src->entity = it.entities[i];
            src->camera = view[i].camera;
            src->priority = view[i].priority;
        }
    }

    int32_t count = ecs_vec_count(&state->view_list);
    if (count > 1) {
        qsort(ecs_vec_first_t(&state->view_list, box_view_src_t), (size_t)count,
            sizeof(box_view_src_t), box_view_compare);
    }
}

/** Give every render target a handle and send its size when it changed.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 */
static void box_view_targets(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->dead_targets); i ++) {
        *(uint32_t*)box_vec_push(&p->target_destroy) =
            ecs_vec_get_t(&state->dead_targets, uint32_t, i)[0];
    }

    ecs_vec_clear(&state->dead_targets);

    ecs_iter_t it = ecs_query_iter(world, state->targets);
    while (ecs_query_next(&it)) {
        RenderTarget *target = ecs_field(&it, RenderTarget, 0);
        RenderTargetImpl *impl = ecs_field(&it, RenderTargetImpl, 1);
        int32_t t;
        for (t = 0; t < it.count; t ++) {
            ecs_assert(target[t].width > 0 && target[t].height > 0, ECS_INVALID_PARAMETER,
                "render target %s needs a width and a height",
                ecs_get_name(world, it.entities[t]));
            bool resized = impl[t].width != target[t].width ||
                impl[t].height != target[t].height;
            if (impl[t].handle && !resized) {
                continue;
            }

            if (!impl[t].handle) {
                impl[t].handle = ++ state->target_count;
            }

            impl[t].width = target[t].width;
            impl[t].height = target[t].height;
            box_target_cmd_t *cmd = box_vec_push(&p->target_create);
            cmd->handle = impl[t].handle;
            cmd->width = (uint32_t)target[t].width;
            cmd->height = (uint32_t)target[t].height;
        }
    }
}

/** Fill the target, clear color and size of a view from its component.
 *
 * @param world The world
 * @param e The View entity, 0 for the view a world without views gets
 * @param p The packet
 * @param v The view to fill
 */
static void box_view_target(
    ecs_world_t *world,
    ecs_entity_t e,
    const box_packet_t *p,
    box_view_t *v)
{
    v->target = 0;
    v->width = p->width;
    v->height = p->height;
    memcpy(v->clear, p->clear, sizeof(v->clear));
    const View *view = e ? ecs_get(world, e, View) : NULL;
    if (!view) {
        goto done;
    }

    v->clear[0] = box_srgb_to_linear(view->clear.r);
    v->clear[1] = box_srgb_to_linear(view->clear.g);
    v->clear[2] = box_srgb_to_linear(view->clear.b);
    v->clear[3] = view->clear_alpha;
    v->shadows = view->shadows ? 1u : 0u;
    v->effects = view->effects ? 1u : 0u;
    v->priority = view->priority;
    if (!view->target) {
        goto done;
    }

    const RenderTargetImpl *impl = ecs_get(world, view->target, RenderTargetImpl);
    ecs_assert(impl != NULL, ECS_INVALID_PARAMETER, "target of view %s is not a RenderTarget",
        ecs_get_name(world, e));
    v->target = impl->handle;
    v->width = (uint32_t)impl->width;
    v->height = (uint32_t)impl->height;
done:
    return;
}

/** Fill the camera matrices, eye and depth range of a view.
 *
 * @param world The world
 * @param camera The camera entity, 0 for the default camera
 * @param v The view to fill
 */
static void box_view_camera(
    ecs_world_t *world,
    ecs_entity_t camera,
    box_view_t *v)
{
    float aspect = v->height ? (float)v->width / (float)v->height : 1.0f;
    mat4 view = mat4_look_at(vec3_new(0, 3, -8), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(BOX_VIEW_FOV * BOX_VIEW_RADIANS, aspect, BOX_VIEW_NEAR);
    vec3 eye = vec3_new(0, 3, -8);
    float near = BOX_VIEW_NEAR;
    float far = BOX_VIEW_FAR;

    const Camera *c = camera ? ecs_get(world, camera, Camera) : NULL;
    const WorldTransform *transform = camera ? ecs_get(world, camera, WorldTransform) : NULL;
    if (c && transform) {
        view = mat4_inverse(&transform->m);
        eye = mat4_get_translation(&transform->m);
        float fov = c->fov > 0 ? c->fov : BOX_VIEW_FOV;
        near = c->near_ > 0 ? c->near_ : near;
        far = c->far_ > 0 ? c->far_ : far;
        float ratio = c->aspect_ratio > 0 ? c->aspect_ratio : aspect;
        if (c->orthographic) {
            float height = c->ortho_height > 0 ? c->ortho_height : BOX_VIEW_ORTHO_HEIGHT;
            proj = mat4_orthographic(height, ratio, near, far);
        } else {
            proj = mat4_perspective(fov * BOX_VIEW_RADIANS, ratio, near);
        }
    }

    memcpy(v->view, view.v, sizeof(view.v));
    memcpy(v->proj, proj.v, sizeof(proj.v));
    v->eye[0] = eye.x;
    v->eye[1] = eye.y;
    v->eye[2] = eye.z;
    v->cluster.near_ = near;
    v->cluster.far_ = far;
}

/** Fill the sun of a view from the first directional light, or a default one.
 *
 * An atmosphere takes the choice over afterwards: it lights the view with the
 * body that reaches the camera brightest and with the color the air leaves of
 * it, which is why the sun of a view is resolved before its shadows are fit.
 *
 * @param world The world
 * @param state The registry
 * @param v The view to fill
 * @return True when the world holds a directional light
 */
bool box_render_view_sun(
    ecs_world_t *world,
    RenderState *state,
    box_view_t *v)
{
    vec3 dir = vec3_normalize(vec3_new(0.4f, -1.0f, 0.3f));
    float intensity = 3.0f;
    vec3 color = vec3_new(1, 1, 1);
    bool result = false;

    ecs_iter_t it = ecs_query_iter(world, state->suns);
    if (ecs_query_next(&it)) {
        result = true;
        DirectionalLight *light = ecs_field(&it, DirectionalLight, 0);
        WorldTransform *transform = ecs_field(&it, WorldTransform, 1);
        dir = vec3_normalize(mat4_transform_dir(&transform->m, vec3_new(0, 0, 1)));
        intensity = light->intensity;
        const Rgb *rgb = ecs_get(world, it.entities[0], Rgb);
        if (rgb) {
            color = vec3_new(box_srgb_to_linear(rgb->r), box_srgb_to_linear(rgb->g),
                box_srgb_to_linear(rgb->b));
        }

        ecs_iter_fini(&it);
    }

    v->sun_dir[0] = dir.x;
    v->sun_dir[1] = dir.y;
    v->sun_dir[2] = dir.z;
    v->sun_intensity = intensity;
    v->sun_color[0] = color.x;
    v->sun_color[1] = color.y;
    v->sun_color[2] = color.z;
    result = box_sky_celestial(world, v) || result;

    return result;
}

/** Check whether two environment lights hold the same values.
 *
 * @param a The first environment
 * @param b The second environment
 * @return true when every field matches
 */
static bool box_view_env_equal(
    const EnvironmentLight *a,
    const EnvironmentLight *b)
{
    return a->sky_color.r == b->sky_color.r && a->sky_color.g == b->sky_color.g &&
        a->sky_color.b == b->sky_color.b && a->ground_color.r == b->ground_color.r &&
        a->ground_color.g == b->ground_color.g && a->ground_color.b == b->ground_color.b &&
        a->intensity == b->intensity && a->source == b->source;
}

/** Write a display encoded color to a view field as a linear color.
 *
 * @param color The color
 * @param out The four floats to fill, the fourth stays zero
 */
static void box_view_env_color(
    rgb color,
    float *out)
{
    out[0] = box_srgb_to_linear(color.r);
    out[1] = box_srgb_to_linear(color.g);
    out[2] = box_srgb_to_linear(color.b);
    out[3] = 0;
}

/** Number the environment of a view so the renderer knows when to prefilter.
 *
 * A view keeps its generation while its values do not change, and two views
 * with the same values share one generation, so the renderer prefilters the
 * environment once and binds it to both.
 *
 * @param state The registry
 * @param index Index of the view in the packet
 * @param env The environment of the view
 * @return The generation of the environment
 */
static uint32_t box_view_env_generation(
    RenderState *state,
    int32_t index,
    const EnvironmentLight *env)
{
    int32_t have = ecs_vec_count(&state->view_env);
    if (have <= index) {
        ecs_vec_set_count_t(NULL, &state->view_env, box_view_env_t, index + 1);
        memset(ecs_vec_get_t(&state->view_env, box_view_env_t, have), 0,
            (size_t)(index + 1 - have) * sizeof(box_view_env_t));
    }

    box_view_env_t *entry = ecs_vec_get_t(&state->view_env, box_view_env_t, index);
    if (entry->generation && box_view_env_equal(&entry->env, env)) {
        goto done;
    }

    uint32_t generation = 0;
    int32_t i;
    for (i = 0; i < index && !generation; i ++) {
        box_view_env_t *other = ecs_vec_get_t(&state->view_env, box_view_env_t, i);
        if (other->generation && box_view_env_equal(&other->env, env)) {
            generation = other->generation;
        }
    }

    entry->env = *env;
    entry->generation = generation ? generation : ++ state->env_generation;
done:
    return entry->generation;
}

/** Copy the environment of a view into it, the view first and the singleton after.
 *
 * A UI view lights its tree with a flat white environment instead, so a node
 * shows the color it was given whatever the scene around it is lit with.
 *
 * @param world The world
 * @param state The registry
 * @param e The View entity, 0 for the view a world without views gets
 * @param index Index of the view in the packet
 * @param ui The view draws a UI tree
 * @param v The view to fill
 */
static void box_view_environment(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e,
    int32_t index,
    bool ui,
    box_view_t *v)
{
    EnvironmentLight env = {
        .sky_color = { BOX_ENV_SKY, BOX_ENV_SKY, BOX_ENV_SKY },
        .ground_color = { BOX_ENV_GROUND, BOX_ENV_GROUND, BOX_ENV_GROUND },
        .intensity = BOX_ENV_INTENSITY
    };

    const EnvironmentLight *set = ui ?
        NULL : box_render_view_setting(world, e, ecs_id(EnvironmentLight));
    if (ui) {
        env.sky_color = (rgb){ 255, 255, 255 };
        env.ground_color = (rgb){ 255, 255, 255 };
        env.intensity = 1.0f;
    }

    if (set) {
        env = *set;
        env.intensity = env.intensity > 0 ? env.intensity : 1.0f;
    }

    box_view_env_color(env.sky_color, v->env_sky);
    box_view_env_color(env.ground_color, v->env_ground);
    v->env_intensity = env.intensity;
    v->env_generation = box_view_env_generation(state, index, &env);
    v->sky_env = env.source && ecs_is_alive(world, env.source) &&
        ecs_has(world, env.source, Atmosphere) ? 1u : 0u;
}

/** Resolve the layers a view shows and the layers that cast its shadows.
 *
 * A world that uses no layer skips the lists, so every view shows everything.
 * A strict view only restricts what it draws while it lists layers of its own.
 *
 * A view that does not draw a UI tree hides the layers of every tree, which is
 * what keeps the trees of a world out of the views of its scene.
 *
 * @param world The world
 * @param state The registry
 * @param e The View entity, 0 for the view a world without views gets
 * @param ui The view draws a UI tree
 * @param v The view to fill
 */
static void box_view_layers(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e,
    bool ui,
    box_view_t *v)
{
    v->layer_mask = BOX_VIEW_LAYER_ALL;
    v->caster_mask = BOX_VIEW_LAYER_ALL;
    v->strict_layers = 0;
    const View *view = state->layer_used && e ? ecs_get(world, e, View) : NULL;
    if (!view) {
        goto done;
    }

    v->layer_mask = box_render_view_layers(state, view->layers);
    v->caster_mask = box_render_view_layers(state, view->shadow_layers);
    v->strict_layers = view->strict_layers && v->layer_mask != BOX_VIEW_LAYER_ALL ? 1u : 0u;
done:
    if (!ui) {
        v->layer_mask &= ~state->ui_mask;
        v->caster_mask &= ~state->ui_mask;
    }

    return;
}

/** Copy the culling settings of a view into it, the view first and the singleton after.
 *
 * @param world The world
 * @param e The View entity, 0 for the view a world without views gets
 * @param v The view to fill
 */
static void box_view_cull(
    ecs_world_t *world,
    ecs_entity_t e,
    box_view_t *v)
{
    const CullSettings *s = box_render_view_setting(world, e, ecs_id(CullSettings));
    v->cull_enabled = !s || s->enabled ? 1u : 0u;
    v->cull_threshold = s && s->gpu_threshold > 0 ?
        (uint32_t)s->gpu_threshold : BOX_CULL_GPU_THRESHOLD;
    v->occlusion = v->cull_enabled && (!s || s->occlusion) ? 1u : 0u;
    v->hiz_levels = s && s->hiz_levels > 0 ? (uint32_t)s->hiz_levels : 0u;
}

/** Copy the LOD settings of a view into it, the view first and the singleton after.
 *
 * Quality scales both thresholds, so a view of quality two allows half the
 * error its settings name, and the shadow threshold stays the more aggressive
 * of the two because a coarser caster hardly shows in a shadow.
 *
 * @param world The world
 * @param e The View entity, 0 for the view a world without views gets
 * @param v The view to fill
 */
static void box_view_lod(
    ecs_world_t *world,
    ecs_entity_t e,
    box_view_t *v)
{
    const LodSettings *s = box_render_view_setting(world, e, ecs_id(LodSettings));
    float quality = s && s->quality > 0.0f ? s->quality : BOX_VIEW_LOD_QUALITY;
    float error = s && s->error_px > 0.0f ? s->error_px : BOX_VIEW_LOD_ERROR;
    float shadow = s && s->shadow_error_px > 0.0f ?
        s->shadow_error_px : BOX_VIEW_LOD_SHADOW_ERROR;
    v->lod_enabled = !s || s->enabled ? 1u : 0u;
    v->lod_error_px = error / quality;
    v->lod_shadow_error_px = shadow / quality;
    v->lod_hysteresis = s && s->hysteresis > 0.0f ? s->hysteresis : BOX_VIEW_LOD_HYSTERESIS;
}

/** Copy the frame rate overlay of a view into it, the view first and the singleton after.
 *
 * The component belongs to box.monitor, which a world that never imports the
 * module leaves unregistered; the view then carries no panel at all.
 *
 * @param world The world
 * @param e The View entity, 0 for the view a world without views gets
 * @param v The view to fill
 */
static void box_view_overlay(
    ecs_world_t *world,
    ecs_entity_t e,
    box_view_t *v)
{
    if (!ecs_id(FpsOverlay)) {
        goto done;
    }

    const FpsOverlay *o = box_render_view_setting(world, e, ecs_id(FpsOverlay));
    if (!o || !o->enabled) {
        goto done;
    }

    v->overlay = 1u;
    v->overlay_corner = (uint32_t)o->corner;
    v->overlay_scale = o->scale > 0.0f ? o->scale : 1.0f;
    v->overlay_opacity = o->opacity;
done:
    return;
}

/** Fill the clip to world and the reprojection matrices a chain of a view reads.
 *
 * A pass reconstructs a world position from the depth with the inverse and the
 * motion pass reprojects it with the world to clip of the previous frame, which
 * is kept per view index so a view that appears for the first time reports no
 * motion instead of a jump. A view without a chain pays for neither.
 *
 * @param state The registry
 * @param index Index of the view in the packet
 * @param v The view to fill
 */
static void box_view_motion(
    RenderState *state,
    int32_t index,
    box_view_t *v)
{
    mat4 view;
    mat4 proj;
    memcpy(view.v, v->view, sizeof(view.v));
    memcpy(proj.v, v->proj, sizeof(proj.v));
    mat4 clip = mat4_mul(&proj, &view);
    mat4 inverse = mat4_inverse(&clip);
    memcpy(v->clip, clip.v, sizeof(clip.v));
    memcpy(v->inv_clip, inverse.v, sizeof(inverse.v));

    while (ecs_vec_count(&state->view_clip) <= index) {
        mat4 *created = ecs_vec_append_t(NULL, &state->view_clip, mat4);
        *created = clip;
    }

    mat4 *stored = ecs_vec_get_t(&state->view_clip, mat4, index);
    memcpy(v->prev_clip, stored->v, sizeof(stored->v));
    *stored = clip;
}

/** Fill one view of the packet from its view entity and its camera.
 *
 * @param world The world
 * @param state The registry
 * @param src The view of the frame
 * @param index Index of the view in the packet
 * @param p The packet
 */
static void box_view_fill(
    ecs_world_t *world,
    RenderState *state,
    const box_view_src_t *src,
    int32_t index,
    box_packet_t *p)
{
    box_view_t *v = box_vec_push(&p->views);
    bool ui = box_view_is_ui(world, state, src->entity);
    memset(v, 0, sizeof(box_view_t));
    v->shadows = 1;
    v->effects = 1;
    box_view_layers(world, state, src->entity, ui, v);
    box_view_target(world, src->entity, p, v);
    box_view_camera(world, src->camera, v);
    box_view_cull(world, src->entity, v);
    box_view_lod(world, src->entity, v);
    if (!ui) {
        box_view_overlay(world, src->entity, v);
    }

    bool sun = box_render_view_sun(world, state, v) && !ui;
    if (ui) {
        v->sun_intensity = 0;
        v->plain_blend = 1;
    }

    box_render_extract_shadows(world, src->entity, sun, v);
    box_view_environment(world, state, src->entity, index, ui, v);
    box_render_extract_lights(world, state, p, src->entity, v, index == 0);
    if (ui) {
        v->light_count = 0;
    }

    box_render_extract_transparent(world, state, p, v);
    v->sky = !ui && box_sky_entity(world) ? 1u : 0u;
    box_effects_extract(world, src->entity, p, v);
    if (v->effect_count || v->sky) {
        box_view_motion(state, index, v);
    }

    v->dynamic_first = 0;
    if (state->layer_used) {
        v->dynamic_first = p->dynamic_batches.count;
        box_render_extract_dynamic(world, state, p, v->layer_mask, v->caster_mask,
            v->strict_layers != 0);
    }

    v->dynamic_count = p->dynamic_batches.count - v->dynamic_first;

    const MaterialLayerSettings *settings =
        box_render_view_setting(world, src->entity, ecs_id(MaterialLayerSettings));
    const View *view = src->entity ? ecs_get(world, src->entity, View) : NULL;
    bool enabled = !settings || settings->enabled;
    v->layers_enabled = enabled && (!view || view->material_layers) ? 1u : 0u;

    if (src->entity) {
        ViewImpl *impl = ecs_get_mut(world, src->entity, ViewImpl);
        ecs_assert(impl != NULL, ECS_INTERNAL_ERROR, NULL);
        impl->index = (uint32_t)index;
        impl->extracted = true;
    }
}

void box_render_view_probe(
    ecs_world_t *world,
    const box_view_src_t *src,
    const box_packet_t *p,
    box_view_t *out)
{
    memset(out, 0, sizeof(box_view_t));
    box_view_target(world, src->entity, p, out);
    box_view_camera(world, src->camera, out);
    box_view_lod(world, src->entity, out);
}

ecs_entity_t box_render_collect_views(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    ecs_entity_t result = box_view_capture(world, state, p);
    ecs_vec_clear(&state->view_list);
    if (state->view_count) {
        box_view_collect(world, state);
    }

    if (state->target_live || ecs_vec_count(&state->dead_targets)) {
        box_view_targets(world, state, p);
    }

    return result;
}

ecs_entity_t box_render_primary_view(
    RenderState *state)
{
    ecs_entity_t result = 0;
    if (ecs_vec_count(&state->view_list)) {
        result = ecs_vec_get_t(&state->view_list, box_view_src_t, 0)->entity;
    }

    return result;
}

/** Check whether a view of the frame draws something other than a UI tree.
 *
 * @param world The world
 * @param state The registry
 * @return True when a collected view draws the scene
 */
static bool box_view_has_scene(
    ecs_world_t *world,
    RenderState *state)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->view_list) && !result; i ++) {
        const box_view_src_t *src = ecs_vec_get_t(&state->view_list, box_view_src_t, i);

        result = !box_view_is_ui(world, state, src->entity);
    }

    return result;
}

/** Camera the synthesized view of the frame renders, never one of a UI view.
 *
 * @param world The world
 * @param state The registry
 * @return The camera, 0 when the world holds none
 */
static ecs_entity_t box_view_scene_camera(
    ecs_world_t *world,
    RenderState *state)
{
    ecs_entity_t result = 0;
    if (!state->ui_mask) {
        result = box_render_active_camera(world);
        goto done;
    }

    ecs_iter_t it = ecs_query_iter(world, state->cameras);
    while (ecs_query_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            bool used = false;
            int32_t v;
            for (v = 0; v < ecs_vec_count(&state->view_list) && !used; v ++) {
                const box_view_src_t *src =
                    ecs_vec_get_t(&state->view_list, box_view_src_t, v);

                used = src->camera == it.entities[i] &&
                    box_view_is_ui(world, state, src->entity);
            }

            if (!used && !result) {
                result = it.entities[i];
            }
        }
    }

done:
    return result;
}

void box_render_extract_views(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p,
    ecs_entity_t capture)
{
    box_vec_clear(&p->views);
    p->capture_view = BOX_VIEW_NONE;
    int32_t count = ecs_vec_count(&state->view_list);
    int32_t index = 0;
    if (!box_view_has_scene(world, state)) {
        box_view_src_t src = {0};
        src.camera = capture ? capture : box_view_scene_camera(world, state);
        box_view_fill(world, state, &src, index ++, p);
    }

    int32_t i;
    for (i = 0; i < count; i ++) {
        const box_view_src_t *src = ecs_vec_get_t(&state->view_list, box_view_src_t, i);
        if (capture && src->camera == capture) {
            p->capture_view = (uint32_t)index;
        }

        box_view_fill(world, state, src, index ++, p);
    }

    p->has_view = p->views.count != 0;
    if (p->has_view) {
        p->view = *(box_view_t*)box_vec_at(&p->views, 0);
    }
}

/** ViewImpl observer: count the views so an empty world skips the view query.
 *
 * @param it Iterator over the added or removed components
 */
static void BoxRenderViewCount(
    ecs_iter_t *it)
{
    RenderState *state = box_render_state(it->world);
    state->view_count += it->event == EcsOnAdd ? it->count : -it->count;
}

/** RenderTargetImpl observer: count the targets and queue the destroy of a handle.
 *
 * @param it Iterator over the added or removed components
 */
static void BoxRenderTargetCount(
    ecs_iter_t *it)
{
    RenderState *state = box_render_state(it->world);
    if (it->event == EcsOnAdd) {
        state->target_live += it->count;
        goto done;
    }

    state->target_live -= it->count;
    RenderTargetImpl *impl = ecs_field(it, RenderTargetImpl, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (impl[i].handle) {
            ecs_vec_append_t(NULL, &state->dead_targets, uint32_t)[0] = impl[i].handle;
        }
    }

done:
    return;
}

void box_render_view_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(ViewImpl) }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxRenderViewCount
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(RenderTargetImpl) }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxRenderTargetCount
    });
}
