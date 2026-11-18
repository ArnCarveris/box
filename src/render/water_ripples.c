/**
 * @file render/water_ripples.c
 * @brief The fields of WaterRipples on the main thread: the water frame each field lives in, its
 * window, its fixed steps and the RippleSource entities and ripple_at disturbances it applies,
 * appended to the packet for the backend (gpu_ripples.c).
 *
 * The water frame is the integral of the WaterFlow velocity in double precision: ripples drift
 * with the flow because the frame moves, never by resampling. The window follows its entity by
 * whole cells of that frame, which the backend addresses modulo the resolution.
 *
 * A RippleSource pushes the water by its speed relative to the WaterFlow of the field, its world
 * velocity being the change of its world position over the frame delta time.
 */

#include "render.h"
#include <math.h>

/** A field of WaterRipples the extract keeps between frames */
typedef struct box_ripple_field_t {
    ecs_entity_t water;     /**< Entity with the WaterRipples */
    double frame[2];        /**< World XZ offset of the water frame, the integral of WaterFlow */
    double accumulator;     /**< Seconds not stepped yet */
    int64_t origin[2];      /**< Cell of the corner of the window in the water frame */
    int32_t resolution;     /**< Cells per side */
    float cell;             /**< World units of a cell */
    int64_t seen;           /**< Extract frame that saw it last */
} box_ripple_field_t;

/** A RippleSource the extract keeps between frames for its velocity */
typedef struct box_ripple_mover_t {
    double position[3];     /**< World position at the end of the frame it was seen last */
    float velocity[3];      /**< World velocity of that frame */
    int64_t seen;           /**< Extract frame that saw it last */
} box_ripple_mover_t;

/** A RippleSource of this frame */
typedef struct box_ripple_entry_t {
    ecs_entity_t scene;     /**< Scene root of the source, 0 for the default scene */
    double from[3];         /**< World position at the start of the frame */
    double to[3];           /**< World position at the end of the frame */
    float velocity[3];      /**< World velocity */
    float axis[2];          /**< Its X axis in world XZ, normalized */
    float extents[2];       /**< World units of its half size along its X and Z */
    float strength;         /**< RippleSource.strength, 0 when not finite */
} box_ripple_entry_t;

/** The state of the ripple extract */
typedef struct box_render_ripples_state_t {
    ecs_query_t *fields;    /**< WaterRipples with Water, their transform and scene */
    ecs_query_t *sources;   /**< RippleSource with a transform and scene */
    ecs_map_t by_water;     /**< Water entity to box_ripple_field_t* */
    ecs_map_t movers;       /**< Source entity to box_ripple_mover_t* */
    ecs_vec_t entries;      /**< box_ripple_entry_t, the sources of this frame */
    ecs_vec_t scenes;       /**< ecs_entity_t, scratch: the scenes that have a field this frame */
    ecs_vec_t keep;         /**< box_material_ripple_t, scratch: the disturbances that wait */
    int64_t frame;          /**< Frames extracted */
    bool warned;            /**< A scene with more than one field was reported */
} box_render_ripples_state_t;

int32_t box_render_ripples_resolution(
    int32_t resolution)
{
    int32_t result = BOX_MATERIAL_RIPPLES_RESOLUTION_MIN;
    while (result < BOX_MATERIAL_RIPPLES_RESOLUTION_MAX &&
        (int64_t)resolution * 4 >= (int64_t)result * 6)
    {
        result *= 2;
    }

    return result;
}

void box_render_ripples_window(
    double x,
    double z,
    double cell,
    int32_t resolution,
    int64_t *out)
{
    double cx = cell > 0.0 && isfinite(x) ? floor(x / cell) : 0.0;
    double cz = cell > 0.0 && isfinite(z) ? floor(z / cell) : 0.0;
    out[0] = (int64_t)cx - resolution / 2;
    out[1] = (int64_t)cz - resolution / 2;
}

