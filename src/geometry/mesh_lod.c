/**
 * @file geometry/mesh_lod.c
 * @brief Reduces the tessellation of a parametric shape to its projected size.
 *
 * A shape that is drawn with a segment count gets a ladder of levels, from the
 * authored count down to the floor of its family. Every level is an ordinary
 * entry of the mesh cache, so the instances that settle on one level share a
 * mesh and a level change is a handle swap instead of a rebuild. The selection
 * runs once per frame at the head of the extract, over the views of that frame
 * and with the radius of the instance in world units, and takes the finest
 * level any view asks for, because one handle serves them all.
 */

#include "geometry_internal.h"
#include "../render/render_internal.h"

/** Part of a full circle the quarter arc of a rounded box corner covers */
#define BOX_MESH_LOD_QUARTER (0.25f)

/** Segments a rounded box corner never goes below */
#define BOX_MESH_LOD_CORNER_MIN (1)

/** Radians in a full circle, the sweep an unreduced tube covers */
#define BOX_MESH_LOD_CIRCLE (6.28318530718f)

/** The level ladder of one shape, written when its component is set.
 *
 * The key is the authored one, which the level only changes the segment counts
 * of, so the ladder never leaves the family of the shape and the authored
 * count stays its upper bound.
 */
typedef struct MeshLod {
    box_mesh_key_t key;      /**< Mesh key of the authored level */
    box_lod_ladder_t ladder; /**< Segment counts the shape reduces through */
    int32_t rings;           /**< Authored subdivisions around a tube, 0 without any */
    float radius;            /**< Radius of the arc in the local space of the mesh */
    float span;              /**< Part of a full circle the segments cover */
    bool axial;              /**< The arc lies in the XZ plane, so Y never scales it */
    int32_t level;           /**< Level the previous frame settled on */
} MeshLod;

/** One view the reduction measures against */
typedef struct box_mesh_lod_view_t {
    box_lod_camera_t camera; /**< Camera of the view */
    float viewport_px;       /**< Height of what the view renders into, in pixels */
    float threshold;         /**< Projected error in pixels a level may not exceed */
    float hysteresis;        /**< Band around the threshold a switch has to cross */
} box_mesh_lod_view_t;

/** Working state of the reduction, stored as a singleton.
 *
 * The query is owned by an entity of its own, which is what frees it when the
 * world goes away: a component destructor runs after the entities of the world
 * are deleted, too late to still touch a query.
 */
typedef struct MeshLodState {
    ecs_query_t *query; /**< Shapes that carry a ladder, owned by its entity */
    ecs_vec_t views;    /**< box_mesh_lod_view_t of the views of this frame */
} MeshLodState;

ECS_COMPONENT_DECLARE(MeshLod);
ECS_COMPONENT_DECLARE(MeshLodState);

/** Working state destructor.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_mesh_lod_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    MeshLodState *state = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &state[i].views, box_mesh_lod_view_t);
    }
}

/** Corner radius a rounded box is built with, which its size clamps.
 *
 * The generator never rounds a corner by more than half of the smallest
 * dimension of the box, so a larger authored radius would measure an arc that
 * is not there.
 *
 * @param key The mesh key of the rounded box
 * @return The radius of its corner arcs
 */
static float box_mesh_lod_corner(
    const box_mesh_key_t *key)
{
    float smallest = key->f0 < key->f1 ? key->f0 : key->f1;
    float result = key->f3;
    smallest = smallest < key->f2 ? smallest : key->f2;
    if (result > smallest * 0.5f) {
        result = smallest * 0.5f;
    }

    return result;
}

/** Fill the ladder of a shape family from its key.
 *
 * A family the reduction does not know, or one whose authored count is already
 * at the floor, leaves the ladder with a single level and never reduces.
 *
 * @param key The mesh key of the authored shape
 * @param out Receives the ladder, the radius and the span
 * @return True when the shape has more than one level
 */
static bool box_mesh_lod_ladder(
    const box_mesh_key_t *key,
    MeshLod *out)
{
    int32_t min_segments = BOX_LOD_MIN_SEGMENTS;
    bool round = true;
    out->key = *key;
    out->radius = 1.0f;
    out->span = 1.0f;
    out->rings = 0;
    out->axial = false;
    out->level = 0;

    switch (key->kind) {
    case BoxShapeSphere:
    case BoxShapeHemiSphere:
    case BoxShapeCapsule:
        break;
    case BoxShapeCone:
    case BoxShapeCylinder:
    case BoxShapeFrustum:
        out->axial = true;
        break;
    case BoxShapeTorus:
        out->rings = key->i1;
        out->axial = true;
        break;
    case BoxShapeTube:
        out->span = key->f1 > 0.0f && key->f1 < BOX_MESH_LOD_CIRCLE ?
            key->f1 / BOX_MESH_LOD_CIRCLE : 1.0f;
        out->axial = true;
        break;
    case BoxShapeRoundedBox:
        out->radius = box_mesh_lod_corner(key);
        out->span = BOX_MESH_LOD_QUARTER;
        min_segments = BOX_MESH_LOD_CORNER_MIN;
        break;
    default:
        round = false;
        break;
    }

    if (!round || out->radius <= 0.0f) {
        goto done;
    }

    box_lod_ladder(key->i0, min_segments, &out->ladder);
done:
    return out->ladder.count > 1;
}

