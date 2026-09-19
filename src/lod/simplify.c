/**
 * @file lod/simplify.c
 * @brief Simplifies a merged mesh with meshoptimizer, one material group at a time.
 *
 * Every group is collapsed on its own, so a triangle never takes the material
 * of its neighbour, and both the bound the simplifier is given and the error it
 * reports are absolute lengths in world units, which is what lets the screen
 * space metric project the result. The vertices the surviving triangles no
 * longer use are dropped afterwards, in the order they were merged in.
 */

#include "mesh_internal.h"
#include <meshoptimizer.h>

/** Collapse every group of the mesh and rewrite the index buffer.
 *
 * @param builder The builder
 * @param desc What the simplification aims for
 * @return The largest error of the groups, in world units
 */
static float box_lod_simplify_groups(
    box_lod_mesh_builder_t *builder,
    const box_lod_simplify_desc_t *desc)
{
    uint32_t *indices = ecs_vec_first_t(&builder->indices, uint32_t);
    const float *positions = &ecs_vec_first_t(&builder->vertices, vec3)->x;
    int32_t vertices = ecs_vec_count(&builder->vertices);
    int32_t index_count = ecs_vec_count(&builder->indices);
    int32_t groups = ecs_vec_count(&builder->groups);
    uint32_t *dst = box_lod_mesh_scratch(builder,
        index_count * (int32_t)sizeof(uint32_t));
    float ratio = desc && desc->ratio > 0 ? desc->ratio : BOX_LOD_SIMPLIFY_RATIO;
    float size = vec3_length(vec3_sub(builder->max, builder->min));
    float limit = desc && desc->max_error > 0 ? desc->max_error : size;
    unsigned int options = meshopt_SimplifyErrorAbsolute | meshopt_SimplifySparse;
    float result = 0;
    int32_t i, write = 0;

    if (ratio > 1.0f) {
        ratio = 1.0f;
    }

    if (limit <= 0) {
        limit = 1.0f;
    }

    if (desc && desc->lock_border) {
        options |= meshopt_SimplifyLockBorder;
    }

    for (i = 0; i < groups; i ++) {
        box_lod_mesh_group_t *group = ecs_vec_get_t(
            &builder->groups, box_lod_mesh_group_t, i);
        int32_t target = (int32_t)((float)(group->index_count / 3) * ratio) * 3;
        float error = 0;
        size_t count;

        if (target < 3) {
            target = 3;
        }

        count = meshopt_simplify(&dst[write], &indices[group->first_index],
            (size_t)group->index_count, positions, (size_t)vertices, sizeof(vec3),
            (size_t)target, limit, options, &error);

        group->first_index = write;
        group->index_count = (int32_t)count;
        group->error = error;
        write += (int32_t)count;

        if (error > result) {
            result = error;
        }
    }

    ecs_os_memcpy_n(indices, dst, uint32_t, write);
    ecs_vec_set_count_t(NULL, &builder->indices, uint32_t, write);

    return result;
}

/** Drop the vertices no triangle uses any more, keeping the order of the rest.
 *
 * @param builder The builder
 */
static void box_lod_simplify_compact(
    box_lod_mesh_builder_t *builder)
{
    int32_t vertices = ecs_vec_count(&builder->vertices);
    int32_t index_count = ecs_vec_count(&builder->indices);
    uint32_t *indices = ecs_vec_first_t(&builder->indices, uint32_t);
    uint32_t *remap = box_lod_mesh_scratch(builder,
        vertices * (int32_t)sizeof(uint32_t));
    vec3 *positions = ecs_vec_first_t(&builder->vertices, vec3);
    vec3 *normals = ecs_vec_first_t(&builder->normals, vec3);
    vec2 *uvs = ecs_vec_first_t(&builder->uvs, vec2);
    rgba *colors = ecs_vec_first_t(&builder->colors, rgba);
    int32_t i, write = 0;

    for (i = 0; i < vertices; i ++) {
        remap[i] = UINT32_MAX;
    }

    for (i = 0; i < index_count; i ++) {
        remap[indices[i]] = 0;
    }

    for (i = 0; i < vertices; i ++) {
        if (remap[i] == UINT32_MAX) {
            continue;
        }

        positions[write] = positions[i];
        normals[write] = normals[i];
        uvs[write] = uvs[i];
        colors[write] = colors[i];
        remap[i] = (uint32_t)write;
        write ++;
    }

    for (i = 0; i < index_count; i ++) {
        indices[i] = remap[indices[i]];
    }

    ecs_vec_set_count_t(NULL, &builder->vertices, vec3, write);
    ecs_vec_set_count_t(NULL, &builder->normals, vec3, write);
    ecs_vec_set_count_t(NULL, &builder->uvs, vec2, write);
    ecs_vec_set_count_t(NULL, &builder->colors, rgba, write);
    ecs_vec_clear(&builder->keys);
}

bool box_lod_simplify(
    box_lod_mesh_builder_t *builder,
    const box_lod_simplify_desc_t *desc,
    box_lod_mesh_t *out)
{
    int32_t index_count = ecs_vec_count(&builder->indices);
    int32_t vertices = ecs_vec_count(&builder->vertices);
    float error = 0;

    if (index_count >= 3 && vertices > 0) {
        error = box_lod_simplify_groups(builder, desc);
        box_lod_simplify_compact(builder);
        box_lod_mesh_bounds(builder);
    }

    return box_lod_mesh_result(builder, error, out);
}
