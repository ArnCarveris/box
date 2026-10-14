/**
 * @file render/lights.c
 * @brief Collects the lights of every scene into light buffers and gives every view its lights.
 *
 * Every frame the lights are gathered per scene, directional lights first; a light buffer is
 * only uploaded when its contents changed. A scene without any light entity is lit by the
 * default sun of a shared buffer, a scene without AmbientLight by the default sky and ground.
 */

#include "render.h"
#include <math.h>

/** Lights the light buffer of a scene holds at least */
#define BOX_LIGHTS_MIN_CAPACITY (16)

/** Range of a point or spot light that has none */
#define BOX_LIGHTS_RANGE (10.0f)

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

/** The lights of one scene and what its light buffer holds on the backend */
typedef struct box_light_scene_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    uint32_t buffer;        /**< Handle of the light buffer, 0 before it is created */
    uint32_t capacity;      /**< Lights the light buffer holds, 0 when the backend lacks it */
    uint32_t directional;   /**< Directional lights at the start of lights */
    bool has_lights;        /**< The scene has a light entity, even one that lights nothing */
    bool has_ambient;       /**< The scene has an AmbientLight */
    float sky[3];           /**< Linear ambient light from above, summed */
    float ground[3];        /**< Linear ambient light from below, summed */
    ecs_vec_t lights;       /**< box_render_light_t of this frame */
    ecs_vec_t sent;         /**< box_render_light_t the light buffer holds */
} box_light_scene_t;

/** Main thread state of the light extract */
typedef struct box_render_lights_t {
    ecs_query_t *directional;   /**< Entities with DirectionalLight */
    ecs_query_t *point;         /**< Entities with PointLight */
    ecs_query_t *spot;          /**< Entities with SpotLight */
    ecs_query_t *ambient;       /**< Entities with AmbientLight */
    ecs_vec_t scenes;           /**< box_light_scene_t */
    ecs_map_t lookup;           /**< Scene root plus one to the index of its scene plus one */
    ecs_vec_t free_buffers;     /**< uint32_t, light buffer handles to reuse */
    uint32_t next_buffer;       /**< Next light buffer handle */
    bool default_sent;          /**< The backend has the buffer of the default sun */
    const void *backend;        /**< Context of the backend the buffers were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
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

/** Get the scene root an entity belongs to.
 *
 * @param world The world
 * @param e The entity
 * @return The entity when it has Scene, else its nearest ancestor with Scene, 0 for none
 */
static ecs_entity_t box_lights_scene_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_has(world, e, Scene)
        ? e
        : ecs_get_target_for_id(world, e, EcsChildOf, ecs_id(Scene));
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
    int32_t index = box_lights_find(l, scene);
    if (index == -1) {
        index = ecs_vec_count(&l->scenes);
        box_light_scene_t *s = ecs_vec_append_t(NULL, &l->scenes, box_light_scene_t);
        ecs_os_memset_t(s, 0, box_light_scene_t);
        s->scene = scene;
        ecs_vec_init_t(NULL, &s->lights, box_render_light_t, 0);
        ecs_vec_init_t(NULL, &s->sent, box_render_light_t, 0);
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
    int32_t i;
    for (i = 0; i < ecs_vec_count(&l->scenes); i ++) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        ecs_vec_clear(&s->lights);
        s->directional = 0;
        s->has_lights = false;
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

/** Append a light with a position, a range and no cone to a scene.
 *
 * @param s The scene
 * @param transform World transform of the light, NULL for the origin
 * @param rgb Color of the light, NULL for white
 * @param intensity Radiance scale
 * @param range Distance at which the light reaches zero, 0 for the default
 * @return The light
 */
static box_render_light_t* box_lights_add_point(
    box_light_scene_t *s,
    const WorldTransform *transform,
    const Rgb *rgb,
    float intensity,
    float range)
{
    box_render_light_t *out = ecs_vec_append_t(NULL, &s->lights, box_render_light_t);
    if (transform) {
        glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[3]), out->position);
    } else {
        glm_vec3_zero(out->position);
    }

    out->range = range > 0 ? range : BOX_LIGHTS_RANGE;
    box_lights_color(rgb, intensity, out->color);
    glm_vec3_copy((vec3){0, 0, 1}, out->direction);
    out->spot_scale = 0.0f;
    out->spot_offset = 1.0f;

    return out;
}

