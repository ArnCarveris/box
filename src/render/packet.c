/**
 * @file render/packet.c
 * @brief Render packet storage: appending views, batches, uploads and resource changes.
 */

#include "render.h"
#include <string.h>

void box_render_packet_init(
    box_render_packet_t *p)
{
    ecs_os_memset_t(p, 0, box_render_packet_t);
    ecs_vec_init_t(NULL, &p->resources, box_render_resource_t, 0);
    ecs_vec_init_t(NULL, &p->uploads, box_render_upload_t, 0);
    ecs_vec_init_t(NULL, &p->scatters, box_render_scatter_t, 0);
    ecs_vec_init_t(NULL, &p->views, box_render_view_t, 0);
    ecs_vec_init_t(NULL, &p->batches, box_render_batch_t, 0);
    ecs_vec_init_t(NULL, &p->targets, box_render_target_t, 0);
    ecs_vec_init_t(NULL, &p->data, uint8_t, 0);
}

void box_render_packet_fini(
    box_render_packet_t *p)
{
    ecs_vec_fini_t(NULL, &p->resources, box_render_resource_t);
    ecs_vec_fini_t(NULL, &p->uploads, box_render_upload_t);
    ecs_vec_fini_t(NULL, &p->scatters, box_render_scatter_t);
    ecs_vec_fini_t(NULL, &p->views, box_render_view_t);
    ecs_vec_fini_t(NULL, &p->batches, box_render_batch_t);
    ecs_vec_fini_t(NULL, &p->targets, box_render_target_t);
    ecs_vec_fini_t(NULL, &p->data, uint8_t);
}

void box_render_packet_reset(
    box_render_packet_t *p)
{
    p->frame = 0;
    p->time = 0;
    p->delta = 0;
    p->anisotropy = 0;
    ecs_vec_clear(&p->resources);
    ecs_vec_clear(&p->uploads);
    ecs_vec_clear(&p->scatters);
    ecs_vec_clear(&p->views);
    ecs_vec_clear(&p->batches);
    ecs_vec_clear(&p->targets);
    ecs_vec_clear(&p->data);
    if (ecs_vec_size(&p->data) > BOX_RENDER_DATA_KEEP) {
        ecs_vec_fini_t(NULL, &p->data, uint8_t);
        ecs_vec_init_t(NULL, &p->data, uint8_t, 0);
    }
}

/** Copy one vector of a packet into another, reusing the storage of the destination.
 *
 * @param dst The destination vector
 * @param src The source vector
 * @param size The element size
 */
static void box_render_vec_copy(
    ecs_vec_t *dst,
    const ecs_vec_t *src,
    ecs_size_t size)
{
    int32_t count = ecs_vec_count(src);
    ecs_vec_set_count(NULL, dst, size, count);
    if (count) {
        ecs_os_memcpy(ecs_vec_first(dst), ecs_vec_first(src), size * count);
    }
}

void box_render_packet_copy(
    box_render_packet_t *dst,
    const box_render_packet_t *src)
{
    dst->frame = src->frame;
    dst->time = src->time;
    dst->delta = src->delta;
    dst->anisotropy = src->anisotropy;
    box_render_vec_copy(&dst->resources, &src->resources, ECS_SIZEOF(box_render_resource_t));
    box_render_vec_copy(&dst->uploads, &src->uploads, ECS_SIZEOF(box_render_upload_t));
    box_render_vec_copy(&dst->scatters, &src->scatters, ECS_SIZEOF(box_render_scatter_t));
    box_render_vec_copy(&dst->views, &src->views, ECS_SIZEOF(box_render_view_t));
    box_render_vec_copy(&dst->batches, &src->batches, ECS_SIZEOF(box_render_batch_t));
    box_render_vec_copy(&dst->targets, &src->targets, ECS_SIZEOF(box_render_target_t));
    box_render_vec_copy(&dst->data, &src->data, ECS_SIZEOF(uint8_t));
}

box_render_view_t* box_render_packet_view(
    box_render_packet_t *p)
{
    box_render_view_t *result = ecs_vec_append_t(NULL, &p->views, box_render_view_t);
    ecs_os_memset_t(result, 0, box_render_view_t);

    return result;
}

box_render_batch_t* box_render_packet_batch(
    box_render_packet_t *p)
{
    box_render_batch_t *result = ecs_vec_append_t(NULL, &p->batches, box_render_batch_t);
    ecs_os_memset_t(result, 0, box_render_batch_t);
    result->view = -1;

    return result;
}

box_render_scatter_t* box_render_packet_scatter(
    box_render_packet_t *p)
{
    box_render_scatter_t *result = ecs_vec_append_t(NULL, &p->scatters, box_render_scatter_t);
    ecs_os_memset_t(result, 0, box_render_scatter_t);

    return result;
}

/** Reserve an aligned block in the data of a packet.
 *
 * @param p The packet
 * @param size Bytes to reserve
 * @param offset_out The offset of the block
 * @return The block, NULL when size is 0
 */
static void* box_render_packet_alloc(
    box_render_packet_t *p,
    uint32_t size,
    uint32_t *offset_out)
{
    void *result = NULL;
    int32_t count = ecs_vec_count(&p->data);
    int32_t offset = (count + BOX_RENDER_DATA_ALIGN - 1) & ~(BOX_RENDER_DATA_ALIGN - 1);
    *offset_out = (uint32_t)offset;
    if (!size) {
        goto done;
    }

    ecs_vec_set_count_t(NULL, &p->data, uint8_t, offset + (int32_t)size);
    result = ecs_vec_get_t(&p->data, uint8_t, offset);
done:
    return result;
}

void* box_render_packet_upload(
    box_render_packet_t *p,
    uint32_t buffer,
    uint32_t offset,
    uint32_t size)
{
    box_render_upload_t *upload = ecs_vec_append_t(NULL, &p->uploads, box_render_upload_t);
    upload->buffer = buffer;
    upload->offset = offset;
    upload->size = size;

    return box_render_packet_alloc(p, size, &upload->data);
}

box_render_target_t* box_render_packet_target(
    box_render_packet_t *p,
    uint32_t handle,
    const char *capture)
{
    uint32_t size = capture ? (uint32_t)ecs_os_strlen(capture) + 1 : 0;
    uint32_t offset = 0;
    char *path = box_render_packet_alloc(p, size, &offset);
    box_render_target_t *result = ecs_vec_append_t(NULL, &p->targets, box_render_target_t);
    ecs_os_memset_t(result, 0, box_render_target_t);
    result->handle = handle;
    result->capture = offset;
    result->capture_size = size;
    if (path) {
        ecs_os_memcpy(path, capture, (ecs_size_t)size);
    }

    return result;
}

void* box_render_packet_resource(
    box_render_packet_t *p,
    box_render_resource_kind_t kind,
    box_render_resource_op_t op,
    uint32_t handle,
    uint32_t size)
{
    box_render_resource_t *resource = ecs_vec_append_t(
        NULL, &p->resources, box_render_resource_t);
    resource->kind = kind;
    resource->op = op;
    resource->handle = handle;
    resource->size = size;

    return box_render_packet_alloc(p, size, &resource->data);
}

const void* box_render_packet_data(
    const box_render_packet_t *p,
    uint32_t offset)
{
    return ECS_OFFSET(p->data.array, offset);
}
