/**
 * @file render/bake.c
 * @brief Irradiance probe volume of every scene with LightBake, sent to the backend on change.
 *
 * The grid covers the static bounds the spatial index holds, padded and spaced as LightBake
 * says; probes inside a static shape are invalid. Probes start as the SH of the hemisphere
 * ambient of the scene, so a view that samples them looks as it did with the hemisphere.
 */

#include "render.h"
#include "../spatial/index.h"
#include "../geometry/mesh_cache.h"
#include <math.h>

/** Probes of a volume at most; a larger grid is spaced wider until it fits */
#define BOX_BAKE_PROBES_MAX (1 << 18)

/** Fraction of the spacing an extent may exceed a whole number of spacings by, float noise */
#define BOX_BAKE_EPSILON (1e-4)

/** Grid of probes of a volume */
typedef struct box_bake_grid_t {
    float origin[3];        /**< World position of the first probe */
    float spacing;          /**< World units between neighbouring probes */
    int32_t size[3];        /**< Probes per axis, 0 for a volume without probes */
} box_bake_grid_t;

/** The volume of one scene with LightBake */
typedef struct box_bake_volume_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    ecs_entity_t entity;    /**< Entity with the LightBake of the scene */
    uint32_t handle;        /**< Volume handle, 0 while the grid has no probes */
    int32_t probes;         /**< Probes of the grid */
    box_bake_grid_t grid;   /**< The grid the backend has */
    float probe_spacing;    /**< LightBake.probe_spacing the grid was placed with */
    float bounds_padding;   /**< LightBake.bounds_padding the grid was placed with */
    bool placed;            /**< The grid was placed at least once */
    bool seen;              /**< A LightBake of the scene exists in this frame */
    bool sent;              /**< The backend has the volume with this grid */
    bool viewed;            /**< A view of the scene is in the packet of this frame */
    float sh[BOX_RENDER_SH_COEFFICIENTS][4]; /**< Irradiance the probes are filled with */
    float ambient[BOX_RENDER_SH_COEFFICIENTS][4]; /**< Irradiance of the views of this frame */
    ecs_vec_t valid;        /**< uint32_t, one bit per probe outside static geometry */
} box_bake_volume_t;

/** Main thread state of the bake extract */
typedef struct box_render_bake_t {
    ecs_query_t *bakes;     /**< Entities with LightBake and their scene */
    ecs_vec_t volumes;      /**< box_bake_volume_t */
    ecs_vec_t free_handles; /**< uint32_t, volume handles to reuse */
    uint32_t next_handle;   /**< Next volume handle */
    ecs_vec_t hits;         /**< ecs_entity_t, scratch of the overlap queries */
    int32_t indexed;        /**< Entities of the spatial index at the last extract */
    const void *backend;    /**< Context of the backend the volumes were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
} box_render_bake_t;

/** Find the volume of a scene.
 *
 * @param b The state
 * @param scene The scene root
 * @return The volume, NULL when the scene has none
 */
static box_bake_volume_t* box_bake_find(
    const box_render_bake_t *b,
    ecs_entity_t scene)
{
    box_bake_volume_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&b->volumes); i ++) {
        box_bake_volume_t *v = ecs_vec_get_t(&b->volumes, box_bake_volume_t, i);
        if (v->scene == scene) {
            result = v;
            break;
        }
    }

    return result;
}

/** Find the volume of a scene, adding one when it has none.
 *
 * @param b The state
 * @param scene The scene root
 * @return The volume, valid until the next volume is added
 */
static box_bake_volume_t* box_bake_ensure(
    box_render_bake_t *b,
    ecs_entity_t scene)
{
    box_bake_volume_t *result = box_bake_find(b, scene);
    if (!result) {
        result = ecs_vec_append_t(NULL, &b->volumes, box_bake_volume_t);
        ecs_os_memset_t(result, 0, box_bake_volume_t);
        result->scene = scene;
        ecs_vec_init_t(NULL, &result->valid, uint32_t, 0);
    }

    return result;
}

