/**
 * @file picking/pick.c
 * @brief Casts the ray of a cursor through the views of a surface into their scenes.
 *
 * Shapes and UI elements come from the spatial index with an exact narrow phase. A hit that
 * shows the texture of a surface picks on through that surface at its UV.
 */

#include "picking.h"
#include "../render/render.h"
#include "../spatial/spatial.h"
#include "../geometry/mesh_cache.h"
#include "../jobs/jobs.h"
#include <math.h>
#include <stdlib.h>

/** Distance, relative to the nearest hit, within which hits count as equally near */
#define BOX_PICK_TIE (1e-4f)

/** Triangles from which a picked mesh gets a triangle hierarchy instead of a linear scan */
#define BOX_PICK_BVH_TRIANGLES (64)

/** Frames a triangle hierarchy outlives the last shape of its mesh, so a shape that goes away
 * and comes back, as in a reload or a toggle, finds it built */
#define BOX_PICK_MESH_IDLE (600)

/** The ray of a cursor in one view */
typedef struct box_pick_ray_t {
    ecs_entity_t scene;  /**< Scene of the camera of the view */
    vec3 origin;         /**< Point of the cursor on the near plane */
    vec3 dir;            /**< Unit direction into the scene */
    float max_distance;  /**< Distance to the far plane, 0 for none */
} box_pick_ray_t;

/** Walk of a ray through the triangle hierarchy of a unit mesh */
typedef struct box_pick_tri_walk_t {
    const box_mesh_t *mesh; /**< The unit mesh */
    box_bvh_ray_t ray;      /**< The ray in unit mesh space, its direction normalized */
    float reach;            /**< Distance of the nearest hit so far along the normalized ray */
    vec3 best;              /**< Distance and barycentric weights of the nearest hit */
    int32_t hit;            /**< First index of the nearest triangle, -1 for none */
} box_pick_tri_walk_t;

/** A job that builds the triangle hierarchy of a unit mesh on a worker */
typedef struct box_pick_build_t {
    box_mesh_t mesh;         /**< Copy of the mesh header; picking holds a reference */
    uint32_t handle;         /**< Mesh handle */
    box_pick_state_t *st;    /**< The module state, which takes the tree on completion */
    box_bvh_t tree;          /**< The tree the work builds */
} box_pick_build_t;

/** Context of the narrow phase of a ray cast */
typedef struct box_pick_cast_t {
    const ecs_world_t *world;  /**< The world */
    box_pick_state_t *st;      /**< The module state, which collects the candidates */
    const box_pick_ray_t *ray; /**< The ray */
} box_pick_cast_t;

/** Get the transform of an entity, the identity when it has none.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the transform
 */
static void box_pick_transform(
    const ecs_world_t *world,
    ecs_entity_t e,
    mat4 out)
{
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    if (t) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, t->m), out);
    } else {
        glm_mat4_identity(out);
    }
}

/** Get the hit area of a UI element: LayoutBounds, else its Rect, Circle or TextBounds.
 *
 * @param world The world
 * @param e The entity
 * @param size Receives width and height
 * @param circle Receives whether the area is the disc inside size
 * @return false when the entity is not a UI element
 */
static bool box_pick_ui_size(
    const ecs_world_t *world,
    ecs_entity_t e,
    vec2 size,
    bool *circle)
{
    const LayoutBounds *lb = ecs_get(world, e, LayoutBounds);
    const Rect *r = lb ? NULL : ecs_get(world, e, Rect);
    const Circle *c = lb || r ? NULL : ecs_get(world, e, Circle);
    const TextBounds *tb = lb || r || c ? NULL : ecs_get(world, e, TextBounds);
    bool result = true;
    *circle = false;
    if (lb) {
        size[0] = lb->width;
        size[1] = lb->height;
    } else if (r) {
        size[0] = r->width;
        size[1] = r->height;
    } else if (c) {
        size[0] = size[1] = c->radius * 2.0f;
        *circle = true;
    } else if (tb) {
        size[0] = tb->width;
        size[1] = tb->height;
    } else {
        result = false;
    }

    return result;
}

/** Intersect a ray with the area of a UI element in the XY plane of its transform.
 *
 * @param m The world transform of the element
 * @param size Width and height of the area, centered on the origin
 * @param circle Whether the area is the disc inside size
 * @param origin Origin of the ray
 * @param dir Direction of the ray
 * @param t Receives the distance of the hit
 * @param local Receives the hit in the local units of the element, y up
 * @return Whether the ray hits the area in front of its origin
 */
static bool box_pick_plane(
    const mat4 m,
    const vec2 size,
    bool circle,
    const vec3 origin,
    const vec3 dir,
    float *t,
    vec2 local)
{
    vec3 u, v, n, d, p;
    bool result = false;
    glm_vec3_copy(ECS_CONST_CAST(float*, m[0]), u);
    glm_vec3_copy(ECS_CONST_CAST(float*, m[1]), v);
    glm_vec3_cross(u, v, n);
    float denom = glm_vec3_dot(ECS_CONST_CAST(float*, dir), n);
    float uu = glm_vec3_dot(u, u), vv = glm_vec3_dot(v, v);
    if (fabsf(denom) < 1e-20f || uu <= 0 || vv <= 0) {
        goto done;
    }

    glm_vec3_sub(ECS_CONST_CAST(float*, m[3]), ECS_CONST_CAST(float*, origin), d);
    *t = glm_vec3_dot(d, n) / denom;
    if (*t < 0) {
        goto done;
    }

    glm_vec3_scale(ECS_CONST_CAST(float*, dir), *t, p);
    glm_vec3_add(p, ECS_CONST_CAST(float*, origin), p);
    glm_vec3_sub(p, ECS_CONST_CAST(float*, m[3]), d);
    local[0] = glm_vec3_dot(d, u) / uu;
    local[1] = glm_vec3_dot(d, v) / vv;
    if (circle) {
        float rx = size[0] * 0.5f, ry = size[1] * 0.5f;
        result = rx > 0 && ry > 0 &&
            (local[0] * local[0]) / (rx * rx) + (local[1] * local[1]) / (ry * ry) <= 1.0f;
    } else {
        result = fabsf(local[0]) <= size[0] * 0.5f && fabsf(local[1]) <= size[1] * 0.5f;
    }

done:
    return result;
}

