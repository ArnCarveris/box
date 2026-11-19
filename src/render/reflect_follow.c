/**
 * @file render/reflect_follow.c
 * @brief The scene probe of a ReflectionBake with follow_focus: placed above the focus of its
 * scene, moved once the focus is far from it, blending from its previous capture to the new one.
 */

#include "reflect.h"
#include <math.h>

/** Foci of the world a scene probe looks for the one of its scene among at most */
#define BOX_REFLECT_FOCI (16)

void box_reflect_drop_previous(
    box_reflect_scene_t *s)
{
    if (s->previous >= 0 && s->previous < ecs_vec_count(&s->slots)) {
        *ecs_vec_get_t(&s->slots, uint8_t, s->previous) = 0;
    }

    s->previous = -1;
    s->fade = 0.0f;
}

bool box_reflect_hold(
    box_reflect_scene_t *s,
    box_reflect_probe_t *probe)
{
    uint8_t *slots = ecs_vec_first(&s->slots);
    int32_t i, count = ecs_vec_count(&s->slots), free_slot = -1;
    bool result = false;
    if (probe->slot < 0 || !probe->captured || probe->packet) {
        goto done;
    }

    box_reflect_drop_previous(s);
    for (i = 0; i < count && free_slot < 0; i ++) {
        free_slot = slots[i] ? -1 : i;
    }

    if (free_slot < 0) {
        goto done;
    }

    slots[free_slot] = 1;
    s->previous = probe->slot;
    s->fade = 1.0f;
    probe->slot = free_slot;
    probe->captured = false;
    probe->queued = false;
    result = true;
done:
    return result;
}

void box_reflect_fade(
    box_reflect_scene_t *s,
    float delta)
{
    const box_reflect_probe_t *probe = s->scene_index >= 0
        ? ecs_vec_get_t(&s->probes, box_reflect_probe_t, s->scene_index)
        : NULL;
    if (s->previous < 0) {
        goto done;
    }

    if (!probe || probe->slot < 0) {
        box_reflect_drop_previous(s);
        goto done;
    }

    if (!probe->captured && !probe->packet) {
        goto done;
    }

    s->fade = s->follow_blend > 0 && isfinite(s->follow_blend)
        ? s->fade - glm_max(delta, 0.0f) / s->follow_blend
        : 0.0f;
    if (!(s->fade > 0.0f)) {
        box_reflect_drop_previous(s);
    }

done:
    return;
}

bool box_reflect_within(
    const float *point,
    const vec3 center,
    const vec3 half,
    float radius)
{
    float outside = 0.0f;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        float out = glm_max(fabsf(point[i] - center[i]) - fabsf(half[i]), 0.0f);
        outside += out * out;
    }

    return outside <= radius * radius;
}

bool box_reflect_focus(
    const ecs_world_t *world,
    ecs_entity_t scene,
    vec3 out)
{
    box_world_focus_t foci[BOX_REFLECT_FOCI];
    int32_t i, count = box_world_focus(world, foci, BOX_REFLECT_FOCI);
    bool result = false;
    for (i = 0; !result && i < count && i < BOX_REFLECT_FOCI; i ++) {
        const float *at = foci[i].position;
        if (foci[i].scene == scene && isfinite(at[0]) && isfinite(at[1]) && isfinite(at[2])) {
            glm_vec3_copy(foci[i].position, out);
            result = true;
        }
    }

    return result;
}

void box_reflect_place_focus(
    box_reflect_scene_t *s,
    const vec3 focus,
    float height,
    float distance)
{
    if (s->anchored && s->height == height &&
        !(glm_vec3_distance(ECS_CONST_CAST(float*, focus), s->anchor) > distance))
    {
        goto done;
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, focus), s->anchor);
    glm_vec3_copy(ECS_CONST_CAST(float*, focus), s->scene_capture);
    s->scene_capture[1] += height;
    s->height = height;
    s->anchored = true;
    s->placed = true;
done:
    return;
}
