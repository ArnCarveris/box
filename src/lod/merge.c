/**
 * @file lod/merge.c
 * @brief Merges the meshes of a subtree into one world space mesh with a range per material.
 *
 * Sources are ordered by material key so each material gets one index range.
 * Vertices are transformed to world space and welded; normals are summed per
 * vertex and normalized at the end, which smooths welded seams.
 */

#include "mesh_internal.h"

/** Pair of a material key and a source, which the merge sorts to order its groups */
typedef struct box_lod_order_t {
    uint64_t material; /**< Material key of the source */
    int32_t index;     /**< Place of the source in the description */
} box_lod_order_t;

/** One vertex as the merge reads it out of a source, in world space */
typedef struct box_lod_vertex_t {
    vec3 position; /**< World position */
    vec3 normal;   /**< World normal, zero when the source has none */
    vec2 uv;       /**< Texture coordinate */
    rgba color;    /**< Vertex color */
} box_lod_vertex_t;

/** Order two sources by material key, breaking the tie by input order.
 *
 * @param a First pair
 * @param b Second pair
 * @return Negative when a comes first, positive when b does
 */
static int box_lod_order_cmp(
    const void *a,
    const void *b)
{
    const box_lod_order_t *first = a, *second = b;
    int result = 0;

    if (first->material != second->material) {
        result = first->material < second->material ? -1 : 1;
    } else {
        result = first->index < second->index ? -1 : 1;
    }

    return result;
}

/** Mix one float into a hash, reading a negative zero as a zero.
 *
 * @param hash The hash so far
 * @param value The float
 * @return The hash with the float mixed in
 */
static uint32_t box_lod_hash_float(
    uint32_t hash,
    float value)
{
    float v = value == 0.0f ? 0.0f : value;
    uint32_t bits;

    ecs_os_memcpy(&bits, &v, sizeof(uint32_t));

    return (hash ^ bits) * 16777619u;
}

/** Spread the bits of a hash, so the low bits that pick a slot are mixed.
 *
 * The floats of a grid share their low mantissa bits, which a multiply alone
 * leaves in the low bits of the hash and which would pile every vertex of a
 * grid onto a handful of slots.
 *
 * @param hash The hash
 * @return The spread hash
 */
static uint32_t box_lod_hash_finish(
    uint32_t hash)
{
    hash ^= hash >> 16;
    hash *= 0x85ebca6bu;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35u;

    return hash ^ (hash >> 16);
}

/** Hash the key a vertex is matched on.
 *
 * @param v The vertex
 * @param weld The merge matches on the position alone
 * @return The hash
 */
static uint32_t box_lod_vertex_hash(
    const box_lod_vertex_t *v,
    bool weld)
{
    uint32_t hash = 2166136261u;

    hash = box_lod_hash_float(hash, v->position.x);
    hash = box_lod_hash_float(hash, v->position.y);
    hash = box_lod_hash_float(hash, v->position.z);

    if (!weld) {
        hash = box_lod_hash_float(hash, v->normal.x);
        hash = box_lod_hash_float(hash, v->normal.y);
        hash = box_lod_hash_float(hash, v->normal.z);
        hash = box_lod_hash_float(hash, v->uv.x);
        hash = box_lod_hash_float(hash, v->uv.y);
        hash = (hash ^ (uint32_t)v->color.r) * 16777619u;
        hash = (hash ^ (uint32_t)v->color.g) * 16777619u;
        hash = (hash ^ (uint32_t)v->color.b) * 16777619u;
        hash = (hash ^ (uint32_t)v->color.a) * 16777619u;
    }

    return box_lod_hash_finish(hash);
}

/** Check whether a merged vertex carries the key of a source vertex.
 *
 * @param builder The builder
 * @param index The merged vertex
 * @param v The source vertex
 * @param weld The merge matches on the position alone
 * @return True when the two are one vertex
 */
static bool box_lod_vertex_eq(
    box_lod_mesh_builder_t *builder,
    int32_t index,
    const box_lod_vertex_t *v,
    bool weld)
{
    const vec3 *positions = ecs_vec_first_t(&builder->vertices, vec3);
    const vec3 *keys = ecs_vec_first_t(&builder->keys, vec3);
    const vec2 *uvs = ecs_vec_first_t(&builder->uvs, vec2);
    const rgba *colors = ecs_vec_first_t(&builder->colors, rgba);
    bool result = positions[index].x == v->position.x &&
        positions[index].y == v->position.y &&
        positions[index].z == v->position.z;

    if (result && !weld) {
        result = keys[index].x == v->normal.x && keys[index].y == v->normal.y &&
            keys[index].z == v->normal.z && uvs[index].x == v->uv.x &&
            uvs[index].y == v->uv.y && colors[index].r == v->color.r &&
            colors[index].g == v->color.g && colors[index].b == v->color.b &&
            colors[index].a == v->color.a;
    }

    return result;
}

