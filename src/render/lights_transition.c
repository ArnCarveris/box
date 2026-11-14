/**
 * @file render/lights_transition.c
 * @brief Light transitions in the light extract: the lights that render their way out after
 * an on_remove, and the light records that patch the light buffers while a light transitions.
 */

#include "lights.h"

bool box_lights_list_transitions(
    const box_render_lights_t *l,
    const box_light_list_t *s)
{
    const box_render_light_transition_t *lights = NULL;
    const ecs_entity_t *entities = ecs_vec_first_t(&s->entities, ecs_entity_t);
    bool result = false;
    int32_t i;
    if (!box_render_transitions_lights(l->transitions, &lights)) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&s->entities) && !result; i ++) {
        result = box_render_transitions_light(l->transitions, entities[i]) != NULL;
    }
done:
    return result;
}

/** Get the uniform world scale the extract applied to the range of a point or spot light, from
 * the range of the light of the list and the range of its target (box_lights_point_light).
 *
 * @param lt The transition of the light
 * @param light The light of the list
 * @return The scale, 1 for a directional light
 */
static float box_lights_range_scale(
    const box_render_light_transition_t *lt,
    const box_render_light_t *light)
{
    float range, result = 1.0f;
    if (lt->kind) {
        ecs_os_memcpy(&range, &lt->value[ECS_SIZEOF(float)], ECS_SIZEOF(float));
        result = light->range / glm_max(range, BOX_LIGHTS_MIN_RANGE);
    }

    return result;
}

/** Get the per channel factor the extract applied to the color of a light after its Rgb and
 * intensity: the attenuation of a sun or a moon, the square of the world scale of a point or
 * spot light. A channel without color takes the mean of the others, and no color 1.
 *
 * @param lt The transition of the light
 * @param light The light of the list
 * @param range_scale The world scale of the light (box_lights_range_scale)
 * @param out Receives the factor
 */
static void box_lights_scale(
    const box_render_light_transition_t *lt,
    const box_render_light_t *light,
    float range_scale,
    float *out)
{
    float intensity, base, sum = 0.0f;
    int32_t c, known = 0;
    const uint8_t *rgb = &lt->rgb.r;
    glm_vec3_one(out);
    if (lt->kind) {
        glm_vec3_fill(out, range_scale * range_scale);
        goto done;
    }

    ecs_os_memcpy(&intensity, lt->value, ECS_SIZEOF(float));
    for (c = 0; c < 3; c ++) {
        base = (lt->has_rgb ? box_lights_srgb[rgb[c]] : 1.0f) * intensity;
        out[c] = -1.0f;
        if (base > 0.0f) {
            out[c] = light->color[c] / base;
            sum += out[c];
            known ++;
        }
    }

    for (c = 0; c < 3; c ++) {
        if (out[c] < 0.0f) {
            out[c] = known ? sum / (float)known : 1.0f;
        }
    }
done:
    return;
}

void box_lights_send_transitions(
    const box_render_lights_t *l,
    const box_light_list_t *s,
    box_render_packet_t *p,
    uint32_t data)
{
    const ecs_entity_t *entities = ecs_vec_first_t(&s->entities, ecs_entity_t);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->entities); i ++) {
        const box_render_light_transition_t *lt = box_render_transitions_light(
            l->transitions, entities[i]);
        box_render_transition_light_t target = {0};
        box_render_transition_record_t *r;
        uint32_t offset = 0;
        if (!lt) {
            continue;
        }

        target.kind = (uint32_t)lt->kind;
        target.rgb_node = lt->rgb_node;
        ecs_os_memcpy(target.values, lt->value,
            (lt->kind == 2 ? 4 : (lt->kind == 1 ? 2 : 1)) * ECS_SIZEOF(float));
        target.rgb[0] = lt->rgb.r;
        target.rgb[1] = lt->rgb.g;
        target.rgb[2] = lt->rgb.b;
        target.has_rgb = lt->has_rgb;
        target.range_scale = box_lights_range_scale(lt,
            ecs_vec_get_t(&s->lights, box_render_light_t, i));
        box_lights_scale(lt, ecs_vec_get_t(&s->lights, box_render_light_t, i),
            target.range_scale, target.scale);
        ecs_os_memcpy(box_render_packet_reserve(p, (uint32_t)sizeof(target), &offset),
            &target, ECS_SIZEOF(target));
        r = box_render_packet_transition(p, BOX_RENDER_TRANSITION_LIGHT, lt->node);
        r->data = offset;
        r->offset = data + (uint32_t)i * (uint32_t)sizeof(box_render_light_t);
    }
}

void box_lights_collect_leaving(
    ecs_world_t *world,
    box_render_lights_t *l)
{
    const box_render_light_transition_t *lights = NULL;
    int32_t i, count = box_render_transitions_lights(l->transitions, &lights);
    for (i = 0; i < count; i ++) {
        const box_render_light_transition_t *lt = &lights[i];
        const WorldTransform *transform;
        box_light_scene_t *s;
        if (!lt->leaving || !ecs_is_alive(world, lt->entity)) {
            continue;
        }

        transform = ecs_get(world, lt->entity, WorldTransform);
        if (!transform) {
            continue;
        }

        s = box_lights_ensure(l, box_render_scene_of(world, lt->entity));
        s->has_lights = true;
        if (lt->kind == 1) {
            PointLight p;
            ecs_os_memcpy(&p, lt->value, ECS_SIZEOF(PointLight));
            box_lights_point_light(s, lt->entity, transform, lt->has_rgb ? &lt->rgb : NULL,
                &p, lt);
        } else if (lt->kind == 2) {
            SpotLight sp;
            ecs_os_memcpy(&sp, lt->value, ECS_SIZEOF(SpotLight));
            box_lights_spot_light(s, lt->entity, transform, lt->has_rgb ? &lt->rgb : NULL,
                &sp, lt);
        }
    }
}

void box_render_lights_patch(
    const box_render_transition_light_t *target,
    const float *values,
    const Rgb *rgb,
    box_render_light_t *light)
{
    int32_t c;
    box_lights_color(target->has_rgb ? rgb : NULL, glm_max(values[0], 0.0f), light->color);
    for (c = 0; c < 3; c ++) {
        light->color[c] *= target->scale[c];
    }

    if (target->kind) {
        light->range = glm_max(values[1], BOX_LIGHTS_MIN_RANGE) * target->range_scale;
    }

    if (target->kind == 2) {
        box_lights_cone(values[2], values[3], light);
    }
}
