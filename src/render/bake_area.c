/**
 * @file render/bake_area.c
 * @brief Irradiance grids of the areas (VisArea with LightBake) that a mover carries, in the
 * frame of the mover.
 *
 * The grid covers the outline of the area in the frame of its mover without its scale, so its
 * probes, their captures and their lookups move and turn with it: sailing rebakes nothing. The
 * captures draw the carried batches, lit by the point and spot lights the mover carries.
 *
 * Every frame the volume sends the rows from the world to its frame, the index of its area in
 * the shading block of the views and its portals, across which the views blend it with the
 * light beyond them.
 */

#include "bake.h"
#include "lights.h"
#include "../portal/portal.h"
#include <float.h>
#include <math.h>
#include <string.h>

/** Distance in grid units a light of an area grid may move in its frame before it counts as a
 * change, beyond the rounding of a mover far from the origin */
#define BOX_BAKE_AREA_MOVE (1e-3)

/** Part of the distance of the mover from the origin a light may move in the grid frame before
 * it counts as a change, about 32 roundings of a float */
#define BOX_BAKE_AREA_ROUNDING (4e-6)

/** Lists of the lights of the captures of an area grid */
typedef enum box_bake_area_list_t {
    BOX_BAKE_AREA_FIRST,    /**< Mixed and Baked lights, of a pass over a black ambient */
    BOX_BAKE_AREA_LATER,    /**< Mixed lights, of a pass over a previous one */
    BOX_BAKE_AREA_BAKED,    /**< Baked lights, whose direct light the projection adds */
    BOX_BAKE_AREA_LISTS     /**< Number of lists */
} box_bake_area_list_t;

/** A light of the captures of an area grid, in the grid frame */
typedef struct box_bake_area_light_t {
    ecs_entity_t entity;    /**< The light entity */
    uint8_t mode;           /**< Its LightBakeMode */
    box_render_light_t light; /**< The light, its position and direction in the grid frame */
} box_bake_area_light_t;

/** The frame, area and portals of an area grid, as the uniform of its volume holds them */
typedef struct box_bake_area_shade_t {
    float rows[3][4];       /**< World to grid frame rows */
    float area[4];          /**< Index of the area in the index, portals, blend distance */
    float portals[BOX_RENDER_AREA_PORTALS * BOX_RENDER_AREA_PORTAL][4]; /**< The portals */
} box_bake_area_shade_t;

struct box_bake_area_t {
    box_light_list_t lists[BOX_BAKE_AREA_LISTS]; /**< Lights the mover carries, by list */
    ecs_vec_t lights;       /**< box_bake_area_light_t of this frame */
    ecs_vec_t known;        /**< box_bake_area_light_t the probes were baked with */
    bool known_set;         /**< known holds the lights of a frame */
    float ambient[3];       /**< VisArea.ambient of this frame */
    float known_ambient[3]; /**< VisArea.ambient the probes were baked with */
    ecs_vec_t outline;      /**< vec2, corners of the outline in x and z of the grid frame */
    float floor;            /**< Lowest y of the area in the grid frame */
    float top;              /**< Highest y of the area in the grid frame */
    box_bake_area_shade_t shade; /**< Frame, area and portals of this frame */
    box_bake_area_shade_t sent; /**< Those the backend has */
    bool shade_sent;        /**< sent holds what the backend has */
    int32_t builds;         /**< PortalIndex builds the portals were taken at, -1 for none */
    int32_t index;          /**< Index of the area in the portal index, -1 when not in it */
};

box_bake_volume_t* box_bake_find_area(
    const box_render_bake_t *b,
    ecs_entity_t area)
{
    box_bake_volume_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&b->volumes) && !result; i ++) {
        box_bake_volume_t *v = ecs_vec_get_t(&b->volumes, box_bake_volume_t, i);
        if (v->area == area) {
            result = v;
        }
    }

    return result;
}

/** Find the area grid of an area, adding one when it has none.
 *
 * @param b The state
 * @param scene Scene of the area
 * @param area Entity with the VisArea
 * @return The volume, valid until the next volume is added
 */
