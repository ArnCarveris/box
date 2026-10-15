/**
 * @file render/bake_change.c
 * @brief Changes of baked scenes that rebake the probes they can reach.
 *
 * Static shapes that move, change their look or leave the captures queue the probes around the
 * bounds they had and have, padded once per pass (as Lumen invalidates its surface cache by the
 * bounds of changed primitives). Lights are compared with the last frame per scene.
 */

#include "bake.h"
#include "../spatial/spatial.h"
#include "../geometry/mesh_cache.h"
#include <float.h>
#include <string.h>

/** Queue a probe of a volume for the next bake when it is valid and not queued yet.
 *
 * @param v The volume
 * @param probe Index of the probe
 */
static void box_bake_mark(
    box_bake_volume_t *v,
    uint32_t probe)
{
    uint8_t *flag = ecs_vec_get_t(&v->flags, uint8_t, (int32_t)probe);
    if (!*flag && box_bake_valid(v, probe)) {
        *flag = 1;
        *ecs_vec_append_t(NULL, &v->dirty, uint32_t) = probe;
    }
}

/** Queue every valid probe of a volume for the next bake.
 *
 * @param v The volume
 */
void box_bake_mark_all(
    box_bake_volume_t *v)
{
    int32_t i;
    for (i = 0; i < v->probes; i ++) {
        box_bake_mark(v, (uint32_t)i);
    }
}

/** Get the probes of the grid of a volume inside a box, per axis.
 *
 * @param v The volume
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param lo Receives the first probe per axis
 * @param hi Receives the last probe per axis
 * @return false when no probe is inside
 */
static bool box_bake_cells(
    const box_bake_volume_t *v,
    const vec3 min,
    const vec3 max,
    int32_t lo[3],
    int32_t hi[3])
{
    const box_bake_grid_t *g = &v->grid;
    bool result = v->probes > 0;
    int32_t i;
    for (i = 0; result && i < 3; i ++) {
        float a = ceilf((min[i] - g->origin[i]) / g->spacing);
        float c = floorf((max[i] - g->origin[i]) / g->spacing);
        a = glm_max(a, 0.0f);
        c = glm_min(c, (float)(g->size[i] - 1));
        result = a <= c;
        lo[i] = (int32_t)a;
        hi[i] = (int32_t)c;
    }

    return result;
}

/** Queue the probes of a volume inside a box, or inside the sphere the box holds.
 *
 * @param v The volume
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param sphere Whether only the probes inside the sphere in the box count
 */
static void box_bake_mark_box(
    box_bake_volume_t *v,
    const vec3 min,
    const vec3 max,
    bool sphere)
{
    int32_t lo[3], hi[3], x, y, z;
    vec3 center, pos;
    float radius = (max[0] - min[0]) * 0.5f;
    glm_vec3_center(ECS_CONST_CAST(float*, min), ECS_CONST_CAST(float*, max), center);
    if (!box_bake_cells(v, min, max, lo, hi)) {
        goto done;
    }

    for (z = lo[2]; z <= hi[2]; z ++) {
        for (y = lo[1]; y <= hi[1]; y ++) {
            for (x = lo[0]; x <= hi[0]; x ++) {
                uint32_t probe = (uint32_t)(x + (y + z * v->grid.size[1]) * v->grid.size[0]);
                box_bake_probe_position(v, probe, pos);
                if (!sphere || glm_vec3_distance2(pos, center) <= radius * radius) {
                    box_bake_mark(v, probe);
                }
            }
        }
    }

done:
    return;
}

/** Check again whether the probes of a volume inside a box lie inside static geometry.
 *
 * A probe that became invalid is hidden from the views at once. One that became valid is
 * shown when the bake that captures it is shown, so the views never see its old values.
 *
 * @param world The world
 * @param b The state
 * @param v The volume
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 */
static void box_bake_revalidate(
    const ecs_world_t *world,
    box_render_bake_t *b,
    box_bake_volume_t *v,
    const vec3 min,
    const vec3 max)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    uint32_t *valid = ecs_vec_first(&v->valid);
    uint32_t *shown = ecs_vec_first(&v->shown);
    int32_t lo[3], hi[3], x, y, z;
    if (!box_bake_cells(v, min, max, lo, hi)) {
        goto done;
    }

    for (z = lo[2]; z <= hi[2]; z ++) {
        for (y = lo[1]; y <= hi[1]; y ++) {
            for (x = lo[0]; x <= hi[0]; x ++) {
                uint32_t probe = (uint32_t)(x + (y + z * v->grid.size[1]) * v->grid.size[0]);
                uint32_t bit = 1u << (probe & 31);
                vec3 pos;
                box_bake_probe_position(v, probe, pos);
                bool now = !box_bake_occupied(world, b, cache, v->scene, pos);
                if (now == ((valid[probe >> 5] & bit) != 0)) {
                    continue;
                }

                v->valid_count += now ? 1 : -1;
                valid[probe >> 5] ^= bit;
                if (!now && (shown[probe >> 5] & bit)) {
                    shown[probe >> 5] &= ~bit;
                    v->mask = true;
                }
            }
        }
    }

