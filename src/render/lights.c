/**
 * @file render/lights.c
 * @brief Collects the lights of every scene into light buffers and gives every view its lights.
 *
 * Lights are gathered per scene each frame, directional first, a buffer uploaded on change. A
 * scene without light entities gets the default sun, without AmbientLight the default sky and
 * ground; an Atmosphere attenuates its sun and moon, the default sun included.
 *
 * With LightBake views get the Mixed and Realtime lights, captures the Mixed and Baked ones over
 * a black ambient and the Mixed ones over a pass, while every light is Mixed all share the view
 * list (see box_lights_push); without it every light is realtime.
 */

#include "bake.h"
#include "../atmosphere/atmosphere.h"
#include <math.h>

/** Lights the light buffer of a scene holds at least */
#define BOX_LIGHTS_MIN_CAPACITY (16)

/** Buffer handle of the shared buffer that holds the default sun */
#define BOX_LIGHTS_DEFAULT_BUFFER (BOX_RENDER_BUFFER_LIGHT_FIRST)

/** Direction toward the default sun, in world space */
static const float box_lights_sun[3] = { 0.35f, 0.8f, 0.5f };

/** Linear color of the default sun */
static const float box_lights_sun_color[3] = { 1.0f, 0.96f, 0.9f };

/** Linear default ambient light from above */
static const float box_lights_sky[3] = { 0.18f, 0.22f, 0.3f };

/** Linear default ambient light from below */
static const float box_lights_ground[3] = { 0.08f, 0.07f, 0.06f };

/** Linear value of every display encoded channel value */
static float box_lights_srgb[256];

/** The lists of lights of a scene, by the modes they hold */
typedef enum box_light_list_kind_t {
    BOX_LIGHTS_VIEW,        /**< Lights of the views: every light, or without the Baked ones */
    BOX_LIGHTS_CAPTURE,     /**< Mixed and Baked lights, of captures over a black ambient */
    BOX_LIGHTS_MIXED,       /**< Mixed lights, of captures over a previous pass */
    BOX_LIGHTS_BAKED,       /**< Baked lights, whose direct light the projection adds */
    BOX_LIGHTS_LISTS        /**< Number of lists */
} box_light_list_kind_t;

/** A list of lights of a scene and what its light buffer holds on the backend */
typedef struct box_light_list_t {
    uint32_t buffer;        /**< Handle of the light buffer, 0 before it is created */
    uint32_t capacity;      /**< Lights the light buffer holds, 0 when the backend lacks it */
    uint32_t directional;   /**< Directional lights at the start of lights */
    ecs_vec_t lights;       /**< box_render_light_t of this frame */
    ecs_vec_t entities;     /**< ecs_entity_t, the entity of each light of this frame */
    ecs_vec_t modes;        /**< uint8_t, the LightBakeMode of each light of this frame */
    ecs_vec_t sent;         /**< box_render_light_t the light buffer holds */
} box_light_list_t;

/** A light of an atmosphere: its sun or its moon */
typedef struct box_light_atmos_light_t {
    bool found;             /**< The light was collected in this frame */
    float direction[3];     /**< Normalized direction toward the light */
    float transmittance[3]; /**< Transmittance from the ground altitude toward the light */
} box_light_atmos_light_t;

/** The Atmosphere of a scene in this frame */
typedef struct box_light_atmos_t {
    ecs_entity_t entity;    /**< Entity with the Atmosphere, 0 when the scene has none */
    const char *error;      /**< Why its values are invalid, NULL when they are valid */
    Atmosphere value;       /**< Its values */
    box_atmos_medium_t medium; /**< Its medium, set when it is valid */
    box_light_atmos_light_t sun; /**< The sun */
    box_light_atmos_light_t moon; /**< The moon */
} box_light_atmos_t;

/** The lights of one scene */
typedef struct box_light_scene_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    bool has_lights;        /**< The scene has a light entity, even one that lights nothing */
    bool has_ambient;       /**< The scene has an AmbientLight */
    bool baking;            /**< The scene has LightBake, so bake modes apply */
    bool split;             /**< Baking with a light that is not Mixed: the capture, mixed and
                             *   baked lists are kept, else the captures use the view list */
    bool default_sun;       /**< The view list holds the default sun attenuated by the
                             *   atmosphere of a scene without light entities */
    box_light_atmos_t atmos; /**< The atmosphere of the scene */
    float sky[3];           /**< Linear ambient light from above, summed */
    float ground[3];        /**< Linear ambient light from below, summed */
    box_light_list_t lists[BOX_LIGHTS_LISTS]; /**< The lists, by box_light_list_kind_t */
} box_light_scene_t;

