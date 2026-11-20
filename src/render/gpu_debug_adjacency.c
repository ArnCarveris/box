/**
 * @file render/gpu_debug_adjacency.c
 * @brief Edge adjacency of the meshes of the webgpu backend for the wireframe of a view
 * (etc/shaders/debug_wire.wgsl): the triangle across each edge of each triangle, over the
 * vertices welded by position, so a wire draw tells the edges of the silhouette and of the
 * border of a mesh.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Distance up to which the vertices of a mesh weld, relative to its largest coordinate: wider
 * than the rounding of the positions a seam computes twice (the sine of 0 and of 2 pi), far
 * below the size of a triangle. */
#define BOX_GPU_DEBUG_WELD (1e-5f)

/** Get the cell of a weld grid a coordinate is in.
 *
 * @param x The coordinate
 * @param cell The size of a cell
 * @return The cell
 */
static int32_t box_gpu_debug_cell(
    float x,
    float cell)
{
    return (int32_t)floorf(x / cell);
}

/** Hash of a cell of the weld grid.
 *
 * @param c The cell
 * @return The hash
 */
static uint64_t box_gpu_debug_cell_hash(
    const int32_t *c)
{
    uint64_t h = 1469598103934665603ull;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        h = (h ^ (uint32_t)c[i]) * 1099511628211ull;
        h ^= h >> 29;
    }

    return h;
}

/** Find a vertex within the weld distance of a position in a cell of the weld grid.
 *
 * @param vertices The vertices
 * @param table The open addressing hash table of the vertices by cell, vertex + 1, 0 when empty
 * @param size The number of slots of the table, a power of two
 * @param p The position
 * @param c The cell
 * @param cell The size of a cell
 * @param eps The weld distance
 * @return The slot of the vertex, or the empty slot that ends the probe when there is none
 */
static uint32_t box_gpu_debug_weld_find(
    const box_vertex_t *vertices,
    const uint32_t *table,
    uint32_t size,
    const float *p,
    const int32_t *c,
    float cell,
    float eps)
{
    uint32_t slot = (uint32_t)box_gpu_debug_cell_hash(c) & (size - 1);
    while (table[slot]) {
        const float *q = vertices[table[slot] - 1].position;
        if (box_gpu_debug_cell(q[0], cell) == c[0] &&
            box_gpu_debug_cell(q[1], cell) == c[1] &&
            box_gpu_debug_cell(q[2], cell) == c[2] &&
            fabsf(p[0] - q[0]) <= eps && fabsf(p[1] - q[1]) <= eps &&
            fabsf(p[2] - q[2]) <= eps)
        {
            break;
        }

        slot = (slot + 1) & (size - 1);
    }

    return slot;
}

/** Weld the vertices of a mesh by position: for each, the first vertex within the weld distance
 * (BOX_GPU_DEBUG_WELD), found in a grid of cells twice that size hashed by cell, in the cell of
 * the vertex and those its weld distance reaches.
 *
 * @param vertices The vertices
 * @param vertex_count Number of vertices
 * @return The vertex each vertex welds to, freed with ecs_os_free
 */
static uint32_t* box_gpu_debug_weld(
    const box_vertex_t *vertices,
    uint32_t vertex_count)
{
    uint32_t *weld = ecs_os_malloc_n(uint32_t, vertex_count);
    uint32_t size = 1, i, k;
    float largest = 0.0f;
    while (size < vertex_count * 2u) {
        size <<= 1;
    }

    for (i = 0; i < vertex_count; i ++) {
        for (k = 0; k < 3; k ++) {
            float x = fabsf(vertices[i].position[k]);
            largest = isfinite(x) && x > largest ? x : largest;
        }
    }

    float eps = BOX_GPU_DEBUG_WELD * (largest > 1e-30f ? largest : 1e-30f);
    float cell = eps * 2.0f;
    uint32_t *table = ecs_os_calloc_n(uint32_t, size);
    for (i = 0; i < vertex_count; i ++) {
        const float *p = vertices[i].position;
        weld[i] = i;
        if (!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2])) {
            continue;
        }

        int32_t lo[3], hi[3], c[3];
        for (k = 0; k < 3; k ++) {
            lo[k] = box_gpu_debug_cell(p[k] - eps, cell);
            hi[k] = box_gpu_debug_cell(p[k] + eps, cell);
        }

        uint32_t slot = 0;
        bool found = false;
        for (k = 0; k < 8 && !found; k ++) {
            c[0] = k & 1 ? hi[0] : lo[0];
            c[1] = k & 2 ? hi[1] : lo[1];
            c[2] = k & 4 ? hi[2] : lo[2];
            if (((k & 1) && hi[0] == lo[0]) || ((k & 2) && hi[1] == lo[1]) ||
                ((k & 4) && hi[2] == lo[2]))
            {
                continue;
            }

            slot = box_gpu_debug_weld_find(vertices, table, size, p, c, cell, eps);
            found = table[slot] != 0;
        }

        if (found) {
            weld[i] = table[slot] - 1;
            continue;
        }

        for (k = 0; k < 3; k ++) {
            c[k] = box_gpu_debug_cell(p[k], cell);
        }

        slot = (uint32_t)box_gpu_debug_cell_hash(c) & (size - 1);
        while (table[slot]) {
            slot = (slot + 1) & (size - 1);
        }

        table[slot] = i + 1;
    }

    ecs_os_free(table);

    return weld;
}