/** Compute the static bounds of a scene from the static items of the spatial index.
 *
 * @param world The world
 * @param scene The scene root
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 * @return false when the scene has no static shapes
 */
static bool box_bake_bounds(
    const ecs_world_t *world,
    ecs_entity_t scene,
    vec3 min,
    vec3 max)
{
    const SpatialIndex *si = ecs_id(SpatialIndex) ?
        ecs_singleton_get(world, SpatialIndex) : NULL;
    const box_spatial_scene_t *s = si && si->index ? box_spatial_scene(si->index, scene) : NULL;
    bool result = false;
    int32_t i, count = s ? ecs_vec_count(&s->items) : 0;
    const box_spatial_item_t *items = s ? ecs_vec_first(&s->items) : NULL;
    for (i = 0; i < count; i ++) {
        const box_spatial_item_t *it = &items[i];
        if (!it->entity || it->tree != BOX_SPATIAL_STATIC) {
            continue;
        }

        if (!result) {
            glm_vec3_copy(ECS_CONST_CAST(float*, it->min), min);
            glm_vec3_copy(ECS_CONST_CAST(float*, it->max), max);
            result = true;
        } else {
            glm_vec3_minv(min, ECS_CONST_CAST(float*, it->min), min);
            glm_vec3_maxv(max, ECS_CONST_CAST(float*, it->max), max);
        }
    }

    return result;
}

/** Count the probes along the axes of padded bounds at a spacing.
 *
 * @param extent Size of the padded bounds per axis
 * @param spacing World units between probes
 * @param size Receives the probes per axis
 * @return Probes of the grid
 */
static double box_bake_count(
    const double *extent,
    double spacing,
    int32_t *size)
{
    double result = 1;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        double n = ceil(extent[i] / spacing - BOX_BAKE_EPSILON) + 1.0;
        n = fmin(fmax(n, 1.0), (double)BOX_BAKE_PROBES_MAX + 1.0);
        size[i] = (int32_t)n;
        result *= n;
    }

    return result;
}

/** Place a grid over bounds: probes spacing apart, centered on the padded bounds.
 *
 * Probes per axis are ceil(extent / spacing) + 1, so the grid covers the padded bounds; a
 * grid of more than BOX_BAKE_PROBES_MAX probes is spaced wider until it fits.
 *
 * @param min Minimum corner of the static bounds
 * @param max Maximum corner of the static bounds
 * @param spacing World units between probes, no grid when not positive
 * @param padding World units added on every side of the bounds
 * @param out Receives the grid
 * @return Probes of the grid
 */
static int32_t box_bake_place(
    const vec3 min,
    const vec3 max,
    float spacing,
    float padding,
    box_bake_grid_t *out)
{
    double extent[3], center[3], step = spacing;
    int32_t i, result = 0;
    ecs_os_memset_t(out, 0, box_bake_grid_t);
    if (!(spacing > 0) || !isfinite(spacing) || !isfinite(padding)) {
        goto done;
    }

    for (i = 0; i < 3; i ++) {
        center[i] = ((double)min[i] + (double)max[i]) * 0.5;
        extent[i] = fmax((double)max[i] - (double)min[i] + 2.0 * (double)padding, 0.0);
    }

    double probes = box_bake_count(extent, step, out->size);
    while (probes > BOX_BAKE_PROBES_MAX) {
        step *= fmax(cbrt(probes / BOX_BAKE_PROBES_MAX), 1.01);
        probes = box_bake_count(extent, step, out->size);
    }

    out->spacing = (float)step;
    for (i = 0; i < 3; i ++) {
        out->origin[i] = (float)(center[i] - (double)(out->size[i] - 1) * step * 0.5);
    }

    result = (int32_t)probes;
done:
    return result;
}

