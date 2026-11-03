/**
 * @file render/bake.c
 * @brief Irradiance probe volume of every scene with LightBake and the captures that bake it.
 *
 * The grid covers the static bounds of the spatial index, padded and spaced as LightBake says;
 * probes inside a static shape or in open air, which no static surface blends, are invalid.
 * Probes start as the SH of the sky: the hemisphere ambient, or LightBake.environment.
 *
 * Captures of the nearest unbaked probes go into each packet as budget_ms allows for bounces
 * + 1 passes, each shading with the previous one; views keep the last complete pass. A change
 * rebakes the probes it can reach (bake_change.c) in a copy of the shown buffer, then shows it.
 */

#include "bake.h"
#include "environment.h"
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

/** Error of a LightBake whose bounces is out of range */
#define BOX_BAKE_BOUNCES_ERROR "bounces must be 0 to 16"

/** Find the volume of a scene.
 *
 * @param b The state
 * @param scene The scene root
 * @return The volume, NULL when the scene has none
 */
box_bake_volume_t* box_bake_find(
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
        ecs_vec_init_t(NULL, &result->checked, uint32_t, 0);
        ecs_vec_init_t(NULL, &result->queue, uint32_t, 0);
        ecs_vec_init_t(NULL, &result->flight, box_bake_flight_t, 0);
        ecs_vec_init_t(NULL, &result->done, uint8_t, 0);
        ecs_vec_init_t(NULL, &result->members, uint32_t, 0);
        ecs_vec_init_t(NULL, &result->dirty, uint32_t, 0);
        ecs_vec_init_t(NULL, &result->flags, uint8_t, 0);
        ecs_vec_init_t(NULL, &result->shown, uint32_t, 0);
        box_bake_light_record_init(&result->known);
        ecs_vec_init_t(NULL, &result->cameras, vec3, 0);
        ecs_vec_init_t(NULL, &result->sorted, vec3, 0);
    }

    return result;
}

/** Place the probes of one axis of padded bounds on the lattice (k + 1/2) * spacing.
 *
 * @param lo Low end of the padded bounds
 * @param hi High end of the padded bounds
 * @param spacing World units between probes
 * @param first Receives the lattice index of the first probe
 * @return Probes along the axis, at least 1
 */
static double box_bake_axis(
    double lo,
    double hi,
    double spacing,
    int32_t *first)
{
    double a = ceil(lo / spacing - 0.5 - BOX_BAKE_EPSILON);
    double b = floor(hi / spacing - 0.5 + BOX_BAKE_EPSILON);
    if (b < a) {
        a = b = floor(((lo + hi) * 0.5) / spacing);
    }

    b = fmin(b, a + (double)BOX_BAKE_PROBES_MAX);
    *first = (int32_t)a;

    return b - a + 1.0;
}

/** Place a grid over bounds: probes at (k + 1/2) * spacing inside the padded bounds, so a
 * change of the bounds only adds or removes probes at the edges (as Unity places adaptive
 * probe volume bricks on a world grid). Above BOX_BAKE_PROBES_MAX probes it is spaced wider.
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
    double step = spacing, probes = 0;
    int32_t i, first[3], result = 0;
    ecs_os_memset_t(out, 0, box_bake_grid_t);
    if (!(spacing > 0) || !isfinite(spacing) || !isfinite(padding)) {
        goto done;
    }

    do {
        if (probes > BOX_BAKE_PROBES_MAX) {
            step *= fmax(cbrt(probes / BOX_BAKE_PROBES_MAX), 1.01);
        }

        probes = 1;
        for (i = 0; i < 3; i ++) {
            double n = box_bake_axis((double)min[i] - padding, (double)max[i] + padding, step,
                &first[i]);
            out->size[i] = (int32_t)n;
            probes *= n;
        }
    } while (probes > BOX_BAKE_PROBES_MAX);

    out->spacing = (float)step;
    for (i = 0; i < 3; i ++) {
        out->origin[i] = (float)(((double)first[i] + 0.5) * step);
    }

    result = (int32_t)probes;
done:
    return result;
}

/** Get the world position of a probe of a volume.
 *
 * @param v The volume
 * @param index Index of the probe
 * @param out Receives the position
 */
