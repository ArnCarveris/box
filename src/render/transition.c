/**
 * @file render/transition.c
 * @brief Transition resolver: the render thread table of transition nodes, the application of
 * the records of a packet and the evaluation of the running transitions on the packet clock.
 *
 * Runs where the packets are consumed (the render thread, or the main thread in main thread
 * mode) and never touches the world. Values are stored in the layout of their component, with a
 * description of the fields of each component, so a new component is an entry of the table.
 */

#include "render.h"
#include <stddef.h>

/** A float field of a component */
#define BOX_TRANSITION_FLOAT(T, m) \
    { (uint16_t)offsetof(T, m), BOX_RENDER_TRANSITION_F32 }

/** A uint8_t field of a component */
#define BOX_TRANSITION_U8(T, m) \
    { (uint16_t)offsetof(T, m), BOX_RENDER_TRANSITION_U8 }

/** Layout of every transitionable component, indexed by box_render_transition_component_t */
static const box_render_transition_layout_t box_transition_layouts[
    BOX_RENDER_TRANSITION_COMPONENTS] =
{
    [BOX_RENDER_TRANSITION_POSITION] = { ECS_SIZEOF(Position), 3, {
        BOX_TRANSITION_FLOAT(Position, x),
        BOX_TRANSITION_FLOAT(Position, y),
        BOX_TRANSITION_FLOAT(Position, z) } },
    [BOX_RENDER_TRANSITION_ROTATION] = { ECS_SIZEOF(Rotation), 3, {
        BOX_TRANSITION_FLOAT(Rotation, x),
        BOX_TRANSITION_FLOAT(Rotation, y),
        BOX_TRANSITION_FLOAT(Rotation, z) } },
    [BOX_RENDER_TRANSITION_SCALE] = { ECS_SIZEOF(Scale), 3, {
        BOX_TRANSITION_FLOAT(Scale, x),
        BOX_TRANSITION_FLOAT(Scale, y),
        BOX_TRANSITION_FLOAT(Scale, z) } },
    [BOX_RENDER_TRANSITION_RGB] = { ECS_SIZEOF(Rgb), 3, {
        BOX_TRANSITION_U8(Rgb, r),
        BOX_TRANSITION_U8(Rgb, g),
        BOX_TRANSITION_U8(Rgb, b) } },
    [BOX_RENDER_TRANSITION_OPACITY] = { ECS_SIZEOF(Opacity), 1, {
        BOX_TRANSITION_FLOAT(Opacity, value) } },
    [BOX_RENDER_TRANSITION_ALPHA_CUTOFF] = { ECS_SIZEOF(AlphaCutoff), 1, {
        BOX_TRANSITION_FLOAT(AlphaCutoff, value) } },
    [BOX_RENDER_TRANSITION_TINT] = { ECS_SIZEOF(Tint), 4, {
        BOX_TRANSITION_U8(Tint, r),
        BOX_TRANSITION_U8(Tint, g),
        BOX_TRANSITION_U8(Tint, b),
        BOX_TRANSITION_U8(Tint, a) } },
    [BOX_RENDER_TRANSITION_PBR_MATERIAL] = { ECS_SIZEOF(PbrMaterial), 2, {
        BOX_TRANSITION_FLOAT(PbrMaterial, metallic),
        BOX_TRANSITION_FLOAT(PbrMaterial, roughness) } },
    [BOX_RENDER_TRANSITION_EMISSIVE] = { ECS_SIZEOF(Emissive), 4, {
        BOX_TRANSITION_FLOAT(Emissive, strength),
        BOX_TRANSITION_U8(Emissive, color.r),
        BOX_TRANSITION_U8(Emissive, color.g),
        BOX_TRANSITION_U8(Emissive, color.b) } },
    [BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM] = { ECS_SIZEOF(TextureTransform), 4, {
        BOX_TRANSITION_FLOAT(TextureTransform, scale_x),
        BOX_TRANSITION_FLOAT(TextureTransform, scale_y),
        BOX_TRANSITION_FLOAT(TextureTransform, offset_x),
        BOX_TRANSITION_FLOAT(TextureTransform, offset_y) } }
};

