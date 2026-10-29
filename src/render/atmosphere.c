/**
 * @file render/atmosphere.c
 * @brief Keeps the transmittance and multi scattering LUTs of every scene with an Atmosphere and
 * gives the views of those scenes the inputs of their sky view and aerial perspective.
 *
 * The LUTs are a BOX_RENDER_ATMOSPHERE resource per scene, built again only when the medium they
 * were built from changes; the sun, the colors and the cameras only reach the per view passes.
 *
 * The entity of the LUTs is also an environment source: its sky goes into the cubemap of its
 * Environment as a BOX_RENDER_ATMOSPHERE_ENVIRONMENT, throttled to the changes that show.
 */

#include "environment.h"
#include "../atmosphere/atmosphere.h"
#include <math.h>

/** First handle of the LUTs of an atmosphere */
#define BOX_ATMOS_HANDLE_FIRST (1)

/** The LUTs of the atmosphere of a scene */
typedef struct box_atmos_scene_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    ecs_entity_t entity;    /**< Entity with the Atmosphere the LUTs are built from */
    uint32_t handle;        /**< Handle of the BOX_RENDER_ATMOSPHERE */
    bool sent;              /**< The backend has the LUTs */
    bool seen;              /**< The scene has a valid Atmosphere in this frame */
    int32_t version;        /**< lut_version of the last build */
    box_render_atmosphere_desc_t key; /**< Medium of the last build */
    Atmosphere value;       /**< Values of the Atmosphere in this frame */
    bool env_sent;          /**< The sky was rendered into the environment of env_entity */
    ecs_entity_t env_entity; /**< Entity whose environment the sky was last rendered into */
    uint32_t env_handle;    /**< Handle of the cubemap it was rendered into */
    box_render_atmosphere_desc_t env_key; /**< Medium of that render */
    box_render_atmosphere_env_t env; /**< Inputs of that render */
} box_atmos_scene_t;

/** Main thread state of the atmosphere extract */
typedef struct box_render_atmos_t {
    ecs_query_t *atmospheres;   /**< Atmosphere and Scene(self|up), NULL without box.atmosphere */
    ecs_vec_t scenes;           /**< box_atmos_scene_t */
    ecs_vec_t free_handles;     /**< uint32_t, handles to reuse */
    uint32_t next_handle;       /**< Next handle */
    const void *backend;        /**< Context of the backend the LUTs were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;         /**< Generation of that backend, see box_render_backend_t */
} box_render_atmos_t;

/** Decode a display encoded channel to linear.
 *
 * @param c The channel
 * @return The linear value, 0 to 1
 */
static float box_atmos_extract_linear(
    uint8_t c)
{
    float v = (float)c / 255.0f;

    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

/** Get the state of the atmosphere extract of a world.
 *
 * @param world The world
 * @return The state, NULL without the extract
 */
static box_render_atmos_t* box_atmos_state(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractAtmosphere");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;

    return s ? s->ctx : NULL;
}

/** Find the LUTs of a scene.
 *
 * @param a The state
 * @param scene The scene root
 * @return The LUTs, NULL when the scene has none
 */
static box_atmos_scene_t* box_atmos_find(
    const box_render_atmos_t *a,
    ecs_entity_t scene)
{
    box_atmos_scene_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&a->scenes); i ++) {
        box_atmos_scene_t *s = ecs_vec_get_t(&a->scenes, box_atmos_scene_t, i);
        if (s->scene == scene) {
            result = s;
            break;
        }
    }

    return result;
}

/** Write the medium of a valid atmosphere as the LUT passes read it.
 *
 * @param value The atmosphere
 * @param out Receives the medium, every padding member zero
 */
