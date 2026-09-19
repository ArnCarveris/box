/**
 * @file render/lod_impostor.c
 * @brief Impostors: a few views of a subtree in an atlas and the billboard that shows them.
 *
 * A subtree that is still too expensive where even its merged mesh is more
 * than the frame wants gets an atlas of views around its Y axis, rendered by
 * one extra view of the packet through the same offscreen path a UI group
 * composites with. The billboard is one sorted draw that shows the tile
 * standing closest to where the eye is.
 */

#include "lod_cache.h"
#include "../geometry/geometry_internal.h"
#include <math.h>

/** Vertices the quad of one tile holds */
#define BOX_LOD_TILE_VERTS (4)

/** Indices the quad of one tile holds */
#define BOX_LOD_TILE_INDICES (6)

/** Hash the tile meshes are shared by, one per tile index */
#define BOX_LOD_TILE_HASH (0x6c6f64696d706f73ull)

/** Part of its radius a capture leaves around the subtree in its tile */
#define BOX_LOD_TILE_MARGIN (1.04f)

/** Turn of a full circle, in radians */
#define BOX_LOD_TURN (6.283185307179586f)

void box_lod_impostor_size(
    vec3 min,
    vec3 max,
    float *out)
{
    vec3 size = vec3_sub(max, min);

    out[0] = sqrtf(size.x * size.x + size.z * size.z) * BOX_LOD_TILE_MARGIN;
    out[1] = size.y * BOX_LOD_TILE_MARGIN;
}

float box_lod_impostor_error(
    vec3 min,
    vec3 max)
{
    vec3 size = vec3_sub(max, min);
    float radius = vec3_length(size) * 0.5f;
    float step = BOX_LOD_TURN / (float)BOX_LOD_IMPOSTOR_VIEWS;
    float tile[2];

    box_lod_impostor_size(min, max, tile);
    float widest = tile[0] > tile[1] ? tile[0] : tile[1];
    float texel = widest / (float)BOX_LOD_IMPOSTOR_TILE;

    return texel + radius * (1.0f - cosf(step * 0.5f));
}

bool box_lod_impostor_atlas(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    box_packet_t *p,
    box_lod_entry_t *entry)
{
    RenderState *state = box_render_state(world);
    if (entry->atlas) {
        goto done;
    }

    if (!state || cache->atlases >= BOX_LOD_IMPOSTOR_MAX) {
        goto done;
    }

    entry->atlas = ++ state->target_count;
    cache->atlases ++;

    box_target_cmd_t *cmd = box_vec_push(&p->target_create);

    cmd->handle = entry->atlas;
    cmd->width = (uint32_t)(BOX_LOD_IMPOSTOR_TILE * BOX_LOD_IMPOSTOR_VIEWS);
    cmd->height = (uint32_t)BOX_LOD_IMPOSTOR_TILE;
done:
    return entry->atlas != 0;
}

void box_lod_impostor_drop(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    box_lod_entry_t *entry)
{
    RenderState *state = entry->atlas ? box_render_state(world) : NULL;
    if (!entry->atlas) {
        goto done;
    }

    if (state) {
        ecs_vec_append_t(NULL, &state->dead_targets, uint32_t)[0] = entry->atlas;
    }

    cache->atlases --;
    entry->atlas = 0;
    entry->captured = false;
done:
    return;
}

/** Upload the quad meshes of every tile of an atlas, once per world.
 *
 * @param world The world
 * @param cache The cache the handles are kept in
 */
static void box_lod_impostor_tiles(
    ecs_world_t *world,
    box_lod_cache_t *cache)
{
    float step = 1.0f / (float)BOX_LOD_IMPOSTOR_VIEWS;
    int32_t tile;

    for (tile = 0; tile < BOX_LOD_IMPOSTOR_VIEWS; tile ++) {
        box_mesh_data_t data = {0};
        float u0 = (float)tile * step;
        float u1 = u0 + step;
        const float positions[12] = {
            u0, 0.0f, 0.0f, u1, 0.0f, 0.0f, u1, 1.0f, 0.0f, u0, 1.0f, 0.0f
        };
        const float normals[12] = {
            0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1
        };
        const float uvs[8] = { 0, 1, 1, 1, 1, 0, 0, 0 };
        const uint32_t indices[BOX_LOD_TILE_INDICES] = { 0, 2, 1, 0, 3, 2 };

        data.vertex_count = BOX_LOD_TILE_VERTS;
        data.index_count = BOX_LOD_TILE_INDICES;
        data.positions = ecs_os_malloc_n(float, 12);
        data.normals = ecs_os_malloc_n(float, 12);
        data.uvs = ecs_os_malloc_n(float, 8);
        data.indices = ecs_os_malloc_n(uint32_t, BOX_LOD_TILE_INDICES);
        memcpy(data.positions, positions, sizeof(positions));
        memcpy(data.normals, normals, sizeof(normals));
        memcpy(data.uvs, uvs, sizeof(uvs));
        memcpy(data.indices, indices, sizeof(indices));
        data.bounds_min = vec3_new(0, 0, -0.5f);
        data.bounds_max = vec3_new(1, 1, 0.5f);
        cache->tiles[tile] = box_mesh_cache_upload(world,
            BOX_LOD_TILE_HASH + (uint64_t)tile, &data);
        box_mesh_data_fini(&data);
    }

    cache->tiled = true;
}