const box_render_transition_layout_t* box_render_transition_layout(
    box_render_transition_component_t component)
{
    ecs_assert(component >= 0 && component < BOX_RENDER_TRANSITION_COMPONENTS,
        ECS_INVALID_PARAMETER, NULL);

    return &box_transition_layouts[component];
}

/** Get the size in bytes of a field.
 *
 * @param field The field
 * @return 4 for a float, 1 for a uint8_t
 */
static int32_t box_transition_field_size(
    const box_render_transition_field_t *field)
{
    return field->kind == BOX_RENDER_TRANSITION_F32 ? ECS_SIZEOF(float) : ECS_SIZEOF(uint8_t);
}

void box_render_transition_pack(
    box_render_transition_component_t component,
    const void *src,
    void *dst)
{
    const box_render_transition_layout_t *layout = box_render_transition_layout(component);
    int32_t i;
    ecs_os_memset(dst, 0, BOX_RENDER_TRANSITION_VALUE);
    for (i = 0; i < layout->count; i ++) {
        const box_render_transition_field_t *field = &layout->fields[i];
        ecs_os_memcpy(ECS_OFFSET(dst, field->offset), ECS_OFFSET(src, field->offset),
            box_transition_field_size(field));
    }
}

bool box_render_transition_equal(
    box_render_transition_component_t component,
    const void *a,
    const void *b)
{
    const box_render_transition_layout_t *layout = box_render_transition_layout(component);
    bool result = true;
    int32_t i;
    for (i = 0; i < layout->count && result; i ++) {
        const box_render_transition_field_t *field = &layout->fields[i];
        result = !ecs_os_memcmp(ECS_OFFSET(a, field->offset), ECS_OFFSET(b, field->offset),
            box_transition_field_size(field));
    }

    return result;
}

void box_render_transition_mix(
    box_render_transition_component_t component,
    const void *from,
    const void *to,
    float t,
    void *out)
{
    const box_render_transition_layout_t *layout = box_render_transition_layout(component);
    int32_t i;
    if (!(t > 0.0f)) {
        ecs_os_memcpy(out, from, BOX_RENDER_TRANSITION_VALUE);
        goto done;
    }

    if (t >= 1.0f) {
        ecs_os_memcpy(out, to, BOX_RENDER_TRANSITION_VALUE);
        goto done;
    }

    for (i = 0; i < layout->count; i ++) {
        const box_render_transition_field_t *field = &layout->fields[i];
        if (field->kind == BOX_RENDER_TRANSITION_F32) {
            float a, b;
            ecs_os_memcpy(&a, ECS_OFFSET(from, field->offset), ECS_SIZEOF(float));
            ecs_os_memcpy(&b, ECS_OFFSET(to, field->offset), ECS_SIZEOF(float));
            float v = a + (b - a) * t;
            ecs_os_memcpy(ECS_OFFSET(out, field->offset), &v, ECS_SIZEOF(float));
        } else {
            float a = (float)*(const uint8_t*)ECS_OFFSET(from, field->offset);
            float b = (float)*(const uint8_t*)ECS_OFFSET(to, field->offset);
            float v = a + (b - a) * t;
            v = v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
            *(uint8_t*)ECS_OFFSET(out, field->offset) = (uint8_t)(v + 0.5f);
        }
    }
done:
    return;
}

float box_render_transition_progress(
    double start,
    float delay,
    float duration,
    double time)
{
    double elapsed = time - start - (double)delay;
    float result;
    if (!(duration > 0.0f)) {
        result = elapsed >= 0.0 ? 1.0f : 0.0f;
    } else if (elapsed <= 0.0) {
        result = 0.0f;
    } else if (elapsed >= (double)duration) {
        result = 1.0f;
    } else {
        result = (float)(elapsed / (double)duration);
    }

    return result;
}

bool box_render_transition_running(
    double start,
    float delay,
    float duration,
    double time)
{
    return box_render_transition_progress(start, delay, duration, time) < 1.0f;
}

void box_render_transition_table_init(
    box_render_transition_table_t *t)
{
    ecs_vec_init_t(NULL, &t->nodes, box_render_transition_node_t, 0);
    ecs_vec_init_t(NULL, &t->active, uint32_t, 0);
}