static box_bake_volume_t* box_bake_area_ensure(
    box_render_bake_t *b,
    ecs_entity_t scene,
    ecs_entity_t area)
{
    box_bake_volume_t *result = box_bake_find_area(b, area);
    int32_t i;
    if (!result) {
        result = box_bake_volume_add(b, scene);
        result->area = area;
        result->area_data = ecs_os_calloc_t(box_bake_area_t);
        for (i = 0; i < BOX_BAKE_AREA_LISTS; i ++) {
            box_lights_list_init(&result->area_data->lists[i]);
        }

        ecs_vec_init_t(NULL, &result->area_data->lights, box_bake_area_light_t, 0);
        ecs_vec_init_t(NULL, &result->area_data->known, box_bake_area_light_t, 0);
        ecs_vec_init_t(NULL, &result->area_data->outline, vec2, 0);
        result->area_data->builds = -1;
        result->area_data->index = -1;
    }

    return result;
}

void box_bake_area_fini(
    box_render_bake_t *b,
    box_bake_volume_t *v,
    box_render_packet_t *p)
{
    box_bake_area_t *a = v->area_data;
    box_render_lights_t *l = ECS_CONST_CAST(box_render_lights_t*, b->lights_state);
    int32_t i;
    if (!a) {
        goto done;
    }

    for (i = 0; i < BOX_BAKE_AREA_LISTS; i ++) {
        if (p && l) {
            box_lights_release(l, &a->lists[i], p);
        }

        box_lights_list_fini(&a->lists[i]);
    }

    ecs_vec_fini_t(NULL, &a->lights, box_bake_area_light_t);
    ecs_vec_fini_t(NULL, &a->known, box_bake_area_light_t);
    ecs_vec_fini_t(NULL, &a->outline, vec2);
    ecs_os_free(a);
    v->area_data = NULL;
done:
    return;
}

/** Get the transform of an entity relative to the mover that carries it, composed from the
 * local transforms in between, so it does not change while only the mover moves.
 *
 * @param world The world
 * @param e The entity
 * @param mover The mover, an ancestor of the entity or the entity itself
 * @param out Receives the transform, times the scale of the mover
 * @return Whether the mover is the entity or one of its ancestors
 */
static bool box_bake_area_relative(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t mover,
    mat4 out)
{
    const Position origin = {0, 0, 0};
    ecs_entity_t x = e;
    bool result = false;
    glm_mat4_identity(out);
    while (x && x != mover) {
        const Position *pos = ecs_get(world, x, Position);
        mat4 local;
        box_transform_local(pos ? pos : &origin, ecs_get(world, x, Rotation),
            ecs_get(world, x, Scale), local);
        glm_mat4_mul(local, out, out);
        x = ecs_get_parent(world, x);
    }

    result = x != 0 && x == mover;
    if (result) {
        const Scale *s = ecs_get(world, mover, Scale);
        if (s) {
            mat4 scale = GLM_MAT4_IDENTITY_INIT;
            scale[0][0] = s->x;
            scale[1][1] = s->y;
            scale[2][2] = s->z;
            glm_mat4_mul(scale, out, out);
        }
    }

    return result;
}

/** Take the frame of an area grid in this frame: the world transform of its mover without its
 * scale, and the rows from the world to that frame.
 *
 * @param world The world
 * @param v The volume, its mover set
 */
static void box_bake_area_frame(
    const ecs_world_t *world,
    box_bake_volume_t *v)
{
    WorldTransform buf;
    const WorldTransform *t = v->mover ? box_transform_world(world, v->mover, &buf) : NULL;
    int32_t c, r;
    glm_mat4_identity(v->frame);
    glm_mat4_identity(v->rows);
    if (!t) {
        goto done;
    }

    for (c = 0; c < 3; c ++) {
        float len = glm_vec3_norm(ECS_CONST_CAST(float*, t->m[c]));
        for (r = 0; r < 3; r ++) {
            v->frame[c][r] = len > 0 ? t->m[c][r] / len : (float)(c == r);
        }
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, t->m[3]), v->frame[3]);
    for (c = 0; c < 3; c ++) {
        double d = 0;
        for (r = 0; r < 3; r ++) {
            v->rows[r][c] = v->frame[c][r];
            d += (double)v->frame[c][r] * (double)v->frame[3][r];
        }

        v->rows[3][c] = (float)-d;
    }

