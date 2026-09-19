/**
 * @file geometry/mesh3.c
 * @brief Turns the vertex data of a Mesh3 into a shared, reference counted render mesh.
 *
 * The component owns five vectors, so it carries its own lifecycle hooks: a
 * component registered from C keeps the hooks of its C type and never gets the
 * ones flecs generates for a reflected type. Setting the component hashes the
 * data, hands the hash to the mesh cache and writes the RenderMesh the shared
 * mesh is drawn with, so entities that hold the same data upload once.
 */

#include "geometry_internal.h"
#include "mesh_gen_internal.h"
#include "../render/render_internal.h"
#include <string.h>

/** Vector members a Mesh3 holds */
#define BOX_MESH3_VECTORS (5)

/** Offset basis of the 64 bit FNV-1a hash */
#define BOX_MESH3_HASH_SEED (14695981039346656037u)

/** Prime of the 64 bit FNV-1a hash */
#define BOX_MESH3_HASH_PRIME (1099511628211u)

/** Element size of every vector member of a Mesh3, in declaration order */
static const ecs_size_t box_mesh3_elem[BOX_MESH3_VECTORS] = {
    ECS_SIZEOF(vec3), ECS_SIZEOF(vec3), ECS_SIZEOF(vec2),
    ECS_SIZEOF(uint32_t), ECS_SIZEOF(rgb)
};

/** Collect the vector members of a Mesh3 in declaration order.
 *
 * @param m The component
 * @param out Receives one pointer per vector member
 */
static void box_mesh3_vectors(
    Mesh3 *m,
    ecs_vec_t **out)
{
    out[0] = &m->vertices;
    out[1] = &m->normals;
    out[2] = &m->uvs;
    out[3] = &m->indices;
    out[4] = &m->colors;
}

/** Release the vectors of a Mesh3.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_mesh3_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Mesh3 *m = ptr;
    int32_t i, v;
    for (i = 0; i < count; i ++) {
        ecs_vec_t *vectors[BOX_MESH3_VECTORS];

        box_mesh3_vectors(&m[i], vectors);
        for (v = 0; v < BOX_MESH3_VECTORS; v ++) {
            ecs_vec_fini(NULL, vectors[v], box_mesh3_elem[v]);
        }
    }
}

/** Replace the contents of a vector with a copy of another one.
 *
 * @param dst The vector to overwrite
 * @param src The vector to copy
 * @param size Element size of both vectors
 */
static void box_mesh3_vec_copy(
    ecs_vec_t *dst,
    const ecs_vec_t *src,
    ecs_size_t size)
{
    int32_t count = ecs_vec_count(src);

    ecs_vec_fini(NULL, dst, size);
    ecs_vec_init(NULL, dst, size, count);
    ecs_vec_set_count(NULL, dst, size, count);
    if (count) {
        memcpy(ecs_vec_first(dst), ecs_vec_first(src), (size_t)size * (size_t)count);
    }
}

/** Deep copy the vectors of a Mesh3.
 *
 * @param dst_ptr The components to overwrite
 * @param src_ptr The components to copy
 * @param count Number of components
 * @param ti Type info
 */
static void box_mesh3_copy(
    void *dst_ptr,
    const void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Mesh3 *dst = dst_ptr;
    Mesh3 *src = ECS_CONST_CAST(Mesh3*, src_ptr);
    int32_t i, v;
    for (i = 0; i < count; i ++) {
        ecs_vec_t *to[BOX_MESH3_VECTORS];
        ecs_vec_t *from[BOX_MESH3_VECTORS];

        box_mesh3_vectors(&dst[i], to);
        box_mesh3_vectors(&src[i], from);
        for (v = 0; v < BOX_MESH3_VECTORS; v ++) {
            box_mesh3_vec_copy(to[v], from[v], box_mesh3_elem[v]);
        }
    }
}

/** Hand the vectors of a Mesh3 over, leaving the source empty.
 *
 * @param dst_ptr The components to overwrite
 * @param src_ptr The components to empty
 * @param count Number of components
 * @param ti Type info
 */
