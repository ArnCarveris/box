/**
 * @file render/atmosphere.c
 * @brief Keeps the transmittance and multi scattering LUTs of every scene with an Atmosphere and
 * gives the views of those scenes the inputs of their sky view and aerial perspective.
 *
 * The LUTs are a BOX_RENDER_ATMOSPHERE per scene and per valid PlanetAtmosphere, built again only
 * when their medium changes; the sun, the colors and the cameras only reach the per view passes,
 * and the lit views of the scene of a planet draw its shell.
 *
 * The entity of the LUTs is also an environment source: its sky goes into the cubemap of its
 * Environment as a BOX_RENDER_ATMOSPHERE_ENVIRONMENT, throttled to the changes that show. Its
 * CloudLayers, at most BOX_CLOUD_LAYER_MAX by entity id, reach the sky and that cubemap.
 */

#include "environment.h"
#include "../atmosphere/atmosphere.h"
#include "../texture/texture.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** First handle of the LUTs of an atmosphere */
#define BOX_ATMOS_HANDLE_FIRST (1)

/** A CloudLayer of a scene in this frame */
typedef struct box_atmos_cloud_t {
    ecs_entity_t entity;    /**< Entity with the CloudLayer */
    CloudLayer value;       /**< Its values */
    float offset[2];        /**< Its offset at the total time, wrapped to [0, size) */
} box_atmos_cloud_t;

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
    box_atmos_cloud_t clouds[BOX_CLOUD_LAYER_MAX]; /**< Cloud layers of this frame by ascending
                             *   altitude */
    int32_t cloud_count;    /**< Layers in clouds */
} box_atmos_scene_t;

/** The LUTs of a PlanetAtmosphere */
typedef struct box_atmos_planet_t {
    ecs_entity_t entity;    /**< Entity with the PlanetAtmosphere */
    ecs_entity_t scene;     /**< Its scene root, 0 for the default scene */
    uint32_t handle;        /**< Handle of the BOX_RENDER_ATMOSPHERE */
    bool sent;              /**< The backend has the LUTs */
    bool seen;              /**< The entity has a valid PlanetAtmosphere in this frame */
    int32_t version;        /**< lut_version of the last build */
    box_render_atmosphere_desc_t key; /**< Medium of the last build */
    PlanetAtmosphere value; /**< Values of the PlanetAtmosphere in this frame */
    float center[3];        /**< World position of the entity in this frame */
    float scale;            /**< Uniform world scale of the entity in this frame */
} box_atmos_planet_t;

/** A planet shell a view sees, by its distance from the camera */
typedef struct box_atmos_planet_order_t {
    float distance;         /**< Distance of the center from the camera */
    int32_t index;          /**< Index of the planet in the planets of the state */
} box_atmos_planet_order_t;