done:
    return;
}

/** Check whether a point of the grid frame lies in the outline of an area, in x and z.
 *
 * @param a The state of the area grid
 * @param x X of the point
 * @param z Z of the point
 * @return Whether an odd number of edges of the outline crosses the ray toward +x
 */
static bool box_bake_area_inside(
    const box_bake_area_t *a,
    float x,
    float z)
{
    const vec2 *pts = ecs_vec_first_t(&a->outline, vec2);
    int32_t i, j, count = ecs_vec_count(&a->outline);
    bool result = false;
    for (i = 0, j = count - 1; i < count; j = i ++) {
        if ((pts[i][1] > z) != (pts[j][1] > z)) {
            float cross = pts[j][0] + (z - pts[j][1]) * (pts[i][0] - pts[j][0]) /
                (pts[i][1] - pts[j][1]);
            result = x < cross ? !result : result;
        }
    }

    return result;
}

void box_bake_area_validate(
    box_bake_volume_t *v)
{
    const box_bake_area_t *a = v->area_data;
    int32_t words = (v->probes + 31) / 32;
    int32_t i;
    ecs_vec_set_count_t(NULL, &v->valid, uint32_t, words);
    uint32_t *valid = ecs_vec_first(&v->valid);
    ecs_os_memset(valid, 0, words * ECS_SIZEOF(uint32_t));
    v->valid_count = 0;
    for (i = 0; a && i < v->probes; i ++) {
        vec3 pos;
        box_bake_probe_position(v, (uint32_t)i, pos);
        if (pos[1] > a->floor && pos[1] < a->top && box_bake_area_inside(a, pos[0], pos[2])) {
            valid[i >> 5] |= 1u << (i & 31);
            v->valid_count ++;
        }
    }
}

/** Take the outline of an area in the grid frame and its bounds.
 *
 * @param world The world
 * @param v The volume, its mover set
 * @param out_min Receives the minimum corner of the bounds
 * @param out_max Receives the maximum corner of the bounds
 * @return Whether the outline or its heights changed
 */
static bool box_bake_area_outline(
    const ecs_world_t *world,
    box_bake_volume_t *v,
    vec3 out_min,
    vec3 out_max)
{
    box_bake_area_t *a = v->area_data;
    const VisArea *area = ecs_get(world, v->area, VisArea);
    int32_t i, count = area ? ecs_vec_count(&area->shape) : 0;
    const vec2 *shape = area ? ecs_vec_first_t(&area->shape, vec2) : NULL;
    float floor = FLT_MAX, top = -FLT_MAX;
    bool result = count != ecs_vec_count(&a->outline);
    mat4 rel;
    glm_vec3_fill(out_min, FLT_MAX);
    glm_vec3_fill(out_max, -FLT_MAX);
    if (!count || !box_bake_area_relative(world, v->area, v->mover, rel)) {
        result = ecs_vec_count(&a->outline) != 0;
        ecs_vec_clear(&a->outline);
        glm_vec3_zero(out_min);
        glm_vec3_zero(out_max);
        goto done;
    }

    ecs_vec_set_count_t(NULL, &a->outline, vec2, count);
    vec2 *pts = ecs_vec_first_t(&a->outline, vec2);
    for (i = 0; i < count; i ++) {
        vec3 low, high;
        glm_mat4_mulv3(rel, (vec3){ shape[i][0], area->y, shape[i][1] }, 1.0f, low);
        glm_mat4_mulv3(rel, (vec3){ shape[i][0], area->y + area->height, shape[i][1] }, 1.0f,
            high);
        result |= pts[i][0] != low[0] || pts[i][1] != low[2];
        pts[i][0] = low[0];
        pts[i][1] = low[2];
        glm_vec3_minv(out_min, low, out_min);
        glm_vec3_minv(out_min, high, out_min);
        glm_vec3_maxv(out_max, low, out_max);
        glm_vec3_maxv(out_max, high, out_max);
        floor = glm_min(floor, glm_min(low[1], high[1]));
        top = glm_max(top, glm_max(low[1], high[1]));
    }

    result |= floor != a->floor || top != a->top;
    a->floor = floor;
    a->top = top;
done:
    return result;
}