float box_render_ripples_relative(
    const float *velocity,
    const WaterFlow *flow,
    float *out)
{
    float fx = 0.0f, fz = 0.0f, result;
    if (flow && isfinite(flow->velocity[0]) && isfinite(flow->velocity[1])) {
        fx = flow->velocity[0];
        fz = flow->velocity[1];
    }

    out[0] = isfinite(velocity[0]) ? velocity[0] - fx : 0.0f;
    out[1] = isfinite(velocity[1]) ? velocity[1] - fz : 0.0f;
    result = sqrtf(out[0] * out[0] + out[1] * out[1]);
    if (!(result >= BOX_RENDER_RIPPLE_SPEED_MIN)) {
        result = 0.0f;
    }

    return result;
}

float box_render_ripples_wave(
    const WaterRipples *ripples,
    float cell,
    float *courant2,
    float *keep)
{
    double step = BOX_RENDER_RIPPLE_STEP;
    double speed = isfinite(ripples->wave_speed) && ripples->wave_speed > 0.0f
        ? ripples->wave_speed : 0.0;
    double most = cell > 0.0f ? (double)BOX_RENDER_RIPPLE_COURANT * cell / step : 0.0;
    double damping = isfinite(ripples->damping)
        ? glm_clamp(ripples->damping, 0.0f, 1.0f) : 0.0;
    double courant;
    if (speed > most) {
        speed = most;
    }

    courant = cell > 0.0f ? speed * step / cell : 0.0;
    *courant2 = (float)(courant * courant);
    *keep = damping >= 1.0 ? 0.0f : (float)pow(1.0 - damping, step);

    return (float)speed;
}

/** Get the state of the ripple extract of a world.
 *
 * @param world The world
 * @return The state, NULL before box.render was imported
 */
static box_render_ripples_state_t* box_ripples_state(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.WaterRipples");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;

    return s ? s->ctx : NULL;
}

bool box_render_ripples_velocity(
    const ecs_world_t *world,
    ecs_entity_t source,
    float *out)
{
    const box_render_ripples_state_t *l = box_ripples_state(world);
    const box_ripple_mover_t *m = l
        ? ecs_map_get_deref(&l->movers, box_ripple_mover_t, source) : NULL;
    if (m) {
        out[0] = m->velocity[0];
        out[1] = m->velocity[1];
        out[2] = m->velocity[2];
    }

    return m != NULL;
}

/** Collect the RippleSource entities of the frame and update their velocities; the ones that
 * were not seen in the previous frame start still.
 *
 * @param world The world
 * @param l The state
 * @param delta Seconds of the frame
 */
static void box_ripples_sources(
    ecs_world_t *world,
    box_render_ripples_state_t *l,
    double delta)
{
    ecs_vec_clear(&l->entries);
    ecs_iter_t it = ecs_query_iter(world, l->sources);
    while (ecs_query_next(&it)) {
        const RippleSource *sources = ecs_field(&it, RippleSource, 0);
        const WorldTransform *transforms = ecs_field(&it, WorldTransform, 1);
        int32_t i, k;
        for (i = 0; i < it.count; i ++) {
            WorldTransform buf;
            const WorldTransform *t = box_transform_world_field(world, &it, transforms, i, &buf);
            const RippleSource *src = &sources[i];
            if (!t) {
                continue;
            }

            box_ripple_mover_t **slot = (box_ripple_mover_t**)ecs_map_ensure(
                &l->movers, it.entities[i]);
            box_ripple_mover_t *m = *slot;
            bool known = m && m->seen == l->frame - 1;
            if (!m) {
                m = *slot = ecs_os_calloc_t(box_ripple_mover_t);
            }

            box_ripple_entry_t *e = ecs_vec_append_t(NULL, &l->entries, box_ripple_entry_t);
            ecs_os_memset_t(e, 0, box_ripple_entry_t);
            e->scene = box_render_scene_field(&it, 2, i);
            for (k = 0; k < 3; k ++) {
                e->to[k] = t->m[3][k];
                e->from[k] = known ? m->position[k] : e->to[k];
                m->velocity[k] = known && delta > 0.0
                    ? (float)((e->to[k] - e->from[k]) / delta) : 0.0f;
                m->position[k] = e->to[k];
                e->velocity[k] = m->velocity[k];
            }

            m->seen = l->frame;
            float ax = t->m[0][0], az = t->m[0][2];
            float len = sqrtf(ax * ax + az * az);
            if (!(len > 1e-6f) || !isfinite(len)) {
                ax = 1.0f;
                az = 0.0f;
            } else {
                ax /= len;
                az /= len;
            }

            e->axis[0] = ax;
            e->axis[1] = az;
            float sx = glm_vec3_norm(ECS_CONST_CAST(float*, t->m[0]));
            float sz = glm_vec3_norm(ECS_CONST_CAST(float*, t->m[2]));
            e->extents[0] = isfinite(src->extents[0]) && isfinite(sx)
                ? fabsf(src->extents[0]) * sx : 0.0f;
            e->extents[1] = isfinite(src->extents[1]) && isfinite(sz)
                ? fabsf(src->extents[1]) * sz : 0.0f;
            e->strength = isfinite(src->strength) ? src->strength : 0.0f;
        }
    }

    ecs_map_iter_t mit = ecs_map_iter(&l->movers);
    while (ecs_map_next(&mit)) {
        box_ripple_mover_t *m = ecs_map_ptr(&mit);
        if (m->seen != l->frame) {
            ecs_os_free(m);
            ecs_map_remove(&l->movers, ecs_map_key(&mit));
            mit = ecs_map_iter(&l->movers);
        }
    }
}