/** Main thread state of the atmosphere extract */
typedef struct box_render_atmos_t {
    ecs_query_t *atmospheres;   /**< Atmosphere and Scene(self|up), NULL without box.atmosphere */
    ecs_query_t *clouds;        /**< CloudLayer and Scene(self|up), NULL without box.atmosphere */
    double time;                /**< Seconds the world advanced in total, the wind time */
    bool clouds_warned;         /**< A scene had more than BOX_CLOUD_LAYER_MAX layers */
    ecs_vec_t scenes;           /**< box_atmos_scene_t */
    ecs_query_t *planets;       /**< PlanetAtmosphere, WorldTransform and Scene(self|up), NULL
                                 *   without box.atmosphere */
    ecs_vec_t planet_luts;      /**< box_atmos_planet_t */
    ecs_vec_t planet_order;     /**< box_atmos_planet_order_t, scratch: the planets of a view */
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
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.Atmosphere");
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

/** Take a handle of a BOX_RENDER_ATMOSPHERE, a released one first.
 *
 * @param a The state
 * @return The handle
 */
static uint32_t box_atmos_handle(
    box_render_atmos_t *a)
{
    uint32_t result;
    if (ecs_vec_count(&a->free_handles)) {
        result = *ecs_vec_last_t(&a->free_handles, uint32_t);
        ecs_vec_remove_last(&a->free_handles);
    } else {
        result = a->next_handle ++;
    }

    return result;
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
    result->handle = box_atmos_handle(a);

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

/** Add a CloudLayer to the layers of its scene, which keep the BOX_CLOUD_LAYER_MAX lowest entity
 * ids; warn once when a scene has more.
 *
 * @param a The state
 * @param s The LUTs of the scene
 * @param entity Entity with the CloudLayer
 * @param value Its values
 */
static void box_atmos_cloud_add(
    box_render_atmos_t *a,
    box_atmos_scene_t *s,
    ecs_entity_t entity,
    const CloudLayer *value)
{
    box_atmos_cloud_t *slot = NULL;
    int32_t i;
    if (s->cloud_count < BOX_CLOUD_LAYER_MAX) {
        slot = &s->clouds[s->cloud_count ++];
    } else {
        if (!a->clouds_warned) {
            ecs_warn("box.atmosphere: a scene has more than %d CloudLayers, the others are "
                "ignored", BOX_CLOUD_LAYER_MAX);
            a->clouds_warned = true;
        }

        for (i = 0; i < s->cloud_count; i ++) {
            if (s->clouds[i].entity > entity &&
                (!slot || s->clouds[i].entity > slot->entity))
            {
                slot = &s->clouds[i];
            }
        }
    }

    if (slot) {
        slot->entity = entity;
        slot->value = *value;
    }
}

/** Order two cloud layers by ascending altitude, then entity id.
 *
 * @param p1 The first layer
 * @param p2 The second layer
 * @return Their order
 */
static int box_atmos_cloud_compare(
    const void *p1,
    const void *p2)
{
    const box_atmos_cloud_t *c1 = p1, *c2 = p2;
    int result = (c1->value.altitude > c2->value.altitude) -
        (c1->value.altitude < c2->value.altitude);
    if (!result) {
        result = (c1->entity > c2->entity) - (c1->entity < c2->entity);
    }

    return result;
}

/** Collect the CloudLayers of every scene with LUTs, sort them by altitude and compute the
 * offsets of their wind at the total time.
 *
 * @param world The world
 * @param a The state
 */
static void box_atmos_collect_clouds(
    ecs_world_t *world,
    box_render_atmos_t *a)
{
    int32_t i, j;
    for (i = 0; i < ecs_vec_count(&a->scenes); i ++) {
        ecs_vec_get_t(&a->scenes, box_atmos_scene_t, i)->cloud_count = 0;
    }

    ecs_iter_t it = ecs_query_iter(world, a->clouds);
    while (ecs_query_next(&it)) {
        const CloudLayer *values = ecs_field(&it, CloudLayer, 0);
        for (i = 0; i < it.count; i ++) {
            box_atmos_scene_t *s = box_atmos_find(a, box_render_scene_field(&it, 1, i));
            if (s && s->seen) {
                box_atmos_cloud_add(a, s, it.entities[i], &values[i]);
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&a->scenes); i ++) {
        box_atmos_scene_t *s = ecs_vec_get_t(&a->scenes, box_atmos_scene_t, i);
        qsort(s->clouds, (size_t)s->cloud_count, sizeof(box_atmos_cloud_t),
            box_atmos_cloud_compare);
        for (j = 0; j < s->cloud_count; j ++) {
            box_atmos_cloud_t *c = &s->clouds[j];
            LightCookie wind = {
                .size = c->value.size,
                .offset = { c->value.offset[0], c->value.offset[1] },
                .velocity = { c->value.velocity[0], c->value.velocity[1] }
            };
            box_render_cookie_offset(&wind, a->time, c->offset);
        }
    }
}

/** Find the LUTs of a PlanetAtmosphere.
 *
 * @param a The state
 * @param entity The entity with the PlanetAtmosphere
 * @return Index of the LUTs, -1 when the entity has none
 */
static int32_t box_atmos_planet_find(
    const box_render_atmos_t *a,
    ecs_entity_t entity)
{
    int32_t i, result = -1;
    for (i = 0; i < ecs_vec_count(&a->planet_luts); i ++) {
        if (ecs_vec_get_t(&a->planet_luts, box_atmos_planet_t, i)->entity == entity) {
            result = i;
            break;
        }
    }

    return result;
}

/** Write the lut_version and the error of a PlanetAtmosphere into its status when they changed.
 *
 * @param world The world
 * @param entity The entity with the PlanetAtmosphere
 * @param version The lut_version
 * @param error The error, NULL for none
 */
static void box_atmos_planet_status(
    ecs_world_t *world,
    ecs_entity_t entity,
    int32_t version,
    const char *error)
{
    const PlanetAtmosphereStatus *status = ecs_get(world, entity, PlanetAtmosphereStatus);
    if (!status) {
        goto done;
    }

    bool same_error = error ? status->error && !strcmp(status->error, error) : !status->error;
    if (!same_error || status->lut_version != version) {
        PlanetAtmosphereStatus value = {
            .lut_version = version,
            .error = ECS_CONST_CAST(char*, error)
        };
        ecs_set_ptr(world, entity, PlanetAtmosphereStatus, &value);
    }

done:
    return;
}

/** Visit every PlanetAtmosphere, build the LUTs of a valid one when the backend lacks them or
 * its medium changed, and write its status.
 *
 * @param world The world
 * @param a The state
 * @param p The packet
 */
static void box_atmos_collect_planets(
    ecs_world_t *world,
    box_render_atmos_t *a,
    box_render_packet_t *p)
{
    ecs_iter_t it = ecs_query_iter(world, a->planets);
    while (ecs_query_next(&it)) {
        const PlanetAtmosphere *values = ecs_field(&it, PlanetAtmosphere, 0);
        const WorldTransform *transforms = ecs_field(&it, WorldTransform, 1);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            const PlanetAtmosphereStatus *status = ecs_get(world, e, PlanetAtmosphereStatus);
            const char *error = box_atmos_planet_error(&values[i]);
            int32_t index = box_atmos_planet_find(a, e);
            int32_t version = status ? status->lut_version : 0;
            box_atmos_planet_t *l = index != -1
                ? ecs_vec_get_t(&a->planet_luts, box_atmos_planet_t, index)
                : NULL;
            if (!error && (!l || !l->seen)) {
                box_render_atmosphere_desc_t key;
                Atmosphere medium;
                if (!l) {
                    l = ecs_vec_append_t(NULL, &a->planet_luts, box_atmos_planet_t);
                    ecs_os_memset_t(l, 0, box_atmos_planet_t);
                    l->entity = e;
                    l->handle = box_atmos_handle(a);
                    l->version = version;
                }

                box_atmos_planet_medium(&values[i], &medium);
                box_atmos_desc(&medium, &key);
                l->seen = true;
                l->scene = box_render_scene_field(&it, 2, i);
                l->value = values[i];
                WorldTransform buf;
                const WorldTransform *t = box_transform_world_field(
                    world, &it, transforms, i, &buf);
                if (t) {
                    glm_vec3_copy(ECS_CONST_CAST(float*, t->m[3]), l->center);
                    l->scale = box_transform_world_scale(t);
                } else {
                    glm_vec3_zero(l->center);
                    l->scale = 1.0f;
                }

                if (!l->sent || ecs_os_memcmp_t(&key, &l->key, box_render_atmosphere_desc_t)) {
                    box_render_atmosphere_desc_t *desc = box_render_packet_resource(p,
                        BOX_RENDER_ATMOSPHERE, l->sent ? BOX_RENDER_UPDATE : BOX_RENDER_CREATE,
                        l->handle, sizeof(box_render_atmosphere_desc_t));
                    if (desc) {
                        *desc = key;
                    }

                    l->key = key;
                    l->sent = true;
                    l->version = version + 1;
                }

                version = l->version;
            }

            box_atmos_planet_status(world, e, version, error);
        }
    }
}

/** Destroy the LUTs of a PlanetAtmosphere on the backend and remove them.
 *
 * @param a The state
 * @param p The packet, receives the destroy
 * @param index Index of the LUTs
 */
static void box_atmos_planet_remove(
    box_render_atmos_t *a,
    box_render_packet_t *p,
    int32_t index)
{
    box_atmos_planet_t *l = ecs_vec_get_t(&a->planet_luts, box_atmos_planet_t, index);
    if (l->sent) {
        box_render_packet_resource(p, BOX_RENDER_ATMOSPHERE, BOX_RENDER_DESTROY, l->handle, 0);
    }

    *ecs_vec_append_t(NULL, &a->free_handles, uint32_t) = l->handle;
    ecs_vec_remove_t(&a->planet_luts, box_atmos_planet_t, index);
}

/** Keep the LUTs of the scenes with a valid Atmosphere and of the valid PlanetAtmospheres, and
 * destroy the others.
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
    a->time += (double)it->delta_time;
    if (!a->atmospheres && !ecs_vec_count(&a->scenes) && !ecs_vec_count(&a->planet_luts)) {
        goto done;
    }

    p = box_render_packet(world);
    if (backend->ctx != a->backend || backend->render != a->render ||
        backend->generation != a->generation)
    {
        for (i = 0; i < ecs_vec_count(&a->scenes); i ++) {
            ecs_vec_get_t(&a->scenes, box_atmos_scene_t, i)->sent = false;
        }

        for (i = 0; i < ecs_vec_count(&a->planet_luts); i ++) {
            ecs_vec_get_t(&a->planet_luts, box_atmos_planet_t, i)->sent = false;
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

    if (a->clouds) {
        box_atmos_collect_clouds(world, a);
    }

    for (i = 0; i < ecs_vec_count(&a->planet_luts); i ++) {
        ecs_vec_get_t(&a->planet_luts, box_atmos_planet_t, i)->seen = false;
    }

    if (a->planets) {
        box_atmos_collect_planets(world, a, p);
    }

    for (i = ecs_vec_count(&a->planet_luts) - 1; i >= 0; i --) {
        if (!ecs_vec_get_t(&a->planet_luts, box_atmos_planet_t, i)->seen) {
            box_atmos_planet_remove(a, p, i);
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

/** Wrap a texture coordinate to [0, 1).
 *
 * @param v The coordinate
 * @return The wrapped coordinate
 */
static float box_atmos_cloud_wrap(
    double v)
{
    double w = v - floor(v);
    float result = (float)w;

    return result >= 1.0f ? 0.0f : result;
}

/** Write the cloud layers of a scene seen from a point: those with a size above 0 and a texture
 * the backend has an image of, in the order of the scene.
 *
 * @param world The world
 * @param s The LUTs of the scene
 * @param x Absolute world X of the point
 * @param y Absolute world Y of the point
 * @param z Absolute world Z of the point
 * @param out Receives the layers, BOX_CLOUD_LAYER_MAX at most
 * @return The layers written
 */
static int32_t box_atmos_clouds_from(
    const ecs_world_t *world,
    const box_atmos_scene_t *s,
    double x,
    double y,
    double z,
    box_render_cloud_layer_t *out)
{
    const box_textures_t *textures = box_textures_get(world);
    int32_t i, result = 0;
    for (i = 0; i < s->cloud_count && result < BOX_CLOUD_LAYER_MAX; i ++) {
        const box_atmos_cloud_t *c = &s->clouds[i];
        const CloudLayer *value = &c->value;
        uint32_t handle = value->texture && value->size > 0
            ? box_textures_handle(textures, value->texture)
            : 0;
        if (!handle) {
            continue;
        }

        box_render_cloud_layer_t *l = &out[result ++];
        double size = value->size;
        l->texture = handle;
        l->inv_size = 1.0f / value->size;
        l->height = (float)((double)value->altitude - y);
        l->altitude = value->altitude;
        l->offset[0] = c->offset[0];
        l->offset[1] = c->offset[1];
        l->origin[0] = box_atmos_cloud_wrap((x + (double)c->offset[0]) / size);
        l->origin[1] = box_atmos_cloud_wrap((z + (double)c->offset[1]) / size);
        l->threshold = value->threshold;
        l->softness = glm_max(value->softness, 0.0f);
        l->density = glm_max(value->density, 0.0f);
        l->tint[0] = box_atmos_extract_linear(value->tint.r);
        l->tint[1] = box_atmos_extract_linear(value->tint.g);
        l->tint[2] = box_atmos_extract_linear(value->tint.b);
        l->entity = c->entity;
    }

    return result;
}

/** Write the cloud layers of a view that draws the sky, seen from its camera.
 *
 * @param world The world
 * @param s The LUTs of the scene of the view
 * @param v The view
 */
static void box_atmos_view_clouds(
    const ecs_world_t *world,
    const box_atmos_scene_t *s,
    box_render_view_t *v)
{
    box_render_view_atmos_t *out = &v->atmos;
    double origin[3];
    box_render_origin(world, origin);
    out->cloud_count = box_atmos_clouds_from(world, s,
        origin[0] + (double)v->camera_transform[3][0],
        origin[1] + (double)v->camera_transform[3][1],
        origin[2] + (double)v->camera_transform[3][2], out->clouds);
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
    double origin[3];
    box_render_origin(world, origin);
    float camera_y = (float)(origin[1] + (double)v->camera_transform[3][1]);
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
    if (out->sky) {
        box_atmos_view_clouds(world, s, v);
    }
}

/** Order planet shells by their distance from the camera, farthest first, then by index.
 *
 * @param p1 The first planet
 * @param p2 The second planet
 * @return Negative when the first is farther
 */
static int box_atmos_planet_compare(
    const void *p1,
    const void *p2)
{
    const box_atmos_planet_order_t *o1 = p1, *o2 = p2;
    int result = (o1->distance < o2->distance) - (o1->distance > o2->distance);
    if (!result) {
        result = (o1->index > o2->index) - (o1->index < o2->index);
    }

    return result;
}

/** Get the km in a world unit of a planet: its planet_radius over its radius in world units,
 * the radius times the uniform world scale of the entity.
 *
 * @param l The LUTs of the planet
 * @return The km per world unit, 0 for a planet flattened to a scale of 0
 */
static float box_atmos_planet_km(
    const box_atmos_planet_t *l)
{
    float radius = l->value.radius * l->scale;

    return radius > 0.0f ? l->value.planet_radius / radius : 0.0f;
}

/** Write the shell of a planet as a view draws it, lit by its sun.
 *
 * @param lights The state of the light extract
 * @param l The LUTs of the planet
 * @param out Receives the shell
 */
static void box_atmos_planet_record(
    const void *lights,
    const box_atmos_planet_t *l,
    box_render_planet_atmos_t *out)
{
    const PlanetAtmosphere *value = &l->value;
    ecs_os_memset_t(out, 0, box_render_planet_atmos_t);
    out->medium = l->key;
    glm_vec3_copy(ECS_CONST_CAST(float*, l->center), out->center);
    out->center[3] = box_atmos_planet_km(l);
    if (!box_render_lights_planet_sun(lights, l->scene, value->sun, out->sun, out->sun_color)) {
        glm_vec3_copy((vec3){0, -1, 0}, out->sun);
        glm_vec3_zero(out->sun_color);
    }

    out->sun[3] = value->mie.anisotropy;
    out->sun_color[3] = glm_max(value->intensity, 0.0f);
    out->atmosphere = l->handle;
    out->entity = l->entity;
}

/** Give a view the shells of the planets of its scene that may be in its frustum, back to front:
 * a lit view of a View entity with a depth.
 *
 * @param world The world
 * @param lights The state of the light extract
 * @param a The state
 * @param p The packet
 * @param v The view
 */
static void box_atmos_view_planets(
    const ecs_world_t *world,
    const void *lights,
    box_render_atmos_t *a,
    box_render_packet_t *p,
    box_render_view_t *v)
{
    const View *view = v->view && ecs_is_alive(world, v->view)
        ? ecs_get(world, v->view, View)
        : NULL;
    int32_t i, count;
    v->planet_first = ecs_vec_count(&p->planet_atmospheres);
    v->planet_count = 0;
    if (!view || !view->lighting.enabled || !box_camera_has_depth(&v->camera_data)) {
        goto done;
    }

    ecs_vec_clear(&a->planet_order);
    for (i = 0; i < ecs_vec_count(&a->planet_luts); i ++) {
        const box_atmos_planet_t *l = ecs_vec_get_t(&a->planet_luts, box_atmos_planet_t, i);
        float top = (l->value.planet_radius + l->value.thickness) * l->value.radius *
            l->scale / l->value.planet_radius;
        if (!l->seen || !(top > 0.0f) || l->scene != v->scene ||
            !box_render_view_sees(v, l->center, top))
        {
            continue;
        }

        box_atmos_planet_order_t *o = ecs_vec_append_t(
            NULL, &a->planet_order, box_atmos_planet_order_t);
        o->distance = glm_vec3_distance(v->camera_transform[3],
            ECS_CONST_CAST(float*, l->center));
        o->index = i;
    }

    count = ecs_vec_count(&a->planet_order);
    box_atmos_planet_order_t *order = ecs_vec_first_t(
        &a->planet_order, box_atmos_planet_order_t);
    if (count > 1) {
        qsort(order, (size_t)count, sizeof(box_atmos_planet_order_t),
            box_atmos_planet_compare);
    }

    for (i = 0; i < count; i ++) {
        box_atmos_planet_record(lights, ecs_vec_get_t(&a->planet_luts, box_atmos_planet_t,
            order[i].index), ecs_vec_append_t(NULL, &p->planet_atmospheres,
                box_render_planet_atmos_t));
    }

    v->planet_count = count;
done:
    return;
}

/** Write the planet light of every valid PlanetAtmosphere whose meshes took a planet light
 * slot: its center, radii, the direction toward its sun and the angular radius of its disk.
 *
 * @param world The world
 * @param lights The state of the light extract
 * @param a The state
 * @param p The packet
 */
static void box_atmos_planet_lights(
    const ecs_world_t *world,
    const void *lights,
    const box_render_atmos_t *a,
    box_render_packet_t *p)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&a->planet_luts); i ++) {
        const box_atmos_planet_t *l = ecs_vec_get_t(&a->planet_luts, box_atmos_planet_t, i);
        uint32_t slot = l->seen ? box_render_planet_light_slot(world, l->entity) : 0u;
        float color[4];
        if (!slot) {
            continue;
        }

        box_render_planet_light_t *out = ecs_vec_append_t(
            NULL, &p->planet_lights, box_render_planet_light_t);
        ecs_os_memset_t(out, 0, box_render_planet_light_t);
        glm_vec3_copy(ECS_CONST_CAST(float*, l->center), out->center);
        out->center[3] = box_atmos_planet_km(l);
        out->radii[0] = l->key.radii[0];
        out->radii[1] = l->key.radii[1];
        out->radii[3] = glm_max(l->value.intensity, 0.0f);
        if (box_render_lights_planet_sun(lights, l->scene, l->value.sun, out->sun, color) &&
            glm_vec3_norm(out->sun) > 0.0f)
        {
            glm_vec3_normalize(out->sun);
        } else {
            glm_vec3_copy((vec3){0, -1, 0}, out->sun);
        }

        out->sun[3] = glm_rad(glm_clamp(l->value.sun_radius, 0.0f, 90.0f));
        out->slot = slot;
        out->atmosphere = l->handle;
        out->entity = l->entity;
    }
}

