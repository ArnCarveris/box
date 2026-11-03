/**
 * @file render/transition_bounds.c
 * @brief The bounds of the poses a transform transition can render a shadow caster at.
 *
 * The game thread does not know the value the resolver renders, only a range each field lies
 * in. The matrices are carried as ranges, element by element, through the same chain of
 * products the resolver computes, so the bounds hold every matrix it can render with.
 */

#include "draw.h"
#include <math.h>

void box_draws_range_point(
    mat4 m,
    box_draw_transition_range_t *out)
{
    glm_mat4_copy(m, out->lo);
    glm_mat4_copy(m, out->hi);
}

/** Get the range of the product of two values that lie in ranges.
 *
 * @param a_lo Minimum of a
 * @param a_hi Maximum of a
 * @param b_lo Minimum of b
 * @param b_hi Maximum of b
 * @param lo Receives the minimum of the product
 * @param hi Receives the maximum of the product
 */
static void box_draws_range_product(
    float a_lo,
    float a_hi,
    float b_lo,
    float b_hi,
    float *lo,
    float *hi)
{
    float p0 = a_lo * b_lo, p1 = a_lo * b_hi, p2 = a_hi * b_lo, p3 = a_hi * b_hi;
    *lo = glm_min(glm_min(p0, p1), glm_min(p2, p3));
    *hi = glm_max(glm_max(p0, p1), glm_max(p2, p3));
}

/** Check whether a range holds a single matrix.
 *
 * @param r The range
 * @return Whether its minimum and maximum are the same, bit for bit
 */
static bool box_draws_range_single(
    const box_draw_transition_range_t *r)
{
    return !ecs_os_memcmp(r->lo, r->hi, ECS_SIZEOF(mat4));
}

/** Get the range of the product of a single value and a value that lies in a range: the
 * products box_draws_range_product would pick, with two multiplications instead of four.
 *
 * @param a The single value
 * @param b_lo Minimum of b
 * @param b_hi Maximum of b
 * @param lo Receives the minimum of the product
 * @param hi Receives the maximum of the product
 */
static void box_draws_range_scale(
    float a,
    float b_lo,
    float b_hi,
    float *lo,
    float *hi)
{
    float p0 = a * b_lo, p1 = a * b_hi;
    *lo = glm_min(p0, p1);
    *hi = glm_max(p0, p1);
}

void box_draws_range_mul(
    const box_draw_transition_range_t *a,
    const box_draw_transition_range_t *b,
    box_draw_transition_range_t *out)
{
    box_draw_transition_range_t result;
    bool a_single = box_draws_range_single(a), b_single = box_draws_range_single(b);
    int32_t col, row, k;
    for (col = 0; col < 4; col ++) {
        for (row = 0; row < 4; row ++) {
            float lo = 0.0f, hi = 0.0f;
            for (k = 0; k < 4; k ++) {
                float p_lo, p_hi;
                if (a_single && b_single) {
                    p_lo = p_hi = a->lo[k][row] * b->lo[col][k];
                } else if (a_single) {
                    box_draws_range_scale(a->lo[k][row], b->lo[col][k], b->hi[col][k],
                        &p_lo, &p_hi);
                } else if (b_single) {
                    box_draws_range_scale(b->lo[col][k], a->lo[k][row], a->hi[k][row],
                        &p_lo, &p_hi);
                } else {
                    box_draws_range_product(a->lo[k][row], a->hi[k][row],
                        b->lo[col][k], b->hi[col][k], &p_lo, &p_hi);
                }

                lo += p_lo;
                hi += p_hi;
            }

            result.lo[col][row] = lo;
            result.hi[col][row] = hi;
        }
    }

    *out = result;
}

/** Get the range of the sine or the cosine of an angle that lies in a range.
 *
 * @param lo Minimum of the angle
 * @param hi Maximum of the angle
 * @param cosine Whether the cosine, else the sine
 * @param out_lo Receives the minimum
 * @param out_hi Receives the maximum
 */
