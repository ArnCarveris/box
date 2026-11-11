/**
 * @file render/bake_change.c
 * @brief Changes of baked scenes that rebake the probes they can reach.
 *
 * Static shapes that move, change their look or leave the captures queue the probes around the
 * bounds they had and have, padded once per pass (as Lumen invalidates its surface cache by the
 * bounds of changed primitives). Lights are compared with the last frame per scene.
 *
 * The regions the shapes covered stay until the next extract, for reflect.c.
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
void box_bake_mark(
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

/** Check again whether the probes of a volume inside a box are usable.
 *
 * A probe that became invalid is hidden from the views at once. One that became valid is
 * shown when the bake that captures it is shown, so the views never see its old values. A probe
 * is checked once per frame, however many changed shapes reach it.
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
    uint32_t *checked = ecs_vec_first(&v->checked);
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
                if (checked[probe >> 5] & bit) {
                    continue;
                }

                checked[probe >> 5] |= bit;
                box_bake_probe_position(v, probe, pos);
                bool now = box_bake_usable(world, b, cache, v, pos);
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

    const float *local_min = shape->surface.p[0][0] ? shape->min : mesh->min;
    const float *local_max = shape->surface.p[0][0] ? shape->max : mesh->max;
    glm_vec3_fill(min, FLT_MAX);
    glm_vec3_fill(max, -FLT_MAX);
    for (c = 0; c < 8; c ++) {
        vec4 corner = {
            ((c & 1) ? local_max[0] : local_min[0]) * shape->scale[0],
            ((c & 2) ? local_max[1] : local_min[1]) * shape->scale[1],
            ((c & 4) ? local_max[2] : local_min[2]) * shape->scale[2],
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

/** Check whether the changes of static shapes are recorded: while a scene has LightBake or
 * ReflectionBake.
 *
 * @param b The state
 * @return Whether changes are recorded
 */
static bool box_bake_recording(
    const box_render_bake_t *b)
{
    return ecs_vec_count(&b->volumes) || b->reflecting;
}

/** Remember a shape that changed with the bounds it had, which the spatial index still holds
 * until it is updated in OnStore, or which a removed shape still has.
 *
 * @param world The world
 * @param b The state
 * @param e The shape
 * @param removed Whether the shape is being removed, so the index may have lost it already
 * @param geometry Whether the change can move, add or remove surfaces
 */
static void box_bake_record(
    const ecs_world_t *world,
    box_render_bake_t *b,
    ecs_entity_t e,
    bool removed,
    bool geometry)
{
    box_bake_change_t *c = ecs_vec_append_t(NULL, &b->changes, box_bake_change_t);
    c->entity = e;
    c->geometry = geometry;
    c->old = box_spatial_bounds(world, e, &c->scene, c->min, c->max);
    if (!c->old && removed && ecs_has(world, e, WorldTransform) &&
        box_bake_shape_bounds(world, e, c->min, c->max))
    {
        c->scene = box_render_scene_of(world, e);
        c->old = true;
    }
}

/** Check whether a shape is captured apart from one of the flags that keep shapes out.
 *
 * @param world The world
 * @param table The table of the shape
 * @param e The shape
 * @param skip The flag to ignore, 0 for none
 * @return Whether the shape is static and baked as far as the other flags go
 */
static bool box_bake_captured_but(
    const ecs_world_t *world,
    const ecs_table_t *table,
    ecs_entity_t e,
    ecs_id_t skip)
{
    bool dynamic = skip != DynamicTransform && box_transform_dynamic(world, table);
    bool manual = skip != ManualTransform && ecs_has_id(world, e, ManualTransform) &&
        !ecs_has(world, e, SurfaceAttachment);
    bool hidden = skip != NoBake && ecs_has_id(world, e, NoBake);

    return !dynamic && !manual && !hidden && !box_projection_anchored(world, e);
}

/** Remember the static shapes whose captured look changed.
 *
 * A shape that gains or loses DynamicTransform, ManualTransform or NoBake counts when the
 * other flags keep it static and baked, since it then enters or leaves the captures. A shape
 * that is deleted counts only when it was captured.
 *
 * Nothing counts once the world quits: it deletes its roots before ecs_is_fini holds, while
 * the spatial index cannot be queried.
 *
 * @param it The iterator of the observer
 */
