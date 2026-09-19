/**
 * @file particle/budget.c
 * @brief The share of the budget an emitter gets and the views that keep it alive.
 *
 * Ten million particles are not ten million particles per emitter: a world has
 * a ceiling on what every pool together may hold and on what a frame may spawn,
 * and an emitter that asks for more than its share of either gets the share
 * instead of the ask. The share is proportional, so an emitter that asked for
 * half of what was asked for keeps half of what there is, and it is computed
 * from the asks alone, which do not change while a scene stands still: a pool
 * only changes size when the scene changes, never because a frame was busy.
 *
 * What an emitter is worth is decided per view. The particles of an emitter
 * stay inside a sphere around it that its speed, its lifetime and its
 * acceleration bound, and an emitter whose sphere no view can see is not
 * stepped at all while one that is far from every camera is stepped every other
 * frame with the two frames in one step, which is the same motion at half the
 * cost.
 */

#include "particle_internal.h"
#include <math.h>

/** Seconds of a lifetime the reach of an emitter is bounded over at most.
 *
 * An emitter whose particles live for minutes would otherwise reach a sphere no
 * frustum ever rejects, which costs the culling its point.
 */
#define BOX_PARTICLE_REACH_SECONDS (30.0f)

float box_particle_reach(
    const ParticleEmitter *emitter,
    const ParticleEmitterImpl *impl)
{
    float extent = emitter->radius > 0.0f ? emitter->radius : 0.0f;
    float box = vec3_length(emitter->extents);
    if (box > extent) {
        extent = box;
    }

    if (impl->extent > extent) {
        extent = impl->extent;
    }

    float life = emitter->lifetime.max > emitter->lifetime.min ?
        emitter->lifetime.max : emitter->lifetime.min;
    if (life > BOX_PARTICLE_REACH_SECONDS) {
        life = BOX_PARTICLE_REACH_SECONDS;
    }

    if (life < 0.0f) {
        life = 0.0f;
    }

    float speed = emitter->speed.max > emitter->speed.min ?
        emitter->speed.max : emitter->speed.min;
    float size = emitter->size.max > emitter->size.min ?
        emitter->size.max : emitter->size.min;
    float accel = vec3_length(emitter->acceleration);

    return extent + speed * life + 0.5f * accel * life * life + size;
}

uint32_t box_particle_share(
    uint32_t want,
    uint64_t total,
    uint32_t budget)
{
    uint32_t result = want;
    if (!budget || !want || total <= (uint64_t)budget) {
        goto done;
    }

    result = (uint32_t)(((uint64_t)want * (uint64_t)budget) / total);
    if (!result) {
        result = 1;
    }

done:
    return result;
}

/** Distance from the sphere of an emitter to the nearest camera of the views.
 *
 * @param views The views, one frustum and one eye each
 * @param count Number of views
 * @param origin World position of the emitter
 * @param reach Radius its particles stay inside
 * @return The distance, zero when a camera is inside the sphere
 */
static float box_particle_distance(
    const box_particle_view_t *views,
    int32_t count,
    const float *origin,
    float reach)
{
    float result = -1.0f;
    int32_t i;
    for (i = 0; i < count; i ++) {
        float dx = origin[0] - views[i].eye[0];
        float dy = origin[1] - views[i].eye[1];
        float dz = origin[2] - views[i].eye[2];
        float distance = sqrtf(dx * dx + dy * dy + dz * dz) - reach;
        if (distance < 0.0f) {
            distance = 0.0f;
        }

        if (result < 0.0f || distance < result) {
            result = distance;
        }
    }

    return result < 0.0f ? 0.0f : result;
}

int32_t box_particle_cull(
    const box_particle_view_t *views,
    int32_t count,
    const float *origin,
    float reach,
    const ParticleSettings *settings)
{
    int32_t result = BOX_PARTICLE_STEP_FULL;
    if (!count || !settings) {
        goto done;
    }

    float distance = box_particle_distance(views, count, origin, reach);
    if (settings->cull_distance > 0.0f && distance > settings->cull_distance) {
        result = BOX_PARTICLE_STEP_NONE;
        goto done;
    }

    if (settings->cull) {
        float margin = reach + (settings->cull_margin > 0.0f ?
            settings->cull_margin : 0.0f);
        bool seen = false;
        int32_t i;
        for (i = 0; i < count && !seen; i ++) {
            seen = box_frustum_sphere(&views[i].frustum, origin, margin);
        }

        if (!seen) {
            result = BOX_PARTICLE_STEP_NONE;
            goto done;
        }
    }

    if (settings->step_distance > 0.0f && distance > settings->step_distance) {
        result = BOX_PARTICLE_STEP_HALF;
    }

done:
    return result;
}

void box_particle_views(
    const box_packet_t *p,
    ecs_vec_t *out)
{
    ecs_vec_clear(out);
    uint32_t i;
    for (i = 0; i < p->views.count; i ++) {
        const box_view_t *view = box_vec_at(&p->views, i);
        box_particle_view_t *entry = ecs_vec_append_t(NULL, out, box_particle_view_t);
        box_frustum_of_view(view, &entry->frustum);
        entry->eye[0] = view->eye[0];
        entry->eye[1] = view->eye[1];
        entry->eye[2] = view->eye[2];
    }
}
