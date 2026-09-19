/**
 * @file render/mesh.c
 * @brief Mesh handles: the upload commands, the destroy commands and their packet hand off.
 *
 * A handle is an index into the batch list, so it is handed out once and never
 * reused. Destroying a mesh frees what the renderer holds for the handle and
 * leaves the batch behind, which keeps every instance that still names the
 * handle pointing at an empty mesh instead of at another one.
 */

#include "render_internal.h"

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

void box_render_mesh_destroy(
    ecs_world_t *world,
    uint32_t handle)
{
    RenderState *state = ecs_get_mut(world, ecs_id(RenderState), RenderState);
    if (!state) {
        goto done;
    }

    ecs_assert(handle < state->mesh_count, ECS_INVALID_PARAMETER, "bad mesh handle");
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->pending_meshes); i ++) {
        box_mesh_cmd_t *cmd = ecs_vec_get_t(&state->pending_meshes, box_mesh_cmd_t, i);
        if (cmd->handle != handle) {
            continue;
        }

        free(cmd->vertices);
        free(cmd->indices);
        ecs_vec_remove_t(&state->pending_meshes, box_mesh_cmd_t, i);
        goto done;
    }

    ecs_vec_append_t(NULL, &state->dead_meshes, uint32_t)[0] = handle;
done:
    return;
}

void box_render_mesh_init(
    RenderState *state)
{
    ecs_vec_init_t(NULL, &state->pending_meshes, box_mesh_cmd_t, 0);
    ecs_vec_init_t(NULL, &state->dead_meshes, uint32_t, 0);
}

void box_render_mesh_fini(
    RenderState *state)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->pending_meshes); i ++) {
        box_mesh_cmd_t *cmd = ecs_vec_get_t(&state->pending_meshes, box_mesh_cmd_t, i);
        free(cmd->vertices);
        free(cmd->indices);
    }

    ecs_vec_fini_t(NULL, &state->pending_meshes, box_mesh_cmd_t);
    ecs_vec_fini_t(NULL, &state->dead_meshes, uint32_t);
}

void box_render_extract_meshes(
    RenderState *state,
    box_packet_t *p)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->dead_meshes); i ++) {
        uint32_t *handle = box_vec_push(&p->mesh_destroy);
        *handle = ecs_vec_get_t(&state->dead_meshes, uint32_t, i)[0];
    }

    for (i = 0; i < ecs_vec_count(&state->pending_meshes); i ++) {
        box_mesh_cmd_t *cmd = box_vec_push(&p->mesh_create);
        *cmd = *ecs_vec_get_t(&state->pending_meshes, box_mesh_cmd_t, i);
    }

    ecs_vec_clear(&state->dead_meshes);
    ecs_vec_clear(&state->pending_meshes);
}
