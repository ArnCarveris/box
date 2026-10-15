/**
 * @file render/bake.c
 * @brief Irradiance probe volume of every scene with LightBake and the captures that bake it.
 *
 * The grid covers the static bounds the spatial index holds, padded and spaced as LightBake
 * says; probes inside a static shape are invalid. Probes start as the SH of the hemisphere
 * ambient of the scene, so a view that samples them looks as it did with the hemisphere.
 *
 * Captures of the nearest unbaked probes go into each packet as budget_ms allows, all of them
 * in a headless run, for bounces + 1 passes, each shading with the probes of the previous one.
 * Views keep the probe buffer of the last complete pass while the next writes the other one.
 */

#include "render.h"
#include "../spatial/spatial.h"
#include "../geometry/mesh_cache.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Probes of a volume at most; a larger grid is spaced wider until it fits */
#define BOX_BAKE_PROBES_MAX (1 << 18)

/** Fraction of the spacing an extent may exceed a whole number of spacings by, float noise */
#define BOX_BAKE_EPSILON (1e-4)

/** GPU milliseconds per probe assumed until the backend measured one, or when it cannot */
#define BOX_BAKE_PROBE_MS (1.0f)

/** Probes captured per scene and frame at most outside a headless run */
#define BOX_BAKE_FRAME_MAX (256)

/** Face edge of a capture in texels at most */
#define BOX_BAKE_SIZE_MAX (256)

/** Error of a LightBake whose capture_size is out of range */
#define BOX_BAKE_SIZE_ERROR "capture_size must be 1 to 256"

/** Bounces a bake may have at most, as the range of Godot's LightmapGI */
#define BOX_BAKE_BOUNCES_MAX (16)

/** Error of a LightBake whose bounces is out of range */
#define BOX_BAKE_BOUNCES_ERROR "bounces must be 0 to 16"

/** Passes of a bake at most, the first one and one per bounce */
#define BOX_BAKE_PASSES_MAX (BOX_BAKE_BOUNCES_MAX + 1)

/** A probe in a packet whose capture the backend has not reported yet */
typedef struct box_bake_flight_t {
    uint32_t probe;         /**< Index of the probe */
    int32_t pass;           /**< Pass of the capture */
    int32_t packet;         /**< Number of the packet, as RenderStatus.submitted after it */
} box_bake_flight_t;

/** Weight of a new measurement in the GPU time per probe */
#define BOX_BAKE_SMOOTHING (0.5f)

/** A probe waiting for its capture and its squared distance to the nearest camera */
typedef struct box_bake_pick_t {
    float distance;         /**< Squared distance to the nearest camera of the scene */
    uint32_t probe;         /**< Index of the probe */
} box_bake_pick_t;

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
    bool converged;         /**< The backend reported every pass of every valid probe */
    bool pending;           /**< The bake can progress this frame and has not converged */
    bool resort;            /**< The queue is sorted again before the next probes are taken */
    float sh[BOX_RENDER_SH_COEFFICIENTS][4]; /**< Irradiance the probes are filled with */
    float ambient[BOX_RENDER_SH_COEFFICIENTS][4]; /**< Irradiance of the views of this frame */
    ecs_vec_t valid;        /**< uint32_t, one bit per probe outside static geometry */
    ecs_vec_t queue;        /**< uint32_t, probes waiting for capture, the nearest last */
    ecs_vec_t flight;       /**< box_bake_flight_t, probes in packets not reported captured */
    ecs_vec_t done;         /**< uint8_t, passes the backend reported for every probe */
    ecs_vec_t cameras;      /**< vec3, cameras of the views of the scene in this frame */
    ecs_vec_t sorted;       /**< vec3, cameras the queue was sorted for */
    box_render_view_lights_t lights; /**< Lights of the first view of the scene in this frame */
    vec3 min;               /**< Minimum corner of the static bounds of the grid */
    vec3 max;               /**< Maximum corner of the static bounds of the grid */
    int32_t capture_size;   /**< LightBake.capture_size of this frame */
    float budget_ms;        /**< LightBake.budget_ms of this frame */
    int32_t bounces;        /**< LightBake.bounces of this frame */
    uint32_t generation;    /**< Number of the bake, so captures of an earlier one are ignored */
    int32_t passes;         /**< Passes of the bake, one more than its bounces */
    int32_t submitted;      /**< Passes whose every probe went into a packet */
    int32_t completed;      /**< Passes the backend reported for every valid probe */
    int32_t valid_count;    /**< Probes outside static geometry, captured by every pass */
    int32_t reported[BOX_BAKE_PASSES_MAX]; /**< Probes the backend reported per pass */
    int32_t first;          /**< Probe buffer the first pass writes, later passes alternate */
    int32_t display;        /**< Probe buffer the views sample */
    bool filled;            /**< The buffer the views sample holds the ambient, no pass */
    const char *error;      /**< Why the volume bakes nothing, NULL for none */
} box_bake_volume_t;

