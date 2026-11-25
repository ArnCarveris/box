/**
 * @file render/transition.c
 * @brief Transition resolver: the render thread table of transition nodes, the application of
 * the records of a packet and the evaluation of the running transitions on the packet clock.
 *
 * Runs where the packets are consumed (the render thread, or the main thread in main thread
 * mode) and never touches the world. Values are stored in the layout of their component, with a
 * description of the fields of each component, so a new component is an entry of the table.
 */

#include "draw.h"
#include <stddef.h>
#include <math.h>

/** A float field of a component */
#define BOX_TRANSITION_FLOAT(T, m) \
    { (uint16_t)offsetof(T, m), BOX_RENDER_TRANSITION_F32 }

/** A double field of a component */
#define BOX_TRANSITION_DOUBLE(T, m) \
    { (uint16_t)offsetof(T, m), BOX_RENDER_TRANSITION_F64 }

/** A uint8_t field of a component */
#define BOX_TRANSITION_U8(T, m) \
    { (uint16_t)offsetof(T, m), BOX_RENDER_TRANSITION_U8 }

/** Copy a member of a component between unaligned values */
#define BOX_TRANSITION_COPY(T, m, src, dst) \
    ecs_os_memcpy(ECS_OFFSET(dst, offsetof(T, m)), ECS_OFFSET(src, offsetof(T, m)), \
        ECS_SIZEOF(((T*)0)->m))

/* A value of a record or the table holds every transitionable component */
_Static_assert(sizeof(Position) <= BOX_RENDER_TRANSITION_VALUE, "Position fits a value");
_Static_assert(sizeof(Rotation) <= BOX_RENDER_TRANSITION_VALUE, "Rotation fits a value");
_Static_assert(sizeof(Scale) <= BOX_RENDER_TRANSITION_VALUE, "Scale fits a value");
_Static_assert(sizeof(Rgb) <= BOX_RENDER_TRANSITION_VALUE, "Rgb fits a value");
_Static_assert(sizeof(Opacity) <= BOX_RENDER_TRANSITION_VALUE, "Opacity fits a value");
_Static_assert(sizeof(AlphaCutoff) <= BOX_RENDER_TRANSITION_VALUE, "AlphaCutoff fits a value");
_Static_assert(sizeof(Tint) <= BOX_RENDER_TRANSITION_VALUE, "Tint fits a value");
_Static_assert(sizeof(PbrMaterial) <= BOX_RENDER_TRANSITION_VALUE, "PbrMaterial fits a value");
_Static_assert(sizeof(Emissive) <= BOX_RENDER_TRANSITION_VALUE, "Emissive fits a value");
_Static_assert(sizeof(TextureTransform) <= BOX_RENDER_TRANSITION_VALUE,
    "TextureTransform fits a value");
_Static_assert(sizeof(Transmission) <= BOX_RENDER_TRANSITION_VALUE, "Transmission fits a value");
_Static_assert(sizeof(ClearCoat) <= BOX_RENDER_TRANSITION_VALUE, "ClearCoat fits a value");
_Static_assert(sizeof(SpecularAnisotropy) <= BOX_RENDER_TRANSITION_VALUE,
    "SpecularAnisotropy fits a value");
_Static_assert(sizeof(Sheen) <= BOX_RENDER_TRANSITION_VALUE, "Sheen fits a value");
_Static_assert(sizeof(DiffuseTransmission) <= BOX_RENDER_TRANSITION_VALUE,
    "DiffuseTransmission fits a value");