static void box_mesh3_move(
    void *dst_ptr,
    void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Mesh3 *dst = dst_ptr;
    Mesh3 *src = src_ptr;

    box_mesh3_dtor(dst_ptr, count, ti);
    memcpy(dst, src, (size_t)count * sizeof(Mesh3));
    memset(src, 0, (size_t)count * sizeof(Mesh3));
}

/** Report a problem with the mesh data of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param problem What is wrong with the data
 */
static void box_mesh3_error(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *problem)
{
    char *path = ecs_get_path(world, e);

    ecs_err("box.geometry: Mesh3 of '%s' %s, the mesh is not uploaded", path, problem);
    ecs_os_free(path);
}

/** Check that the streams of a Mesh3 agree and that every index names a vertex.
 *
 * @param world The world
 * @param e The entity holding the component
 * @param m The component
 * @return True when the data can be uploaded
 */
static bool box_mesh3_check(
    ecs_world_t *world,
    ecs_entity_t e,
    const Mesh3 *m)
{
    int32_t vertices = ecs_vec_count(&m->vertices);
    int32_t indices = ecs_vec_count(&m->indices);
    const uint32_t *index = ecs_vec_first_t(&m->indices, uint32_t);
    const char *problem = NULL;
    bool result = false;
    int32_t i;

    if (!vertices) {
        problem = "has no vertices";
    } else if (ecs_vec_count(&m->normals) && ecs_vec_count(&m->normals) != vertices) {
        problem = "holds a normal count that is not its vertex count";
    } else if (ecs_vec_count(&m->uvs) && ecs_vec_count(&m->uvs) != vertices) {
        problem = "holds a uv count that is not its vertex count";
    } else if (ecs_vec_count(&m->colors) && ecs_vec_count(&m->colors) != vertices) {
        problem = "holds a color count that is not its vertex count";
    } else if ((indices ? indices : vertices) % 3) {
        problem = "does not hold whole triangles";
    }

    for (i = 0; i < indices && !problem; i ++) {
        if (index[i] >= (uint32_t)vertices) {
            problem = "holds an index that names no vertex";
        }
    }

    if (problem) {
        box_mesh3_error(world, e, problem);
    } else {
        result = true;
    }

    return result;
}

/** Derive vertex normals from the triangles, weighted by their area.
 *
 * @param mesh The mesh, its positions and indices are filled in
 */
static void box_mesh3_derive_normals(
    box_mesh_data_t *mesh)
{
    uint32_t i;

    memset(mesh->normals, 0, (size_t)mesh->vertex_count * 3 * sizeof(float));
    for (i = 0; i + 2 < mesh->index_count; i += 3) {
        const uint32_t *tri = &mesh->indices[i];
        const float *pa = &mesh->positions[tri[0] * 3];
        const float *pb = &mesh->positions[tri[1] * 3];
        const float *pc = &mesh->positions[tri[2] * 3];
        vec3 a = vec3_new(pa[0], pa[1], pa[2]);
        vec3 n = vec3_cross(
            vec3_sub(vec3_new(pb[0], pb[1], pb[2]), a),
            vec3_sub(vec3_new(pc[0], pc[1], pc[2]), a));
        uint32_t c;
        for (c = 0; c < 3; c ++) {
            float *dst = &mesh->normals[tri[c] * 3];

            dst[0] += n.x;
            dst[1] += n.y;
            dst[2] += n.z;
        }
    }

    for (i = 0; i < mesh->vertex_count; i ++) {
        float *dst = &mesh->normals[i * 3];
        vec3 n = vec3_new(dst[0], dst[1], dst[2]);
        float len = vec3_length(n);

        n = len > 1e-12f ? vec3_scale(n, 1.0f / len) : vec3_new(0.0f, 1.0f, 0.0f);
        dst[0] = n.x;
        dst[1] = n.y;
        dst[2] = n.z;
    }
}