void box_render_transition_table_fini(
    box_render_transition_table_t *t)
{
    ecs_vec_fini_t(NULL, &t->nodes, box_render_transition_node_t);
    ecs_vec_fini_t(NULL, &t->active, uint32_t);
}

/** Get a node of the table.
 *
 * @param t The table
 * @param node The node id
 * @return The node, NULL when the id is outside the table
 */
static box_render_transition_node_t* box_transition_node_get(
    const box_render_transition_table_t *t,
    uint32_t node)
{
    box_render_transition_node_t *result = NULL;
    if (node && (int32_t)node < ecs_vec_count(&t->nodes)) {
        result = ecs_vec_get_t(&t->nodes, box_render_transition_node_t, (int32_t)node);
    }

    return result;
}

/** Drop the values of a node; it stays in the active list until the next evaluation drops it.
 *
 * @param node The node
 * @param alive Whether the node exists afterwards
 */
static void box_transition_node_reset(
    box_render_transition_node_t *node,
    bool alive)
{
    bool listed = node->listed;
    ecs_os_memset_t(node, 0, box_render_transition_node_t);
    node->listed = listed;
    node->alive = alive;
    node->posed = -1;
}

/** Get the value a component of a node renders at a time, without changing it.
 *
 * @param v The value of the component
 * @param component The component
 * @param time The time
 * @param out Receives the value
 */
static void box_transition_value_at(
    const box_render_transition_value_t *v,
    box_render_transition_component_t component,
    double time,
    void *out)
{
    if (!v->active) {
        ecs_os_memcpy(out, v->current, BOX_RENDER_TRANSITION_VALUE);
    } else {
        float progress = box_render_transition_progress(v->start, v->delay, v->duration, time);
        box_render_transition_mix(component, v->from, v->target,
            box_transition_ease(v->timing_function, progress), out);
    }
}

/** Create a node, growing the table when the id is past its end. The active list holds each
 * node at most once and grows with the table, so it never grows in a frame where the table does
 * not.
 *
 * @param t The table
 * @param node The node id
 */
static void box_transition_create(
    box_render_transition_table_t *t,
    uint32_t node)
{
    int32_t count = (int32_t)node + 1;
    if (count > ecs_vec_count(&t->nodes)) {
        ecs_vec_set_min_count_zeromem_t(NULL, &t->nodes, box_render_transition_node_t, count);
        ecs_vec_set_min_size_t(NULL, &t->active, uint32_t, ecs_vec_count(&t->nodes));
    }

    box_transition_node_reset(box_transition_node_get(t, node), true);
}

/** Apply a start record: set the value at once, or start a transition from the value the node
 * renders at the start time.
 *
 * @param t The table
 * @param r The record
 */
static void box_transition_start(
    box_render_transition_table_t *t,
    const box_render_transition_record_t *r)
{
    box_render_transition_node_t *node = box_transition_node_get(t, r->node);
    if (!node || !node->alive || r->component < 0 ||
        r->component >= BOX_RENDER_TRANSITION_COMPONENTS)
    {
        goto done;
    }

    box_render_transition_value_t *v = &node->values[r->component];
    if (!v->present || !box_render_transition_running(r->start, r->delay, r->duration, r->start)) {
        ecs_os_memcpy(v->from, r->value, BOX_RENDER_TRANSITION_VALUE);
        ecs_os_memcpy(v->current, r->value, BOX_RENDER_TRANSITION_VALUE);
        ecs_os_memcpy(v->target, r->value, BOX_RENDER_TRANSITION_VALUE);
        if (v->active) {
            v->active = false;
            node->running --;
        }

        v->present = true;
        goto done;
    }

    box_transition_value_at(v, r->component, r->start, v->from);
    ecs_os_memcpy(v->current, v->from, BOX_RENDER_TRANSITION_VALUE);
    ecs_os_memcpy(v->target, r->value, BOX_RENDER_TRANSITION_VALUE);
    v->start = r->start;
    v->delay = r->delay;
    v->duration = r->duration;
    v->timing_function = r->timing_function;
    if (!v->active) {
        v->active = true;
        node->running ++;
    }

    if (!node->listed) {
        node->listed = true;
        *ecs_vec_append_t(NULL, &t->active, uint32_t) = r->node;
    }
done:
    return;
}