static void box_atmos_desc(
    const Atmosphere *value,
    box_render_atmosphere_desc_t *out)
{
    box_atmos_medium_t m;
    int32_t c;
    box_atmos_medium(value, &m);
    ecs_os_memset_t(out, 0, box_render_atmosphere_desc_t);
    out->radii[0] = m.bottom;
    out->radii[1] = m.top;
    out->radii[2] = m.rayleigh_height;
    out->radii[3] = m.mie_height;
    out->ground_albedo[0] = box_atmos_extract_linear(value->ground_albedo.r);
    out->ground_albedo[1] = box_atmos_extract_linear(value->ground_albedo.g);
    out->ground_albedo[2] = box_atmos_extract_linear(value->ground_albedo.b);
    for (c = 0; c < 3; c ++) {
        out->rayleigh[c] = m.rayleigh_scattering[c];
        out->mie_scattering[c] = m.mie_scattering[c];
        out->mie_extinction[c] = m.mie_extinction[c];
        out->ozone[c] = m.ozone_absorption[c];
    }
}

/** Build the LUTs of a scene: create them on the backend or build them again, and count the
 * build in the lut_version.
 *
 * @param world The world
 * @param p The packet
 * @param s The LUTs
 * @param key The medium to build them from
 */
static void box_atmos_build(
    ecs_world_t *world,
    box_render_packet_t *p,
    box_atmos_scene_t *s,
    const box_render_atmosphere_desc_t *key)
{
    const AtmosphereStatus *status = ecs_get(world, s->entity, AtmosphereStatus);
    box_render_atmosphere_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_ATMOSPHERE,
        s->sent ? BOX_RENDER_UPDATE : BOX_RENDER_CREATE, s->handle,
        sizeof(box_render_atmosphere_desc_t));
    if (desc) {
        *desc = *key;
    }

    s->key = *key;
    s->sent = true;
    s->version = (status ? status->lut_version : s->version) + 1;
}

/** Destroy the LUTs of a scene on the backend and remove them.
 *
 * @param a The state
 * @param p The packet, receives the destroy
 * @param index Index of the LUTs
 */
static void box_atmos_remove(
    box_render_atmos_t *a,
    box_render_packet_t *p,
    int32_t index)
{
    box_atmos_scene_t *s = ecs_vec_get_t(&a->scenes, box_atmos_scene_t, index);
    if (s->sent) {
        box_render_packet_resource(p, BOX_RENDER_ATMOSPHERE, BOX_RENDER_DESTROY, s->handle, 0);
    }

    *ecs_vec_append_t(NULL, &a->free_handles, uint32_t) = s->handle;
    ecs_vec_remove_t(&a->scenes, box_atmos_scene_t, index);
}

/** Add the LUTs of a scene that got a valid atmosphere.
 *
 * @param a The state
 * @param scene The scene root
 * @return The LUTs, not sent
 */
static box_atmos_scene_t* box_atmos_add(
    box_render_atmos_t *a,
    ecs_entity_t scene)
{
    box_atmos_scene_t *result = ecs_vec_append_t(NULL, &a->scenes, box_atmos_scene_t);
    ecs_os_memset_t(result, 0, box_atmos_scene_t);
    result->scene = scene;
    if (ecs_vec_count(&a->free_handles)) {
        result->handle = *ecs_vec_last_t(&a->free_handles, uint32_t);
        ecs_vec_remove_last(&a->free_handles);
    } else {
        result->handle = a->next_handle ++;
    }

    return result;
}

/** Visit the valid atmosphere of every scene, the first one of a scene that has several as the
 * light extract picks it, and build its LUTs when the backend lacks them or its medium changed.
 *
 * @param world The world
 * @param a The state
 * @param p The packet
 */
static void box_atmos_collect(
    ecs_world_t *world,
    box_render_atmos_t *a,
    box_render_packet_t *p)
{
    ecs_iter_t it = ecs_query_iter(world, a->atmospheres);
    while (ecs_query_next(&it)) {
        const Atmosphere *values = ecs_field(&it, Atmosphere, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t scene = box_render_scene_field(&it, 1, i);
            box_atmos_scene_t *s = box_atmos_find(a, scene);
            box_render_atmosphere_desc_t key;
            if ((s && s->seen) || box_atmos_error(&values[i])) {
                continue;
            }

            if (!s) {
                s = box_atmos_add(a, scene);
            }

            box_atmos_desc(&values[i], &key);
            s->seen = true;
            s->value = values[i];
            if (!s->sent || s->entity != it.entities[i] ||
                ecs_os_memcmp_t(&key, &s->key, box_render_atmosphere_desc_t))
            {
                s->entity = it.entities[i];
                box_atmos_build(world, p, s, &key);
            }
        }
    }
}

