/**
 * @file render/extract_lights.c
 * @brief Extracts the point and spot lights inside the view frustum into the packet.
 *
 * Static lights are marked so baked instances skip them and are also carried
 * unculled in a second array for the bake. Layer membership is read once per table,
 * and a light hidden by a Hidden switch leaves both arrays.
 */

#include "render_internal.h"

/** Range a light without one gets */
#define BOX_LIGHT_RANGE (10.0f)

/** Outer cone half angle in degrees a spot light without one gets */
#define BOX_LIGHT_OUTER (30.0f)

/** Part of the outer half angle an inner half angle of zero becomes */
#define BOX_LIGHT_INNER_RATIO (0.75f)

/** Radians in one degree */
#define BOX_LIGHT_RADIANS (3.14159265f / 180.0f)

/** Copy the cluster settings of a view into it, filling in defaults.
 *
 * @param world The world
 * @param e The view entity
 * @param v The view to fill
 */
static void box_light_settings(
    ecs_world_t *world,
    ecs_entity_t e,
    box_view_t *v)
{
    const ClusterSettings *s = box_render_view_setting(world, e, ecs_id(ClusterSettings));
    box_cluster_t *c = &v->cluster;
    c->x = s && s->x > 0 ? (uint32_t)s->x : BOX_CLUSTER_X;
    c->y = s && s->y > 0 ? (uint32_t)s->y : BOX_CLUSTER_Y;
    c->z = s && s->z > 0 ? (uint32_t)s->z : BOX_CLUSTER_Z;
    c->max_lights = s && s->max_lights > 0 ?
        (uint32_t)s->max_lights : BOX_CLUSTER_MAX_LIGHTS;
}

/** Fill the fields every light kind has.
 *
 * @param transform World transform of the light
 * @param rgb Color of the light, NULL for white
 * @param intensity Radiance scale, zero takes the default
 * @param range Distance at which the light reaches zero, zero takes the default
 * @param out The light to fill
 */
static void box_light_fill(
    const WorldTransform *transform,
    const Rgb *rgb,
    float intensity,
    float range,
    box_light_t *out)
{
    vec3 position = mat4_get_translation(&transform->m);
    vec3 color = vec3_new(1, 1, 1);
    if (rgb) {
        color = vec3_new(box_srgb_to_linear(rgb->r), box_srgb_to_linear(rgb->g),
            box_srgb_to_linear(rgb->b));
    }

    out->position[0] = position.x;
    out->position[1] = position.y;
    out->position[2] = position.z;
    out->range = range > 0 ? range : BOX_LIGHT_RANGE;
    out->color[0] = color.x;
    out->color[1] = color.y;
    out->color[2] = color.z;
    out->intensity = intensity > 0 ? intensity : 1.0f;
    out->direction[0] = 0;
    out->direction[1] = 0;
    out->direction[2] = 1;
    out->cos_inner = 1.0f;
    out->cos_outer = -1.0f;
    out->type = BOX_LIGHT_POINT;
    out->is_static = 0;
    out->modulator = 0;
}

/** Turn a light into a spot light: forward axis of the transform and cone cosines.
 *
 * @param light The spot light component
 * @param transform World transform of the light
 * @param out The light to fill
 */
static void box_light_cone(
    const SpotLight *light,
    const WorldTransform *transform,
    box_light_t *out)
{
    vec3 dir = vec3_normalize(mat4_transform_dir(&transform->m, vec3_new(0, 0, 1)));
    float outer = light->outer_angle > 0 ? light->outer_angle : BOX_LIGHT_OUTER;
    float inner = light->inner_angle > 0 ? light->inner_angle : outer * BOX_LIGHT_INNER_RATIO;
    if (inner > outer) {
        inner = outer;
    }

    out->direction[0] = dir.x;
    out->direction[1] = dir.y;
    out->direction[2] = dir.z;
    out->cos_inner = cosf(inner * BOX_LIGHT_RADIANS);
    out->cos_outer = cosf(outer * BOX_LIGHT_RADIANS);
    out->type = BOX_LIGHT_SPOT;
}

/** Extract one light of a table into the packet when it survives the culling.
 *
 * @param it The light query iterator
 * @param i Index of the entity in the table
 * @param frustum The frustum of the view
 * @param is_static Neither the light nor an ancestor carries a dynamic tag
 * @param visible The view shows the layers of the light
 * @param bake The unculled static lights of the bake are collected too
 * @param state The registry, for the modulators of the light
 * @param p The packet
 */
static void box_light_extract(
    const ecs_iter_t *it,
    int32_t i,
    const box_frustum_t *frustum,
    bool is_static,
    bool visible,
    bool bake,
    const RenderState *state,
    box_packet_t *p)
{
    bool spot = ecs_field_id(it, 0) == ecs_id(SpotLight);
    int32_t light_index = ecs_field_is_self(it, 0) ? i : 0;
    const WorldTransform *transform = &ecs_field(it, WorldTransform, 1)[i];
    const Rgb *rgb = NULL;
    if (ecs_field_is_set(it, 2)) {
        rgb = &ecs_field(it, Rgb, 2)[ecs_field_is_self(it, 2) ? i : 0];
    }

    box_light_t light;
    if (spot) {
        const SpotLight *s = &ecs_field(it, SpotLight, 0)[light_index];
        box_light_fill(transform, rgb, s->intensity, s->range, &light);
        box_light_cone(s, transform, &light);
    } else {
        const PointLight *l = &ecs_field(it, PointLight, 0)[light_index];
        box_light_fill(transform, rgb, l->intensity, l->range, &light);
    }

    light.is_static = is_static ? 1u : 0u;
    if (state->modulated) {
        light.modulator = box_render_modulator_mask(it->world, state, it->entities[i]);
    }

    if (visible && box_frustum_sphere(frustum, light.position, light.range)) {
        *(box_light_t*)box_vec_push(&p->lights) = light;
    }

    if (bake && is_static && p->bake.enabled && p->bake_lights.count < BOX_LIGHT_MAX &&
        !(box_render_entity_layers(it->world, state, it->entities[i]) & state->ui_mask)) {
        *(box_light_t*)box_vec_push(&p->bake_lights) = light;
    }
}

void box_render_extract_lights(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p,
    ecs_entity_t e,
    box_view_t *v,
    bool bake)
{
    box_light_settings(world, e, v);
    v->light_first = p->lights.count;

    box_frustum_t frustum;
    bool has_planes = false;
    ecs_iter_t it = ecs_query_iter(world, state->lights);
    while (ecs_query_next(&it)) {
        if (!has_planes) {
            box_frustum_of_view(v, &frustum);
            box_frustum_close(&frustum, v->view, v->cluster.far_);
            has_planes = true;
        }

        bool is_static = !box_render_is_dynamic(world, it.entities[0]);
        bool visible = true;
        if (v->layer_mask != BOX_VIEW_LAYER_ALL || v->strict_layers) {
            visible = box_view_shows(v,
                box_render_entity_layers(world, state, it.entities[0]));
        }

        if (!visible && !bake) {
            continue;
        }

        int32_t i;
        for (i = 0; i < it.count && p->lights.count < BOX_LIGHT_MAX; i ++) {
            if (state->hide_used &&
                box_hierarchy_ancestor_has(world, it.entities[i], Hidden))
            {
                continue;
            }

            box_light_extract(&it, i, &frustum, is_static, visible, bake, state, p);
        }
    }

    v->light_count = p->lights.count - v->light_first;
}