_Static_assert(sizeof(Fresnel) <= BOX_RENDER_TRANSITION_VALUE, "Fresnel fits a value");
_Static_assert(sizeof(Distortion) <= BOX_RENDER_TRANSITION_VALUE, "Distortion fits a value");
_Static_assert(sizeof(BoxShadow) <= BOX_RENDER_TRANSITION_VALUE, "BoxShadow fits a value");
_Static_assert(sizeof(Border) <= BOX_RENDER_TRANSITION_VALUE, "Border fits a value");
_Static_assert(sizeof(PointLight) <= BOX_RENDER_TRANSITION_VALUE, "PointLight fits a value");
_Static_assert(sizeof(SpotLight) <= BOX_RENDER_TRANSITION_VALUE, "SpotLight fits a value");
_Static_assert(offsetof(PointLight, intensity) == offsetof(SpotLight, intensity) &&
    offsetof(PointLight, range) == offsetof(SpotLight, range),
    "A light record reads the intensity and range of both lights alike");
_Static_assert(BOX_RENDER_TRANSITION_VALUE % sizeof(float) == 0, "A value holds floats");
_Static_assert(BOX_RENDER_TRANSITION_COMPONENTS <= 32,
    "box_draw_transition_material_t.present holds a bit per component");

/** Layout of every transitionable component, indexed by box_render_transition_component_t */
static const box_render_transition_layout_t box_transition_layouts[
    BOX_RENDER_TRANSITION_COMPONENTS] =
{
    [BOX_RENDER_TRANSITION_POSITION] = { ECS_SIZEOF(Position), 3, {
        BOX_TRANSITION_DOUBLE(Position, x),
        BOX_TRANSITION_DOUBLE(Position, y),
        BOX_TRANSITION_DOUBLE(Position, z) } },
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
        BOX_TRANSITION_FLOAT(TextureTransform, offset_y) } },
    [BOX_RENDER_TRANSITION_TRANSMISSION] = { ECS_SIZEOF(Transmission), 7, {
        BOX_TRANSITION_FLOAT(Transmission, transmission_factor),
        BOX_TRANSITION_FLOAT(Transmission, ior),
        BOX_TRANSITION_FLOAT(Transmission, thickness_factor),
        BOX_TRANSITION_FLOAT(Transmission, attenuation_distance),
        BOX_TRANSITION_U8(Transmission, attenuation_color.r),
        BOX_TRANSITION_U8(Transmission, attenuation_color.g),
        BOX_TRANSITION_U8(Transmission, attenuation_color.b) } },
    [BOX_RENDER_TRANSITION_CLEAR_COAT] = { ECS_SIZEOF(ClearCoat), 2, {
        BOX_TRANSITION_FLOAT(ClearCoat, strength),
        BOX_TRANSITION_FLOAT(ClearCoat, roughness) } },
    [BOX_RENDER_TRANSITION_ANISOTROPY] = { ECS_SIZEOF(SpecularAnisotropy), 2, {
        BOX_TRANSITION_FLOAT(SpecularAnisotropy, strength),
        BOX_TRANSITION_FLOAT(SpecularAnisotropy, rotation) } },
    [BOX_RENDER_TRANSITION_SHEEN] = { ECS_SIZEOF(Sheen), 4, {
        BOX_TRANSITION_U8(Sheen, color.r),
        BOX_TRANSITION_U8(Sheen, color.g),
        BOX_TRANSITION_U8(Sheen, color.b),
        BOX_TRANSITION_FLOAT(Sheen, roughness) } },
    [BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION] = { ECS_SIZEOF(DiffuseTransmission), 6, {
        BOX_TRANSITION_FLOAT(DiffuseTransmission, factor),
        BOX_TRANSITION_U8(DiffuseTransmission, color.r),
        BOX_TRANSITION_U8(DiffuseTransmission, color.g),
        BOX_TRANSITION_U8(DiffuseTransmission, color.b),
        BOX_TRANSITION_FLOAT(DiffuseTransmission, thickness),
        BOX_TRANSITION_FLOAT(DiffuseTransmission, blur) } },
    [BOX_RENDER_TRANSITION_FRESNEL] = { ECS_SIZEOF(Fresnel), 7, {
        BOX_TRANSITION_U8(Fresnel, color.r),
        BOX_TRANSITION_U8(Fresnel, color.g),
        BOX_TRANSITION_U8(Fresnel, color.b),
        BOX_TRANSITION_FLOAT(Fresnel, strength),
        BOX_TRANSITION_FLOAT(Fresnel, power),
        BOX_TRANSITION_FLOAT(Fresnel, bias),
        BOX_TRANSITION_FLOAT(Fresnel, opacity) } },
    [BOX_RENDER_TRANSITION_DISTORTION] = { ECS_SIZEOF(Distortion), 3, {
        BOX_TRANSITION_FLOAT(Distortion, strength),
        BOX_TRANSITION_FLOAT(Distortion, scale),
        BOX_TRANSITION_FLOAT(Distortion, speed) } },
    [BOX_RENDER_TRANSITION_BOX_SHADOW] = { ECS_SIZEOF(BoxShadow), 8, {
        BOX_TRANSITION_FLOAT(BoxShadow, offset_x),
        BOX_TRANSITION_FLOAT(BoxShadow, offset_y),
        BOX_TRANSITION_FLOAT(BoxShadow, blur),
        BOX_TRANSITION_FLOAT(BoxShadow, spread),
        BOX_TRANSITION_U8(BoxShadow, color.r),
        BOX_TRANSITION_U8(BoxShadow, color.g),
        BOX_TRANSITION_U8(BoxShadow, color.b),
        BOX_TRANSITION_U8(BoxShadow, color.a) } },
    [BOX_RENDER_TRANSITION_BORDER] = { ECS_SIZEOF(Border), 6, {
        BOX_TRANSITION_FLOAT(Border, width),
        BOX_TRANSITION_FLOAT(Border, radius),
        BOX_TRANSITION_U8(Border, color.r),
        BOX_TRANSITION_U8(Border, color.g),
        BOX_TRANSITION_U8(Border, color.b),
        BOX_TRANSITION_U8(Border, color.a) } },
    [BOX_RENDER_TRANSITION_POINT_LIGHT] = { ECS_SIZEOF(PointLight), 2, {
        BOX_TRANSITION_FLOAT(PointLight, intensity),
        BOX_TRANSITION_FLOAT(PointLight, range) } },
    [BOX_RENDER_TRANSITION_SPOT_LIGHT] = { ECS_SIZEOF(SpotLight), 4, {
        BOX_TRANSITION_FLOAT(SpotLight, intensity),
        BOX_TRANSITION_FLOAT(SpotLight, range),
        BOX_TRANSITION_FLOAT(SpotLight, inner_angle),
        BOX_TRANSITION_FLOAT(SpotLight, outer_angle) } }
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
    int32_t result = ECS_SIZEOF(uint8_t);
    if (field->kind == BOX_RENDER_TRANSITION_F32) {
        result = ECS_SIZEOF(float);
    } else if (field->kind == BOX_RENDER_TRANSITION_F64) {
        result = ECS_SIZEOF(double);
    }

    return result;
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

    if (component == BOX_RENDER_TRANSITION_POINT_LIGHT) {
        BOX_TRANSITION_COPY(PointLight, bake, src, dst);
        BOX_TRANSITION_COPY(PointLight, shadows, src, dst);
        BOX_TRANSITION_COPY(PointLight, shadow_resolution, src, dst);
    } else if (component == BOX_RENDER_TRANSITION_SPOT_LIGHT) {
        BOX_TRANSITION_COPY(SpotLight, bake, src, dst);
        BOX_TRANSITION_COPY(SpotLight, shadows, src, dst);
        BOX_TRANSITION_COPY(SpotLight, shadow_resolution, src, dst);
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
    if (t == 0.0f || isnan(t)) {
        ecs_os_memcpy(out, from, BOX_RENDER_TRANSITION_VALUE);
        goto done;
    }

    if (t == 1.0f) {
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
        } else if (field->kind == BOX_RENDER_TRANSITION_F64) {
            double a, b;
            ecs_os_memcpy(&a, ECS_OFFSET(from, field->offset), ECS_SIZEOF(double));
            ecs_os_memcpy(&b, ECS_OFFSET(to, field->offset), ECS_SIZEOF(double));
            double v = a + (b - a) * (double)t;
            ecs_os_memcpy(ECS_OFFSET(out, field->offset), &v, ECS_SIZEOF(double));
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

void box_render_transition_widen(
    box_render_transition_component_t component,
    const TransitionSpec *spec,
    float *lo,
    float *hi)
{
    float over;
    int32_t i, count = box_render_transition_layout(component)->count;
    count = count < 4 ? count : 4;
    if (!box_render_transition_ranged(component) || spec->timing_function != CubicBezier) {
        goto done;
    }

    over = glm_max(glm_max(spec->bezier.y1, spec->bezier.y2) - 1.0f,
        -glm_min(spec->bezier.y1, spec->bezier.y2));
    if (!(over > 0.0f)) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        float width = (hi[i] - lo[i]) * over;
        lo[i] -= width;
        hi[i] += width;
    }
done:
    return;
}

bool box_render_transition_ranged(
    box_render_transition_component_t component)
{
    return component <= BOX_RENDER_TRANSITION_SCALE ||
        component == BOX_RENDER_TRANSITION_BOX_SHADOW ||
        component == BOX_RENDER_TRANSITION_POINT_LIGHT ||
        component == BOX_RENDER_TRANSITION_SPOT_LIGHT;
}

bool box_render_transition_neutral(
    box_render_transition_component_t component,
    const void *target,
    void *out)
{
    float value[BOX_RENDER_TRANSITION_VALUE / sizeof(float)];
    bool result = true;
    ecs_os_memcpy(value, target, BOX_RENDER_TRANSITION_VALUE);
    switch (component) {
    case BOX_RENDER_TRANSITION_ROTATION:
        *(Rotation*)value = (Rotation){0};
        break;
    case BOX_RENDER_TRANSITION_SCALE:
        *(Scale*)value = (Scale){1.0f, 1.0f, 1.0f};
        break;
    case BOX_RENDER_TRANSITION_OPACITY:
        ((Opacity*)value)->value = 1.0f;
        break;
    case BOX_RENDER_TRANSITION_TINT:
        ((Tint*)value)->a = 0;
        break;
    case BOX_RENDER_TRANSITION_EMISSIVE:
        ((Emissive*)value)->strength = 0.0f;
        break;
    case BOX_RENDER_TRANSITION_PBR_MATERIAL:
        ((PbrMaterial*)value)->metallic = BOX_MATERIAL_METALLIC;
        ((PbrMaterial*)value)->roughness = BOX_MATERIAL_ROUGHNESS;
        break;
    case BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM:
        ((TextureTransform*)value)->scale_x = 1.0f;
        ((TextureTransform*)value)->scale_y = 1.0f;
        ((TextureTransform*)value)->offset_x = 0.0f;
        ((TextureTransform*)value)->offset_y = 0.0f;
        break;
    case BOX_RENDER_TRANSITION_CLEAR_COAT:
        ((ClearCoat*)value)->strength = 0.0f;
        break;
    case BOX_RENDER_TRANSITION_ANISOTROPY:
        ((SpecularAnisotropy*)value)->strength = 0.0f;
        break;
    case BOX_RENDER_TRANSITION_SHEEN:
        ((Sheen*)value)->color = (Rgb){0, 0, 0};
        break;
    case BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION:
        ((DiffuseTransmission*)value)->factor = 0.0f;
        ((DiffuseTransmission*)value)->thickness = 0.0f;
        ((DiffuseTransmission*)value)->blur = 0.0f;
        break;
    case BOX_RENDER_TRANSITION_FRESNEL:
        ((Fresnel*)value)->strength = 0.0f;
        ((Fresnel*)value)->opacity = 0.0f;
        break;
    case BOX_RENDER_TRANSITION_DISTORTION:
        ((Distortion*)value)->strength = 0.0f;
        break;
    case BOX_RENDER_TRANSITION_BOX_SHADOW:
        ((BoxShadow*)value)->color.a = 0;
        break;
    case BOX_RENDER_TRANSITION_POINT_LIGHT:
        ((PointLight*)value)->intensity = 0.0f;
        break;
    case BOX_RENDER_TRANSITION_SPOT_LIGHT:
        ((SpotLight*)value)->intensity = 0.0f;
        break;
    case BOX_RENDER_TRANSITION_POSITION:
    case BOX_RENDER_TRANSITION_RGB:
    case BOX_RENDER_TRANSITION_ALPHA_CUTOFF:
    case BOX_RENDER_TRANSITION_TRANSMISSION:
    case BOX_RENDER_TRANSITION_BORDER:
    case BOX_RENDER_TRANSITION_COMPONENTS:
        result = false;
        break;
    }

    ecs_os_memcpy(out, value, BOX_RENDER_TRANSITION_VALUE);

    return result;
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
    node->faded = -1;
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
        float eased = v->timing_function == CubicBezier
            ? box_transition_ease_bezier(CubicBezier, &v->bezier, progress)
            : box_transition_ease(v->timing_function, progress);
        box_render_transition_mix(component, v->from, v->target, eased, out);
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
    v->bezier = r->bezier;
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
    WorldTransform local_t;
    mat4 rel, local, chain;
    if (!node || !node->alive || !src) {
        goto done;
    }

    if (r->parent && (!parent || !parent->alive || parent->posed != p->frame)) {
        goto done;
    }

    ecs_os_memcpy(&pose, src, ECS_SIZEOF(pose));
    const box_render_transition_value_t *pv = &node->values[BOX_RENDER_TRANSITION_POSITION];
    if (pv->active) {
        ecs_os_memcpy(&position, pv->current, ECS_SIZEOF(Position));
    } else {
        ecs_os_memcpy(&position, pose.position, ECS_SIZEOF(Position));
    }

    position.x -= pose.offset[0];
    position.y -= pose.offset[1];
    position.z -= pose.offset[2];
    box_transition_pose_value(node, BOX_RENDER_TRANSITION_ROTATION, pose.rotation, &rotation.x);
    box_transition_pose_value(node, BOX_RENDER_TRANSITION_SCALE, pose.scale, &scale.x);
    box_transform_local(&position, &rotation, &scale, &local_t);
    box_transform_mat4(&local_t, NULL, local);
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
    if (!node || !node->alive || node->posed != p->frame || !src || !dst) {
        goto done;
    }

    ecs_os_memcpy(rel, src, ECS_SIZEOF(mat4));
    glm_mul(ECS_CONST_CAST(vec4*, node->world), rel, m);
    box_render_transform_from_mat4(&out, m, p->origin);

    ecs_os_memcpy(dst, &out, ECS_SIZEOF(box_render_transform_t));
done:
    return;
}

/** Get the Tint and Opacity a node renders: the evaluated value while its transition runs,
 * else the target the fade record carries.
 *
 * @param node The node
 * @param fade The fade record
 * @param tint Receives the tint, r, g, b, a
 * @param opacity Receives the opacity, 1 without one
 * @return Whether the node has a Tint
 */
static bool box_transition_fade_own(
    const box_render_transition_node_t *node,
    const box_render_transition_fade_t *fade,
    uint8_t *tint,
    float *opacity)
{
    const box_render_transition_value_t *t = &node->values[BOX_RENDER_TRANSITION_TINT];
    const box_render_transition_value_t *o = &node->values[BOX_RENDER_TRANSITION_OPACITY];
    bool result = (fade->present & (1u << BOX_RENDER_TRANSITION_TINT)) != 0;
    *opacity = 1.0f;
    if (fade->present & (1u << BOX_RENDER_TRANSITION_OPACITY)) {
        *opacity = fade->opacity;
        if (o->active) {
            ecs_os_memcpy(opacity, o->current, ECS_SIZEOF(float));
        }
    }

    if (result) {
        ecs_os_memcpy(tint, fade->tint, 4);
        if (t->active) {
            ecs_os_memcpy(tint, t->current, 4);
        }
    }

    return result;
}

/** Compute the rendered tints and opacity of a node from a fade record: what its parent node
 * renders, the composed values of the entities between them, and the rendered Tint and Opacity
 * of the node, in the order box_draws_fade and box_draws_mesh_shading apply them.
 *
 * Nothing is divided, so an opacity of 0 works.
 *
 * @param t The table
 * @param p The packet
 * @param r The record
 */
static void box_transition_fade(
    box_render_transition_table_t *t,
    box_render_packet_t *p,
    const box_render_transition_record_t *r)
{
    box_render_transition_node_t *node = box_transition_node_get(t, r->node);
    const box_render_transition_node_t *parent = r->parent
        ? box_transition_node_get(t, r->parent) : NULL;
    box_render_transition_fade_t fade;
    const void *src = box_transition_data(p, r->data, ECS_SIZEOF(fade));
    uint8_t tint[4];
    float opacity;
    int32_t i;
    if (!node || !node->alive || !src) {
        goto done;
    }

    if (r->parent && (!parent || !parent->alive || parent->faded != p->frame)) {
        goto done;
    }

    ecs_os_memcpy(&fade, src, ECS_SIZEOF(fade));
    node->above_keep = fade.keep;
    node->above_alpha = fade.alpha;
    for (i = 0; i < 3; i ++) {
        node->above_add[i] = fade.add[i];
        if (parent) {
            node->above_add[i] += parent->fade_add[i] * fade.keep;
        }
    }

    if (parent) {
        node->above_keep *= parent->fade_keep;
        node->above_alpha *= parent->fade_alpha;
    }

    node->fade_keep = node->above_keep;
    glm_vec3_copy(node->above_add, node->fade_add);
    if (box_transition_fade_own(node, &fade, tint, &opacity)) {
        float a = (float)tint[3] / 255.0f;
        for (i = 0; i < 3; i ++) {
            node->fade_add[i] = node->fade_add[i] * (1.0f - a) + (float)tint[i] / 255.0f * a;
        }

        node->fade_keep *= 1.0f - a;
    }

    node->fade_alpha = node->above_alpha * glm_clamp(opacity, 0.0f, 1.0f);
    node->faded = p->frame;
done:
    return;
}

/** Replace the tints and fade of the ancestors of a material by those its fade node renders.
 *
 * @param t The table
 * @param p The packet
 * @param material The material
 * @return Whether the node rendered in this packet, false to leave the record as it is
 */
static bool box_transition_material_fade(
    const box_render_transition_table_t *t,
    const box_render_packet_t *p,
    box_draw_transition_material_t *material)
{
    const box_render_transition_node_t *node = box_transition_node_get(t, material->fade_node);
    bool result = node && node->alive && node->faded == p->frame;
    const box_draw_tint_t *rel = &material->fade_rel;
    int32_t i;
    if (!result) {
        goto done;
    }

    material->tinted = true;
    if (material->fade_self) {
        glm_vec3_copy(ECS_CONST_CAST(float*, node->above_add), material->inherited.add);
        material->inherited.keep = node->above_keep;
        material->fade = node->above_alpha;
        goto done;
    }

    for (i = 0; i < 3; i ++) {
        material->inherited.add[i] = node->fade_add[i] * rel->keep + rel->add[i];
    }

    material->inherited.keep = node->fade_keep * rel->keep;
    material->fade = node->fade_alpha * material->fade_alpha;
done:
    return result;
}

/** Replace the value of each component of a material that has a node by the value the node
 * renders.
 *
 * @param t The table
 * @param material The material
 */
static void box_transition_material_rendered(
    const box_render_transition_table_t *t,
    box_draw_transition_material_t *material)
{
    int32_t c;
    for (c = 0; c < BOX_RENDER_TRANSITION_COMPONENTS; c ++) {
        box_render_transition_component_t component = (box_render_transition_component_t)c;
        const box_render_transition_node_t *node = material->nodes[c]
            ? box_transition_node_get(t, material->nodes[c]) : NULL;
        void *value = box_draws_transition_value(material, component);
        if (!node || !node->alive || !node->values[c].present || !value ||
            !(material->present & (1u << c)))
        {
            continue;
        }

        ecs_os_memcpy(value, node->values[c].current,
            box_render_transition_layout(component)->size);
        if (component == BOX_RENDER_TRANSITION_OPACITY) {
            material->opacity.value = glm_clamp(material->opacity.value, 0.0f, 1.0f) *
                material->opacity_scale;
        }
    }
}

/** Patch a record of the material table or a 2D shading in the packet data: the target
 * material of the record with the rendered value of each component that has a node, encoded
 * with the functions of the game thread (box_draws_transition_encode).
 *
 * @param t The table
 * @param p The packet
 * @param r The record
 */
static void box_transition_material(
    box_render_transition_table_t *t,
    box_render_packet_t *p,
    const box_render_transition_record_t *r)
{
    box_draw_transition_material_t material;
    const void *src = box_transition_data(p, r->data, ECS_SIZEOF(material));
    ecs_size_t size = ECS_SIZEOF(box_render_material_t);
    void *dst;
    if (!src) {
        goto done;
    }

    ecs_os_memcpy(&material, src, ECS_SIZEOF(material));
    if (r->op == BOX_RENDER_TRANSITION_MATERIAL) {
        material.kind = BOX_DRAW_TRANSITION_TABLE;
    } else if (material.kind == BOX_DRAW_TRANSITION_TABLE ||
        material.kind > BOX_DRAW_TRANSITION_BOX_SHADOW)
    {
        goto done;
    } else if (material.kind == BOX_DRAW_TRANSITION_MESH) {
        size = ECS_SIZEOF(box_render_mesh_shading_t);
    } else {
        size = ECS_SIZEOF(box_render_shading_t);
    }

    dst = box_transition_data(p, r->offset, size);
    if (!dst) {
        goto done;
    }

    if (material.fade_node && !box_transition_material_fade(t, p, &material)) {
        goto done;
    }

    box_transition_material_rendered(t, &material);
    box_draws_transition_encode(&material, dst);
done:
    return;
}

/** Patch the transform and the shading of the shadow of a 2D shape whose BoxShadow transitions
 * with the functions of the game thread (box_draws_box_shadow_patch): the BoxShadow its node
 * renders, with the Tint and Opacity its shape renders at.
 *
 * The world matrix is that of the node that moves the shape times the target chain, else the
 * target. A fade node without a fade record in the packet leaves the tints of the target.
 *
 * @param t The table
 * @param p The packet
 * @param r The record
 */
static void box_transition_box_shadow(
    box_render_transition_table_t *t,
    box_render_packet_t *p,
    const box_render_transition_record_t *r)
{
    box_draw_transition_box_shadow_t record;
    const void *src = box_transition_data(p, r->data, ECS_SIZEOF(record));
    void *shading = box_transition_data(p, r->offset, ECS_SIZEOF(box_render_shading_t));
    const box_render_transition_node_t *node = box_transition_node_get(t, r->node);
    const box_render_transition_node_t *mover = r->parent
        ? box_transition_node_get(t, r->parent) : NULL;
    const box_render_transition_value_t *v;
    BoxShadow shadow;
    void *transform;
    mat4 world, rel;
    if (!src || !shading || !node || !node->alive) {
        goto done;
    }

    ecs_os_memcpy(&record, src, ECS_SIZEOF(record));
    transform = box_transition_data(p, record.transform, ECS_SIZEOF(box_render_transform_t));
    if (!transform) {
        goto done;
    }

    shadow = record.shadow;
    v = &node->values[BOX_RENDER_TRANSITION_BOX_SHADOW];
    if (v->present) {
        ecs_os_memcpy(&shadow, v->current, ECS_SIZEOF(BoxShadow));
    }

    if (mover && mover->alive && mover->posed == p->frame) {
        ecs_os_memcpy(rel, record.rel, ECS_SIZEOF(mat4));
        glm_mul(ECS_CONST_CAST(vec4*, mover->world), rel, world);
    } else {
        ecs_os_memcpy(world, record.world, ECS_SIZEOF(mat4));
    }

    if (record.material.fade_node) {
        box_transition_material_fade(t, p, &record.material);
    }

    box_transition_material_rendered(t, &record.material);
    box_draws_transition_box_shadow(&record, &shadow, world, p->origin, transform, shading);
done:
    return;
}

/** Patch a light of a light buffer upload with the light and the Rgb its nodes render, with
 * the function of the game thread (box_render_lights_patch). A node that does not have the
 * value leaves the target of the record.
 *
 * @param t The table
 * @param p The packet
 * @param r The record
 */
static void box_transition_light(
    box_render_transition_table_t *t,
    box_render_packet_t *p,
    const box_render_transition_record_t *r)
{
    box_render_transition_light_t target;
    const void *src = box_transition_data(p, r->data, ECS_SIZEOF(target));
    void *dst = box_transition_data(p, r->offset, ECS_SIZEOF(box_render_light_t));
    const box_render_transition_node_t *node = box_transition_node_get(t, r->node);
    const box_render_transition_node_t *rgb_node;
    box_render_light_t light;
    float values[4];
    Rgb rgb;
    if (!src || !dst) {
        goto done;
    }

    ecs_os_memcpy(&target, src, ECS_SIZEOF(target));
    ecs_os_memcpy(&light, dst, ECS_SIZEOF(light));
    ecs_os_memcpy(values, target.values, ECS_SIZEOF(values));
    rgb = (Rgb){ target.rgb[0], target.rgb[1], target.rgb[2] };
    if (node && node->alive && target.kind) {
        box_render_transition_component_t component = target.kind == 1
            ? BOX_RENDER_TRANSITION_POINT_LIGHT
            : BOX_RENDER_TRANSITION_SPOT_LIGHT;
        const box_render_transition_value_t *v = &node->values[component];
        if (v->present) {
            ecs_os_memcpy(values, v->current,
                box_render_transition_layout(component)->count * ECS_SIZEOF(float));
        }
    }

    rgb_node = target.rgb_node ? box_transition_node_get(t, target.rgb_node) : NULL;
    if (rgb_node && rgb_node->alive && rgb_node->values[BOX_RENDER_TRANSITION_RGB].present) {
        ecs_os_memcpy(&rgb, rgb_node->values[BOX_RENDER_TRANSITION_RGB].current,
            ECS_SIZEOF(Rgb));
    }

    box_render_lights_patch(&target, values, &rgb, &light);
    ecs_os_memcpy(dst, &light, ECS_SIZEOF(light));
done:
    return;
}

/** Apply the pose, slot, fade, material, box shadow and light records of a packet, in the
 * order they were made, which puts a parent node before its children and every pose and fade
 * before the records that read it.
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
        } else if (r->op == BOX_RENDER_TRANSITION_MATERIAL ||
            r->op == BOX_RENDER_TRANSITION_SHADING)
        {
            box_transition_material(t, p, r);
        } else if (r->op == BOX_RENDER_TRANSITION_FADE) {
            box_transition_fade(t, p, r);
        } else if (r->op == BOX_RENDER_TRANSITION_SHADOW_BOX) {
            box_transition_box_shadow(t, p, r);
        } else if (r->op == BOX_RENDER_TRANSITION_LIGHT) {
            box_transition_light(t, p, r);
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