/** Collect the lights one query matches into the scenes they belong to.
 *
 * Every entity in a table shares its scene unless the table has non fragmenting parents.
 *
 * @param world The world
 * @param l The state
 * @param q The query, with the light component, WorldTransform and Rgb
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
        bool per_entity = ecs_table_has_id(world, it.table, ecs_id(EcsParent));
        box_light_scene_t *s = NULL;
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const WorldTransform *transform = transforms ? &transforms[i] : NULL;
            const Rgb *rgb = colors ? &colors[i] : NULL;
            if (!s || per_entity) {
                s = box_lights_ensure(l, box_lights_scene_of(world, it.entities[i]));
            }

            s->has_lights = true;
            if (kind == 0) {
                const DirectionalLight *d = &ecs_field(&it, DirectionalLight, 0)[i];
                if (d->intensity > 0) {
                    box_render_light_t *out = box_lights_add_point(s, NULL, rgb, d->intensity, 0);
                    box_lights_axis(transform, out->position);
                    glm_vec3_negate(out->position);
                    out->range = 0.0f;
                    s->directional ++;
                }
            } else if (kind == 1) {
                const PointLight *p = &ecs_field(&it, PointLight, 0)[i];
                if (p->intensity > 0) {
                    box_lights_add_point(s, transform, rgb, p->intensity, p->range);
                }
            } else {
                const SpotLight *sp = &ecs_field(&it, SpotLight, 0)[i];
                if (sp->intensity > 0) {
                    float outer = glm_clamp(sp->outer_angle, 0.01f, 89.9f);
                    float inner = glm_clamp(sp->inner_angle, 0.0f, outer);
                    float cos_outer = cosf(glm_rad(outer));
                    float cos_inner = cosf(glm_rad(inner));
                    box_render_light_t *out = box_lights_add_point(
                        s, transform, rgb, sp->intensity, sp->range);
                    box_lights_axis(transform, out->direction);
                    out->spot_scale = 1.0f / glm_max(cos_inner - cos_outer, 1e-4f);
                    out->spot_offset = -cos_outer * out->spot_scale;
                }
            }
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
            box_light_scene_t *s = box_lights_ensure(
                l, box_lights_scene_of(world, it.entities[i]));
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
    box_render_light_t sun = {0};
    if (l->default_sent) {
        goto done;
    }

    glm_vec3_normalize_to(ECS_CONST_CAST(float*, box_lights_sun), sun.position);
    glm_vec3_scale(ECS_CONST_CAST(float*, box_lights_sun_color), GLM_PIf, sun.color);
    glm_vec3_copy((vec3){0, 0, 1}, sun.direction);
    sun.spot_offset = 1.0f;
    box_lights_buffer_create(p, BOX_LIGHTS_DEFAULT_BUFFER, 1);
    ecs_os_memcpy_t(box_render_packet_upload(p, BOX_LIGHTS_DEFAULT_BUFFER, 0,
        sizeof(box_render_light_t)), &sun, box_render_light_t);
    l->default_sent = true;
done:
    return;
}

/** Send the light buffer of a scene when the backend lacks it or it changed.
 *
 * @param l The state
 * @param s The scene
 * @param p The packet
 */
static void box_lights_send_scene(
    box_render_lights_t *l,
    box_light_scene_t *s,
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

/** Remove the entry of a scene, moving the last entry into its place.
 *
 * @param l The state
 * @param p The packet, receives the destroy of the light buffer
 * @param index Index of the scene
 */
static void box_lights_remove(
    box_render_lights_t *l,
    box_render_packet_t *p,
    int32_t index)
{
    box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, index);
    int32_t last = ecs_vec_count(&l->scenes) - 1;
    if (s->buffer) {
        box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY, s->buffer, 0);
        *ecs_vec_append_t(NULL, &l->free_buffers, uint32_t) = s->buffer;
    }

    ecs_map_remove(&l->lookup, s->scene + 1);
    ecs_vec_fini_t(NULL, &s->lights, box_render_light_t);
    ecs_vec_fini_t(NULL, &s->sent, box_render_light_t);
    if (index != last) {
        *s = *ecs_vec_get_t(&l->scenes, box_light_scene_t, last);
        ecs_map_val_t *moved = ecs_map_get(&l->lookup, s->scene + 1);
        *moved = (ecs_map_val_t)index + 1;
    }

    ecs_vec_remove_last(&l->scenes);
}