/** Take the portals of an area from the portal index into the shade of its grid, in the grid
 * frame from their rest, again only when the index was built again.
 *
 * @param world The world
 * @param v The volume
 */
static void box_bake_area_portals(
    ecs_world_t *world,
    box_bake_volume_t *v)
{
    box_bake_area_t *a = v->area_data;
    const box_portal_index_t *index = box_portal_index(world);
    int32_t i, k, count = 0, found = -1;
    if (!index) {
        a->index = -1;
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&index->areas) && found < 0; i ++) {
        if (ecs_vec_get_t(&index->areas, box_portal_area_t, i)->entity == v->area) {
            found = i;
        }
    }

    if (found == a->index && index->builds == a->builds) {
        goto done;
    }

    a->index = found;
    a->builds = index->builds;
    ecs_os_memset_n(a->shade.portals, 0, float, BOX_RENDER_AREA_PORTALS *
        BOX_RENDER_AREA_PORTAL * 4);
    const box_portal_area_t *area = found >= 0
        ? ecs_vec_get_t(&index->areas, box_portal_area_t, found)
        : NULL;
    const box_portal_mover_t *mover = area && area->mover >= 0 &&
        area->mover < ecs_vec_count(&index->movers)
        ? ecs_vec_get_t(&index->movers, box_portal_mover_t, area->mover)
        : NULL;
    const int32_t *links = ecs_vec_first_t(&index->links, int32_t);
    mat4 rest = GLM_MAT4_IDENTITY_INIT, inv;
    if (mover) {
        for (k = 0; k < 3; k ++) {
            glm_vec3_normalize_to(ECS_CONST_CAST(float*, mover->rest[k]), rest[k]);
        }

        glm_vec3_copy(ECS_CONST_CAST(float*, mover->rest[3]), rest[3]);
    }

    glm_mat4_inv(rest, inv);
    for (k = 0; mover && k < area->link_count && count < BOX_RENDER_AREA_PORTALS; k ++) {
        int32_t at = links[area->link_first + k];
        const box_portal_portal_t *p = ecs_vec_get_t(&index->rest_portals,
            box_portal_portal_t, at);
        float (*out)[4] = &a->shade.portals[count * BOX_RENDER_AREA_PORTAL];
        bool back = p->back == found;
        vec3 normal;
        if (p->mover != area->mover) {
            continue;
        }

        glm_vec3_scale(ECS_CONST_CAST(float*, p->normal), back ? 1.0f : -1.0f, normal);
        glm_mat4_mulv3(inv, ECS_CONST_CAST(float*, p->center), 1.0f, out[0]);
        out[0][3] = (float)(back ? p->front : p->back);
        glm_mat4_mulv3(inv, ECS_CONST_CAST(float*, p->right), 0.0f, out[1]);
        glm_vec3_scale(out[1], p->width * 0.5f, out[1]);
        glm_mat4_mulv3(inv, ECS_CONST_CAST(float*, p->up), 0.0f, out[2]);
        glm_vec3_scale(out[2], p->height * 0.5f, out[2]);
        glm_mat4_mulv3(inv, normal, 0.0f, out[3]);
        count ++;
    }

    a->shade.area[1] = (float)count;
done:
    a->shade.area[0] = (float)a->index;
}

/** Write the rows of the frame of an area grid and its blend distance into its shade.
 *
 * @param v The volume
 */
static void box_bake_area_shade(
    box_bake_volume_t *v)
{
    box_bake_area_t *a = v->area_data;
    int32_t r;
    for (r = 0; r < 3; r ++) {
        a->shade.rows[r][0] = v->rows[0][r];
        a->shade.rows[r][1] = v->rows[1][r];
        a->shade.rows[r][2] = v->rows[2][r];
        a->shade.rows[r][3] = v->rows[3][r];
    }

    a->shade.area[2] = v->grid.spacing;
}