/** Look one vertex up in the weld table.
 *
 * @param builder The builder
 * @param v The vertex
 * @param weld The merge matches on the position alone
 * @param hash Hash of the key of the vertex
 * @return The merged vertex, or -1 when the table has none
 */
static int32_t box_lod_table_find(
    box_lod_mesh_builder_t *builder,
    const box_lod_vertex_t *v,
    bool weld,
    uint32_t hash)
{
    int32_t *table = ecs_vec_first_t(&builder->table, int32_t);
    uint32_t mask = (uint32_t)ecs_vec_count(&builder->table) - 1;
    uint32_t slot = hash & mask;
    int32_t result = -1;

    while (table[slot]) {
        if (box_lod_vertex_eq(builder, table[slot] - 1, v, weld)) {
            result = table[slot] - 1;
            break;
        }

        slot = (slot + 1) & mask;
    }

    return result;
}

/** Add a vertex to the mesh and to the weld table.
 *
 * @param builder The builder
 * @param v The vertex
 * @param hash Hash of the key of the vertex
 * @return The merged vertex
 */
static int32_t box_lod_vertex_add(
    box_lod_mesh_builder_t *builder,
    const box_lod_vertex_t *v,
    uint32_t hash)
{
    int32_t *table = ecs_vec_first_t(&builder->table, int32_t);
    uint32_t mask = (uint32_t)ecs_vec_count(&builder->table) - 1;
    uint32_t slot = hash & mask;
    int32_t result = ecs_vec_count(&builder->vertices);

    *ecs_vec_append_t(NULL, &builder->vertices, vec3) = v->position;
    *ecs_vec_append_t(NULL, &builder->normals, vec3) = vec3_new(0, 0, 0);
    *ecs_vec_append_t(NULL, &builder->keys, vec3) = v->normal;
    *ecs_vec_append_t(NULL, &builder->uvs, vec2) = v->uv;
    *ecs_vec_append_t(NULL, &builder->colors, rgba) = v->color;

    while (table[slot]) {
        slot = (slot + 1) & mask;
    }

    table[slot] = result + 1;

    return result;
}

/** Read one vertex of a source and give it its place in the mesh.
 *
 * @param builder The builder
 * @param src The source
 * @param vertex The vertex of the source
 * @param weld The merge matches on the position alone
 * @return The merged vertex
 */
static int32_t box_lod_merge_vertex(
    box_lod_mesh_builder_t *builder,
    const box_lod_mesh_source_t *src,
    int32_t vertex,
    bool weld)
{
    box_lod_vertex_t v;
    uint32_t hash;
    int32_t result;

    v.position = mat4_transform_point(&src->transform, src->vertices[vertex]);
    v.normal = vec3_new(0, 0, 0);
    v.uv = src->uvs ? src->uvs[vertex] : (vec2){0, 0};
    v.color = src->colors ? src->colors[vertex] : (rgba){255, 255, 255, 255};

    if (src->normals) {
        v.normal = vec3_normalize(
            mat4_transform_dir(&src->transform, src->normals[vertex]));
    }

    hash = box_lod_vertex_hash(&v, weld);
    result = box_lod_table_find(builder, &v, weld, hash);
    if (result < 0) {
        result = box_lod_vertex_add(builder, &v, hash);
    }

    if (src->normals) {
        vec3 *normals = ecs_vec_first_t(&builder->normals, vec3);
        normals[result] = vec3_add(normals[result], v.normal);
    }

    return result;
}

/** Add the normal of a triangle to the three vertices it joins.
 *
 * @param builder The builder
 * @param v The three merged vertices of the triangle
 */
static void box_lod_merge_face(
    box_lod_mesh_builder_t *builder,
    const int32_t *v)
{
    const vec3 *positions = ecs_vec_first_t(&builder->vertices, vec3);
    vec3 *normals = ecs_vec_first_t(&builder->normals, vec3);
    vec3 face = vec3_cross(vec3_sub(positions[v[1]], positions[v[0]]),
        vec3_sub(positions[v[2]], positions[v[0]]));
    int32_t i;

    for (i = 0; i < 3; i ++) {
        normals[v[i]] = vec3_add(normals[v[i]], face);
    }
}

