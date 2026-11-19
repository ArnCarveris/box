/**
 * @file render/batch_mesh.c
 * @brief Meshes of the batches: sent to the backend, held and released with their last shape.
 *
 * The meshes of the levels of detail of a batch are sent and held as its own mesh is, for the
 * views with LodSettings; the cull pass of each view picks the level of each instance.
 */

#include "draw.h"

bool box_draws_mesh_send(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_mesh_cache_t *cache,
    uint32_t handle)
{
    bool result = true;
    const box_mesh_t *mesh;
    if (ecs_map_get(&d->meshes, handle)) {
        goto done;
    }

    mesh = box_mesh_cache_get(cache, handle);
    if (!mesh || !mesh->vertex_count || !mesh->index_count) {
        result = false;
        goto done;
    }

    uint64_t total_size = sizeof(box_render_mesh_desc_t) +
        (uint64_t)mesh->vertex_count * sizeof(box_vertex_t) +
        (uint64_t)mesh->index_count * sizeof(uint32_t) +
        (uint64_t)mesh->surface_asset_count * sizeof(vec4) +
        (mesh->data ? (uint64_t)mesh->vertex_count * 2 * sizeof(vec4) : 0);
    if (total_size + (uint64_t)ecs_vec_count(&p->data) + BOX_RENDER_DATA_ALIGN > INT32_MAX) {
        ecs_err("surface render packet capacity exceeded");
        result = false;
        goto done;
    }

    uint32_t vertex_size = (uint32_t)mesh->vertex_count * (uint32_t)sizeof(box_vertex_t);
    uint32_t index_size = (uint32_t)mesh->index_count * (uint32_t)sizeof(uint32_t);
    uint32_t asset_size = (uint32_t)mesh->surface_asset_count * (uint32_t)sizeof(vec4);
    uint32_t data_size = mesh->data
        ? (uint32_t)mesh->vertex_count * 2 * (uint32_t)sizeof(vec4) : 0;
    box_render_mesh_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_MESH,
        BOX_RENDER_CREATE, handle,
        (uint32_t)sizeof(box_render_mesh_desc_t) + vertex_size + index_size + asset_size +
        data_size);
    uint8_t *data = (uint8_t*)(desc + 1);
    desc->surface_asset_count = (uint32_t)mesh->surface_asset_count;
    desc->vertex_count = (uint32_t)mesh->vertex_count;
    desc->index_count = (uint32_t)mesh->index_count;
    desc->data = mesh->data ? 1u : 0u;
    ecs_os_memcpy(data, mesh->vertices, (ecs_size_t)vertex_size);
    ecs_os_memcpy(data + vertex_size, mesh->indices, (ecs_size_t)index_size);
    if (mesh->surface_asset_count) {
        ecs_os_memcpy(data + vertex_size + index_size, mesh->surface_assets,
            mesh->surface_asset_count * sizeof(vec4));
    }

    if (data_size) {
        ecs_os_memcpy(data + vertex_size + index_size + asset_size, mesh->data,
            (ecs_size_t)data_size);
    }

    box_mesh_cache_hold(cache, handle);
    ecs_map_insert(&d->meshes, handle, 1);
done:
    return result;
}

int32_t box_draws_lod_levels(
    const box_render_packet_t *p)
{
    int32_t result = 0, i;
    for (i = 0; i < ecs_vec_count(&p->views); i ++) {
        int32_t levels = ecs_vec_get_t(&p->views, box_render_view_t, i)->lod.levels;
        result = levels > result ? levels : result;
    }

    return result > 1 ? result : 0;
}

void box_draws_lods(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_mesh_cache_t *cache,
    box_draw_batch_t *b,
    int32_t levels)
{
    int32_t l;
    ecs_os_memset_n(b->lods, 0, uint32_t, BOX_LOD_LEVELS_MAX);
    if (!b->ready || b->key.blend != Opaque || !box_render_is_mesh(b->key.pipeline)) {
        goto done;
    }

    b->lods[0] = b->key.mesh;
    for (l = 1; l < levels; l ++) {
        uint32_t handle = box_mesh_cache_lod(cache, b->key.mesh, l);
        bool sent = handle && box_draws_mesh_send(p, d, cache, handle);
        b->lods[l] = sent ? handle : b->lods[l - 1];
    }

done:
    return;
}

void box_draws_meshes_release(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_mesh_cache_t *cache)
{
    int32_t i;
    ecs_vec_clear(&d->released);
    ecs_map_iter_t it = ecs_map_iter(&d->meshes);
    while (ecs_map_next(&it)) {
        uint32_t handle = (uint32_t)ecs_map_key(&it);
        if (!box_mesh_cache_users(cache, handle)) {
            *ecs_vec_append_t(NULL, &d->released, uint32_t) = handle;
        }
    }

    for (i = 0; i < ecs_vec_count(&d->released); i ++) {
        uint32_t handle = *ecs_vec_get_t(&d->released, uint32_t, i);
        box_render_packet_resource(p, BOX_RENDER_MESH, BOX_RENDER_DESTROY, handle, 0);
        box_mesh_cache_unhold(cache, handle);
        ecs_map_remove(&d->meshes, handle);
    }
}