done:
    return;
}

/** Get the world bounds of a shape: its unit mesh scaled by the shape, placed by its transform.
 *
 * @param world The world
 * @param e The shape
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 * @return false when the entity has no shape with a mesh
 */
static bool box_bake_shape_bounds(
    const ecs_world_t *world,
    ecs_entity_t e,
    vec3 min,
    vec3 max)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    const box_mesh_t *mesh = cache && shape && shape->mesh ?
        box_mesh_cache_get(cache, shape->mesh) : NULL;
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    bool result = mesh && mesh->vertex_count;
    int32_t c, i;
    if (!result) {
        goto done;
    }

    glm_vec3_fill(min, FLT_MAX);
    glm_vec3_fill(max, -FLT_MAX);
    for (c = 0; c < 8; c ++) {
        vec4 corner = {
            ((c & 1) ? mesh->max[0] : mesh->min[0]) * shape->scale[0],
            ((c & 2) ? mesh->max[1] : mesh->min[1]) * shape->scale[1],
            ((c & 4) ? mesh->max[2] : mesh->min[2]) * shape->scale[2],
            1.0f
        };
        if (t) {
            glm_mat4_mulv(ECS_CONST_CAST(vec4*, t->m), corner, corner);
        }

        for (i = 0; i < 3; i ++) {
            min[i] = glm_min(min[i], corner[i]);
            max[i] = glm_max(max[i], corner[i]);
        }
    }

done:
    return result;
}

/** Remember a shape that changed with the bounds it had, which the spatial index still holds
 * until it is updated in OnStore, or which a removed shape still has.
 *
 * @param world The world
 * @param b The state
 * @param e The shape
 * @param removed Whether the shape is being removed, so the index may have lost it already
 */
static void box_bake_record(
    const ecs_world_t *world,
    box_render_bake_t *b,
    ecs_entity_t e,
    bool removed)
{
    box_bake_change_t *c = ecs_vec_append_t(NULL, &b->changes, box_bake_change_t);
    c->entity = e;
    c->old = box_spatial_bounds(world, e, &c->scene, c->min, c->max);
    if (!c->old && removed && ecs_has(world, e, WorldTransform) &&
        box_bake_shape_bounds(world, e, c->min, c->max))
    {
        c->scene = box_render_scene_of(world, e);
        c->old = true;
    }
}

/** Remember the static shapes whose captured look changed.
 *
 * A static shape that becomes dynamic, manual or NoBake leaves the captures and one that stops
 * being one enters them, so those changes count whatever the shape is now.
 *
 * @param it The iterator of the observer
 */
static void BakeOnChange(
    ecs_iter_t *it)
{
    box_render_bake_t *b = it->ctx;
    ecs_id_t id = it->event_id;
    bool flag = id == DynamicTransform || id == ManualTransform || id == NoBake;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!ecs_has(it->world, e, ShapeMesh)) {
            continue;
        }

        if (!flag && (box_transform_dynamic(it->world, it->table) ||
            ecs_has_id(it->world, e, ManualTransform) || ecs_has_id(it->world, e, NoBake)))
        {
            continue;
        }

        box_bake_record(it->world, b, e, it->event == EcsOnRemove);
    }

done:
    return;
}

/** Remember the static shapes whose transform TransformStatic changed.
 *
 * @param world The world
 * @param entities The entities
 * @param count Number of entities
 * @param ctx The state
 */
static void box_bake_on_moved(
    ecs_world_t *world,
    const ecs_entity_t *entities,
    int32_t count,
    void *ctx)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (ecs_has(world, entities[i], ShapeMesh) && !ecs_has_id(world, entities[i], NoBake)) {
            box_bake_record(world, ctx, entities[i], false);
        }
    }
}

/** Queue the probes a changed region of a scene can reach: its bounds padded by the probe
 * spacing once per pass, after checking again which probes in it lie inside geometry.
 *
 * @param world The world
 * @param b The state
 * @param scene The scene of the region
 * @param min Minimum corner of the region
 * @param max Maximum corner of the region
 */
static void box_bake_region(
    const ecs_world_t *world,
    box_render_bake_t *b,
    ecs_entity_t scene,
    const vec3 min,
    const vec3 max)
{
    box_bake_volume_t *v = box_bake_find(b, scene);
    vec3 lo, hi;
    if (!v || !v->sent || !v->probes || v->fresh) {
        goto done;
    }

    int32_t bounces = v->bounces >= 0 && v->bounces <= BOX_BAKE_BOUNCES_MAX ? v->bounces : 0;
    float pad = v->grid.spacing * (float)(1 + bounces);
    box_bake_revalidate(world, b, v, min, max);
    glm_vec3_subs(ECS_CONST_CAST(float*, min), pad, lo);
    glm_vec3_adds(ECS_CONST_CAST(float*, max), pad, hi);
    box_bake_mark_box(v, lo, hi, false);
done:
    return;
}

/** Queue the probes the shapes that changed since the last extract can reach, around the
 * bounds they had and the bounds they have now.
 *
 * @param world The world
 * @param b The state
 */