/** Find the group the triangles of a material go in, appending it when needed.
 *
 * @param builder The builder
 * @param material The material key
 * @return The group, which is always the last one
 */
static box_lod_mesh_group_t* box_lod_merge_group(
    box_lod_mesh_builder_t *builder,
    uint64_t material)
{
    int32_t count = ecs_vec_count(&builder->groups);
    box_lod_mesh_group_t *result = NULL;

    if (count) {
        result = ecs_vec_get_t(&builder->groups, box_lod_mesh_group_t, count - 1);
    }

    if (!result || result->material != material) {
        result = ecs_vec_append_t(NULL, &builder->groups, box_lod_mesh_group_t);
        result->material = material;
        result->first_index = ecs_vec_count(&builder->indices);
        result->index_count = 0;
        result->error = 0;
    }

    return result;
}

/** Append the triangles of one source to the mesh.
 *
 * @param builder The builder
 * @param src The source
 * @param weld The merge matches vertices on their position alone
 */
static void box_lod_merge_source(
    box_lod_mesh_builder_t *builder,
    const box_lod_mesh_source_t *src,
    bool weld)
{
    int32_t indices = src->index_count > 0 ? src->index_count : src->vertex_count;
    box_lod_mesh_group_t *group = NULL;
    int32_t i, j, triangles;

    if (!src->vertices || src->vertex_count <= 0) {
        goto done;
    }

    triangles = indices / 3;

    for (i = 0; i < triangles; i ++) {
        int32_t v[3] = {0, 0, 0};
        bool valid = true;

        for (j = 0; j < 3; j ++) {
            uint32_t index = src->indices ?
                src->indices[i * 3 + j] : (uint32_t)(i * 3 + j);

            if (index >= (uint32_t)src->vertex_count) {
                valid = false;
                break;
            }

            v[j] = box_lod_merge_vertex(builder, src, (int32_t)index, weld);
        }

        if (!valid || v[0] == v[1] || v[1] == v[2] || v[0] == v[2]) {
            continue;
        }

        if (!group) {
            group = box_lod_merge_group(builder, src->material);
        }

        for (j = 0; j < 3; j ++) {
            *ecs_vec_append_t(NULL, &builder->indices, uint32_t) = (uint32_t)v[j];
        }

        group->index_count += 3;

        if (!src->normals) {
            box_lod_merge_face(builder, v);
        }
    }

done:
    return;
}

/** Empty the vectors of a builder and size its weld table for a description.
 *
 * @param builder The builder
 * @param desc The meshes of the subtree, NULL for none
 */
static void box_lod_merge_reset(
    box_lod_mesh_builder_t *builder,
    const box_lod_merge_desc_t *desc)
{
    int32_t capacity = BOX_LOD_TABLE_MIN;
    int32_t i, total = 0;

    ecs_vec_clear(&builder->vertices);
    ecs_vec_clear(&builder->normals);
    ecs_vec_clear(&builder->uvs);
    ecs_vec_clear(&builder->colors);
    ecs_vec_clear(&builder->keys);
    ecs_vec_clear(&builder->indices);
    ecs_vec_clear(&builder->groups);

    if (desc && desc->sources) {
        for (i = 0; i < desc->count; i ++) {
            int32_t count = desc->sources[i].vertex_count;
            total += count > 0 ? count : 0;
        }
    }

    while (capacity < total * 2) {
        capacity *= 2;
    }

    ecs_vec_set_count_t(NULL, &builder->table, int32_t, capacity);
    ecs_os_memset_n(ecs_vec_first_t(&builder->table, int32_t), 0, int32_t, capacity);
}

/** Turn the summed normal of every vertex into a unit normal.
 *
 * @param builder The builder
 */
static void box_lod_merge_normals(
    box_lod_mesh_builder_t *builder)
{
    vec3 *normals = ecs_vec_first_t(&builder->normals, vec3);
    int32_t i, count = ecs_vec_count(&builder->normals);

    for (i = 0; i < count; i ++) {
        float length = vec3_length(normals[i]);

        if (length > 0) {
            normals[i] = vec3_scale(normals[i], 1.0f / length);
        } else {
            normals[i] = vec3_new(0, 1, 0);
        }
    }
}

void* box_lod_mesh_scratch(
    box_lod_mesh_builder_t *builder,
    int32_t size)
{
    ecs_vec_set_count_t(NULL, &builder->scratch, uint8_t, size);

    return ecs_vec_first_t(&builder->scratch, uint8_t);
}

