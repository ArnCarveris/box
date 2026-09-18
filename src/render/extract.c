/**
 * @file render/extract.c
 * @brief Extract system: moves meshes, instances, camera and light into the frame packet.
 */

#include "render_internal.h"
#include "../surface/surface_internal.h"

/** Check whether an entity or an ancestor carries a dynamic tag.
 *
 * @param world The world
 * @param e The entity
 * @return true when the entity is extracted every frame
 */
static bool box_render_is_dynamic(
    ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    ecs_entity_t cur = e;
    while (cur && !result) {
        result = ecs_has_id(world, cur, DynamicTransform) ||
            ecs_has_id(world, cur, DynamicMaterial);
        cur = ecs_get_target(world, cur, EcsChildOf, 0);
    }

    return result;
}

/** Write the world space bounds of an instance to its AABB component.
 *
 * @param world The world
 * @param e The entity
 * @param mesh Its render mesh
 * @param transform Its world transform
 */
static void box_render_update_aabb(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    const WorldTransform *transform)
{
    AABB *aabb = ecs_get_mut(world, e, AABB);
    if (!aabb) {
        aabb = ecs_ensure(world, e, AABB);
    }

    vec3 lo = vec3_mul(mesh->bounds_min, mesh->scale);
    vec3 hi = vec3_mul(mesh->bounds_max, mesh->scale);
    int corner;
    for (corner = 0; corner < 8; corner ++) {
        vec3 c = { corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z };
        vec3 w = mat4_transform_point(&transform->m, c);
        aabb->min = corner ? vec3_min(aabb->min, w) : w;
        aabb->max = corner ? vec3_max(aabb->max, w) : w;
    }
}

/** Move pending mesh uploads into the packet.
 *
 * @param state The registry
 * @param p The packet
 */
static void box_extract_meshes(
    RenderState *state,
    box_packet_t *p)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->pending_meshes); i ++) {
        box_mesh_cmd_t *cmd = box_vec_push(&p->mesh_create);
        *cmd = *ecs_vec_get_t(&state->pending_meshes, box_mesh_cmd_t, i);
    }

    ecs_vec_clear(&state->pending_meshes);
}

/** Extract one dirty static entity: assign a slot and emit its instance data.
 *
 * @param world The world
 * @param state The registry
 * @param e The entity
 * @param p The packet
 */
static void box_extract_static(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e,
    box_packet_t *p)
{
    const RenderMesh *mesh = ecs_get(world, e, RenderMesh);
    const WorldTransform *transform = ecs_get(world, e, WorldTransform);
    RenderInstance *inst = ecs_get_mut(world, e, RenderInstance);
    ecs_assert(inst != NULL, ECS_INTERNAL_ERROR, NULL);
    inst->dirty = false;
    bool dynamic = box_render_is_dynamic(world, e);
    bool has_slot = box_render_has_slot(state, e, inst);

    if (has_slot && (dynamic || inst->mesh != mesh->handle)) {
        box_render_release_slot(world, state, e, inst);
        has_slot = false;
    }

    inst->dynamic = dynamic;
    inst->mesh = mesh->handle;
    if (dynamic) {
        goto done;
    }

    box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, (int32_t)mesh->handle);
    if (!has_slot) {
        inst->slot = (uint32_t)ecs_vec_count(&batch->entities);
        ecs_vec_append_t(NULL, &batch->entities, ecs_entity_t)[0] = e;
        batch->count_dirty = true;
    }

    box_static_update_t *update = box_vec_push(&p->static_updates);
    update->mesh = mesh->handle;
    update->slot = inst->slot;
    box_render_fill_instance(world, e, mesh, transform, &update->data);
    box_render_update_aabb(world, e, mesh, transform);
done:
    return;
}

/** Emit the new static counts of batches that changed.
 *
 * @param state The registry
 * @param p The packet
 */
static void box_extract_counts(
    RenderState *state,
    box_packet_t *p)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->batches); i ++) {
        box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, i);
        if (batch->count_dirty) {
            box_static_count_t *count = box_vec_push(&p->static_counts);
            count->mesh = (uint32_t)i;
            count->count = (uint32_t)ecs_vec_count(&batch->entities);
            batch->count_dirty = false;
        }
    }
}

