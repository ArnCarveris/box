/**
 * @file render/lights.c
 * @brief Collects the lights of every scene into light buffers and gives every view its lights.
 *
 * Lights are gathered per scene each frame, directional first, a buffer uploaded on change. A
 * scene without lights gets the default sun, without AmbientLight the default ambient; an
 * Atmosphere attenuates its sun and moon, else a TimeOfDay its own by the fallback.
 *
 * With LightBake views get the Mixed and Realtime lights, captures the Mixed and Baked ones over
 * a black ambient and the Mixed ones over a pass, while every light is Mixed all share the view
 * list (see box_lights_push); shadowed spot lights of the view list get atlas tiles.
 */

#include "bake.h"
#include "../atmosphere/atmosphere.h"
#include <float.h>
#include <math.h>

/** Lights the light buffer of a scene holds at least */
#define BOX_LIGHTS_MIN_CAPACITY (16)

/** Buffer handle of the shared buffer that holds the default sun */
#define BOX_LIGHTS_DEFAULT_BUFFER (BOX_RENDER_BUFFER_LIGHT_FIRST)

/** Aspect ratio the frustum of a camera that follows its surface is assumed to have at most
 * when the lights it sees are picked for shadow tiles, so a light it may see is never left out */
#define BOX_LIGHTS_SHADOW_ASPECT (4.0f)

/** Direction toward the default sun, in world space */
static const float box_lights_sun[3] = { 0.35f, 0.8f, 0.5f };

/** Linear color of the default sun, which has intensity pi so it lights a facing white surface
 * at about its base color */
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
    float color[3];         /**< Color of the light before the transmittance */
    float transmittance[3]; /**< Transmittance from the ground altitude toward the light */
    float fade;             /**< Fade of its disk at the horizon */
    float attenuated[3];    /**< Color times the transmittance and the fade, for the moon also
                             *   its illumination */
} box_light_atmos_light_t;

/** The Atmosphere of a scene in this frame */
typedef struct box_light_atmos_t {
    ecs_entity_t entity;    /**< Entity with the Atmosphere, 0 when the scene has none */
    const char *error;      /**< Why its values are invalid, NULL when they are valid */
    Atmosphere value;       /**< Its values */
    box_atmos_medium_t medium; /**< Its medium, set when it is valid */
    box_light_atmos_light_t sun; /**< The sun */
    box_light_atmos_light_t moon; /**< The moon */
    float moon_illumination; /**< Lit fraction of the moon, 0 without a sun or a moon */
} box_light_atmos_t;

/** A shadow casting spot light of the view list of a scene in this frame */
typedef struct box_light_spot_t {
    ecs_entity_t entity;    /**< The light entity */
    int32_t index;          /**< Index of the light in the view list */
    int32_t resolution;     /**< SpotLight.shadow_resolution */
    float outer;            /**< Outer half angle in degrees */
} box_light_spot_t;

/** The shadow atlas of a scene and what the backend holds of it */
typedef struct box_light_atlas_t {
    ShadowAtlas settings;   /**< ShadowAtlas of the scene this frame, resolved */
    bool found;             /**< The scene has a ShadowAtlas this frame */
    uint32_t handle;        /**< Handle of the atlas and its tile buffer, 0 while it has none */
    int32_t resolution;     /**< Edge the backend has the atlas at, 0 when it lacks it */
    bool buffer;            /**< The backend has the tile buffer */
    ecs_vec_t records;      /**< box_render_shadow_record_t of this frame */
    ecs_vec_t tiles;        /**< box_render_shadow_tile_t of this frame, as records */
    ecs_vec_t sent;         /**< box_render_shadow_record_t the tile buffer holds */
} box_light_atlas_t;

/** The TimeOfDay of a scene in this frame */
typedef struct box_light_tod_t {
    ecs_entity_t entity;    /**< Entity with the TimeOfDay, 0 when the scene has none */
    ecs_entity_t sun;       /**< TimeOfDay.sun, 0 for the first directional light */
    ecs_entity_t moon;      /**< TimeOfDay.moon, 0 for none */
    bool sun_found;         /**< The sun was collected in this frame */
    bool moon_found;        /**< The moon was collected in this frame */
} box_light_tod_t;

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
    box_light_tod_t tod;    /**< The time of day of the scene */
    float sky[3];           /**< Linear ambient light from above, summed */
    float ground[3];        /**< Linear ambient light from below, summed */
    box_light_list_t lists[BOX_LIGHTS_LISTS]; /**< The lists, by box_light_list_kind_t */
    ecs_vec_t spots;        /**< box_light_spot_t, the shadow casting spot lights of this frame */
    box_light_atlas_t atlas; /**< The shadow atlas of the scene */
} box_light_scene_t;