/** Check whether a source of a field overlaps its window at some time of the frame.
 *
 * @param s The source, in cells of the window
 * @param resolution Cells per side
 * @return Whether its ellipse may touch a cell of the window
 */
static bool box_ripples_overlaps(
    const box_render_ripple_source_t *s,
    int32_t resolution)
{
    float r = glm_max(s->extents[0], s->extents[1]) + 1.0f;
    float lo_x = glm_min(s->from[0], s->to[0]) - r, hi_x = glm_max(s->from[0], s->to[0]) + r;
    float lo_z = glm_min(s->from[1], s->to[1]) - r, hi_z = glm_max(s->from[1], s->to[1]) + r;

    return hi_x >= 0.0f && hi_z >= 0.0f && lo_x <= (float)resolution &&
        lo_z <= (float)resolution;
}

/** Append the RippleSource entities of the scene of a field that overlap its window and move
 * relative to its water.
 *
 * @param p The packet
 * @param l The state
 * @param rec The record of the field, whose sources are appended
 * @param flow WaterFlow of the Water, NULL for still water
 * @param from_frame Water frame offset at the start of the frame
 * @param f The field
 */
static void box_ripples_push_sources(
    box_render_packet_t *p,
    const box_render_ripples_state_t *l,
    box_render_water_ripples_t *rec,
    const WaterFlow *flow,
    const double *from_frame,
    const box_ripple_field_t *f)
{
    const box_ripple_entry_t *entries = ecs_vec_first_t(&l->entries, box_ripple_entry_t);
    int32_t i, count = ecs_vec_count(&l->entries), pushed = 0;
    double cell = f->cell;
    for (i = 0; i < count && pushed < BOX_RENDER_RIPPLE_SOURCES; i ++) {
        const box_ripple_entry_t *e = &entries[i];
        float velocity[2] = { e->velocity[0], e->velocity[2] }, relative[2];
        box_render_ripple_source_t s;
        if (e->scene != rec->scene || e->strength == 0.0f) {
            continue;
        }

        float speed = box_render_ripples_relative(velocity, flow, relative);
        if (speed <= 0.0f || !(e->extents[0] > 0.0f) || !(e->extents[1] > 0.0f)) {
            continue;
        }

        ecs_os_memset_t(&s, 0, box_render_ripple_source_t);
        s.from[0] = (float)((e->from[0] - from_frame[0]) / cell - (double)f->origin[0]);
        s.from[1] = (float)((e->from[2] - from_frame[1]) / cell - (double)f->origin[1]);
        s.to[0] = (float)((e->to[0] - f->frame[0]) / cell - (double)f->origin[0]);
        s.to[1] = (float)((e->to[2] - f->frame[1]) / cell - (double)f->origin[1]);
        s.extents[0] = (float)(e->extents[0] / cell);
        s.extents[1] = (float)(e->extents[1] / cell);
        s.axis[0] = e->axis[0];
        s.axis[1] = e->axis[1];
        s.amount = (float)(-(double)e->strength * BOX_RENDER_RIPPLE_PUSH * speed *
            BOX_RENDER_RIPPLE_STEP);
        if (!box_ripples_overlaps(&s, (int32_t)rec->resolution)) {
            continue;
        }

        *ecs_vec_append_t(NULL, &p->ripple_sources, box_render_ripple_source_t) = s;
        rec->source_count ++;
        pushed ++;
    }
}