/** Intersect a ray with one triangle, from both sides.
 *
 * @param o Origin of the ray
 * @param d Direction of the ray
 * @param v0 First corner
 * @param v1 Second corner
 * @param v2 Third corner
 * @param out Receives distance, then the barycentric weights of v1 and v2
 * @return Whether the ray hits the triangle in front of its origin
 */
static bool box_pick_triangle(
    const vec3 o,
    const vec3 d,
    const vec3 v0,
    const vec3 v1,
    const vec3 v2,
    vec3 out)
{
    vec3 e1, e2, p, s, q;
    bool result = false;
    glm_vec3_sub(ECS_CONST_CAST(float*, v1), ECS_CONST_CAST(float*, v0), e1);
    glm_vec3_sub(ECS_CONST_CAST(float*, v2), ECS_CONST_CAST(float*, v0), e2);
    glm_vec3_cross(ECS_CONST_CAST(float*, d), e2, p);
    float det = glm_vec3_dot(e1, p);
    if (det == 0) {
        goto done;
    }

    float inv = 1.0f / det;
    glm_vec3_sub(ECS_CONST_CAST(float*, o), ECS_CONST_CAST(float*, v0), s);
    out[1] = glm_vec3_dot(s, p) * inv;
    glm_vec3_cross(s, e1, q);
    out[2] = glm_vec3_dot(ECS_CONST_CAST(float*, d), q) * inv;
    out[0] = glm_vec3_dot(e2, q) * inv;
    result = out[1] >= 0 && out[2] >= 0 && out[1] + out[2] <= 1.0f && out[0] >= 0;
done:
    return result;
}

/** Get the corners of a triangle of a mesh.
 *
 * @param mesh The mesh
 * @param first First index of the triangle
 * @param scale Scale applied to the positions, NULL for none
 * @param v Receives the corners
 */
static void box_pick_corners(
    const box_mesh_t *mesh,
    int32_t first,
    const float *scale,
    vec3 v[3])
{
    int32_t k;
    for (k = 0; k < 3; k ++) {
        uint32_t index = mesh->index_count ? mesh->indices[first + k] : (uint32_t)(first + k);
        glm_vec3_copy(mesh->vertices[index].position, v[k]);
        if (scale) {
            glm_vec3_mul(v[k], ECS_CONST_CAST(float*, scale), v[k]);
        }
    }
}

/** Build the triangle hierarchy of a unit mesh.
 *
 * @param mesh The mesh
 * @param tree Receives the hierarchy, the item of a leaf is a triangle index
 */
static void box_pick_mesh_build(
    const box_mesh_t *mesh,
    box_bvh_t *tree)
{
    int32_t i, count = (mesh->index_count ? mesh->index_count : mesh->vertex_count) / 3;
    box_bvh_leaf_t *leaves = ecs_os_malloc_n(box_bvh_leaf_t, count);
    for (i = 0; i < count; i ++) {
        vec3 v[3];
        box_pick_corners(mesh, i * 3, NULL, v);
        glm_vec3_minv(v[0], v[1], leaves[i].min);
        glm_vec3_minv(leaves[i].min, v[2], leaves[i].min);
        glm_vec3_maxv(v[0], v[1], leaves[i].max);
        glm_vec3_maxv(leaves[i].max, v[2], leaves[i].max);
        leaves[i].item = i;
    }

    box_bvh_init(tree);
    box_bvh_build(tree, leaves, count);
    ecs_os_free(leaves);
}

/** Build the triangle hierarchy of a job on a worker.
 *
 * @param ctx The job
 */
static void box_pick_build_work(
    void *ctx)
{
    box_pick_build_t *b = ctx;
    box_pick_mesh_build(&b->mesh, &b->tree);
}

/** Hand the built hierarchy of a job to the entry of its mesh.
 *
 * @param world The world
 * @param ctx The job
 */
static void box_pick_build_complete(
    ecs_world_t *world,
    void *ctx)
{
    box_pick_build_t *b = ctx;
    box_pick_mesh_t *pm = ecs_map_get_deref(&b->st->meshes, box_pick_mesh_t, b->handle);
    (void)world;
    if (pm && pm->building) {
        pm->tree = b->tree;
        pm->ready = true;
        pm->building = false;
        box_bvh_init(&b->tree);
    }
}

/** Free a job and the tree it did not hand over.
 *
 * @param ctx The job
 */
static void box_pick_build_free(
    void *ctx)
{
    box_pick_build_t *b = ctx;
    box_bvh_fini(&b->tree);
    ecs_os_free(b);
}

/** Get the triangle hierarchy of a mesh with many triangles.
 *
 * The first pick of a mesh queues a job that builds it on a worker; until the job completes
 * the picks of that mesh scan its triangles, so no frame pays for the build.
 *
 * @param world The world
 * @param cache The mesh cache
 * @param st The module state
 * @param handle The mesh handle
 * @param mesh The mesh
 * @return The hierarchy, NULL for a mesh with few triangles or one whose tree is not built yet
 */
