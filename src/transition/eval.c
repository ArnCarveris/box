/**
 * @file transition/eval.c
 * @brief Interpolation of a transition record, the per frame work of the module.
 *
 * Nothing here reads the world. A record carries the value it started from,
 * the value that was assigned, the time it started and the parameters, which
 * is everything the interpolation needs, so the game side and the renderer
 * thread run the same interpolation over their own copies of a record.
 */

#include "transition_internal.h"

bool box_transition_eval(
    const box_transition_t *record,
    double time,
    float *out)
{
    return box_transition_lerp(record->start, record->delay, record->duration,
        box_transition_rate(record->duration), (uint32_t)record->timing, record->from,
        record->to, record->count, time, out);
}

void box_transition_read(
    const box_transition_slot_t *slots,
    int32_t count,
    const void *value,
    float *out)
{
    const char *base = value;
    int32_t i;
    for (i = 0; i < count; i ++) {
        const void *ptr = base + slots[i].offset;
        if (slots[i].kind == BoxTransitionU8) {
            out[i] = (float)(*(const uint8_t*)ptr);
        } else {
            out[i] = *(const float*)ptr;
        }
    }
}

void box_transition_write(
    const box_transition_slot_t *slots,
    int32_t count,
    const float *in,
    void *value)
{
    char *base = value;
    int32_t i;
    for (i = 0; i < count; i ++) {
        void *ptr = base + slots[i].offset;
        if (slots[i].kind == BoxTransitionU8) {
            float v = in[i] < 0.0f ? 0.0f : (in[i] > 255.0f ? 255.0f : in[i]);

            *(uint8_t*)ptr = (uint8_t)(v + 0.5f);
        } else {
            *(float*)ptr = in[i];
        }
    }
}

bool box_transition_display(
    const TransitionState *state,
    ecs_entity_t component,
    double time,
    void *value)
{
    bool result = false;
    const box_transition_t *records = ecs_vec_first_t(
        &state->records, box_transition_t);
    int32_t i, count = ecs_vec_count(&state->records);
    for (i = 0; i < count; i ++) {
        if (records[i].component != component) {
            continue;
        }

        float displayed[BOX_TRANSITION_MAX];

        box_transition_eval(&records[i], time, displayed);
        box_transition_write(records[i].slots, records[i].count, displayed, value);
        result = true;
    }

    return result;
}