static void box_draws_range_wave(
    float lo,
    float hi,
    bool cosine,
    float *out_lo,
    float *out_hi)
{
    const double tau = 2.0 * GLM_PI;
    double peak = cosine ? 0.0 : GLM_PI_2;
    float a = cosine ? cosf(lo) : sinf(lo);
    float b = cosine ? cosf(hi) : sinf(hi);
    *out_lo = glm_min(a, b);
    *out_hi = glm_max(a, b);
    if (!((double)hi - (double)lo < tau)) {
        *out_lo = -1.0f;
        *out_hi = 1.0f;
        goto done;
    }

    if (peak + tau * ceil(((double)lo - peak) / tau) <= (double)hi) {
        *out_hi = 1.0f;
    }

    peak += GLM_PI;
    if (peak + tau * ceil(((double)lo - peak) / tau) <= (double)hi) {
        *out_lo = -1.0f;
    }
done:
    return;
}

void box_draws_range_local(
    vec3 lo[3],
    vec3 hi[3],
    box_draw_transition_range_t *out)
{
    vec3 sin_lo, sin_hi, cos_lo, cos_hi;
    box_draw_transition_range_t ry, rx, rz;
    int32_t i, col, row;
    for (i = 0; i < 3; i ++) {
        if (lo[1][i] == hi[1][i]) {
            sin_lo[i] = sin_hi[i] = sinf(lo[1][i]);
            cos_lo[i] = cos_hi[i] = cosf(lo[1][i]);
        } else {
            box_draws_range_wave(lo[1][i], hi[1][i], false, &sin_lo[i], &sin_hi[i]);
            box_draws_range_wave(lo[1][i], hi[1][i], true, &cos_lo[i], &cos_hi[i]);
        }
    }

    box_draws_range_point(GLM_MAT4_IDENTITY, &ry);
    box_draws_range_point(GLM_MAT4_IDENTITY, &rx);
    box_draws_range_point(GLM_MAT4_IDENTITY, &rz);
    ry.lo[0][0] = cos_lo[1]; ry.hi[0][0] = cos_hi[1];
    ry.lo[0][2] = -sin_hi[1]; ry.hi[0][2] = -sin_lo[1];
    ry.lo[2][0] = sin_lo[1]; ry.hi[2][0] = sin_hi[1];
    ry.lo[2][2] = cos_lo[1]; ry.hi[2][2] = cos_hi[1];
    rx.lo[1][1] = cos_lo[0]; rx.hi[1][1] = cos_hi[0];
    rx.lo[1][2] = sin_lo[0]; rx.hi[1][2] = sin_hi[0];
    rx.lo[2][1] = -sin_hi[0]; rx.hi[2][1] = -sin_lo[0];
    rx.lo[2][2] = cos_lo[0]; rx.hi[2][2] = cos_hi[0];
    rz.lo[0][0] = cos_lo[2]; rz.hi[0][0] = cos_hi[2];
    rz.lo[0][1] = sin_lo[2]; rz.hi[0][1] = sin_hi[2];
    rz.lo[1][0] = -sin_hi[2]; rz.hi[1][0] = -sin_lo[2];
    rz.lo[1][1] = cos_lo[2]; rz.hi[1][1] = cos_hi[2];
    box_draws_range_mul(&ry, &rx, out);
    box_draws_range_mul(out, &rz, out);
    for (col = 0; col < 3; col ++) {
        for (row = 0; row < 3; row ++) {
            float r_lo = glm_clamp(out->lo[col][row], -1.0f, 1.0f);
            float r_hi = glm_clamp(out->hi[col][row], -1.0f, 1.0f);
            box_draws_range_product(r_lo, r_hi, lo[2][col], hi[2][col],
                &out->lo[col][row], &out->hi[col][row]);
        }

        out->lo[3][col] = lo[0][col];
        out->hi[3][col] = hi[0][col];
    }
}