/** Main thread state of the light extract */
typedef struct box_render_lights_t {
    ecs_query_t *directional;   /**< Entities with DirectionalLight */
    ecs_query_t *point;         /**< Entities with PointLight */
    ecs_query_t *spot;          /**< Entities with SpotLight */
    ecs_query_t *ambient;       /**< Entities with AmbientLight */
    ecs_query_t *bakes;         /**< Entities with LightBake */
    ecs_query_t *atlases;       /**< Entities with ShadowAtlas */
    ecs_query_t *atmospheres;   /**< Entities with Atmosphere, NULL without box.atmosphere */
    ecs_query_t *times;         /**< Entities with TimeOfDay, NULL without box.atmosphere */
    ecs_vec_t scenes;           /**< box_light_scene_t */
    ecs_map_t lookup;           /**< Scene root plus one to the index of its scene plus one */
    ecs_vec_t free_buffers;     /**< uint32_t, light buffer handles to reuse */
    uint32_t next_buffer;       /**< Next light buffer handle */
    ecs_vec_t free_atlases;     /**< uint32_t, shadow atlas handles to reuse */
    uint32_t next_atlas;        /**< Next shadow atlas handle */
    ecs_vec_t requests;         /**< box_render_shadow_request_t, scratch of the allocation */
    ecs_vec_t places;           /**< box_render_shadow_place_t, scratch of the allocation */
    ecs_vec_t candidates;       /**< int32_t, scratch: the spot of each request */
    ecs_vec_t views;            /**< int32_t, scratch: the views that sample an atlas */
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

/** Prepare the empty shadow atlas state of a scene.
 *
 * @param s The scene
 */
static void box_lights_atlas_init(
    box_light_scene_t *s)
{
    ecs_vec_init_t(NULL, &s->spots, box_light_spot_t, 0);
    ecs_vec_init_t(NULL, &s->atlas.records, box_render_shadow_record_t, 0);
    ecs_vec_init_t(NULL, &s->atlas.tiles, box_render_shadow_tile_t, 0);
    ecs_vec_init_t(NULL, &s->atlas.sent, box_render_shadow_record_t, 0);
}

/** Free the shadow atlas state of a scene; its atlas is released by the caller.
 *
 * @param s The scene
 */
static void box_lights_atlas_fini(
    box_light_scene_t *s)
{
    ecs_vec_fini_t(NULL, &s->spots, box_light_spot_t);
    ecs_vec_fini_t(NULL, &s->atlas.records, box_render_shadow_record_t);
    ecs_vec_fini_t(NULL, &s->atlas.tiles, box_render_shadow_tile_t);
    ecs_vec_fini_t(NULL, &s->atlas.sent, box_render_shadow_record_t);
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

        box_lights_atlas_init(s);

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
        ecs_os_memset_t(&s->tod, 0, box_light_tod_t);
        s->has_ambient = false;
        glm_vec3_zero(s->sky);
        glm_vec3_zero(s->ground);
        ecs_vec_clear(&s->spots);
        ecs_vec_clear(&s->atlas.records);
        ecs_vec_clear(&s->atlas.tiles);
        s->atlas.found = false;
    }
}

/** Write the linear color of a light, scaled by its intensity: the irradiance it gives a
 * surface facing it, which a Lambert surface reflects as albedo / pi times that.
 *
 * @param rgb Color of the light, NULL for white
 * @param intensity Irradiance scale
 * @param out Receives the color
 */
static void box_lights_color(
    const Rgb *rgb,
    float intensity,
    float *out)
{
    out[0] = (rgb ? box_lights_srgb[rgb->r] : 1.0f) * intensity;
    out[1] = (rgb ? box_lights_srgb[rgb->g] : 1.0f) * intensity;
    out[2] = (rgb ? box_lights_srgb[rgb->b] : 1.0f) * intensity;
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
 * @param intensity Irradiance scale
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
    ecs_os_memset_t(out, 0, box_render_light_t);
    if (transform) {
        glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[3]), out->position);
    } else {
        glm_vec3_zero(out->position);
    }

    out->shadow = BOX_RENDER_SHADOW_NONE;
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

