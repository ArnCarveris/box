/**
 * @file lod/shape.c
 * @brief Turns one cluster into the primitive that stands in for it and measures the loss.
 *
 * The shape follows the bounds of the cluster: a cluster that fills its bounds
 * keeps the box silhouette, a round one becomes a sphere and a tall round one a
 * capsule. The loss is the volume the shape gets wrong against its members.
 */

#include "lod_internal.h"

/** Largest of the three components of a vector.
 *
 * @param v The vector
 * @return The largest component
 */
static float box_lod_max3(
    vec3 v)
{
    float result = v.x > v.y ? v.x : v.y;

    return result > v.z ? result : v.z;
}

/** Smallest of the three components of a vector.
 *
 * @param v The vector
 * @return The smallest component
 */
static float box_lod_min3(
    vec3 v)
{
    float result = v.x < v.y ? v.x : v.y;

    return result < v.z ? result : v.z;
}

/** Volume a generated shape fills.
 *
 * @param shape The shape
 * @return The volume of the shape in world units
 */
static float box_lod_shape_volume(
    const box_lod_shape_t *shape)
{
    float result;
    float r;
    if (shape->kind == BoxLodShapeSphere) {
        r = shape->sphere.radius;
        result = (4.0f / 3.0f) * BOX_LOD_PI * r * r * r;
    } else if (shape->kind == BoxLodShapeCapsule) {
        r = shape->capsule.radius;
        result = BOX_LOD_PI * r * r * shape->capsule.length +
            (4.0f / 3.0f) * BOX_LOD_PI * r * r * r;
    } else {
        result = shape->box.x * shape->box.y * shape->box.z;
    }

    return result;
}

/** Pick the primitive that keeps the silhouette of a cluster.
 *
 * @param extents Half size of the bounds of the cluster
 * @param fill Part of those bounds the members of the cluster fill
 * @param out The shape to fill
 */
static void box_lod_shape_select(
    vec3 extents,
    float fill,
    box_lod_shape_t *out)
{
    float wide = extents.x > extents.z ? extents.x : extents.z;
    float thin = extents.x < extents.z ? extents.x : extents.z;
    bool round = box_lod_max3(extents) <= box_lod_min3(extents) * BOX_LOD_ROUND_RATIO;
    bool tall = extents.y >= wide * BOX_LOD_TALL_RATIO &&
        wide <= thin * BOX_LOD_ROUND_RATIO;
    float radius;

    if (fill < BOX_LOD_BOX_FILL && round) {
        out->kind = BoxLodShapeSphere;
        out->sphere = (Sphere){
            .segments = BOX_LOD_SEGMENTS,
            .smooth = true,
            .radius = (extents.x + extents.y + extents.z) / 3.0f
        };
    } else if (fill < BOX_LOD_BOX_FILL && tall) {
        radius = (extents.x + extents.z) * 0.5f;
        out->kind = BoxLodShapeCapsule;
        out->capsule = (Capsule){
            .segments = BOX_LOD_SEGMENTS,
            .smooth = true,
            .length = (extents.y - radius) * 2.0f,
            .radius = radius
        };
    } else {
        out->kind = BoxLodShapeBox;
        out->box = (Box){ extents.x * 2.0f, extents.y * 2.0f, extents.z * 2.0f };
    }
}

float box_lod_shape_build(
    const box_lod_cluster_t *cluster,
    uint64_t material,
    box_lod_shape_t *out)
{
    vec3 size = vec3_sub(cluster->max, cluster->min);
    vec3 extents = vec3_scale(size, 0.5f);
    float bounds = size.x * size.y * size.z;
    float filled = cluster->volume;
    float shape, ratio, missed;

    ecs_os_memset_t(out, 0, box_lod_shape_t);
    if (bounds > 0 && filled > bounds) {
        filled = bounds;
    }

    box_lod_shape_select(extents, bounds > 0 ? filled / bounds : 1.0f, out);
    out->position = vec3_scale(vec3_add(cluster->min, cluster->max), 0.5f);
    out->material = material;
    out->count = cluster->count;

    shape = box_lod_shape_volume(out);
    ratio = 1.0f;
    if (bounds > 0) {
        ratio = shape / bounds;
        ratio = ratio > 1.0f ? 1.0f : ratio;
    }

    missed = shape + filled - 2.0f * filled * ratio;
    missed = missed > 0 ? missed : 0;
    out->error = cbrtf(missed);

    return missed;
}