/** Check whether a position lies inside a static shape that the bake captures.
 *
 * The test is against the oriented bounds of the unit mesh of the shape, scaled by its
 * instance scale and placed by its world transform; a position on the surface is outside.
 *
 * @param world The world
 * @param cache The mesh cache
 * @param e The shape
 * @param pos The position
 * @return Whether the position is inside
 */
static bool box_bake_inside(
    const ecs_world_t *world,
    const box_mesh_cache_t *cache,
    ecs_entity_t e,
    const vec3 pos)
{
    bool result = false;
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    const box_mesh_t *mesh = cache && shape && shape->mesh ?
        box_mesh_cache_get(cache, shape->mesh) : NULL;
    if (!mesh || ecs_has_id(world, e, DynamicTransform) ||
        ecs_has_id(world, e, ManualTransform) || ecs_has_id(world, e, NoBake))
    {
        goto done;
    }

    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    mat4 inv;
    vec4 local = { pos[0], pos[1], pos[2], 1.0f };
    if (t) {
        glm_mat4_inv(ECS_CONST_CAST(vec4*, t->m), inv);
        glm_mat4_mulv(inv, local, local);
    }

    int32_t i;
    result = true;
    for (i = 0; i < 3; i ++) {
        float a = mesh->min[i] * shape->scale[i];
        float b = mesh->max[i] * shape->scale[i];
        if (!(local[i] > glm_min(a, b) && local[i] < glm_max(a, b))) {
            result = false;
        }
    }

done:
    return result;
}

/** Mark the probes of a volume that lie inside static geometry as invalid.
 *
 * @param world The world
 * @param b The state, owns the scratch of the overlap queries
 * @param v The volume, with its grid placed
 */
static void box_bake_validate(
    const ecs_world_t *world,
    box_render_bake_t *b,
    box_bake_volume_t *v)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const box_bake_grid_t *g = &v->grid;
    int32_t words = (v->probes + 31) / 32;
    int32_t x, y, z, index = 0;
    ecs_vec_set_count_t(NULL, &v->valid, uint32_t, words);
    uint32_t *valid = ecs_vec_first(&v->valid);
    ecs_os_memset(valid, 0, words * ECS_SIZEOF(uint32_t));
    for (z = 0; z < g->size[2]; z ++) {
        for (y = 0; y < g->size[1]; y ++) {
            for (x = 0; x < g->size[0]; x ++, index ++) {
                vec3 p = {
                    g->origin[0] + (float)x * g->spacing,
                    g->origin[1] + (float)y * g->spacing,
                    g->origin[2] + (float)z * g->spacing
                };
                bool inside = false;
                int32_t h;
                ecs_vec_clear(&b->hits);
                box_spatial_overlap_box(world, v->scene, p, p, &b->hits);
                for (h = 0; h < ecs_vec_count(&b->hits) && !inside; h ++) {
                    inside = box_bake_inside(world, cache,
                        *ecs_vec_get_t(&b->hits, ecs_entity_t, h), p);
                }

                if (!inside) {
                    valid[index >> 5] |= 1u << (index & 31);
                }
            }
        }
    }
}

/** Write the probe count of a volume into the LightBakeStatus of its entity.
 *
 * @param it The iterator of the system
 * @param v The volume
 */
static void box_bake_status(
    ecs_iter_t *it,
    const box_bake_volume_t *v)
{
    const LightBakeStatus *status = ecs_get(it->world, v->entity, LightBakeStatus);
    if (status && status->probes != v->probes) {
        LightBakeStatus value = *status;
        value.probes = v->probes;
        ecs_set_ptr(it->stage, v->entity, LightBakeStatus, &value);
    }
}

/** Get a volume handle.
 *
 * @param b The state
 * @return The handle
 */
static uint32_t box_bake_handle_new(
    box_render_bake_t *b)
{
    uint32_t result;
    if (ecs_vec_count(&b->free_handles)) {
        result = *ecs_vec_last_t(&b->free_handles, uint32_t);
        ecs_vec_remove_last(&b->free_handles);
    } else {
        result = b->next_handle ++;
    }

    return result;
}