/** Main thread state of the light extract */
typedef struct box_render_lights_t {
    ecs_query_t *directional;   /**< Entities with DirectionalLight */
    ecs_query_t *point;         /**< Entities with PointLight */
    ecs_query_t *spot;          /**< Entities with SpotLight */
    ecs_query_t *ambient;       /**< Entities with AmbientLight */
    ecs_query_t *bakes;         /**< Entities with LightBake */
    ecs_query_t *atmospheres;   /**< Entities with Atmosphere, NULL without box.atmosphere */
    ecs_vec_t scenes;           /**< box_light_scene_t */
    ecs_map_t lookup;           /**< Scene root plus one to the index of its scene plus one */
    ecs_vec_t free_buffers;     /**< uint32_t, light buffer handles to reuse */
    uint32_t next_buffer;       /**< Next light buffer handle */
    bool default_sent;          /**< The backend has the buffer of the default sun */
    const void *backend;        /**< Context of the backend the buffers were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;         /**< Generation of that backend, see box_render_backend_t */
    box_render_lights_stats_t stats; /**< Counters of the last extract */
} box_render_lights_t;

/** Fill the conversion table of display encoded channels, once per process.
 */
static void box_lights_table_init(void)
{
    int32_t i;
    for (i = 0; i < 256; i ++) {
        float c = (float)i / 255.0f;
        box_lights_srgb[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    }
}

/** Prepare an empty list of lights.
 *
 * @param list The list
 */
static void box_lights_list_init(
    box_light_list_t *list)
{
    ecs_os_memset_t(list, 0, box_light_list_t);
    ecs_vec_init_t(NULL, &list->lights, box_render_light_t, 0);
    ecs_vec_init_t(NULL, &list->entities, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &list->modes, uint8_t, 0);
    ecs_vec_init_t(NULL, &list->sent, box_render_light_t, 0);
}

/** Free a list of lights; its light buffer is released by the caller.
 *
 * @param list The list
 */
static void box_lights_list_fini(
    box_light_list_t *list)
{
    ecs_vec_fini_t(NULL, &list->lights, box_render_light_t);
    ecs_vec_fini_t(NULL, &list->entities, ecs_entity_t);
    ecs_vec_fini_t(NULL, &list->modes, uint8_t);
    ecs_vec_fini_t(NULL, &list->sent, box_render_light_t);
}

/** Empty a list of lights for a new frame, keeping its light buffer.
 *
 * @param list The list
 */
static void box_lights_list_clear(
    box_light_list_t *list)
{
    ecs_vec_clear(&list->lights);
    ecs_vec_clear(&list->entities);
    ecs_vec_clear(&list->modes);
    list->directional = 0;
}

/** Copy the lights of a list into an empty one.
 *
 * @param dst The empty list
 * @param src The list copied
 */
static void box_lights_list_copy(
    box_light_list_t *dst,
    const box_light_list_t *src)
{
    int32_t count = ecs_vec_count(&src->lights);
    ecs_vec_set_count_t(NULL, &dst->lights, box_render_light_t, count);
    ecs_vec_set_count_t(NULL, &dst->entities, ecs_entity_t, count);
    ecs_vec_set_count_t(NULL, &dst->modes, uint8_t, count);
    if (count) {
        ecs_os_memcpy_n(ecs_vec_first(&dst->lights), ecs_vec_first(&src->lights),
            box_render_light_t, count);
        ecs_os_memcpy_n(ecs_vec_first(&dst->entities), ecs_vec_first(&src->entities),
            ecs_entity_t, count);
        ecs_os_memcpy_n(ecs_vec_first(&dst->modes), ecs_vec_first(&src->modes), uint8_t, count);
    }

    dst->directional = src->directional;
}

/** Find the lights of a scene.
 *
 * @param l The state
 * @param scene The scene root
 * @return Index of the scene, -1 when it has no entry
 */
static int32_t box_lights_find(
    box_render_lights_t *l,
    ecs_entity_t scene)
{
    ecs_map_val_t *index = ecs_map_get(&l->lookup, scene + 1);

    return index ? (int32_t)*index - 1 : -1;
}

/** Find the lights of a scene, adding an entry when it has none.
 *
 * @param l The state
 * @param scene The scene root
 * @return The scene, valid until the next entry is added
 */
static box_light_scene_t* box_lights_ensure(
    box_render_lights_t *l,
    ecs_entity_t scene)
{
    int32_t k, index = box_lights_find(l, scene);
    if (index == -1) {
        index = ecs_vec_count(&l->scenes);
        box_light_scene_t *s = ecs_vec_append_t(NULL, &l->scenes, box_light_scene_t);
        ecs_os_memset_t(s, 0, box_light_scene_t);
        s->scene = scene;
        for (k = 0; k < BOX_LIGHTS_LISTS; k ++) {
            box_lights_list_init(&s->lists[k]);
        }

        ecs_map_insert(&l->lookup, scene + 1, (ecs_map_val_t)index + 1);
    }

    return ecs_vec_get_t(&l->scenes, box_light_scene_t, index);
}

/** Forget the lights of the previous frame of every scene.
 *
 * @param l The state
 */
static void box_lights_reset(
    box_render_lights_t *l)
{
    int32_t i, k;
    for (i = 0; i < ecs_vec_count(&l->scenes); i ++) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        for (k = 0; k < BOX_LIGHTS_LISTS; k ++) {
            box_lights_list_clear(&s->lists[k]);
        }

        s->has_lights = false;
        s->baking = false;
        s->split = false;
        s->default_sun = false;
        ecs_os_memset_t(&s->atmos, 0, box_light_atmos_t);
        s->has_ambient = false;
        glm_vec3_zero(s->sky);
        glm_vec3_zero(s->ground);
    }
}

