/**
 * @file audio/spatial.c
 * @brief The spatial model of box.audio: the listener, distance attenuation, equal power
 * panning and the distance filter, computed on the main thread and sent as the volume, pan
 * and cutoff of a voice.
 */

#include "module.h"
#include <math.h>

/** Shortest min_distance, so that the inverse distance stays defined */
#define BOX_AUDIO_SPATIAL_MIN (1e-3f)

/** Inverse distance gain, unity within min_distance.
 *
 * @param d Distance
 * @param min Full volume within this distance, more than 0
 * @param rolloff How fast the gain falls, at least 0
 * @return The gain, 0..1
 */
static float box_audio_inverse(
    float d,
    float min,
    float rolloff)
{
    return d <= min ? 1.0f : min / (min + rolloff * (d - min));
}

float box_audio_spatial_cutoff(
    const AudioSpatial *spatial,
    float distance)
{
    float min, max, cutoff, t, result = 0;
    if (!spatial || !(spatial->cutoff > 0) || !(spatial->cutoff < BOX_AUDIO_SPATIAL_OPEN)) {
        goto done;
    }

    min = spatial->min_distance > BOX_AUDIO_SPATIAL_MIN ?
        spatial->min_distance : BOX_AUDIO_SPATIAL_MIN;
    max = spatial->max_distance;
    cutoff = spatial->cutoff > BOX_AUDIO_SPATIAL_CUTOFF_MIN ?
        spatial->cutoff : BOX_AUDIO_SPATIAL_CUTOFF_MIN;
    if (!(distance > min)) {
        goto done;
    }

    t = max > min ? (distance - min) / (max - min) : 1.0f;
    result = t < 1.0f ? BOX_AUDIO_SPATIAL_OPEN * powf(cutoff / BOX_AUDIO_SPATIAL_OPEN, t) :
        cutoff;
    if (!(result > 0) || !(result < BOX_AUDIO_SPATIAL_OPEN)) {
        result = 0;
    }

done:
    return result;
}

float box_audio_spatial_gain(
    const AudioSpatial *spatial,
    const mat4 listener,
    const vec3 position,
    float *pan)
{
    return box_audio_spatial_place(spatial, listener, position, pan, NULL);
}

float box_audio_spatial_place(
    const AudioSpatial *spatial,
    const mat4 listener,
    const vec3 position,
    float *pan,
    float *cutoff)
{
    AudioSpatial s = spatial ? *spatial :
        (AudioSpatial){ .min_distance = 1.0f, .max_distance = 50.0f, .rolloff = 1.0f };
    float min = s.min_distance > BOX_AUDIO_SPATIAL_MIN ? s.min_distance : BOX_AUDIO_SPATIAL_MIN;
    float rolloff = s.rolloff > 0 ? s.rolloff : 0;
    float dx = position[0] - listener[3][0];
    float dy = position[1] - listener[3][1];
    float dz = position[2] - listener[3][2];
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    float rx = listener[0][0], ry = listener[0][1], rz = listener[0][2];
    float rl = sqrtf(rx * rx + ry * ry + rz * rz);
    float lateral = rl > 0 ? (dx * rx + dy * ry + dz * rz) / rl : dx;
    float reach = d > min ? d : min;
    float result = 0, p = 0, g, end;
    if (d < s.max_distance) {
        g = box_audio_inverse(d, min, rolloff);
        end = box_audio_inverse(s.max_distance, min, rolloff);
        result = end < 1.0f ? (g - end) / (1.0f - end) : 1.0f;
        result = result > 0 ? (result < 1.0f ? result : 1.0f) : 0;
    }

    p = lateral / reach;
    p = p > -1.0f ? (p < 1.0f ? p : 1.0f) : -1.0f;
    if (p != p) {
        p = 0;
    }

    if (result != result) {
        result = 0;
    }

    if (pan) {
        *pan = p;
    }

    if (cutoff) {
        *cutoff = spatial && spatial->cutoff > 0 ? box_audio_spatial_cutoff(spatial, d) : 0;
    }

    return result;
}

void box_audio_listener_find(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_listener_t *listener)
{
    ecs_iter_t it = ecs_query_iter(world, state->listeners);
    WorldTransform best_buf;
    const WorldTransform *best = NULL;
    int32_t best_order = 0, count = 0, i;
    *listener = (box_audio_listener_t){0};
    for (i = 0; i < 4; i ++) {
        listener->m[i][i] = 1.0f;
    }

    while (ecs_query_next(&it)) {
        const WorldTransform *wt = ecs_field(&it, WorldTransform, 0);
        for (i = 0; i < it.count; i ++) {
            count ++;
            if (!listener->entity || it.entities[i] < listener->entity) {
                listener->entity = it.entities[i];
                best = &wt[i];
            }
        }
    }

    if (count > 1 && !state->listener_warned) {
        char *path = ecs_get_path(world, listener->entity);
        ecs_warn("box.audio: %d entities have Listener, '%s' listens", count, path);
        ecs_os_free(path);
        state->listener_warned = true;
    } else if (count <= 1) {
        state->listener_warned = false;
    }

    if (best) {
        best = box_transform_world(world, listener->entity, &best_buf);
        ecs_os_memcpy(listener->m, best->m, ECS_SIZEOF(mat4));
        goto done;
    }

    it = ecs_query_iter(world, state->views);
    while (ecs_query_next(&it)) {
        const View *views = ecs_field(&it, View, 0);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t camera = views[i].camera;
            WorldTransform wt_buf;
            const WorldTransform *wt = camera && ecs_is_alive(world, camera) ?
                box_transform_world(world, camera, &wt_buf) : NULL;
            if (wt && (!best ||
                box_view_compare(views[i].order, it.entities[i], best_order, listener->entity) < 0))
            {
                best_buf = *wt;
                best = &best_buf;
                best_order = views[i].order;
                listener->entity = it.entities[i];
            }
        }
    }

    if (best) {
        ecs_os_memcpy(listener->m, best->m, ECS_SIZEOF(mat4));
        listener->entity = ecs_get(world, listener->entity, View)->camera;
    }

done:
    return;
}