/** Release the handle of a volume, destroying the volume on the backend.
 *
 * @param b The state
 * @param p The packet
 * @param v The volume
 */
static void box_bake_handle_free(
    box_render_bake_t *b,
    box_render_packet_t *p,
    box_bake_volume_t *v)
{
    if (v->handle) {
        box_render_packet_resource(p, BOX_RENDER_VOLUME, BOX_RENDER_DESTROY, v->handle, 0);
        *ecs_vec_append_t(NULL, &b->free_handles, uint32_t) = v->handle;
        v->handle = 0;
    }

    v->sent = false;
}

/** Place the grid of a volume again and keep it when it did not change.
 *
 * @param it The iterator of the system
 * @param b The state
 * @param p The packet
 * @param v The volume
 * @param bake The LightBake of the scene
 */
static void box_bake_update(
    ecs_iter_t *it,
    box_render_bake_t *b,
    box_render_packet_t *p,
    box_bake_volume_t *v,
    const LightBake *bake)
{
    box_bake_grid_t grid = {0};
    vec3 min, max;
    int32_t probes = 0;
    if (box_bake_bounds(it->world, v->scene, min, max)) {
        probes = box_bake_place(min, max, bake->probe_spacing, bake->bounds_padding, &grid);
    }

    v->placed = true;
    v->probe_spacing = bake->probe_spacing;
    v->bounds_padding = bake->bounds_padding;
    if (probes == v->probes && !ecs_os_memcmp_t(&grid, &v->grid, box_bake_grid_t)) {
        goto done;
    }

    v->grid = grid;
    v->probes = probes;
    v->sent = false;
    if (probes) {
        box_bake_validate(it->world, b, v);
        if (!v->handle) {
            v->handle = box_bake_handle_new(b);
        }
    } else {
        box_bake_handle_free(b, p, v);
        ecs_vec_clear(&v->valid);
    }

done:
    return;
}

/** Write the SH irradiance of the hemisphere ambient of a view.
 *
 * The hemisphere mix(ground, sky, y / 2 + 1 / 2) is the irradiance over pi the forward pass
 * shades with: a + b * y, a the mean and b half the difference of sky and ground.
 *
 * Its projection is exact in bands 0 and 1, E_00 = pi * a * 2 sqrt(pi) and E_1-1 = pi * b *
 * 2 sqrt(pi / 3), so the probes evaluate to the hemisphere.
 *
 * @param l The lights of the view
 * @param out Receives the coefficients
 */
static void box_bake_ambient(
    const box_render_view_lights_t *l,
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    double band0 = GLM_PI * 2.0 * sqrt(GLM_PI);
    double band1 = GLM_PI * 2.0 * sqrt(GLM_PI / 3.0);
    int32_t c;
    ecs_os_memset(out, 0, ECS_SIZEOF(float) * BOX_RENDER_SH_COEFFICIENTS * 4);
    for (c = 0; c < 3; c ++) {
        double sky = l->ambient_sky[c], ground = l->ambient_ground[c];
        out[0][c] = (float)((sky + ground) * 0.5 * band0);
        out[1][c] = (float)((sky - ground) * 0.5 * band1);
    }
}

/** Point the views of the packet at the volume of their scene and take the ambient light of
 * the first view of every scene.
 *
 * @param world The world
 * @param b The state
 * @param p The packet
 */
