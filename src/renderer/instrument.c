/**
 * @file renderer/instrument.c
 * @brief Counts the WebGPU calls of a frame and tracks the resources of every device.
 *
 * A registry keyed by handle keeps the bytes, usage, label and topology of every
 * resource and pipeline the renderer creates. The frame being counted is found
 * through a thread local pointer that is only set while a frame is counted.
 */

#define BOX_RENDERER_INSTRUMENT_IMPL
#include "renderer_internal.h"
#include <stdlib.h>
#include <string.h>

/** Devices whose resources the registry can total at once */
#define BOX_COUNT_DEVICES (8)

/** Entries the registry starts with, a power of two */
#define BOX_COUNT_REGISTRY_MIN (256)

/** Bytes of the arguments of an indexed indirect draw */
#define BOX_COUNT_INDEXED_ARGS (20)

/** Bytes of the arguments of an indirect draw */
#define BOX_COUNT_DRAW_ARGS (16)

/** Kinds of objects the registry holds */
typedef enum box_count_kind_t {
    BOX_COUNT_KIND_BUFFER = 1,  /**< A buffer */
    BOX_COUNT_KIND_TEXTURE = 2, /**< A texture */
    BOX_COUNT_KIND_QUERY = 3,   /**< A query set */
    BOX_COUNT_KIND_RENDER = 4,  /**< A render pipeline */
    BOX_COUNT_KIND_COMPUTE = 5  /**< A compute pipeline */
} box_count_kind_t;

/** What the registry keeps of one resource or pipeline */
typedef struct box_count_object_t {
    const void *key;               /**< The handle, NULL for a free slot */
    const void *device;            /**< Device that created it */
    box_count_kind_t kind;         /**< What it is */
    uint32_t usage;                /**< Usage class of a buffer */
    uint64_t bytes;                /**< Bytes a buffer, texture or query set holds */
    box_count_topology_t topology; /**< Primitive kind of a render pipeline */
    char name[BOX_COUNT_NAME_MAX]; /**< Label of a pipeline */
} box_count_object_t;

/** Resources one device holds */
typedef struct box_count_memory_t {
    const void *device;                             /**< The device, NULL when unused */
    uint32_t buffers;                               /**< Live buffers */
    uint64_t buffer_bytes;                          /**< Bytes they hold */
    uint32_t textures;                              /**< Live textures */
    uint64_t texture_bytes;                         /**< Bytes they hold */
    uint64_t query_bytes;                           /**< Bytes the query sets hold */
    uint32_t usage_buffers[BOX_COUNT_USAGE_COUNT];  /**< Live buffers per usage class */
    uint64_t usage_bytes[BOX_COUNT_USAGE_COUNT];    /**< Bytes they hold per usage class */
} box_count_memory_t;

/** Handles the renderer created, with linear probing, and the totals per device */
typedef struct box_count_registry_t {
    box_count_object_t *objects;                   /**< Slots, capacity of them */
    uint32_t capacity;                             /**< Slots, a power of two */
    uint32_t count;                                /**< Slots in use */
    box_count_memory_t devices[BOX_COUNT_DEVICES]; /**< Totals per device */
} box_count_registry_t;

/** The registry of every renderer of the process */
static box_count_registry_t box_count_registry;

#ifndef BOX_PLATFORM_WEB
/** Guards the registry, which renderer threads share */
static pthread_mutex_t box_count_registry_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/** Frame the calls of this thread are counted into, NULL while none is counted */
static _Thread_local box_count_state_t *box_count_current;

/** Take the registry lock. */
static void box_count_registry_enter(void)
{
#ifndef BOX_PLATFORM_WEB
    pthread_mutex_lock(&box_count_registry_lock);
#endif
}

/** Release the registry lock. */
static void box_count_registry_leave(void)
{
#ifndef BOX_PLATFORM_WEB
    pthread_mutex_unlock(&box_count_registry_lock);
#endif
}

/** Slot a handle hashes to.
 *
 * @param key The handle
 * @param capacity Slots of the table, a power of two
 * @return The slot
 */
static uint32_t box_count_hash(
    const void *key,
    uint32_t capacity)
{
    uint64_t h = (uint64_t)(uintptr_t)key;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;

    return (uint32_t)h & (capacity - 1);
}

/** Slot of a handle in the registry.
 *
 * @param key The handle
 * @return The slot, -1 when the registry does not hold it
 */
static int32_t box_count_find(
    const void *key)
{
    int32_t result = -1;
    box_count_registry_t *reg = &box_count_registry;
    if (!reg->capacity || !key) {
        goto done;
    }

    uint32_t slot = box_count_hash(key, reg->capacity);
    while (reg->objects[slot].key) {
        if (reg->objects[slot].key == key) {
            result = (int32_t)slot;
            goto done;
        }

        slot = (slot + 1) & (reg->capacity - 1);
    }

done:
    return result;
}

/** Put an object in a table without growing it.
 *
 * @param objects The slots
 * @param capacity Slots of the table
 * @param obj The object
 */
static void box_count_place(
    box_count_object_t *objects,
    uint32_t capacity,
    const box_count_object_t *obj)
{
    uint32_t slot = box_count_hash(obj->key, capacity);
    while (objects[slot].key && objects[slot].key != obj->key) {
        slot = (slot + 1) & (capacity - 1);
    }

    objects[slot] = *obj;
}