void box_lod_mesh_bounds(
    box_lod_mesh_builder_t *builder)
{
    const vec3 *positions = ecs_vec_first_t(&builder->vertices, vec3);
    int32_t i, count = ecs_vec_count(&builder->vertices);

    builder->min = vec3_new(0, 0, 0);
    builder->max = vec3_new(0, 0, 0);

    if (count) {
        builder->min = vec3_new(FLT_MAX, FLT_MAX, FLT_MAX);
        builder->max = vec3_new(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    }

    for (i = 0; i < count; i ++) {
        builder->min = vec3_min(builder->min, positions[i]);
        builder->max = vec3_max(builder->max, positions[i]);
    }
}

bool box_lod_mesh_result(
    box_lod_mesh_builder_t *builder,
    float error,
    box_lod_mesh_t *out)
{
    if (out) {
        out->vertices = ecs_vec_first_t(&builder->vertices, vec3);
        out->normals = ecs_vec_first_t(&builder->normals, vec3);
        out->uvs = ecs_vec_first_t(&builder->uvs, vec2);
        out->colors = ecs_vec_first_t(&builder->colors, rgba);
        out->indices = ecs_vec_first_t(&builder->indices, uint32_t);
        out->vertex_count = ecs_vec_count(&builder->vertices);
        out->index_count = ecs_vec_count(&builder->indices);
        out->groups = ecs_vec_first_t(&builder->groups, box_lod_mesh_group_t);
        out->group_count = ecs_vec_count(&builder->groups);
        out->min = builder->min;
        out->max = builder->max;
        out->error = error;
    }

    return ecs_vec_count(&builder->indices) > 0;
}

void box_lod_mesh_builder_init(
    box_lod_mesh_builder_t *builder)
{
    ecs_assert(builder != NULL, ECS_INVALID_PARAMETER, NULL);

    ecs_os_init();
    ecs_os_memset_t(builder, 0, box_lod_mesh_builder_t);
    ecs_vec_init_t(NULL, &builder->vertices, vec3, 0);
    ecs_vec_init_t(NULL, &builder->normals, vec3, 0);
    ecs_vec_init_t(NULL, &builder->uvs, vec2, 0);
    ecs_vec_init_t(NULL, &builder->colors, rgba, 0);
    ecs_vec_init_t(NULL, &builder->keys, vec3, 0);
    ecs_vec_init_t(NULL, &builder->indices, uint32_t, 0);
    ecs_vec_init_t(NULL, &builder->groups, box_lod_mesh_group_t, 0);
    ecs_vec_init_t(NULL, &builder->table, int32_t, 0);
    ecs_vec_init_t(NULL, &builder->scratch, uint8_t, 0);
}

void box_lod_mesh_builder_fini(
    box_lod_mesh_builder_t *builder)
{
    ecs_vec_fini_t(NULL, &builder->vertices, vec3);
    ecs_vec_fini_t(NULL, &builder->normals, vec3);
    ecs_vec_fini_t(NULL, &builder->uvs, vec2);
    ecs_vec_fini_t(NULL, &builder->colors, rgba);
    ecs_vec_fini_t(NULL, &builder->keys, vec3);
    ecs_vec_fini_t(NULL, &builder->indices, uint32_t);
    ecs_vec_fini_t(NULL, &builder->groups, box_lod_mesh_group_t);
    ecs_vec_fini_t(NULL, &builder->table, int32_t);
    ecs_vec_fini_t(NULL, &builder->scratch, uint8_t);
}

bool box_lod_merge(
    box_lod_mesh_builder_t *builder,
    const box_lod_merge_desc_t *desc,
    box_lod_mesh_t *out)
{
    int32_t count = desc ? desc->count : 0;
    int32_t i;

    box_lod_merge_reset(builder, desc);

    if (desc && desc->sources && count > 0) {
        box_lod_order_t *order = box_lod_mesh_scratch(builder,
            count * (int32_t)sizeof(box_lod_order_t));

        for (i = 0; i < count; i ++) {
            order[i].material = desc->sources[i].material;
            order[i].index = i;
        }

        qsort(order, (size_t)count, sizeof(box_lod_order_t), box_lod_order_cmp);

        for (i = 0; i < count; i ++) {
            box_lod_merge_source(builder,
                &desc->sources[order[i].index], desc->weld);
        }

        box_lod_merge_normals(builder);
    }

    box_lod_mesh_bounds(builder);

    return box_lod_mesh_result(builder, 0, out);
}
