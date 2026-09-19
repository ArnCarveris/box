/**
 * @file renderer/transition.c
 * @brief Evaluates the transitions of the static instances on the renderer thread.
 *
 * An instance whose values a transition moves keeps a record next to its slot.
 * Every frame the records are interpolated at the time of the packet and the
 * channels they touch are composed into the instance again, which is the only
 * work a running transition costs; the game side uploads the record once and
 * hears about the end of it through the packet it gets back.
 */

#include "renderer_internal.h"
#include <box/math.h>
#include <box/material.h>

/** Round an interpolated float back to the byte the component holds.
 *
 * @param value The float
 * @return The byte
 */
static uint8_t box_transition_byte(
    float value)
{
    float v = value < 0.0f ? 0.0f : (value > 255.0f ? 255.0f : value);

    return (uint8_t)(v + 0.5f);
}

/** Compose the world transform of the instance from the displayed pose.
 *
 * @param cmd The transitions of the instance
 * @param values The displayed values of every channel
 * @return The world matrix of the instance, without the mesh scale
 */
static mat4 box_transition_pose(
    const box_transition_cmd_t *cmd,
    const float values[BOX_DISPLAY_CHANNELS][BOX_DISPLAY_FLOATS])
{
    vec3 t = vec3_new(0, 0, 0);
    vec3 s = vec3_new(1, 1, 1);
    mat4 rot = mat4_identity();
    if (cmd->present & (1u << BOX_DISPLAY_POSITION)) {
        const float *p = values[BOX_DISPLAY_POSITION];

        t = vec3_new(p[0], p[1], p[2]);
    }

    if (cmd->present & (1u << BOX_DISPLAY_SCALE)) {
        const float *v = values[BOX_DISPLAY_SCALE];

        s = vec3_new(v[0], v[1], v[2]);
    }

    if (cmd->present & (1u << BOX_DISPLAY_ROTATION)) {
        const float *r = values[BOX_DISPLAY_ROTATION];

        rot = cmd->oriented ? mat4_rotation_yxz(r[0], r[1], r[2]) :
            mat4_rotation_xyz(vec3_new(r[0], r[1], r[2]));
    }

    mat4 parent;
    mat4 local = mat4_trs(t, &rot, s);

    memcpy(parent.v, cmd->parent, sizeof(parent.v));

    return mat4_mul(&parent, &local);
}

/** Write the transform and the bounding sphere of the instance.
 *
 * @param cmd The transitions of the instance
 * @param values The displayed values of every channel
 * @param out The instance data
 */
static void box_transition_transform(
    const box_transition_cmd_t *cmd,
    const float values[BOX_DISPLAY_CHANNELS][BOX_DISPLAY_FLOATS],
    box_instance_t *out)
{
    mat4 world = box_transition_pose(cmd, values);
    vec3 mesh_scale = vec3_new(cmd->mesh_scale[0], cmd->mesh_scale[1], cmd->mesh_scale[2]);
    mat4 scale = mat4_scaling(mesh_scale);
    mat4 m = mat4_mul(&world, &scale);

    m.v[14] += cmd->depth;

    int row;
    for (row = 0; row < 3; row ++) {
        out->transform[row * 4 + 0] = m.v[0 + row];
        out->transform[row * 4 + 1] = m.v[4 + row];
        out->transform[row * 4 + 2] = m.v[8 + row];
        out->transform[row * 4 + 3] = m.v[12 + row];
    }

    vec3 lo = vec3_new(cmd->bounds_min[0], cmd->bounds_min[1], cmd->bounds_min[2]);
    vec3 hi = vec3_new(cmd->bounds_max[0], cmd->bounds_max[1], cmd->bounds_max[2]);
    vec3 center = vec3_scale(vec3_add(lo, hi), 0.5f);
    vec3 extent = vec3_scale(vec3_sub(hi, lo), 0.5f);
    float radius = 0;
    int axis;
    for (axis = 0; axis < 3; axis ++) {
        float x = m.v[axis];
        float y = m.v[4 + axis];
        float z = m.v[8 + axis];
        float e = fabsf(x) * extent.x + fabsf(y) * extent.y + fabsf(z) * extent.z;

        radius += e * e;
        out->bounds[axis] = x * center.x + y * center.y + z * center.z + m.v[12 + axis];
    }

    out->bounds[3] = sqrtf(radius);
}

/** Write the linear albedo and opacity of the instance.
 *
 * @param cmd The transitions of the instance
 * @param values The displayed values of every channel
 * @param out The instance data
 */