/** Grow the registry so it stays at most half full.
 *
 * @return false when the memory for the larger table could not be had
 */
static bool box_count_grow(void)
{
    bool result = true;
    box_count_registry_t *reg = &box_count_registry;
    if ((reg->count + 1) * 2 <= reg->capacity) {
        goto done;
    }

    uint32_t capacity = reg->capacity ? reg->capacity * 2 : BOX_COUNT_REGISTRY_MIN;
    box_count_object_t *objects = calloc(capacity, sizeof(box_count_object_t));
    if (!objects) {
        result = false;
        goto done;
    }

    uint32_t i;
    for (i = 0; i < reg->capacity; i ++) {
        if (reg->objects[i].key) {
            box_count_place(objects, capacity, &reg->objects[i]);
        }
    }

    free(reg->objects);
    reg->objects = objects;
    reg->capacity = capacity;
done:
    return result;
}

/** Remove the object of a slot, moving the ones after it back into the gap.
 *
 * @param slot The slot
 */
static void box_count_erase(
    uint32_t slot)
{
    box_count_registry_t *reg = &box_count_registry;
    uint32_t mask = reg->capacity - 1;
    uint32_t gap = slot;
    uint32_t next = (slot + 1) & mask;
    while (reg->objects[next].key) {
        uint32_t home = box_count_hash(reg->objects[next].key, reg->capacity);
        bool moves = ((next - home) & mask) >= ((next - gap) & mask);
        if (moves) {
            reg->objects[gap] = reg->objects[next];
            gap = next;
        }

        next = (next + 1) & mask;
    }

    memset(&reg->objects[gap], 0, sizeof(box_count_object_t));
    reg->count --;
}

/** Totals of a device, taking a free entry for a device that has none.
 *
 * @param device The device
 * @param add Take a free entry when the device has none
 * @return The totals, NULL when the device has none and none could be taken
 */
static box_count_memory_t* box_count_memory(
    const void *device,
    bool add)
{
    box_count_memory_t *result = NULL;
    box_count_memory_t *free_entry = NULL;
    uint32_t i;
    for (i = 0; i < BOX_COUNT_DEVICES; i ++) {
        box_count_memory_t *m = &box_count_registry.devices[i];
        if (m->device == device) {
            result = m;
            goto done;
        }

        if (!m->device && !free_entry) {
            free_entry = m;
        }
    }

    if (add && free_entry) {
        memset(free_entry, 0, sizeof(*free_entry));
        free_entry->device = device;
        result = free_entry;
    }

done:
    return result;
}

/** Add or subtract a resource from the totals of its device.
 *
 * @param obj The resource
 * @param sign 1 to add, -1 to subtract
 */
static void box_count_account(
    const box_count_object_t *obj,
    int32_t sign)
{
    box_count_memory_t *m = box_count_memory(obj->device, sign > 0);
    if (!m) {
        goto done;
    }

    int64_t bytes = sign > 0 ? (int64_t)obj->bytes : -(int64_t)obj->bytes;
    if (obj->kind == BOX_COUNT_KIND_BUFFER) {
        m->buffers += (uint32_t)sign;
        m->buffer_bytes += (uint64_t)bytes;
        m->usage_buffers[obj->usage] += (uint32_t)sign;
        m->usage_bytes[obj->usage] += (uint64_t)bytes;
    } else if (obj->kind == BOX_COUNT_KIND_TEXTURE) {
        m->textures += (uint32_t)sign;
        m->texture_bytes += (uint64_t)bytes;
    } else if (obj->kind == BOX_COUNT_KIND_QUERY) {
        m->query_bytes += (uint64_t)bytes;
    }

done:
    return;
}

/** Keep a created resource or pipeline in the registry.
 *
 * @param obj What to keep of it
 */
static void box_count_track(
    const box_count_object_t *obj)
{
    if (!obj->key) {
        goto done;
    }

    box_count_registry_enter();
    int32_t slot = box_count_find(obj->key);
    if (slot >= 0) {
        box_count_account(&box_count_registry.objects[slot], -1);
        box_count_erase((uint32_t)slot);
    }

    if (box_count_grow()) {
        box_count_place(box_count_registry.objects, box_count_registry.capacity, obj);
        box_count_registry.count ++;
        box_count_account(obj, 1);
    }

    box_count_registry_leave();
done:
    return;
}

/** Drop a released resource or pipeline from the registry.
 *
 * @param key The handle
 */
static void box_count_untrack(
    const void *key)
{
    if (!key) {
        goto done;
    }

    box_count_registry_enter();
    int32_t slot = box_count_find(key);
    if (slot >= 0) {
        box_count_account(&box_count_registry.objects[slot], -1);
        box_count_erase((uint32_t)slot);
    }

    box_count_registry_leave();
done:
    return;
}

/** Drop every object of a device and its totals.
 *
 * @param device The device
 */