static const box_bvh_t* box_pick_mesh_tree(
    const ecs_world_t *world,
    box_mesh_cache_t *cache,
    box_pick_state_t *st,
    uint32_t handle,
    const box_mesh_t *mesh)
{
    int32_t count = mesh->index_count ? mesh->index_count : mesh->vertex_count;
    box_pick_mesh_t *result = NULL;
    if (count / 3 < BOX_PICK_BVH_TRIANGLES) {
        goto done;
    }

    result = ecs_map_get_deref(&st->meshes, box_pick_mesh_t, handle);
    if (!result) {
        box_pick_build_t *b = ecs_os_calloc_t(box_pick_build_t);
        b->mesh = *mesh;
        b->handle = handle;
        b->st = st;
        box_bvh_init(&b->tree);
        result = ecs_os_calloc_t(box_pick_mesh_t);
        result->handle = handle;
        box_bvh_init(&result->tree);
        box_mesh_cache_hold(cache, handle);
        ecs_map_insert_ptr(&st->meshes, handle, result);
        result->building = !box_job_submit(ECS_CONST_CAST(ecs_world_t*, world),
            &(box_job_desc_t){ .work = box_pick_build_work,
                .complete = box_pick_build_complete, .ctx_free = box_pick_build_free,
                .ctx = b });
        if (!result->building) {
            box_pick_build_free(b);
        }
    }

done:
    return result && result->ready ? &result->tree : NULL;
}

/** Keep the nearest triangle a ray walk reaches.
 *
 * @param tri The triangle index
 * @param ctx The walk
 * @return true to keep walking
 */
static bool box_pick_tri_visit(
    int32_t tri,
    void *ctx)
{
    box_pick_tri_walk_t *w = ctx;
    vec3 v[3], out;
    box_pick_corners(w->mesh, tri * 3, NULL, v);
    if (box_pick_triangle(w->ray.origin, w->ray.dir, v[0], v[1], v[2], out) &&
        out[0] < w->best[0])
    {
        glm_vec3_copy(out, w->best);
        w->hit = tri * 3;
        w->reach = out[0];
    }

    return true;
}

/** Intersect a ray in the space of a shape with its triangles through their hierarchy.
 *
 * The hierarchy holds the unit mesh, so the ray is divided by the scale of the shape, which
 * keeps the distance along the ray and the barycentric weights.
 *
 * @param tree The triangle hierarchy
 * @param mesh The unit mesh
 * @param scale Scale of the shape, without zero components
 * @param o Origin of the ray in the space of the shape
 * @param d Direction of the ray in the space of the shape
 * @param best Receives distance and barycentric weights of the nearest hit
 * @return First index of the nearest triangle, -1 for none
 */
static int32_t box_pick_mesh_walk(
    const box_bvh_t *tree,
    const box_mesh_t *mesh,
    const vec3 scale,
    const vec3 o,
    const vec3 d,
    vec3 best)
{
    box_pick_tri_walk_t w = { .mesh = mesh, .reach = INFINITY, .best = { INFINITY, 0, 0 },
        .hit = -1 };
    vec3 uo, ud;
    glm_vec3_div(ECS_CONST_CAST(float*, o), ECS_CONST_CAST(float*, scale), uo);
    glm_vec3_div(ECS_CONST_CAST(float*, d), ECS_CONST_CAST(float*, scale), ud);
    float len = glm_vec3_norm(ud);
    if (box_bvh_ray_init(&w.ray, uo, ud)) {
        box_bvh_ray(tree, &w.ray, &w.reach, box_pick_tri_visit, &w);
    }

    glm_vec3_copy(w.best, best);
    best[0] = w.hit >= 0 ? best[0] / len : INFINITY;

    return w.hit;
}

/** Intersect a ray with the triangles of the mesh of a shape, through the triangle hierarchy of
 * its unit mesh when it has many triangles.
 *
 * @param world The world
 * @param st The module state
 * @param e The entity with a ShapeMesh
 * @param origin Origin of the ray
 * @param dir Direction of the ray
 * @param t Receives the distance of the nearest hit
 * @param uv Receives the texture coordinate of the mesh at the hit
 * @return Whether the ray hits a triangle
 */
static bool box_pick_mesh(
    const ecs_world_t *world,
    box_pick_state_t *st,
    ecs_entity_t e,
    const vec3 origin,
    const vec3 dir,
    float *t,
    vec2 uv)
{
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const box_mesh_t *mesh = shape && shape->mesh && cache ?
        box_mesh_cache_get(cache, shape->mesh) : NULL;
    const box_bvh_t *tree = NULL;
    mat4 m, inv;
    vec3 o, d, best = { INFINITY, 0, 0 };
    int32_t i, k, count, hit = -1;
    if (!mesh || !mesh->vertex_count) {
        goto done;
    }

    box_pick_transform(world, e, m);
    glm_mat4_inv(m, inv);
    glm_mat4_mulv3(inv, ECS_CONST_CAST(float*, origin), 1.0f, o);
    glm_mat4_mulv3(inv, ECS_CONST_CAST(float*, dir), 0.0f, d);
    if (shape->scale[0] != 0 && shape->scale[1] != 0 && shape->scale[2] != 0) {
        tree = box_pick_mesh_tree(world, cache, st, shape->mesh, mesh);
    }

    if (tree) {
        hit = box_pick_mesh_walk(tree, mesh, shape->scale, o, d, best);
        goto found;
    }

    count = mesh->index_count ? mesh->index_count : mesh->vertex_count;
    for (i = 0; i + 2 < count; i += 3) {
        vec3 v[3], w;
        box_pick_corners(mesh, i, shape->scale, v);
        if (box_pick_triangle(o, d, v[0], v[1], v[2], w) && w[0] < best[0]) {
            glm_vec3_copy(w, best);
            hit = i;
        }
    }

found:
    if (hit >= 0) {
        uint32_t i0 = mesh->index_count ? mesh->indices[hit] : (uint32_t)hit;
        uint32_t i1 = mesh->index_count ? mesh->indices[hit + 1] : (uint32_t)hit + 1;
        uint32_t i2 = mesh->index_count ? mesh->indices[hit + 2] : (uint32_t)hit + 2;
        float w0 = 1.0f - best[1] - best[2];
        for (k = 0; k < 2; k ++) {
            uv[k] = w0 * mesh->vertices[i0].uv[k] + best[1] * mesh->vertices[i1].uv[k] +
                best[2] * mesh->vertices[i2].uv[k];
        }

        *t = best[0];
    }

done:
    return hit >= 0;
}