/** Append the queued ripple_at disturbances of the Water of a field that steps this frame, and
 * mark the ones it took.
 *
 * @param p The packet
 * @param queue The queue, box_material_ripple_t
 * @param taken One flag per entry of the queue, set for the ones applied
 * @param rec The record of the field
 * @param f The field
 */
static void box_ripples_push_impulses(
    box_render_packet_t *p,
    const ecs_vec_t *queue,
    bool *taken,
    box_render_water_ripples_t *rec,
    const box_ripple_field_t *f)
{
    int32_t i, count = queue ? ecs_vec_count(queue) : 0, pushed = 0;
    double cell = f->cell;
    for (i = 0; i < count && pushed < BOX_RENDER_RIPPLE_IMPULSES; i ++) {
        const box_material_ripple_t *r = ecs_vec_get_t(queue, box_material_ripple_t, i);
        box_render_ripple_source_t s;
        if (r->water != f->water || taken[i]) {
            continue;
        }

        taken[i] = true;
        ecs_os_memset_t(&s, 0, box_render_ripple_source_t);
        s.to[0] = s.from[0] = (float)(((double)r->position[0] - f->frame[0]) / cell -
            (double)f->origin[0]);
        s.to[1] = s.from[1] = (float)(((double)r->position[2] - f->frame[1]) / cell -
            (double)f->origin[1]);
        s.extents[0] = s.extents[1] = (float)((double)r->radius / cell);
        s.axis[0] = 1.0f;
        s.amount = -r->strength;
        s.once = 1;
        pushed ++;
        if (!box_ripples_overlaps(&s, (int32_t)rec->resolution)) {
            continue;
        }

        *ecs_vec_append_t(NULL, &p->ripple_sources, box_render_ripple_source_t) = s;
        rec->source_count ++;
    }
}

/** Check whether a scene already has a field this frame, and remember it.
 *
 * @param l The state
 * @param scene The scene root
 * @return Whether it had one
 */
static bool box_ripples_scene_taken(
    box_render_ripples_state_t *l,
    ecs_entity_t scene)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&l->scenes) && !result; i ++) {
        result = *ecs_vec_get_t(&l->scenes, ecs_entity_t, i) == scene;
    }

    if (!result) {
        *ecs_vec_append_t(NULL, &l->scenes, ecs_entity_t) = scene;
    }

    return result;
}

/** Step the water frame of a field, place its window and append its record and sources.
 *
 * @param world The world
 * @param p The packet
 * @param l The state
 * @param water The Water entity
 * @param ripples Its WaterRipples
 * @param t Its world transform, NULL for none
 * @param scene Its scene root
 * @param queue The ripple_at queue, NULL for none
 * @param taken One flag per entry of the queue
 */
