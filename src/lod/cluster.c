/**
 * @file lod/cluster.c
 * @brief Refines the inputs of one material group into clusters of nearby primitives.
 *
 * Seeds are picked with farthest point sampling and moved with a volume weighted
 * k means refinement. Every tie is broken by input order, so the same subtree
 * always produces the same clusters.
 */

#include "lod_internal.h"

/** Squared distance between two points.
 *
 * @param a The first point
 * @param b The second point
 * @return The squared distance between a and b
 */
static float box_lod_dist2(
    vec3 a,
    vec3 b)
{
    vec3 d = vec3_sub(a, b);

    return vec3_dot(d, d);
}

/** Pick the seed centers of a group with farthest point sampling.
 *
 * The first seed is the input that fills the most volume, every later seed the
 * input that lies farthest from the seeds picked so far.
 *
 * @param inputs The inputs of the group
 * @param nearest Squared distance of every input to its nearest seed
 * @param clusters The clusters to seed
 * @param count Inputs in the group
 * @param k Clusters to seed
 */
static void box_lod_seed(
    const box_lod_input_t *inputs,
    float *nearest,
    box_lod_cluster_t *clusters,
    int32_t count,
    int32_t k)
{
    int32_t best = 0;
    int32_t i, c;
    for (i = 1; i < count; i ++) {
        if (inputs[i].volume > inputs[best].volume) {
            best = i;
        }
    }

    clusters[0].center = inputs[best].center;
    for (i = 0; i < count; i ++) {
        nearest[i] = box_lod_dist2(inputs[i].center, clusters[0].center);
    }

    for (c = 1; c < k; c ++) {
        best = 0;
        for (i = 1; i < count; i ++) {
            if (nearest[i] > nearest[best]) {
                best = i;
            }
        }

        clusters[c].center = inputs[best].center;
        for (i = 0; i < count; i ++) {
            float d = box_lod_dist2(inputs[i].center, clusters[c].center);
            if (d < nearest[i]) {
                nearest[i] = d;
            }
        }
    }
}

/** Assign every input to its nearest cluster and accumulate that cluster.
 *
 * @param inputs The inputs of the group
 * @param members Cluster of every input, which the pass overwrites
 * @param clusters The clusters
 * @param count Inputs in the group
 * @param k Number of clusters
 * @return True when an input changed cluster
 */
static bool box_lod_assign(
    const box_lod_input_t *inputs,
    int32_t *members,
    box_lod_cluster_t *clusters,
    int32_t count,
    int32_t k)
{
    bool changed = false;
    int32_t i, c;
    for (c = 0; c < k; c ++) {
        clusters[c].min = vec3_new(FLT_MAX, FLT_MAX, FLT_MAX);
        clusters[c].max = vec3_new(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        clusters[c].sum = vec3_new(0, 0, 0);
        clusters[c].total = vec3_new(0, 0, 0);
        clusters[c].volume = 0;
        clusters[c].count = 0;
    }

    for (i = 0; i < count; i ++) {
        box_lod_cluster_t *cluster;
        float best_d = box_lod_dist2(inputs[i].center, clusters[0].center);
        int32_t best = 0;
        for (c = 1; c < k; c ++) {
            float d = box_lod_dist2(inputs[i].center, clusters[c].center);
            if (d < best_d) {
                best_d = d;
                best = c;
            }
        }

        changed |= members[i] != best;
        members[i] = best;
        cluster = &clusters[best];
        cluster->min = vec3_min(cluster->min, inputs[i].min);
        cluster->max = vec3_max(cluster->max, inputs[i].max);
        cluster->sum = vec3_add(cluster->sum,
            vec3_scale(inputs[i].center, inputs[i].volume));
        cluster->total = vec3_add(cluster->total, inputs[i].center);
        cluster->volume += inputs[i].volume;
        cluster->count ++;
    }

    return changed;
}

/** Move every cluster center onto the volume weighted mean of its members.
 *
 * A cluster whose members fill no volume falls back to the plain mean, which is
 * what keeps flat and zero sized primitives from moving a center to the origin.
 *
 * @param clusters The clusters
 * @param k Number of clusters
 */
static void box_lod_update(
    box_lod_cluster_t *clusters,
    int32_t k)
{
    int32_t c;
    for (c = 0; c < k; c ++) {
        if (clusters[c].volume > 0) {
            clusters[c].center = vec3_scale(clusters[c].sum, 1.0f / clusters[c].volume);
        } else if (clusters[c].count) {
            clusters[c].center = vec3_scale(clusters[c].total,
                1.0f / (float)clusters[c].count);
        }
    }
}

int32_t box_lod_cluster_group(
    box_lod_builder_t *builder,
    const box_lod_group_t *group,
    int32_t k,
    int32_t iterations)
{
    box_lod_input_t *inputs = ecs_vec_first_t(&builder->inputs, box_lod_input_t);
    int32_t *members = ecs_vec_first_t(&builder->members, int32_t);
    float *nearest = ecs_vec_first_t(&builder->nearest, float);
    box_lod_cluster_t *clusters;
    int32_t i, iter;

    if (k > group->count) {
        k = group->count;
    }

    if (k < 1) {
        k = 1;
    }

    inputs += group->first;
    members += group->first;
    nearest += group->first;
    ecs_vec_set_count_t(NULL, &builder->clusters, box_lod_cluster_t, k);
    clusters = ecs_vec_first_t(&builder->clusters, box_lod_cluster_t);

    for (i = 0; i < group->count; i ++) {
        members[i] = -1;
    }

    box_lod_seed(inputs, nearest, clusters, group->count, k);

    for (iter = 0; iter < iterations; iter ++) {
        if (!box_lod_assign(inputs, members, clusters, group->count, k)) {
            break;
        }

        box_lod_update(clusters, k);
    }

    return k;
}