static void box_transition_color(
    const box_transition_cmd_t *cmd,
    const float values[BOX_DISPLAY_CHANNELS][BOX_DISPLAY_FLOATS],
    box_instance_t *out)
{
    const float *rgb = values[BOX_DISPLAY_RGB];
    const float *tint = values[BOX_DISPLAY_TINT];
    bool has_rgb = (cmd->present & (1u << BOX_DISPLAY_RGB)) != 0;
    bool has_tint = (cmd->present & (1u << BOX_DISPLAY_TINT)) != 0;

    out->color[0] = has_rgb ? box_srgb_to_linear(box_transition_byte(rgb[0])) : 1.0f;
    out->color[1] = has_rgb ? box_srgb_to_linear(box_transition_byte(rgb[1])) : 1.0f;
    out->color[2] = has_rgb ? box_srgb_to_linear(box_transition_byte(rgb[2])) : 1.0f;
    out->color[3] = (cmd->present & (1u << BOX_DISPLAY_OPACITY)) ?
        values[BOX_DISPLAY_OPACITY][0] : 1.0f;
    if (has_tint) {
        float a = (float)box_transition_byte(tint[3]) / 255.0f;
        int i;
        for (i = 0; i < 3; i ++) {
            float c = (float)box_transition_byte(tint[i]) / 255.0f;

            out->color[i] += (c - out->color[i]) * a;
        }
    }
}

/** Write the linear emission of the instance.
 *
 * @param cmd The transitions of the instance
 * @param values The displayed values of every channel
 * @param out The instance data
 */
static void box_transition_emissive(
    const box_transition_cmd_t *cmd,
    const float values[BOX_DISPLAY_CHANNELS][BOX_DISPLAY_FLOATS],
    box_instance_t *out)
{
    const float *e = values[BOX_DISPLAY_EMISSIVE];
    bool has = (cmd->present & (1u << BOX_DISPLAY_EMISSIVE)) != 0;
    float strength = has ? e[0] : 0.0f;
    int i;
    for (i = 0; i < 3; i ++) {
        out->emissive[i] = has ?
            box_srgb_to_linear(box_transition_byte(e[i + 1])) * strength : 0.0f;
    }

    out->emissive[3] = strength;
}

bool box_transition_fill(
    const box_transition_cmd_t *cmd,
    const box_transition_rec_t *records,
    double time,
    struct box_instance_t *out)
{
    float values[BOX_DISPLAY_CHANNELS][BOX_DISPLAY_FLOATS];
    uint32_t moved = 0;
    bool running = false;

    memcpy(values, cmd->values, sizeof(values));

    uint32_t i;
    for (i = 0; i < cmd->count; i ++) {
        const box_transition_rec_t *rec = &records[i];
        float displayed[BOX_DISPLAY_FLOATS];

        running |= box_transition_lerp(rec->start, rec->delay, rec->duration,
            rec->timing, rec->from, rec->to, (int32_t)rec->count, time, displayed);

        uint32_t j;
        for (j = 0; j < rec->count; j ++) {
            values[rec->channel][rec->first + j] = displayed[j];
        }

        moved |= 1u << rec->channel;
    }

    if (cmd->posed) {
        box_transition_transform(cmd, values, out);
    }

    uint32_t color = (1u << BOX_DISPLAY_RGB) | (1u << BOX_DISPLAY_TINT) |
        (1u << BOX_DISPLAY_OPACITY);
    if (moved & color) {
        box_transition_color(cmd, values, out);
    }

    if (moved & (1u << BOX_DISPLAY_EMISSIVE)) {
        box_transition_emissive(cmd, values, out);
    }

    return running;
}

/** Entry index of a slot of a mesh, growing the table as needed.
 *
 * @param m The mesh entry
 * @param slot The instance slot
 * @return Pointer to the entry index of the slot
 */
static uint32_t* box_transition_index(
    box_gpu_mesh_t *m,
    uint32_t slot)
{
    while (m->transition_index.count <= slot) {
        *(uint32_t*)box_vec_push(&m->transition_index) = BOX_TRANSITION_NONE;
    }

    return box_vec_at(&m->transition_index, slot);
}

/** Drop an entry and move the last one into its place.
 *
 * @param r The renderer
 * @param index Index of the entry
 */
static void box_transition_release(
    box_renderer_t *r,
    uint32_t index)
{
    uint32_t last = r->transitions.count - 1;
    if (index != last) {
        box_transition_entry_t *moved = box_vec_at(&r->transitions, last);
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, moved->cmd.mesh);

        *(box_transition_entry_t*)box_vec_at(&r->transitions, index) = *moved;
        *(uint32_t*)box_vec_at(&m->transition_index, moved->cmd.slot) = index;
    }

    r->transitions.count = last;
}

/** Order two instance slots so that a run of them uploads in one write.
 *
 * @param a First slot
 * @param b Second slot
 * @return Negative when a comes first, positive when b does
 */
static int box_transition_cmp(
    const void *a,
    const void *b)
{
    uint32_t x = *(const uint32_t*)a;
    uint32_t y = *(const uint32_t*)b;

    return x < y ? -1 : (x > y ? 1 : 0);
}