static void box_bake_views(
    ecs_world_t *world,
    box_render_bake_t *b,
    box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&p->views);
    for (i = 0; i < count; i ++) {
        box_render_view_t *rv = ecs_vec_get_t(&p->views, box_render_view_t, i);
        box_bake_volume_t *v = box_bake_find(b, rv->scene);
        if (!v || !v->handle) {
            continue;
        }

        const View *view = rv->view && ecs_is_alive(world, rv->view)
            ? ecs_get(world, rv->view, View)
            : NULL;
        if (!v->viewed) {
            box_bake_ambient(&rv->lights, v->ambient);
            v->viewed = true;
        }

        if (rv->lights.enabled && (view ? view->lighting.baked : true)) {
            rv->volume = v->handle;
        }
    }
}

/** Send a volume when the backend lacks its grid, or refill it when its ambient changed.
 *
 * @param p The packet
 * @param v The volume
 */
static void box_bake_send(
    box_render_packet_t *p,
    box_bake_volume_t *v)
{
    float (*sh)[4] = v->viewed ? v->ambient : v->sh;
    size_t sh_size = sizeof(v->sh);
    bool refill = memcmp(sh, v->sh, sh_size) != 0;
    if (!v->handle || (v->sent && !refill)) {
        goto done;
    }

    int32_t words = ecs_vec_count(&v->valid);
    uint32_t size = (uint32_t)sizeof(box_render_volume_desc_t) + (uint32_t)words * 4u;
    box_render_volume_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_VOLUME,
        v->sent ? BOX_RENDER_UPDATE : BOX_RENDER_CREATE, v->handle, size);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        desc->origin[i] = v->grid.origin[i];
        desc->size[i] = (uint32_t)v->grid.size[i];
    }

    desc->spacing = v->grid.spacing;
    desc->probes = (uint32_t)v->probes;
    ecs_os_memcpy(desc->sh, sh, (ecs_size_t)sh_size);
    ecs_os_memcpy(desc + 1, ecs_vec_first(&v->valid), words * ECS_SIZEOF(uint32_t));
    if (sh != v->sh) {
        ecs_os_memcpy(v->sh, sh, (ecs_size_t)sh_size);
    }

    v->sent = true;
done:
    return;
}

/** Remove the volume of a scene that lost its LightBake, moving the last one into its place.
 *
 * @param b The state
 * @param p The packet
 * @param index Index of the volume
 */
static void box_bake_remove(
    box_render_bake_t *b,
    box_render_packet_t *p,
    int32_t index)
{
    box_bake_volume_t *v = ecs_vec_get_t(&b->volumes, box_bake_volume_t, index);
    int32_t last = ecs_vec_count(&b->volumes) - 1;
    box_bake_handle_free(b, p, v);
    ecs_vec_fini_t(NULL, &v->valid, uint32_t);
    if (index != last) {
        *v = *ecs_vec_get_t(&b->volumes, box_bake_volume_t, last);
    }

    ecs_vec_remove_last(&b->volumes);
}

/** Check whether the static content of the spatial index changed since the last extract.
 *
 * @param world The world
 * @param b The state
 * @return Whether an entity was placed again, added or removed
 */
static bool box_bake_spatial_changed(
    const ecs_world_t *world,
    box_render_bake_t *b)
{
    box_spatial_stats_t stats;
    box_spatial_stats(world, &stats);
    bool result = stats.updated != 0 || stats.entities != b->indexed;
    b->indexed = stats.entities;

    return result;
}