/** Main thread state of the bake extract */
typedef struct box_render_bake_t {
    ecs_query_t *bakes;     /**< Entities with LightBake and their scene */
    ecs_vec_t volumes;      /**< box_bake_volume_t */
    ecs_vec_t free_handles; /**< uint32_t, volume handles to reuse */
    uint32_t next_handle;   /**< Next volume handle */
    ecs_vec_t hits;         /**< ecs_entity_t, scratch of the overlap queries */
    int32_t indexed;        /**< Entities of the spatial index at the last extract */
    ecs_vec_t picks;        /**< box_bake_pick_t, scratch of the choice of the next probes */
    ecs_vec_t captured;     /**< box_render_bake_result_t, scratch of the reported captures */
    float probe_ms;         /**< GPU milliseconds per probe capture */
    bool measured;          /**< probe_ms comes from a measurement */
    int32_t pending;        /**< Probes left in the volumes that can progress */
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
        ecs_vec_init_t(NULL, &result->queue, uint32_t, 0);
        ecs_vec_init_t(NULL, &result->flight, box_bake_flight_t, 0);
        ecs_vec_init_t(NULL, &result->done, uint8_t, 0);
        ecs_vec_init_t(NULL, &result->cameras, vec3, 0);
        ecs_vec_init_t(NULL, &result->sorted, vec3, 0);
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

/** Write the progress of a volume into the LightBakeStatus of its entity when it changed.
 *
 * @param it The iterator of the system
 * @param v The volume
 */
static void box_bake_status(
    ecs_iter_t *it,
    const box_bake_volume_t *v)
{
    const LightBakeStatus *status = v->entity && ecs_is_alive(it->world, v->entity)
        ? ecs_get(it->world, v->entity, LightBakeStatus)
        : NULL;
    int32_t pass = v->completed > 0 ? v->completed - 1 : 0;
    int32_t current = v->completed < v->passes ? v->completed : v->passes - 1;
    int32_t baked = current >= 0 ? v->reported[current] : 0;
    if (!status) {
        goto done;
    }

    bool same_error = v->error
        ? status->error && !strcmp(status->error, v->error)
        : !status->error;
    if (status->probes != v->probes || status->baked != baked || status->pass != pass ||
        status->converged != v->converged || !same_error)
    {
        LightBakeStatus value = *status;
        value.probes = v->probes;
        value.baked = baked;
        value.pass = pass;
        value.converged = v->converged;
        value.error = ECS_CONST_CAST(char*, v->error);
        ecs_set_ptr(it->stage, v->entity, LightBakeStatus, &value);
    }

done:
    return;
}

/** Forget the bake of a volume: no probe is queued, in flight or baked.
 *
 * The passes follow the bounces of this frame; bounces out of range bake one pass, which its
 * error keeps from starting.
 *
 * @param v The volume
 */
static void box_bake_clear(
    box_bake_volume_t *v)
{
    bool bounces = v->bounces >= 0 && v->bounces <= BOX_BAKE_BOUNCES_MAX;
    ecs_vec_clear(&v->queue);
    ecs_vec_clear(&v->flight);
    ecs_vec_set_count_t(NULL, &v->done, uint8_t, v->probes);
    if (v->probes) {
        ecs_os_memset(ecs_vec_first(&v->done), 0, v->probes);
    }

    ecs_os_memset(v->reported, 0, ECS_SIZEOF(v->reported));
    v->passes = bounces ? v->bounces + 1 : 1;
    v->submitted = 0;
    v->completed = 0;
    v->valid_count = 0;
    v->converged = false;
    v->first = v->filled ? v->display : 1 - v->display;
    v->generation ++;
}

/** Queue every valid probe of a volume for the captures of the next pass.
 *
 * @param v The volume
 */
static void box_bake_queue(
    box_bake_volume_t *v)
{
    const uint32_t *valid = ecs_vec_first(&v->valid);
    int32_t i;
    ecs_vec_clear(&v->queue);
    for (i = 0; valid && i < v->probes; i ++) {
        if ((valid[i >> 5] >> (i & 31)) & 1u) {
            *ecs_vec_append_t(NULL, &v->queue, uint32_t) = (uint32_t)i;
        }
    }

    v->valid_count = ecs_vec_count(&v->queue);
    v->resort = true;
}

/** Start the bake of a volume again from its first pass.
 *
 * @param v The volume
 */
static void box_bake_restart(
    box_bake_volume_t *v)
{
    box_bake_clear(v);
    box_bake_queue(v);
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
    if (box_spatial_static_bounds(it->world, v->scene, min, max)) {
        probes = box_bake_place(min, max, bake->probe_spacing, bake->bounds_padding, &grid);
        glm_vec3_copy(min, v->min);
        glm_vec3_copy(max, v->max);
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
        box_bake_clear(v);
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

/** Point the views of the packet at the volume of their scene, take the ambient light and the
 * lights of the first view of every scene and collect the cameras of its views.
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
            v->lights = rv->lights;
            v->viewed = true;
        }

        glm_vec3_copy(rv->camera_transform[3], *ecs_vec_append_t(NULL, &v->cameras, vec3));

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
    v->filled = true;
    v->display = 0;
    box_bake_restart(v);
done:
    return;
}

/** Free the vectors of a volume.
 *
 * @param v The volume
 */
static void box_bake_volume_fini(
    box_bake_volume_t *v)
{
    ecs_vec_fini_t(NULL, &v->valid, uint32_t);
    ecs_vec_fini_t(NULL, &v->queue, uint32_t);
    ecs_vec_fini_t(NULL, &v->flight, box_bake_flight_t);
    ecs_vec_fini_t(NULL, &v->done, uint8_t);
    ecs_vec_fini_t(NULL, &v->cameras, vec3);
    ecs_vec_fini_t(NULL, &v->sorted, vec3);
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
    box_bake_volume_fini(v);
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

/** Order picks farthest first, so the nearest probe is the last.
 *
 * @param a The first pick
 * @param b The second pick
 * @return Negative when a comes first
 */
static int box_bake_pick_cmp(
    const void *a,
    const void *b)
{
    float da = ((const box_bake_pick_t*)a)->distance;
    float db = ((const box_bake_pick_t*)b)->distance;
    uint32_t pa = ((const box_bake_pick_t*)a)->probe;
    uint32_t pb = ((const box_bake_pick_t*)b)->probe;
    int result = (da < db) - (da > db);

    return result ? result : (pa < pb) - (pa > pb);
}

/** Get the world position of a probe of a volume.
 *
 * @param v The volume
 * @param index Index of the probe
 * @param out Receives the position
 */
static void box_bake_probe_position(
    const box_bake_volume_t *v,
    uint32_t index,
    vec3 out)
{
    const box_bake_grid_t *g = &v->grid;
    uint32_t sx = (uint32_t)g->size[0], sy = (uint32_t)g->size[1];
    out[0] = g->origin[0] + (float)(index % sx) * g->spacing;
    out[1] = g->origin[1] + (float)((index / sx) % sy) * g->spacing;
    out[2] = g->origin[2] + (float)(index / (sx * sy)) * g->spacing;
}

/** Check whether the cameras moved more than a probe spacing since the queue was sorted.
 *
 * @param v The volume
 * @return Whether the queue must be sorted again
 */
static bool box_bake_cameras_moved(
    const box_bake_volume_t *v)
{
    int32_t i, count = ecs_vec_count(&v->cameras);
    float reach = v->grid.spacing * v->grid.spacing;
    bool result = count != ecs_vec_count(&v->sorted);
    for (i = 0; !result && i < count; i ++) {
        result = glm_vec3_distance2(*ecs_vec_get_t(&v->cameras, vec3, i),
            *ecs_vec_get_t(&v->sorted, vec3, i)) > reach;
    }

    return result;
}

/** Sort the queue of a volume by the distance to the nearest camera of its scene, nearest last,
 * when probes were queued again or the cameras moved more than a probe spacing.
 *
 * @param b The state
 * @param v The volume
 */
static void box_bake_sort(
    box_render_bake_t *b,
    box_bake_volume_t *v)
{
    int32_t i, c, n = ecs_vec_count(&v->queue), cams = ecs_vec_count(&v->cameras);
    uint32_t *queue = ecs_vec_first(&v->queue);
    if (!v->resort && !box_bake_cameras_moved(v)) {
        goto done;
    }

    ecs_vec_set_count_t(NULL, &b->picks, box_bake_pick_t, n);
    box_bake_pick_t *picks = ecs_vec_first(&b->picks);
    for (i = 0; i < n; i ++) {
        vec3 pos;
        box_bake_probe_position(v, queue[i], pos);
        picks[i].probe = queue[i];
        picks[i].distance = FLT_MAX;
        for (c = 0; c < cams; c ++) {
            float d = glm_vec3_distance2(pos, *ecs_vec_get_t(&v->cameras, vec3, c));
            picks[i].distance = glm_min(picks[i].distance, d);
        }
    }

    qsort(picks, (size_t)n, sizeof(box_bake_pick_t), box_bake_pick_cmp);
    for (i = 0; i < n; i ++) {
        queue[i] = picks[i].probe;
    }

    ecs_vec_set_count_t(NULL, &v->sorted, vec3, cams);
    if (cams) {
        ecs_os_memcpy_n(ecs_vec_first(&v->sorted), ecs_vec_first(&v->cameras), vec3, cams);
    }

    v->resort = false;
done:
    return;
}

/** Move the probes of the next captures from the end of the queue of a volume into a packet.
 *
 * @param v The volume
 * @param count Probes to take, at most the queue
 * @param p The packet the captures are added to
 * @param packet Number of the packet, as RenderStatus.submitted after it
 */
static void box_bake_pick(
    box_bake_volume_t *v,
    int32_t count,
    box_render_packet_t *p,
    int32_t packet)
{
    int32_t i, n = ecs_vec_count(&v->queue);
    uint32_t *queue = ecs_vec_first(&v->queue);
    for (i = n - 1; i >= n - count; i --) {
        box_render_probe_capture_t *out = ecs_vec_append_t(
            NULL, &p->probes, box_render_probe_capture_t);
        out->index = queue[i];
        box_bake_probe_position(v, queue[i], out->position);
        *ecs_vec_append_t(NULL, &v->flight, box_bake_flight_t) = (box_bake_flight_t){
            .probe = queue[i], .pass = v->submitted, .packet = packet
        };
    }

    ecs_vec_set_count_t(NULL, &v->queue, uint32_t, n - count);
}

/** Get the probes a scene captures in a frame of its budget.
 *
 * @param b The state
 * @param v The volume
 * @return Probes of the budget at the GPU time per probe, at least one
 */
static int32_t box_bake_budget(
    const box_render_bake_t *b,
    const box_bake_volume_t *v)
{
    float probes = floorf(v->budget_ms / b->probe_ms);
    int32_t result = BOX_BAKE_FRAME_MAX;
    if (probes < (float)BOX_BAKE_FRAME_MAX) {
        result = probes < 1.0f ? 1 : (int32_t)probes;
    }

    return result;
}

/** Check whether a volume can bake and why not.
 *
 * @param v The volume
 * @return The error that stops it, NULL when its settings are valid
 */
static const char* box_bake_error(
    const box_bake_volume_t *v)
{
    bool size = v->capture_size >= 1 && v->capture_size <= BOX_BAKE_SIZE_MAX;
    bool bounces = v->bounces >= 0 && v->bounces <= BOX_BAKE_BOUNCES_MAX;

    return !size ? BOX_BAKE_SIZE_ERROR : !bounces ? BOX_BAKE_BOUNCES_ERROR : NULL;
}

/** Add a batch of captures of the pass of a volume whose probes are queued to the packet.
 *
 * @param p The packet
 * @param v The volume
 * @param count Probes to take from the end of the queue
 * @param packet Number of the packet, as RenderStatus.submitted after it
 */
static void box_bake_batch(
    box_render_packet_t *p,
    box_bake_volume_t *v,
    int32_t count,
    int32_t packet)
{
    int32_t pass = v->submitted;
    box_render_bake_batch_t *batch = ecs_vec_append_t(NULL, &p->bakes, box_render_bake_batch_t);
    ecs_os_memset_t(batch, 0, box_render_bake_batch_t);
    batch->volume = v->handle;
    batch->generation = v->generation;
    batch->pass = pass;
    batch->source = pass ? v->first ^ ((pass - 1) & 1) : BOX_RENDER_BAKE_BLACK;
    batch->target = v->first ^ (pass & 1);
    batch->scene = v->scene;
    batch->size = v->capture_size;
    batch->lights = v->lights;
    batch->lights.enabled = true;
    glm_vec3_copy(v->min, batch->bounds_min);
    glm_vec3_copy(v->max, batch->bounds_max);
    batch->first = ecs_vec_count(&p->probes);
    batch->count = count;
    box_bake_pick(v, count, p, packet);
}

/** Add the next captures of a volume to the packet.
 *
 * Once every probe of a pass is in a packet the views sample its buffer, which the captures
 * fill before the targets draw, and the probes queue for the next pass. A headless run captures
 * every pass left in one packet, each reading the buffer the batch before it wrote.
 *
 * @param b The state
 * @param p The packet
 * @param v The volume
 * @param headless Whether the run is headless
 * @param packet Number of the packet, as RenderStatus.submitted after it
 */
static void box_bake_schedule(
    box_render_bake_t *b,
    box_render_packet_t *p,
    box_bake_volume_t *v,
    bool headless,
    int32_t packet)
{
    int32_t budget = headless ? INT32_MAX : box_bake_budget(b, v);
    bool more = true;
    while (more && budget > 0 && v->submitted < v->passes) {
        int32_t left = ecs_vec_count(&v->queue);
        int32_t count = budget < left ? budget : left;
        if (count < left) {
            box_bake_sort(b, v);
        }

        if (count) {
            box_bake_batch(p, v, count, packet);
            budget -= count;
        }

        more = false;
        if (count == left) {
            v->display = v->first ^ (v->submitted & 1);
            v->filled = false;
            v->submitted ++;
            ecs_vec_clear(&v->queue);
            if (v->submitted < v->passes) {
                box_bake_queue(v);
            }

            more = headless;
        }
    }
}

/** Mark the probes the backend reported captured as baked.
 *
 * @param world The world
 * @param b The state
 */
static void box_bake_captured(
    ecs_world_t *world,
    box_render_bake_t *b)
{
    box_bake_volume_t *v = NULL;
    int32_t i, j;
    box_render_gpu_bake_captured(world, &b->captured);
    for (i = 0; i < ecs_vec_count(&b->captured); i ++) {
        const box_render_bake_result_t *r = ecs_vec_get_t(
            &b->captured, box_render_bake_result_t, i);
        for (j = 0; (!v || v->handle != r->volume) && j < ecs_vec_count(&b->volumes); j ++) {
            v = ecs_vec_get_t(&b->volumes, box_bake_volume_t, j);
        }

        if (!v || v->handle != r->volume || v->generation != r->generation ||
            r->probe >= (uint32_t)v->probes || r->pass < 0 || r->pass >= v->passes)
        {
            continue;
        }

        uint8_t *done = ecs_vec_get_t(&v->done, uint8_t, (int32_t)r->probe);
        if (*done == r->pass) {
            (*done) ++;
            v->reported[r->pass] ++;
        }
    }
}

/** Queue the probes of the rendered packets of a volume again that the backend did not
 * capture, and forget the probes it reported.
 *
 * A probe of a released packet that was not reported was skipped or its packet dropped. It goes
 * back to the queue of its pass, or, when the next pass or the views already read that pass,
 * the bake starts again.
 *
 * @param v The volume
 * @param rendered Packets the backend released, as RenderStatus.rendered
 */
static void box_bake_requeue(
    box_bake_volume_t *v,
    int32_t rendered)
{
    int32_t i, kept = 0, count = ecs_vec_count(&v->flight);
    box_bake_flight_t *flight = ecs_vec_first(&v->flight);
    const uint8_t *done = ecs_vec_first(&v->done);
    bool restart = false;
    for (i = 0; i < count; i ++) {
        if (done[flight[i].probe] > flight[i].pass) {
            continue;
        }

        if (flight[i].packet > rendered) {
            flight[kept ++] = flight[i];
        } else if (flight[i].pass == v->submitted) {
            *ecs_vec_append_t(NULL, &v->queue, uint32_t) = flight[i].probe;
            v->resort = true;
        } else {
            restart = true;
        }
    }

    ecs_vec_set_count_t(NULL, &v->flight, box_bake_flight_t, kept);
    if (restart) {
        box_bake_restart(v);
    }
}

/** Count the passes of a volume the backend reported for every valid probe.
 *
 * @param v The volume
 * @return Probe captures of every pass of the bake not reported yet
 */
static int32_t box_bake_complete(
    box_bake_volume_t *v)
{
    int32_t i, result = 0;
    while (v->completed < v->submitted && v->reported[v->completed] >= v->valid_count) {
        v->completed ++;
    }

    for (i = 0; i < v->passes; i ++) {
        result += v->valid_count - v->reported[i];
    }

    return result;
}

/** Take the GPU time of the captures the backend measured into the time per probe.
 *
 * @param world The world
 * @param b The state
 */
static void box_bake_timing(
    ecs_world_t *world,
    box_render_bake_t *b)
{
    box_render_bake_timing_t timing;
    if (!box_render_gpu_bake_timing(world, &timing) || timing.probes <= 0) {
        goto done;
    }

    float per = (float)(timing.ms / (double)timing.probes);
    b->probe_ms = b->measured
        ? b->probe_ms + (per - b->probe_ms) * BOX_BAKE_SMOOTHING
        : per;
    b->probe_ms = glm_max(b->probe_ms, 1e-3f);
    b->measured = true;
done:
    return;
}

/** Capture the next probes of every volume and update the progress of the volumes.
 *
 * @param it The iterator of the system
 * @param b The state
 * @param p The packet
 */
static void box_bake_progress(
    ecs_iter_t *it,
    box_render_bake_t *b,
    box_render_packet_t *p)
{
    box_render_gpu_stats_t stats;
    const App *app = ecs_singleton_get(it->world, App);
    const RenderStatus *status = ecs_singleton_get(it->world, RenderStatus);
    bool gpu = box_render_gpu_stats(it->world, &stats) && !stats.failed;
    int32_t i, packet = (status ? status->submitted : 0) + 1;
    int32_t rendered = status ? status->rendered : 0;
    box_bake_timing(it->world, b);
    box_bake_captured(it->world, b);
    b->pending = 0;
    for (i = 0; i < ecs_vec_count(&b->volumes); i ++) {
        box_bake_volume_t *v = ecs_vec_get_t(&b->volumes, box_bake_volume_t, i);
        box_bake_requeue(v, rendered);
        box_bake_complete(v);
        v->error = box_bake_error(v);
        v->converged = v->sent && v->probes && v->completed == v->passes;
        v->pending = gpu && v->handle && v->sent && v->viewed && !v->error &&
            v->budget_ms > 0 && !v->converged;
        if (v->pending) {
            box_bake_schedule(b, p, v, app && app->headless, packet);
            b->pending += box_bake_complete(v);
        }

        box_bake_status(it, v);
    }
}

/** Point the views of the packet that sample a volume at the probe buffer it shows.
 *
 * @param b The state
 * @param p The packet
 */
static void box_bake_display(
    const box_render_bake_t *b,
    box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&p->views);
    for (i = 0; i < count; i ++) {
        box_render_view_t *rv = ecs_vec_get_t(&p->views, box_render_view_t, i);
        const box_bake_volume_t *v = rv->volume ? box_bake_find(b, rv->scene) : NULL;
        if (v) {
            rv->volume_buffer = (uint32_t)v->display;
        }
    }
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
        ecs_vec_clear(&v->cameras);
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
            v->capture_size = bakes[row].capture_size;
            v->budget_ms = bakes[row].budget_ms;
            if (v->bounces != bakes[row].bounces) {
                v->bounces = bakes[row].bounces;
                box_bake_restart(v);
            }

            if (!v->placed || changed || v->probe_spacing != bakes[row].probe_spacing ||
                v->bounds_padding != bakes[row].bounds_padding)
            {
                box_bake_update(it, b, p, v, &bakes[row]);
            }
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

    box_bake_progress(it, b, p);
    box_bake_display(b, p);
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
        box_bake_volume_fini(ecs_vec_get_t(&b->volumes, box_bake_volume_t, i));
    }

    ecs_vec_fini_t(NULL, &b->volumes, box_bake_volume_t);
    ecs_vec_fini_t(NULL, &b->picks, box_bake_pick_t);
    ecs_vec_fini_t(NULL, &b->captured, box_render_bake_result_t);
    ecs_vec_fini_t(NULL, &b->free_handles, uint32_t);
    ecs_vec_fini_t(NULL, &b->hits, ecs_entity_t);
    ecs_os_free(b);
}

void box_render_bake_init(
    ecs_world_t *world)
{
    box_render_bake_t *b = ecs_os_calloc_t(box_render_bake_t);
    b->probe_ms = BOX_BAKE_PROBE_MS;
    ecs_vec_init_t(NULL, &b->picks, box_bake_pick_t, 0);
    ecs_vec_init_t(NULL, &b->captured, box_render_bake_result_t, 0);
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

int32_t box_render_bake_pending(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractBake");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    const box_render_bake_t *b = s ? s->ctx : NULL;

    return b ? b->pending : 0;
}