void box_bake_area_update(
    ecs_world_t *world,
    box_render_bake_t *b,
    box_render_packet_t *p,
    ecs_entity_t scene,
    ecs_entity_t area,
    const LightBake *bake)
{
    box_bake_volume_t *v = box_bake_area_ensure(b, scene, area);
    box_bake_grid_t grid = {0};
    vec3 min, max;
    int32_t probes = 0;
    if (v->seen) {
        goto done;
    }

    v->seen = true;
    v->entity = area;
    v->scene = scene;
    v->environment = 0;
    v->capture_size = bake->capture_size;
    v->budget_ms = bake->budget_ms;
    v->compute = bake->compute;
    v->threshold.light = bake->light_threshold;
    v->threshold.color = bake->color_threshold;
    v->mover = box_portal_mover_of(world, area);
    if (v->bounces != bake->bounces) {
        v->bounces = bake->bounces;
        if (v->sent && v->probes) {
            box_bake_mark_all(v);
        }
    }

    if (v->sent && v->capture_size != v->baked_size) {
        v->baked_size = v->capture_size;
        box_bake_mark_all(v);
    }

    box_bake_area_frame(world, v);
    bool outline = box_bake_area_outline(world, v, min, max);
    if (v->mover && ecs_vec_count(&v->area_data->outline)) {
        probes = box_bake_place(min, max, bake->probe_spacing, 0.0f, &grid);
    }

    box_bake_area_portals(world, v);
    box_bake_area_shade(v);
    if (!v->placed || outline || v->probe_spacing != bake->probe_spacing) {
        bool same = probes == v->probes && !ecs_os_memcmp_t(&grid, &v->grid, box_bake_grid_t);
        v->placed = true;
        v->probe_spacing = bake->probe_spacing;
        v->bounds_padding = 0;
        glm_vec3_copy(min, v->min);
        glm_vec3_copy(max, v->max);
        box_bake_regrid(world, b, p, v, &grid, probes);
        if (same && outline && v->probes) {
            box_bake_area_validate(v);
            v->sent = false;
        }

        box_bake_area_shade(v);
    }

done:
    return;
}

/** Collect the lights an area grid is captured with from the capture lights of its scene: the
 * point and spot lights its mover carries, and the ambient of the area as an even sky.
 *
 * @param world The world
 * @param b The state
 * @param v The volume
 * @param rv A view of its scene
 * @param p The packet, receives the light buffers
 */
static void box_bake_area_capture_lights(
    ecs_world_t *world,
    box_render_bake_t *b,
    box_bake_volume_t *v,
    const box_render_view_t *rv,
    box_render_packet_t *p)
{
    box_bake_area_t *a = v->area_data;
    box_render_lights_t *l = ECS_CONST_CAST(box_render_lights_t*, b->lights_state);
    const VisArea *area = ecs_get(world, v->area, VisArea);
    box_bake_lights_t scene;
    box_render_view_lights_t first, later;
    int32_t i, k;
    box_bake_scene_lights(b->lights_state, v->scene, &rv->lights, &scene);
    for (k = 0; k < BOX_BAKE_AREA_LISTS; k ++) {
        box_lights_list_clear(&a->lists[k]);
    }

    ecs_vec_clear(&a->lights);
    for (i = 0; i < scene.count; i ++) {
        const box_render_light_t *light = &scene.lights[i];
        uint8_t mode = scene.modes[i];
        if (!(light->range > 0) || box_portal_mover_of(world, scene.entities[i]) != v->mover) {
            continue;
        }

        box_lights_append(&a->lists[BOX_BAKE_AREA_FIRST], scene.entities[i], mode, light);
        if (mode == LightBakeModeMixed) {
            box_lights_append(&a->lists[BOX_BAKE_AREA_LATER], scene.entities[i], mode, light);
        } else if (mode == LightBakeModeBaked) {
            box_lights_append(&a->lists[BOX_BAKE_AREA_BAKED], scene.entities[i], mode, light);
        }

        box_bake_area_light_t *out = ecs_vec_append_t(NULL, &a->lights, box_bake_area_light_t);
        out->entity = scene.entities[i];
        out->mode = mode;
        out->light = *light;
        glm_mat4_mulv3(v->rows, ECS_CONST_CAST(float*, light->position), 1.0f,
            out->light.position);
        if (light->spot_scale > 0) {
            glm_mat4_mulv3(v->rows, ECS_CONST_CAST(float*, light->direction), 0.0f,
                out->light.direction);
        }
    }

    if (l) {
        box_lights_send_list(l, &a->lists[BOX_BAKE_AREA_FIRST], p);
        box_lights_send_list(l, &a->lists[BOX_BAKE_AREA_LATER], p);
        if (ecs_vec_count(&a->lists[BOX_BAKE_AREA_BAKED].lights)) {
            box_lights_send_list(l, &a->lists[BOX_BAKE_AREA_BAKED], p);
        } else {
            box_lights_release(l, &a->lists[BOX_BAKE_AREA_BAKED], p);
        }
    }

    glm_vec3_zero(a->ambient);
    if (area) {
        glm_vec3_copy(ECS_CONST_CAST(float*, area->ambient), a->ambient);
    }

    ecs_os_memset_t(&first, 0, box_render_view_lights_t);
    first.enabled = true;
    glm_vec3_copy(a->ambient, first.ambient_sky);
    glm_vec3_copy(a->ambient, first.ambient_ground);
    later = first;
    box_lights_list_view(&a->lists[BOX_BAKE_AREA_FIRST], &first);
    box_lights_list_view(&a->lists[BOX_BAKE_AREA_LATER], &later);
    ecs_os_memset_t(&v->lights, 0, box_bake_lights_t);
    v->lights.first = first;
    v->lights.later = later;
    v->lights.baked = a->lists[BOX_BAKE_AREA_BAKED].buffer;
    v->lights.baked_count = (uint32_t)ecs_vec_count(&a->lists[BOX_BAKE_AREA_BAKED].lights);
    v->lights.count = ecs_vec_count(&a->lists[BOX_BAKE_AREA_FIRST].lights);
    v->lights.lights = ecs_vec_first(&a->lists[BOX_BAKE_AREA_FIRST].lights);
    v->lights.entities = ecs_vec_first(&a->lists[BOX_BAKE_AREA_FIRST].entities);
    v->lights.modes = ecs_vec_first(&a->lists[BOX_BAKE_AREA_FIRST].modes);
    box_bake_ambient(&first, v->ambient);
}

