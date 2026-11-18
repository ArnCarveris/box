/**
 * @file controllers/triangles.c
 * @brief Triangles of TriangleCollider shapes: a triangle hierarchy per mesh and its queries.
 *
 * A TriangleCollider shape, such as a terrain or a hull, collides by its triangles instead of
 * its oriented bounding box. The hierarchy of a mesh is built in its unit space the first time
 * a body comes near it and placed by the world transform and scale of each shape that uses it.
 *
 * An entry remembers the vertices it was built from, so a mesh handle that comes back for
 * another mesh is built again.
 */

#include "controllers.h"
#include "../geometry/mesh_cache.h"
#include "../spatial/bvh.h"
#include <math.h>

ECS_COMPONENT_DECLARE(BoxTriangleMeshes);

/** Triangle hierarchy of one mesh */
typedef struct box_triangle_mesh_t {
    box_bvh_t tree;                 /**< Leaves are triangle indices, boxes in unit mesh space */
    const box_vertex_t *vertices;   /**< Vertices it was built from */
    int32_t vertex_count;           /**< Number of vertices it was built from */
    int32_t index_count;            /**< Number of indices it was built from */
} box_triangle_mesh_t;

/** A TriangleCollider shape as queries see it: its mesh, tree and placement */
typedef struct box_triangle_shape_t {
    ecs_entity_t entity;            /**< The shape */
    const box_mesh_t *mesh;         /**< The unit mesh */
    const box_bvh_t *tree;          /**< Its triangle hierarchy */
    mat4 pose;                      /**< Unit mesh space to world space, scale included */
} box_triangle_shape_t;

/** An overlap query over the triangles of a shape */
typedef struct box_triangle_overlap_t {
    const box_triangle_shape_t *shape; /**< The shape */
    ecs_vec_t *out;                    /**< Vector of box_collider_t the triangles go to */
    int32_t count;                     /**< Triangles appended */
} box_triangle_overlap_t;

/** A ray query over the triangles of a shape */
typedef struct box_triangle_cast_t {
    const box_triangle_shape_t *shape; /**< The shape */
    vec3 origin;                       /**< Origin of the ray */
    vec3 dir;                          /**< Direction of the ray, as given */
    float scale;                       /**< Length of dir: walk distances over it are in lengths */
    float reach;                       /**< Nearest hit so far, in world units along the ray */
    float t;                           /**< Nearest hit so far, in lengths of dir */
    vec3 normal;                       /**< Normal at the nearest hit */
    bool hit;                          /**< A triangle was hit */
} box_triangle_cast_t;

/** Free the hierarchies of a world.
 *
 * @param ptr The BoxTriangleMeshes values
 * @param count Number of values
 * @param ti Type info
 */
static void box_triangle_meshes_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    BoxTriangleMeshes *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_map_iter_t it = ecs_map_iter(&values[i].meshes);
        while (ecs_map_next(&it)) {
            box_triangle_mesh_t *tm = ecs_map_ptr(&it);
            box_bvh_fini(&tm->tree);
            ecs_os_free(tm);
        }

        ecs_map_fini(&values[i].meshes);
    }
}

/** Set up the hierarchies of a world.
 *
 * @param ptr The BoxTriangleMeshes values
 * @param count Number of values
 * @param ti Type info
 */
static void box_triangle_meshes_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    BoxTriangleMeshes *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_map_init(&values[i].meshes, NULL);
    }
}

/** Get a corner of a triangle of a mesh in its unit space.
 *
 * @param mesh The mesh
 * @param first First index of the triangle
 * @param k Corner, 0 to 2
 * @return The position of the corner
 */
static const float* box_triangle_corner(
    const box_mesh_t *mesh,
    int32_t first,
    int32_t k)
{
    uint32_t index = mesh->index_count ? mesh->indices[first + k] : (uint32_t)(first + k);

    return mesh->vertices[index].position;
}