uint32_t box_lod_impostor_tile(
    ecs_world_t *world,
    box_lod_cache_t *cache,
    int32_t tile)
{
    uint32_t result = 0;
    if (tile < 0 || tile >= BOX_LOD_IMPOSTOR_VIEWS) {
        goto done;
    }

    if (!cache->tiled) {
        box_lod_impostor_tiles(world, cache);
    }

    result = cache->tiles[tile];
done:
    return result;
}

/** Write a transform into the three rows an instance holds.
 *
 * @param m The transform
 * @param out The instance
 */
static void box_lod_impostor_rows(
    const mat4 *m,
    box_instance_t *out)
{
    int32_t row;
    for (row = 0; row < 3; row ++) {
        out->transform[row * 4 + 0] = m->v[0 + row];
        out->transform[row * 4 + 1] = m->v[4 + row];
        out->transform[row * 4 + 2] = m->v[8 + row];
        out->transform[row * 4 + 3] = m->v[12 + row];
    }
}

/** Add one instance of a capture as a batch of its own.
 *
 * @param p The packet
 * @param mesh Mesh handle of the instance
 * @param m Transform of the instance in the space of the capture
 * @param color Linear albedo and opacity
 * @param params Metallic and roughness
 */
static void box_lod_impostor_part(
    box_packet_t *p,
    uint32_t mesh,
    const mat4 *m,
    const float *color,
    const float *params)
{
    box_dynamic_batch_t *batch = box_vec_push(&p->dynamic_batches);
    box_instance_t *inst = box_vec_push(&p->dynamic_instances);

    batch->mesh = mesh;
    batch->first = p->dynamic_instances.count - 1;
    batch->count = 1;
    memset(inst, 0, sizeof(box_instance_t));
    box_lod_impostor_rows(m, inst);
    memcpy(inst->color, color, 4 * sizeof(float));
    inst->params[0] = params[0];
    inst->params[1] = params[1];
    inst->uv_transform[0] = 1.0f;
    inst->uv_transform[1] = 1.0f;
    inst->bounds[3] = 1e9f;
}

/** Place the representation of a node in the tile of one captured direction.
 *
 * @param p The packet
 * @param entry The entry
 * @param tile Index of the tile
 * @param side Width and height one tile stands for, in the space of the node
 */
static void box_lod_impostor_place(
    box_packet_t *p,
    const box_lod_entry_t *entry,
    int32_t tile,
    const float *side)
{
    vec3 center = vec3_scale(vec3_add(entry->local_min, entry->local_max), 0.5f);
    float angle = BOX_LOD_TURN * (float)tile / (float)BOX_LOD_IMPOSTOR_VIEWS;
    mat4 slot = mat4_translation(
        vec3_new(((float)tile + 0.5f) * side[0], side[1] * 0.5f, 0));
    mat4 turn = mat4_rotation_y(-angle);
    mat4 back = mat4_translation(vec3_scale(center, -1.0f));
    mat4 spin = mat4_mul(&turn, &back);
    mat4 place = mat4_mul(&slot, &spin);
    int32_t i, count = ecs_vec_count(&entry->parts);

    if (entry->mesh) {
        mat4 m = mat4_mul(&place, &entry->transform);

        box_lod_impostor_part(p, entry->mesh, &m, entry->color, entry->params);
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const box_lod_part_t *part = ecs_vec_get_t(&entry->parts, box_lod_part_t, i);
        mat4 m = mat4_mul(&place, &part->transform);

        box_lod_impostor_part(p, part->mesh, &m, part->color, part->params);
    }

done:
    return;
}