/** Compute the edge adjacency of a mesh by matching the edges of its welded vertices with a
 * map of their sorted pairs.
 *
 * The edge opposite corner k of a triangle is the one between its corners k + 1 and k + 2, so
 * the neighbor across the edge e gets the vertex indices[e]. An edge of more than two triangles
 * keeps the first one as the neighbor of the others.
 *
 * A triangle with two vertices at one position draws nothing and is no neighbor, so the
 * triangles around the pole of a sphere or the center of a cap join each other.
 *
 * @param vertices The vertices
 * @param vertex_count Number of vertices
 * @param indices The indices, three per triangle
 * @param index_count Number of indices
 * @return One neighbor per index, freed with ecs_os_free
 */
static uint32_t* box_gpu_debug_adjacency(
    const box_vertex_t *vertices,
    uint32_t vertex_count,
    const uint32_t *indices,
    uint32_t index_count)
{
    uint32_t *result = ecs_os_malloc_n(uint32_t, index_count);
    uint32_t *weld = box_gpu_debug_weld(vertices, vertex_count);
    uint32_t i, k;
    ecs_map_t edges;
    ecs_map_init(&edges, NULL);
    for (i = 0; i < index_count; i ++) {
        result[i] = BOX_GPU_WIRE_NONE;
    }

    for (i = 0; i + 2 < index_count; i += 3) {
        if (indices[i] >= vertex_count || indices[i + 1] >= vertex_count ||
            indices[i + 2] >= vertex_count)
        {
            continue;
        }

        uint32_t w[3] = {weld[indices[i]], weld[indices[i + 1]], weld[indices[i + 2]]};
        if (w[0] == w[1] || w[1] == w[2] || w[2] == w[0]) {
            continue;
        }

        for (k = 0; k < 3; k ++) {
            uint32_t a = w[(k + 1) % 3];
            uint32_t b = w[(k + 2) % 3];
            uint64_t key = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
            uint32_t e = i + k;
            ecs_map_val_t *other = ecs_map_get(&edges, key);
            if (!other) {
                ecs_map_insert(&edges, key, e);
                continue;
            }

            uint32_t o = (uint32_t)*other;
            result[e] = indices[o];
            if (result[o] == BOX_GPU_WIRE_NONE) {
                result[o] = indices[e];
            }
        }
    }

    ecs_map_fini(&edges);
    ecs_os_free(weld);

    return result;
}

void box_gpu_debug_mesh_adjacency(
    box_gpu_t *gpu,
    box_gpu_mesh_t *m,
    const box_vertex_t *vertices,
    uint32_t vertex_count,
    const uint32_t *indices)
{
    if (!m->wireable) {
        goto done;
    }

    uint64_t size = (uint64_t)m->index_count * sizeof(uint32_t);
    uint32_t *adjacency = box_gpu_debug_adjacency(vertices, vertex_count, indices,
        m->index_count);
    m->adjacency = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = size * 2
    });
    wgpuQueueWriteBuffer(gpu->queue, m->adjacency, 0, indices, (size_t)size);
    wgpuQueueWriteBuffer(gpu->queue, m->adjacency, size, adjacency, (size_t)size);
    ecs_os_free(adjacency);
done:
    return;
}

#endif
