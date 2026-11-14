/**
 * @file render/stars.c
 * @brief Gives every lit 3D view the StarFields of its scene, for the light extract, and
 * generates the stars as etc/shaders/star_field.wgsl does.
 *
 * The fields are collected once per frame by entity id; a lit view with a perspective camera
 * with a depth range gets those of the scene of its camera.
 *
 * The stars are not in the packet: the shader generates star i of a field from a hash of i and
 * the seed, and box_render_star_sample repeats that on the CPU for the tests.
 */

#include "render.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>

/** Odd constant the hash keys of a star and of its values are spread by */
#define BOX_STAR_GOLDEN (0x9e3779b9u)

/** Pi */
#define BOX_STAR_PI (3.14159265358979f)

/** A StarField of this frame */
typedef struct box_star_field_t {
    ecs_entity_t scene;     /**< Scene root of the field, 0 for the default scene */
    box_render_star_field_t record; /**< Its record */
} box_star_field_t;

struct box_render_stars_t {
    ecs_query_t *query;     /**< Entities with StarField, their transform and scene */
    ecs_vec_t fields;       /**< box_star_field_t, the star fields of this frame */
};

/** Order star fields by the id of their entity.
 *
 * @param a The first field
 * @param b The second field
 * @return Negative when a comes first
 */
static int box_star_cmp(
    const void *a,
    const void *b)
{
    const box_star_field_t *x = a, *y = b;

    return (x->record.entity > y->record.entity) - (x->record.entity < y->record.entity);
}

/** Clamp a value to a range, NaN to the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_star_clamp(
    float v,
    float lo,
    float hi)
{
    return v >= lo ? (v <= hi ? v : hi) : lo;
}

void box_render_star_record(
    const StarField *field,
    const WorldTransform *transform,
    ecs_entity_t entity,
    box_render_star_field_t *out)
{
    int32_t k;
    ecs_os_memset_t(out, 0, box_render_star_field_t);
    for (k = 0; k < 3; k ++) {
        vec3 axis = {0, 0, 0};
        axis[k] = 1.0f;
        if (transform) {
            vec3 column;
            glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[k]), column);
            if (glm_vec3_norm(column) > 1e-12f) {
                glm_vec3_normalize_to(column, axis);
            }
        }

        glm_vec3_copy(axis, out->rotation[k]);
    }

    out->count = field->count < 0 ? 0 :
        (field->count > BOX_STAR_FIELD_MAX ? BOX_STAR_FIELD_MAX : field->count);
    out->seed = field->seed;
    out->brightness = box_star_clamp(field->brightness, 0.0f, FLT_MAX);
    out->falloff = box_star_clamp(field->falloff, 0.0f, FLT_MAX);
    out->size = box_star_clamp(field->size, 0.0f, FLT_MAX);
    out->temperature_min = box_star_clamp(field->temperature_min, 1000.0f, 40000.0f);
    out->temperature_max = box_star_clamp(field->temperature_max, out->temperature_min,
        40000.0f);
    out->saturation = box_star_clamp(field->saturation, 0.0f, 1.0f);
    out->band = box_star_clamp(field->band, 0.0f, 1.0f);
    out->band_width = box_star_clamp(field->band_width, 0.0f, 180.0f) * BOX_STAR_PI / 180.0f;
    out->entity = entity;
}

/** Collect the StarField of every entity with its scene, by entity id.
 *
 * @param world The world
 * @param s The star state
 */
static void box_star_collect(
    ecs_world_t *world,
    box_render_stars_t *s)
{
    ecs_vec_clear(&s->fields);
    ecs_iter_t it = ecs_query_iter(world, s->query);
    while (ecs_query_next(&it)) {
        const StarField *fields = ecs_field(&it, StarField, 0);
        const WorldTransform *transforms = ecs_field(&it, WorldTransform, 1);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_star_field_t *f = ecs_vec_append_t(NULL, &s->fields, box_star_field_t);
            f->scene = box_render_scene_field(&it, 2, i);
            box_render_star_record(&fields[i], transforms ? &transforms[i] : NULL,
                it.entities[i], &f->record);
        }
    }

    if (ecs_vec_count(&s->fields) > 1) {
        qsort(ecs_vec_first(&s->fields), (size_t)ecs_vec_count(&s->fields),
            sizeof(box_star_field_t), box_star_cmp);
    }
}

