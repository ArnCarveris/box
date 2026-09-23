/**
 * @file render/extract_bake.c
 * @brief Extracts the baked lighting settings and the emissive static instances.
 */

#include "render_internal.h"

/** Queue every instance for extraction, used when the bake setting changed.
 *
 * @param world The world
 * @param state The registry
 */
static void box_extract_mark_all(
    ecs_world_t *world,
    RenderState *state)
{
    ecs_iter_t it = ecs_query_iter(world, state->instances);
    while (ecs_query_next(&it)) {
        RenderInstance *inst = ecs_field(&it, RenderInstance, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!inst[i].dirty) {
                inst[i].dirty = true;
                ecs_vec_append_t(NULL, &state->dirty, ecs_entity_t)[0] = it.entities[i];
            }
        }
    }
}

void box_render_extract_bake(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    ecs_entity_t primary = box_render_primary_view(world, state);
    const View *view = primary ? ecs_get(world, primary, View) : NULL;
    const BakeSettings *s = view ? &view->bake : NULL;
    p->bake.enabled = s && s->enabled ? 1u : 0u;
    p->bake.budget = s && s->budget > 0 ? (uint32_t)s->budget : BOX_BAKE_BUDGET;
    p->bake.spacing = s && s->probe_spacing > 0 ? s->probe_spacing : BOX_BAKE_SPACING;
    p->bake.occluder_min_size = s && s->occluder_min_size > 0 ? s->occluder_min_size : 0.5f;
    p->bake.probe_max = s && s->probe_max > 0 ? (uint32_t)s->probe_max : BOX_BAKE_PROBE_MAX;
    p->bake.x = s && s->x > 0 ? (uint32_t)s->x : BOX_BAKE_GRID_X;
    p->bake.y = s && s->y > 0 ? (uint32_t)s->y : BOX_BAKE_GRID_Y;
    p->bake.z = s && s->z > 0 ? (uint32_t)s->z : BOX_BAKE_GRID_Z;
    p->bake.steps = s && s->steps > 0 ? (uint32_t)s->steps : BOX_BAKE_STEPS;

    bool enabled = p->bake.enabled != 0;
    if (enabled != state->bake_enabled ||
        p->bake.occluder_min_size != state->bake_occluder_min_size) {
        state->bake_occluder_min_size = p->bake.occluder_min_size;
        state->bake_enabled = enabled;
        box_extract_mark_all(world, state);
    }
}

/** World bounds of an instance, the same box the renderer builds its probe from.
 *
 * @param mesh Its render mesh
 * @param transform Its world transform
 * @param center World center of the bounds
 * @param radius Radius of the sphere around the bounds
 */
static void box_extract_emitter_bounds(
    const RenderMesh *mesh,
    const WorldTransform *transform,
    vec3 *center,
    float *radius)
{
    vec3 lo = vec3_mul(mesh->bounds_min, mesh->scale);
    vec3 hi = vec3_mul(mesh->bounds_max, mesh->scale);
    vec3 min = vec3_new(0, 0, 0);
    vec3 max = vec3_new(0, 0, 0);
    int corner;
    for (corner = 0; corner < 8; corner ++) {
        vec3 c = { corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z };
        vec3 w = mat4_transform_point(&transform->m, c);
        min = corner ? vec3_min(min, w) : w;
        max = corner ? vec3_max(max, w) : w;
    }

    vec3 half = vec3_scale(vec3_sub(max, min), 0.5f);
    *center = vec3_add(min, half);
    *radius = vec3_length(half);
}

/** Extract one emissive static instance into the packet.
 *
 * @param it The emitter query iterator
 * @param i Index of the entity in the table
 * @param p The packet
 */
static void box_extract_emitter(
    const ecs_iter_t *it,
    int32_t i,
    box_packet_t *p)
{
    const Emissive *emissive = &ecs_field(it, Emissive, 0)[ecs_field_is_self(it, 0) ? i : 0];
    const RenderMesh *mesh = &ecs_field(it, RenderMesh, 1)[i];
    const WorldTransform *transform = &ecs_field(it, WorldTransform, 2)[i];
    float color[3] = {
        box_srgb_to_linear(emissive->color.r) * emissive->strength,
        box_srgb_to_linear(emissive->color.g) * emissive->strength,
        box_srgb_to_linear(emissive->color.b) * emissive->strength };
    if (color[0] <= 0 && color[1] <= 0 && color[2] <= 0) {
        goto done;
    }

    vec3 center;
    float radius;
    box_extract_emitter_bounds(mesh, transform, &center, &radius);

    box_emitter_t *out = box_vec_push(&p->emitters);
    out->position[0] = center.x;
    out->position[1] = center.y;
    out->position[2] = center.z;
    out->radius = radius;
    out->color[0] = color[0];
    out->color[1] = color[1];
    out->color[2] = color[2];
    out->range = box_bake_emitter_range(color, radius);
    const RenderInstance *instance = ecs_get(it->world, it->entities[i], RenderInstance);
    out->mesh = mesh->handle;
    out->slot = instance ? instance->slot : UINT32_MAX;
    out->pad[0] = out->pad[1] = 0;
done:
    return;
}

void box_render_extract_emitters(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    if (!p->bake.enabled) {
        goto done;
    }

    ecs_iter_t it = ecs_query_iter(world, state->emitters);
    while (ecs_query_next(&it)) {
        if (box_render_is_dynamic(world, it.entities[0])) {
            continue;
        }

        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (state->ui_mask &&
                (box_render_entity_layers(world, state, it.entities[i]) & state->ui_mask)) {
                continue;
            }
            if (state->hide_used &&
                box_hierarchy_ancestor_has(world, it.entities[i], Hidden))
            {
                continue;
            }

            box_extract_emitter(&it, i, p);
        }
    }

done:
    return;
}