static void box_count_purge(
    const void *device)
{
    box_count_registry_enter();
    box_count_registry_t *reg = &box_count_registry;
    uint32_t i = 0;
    while (i < reg->capacity) {
        if (reg->objects[i].key && reg->objects[i].device == device) {
            box_count_erase(i);
        } else {
            i ++;
        }
    }

    box_count_memory_t *m = box_count_memory(device, false);
    if (m) {
        memset(m, 0, sizeof(*m));
    }

    box_count_registry_leave();
}

/** Usage class of a buffer.
 *
 * @param usage Usage flags of the buffer
 * @return The class
 */
static uint32_t box_count_usage(
    WGPUBufferUsage usage)
{
    uint32_t result = BOX_COUNT_USAGE_OTHER;
    if (usage & WGPUBufferUsage_Indirect) {
        result = BOX_COUNT_USAGE_INDIRECT;
    } else if (usage & WGPUBufferUsage_Vertex) {
        result = BOX_COUNT_USAGE_VERTEX;
    } else if (usage & WGPUBufferUsage_Index) {
        result = BOX_COUNT_USAGE_INDEX;
    } else if (usage & WGPUBufferUsage_Uniform) {
        result = BOX_COUNT_USAGE_UNIFORM;
    } else if (usage & WGPUBufferUsage_Storage) {
        result = BOX_COUNT_USAGE_STORAGE;
    } else if (usage & WGPUBufferUsage_MapRead) {
        result = BOX_COUNT_USAGE_READBACK;
    } else if (usage & WGPUBufferUsage_QueryResolve) {
        result = BOX_COUNT_USAGE_QUERY;
    }

    return result;
}

/** Bytes of a texel, or of a four by four block for a block compressed format.
 *
 * @param format The format
 * @param block Set to true for a block compressed format
 * @return The bytes
 */
static uint32_t box_count_texel_bytes(
    WGPUTextureFormat format,
    bool *block)
{
    uint32_t result = 4;
    *block = false;
    switch (format) {
    case WGPUTextureFormat_R8Unorm:
    case WGPUTextureFormat_R8Snorm:
    case WGPUTextureFormat_R8Uint:
    case WGPUTextureFormat_R8Sint:
    case WGPUTextureFormat_Stencil8:
        result = 1;
        break;
    case WGPUTextureFormat_R16Uint:
    case WGPUTextureFormat_R16Sint:
    case WGPUTextureFormat_R16Float:
    case WGPUTextureFormat_RG8Unorm:
    case WGPUTextureFormat_RG8Snorm:
    case WGPUTextureFormat_RG8Uint:
    case WGPUTextureFormat_RG8Sint:
    case WGPUTextureFormat_Depth16Unorm:
        result = 2;
        break;
    case WGPUTextureFormat_RG32Float:
    case WGPUTextureFormat_RG32Uint:
    case WGPUTextureFormat_RG32Sint:
    case WGPUTextureFormat_RGBA16Uint:
    case WGPUTextureFormat_RGBA16Sint:
    case WGPUTextureFormat_RGBA16Float:
    case WGPUTextureFormat_Depth32FloatStencil8:
        result = 8;
        break;
    case WGPUTextureFormat_RGBA32Float:
    case WGPUTextureFormat_RGBA32Uint:
    case WGPUTextureFormat_RGBA32Sint:
        result = 16;
        break;
    case WGPUTextureFormat_BC1RGBAUnorm:
    case WGPUTextureFormat_BC1RGBAUnormSrgb:
    case WGPUTextureFormat_BC4RUnorm:
    case WGPUTextureFormat_BC4RSnorm:
        result = 8;
        *block = true;
        break;
    case WGPUTextureFormat_BC2RGBAUnorm:
    case WGPUTextureFormat_BC2RGBAUnormSrgb:
    case WGPUTextureFormat_BC3RGBAUnorm:
    case WGPUTextureFormat_BC3RGBAUnormSrgb:
    case WGPUTextureFormat_BC5RGUnorm:
    case WGPUTextureFormat_BC5RGSnorm:
    case WGPUTextureFormat_BC6HRGBUfloat:
    case WGPUTextureFormat_BC6HRGBFloat:
    case WGPUTextureFormat_BC7RGBAUnorm:
    case WGPUTextureFormat_BC7RGBAUnormSrgb:
        result = 16;
        *block = true;
        break;
    default:
        break;
    }

    return result;
}

uint64_t box_count_texture_bytes(
    const WGPUTextureDescriptor *desc)
{
    bool block;
    uint64_t texel = box_count_texel_bytes(desc->format, &block);
    bool volume = desc->dimension == WGPUTextureDimension_3D;
    uint32_t mips = desc->mipLevelCount ? desc->mipLevelCount : 1;
    uint32_t samples = desc->sampleCount ? desc->sampleCount : 1;
    uint64_t result = 0;
    uint32_t mip;
    for (mip = 0; mip < mips; mip ++) {
        uint64_t w = desc->size.width >> mip;
        uint64_t h = desc->size.height >> mip;
        uint64_t d = volume ? desc->size.depthOrArrayLayers >> mip :
            desc->size.depthOrArrayLayers;
        w = w ? w : 1;
        h = h ? h : 1;
        d = d ? d : 1;
        if (block) {
            w = (w + 3) / 4;
            h = (h + 3) / 4;
        }

        result += w * h * d * texel;
    }

    return result * samples;
}

