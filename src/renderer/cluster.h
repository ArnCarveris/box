/**
 * @file renderer/cluster.h
 * @brief Froxel grid math, shared by extract, the renderer and the shaders.
 *
 * The grid splits the view frustum into tiles across the screen and slices
 * that grow logarithmically with view depth. The same math runs in the mesh
 * fragment shader, so a change here changes both sides.
 */

#ifndef BOX_RENDERER_CLUSTER_H
#define BOX_RENDERER_CLUSTER_H

#include <math.h>
#include "packet.h"

/** Smallest view depth the cluster math accepts */
#define BOX_CLUSTER_EPSILON (1e-4f)

/** Turn a grid coordinate into an index of a grid axis.
 *
 * @param value The coordinate, may fall outside the grid
 * @param count Cells on the axis
 * @return The index, clamped to the last cell
 */
static inline uint32_t box_cluster_cell(
    float value,
    uint32_t count)
{
    uint32_t result = 0;
    if (value > 0) {
        result = (uint32_t)value;
        if (result >= count) {
            result = count - 1;
        }
    }

    return result;
}

/** Depth slice a view space depth falls in.
 *
 * @param settings The grid settings
 * @param depth View space depth
 * @return The slice index
 */
static inline uint32_t box_cluster_slice(
    const box_cluster_t *settings,
    float depth)
{
    float range = logf(settings->far_ / settings->near_);
    float t = logf(depth / settings->near_) / range;

    return box_cluster_cell(t * (float)settings->z, settings->z);
}

/** Cluster a view space position falls in.
 *
 * @param settings The grid settings
 * @param view The view the grid belongs to
 * @param position View space position, x right, y up, z forward
 * @return The cluster index
 */
static inline uint32_t box_cluster_index(
    const box_cluster_t *settings,
    const box_view_t *view,
    const float position[3])
{
    float depth = position[2] > BOX_CLUSTER_EPSILON ? position[2] : BOX_CLUSTER_EPSILON;
    float ndc_x = view->proj[0] * position[0] / depth;
    float ndc_y = view->proj[5] * position[1] / depth;
    uint32_t x = box_cluster_cell((ndc_x + 1.0f) * 0.5f * (float)settings->x, settings->x);
    uint32_t y = box_cluster_cell((1.0f - ndc_y) * 0.5f * (float)settings->y, settings->y);
    uint32_t z = box_cluster_slice(settings, depth);

    return x + y * settings->x + z * settings->x * settings->y;
}

/** Clusters in a grid.
 *
 * @param settings The grid settings
 * @return The cluster count
 */
static inline uint32_t box_cluster_count(
    const box_cluster_t *settings)
{
    return settings->x * settings->y * settings->z;
}

#endif
