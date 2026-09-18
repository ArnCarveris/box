/**
 * @file render/extract.c
 * @brief Extract system: moves meshes, instances, camera and light into the frame packet.
 */

#include "render_internal.h"
#include "../surface/surface_internal.h"

bool box_render_is_dynamic(
    ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    ecs_entity_t cur = e;
    while (cur && !result) {
        result = ecs_has_id(world, cur, DynamicTransform) ||
            ecs_has_id(world, cur, DynamicMaterial);
        cur = ecs_get_target(world, cur, EcsChildOf, 0);
    }

    return result;
}

/** Write the world space bounds of an instance to its AABB component.
 *
 * @param world The world
 * @param e The entity
 * @param mesh Its render mesh
 * @param transform Its world transform
 */
static void box_render_update_aabb(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    const WorldTransform *transform)
{
    AABB *aabb = ecs_get_mut(world, e, AABB);
    if (!aabb) {
        aabb = ecs_ensure(world, e, AABB);
    }

    vec3 lo = vec3_mul(mesh->bounds_min, mesh->scale);
    vec3 hi = vec3_mul(mesh->bounds_max, mesh->scale);
    int corner;
    for (corner = 0; corner < 8; corner ++) {
        vec3 c = { corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z };
        vec3 w = mat4_transform_point(&transform->m, c);
        aabb->min = corner ? vec3_min(aabb->min, w) : w;
        aabb->max = corner ? vec3_max(aabb->max, w) : w;
    }
}

/** Move pending mesh uploads into the packet.
 *
 * @param state The registry
 * @param p The packet
 */
static void box_extract_meshes(
    RenderState *state,
    box_packet_t *p)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->pending_meshes); i ++) {
        box_mesh_cmd_t *cmd = box_vec_push(&p->mesh_create);
        *cmd = *ecs_vec_get_t(&state->pending_meshes, box_mesh_cmd_t, i);
    }

    ecs_vec_clear(&state->pending_meshes);
}

/** Extract one dirty static entity: assign a slot and emit its instance data.
 *
 * @param world The world
 * @param state The registry
 * @param e The entity
 * @param p The packet
 */
static void box_extract_static(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e,
    box_packet_t *p)
{
    const RenderMesh *mesh = ecs_get(world, e, RenderMesh);
    const WorldTransform *transform = ecs_get(world, e, WorldTransform);
    RenderInstance *inst = ecs_get_mut(world, e, RenderInstance);
    ecs_assert(inst != NULL, ECS_INTERNAL_ERROR, NULL);
    inst->dirty = false;
    inst->layer_mask = state->layer_used ?
        box_render_entity_layers(world, state, e) : 0;
    bool dynamic = box_render_is_dynamic(world, e);
    bool transparent = box_render_is_transparent(world, state, e);
    bool has_slot = box_render_has_slot(state, e, inst);

    if (has_slot && (dynamic || transparent || inst->mesh != mesh->handle)) {
        box_render_release_slot(world, state, e, inst);
        has_slot = false;
    }

    box_render_set_transparent(world, state, e, inst, transparent);
    inst->dynamic = dynamic;
    inst->mesh = mesh->handle;
    if (dynamic) {
        goto done;
    }

    if (transparent) {
        box_render_update_aabb(world, e, mesh, transform);
        goto done;
    }

    box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, (int32_t)mesh->handle);
    if (!has_slot) {
        box_render_take_slot(world, batch, e, inst);
    } else if (state->layer_used) {
        box_render_regroup_slot(world, state, batch, e, inst);
    }

    box_static_update_t *update = box_vec_push(&p->static_updates);
    update->mesh = mesh->handle;
    update->slot = inst->slot;
    box_render_fill_instance(world, e, mesh, transform, state->bake_enabled,
        state->textured, state->tiled, state->layered ? state : NULL, &update->data);
    update->data.params[3] = (float)inst->slot;
    box_render_update_aabb(world, e, mesh, transform);
done:
    return;
}