/** Extract every dynamic entity, grouped by mesh.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 */
static void box_extract_dynamic(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    ecs_iter_t it = ecs_query_iter(world, state->dynamic);
    while (ecs_query_next(&it)) {
        RenderMesh *mesh = ecs_field(&it, RenderMesh, 0);
        WorldTransform *transform = ecs_field(&it, WorldTransform, 1);
        RenderInstance *inst = ecs_field(&it, RenderInstance, 2);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!inst[i].dynamic) {
                box_render_mark_dirty(world, it.entities[i]);
                continue;
            }

            box_dynamic_batch_t *batch = p->dynamic_batches.count ?
                box_vec_at(&p->dynamic_batches, p->dynamic_batches.count - 1) : NULL;
            if (!batch || batch->mesh != mesh[i].handle) {
                batch = box_vec_push(&p->dynamic_batches);
                batch->mesh = mesh[i].handle;
                batch->first = p->dynamic_instances.count;
                batch->count = 0;
            }

            box_instance_t *data = box_vec_push(&p->dynamic_instances);
            box_render_fill_instance(world, it.entities[i], &mesh[i], &transform[i], data);
            batch->count ++;
        }
    }
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
static ecs_entity_t box_extract_capture(
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

/** Fill the packet view from the capture or active camera, or a default one.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 */
static void box_extract_camera(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    float aspect = p->height ? (float)p->width / (float)p->height : 1.0f;
    mat4 view = mat4_look_at(vec3_new(0, 3, -8), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(60.0f * 3.14159265f / 180.0f, aspect, 0.1f);
    vec3 eye = vec3_new(0, 3, -8);
    float near = 0.1f;
    float far = 1000.0f;

    ecs_entity_t e = box_extract_capture(world, state, p);
    if (!e) {
        e = box_render_active_camera(world);
    }

    const Camera *camera = e ? ecs_get(world, e, Camera) : NULL;
    const WorldTransform *transform = e ? ecs_get(world, e, WorldTransform) : NULL;
    if (camera && transform) {
        view = mat4_inverse(&transform->m);
        eye = mat4_get_translation(&transform->m);
        float fov = camera->fov > 0 ? camera->fov : 60.0f;
        near = camera->near_ > 0 ? camera->near_ : near;
        far = camera->far_ > 0 ? camera->far_ : far;
        float ratio = camera->aspect_ratio > 0 ? camera->aspect_ratio : aspect;
        if (camera->orthographic) {
            float height = camera->ortho_height > 0 ? camera->ortho_height : 10.0f;
            proj = mat4_orthographic(height, ratio, near, far);
        } else {
            proj = mat4_perspective(fov * 3.14159265f / 180.0f, ratio, near);
        }
    }

    memcpy(p->view.view, view.v, sizeof(view.v));
    memcpy(p->view.proj, proj.v, sizeof(proj.v));
    p->view.eye[0] = eye.x;
    p->view.eye[1] = eye.y;
    p->view.eye[2] = eye.z;
    p->view.cluster.near_ = near;
    p->view.cluster.far_ = far;
    p->has_view = true;
}

/** Fill the packet sun light from the first directional light, or a default one.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 */
static void box_extract_sun(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    vec3 dir = vec3_normalize(vec3_new(0.4f, -1.0f, 0.3f));
    float intensity = 3.0f;
    vec3 color = vec3_new(1, 1, 1);

    ecs_iter_t it = ecs_query_iter(world, state->suns);
    if (ecs_query_next(&it)) {
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

    p->view.sun_dir[0] = dir.x;
    p->view.sun_dir[1] = dir.y;
    p->view.sun_dir[2] = dir.z;
    p->view.sun_intensity = intensity;
    p->view.sun_color[0] = color.x;
    p->view.sun_color[1] = color.y;
    p->view.sun_color[2] = color.z;
}

/** Check whether two environment lights hold the same values.
 *
 * @param a The first environment
 * @param b The second environment
 * @return true when every field matches
 */
static bool box_extract_env_equal(
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
static void box_extract_env_color(
    rgb color,
    float *out)
{
    out[0] = box_srgb_to_linear(color.r);
    out[1] = box_srgb_to_linear(color.g);
    out[2] = box_srgb_to_linear(color.b);
    out[3] = 0;
}

/** Copy the environment light singleton into the view, or the default gradient.
 *
 * The generation counts the environments the world has had, so it only changes
 * when the extracted values differ from those of the previous packet and the
 * renderer knows when to prefilter again.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 */
static void box_extract_environment(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    EnvironmentLight env = {
        .sky_color = { BOX_ENV_SKY, BOX_ENV_SKY, BOX_ENV_SKY },
        .ground_color = { BOX_ENV_GROUND, BOX_ENV_GROUND, BOX_ENV_GROUND },
        .intensity = BOX_ENV_INTENSITY
    };

    const EnvironmentLight *set = ecs_singleton_get(world, EnvironmentLight);
    if (set) {
        env = *set;
        env.intensity = env.intensity > 0 ? env.intensity : 1.0f;
    }

    if (!box_extract_env_equal(&env, &state->env)) {
        state->env = env;
        state->env_generation ++;
    }

    box_extract_env_color(env.sky_color, p->view.env_sky);
    box_extract_env_color(env.ground_color, p->view.env_ground);
    p->view.env_intensity = env.intensity;
    p->view.env_generation = state->env_generation;
}

ecs_entity_t box_render_active_camera(
    ecs_world_t *world)
{
    ecs_entity_t result = 0;
    ecs_entity_t singleton = ecs_lookup(world, "box.render.RenderState");
    if (!singleton) {
        goto done;
    }

    const RenderState *state = ecs_get_id(world, singleton, singleton);
    if (!state || !state->cameras) {
        goto done;
    }

    ecs_iter_t it = ecs_query_iter(world, state->cameras);
    if (ecs_query_next(&it)) {
        result = it.entities[0];
        ecs_iter_fini(&it);
    }

done:
    return result;
}

void box_render_extract(
    ecs_world_t *world,
    box_packet_t *p)
{
    RenderState *state = box_render_state(world);
    box_extract_meshes(state, p);

    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->dirty); i ++) {
        ecs_entity_t e = ecs_vec_get_t(&state->dirty, ecs_entity_t, i)[0];
        if (ecs_is_alive(world, e) && ecs_has(world, e, RenderMesh)) {
            box_extract_static(world, state, e, p);
        }
    }

    ecs_vec_clear(&state->dirty);
    box_extract_dynamic(world, state, p);
    box_extract_counts(state, p);
    box_extract_camera(world, state, p);
    box_extract_sun(world, state, p);
    box_extract_environment(world, state, p);
    box_render_extract_lights(world, state, p);
    box_render_validate(world);
}

void box_render_validate(
    ecs_world_t *world)
{
#ifndef NDEBUG
    RenderState *state = box_render_state(world);
    ecs_iter_t it = ecs_query_iter(world, state->instances);
    while (ecs_query_next(&it)) {
        RenderInstance *inst = ecs_field(&it, RenderInstance, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            if (inst[i].dirty) {
                ecs_abort(ECS_INTERNAL_ERROR, "entity %s still dirty after extract",
                    ecs_get_name(world, e));
            }

            if (inst[i].dynamic) {
                continue;
            }

            box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, (int32_t)inst[i].mesh);
            ecs_entity_t *slots = ecs_vec_first_t(&batch->entities, ecs_entity_t);
            if (inst[i].slot >= (uint32_t)ecs_vec_count(&batch->entities) ||
                slots[inst[i].slot] != e)
            {
                ecs_abort(ECS_INTERNAL_ERROR, "entity %s has slot %u of mesh %u but slot holds %s",
                    ecs_get_name(world, e), inst[i].slot, inst[i].mesh,
                    ecs_get_name(world, slots[inst[i].slot]));
            }
        }
    }
#else
    (void)world;
#endif
}

/** Extract system body: fill the packet of the frame's surface.
 *
 * The stage is used as the world, so that the removal of a consumed Capture is
 * queued instead of applied to the readonly world of a system.
 *
 * @param it System iterator
 */
static void BoxRenderExtract(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->stage;
    RenderState *state = box_render_state(world);
    box_packet_t *p = NULL;

    ecs_iter_t sit = ecs_query_iter(world, state->surfaces);
    while (ecs_query_next(&sit)) {
        SurfaceImpl *impl = ecs_field(&sit, SurfaceImpl, 0);
        if (!p && impl[0].packet) {
            p = impl[0].packet;
        }
    }

    if (p) {
        box_render_extract(world, p);
    }
}

void box_render_extract_init(
    ecs_world_t *world)
{
    RenderState *state = ecs_singleton_ensure(world, RenderState);
    state->dynamic = ecs_query(world, {
        .terms = {
            { .id = ecs_id(RenderMesh), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            { .id = ecs_id(RenderInstance) },
            { .id = DynamicTransform, .src.id = EcsSelf | EcsUp, .oper = EcsOr },
            { .id = DynamicMaterial, .src.id = EcsSelf | EcsUp }
        },
        .cache_kind = EcsQueryCacheAuto
    });
    state->cameras = ecs_query(world, {
        .terms = {
            { .id = ecs_id(Camera), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn }
        }
    });
    state->captures = ecs_query(world, {
        .terms = {
            { .id = ecs_id(Capture), .inout = EcsInOut },
            { .id = ecs_id(Camera), .inout = EcsInOutNone }
        }
    });
    state->suns = ecs_query(world, {
        .terms = {
            { .id = ecs_id(DirectionalLight), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn }
        }
    });
    state->lights = ecs_query(world, {
        .terms = {
            { .id = ecs_id(PointLight), .inout = EcsIn, .oper = EcsOr },
            { .id = ecs_id(SpotLight), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            { .id = ecs_id(Rgb), .inout = EcsIn, .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });
    state->surfaces = ecs_query(world, {
        .terms = {{ .id = ecs_id(SurfaceImpl) }}
    });
    state->instances = ecs_query(world, {
        .terms = {
            { .id = ecs_id(RenderInstance), .inout = EcsIn },
            { .id = ecs_id(RenderMesh), .inout = EcsInOutNone }
        }
    });

    ECS_SYSTEM(world, BoxRenderExtract, EcsPreStore, 0);
}
