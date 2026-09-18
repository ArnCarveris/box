/**
 * @file render/render.c
 * @brief Registers the box.render module, its registry singleton and mesh uploads.
 */

#define BOX_RENDER_IMPL
#include "render_internal.h"

ECS_COMPONENT_DECLARE(RenderInstance);
ECS_COMPONENT_DECLARE(RenderState);

/** Registry destructor releasing batches, pending meshes and queries.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    RenderState *state = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        int32_t j;
        for (j = 0; j < ecs_vec_count(&state[i].batches); j ++) {
            box_batch_t *batch = ecs_vec_get_t(&state[i].batches, box_batch_t, j);
            ecs_vec_fini_t(NULL, &batch->entities, ecs_entity_t);
            ecs_vec_fini_t(NULL, &batch->groups, box_slot_group_t);
        }

        ecs_vec_fini_t(NULL, &state[i].batches, box_batch_t);
        for (j = 0; j < ecs_vec_count(&state[i].pending_meshes); j ++) {
            box_mesh_cmd_t *cmd = ecs_vec_get_t(&state[i].pending_meshes, box_mesh_cmd_t, j);
            free(cmd->vertices);
            free(cmd->indices);
        }

        ecs_vec_fini_t(NULL, &state[i].pending_meshes, box_mesh_cmd_t);
        for (j = 0; j < ecs_vec_count(&state[i].pending_textures); j ++) {
            free(ecs_vec_get_t(&state[i].pending_textures, box_texture_cmd_t, j)->pixels);
        }

        ecs_vec_fini_t(NULL, &state[i].pending_textures, box_texture_cmd_t);
        ecs_vec_fini_t(NULL, &state[i].dead_textures, uint32_t);
        for (j = 0; j < BOX_TEXTURE_CLASSES; j ++) {
            ecs_vec_fini_t(NULL, &state[i].free_layers[j], uint32_t);
        }

        ecs_vec_fini_t(NULL, &state[i].layers, box_layer_t);
        ecs_vec_fini_t(NULL, &state[i].view_layers, ecs_entity_t);
        ecs_vec_fini_t(NULL, &state[i].dirty, ecs_entity_t);
        ecs_vec_fini_t(NULL, &state[i].transparent, ecs_entity_t);
        ecs_vec_fini_t(NULL, &state[i].sort, box_sort_key_t);
        ecs_vec_fini_t(NULL, &state[i].sort_tmp, box_sort_key_t);
        ecs_vec_fini_t(NULL, &state[i].view_list, box_view_src_t);
        ecs_vec_fini_t(NULL, &state[i].view_env, box_view_env_t);
        ecs_vec_fini_t(NULL, &state[i].dead_targets, uint32_t);
        if (state[i].views) ecs_query_fini(state[i].views);
        if (state[i].targets) ecs_query_fini(state[i].targets);
        if (state[i].dynamic) ecs_query_fini(state[i].dynamic);
        if (state[i].cameras) ecs_query_fini(state[i].cameras);
        if (state[i].captures) ecs_query_fini(state[i].captures);
        if (state[i].suns) ecs_query_fini(state[i].suns);
        if (state[i].lights) ecs_query_fini(state[i].lights);
        if (state[i].emitters) ecs_query_fini(state[i].emitters);
        if (state[i].layer_query) ecs_query_fini(state[i].layer_query);
        if (state[i].surfaces) ecs_query_fini(state[i].surfaces);
        if (state[i].instances) ecs_query_fini(state[i].instances);
    }
}

/** Pack the position of a vertex inside the mesh bounds into four bytes.
 *
 * @param p The vertex position
 * @param min Minimum corner of the mesh bounds
 * @param extent Size of the mesh bounds, zero on an axis the mesh is flat in
 * @return The position in the bounds, one byte per axis and a zero fourth byte
 */
static uint32_t box_render_pack_local(
    const float *p,
    vec3 min,
    vec3 extent)
{
    float origin[3] = { min.x, min.y, min.z };
    float size[3] = { extent.x, extent.y, extent.z };
    uint32_t result = 0;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        float t = size[i] > 0 ? (p[i] - origin[i]) / size[i] : 0.5f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        result |= (uint32_t)(t * 255.0f + 0.5f) << (i * 8);
    }

    return result;
}

RenderState* box_render_state(
    ecs_world_t *world)
{
    RenderState *state = ecs_get_mut(world, ecs_id(RenderState), RenderState);
    ecs_assert(state != NULL, ECS_INVALID_OPERATION, "box.render is not imported");

    return state;
}

