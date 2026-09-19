/**
 * @file gltf/mesh.c
 * @brief Reads a glTF primitive into the Mesh3 that the mesh cache shares and uploads.
 *
 * glTF is right handed with Z toward the viewer and box is left handed with Z
 * away from it, so positions and normals are mirrored in Z. The front face of
 * box is the mirror of the glTF one, so that mirror also turns a glTF front
 * face into a box front face and the triangles keep the order they are given
 * in. The V axis of a glTF uv runs down the image and the one of a box texture
 * runs up it, so V is inverted.
 */

#include "gltf_internal.h"

/** Read the elements of an accessor into a vector of a Mesh3.
 *
 * @param accessor The accessor
 * @param vec The vector to fill
 * @param size Element size of the vector
 * @param components Floats per element
 * @param count Number of elements to read
 */
static void box_gltf_read(
    const cgltf_accessor *accessor,
    ecs_vec_t *vec,
    ecs_size_t size,
    int32_t components,
    int32_t count)
{
    ecs_vec_init(NULL, vec, size, count);
    ecs_vec_set_count(NULL, vec, size, count);
    cgltf_accessor_unpack_floats(accessor, ecs_vec_first(vec),
        (cgltf_size)count * (cgltf_size)components);
}

/** Mirror a stream of vectors in Z, which turns glTF axes into box axes.
 *
 * @param v The vectors
 * @param count Number of vectors
 */
static void box_gltf_mirror(
    vec3 *v,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        v[i].z = -v[i].z;
    }
}

/** Flip the V axis of a stream of texture coordinates.
 *
 * @param uv The coordinates
 * @param count Number of coordinates
 */
static void box_gltf_flip_v(
    vec2 *uv,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        uv[i].y = 1.0f - uv[i].y;
    }
}

/** Read the triangles of a primitive, filling in the order of a primitive without indices.
 *
 * @param prim The primitive
 * @param vec The index vector to fill
 * @param vertices Number of vertices, the index count of a primitive without indices
 */
static void box_gltf_indices(
    const cgltf_primitive *prim,
    ecs_vec_t *vec,
    int32_t vertices)
{
    int32_t count = prim->indices ? (int32_t)prim->indices->count : vertices;
    int32_t i;

    ecs_vec_init_t(NULL, vec, uint32_t, count);
    ecs_vec_set_count_t(NULL, vec, uint32_t, count);

    uint32_t *out = ecs_vec_first_t(vec, uint32_t);
    if (prim->indices) {
        cgltf_accessor_unpack_indices(prim->indices, out, sizeof(uint32_t),
            (cgltf_size)count);
    } else {
        for (i = 0; i < count; i ++) {
            out[i] = (uint32_t)i;
        }
    }
}

bool box_gltf_mesh(
    ecs_world_t *world,
    ecs_entity_t e,
    const cgltf_primitive *prim)
{
    const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
    bool result = false;

    if (prim->type != cgltf_primitive_type_triangles || !pos || !pos->count) {
        goto done;
    }

    const cgltf_accessor *normals = cgltf_find_accessor(prim, cgltf_attribute_type_normal, 0);
    const cgltf_accessor *uvs = cgltf_find_accessor(prim, cgltf_attribute_type_texcoord, 0);
    int32_t count = (int32_t)pos->count;
    Mesh3 *m = ecs_ensure(world, e, Mesh3);

    box_gltf_read(pos, &m->vertices, ECS_SIZEOF(vec3), 3, count);
    box_gltf_mirror(ecs_vec_first_t(&m->vertices, vec3), count);

    if (normals && (int32_t)normals->count == count) {
        box_gltf_read(normals, &m->normals, ECS_SIZEOF(vec3), 3, count);
        box_gltf_mirror(ecs_vec_first_t(&m->normals, vec3), count);
    }

    if (uvs && (int32_t)uvs->count == count) {
        box_gltf_read(uvs, &m->uvs, ECS_SIZEOF(vec2), 2, count);
        box_gltf_flip_v(ecs_vec_first_t(&m->uvs, vec2), count);
    }

    box_gltf_indices(prim, &m->indices, count);
    ecs_modified(world, e, Mesh3);
    result = true;
done:
    return result;
}