void box_render_star_views(
    ecs_world_t *world,
    box_render_stars_t *s,
    box_render_packet_t *p)
{
    const box_star_field_t *fields = NULL;
    int32_t i, j, count = ecs_vec_count(&p->views), field_count = 0;
    box_star_collect(world, s);
    fields = ecs_vec_first_t(&s->fields, box_star_field_t);
    field_count = ecs_vec_count(&s->fields);
    for (i = 0; i < count; i ++) {
        box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        const View *view = v->view && ecs_is_alive(world, v->view)
            ? ecs_get(world, v->view, View)
            : NULL;
        v->star_first = ecs_vec_count(&p->star_fields);
        v->star_count = 0;
        if (!field_count || !view || !view->lighting.enabled || v->camera_data.ortho ||
            !box_camera_has_depth(&v->camera_data))
        {
            continue;
        }

        for (j = 0; j < field_count; j ++) {
            if (fields[j].scene != v->scene || !fields[j].record.count) {
                continue;
            }

            *ecs_vec_append_t(NULL, &p->star_fields, box_render_star_field_t) =
                fields[j].record;
            v->star_count ++;
        }
    }
}

uint32_t box_render_star_hash(
    uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;

    return x;
}

/** Get a value of a star uniform in (0, 1].
 *
 * @param key Key of the star
 * @param k Index of the value
 * @return The value
 */
static float box_star_unit(
    uint32_t key,
    uint32_t k)
{
    uint32_t h = box_render_star_hash(key + k * BOX_STAR_GOLDEN);

    return ((float)(h >> 8) + 1.0f) / 16777216.0f;
}

void box_render_star_blackbody(
    float kelvin,
    float *out)
{
    float t = box_star_clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
    float r = t <= 66.0f ? 255.0f : 329.698727446f * powf(glm_max(t - 60.0f, 1.0f),
        -0.1332047592f);
    float g = t <= 66.0f
        ? 99.4708025861f * logf(t) - 161.1195681661f
        : 288.1221695283f * powf(glm_max(t - 60.0f, 1.0f), -0.0755148492f);
    float b = t >= 66.0f ? 255.0f : (t <= 19.0f ? 0.0f :
        138.5177312231f * logf(glm_max(t - 10.0f, 1.0f)) - 305.0447927307f);
    out[0] = glm_clamp(r, 0.0f, 255.0f);
    out[1] = glm_clamp(g, 0.0f, 255.0f);
    out[2] = glm_clamp(b, 0.0f, 255.0f);
}

/** Decode a display encoded channel from 0 to 1 to linear.
 *
 * @param v The channel
 * @return The linear value
 */
static float box_star_linear(
    float v)
{
    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

void box_render_star_sample(
    const box_render_star_field_t *f,
    uint32_t index,
    box_render_star_t *out)
{
    uint32_t key = box_render_star_hash(index ^ box_render_star_hash(f->seed + BOX_STAR_GOLDEN));
    float u[6], local[3], y, rxz, phi, bb[3];
    int32_t k;
    for (k = 0; k < 6; k ++) {
        u[k] = box_star_unit(key, (uint32_t)k);
    }

    phi = 2.0f * BOX_STAR_PI * u[1];
    if (u[0] <= f->band) {
        float g = sqrtf(-2.0f * logf(u[2])) * cosf(2.0f * BOX_STAR_PI * u[3]);
        y = sinf(glm_clamp(g * f->band_width, -0.5f * BOX_STAR_PI, 0.5f * BOX_STAR_PI));
    } else {
        y = 2.0f * u[2] - 1.0f;
    }

    rxz = sqrtf(glm_max(1.0f - y * y, 0.0f));
    local[0] = rxz * cosf(phi);
    local[1] = y;
    local[2] = rxz * sinf(phi);
    for (k = 0; k < 3; k ++) {
        out->direction[k] = local[0] * f->rotation[0][k] + local[1] * f->rotation[1][k] +
            local[2] * f->rotation[2][k];
    }

    out->relative = f->falloff > 0.0f ? powf(u[4], f->falloff) : 1.0f;
    out->temperature = f->temperature_min + (f->temperature_max - f->temperature_min) * u[5];
    out->radius = f->size * sqrtf(out->relative);
    if (out->radius < BOX_RENDER_STAR_MIN_RADIUS) {
        out->drawn = 0.0f;
        out->peak = f->brightness * BOX_STAR_PI * out->radius * out->radius;
    } else {
        out->drawn = out->radius;
        out->peak = f->brightness * out->radius * out->radius /
            (out->radius * out->radius + 1.0f / 12.0f);
    }

    box_render_star_blackbody(out->temperature, bb);
    for (k = 0; k < 3; k ++) {
        float c = box_star_linear(bb[k] / 255.0f);
        out->color[k] = 1.0f + (c - 1.0f) * f->saturation;
    }
}

box_render_stars_t* box_render_stars_new(
    ecs_world_t *world)
{
    box_render_stars_t *result = ecs_os_calloc_t(box_render_stars_t);
    ecs_vec_init_t(NULL, &result->fields, box_star_field_t, 0);
    result->query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "RenderStarFields" }),
        .terms = {
            { .id = ecs_id(StarField), .inout = EcsIn },
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

    return result;
}

void box_render_stars_free(
    box_render_stars_t *s)
{
    if (s) {
        ecs_vec_fini_t(NULL, &s->fields, box_star_field_t);
        ecs_os_free(s);
    }
}