void box_count_label(
    WGPUStringView label,
    const char *fallback,
    char *dst)
{
    if (label.data && label.length) {
        box_count_name(dst, label.data, label.length);
    } else {
        box_count_name(dst, fallback, UINT64_MAX);
    }
}

/** Account a draw to the frame, its pass and its pipeline.
 *
 * @param s The frame being counted
 * @param draws Draw calls, zero when the draw was counted already
 * @param vertices Vertices or indices of an instance
 * @param instances Instances
 * @param pass Entry of the pass, -1 when not counted
 * @param pipeline Entry of the pipeline, -1 when not counted
 * @param topology Primitive kind of the pipeline
 */
static void box_count_add_draw(
    box_count_state_t *s,
    uint32_t draws,
    uint32_t vertices,
    uint32_t instances,
    int32_t pass,
    int32_t pipeline,
    box_count_topology_t topology)
{
    box_counters_t *c = &s->frame;
    uint64_t triangles = box_count_triangles(topology, vertices, instances);
    c->draws += draws;
    c->instances += instances;
    c->triangles += triangles;
    if (pass >= 0) {
        c->passes[pass].draws += draws;
        c->passes[pass].instances += instances;
        c->passes[pass].triangles += triangles;
    }

    if (pipeline >= 0) {
        c->pipelines[pipeline].draws += draws;
        c->pipelines[pipeline].instances += instances;
        c->pipelines[pipeline].triangles += triangles;
    }
}

/** Keep an indirect draw of the frame for the read back of its arguments.
 *
 * @param s The frame being counted
 * @param buffer Buffer of the arguments
 * @param offset Byte the arguments start at
 * @param indexed The arguments are those of an indexed draw
 */
static void box_count_add_indirect(
    box_count_state_t *s,
    WGPUBuffer buffer,
    uint64_t offset,
    bool indexed)
{
    s->frame.draws ++;
    s->frame.indirect_draws ++;
    if (s->pass >= 0) {
        s->frame.passes[s->pass].draws ++;
    }

    if (s->pipeline_entry >= 0) {
        s->frame.pipelines[s->pipeline_entry].draws ++;
    }

    if (s->indirect_count == s->indirect_capacity) {
        uint32_t capacity = s->indirect_capacity ? s->indirect_capacity * 2 : 64;
        box_count_indirect_t *grown = realloc(s->indirect,
            capacity * sizeof(box_count_indirect_t));
        if (!grown) {
            goto done;
        }

        s->indirect = grown;
        s->indirect_capacity = capacity;
    }

    box_count_indirect_t *d = &s->indirect[s->indirect_count ++];
    d->buffer = buffer;
    d->offset = offset;
    d->indexed = indexed;
    d->topology = s->topology;
    d->pass = s->pass;
    d->pipeline = s->pipeline_entry;
    d->dst = 0;
done:
    return;
}

/** Account a pipeline set call and look up the label and topology of a new pipeline.
 *
 * @param s The frame being counted
 * @param handle The pipeline
 * @param compute The pipeline is a compute pipeline
 */
static void box_count_add_pipeline(
    box_count_state_t *s,
    const void *handle,
    bool compute)
{
    box_counters_t *c = &s->frame;
    box_count_pass_t *pass = s->pass >= 0 ? &c->passes[s->pass] : NULL;
    bool changed = handle != s->pipeline;
    c->pipeline_sets ++;
    if (pass) {
        pass->pipeline_sets ++;
    }

    if (changed) {
        char name[BOX_COUNT_NAME_MAX];
        box_count_name(name, compute ? "compute" : "render", UINT64_MAX);
        s->topology = BOX_COUNT_TRIANGLES;
        box_count_registry_enter();
        int32_t slot = box_count_find(handle);
        if (slot >= 0) {
            memcpy(name, box_count_registry.objects[slot].name, sizeof(name));
            s->topology = box_count_registry.objects[slot].topology;
        }

        box_count_registry_leave();
        s->pipeline = handle;
        s->pipeline_entry = -1;
        if (c->flags & BOX_COUNT_PIPELINES) {
            s->pipeline_entry = box_count_pipeline(c, handle, name, compute);
        }

        c->pipeline_switches ++;
        if (pass) {
            pass->pipeline_switches ++;
        }

        if (s->pipeline_entry >= 0) {
            c->pipelines[s->pipeline_entry].switches ++;
        }
    }

    if (s->pipeline_entry >= 0) {
        c->pipelines[s->pipeline_entry].sets ++;
    }
}

/** Account a bind group set call, which changes the slot unless it binds the same.
 *
 * @param s The frame being counted
 * @param index Slot of the group
 * @param group The group
 * @param count Dynamic offsets
 * @param offsets The dynamic offsets
 */