/** Emit the new static counts of batches that changed.
 *
 * @param state The registry
 * @param p The packet
 */
static void box_extract_counts(
    RenderState *state,
    box_packet_t *p)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->batches); i ++) {
        box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, i);
        if (batch->count_dirty) {
            box_static_count_t *count = box_vec_push(&p->static_counts);
            count->mesh = (uint32_t)i;
            count->count = (uint32_t)ecs_vec_count(&batch->entities);
            batch->count_dirty = false;
        }
    }
}

void box_render_extract_dynamic(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p,
    uint32_t view_mask,
    bool strict)
{
    ecs_iter_t it = ecs_query_iter(world, state->dynamic);
    while (ecs_query_next(&it)) {
        RenderMesh *mesh = ecs_field(&it, RenderMesh, 0);
        WorldTransform *transform = ecs_field(&it, WorldTransform, 1);
        RenderInstance *inst = ecs_field(&it, RenderInstance, 2);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!inst[i].dynamic) {
                box_render_mark_dirty(world, it.entities[i]);
                continue;
            }

            if (inst[i].transparent) {
                continue;
            }

            if ((view_mask != BOX_VIEW_LAYER_ALL || strict) &&
                !box_layer_shown(inst[i].layer_mask, view_mask, strict))
            {
                continue;
            }

            box_dynamic_batch_t *batch = p->dynamic_batches.count ?
                box_vec_at(&p->dynamic_batches, p->dynamic_batches.count - 1) : NULL;
            if (!batch || batch->mesh != mesh[i].handle) {
                batch = box_vec_push(&p->dynamic_batches);
                batch->mesh = mesh[i].handle;
                batch->first = p->dynamic_instances.count;
                batch->count = 0;
            }

            box_instance_t *data = box_vec_push(&p->dynamic_instances);
            box_render_fill_instance(world, it.entities[i], &mesh[i], &transform[i], false,
                state->textured, state->tiled, state->layered ? state : NULL, data);
            batch->count ++;
        }
    }
}

ecs_entity_t box_render_active_camera(
    ecs_world_t *world)
{
    ecs_entity_t result = 0;
    ecs_entity_t singleton = ecs_lookup(world, "box.render.RenderState");
    if (!singleton) {
        goto done;
    }

    const RenderState *state = ecs_get_id(world, singleton, singleton);
    if (!state || !state->cameras) {
        goto done;
    }

    ecs_iter_t it = ecs_query_iter(world, state->cameras);
    if (ecs_query_next(&it)) {
        result = it.entities[0];
        ecs_iter_fini(&it);
    }

done:
    return result;
}

void box_render_extract(
    ecs_world_t *world,
    box_packet_t *p)
{
    RenderState *state = box_render_state(world);
    ecs_entity_t capture = box_render_collect_views(world, state, p);
    box_render_extract_bake(world, state, p);
    box_render_extract_layers(world, state, p);
    box_extract_meshes(state, p);
    box_render_extract_textures(state, p);

    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->dirty); i ++) {
        ecs_entity_t e = ecs_vec_get_t(&state->dirty, ecs_entity_t, i)[0];
        if (ecs_is_alive(world, e) && ecs_has(world, e, RenderMesh)) {
            box_extract_static(world, state, e, p);
        }
    }

    ecs_vec_clear(&state->dirty);
    if (!state->layer_used) {
        box_render_extract_dynamic(world, state, p, BOX_VIEW_LAYER_ALL, false);
    }

    box_extract_counts(state, p);
    box_render_extract_groups(state, p);
    box_render_extract_views(world, state, p, capture);
    box_render_extract_emitters(world, state, p);
    box_render_validate(world);
}

