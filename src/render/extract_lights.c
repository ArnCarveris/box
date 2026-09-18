/**
 * @file render/extract_lights.c
 * @brief Extracts the point and spot lights inside the view frustum into the packet.
 *
 * Static lights are marked so baked instances skip them, and are carried
 * unculled in a second array that the bake accumulates into its probes.
 */

#include "render_internal.h"

/** Planes of a view frustum: left, right, bottom, top, near and far */
#define BOX_FRUSTUM_PLANES (6)

/** Range a light without one gets */
#define BOX_LIGHT_RANGE (10.0f)

/** Outer cone half angle in degrees a spot light without one gets */
#define BOX_LIGHT_OUTER (30.0f)

/** Part of the outer half angle an inner half angle of zero becomes */
#define BOX_LIGHT_INNER_RATIO (0.75f)

/** Radians in one degree */
#define BOX_LIGHT_RADIANS (3.14159265f / 180.0f)

/** Plane of a view frustum in world space */
typedef struct box_plane_t {
    vec3 normal;  /**< Unit normal pointing into the frustum */
    float d;      /**< Distance of the plane from the origin along its normal */
} box_plane_t;

/** Build a plane from its equation and normalize it.
 *
 * @param x Normal x
 * @param y Normal y
 * @param z Normal z
 * @param w Distance term
 * @return The normalized plane
 */
static box_plane_t box_light_plane(
    float x,
    float y,
    float z,
    float w)
{
    float length = sqrtf(x * x + y * y + z * z);
    float scale = length > 0 ? 1.0f / length : 1.0f;
    box_plane_t result = { vec3_new(x * scale, y * scale, z * scale), w * scale };

    return result;
}

/** Build the six frustum planes of a view.
 *
 * The side and near planes come from the view projection matrix; the far plane
 * comes from the forward axis of the view matrix, because the projection has an
 * infinite far plane.
 *
 * @param v The view
 * @param planes Array of BOX_FRUSTUM_PLANES planes to fill
 */
static void box_light_planes(
    const box_view_t *v,
    box_plane_t *planes)
{
    mat4 proj;
    mat4 view;
    memcpy(proj.v, v->proj, sizeof(proj.v));
    memcpy(view.v, v->view, sizeof(view.v));
    mat4 vp = mat4_mul(&proj, &view);
    const float *m = vp.v;
    planes[0] = box_light_plane(m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]);
    planes[1] = box_light_plane(m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]);
    planes[2] = box_light_plane(m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]);
    planes[3] = box_light_plane(m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]);
    planes[4] = box_light_plane(m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]);

    const float *w = view.v;
    float far_ = v->cluster.far_;
    planes[5] = box_light_plane(-w[2], -w[6], -w[10], far_ - w[14]);
}

/** Check a light sphere against the frustum planes.
 *
 * @param planes The frustum planes
 * @param center World space center of the sphere
 * @param radius Radius of the sphere
 * @return true when the sphere is not fully outside a plane
 */
static bool box_light_visible(
    const box_plane_t *planes,
    vec3 center,
    float radius)
{
    bool result = true;
    int i;
    for (i = 0; i < BOX_FRUSTUM_PLANES && result; i ++) {
        result = vec3_dot(planes[i].normal, center) + planes[i].d >= -radius;
    }

    return result;
}

/** Copy the cluster settings of a view into it, filling in defaults.
 *
 * @param world The world
 * @param e The View entity, 0 for the view a world without views gets
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
    out->pad = 0;
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
 * @param planes The frustum planes
 * @param is_static Neither the light nor an ancestor carries a dynamic tag
 * @param bake The unculled static lights of the bake are collected too
 * @param p The packet
 */
static void box_light_extract(
    const ecs_iter_t *it,
    int32_t i,
    const box_plane_t *planes,
    bool is_static,
    bool bake,
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
    vec3 center = vec3_new(light.position[0], light.position[1], light.position[2]);
    if (box_light_visible(planes, center, light.range)) {
        *(box_light_t*)box_vec_push(&p->lights) = light;
    }

    if (bake && is_static && p->bake.enabled && p->bake_lights.count < BOX_LIGHT_MAX) {
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

    box_plane_t planes[BOX_FRUSTUM_PLANES];
    bool has_planes = false;
    ecs_iter_t it = ecs_query_iter(world, state->lights);
    while (ecs_query_next(&it)) {
        if (!has_planes) {
            box_light_planes(v, planes);
            has_planes = true;
        }

        bool is_static = !box_render_is_dynamic(world, it.entities[0]);
        int32_t i;
        for (i = 0; i < it.count && p->lights.count < BOX_LIGHT_MAX; i ++) {
            box_light_extract(&it, i, planes, is_static, bake, p);
        }
    }

    v->light_count = p->lights.count - v->light_first;
}