/** Write the linear color of a light, scaled by its intensity and pi.
 *
 * @param rgb Color of the light, NULL for white
 * @param intensity Radiance scale
 * @param out Receives the color
 */
static void box_lights_color(
    const Rgb *rgb,
    float intensity,
    float *out)
{
    float scale = intensity * GLM_PIf;
    out[0] = (rgb ? box_lights_srgb[rgb->r] : 1.0f) * scale;
    out[1] = (rgb ? box_lights_srgb[rgb->g] : 1.0f) * scale;
    out[2] = (rgb ? box_lights_srgb[rgb->b] : 1.0f) * scale;
}

/** Write the normalized local +Z axis of a transform.
 *
 * @param transform The world transform, NULL for the identity
 * @param out Receives the axis
 */
static void box_lights_axis(
    const WorldTransform *transform,
    float *out)
{
    if (transform) {
        glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[2]), out);
    } else {
        glm_vec3_copy((vec3){0, 0, 1}, out);
    }

    if (glm_vec3_norm(out) > 0) {
        glm_vec3_normalize(out);
    } else {
        glm_vec3_copy((vec3){0, 0, 1}, out);
    }
}

/** Write a light with a position, a range and no cone.
 *
 * @param transform World transform of the light, NULL for the origin
 * @param rgb Color of the light, NULL for white
 * @param intensity Radiance scale
 * @param range Distance at which the light reaches zero
 * @param out Receives the light
 */
static void box_lights_point(
    const WorldTransform *transform,
    const Rgb *rgb,
    float intensity,
    float range,
    box_render_light_t *out)
{
    if (transform) {
        glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[3]), out->position);
    } else {
        glm_vec3_zero(out->position);
    }

    out->range = range;
    box_lights_color(rgb, intensity, out->color);
    glm_vec3_copy((vec3){0, 0, 1}, out->direction);
    out->spot_scale = 0.0f;
    out->spot_offset = 1.0f;
}

/** Append a light to a list.
 *
 * @param list The list
 * @param e The light entity
 * @param mode The bake mode of the light
 * @param light The light, directional when its range is 0
 */
static void box_lights_append(
    box_light_list_t *list,
    ecs_entity_t e,
    LightBakeMode mode,
    const box_render_light_t *light)
{
    *ecs_vec_append_t(NULL, &list->lights, box_render_light_t) = *light;
    *ecs_vec_append_t(NULL, &list->entities, ecs_entity_t) = e;
    *ecs_vec_append_t(NULL, &list->modes, uint8_t) = (uint8_t)mode;
    list->directional += light->range > 0 ? 0u : 1u;
}

/** Append a light to the lists of its scene that its bake mode puts it in. Without LightBake
 * that is the view list. With it the view list takes every light but the Baked ones.
 *
 * The first light that is not Mixed splits the capture lists off the view list, which until
 * then held Mixed lights only. The capture list takes every light but the Realtime ones, the
 * mixed list the Mixed ones and the baked list the Baked ones.
 *
 * @param s The scene
 * @param e The light entity
 * @param mode The bake mode of the light
 * @param light The light, directional when its range is 0
 */
static void box_lights_push(
    box_light_scene_t *s,
    ecs_entity_t e,
    LightBakeMode mode,
    const box_render_light_t *light)
{
    box_light_list_t *lists = s->lists;
    if (s->baking && mode != LightBakeModeMixed && !s->split) {
        box_lights_list_copy(&lists[BOX_LIGHTS_CAPTURE], &lists[BOX_LIGHTS_VIEW]);
        box_lights_list_copy(&lists[BOX_LIGHTS_MIXED], &lists[BOX_LIGHTS_VIEW]);
        s->split = true;
    }

    if (!s->baking || mode != LightBakeModeBaked) {
        box_lights_append(&lists[BOX_LIGHTS_VIEW], e, mode, light);
    }

    if (s->split && mode != LightBakeModeRealtime) {
        box_lights_append(&lists[BOX_LIGHTS_CAPTURE], e, mode, light);
        box_lights_append(&lists[mode == LightBakeModeMixed ? BOX_LIGHTS_MIXED : BOX_LIGHTS_BAKED],
            e, mode, light);
    }
}

/** Write the default sun.
 *
 * @param out Receives the light
 */
static void box_lights_default_sun(
    box_render_light_t *out)
{
    ecs_os_memset_t(out, 0, box_render_light_t);
    glm_vec3_normalize_to(ECS_CONST_CAST(float*, box_lights_sun), out->position);
    glm_vec3_scale(ECS_CONST_CAST(float*, box_lights_sun_color), GLM_PIf, out->color);
    glm_vec3_copy((vec3){0, 0, 1}, out->direction);
    out->spot_offset = 1.0f;
}

/** Attenuate the sun or moon of an atmosphere by the transmittance toward it and its fade at
 * the horizon, and remember its direction and transmittance for the AtmosphereStatus.
 *
 * @param a The atmosphere, valid
 * @param target The sun or moon of the atmosphere
 * @param radius Angular radius of the disk of the light in degrees
 * @param light The directional light, its color is attenuated
 */
