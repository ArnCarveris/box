/**
 * @file terrain/tile_shape.c
 * @brief The TerrainTile shape: the grid mesh of a tile with its skirts, its key, scale and
 * surface, and the sizes of the heights of a tile.
 */

#include "terrain.h"
#include <math.h>

int32_t box_terrain_vertices(
    int32_t resolution)
{
    int32_t result = resolution > 0 ? resolution : 65;
    result = result < 5 ? 5 : result > 257 ? 257 : result;

    return result + ((result & 1) ? 0 : 1);
}

int32_t box_terrain_coarse_texels(
    int32_t resolution)
{
    return (box_terrain_vertices(resolution) + 2 * BOX_TERRAIN_BORDER + 1) / 2;
}

uint32_t box_terrain_row_bytes(
    int32_t resolution)
{
    uint32_t texels = (uint32_t)box_terrain_vertices(resolution) + 2u * BOX_TERRAIN_BORDER;

    return (texels * 4u + 255u) & ~255u;
}

void box_terrain_tile_surface(
    const TerrainTile *tile,
    box_surface_data_t *data,
    vec3 min,
    vec3 max)
{
    int32_t resolution = box_terrain_vertices(tile->resolution);
    data->p[0][0] = BOX_TERRAIN_SURFACE;
    data->p[1][0] = (float)tile->slot;
    data->p[1][1] = (float)(box_terrain_row_bytes(resolution) / 4u);
    data->p[1][2] = (float)(resolution + 2 * BOX_TERRAIN_BORDER);
    data->p[1][3] = (float)resolution;
    data->p[2][0] = tile->focus[0];
    data->p[2][1] = tile->focus[1];
    data->p[2][2] = tile->morph[0];
    data->p[2][3] = tile->morph[1];
    data->p[3][0] = tile->masked ? 1.0f : 0.0f;
    data->p[4][0] = tile->size;
    data->p[4][1] = tile->skirt;
    data->p[4][2] = (float)tile->level;
    data->p[4][3] = tile->bounded ? 1.0f : 0.0f;
    data->p[5][0] = tile->clip[0];
    data->p[5][1] = tile->clip[1];
    data->p[5][2] = tile->clip[2];
    data->p[5][3] = tile->clip[3];
    glm_vec3_copy((vec3){ -0.5f, tile->min_height - tile->skirt, -0.5f }, min);
    glm_vec3_copy((vec3){ 0.5f, tile->max_height, 0.5f }, max);
}

/** Key of the mesh of a TerrainTile: its vertices per edge.
 *
 * @param value The tile
 * @param key The key
 */
static void box_terrain_tile_key(
    const void *value,
    box_shape_key_t *key)
{
    const TerrainTile *t = value;
    key->i[0] = box_terrain_vertices(t->resolution);
}

/** Scale of a TerrainTile: its edge along x and z.
 *
 * @param value The tile
 * @param scale The scale
 */
static void box_terrain_tile_scale(
    const void *value,
    vec3 scale)
{
    const TerrainTile *t = value;
    float size = isfinite(t->size) && t->size > 0.0f ? t->size : 1.0f;
    glm_vec3_copy((vec3){ size, 1.0f, size }, scale);
}

/** Surface of a TerrainTile.
 *
 * @param value The tile
 * @param data The surface
 * @param min The minimum corner of the local bounds
 * @param max The maximum corner of the local bounds
 */
static void box_terrain_tile_data(
    const void *value,
    box_surface_data_t *data,
    vec3 min,
    vec3 max)
{
    box_terrain_tile_surface(value, data, min, max);
}

/** Append the skirt of an edge of the grid: a copy of each vertex of the edge at y -1 and the
 * quads between them, facing out.
 *
 * @param mesh The mesh
 * @param n Vertices per edge
 * @param start Index of the first vertex of the edge
 * @param stride Index step along the edge
 * @param outward Direction out of the grid across the edge
 */
static void box_terrain_skirt(
    box_mesh_t *mesh,
    int32_t n,
    int32_t start,
    int32_t stride,
    const vec3 outward)
{
    int32_t first = mesh->vertex_count, k;
    for (k = 0; k < n; k ++) {
        const box_vertex_t *top = &mesh->vertices[start + k * stride];
        vec3 position = { top->position[0], -1.0f, top->position[2] };
        box_mesh_vertex(mesh, position, (vec3){ 0.0f, 1.0f, 0.0f }, top->uv[0], top->uv[1]);
    }

    vec3 a, b, normal;
    glm_vec3_sub(mesh->vertices[first].position, mesh->vertices[start].position, a);
    glm_vec3_sub(mesh->vertices[start + stride].position, mesh->vertices[start].position, b);
    glm_vec3_cross(a, b, normal);
    bool flip = glm_vec3_dot(normal, ECS_CONST_CAST(float*, outward)) < 0.0f;
    for (k = 0; k < n - 1; k ++) {
        int32_t t0 = start + k * stride, t1 = start + (k + 1) * stride;
        int32_t s0 = first + k, s1 = first + k + 1;
        if (flip) {
            box_mesh_triangle(mesh, t0, t1, s0);
            box_mesh_triangle(mesh, t1, s1, s0);
        } else {
            box_mesh_triangle(mesh, t0, s0, t1);
            box_mesh_triangle(mesh, t1, s0, s1);
        }
    }
}

/** Generate the grid of a TerrainTile: n x n vertices over the unit square in XZ, each cell split
 * from its lower to its higher corner as the morphed cells are, skirts at y -1 below the edges,
 * and one surface asset, so its draws bind the heights the backend copies into the assets.
 *
 * @param value The tile
 * @param key The key
 * @param mesh The mesh
 */
static void box_terrain_tile_generate(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    int32_t n = box_terrain_vertices(key->i[0]), i, j;
    float last = (float)(n - 1);
    (void)value;
    for (j = 0; j < n; j ++) {
        for (i = 0; i < n; i ++) {
            float u = (float)i / last, v = (float)j / last;
            box_mesh_vertex(mesh, (vec3){ u - 0.5f, 0.0f, v - 0.5f },
                (vec3){ 0.0f, 1.0f, 0.0f }, u, v);
        }
    }

    for (j = 0; j < n - 1; j ++) {
        for (i = 0; i < n - 1; i ++) {
            int32_t a = j * n + i, b = a + 1, c = a + n + 1, d = a + n;
            box_mesh_triangle(mesh, a, c, b);
            box_mesh_triangle(mesh, a, d, c);
        }
    }

    box_terrain_skirt(mesh, n, 0, 1, (vec3){ 0.0f, 0.0f, -1.0f });
    box_terrain_skirt(mesh, n, n - 1, n, (vec3){ 1.0f, 0.0f, 0.0f });
    box_terrain_skirt(mesh, n, (n - 1) * n, 1, (vec3){ 0.0f, 0.0f, 1.0f });
    box_terrain_skirt(mesh, n, 0, n, (vec3){ -1.0f, 0.0f, 0.0f });
    mesh->surface_assets = ecs_os_calloc_n(vec4, 1);
    mesh->surface_asset_count = 1;
}

void box_terrain_tile_shape(
    ecs_world_t *world)
{
    box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(TerrainTile),
        .key = box_terrain_tile_key,
        .generate = box_terrain_tile_generate,
        .scale = box_terrain_tile_scale,
        .surface = box_terrain_tile_data
    });
}