/** Send the light buffers of the scenes and drop the scenes that lost their lights.
 *
 * @param l The state
 * @param p The packet
 */
static void box_lights_send(
    box_render_lights_t *l,
    box_render_packet_t *p)
{
    int32_t i;
    for (i = ecs_vec_count(&l->scenes) - 1; i >= 0; i --) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        if (s->has_lights) {
            box_lights_send_scene(l, s, p);
            l->stats.scenes ++;
            l->stats.lights += ecs_vec_count(&s->lights);
        } else if (!s->has_ambient) {
            box_lights_remove(l, p, i);
        } else if (s->buffer) {
            box_render_packet_resource(p, BOX_RENDER_BUFFER, BOX_RENDER_DESTROY, s->buffer, 0);
            *ecs_vec_append_t(NULL, &l->free_buffers, uint32_t) = s->buffer;
            s->buffer = 0;
            s->capacity = 0;
            ecs_vec_clear(&s->sent);
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
        if (s && s->has_lights) {
            out->buffer = s->buffer;
            out->directional = s->directional;
            out->count = (uint32_t)ecs_vec_count(&s->lights);
            if (s->directional) {
                glm_vec3_copy(ecs_vec_first_t(&s->lights, box_render_light_t)->position,
                    out->sun);
            }
        } else {
            box_lights_send_default(l, p);
            out->buffer = BOX_LIGHTS_DEFAULT_BUFFER;
            out->directional = 1;
            out->count = 1;
            glm_vec3_copy(ECS_CONST_CAST(float*, box_lights_sun), out->sun);
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
    int32_t i;
    for (i = 0; i < ecs_vec_count(&l->scenes); i ++) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        s->capacity = 0;
        ecs_vec_clear(&s->sent);
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
    if (backend->ctx != l->backend || backend->render != l->render) {
        box_lights_reset_backend(l);
        l->backend = backend->ctx;
        l->render = backend->render;
    }

    ecs_os_memset_t(&l->stats, 0, box_render_lights_stats_t);
    box_lights_reset(l);
    box_lights_collect(world, l, l->directional, 0);
    box_lights_collect(world, l, l->point, 1);
    box_lights_collect(world, l, l->spot, 2);
    box_lights_collect_ambient(world, l);
    box_lights_send(l, p);
    box_lights_views(world, l, p);
}

/** Free the state of the light extract.
 *
 * @param ctx The state
 */
static void box_lights_free(
    void *ctx)
{
    box_render_lights_t *l = ctx;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&l->scenes); i ++) {
        box_light_scene_t *s = ecs_vec_get_t(&l->scenes, box_light_scene_t, i);
        ecs_vec_fini_t(NULL, &s->lights, box_render_light_t);
        ecs_vec_fini_t(NULL, &s->sent, box_render_light_t);
    }

    ecs_vec_fini_t(NULL, &l->scenes, box_light_scene_t);
    ecs_vec_fini_t(NULL, &l->free_buffers, uint32_t);
    ecs_map_fini(&l->lookup);
    ecs_os_free(l);
}

/** Create the query of one kind of light.
 *
 * @param world The world
 * @param light The light component
 * @return The query, with the light, WorldTransform and Rgb
 */
static ecs_query_t* box_lights_query(
    ecs_world_t *world,
    ecs_entity_t light)
{
    return ecs_query(world, {
        .terms = {
            { .id = light, .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Rgb), .inout = EcsIn, .oper = EcsOptional }
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
    l->ambient = ecs_query(world, {
        .terms = {{ .id = ecs_id(AmbientLight), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

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