static void box_ripples_field(
    ecs_world_t *world,
    box_render_packet_t *p,
    box_render_ripples_state_t *l,
    ecs_entity_t water,
    const WaterRipples *ripples,
    const WorldTransform *t,
    ecs_entity_t scene,
    const ecs_vec_t *queue,
    bool *taken)
{
    double delta = isfinite(p->delta) && p->delta > 0.0f ? (double)p->delta : 0.0;
    int32_t resolution = box_render_ripples_resolution(ripples->resolution), k;
    float cell = ripples->size / (float)resolution;
    bool reset = false;
    box_ripple_field_t **slot = (box_ripple_field_t**)ecs_map_ensure(&l->by_water, water);
    box_ripple_field_t *f = *slot;
    if (!f) {
        f = *slot = ecs_os_calloc_t(box_ripple_field_t);
        f->water = water;
        reset = true;
    }

    if (f->resolution != resolution || f->cell != cell || f->seen != l->frame - 1) {
        reset = true;
    }

    f->resolution = resolution;
    f->cell = cell;
    f->seen = l->frame;

    const WaterFlow *flow = ecs_get(world, water, WaterFlow);
    double from_frame[2] = { f->frame[0], f->frame[1] };
    if (flow && isfinite(flow->velocity[0]) && isfinite(flow->velocity[1])) {
        f->frame[0] += (double)flow->velocity[0] * delta;
        f->frame[1] += (double)flow->velocity[1] * delta;
    }

    uint32_t steps = 0;
    f->accumulator += delta;
    while (steps < BOX_RENDER_RIPPLE_SUBSTEPS &&
        f->accumulator >= BOX_RENDER_RIPPLE_STEP * (1.0 - 1e-4))
    {
        f->accumulator -= BOX_RENDER_RIPPLE_STEP;
        steps ++;
    }

    if (f->accumulator >= BOX_RENDER_RIPPLE_STEP) {
        f->accumulator = 0.0;
    }

    WorldTransform buf;
    const WorldTransform *center = t;
    if (ripples->follow && ecs_is_alive(world, ripples->follow)) {
        const WorldTransform *ft = box_transform_world(world, ripples->follow, &buf);
        center = ft ? ft : center;
    }

    double cx = center ? (double)center->m[3][0] : 0.0;
    double cz = center ? (double)center->m[3][2] : 0.0;
    int64_t origin[2];
    box_render_ripples_window(cx - f->frame[0], cz - f->frame[1], cell, resolution, origin);

    box_render_water_ripples_t *rec = ecs_vec_append_t(
        NULL, &p->water_ripples, box_render_water_ripples_t);
    ecs_os_memset_t(rec, 0, box_render_water_ripples_t);
    rec->water = water;
    rec->scene = scene;
    rec->resolution = (uint32_t)resolution;
    rec->steps = steps;
    rec->reset = reset;
    for (k = 0; k < 2; k ++) {
        int64_t shift = reset ? resolution : origin[k] - f->origin[k];
        shift = shift < -resolution ? -resolution : shift;
        rec->shift[k] = (int32_t)(shift > resolution ? resolution : shift);
        rec->origin[k] = (int32_t)(((origin[k] % resolution) + resolution) % resolution);
        f->origin[k] = origin[k];
    }

    rec->cell = cell;
    box_render_ripples_wave(ripples, cell, &rec->courant2, &rec->keep);
    rec->corner[0] = (float)((double)origin[0] * cell + f->frame[0]);
    rec->corner[1] = (float)((double)origin[1] * cell + f->frame[1]);
    rec->height = t ? t->m[3][1] : 0.0f;
    rec->strength = isfinite(ripples->strength) ? ripples->strength : 0.0f;
    rec->source_first = ecs_vec_count(&p->ripple_sources);
    if (steps) {
        box_ripples_push_sources(p, l, rec, flow, from_frame, f);
        box_ripples_push_impulses(p, queue, taken, rec, f);
    }
}