void box_render_validate(
    ecs_world_t *world)
{
#ifndef NDEBUG
    RenderState *state = box_render_state(world);
    ecs_iter_t it = ecs_query_iter(world, state->instances);
    while (ecs_query_next(&it)) {
        RenderInstance *inst = ecs_field(&it, RenderInstance, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            if (inst[i].dirty) {
                ecs_abort(ECS_INTERNAL_ERROR, "entity %s still dirty after extract",
                    ecs_get_name(world, e));
            }

            if (inst[i].dynamic || inst[i].transparent) {
                continue;
            }

            box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, (int32_t)inst[i].mesh);
            ecs_entity_t *slots = ecs_vec_first_t(&batch->entities, ecs_entity_t);
            if (inst[i].slot >= (uint32_t)ecs_vec_count(&batch->entities) ||
                slots[inst[i].slot] != e)
            {
                ecs_abort(ECS_INTERNAL_ERROR, "entity %s has slot %u of mesh %u but slot holds %s",
                    ecs_get_name(world, e), inst[i].slot, inst[i].mesh,
                    ecs_get_name(world, slots[inst[i].slot]));
            }
        }
    }
#else
    (void)world;
#endif
}

/** Extract system body: fill the packet of the frame's surface.
 *
 * The stage is used as the world, so that the removal of a consumed Capture is
 * queued instead of applied to the readonly world of a system.
 *
 * @param it System iterator
 */
static void BoxRenderExtract(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->stage;
    RenderState *state = box_render_state(world);
    box_packet_t *p = NULL;

    ecs_iter_t sit = ecs_query_iter(world, state->surfaces);
    while (ecs_query_next(&sit)) {
        SurfaceImpl *impl = ecs_field(&sit, SurfaceImpl, 0);
        if (!p && impl[0].packet) {
            p = impl[0].packet;
        }
    }

    if (p) {
        box_render_extract(world, p);
    }
}

void box_render_extract_init(
    ecs_world_t *world)
{
    RenderState *state = ecs_singleton_ensure(world, RenderState);
    state->dynamic = ecs_query(world, {
        .terms = {
            { .id = ecs_id(RenderMesh), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            { .id = ecs_id(RenderInstance) },
            { .id = DynamicTransform, .src.id = EcsSelf | EcsUp, .oper = EcsOr },
            { .id = DynamicMaterial, .src.id = EcsSelf | EcsUp }
        },
        .cache_kind = EcsQueryCacheAuto
    });
    state->views = ecs_query(world, {
        .terms = {
            { .id = ecs_id(View), .inout = EcsIn },
            { .id = ecs_id(ViewImpl), .inout = EcsInOut }
        },
        .cache_kind = EcsQueryCacheAuto
    });
    state->targets = ecs_query(world, {
        .terms = {
            { .id = ecs_id(RenderTarget), .inout = EcsIn },
            { .id = ecs_id(RenderTargetImpl), .inout = EcsInOut }
        },
        .cache_kind = EcsQueryCacheAuto
    });
    state->cameras = ecs_query(world, {
        .terms = {
            { .id = ecs_id(Camera), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn }
        }
    });
    state->captures = ecs_query(world, {
        .terms = {
            { .id = ecs_id(Capture), .inout = EcsInOut },
            { .id = ecs_id(Camera), .inout = EcsInOutNone }
        }
    });
    state->suns = ecs_query(world, {
        .terms = {
            { .id = ecs_id(DirectionalLight), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn }
        }
    });
    state->lights = ecs_query(world, {
        .terms = {
            { .id = ecs_id(PointLight), .inout = EcsIn, .oper = EcsOr },
            { .id = ecs_id(SpotLight), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            { .id = ecs_id(Rgb), .inout = EcsIn, .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });
    state->surfaces = ecs_query(world, {
        .terms = {{ .id = ecs_id(SurfaceImpl) }}
    });
    state->instances = ecs_query(world, {
        .terms = {
            { .id = ecs_id(RenderInstance), .inout = EcsInOut },
            { .id = ecs_id(RenderMesh), .inout = EcsInOutNone }
        }
    });
    state->emitters = ecs_query(world, {
        .terms = {
            { .id = ecs_id(Emissive), .inout = EcsIn },
            { .id = ecs_id(RenderMesh), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ECS_SYSTEM(world, BoxRenderExtract, EcsPreStore, 0);
}