/** Keep the LUTs of the scenes with a valid Atmosphere and destroy those of the other scenes.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractAtmosphere(
    ecs_iter_t *it)
{
    box_render_atmos_t *a = it->ctx;
    ecs_world_t *world = it->world;
    const box_render_backend_t *backend = box_render_get_backend(world);
    box_render_packet_t *p = NULL;
    int32_t i;
    if (!a->atmospheres && !ecs_vec_count(&a->scenes)) {
        goto done;
    }

    p = box_render_packet(world);
    if (backend->ctx != a->backend || backend->render != a->render ||
        backend->generation != a->generation)
    {
        for (i = 0; i < ecs_vec_count(&a->scenes); i ++) {
            ecs_vec_get_t(&a->scenes, box_atmos_scene_t, i)->sent = false;
        }

        a->backend = backend->ctx;
        a->render = backend->render;
        a->generation = backend->generation;
    }

    for (i = 0; i < ecs_vec_count(&a->scenes); i ++) {
        ecs_vec_get_t(&a->scenes, box_atmos_scene_t, i)->seen = false;
    }

    if (a->atmospheres) {
        box_atmos_collect(world, a, p);
    }

    for (i = ecs_vec_count(&a->scenes) - 1; i >= 0; i --) {
        if (!ecs_vec_get_t(&a->scenes, box_atmos_scene_t, i)->seen) {
            box_atmos_remove(a, p, i);
        }
    }

done:
    return;
}

/** Write the main light of the atmosphere of a scene, which the in scatter color of a fog
 * whose HeightFog.atmosphere is the atmosphere of the scene follows.
 *
 * @param lights The state of the light extract
 * @param scene The scene
 * @param out The atmosphere inputs of a view, zeroed
 */
static void box_atmos_view_fog(
    const void *lights,
    ecs_entity_t scene,
    box_render_view_atmos_t *out)
{
    float color[3], fade = 0;
    bool moon = false;
    if (box_render_lights_atmos_main(lights, scene, out->fog_light, color, &fade, &moon)) {
        out->fog_light[3] = fade;
        if (moon) {
            glm_vec3_scale(color, 1.0f / GLM_PIf, out->fog_moon);
            out->fog_moon[3] = 1.0f;
        }
    }
}

/** Write the inputs of the sky view and aerial perspective of a view of a scene with LUTs.
 *
 * @param world The world
 * @param lights The state of the light extract
 * @param s The LUTs of the scene of the view
 * @param v The view
 */