/** Give every view of a scene with LUTs its atmosphere and every lit view the planet shells of
 * its scene, after the light extract found the suns.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractAtmosphereViews(
    ecs_iter_t *it)
{
    box_render_atmos_t *a = box_atmos_state(it->world);
    const void *lights = box_render_lights_state(it->world);
    box_render_packet_t *p = NULL;
    int32_t i, count;
    if (!a || (!ecs_vec_count(&a->scenes) && !ecs_vec_count(&a->planet_luts))) {
        goto done;
    }

    p = box_render_packet(it->world);
    box_atmos_planet_lights(it->world, lights, a, p);
    count = ecs_vec_count(&p->views);
    for (i = 0; i < count; i ++) {
        box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        const box_atmos_scene_t *s = box_atmos_find(a, v->scene);
        if (s && s->seen) {
            box_atmos_view(it->world, lights, s, v);
        }

        box_atmos_view_planets(it->world, lights, a, p, v);
    }

done:
    return;
}

/** Write the inputs of the environment of a scene with LUTs: its sky seen from the ground, the
 * ground altitude at world X and Z 0, and its cloud layers seen from there.
 *
 * @param world The world
 * @param lights The state of the light extract
 * @param s The LUTs of the scene
 * @param out Receives the inputs, version 0
 */