void box_mesh_lod_set(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t e,
    const box_mesh_key_t *key)
{
    MeshLod lod = {0};
    if (box_mesh_lod_ladder(key, &lod)) {
        ecs_set_ptr(stage, e, MeshLod, &lod);
        goto done;
    }

    if (ecs_has(world, e, MeshLod)) {
        ecs_remove(stage, e, MeshLod);
    }

done:
    return;
}

/** Read the camera and the thresholds of one view of the frame.
 *
 * A view without LOD settings, and a view whose settings are off, gets a
 * threshold of zero, which holds every shape it draws at its authored level.
 *
 * @param world The world
 * @param camera The camera the view renders
 * @param viewport_px Height of the surface in pixels
 * @param out Receives the view
 * @return True when the view has a camera and takes part in the reduction
 */
static bool box_mesh_lod_view(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t camera,
    float viewport_px,
    box_mesh_lod_view_t *out)
{
    const Camera *c = camera ? ecs_get(world, camera, Camera) : NULL;
    const WorldTransform *transform = camera ? ecs_get(world, camera, WorldTransform) : NULL;
    memset(out, 0, sizeof(box_mesh_lod_view_t));
    if (!c || !transform) {
        goto done;
    }

    out->camera.position = mat4_get_translation(&transform->m);
    out->camera.fov = c->fov;
    out->camera.ortho_height = c->ortho_height;
    out->camera.orthographic = c->orthographic;
    out->viewport_px = viewport_px;

    const View *view = e ? ecs_get(world, e, View) : NULL;
    const RenderTarget *target = view && view->target ?
        ecs_get(world, view->target, RenderTarget) : NULL;
    if (target && target->height > 0) {
        out->viewport_px = (float)target->height;
    }

    const LodSettings *s = box_render_view_setting(world, e, ecs_id(LodSettings));
    if (!s || !s->enabled || out->viewport_px <= 0.0f) {
        goto done;
    }

    float quality = s->quality > 0.0f ? s->quality : BOX_VIEW_LOD_QUALITY;
    float error = s->error_px > 0.0f ? s->error_px : BOX_VIEW_LOD_ERROR;
    out->threshold = error / quality;
    out->hysteresis = s->hysteresis > 0.0f ? s->hysteresis : BOX_VIEW_LOD_HYSTERESIS;

done:
    return c != NULL && transform != NULL;
}

/** Collect the views the reduction of this frame measures against.
 *
 * @param world The world
 * @param state The working state, whose view list is refilled
 * @param viewport_px Height of the surface in pixels
 * @return The number of views, 0 when no view asks for a reduction
 */
static int32_t box_mesh_lod_views(
    ecs_world_t *world,
    MeshLodState *state,
    float viewport_px)
{
    RenderState *render = box_render_state(world);
    box_mesh_lod_view_t view;
    bool enabled = false;
    int32_t count = ecs_vec_count(&render->view_list);
    int32_t i;

    ecs_vec_clear(&state->views);
    for (i = 0; i < count; i ++) {
        const box_view_src_t *src = ecs_vec_get_t(&render->view_list, box_view_src_t, i);
        if (!box_mesh_lod_view(world, src->entity, src->camera, viewport_px, &view)) {
            continue;
        }

        enabled = enabled || view.threshold > 0.0f;
        ecs_vec_append_t(NULL, &state->views, box_mesh_lod_view_t)[0] = view;
    }

    return enabled ? ecs_vec_count(&state->views) : 0;
}

/** Largest factor an instance matrix scales the radius of an arc by.
 *
 * The length of a column is what the matrix scales that local axis by, so the
 * longest column of the axes the arc sweeps is the factor its radius grows by.
 * An arc in the XZ plane skips the Y column, because the height of a cylinder
 * never changes how round its circle is.
 *
 * @param m The instance matrix, the world transform times the shape scale
 * @param axial The arc lies in the XZ plane of the mesh
 * @return The factor the radius of the arc is scaled by
 */
static float box_mesh_lod_scale(
    const mat4 *m,
    bool axial)
{
    float result = 0.0f;
    int32_t axis;
    for (axis = 0; axis < 3; axis ++) {
        if (axial && axis == 1) {
            continue;
        }

        vec3 column = vec3_new(m->v[axis * 4], m->v[axis * 4 + 1], m->v[axis * 4 + 2]);
        float length = vec3_length(column);

        result = length > result ? length : result;
    }

    return result;
}