uint32_t box_render_mesh_create(
    ecs_world_t *world,
    const box_mesh_data_t *data)
{
    RenderState *state = box_render_state(world);
    uint32_t handle = state->mesh_count ++;

    box_batch_t *batch = ecs_vec_append_t(NULL, &state->batches, box_batch_t);
    ecs_vec_init_t(NULL, &batch->entities, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &batch->groups, box_slot_group_t, 0);
    batch->count_dirty = false;
    batch->groups_dirty = false;

    box_mesh_cmd_t *cmd = ecs_vec_append_t(NULL, &state->pending_meshes, box_mesh_cmd_t);
    cmd->handle = handle;
    cmd->vertex_count = data->vertex_count;
    cmd->index_count = data->index_count;
    cmd->vertices = malloc((size_t)data->vertex_count * BOX_VERTEX_FLOATS * sizeof(float));
    cmd->indices = malloc((size_t)data->index_count * sizeof(uint32_t));
    memcpy(cmd->indices, data->indices, (size_t)data->index_count * sizeof(uint32_t));
    cmd->bounds_min[0] = data->bounds_min.x;
    cmd->bounds_min[1] = data->bounds_min.y;
    cmd->bounds_min[2] = data->bounds_min.z;
    cmd->bounds_max[0] = data->bounds_max.x;
    cmd->bounds_max[1] = data->bounds_max.y;
    cmd->bounds_max[2] = data->bounds_max.z;

    vec3 extent = vec3_sub(data->bounds_max, data->bounds_min);
    uint32_t v;
    for (v = 0; v < data->vertex_count; v ++) {
        const float *pos = data->positions + (size_t)v * 3;
        float *dst = cmd->vertices + (size_t)v * BOX_VERTEX_FLOATS;
        memcpy(dst, pos, 3 * sizeof(float));
        memcpy(dst + 3, data->normals + (size_t)v * 3, 3 * sizeof(float));
        memcpy(dst + 6, data->uvs + (size_t)v * 2, 2 * sizeof(float));
        uint32_t local = box_render_pack_local(pos, data->bounds_min, extent);
        memcpy(dst + 8, &local, sizeof(local));
    }

    return handle;
}

void BoxRenderImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxRender);
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxMaterial);

    ECS_META_COMPONENT(world, RenderMesh);
    ECS_META_COMPONENT(world, RenderTexture);
    ECS_COMPONENT_DEFINE(world, RenderInstance);
    ECS_COMPONENT_DEFINE(world, RenderState);

    ecs_set_hooks(world, RenderState, {
        .ctor = flecs_default_ctor,
        .dtor = box_render_state_dtor
    });
    ecs_set_hooks(world, RenderInstance, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, RenderTexture, { .ctor = flecs_default_ctor });
    ecs_add_pair(world, ecs_id(RenderInstance), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(RenderMesh), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(RenderTexture), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(RenderMesh), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(RenderMesh), EcsWith, ecs_id(RenderInstance));

    RenderState *state = ecs_singleton_ensure(world, RenderState);
    ecs_vec_init_t(NULL, &state->batches, box_batch_t, 0);
    ecs_vec_init_t(NULL, &state->pending_meshes, box_mesh_cmd_t, 0);
    ecs_vec_init_t(NULL, &state->pending_textures, box_texture_cmd_t, 0);
    ecs_vec_init_t(NULL, &state->dead_textures, uint32_t, 0);
    int32_t cls;
    for (cls = 0; cls < BOX_TEXTURE_CLASSES; cls ++) {
        ecs_vec_init_t(NULL, &state->free_layers[cls], uint32_t, 0);
    }

    ecs_vec_init_t(NULL, &state->layers, box_layer_t, 0);
    ecs_vec_init_t(NULL, &state->view_layers, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->dirty, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->transparent, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->sort, box_sort_key_t, 0);
    ecs_vec_init_t(NULL, &state->sort_tmp, box_sort_key_t, 0);
    ecs_vec_init_t(NULL, &state->view_list, box_view_src_t, 0);
    ecs_vec_init_t(NULL, &state->view_env, box_view_env_t, 0);
    ecs_vec_init_t(NULL, &state->dead_targets, uint32_t, 0);

    box_render_texture_init(world);
    box_render_view_init(world);
    box_render_layers_init(world);
    box_render_view_layers_init(world);
    box_render_instance_init(world);
    box_render_extract_init(world);
}