static void box_atmos_view(
    const ecs_world_t *world,
    const void *lights,
    const box_atmos_scene_t *s,
    box_render_view_t *v)
{
    const Atmosphere *value = &s->value;
    const View *view = v->view && ecs_is_alive(world, v->view)
        ? ecs_get(world, v->view, View)
        : NULL;
    box_render_view_atmos_t *out = &v->atmos;
    float camera_y = v->camera_transform[3][1];
    float scale = value->night_intensity;
    ecs_os_memset_t(out, 0, box_render_view_atmos_t);
    v->atmosphere = s->handle;
    if (!box_render_lights_atmos_sun(lights, v->scene, out->sun, out->sun_color)) {
        glm_vec3_copy((vec3){0, -1, 0}, out->sun);
        glm_vec3_zero(out->sun_color);
    }

    out->night[0] = box_atmos_extract_linear(value->night_tint.r) * scale;
    out->night[1] = box_atmos_extract_linear(value->night_tint.g) * scale;
    out->night[2] = box_atmos_extract_linear(value->night_tint.b) * scale;
    float radius = glm_rad(glm_min(value->sun_radius, 90.0f));
    out->sun_disk[0] = cosf(radius);
    out->sun_disk[1] = sinf(radius);
    out->sun_disk[2] = glm_max(value->sun_disk_intensity, 0.0f);
    if (!box_render_lights_atmos_moon(lights, v->scene, out->moon, out->moon_color,
        &out->moon[3]))
    {
        glm_vec4_copy((vec4){0, -1, 0, 0}, out->moon);
        glm_vec3_zero(out->moon_color);
    }

    radius = glm_rad(glm_min(value->moon_radius, 90.0f));
    out->moon_disk[0] = cosf(radius);
    out->moon_disk[1] = sinf(radius);
    out->moon_disk[2] = glm_max(value->moon_disk_intensity, 0.0f) *
        glm_max(value->moon_albedo, 0.0f);
    out->moon_disk[3] = glm_max(value->earthshine, 0.0f);
    out->altitude = glm_max(value->ground_altitude +
        (camera_y - value->sea_level) / value->units_per_km, (float)BOX_ATMOS_PLANET_OFFSET);
    out->mie_anisotropy = value->mie.anisotropy;
    out->aerial_distance = value->aerial_distance;
    out->aerial_intensity = value->aerial_intensity;
    out->km_per_unit = 1.0f / value->units_per_km;
    box_atmos_view_fog(lights, v->scene, out);
    out->sky = view ? view->lighting.atmosphere : true;
    out->entity = s->entity;
}

/** Give every view of a scene with LUTs its atmosphere, after the light extract found the sun.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractAtmosphereViews(
    ecs_iter_t *it)
{
    const box_render_atmos_t *a = box_atmos_state(it->world);
    const void *lights = box_render_lights_state(it->world);
    box_render_packet_t *p = NULL;
    int32_t i, count;
    if (!a || !ecs_vec_count(&a->scenes)) {
        goto done;
    }

    p = box_render_packet(it->world);
    count = ecs_vec_count(&p->views);
    for (i = 0; i < count; i ++) {
        box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        const box_atmos_scene_t *s = box_atmos_find(a, v->scene);
        if (s && s->seen) {
            box_atmos_view(it->world, lights, s, v);
        }
    }

done:
    return;
}

/** Write the inputs of the environment of a scene with LUTs: its sky seen from the ground.
 *
 * @param lights The state of the light extract
 * @param s The LUTs of the scene
 * @param out Receives the inputs, version 0
 */
static void box_atmos_env_inputs(
    const void *lights,
    const box_atmos_scene_t *s,
    box_render_atmosphere_env_t *out)
{
    const Atmosphere *value = &s->value;
    float scale = value->night_intensity;
    ecs_os_memset_t(out, 0, box_render_atmosphere_env_t);
    out->atmosphere = s->handle;
    out->altitude = glm_max(value->ground_altitude, (float)BOX_ATMOS_PLANET_OFFSET);
    out->mie_anisotropy = value->mie.anisotropy;
    if (!box_render_lights_atmos_sun(lights, s->scene, out->sun, out->sun_color)) {
        glm_vec3_copy((vec3){0, -1, 0}, out->sun);
    }

    if (!box_render_lights_atmos_moon(lights, s->scene, out->moon, out->moon_color,
        &out->moon[3]))
    {
        glm_vec3_copy((vec3){0, -1, 0}, out->moon);
    }

    out->night[0] = box_atmos_extract_linear(value->night_tint.r) * scale;
    out->night[1] = box_atmos_extract_linear(value->night_tint.g) * scale;
    out->night[2] = box_atmos_extract_linear(value->night_tint.b) * scale;
}

/** Check whether a light of an environment changed enough since its last render to show: its
 * direction turned by more than an angle, or a channel of its color changed by more than
 * BOX_RENDER_ATMOS_ENV_COLOR of the brightest channel it had.
 *
 * @param was_dir Direction at the last render
 * @param was_color Color at the last render
 * @param dir Direction now
 * @param color Color now
 * @param cos_angle Cosine of the angle
 * @return Whether the environment is rendered again
 */