static void BakeOnChange(
    ecs_iter_t *it)
{
    box_render_bake_t *b = it->ctx;
    ecs_id_t id = it->event_id;
    bool flag = id == DynamicTransform || id == ManualTransform || id == NoBake;
    bool deleted = it->event == EcsOnRemove && it->other_table &&
        ecs_table_get_type(it->other_table)->count == 0;
    int32_t i;
    if (ecs_should_quit(it->world) || !box_bake_recording(b) || (flag && deleted)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (ecs_has(it->world, e, ShapeMesh) &&
            box_bake_captured_but(it->world, it->table, e, flag ? id : 0))
        {
            box_bake_record(it->world, b, e, it->event == EcsOnRemove,
                flag || deleted || id == ecs_id(ShapeMesh));
        }
    }

done:
    return;
}

/** Remember the static shapes whose TextureBlend uses an entity as its mask, a layer or a
 * height.
 *
 * @param world The world
 * @param b The state
 * @param e The entity
 */
static void box_bake_record_blends(
    const ecs_world_t *world,
    box_render_bake_t *b,
    ecs_entity_t e)
{
    ecs_iter_t it = ecs_query_iter(world, b->blends);
    int32_t i, k;
    while (ecs_query_next(&it)) {
        const TextureBlend *blends = ecs_field(&it, TextureBlend, 0);
        bool self = ecs_field_is_self(&it, 0);
        for (i = 0; i < it.count; i ++) {
            const TextureBlend *blend = &blends[self ? i : 0];
            bool used = blend->mask == e;
            for (k = 0; k < 4; k ++) {
                used |= blend->layers[k] == e || blend->heights[k] == e;
            }

            if (used && box_bake_captured_but(world, it.table, it.entities[i], 0)) {
                box_bake_record(world, b, it.entities[i], false, false);
            }
        }
    }
}

/** Remember the static shapes whose TextureBlend has a changed entity as a layer: its
 * PbrTextures or TextureTransform changed what the captures see of them. Nothing counts once
 * the world quits, as for BakeOnChange.
 *
 * @param it The iterator of the observer
 */
static void BakeOnLayer(
    ecs_iter_t *it)
{
    box_render_bake_t *b = it->ctx;
    int32_t i;
    if (ecs_should_quit(it->world) || !box_bake_recording(b) || !b->blends ||
        !ecs_id_in_use(it->world, ecs_id(TextureBlend)))
    {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_bake_record_blends(it->world, b, it->entities[i]);
    }

done:
    return;
}

/** Remember an entity and the static shapes below it, whose look an Opacity changed.
 *
 * @param world The world
 * @param b The state
 * @param e The entity
 */
static void box_bake_record_tree(
    const ecs_world_t *world,
    box_render_bake_t *b,
    ecs_entity_t e)
{
    if (ecs_has(world, e, ShapeMesh) &&
        box_bake_captured_but(world, ecs_get_table(world, e), e, 0))
    {
        box_bake_record(world, b, e, false, false);
    }

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_bake_record_tree(world, b, it.entities[i]);
        }
    }
}

/** Remember the static shapes below the entities whose Opacity changed, which applies to every
 * shape below them. Tint does not count: the captures ignore it. Nothing counts once the world
 * quits, as for BakeOnChange.
 *
 * @param it The iterator of the observer
 */
static void BakeOnTree(
    ecs_iter_t *it)
{
    box_render_bake_t *b = it->ctx;
    int32_t i;
    if (!ecs_should_quit(it->world) && box_bake_recording(b)) {
        for (i = 0; i < it->count; i ++) {
            box_bake_record_tree(it->world, b, it->entities[i]);
        }
    }
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
    const box_render_bake_t *b = ctx;
    int32_t i;
    for (i = 0; box_bake_recording(b) && i < count; i ++) {
        if (ecs_has(world, entities[i], ShapeMesh) && !ecs_has_id(world, entities[i], NoBake)) {
            box_bake_record(world, ctx, entities[i], false, true);
        }
    }
}

/** Queue the probes a changed region of a scene can reach: its bounds padded by the probe
 * spacing once per pass. When its surfaces changed, not only their look, it first checks again
 * which probes within reach of it are usable.
 *
 * @param world The world
 * @param b The state
 * @param scene The scene of the region
 * @param min Minimum corner of the region
 * @param max Maximum corner of the region
 * @param geometry Whether surfaces of the region moved, came or went
 */