/** Write the default sun, of intensity pi.
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
    out->shadow = BOX_RENDER_SHADOW_NONE;
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
    glm_vec3_copy(light->color, target->color);
    box_atmos_transmittance(&a->medium, a->value.ground_altitude, target->direction[1],
        target->transmittance);
    fade = box_atmos_horizon_fade(target->direction[1], radius);
    target->fade = fade;
    for (c = 0; c < 3; c ++) {
        light->color[c] *= target->transmittance[c] * fade;
        target->attenuated[c] = light->color[c];
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

/** Attenuate a directional light that is the sun or the moon of the TimeOfDay of a scene
 * without an Atmosphere by the fallback of the time of day.
 *
 * Without TimeOfDay.sun the sun is the first directional light of the scene that is not the
 * moon.
 *
 * @param s The scene of the light
 * @param e The light entity
 * @param light The light, its color is attenuated
 */
static void box_lights_tod_directional(
    box_light_scene_t *s,
    ecs_entity_t e,
    box_render_light_t *light)
{
    box_light_tod_t *t = &s->tod;
    if (!t->entity || s->atmos.entity) {
        goto done;
    }

    if (!t->sun_found && (t->sun ? e == t->sun : e != t->moon)) {
        t->sun_found = true;
        box_tod_fallback(light->position[1], light->color);
    } else if (!t->moon_found && t->moon && e == t->moon) {
        t->moon_found = true;
        box_tod_fallback(light->position[1], light->color);
    }

done:
    return;
}

/** Collect the lights one query matches into the scenes they belong to.
 *
 * The scene of a light comes from the Scene(self|up) field of the query. Its Rgb may be
 * inherited from a prefab, which the field then holds once for every row.
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
        int32_t color_step = colors && ecs_field_is_self(&it, 2) ? 1 : 0;
        box_light_scene_t *s = NULL;
        ecs_entity_t scene = 0;
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const WorldTransform *transform = transforms ? &transforms[i] : NULL;
            const Rgb *rgb = colors ? &colors[i * color_step] : NULL;
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
                box_lights_tod_directional(s, it.entities[i], &out);
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
                    int32_t index = ecs_vec_count(&s->lists[BOX_LIGHTS_VIEW].lights);
                    box_lights_point(transform, rgb, sp->intensity, sp->range, &out);
                    box_lights_axis(transform, out.direction);
                    out.spot_scale = 1.0f / glm_max(cos_inner - cos_outer, 1e-4f);
                    out.spot_offset = -cos_outer * out.spot_scale;
                    box_lights_push(s, it.entities[i], sp->bake, &out);
                    if (sp->shadows && sp->bake != LightBakeModeBaked &&
                        ecs_vec_count(&s->lists[BOX_LIGHTS_VIEW].lights) > index)
                    {
                        *ecs_vec_append_t(NULL, &s->spots, box_light_spot_t) =
                            (box_light_spot_t){
                                .entity = it.entities[i],
                                .index = index,
                                .resolution = sp->shadow_resolution,
                                .outer = outer
                            };
                    }
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

/** Give the time of day of every scene in this frame to its scene, the first one of a scene
 * that has several.
 *
 * @param world The world
 * @param l The state
 */