/** Get the distance from a point to a box, 0 inside it.
 *
 * @param p The point
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @return The distance
 */
static float box_bake_area_distance(
    const vec3 p,
    const vec3 min,
    const vec3 max)
{
    float d2 = 0;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        float d = glm_max(glm_max(min[i] - p[i], p[i] - max[i]), 0.0f);
        d2 += d * d;
    }

    return sqrtf(d2);
}

void box_bake_area_views(
    ecs_world_t *world,
    box_render_bake_t *b,
    box_render_packet_t *p)
{
    int32_t i, j, count = ecs_vec_count(&p->views);
    for (i = 0; i < count; i ++) {
        box_render_view_t *rv = ecs_vec_get_t(&p->views, box_render_view_t, i);
        const box_bake_volume_t *best = NULL;
        float nearest = FLT_MAX;
        for (j = 0; j < ecs_vec_count(&b->volumes); j ++) {
            box_bake_volume_t *v = ecs_vec_get_t(&b->volumes, box_bake_volume_t, j);
            vec3 camera;
            if (!v->area || v->scene != rv->scene) {
                continue;
            }

            if (!v->viewed) {
                box_bake_area_capture_lights(world, b, v, rv, p);
                v->viewed = true;
            }

            glm_mat4_mulv3(v->rows, rv->camera_transform[3], 1.0f, camera);
            glm_vec3_copy(camera, *ecs_vec_append_t(NULL, &v->cameras, vec3));
            float d = box_bake_area_distance(camera, v->min, v->max);
            if (v->handle && v->probes && d < nearest) {
                nearest = d;
                best = v;
            }
        }

        if (best && rv->lights.enabled && (rv->features & BOX_RENDER_FEATURE_BAKED)) {
            rv->area_volume = best->handle;
        }
    }
}