void box_pick_meshes_prune(
    ecs_world_t *world,
    box_pick_state_t *st)
{
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_map_iter_t it = ecs_map_iter(&st->meshes);
    uint32_t drop[64];
    int32_t i, count = 0;
    while (cache && count < 64 && ecs_map_next(&it)) {
        box_pick_mesh_t *pm = ecs_map_ptr(&it);
        pm->idle = box_mesh_cache_users(cache, pm->handle) ? 0 : pm->idle + 1;
        if (!pm->building && pm->idle > BOX_PICK_MESH_IDLE) {
            drop[count ++] = pm->handle;
        }
    }

    for (i = 0; i < count; i ++) {
        box_pick_mesh_t *pm = ecs_map_get_deref(&st->meshes, box_pick_mesh_t, drop[i]);
        ecs_map_remove(&st->meshes, drop[i]);
        box_mesh_cache_unhold(cache, pm->handle);
        box_bvh_fini(&pm->tree);
        ecs_os_free(pm);
    }
}

void box_pick_meshes_fini(
    box_pick_state_t *st)
{
    ecs_map_iter_t it = ecs_map_iter(&st->meshes);
    while (ecs_map_next(&it)) {
        box_pick_mesh_t *pm = ecs_map_ptr(&it);
        box_bvh_fini(&pm->tree);
        ecs_os_free(pm);
    }

    ecs_map_fini(&st->meshes);
}

/** Keep a hit of the ray being cast as a candidate.
 *
 * @param cast The cast
 * @param e The entity
 * @param t Distance of the hit
 * @param ui Whether the entity is a UI element
 */
static void box_pick_candidate(
    box_pick_cast_t *cast,
    ecs_entity_t e,
    float t,
    bool ui)
{
    box_pick_candidate_t *c = ecs_vec_append_t(
        NULL, &cast->st->candidates, box_pick_candidate_t);
    c->entity = e;
    c->distance = t;
    c->ui = ui;
}

/** Narrow phase of the spatial ray: the area of a UI element or the triangles of a shape.
 *
 * A hit on a UI element or a 2D shape outside the area of a ClipChildren ancestor is no hit, as
 * it is not drawn there, and an element with PickThrough is never hit.
 *
 * The distance it reports is moved out by the tie margin, so the walk still reaches every hit
 * that ties with the nearest one, which the selection weighs.
 *
 * @param ctx The cast
 * @param e The entity whose bounds the ray hit
 * @param distance Receives the distance of the hit plus the tie margin
 * @return Whether the ray hits the entity
 */
static bool box_pick_filter(
    void *ctx,
    ecs_entity_t e,
    float *distance)
{
    box_pick_cast_t *cast = ctx;
    const box_pick_ray_t *ray = cast->ray;
    vec2 size, local;
    bool circle = false;
    float t = 0;
    bool ui = box_pick_ui_size(cast->world, e, size, &circle);
    bool result;
    if (ecs_has_id(cast->world, e, PickThrough)) {
        result = false;
    } else if (ui && !ecs_has(cast->world, e, ShapeMesh) &&
        ecs_has_id(cast->world, e, EcsDisabled))
    {
        result = false;
    } else if (ui) {
        mat4 m;
        box_pick_transform(cast->world, e, m);
        result = box_pick_plane(m, size, circle, ray->origin, ray->dir, &t, local);
    } else {
        result = box_pick_mesh(cast->world, cast->st, e, ray->origin, ray->dir, &t, local);
    }

    result = result && (ray->max_distance <= 0 || t <= ray->max_distance);
    if (result && (ui || ecs_has(cast->world, e, Line))) {
        vec3 point;
        glm_vec3_scale(ECS_CONST_CAST(float*, ray->dir), t, point);
        glm_vec3_add(point, ECS_CONST_CAST(float*, ray->origin), point);
        result = box_render_clip_contains(cast->world, e, point);
    }

    if (result) {
        box_pick_candidate(cast, e, t, ui);
        *distance = t + BOX_PICK_TIE * (t > 1.0f ? t : 1.0f);
    }

    return result;
}

/** Collect the path of an entity from its root down.
 *
 * @param world The world
 * @param e The entity
 * @param path Receives the path, root first
 * @return Length of the path
 */
static int32_t box_pick_path(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t *path)
{
    ecs_entity_t up[BOX_PICK_CHAIN_MAX];
    int32_t i, count = 0;
    for (; e && count < BOX_PICK_CHAIN_MAX; e = ecs_get_parent(world, e)) {
        up[count ++] = e;
    }

    for (i = 0; i < count; i ++) {
        path[i] = up[count - 1 - i];
    }

    return count;
}