static void box_lights_atmos_light(
    box_light_atmos_t *a,
    box_light_atmos_light_t *target,
    float radius,
    box_render_light_t *light)
{
    float fade;
    int32_t c;
    target->found = true;
    glm_vec3_copy(light->position, target->direction);
    box_atmos_transmittance(&a->medium, a->value.ground_altitude, target->direction[1],
        target->transmittance);
    fade = box_atmos_horizon_fade(target->direction[1], radius);
    for (c = 0; c < 3; c ++) {
        light->color[c] *= target->transmittance[c] * fade;
    }
}

/** Attenuate a directional light that is the sun or the moon of the atmosphere of its scene.
 *
 * Without Atmosphere.sun the sun is the first directional light of the scene that is not the
 * moon.
 *
 * @param s The scene of the light
 * @param e The light entity
 * @param light The light, its color is attenuated
 */
static void box_lights_atmos_directional(
    box_light_scene_t *s,
    ecs_entity_t e,
    box_render_light_t *light)
{
    box_light_atmos_t *a = &s->atmos;
    const Atmosphere *v = &a->value;
    if (!a->entity || a->error) {
        goto done;
    }

    if (!a->sun.found && (v->sun ? e == v->sun : e != v->moon)) {
        box_lights_atmos_light(a, &a->sun, v->sun_radius, light);
    } else if (!a->moon.found && v->moon && e == v->moon) {
        box_lights_atmos_light(a, &a->moon, v->moon_radius, light);
    }

done:
    return;
}

/** Collect the lights one query matches into the scenes they belong to.
 *
 * The scene of a light comes from the Scene(self|up) field of the query.
 *
 * @param world The world
 * @param l The state
 * @param q The query, with the light component, WorldTransform, Rgb and Scene
 * @param kind 0 for directional, 1 for point and 2 for spot lights
 */
static void box_lights_collect(
    ecs_world_t *world,
    box_render_lights_t *l,
    ecs_query_t *q,
    int32_t kind)
{
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        const WorldTransform *transforms = ecs_field(&it, WorldTransform, 1);
        const Rgb *colors = ecs_field(&it, Rgb, 2);
        box_light_scene_t *s = NULL;
        ecs_entity_t scene = 0;
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const WorldTransform *transform = transforms ? &transforms[i] : NULL;
            const Rgb *rgb = colors ? &colors[i] : NULL;
            ecs_entity_t member = box_render_scene_field(&it, 3, i);
            if (!s || member != scene) {
                scene = member;
                s = box_lights_ensure(l, scene);
            }

            s->has_lights = true;
            if (kind == 0) {
                const DirectionalLight *d = &ecs_field(&it, DirectionalLight, 0)[i];
                box_render_light_t out;
                box_lights_point(NULL, rgb, d->intensity, 0, &out);
                box_lights_axis(transform, out.position);
                glm_vec3_negate(out.position);
                box_lights_atmos_directional(s, it.entities[i], &out);
                if (d->intensity > 0) {
                    box_lights_push(s, it.entities[i], d->bake, &out);
                }
            } else if (kind == 1) {
                const PointLight *p = &ecs_field(&it, PointLight, 0)[i];
                if (p->intensity > 0 && p->range > 0) {
                    box_render_light_t out;
                    box_lights_point(transform, rgb, p->intensity, p->range, &out);
                    box_lights_push(s, it.entities[i], p->bake, &out);
                }
            } else {
                const SpotLight *sp = &ecs_field(&it, SpotLight, 0)[i];
                if (sp->intensity > 0 && sp->range > 0) {
                    float outer = glm_clamp(sp->outer_angle, 0.01f, 89.9f);
                    float inner = glm_clamp(sp->inner_angle, 0.0f, outer);
                    float cos_outer = cosf(glm_rad(outer));
                    float cos_inner = cosf(glm_rad(inner));
                    box_render_light_t out;
                    box_lights_point(transform, rgb, sp->intensity, sp->range, &out);
                    box_lights_axis(transform, out.direction);
                    out.spot_scale = 1.0f / glm_max(cos_inner - cos_outer, 1e-4f);
                    out.spot_offset = -cos_outer * out.spot_scale;
                    box_lights_push(s, it.entities[i], sp->bake, &out);
                }
            }
        }
    }
}

/** Give the atmosphere of every scene in this frame to its scene, the first one of a scene
 * that has several.
 *
 * @param world The world
 * @param l The state
 */
static void box_lights_collect_atmospheres(
    ecs_world_t *world,
    box_render_lights_t *l)
{
    if (l->atmospheres) {
        ecs_iter_t it = ecs_query_iter(world, l->atmospheres);
        while (ecs_query_next(&it)) {
            const Atmosphere *values = ecs_field(&it, Atmosphere, 0);
            int32_t i;
            for (i = 0; i < it.count; i ++) {
                box_light_scene_t *s = box_lights_ensure(l, box_render_scene_field(&it, 1, i));
                box_light_atmos_t *a = &s->atmos;
                if (!a->entity) {
                    a->entity = it.entities[i];
                    a->value = values[i];
                    a->error = box_atmos_error(&values[i]);
                    if (!a->error) {
                        box_atmos_medium(&values[i], &a->medium);
                    }
                }
            }
        }
    }
}