void box_bake_area_desc(
    box_bake_volume_t *v,
    box_render_volume_desc_t *desc)
{
    box_bake_area_t *a = v->area_data;
    desc->turned = 1;
    ecs_os_memcpy(desc->rows, a->shade.rows, ECS_SIZEOF(a->shade.rows));
    ecs_os_memcpy(desc->area, a->shade.area, ECS_SIZEOF(a->shade.area));
    ecs_os_memcpy(desc->portals, a->shade.portals, ECS_SIZEOF(a->shade.portals));
    if (desc->op == BOX_RENDER_VOLUME_FILL || desc->op == BOX_RENDER_VOLUME_RESIZE) {
        a->sent = a->shade;
        a->shade_sent = true;
    }
}

void box_bake_area_frames(
    box_render_bake_t *b,
    box_render_packet_t *p)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&b->volumes); i ++) {
        box_bake_volume_t *v = ecs_vec_get_t(&b->volumes, box_bake_volume_t, i);
        box_bake_area_t *a = v->area_data;
        if (!a || !v->handle || !v->sent || (a->shade_sent &&
            !ecs_os_memcmp_t(&a->shade, &a->sent, box_bake_area_shade_t)))
        {
            continue;
        }

        box_render_volume_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_VOLUME,
            BOX_RENDER_UPDATE, v->handle, (uint32_t)sizeof(box_render_volume_desc_t));
        ecs_os_memset_t(desc, 0, box_render_volume_desc_t);
        desc->op = BOX_RENDER_VOLUME_FRAME;
        box_bake_area_desc(v, desc);
        a->sent = a->shade;
        a->shade_sent = true;
    }
}

/** Check whether two values of lights are the same within the rounding of the scale of the
 * world transforms of a mover that turns.
 *
 * @param a The first value
 * @param b The second value
 * @return Whether they differ by at most BOX_BAKE_AREA_MOVE of the larger one
 */
static bool box_bake_area_close(
    float a,
    float b)
{
    return fabsf(a - b) <= (float)BOX_BAKE_AREA_MOVE * glm_max(fabsf(a), fabsf(b));
}

/** Check whether two lights of an area grid in its frame are the same, within the rounding of
 * a mover at a distance from the origin.
 *
 * @param x The first light
 * @param y The second light
 * @param tolerance Distance their positions may differ by
 * @return Whether they are the same
 */
static bool box_bake_area_light_same(
    const box_bake_area_light_t *x,
    const box_bake_area_light_t *y,
    float tolerance)
{
    const box_render_light_t *a = &x->light, *b = &y->light;
    bool result = x->entity == y->entity && x->mode == y->mode &&
        box_bake_area_close(a->range, b->range) &&
        box_bake_area_close(a->spot_scale, b->spot_scale) &&
        box_bake_area_close(a->spot_offset, b->spot_offset);
    int32_t i;
    for (i = 0; result && i < 3; i ++) {
        result = fabsf(a->position[i] - b->position[i]) <= tolerance &&
            fabsf(a->direction[i] - b->direction[i]) <= (float)BOX_BAKE_AREA_MOVE &&
            box_bake_area_close(a->color[i], b->color[i]);
    }

    return result;
}

void box_bake_area_lights(
    box_bake_volume_t *v)
{
    box_bake_area_t *a = v->area_data;
    int32_t i, count = ecs_vec_count(&a->lights);
    float tolerance = (float)(BOX_BAKE_AREA_MOVE +
        BOX_BAKE_AREA_ROUNDING * glm_vec3_norm(v->frame[3]));
    bool same = count == ecs_vec_count(&a->known) &&
        !ecs_os_memcmp(a->ambient, a->known_ambient, ECS_SIZEOF(a->ambient));
    if (!v->viewed || !v->sent || !v->probes) {
        goto done;
    }

    for (i = 0; same && i < count; i ++) {
        same = box_bake_area_light_same(ecs_vec_get_t(&a->lights, box_bake_area_light_t, i),
            ecs_vec_get_t(&a->known, box_bake_area_light_t, i), tolerance);
    }

    if (same) {
        goto done;
    }

    if (a->known_set) {
        box_bake_mark_all(v);
    }

    ecs_vec_set_count_t(NULL, &a->known, box_bake_area_light_t, count);
    if (count) {
        ecs_os_memcpy_n(ecs_vec_first(&a->known), ecs_vec_first(&a->lights),
            box_bake_area_light_t, count);
    }

    glm_vec3_copy(a->ambient, a->known_ambient);
    a->known_set = true;
done:
    return;
}