/** Pack the per vertex colors of a Mesh3 into the stream the upload reads.
 *
 * The component holds display encoded colors without an alpha channel, so the
 * alpha of every vertex is opaque and leaves the roughness of the material
 * alone. A mesh without colors keeps an empty stream, which uploads as white.
 *
 * @param m The component, already checked
 * @param out The mesh to fill, its vertex count is set
 */
static void box_mesh3_colors(
    const Mesh3 *m,
    box_mesh_data_t *out)
{
    const rgb *colors = ecs_vec_first_t(&m->colors, rgb);
    uint32_t i;

    if (!colors) {
        goto done;
    }

    out->colors = ecs_os_malloc_n(uint32_t, out->vertex_count);
    for (i = 0; i < out->vertex_count; i ++) {
        out->colors[i] = (uint32_t)colors[i].r | ((uint32_t)colors[i].g << 8) |
            ((uint32_t)colors[i].b << 16) | 0xFF000000u;
    }
done:
    return;
}

/** Build the mesh data a Mesh3 describes, filling in what it leaves out.
 *
 * @param m The component, already checked
 * @param out The mesh to fill
 */
static void box_mesh3_build(
    const Mesh3 *m,
    box_mesh_data_t *out)
{
    uint32_t vertices = (uint32_t)ecs_vec_count(&m->vertices);
    uint32_t indices = (uint32_t)ecs_vec_count(&m->indices);
    const vec3 *normals = ecs_vec_first_t(&m->normals, vec3);
    const vec2 *uvs = ecs_vec_first_t(&m->uvs, vec2);
    box_mesh_builder_t builder;
    uint32_t i;

    box_mesh_builder_init(&builder, out, vertices, indices ? indices : vertices);
    memcpy(out->positions, ecs_vec_first(&m->vertices), (size_t)vertices * 3 * sizeof(float));
    box_mesh3_colors(m, out);
    if (uvs) {
        memcpy(out->uvs, uvs, (size_t)vertices * 2 * sizeof(float));
    } else {
        memset(out->uvs, 0, (size_t)vertices * 2 * sizeof(float));
    }

    if (indices) {
        memcpy(out->indices, ecs_vec_first(&m->indices), (size_t)indices * sizeof(uint32_t));
    } else {
        for (i = 0; i < vertices; i ++) {
            out->indices[i] = i;
        }
    }

    if (normals) {
        memcpy(out->normals, normals, (size_t)vertices * 3 * sizeof(float));
    } else {
        box_mesh3_derive_normals(out);
    }

    box_mesh_builder_finish(&builder);
}

/** Mix a block of bytes into a 64 bit FNV-1a hash, eight bytes at a time.
 *
 * @param hash The hash so far
 * @param data The bytes
 * @param size Number of bytes
 * @return The hash with the bytes mixed in
 */
static uint64_t box_mesh3_hash_bytes(
    uint64_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    size_t i = 0;

    while (i + sizeof(uint64_t) <= size) {
        uint64_t word;

        memcpy(&word, bytes + i, sizeof(word));
        hash = (hash ^ word) * BOX_MESH3_HASH_PRIME;
        i += sizeof(uint64_t);
    }

    while (i < size) {
        hash = (hash ^ bytes[i]) * BOX_MESH3_HASH_PRIME;
        i ++;
    }

    return hash;
}

/** Hash the streams of a Mesh3, which is what the cache shares meshes on.
 *
 * @param m The component
 * @return The hash of its contents
 */
static uint64_t box_mesh3_hash(
    const Mesh3 *m)
{
    Mesh3 *src = ECS_CONST_CAST(Mesh3*, m);
    ecs_vec_t *vectors[BOX_MESH3_VECTORS];
    uint64_t hash = BOX_MESH3_HASH_SEED;
    int32_t v;

    box_mesh3_vectors(src, vectors);
    for (v = 0; v < BOX_MESH3_VECTORS; v ++) {
        int32_t count = ecs_vec_count(vectors[v]);
        size_t size = (size_t)box_mesh3_elem[v] * (size_t)count;

        hash = box_mesh3_hash_bytes(hash, &count, sizeof(count));
        hash = box_mesh3_hash_bytes(hash, ecs_vec_first(vectors[v]), size);
    }

    return hash;
}