/** Light the scenes with a valid atmosphere, no light entities and no Atmosphere.sun by the
 * default sun of their atmosphere, kept in their view list.
 *
 * @param l The state
 */
static void box_lights_atmos_defaults(
    box_render_lights_t *l)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&l->scenes); i ++) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        box_light_atmos_t *a = &s->atmos;
        if (a->entity && !a->error && !s->has_lights && !a->value.sun) {
            box_render_light_t sun;
            box_lights_default_sun(&sun);
            box_lights_atmos_light(a, &a->sun, a->value.sun_radius, &sun);
            box_lights_push(s, a->entity, LightBakeModeMixed, &sun);
            s->default_sun = true;
        }
    }
}

/** Write the transmittances and the error of the atmosphere of every scene into its
 * AtmosphereStatus when they changed.
 *
 * @param it The iterator of the system
 * @param l The state
 */
static void box_lights_atmos_status(
    ecs_iter_t *it,
    box_render_lights_t *l)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&l->scenes); i ++) {
        const box_light_atmos_t *a = &ecs_vec_get_t(&l->scenes, box_light_scene_t, i)->atmos;
        const AtmosphereStatus *status = a->entity
            ? ecs_get(it->world, a->entity, AtmosphereStatus)
            : NULL;
        if (!status) {
            continue;
        }

        AtmosphereStatus value = *status;
        glm_vec3_zero(value.sun_transmittance);
        glm_vec3_zero(value.moon_transmittance);
        value.moon_illumination = 0.0f;
        if (!a->error && a->sun.found) {
            glm_vec3_copy(ECS_CONST_CAST(float*, a->sun.transmittance), value.sun_transmittance);
        }

        if (!a->error && a->moon.found) {
            glm_vec3_copy(ECS_CONST_CAST(float*, a->moon.transmittance),
                value.moon_transmittance);
        }

        if (!a->error && a->sun.found && a->moon.found) {
            value.moon_illumination = box_atmos_moon_illumination(a->sun.direction,
                a->moon.direction);
        }

        bool same_error = a->error
            ? status->error && !strcmp(status->error, a->error)
            : !status->error;
        if (!same_error || value.moon_illumination != status->moon_illumination ||
            ecs_os_memcmp(value.sun_transmittance, status->sun_transmittance, sizeof(vec3)) ||
            ecs_os_memcmp(value.moon_transmittance, status->moon_transmittance, sizeof(vec3)))
        {
            value.error = ECS_CONST_CAST(char*, a->error);
            ecs_set_ptr(it->stage, a->entity, AtmosphereStatus, &value);
        }
    }
}

/** Sum the AmbientLight of every scene.
 *
 * @param world The world
 * @param l The state
 */
static void box_lights_collect_ambient(
    ecs_world_t *world,
    box_render_lights_t *l)
{
    ecs_iter_t it = ecs_query_iter(world, l->ambient);
    while (ecs_query_next(&it)) {
        const AmbientLight *a = ecs_field(&it, AmbientLight, 0);
        int32_t i, c;
        for (i = 0; i < it.count; i ++) {
            box_light_scene_t *s = box_lights_ensure(l, box_render_scene_field(&it, 1, i));
            const uint8_t sky[3] = { a[i].sky.r, a[i].sky.g, a[i].sky.b };
            const uint8_t ground[3] = { a[i].ground.r, a[i].ground.g, a[i].ground.b };
            float intensity = glm_max(a[i].intensity, 0.0f);
            s->has_ambient = true;
            for (c = 0; c < 3; c ++) {
                s->sky[c] += box_lights_srgb[sky[c]] * intensity;
                s->ground[c] += box_lights_srgb[ground[c]] * intensity;
            }
        }
    }
}

/** Mark the scenes that have LightBake in this frame, before their lights are collected.
 *
 * @param world The world
 * @param l The state
 */
static void box_lights_collect_bakes(
    ecs_world_t *world,
    box_render_lights_t *l)
{
    ecs_iter_t it = ecs_query_iter(world, l->bakes);
    while (ecs_query_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_lights_ensure(l, box_render_scene_field(&it, 1, i))->baking = true;
        }
    }
}

/** Get a light buffer handle.
 *
 * @param l The state
 * @return The handle
 */
static uint32_t box_lights_buffer_new(
    box_render_lights_t *l)
{
    uint32_t result;
    if (ecs_vec_count(&l->free_buffers)) {
        result = *ecs_vec_last_t(&l->free_buffers, uint32_t);
        ecs_vec_remove_last(&l->free_buffers);
    } else {
        result = l->next_buffer ++;
    }

    return result;
}

/** Create a light buffer on the backend.
 *
 * @param p The packet
 * @param handle Handle of the buffer
 * @param capacity Lights the buffer holds
 */
static void box_lights_buffer_create(
    box_render_packet_t *p,
    uint32_t handle,
    uint32_t capacity)
{
    box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
        BOX_RENDER_CREATE, handle, sizeof(box_render_buffer_desc_t));
    desc->size = capacity * (uint32_t)sizeof(box_render_light_t);
    desc->usage = BOX_RENDER_BUFFER_STORAGE;
}