static void box_lights_collect_times(
    ecs_world_t *world,
    box_render_lights_t *l)
{
    if (l->times) {
        ecs_iter_t it = ecs_query_iter(world, l->times);
        while (ecs_query_next(&it)) {
            const TimeOfDay *values = ecs_field(&it, TimeOfDay, 0);
            int32_t i;
            for (i = 0; i < it.count; i ++) {
                box_light_scene_t *s = box_lights_ensure(l, box_render_scene_field(&it, 1, i));
                if (!s->tod.entity) {
                    s->tod.entity = it.entities[i];
                    s->tod.sun = values[i].sun;
                    s->tod.moon = values[i].moon;
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

/** Scale the moon light of a list by its illumination.
 *
 * @param list The list
 * @param moon The moon entity
 * @param illumination The lit fraction of the moon
 */
static void box_lights_list_moon(
    box_light_list_t *list,
    ecs_entity_t moon,
    float illumination)
{
    const ecs_entity_t *entities = ecs_vec_first_t(&list->entities, ecs_entity_t);
    int32_t i;
    for (i = 0; i < (int32_t)list->directional; i ++) {
        if (entities[i] == moon) {
            box_render_light_t *light = ecs_vec_get_t(&list->lights, box_render_light_t, i);
            glm_vec3_scale(light->color, illumination, light->color);
        }
    }
}

/** Move the brightest directional light of a list to its start, keeping the order of the
 * others. Of lights equally bright the first stays first.
 *
 * @param list The list
 */
static void box_lights_list_brightest(
    box_light_list_t *list)
{
    box_render_light_t *lights = ecs_vec_first_t(&list->lights, box_render_light_t);
    ecs_entity_t *entities = ecs_vec_first_t(&list->entities, ecs_entity_t);
    uint8_t *modes = ecs_vec_first_t(&list->modes, uint8_t);
    int32_t i, brightest = 0;
    for (i = 1; i < (int32_t)list->directional; i ++) {
        if (glm_luminance(lights[i].color) > glm_luminance(lights[brightest].color)) {
            brightest = i;
        }
    }

    if (brightest > 0) {
        box_render_light_t light = lights[brightest];
        ecs_entity_t e = entities[brightest];
        uint8_t mode = modes[brightest];
        ecs_os_memmove_n(&lights[1], &lights[0], box_render_light_t, brightest);
        ecs_os_memmove_n(&entities[1], &entities[0], ecs_entity_t, brightest);
        ecs_os_memmove_n(&modes[1], &modes[0], uint8_t, brightest);
        lights[0] = light;
        entities[0] = e;
        modes[0] = mode;
    }
}

/** Scale the moon of every scene with a valid atmosphere by its illumination, which needs the
 * sun, and put the brightest directional light of the scene first in its lists so that it
 * casts the shadows: the sun by day, the moon after sunset.
 *
 * @param l The state
 */
static void box_lights_atmos_moonlight(
    box_render_lights_t *l)
{
    int32_t i, k;
    for (i = 0; i < ecs_vec_count(&l->scenes); i ++) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        box_light_atmos_t *a = &s->atmos;
        if (!a->entity || a->error) {
            continue;
        }

        if (a->moon.found) {
            a->moon_illumination = a->sun.found
                ? box_atmos_moon_illumination(a->sun.direction, a->moon.direction)
                : 0.0f;
            glm_vec3_scale(a->moon.attenuated, a->moon_illumination, a->moon.attenuated);
        }

        for (k = 0; k < BOX_LIGHTS_LISTS; k ++) {
            if (a->moon.found) {
                box_lights_list_moon(&s->lists[k], a->value.moon, a->moon_illumination);
            }

            box_lights_list_brightest(&s->lists[k]);
        }
    }
}

/** Write the transmittances, the error and the lut_version of the atmosphere of every scene
 * into its AtmosphereStatus when they changed.
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
        const box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        const box_light_atmos_t *a = &s->atmos;
        const AtmosphereStatus *status = a->entity
            ? ecs_get(it->world, a->entity, AtmosphereStatus)
            : NULL;
        if (!status) {
            continue;
        }

        AtmosphereStatus value = *status;
        value.lut_version = box_render_atmosphere_version(it->world, s->scene, a->entity,
            status->lut_version);
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

        if (!a->error) {
            value.moon_illumination = a->moon_illumination;
        }

        bool same_error = a->error
            ? status->error && !strcmp(status->error, a->error)
            : !status->error;
        if (!same_error || value.moon_illumination != status->moon_illumination ||
            value.lut_version != status->lut_version ||
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

/** Destroy the shadow atlas of a scene and its tile buffer when it has them.
 *
 * @param l The state
 * @param s The scene
 * @param p The packet, receives the destroys
 */
static void box_lights_atlas_release(
    box_render_lights_t *l,
    box_light_scene_t *s,
    box_render_packet_t *p)
{
    box_light_atlas_t *a = &s->atlas;
    if (!a->handle) {
        goto done;
    }

    if (a->resolution) {
        box_render_packet_resource(p, BOX_RENDER_SHADOW_ATLAS, BOX_RENDER_DESTROY, a->handle, 0);
    }

    if (a->buffer) {
        box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY, a->handle, 0);
    }

    *ecs_vec_append_t(NULL, &l->free_atlases, uint32_t) = a->handle;
    a->handle = 0;
    a->resolution = 0;
    a->buffer = false;
    ecs_vec_clear(&a->sent);
done:
    return;
}

/** Remove the entry of a scene, moving the last entry into its place.
 *
 * @param l The state
 * @param p The packet, receives the destroy of the light buffers and the shadow atlas
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

    box_lights_atlas_release(l, s, p);
    box_lights_atlas_fini(s);
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
        ecs_entity_t scene = view && view->lighting.scene ? view->lighting.scene : v->scene;
        int32_t index = box_lights_find(l, scene);
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

        s->atlas.resolution = 0;
        s->atlas.buffer = false;
        ecs_vec_clear(&s->atlas.sent);
    }

    l->default_sent = false;
}

/** Give every scene with lights its ShadowAtlas, the first one of a scene that has several, or
 * the defaults.
 *
 * @param world The world
 * @param l The state
 */
static void box_lights_collect_atlases(
    ecs_world_t *world,
    box_render_lights_t *l)
{
    int32_t i;
    ecs_iter_t it = ecs_query_iter(world, l->atlases);
    while (ecs_query_next(&it)) {
        const ShadowAtlas *values = ecs_field(&it, ShadowAtlas, 0);
        for (i = 0; i < it.count; i ++) {
            int32_t index = box_lights_find(l, box_render_scene_field(&it, 1, i));
            box_light_scene_t *s = index != -1
                ? ecs_vec_get_t(&l->scenes, box_light_scene_t, index)
                : NULL;
            if (s && !s->atlas.found) {
                s->atlas.found = true;
                box_render_shadow_atlas_settings(&values[i], &s->atlas.settings);
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&l->scenes); i ++) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        if (!s->atlas.found) {
            box_render_shadow_atlas_settings(NULL, &s->atlas.settings);
        }
    }
}

/** Check whether a view of the packet renders the shadows of the spot lights of a scene: it is
 * lit by the lights of the scene, renders shadows and its camera has a depth range.
 *
 * @param world The world
 * @param v The view
 * @param scene The scene of the lights
 * @return Whether the view samples the shadow atlas of the scene
 */
static bool box_lights_shadow_view(
    ecs_world_t *world,
    const box_render_view_t *v,
    ecs_entity_t scene)
{
    const View *view = v->view && ecs_is_alive(world, v->view)
        ? ecs_get(world, v->view, View)
        : NULL;
    ecs_entity_t lit = view && view->lighting.scene ? view->lighting.scene : v->scene;

    return view && view->lighting.enabled && view->shadows.enabled && lit == scene &&
        box_camera_has_depth(&v->camera_data);
}

/** Check whether a sphere may reach into the frustum of a view.
 *
 * A camera that follows the aspect of its surface is tested with the wide
 * BOX_LIGHTS_SHADOW_ASPECT, an orthographic one sized by its surface only by its depth range,
 * so a sphere the view may see is never left out.
 *
 * @param v The view
 * @param center World center of the sphere
 * @param radius Radius of the sphere
 * @return Whether the sphere is not entirely outside a plane of the frustum
 */
static bool box_lights_view_sees(
    const box_render_view_t *v,
    const float *center,
    float radius)
{
    const Camera *c = &v->camera_data;
    bool sides = !c->ortho || c->ortho_scale <= 0, result = true;
    float aspect = c->aspect_ratio > 0 ? c->aspect_ratio : BOX_LIGHTS_SHADOW_ASPECT;
    mat4 proj, m;
    int32_t i, j;
    box_camera_projection(c, aspect, 1.0f, proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), m);
    for (i = sides ? 0 : 4; i < 6 && result; i ++) {
        vec4 plane;
        for (j = 0; j < 4; j ++) {
            if (i < 4) {
                plane[j] = m[j][3] + ((i & 1) ? -1.0f : 1.0f) * m[j][i / 2];
            } else {
                plane[j] = i == 4 ? m[j][3] - m[j][2] : m[j][2];
            }
        }

        float len = sqrtf(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
        if (len > 1e-12f) {
            float d = (plane[0] * center[0] + plane[1] * center[1] + plane[2] * center[2] +
                plane[3]) / len;
            result = d >= -radius;
        }
    }

    return result;
}

/** Allocate the tiles of the shadow casting spot lights of a scene that a view with shadows
 * sees, write their tile records and give each light its record index.
 *
 * @param world The world
 * @param l The state
 * @param p The packet
 * @param s The scene
 */
static void box_lights_shadow_tiles(
    ecs_world_t *world,
    box_render_lights_t *l,
    box_render_packet_t *p,
    box_light_scene_t *s)
{
    box_light_atlas_t *a = &s->atlas;
    box_render_light_t *lights = ecs_vec_first_t(
        &s->lists[BOX_LIGHTS_VIEW].lights, box_render_light_t);
    int32_t i, j, n, dropped = 0, count = ecs_vec_count(&s->spots);
    ecs_vec_clear(&l->requests);
    ecs_vec_clear(&l->candidates);
    ecs_vec_clear(&l->views);
    for (i = 0; count && i < ecs_vec_count(&p->views); i ++) {
        if (box_lights_shadow_view(world,
            ecs_vec_get_t(&p->views, box_render_view_t, i), s->scene))
        {
            *ecs_vec_append_t(NULL, &l->views, int32_t) = i;
        }
    }

    for (i = 0; i < count && ecs_vec_count(&l->views); i ++) {
        const box_light_spot_t *spot = ecs_vec_get_t(&s->spots, box_light_spot_t, i);
        const box_render_light_t *light = &lights[spot->index];
        float distance = FLT_MAX;
        bool seen = false;
        for (j = 0; j < ecs_vec_count(&l->views); j ++) {
            const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t,
                *ecs_vec_get_t(&l->views, int32_t, j));
            float d = glm_vec3_distance(ECS_CONST_CAST(float*, v->camera_transform[3]),
                ECS_CONST_CAST(float*, light->position)) - light->range;
            distance = glm_min(distance, glm_max(d, 0.0f));
            seen = seen || box_lights_view_sees(v, light->position, light->range);
        }

        if (seen) {
            *ecs_vec_append_t(NULL, &l->requests, box_render_shadow_request_t) =
                (box_render_shadow_request_t){
                    .light = spot->entity,
                    .size = box_render_shadow_request_size(spot->resolution, &a->settings),
                    .distance = distance
                };
            *ecs_vec_append_t(NULL, &l->candidates, int32_t) = i;
        }
    }

    n = ecs_vec_count(&l->requests);
    ecs_vec_set_count_t(NULL, &l->places, box_render_shadow_place_t, n);
    if (!n) {
        goto done;
    }

    l->stats.shadow_tiles += box_render_shadow_allocate(ecs_vec_first(&l->requests), n,
        &a->settings, ecs_vec_first(&l->places), &dropped);
    l->stats.shadow_dropped += dropped;
    for (i = 0; i < n; i ++) {
        const box_render_shadow_place_t *place = ecs_vec_get_t(
            &l->places, box_render_shadow_place_t, i);
        const box_light_spot_t *spot = ecs_vec_get_t(&s->spots, box_light_spot_t,
            *ecs_vec_get_t(&l->candidates, int32_t, i));
        box_render_light_t *light = &lights[spot->index];
        if (!place->size) {
            continue;
        }

        box_render_shadow_record_t *r = ecs_vec_append_t(
            NULL, &a->records, box_render_shadow_record_t);
        box_render_shadow_spot(light->position, light->direction, light->range, spot->outer,
            place, a->settings.resolution, r);
        box_render_shadow_tile_t *t = ecs_vec_append_t(NULL, &a->tiles, box_render_shadow_tile_t);
        t->light = spot->entity;
        t->x = place->x;
        t->y = place->y;
        t->size = place->size;
        glm_mat4_copy(r->matrix, t->matrix);
        light->shadow = (uint32_t)ecs_vec_count(&a->records) - 1;
    }

done:
    return;
}

/** Send the shadow atlas of a scene, its tile records when they changed and its tiles; release
 * it when the scene has no tile.
 *
 * The atlas is created when the scene first gets a tile and again when its resolution changes;
 * the tile buffer always holds BOX_RENDER_SHADOW_TILES records.
 *
 * @param l The state
 * @param s The scene
 * @param p The packet
 */
static void box_lights_atlas_send(
    box_render_lights_t *l,
    box_light_scene_t *s,
    box_render_packet_t *p)
{
    box_light_atlas_t *a = &s->atlas;
    int32_t count = ecs_vec_count(&a->records);
    uint32_t size = (uint32_t)count * (uint32_t)sizeof(box_render_shadow_record_t);
    if (!count) {
        box_lights_atlas_release(l, s, p);
        goto done;
    }

    if (!a->handle) {
        if (ecs_vec_count(&l->free_atlases)) {
            a->handle = *ecs_vec_last_t(&l->free_atlases, uint32_t);
            ecs_vec_remove_last(&l->free_atlases);
        } else {
            a->handle = l->next_atlas ++;
        }
    }

    if (a->resolution != a->settings.resolution) {
        box_render_shadow_atlas_desc_t *desc = box_render_packet_resource(p,
            BOX_RENDER_SHADOW_ATLAS, BOX_RENDER_CREATE, a->handle,
            sizeof(box_render_shadow_atlas_desc_t));
        desc->resolution = a->settings.resolution;
        a->resolution = a->settings.resolution;
    }

    if (!a->buffer) {
        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, a->handle, sizeof(box_render_buffer_desc_t));
        desc->size = BOX_RENDER_SHADOW_TILES * sizeof(box_render_shadow_record_t);
        desc->usage = BOX_RENDER_BUFFER_UNIFORM;
        a->buffer = true;
        ecs_vec_clear(&a->sent);
    }

    if (count != ecs_vec_count(&a->sent) ||
        ecs_os_memcmp(ecs_vec_first(&a->records), ecs_vec_first(&a->sent), (ecs_size_t)size))
    {
        ecs_os_memcpy(box_render_packet_upload(p, a->handle, 0, size),
            ecs_vec_first(&a->records), (ecs_size_t)size);
        ecs_vec_set_count_t(NULL, &a->sent, box_render_shadow_record_t, count);
        ecs_os_memcpy(ecs_vec_first(&a->sent), ecs_vec_first(&a->records), (ecs_size_t)size);
    }

    box_render_shadow_atlas_t *atlas = ecs_vec_append_t(
        NULL, &p->shadow_atlases, box_render_shadow_atlas_t);
    atlas->scene = s->scene;
    atlas->handle = a->handle;
    atlas->resolution = a->resolution;
    atlas->first = ecs_vec_count(&p->shadow_tiles);
    atlas->count = count;
    ecs_os_memcpy_n(ecs_vec_grow_t(NULL, &p->shadow_tiles, box_render_shadow_tile_t, count),
        ecs_vec_first(&a->tiles), box_render_shadow_tile_t, count);
done:
    return;
}

/** Allocate the shadow tiles of every scene and send its atlas, before the light buffers are
 * sent, since the lights hold the index of their tile records.
 *
 * @param world The world
 * @param l The state
 * @param p The packet
 */
static void box_lights_shadows(
    ecs_world_t *world,
    box_render_lights_t *l,
    box_render_packet_t *p)
{
    int32_t i;
    box_lights_collect_atlases(world, l);
    for (i = 0; i < ecs_vec_count(&l->scenes); i ++) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        box_lights_shadow_tiles(world, l, p, s);
        box_lights_atlas_send(l, s, p);
    }
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
    box_lights_collect_times(world, l);
    box_lights_collect(world, l, l->directional, 0);
    box_lights_collect(world, l, l->point, 1);
    box_lights_collect(world, l, l->spot, 2);
    box_lights_atmos_defaults(l);
    box_lights_atmos_moonlight(l);
    box_lights_collect_ambient(world, l);
    box_lights_shadows(world, l, p);
    box_lights_send(l, p);
    box_lights_views(world, l, p);
    box_lights_atmos_status(it, l);
    RenderStatus *status = ecs_singleton_get_mut(world, RenderStatus);
    if (status) {
        status->shadow_tiles = l->stats.shadow_tiles;
        status->shadow_dropped = l->stats.shadow_dropped;
    }
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

        box_lights_atlas_fini(s);
    }

    ecs_vec_fini_t(NULL, &l->scenes, box_light_scene_t);
    ecs_vec_fini_t(NULL, &l->free_buffers, uint32_t);
    ecs_vec_fini_t(NULL, &l->free_atlases, uint32_t);
    ecs_vec_fini_t(NULL, &l->requests, box_render_shadow_request_t);
    ecs_vec_fini_t(NULL, &l->places, box_render_shadow_place_t);
    ecs_vec_fini_t(NULL, &l->candidates, int32_t);
    ecs_vec_fini_t(NULL, &l->views, int32_t);
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
    ecs_vec_init_t(NULL, &l->free_atlases, uint32_t, 0);
    ecs_vec_init_t(NULL, &l->requests, box_render_shadow_request_t, 0);
    ecs_vec_init_t(NULL, &l->places, box_render_shadow_place_t, 0);
    ecs_vec_init_t(NULL, &l->candidates, int32_t, 0);
    ecs_vec_init_t(NULL, &l->views, int32_t, 0);
    ecs_map_init(&l->lookup, NULL);
    l->next_buffer = BOX_LIGHTS_DEFAULT_BUFFER + 1;
    l->next_atlas = BOX_RENDER_BUFFER_SHADOW_FIRST;
    l->directional = box_lights_query(world, ecs_id(DirectionalLight));
    l->point = box_lights_query(world, ecs_id(PointLight));
    l->spot = box_lights_query(world, ecs_id(SpotLight));
    l->ambient = box_lights_scene_query(world, ecs_id(AmbientLight));
    l->bakes = box_lights_scene_query(world, ecs_id(LightBake));
    l->atlases = box_lights_scene_query(world, ecs_id(ShadowAtlas));

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