void box_lod_impostor_capture(
    box_packet_t *p,
    box_lod_entry_t *entry,
    const box_view_t *lit)
{
    float side[2];
    uint32_t first = p->dynamic_batches.count;
    int32_t tile;

    box_lod_impostor_size(entry->local_min, entry->local_max, side);
    if (!entry->atlas || side[0] <= 0.0f || side[1] <= 0.0f) {
        goto done;
    }

    for (tile = 0; tile < BOX_LOD_IMPOSTOR_VIEWS; tile ++) {
        box_lod_impostor_place(p, entry, tile, side);
    }

    float width = side[0] * (float)BOX_LOD_IMPOSTOR_VIEWS;
    float depth = side[0] + side[1];
    vec3 eye = vec3_new(width * 0.5f, side[1] * 0.5f, -depth);
    vec3 at = vec3_new(width * 0.5f, side[1] * 0.5f, 0);
    mat4 view = mat4_look_at(eye, at, vec3_new(0, 1, 0));
    mat4 proj = mat4_orthographic(side[1], width / side[1], depth * 0.01f, depth * 3.0f);
    box_view_t *v = box_vec_push(&p->views);

    memset(v, 0, sizeof(box_view_t));
    memcpy(v->view, view.v, sizeof(view.v));
    memcpy(v->proj, proj.v, sizeof(proj.v));
    memcpy(v->clip, view.v, sizeof(view.v));
    v->eye[0] = eye.x;
    v->eye[1] = eye.y;
    v->eye[2] = eye.z;
    v->env_intensity = lit->env_intensity;
    memcpy(v->env_sky, lit->env_sky, sizeof(v->env_sky));
    memcpy(v->env_ground, lit->env_ground, sizeof(v->env_ground));
    memcpy(v->sun_dir, lit->sun_dir, sizeof(v->sun_dir));
    memcpy(v->sun_color, lit->sun_color, sizeof(v->sun_color));
    v->sun_intensity = lit->sun_intensity;
    v->sun_modulator = lit->sun_modulator;
    v->frame_time = lit->frame_time;
    v->env_generation = lit->env_generation;
    v->cluster = lit->cluster;
    v->cluster.near_ = depth * 0.01f;
    v->cluster.far_ = depth * 3.0f;
    v->target = entry->atlas;
    v->width = (uint32_t)(BOX_LOD_IMPOSTOR_TILE * BOX_LOD_IMPOSTOR_VIEWS);
    v->height = (uint32_t)BOX_LOD_IMPOSTOR_TILE;
    v->dynamic_first = first;
    v->dynamic_count = p->dynamic_batches.count - first;
    v->layer_mask = 0;
    v->caster_mask = 0;
    v->strict_layers = 1;
    v->cull_enabled = 1;
    v->cull_threshold = 0x7fffffffu;
    v->priority = -1;
    entry->captured = true;
done:
    return;
}

int32_t box_lod_impostor_pick(
    const box_lod_billboard_t *bb,
    vec3 eye)
{
    vec3 dir = vec3_sub(eye, bb->center);
    float step = BOX_LOD_TURN / (float)BOX_LOD_IMPOSTOR_VIEWS;
    float base = atan2f(bb->axis.x, bb->axis.z);
    float angle = atan2f(dir.x, dir.z) - base;
    int32_t result;

    while (angle < 0.0f) {
        angle += BOX_LOD_TURN;
    }

    result = (int32_t)(angle / step + 0.5f) % BOX_LOD_IMPOSTOR_VIEWS;

    return result;
}

void box_lod_impostor_billboard(
    const box_lod_billboard_t *bb,
    uint32_t mesh,
    int32_t tile,
    vec3 eye,
    box_transparent_t *out)
{
    vec3 dir = vec3_sub(eye, bb->center);
    vec3 flat = vec3_new(dir.x, 0, dir.z);
    vec3 forward = vec3_length(flat) > 0.0f ? vec3_normalize(flat) : vec3_new(0, 0, -1);
    vec3 right = vec3_normalize(vec3_cross(vec3_new(0, 1, 0), forward));
    float span = bb->size[0] * (float)BOX_LOD_IMPOSTOR_VIEWS;
    float offset = ((float)tile + 0.5f) / (float)BOX_LOD_IMPOSTOR_VIEWS;
    vec3 up = vec3_new(0, 1, 0);
    vec3 col0 = vec3_scale(right, span);
    vec3 col1 = vec3_scale(up, bb->size[1]);
    vec3 col2 = vec3_scale(forward, bb->size[0]);
    vec3 origin = vec3_sub(bb->center, vec3_scale(col0, offset));
    mat4 m = mat4_identity();

    origin = vec3_sub(origin, vec3_scale(col1, 0.5f));
    m.v[0] = col0.x; m.v[1] = col0.y; m.v[2] = col0.z;
    m.v[4] = col1.x; m.v[5] = col1.y; m.v[6] = col1.z;
    m.v[8] = col2.x; m.v[9] = col2.y; m.v[10] = col2.z;
    m.v[12] = origin.x; m.v[13] = origin.y; m.v[14] = origin.z;

    memset(out, 0, sizeof(box_transparent_t));
    out->mesh = mesh;
    out->group = bb->atlas;
    out->blend = BOX_BLEND_PREMULTIPLIED;
    out->back = BOX_BACK_TEXTURED;
    box_lod_impostor_rows(&m, &out->data);
    memcpy(out->data.color, bb->color, 4 * sizeof(float));
    out->data.uv_transform[0] = 1.0f;
    out->data.uv_transform[1] = 1.0f;
    out->data.bounds[0] = bb->center.x;
    out->data.bounds[1] = bb->center.y;
    out->data.bounds[2] = bb->center.z;
    out->data.bounds[3] = bb->radius;
}
