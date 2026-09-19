/**
 * @file lod/lod_internal.h
 * @brief Private types of the proxy generator: prepared inputs, material groups and clusters.
 *
 * The generator prepares every input in world space, sorts it into a group per
 * material key and refines the group into clusters that each become one shape.
 */

#ifndef BOX_LOD_INTERNAL_H
#define BOX_LOD_INTERNAL_H

#include <box.h>
#include <float.h>
#include <math.h>

/** Extra shapes the generator spends on an error bound before it gives up */
#define BOX_LOD_MAX_ROUNDS (64)

/** Pi as a float constant, used instead of the possibly undefined M_PI */
#define BOX_LOD_PI (3.14159265358979323846f)

/** Fill of its bounds at which a cluster keeps the box silhouette */
#define BOX_LOD_BOX_FILL (0.6f)

/** Ratio below which two extents of a cluster count as equal */
#define BOX_LOD_ROUND_RATIO (1.25f)

/** Ratio at which the height of a cluster makes it a capsule */
#define BOX_LOD_TALL_RATIO (1.5f)

/** One input primitive of a proxy, prepared in world space */
typedef struct box_lod_input_t {
    vec3 center;       /**< Center of the world bounds */
    vec3 min;          /**< Minimum corner of the world bounds */
    vec3 max;          /**< Maximum corner of the world bounds */
    float volume;      /**< Volume the primitive fills */
    uint64_t material; /**< Material key of the primitive */
    int32_t index;     /**< Place in the input array, which breaks every tie */
} box_lod_input_t;

/** The inputs that share one material key, which cluster only with each other */
typedef struct box_lod_group_t {
    int32_t first;     /**< First input of the group in the sorted inputs */
    int32_t count;     /**< Inputs in the group */
    int32_t target;    /**< Clusters the group is allowed */
    float volume;      /**< Volume the inputs of the group fill */
    float missed;      /**< Volume the shapes of the group miss */
    uint64_t material; /**< Material key of the group */
} box_lod_group_t;

/** One cluster of the refinement, which becomes one shape */
typedef struct box_lod_cluster_t {
    vec3 center;   /**< Center the assignment measures against */
    vec3 min;      /**< Minimum corner of the bounds of the members */
    vec3 max;      /**< Maximum corner of the bounds of the members */
    vec3 sum;      /**< Volume weighted sum of the member centers */
    vec3 total;    /**< Plain sum of the member centers */
    float volume;  /**< Volume the members fill */
    int32_t count; /**< Members of the cluster */
} box_lod_cluster_t;

/** Refine the inputs of one group into clusters.
 *
 * Fills the clusters of the builder with the result, where a cluster without
 * members is one the refinement emptied and the caller skips.
 *
 * @param builder The builder
 * @param group The group to refine
 * @param k Clusters to aim for, clamped to the inputs of the group
 * @param iterations Refinement passes the clusters take
 * @return The number of clusters the builder holds
 */
int32_t box_lod_cluster_group(
    box_lod_builder_t *builder,
    const box_lod_group_t *group,
    int32_t k,
    int32_t iterations);

/** Turn one cluster into the shape that stands in for it.
 *
 * @param cluster The cluster
 * @param material Material key of the group the cluster belongs to
 * @param out Receives the shape
 * @return The volume the shape misses against the union of the members
 */
float box_lod_shape_build(
    const box_lod_cluster_t *cluster,
    uint64_t material,
    box_lod_shape_t *out);

#endif