static void box_count_add_bind(
    box_count_state_t *s,
    uint32_t index,
    const void *group,
    size_t count,
    const uint32_t *offsets)
{
    box_counters_t *c = &s->frame;
    uint64_t hash = 1469598103934665603ull ^ (uint64_t)count;
    size_t i;
    for (i = 0; i < count; i ++) {
        hash = (hash ^ offsets[i]) * 1099511628211ull;
    }

    bool changed = true;
    if (index < BOX_COUNT_SLOT_MAX) {
        changed = s->groups[index] != group || s->offsets[index] != hash;
        s->groups[index] = group;
        s->offsets[index] = hash;
    }

    c->bind_sets ++;
    c->bind_changes += changed;
    if (s->pass < 0) {
        goto done;
    }

    c->passes[s->pass].bind_sets ++;
    c->passes[s->pass].bind_changes += changed;
    if (c->flags & BOX_COUNT_BIND_GROUPS) {
        int32_t entry = box_count_bind(c, (uint32_t)s->pass, index);
        if (entry >= 0) {
            c->binds[entry].sets ++;
            c->binds[entry].changes += changed;
        }
    }

done:
    return;
}

/** Start a pass of the frame, which binds nothing yet.
 *
 * @param s The frame being counted
 * @param label Label of the pass descriptor
 * @param compute The pass is a compute pass
 */
static void box_count_add_pass(
    box_count_state_t *s,
    WGPUStringView label,
    bool compute)
{
    char name[BOX_COUNT_NAME_MAX];
    box_count_label(label, compute ? "compute" : "render", name);
    if (compute) {
        s->frame.compute_passes ++;
    } else {
        s->frame.render_passes ++;
    }

    s->pass = box_count_pass(&s->frame, name, compute);
    if (s->pass >= 0) {
        s->frame.passes[s->pass].recorded ++;
    }

    s->pipeline = NULL;
    s->pipeline_entry = -1;
    s->topology = BOX_COUNT_TRIANGLES;
    memset(s->groups, 0, sizeof(s->groups));
    memset(s->offsets, 0, sizeof(s->offsets));
}

void box_count_draw(
    WGPURenderPassEncoder pass,
    uint32_t vertices,
    uint32_t instances,
    uint32_t first_vertex,
    uint32_t first_instance)
{
    wgpuRenderPassEncoderDraw(pass, vertices, instances, first_vertex, first_instance);
    box_count_state_t *s = box_count_current;
    if (s) {
        box_count_add_draw(s, 1, vertices, instances, s->pass, s->pipeline_entry, s->topology);
    }
}

void box_count_draw_indexed(
    WGPURenderPassEncoder pass,
    uint32_t indices,
    uint32_t instances,
    uint32_t first_index,
    int32_t base_vertex,
    uint32_t first_instance)
{
    wgpuRenderPassEncoderDrawIndexed(pass, indices, instances, first_index, base_vertex,
        first_instance);
    box_count_state_t *s = box_count_current;
    if (s) {
        box_count_add_draw(s, 1, indices, instances, s->pass, s->pipeline_entry, s->topology);
    }
}

void box_count_draw_indirect(
    WGPURenderPassEncoder pass,
    WGPUBuffer buffer,
    uint64_t offset)
{
    wgpuRenderPassEncoderDrawIndirect(pass, buffer, offset);
    box_count_state_t *s = box_count_current;
    if (s) {
        box_count_add_indirect(s, buffer, offset, false);
    }
}

void box_count_draw_indexed_indirect(
    WGPURenderPassEncoder pass,
    WGPUBuffer buffer,
    uint64_t offset)
{
    wgpuRenderPassEncoderDrawIndexedIndirect(pass, buffer, offset);
    box_count_state_t *s = box_count_current;
    if (s) {
        box_count_add_indirect(s, buffer, offset, true);
    }
}

void box_count_set_render_pipeline(
    WGPURenderPassEncoder pass,
    WGPURenderPipeline pipeline)
{
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    box_count_state_t *s = box_count_current;
    if (s) {
        box_count_add_pipeline(s, pipeline, false);
    }
}

void box_count_set_compute_pipeline(
    WGPUComputePassEncoder pass,
    WGPUComputePipeline pipeline)
{
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    box_count_state_t *s = box_count_current;
    if (s) {
        box_count_add_pipeline(s, pipeline, true);
    }
}

void box_count_set_render_bind_group(
    WGPURenderPassEncoder pass,
    uint32_t index,
    WGPUBindGroup group,
    size_t count,
    const uint32_t *offsets)
{
    wgpuRenderPassEncoderSetBindGroup(pass, index, group, count, offsets);
    box_count_state_t *s = box_count_current;
    if (s) {
        box_count_add_bind(s, index, group, count, offsets);
    }
}

void box_count_set_compute_bind_group(
    WGPUComputePassEncoder pass,
    uint32_t index,
    WGPUBindGroup group,
    size_t count,
    const uint32_t *offsets)
{
    wgpuComputePassEncoderSetBindGroup(pass, index, group, count, offsets);
    box_count_state_t *s = box_count_current;
    if (s) {
        box_count_add_bind(s, index, group, count, offsets);
    }
}

void box_count_dispatch(
    WGPUComputePassEncoder pass,
    uint32_t x,
    uint32_t y,
    uint32_t z)
{
    wgpuComputePassEncoderDispatchWorkgroups(pass, x, y, z);
    box_count_state_t *s = box_count_current;
    if (!s) {
        goto done;
    }

    uint64_t groups = (uint64_t)x * y * z;
    s->frame.dispatches ++;
    s->frame.workgroups += groups;
    if (s->pass >= 0) {
        s->frame.passes[s->pass].dispatches ++;
        s->frame.passes[s->pass].workgroups += groups;
    }

    if (s->pipeline_entry >= 0) {
        s->frame.pipelines[s->pipeline_entry].dispatches ++;
    }

done:
    return;
}