/** Get the z of the Position of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The z, 0 without a Position
 */
static float box_pick_z(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const Position *p = ecs_get(world, e, Position);

    return p ? p->z : 0;
}

/** Check whether one sibling comes after another in the OrderedChildren order of a parent.
 *
 * @param world The world
 * @param parent The parent
 * @param a The first sibling
 * @param b The second sibling
 * @return Whether a comes after b
 */
static bool box_pick_ordered_after(
    const ecs_world_t *world,
    ecs_entity_t parent,
    ecs_entity_t a,
    ecs_entity_t b)
{
    bool result = false, found = false;
    ecs_iter_t it = ecs_children(world, parent);
    while (!found && ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count && !found; i ++) {
            found = it.entities[i] == a || it.entities[i] == b;
            result = it.entities[i] == b;
        }
    }

    if (found) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Check whether an element or one of its ancestors floats (LayoutFloat).
 *
 * @param world The world
 * @param path The element and its ancestors, root first
 * @param count Length of the path
 * @return Whether it floats
 */
static bool box_pick_floating(
    const ecs_world_t *world,
    const ecs_entity_t *path,
    int32_t count)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < count && !result; i ++) {
        result = ecs_has(world, path[i], LayoutFloat);
    }

    return result;
}

bool box_pick_drawn_after(
    const ecs_world_t *world,
    ecs_entity_t a,
    ecs_entity_t b)
{
    ecs_entity_t pa[BOX_PICK_CHAIN_MAX], pb[BOX_PICK_CHAIN_MAX];
    int32_t na = box_pick_path(world, a, pa), nb = box_pick_path(world, b, pb);
    int32_t i = 0;
    bool result;
    bool fa = box_pick_floating(world, pa, na), fb = box_pick_floating(world, pb, nb);
    while (i < na && i < nb && pa[i] == pb[i]) {
        i ++;
    }

    if (fa != fb) {
        result = fa;
    } else if (i == na || i == nb) {
        result = na > nb;
    } else {
        float za = box_pick_z(world, pa[i]), zb = box_pick_z(world, pb[i]);
        ecs_entity_t parent = i ? pa[i - 1] : 0;
        if (za != zb) {
            result = za > zb;
        } else if (parent && ecs_has_id(world, parent, EcsOrderedChildren)) {
            result = box_pick_ordered_after(world, parent, pa[i], pb[i]);
        } else {
            result = pa[i] > pb[i];
        }
    }

    return result;
}

/** Choose the hit of a cast among its candidates.
 *
 * The nearest wins. Of the hits as near as it, UI elements win over shapes and the element
 * drawn last wins over the others.
 *
 * @param world The world
 * @param st The module state with the candidates
 * @param out Receives the chosen candidate
 * @return Whether there was a candidate
 */
static bool box_pick_select(
    const ecs_world_t *world,
    box_pick_state_t *st,
    box_pick_candidate_t *out)
{
    const box_pick_candidate_t *c = ecs_vec_first_t(&st->candidates, box_pick_candidate_t);
    int32_t i, count = ecs_vec_count(&st->candidates);
    float best = INFINITY;
    bool result = false;
    for (i = 0; i < count; i ++) {
        best = c[i].distance < best ? c[i].distance : best;
    }

    float tie = best + BOX_PICK_TIE * (best > 1.0f ? best : 1.0f);
    for (i = 0; i < count; i ++) {
        bool take = false;
        if (c[i].distance > tie) {
            continue;
        }

        if (!result) {
            take = true;
        } else if (c[i].ui != out->ui) {
            take = c[i].ui;
        } else if (c[i].ui) {
            take = box_pick_drawn_after(world, c[i].entity, out->entity);
        } else {
            take = c[i].distance < out->distance;
        }

        if (take) {
            *out = c[i];
            result = true;
        }
    }

    return result;
}

/** Find the entity a ray hits first in its scene.
 *
 * @param world The world
 * @param st The module state
 * @param ray The ray
 * @param out Receives the hit
 * @return Whether the ray hit an entity
 */
static bool box_pick_cast(
    const ecs_world_t *world,
    box_pick_state_t *st,
    const box_pick_ray_t *ray,
    box_pick_candidate_t *out)
{
    box_pick_cast_t cast = { .world = world, .st = st, .ray = ray };
    box_spatial_hit_t hit;
    box_spatial_ray_t desc = {
        .scene = ray->scene,
        .max_distance = ray->max_distance,
        .filter = box_pick_filter,
        .ctx = &cast,
        .ui = true
    };
    glm_vec3_copy(ECS_CONST_CAST(float*, ray->origin), desc.origin);
    glm_vec3_copy(ECS_CONST_CAST(float*, ray->dir), desc.dir);
    ecs_vec_clear(&st->candidates);
    box_spatial_ray(world, &desc, &hit);

    return box_pick_select(world, st, out);
}

/** Build the ray of a cursor in a view.
 *
 * @param world The world
 * @param view The view
 * @param s The surface the view renders to
 * @param cursor Cursor in points of the surface, origin at the top left
 * @param ray Receives the ray
 * @return false when the view has no camera
 */