/** Send the buffer of the default sun when the backend does not have it.
 *
 * @param l The state
 * @param p The packet
 */
static void box_lights_send_default(
    box_render_lights_t *l,
    box_render_packet_t *p)
{
    box_render_light_t sun;
    if (l->default_sent) {
        goto done;
    }

    box_lights_default_sun(&sun);
    box_lights_buffer_create(p, BOX_LIGHTS_DEFAULT_BUFFER, 1);
    ecs_os_memcpy_t(box_render_packet_upload(p, BOX_LIGHTS_DEFAULT_BUFFER, 0,
        sizeof(box_render_light_t)), &sun, box_render_light_t);
    l->default_sent = true;
done:
    return;
}

/** Send the light buffer of a list when the backend lacks it or it changed.
 *
 * @param l The state
 * @param s The list
 * @param p The packet
 */
static void box_lights_send_list(
    box_render_lights_t *l,
    box_light_list_t *s,
    box_render_packet_t *p)
{
    int32_t count = ecs_vec_count(&s->lights);
    uint32_t size = (uint32_t)count * (uint32_t)sizeof(box_render_light_t);
    if (!s->buffer) {
        s->buffer = box_lights_buffer_new(l);
    }

    if (!s->capacity || (uint32_t)count > s->capacity) {
        s->capacity = BOX_LIGHTS_MIN_CAPACITY;
        while (s->capacity < (uint32_t)count) {
            s->capacity *= 2;
        }

        box_lights_buffer_create(p, s->buffer, s->capacity);
        ecs_vec_clear(&s->sent);
    } else if (count == ecs_vec_count(&s->sent) &&
        !ecs_os_memcmp(ecs_vec_first(&s->lights), ecs_vec_first(&s->sent), (ecs_size_t)size))
    {
        goto done;
    }

    if (count) {
        ecs_os_memcpy(box_render_packet_upload(p, s->buffer, 0, size),
            ecs_vec_first(&s->lights), (ecs_size_t)size);
        l->stats.upload_bytes += size;
    }

    ecs_vec_set_count_t(NULL, &s->sent, box_render_light_t, count);
    if (count) {
        ecs_os_memcpy(ecs_vec_first(&s->sent), ecs_vec_first(&s->lights), (ecs_size_t)size);
    }

done:
    return;
}

/** Destroy the light buffer of a list when it has one.
 *
 * @param l The state
 * @param list The list
 * @param p The packet, receives the destroy of the light buffer
 */
static void box_lights_release(
    box_render_lights_t *l,
    box_light_list_t *list,
    box_render_packet_t *p)
{
    if (list->buffer) {
        box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY, list->buffer, 0);
        *ecs_vec_append_t(NULL, &l->free_buffers, uint32_t) = list->buffer;
        list->buffer = 0;
        list->capacity = 0;
        ecs_vec_clear(&list->sent);
    }
}

/** Remove the entry of a scene, moving the last entry into its place.
 *
 * @param l The state
 * @param p The packet, receives the destroy of the light buffers
 * @param index Index of the scene
 */
static void box_lights_remove(
    box_render_lights_t *l,
    box_render_packet_t *p,
    int32_t index)
{
    box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, index);
    int32_t k, last = ecs_vec_count(&l->scenes) - 1;
    ecs_map_remove(&l->lookup, s->scene + 1);
    for (k = 0; k < BOX_LIGHTS_LISTS; k ++) {
        box_lights_release(l, &s->lists[k], p);
        box_lights_list_fini(&s->lists[k]);
    }

    if (index != last) {
        *s = *ecs_vec_get_t(&l->scenes, box_light_scene_t, last);
        ecs_map_val_t *moved = ecs_map_get(&l->lookup, s->scene + 1);
        *moved = (ecs_map_val_t)index + 1;
    }

    ecs_vec_remove_last(&l->scenes);
}

/** Send the light buffers of the scenes and drop the scenes that lost their lights, ambient
 * and LightBake. The capture, mixed and baked lists have a buffer only while they are split
 * off, the baked list only while it holds a light.
 *
 * @param l The state
 * @param p The packet
 */
static void box_lights_send(
    box_render_lights_t *l,
    box_render_packet_t *p)
{
    int32_t i, k;
    for (i = ecs_vec_count(&l->scenes) - 1; i >= 0; i --) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        if (!s->has_lights && !s->has_ambient && !s->baking && !s->atmos.entity) {
            box_lights_remove(l, p, i);
            continue;
        }

        for (k = 0; k < BOX_LIGHTS_LISTS; k ++) {
            box_light_list_t *list = &s->lists[k];
            bool used = (s->has_lights || s->default_sun) && (k == BOX_LIGHTS_VIEW ||
                (s->split && (k != BOX_LIGHTS_BAKED || ecs_vec_count(&list->lights))));
            if (used) {
                box_lights_send_list(l, list, p);
            } else {
                box_lights_release(l, list, p);
            }
        }

        if (s->has_lights) {
            l->stats.scenes ++;
            l->stats.lights += ecs_vec_count(&s->lists[BOX_LIGHTS_VIEW].lights);
        }
    }
}