/** Step the fields of WaterRipples of the world and append them to the packet.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractRipples(
    ecs_iter_t *it)
{
    box_render_ripples_state_t *l = it->ctx;
    ecs_world_t *world = it->world;
    box_render_packet_t *p = box_render_packet(world);
    ecs_vec_t *queue = box_material_ripples_queue(world);
    int32_t queued = queue ? ecs_vec_count(queue) : 0, i;
    bool *taken = queued ? ecs_os_calloc_n(bool, queued) : NULL;
    bool *known = queued ? ecs_os_calloc_n(bool, queued) : NULL;
    double delta = isfinite(p->delta) && p->delta > 0.0f ? (double)p->delta : 0.0;
    l->frame ++;
    ecs_vec_clear(&l->scenes);
    box_ripples_sources(world, l, delta);

    ecs_iter_t fit = ecs_query_iter(world, l->fields);
    while (ecs_query_next(&fit)) {
        const WaterRipples *ripples = ecs_field(&fit, WaterRipples, 0);
        const WorldTransform *transforms = ecs_field(&fit, WorldTransform, 2);
        for (i = 0; i < fit.count; i ++) {
            const WaterRipples *r = &ripples[i];
            ecs_entity_t scene = box_render_scene_field(&fit, 3, i);
            if (!isfinite(r->size) || r->size <= 0.0f ||
                !(box_render_scene_features(world, scene) & BOX_RENDER_FEATURE_WATER))
            {
                continue;
            }

            if (box_ripples_scene_taken(l, scene)) {
                if (!l->warned) {
                    char *name = ecs_get_path(world, fit.entities[i]);
                    ecs_err("WaterRipples of '%s': its scene already has a field, only the "
                        "first one simulates", name);
                    ecs_os_free(name);
                    l->warned = true;
                }

                continue;
            }

            WorldTransform buf;
            const WorldTransform *t = box_transform_world_field(world, &fit, transforms, i,
                &buf);
            int32_t first = ecs_vec_count(&p->water_ripples);
            box_ripples_field(world, p, l, fit.entities[i], r, t, scene, queue, taken);
            const box_render_water_ripples_t *rec = ecs_vec_get_t(
                &p->water_ripples, box_render_water_ripples_t, first);
            int32_t j;
            for (j = 0; j < queued; j ++) {
                const box_material_ripple_t *q = ecs_vec_get_t(queue, box_material_ripple_t, j);
                if (q->water == rec->water) {
                    known[j] = true;
                }
            }
        }
    }

    if (queued) {
        ecs_vec_clear(&l->keep);
        for (i = 0; i < queued; i ++) {
            if (known[i] && !taken[i]) {
                *ecs_vec_append_t(NULL, &l->keep, box_material_ripple_t) =
                    *ecs_vec_get_t(queue, box_material_ripple_t, i);
            }
        }

        int32_t kept = ecs_vec_count(&l->keep);
        ecs_vec_set_count_t(NULL, queue, box_material_ripple_t, kept);
        if (kept) {
            ecs_os_memcpy_n(ecs_vec_first(queue), ecs_vec_first(&l->keep),
                box_material_ripple_t, kept);
        }
    }

    ecs_map_iter_t mit = ecs_map_iter(&l->by_water);
    while (ecs_map_next(&mit)) {
        box_ripple_field_t *f = ecs_map_ptr(&mit);
        if (f->seen != l->frame) {
            ecs_os_free(f);
            ecs_map_remove(&l->by_water, ecs_map_key(&mit));
            mit = ecs_map_iter(&l->by_water);
        }
    }

    ecs_os_free(taken);
    ecs_os_free(known);
}

/** Free the state of the ripple extract.
 *
 * @param ctx The state
 */
static void box_ripples_free(
    void *ctx)
{
    box_render_ripples_state_t *l = ctx;
    ecs_map_iter_t it = ecs_map_iter(&l->by_water);
    while (ecs_map_next(&it)) {
        ecs_os_free(ecs_map_ptr(&it));
    }

    it = ecs_map_iter(&l->movers);
    while (ecs_map_next(&it)) {
        ecs_os_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&l->by_water);
    ecs_map_fini(&l->movers);
    ecs_vec_fini_t(NULL, &l->entries, box_ripple_entry_t);
    ecs_vec_fini_t(NULL, &l->scenes, ecs_entity_t);
    ecs_vec_fini_t(NULL, &l->keep, box_material_ripple_t);
    ecs_os_free(l);
}

void box_render_water_ripples_init(
    ecs_world_t *world)
{
    box_render_ripples_state_t *l = ecs_os_calloc_t(box_render_ripples_state_t);
    ecs_map_init(&l->by_water, NULL);
    ecs_map_init(&l->movers, NULL);
    ecs_vec_init_t(NULL, &l->entries, box_ripple_entry_t, 0);
    ecs_vec_init_t(NULL, &l->scenes, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &l->keep, box_material_ripple_t, 0);
    l->fields = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "WaterRippleFields" }),
        .terms = {
            { .id = ecs_id(WaterRipples), .inout = EcsIn },
            { .id = ecs_id(Water), .inout = EcsInOutNone },
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
    l->sources = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "RippleSources" }),
        .terms = {
            { .id = ecs_id(RippleSource), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
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

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.WaterRipples" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractRipples,
        .ctx = l,
        .ctx_free = box_ripples_free
    });
}