static bool box_pick_view_ray(
    const ecs_world_t *world,
    ecs_entity_t view,
    const box_pick_surface_t *s,
    const vec2 cursor,
    box_pick_ray_t *ray)
{
    const View *v = ecs_get(world, view, View);
    ecs_entity_t camera = v ? v->camera : 0;
    const Camera *c = camera && ecs_is_alive(world, camera) ? ecs_get(world, camera, Camera) : 0;
    mat4 transform, inverse, proj, vp;
    vec4 near_p, mid_p, far_p;
    bool result = false;
    if (!c || s->width <= 0 || s->height <= 0) {
        goto done;
    }

    box_pick_transform(world, camera, transform);
    glm_mat4_inv(transform, inverse);
    box_camera_projection(c, s->width, s->height, proj);
    glm_mat4_mul(proj, inverse, vp);
    glm_mat4_inv(vp, vp);
    float x = 2.0f * cursor[0] / s->width - 1.0f;
    float y = 1.0f - 2.0f * cursor[1] / s->height;
    glm_mat4_mulv(vp, (vec4){ x, y, 1.0f, 1.0f }, near_p);
    glm_mat4_mulv(vp, (vec4){ x, y, 0.5f, 1.0f }, mid_p);
    glm_mat4_mulv(vp, (vec4){ x, y, 0.0f, 1.0f }, far_p);
    if (fabsf(near_p[3]) < 1e-12f || fabsf(mid_p[3]) < 1e-12f) {
        goto done;
    }

    glm_vec3_scale(near_p, 1.0f / near_p[3], ray->origin);
    glm_vec3_scale(mid_p, 1.0f / mid_p[3], ray->dir);
    glm_vec3_sub(ray->dir, ray->origin, ray->dir);
    glm_vec3_normalize(ray->dir);
    ray->max_distance = 0;
    if (fabsf(far_p[3]) > 1e-12f) {
        vec3 far_point;
        glm_vec3_scale(far_p, 1.0f / far_p[3], far_point);
        ray->max_distance = glm_vec3_distance(far_point, ray->origin);
    }

    ray->scene = box_render_scene_of(world, camera);
    result = glm_vec3_norm(ray->dir) > 0;
done:
    return result;
}

/** Find the surface that renders into a texture entity.
 *
 * @param world The world
 * @param st The module state
 * @param texture The texture entity
 * @param out Receives the surface
 * @return Whether a surface renders into the texture
 */