/** Apply the records of a packet in the order they were made.
 *
 * @param t The table
 * @param p The packet
 */
static void box_transition_apply(
    box_render_transition_table_t *t,
    const box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&p->transitions);
    const box_render_transition_record_t *records = ecs_vec_first(&p->transitions);
    for (i = 0; i < count; i ++) {
        const box_render_transition_record_t *r = &records[i];
        if (r->op == BOX_RENDER_TRANSITION_CREATE) {
            box_transition_create(t, r->node);
        } else if (r->op == BOX_RENDER_TRANSITION_DESTROY) {
            box_render_transition_node_t *node = box_transition_node_get(t, r->node);
            if (node) {
                box_transition_node_reset(node, false);
            }
        } else if (r->op == BOX_RENDER_TRANSITION_START) {
            box_transition_start(t, r);
        }
    }
}

/** Evaluate the running transitions of a node at a time, ending those that reached their
 * target.
 *
 * @param node The node
 * @param time The time
 */
static void box_transition_evaluate(
    box_render_transition_node_t *node,
    double time)
{
    int32_t c;
    for (c = 0; c < BOX_RENDER_TRANSITION_COMPONENTS; c ++) {
        box_render_transition_value_t *v = &node->values[c];
        if (!v->active) {
            continue;
        }

        if (!box_render_transition_running(v->start, v->delay, v->duration, time)) {
            ecs_os_memcpy(v->current, v->target, BOX_RENDER_TRANSITION_VALUE);
            v->active = false;
            node->running --;
        } else {
            box_transition_value_at(v, (box_render_transition_component_t)c, time, v->current);
        }
    }
}

/** Get a component of a node as it renders: the evaluated value while its transition runs,
 * else the target the pose record carries.
 *
 * @param node The node
 * @param component The component
 * @param target The target of the pose record
 * @param out Receives the three floats of the value
 */
static void box_transition_pose_value(
    const box_render_transition_node_t *node,
    box_render_transition_component_t component,
    const float *target,
    float *out)
{
    const box_render_transition_value_t *v = &node->values[component];
    if (v->active) {
        ecs_os_memcpy(out, v->current, 3 * ECS_SIZEOF(float));
    } else {
        ecs_os_memcpy(out, target, 3 * ECS_SIZEOF(float));
    }
}

/** Get a block of the packet data that a pose or slot record points at.
 *
 * @param p The packet
 * @param offset Offset of the block
 * @param size Bytes of the block
 * @return The block, NULL when it is outside the data
 */
static void* box_transition_data(
    box_render_packet_t *p,
    uint32_t offset,
    ecs_size_t size)
{
    void *result = NULL;
    if ((int64_t)offset + size <= (int64_t)ecs_vec_count(&p->data)) {
        result = ECS_OFFSET(ecs_vec_first(&p->data), offset);
    }

    return result;
}

/** Compute the rendered world matrix of a node from a pose record: the matrix of its parent
 * node, the target chain to the node and the local matrix of its rendered pose.
 *
 * box_transform_local builds it as the transform systems do, so a pose at its target is the
 * target matrix.
 *
 * @param t The table
 * @param p The packet
 * @param r The record
 */
static void box_transition_pose(
    box_render_transition_table_t *t,
    box_render_packet_t *p,
    const box_render_transition_record_t *r)
{
    box_render_transition_node_t *node = box_transition_node_get(t, r->node);
    const box_render_transition_node_t *parent = r->parent
        ? box_transition_node_get(t, r->parent) : NULL;
    box_render_transition_pose_t pose;
    const void *src = box_transition_data(p, r->data, ECS_SIZEOF(pose));
    Position position;
    Rotation rotation;
    Scale scale;
    mat4 rel, local, chain;
    if (!node || !node->alive || !src) {
        goto done;
    }

    if (r->parent && (!parent || !parent->alive || parent->posed != p->frame)) {
        goto done;
    }

    ecs_os_memcpy(&pose, src, ECS_SIZEOF(pose));
    box_transition_pose_value(node, BOX_RENDER_TRANSITION_POSITION, pose.position, &position.x);
    box_transition_pose_value(node, BOX_RENDER_TRANSITION_ROTATION, pose.rotation, &rotation.x);
    box_transition_pose_value(node, BOX_RENDER_TRANSITION_SCALE, pose.scale, &scale.x);
    box_transform_local(&position, &rotation, &scale, local);
    ecs_os_memcpy(rel, pose.rel, ECS_SIZEOF(mat4));
    if (parent) {
        glm_mul(ECS_CONST_CAST(vec4*, parent->world), rel, chain);
        glm_mul(chain, local, node->world);
    } else {
        glm_mul(rel, local, node->world);
    }

    node->posed = p->frame;
done:
    return;
}