WGPURenderPassEncoder box_count_begin_render_pass(
    WGPUCommandEncoder encoder,
    const WGPURenderPassDescriptor *desc)
{
    WGPURenderPassEncoder result = wgpuCommandEncoderBeginRenderPass(encoder, desc);
    box_count_state_t *s = box_count_current;
    if (s) {
        box_count_add_pass(s, desc->label, false);
    }

    return result;
}

WGPUComputePassEncoder box_count_begin_compute_pass(
    WGPUCommandEncoder encoder,
    const WGPUComputePassDescriptor *desc)
{
    WGPUComputePassEncoder result = wgpuCommandEncoderBeginComputePass(encoder, desc);
    box_count_state_t *s = box_count_current;
    if (s) {
        WGPUStringView none = { NULL, 0 };
        box_count_add_pass(s, desc ? desc->label : none, true);
    }

    return result;
}

void box_count_write_buffer(
    WGPUQueue queue,
    WGPUBuffer buffer,
    uint64_t offset,
    const void *data,
    size_t size)
{
    wgpuQueueWriteBuffer(queue, buffer, offset, data, size);
    box_count_state_t *s = box_count_current;
    if (!s) {
        goto done;
    }

    s->frame.buffer_uploads ++;
    s->frame.buffer_upload_bytes += size;
    if (s->frame.flags & BOX_COUNT_BUFFERS) {
        uint32_t usage = box_count_usage(wgpuBufferGetUsage(buffer));
        s->frame.usages[usage].uploads ++;
        s->frame.usages[usage].upload_bytes += size;
    }

done:
    return;
}

void box_count_write_texture(
    WGPUQueue queue,
    const WGPUTexelCopyTextureInfo *dst,
    const void *data,
    size_t size,
    const WGPUTexelCopyBufferLayout *layout,
    const WGPUExtent3D *extent)
{
    wgpuQueueWriteTexture(queue, dst, data, size, layout, extent);
    box_count_state_t *s = box_count_current;
    if (s) {
        s->frame.texture_uploads ++;
        s->frame.texture_upload_bytes += size;
    }
}

WGPUBuffer box_count_create_buffer(
    WGPUDevice device,
    const WGPUBufferDescriptor *desc)
{
    WGPUBuffer result = wgpuDeviceCreateBuffer(device, desc);
    box_count_object_t obj = {
        .key = result,
        .device = device,
        .kind = BOX_COUNT_KIND_BUFFER,
        .usage = box_count_usage(desc->usage),
        .bytes = desc->size
    };
    box_count_track(&obj);

    return result;
}

void box_count_release_buffer(
    WGPUBuffer buffer)
{
    box_count_untrack(buffer);
    wgpuBufferRelease(buffer);
}

WGPUTexture box_count_create_texture(
    WGPUDevice device,
    const WGPUTextureDescriptor *desc)
{
    WGPUTexture result = wgpuDeviceCreateTexture(device, desc);
    box_count_object_t obj = {
        .key = result,
        .device = device,
        .kind = BOX_COUNT_KIND_TEXTURE,
        .bytes = box_count_texture_bytes(desc)
    };
    box_count_track(&obj);

    return result;
}

void box_count_release_texture(
    WGPUTexture texture)
{
    box_count_untrack(texture);
    wgpuTextureRelease(texture);
}

WGPUQuerySet box_count_create_query_set(
    WGPUDevice device,
    const WGPUQuerySetDescriptor *desc)
{
    WGPUQuerySet result = wgpuDeviceCreateQuerySet(device, desc);
    box_count_object_t obj = {
        .key = result,
        .device = device,
        .kind = BOX_COUNT_KIND_QUERY,
        .bytes = (uint64_t)desc->count * sizeof(uint64_t)
    };
    box_count_track(&obj);

    return result;
}

void box_count_release_query_set(
    WGPUQuerySet set)
{
    box_count_untrack(set);
    wgpuQuerySetRelease(set);
}

WGPURenderPipeline box_count_create_render_pipeline(
    WGPUDevice device,
    const WGPURenderPipelineDescriptor *desc)
{
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(device, desc);
    box_count_object_t obj = {
        .key = result,
        .device = device,
        .kind = BOX_COUNT_KIND_RENDER,
        .topology = BOX_COUNT_TRIANGLES
    };
    WGPUPrimitiveTopology topology = desc->primitive.topology;
    if (topology == WGPUPrimitiveTopology_TriangleStrip) {
        obj.topology = BOX_COUNT_STRIP;
    } else if (topology != WGPUPrimitiveTopology_TriangleList &&
        topology != WGPUPrimitiveTopology_Undefined)
    {
        obj.topology = BOX_COUNT_LINES;
    }

    WGPUStringView entry = desc->vertex.entryPoint;
    if (desc->fragment) {
        entry = desc->fragment->entryPoint;
    }

    box_count_label(desc->label, NULL, obj.name);
    if (!obj.name[0]) {
        box_count_label(entry, "render", obj.name);
    }

    box_count_track(&obj);

    return result;
}