static void box_atmos_env_inputs(
    const ecs_world_t *world,
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
    out->aerial_intensity = value->aerial_intensity;
    out->km_per_unit = 1.0f / value->units_per_km;
    out->cloud_count = box_atmos_clouds_from(world, s, 0.0, value->sea_level, 0.0,
        out->clouds);
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

/** How the inputs of the environment of a scene changed since its last render */
typedef enum box_atmos_env_change_t {
    BOX_ATMOS_ENV_SAME,     /**< Not enough to show, the environment is not rendered */
    BOX_ATMOS_ENV_MOVED,    /**< Only cloud layers moved, rendered without new captures */
    BOX_ATMOS_ENV_CHANGED   /**< Rendered, and the captures that fill with it capture again */
} box_atmos_env_change_t;

/** Check how the cloud layers of an environment changed since its last render: a layer was
 * added, removed or changed, or one only moved more than BOX_RENDER_CLOUD_ENV_MOVE of its size.
 *
 * @param was The inputs of the last render
 * @param in The inputs of this frame
 * @return The change
 */
static box_atmos_env_change_t box_atmos_env_clouds_changed(
    const box_render_atmosphere_env_t *was,
    const box_render_atmosphere_env_t *in)
{
    box_atmos_env_change_t result = BOX_ATMOS_ENV_SAME;
    int32_t i;
    if (was->cloud_count != in->cloud_count || was->km_per_unit != in->km_per_unit ||
        was->aerial_intensity != in->aerial_intensity)
    {
        result = in->cloud_count || was->cloud_count
            ? BOX_ATMOS_ENV_CHANGED
            : BOX_ATMOS_ENV_SAME;
        goto done;
    }

    for (i = 0; i < in->cloud_count; i ++) {
        const box_render_cloud_layer_t *w = &was->clouds[i], *l = &in->clouds[i];
        if (w->entity != l->entity || w->texture != l->texture ||
            w->inv_size != l->inv_size || w->height != l->height ||
            w->threshold != l->threshold || w->softness != l->softness ||
            w->density != l->density || ecs_os_memcmp(w->tint, l->tint, ECS_SIZEOF(w->tint)))
        {
            result = BOX_ATMOS_ENV_CHANGED;
            break;
        }

        if (box_render_cloud_moved(w->offset, l->offset, 1.0f / l->inv_size)) {
            result = BOX_ATMOS_ENV_MOVED;
        }
    }

done:
    return result;
}

/** Check whether the environment of a scene is rendered again with new inputs: it never was,
 * its cubemap is new or empty, the medium changed, the sun or moon changed enough to show, or
 * its cloud layers changed or moved enough to show.
 *
 * @param world The world
 * @param s The LUTs of the scene
 * @param handle Handle of the cubemap
 * @param in The inputs of this frame
 * @return The change
 */
static box_atmos_env_change_t box_atmos_env_changed(
    const ecs_world_t *world,
    const box_atmos_scene_t *s,
    uint32_t handle,
    const box_render_atmosphere_env_t *in)
{
    const box_render_atmosphere_env_t *was = &s->env;
    float angle = glm_clamp(s->value.ibl_update_angle, 0.0f, 180.0f);
    float cos_angle = cosf(glm_rad(angle));
    bool changed = !s->env_sent || s->env_entity != s->entity || s->env_handle != handle ||
        !box_render_environment_content(world, s->entity);
    box_atmos_env_change_t result = BOX_ATMOS_ENV_SAME;
    if (!changed) {
        changed = ecs_os_memcmp_t(&s->env_key, &s->key, box_render_atmosphere_desc_t) ||
            was->atmosphere != in->atmosphere || was->altitude != in->altitude ||
            was->mie_anisotropy != in->mie_anisotropy ||
            ecs_os_memcmp(was->night, in->night, ECS_SIZEOF(float) * 4);
    }

    if (!changed) {
        changed = box_atmos_env_light_changed(was->sun, was->sun_color, in->sun, in->sun_color,
            cos_angle) ||
            box_atmos_env_light_changed(was->moon, was->moon_color, in->moon, in->moon_color,
                cos_angle);
    }

    result = changed ? BOX_ATMOS_ENV_CHANGED : box_atmos_env_clouds_changed(was, in);

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

    box_atmos_env_inputs(world, lights, s, &in);
    box_atmos_env_change_t change = box_atmos_env_changed(world, s, handle, &in);
    if (change == BOX_ATMOS_ENV_SAME) {
        goto done;
    }

    in.version = change == BOX_ATMOS_ENV_MOVED
        ? box_render_environment_sent_moved(world, s->entity)
        : box_render_environment_sent(world, s->entity);
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
    ecs_vec_fini_t(NULL, &a->planet_luts, box_atmos_planet_t);
    ecs_vec_fini_t(NULL, &a->planet_order, box_atmos_planet_order_t);
    ecs_vec_fini_t(NULL, &a->free_handles, uint32_t);
    ecs_os_free(a);
}

void box_render_atmosphere_init(
    ecs_world_t *world)
{
    box_render_atmos_t *a = ecs_os_calloc_t(box_render_atmos_t);
    ecs_vec_init_t(NULL, &a->scenes, box_atmos_scene_t, 0);
    ecs_vec_init_t(NULL, &a->planet_luts, box_atmos_planet_t, 0);
    ecs_vec_init_t(NULL, &a->planet_order, box_atmos_planet_order_t, 0);
    ecs_vec_init_t(NULL, &a->free_handles, uint32_t, 0);
    a->next_handle = BOX_ATMOS_HANDLE_FIRST;

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.Atmosphere" }),
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
        .entity = ecs_entity(world, { .name = "extract.AtmosphereViews" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractAtmosphereViews
    });
}

void box_render_atmosphere_environment_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.AtmosphereEnvironment" }),
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
            .entity = ecs_entity(world, { .name = "Atmospheres" }),
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

    if (a && !a->planets) {
        a->planets = ecs_query(world, {
            .entity = ecs_entity(world, { .name = "PlanetAtmospheres" }),
            .terms = {
                { .id = ecs_id(PlanetAtmosphere), .inout = EcsIn },
                { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional },
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
        box_render_planet_observers(world);
    }

    if (a && !a->clouds) {
        a->clouds = ecs_query(world, {
            .entity = ecs_entity(world, { .name = "CloudLayers" }),
            .terms = {
                { .id = ecs_id(CloudLayer), .inout = EcsIn },
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

    result |= ecs_vec_count(&p->planet_atmospheres) > 0;
    result |= ecs_vec_count(&p->planet_lights) > 0;

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

bool box_render_cloud_moved(
    const float *was,
    const float *now,
    float size)
{
    float d[2];
    int32_t c;
    bool result = false;
    if (!(size > 0)) {
        goto done;
    }

    for (c = 0; c < 2; c ++) {
        d[c] = fmodf(fabsf(now[c] - was[c]), size);
        d[c] = glm_min(d[c], size - d[c]);
    }

    result = d[0] * d[0] + d[1] * d[1] > (size * BOX_RENDER_CLOUD_ENV_MOVE) *
        (size * BOX_RENDER_CLOUD_ENV_MOVE);
done:
    return result;
}

float box_render_cloud_coverage(
    float t,
    float threshold,
    float softness)
{
    float ramp = glm_max(2.0f * softness, BOX_RENDER_CLOUD_MIN_RAMP);

    return glm_clamp((t - threshold) / ramp + 0.5f, 0.0f, 1.0f);
}

float box_render_cloud_alpha(
    float coverage,
    float density)
{
    return glm_clamp(coverage * density, 0.0f, 1.0f);
}