bool box_render_lights_atmos_sun(
    const void *state,
    ecs_entity_t scene,
    float *direction,
    float *color)
{
    const box_render_lights_t *l = state;
    int32_t index = l ? box_lights_find(ECS_CONST_CAST(box_render_lights_t*, l), scene) : -1;
    const box_light_scene_t *s = index != -1
        ? ecs_vec_get_t(&l->scenes, box_light_scene_t, index)
        : NULL;
    bool result = s && s->atmos.entity && !s->atmos.error && s->atmos.sun.found;
    if (result) {
        glm_vec3_copy(ECS_CONST_CAST(float*, s->atmos.sun.direction), direction);
        glm_vec3_copy(ECS_CONST_CAST(float*, s->atmos.sun.color), color);
    }

    return result;
}

bool box_render_lights_atmos_moon(
    const void *state,
    ecs_entity_t scene,
    float *direction,
    float *color,
    float *illumination)
{
    const box_render_lights_t *l = state;
    int32_t index = l ? box_lights_find(ECS_CONST_CAST(box_render_lights_t*, l), scene) : -1;
    const box_light_scene_t *s = index != -1
        ? ecs_vec_get_t(&l->scenes, box_light_scene_t, index)
        : NULL;
    bool result = s && s->atmos.entity && !s->atmos.error && s->atmos.moon.found;
    if (result) {
        glm_vec3_copy(ECS_CONST_CAST(float*, s->atmos.moon.direction), direction);
        glm_vec3_copy(ECS_CONST_CAST(float*, s->atmos.moon.color), color);
        *illumination = s->atmos.moon_illumination;
    }

    return result;
}