static bool box_pick_texture_surface(
    const ecs_world_t *world,
    box_pick_state_t *st,
    ecs_entity_t texture,
    box_pick_surface_t *out)
{
    bool result = false;
    ecs_iter_t it = ecs_query_iter(world, st->surfaces);
    while (!result && ecs_query_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        int32_t i;
        for (i = 0; i < it.count && !result; i ++) {
            if (texture && s[i].texture == texture) {
                out->view_root = s[i].view_root;
                out->width = (float)s[i].width;
                out->height = (float)s[i].height;
                result = true;
            }
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Wrap a texture coordinate the way the sampler of a material does.
 *
 * @param value The coordinate
 * @param wrap The wrap mode
 * @return The wrapped coordinate in 0..1
 */
static float box_pick_wrap(
    float value,
    TextureWrap wrap)
{
    float result = value;
    if (wrap == WrapClamp) {
        result = glm_clamp(value, 0.0f, 1.0f);
    } else if (wrap == WrapMirror) {
        result = value - 2.0f * floorf(value * 0.5f);
        result = result > 1.0f ? 2.0f - result : result;
    } else {
        result = value - floorf(value);
    }

    return result;
}

/** Map a hit on an entity that shows the texture of a surface to a cursor on that surface.
 *
 * An Image maps the area of the element to its texture area, PbrTextures map the UV of the
 * mesh through TextureTransform and TextureWrap.
 *
 * @param world The world
 * @param st The module state
 * @param e The entity
 * @param ray The ray that hit it
 * @param cursor Receives the cursor in points of the surface
 * @param out Receives the surface
 * @return Whether the entity shows the texture of a surface where the ray hit it
 */
static bool box_pick_texture(
    const ecs_world_t *world,
    box_pick_state_t *st,
    ecs_entity_t e,
    const box_pick_ray_t *ray,
    vec2 cursor,
    box_pick_surface_t *out)
{
    const Image *img = ecs_get(world, e, Image);
    const PbrTextures *pt = img ? NULL : ecs_get(world, e, PbrTextures);
    vec2 uv = {0}, size, local;
    bool circle = false, result = false;
    float t = 0;
    ecs_os_zeromem(out);
    if (img && box_pick_texture_surface(world, st, img->texture, out)) {
        mat4 m;
        box_pick_transform(world, e, m);
        if (box_pick_ui_size(world, e, size, &circle) && size[0] > 0 && size[1] > 0 &&
            box_pick_plane(m, size, circle, ray->origin, ray->dir, &t, local))
        {
            float u = local[0] / size[0] + 0.5f, v = 0.5f - local[1] / size[1];
            uv[0] = img->u0 + u * (img->u1 - img->u0);
            uv[1] = img->v0 + v * (img->v1 - img->v0);
            result = true;
        }
    } else if (pt && (box_pick_texture_surface(world, st, pt->albedo, out) ||
        box_pick_texture_surface(world, st, pt->emissive, out)))
    {
        const TextureTransform *tt = ecs_get(world, e, TextureTransform);
        const TextureWrap *wrap = ecs_get(world, e, TextureWrap);
        result = box_pick_mesh(world, st, e, ray->origin, ray->dir, &t, uv);
        if (tt) {
            uv[0] = uv[0] * tt->scale_x + tt->offset_x;
            uv[1] = uv[1] * tt->scale_y + tt->offset_y;
        }

        uv[0] = box_pick_wrap(uv[0], wrap ? *wrap : WrapRepeat);
        uv[1] = box_pick_wrap(uv[1], wrap ? *wrap : WrapRepeat);
    }

    cursor[0] = uv[0] * out->width;
    cursor[1] = uv[1] * out->height;

    return result;
}

/** Find the first entity with a tag on the path of a hit: itself or an ancestor.
 *
 * @param world The world
 * @param e The hit
 * @param tag Interactive or InputBlocker
 * @return The entity, 0 when there is none
 */
static ecs_entity_t box_pick_path_find(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t tag)
{
    int32_t depth = 0;
    while (e && depth < BOX_PICK_CHAIN_MAX && !ecs_has_id(world, e, tag)) {
        e = ecs_get_parent(world, e);
        depth ++;
    }

    return depth < BOX_PICK_CHAIN_MAX ? e : 0;
}

/** Find the entity that handles the events of a hit: the first Interactive entity on its path,
 * else the first InputBlocker, which keeps them from passing to the views rendered before.
 *
 * @param world The world
 * @param e The hit
 * @return The handler, 0 when there is none
 */
static ecs_entity_t box_pick_handler(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = box_pick_path_find(world, e, Interactive);
    if (!result) {
        result = box_pick_path_find(world, e, InputBlocker);
    }

    return result;
}

/** Order views by descending id, the reverse of their render order.
 *
 * @param a The first view
 * @param b The second view
 * @return Negative when a renders after b
 */
static int box_pick_view_compare(
    const void *a,
    const void *b)
{
    ecs_entity_t x = *(const ecs_entity_t*)a;
    ecs_entity_t y = *(const ecs_entity_t*)b;

    return (x < y) - (x > y);
}

/** Collect the views of a surface, the last rendered first.
 *
 * @param world The world
 * @param st The module state
 * @param s The surface
 * @param out Receives the views
 * @return Number of views
 */
static int32_t box_pick_views(
    const ecs_world_t *world,
    box_pick_state_t *st,
    const box_pick_surface_t *s,
    ecs_entity_t *out)
{
    ecs_entity_t roots[BOX_PICK_VIEW_MAX];
    int32_t i, k, root_count = 0, count = 0;
    ecs_iter_t it = ecs_query_iter(world, st->surfaces);
    while (ecs_query_next(&it)) {
        const Surface *surface = ecs_field(&it, Surface, 0);
        for (i = 0; i < it.count; i ++) {
            if (surface[i].view_root && root_count < BOX_PICK_VIEW_MAX) {
                roots[root_count ++] = surface[i].view_root;
            }
        }
    }

    it = ecs_query_iter(world, st->views);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count && count < BOX_PICK_VIEW_MAX; i ++) {
            ecs_entity_t parent = ecs_get_parent(world, it.entities[i]);
            bool claimed = false;
            for (k = 0; k < root_count && !s->view_root; k ++) {
                claimed |= parent && parent == roots[k];
            }

            if (s->view_root ? parent == s->view_root : !claimed) {
                out[count ++] = it.entities[i];
            }
        }
    }

    qsort(out, (size_t)count, sizeof(ecs_entity_t), box_pick_view_compare);

    return count;
}

/** Pick through the views of a surface, the last rendered first.
 *
 * @param world The world
 * @param st The module state
 * @param s The surface
 * @param cursor Cursor in points of the surface, origin at the top left
 * @param depth Render-to-texture nesting of the surface
 * @param out Receives what the pick found
 * @return Whether a view handles the events at the cursor
 */
static bool box_pick_surface(
    const ecs_world_t *world,
    box_pick_state_t *st,
    const box_pick_surface_t *s,
    const vec2 cursor,
    int32_t depth,
    box_pick_t *out);

/** Pick in one view: its nearest hit, then the surface its texture shows, then its handler.
 *
 * @param world The world
 * @param st The module state
 * @param view The view
 * @param s The surface of the view
 * @param cursor Cursor in points of the surface
 * @param depth Render-to-texture nesting of the surface
 * @param out Receives what the pick found
 * @return Whether the view handles the events at the cursor
 */
static bool box_pick_view(
    const ecs_world_t *world,
    box_pick_state_t *st,
    ecs_entity_t view,
    const box_pick_surface_t *s,
    const vec2 cursor,
    int32_t depth,
    box_pick_t *out)
{
    box_pick_ray_t ray;
    box_pick_candidate_t hit;
    box_pick_surface_t inner;
    vec2 inner_cursor;
    vec3 point;
    bool result = false;
    if (!box_pick_view_ray(world, view, s, cursor, &ray) || !box_pick_cast(world, st, &ray, &hit)) {
        goto done;
    }

    glm_vec3_scale(ray.dir, hit.distance, point);
    glm_vec3_add(point, ray.origin, point);
    if (!out->hit) {
        out->hit = hit.entity;
        out->hit_view = view;
        out->hit_distance = hit.distance;
        glm_vec3_copy(point, out->hit_point);
    }

    if (depth < BOX_PICK_DEPTH_MAX &&
        box_pick_texture(world, st, hit.entity, &ray, inner_cursor, &inner) &&
        box_pick_surface(world, st, &inner, inner_cursor, depth + 1, out))
    {
        result = true;
        goto done;
    }

    ecs_entity_t handler = box_pick_handler(world, hit.entity);
    if (handler) {
        out->target = hit.entity;
        out->handler = handler;
        out->view = view;
        out->surface = *s;
        out->surface_cursor[0] = cursor[0];
        out->surface_cursor[1] = cursor[1];
        out->distance = hit.distance;
        glm_vec3_copy(point, out->point);
        result = true;
    }

done:
    return result;
}

/** Pick through the views of a surface, the last rendered first.
 *
 * @param world The world
 * @param st The module state
 * @param s The surface
 * @param cursor Cursor in points of the surface, origin at the top left
 * @param depth Render-to-texture nesting of the surface
 * @param out Receives what the pick found
 * @return Whether a view handles the events at the cursor
 */
static bool box_pick_surface(
    const ecs_world_t *world,
    box_pick_state_t *st,
    const box_pick_surface_t *s,
    const vec2 cursor,
    int32_t depth,
    box_pick_t *out)
{
    ecs_entity_t views[BOX_PICK_VIEW_MAX];
    bool result = false;
    int32_t i, count;
    if (cursor[0] < 0 || cursor[1] < 0 || cursor[0] > s->width || cursor[1] > s->height) {
        goto done;
    }

    count = box_pick_views(world, st, s, views);
    for (i = 0; i < count && !result; i ++) {
        result = box_pick_view(world, st, views[i], s, cursor, depth, out);
    }

done:
    return result;
}

/** Find the surface the window cursor is on: Input.surface, the surface with the input focus,
 * else the first surface without a texture.
 *
 * Its size is the window size of Input once known, else the size the surface requests, else
 * the default surface size.
 *
 * @param world The world
 * @param st The module state
 * @param out Receives the surface
 */
static void box_pick_input_surface(
    const ecs_world_t *world,
    box_pick_state_t *st,
    box_pick_surface_t *out)
{
    const Input *in = ecs_singleton_get(world, Input);
    const Surface *focused = in && in->surface && ecs_is_alive(world, in->surface) ?
        ecs_get(world, in->surface, Surface) : NULL;
    ecs_entity_t first = 0;
    ecs_iter_t it;
    int32_t i;
    out->view_root = 0;
    out->width = BOX_SURFACE_WIDTH;
    out->height = BOX_SURFACE_HEIGHT;
    if (focused && !focused->texture) {
        out->view_root = focused->view_root;
        out->width = (float)focused->width;
        out->height = (float)focused->height;
        goto size;
    }

    it = ecs_query_iter(world, st->surfaces);
    while (ecs_query_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (i = 0; i < it.count; i ++) {
            if (!s[i].texture && (!first || it.entities[i] < first)) {
                first = it.entities[i];
                out->view_root = s[i].view_root;
                out->width = (float)s[i].width;
                out->height = (float)s[i].height;
            }
        }
    }

size:
    if (in && in->size[0] > 0 && in->size[1] > 0) {
        out->width = in->size[0];
        out->height = in->size[1];
    }
}

void box_pick_at(
    ecs_world_t *world,
    box_pick_state_t *st,
    const vec2 cursor,
    box_pick_t *out)
{
    box_pick_surface_t s;
    ecs_os_zeromem(out);
    box_pick_input_surface(world, st, &s);
    box_pick_surface(world, st, &s, cursor, 0, out);
}

int32_t box_pick_input_views(
    ecs_world_t *world,
    box_pick_state_t *st,
    ecs_entity_t *out)
{
    box_pick_surface_t s;
    box_pick_input_surface(world, st, &s);

    return box_pick_views(world, st, &s, out);
}

/** Find where the ray of a cursor meets the plane of an entity.
 *
 * @param world The world
 * @param e The entity, whose local z = 0 plane is used
 * @param ray The ray
 * @param point Receives the position in the scene
 * @param distance Receives the distance along the ray
 * @return Whether the ray meets the plane
 */
static bool box_pick_capture_plane(
    const ecs_world_t *world,
    ecs_entity_t e,
    const box_pick_ray_t *ray,
    vec3 point,
    float *distance)
{
    mat4 m, inv;
    vec3 o, d;
    bool result = false;
    box_pick_transform(world, e, m);
    glm_mat4_inv(m, inv);
    glm_mat4_mulv3(inv, ECS_CONST_CAST(float*, ray->origin), 1.0f, o);
    glm_mat4_mulv3(inv, ECS_CONST_CAST(float*, ray->dir), 0.0f, d);
    if (fabsf(d[2]) > 1e-8f) {
        *distance = -o[2] / d[2];
        glm_vec3_scale(ECS_CONST_CAST(float*, ray->dir), *distance, point);
        glm_vec3_add(point, ECS_CONST_CAST(float*, ray->origin), point);
        result = true;
    }

    return result;
}

void box_pick_resolve(
    ecs_world_t *world,
    box_pick_state_t *st,
    const vec2 cursor,
    box_pick_t *out)
{
    box_pick_capture_t *cap = &st->capture;
    box_pick_ray_t ray;
    vec2 surface_cursor;
    box_pick_at(world, st, cursor, out);
    if (!cap->target) {
        goto done;
    }

    if (!ecs_is_alive(world, cap->target) || !ecs_is_alive(world, cap->view)) {
        ecs_os_zeromem(cap);
        goto done;
    }

    out->target = cap->target;
    out->handler = cap->handler;
    out->view = cap->view;
    out->surface = cap->surface;
    glm_vec3_copy(cap->point, out->point);
    surface_cursor[0] = cursor[0] + cap->offset[0];
    surface_cursor[1] = cursor[1] + cap->offset[1];
    if (box_pick_view_ray(world, cap->view, &cap->surface, surface_cursor, &ray)) {
        box_pick_capture_plane(world, cap->target, &ray, out->point, &out->distance);
    }

done:
    return;
}

void box_pick_queries_init(
    ecs_world_t *world,
    box_pick_state_t *st)
{
    st->views = ecs_query(world, {
        .terms = {{ .id = ecs_id(View), .inout = EcsInOutNone }},
        .cache_kind = EcsQueryCacheAuto
    });

    st->surfaces = ecs_query(world, {
        .terms = {{ .id = ecs_id(Surface), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });
}