static bool box_atmos_env_light_changed(
    const float *was_dir,
    const float *was_color,
    const float *dir,
    const float *color,
    float cos_angle)
{
    float ref = glm_max(glm_max(was_color[0], was_color[1]), was_color[2]);
    bool result = glm_vec3_dot(ECS_CONST_CAST(float*, was_dir), ECS_CONST_CAST(float*, dir)) <
        cos_angle;
    int32_t c;
    for (c = 0; c < 3 && !result; c ++) {
        result = fabsf(color[c] - was_color[c]) > BOX_RENDER_ATMOS_ENV_COLOR * ref;
    }

    return result;
}

/** Check whether the environment of a scene is rendered again with new inputs: it never was,
 * its cubemap is new or empty, the medium changed, or the sun or moon changed enough to show.
 *
 * @param world The world
 * @param s The LUTs of the scene
 * @param handle Handle of the cubemap
 * @param in The inputs of this frame
 * @return Whether the environment is rendered
 */
static bool box_atmos_env_changed(
    const ecs_world_t *world,
    const box_atmos_scene_t *s,
    uint32_t handle,
    const box_render_atmosphere_env_t *in)
{
    const box_render_atmosphere_env_t *was = &s->env;
    float angle = glm_clamp(s->value.ibl_update_angle, 0.0f, 180.0f);
    float cos_angle = cosf(glm_rad(angle));
    bool result = !s->env_sent || s->env_entity != s->entity || s->env_handle != handle ||
        !box_render_environment_content(world, s->entity);
    if (!result) {
        result = ecs_os_memcmp_t(&s->env_key, &s->key, box_render_atmosphere_desc_t) ||
            was->atmosphere != in->atmosphere || was->altitude != in->altitude ||
            was->mie_anisotropy != in->mie_anisotropy ||
            ecs_os_memcmp(was->night, in->night, ECS_SIZEOF(float) * 4);
    }

    if (!result) {
        result = box_atmos_env_light_changed(was->sun, was->sun_color, in->sun, in->sun_color,
            cos_angle) ||
            box_atmos_env_light_changed(was->moon, was->moon_color, in->moon, in->moon_color,
                cos_angle);
    }

    return result;
}

/** Render the sky of a scene with LUTs into the environment of the entity of its Atmosphere
 * when that changed enough since the last render.
 *
 * @param world The world
 * @param lights The state of the light extract
 * @param p The packet
 * @param s The LUTs of the scene
 */
static void box_atmos_environment(
    ecs_world_t *world,
    const void *lights,
    box_render_packet_t *p,
    box_atmos_scene_t *s)
{
    uint32_t handle = box_render_environment_handle(world, s->entity);
    box_render_atmosphere_env_t in;
    if (!handle) {
        s->env_sent = false;
        goto done;
    }

    box_atmos_env_inputs(lights, s, &in);
    if (!box_atmos_env_changed(world, s, handle, &in)) {
        goto done;
    }

    in.version = box_render_environment_sent(world, s->entity);
    box_render_atmosphere_env_t *desc = box_render_packet_resource(p,
        BOX_RENDER_ATMOSPHERE_ENVIRONMENT, BOX_RENDER_UPDATE, handle,
        sizeof(box_render_atmosphere_env_t));
    if (desc) {
        *desc = in;
    }

    s->env = in;
    s->env_key = s->key;
    s->env_entity = s->entity;
    s->env_handle = handle;
    s->env_sent = true;
done:
    return;
}

/** Render the environment of every scene with LUTs whose sky changed enough to show.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractAtmosphereEnvironment(
    ecs_iter_t *it)
{
    box_render_atmos_t *a = box_atmos_state(it->world);
    const void *lights = box_render_lights_state(it->world);
    box_render_packet_t *p = NULL;
    int32_t i;
    if (!a || !ecs_vec_count(&a->scenes)) {
        goto done;
    }

    p = box_render_packet(it->world);
    for (i = 0; i < ecs_vec_count(&a->scenes); i ++) {
        box_atmos_scene_t *s = ecs_vec_get_t(&a->scenes, box_atmos_scene_t, i);
        if (s->seen) {
            box_atmos_environment(it->world, lights, p, s);
        }
    }

done:
    return;
}

/** Free the state of the atmosphere extract.
 *
 * @param ctx The state
 */
