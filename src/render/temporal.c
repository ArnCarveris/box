/**
 * @file render/temporal.c
 * @brief History of the views from frame to frame, which the temporal effects of a backend read,
 * and the low discrepancy sequences of a frame index.
 *
 * A backend keeps a history per target and view: the view projection and camera position of the
 * previous frame the view rendered and the frames since its last reset. It counts rendered
 * frames, not time, so a headless run with a fixed delta time is deterministic.
 */

#include "render.h"
#include <math.h>
#include <string.h>

/** Golden ratio of the plane, the root of x^3 = x + 1 that the R2 sequence steps by */
#define BOX_RENDER_R2_G (1.32471795724474602596)

/** Fractional part of a value.
 *
 * @param x The value
 * @return x - floor(x), in [0, 1)
 */
static float box_render_temporal_fract(
    float x)
{
    float result = x - floorf(x);

    return result < 1.0f ? result : 0.0f;
}

/** Radical inverse of an index in a base: its digits mirrored around the radix point.
 *
 * @param index The index
 * @param base The base, at least 2
 * @return The inverse in [0, 1)
 */
static float box_render_temporal_radical(
    uint32_t index,
    uint32_t base)
{
    float inv = 1.0f / (float)base, f = inv, result = 0.0f;
    uint32_t n = index;
    while (n > 0) {
        result += f * (float)(n % base);
        n /= base;
        f *= inv;
    }

    return result;
}

void box_render_temporal_sequence_2d(
    uint32_t index,
    float *out)
{
    float n = (float)(index % BOX_RENDER_TEMPORAL_LENGTH);
    out[0] = box_render_temporal_fract(0.5f + n * (float)(1.0 / BOX_RENDER_R2_G));
    out[1] = box_render_temporal_fract(
        0.5f + n * (float)(1.0 / (BOX_RENDER_R2_G * BOX_RENDER_R2_G)));
}

void box_render_temporal_sequence_3d(
    uint32_t index,
    float *out)
{
    uint32_t n = index % BOX_RENDER_TEMPORAL_LENGTH + 1;
    out[0] = box_render_temporal_radical(n, 2);
    out[1] = box_render_temporal_radical(n, 3);
    out[2] = box_render_temporal_radical(n, 5);
}

/** Find the index of the history of a view in a target.
 *
 * @param histories box_render_history_t
 * @param target Handle of the target, 0 for any
 * @param view View entity
 * @return The index, -1 for none
 */
static int32_t box_render_history_index(
    const ecs_vec_t *histories,
    uint32_t target,
    ecs_entity_t view)
{
    int32_t i, result = -1, count = ecs_vec_count(histories);
    for (i = 0; i < count && result < 0; i ++) {
        const box_render_history_t *h = ecs_vec_get_t(histories, box_render_history_t, i);
        if ((!target || h->target == target) && h->view == view) {
            result = i;
        }
    }

    return result;
}

const box_render_history_t* box_render_history_find(
    const ecs_vec_t *histories,
    uint32_t target,
    ecs_entity_t view)
{
    int32_t i = box_render_history_index(histories, target, view);

    return i < 0 ? NULL : ecs_vec_get_t(histories, box_render_history_t, i);
}

/** Check whether the history of a view carries over to a frame.
 *
 * @param h The history, of the last frame the view rendered
 * @param v The view
 * @param width Render width of the frame
 * @param height Render height of the frame
 * @param proj Projection of the frame
 * @return Whether the frame continues the history
 */
static bool box_render_history_continues(
    const box_render_history_t *h,
    const box_render_view_t *v,
    int32_t width,
    int32_t height,
    const mat4 proj)
{
    return !v->history_reset && h->camera == v->camera && h->width == width &&
        h->height == height && !memcmp(h->proj, proj, sizeof(mat4));
}

const box_render_temporal_t* box_render_history_update(
    ecs_vec_t *histories,
    uint32_t target,
    const box_render_view_t *v,
    int64_t frame,
    int32_t width,
    int32_t height,
    const mat4 proj)
{
    int32_t i = box_render_history_index(histories, target, v->view);
    box_render_history_t *h = NULL;
    bool first = i < 0;
    mat4 view_proj;
    if (first) {
        h = ecs_vec_append_t(NULL, histories, box_render_history_t);
        ecs_os_memset_t(h, 0, box_render_history_t);
        h->target = target;
        h->view = v->view;
    } else {
        h = ecs_vec_get_t(histories, box_render_history_t, i);
        if (h->frame == frame) {
            goto done;
        }
    }

    glm_mat4_mul(ECS_CONST_CAST(vec4*, proj), ECS_CONST_CAST(vec4*, v->view_matrix), view_proj);
    box_render_temporal_t *t = &h->current;
    if (!first && box_render_history_continues(h, v, width, height, proj)) {
        glm_mat4_copy(h->view_proj, t->prev_view_proj);
        glm_vec4_copy(h->camera_position, t->prev_camera);
        t->frame_index ++;
        t->valid = true;
        t->prev_frame = h->frame;
    } else {
        glm_mat4_copy(view_proj, t->prev_view_proj);
        glm_vec3_copy(ECS_CONST_CAST(float*, v->camera_transform[3]), t->prev_camera);
        t->prev_camera[3] = 1.0f;
        t->frame_index = 0;
        t->valid = false;
        t->prev_frame = -1;
    }

    h->frame = frame;
    h->camera = v->camera;
    h->width = width;
    h->height = height;
    glm_mat4_copy(ECS_CONST_CAST(vec4*, proj), h->proj);
    glm_mat4_copy(view_proj, h->view_proj);
    glm_vec3_copy(ECS_CONST_CAST(float*, v->camera_transform[3]), h->camera_position);
    h->camera_position[3] = 1.0f;
done:
    return &h->current;
}

void box_render_history_sweep(
    ecs_vec_t *histories,
    int64_t frame)
{
    int32_t i;
    for (i = ecs_vec_count(histories) - 1; i >= 0; i --) {
        if (ecs_vec_get_t(histories, box_render_history_t, i)->frame != frame) {
            ecs_vec_remove_t(histories, box_render_history_t, i);
        }
    }
}