void box_bake_probe_position(
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

/** Signed solid angle that a triangle subtends at the origin (Van Oosterom and Strackee),
 * positive when its corners wind counter-clockwise seen from the origin.
 *
 * @param a The first corner, relative to the origin
 * @param b The second corner, relative to the origin
 * @param c The third corner, relative to the origin
 * @return The solid angle in steradians
 */
static double box_bake_solid_angle(
    const vec3 a,
    const vec3 b,
    const vec3 c)
{
    vec3 bc;
    glm_vec3_cross(ECS_CONST_CAST(float*, b), ECS_CONST_CAST(float*, c), bc);
    double la = glm_vec3_norm(ECS_CONST_CAST(float*, a));
    double lb = glm_vec3_norm(ECS_CONST_CAST(float*, b));
    double lc = glm_vec3_norm(ECS_CONST_CAST(float*, c));
    double num = glm_vec3_dot(ECS_CONST_CAST(float*, a), bc);
    double den = la * lb * lc +
        glm_vec3_dot(ECS_CONST_CAST(float*, a), ECS_CONST_CAST(float*, b)) * lc +
        glm_vec3_dot(ECS_CONST_CAST(float*, b), ECS_CONST_CAST(float*, c)) * la +
        glm_vec3_dot(ECS_CONST_CAST(float*, c), ECS_CONST_CAST(float*, a)) * lb;

    return 2.0 * atan2(num, den);
}

/** Check whether a point in the space of a unit mesh is enclosed by it: the generalized
 * winding number of the mesh at the point, the solid angle of its triangles over 4 pi,
 * exceeds one half. Holds for meshes with small gaps, and either winding.
 *
 * @param mesh The mesh
 * @param p The point
 * @return Whether the mesh encloses the point
 */
static bool box_bake_enclosed(
    const box_mesh_t *mesh,
    const vec3 p)
{
    double angle = 0;
    int32_t t;
    for (t = 0; t + 2 < mesh->index_count; t += 3) {
        vec3 a, b, c;
        glm_vec3_sub(mesh->vertices[mesh->indices[t]].position, ECS_CONST_CAST(float*, p), a);
        glm_vec3_sub(mesh->vertices[mesh->indices[t + 1]].position,
            ECS_CONST_CAST(float*, p), b);
        glm_vec3_sub(mesh->vertices[mesh->indices[t + 2]].position,
            ECS_CONST_CAST(float*, p), c);
        angle += box_bake_solid_angle(a, b, c);
    }

    return fabs(angle) > 2.0 * GLM_PI;
}

/** Check whether a position lies inside a static shape that the bake captures.
 *
 * A position inside the oriented bounds of the scaled and placed unit mesh of the shape is
 * tested against its triangles: one the surface does not enclose, such as a corner of the
 * bounds of a sphere or the hole of a torus, is outside, as is a position on the surface.
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
        (ecs_has_id(world, e, ManualTransform) && !ecs_has(world, e, SurfaceAttachment)) ||
        ecs_has_id(world, e, NoBake) ||
        box_projection_anchored(world, e))
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

    if (shape->surface.p[0][0]) {
        vec3 point;
        for (int j = 0; j < 3; j ++) {
            if (!shape->scale[j]) {
                goto done;
            }

            point[j] = local[j] / shape->scale[j];
        }

        result = box_surface_enclosed(shape, mesh, point);
        goto done;
    }

    int32_t i;
    vec3 unit = {0};
    result = true;
    for (i = 0; i < 3; i ++) {
        float a = mesh->min[i] * shape->scale[i];
        float b = mesh->max[i] * shape->scale[i];
        if (!(local[i] > glm_min(a, b) && local[i] < glm_max(a, b))) {
            result = false;
        } else {
            unit[i] = local[i] / shape->scale[i];
        }
    }

    if (result) {
        result = box_bake_enclosed(mesh, unit);
    }

done:
    return result;
}

/** Check whether a position lies inside a static shape of a scene that the bake captures.
 *
 * @param world The world
 * @param b The state, owns the scratch of the overlap queries
 * @param cache The mesh cache
 * @param scene The scene
 * @param pos The position
 * @return Whether the position is inside
 */
bool box_bake_occupied(
    const ecs_world_t *world,
    box_render_bake_t *b,
    const box_mesh_cache_t *cache,
    ecs_entity_t scene,
    const vec3 pos)
{
    bool result = false;
    int32_t h;
    ecs_vec_clear(&b->hits);
    box_spatial_overlap_box(world, scene, pos, pos, &b->hits);
    for (h = 0; h < ecs_vec_count(&b->hits) && !result; h ++) {
        result = box_bake_inside(world, cache, *ecs_vec_get_t(&b->hits, ecs_entity_t, h), pos);
    }

    return result;
}

float box_bake_reach(
    const box_bake_volume_t *v)
{
    float beyond = v->bounds_padding < 0 ? -v->bounds_padding : 0.0f;

    return (v->grid.spacing + beyond) * (1.0f + (float)BOX_BAKE_EPSILON);
}

bool box_bake_usable(
    const ecs_world_t *world,
    box_render_bake_t *b,
    const box_mesh_cache_t *cache,
    const box_bake_volume_t *v,
    const vec3 pos)
{
    float reach = box_bake_reach(v);
    ecs_entity_t scene = v->scene;
    bool result = false;
    vec3 lo, hi;
    glm_vec3_subs(ECS_CONST_CAST(float*, pos), reach, lo);
    glm_vec3_adds(ECS_CONST_CAST(float*, pos), reach, hi);
    ecs_vec_clear(&b->hits);
    if (box_spatial_overlap_box(world, scene, lo, hi, &b->hits)) {
        result = !box_bake_occupied(world, b, cache, scene, pos);
    }

    return result;
}

/** Mark the probes of a volume that no static surface can use as invalid.
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
    int32_t words = (v->probes + 31) / 32;
    int32_t i;
    ecs_vec_set_count_t(NULL, &v->valid, uint32_t, words);
    uint32_t *valid = ecs_vec_first(&v->valid);
    ecs_os_memset(valid, 0, words * ECS_SIZEOF(uint32_t));
    v->valid_count = 0;
    for (i = 0; i < v->probes; i ++) {
        vec3 pos;
        box_bake_probe_position(v, (uint32_t)i, pos);
        if (box_bake_usable(world, b, cache, v, pos)) {
            valid[i >> 5] |= 1u << (i & 31);
            v->valid_count ++;
        }
    }
}

/** Count the probes a volume still has to bake: those of the pass being baked that were not
 * reported yet and those a change queued for the next bake.
 *
 * @param v The volume
 * @return The probes
 */
static int32_t box_bake_left(
    const box_bake_volume_t *v)
{
    int32_t current = v->completed < v->passes ? v->completed : v->passes - 1;
    int32_t reported = current >= 0 ? v->reported[current] : 0;

    return v->count - reported + ecs_vec_count(&v->dirty);
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
    int32_t pass = v->completed;
    int32_t baked = v->valid_count - box_bake_left(v);
    baked = baked > 0 ? baked : 0;
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

/** Forget the queue of changes of a volume.
 *
 * @param v The volume
 */
static void box_bake_forget(
    box_bake_volume_t *v)
{
    ecs_vec_clear(&v->dirty);
    ecs_vec_set_count_t(NULL, &v->flags, uint8_t, v->probes);
    if (v->probes) {
        ecs_os_memset(ecs_vec_first(&v->flags), 0, v->probes);
    }
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
    v->count = 0;
    v->converged = false;
    v->first = v->display;
    v->generation ++;
}

/** Queue the probes of the bake of a volume for the captures of the next pass.
 *
 * @param v The volume
 */
static void box_bake_queue(
    box_bake_volume_t *v)
{
    ecs_vec_set_count_t(NULL, &v->queue, uint32_t, v->count);
    if (v->count) {
        ecs_os_memcpy_n(ecs_vec_first(&v->queue), ecs_vec_first(&v->members), uint32_t,
            v->count);
    }

    v->resort = true;
}

/** Check whether a probe of a volume is one a static surface can use.
 *
 * @param v The volume
 * @param probe Index of the probe
 * @return Whether the probe is valid
 */
bool box_bake_valid(
    const box_bake_volume_t *v,
    uint32_t probe)
{
    const uint32_t *valid = ecs_vec_first(&v->valid);

    return valid && ((valid[probe >> 5] >> (probe & 31)) & 1u);
}

/** Start a bake of a volume from its first pass, of every valid probe or of its members again.
 *
 * Over probes that still hold the ambient the first pass writes the shown buffer, so a new
 * volume lights up probe by probe. Otherwise every pass bakes in place (Gauss-Seidel, as DDGI)
 * in a copy of the shown buffer, which the views switch to once the last pass is in a packet.
 *
 * @param v The volume
 * @param all Whether every valid probe is baked, else the members of the last bake
 */
static void box_bake_begin(
    box_bake_volume_t *v,
    bool all)
{
    int32_t i;
    if (all) {
        ecs_vec_clear(&v->members);
        for (i = 0; i < v->probes; i ++) {
            if (box_bake_valid(v, (uint32_t)i)) {
                *ecs_vec_append_t(NULL, &v->members, uint32_t) = (uint32_t)i;
            }
        }

        box_bake_forget(v);
    }

    box_bake_clear(v);
    v->count = ecs_vec_count(&v->members);
    v->staged = !v->filled;
    v->stage = 1 - v->display;
    v->copy = v->staged;
    v->baked_size = v->capture_size;
    v->fresh |= all;
    box_bake_queue(v);
}

/** Start a bake of the probes changes queued, when no bake is in progress.
 *
 * @param v The volume
 */
static void box_bake_next(
    box_bake_volume_t *v)
{
    int32_t i, count = ecs_vec_count(&v->dirty);
    const uint32_t *dirty = ecs_vec_first(&v->dirty);
    if (!count || v->completed < v->passes) {
        goto done;
    }

    ecs_vec_clear(&v->members);
    for (i = 0; i < count; i ++) {
        if (box_bake_valid(v, dirty[i])) {
            *ecs_vec_append_t(NULL, &v->members, uint32_t) = dirty[i];
        }
    }

    box_bake_forget(v);
    box_bake_begin(v, false);
done:
    return;
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

/** Point the views of the packet at the volume of their scene, take the ambient light of the
 * first view of every scene with the lights of its captures and collect the cameras of its
 * views.
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
    if (!b->lights_state) {
        b->lights_state = box_render_lights_state(world);
    }

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
            box_bake_scene_lights(b->lights_state, v->scene, &rv->lights, &v->lights);
            v->viewed = true;
        }

        glm_vec3_copy(rv->camera_transform[3], *ecs_vec_append_t(NULL, &v->cameras, vec3));

        if (rv->lights.enabled && (view ? view->lighting.baked : true)) {
            rv->volume = v->handle;
            if (rv->debug.light_probes && v->probes > BOX_RENDER_DEBUG_PROBES_MAX &&
                !v->debug_warned)
            {
                char *path = ecs_get_path(world, rv->view);
                ecs_warn("view '%s' shows %d of the %d light probes of its volume "
                    "(BOX_GPU_DEBUG_PROBES_MAX)", path, BOX_RENDER_DEBUG_PROBES_MAX, v->probes);
                ecs_os_free(path);
                v->debug_warned = true;
            }
        }
    }
}

/** Add a BOX_RENDER_VOLUME resource of a volume to a packet, followed by the shown mask.
 *
 * @param p The packet
 * @param v The volume
 * @param op What the backend does
 * @param create Whether the backend creates the volume
 * @return The description, which a resize completes with the old grid
 */
static box_render_volume_desc_t* box_bake_desc(
    box_render_packet_t *p,
    const box_bake_volume_t *v,
    box_render_volume_op_t op,
    bool create)
{
    int32_t i, words = ecs_vec_count(&v->shown);
    uint32_t size = (uint32_t)sizeof(box_render_volume_desc_t) + (uint32_t)words * 4u;
    box_render_volume_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_VOLUME,
        create ? BOX_RENDER_CREATE : BOX_RENDER_UPDATE, v->handle, size);
    for (i = 0; i < 3; i ++) {
        desc->origin[i] = v->grid.origin[i];
        desc->size[i] = (uint32_t)v->grid.size[i];
    }

    desc->spacing = v->grid.spacing;
    desc->probes = (uint32_t)v->probes;
    desc->op = (uint32_t)op;
    desc->from = (uint32_t)(1 - v->stage);
    desc->environment = v->filled_env;
    ecs_os_memcpy(desc->sh, v->sh, ECS_SIZEOF(v->sh));
    ecs_os_memcpy(desc + 1, ecs_vec_first(&v->shown), words * ECS_SIZEOF(uint32_t));

    return desc;
}

/** Take the environment the captures of a volume see where nothing covers them in this frame:
 * the cubemap of LightBake.environment once its source sent content, else the hemisphere.
 *
 * @param world The world
 * @param v The volume
 */
static void box_bake_environment(
    ecs_world_t *world,
    box_bake_volume_t *v)
{
    uint32_t handle = v->environment
        ? box_render_environment_handle(world, v->environment)
        : 0;
    int32_t content = handle ? box_render_environment_content(world, v->environment) : 0;
    v->env_handle = content ? handle : 0;
    v->env_content = content;
}

/** Send a volume when the backend lacks its grid, or refill it when its ambient or its
 * environment changed before anything was baked. An ambient or environment change after a bake
 * rebakes every probe instead, and the probes keep their values until then.
 *
 * New content of the same environment rebakes nothing while a directional light of the last
 * frame was held within the thresholds of its baked state: the source follows that light, as
 * an atmosphere follows its sun, and the light rebakes once it passes them.
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
    bool ambient = memcmp(sh, v->sh, sh_size) != 0;
    bool source = v->env_handle != v->filled_env;
    bool content = v->env_content != v->filled_content;
    if (!v->handle || (v->sent && !ambient && !source && !content)) {
        goto done;
    }

    if (sh != v->sh) {
        ecs_os_memcpy(v->sh, sh, (ecs_size_t)sh_size);
    }

    v->filled_env = v->env_handle;
    v->filled_content = v->env_content;
    if (v->sent && !v->filled) {
        if (ambient || source || !v->known.held) {
            box_bake_mark_all(v);
            v->relit = true;
        }

        goto done;
    }

    int32_t words = ecs_vec_count(&v->valid);
    ecs_vec_set_count_t(NULL, &v->shown, uint32_t, words);
    if (words) {
        ecs_os_memcpy_n(ecs_vec_first(&v->shown), ecs_vec_first(&v->valid), uint32_t, words);
    }

    box_bake_desc(p, v, BOX_RENDER_VOLUME_FILL, !v->sent);
    v->sent = true;
    v->filled = true;
    v->mask = false;
    v->display = 0;
    v->relit = false;
    v->settled = false;
    box_bake_begin(v, true);
done:
    return;
}

/** Send the copy into the stage buffer a staged bake starts with and the shown mask when it
 * changed.
 *
 * @param p The packet
 * @param v The volume
 */
static void box_bake_ops(
    box_render_packet_t *p,
    box_bake_volume_t *v)
{
    if (!v->sent || !v->handle) {
        goto done;
    }

    if (v->copy) {
        box_bake_desc(p, v, BOX_RENDER_VOLUME_COPY, false);
        v->copy = false;
        v->mask = false;
    } else if (v->mask) {
        box_bake_desc(p, v, BOX_RENDER_VOLUME_MASK, false);
        v->mask = false;
    }

done:
    return;
}

/** Get the index a probe of one grid has in another grid of the same lattice.
 *
 * @param from The grid of the probe
 * @param to The other grid
 * @param probe Index of the probe in from
 * @return Index in to, -1 when to lacks the probe
 */
static int32_t box_bake_reindex(
    const box_bake_grid_t *from,
    const box_bake_grid_t *to,
    uint32_t probe)
{
    int32_t cell[3], i, result = -1;
    cell[0] = (int32_t)(probe % (uint32_t)from->size[0]);
    cell[1] = (int32_t)((probe / (uint32_t)from->size[0]) % (uint32_t)from->size[1]);
    cell[2] = (int32_t)(probe / ((uint32_t)from->size[0] * (uint32_t)from->size[1]));
    for (i = 0; i < 3; i ++) {
        cell[i] += (int32_t)roundf((from->origin[i] - to->origin[i]) / to->spacing);
        if (cell[i] < 0 || cell[i] >= to->size[i]) {
            goto done;
        }
    }

    result = cell[0] + (cell[1] + cell[2] * to->size[1]) * to->size[0];
done:
    return result;
}

/** Grow or shrink the grid of a volume on its lattice; the backend keeps the probes of both
 * buffers it shares with the old grid. New probes start as the ambient, hidden, and are queued
 * with the probes of the bake that was in progress and those changes had queued.
 *
 * @param world The world
 * @param b The state
 * @param p The packet
 * @param v The volume
 * @param grid The new grid
 * @param probes Probes of the new grid
 */
static void box_bake_resize(
    const ecs_world_t *world,
    box_render_bake_t *b,
    box_render_packet_t *p,
    box_bake_volume_t *v,
    const box_bake_grid_t *grid,
    int32_t probes)
{
    box_bake_grid_t old = v->grid;
    int32_t i, words = (probes + 31) / 32;
    ecs_vec_t requeue, shown;
    ecs_vec_init_t(NULL, &requeue, uint32_t, 0);
    ecs_vec_init_t(NULL, &shown, uint32_t, words);
    for (i = 0; i < ecs_vec_count(&v->dirty); i ++) {
        *ecs_vec_append_t(NULL, &requeue, uint32_t) = *ecs_vec_get_t(&v->dirty, uint32_t, i);
    }

    for (i = 0; v->completed < v->passes && i < ecs_vec_count(&v->members); i ++) {
        *ecs_vec_append_t(NULL, &requeue, uint32_t) = *ecs_vec_get_t(&v->members, uint32_t, i);
    }

    ecs_vec_set_count_t(NULL, &shown, uint32_t, words);
    uint32_t *bits = ecs_vec_first(&shown);
    ecs_os_memset(bits, 0, words * ECS_SIZEOF(uint32_t));
    for (i = 0; i < v->probes; i ++) {
        int32_t to = box_bake_reindex(&old, grid, (uint32_t)i);
        if (to != -1 && ((*ecs_vec_get_t(&v->shown, uint32_t, i >> 5) >> (i & 31)) & 1u)) {
            bits[to >> 5] |= 1u << (to & 31);
        }
    }

    v->grid = *grid;
    v->probes = probes;
    box_bake_validate(world, b, v);
    box_bake_clear(v);
    v->completed = v->submitted = v->passes;
    ecs_vec_clear(&v->members);
    box_bake_forget(v);
    for (i = 0; i < words; i ++) {
        bits[i] &= *ecs_vec_get_t(&v->valid, uint32_t, i);
    }

    ecs_vec_fini_t(NULL, &v->shown, uint32_t);
    v->shown = shown;
    for (i = 0; i < ecs_vec_count(&requeue); i ++) {
        int32_t to = box_bake_reindex(&old, grid, *ecs_vec_get_t(&requeue, uint32_t, i));
        if (to != -1) {
            box_bake_mark(v, (uint32_t)to);
        }
    }

    for (i = 0; i < probes; i ++) {
        if (box_bake_reindex(grid, &old, (uint32_t)i) == -1) {
            box_bake_mark(v, (uint32_t)i);
        }
    }

    box_render_volume_desc_t *desc = box_bake_desc(p, v, BOX_RENDER_VOLUME_RESIZE, false);
    for (i = 0; i < 3; i ++) {
        desc->old_size[i] = (uint32_t)old.size[i];
        desc->shift[i] = (int32_t)roundf((grid->origin[i] - old.origin[i]) / grid->spacing);
    }

    v->copy = false;
    v->mask = false;
    ecs_vec_fini_t(NULL, &requeue, uint32_t);
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

    if (v->sent && v->probes && probes && grid.spacing == v->grid.spacing) {
        box_bake_resize(it->world, b, p, v, &grid, probes);
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

/** Free the vectors of a volume.
 *
 * @param v The volume
 */
static void box_bake_volume_fini(
    box_bake_volume_t *v)
{
    ecs_vec_fini_t(NULL, &v->valid, uint32_t);
    ecs_vec_fini_t(NULL, &v->checked, uint32_t);
    ecs_vec_fini_t(NULL, &v->queue, uint32_t);
    ecs_vec_fini_t(NULL, &v->flight, box_bake_flight_t);
    ecs_vec_fini_t(NULL, &v->done, uint8_t);
    ecs_vec_fini_t(NULL, &v->members, uint32_t);
    ecs_vec_fini_t(NULL, &v->dirty, uint32_t);
    ecs_vec_fini_t(NULL, &v->flags, uint8_t);
    ecs_vec_fini_t(NULL, &v->shown, uint32_t);
    box_bake_light_record_fini(&v->known);
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
 * The share of a probe the budget leaves carries over to the next frame. A frame costs a fixed
 * part besides its probes, which the time per probe holds: rounding down would keep a frame at
 * the count that measured it, while the carry lets the count settle where it fills the budget.
 *
 * @param b The state
 * @param v The volume
 * @return Probes of the budget at the GPU time per probe, at least one
 */
static int32_t box_bake_budget(
    const box_render_bake_t *b,
    box_bake_volume_t *v)
{
    float probes = v->credit + v->budget_ms / b->probe_ms;
    int32_t result = BOX_BAKE_FRAME_MAX;
    if (probes < (float)BOX_BAKE_FRAME_MAX) {
        result = probes < 1.0f ? 1 : (int32_t)probes;
    }

    v->credit = probes > (float)result && result < BOX_BAKE_FRAME_MAX
        ? probes - (float)result
        : 0.0f;

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
    if (v->staged) {
        batch->source = pass ? v->stage : BOX_RENDER_BAKE_BLACK;
        batch->target = v->stage;
    } else {
        batch->source = pass ? v->first ^ ((pass - 1) & 1) : BOX_RENDER_BAKE_BLACK;
        batch->target = v->first ^ (pass & 1);
    }

    batch->scene = v->scene;
    batch->size = v->capture_size;
    batch->lights = batch->source == BOX_RENDER_BAKE_BLACK ? v->lights.first : v->lights.later;
    batch->lights.enabled = true;
    batch->baked = v->lights.baked;
    batch->baked_count = v->lights.baked_count;
    batch->baked_directional = v->lights.baked_directional;
    batch->environment = v->env_handle;
    glm_vec3_copy(v->min, batch->bounds_min);
    glm_vec3_copy(v->max, batch->bounds_max);
    batch->first = ecs_vec_count(&p->probes);
    batch->count = count;
    box_bake_pick(v, count, p, packet);
}

/** Show the pass of the bake of a volume whose every probe just went into a packet, when the
 * views show its passes as they complete, or the stage buffer after the last pass of a staged
 * bake. The views then see the probes it captured and those they saw before, still valid.
 *
 * @param v The volume
 */
static void box_bake_submitted(
    box_bake_volume_t *v)
{
    int32_t i, words = ecs_vec_count(&v->shown);
    uint32_t *shown = ecs_vec_first(&v->shown);
    const uint32_t *valid = ecs_vec_first(&v->valid);
    const uint32_t *members = ecs_vec_first(&v->members);
    if (v->staged && v->submitted + 1 < v->passes) {
        goto done;
    }

    v->switches ++;
    if (!v->staged) {
        v->display = v->first ^ (v->submitted & 1);
        v->filled = false;
    } else {
        v->display = v->stage;
    }

    if (v->staged || !v->submitted) {
        for (i = 0; i < v->count; i ++) {
            shown[members[i] >> 5] |= 1u << (members[i] & 31);
        }

        for (i = 0; i < words; i ++) {
            shown[i] &= valid[i];
        }

        v->mask = true;
    }

done:
    return;
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
            box_bake_submitted(v);
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
        box_bake_begin(v, false);
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
    while (v->completed < v->submitted && v->reported[v->completed] >= v->count) {
        v->completed ++;
    }

    for (i = 0; i < v->passes; i ++) {
        result += v->count - v->reported[i];
    }

    return result + ecs_vec_count(&v->dirty) * v->passes;
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
        v->settled |= v->sent && v->probes && v->relit && v->completed == v->passes;
        if (v->sent && v->probes) {
            box_bake_next(v);
        }

        bool converged = v->converged;
        v->converged = v->sent && v->probes && v->completed == v->passes &&
            !ecs_vec_count(&v->dirty);
        v->switches += v->converged && !converged;
        v->settled |= v->converged;
        v->pending = gpu && v->handle && v->sent && v->viewed && !v->error &&
            v->budget_ms > 0 && !v->converged;
        if (v->pending) {
            box_bake_schedule(b, p, v, app && app->headless, packet);
            int32_t left = box_bake_complete(v);
            b->pending += v->settled ? 0 : left;
        }

        box_bake_ops(p, v);
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
    if (backend->ctx != b->backend || backend->render != b->render ||
        backend->generation != b->generation)
    {
        b->backend = backend->ctx;
        b->render = backend->render;
        b->generation = backend->generation;
        for (i = 0; i < ecs_vec_count(&b->volumes); i ++) {
            ecs_vec_get_t(&b->volumes, box_bake_volume_t, i)->sent = false;
        }
    }

    for (i = 0; i < ecs_vec_count(&b->volumes); i ++) {
        box_bake_volume_t *v = ecs_vec_get_t(&b->volumes, box_bake_volume_t, i);
        v->seen = false;
        v->viewed = false;
        v->fresh = false;
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
            v->environment = bakes[row].environment;
            v->capture_size = bakes[row].capture_size;
            v->budget_ms = bakes[row].budget_ms;
            v->threshold.light = bakes[row].light_threshold;
            v->threshold.color = bakes[row].color_threshold;
            if (v->bounces != bakes[row].bounces) {
                v->bounces = bakes[row].bounces;
                if (v->sent && v->probes) {
                    box_bake_begin(v, true);
                }
            }

            if (v->sent && v->capture_size != v->baked_size) {
                v->baked_size = v->capture_size;
                box_bake_mark_all(v);
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
        box_bake_volume_t *v = ecs_vec_get_t(&b->volumes, box_bake_volume_t, i);
        box_bake_environment(world, v);
        box_bake_send(p, v);
    }

    box_bake_changes(world, b);
    for (i = 0; i < ecs_vec_count(&b->volumes); i ++) {
        box_bake_lights(b, ecs_vec_get_t(&b->volumes, box_bake_volume_t, i));
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
    ecs_vec_fini_t(NULL, &b->changes, box_bake_change_t);
    ecs_vec_fini_t(NULL, &b->regions, box_bake_region_t);
    ecs_map_fini(&b->light_map);
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
    ecs_vec_init_t(NULL, &b->changes, box_bake_change_t, 0);
    ecs_vec_init_t(NULL, &b->regions, box_bake_region_t, 0);
    ecs_map_init(&b->light_map, NULL);
    b->next_handle = 1;
    b->bakes = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "RenderBakes" }),
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
    box_bake_observers(world, b);
}

box_render_bake_t* box_bake_state(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractBake");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;

    return s ? s->ctx : NULL;
}

bool box_render_bake_probe(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 position,
    box_render_probe_t *out)
{
    const box_render_bake_t *b = box_bake_state(world);
    const box_bake_volume_t *v = b ? box_bake_find(b, scene) : NULL;
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
    const box_render_bake_t *b = box_bake_state(world);

    return b ? b->pending : 0;
}
