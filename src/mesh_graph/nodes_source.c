/**
 * @file mesh_graph/nodes_source.c
 * @brief Source nodes of mesh graphs, which read the world on the main thread: Shape, Mesh3 and
 * Path.
 */

#include "mesh_graph.h"
#include "../geometry/paths.h"
#include <math.h>

/** Resolve the entity a source node reads.
 *
 * @param node The node entity
 * @param entity The entity member of the node, 0 for the node itself
 * @return The entity
 */
static ecs_entity_t box_mesh_source_entity(
    ecs_entity_t node,
    ecs_entity_t entity)
{
    return entity ? entity : node;
}

/** Check whether the MeshGraph of an entity is still evaluating its first mesh.
 *
 * @param world The world
 * @param e The entity
 * @return Whether it has a MeshGraph that has no mesh yet and did not fail
 */
static bool box_mesh_source_loading(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const MeshGraphStatus *status = ecs_get(world, e, MeshGraphStatus);
    bool result = false;
    if (ecs_has(world, e, MeshGraph)) {
        result = !status || (status->state != MeshGraphFailed && !status->revision);
    }

    return result;
}

/** Read the mesh of the shape of an entity, with the instance scale of the shape.
 *
 * @param world The world
 * @param node The node entity
 * @param component The MeshShape
 * @param out The Mesh
 * @param source Receives the entity with the shape
 * @param error Receives why it failed
 * @return The outcome of the capture
 */
static int box_mesh_shape_capture(
    const ecs_world_t *world,
    ecs_entity_t node,
    const void *component,
    box_mg_value_t *out,
    ecs_entity_t *source,
    char **error)
{
    const MeshShape *shape = component;
    ecs_entity_t e = box_mesh_source_entity(node, shape->entity);
    int result = BOX_MESH_CAPTURE_OK;
    int32_t i;
    *source = e;
    if (!ecs_is_alive(world, e)) {
        *error = ecs_os_strdup("its entity is not alive");
        result = BOX_MESH_CAPTURE_ERROR;
        goto done;
    }

    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    uint32_t handle = sm ? (sm->render_mesh ? sm->render_mesh : sm->mesh) : 0;
    const box_mesh_t *mesh = handle
        ? box_mesh_cache_get(box_mesh_cache_of(world), handle)
        : NULL;
    if (!mesh) {
        bool loading = box_mesh_source_loading(world, e);
        result = loading ? BOX_MESH_CAPTURE_HOLD : BOX_MESH_CAPTURE_WAIT;
        if (!loading) {
            *error = ecs_os_strdup(sm
                ? "its entity has a shape without a mesh"
                : "its entity has no shape");
        }

        goto done;
    }

    box_mg_mesh_t *m = &out->mesh;
    box_mg_mesh_resize(m, mesh->vertex_count, mesh->index_count);
    for (i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        vec3 n = {
            v->normal[0] / (sm->scale[0] != 0 ? sm->scale[0] : 1.0f),
            v->normal[1] / (sm->scale[1] != 0 ? sm->scale[1] : 1.0f),
            v->normal[2] / (sm->scale[2] != 0 ? sm->scale[2] : 1.0f)
        };
        glm_vec3_mul((float*)v->position, (float*)sm->scale, m->positions[i]);
        glm_vec3_normalize_to(n, m->normals[i]);
        glm_vec2_copy((float*)v->uv, m->uvs[i]);
    }

    if (mesh->index_count) {
        ecs_os_memcpy_n(m->indices, mesh->indices, uint32_t, mesh->index_count);
    }

done:
    return result;
}

/** Read the Mesh3 of an entity.
 *
 * @param world The world
 * @param node The node entity
 * @param component The MeshMesh3
 * @param out The Mesh
 * @param source Receives the entity with the Mesh3
 * @param error Receives why it failed
 * @return The outcome of the capture
 */
static int box_mesh_mesh3_capture(
    const ecs_world_t *world,
    ecs_entity_t node,
    const void *component,
    box_mg_value_t *out,
    ecs_entity_t *source,
    char **error)
{
    const MeshMesh3 *n = component;
    ecs_entity_t e = box_mesh_source_entity(node, n->entity);
    const Mesh3 *mesh = ecs_is_alive(world, e) ? ecs_get(world, e, Mesh3) : NULL;
    int result = BOX_MESH_CAPTURE_OK;
    int32_t i, count, indices;
    *source = e;
    if (!mesh) {
        *error = ecs_os_strdup("its entity has no Mesh3");
        result = BOX_MESH_CAPTURE_WAIT;
        goto done;
    }

    count = ecs_vec_count(&mesh->vertices);
    indices = ecs_vec_count(&mesh->indices);
    indices = indices ? indices - indices % 3 : count - count % 3;
    const uint32_t *src = ecs_vec_first_t(&mesh->indices, uint32_t);
    for (i = 0; src && i < indices; i ++) {
        if (src[i] >= (uint32_t)count) {
            *error = flecs_asprintf("index %u of its Mesh3 is out of range", src[i]);
            result = BOX_MESH_CAPTURE_ERROR;
            goto done;
        }
    }

    box_mg_mesh_t *m = &out->mesh;
    box_mg_mesh_resize(m, count, indices);
    if (count) {
        ecs_os_memcpy_n(m->positions, ecs_vec_first(&mesh->vertices), vec3, count);
    }

    if (ecs_vec_count(&mesh->uvs) == count && count) {
        ecs_os_memcpy_n(m->uvs, ecs_vec_first(&mesh->uvs), vec2, count);
    }

    for (i = 0; i < indices; i ++) {
        m->indices[i] = src ? src[i] : (uint32_t)i;
    }

    if (ecs_vec_count(&mesh->normals) == count && count) {
        ecs_os_memcpy_n(m->normals, ecs_vec_first(&mesh->normals), vec3, count);
    } else {
        box_mg_mesh_facet(m);
    }

done:
    return result;
}