/** Give every view of the packet the lights and the ambient light of its scene.
 *
 * @param world The world
 * @param l The state
 * @param p The packet
 */
static void box_lights_views(
    ecs_world_t *world,
    box_render_lights_t *l,
    box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&p->views);
    for (i = 0; i < count; i ++) {
        box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        box_render_view_lights_t *out = &v->lights;
        const View *view = v->view && ecs_is_alive(world, v->view)
            ? ecs_get(world, v->view, View)
            : NULL;
        int32_t index = box_lights_find(l, v->scene);
        box_light_scene_t *s = index != -1
            ? ecs_vec_get_t(&l->scenes, box_light_scene_t, index)
            : NULL;
        out->enabled = view ? view->lighting.enabled : true;
        const box_light_list_t *list = s ? &s->lists[BOX_LIGHTS_VIEW] : NULL;
        if (s && (s->has_lights || s->default_sun)) {
            out->buffer = list->buffer;
            out->directional = list->directional;
            out->count = (uint32_t)ecs_vec_count(&list->lights);
            if (list->directional) {
                box_render_light_t *sun = ecs_vec_first_t(&list->lights, box_render_light_t);
                glm_vec3_copy(sun->position, out->sun);
                glm_vec3_copy(sun->color, out->sun_color);
            }
        } else {
            box_lights_send_default(l, p);
            out->buffer = BOX_LIGHTS_DEFAULT_BUFFER;
            out->directional = 1;
            out->count = 1;
            glm_vec3_copy(ECS_CONST_CAST(float*, box_lights_sun), out->sun);
            glm_vec3_scale(ECS_CONST_CAST(float*, box_lights_sun_color), GLM_PIf,
                out->sun_color);
        }

        if (s && s->has_ambient) {
            glm_vec3_copy(s->sky, out->ambient_sky);
            glm_vec3_copy(s->ground, out->ambient_ground);
        } else {
            glm_vec3_copy(ECS_CONST_CAST(float*, box_lights_sky), out->ambient_sky);
            glm_vec3_copy(ECS_CONST_CAST(float*, box_lights_ground), out->ambient_ground);
        }

        out->ambient_sky[3] = 0.0f;
        out->ambient_ground[3] = 0.0f;
    }
}

/** Forget what an earlier backend received, so every buffer is created again.
 *
 * @param l The state
 */
static void box_lights_reset_backend(
    box_render_lights_t *l)
{
    int32_t i, k;
    for (i = 0; i < ecs_vec_count(&l->scenes); i ++) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        for (k = 0; k < BOX_LIGHTS_LISTS; k ++) {
            s->lists[k].capacity = 0;
            ecs_vec_clear(&s->lists[k].sent);
        }
    }

    l->default_sent = false;
}

/** Collect the lights of every scene, send the changed light buffers and light the views.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractLights(
    ecs_iter_t *it)
{
    box_render_lights_t *l = it->ctx;
    ecs_world_t *world = it->world;
    box_render_packet_t *p = box_render_packet(world);
    const box_render_backend_t *backend = box_render_get_backend(world);
    if (backend->ctx != l->backend || backend->render != l->render ||
        backend->generation != l->generation)
    {
        box_lights_reset_backend(l);
        l->backend = backend->ctx;
        l->render = backend->render;
        l->generation = backend->generation;
    }

    ecs_os_memset_t(&l->stats, 0, box_render_lights_stats_t);
    box_lights_reset(l);
    box_lights_collect_bakes(world, l);
    box_lights_collect_atmospheres(world, l);
    box_lights_collect(world, l, l->directional, 0);
    box_lights_collect(world, l, l->point, 1);
    box_lights_collect(world, l, l->spot, 2);
    box_lights_atmos_defaults(l);
    box_lights_collect_ambient(world, l);
    box_lights_send(l, p);
    box_lights_views(world, l, p);
    box_lights_atmos_status(it, l);
}

/** Free the state of the light extract.
 *
 * @param ctx The state
 */
static void box_lights_free(
    void *ctx)
{
    box_render_lights_t *l = ctx;
    int32_t i, k;
    for (i = 0; i < ecs_vec_count(&l->scenes); i ++) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        for (k = 0; k < BOX_LIGHTS_LISTS; k ++) {
            box_lights_list_fini(&s->lists[k]);
        }
    }

    ecs_vec_fini_t(NULL, &l->scenes, box_light_scene_t);
    ecs_vec_fini_t(NULL, &l->free_buffers, uint32_t);
    ecs_map_fini(&l->lookup);
    ecs_os_free(l);
}

/** Create a query of a component and the scene of its entity.
 *
 * @param world The world
 * @param component The component
 * @return The query, with the component and Scene(self|up)
 */