bool box_render_lights_atmos_main(
    const void *state,
    ecs_entity_t scene,
    float *direction,
    float *color,
    float *fade,
    bool *moon)
{
    const box_render_lights_t *l = state;
    int32_t index = l ? box_lights_find(ECS_CONST_CAST(box_render_lights_t*, l), scene) : -1;
    const box_light_scene_t *s = index != -1
        ? ecs_vec_get_t(&l->scenes, box_light_scene_t, index)
        : NULL;
    const box_light_atmos_t *a = s && s->atmos.entity && !s->atmos.error ? &s->atmos : NULL;
    const box_light_atmos_light_t *light = NULL;
    if (a && a->sun.found) {
        light = &a->sun;
    }

    if (a && a->moon.found && (!light ||
        glm_luminance(ECS_CONST_CAST(float*, a->moon.attenuated)) >
        glm_luminance(ECS_CONST_CAST(float*, a->sun.attenuated))))
    {
        light = &a->moon;
    }

    if (light) {
        glm_vec3_copy(ECS_CONST_CAST(float*, light->direction), direction);
        glm_vec3_copy(ECS_CONST_CAST(float*, light->attenuated), color);
        *fade = light->fade;
        *moon = light == &a->moon;
    }

    return light != NULL;
}

uint32_t box_render_lights_atlas(
    const void *state,
    ecs_entity_t scene,
    uint32_t *handle)
{
    const box_render_lights_t *l = state;
    int32_t index = l ? box_lights_find(ECS_CONST_CAST(box_render_lights_t*, l), scene) : -1;
    const box_light_scene_t *s = index != -1
        ? ecs_vec_get_t(&l->scenes, box_light_scene_t, index)
        : NULL;
    uint32_t result = s ? (uint32_t)ecs_vec_count(&s->atlas.records) : 0;
    *handle = result ? s->atlas.handle : 0;

    return result;
}

void box_render_lights_time_of_day(
    ecs_world_t *world)
{
    box_render_lights_t *l = ECS_CONST_CAST(box_render_lights_t*,
        box_render_lights_state(world));
    if (l && !l->times) {
        l->times = box_lights_scene_query(world, ecs_id(TimeOfDay));
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