WGPUComputePipeline box_count_create_compute_pipeline(
    WGPUDevice device,
    const WGPUComputePipelineDescriptor *desc)
{
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(device, desc);
    box_count_object_t obj = {
        .key = result,
        .device = device,
        .kind = BOX_COUNT_KIND_COMPUTE
    };
    box_count_label(desc->label, NULL, obj.name);
    if (!obj.name[0]) {
        box_count_label(desc->compute.entryPoint, "compute", obj.name);
    }

    box_count_track(&obj);

    return result;
}

void box_count_release_render_pipeline(
    WGPURenderPipeline pipeline)
{
    box_count_untrack(pipeline);
    wgpuRenderPipelineRelease(pipeline);
}

void box_count_release_compute_pipeline(
    WGPUComputePipeline pipeline)
{
    box_count_untrack(pipeline);
    wgpuComputePipelineRelease(pipeline);
}

void box_count_init(
    box_renderer_t *r)
{
    memset(&r->count, 0, sizeof(box_count_t));
#ifndef BOX_PLATFORM_WEB
    pthread_mutex_init(&r->count.lock, NULL);
#endif
}

void box_count_lock(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    pthread_mutex_lock(&r->count.lock);
#else
    (void)r;
#endif
}

void box_count_unlock(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    pthread_mutex_unlock(&r->count.lock);
#else
    (void)r;
#endif
}

/** Copy a finished frame to the published one.
 *
 * @param r The renderer
 */
static void box_count_publish(
    box_renderer_t *r)
{
    box_count_lock(r);
    memcpy(&r->count.published, &r->count.state.frame, sizeof(box_counters_t));
    r->count.has_published = true;
    box_count_unlock(r);
}

/** Copy the resources the device of a renderer holds into the frame being counted.
 *
 * @param r The renderer
 */
static void box_count_snapshot(
    box_renderer_t *r)
{
    box_counters_t *c = &r->count.state.frame;
    box_count_registry_enter();
    box_count_memory_t *m = box_count_memory(r->device, false);
    if (m) {
        c->buffers = m->buffers;
        c->buffer_memory = m->buffer_bytes;
        c->textures = m->textures;
        c->texture_memory = m->texture_bytes;
        c->query_memory = m->query_bytes;
        uint32_t i;
        for (i = 0; i < BOX_COUNT_USAGE_COUNT; i ++) {
            c->usages[i].buffers = m->usage_buffers[i];
            c->usages[i].bytes = m->usage_bytes[i];
        }
    }

    box_count_registry_leave();
}

#ifndef BOX_PLATFORM_WEB
/** Add the instances and triangles of the read back indirect draws and publish the frame.
 *
 * @param status Outcome of the map
 * @param message Error text
 * @param userdata1 The renderer
 * @param userdata2 Unused
 */
static void box_count_mapped(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)message;
    (void)userdata2;
    box_renderer_t *r = userdata1;
    box_count_state_t *s = &r->count.state;
    const uint32_t *words = NULL;
    if (status == WGPUMapAsyncStatus_Success) {
        words = wgpuBufferGetConstMappedRange(r->count.readback, 0,
            (size_t)r->count.readback_size);
    }

    if (words) {
        uint32_t i;
        for (i = 0; i < s->indirect_count; i ++) {
            const box_count_indirect_t *d = &s->indirect[i];
            const uint32_t *args = &words[d->dst / sizeof(uint32_t)];
            box_count_add_draw(s, 0, args[0], args[1], d->pass, d->pipeline, d->topology);
        }

        s->frame.indirect_resolved = true;
        box_count_publish(r);
    }

    if (status == WGPUMapAsyncStatus_Success) {
        wgpuBufferUnmap(r->count.readback);
    }

    r->count.pending = false;
    r->count.busy = false;
}

/** Range of the read back that copies an argument buffer, added when it has none.
 *
 * @param r The renderer
 * @param buffer The argument buffer
 * @return The range, NULL when no memory was left for it
 */
static box_count_range_t* box_count_range(
    box_renderer_t *r,
    WGPUBuffer buffer)
{
    box_count_range_t *result = NULL;
    box_count_t *c = &r->count;
    uint32_t i;
    for (i = 0; i < c->range_count; i ++) {
        if (c->ranges[i].buffer == buffer) {
            result = &c->ranges[i];
            goto done;
        }
    }

    if (c->range_count == c->range_capacity) {
        uint32_t capacity = c->range_capacity ? c->range_capacity * 2 : 16;
        box_count_range_t *grown = realloc(c->ranges, capacity * sizeof(box_count_range_t));
        if (!grown) {
            goto done;
        }

        c->ranges = grown;
        c->range_capacity = capacity;
    }

    result = &c->ranges[c->range_count ++];
    result->buffer = buffer;
    result->first = UINT64_MAX;
    result->end = 0;
    result->dst = 0;
done:
    return result;
}

/** Lay out the ranges the indirect draws of the frame read and where they land.
 *
 * @param r The renderer
 * @return Bytes the read back needs, 0 when a range could not be kept
 */