void box_bake_changes(
    ecs_world_t *world,
    box_render_bake_t *b)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&b->changes); i ++) {
        const box_bake_change_t *c = ecs_vec_get_t(&b->changes, box_bake_change_t, i);
        ecs_entity_t e = c->entity, scene = 0;
        vec3 min, max;
        if (c->old) {
            box_bake_region(world, b, c->scene, c->min, c->max);
        }

        if (ecs_is_alive(world, e) && !ecs_has_id(world, e, NoBake) &&
            !ecs_has_id(world, e, ManualTransform) &&
            !box_transform_dynamic(world, ecs_get_table(world, e)) &&
            box_spatial_bounds(world, e, &scene, min, max))
        {
            box_bake_region(world, b, scene, min, max);
        }
    }

    ecs_vec_clear(&b->changes);
}

/** Check whether a light is in a list of lights.
 *
 * @param list The lights
 * @param count Number of lights
 * @param light The light
 * @return Whether an equal light is in the list
 */
static bool box_bake_has_light(
    const box_render_light_t *list,
    int32_t count,
    const box_render_light_t *light)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < count && !result; i ++) {
        result = !ecs_os_memcmp_t(&list[i], light, box_render_light_t);
    }

    return result;
}

/** Queue the probes within range of the point and spot lights of one list that the other
 * list lacks.
 *
 * @param v The volume
 * @param from The lights that may be missing from the other list
 * @param from_count Number of them
 * @param other The other list
 * @param other_count Number of lights in it
 */
static void box_bake_lights_missing(
    box_bake_volume_t *v,
    const box_render_light_t *from,
    int32_t from_count,
    const box_render_light_t *other,
    int32_t other_count)
{
    int32_t i;
    for (i = 0; i < from_count; i ++) {
        const box_render_light_t *l = &from[i];
        if (l->range > 0 && !box_bake_has_light(other, other_count, l)) {
            vec3 lo, hi;
            glm_vec3_subs(ECS_CONST_CAST(float*, l->position), l->range, lo);
            glm_vec3_adds(ECS_CONST_CAST(float*, l->position), l->range, hi);
            box_bake_mark_box(v, lo, hi, true);
        }
    }
}

/** Queue the probes a change of the lights of the scene of a volume can reach since the last
 * frame: a point or spot light the probes within its range, a directional light or the
 * default sun the whole scene.
 *
 * @param world The world
 * @param v The volume
 */
void box_bake_lights(
    const ecs_world_t *world,
    box_bake_volume_t *v)
{
    int32_t count = 0, known = ecs_vec_count(&v->light_list);
    const box_render_light_t *lights = box_render_lights_of(world, v->scene, &count);
    const box_render_light_t *before = ecs_vec_first(&v->light_list);
    const box_render_view_lights_t *a = &v->baked_lights, *c = &v->lights;
    if (!v->viewed || !v->sent || !v->probes) {
        goto done;
    }

    bool sun = a->directional != c->directional || (a->buffer == 0) != (c->buffer == 0) ||
        memcmp(a->sun, c->sun, sizeof(a->sun)) || memcmp(a->sun_color, c->sun_color,
        sizeof(a->sun_color));
    bool same = count == known && (!count ||
        !ecs_os_memcmp(lights, before, count * ECS_SIZEOF(box_render_light_t)));
    if (v->lights_known && (sun || !same)) {
        if (sun) {
            box_bake_mark_all(v);
        } else {
            box_bake_lights_missing(v, before, known, lights, count);
            box_bake_lights_missing(v, lights, count, before, known);
        }
    }

    ecs_vec_set_count_t(NULL, &v->light_list, box_render_light_t, count);
    if (count) {
        ecs_os_memcpy_n(ecs_vec_first(&v->light_list), lights, box_render_light_t, count);
    }

    v->baked_lights = v->lights;
    v->lights_known = true;
done:
    return;
}

/** Register the observers of the changes of static shapes that rebake the probes near them.
 *
 * @param world The world
 * @param b The state
 */
void box_bake_observers(
    ecs_world_t *world,
    box_render_bake_t *b)
{
    ecs_entity_t looks[11] = {
        ecs_id(Rgb), ecs_id(Emissive), ecs_id(PbrMaterial), ecs_id(BlendMode),
        ecs_id(BackFaceMode), ecs_id(TextureWrap), ecs_id(TextureTransform),
        ecs_id(PbrTextures), ecs_id(Opacity), ecs_id(Tint), ecs_id(ShapeMesh)
    };
    ecs_entity_t flags[3] = { DynamicTransform, ManualTransform, NoBake };
    int32_t i;
    for (i = 0; i < 11; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = looks[i] }},
            .events = { EcsOnSet, EcsOnRemove },
            .callback = BakeOnChange,
            .ctx = b
        });
    }

    for (i = 0; i < 3; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = flags[i] }},
            .events = { EcsOnAdd, EcsOnRemove },
            .callback = BakeOnChange,
            .ctx = b
        });
    }

    box_transform_on_moved(world, box_bake_on_moved, b, NULL);
}
