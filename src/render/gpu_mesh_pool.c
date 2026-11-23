/**
 * @file render/gpu_mesh_pool.c
 * @brief Pool of large buffers the vertices and indices of meshes are suballocated from.
 *
 * Each page is a buffer with a sorted list of free ranges. A range is taken from the first page
 * that fits it, and a new page is created when none does. The per vertex data of a mesh that has
 * it is a range of its own after its vertices, a vertex stream only its draws bind.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Bytes of a page of the mesh pool, unless a range needs a larger one */
#define BOX_GPU_MESH_PAGE_SIZE (32ull * 1024 * 1024)

/** Round a size up to a multiple of BOX_GPU_MESH_ALIGN.
 *
 * @param size Bytes
 * @return The rounded size
 */
static uint64_t box_gpu_mesh_pool_align(
    uint64_t size)
{
    return (size + BOX_GPU_MESH_ALIGN - 1) & ~(uint64_t)(BOX_GPU_MESH_ALIGN - 1);
}

/** Take a range from the first free range of a page that fits it.
 *
 * @param pg The page
 * @param size Bytes of the range, aligned
 * @param offset Receives the offset of the range
 * @return Whether the page had a free range that fits
 */
static bool box_gpu_mesh_pool_take(
    box_gpu_mesh_page_t *pg,
    uint64_t size,
    uint64_t *offset)
{
    int32_t i, count = ecs_vec_count(&pg->free);
    bool result = false;
    for (i = 0; i < count; i ++) {
        box_gpu_mesh_range_t *r = ecs_vec_get_t(&pg->free, box_gpu_mesh_range_t, i);
        if (r->size < size) {
            continue;
        }

        *offset = r->offset;
        r->offset += size;
        r->size -= size;
        if (!r->size) {
            ecs_os_memmove_n(r, r + 1, box_gpu_mesh_range_t, count - i - 1);
            ecs_vec_remove_last(&pg->free);
        }

        result = true;
        break;
    }

    return result;
}

/** Add a page to the mesh pool with a single free range over its buffer.
 *
 * @param gpu The backend
 * @param size Bytes the page must hold at least
 * @return The page, whose buffer is NULL when it could not be created
 */
static box_gpu_mesh_page_t* box_gpu_mesh_pool_page_new(
    box_gpu_t *gpu,
    uint64_t size)
{
    box_gpu_mesh_page_t *pg = ecs_vec_append_t(NULL, &gpu->mesh_pages, box_gpu_mesh_page_t);
    pg->size = size > BOX_GPU_MESH_PAGE_SIZE ? size : BOX_GPU_MESH_PAGE_SIZE;
    pg->buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Index |
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = pg->size
    });
    ecs_vec_init_t(NULL, &pg->free, box_gpu_mesh_range_t, 1);
    *ecs_vec_append_t(NULL, &pg->free, box_gpu_mesh_range_t) = (box_gpu_mesh_range_t){
        .offset = 0,
        .size = pg->size
    };

    return pg;
}

bool box_gpu_mesh_pool_alloc(
    box_gpu_t *gpu,
    uint64_t size,
    int32_t *page,
    uint64_t *offset)
{
    int32_t i, count = ecs_vec_count(&gpu->mesh_pages);
    bool result = false;
    size = box_gpu_mesh_pool_align(size);
    for (i = 0; i < count; i ++) {
        box_gpu_mesh_page_t *pg = ecs_vec_get_t(&gpu->mesh_pages, box_gpu_mesh_page_t, i);
        if (box_gpu_mesh_pool_take(pg, size, offset)) {
            *page = i;
            result = true;
            goto done;
        }
    }

    box_gpu_mesh_page_t *pg = box_gpu_mesh_pool_page_new(gpu, size);
    if (pg->buffer && box_gpu_mesh_pool_take(pg, size, offset)) {
        *page = count;
        result = true;
    }

done:
    return result;
}