/** Patch a transform slot of the packet data with the rows of the rendered matrix of its node
 * times the target chain of the slot.
 *
 * @param t The table
 * @param p The packet
 * @param r The record
 */
static void box_transition_slot(
    box_render_transition_table_t *t,
    box_render_packet_t *p,
    const box_render_transition_record_t *r)
{
    const box_render_transition_node_t *node = box_transition_node_get(t, r->node);
    const void *src = box_transition_data(p, r->data, ECS_SIZEOF(mat4));
    void *dst = box_transition_data(p, r->offset, ECS_SIZEOF(box_render_transform_t));
    box_render_transform_t out;
    mat4 rel, m;
    int32_t row, col;
    if (!node || !node->alive || node->posed != p->frame || !src || !dst) {
        goto done;
    }

    ecs_os_memcpy(rel, src, ECS_SIZEOF(mat4));
    glm_mul(ECS_CONST_CAST(vec4*, node->world), rel, m);
    for (row = 0; row < 3; row ++) {
        for (col = 0; col < 4; col ++) {
            out.rows[row][col] = m[col][row];
        }
    }

    ecs_os_memcpy(dst, &out, ECS_SIZEOF(box_render_transform_t));
done:
    return;
}

/** Apply the pose and slot records of a packet, in the order they were made, which puts a
 * parent node before its children and every pose before the slots.
 *
 * @param t The table
 * @param p The packet
 */
static void box_transition_patch(
    box_render_transition_table_t *t,
    box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&p->transitions);
    const box_render_transition_record_t *records = ecs_vec_first(&p->transitions);
    for (i = 0; i < count; i ++) {
        const box_render_transition_record_t *r = &records[i];
        if (r->op == BOX_RENDER_TRANSITION_POSE) {
            box_transition_pose(t, p, r);
        } else if (r->op == BOX_RENDER_TRANSITION_SLOT) {
            box_transition_slot(t, p, r);
        }
    }
}

void box_render_transition_resolve(
    box_render_transition_table_t *t,
    box_render_packet_t *p)
{
    int32_t i, kept = 0, count = ecs_vec_count(&t->active);
    if (!count && !ecs_vec_count(&p->transitions)) {
        goto done;
    }

    box_transition_apply(t, p);
    count = ecs_vec_count(&t->active);
    uint32_t *ids = ecs_vec_first(&t->active);
    for (i = 0; i < count; i ++) {
        box_render_transition_node_t *node = box_transition_node_get(t, ids[i]);
        if (node->alive && node->running) {
            box_transition_evaluate(node, p->time);
        }

        if (node->alive && node->running) {
            ids[kept ++] = ids[i];
        } else {
            node->listed = false;
        }
    }

    ecs_vec_set_count_t(NULL, &t->active, uint32_t, kept);
    box_transition_patch(t, p);
done:
    return;
}

bool box_render_transition_table_value(
    const box_render_transition_table_t *t,
    uint32_t node,
    box_render_transition_component_t component,
    void *out)
{
    const box_render_transition_node_t *n = box_transition_node_get(t, node);
    bool result = false;
    if (n && n->alive && n->values[component].present) {
        ecs_os_memcpy(out, n->values[component].current,
            box_render_transition_layout(component)->size);
        result = true;
    }

    return result;
}

int32_t box_render_transition_table_active(
    const box_render_transition_table_t *t)
{
    return ecs_vec_count(&t->active);
}