/** Pick the level of one instance, the finest any view of the frame asks for.
 *
 * @param lod The ladder of the shape and the level of the previous frame
 * @param mesh The render mesh of the instance
 * @param transform The world transform of the instance
 * @param views The views of the frame
 * @param count Number of views
 * @return The level of this frame
 */
static int32_t box_mesh_lod_level(
    const MeshLod *lod,
    const RenderMesh *mesh,
    const WorldTransform *transform,
    const box_mesh_lod_view_t *views,
    int32_t count)
{
    mat4 scale = mat4_scaling(mesh->scale);
    mat4 m = mat4_mul(&transform->m, &scale);
    float sphere[4];
    box_render_instance_bounds(mesh, &m, sphere);

    vec3 center = vec3_new(sphere[0], sphere[1], sphere[2]);
    vec3 extent = vec3_new(sphere[3], sphere[3], sphere[3]);
    box_lod_reduce_desc_t desc = {
        .ladder = &lod->ladder, .radius = lod->radius * box_mesh_lod_scale(&m, lod->axial),
        .span = lod->span, .current = lod->level
    };

    desc.bounds.min = vec3_sub(center, extent);
    desc.bounds.max = vec3_add(center, extent);

    int32_t result = lod->ladder.count - 1;
    int32_t i;
    for (i = 0; i < count && result > 0; i ++) {
        desc.camera = views[i].camera;
        desc.viewport_px = views[i].viewport_px;
        desc.threshold = views[i].threshold;
        desc.hysteresis = views[i].hysteresis;

        int32_t level = box_lod_reduce(&desc);
        result = level < result ? level : result;
    }

    return result;
}

/** Give an instance the mesh of its level, generating that mesh on first use.
 *
 * @param world The world
 * @param lod The ladder and the level it settled on
 * @param mesh The render mesh of the instance
 */
static void box_mesh_lod_apply(
    ecs_world_t *world,
    const MeshLod *lod,
    RenderMesh *mesh)
{
    box_mesh_key_t key = lod->key;
    key.i0 = lod->ladder.segments[lod->level];
    if (lod->rings > 0) {
        int32_t rings = lod->rings >> lod->level;

        key.i1 = rings > BOX_LOD_MIN_SEGMENTS ? rings : BOX_LOD_MIN_SEGMENTS;
    }

    mesh->handle = box_mesh_cache_get(world, &key, &mesh->bounds_min, &mesh->bounds_max);
}

void box_mesh_lod_select(
    ecs_world_t *world,
    float viewport_px)
{
    ecs_entity_t id = ecs_lookup(world, "box.geometry.MeshLodState");
    if (!id) {
        goto done;
    }

    MeshLodState *state = ecs_get_mut_id(world, id, id);
    if (!state) {
        goto done;
    }

    int32_t count = box_mesh_lod_views(world, state, viewport_px);
    if (!count) {
        goto done;
    }

    const box_mesh_lod_view_t *views = ecs_vec_first_t(&state->views, box_mesh_lod_view_t);
    ecs_iter_t it = ecs_query_iter(world, state->query);
    while (ecs_query_next(&it)) {
        MeshLod *lod = ecs_field(&it, MeshLod, 0);
        RenderMesh *mesh = ecs_field(&it, RenderMesh, 1);
        const WorldTransform *transform = ecs_field(&it, WorldTransform, 2);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            int32_t level = box_mesh_lod_level(&lod[i], &mesh[i], &transform[i], views, count);
            if (level == lod[i].level) {
                continue;
            }

            lod[i].level = level;
            box_mesh_lod_apply(world, &lod[i], &mesh[i]);
            box_render_mark_dirty(world, it.entities[i]);
        }
    }

done:
    return;
}

void box_mesh_lod_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, MeshLod);
    ECS_COMPONENT_DEFINE(world, MeshLodState);
    ecs_set_hooks(world, MeshLod, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, MeshLodState, {
        .ctor = flecs_default_ctor,
        .dtor = box_mesh_lod_state_dtor
    });
    ecs_add_pair(world, ecs_id(MeshLod), EcsOnInstantiate, EcsDontInherit);

    MeshLodState *state = ecs_singleton_ensure(world, MeshLodState);
    ecs_vec_init_t(NULL, &state->views, box_mesh_lod_view_t, 0);
    state->query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "MeshLodQuery" }),
        .terms = {
            { .id = ecs_id(MeshLod), .inout = EcsInOut },
            { .id = ecs_id(RenderMesh), .inout = EcsInOut },
            { .id = ecs_id(WorldTransform), .inout = EcsIn }
        },
        .cache_kind = EcsQueryCacheAuto
    });
}