void box_gpu_mesh_pool_free(
    box_gpu_t *gpu,
    int32_t page,
    uint64_t offset,
    uint64_t size)
{
    box_gpu_mesh_page_t *pg = ecs_vec_get_t(&gpu->mesh_pages, box_gpu_mesh_page_t, page);
    int32_t i, count = ecs_vec_count(&pg->free);
    size = box_gpu_mesh_pool_align(size);
    for (i = 0; i < count; i ++) {
        if (ecs_vec_get_t(&pg->free, box_gpu_mesh_range_t, i)->offset > offset) {
            break;
        }
    }

    box_gpu_mesh_range_t *prev = i ? ecs_vec_get_t(&pg->free, box_gpu_mesh_range_t, i - 1) : NULL;
    box_gpu_mesh_range_t *next = i < count ?
        ecs_vec_get_t(&pg->free, box_gpu_mesh_range_t, i) : NULL;
    if (prev && prev->offset + prev->size == offset) {
        prev->size += size;
        if (next && offset + size == next->offset) {
            prev->size += next->size;
            ecs_os_memmove_n(next, next + 1, box_gpu_mesh_range_t, count - i - 1);
            ecs_vec_remove_last(&pg->free);
        }
    } else if (next && offset + size == next->offset) {
        next->offset = offset;
        next->size += size;
    } else {
        ecs_vec_append_t(NULL, &pg->free, box_gpu_mesh_range_t);
        box_gpu_mesh_range_t *r = ecs_vec_get_t(&pg->free, box_gpu_mesh_range_t, i);
        ecs_os_memmove_n(r + 1, r, box_gpu_mesh_range_t, count - i);
        *r = (box_gpu_mesh_range_t){ .offset = offset, .size = size };
    }
}

void box_gpu_mesh_bounds(
    box_gpu_mesh_t *m,
    const box_vertex_t *vertices,
    uint32_t count)
{
    vec3 lo, hi;
    uint32_t i;
    glm_vec3_copy(ECS_CONST_CAST(float*, vertices[0].position), lo);
    glm_vec3_copy(lo, hi);
    for (i = 1; i < count; i ++) {
        glm_vec3_minv(lo, ECS_CONST_CAST(float*, vertices[i].position), lo);
        glm_vec3_maxv(hi, ECS_CONST_CAST(float*, vertices[i].position), hi);
    }

    glm_vec3_center(lo, hi, m->center);
    glm_vec3_sub(hi, m->center, m->extent);
}

bool box_gpu_vertex_data(
    const box_gpu_t *gpu,
    const box_render_batch_t *b)
{
    const box_gpu_mesh_t *m = box_render_is_mesh(b->pipeline)
        ? ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh) : NULL;

    return m && m->data_size;
}

WGPUVertexBufferLayout box_gpu_vertex_data_layout(
    WGPUVertexAttribute *attrs)
{
    attrs[0] = (WGPUVertexAttribute){
        .format = WGPUVertexFormat_Float32x4, .offset = 0, .shaderLocation = 4
    };
    attrs[1] = (WGPUVertexAttribute){
        .format = WGPUVertexFormat_Float32x4, .offset = sizeof(vec4), .shaderLocation = 5
    };
    attrs[2] = (WGPUVertexAttribute){
        .format = WGPUVertexFormat_Float32x4, .offset = 2 * sizeof(vec4), .shaderLocation = 6
    };
    attrs[3] = (WGPUVertexAttribute){
        .format = WGPUVertexFormat_Float32x4, .offset = 3 * sizeof(vec4), .shaderLocation = 7
    };

    return (WGPUVertexBufferLayout){
        .stepMode = WGPUVertexStepMode_Vertex,
        .arrayStride = BOX_RENDER_DATA_VECS * sizeof(vec4),
        .attributeCount = BOX_RENDER_DATA_VECS,
        .attributes = attrs
    };
}

WGPUBuffer box_gpu_mesh_pool_buffer(
    const box_gpu_t *gpu,
    int32_t page)
{
    return ecs_vec_get_t(&gpu->mesh_pages, box_gpu_mesh_page_t, page)->buffer;
}

void box_gpu_mesh_pool_fini(
    box_gpu_t *gpu)
{
    int32_t i, count = ecs_vec_count(&gpu->mesh_pages);
    for (i = 0; i < count; i ++) {
        box_gpu_mesh_page_t *pg = ecs_vec_get_t(&gpu->mesh_pages, box_gpu_mesh_page_t, i);
        if (pg->buffer) {
            wgpuBufferRelease(pg->buffer);
        }

        ecs_vec_fini_t(NULL, &pg->free, box_gpu_mesh_range_t);
    }

    ecs_vec_fini_t(NULL, &gpu->mesh_pages, box_gpu_mesh_page_t);
}

#endif