static void box_atmos_free(
    void *ctx)
{
    box_render_atmos_t *a = ctx;
    ecs_vec_fini_t(NULL, &a->scenes, box_atmos_scene_t);
    ecs_vec_fini_t(NULL, &a->free_handles, uint32_t);
    ecs_os_free(a);
}

void box_render_atmosphere_init(
    ecs_world_t *world)
{
    box_render_atmos_t *a = ecs_os_calloc_t(box_render_atmos_t);
    ecs_vec_init_t(NULL, &a->scenes, box_atmos_scene_t, 0);
    ecs_vec_init_t(NULL, &a->free_handles, uint32_t, 0);
    a->next_handle = BOX_ATMOS_HANDLE_FIRST;

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractAtmosphere" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractAtmosphere,
        .ctx = a,
        .ctx_free = box_atmos_free
    });
}

void box_render_atmosphere_views_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractAtmosphereViews" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractAtmosphereViews
    });
}

void box_render_atmosphere_environment_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractAtmosphereEnvironment" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractAtmosphereEnvironment
    });
}

void box_render_atmosphere_import(
    ecs_world_t *world)
{
    box_render_atmos_t *a = box_atmos_state(world);
    if (a && !a->atmospheres) {
        a->atmospheres = ecs_query(world, {
            .entity = ecs_entity(world, { .name = "RenderAtmospheres" }),
            .terms = {
                { .id = ecs_id(Atmosphere), .inout = EcsIn },
                {
                    .id = ecs_id(Scene),
                    .src.id = EcsSelf | EcsUp,
                    .trav = EcsChildOf,
                    .inout = EcsInOutNone,
                    .oper = EcsOptional
                }
            },
            .cache_kind = EcsQueryCacheAuto
        });
    }
}

int32_t box_render_atmosphere_version(
    const ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t entity,
    int32_t current)
{
    const box_render_atmos_t *a = box_atmos_state(world);
    const box_atmos_scene_t *s = a ? box_atmos_find(a, scene) : NULL;

    return s && s->sent && s->entity == entity ? s->version : current;
}

uint32_t box_render_atmosphere_handle(
    const ecs_world_t *world,
    ecs_entity_t scene)
{
    const box_render_atmos_t *a = box_atmos_state(world);
    const box_atmos_scene_t *s = a ? box_atmos_find(a, scene) : NULL;

    return s ? s->handle : 0;
}

bool box_render_atmosphere_use(
    const box_render_packet_t *p)
{
    bool result = false;
    int32_t i;
    for (i = 0; !result && i < ecs_vec_count(&p->views); i ++) {
        result = ecs_vec_get_t(&p->views, box_render_view_t, i)->atmosphere != 0;
    }

    for (i = 0; !result && i < ecs_vec_count(&p->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &p->resources, box_render_resource_t, i);
        result = (r->kind == BOX_RENDER_ATMOSPHERE && r->op != BOX_RENDER_DESTROY) ||
            r->kind == BOX_RENDER_ATMOSPHERE_ENVIRONMENT;
    }

    return result;
}

void box_render_atmos_transmittance_uv(
    float bottom,
    float top,
    float r,
    float mu,
    float *uv)
{
    float h = sqrtf(glm_max(0.0f, top * top - bottom * bottom));
    float rho = sqrtf(glm_max(0.0f, r * r - bottom * bottom));
    float disc = glm_max(0.0f, r * r * (mu * mu - 1.0f) + top * top);
    float d = glm_max(0.0f, -r * mu + sqrtf(disc));
    float d_min = top - r, d_max = rho + h;
    float x_mu = d_max > d_min + 1e-6f ? (d - d_min) / (d_max - d_min) : 0.0f;
    uv[0] = glm_clamp(x_mu, 0.0f, 1.0f);
    uv[1] = glm_clamp(rho / glm_max(h, 1e-6f), 0.0f, 1.0f);
}