/** Build the hierarchy of a mesh.
 *
 * @param mesh The mesh
 * @param tm The entry, its tree is replaced
 */
static void box_triangle_build(
    const box_mesh_t *mesh,
    box_triangle_mesh_t *tm)
{
    int32_t count = (mesh->index_count ? mesh->index_count : mesh->vertex_count) / 3;
    box_bvh_leaf_t *leaves = ecs_os_malloc_n(box_bvh_leaf_t, count > 0 ? count : 1);
    int32_t i, k;
    for (i = 0; i < count; i ++) {
        box_bvh_leaf_t *leaf = &leaves[i];
        glm_vec3_copy(ECS_CONST_CAST(float*, box_triangle_corner(mesh, i * 3, 0)), leaf->min);
        glm_vec3_copy(leaf->min, leaf->max);
        for (k = 1; k < 3; k ++) {
            float *v = ECS_CONST_CAST(float*, box_triangle_corner(mesh, i * 3, k));
            glm_vec3_minv(leaf->min, v, leaf->min);
            glm_vec3_maxv(leaf->max, v, leaf->max);
        }

        leaf->item = i;
    }

    box_bvh_fini(&tm->tree);
    box_bvh_init(&tm->tree);
    box_bvh_build(&tm->tree, leaves, count);
    ecs_os_free(leaves);
    tm->vertices = mesh->vertices;
    tm->vertex_count = mesh->vertex_count;
    tm->index_count = mesh->index_count;
}

/** Get the hierarchy of a mesh, building it when it is missing or was built for another mesh.
 *
 * The controllers run on the main thread, so the singleton made at import changes in place
 * while the world is read only.
 *
 * @param world The world
 * @param handle The mesh handle
 * @param mesh The mesh
 * @return The hierarchy
 */
static const box_bvh_t* box_triangle_tree(
    const ecs_world_t *world,
    uint32_t handle,
    const box_mesh_t *mesh)
{
    BoxTriangleMeshes *meshes = ECS_CONST_CAST(BoxTriangleMeshes*,
        ecs_singleton_get(world, BoxTriangleMeshes));
    box_triangle_mesh_t *tm = ecs_map_get_deref(&meshes->meshes, box_triangle_mesh_t, handle);
    if (!tm) {
        tm = ecs_os_calloc_t(box_triangle_mesh_t);
        box_bvh_init(&tm->tree);
        ecs_map_insert_ptr(&meshes->meshes, handle, tm);
    }

    if (tm->vertices != mesh->vertices || tm->vertex_count != mesh->vertex_count ||
        tm->index_count != mesh->index_count)
    {
        box_triangle_build(mesh, tm);
    }

    return &tm->tree;
}

/** Find the mesh, hierarchy and placement of a TriangleCollider shape.
 *
 * @param world The world
 * @param e The shape
 * @param out The shape, written
 * @return false when the shape is not a TriangleCollider with a mesh
 */
static bool box_triangle_shape(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_triangle_shape_t *out)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    const box_mesh_t *mesh = NULL;
    WorldTransform t_buf;
    const WorldTransform *t = NULL;
    vec3 scale;
    bool result = box_collider_has_triangles(world, e) && cache && shape && shape->mesh;
    if (result) {
        mesh = box_mesh_cache_get(cache, shape->mesh);
        result = mesh && mesh->vertex_count;
    }

    if (!result) {
        goto done;
    }

    t = box_transform_world(world, e, &t_buf);
    out->entity = e;
    out->mesh = mesh;
    out->tree = box_triangle_tree(world, shape->mesh, mesh);
    if (t) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, t->m), out->pose);
    } else {
        glm_mat4_identity(out->pose);
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, shape->scale), scale);
    glm_scale(out->pose, scale);

done:
    return result;
}

/** Make the world space collider of a triangle of a shape.
 *
 * @param shape The shape
 * @param tri The triangle index
 * @param out The collider, written
 */