static uint64_t box_count_layout(
    box_renderer_t *r)
{
    uint64_t result = 0;
    box_count_state_t *s = &r->count.state;
    r->count.range_count = 0;
    uint32_t i;
    for (i = 0; i < s->indirect_count; i ++) {
        box_count_indirect_t *d = &s->indirect[i];
        box_count_range_t *range = box_count_range(r, d->buffer);
        if (!range) {
            goto done;
        }

        uint64_t end = d->offset + (d->indexed ? BOX_COUNT_INDEXED_ARGS : BOX_COUNT_DRAW_ARGS);
        range->first = d->offset < range->first ? d->offset : range->first;
        range->end = end > range->end ? end : range->end;
    }

    uint64_t total = 0;
    for (i = 0; i < r->count.range_count; i ++) {
        r->count.ranges[i].dst = total;
        total += r->count.ranges[i].end - r->count.ranges[i].first;
    }

    for (i = 0; i < s->indirect_count; i ++) {
        box_count_indirect_t *d = &s->indirect[i];
        box_count_range_t *range = box_count_range(r, d->buffer);
        d->dst = range->dst + d->offset - range->first;
    }

    result = (total + 7) & ~(uint64_t)7;
done:
    return result;
}
#endif

void box_count_begin(
    box_renderer_t *r)
{
    box_count_current = NULL;
#ifndef BOX_PLATFORM_WEB
    if (r->count.busy) {
        r->count.wait ++;
        wgpuDevicePoll(r->device, r->count.wait >= BOX_TIMING_DELAY, NULL);
        wgpuInstanceProcessEvents(r->instance);
    }
#endif
    uint32_t flags = r->count.request;
    if (!flags || r->count.pending) {
        goto done;
    }

    box_count_state_t *s = &r->count.state;
    memset(&s->frame, 0, sizeof(box_counters_t));
    s->frame.frame = r->frames;
    s->frame.flags = flags;
    s->pass = -1;
    s->pipeline = NULL;
    s->pipeline_entry = -1;
    s->topology = BOX_COUNT_TRIANGLES;
    s->indirect_count = 0;
    r->count.recording = true;
    box_count_current = s;
done:
    return;
}

void box_count_record(
    box_renderer_t *r,
    WGPUCommandEncoder encoder)
{
#ifndef BOX_PLATFORM_WEB
    box_count_state_t *s = &r->count.state;
    if (!r->count.recording || !s->indirect_count) {
        goto done;
    }

    uint64_t size = box_count_layout(r);
    if (!size) {
        goto done;
    }

    if (size > r->count.readback_capacity) {
        if (r->count.readback) wgpuBufferRelease(r->count.readback);
        WGPUBufferDescriptor desc = {
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = size
        };
        r->count.readback = wgpuDeviceCreateBuffer(r->device, &desc);
        r->count.readback_capacity = r->count.readback ? size : 0;
    }

    if (!r->count.readback) {
        goto done;
    }

    uint32_t i;
    for (i = 0; i < r->count.range_count; i ++) {
        box_count_range_t *range = &r->count.ranges[i];
        wgpuCommandEncoderCopyBufferToBuffer(encoder, range->buffer, range->first,
            r->count.readback, range->dst, range->end - range->first);
    }

    r->count.readback_size = size;
    r->count.pending = true;
done:
    return;
#else
    (void)r;
    (void)encoder;
#endif
}

void box_count_end(
    box_renderer_t *r,
    bool rendered)
{
    box_count_current = NULL;
    if (!r->count.recording) {
        goto done;
    }

    r->count.recording = false;
    if (!rendered) {
        r->count.pending = false;
        goto done;
    }

    box_count_snapshot(r);
    if (!r->count.pending) {
        r->count.state.frame.indirect_resolved = r->count.state.indirect_count == 0;
        box_count_publish(r);
        goto done;
    }

#ifndef BOX_PLATFORM_WEB
    WGPUBufferMapCallbackInfo info = {
        .mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = box_count_mapped,
        .userdata1 = r
    };
    r->count.busy = true;
    r->count.wait = 0;
    wgpuBufferMapAsync(r->count.readback, WGPUMapMode_Read, 0,
        (size_t)r->count.readback_size, info);
#endif
done:
    return;
}

void box_count_fini(
    box_renderer_t *r)
{
    box_count_current = NULL;
#ifndef BOX_PLATFORM_WEB
    while (r->count.busy) {
        wgpuDevicePoll(r->device, true, NULL);
        wgpuInstanceProcessEvents(r->instance);
    }
#endif
    if (r->count.readback) wgpuBufferRelease(r->count.readback);
    r->count.readback = NULL;
    r->count.readback_capacity = 0;
    r->count.recording = false;
    r->count.pending = false;
    free(r->count.state.indirect);
    r->count.state.indirect = NULL;
    r->count.state.indirect_count = 0;
    r->count.state.indirect_capacity = 0;
    free(r->count.ranges);
    r->count.ranges = NULL;
    r->count.range_count = 0;
    r->count.range_capacity = 0;
    if (r->device) {
        box_count_purge(r->device);
    }
}

void box_count_destroy(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    pthread_mutex_destroy(&r->count.lock);
#else
    (void)r;
#endif
}
