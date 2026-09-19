/**
 * @file lod/proxy.c
 * @brief The proxy generator: prepares the inputs, spends the shape budget and refines.
 *
 * The inputs are prepared in world space and sorted into one group per material
 * key, every group gets a share of the shape budget by the volume it fills, and
 * a group whose shapes miss the most gets another shape while the error of the
 * proxy is above the bound the description asks for.
 */

#include "lod_internal.h"
#include <stdlib.h>

/** Order two prepared inputs by material key, then by their place in the input.
 *
 * @param a The first input
 * @param b The second input
 * @return Negative when a comes first, positive when b does
 */
static int box_lod_compare_input(
    const void *a,
    const void *b)
{
    const box_lod_input_t *ia = a;
    const box_lod_input_t *ib = b;
    int result = 0;

    if (ia->material != ib->material) {
        result = ia->material < ib->material ? -1 : 1;
    } else if (ia->index != ib->index) {
        result = ia->index < ib->index ? -1 : 1;
    }

    return result;
}

/** Put every input of a subtree in world space and take the bounds of the whole.
 *
 * @param builder The builder
 * @param desc The description of the build
 * @param out Receives the world bounds of the inputs
 */
static void box_lod_prepare(
    box_lod_builder_t *builder,
    const box_lod_proxy_desc_t *desc,
    box_lod_proxy_t *out)
{
    box_lod_input_t *inputs;
    vec3 lo = vec3_new(FLT_MAX, FLT_MAX, FLT_MAX);
    vec3 hi = vec3_new(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    int32_t i, c;

    ecs_vec_set_count_t(NULL, &builder->inputs, box_lod_input_t, desc->count);
    ecs_vec_set_count_t(NULL, &builder->members, int32_t, desc->count);
    ecs_vec_set_count_t(NULL, &builder->nearest, float, desc->count);
    inputs = ecs_vec_first_t(&builder->inputs, box_lod_input_t);

    for (i = 0; i < desc->count; i ++) {
        const box_lod_primitive_t *prim = &desc->primitives[i];
        vec3 min = vec3_new(FLT_MAX, FLT_MAX, FLT_MAX);
        vec3 max = vec3_new(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        vec3 size;

        for (c = 0; c < 8; c ++) {
            vec3 corner = vec3_new(
                (c & 1) ? prim->max.x : prim->min.x,
                (c & 2) ? prim->max.y : prim->min.y,
                (c & 4) ? prim->max.z : prim->min.z);

            corner = mat4_transform_point(&prim->transform, corner);
            min = vec3_min(min, corner);
            max = vec3_max(max, corner);
        }

        size = vec3_sub(max, min);
        inputs[i].min = min;
        inputs[i].max = max;
        inputs[i].center = vec3_scale(vec3_add(min, max), 0.5f);
        inputs[i].volume = prim->volume > 0 ? prim->volume : size.x * size.y * size.z;
        inputs[i].material = prim->material;
        inputs[i].index = i;
        lo = vec3_min(lo, min);
        hi = vec3_max(hi, max);
    }

    out->min = lo;
    out->max = hi;
}

/** Collect the sorted inputs into one group per material key.
 *
 * @param builder The builder
 */
static void box_lod_collect(
    box_lod_builder_t *builder)
{
    const box_lod_input_t *inputs = ecs_vec_first_t(&builder->inputs, box_lod_input_t);
    int32_t count = ecs_vec_count(&builder->inputs);
    box_lod_group_t *group = NULL;
    int32_t i;

    ecs_vec_set_count_t(NULL, &builder->groups, box_lod_group_t, 0);

    for (i = 0; i < count; i ++) {
        if (!group || inputs[i].material != group->material) {
            group = ecs_vec_append_t(NULL, &builder->groups, box_lod_group_t);
            group->first = i;
            group->count = 0;
            group->target = 1;
            group->volume = 0;
            group->missed = 0;
            group->material = inputs[i].material;
        }

        group->count ++;
        group->volume += inputs[i].volume;
    }
}

/** Hand every group its share of the shape budget.
 *
 * Every material keeps at least one shape, which is what preserves the material
 * mix of the subtree, and the shapes that are left go to the group that fills
 * the most volume per shape it already has.
 *
 * @param builder The builder
 * @param max_shapes Shapes the proxy aims for
 */
static void box_lod_distribute(
    box_lod_builder_t *builder,
    int32_t max_shapes)
{
    box_lod_group_t *groups = ecs_vec_first_t(&builder->groups, box_lod_group_t);
    int32_t count = ecs_vec_count(&builder->groups);
    int32_t remaining = max_shapes - count;
    float total = 0;
    int32_t i;

    for (i = 0; i < count; i ++) {
        total += groups[i].volume;
    }

    while (remaining > 0) {
        int32_t best = -1;
        float best_score = 0;

        for (i = 0; i < count; i ++) {
            float weight = total > 0 ? groups[i].volume : (float)groups[i].count;
            float score = weight / (float)(groups[i].target + 1);

            if (groups[i].target >= groups[i].count) {
                continue;
            }

            if (best < 0 || score > best_score) {
                best = i;
                best_score = score;
            }
        }

        if (best < 0) {
            break;
        }

        groups[best].target ++;
        remaining --;
    }
}

/** Cluster every group and emit the shapes of the proxy.
 *
 * @param builder The builder
 * @param iterations Refinement passes every group takes
 * @return The volume the shapes miss against the union of the inputs
 */
static float box_lod_emit(
    box_lod_builder_t *builder,
    int32_t iterations)
{
    int32_t groups = ecs_vec_count(&builder->groups);
    float missed = 0;
    int32_t g, c;

    ecs_vec_set_count_t(NULL, &builder->shapes, box_lod_shape_t, 0);

    for (g = 0; g < groups; g ++) {
        box_lod_group_t *group = ecs_vec_get_t(&builder->groups, box_lod_group_t, g);
        int32_t clusters = box_lod_cluster_group(
            builder, group, group->target, iterations);

        group->missed = 0;

        for (c = 0; c < clusters; c ++) {
            const box_lod_cluster_t *cluster = ecs_vec_get_t(
                &builder->clusters, box_lod_cluster_t, c);
            box_lod_shape_t shape;

            if (!cluster->count) {
                continue;
            }

            group->missed += box_lod_shape_build(cluster, group->material, &shape);
            *ecs_vec_append_t(NULL, &builder->shapes, box_lod_shape_t) = shape;
        }

        missed += group->missed;
    }

    return missed;
}

/** Give one more shape to the group whose shapes miss the most volume.
 *
 * @param builder The builder
 * @return True when a group could take another shape
 */
static bool box_lod_refine(
    box_lod_builder_t *builder)
{
    box_lod_group_t *groups = ecs_vec_first_t(&builder->groups, box_lod_group_t);
    int32_t count = ecs_vec_count(&builder->groups);
    float best_missed = 0;
    int32_t best = -1;
    bool result = false;
    int32_t i;

    for (i = 0; i < count; i ++) {
        if (groups[i].target >= groups[i].count) {
            continue;
        }

        if (best < 0 || groups[i].missed > best_missed) {
            best = i;
            best_missed = groups[i].missed;
        }
    }

    if (best >= 0) {
        groups[best].target ++;
        result = true;
    }

    return result;
}

void box_lod_builder_init(
    box_lod_builder_t *builder)
{
    ecs_assert(builder != NULL, ECS_INVALID_PARAMETER, NULL);

    ecs_os_init();
    ecs_vec_init_t(NULL, &builder->inputs, box_lod_input_t, 0);
    ecs_vec_init_t(NULL, &builder->groups, box_lod_group_t, 0);
    ecs_vec_init_t(NULL, &builder->clusters, box_lod_cluster_t, 0);
    ecs_vec_init_t(NULL, &builder->members, int32_t, 0);
    ecs_vec_init_t(NULL, &builder->nearest, float, 0);
    ecs_vec_init_t(NULL, &builder->shapes, box_lod_shape_t, 0);
}

void box_lod_builder_fini(
    box_lod_builder_t *builder)
{
    ecs_assert(builder != NULL, ECS_INVALID_PARAMETER, NULL);

    ecs_vec_fini_t(NULL, &builder->inputs, box_lod_input_t);
    ecs_vec_fini_t(NULL, &builder->groups, box_lod_group_t);
    ecs_vec_fini_t(NULL, &builder->clusters, box_lod_cluster_t);
    ecs_vec_fini_t(NULL, &builder->members, int32_t);
    ecs_vec_fini_t(NULL, &builder->nearest, float);
    ecs_vec_fini_t(NULL, &builder->shapes, box_lod_shape_t);
    ecs_os_fini();
}

bool box_lod_proxy_build(
    box_lod_builder_t *builder,
    const box_lod_proxy_desc_t *desc,
    box_lod_proxy_t *out)
{
    int32_t max_shapes = BOX_LOD_MAX_SHAPES;
    int32_t iterations = BOX_LOD_ITERATIONS;
    int32_t rounds = 0;
    float missed = 0;
    bool result = false;

    ecs_assert(builder != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(desc != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(out != NULL, ECS_INVALID_PARAMETER, NULL);

    ecs_os_memset_t(out, 0, box_lod_proxy_t);

    if (desc->count <= 0 || !desc->primitives) {
        goto done;
    }

    if (desc->max_shapes > 0) {
        max_shapes = desc->max_shapes;
    }

    if (desc->iterations > 0) {
        iterations = desc->iterations;
    }

    box_lod_prepare(builder, desc, out);
    qsort(ecs_vec_first_t(&builder->inputs, box_lod_input_t), (size_t)desc->count,
        sizeof(box_lod_input_t), box_lod_compare_input);
    box_lod_collect(builder);
    box_lod_distribute(builder, max_shapes);
    missed = box_lod_emit(builder, iterations);

    while (desc->max_error > 0 && cbrtf(missed) > desc->max_error &&
        rounds < BOX_LOD_MAX_ROUNDS)
    {
        if (!box_lod_refine(builder)) {
            break;
        }

        missed = box_lod_emit(builder, iterations);
        rounds ++;
    }

    out->shapes = ecs_vec_first_t(&builder->shapes, box_lod_shape_t);
    out->count = ecs_vec_count(&builder->shapes);
    out->error = cbrtf(missed);
    result = out->count > 0;
done:
    return result;
}