/** Sample the Path3D or Path2D of an entity into points.
 *
 * @param world The world
 * @param e The entity
 * @param steps Points per curved segment
 * @param points Receives the points as vec3
 * @param closed Receives whether the path is closed
 * @return Whether the entity has a valid path
 */
static bool box_mesh_path_sample(
    const ecs_world_t *world,
    ecs_entity_t e,
    int32_t steps,
    ecs_vec_t *points,
    bool *closed)
{
    bool result = false;
    int32_t i;
    if (ecs_has(world, e, Path3D)) {
        *closed = ecs_get(world, e, Path3D)->closed;
        result = box_path3_sample_steps(world, e, steps, points);
    } else if (ecs_has(world, e, Path2D)) {
        ecs_vec_t flat;
        ecs_vec_init_t(NULL, &flat, vec2, 0);
        *closed = ecs_get(world, e, Path2D)->closed;
        result = box_path2_sample_steps(world, e, steps, &flat);
        for (i = 0; result && i < ecs_vec_count(&flat); i ++) {
            const float *p = *ecs_vec_get_t(&flat, vec2, i);
            glm_vec3_copy((vec3){ p[0], p[1], 0.0f }, *ecs_vec_append_t(NULL, points, vec3));
        }

        ecs_vec_fini_t(NULL, &flat, vec2);
    }

    return result;
}

/** Sample the Path3D or Path2D of an entity into one curve.
 *
 * @param world The world
 * @param node The node entity
 * @param component The MeshPath
 * @param out The Curves
 * @param source Receives the entity with the path
 * @param error Receives why it failed
 * @return The outcome of the capture
 */
static int box_mesh_path_capture(
    const ecs_world_t *world,
    ecs_entity_t node,
    const void *component,
    box_mg_value_t *out,
    ecs_entity_t *source,
    char **error)
{
    const MeshPath *n = component;
    ecs_entity_t e = box_mesh_source_entity(node, n->path);
    int result = BOX_MESH_CAPTURE_OK;
    bool closed = false;
    int32_t i, count;
    ecs_vec_t points;
    ecs_vec_init_t(NULL, &points, vec3, 0);
    *source = e;
    if (!ecs_is_alive(world, e) || (!ecs_has(world, e, Path3D) && !ecs_has(world, e, Path2D))) {
        *error = ecs_os_strdup("its path entity has no Path3D or Path2D");
        result = BOX_MESH_CAPTURE_WAIT;
        goto done;
    }

    if (!box_mesh_path_sample(world, e, n->steps, &points, &closed) ||
        ecs_vec_count(&points) < 2)
    {
        *error = ecs_os_strdup("its path is not valid");
        result = BOX_MESH_CAPTURE_ERROR;
        goto done;
    }

    if (closed) {
        vec3 start;
        glm_vec3_copy(*ecs_vec_first_t(&points, vec3), start);
        glm_vec3_copy(start, *ecs_vec_append_t(NULL, &points, vec3));
    }

    count = ecs_vec_count(&points);
    box_mg_curves_t *c = &out->curves;
    box_mg_curves_resize(c, 1, count);
    c->offsets[0] = 0;
    c->offsets[1] = count;
    ecs_os_memcpy_n(c->positions, ecs_vec_first(&points), vec3, count);
    float *lengths = ecs_os_malloc_n(float, count);
    float length = box_mg_curve_lengths(c, 0, lengths);
    float r0 = n->radius > 0.0f ? n->radius : 0.1f;
    float r1 = n->radius_end > 0.0f ? n->radius_end : r0;
    for (i = 0; i < count; i ++) {
        float t = length > 0.0f ? lengths[i] / length : 0.0f;
        c->radii[i] = r0 + (r1 - r0) * t;
    }

    ecs_os_free(lengths);
    box_mg_curves_frames(c);
done:
    ecs_vec_fini_t(NULL, &points, vec3);

    return result;
}

/** Construct MeshPath values with the documented defaults.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_path_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshPath *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshPath){ .steps = 16, .radius = 0.1f };
    }
}

void box_mesh_nodes_source_register(
    ecs_world_t *world)
{
    ecs_set_hooks(world, MeshPath, { .ctor = box_mesh_path_ctor });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshShape),
        .output = MeshValueMesh,
        .capture = box_mesh_shape_capture
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshMesh3),
        .output = MeshValueMesh,
        .capture = box_mesh_mesh3_capture
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshPath),
        .output = MeshValueCurves,
        .params = {
            { "steps", offsetof(MeshPath, steps), ECS_SIZEOF(int32_t) },
            { "radius", offsetof(MeshPath, radius), ECS_SIZEOF(float) },
            { "radius_end", offsetof(MeshPath, radius_end), ECS_SIZEOF(float) }
        },
        .capture = box_mesh_path_capture
    });
}