void box_transition_start(
    box_renderer_t *r,
    box_gpu_mesh_t *m,
    const box_packet_t *p,
    const box_transition_cmd_t *cmd,
    const box_instance_t *data)
{
    uint32_t *index = box_transition_index(m, cmd->slot);
    if (*index == BOX_TRANSITION_NONE) {
        *index = r->transitions.count;
        box_vec_push(&r->transitions);
        *(uint32_t*)box_vec_push(&m->transition_slots) = cmd->slot;
        m->transition_sorted = false;
    }

    box_transition_entry_t *entry = box_vec_at(&r->transitions, *index);
    uint32_t count = cmd->count > BOX_DISPLAY_CHANNELS ? BOX_DISPLAY_CHANNELS : cmd->count;

    entry->cmd = *cmd;
    entry->cmd.first = 0;
    entry->cmd.count = count;
    entry->base = *data;
    memcpy(entry->records, box_vec_at(&p->transition_records, cmd->first),
        (size_t)count * sizeof(box_transition_rec_t));
}

void box_transition_stop(
    box_renderer_t *r,
    box_gpu_mesh_t *m,
    uint32_t slot)
{
    if (slot >= m->transition_index.count) {
        goto done;
    }

    uint32_t *index = box_vec_at(&m->transition_index, slot);
    if (*index != BOX_TRANSITION_NONE) {
        box_transition_release(r, *index);
        *index = BOX_TRANSITION_NONE;
    }

done:
    return;
}

void box_transition_clear(
    box_renderer_t *r,
    box_gpu_mesh_t *m)
{
    uint32_t i;
    for (i = 0; i < m->transition_index.count; i ++) {
        box_transition_stop(r, m, i);
    }

    box_vec_clear(&m->transition_slots);
    box_vec_clear(&m->transition_index);
    m->transition_sorted = true;
}

/** Stage one instance for upload, flushing the run it does not belong to.
 *
 * @param r The renderer
 * @param mesh The mesh the slot belongs to
 * @param slot The instance slot
 * @param first The slot the staged run starts at, updated when it is flushed
 * @param data The instance data
 */
static void box_transition_stage(
    box_renderer_t *r,
    uint32_t mesh,
    uint32_t slot,
    uint32_t *first,
    const box_instance_t *data)
{
    if (r->static_staging.count && slot != *first + r->static_staging.count) {
        box_scene_flush_statics(r, mesh, *first);
    }

    if (!r->static_staging.count) {
        *first = slot;
    }

    *(box_instance_t*)box_vec_push(&r->static_staging) = *data;
    if (r->static_staging.count >= BOX_SCENE_UPLOAD_RUN) {
        box_scene_flush_statics(r, mesh, *first);
        *first = slot + 1;
    }
}

/** Evaluate and upload the transitions of one mesh, retiring the finished ones.
 *
 * Every instance the records moved is appended to the static updates of the
 * packet, so the passes that watch the static instances of a frame, the shadow
 * cascade cache and the probe descriptors of the bake, see the instances the
 * renderer changed exactly as they see the ones the game side changed.
 *
 * @param r The renderer
 * @param handle The mesh handle
 * @param p The packet, which receives the instances that finished
 */
static void box_transition_mesh(
    box_renderer_t *r,
    uint32_t handle,
    box_packet_t *p)
{
    box_gpu_mesh_t *m = box_vec_at(&r->meshes, handle);
    if (!m->transition_sorted) {
        qsort(m->transition_slots.data, m->transition_slots.count, sizeof(uint32_t),
            box_transition_cmp);
        m->transition_sorted = true;
    }

    uint32_t kept = 0;
    uint32_t first = 0;
    uint32_t i;
    for (i = 0; i < m->transition_slots.count; i ++) {
        uint32_t slot = *(uint32_t*)box_vec_at(&m->transition_slots, i);
        uint32_t index = *(uint32_t*)box_vec_at(&m->transition_index, slot);
        if (index == BOX_TRANSITION_NONE) {
            continue;
        }

        box_transition_entry_t *entry = box_vec_at(&r->transitions, index);
        bool running = slot < m->static_capacity &&
            box_transition_fill(&entry->cmd, entry->records, p->time, &entry->base);
        if (slot < m->static_capacity) {
            box_static_update_t *moved = box_vec_push(&p->static_updates);

            box_transition_stage(r, handle, slot, &first, &entry->base);
            box_scene_mirror(m, slot, &entry->base);
            moved->mesh = handle;
            moved->slot = slot;
            moved->transition = BOX_UPDATE_STILL;
            moved->data = entry->base;
        }

        if (running) {
            *(uint32_t*)box_vec_at(&m->transition_slots, kept ++) = slot;
            continue;
        }

        box_transition_done_t *done = box_vec_push(&p->transitions_done);

        done->mesh = handle;
        done->slot = slot;
        box_transition_release(r, index);
        *(uint32_t*)box_vec_at(&m->transition_index, slot) = BOX_TRANSITION_NONE;
    }

    box_scene_flush_statics(r, handle, first);
    m->transition_slots.count = kept;
}

void box_transition_frame(
    box_renderer_t *r,
    box_packet_t *p)
{
    box_vec_clear(&p->transitions_done);

    uint32_t i;
    for (i = 0; i < r->meshes.count; i ++) {
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, i);
        if (m->transition_slots.count) {
            box_transition_mesh(r, i, p);
        }
    }
}