static ecs_query_t* box_lights_scene_query(
    ecs_world_t *world,
    ecs_entity_t component)
{
    return ecs_query(world, {
        .terms = {
            { .id = component, .inout = EcsIn },
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

/** Create the query of one kind of light.
 *
 * @param world The world
 * @param light The light component
 * @return The query, with the light, WorldTransform, Rgb and Scene(self|up)
 */
static ecs_query_t* box_lights_query(
    ecs_world_t *world,
    ecs_entity_t light)
{
    return ecs_query(world, {
        .terms = {
            { .id = light, .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Rgb), .inout = EcsIn, .oper = EcsOptional },
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

void box_render_lights_init(
    ecs_world_t *world)
{
    box_render_lights_t *l = ecs_os_calloc_t(box_render_lights_t);
    box_lights_table_init();
    ecs_vec_init_t(NULL, &l->scenes, box_light_scene_t, 0);
    ecs_vec_init_t(NULL, &l->free_buffers, uint32_t, 0);
    ecs_map_init(&l->lookup, NULL);
    l->next_buffer = BOX_LIGHTS_DEFAULT_BUFFER + 1;
    l->directional = box_lights_query(world, ecs_id(DirectionalLight));
    l->point = box_lights_query(world, ecs_id(PointLight));
    l->spot = box_lights_query(world, ecs_id(SpotLight));
    l->ambient = box_lights_scene_query(world, ecs_id(AmbientLight));
    l->bakes = box_lights_scene_query(world, ecs_id(LightBake));

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractLights" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractLights,
        .ctx = l,
        .ctx_free = box_lights_free
    });
}

void box_render_lights_stats(
    const ecs_world_t *world,
    box_render_lights_stats_t *stats)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractLights");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    ecs_os_memset_t(stats, 0, box_render_lights_stats_t);
    if (s && s->ctx) {
        *stats = ((const box_render_lights_t*)s->ctx)->stats;
    }
}

const void* box_render_lights_state(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractLights");
    const ecs_system_t *sys = system ? ecs_system_get(world, system) : NULL;

    return sys ? sys->ctx : NULL;
}

void box_render_lights_atmosphere(
    ecs_world_t *world)
{
    box_render_lights_t *l = ECS_CONST_CAST(box_render_lights_t*,
        box_render_lights_state(world));
    if (l && !l->atmospheres) {
        l->atmospheres = box_lights_scene_query(world, ecs_id(Atmosphere));
    }
}

/** Write the buffer, the counts and the first directional light of a list.
 *
 * @param list The list
 * @param out Receives them, the ambient is kept
 */
static void box_lights_list_view(
    const box_light_list_t *list,
    box_render_view_lights_t *out)
{
    out->buffer = list->buffer;
    out->directional = list->directional;
    out->count = (uint32_t)ecs_vec_count(&list->lights);
    glm_vec3_zero(out->sun);
    glm_vec3_zero(out->sun_color);
    if (list->directional) {
        const box_render_light_t *sun = ecs_vec_first_t(&list->lights, box_render_light_t);
        glm_vec3_copy(ECS_CONST_CAST(float*, sun->position), out->sun);
        glm_vec3_copy(ECS_CONST_CAST(float*, sun->color), out->sun_color);
    }
}

void box_bake_scene_lights(
    const void *state,
    ecs_entity_t scene,
    const box_render_view_lights_t *view,
    box_bake_lights_t *out)
{
    const box_render_lights_t *l = state;
    int32_t index = l ? box_lights_find(ECS_CONST_CAST(box_render_lights_t*, l), scene) : -1;
    const box_light_scene_t *s = index != -1
        ? ecs_vec_get_t(&l->scenes, box_light_scene_t, index)
        : NULL;
    const box_light_list_t *capture = NULL;
    ecs_os_memset_t(out, 0, box_bake_lights_t);
    out->first = *view;
    out->later = *view;
    if (!s || !s->baking) {
        goto done;
    }

    capture = &s->lists[s->split ? BOX_LIGHTS_CAPTURE : BOX_LIGHTS_VIEW];
    out->count = ecs_vec_count(&capture->lights);
    out->lights = ecs_vec_first(&capture->lights);
    out->entities = ecs_vec_first(&capture->entities);
    out->modes = ecs_vec_first(&capture->modes);
    if (s->has_lights && s->split) {
        const box_light_list_t *baked = &s->lists[BOX_LIGHTS_BAKED];
        box_lights_list_view(capture, &out->first);
        box_lights_list_view(&s->lists[BOX_LIGHTS_MIXED], &out->later);
        out->baked = baked->buffer;
        out->baked_count = (uint32_t)ecs_vec_count(&baked->lights);
        out->baked_directional = baked->directional;
    }

done:
    return;
}

void box_bake_view_lights(
    const void *state,
    ecs_entity_t scene,
    box_bake_light_set_t *out)
{
    const box_render_lights_t *l = state;
    int32_t index = l ? box_lights_find(ECS_CONST_CAST(box_render_lights_t*, l), scene) : -1;
    const box_light_scene_t *s = index != -1
        ? ecs_vec_get_t(&l->scenes, box_light_scene_t, index)
        : NULL;
    ecs_os_memset_t(out, 0, box_bake_light_set_t);
    if (s && s->has_lights) {
        const box_light_list_t *list = &s->lists[BOX_LIGHTS_VIEW];
        out->count = ecs_vec_count(&list->lights);
        out->lights = ecs_vec_first(&list->lights);
        out->entities = ecs_vec_first(&list->entities);
        out->modes = ecs_vec_first(&list->modes);
    }
}