/** Give an entity the shared mesh of its data and let go of the one it had.
 *
 * The mesh is written through the stage of the observer, because a Mesh3 that
 * arrives with an instantiated prefab is set while the entity is being moved to
 * its table, where a direct set would move it a second time.
 *
 * @param it The observer iterator
 * @param e The entity
 * @param m Its mesh data
 */
static void box_mesh3_apply(
    ecs_iter_t *it,
    ecs_entity_t e,
    const Mesh3 *m)
{
    if (!box_mesh3_check(it->world, e, m)) {
        goto done;
    }

    const RenderMesh *previous = ecs_get(it->world, e, RenderMesh);
    uint32_t released = previous ? previous->handle : 0;
    bool release = previous != NULL;
    uint64_t hash = box_mesh3_hash(m);
    RenderMesh mesh = { .scale = vec3_new(1.0f, 1.0f, 1.0f) };

    if (!box_mesh_cache_acquire(it->world, hash, &mesh.handle,
        &mesh.bounds_min, &mesh.bounds_max))
    {
        box_mesh_data_t data = {0};

        box_mesh3_build(m, &data);
        mesh.handle = box_mesh_cache_upload(it->world, hash, &data);
        mesh.bounds_min = data.bounds_min;
        mesh.bounds_max = data.bounds_max;
        box_mesh_data_fini(&data);
    }

    ecs_set_ptr(it->stage, e, RenderMesh, &mesh);
    if (release) {
        box_mesh_cache_release(it->world, released);
    }
done:
    return;
}

/** Mesh3 observer.
 *
 * @param it Iterator over the changed meshes
 */
static void box_mesh3_on_set(
    ecs_iter_t *it)
{
    const Mesh3 *m = ecs_field(it, Mesh3, 0);
    bool self = ecs_field_is_self(it, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_mesh3_apply(it, it->entities[i], self ? &m[i] : m);
    }
}

/** An entity lost its Mesh3: it loses the mesh and the instance with it.
 *
 * @param it Iterator over the entities that lost the component
 */
static void box_mesh3_on_remove(
    ecs_iter_t *it)
{
    bool deleting = !it->other_table || !ecs_table_get_type(it->other_table)->count ||
        ecs_is_fini(it->world) || ecs_should_quit(it->world);
    int32_t i;
    for (i = 0; !deleting && i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        if (ecs_has(it->world, e, RenderMesh)) {
            ecs_remove(it->stage, e, RenderMesh);
            ecs_remove(it->stage, e, RenderInstance);
        }
    }
}

/** A RenderMesh went away: drop the reference it held on a custom mesh.
 *
 * @param it Iterator over the entities that lost their mesh
 */
static void box_mesh3_on_mesh_removed(
    ecs_iter_t *it)
{
    const RenderMesh *mesh = ecs_field(it, RenderMesh, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_mesh_cache_release(it->world, mesh[i].handle);
    }
}

void box_mesh3_init(
    ecs_world_t *world)
{
    ecs_set_hooks(world, Mesh3, {
        .ctor = flecs_default_ctor,
        .dtor = box_mesh3_dtor,
        .copy = box_mesh3_copy,
        .move = box_mesh3_move
    });

    ecs_add_pair(world, ecs_id(Mesh3), EcsOnInstantiate, EcsInherit);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Mesh3), .inout = EcsIn }},
        .events = { EcsOnSet },
        .callback = box_mesh3_on_set
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Mesh3), .inout = EcsIn }},
        .events = { EcsOnRemove },
        .callback = box_mesh3_on_remove
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(RenderMesh), .inout = EcsIn }},
        .events = { EcsOnRemove },
        .callback = box_mesh3_on_mesh_removed
    });
}