static void box_bake_region(
    const ecs_world_t *world,
    box_render_bake_t *b,
    ecs_entity_t scene,
    const vec3 min,
    const vec3 max,
    bool geometry)
{
    box_bake_volume_t *v = box_bake_find(b, scene);
    vec3 lo, hi;
    if (!v || !v->sent || !v->probes || v->fresh) {
        goto done;
    }

    int32_t bounces = v->bounces >= 0 && v->bounces <= BOX_BAKE_BOUNCES_MAX ? v->bounces : 0;
    float reach = box_bake_reach(v);
    float pad = glm_max(v->grid.spacing * (float)(1 + bounces), reach);
    glm_vec3_subs(ECS_CONST_CAST(float*, min), reach, lo);
    glm_vec3_adds(ECS_CONST_CAST(float*, max), reach, hi);
    if (geometry) {
        box_bake_revalidate(world, b, v, lo, hi);
    }

    glm_vec3_subs(ECS_CONST_CAST(float*, min), pad, lo);
    glm_vec3_adds(ECS_CONST_CAST(float*, max), pad, hi);
    box_bake_mark_box(v, lo, hi, false);
done:
    return;
}

/** Add a region a changed shape covered to the regions of the state.
 *
 * @param b The state
 * @param scene The scene of the region
 * @param min Minimum corner of the region
 * @param max Maximum corner of the region
 * @param geometry Whether surfaces of the region moved, came or went
 */
static void box_bake_add_region(
    box_render_bake_t *b,
    ecs_entity_t scene,
    const vec3 min,
    const vec3 max,
    bool geometry)
{
    box_bake_region_t *r = ecs_vec_append_t(NULL, &b->regions, box_bake_region_t);
    r->scene = scene;
    r->geometry = geometry;
    glm_vec3_copy(ECS_CONST_CAST(float*, min), r->min);
    glm_vec3_copy(ECS_CONST_CAST(float*, max), r->max);
}

void box_bake_changes(
    ecs_world_t *world,
    box_render_bake_t *b)
{
    int32_t i;
    ecs_vec_clear(&b->regions);
    for (i = 0; i < ecs_vec_count(&b->changes); i ++) {
        const box_bake_change_t *c = ecs_vec_get_t(&b->changes, box_bake_change_t, i);
        ecs_entity_t e = c->entity, scene = 0;
        vec3 min, max;
        if (c->old) {
            box_bake_add_region(b, c->scene, c->min, c->max, c->geometry);
        }

        if (ecs_is_alive(world, e) && !ecs_has_id(world, e, NoBake) &&
            (!ecs_has_id(world, e, ManualTransform) || ecs_has(world, e, SurfaceAttachment)) &&
            !box_projection_anchored(world, e) &&
            !box_transform_dynamic(world, ecs_get_table(world, e)) &&
            box_spatial_bounds(world, e, &scene, min, max))
        {
            box_bake_add_region(b, scene, min, max, c->geometry);
        }
    }

    ecs_vec_clear(&b->changes);
    for (i = 0; ecs_vec_count(&b->regions) && i < ecs_vec_count(&b->volumes); i ++) {
        box_bake_volume_t *v = ecs_vec_get_t(&b->volumes, box_bake_volume_t, i);
        int32_t words = ecs_vec_count(&v->valid);
        ecs_vec_set_count_t(NULL, &v->checked, uint32_t, words);
        if (words) {
            ecs_os_memset_n(ecs_vec_first(&v->checked), 0, uint32_t, words);
        }
    }

    for (i = 0; ecs_vec_count(&b->volumes) && i < ecs_vec_count(&b->regions); i ++) {
        const box_bake_region_t *r = ecs_vec_get_t(&b->regions, box_bake_region_t, i);
        box_bake_region(world, b, r->scene, r->min, r->max, r->geometry);
    }
}

/** Queue the probes a light can reach: within its range, or every probe for a directional
 * light or a change of the whole scene.
 *
 * @param ctx The volume
 * @param l The light, NULL for the whole scene
 */
static void box_bake_mark_light(
    void *ctx,
    const box_render_light_t *l)
{
    box_bake_volume_t *v = ctx;
    vec3 lo, hi;
    if (!l || !(l->range > 0)) {
        box_bake_mark_all(v);
        v->relit = true;
    } else {
        glm_vec3_subs(ECS_CONST_CAST(float*, l->position), l->range, lo);
        glm_vec3_adds(ECS_CONST_CAST(float*, l->position), l->range, hi);
        box_bake_mark_box(v, lo, hi, true);
    }
}