static void box_triangle_of(
    const box_triangle_shape_t *shape,
    int32_t tri,
    box_collider_t *out)
{
    vec3 v[3];
    int32_t k;
    for (k = 0; k < 3; k ++) {
        float *p = ECS_CONST_CAST(float*, box_triangle_corner(shape->mesh, tri * 3, k));
        glm_mat4_mulv3(ECS_CONST_CAST(vec4*, shape->pose), p, 1.0f, v[k]);
    }

    box_triangle_collider(shape->entity, ECS_CONST_CAST(const vec3*, v), out);
}

/** Append a triangle an overlap query reached.
 *
 * @param item The triangle index
 * @param ctx The query
 * @return true to keep walking
 */
static bool box_triangle_overlap_visit(
    int32_t item,
    void *ctx)
{
    box_triangle_overlap_t *q = ctx;
    box_collider_t *c = ecs_vec_append_t(NULL, q->out, box_collider_t);
    box_triangle_of(q->shape, item, c);
    q->count ++;

    return true;
}

/** Keep the nearest triangle a ray query reached.
 *
 * @param item The triangle index
 * @param ctx The query
 * @return true to keep walking
 */
static bool box_triangle_cast_visit(
    int32_t item,
    void *ctx)
{
    box_triangle_cast_t *q = ctx;
    box_collider_t c;
    vec3 normal;
    float t;
    box_triangle_of(q->shape, item, &c);
    if (box_triangle_ray(&c, q->origin, q->dir, q->t, &t, normal)) {
        q->t = t;
        q->reach = t * q->scale;
        glm_vec3_copy(normal, q->normal);
        q->hit = true;
    }

    return true;
}

void box_collider_triangles_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, BoxTriangleMeshes);
    ecs_set_hooks(world, BoxTriangleMeshes, {
        .ctor = box_triangle_meshes_ctor,
        .dtor = box_triangle_meshes_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });

    ecs_singleton_ensure(world, BoxTriangleMeshes);
}

bool box_collider_solid(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return !ecs_id(Water) || !ecs_has(world, e, Water);
}

bool box_collider_has_triangles(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_has(world, e, TriangleCollider) && ecs_has(world, e, ShapeMesh);
}

int32_t box_collider_triangles(
    const ecs_world_t *world,
    ecs_entity_t e,
    const vec3 min,
    const vec3 max,
    ecs_vec_t *out)
{
    box_triangle_shape_t shape;
    box_triangle_overlap_t q = { .shape = &shape, .out = out };
    if (box_triangle_shape(world, e, &shape)) {
        box_bvh_overlap_placed(shape.tree, (const float*)shape.pose, min, max,
            box_triangle_overlap_visit, &q);
    }

    return q.count;
}

bool box_collider_cast(
    const ecs_world_t *world,
    ecs_entity_t e,
    const vec3 origin,
    const vec3 dir,
    float max,
    float *t,
    vec3 normal)
{
    box_triangle_shape_t shape;
    box_triangle_cast_t q = { .shape = &shape };
    box_collider_t c;
    box_bvh_ray_t ray;
    if (!box_triangle_shape(world, e, &shape)) {
        q.hit = box_collider_resolve(world, e, &c) &&
            box_collider_ray(&c, origin, dir, max, t, normal);
        goto done;
    }

    if (!box_bvh_ray_init(&ray, origin, dir)) {
        goto done;
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, origin), q.origin);
    glm_vec3_copy(ECS_CONST_CAST(float*, dir), q.dir);
    q.scale = glm_vec3_norm(ECS_CONST_CAST(float*, dir));
    q.t = max;
    q.reach = max < FLT_MAX / q.scale ? max * q.scale : FLT_MAX;
    box_bvh_ray_placed(shape.tree, (const float*)shape.pose, &ray, &q.reach,
        box_triangle_cast_visit, &q);
    if (q.hit) {
        *t = q.t;
        if (normal) {
            glm_vec3_copy(q.normal, normal);
        }
    }

done:
    return q.hit;
}