void box_draws_target_local(
    const ecs_world_t *world,
    ecs_entity_t e,
    mat4 out)
{
    const Position *position = ecs_get(world, e, Position);
    if (position) {
        box_transform_local(position, ecs_get(world, e, Rotation), ecs_get(world, e, Scale),
            out);
    } else {
        glm_mat4_identity(out);
    }
}

void box_draws_parent_world(
    const ecs_world_t *world,
    ecs_entity_t e,
    mat4 out)
{
    const WorldTransform *parent = NULL;
    ecs_entity_t cur = ecs_get_parent(world, e);
    while (cur && !parent) {
        parent = ecs_get(world, cur, WorldTransform);
        cur = ecs_get_parent(world, cur);
    }

    if (parent) {
        ecs_os_memcpy(out, parent->m, ECS_SIZEOF(mat4));
    } else {
        glm_mat4_identity(out);
    }
}

void box_draws_range_pose(
    mat4 rel,
    const box_draw_transition_range_t *up,
    vec3 lo[3],
    vec3 hi[3],
    box_draw_transition_range_t *out)
{
    box_draw_transition_range_t chain, local;
    box_draws_range_point(rel, &chain);
    if (up) {
        box_draws_range_mul(up, &chain, &chain);
    }

    box_draws_range_local(lo, hi, &local);
    box_draws_range_mul(&chain, &local, out);
}

/** Check whether a batch is an alive shadow caster.
 *
 * @param d The state of the batches
 * @param batch Index of the batch, negative for none
 * @return Whether it is
 */
static bool box_draws_caster_at(
    box_render_draws_t *d,
    int32_t batch)
{
    box_draw_batch_t *b = batch >= 0 && batch < ecs_vec_count(&d->batches)
        ? box_draws_batch(d, batch) : NULL;

    return b && b->alive && box_draws_caster(b);
}

bool box_draws_entity_casts(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    const DrawSlot *ds = ecs_get(world, e, DrawSlot);
    const TextSlots *ts = ecs_get(world, e, TextSlots);
    bool result = ds && box_draws_caster_at(d, ds->batch);
    int32_t i;
    if (!result && ds && ds->batch >= 0 && ecs_has(world, e, BoxShadow)) {
        const BoxShadowSlot *bs = ecs_get(world, e, BoxShadowSlot);
        result = bs && box_draws_caster_at(d, bs->batch);
    }

    for (i = 0; ts && !result && i < ecs_vec_count(&ts->slots); i ++) {
        result = box_draws_caster_at(d,
            ecs_vec_get_t(&ts->slots, box_draw_glyph_slot_t, i)->batch);
    }

    return result;
}

void box_draws_range_region(
    const ecs_world_t *world,
    box_draw_batch_t *b,
    int32_t slot,
    const box_draw_transition_range_t *range,
    mat4 rel,
    box_render_shadow_region_t *out)
{
    box_render_transform_t lo, hi;
    int32_t row, col, k;
    for (row = 0; row < 3; row ++) {
        for (col = 0; col < 4; col ++) {
            float p_lo = 0.0f, p_hi = 0.0f;
            for (k = 0; k < 4; k ++) {
                float t_lo, t_hi;
                box_draws_range_scale(rel[col][k], range->lo[k][row], range->hi[k][row],
                    &t_lo, &t_hi);
                p_lo += t_lo;
                p_hi += t_hi;
            }

            lo.rows[row][col] = p_lo;
            hi.rows[row][col] = p_hi;
        }
    }

    if (!box_draws_shadow_bounds(world, b, &lo, &hi, box_draws_slot_surface(b, slot), out)) {
        out->scene = b->key.scene;
        glm_vec3_fill(out->min, -FLT_MAX);
        glm_vec3_fill(out->max, FLT_MAX);
    }
}