void box_bake_light_record_init(
    box_bake_light_record_t *r)
{
    ecs_os_memset_t(r, 0, box_bake_light_record_t);
    ecs_vec_init_t(NULL, &r->lights, box_render_light_t, 0);
    ecs_vec_init_t(NULL, &r->entities, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &r->modes, uint8_t, 0);
    ecs_vec_init_t(NULL, &r->scratch, box_render_light_t, 0);
}

void box_bake_light_record_fini(
    box_bake_light_record_t *r)
{
    ecs_vec_fini_t(NULL, &r->lights, box_render_light_t);
    ecs_vec_fini_t(NULL, &r->entities, ecs_entity_t);
    ecs_vec_fini_t(NULL, &r->modes, uint8_t);
    ecs_vec_fini_t(NULL, &r->scratch, box_render_light_t);
}

/** Check whether two light records describe the same light. The index of the shadow tile of
 * a light is not part of it, so a change of the tiles rebakes and recaptures nothing.
 *
 * @param a The first light
 * @param b The second light
 * @return Whether the lights are the same
 */
static bool box_bake_light_same(
    const box_render_light_t *a,
    const box_render_light_t *b)
{
    return !ecs_os_memcmp(a, b, (ecs_size_t)BOX_RENDER_LIGHT_SHADING);
}

/** Check whether two lists of light records describe the same lights.
 *
 * @param a The first lights
 * @param b The second lights
 * @param count Lights of each list
 * @return Whether every light is the same, see box_bake_light_same
 */
static bool box_bake_lights_same(
    const box_render_light_t *a,
    const box_render_light_t *b,
    int32_t count)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < count && result; i ++) {
        result = box_bake_light_same(&a[i], &b[i]);
    }

    return result;
}

/** Pass the lights that differ between a record and a frame to mark, matched by their entity:
 * a light added, removed or changed, its bake mode included, counts in both its old and new
 * place.
 *
 * @param map Scratch map of light entities
 * @param r The record
 * @param l The lights of this frame
 * @param mark Receives the lights
 * @param ctx Context of mark
 */
static void box_bake_lights_diff(
    ecs_map_t *map,
    const box_bake_light_record_t *r,
    const box_bake_light_set_t *l,
    box_bake_light_mark_t mark,
    void *ctx)
{
    const box_render_light_t *before = ecs_vec_first(&r->lights);
    const ecs_entity_t *owners = ecs_vec_first(&r->entities);
    const uint8_t *modes = ecs_vec_first(&r->modes);
    int32_t i, known = ecs_vec_count(&r->lights);
    ecs_map_clear(map);
    for (i = 0; i < known; i ++) {
        ecs_map_insert(map, owners[i], (ecs_map_val_t)i);
    }

    for (i = 0; i < l->count; i ++) {
        ecs_map_val_t *old = ecs_map_get(map, l->entities[i]);
        if (!old) {
            mark(ctx, &l->lights[i]);
        } else {
            const box_render_light_t *was = &before[*old];
            if (!box_bake_light_same(was, &l->lights[i]) || modes[*old] != l->modes[i])
            {
                mark(ctx, was);
                mark(ctx, &l->lights[i]);
            }

            ecs_map_remove(map, l->entities[i]);
        }
    }

    ecs_map_iter_t mit = ecs_map_iter(map);
    while (ecs_map_next(&mit)) {
        mark(ctx, &before[ecs_map_value(&mit)]);
    }
}

/** Check whether a directional light is within the thresholds of how it was: its direction
 * within threshold->light degrees and its linear color within threshold->color of the
 * brightest channel of either, a threshold of 0 asking for the same value.
 *
 * The direction of a light that is black in both states does not count unless
 * threshold->light is 0. Anything else of the light must be the same.
 *
 * @param was The light as it was
 * @param now The light as it is
 * @param threshold The thresholds
 * @return Whether the light counts as unchanged
 */