/** Keep a volume per scene with LightBake and point the views at the volume of their scene.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractBake(
    ecs_iter_t *it)
{
    box_render_bake_t *b = it->ctx;
    ecs_world_t *world = it->world;
    box_render_packet_t *p = box_render_packet(world);
    const box_render_backend_t *backend = box_render_get_backend(world);
    int32_t i, changed = -1;
    if (backend->ctx != b->backend || backend->render != b->render) {
        b->backend = backend->ctx;
        b->render = backend->render;
        for (i = 0; i < ecs_vec_count(&b->volumes); i ++) {
            ecs_vec_get_t(&b->volumes, box_bake_volume_t, i)->sent = false;
        }
    }

    for (i = 0; i < ecs_vec_count(&b->volumes); i ++) {
        box_bake_volume_t *v = ecs_vec_get_t(&b->volumes, box_bake_volume_t, i);
        v->seen = false;
        v->viewed = false;
    }

    ecs_iter_t qit = ecs_query_iter(world, b->bakes);
    while (ecs_query_next(&qit)) {
        const LightBake *bakes = ecs_field(&qit, LightBake, 0);
        int32_t row;
        for (row = 0; row < qit.count; row ++) {
            box_bake_volume_t *v = box_bake_ensure(b, box_render_scene_field(&qit, 1, row));
            if (v->seen) {
                continue;
            }

            if (changed == -1) {
                changed = box_bake_spatial_changed(world, b);
            }

            v->seen = true;
            v->entity = qit.entities[row];
            if (!v->placed || changed || v->probe_spacing != bakes[row].probe_spacing ||
                v->bounds_padding != bakes[row].bounds_padding)
            {
                box_bake_update(it, b, p, v, &bakes[row]);
            }

            box_bake_status(it, v);
        }
    }

    for (i = ecs_vec_count(&b->volumes) - 1; i >= 0; i --) {
        if (!ecs_vec_get_t(&b->volumes, box_bake_volume_t, i)->seen) {
            box_bake_remove(b, p, i);
        }
    }

    box_bake_views(world, b, p);
    for (i = 0; i < ecs_vec_count(&b->volumes); i ++) {
        box_bake_send(p, ecs_vec_get_t(&b->volumes, box_bake_volume_t, i));
    }
}

/** Free the state of the bake extract.
 *
 * @param ctx The state
 */
static void box_bake_free(
    void *ctx)
{
    box_render_bake_t *b = ctx;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&b->volumes); i ++) {
        ecs_vec_fini_t(NULL, &ecs_vec_get_t(&b->volumes, box_bake_volume_t, i)->valid, uint32_t);
    }

    ecs_vec_fini_t(NULL, &b->volumes, box_bake_volume_t);
    ecs_vec_fini_t(NULL, &b->free_handles, uint32_t);
    ecs_vec_fini_t(NULL, &b->hits, ecs_entity_t);
    ecs_os_free(b);
}

void box_render_bake_init(
    ecs_world_t *world)
{
    box_render_bake_t *b = ecs_os_calloc_t(box_render_bake_t);
    ecs_vec_init_t(NULL, &b->volumes, box_bake_volume_t, 0);
    ecs_vec_init_t(NULL, &b->free_handles, uint32_t, 0);
    ecs_vec_init_t(NULL, &b->hits, ecs_entity_t, 0);
    b->next_handle = 1;
    b->bakes = ecs_query(world, {
        .terms = {
            { .id = ecs_id(LightBake), .inout = EcsIn },
            {
                .id = ecs_id(Scene),
                .src.id = EcsSelf | EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone,
                .oper = EcsOptional
            }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractBake" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractBake,
        .ctx = b,
        .ctx_free = box_bake_free
    });
}

bool box_render_bake_probe(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 position,
    box_render_probe_t *out)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractBake");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    const box_bake_volume_t *v = s && s->ctx ? box_bake_find(s->ctx, scene) : NULL;
    bool result = v && v->probes;
    int32_t i, cell[3];
    if (!result) {
        goto done;
    }

    const box_bake_grid_t *g = &v->grid;
    for (i = 0; i < 3; i ++) {
        float f = roundf((position[i] - g->origin[i]) / g->spacing);
        cell[i] = (int32_t)glm_clamp(f, 0.0f, (float)(g->size[i] - 1));
        out->position[i] = g->origin[i] + (float)cell[i] * g->spacing;
    }

    int32_t index = cell[0] + (cell[1] + cell[2] * g->size[1]) * g->size[0];
    const uint32_t *valid = ecs_vec_first(&v->valid);
    out->valid = (valid[index >> 5] >> (index & 31)) & 1u;
done:
    return result;
}
