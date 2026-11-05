/**
 * @file audio/core/pattern_gate.c
 * @brief The pattern gate kind of effect: a level pattern of up to 16 steps locked to the beat
 * clock of the core, with swing, a gate length, linear edges and a floor, the trance gate.
 *
 * The step of a frame comes from the beat of that frame alone (box_audio_core_beat), so the
 * pattern stays on the grid of the Transport after a seek, a loop wrap or a tempo change and
 * does not depend on where a block ends.
 *
 * The openness of the gain moves toward the level of the step by at most 1 / attack frames
 * per frame up and 1 / release frames down, the same gain on both channels. See
 * box_audio_pattern_gate_params_t.
 */

#include "internal.h"

/** Octaves of level per dB, log2(10) / 20 */
#define BOX_AUDIO_PATTERN_GATE_OCTAVES 0.16609640474436813

/** dB per octave of level, 20 log10(2) */
#define BOX_AUDIO_PATTERN_GATE_DB 6.0205999132796239

/** Index of the length in the params, after the steps */
#define BOX_AUDIO_PATTERN_GATE_LENGTH BOX_AUDIO_PATTERN_GATE_STEPS

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_pattern_gate_clamp(
    float v,
    float lo,
    float hi)
{
    if (!(v >= lo)) {
        v = lo;
    } else if (v > hi) {
        v = hi;
    }

    return v;
}

/** Round a value down to a whole number.
 *
 * @param x The value, within int64
 * @return The largest whole number at most x
 */
static inline double box_audio_pattern_gate_floor(
    double x)
{
    double t = (double)(int64_t)x;

    return t > x ? t - 1.0 : t;
}

/** Clamp the params of an effect into the target of its pattern gate and set its edges.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_pattern_gate_target(
    const box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_pattern_gate_t *g = &fx->data.pattern_gate;
    box_audio_pattern_gate_params_t *t = &g->target;
    const float *p = fx->params + BOX_AUDIO_PATTERN_GATE_LENGTH;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_PATTERN_GATE_STEPS; i ++) {
        t->steps[i] = box_audio_pattern_gate_clamp(fx->params[i], 0, 1);
    }

    t->length = box_audio_pattern_gate_clamp(p[0], 1, BOX_AUDIO_PATTERN_GATE_STEPS);
    t->rate = box_audio_pattern_gate_clamp(p[1], BOX_AUDIO_PATTERN_GATE_RATE_MIN,
        BOX_AUDIO_PATTERN_GATE_RATE_MAX);
    t->swing = box_audio_pattern_gate_clamp(p[2], 0, BOX_AUDIO_PATTERN_GATE_SWING_MAX);
    t->attack = box_audio_pattern_gate_clamp(p[3], BOX_AUDIO_PATTERN_GATE_EDGE_MIN,
        BOX_AUDIO_PATTERN_GATE_EDGE_MAX);
    t->release = box_audio_pattern_gate_clamp(p[4], BOX_AUDIO_PATTERN_GATE_EDGE_MIN,
        BOX_AUDIO_PATTERN_GATE_EDGE_MAX);
    t->gate = box_audio_pattern_gate_clamp(p[5], BOX_AUDIO_PATTERN_GATE_GATE_MIN, 1);
    t->floor = box_audio_pattern_gate_clamp(p[6], BOX_AUDIO_PATTERN_GATE_FLOOR_MIN, 0);
    t->mix = box_audio_pattern_gate_clamp(p[7], 0, 1);
    g->length = (int32_t)(t->length + 0.5f);
    g->rate = (double)t->rate;
    g->swing = (double)t->swing;
    g->up = (float)(1.0 / ((double)t->attack * (double)core->sample_rate));
    g->down = (float)(1.0 / ((double)t->release * (double)core->sample_rate));
}

/** Linear gain of the floor of the target.
 *
 * @param g The pattern gate
 * @return The gain
 */
static float box_audio_pattern_gate_floor_gain(
    const box_audio_pattern_gate_t *g)
{
    return (float)box_audio_synth_exp2((double)g->target.floor * BOX_AUDIO_PATTERN_GATE_OCTAVES);
}

/** Get the openness a pattern gate moves to at a beat: the level of the step of the beat
 * while the step is open, else 0. In each pair of steps from beat 0 the first lasts 1 + swing
 * steps and the second the rest.
 *
 * @param g The pattern gate
 * @param beat The beat
 * @return The openness, 0..1
 */
static inline float box_audio_pattern_gate_level(
    const box_audio_pattern_gate_t *g,
    double beat)
{
    double u = beat / g->rate, pair = box_audio_pattern_gate_floor(u * 0.5);
    double q = u - 2.0 * pair, first = 1.0 + g->swing, phase;
    int64_t step = (int64_t)pair * 2, n;
    if (q < first) {
        phase = q / first;
    } else {
        step ++;
        phase = (q - first) / (2.0 - first);
    }

    n = step % g->length;
    n = n < 0 ? n + g->length : n;

    return phase < (double)g->target.gate ? g->target.steps[n] : 0.0f;
}

/** Run the gain of a pattern gate over frames from the clock of the core on, and apply it
 * to a block when there is one.
 *
 * @param core The core
 * @param fx The effect
 * @param l Left samples, NULL to move the gain on only
 * @param r Right samples
 * @param frames Frames
 * @return The smallest gain
 */
static float box_audio_pattern_gate_run(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames)
{
    box_audio_pattern_gate_t *g = &fx->data.pattern_gate;
    uint64_t frame = core->clock;
    float least = 1;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        float to = box_audio_pattern_gate_level(g,
            box_audio_core_beat(core, frame + (uint64_t)i));
        float low = box_audio_ramp_next(&g->floor);
        float mix = box_audio_ramp_next(&g->mix), gain;
        if (g->open < to) {
            g->open += g->up;
            g->open = g->open < to ? g->open : to;
        } else if (g->open > to) {
            g->open -= g->down;
            g->open = g->open > to ? g->open : to;
        }

        gain = low + (1 - low) * g->open;
        gain = 1 + (gain - 1) * mix;
        least = gain < least ? gain : least;
        if (l) {
            l[i] *= gain;
            r[i] *= gain;
        }
    }

    return least;
}

void box_audio_pattern_gate_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_pattern_gate_t *g = &fx->data.pattern_gate;
    (void)fade_in;
    box_audio_pattern_gate_target(core, fx);
    g->open = box_audio_pattern_gate_level(g, box_audio_core_beat(core, core->clock));
    box_audio_ramp_set(&g->floor, box_audio_pattern_gate_floor_gain(g));
    box_audio_ramp_set(&g->mix, g->target.mix);
}

void box_audio_pattern_gate_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_pattern_gate_t *g = &fx->data.pattern_gate;
    int32_t ramp = (int32_t)(BOX_AUDIO_PATTERN_GATE_RAMP * core->sample_rate);
    box_audio_pattern_gate_target(core, fx);
    box_audio_ramp_to(&g->floor, box_audio_pattern_gate_floor_gain(g), ramp);
    box_audio_ramp_to(&g->mix, g->target.mix, ramp);
}

bool box_audio_pattern_gate_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    float least = box_audio_pattern_gate_run(core, fx, l, r, frames);
    if (least < 1 && least > 0) {
        box_audio_effect_reduction(core, fx, (float)(-BOX_AUDIO_PATTERN_GATE_DB *
            box_audio_synth_log2((double)least)));
    }

    return silent;
}

void box_audio_pattern_gate_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    box_audio_pattern_gate_run(core, fx, NULL, NULL, frames);
}