static bool box_bake_light_near(
    const box_render_light_t *was,
    const box_render_light_t *now,
    const box_bake_threshold_t *threshold)
{
    vec3 a, b;
    float brightest = 0.0f, largest = 0.0f;
    int32_t i;
    bool result = was->range == now->range && was->spot_scale == now->spot_scale &&
        was->spot_offset == now->spot_offset &&
        !ecs_os_memcmp(was->direction, now->direction, ECS_SIZEOF(float) * 3);
    if (!result) {
        goto done;
    }

    if (threshold->light > 0 && glm_vec3_max(ECS_CONST_CAST(float*, was->color)) <= 0 &&
        glm_vec3_max(ECS_CONST_CAST(float*, now->color)) <= 0)
    {
        result = true;
    } else if (threshold->light > 0) {
        glm_vec3_normalize_to(ECS_CONST_CAST(float*, was->position), a);
        glm_vec3_normalize_to(ECS_CONST_CAST(float*, now->position), b);
        float cosine = glm_clamp(glm_vec3_dot(a, b), -1.0f, 1.0f);
        result = glm_deg(acosf(cosine)) <= threshold->light;
    } else {
        result = !ecs_os_memcmp(was->position, now->position, ECS_SIZEOF(float) * 3);
    }

    if (!result) {
        goto done;
    }

    if (threshold->color > 0) {
        for (i = 0; i < 3; i ++) {
            brightest = glm_max(brightest, glm_max(was->color[i], now->color[i]));
            largest = glm_max(largest, fabsf(was->color[i] - now->color[i]));
        }

        result = largest <= threshold->color * brightest;
    } else {
        result = !ecs_os_memcmp(was->color, now->color, ECS_SIZEOF(float) * 3);
    }

done:
    return result;
}

/** Take the lights of a frame into the scratch of a record, each directional light within the
 * thresholds of the one of its entity and mode in the record as the record has it.
 *
 * @param map Scratch map of light entities
 * @param r The record, which knows a frame
 * @param now The lights in this frame
 * @param threshold The thresholds
 * @return The lights to compare and keep
 */
static const box_render_light_t* box_bake_lights_hold(
    ecs_map_t *map,
    box_bake_light_record_t *r,
    const box_bake_light_set_t *now,
    const box_bake_threshold_t *threshold)
{
    const box_render_light_t *before = ecs_vec_first(&r->lights);
    const ecs_entity_t *owners = ecs_vec_first(&r->entities);
    const uint8_t *modes = ecs_vec_first(&r->modes);
    int32_t i, known = ecs_vec_count(&r->lights);
    ecs_vec_set_count_t(NULL, &r->scratch, box_render_light_t, now->count);
    box_render_light_t *out = ecs_vec_first(&r->scratch);
    ecs_os_memcpy_n(out, now->lights, box_render_light_t, now->count);
    ecs_map_clear(map);
    for (i = 0; i < known; i ++) {
        if (!(before[i].range > 0)) {
            ecs_map_insert(map, owners[i], (ecs_map_val_t)i);
        }
    }

    for (i = 0; i < now->count; i ++) {
        ecs_map_val_t *old = ecs_map_get(map, now->entities[i]);
        if (!old || (now->lights[i].range > 0) || modes[*old] != now->modes[i] ||
            box_bake_light_same(&before[*old], &now->lights[i]))
        {
            continue;
        }

        if (box_bake_light_near(&before[*old], &now->lights[i], threshold)) {
            out[i] = before[*old];
            r->held = true;
        }
    }

    return out;
}

void box_bake_lights_compare(
    ecs_map_t *map,
    box_bake_light_record_t *r,
    const box_render_view_lights_t *view,
    const box_bake_light_set_t *frame,
    bool ambient,
    const box_bake_threshold_t *threshold,
    box_bake_light_mark_t mark,
    void *ctx)
{
    box_bake_light_set_t held = *frame;
    const box_bake_light_set_t *now = &held;
    r->held = false;
    if (r->known && held.count && (threshold->light > 0 || threshold->color > 0)) {
        held.lights = box_bake_lights_hold(map, r, frame, threshold);
    }

    int32_t count = now->count, known = ecs_vec_count(&r->lights);
    bool sun = (r->view.buffer == BOX_RENDER_BUFFER_LIGHT_FIRST) !=
        (view->buffer == BOX_RENDER_BUFFER_LIGHT_FIRST);
    bool sky = ambient &&
        (ecs_os_memcmp(r->view.ambient_sky, view->ambient_sky, ECS_SIZEOF(float) * 3) ||
        ecs_os_memcmp(r->view.ambient_ground, view->ambient_ground, ECS_SIZEOF(float) * 3));
    bool same = count == known && (!count ||
        (box_bake_lights_same(now->lights, ecs_vec_first(&r->lights), count) &&
        !ecs_os_memcmp(now->entities, ecs_vec_first(&r->entities),
            count * ECS_SIZEOF(ecs_entity_t)) &&
        !ecs_os_memcmp(now->modes, ecs_vec_first(&r->modes), count)));
    if (r->known && (sun || sky)) {
        mark(ctx, NULL);
    } else if (r->known && !same) {
        box_bake_lights_diff(map, r, now, mark, ctx);
    }

    ecs_vec_set_count_t(NULL, &r->lights, box_render_light_t, count);
    ecs_vec_set_count_t(NULL, &r->entities, ecs_entity_t, count);
    ecs_vec_set_count_t(NULL, &r->modes, uint8_t, count);
    if (count) {
        ecs_os_memcpy_n(ecs_vec_first(&r->lights), now->lights, box_render_light_t, count);
        ecs_os_memcpy_n(ecs_vec_first(&r->entities), now->entities, ecs_entity_t, count);
        ecs_os_memcpy_n(ecs_vec_first(&r->modes), now->modes, uint8_t, count);
    }

    r->view = *view;
    r->known = true;
}

void box_bake_lights(
    box_render_bake_t *b,
    box_bake_volume_t *v)
{
    const box_bake_lights_t *l = &v->lights;
    box_bake_light_set_t now = { l->count, l->lights, l->entities, l->modes };
    if (v->viewed && v->sent && v->probes) {
        box_bake_lights_compare(&b->light_map, &v->known, &l->first, &now, false,
            &v->threshold, box_bake_mark_light, v);
    }
}

/** Register the observers of the changes of static shapes that rebake the probes near them,
 * AlphaCutoff, Transmission and Water also when they are added, as its holes change what the
 * captures see and a transmissive or water shape leaves them.
 *
 * A TextureBlend counts like PbrTextures, and so does a change of the PbrTextures or
 * TextureTransform of an entity that is a layer of one. A set or removed TextureScroll counts,
 * the offsets it moves every frame do not.
 *
 * The terms take the default source, so the change of a material component of a prefab
 * reaches the instances that inherit it.
 *
 * @param world The world
 * @param b The state
 */
void box_bake_observers(
    ecs_world_t *world,
    box_render_bake_t *b)
{
    ecs_entity_t looks[16] = {
        ecs_id(Rgb), ecs_id(Emissive), ecs_id(PbrMaterial), ecs_id(BlendMode),
        ecs_id(BackFaceMode), ecs_id(TextureWrap), ecs_id(TextureTransform), ecs_id(ShapeMesh),
        ecs_id(ClearCoat), ecs_id(SpecularAnisotropy), ecs_id(Sheen), ecs_id(DiffuseTransmission),
        ecs_id(TextureMapping), ecs_id(TextureScroll), ecs_id(TileTerrain), ecs_id(Fresnel)
    };
    ecs_entity_t added[3] = { ecs_id(AlphaCutoff), ecs_id(Transmission), ecs_id(Water) };
    ecs_entity_t flags[2] = { ManualTransform, NoBake };
    int32_t i;
    for (i = 0; i < 16; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = looks[i] }},
            .events = { EcsOnSet, EcsOnRemove },
            .callback = BakeOnChange,
            .ctx = b
        });
    }

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Opacity) }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BakeOnTree,
        .ctx = b
    });

    for (i = 0; i < 3; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = added[i] }},
            .events = { EcsOnAdd, EcsOnSet, EcsOnRemove },
            .callback = BakeOnChange,
            .ctx = b
        });
    }

    box_transform_on_dynamic(world, BakeOnChange, b, NULL);
    for (i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = flags[i] }},
            .events = { EcsOnAdd, EcsOnRemove },
            .callback = BakeOnChange,
            .ctx = b
        });
    }

    ecs_observer(world, {
        .query.terms = {{
            .id = ecs_id(PbrTextures),
            .src.id = EcsSelf | EcsUp,
            .trav = EcsIsA
        }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BakeOnChange,
        .ctx = b
    });
    ecs_observer(world, {
        .query.terms = {{
            .id = ecs_id(TextureBlend),
            .src.id = EcsSelf | EcsUp,
            .trav = EcsIsA
        }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BakeOnChange,
        .ctx = b
    });

    b->blends = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "RenderBakeBlends" }),
        .terms = {
            {
                .id = ecs_id(TextureBlend),
                .src.id = EcsSelf | EcsUp,
                .trav = EcsIsA,
                .inout = EcsIn
            },
            { .id = ecs_id(ShapeMesh), .inout = EcsInOutNone }
        }
    });
    ecs_entity_t layers[2] = { ecs_id(PbrTextures), ecs_id(TextureTransform) };
    for (i = 0; i < 2; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = layers[i] }},
            .events = { EcsOnSet, EcsOnRemove },
            .callback = BakeOnLayer,
            .ctx = b
        });
    }

    box_transform_on_moved(world, box_bake_on_moved, b, NULL);
}
