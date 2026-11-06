/**
 * @file audio/core/modulator.c
 * @brief The beat clock of the core and the modulators that move the parameters of the buses
 * and the effect slots on their own: LFOs, step sequences and envelope followers.
 *
 * The beat clock is a line of (frame, beat, tempo) that each BEAT command restarts at its
 * frame, so a synced source reads its phase at any frame: phase + fract(beat / sync). The
 * modulators update every BOX_AUDIO_MOD_CONTROL frames, at hard edges and after commands.
 *
 * An update computes the modulators, then the maps of the macros, for the last frame before
 * the next update and ramps a bus to it, or gives an effect new params that its kind smooths.
 * The render splits its blocks at the updates, so the output does not depend on render sizes.
 */

#include "internal.h"

/** 1 / ln(2) */
#define BOX_AUDIO_MOD_LOG2E 1.4426950408889634

/** dB of a factor of 2 in level, 20 log10(2) */
#define BOX_AUDIO_MOD_DB_PER_OCTAVE 6.020599913279624

/** Largest octaves the modulators move a parameter by */
#define BOX_AUDIO_MOD_OCTAVES_MAX 20.0f

/** Largest depth in the units of a parameter */
#define BOX_AUDIO_MOD_DEPTH_MAX 1e6f

/** Frames an edge is looked for at most ahead, so that a slow source does not overflow */
#define BOX_AUDIO_MOD_AHEAD ((uint64_t)1 << 40)

/** Round a value down to a whole number.
 *
 * @param x The value, within int64
 * @return The largest whole number at most x
 */
static double box_audio_mod_floor(
    double x)
{
    double t = (double)(int64_t)x;

    return t > x ? t - 1.0 : t;
}

/** Clamp a value of a command; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_mod_clamp(
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

double box_audio_core_beat(
    const box_audio_core_t *core,
    uint64_t frame)
{
    return core->beat_at + (double)(int64_t)(frame - core->beat_frame) * core->beat_tempo /
        (double)core->sample_rate;
}

double box_audio_core_tempo(
    const box_audio_core_t *core)
{
    return core->beat_tempo;
}

int32_t box_audio_core_modulators(
    const box_audio_core_t *core)
{
    uint64_t on = core ? core->mods.on : 0;
    int32_t result = 0;
    while (on) {
        on &= on - 1;
        result ++;
    }

    return result;
}

int32_t box_audio_core_macro_maps(
    const box_audio_core_t *core)
{
    return core ? core->mods.maps : 0;
}

float box_audio_macro_map_value(
    float value,
    float min,
    float max,
    float curve,
    uint32_t scale)
{
    double x = value >= 0 ? (value <= 1 ? (double)value : 1.0) : 0.0, k, result;
    float c = curve != curve ? 0.0f : (curve < -1 ? -1.0f : (curve > 1 ? 1.0f : curve));
    k = (double)c * (double)BOX_AUDIO_MACRO_CURVE;
    if (k > 1e-6 || k < -1e-6) {
        x = (box_audio_synth_exp2(k * x) - 1.0) / (box_audio_synth_exp2(k) - 1.0);
    }

    if (scale == BOX_AUDIO_MOD_OCTAVES && min > 0 && max > 0) {
        result = (double)min * box_audio_synth_exp2(x *
            box_audio_synth_log2((double)max / (double)min));
    } else {
        result = (double)min + ((double)max - (double)min) * x;
    }

    return (float)result;
}

/** Get the index the modulators and the maps use for a parameter of a bus.
 *
 * @param param A box_audio_param_t of a bus: VOLUME, PAN, WIDTH, SEND or SEND_1..3
 * @return The box_audio_mod_bus_param_t
 */
static uint32_t box_audio_mod_bus_param(
    uint32_t param)
{
    return param == BOX_AUDIO_PARAM_VOLUME ? BOX_AUDIO_MOD_BUS_VOLUME :
        (param == BOX_AUDIO_PARAM_PAN ? BOX_AUDIO_MOD_BUS_PAN :
        (param == BOX_AUDIO_PARAM_WIDTH ? BOX_AUDIO_MOD_BUS_WIDTH :
        (param == BOX_AUDIO_PARAM_SEND ? BOX_AUDIO_MOD_BUS_SEND :
        BOX_AUDIO_MOD_BUS_SEND + 1 + (param - BOX_AUDIO_PARAM_SEND_1))));
}

/** Get the value a bus has for a parameter from its commands.
 *
 * @param b The bus
 * @param bus_param A box_audio_mod_bus_param_t
 * @return The value
 */
static float box_audio_mod_bus_value(
    const box_audio_bus_t *b,
    uint32_t bus_param)
{
    float result = b->volume;
    if (bus_param == BOX_AUDIO_MOD_BUS_PAN) {
        result = b->pan;
    } else if (bus_param == BOX_AUDIO_MOD_BUS_WIDTH) {
        result = b->width;
    } else if (bus_param >= BOX_AUDIO_MOD_BUS_SEND && bus_param < BOX_AUDIO_MOD_BUS_PARAMS) {
        result = b->send_level[bus_param - BOX_AUDIO_MOD_BUS_SEND];
    }

    return result;
}

void box_audio_mods_init(
    box_audio_core_t *core)
{
    core->beat_tempo = BOX_AUDIO_SYNTH_TEMPO;
    core->beat_rate = BOX_AUDIO_SYNTH_TEMPO / (double)core->sample_rate;
    core->mods.edge = UINT64_MAX;
}

void box_audio_beat_command(
    box_audio_core_t *core,
    const box_audio_cmd_beat_t *cmd)
{
    float tempo = cmd->tempo > 0 ? box_audio_mod_clamp(cmd->tempo, BOX_AUDIO_SYNTH_TEMPO_MIN,
        BOX_AUDIO_SYNTH_TEMPO_MAX) : BOX_AUDIO_SYNTH_TEMPO;
    uint64_t when = ((uint64_t)cmd->when_hi << 32) | cmd->when_lo;
    core->beat_frame = core->clock;
    core->beat_tempo = (double)tempo;
    core->beat_rate = (double)tempo / (double)core->sample_rate;
    core->beat_at = (double)cmd->whole + (double)box_audio_mod_clamp(cmd->fraction, 0, 1);
    if (when && when < core->clock) {
        core->beat_at += (double)(core->clock - when) * core->beat_rate;
    }

    core->mods.dirty = core->mods.dirty || core->mods.on;
}

/** Get the fade of a modulator at a frame.
 *
 * @param m The modulator
 * @param frame The frame, at or after its fade_at
 * @return The fade, 0..1
 */
static float box_audio_mod_gain(
    const box_audio_mod_t *m,
    uint64_t frame)
{
    float g = m->gain0 + m->fade_dir * (float)(int64_t)(frame - m->fade_at);

    return g < 0 ? 0.0f : (g > 1 ? 1.0f : g);
}

/** Get the position of the source of a modulator at a frame: cycles of an LFO, steps of a step
 * modulator. A synced source reads the beat clock, a free one counts from its start.
 *
 * @param core The core
 * @param m The modulator
 * @param frame The frame
 * @return The position
 */
static double box_audio_mod_cycles(
    const box_audio_core_t *core,
    const box_audio_mod_t *m,
    uint64_t frame)
{
    const box_audio_cmd_modulator_t *c = &m->cmd;
    double result;
    if (c->sync > 0) {
        result = box_audio_core_beat(core, frame) / (double)c->sync + (double)c->phase *
            (c->source == BOX_AUDIO_MOD_STEPS ? (double)c->step_count : 1.0);
    } else {
        result = m->cycles0 + (double)(int64_t)(frame - m->cycles_at) * (double)c->rate /
            (double)core->sample_rate;
    }

    return result;
}

/** Get the change of the position of a source per frame.
 *
 * @param core The core
 * @param m The modulator
 * @return Cycles or steps per frame
 */
static double box_audio_mod_speed(
    const box_audio_core_t *core,
    const box_audio_mod_t *m)
{
    return m->cmd.sync > 0 ? core->beat_rate / (double)m->cmd.sync :
        (double)m->cmd.rate / (double)core->sample_rate;
}

/** A random value of a cycle of a sample and hold LFO, the same for the same cycle and slot.
 *
 * @param cycle The cycle
 * @param slot Slot of the modulator
 * @return The value, -1..1
 */
static float box_audio_mod_random(
    double cycle,
    uint32_t slot)
{
    uint64_t z = (uint64_t)(int64_t)cycle * 0x9E3779B97F4A7C15ull +
        ((uint64_t)slot + 1) * 0xD1B54A32D192ED03ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);

    return (float)((double)(z >> 40) / 8388608.0 - 1.0);
}

/** Get the value of the source of a modulator at a frame; a follower gives the level of its
 * bus as of now.
 *
 * @param core The core
 * @param m The modulator
 * @param frame The frame
 * @return The value, -1..1
 */
static float box_audio_mod_value(
    const box_audio_core_t *core,
    const box_audio_mod_t *m,
    uint64_t frame)
{
    const box_audio_cmd_modulator_t *c = &m->cmd;
    double pos, k, x;
    float v = 0, cur, prev, db;
    int64_t idx, n;
    if (c->source == BOX_AUDIO_MOD_FOLLOWER) {
        if (m->env > 1e-9f) {
            db = (float)(box_audio_synth_log2((double)m->env) * BOX_AUDIO_MOD_DB_PER_OCTAVE);
            v = box_audio_mod_clamp((db - BOX_AUDIO_MOD_FLOOR_DB) / -BOX_AUDIO_MOD_FLOOR_DB,
                0, 1);
        }

        goto done;
    }

    pos = box_audio_mod_cycles(core, m, frame);
    k = box_audio_mod_floor(pos);
    x = pos - k;
    if (c->source == BOX_AUDIO_MOD_STEPS) {
        n = (int64_t)c->step_count;
        idx = (int64_t)k % n;
        idx = idx < 0 ? idx + n : idx;
        cur = c->steps[idx];
        prev = c->steps[(idx + n - 1) % n];
        if (c->smooth > 0 && x < (double)c->smooth) {
            cur = prev + (cur - prev) * (float)(x / (double)c->smooth);
        }

        v = cur;
        goto done;
    }

    switch (c->wave) {
    case BOX_AUDIO_MOD_TRIANGLE:
        v = (float)(x < 0.25 ? 4.0 * x : (x < 0.75 ? 2.0 - 4.0 * x : 4.0 * x - 4.0));
        break;
    case BOX_AUDIO_MOD_SQUARE:
        v = x < 0.5 ? 1.0f : -1.0f;
        break;
    case BOX_AUDIO_MOD_SAW_UP:
        v = (float)(2.0 * x - 1.0);
        break;
    case BOX_AUDIO_MOD_SAW_DOWN:
        v = (float)(1.0 - 2.0 * x);
        break;
    case BOX_AUDIO_MOD_RANDOM:
        v = box_audio_mod_random(k, (uint32_t)(m - core->mods.mod));
        break;
    default:
        v = box_audio_core_sin(x);
        break;
    }

    if (c->unipolar) {
        v = 0.5f * (v + 1.0f);
    }

done:
    return v;
}

/** Get the next frame after a frame at which the source of a modulator jumps: a step, or an
 * edge of a square, a saw or a sample and hold.
 *
 * @param core The core
 * @param m The modulator
 * @param from The frame
 * @return The frame, UINT64_MAX for none
 */
static uint64_t box_audio_mod_edge(
    const box_audio_core_t *core,
    const box_audio_mod_t *m,
    uint64_t from)
{
    const box_audio_cmd_modulator_t *c = &m->cmd;
    double unit = 1.0, speed = box_audio_mod_speed(core, m), pos, target, df;
    uint64_t result = UINT64_MAX, f;
    int32_t i;
    bool hard = c->source == BOX_AUDIO_MOD_STEPS || (c->source == BOX_AUDIO_MOD_LFO &&
        (c->wave == BOX_AUDIO_MOD_SQUARE || c->wave == BOX_AUDIO_MOD_SAW_UP ||
        c->wave == BOX_AUDIO_MOD_SAW_DOWN || c->wave == BOX_AUDIO_MOD_RANDOM));
    if (!hard || !(speed > 0)) {
        goto done;
    }

    unit = c->source == BOX_AUDIO_MOD_LFO && c->wave == BOX_AUDIO_MOD_SQUARE ? 0.5 : 1.0;
    pos = box_audio_mod_cycles(core, m, from);
    target = (box_audio_mod_floor(pos / unit) + 1.0) * unit;
    df = (target - pos) / speed;
    if (!(df < (double)BOX_AUDIO_MOD_AHEAD)) {
        goto done;
    }

    f = from + (uint64_t)df;
    f = f > from ? f : from + 1;
    for (i = 0; i < 4 && box_audio_mod_cycles(core, m, f) < target; i ++) {
        f ++;
    }

    for (i = 0; i < 4 && f > from + 1 && box_audio_mod_cycles(core, m, f - 1) >= target; i ++) {
        f --;
    }

    result = f;
done:
    return result;
}

/** Set the coefficients of the envelope of a follower.
 *
 * @param core The core
 * @param m The modulator
 */
static void box_audio_mod_follow_coefs(
    const box_audio_core_t *core,
    box_audio_mod_t *m)
{
    double rate = (double)core->sample_rate;
    m->env_rise = (float)(1.0 - box_audio_synth_exp2(-BOX_AUDIO_MOD_LOG2E /
        ((double)m->cmd.attack * rate)));
    m->env_fall = (float)(1.0 - box_audio_synth_exp2(-BOX_AUDIO_MOD_LOG2E /
        ((double)m->cmd.release * rate)));
}

/** Recompute the buses the followers measure.
 *
 * @param core The core
 */
static void box_audio_mods_keys(
    box_audio_core_t *core)
{
    box_audio_mods_t *ms = &core->mods;
    int32_t i;
    ms->keys = 0;
    for (i = 0; i < BOX_AUDIO_MOD_MAX; i ++) {
        const box_audio_mod_t *m = &ms->mod[i];
        if (m->on && m->cmd.source == BOX_AUDIO_MOD_FOLLOWER &&
            m->cmd.key < BOX_AUDIO_BUS_MAX)
        {
            ms->keys |= (uint64_t)1 << m->cmd.key;
        }
    }
}

/** Start a modulator on its target: it fades in from 0 at the clock, a free source from its
 * phase.
 *
 * @param core The core
 * @param slot Slot of the modulator
 * @param cmd Its settings, clamped
 */
static void box_audio_mod_start(
    box_audio_core_t *core,
    uint32_t slot,
    const box_audio_cmd_modulator_t *cmd)
{
    box_audio_mod_t *m = &core->mods.mod[slot];
    float fade = BOX_AUDIO_MOD_FADE * core->sample_rate;
    m->cmd = *cmd;
    m->on = true;
    m->pending = false;
    m->gain0 = 0;
    m->fade_at = core->clock;
    m->fade_dir = 1.0f / (fade > 1 ? fade : 1.0f);
    m->cycles0 = (double)cmd->phase *
        (cmd->source == BOX_AUDIO_MOD_STEPS ? (double)cmd->step_count : 1.0);
    m->cycles_at = core->clock;
    m->env = 0;
    if (cmd->target == BOX_AUDIO_MOD_TARGET_BUS) {
        m->bus_param = box_audio_mod_bus_param(cmd->param);
    }

    box_audio_mod_follow_coefs(core, m);
    core->mods.on |= (uint64_t)1 << slot;
}

/** Fade a modulator out from where its fade stands.
 *
 * @param core The core
 * @param m The modulator
 */
static void box_audio_mod_release(
    box_audio_core_t *core,
    box_audio_mod_t *m)
{
    float fade = BOX_AUDIO_MOD_FADE * core->sample_rate;
    m->gain0 = box_audio_mod_gain(m, core->clock);
    m->fade_at = core->clock;
    m->fade_dir = -1.0f / (fade > 1 ? fade : 1.0f);
}

/** Clamp the settings of a modulator command.
 *
 * @param cmd The command
 * @param out Receives the clamped settings
 * @return Whether the target is valid, NONE included
 */
static bool box_audio_mod_check(
    const box_audio_cmd_modulator_t *cmd,
    box_audio_cmd_modulator_t *out)
{
    bool result = true;
    float phase;
    int32_t i;
    *out = *cmd;
    if (cmd->target == BOX_AUDIO_MOD_TARGET_BUS) {
        result = cmd->index < BOX_AUDIO_BUS_MAX && (cmd->param == BOX_AUDIO_PARAM_VOLUME ||
            cmd->param == BOX_AUDIO_PARAM_PAN || cmd->param == BOX_AUDIO_PARAM_WIDTH ||
            cmd->param == BOX_AUDIO_PARAM_SEND || (cmd->param >= BOX_AUDIO_PARAM_SEND_1 &&
            cmd->param <= BOX_AUDIO_PARAM_SEND_3));
        out->scale = BOX_AUDIO_MOD_LINEAR;
    } else if (cmd->target == BOX_AUDIO_MOD_TARGET_EFFECT) {
        result = cmd->index < BOX_AUDIO_EFFECT_MAX && cmd->param < BOX_AUDIO_EFFECT_PARAMS &&
            cmd->kind > BOX_AUDIO_EFFECT_NONE && cmd->kind < BOX_AUDIO_EFFECT_KINDS;
        out->scale = cmd->scale == BOX_AUDIO_MOD_OCTAVES ? BOX_AUDIO_MOD_OCTAVES :
            BOX_AUDIO_MOD_LINEAR;
    } else if (cmd->target == BOX_AUDIO_MOD_TARGET_MACRO) {
        result = cmd->index < BOX_AUDIO_MACRO_MAX;
        out->param = 0;
        out->kind = 0;
        out->scale = BOX_AUDIO_MOD_LINEAR;
    } else {
        result = cmd->target == BOX_AUDIO_MOD_TARGET_NONE;
    }

    out->source = cmd->source <= BOX_AUDIO_MOD_FOLLOWER ? cmd->source : BOX_AUDIO_MOD_LFO;
    out->wave = cmd->wave <= BOX_AUDIO_MOD_RANDOM ? cmd->wave : BOX_AUDIO_MOD_SINE;
    out->unipolar = cmd->unipolar ? 1u : 0u;
    out->key = cmd->key < BOX_AUDIO_BUS_MAX ? cmd->key : BOX_AUDIO_BUS_NONE;
    out->step_count = cmd->step_count < 1 ? 1u : (cmd->step_count > BOX_AUDIO_MOD_STEP_MAX ?
        BOX_AUDIO_MOD_STEP_MAX : cmd->step_count);
    out->depth = cmd->depth == cmd->depth ? box_audio_mod_clamp(cmd->depth,
        -BOX_AUDIO_MOD_DEPTH_MAX, BOX_AUDIO_MOD_DEPTH_MAX) : 0.0f;
    out->rate = box_audio_mod_clamp(cmd->rate, 0, BOX_AUDIO_SYNTH_LFO_RATE_MAX);
    out->sync = box_audio_mod_clamp(cmd->sync, 0, BOX_AUDIO_SYNTH_LFO_SYNC_MAX);
    phase = box_audio_mod_clamp(cmd->phase, -1e6f, 1e6f);
    phase = phase - (float)box_audio_mod_floor((double)phase);
    out->phase = phase < 1.0f ? phase : 0.0f;
    out->smooth = box_audio_mod_clamp(cmd->smooth, 0, 1);
    out->attack = box_audio_mod_clamp(cmd->attack, BOX_AUDIO_MOD_FOLLOW_MIN,
        BOX_AUDIO_MOD_FOLLOW_MAX);
    out->release = box_audio_mod_clamp(cmd->release, BOX_AUDIO_MOD_FOLLOW_MIN,
        BOX_AUDIO_MOD_FOLLOW_MAX);
    for (i = 0; i < BOX_AUDIO_MOD_STEP_MAX; i ++) {
        out->steps[i] = cmd->steps[i] == cmd->steps[i] ?
            box_audio_mod_clamp(cmd->steps[i], -1, 1) : 0.0f;
    }

    return result;
}

void box_audio_mod_command(
    box_audio_core_t *core,
    const box_audio_cmd_modulator_t *cmd)
{
    box_audio_cmd_modulator_t c;
    box_audio_mod_t *m;
    bool same;
    if (cmd->slot >= BOX_AUDIO_MOD_MAX || !box_audio_mod_check(cmd, &c)) {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_MODULATOR, 0);
        goto done;
    }

    m = &core->mods.mod[cmd->slot];
    if (!m->on) {
        if (c.target != BOX_AUDIO_MOD_TARGET_NONE) {
            box_audio_mod_start(core, cmd->slot, &c);
        }
    } else if (c.target == BOX_AUDIO_MOD_TARGET_NONE) {
        m->pending = false;
        if (m->fade_dir > 0) {
            box_audio_mod_release(core, m);
        }
    } else {
        same = c.target == m->cmd.target && c.index == m->cmd.index &&
            c.param == m->cmd.param && c.kind == m->cmd.kind && c.source == m->cmd.source;
        if (same) {
            if (c.sync <= 0) {
                m->cycles0 = box_audio_mod_cycles(core, m, core->clock);
                m->cycles_at = core->clock;
            }

            m->cmd = c;
            m->pending = false;
            box_audio_mod_follow_coefs(core, m);
            if (m->fade_dir < 0) {
                float fade = BOX_AUDIO_MOD_FADE * core->sample_rate;
                m->gain0 = box_audio_mod_gain(m, core->clock);
                m->fade_at = core->clock;
                m->fade_dir = 1.0f / (fade > 1 ? fade : 1.0f);
            }
        } else {
            m->next = c;
            m->pending = true;
            if (m->fade_dir > 0) {
                box_audio_mod_release(core, m);
            }
        }
    }

    box_audio_mods_keys(core);
    core->mods.dirty = true;
done:
    return;
}

void box_audio_macro_command(
    box_audio_core_t *core,
    const box_audio_cmd_macro_t *cmd)
{
    if (cmd->macro >= BOX_AUDIO_MACRO_MAX) {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_MACRO, 0);
        goto done;
    }

    core->mods.macro_value[cmd->macro] = box_audio_mod_clamp(cmd->value, 0, 1);
    core->mods.dirty = true;
done:
    return;
}

/** Get the fade of a map at a frame.
 *
 * @param m The map
 * @param frame The frame, at or after its fade_at
 * @return The fade, 0..1
 */
static float box_audio_map_gain(
    const box_audio_macro_map_t *m,
    uint64_t frame)
{
    float g = m->gain0 + m->fade_dir * (float)(int64_t)(frame - m->fade_at);

    return g < 0 ? 0.0f : (g > 1 ? 1.0f : g);
}

/** Get the value of the macro of a map, 0..1: the value of its commands plus what its
 * modulators added at the last update, or the one it held when it was released.
 *
 * @param ms The modulators
 * @param m The map
 * @return The value
 */
static float box_audio_map_macro(
    const box_audio_mods_t *ms,
    const box_audio_macro_map_t *m)
{
    return m->held ? m->hold : box_audio_mod_clamp(ms->macro_value[m->cmd.macro] +
        ms->macro_add[m->cmd.macro], 0, 1);
}

/** Start a map on its target: it fades in from 0 at the clock.
 *
 * @param core The core
 * @param slot Slot of the map
 * @param cmd Its settings, clamped
 */
static void box_audio_map_start(
    box_audio_core_t *core,
    uint32_t slot,
    const box_audio_cmd_macro_map_t *cmd)
{
    box_audio_macro_map_t *m = &core->mods.map[slot];
    float fade = BOX_AUDIO_MOD_FADE * core->sample_rate;
    m->cmd = *cmd;
    m->on = true;
    m->pending = false;
    m->held = false;
    m->gain0 = 0;
    m->fade_at = core->clock;
    m->fade_dir = 1.0f / (fade > 1 ? fade : 1.0f);
    m->bus_param = cmd->target == BOX_AUDIO_MOD_TARGET_BUS ?
        box_audio_mod_bus_param(cmd->param) : 0;
    core->mods.maps ++;
}

/** Fade a map out from where its fade stands, at the value its macro has now.
 *
 * @param core The core
 * @param m The map
 */
static void box_audio_map_release(
    box_audio_core_t *core,
    box_audio_macro_map_t *m)
{
    float fade = BOX_AUDIO_MOD_FADE * core->sample_rate;
    m->gain0 = box_audio_map_gain(m, core->clock);
    m->fade_at = core->clock;
    m->fade_dir = -1.0f / (fade > 1 ? fade : 1.0f);
    if (!m->held) {
        m->hold = box_audio_map_macro(&core->mods, m);
        m->held = true;
    }
}

/** Clamp the settings of a map command.
 *
 * @param cmd The command
 * @param out Receives the clamped settings
 * @return Whether the target is valid, NONE included
 */
static bool box_audio_map_check(
    const box_audio_cmd_macro_map_t *cmd,
    box_audio_cmd_macro_map_t *out)
{
    bool result = true;
    *out = *cmd;
    if (cmd->target == BOX_AUDIO_MOD_TARGET_BUS) {
        result = cmd->index < BOX_AUDIO_BUS_MAX && (cmd->param == BOX_AUDIO_PARAM_VOLUME ||
            cmd->param == BOX_AUDIO_PARAM_PAN || cmd->param == BOX_AUDIO_PARAM_WIDTH ||
            cmd->param == BOX_AUDIO_PARAM_SEND || (cmd->param >= BOX_AUDIO_PARAM_SEND_1 &&
            cmd->param <= BOX_AUDIO_PARAM_SEND_3));
        out->kind = 0;
        out->scale = BOX_AUDIO_MOD_LINEAR;
    } else if (cmd->target == BOX_AUDIO_MOD_TARGET_EFFECT) {
        result = cmd->index < BOX_AUDIO_EFFECT_MAX && cmd->param < BOX_AUDIO_EFFECT_PARAMS &&
            cmd->kind > BOX_AUDIO_EFFECT_NONE && cmd->kind < BOX_AUDIO_EFFECT_KINDS;
        out->scale = cmd->scale == BOX_AUDIO_MOD_OCTAVES ? BOX_AUDIO_MOD_OCTAVES :
            BOX_AUDIO_MOD_LINEAR;
    } else {
        result = cmd->target == BOX_AUDIO_MOD_TARGET_NONE;
    }

    result = result && (cmd->target == BOX_AUDIO_MOD_TARGET_NONE ||
        cmd->macro < BOX_AUDIO_MACRO_MAX);
    out->macro = cmd->macro < BOX_AUDIO_MACRO_MAX ? cmd->macro : 0;
    out->min = cmd->min == cmd->min ? box_audio_mod_clamp(cmd->min, -BOX_AUDIO_MOD_DEPTH_MAX,
        BOX_AUDIO_MOD_DEPTH_MAX) : 0.0f;
    out->max = cmd->max == cmd->max ? box_audio_mod_clamp(cmd->max, -BOX_AUDIO_MOD_DEPTH_MAX,
        BOX_AUDIO_MOD_DEPTH_MAX) : 0.0f;
    out->curve = cmd->curve == cmd->curve ? box_audio_mod_clamp(cmd->curve, -1, 1) : 0.0f;

    return result;
}

void box_audio_macro_map_command(
    box_audio_core_t *core,
    const box_audio_cmd_macro_map_t *cmd)
{
    box_audio_cmd_macro_map_t c;
    box_audio_macro_map_t *m;
    float fade;
    bool same;
    if (cmd->slot >= BOX_AUDIO_MACRO_MAP_MAX || !box_audio_map_check(cmd, &c)) {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_MACRO_MAP, 0);
        goto done;
    }

    m = &core->mods.map[cmd->slot];
    if (!m->on) {
        if (c.target != BOX_AUDIO_MOD_TARGET_NONE) {
            box_audio_map_start(core, cmd->slot, &c);
        }
    } else if (c.target == BOX_AUDIO_MOD_TARGET_NONE) {
        m->pending = false;
        if (m->fade_dir > 0) {
            box_audio_map_release(core, m);
        }
    } else {
        same = c.target == m->cmd.target && c.index == m->cmd.index &&
            c.param == m->cmd.param && c.kind == m->cmd.kind && c.macro == m->cmd.macro;
        if (same) {
            m->cmd = c;
            m->pending = false;
            if (m->fade_dir < 0) {
                fade = BOX_AUDIO_MOD_FADE * core->sample_rate;
                m->gain0 = box_audio_map_gain(m, core->clock);
                m->fade_at = core->clock;
                m->fade_dir = 1.0f / (fade > 1 ? fade : 1.0f);
                m->held = false;
            }
        } else {
            m->next = c;
            m->pending = true;
            if (m->fade_dir > 0) {
                box_audio_map_release(core, m);
            }
        }
    }

    core->mods.dirty = true;
done:
    return;
}

/** Retire the modulators that faded out: a pending one starts on its new target, another
 * frees its slot.
 *
 * @param core The core
 */
static void box_audio_mods_retire(
    box_audio_core_t *core)
{
    box_audio_mods_t *ms = &core->mods;
    box_audio_cmd_modulator_t next;
    uint32_t i;
    bool changed = false;
    for (i = 0; i < BOX_AUDIO_MOD_MAX; i ++) {
        box_audio_mod_t *m = &ms->mod[i];
        if (!m->on || m->fade_dir >= 0 || box_audio_mod_gain(m, core->clock) > 0) {
            continue;
        }

        m->on = false;
        ms->on &= ~((uint64_t)1 << i);
        changed = true;
        if (m->pending) {
            next = m->next;
            box_audio_mod_start(core, i, &next);
        }
    }

    if (changed) {
        box_audio_mods_keys(core);
    }

    for (i = 0; i < BOX_AUDIO_MACRO_MAP_MAX && ms->maps; i ++) {
        box_audio_macro_map_t *m = &ms->map[i];
        box_audio_cmd_macro_map_t map;
        if (!m->on || m->fade_dir >= 0 || box_audio_map_gain(m, core->clock) > 0) {
            continue;
        }

        m->on = false;
        ms->maps --;
        if (m->pending) {
            map = m->next;
            box_audio_map_start(core, i, &map);
        }
    }
}

/** Clear what the modulators and the maps add to the parameters of an effect slot, the first
 * time an update moves it.
 *
 * @param ms The modulators
 * @param slot Slot of the effect
 */
static void box_audio_mods_effect_clear(
    box_audio_mods_t *ms,
    int32_t slot)
{
    int32_t j;
    for (j = 0; j < BOX_AUDIO_EFFECT_PARAMS; j ++) {
        ms->fx_add[slot][j] = 0;
        ms->fx_octaves[slot][j] = 0;
        ms->fx_macro[slot][j] = 0;
    }
}

/** Update the modulators at the clock: sum what they add to each parameter and macro at the
 * frame before the next update, shape the macros into their maps, and ramp the buses and set
 * the effects to it. A bus or an effect nothing moves any more gets its own values again.
 *
 * @param core The core
 */
static void box_audio_mods_update(
    box_audio_core_t *core)
{
    box_audio_mods_t *ms = &core->mods;
    uint64_t t0 = core->clock, t1, e = UINT64_MAX, f, buses = 0, effects = 0, macro_buses = 0;
    uint64_t mask, bit;
    int32_t i, j, frames, ramp;
    bool edge = t0 >= ms->edge;
    box_audio_mods_retire(core);
    for (i = 0; i < BOX_AUDIO_MACRO_MAX; i ++) {
        ms->macro_add[i] = 0;
    }

    t1 = ((t0 + (edge ? BOX_AUDIO_MOD_EDGE : 0)) / BOX_AUDIO_MOD_CONTROL + 1) *
        BOX_AUDIO_MOD_CONTROL;
    for (i = 0; i < BOX_AUDIO_MOD_MAX; i ++) {
        if (ms->mod[i].on) {
            f = box_audio_mod_edge(core, &ms->mod[i], t0);
            e = f < e ? f : e;
        }
    }

    t1 = e < t1 ? e : t1;
    f = t1 - 1;
    for (i = 0; i < BOX_AUDIO_MOD_MAX; i ++) {
        const box_audio_mod_t *m = &ms->mod[i];
        const box_audio_cmd_modulator_t *c = &m->cmd;
        float a;
        if (!m->on) {
            continue;
        }

        a = box_audio_mod_gain(m, f) * c->depth * box_audio_mod_value(core, m, f);
        if (c->target == BOX_AUDIO_MOD_TARGET_MACRO) {
            ms->macro_add[c->index] += a;
        } else if (c->target == BOX_AUDIO_MOD_TARGET_BUS) {
            bit = (uint64_t)1 << c->index;
            if (!(buses & bit)) {
                for (j = 0; j < BOX_AUDIO_MOD_BUS_PARAMS; j ++) {
                    ms->bus_add[c->index][j] = 0;
                }
            }

            buses |= bit;
            ms->bus_add[c->index][m->bus_param] += a;
        } else if (core->effects[c->index].target_kind == c->kind) {
            bit = (uint64_t)1 << c->index;
            if (!(effects & bit)) {
                box_audio_mods_effect_clear(ms, (int32_t)c->index);
            }

            effects |= bit;
            if (c->scale == BOX_AUDIO_MOD_OCTAVES) {
                ms->fx_octaves[c->index][c->param] += a;
            } else {
                ms->fx_add[c->index][c->param] += a;
            }
        }
    }

    for (i = 0; i < BOX_AUDIO_MACRO_MAP_MAX && ms->maps; i ++) {
        const box_audio_macro_map_t *m = &ms->map[i];
        const box_audio_cmd_macro_map_t *c = &m->cmd;
        float g, value;
        if (!m->on) {
            continue;
        }

        g = box_audio_map_gain(m, f);
        value = box_audio_macro_map_value(box_audio_map_macro(ms, m), c->min, c->max, c->curve,
            c->scale);
        if (c->target == BOX_AUDIO_MOD_TARGET_BUS) {
            bit = (uint64_t)1 << c->index;
            if (!(buses & bit)) {
                for (j = 0; j < BOX_AUDIO_MOD_BUS_PARAMS; j ++) {
                    ms->bus_add[c->index][j] = 0;
                }
            }

            buses |= bit;
            macro_buses |= bit;
            ms->bus_add[c->index][m->bus_param] += g *
                (value - box_audio_mod_bus_value(&core->buses[c->index], m->bus_param));
        } else if (core->effects[c->index].target_kind == c->kind) {
            bit = (uint64_t)1 << c->index;
            if (!(effects & bit)) {
                box_audio_mods_effect_clear(ms, (int32_t)c->index);
            }

            effects |= bit;
            ms->fx_macro[c->index][c->param] += g *
                (value - core->effects[c->index].base[c->param]);
        }
    }

    frames = (int32_t)(t1 - t0);
    mask = buses | ms->buses;
    for (i = 0; mask >> i; i ++) {
        bit = (uint64_t)1 << i;
        ramp = (macro_buses & bit) && core->ramp_frames > frames ? core->ramp_frames : frames;
        if (mask & bit) {
            box_audio_bus_gains_to(core, &core->buses[i], (buses & bit) ? ms->bus_add[i] : NULL,
                ramp);
        }
    }

    mask = effects | ms->effects;
    for (i = 0; mask >> i; i ++) {
        box_audio_effect_t *fx = &core->effects[i];
        bit = (uint64_t)1 << i;
        if (!(mask & bit)) {
            continue;
        }

        for (j = 0; j < BOX_AUDIO_EFFECT_PARAMS; j ++) {
            float p = fx->base[j], oct;
            if (effects & bit) {
                p += ms->fx_macro[i][j];
                oct = box_audio_mod_clamp(ms->fx_octaves[i][j], -BOX_AUDIO_MOD_OCTAVES_MAX,
                    BOX_AUDIO_MOD_OCTAVES_MAX);
                if (oct != 0 && p > 0) {
                    p = (float)((double)p * box_audio_synth_exp2((double)oct));
                }

                p += ms->fx_add[i][j];
            }

            fx->params[j] = p;
        }

        box_audio_effect_changed(core, (uint32_t)i);
    }

    ms->buses = buses;
    ms->effects = effects;
    ms->macro_buses = macro_buses;
    ms->next = t1;
    ms->edge = e;
    ms->dirty = false;
}

int32_t box_audio_mods_block(
    box_audio_core_t *core,
    int32_t frames)
{
    box_audio_mods_t *ms = &core->mods;
    if (ms->dirty || core->clock >= ms->next) {
        box_audio_mods_update(core);
    }

    if ((ms->on || ms->maps || ms->buses || ms->effects) &&
        ms->next - core->clock < (uint64_t)frames)
    {
        frames = (int32_t)(ms->next - core->clock);
    }

    return frames;
}

void box_audio_mods_follow(
    box_audio_core_t *core,
    uint32_t bus,
    const float *l,
    const float *r,
    int32_t frames)
{
    box_audio_mods_t *ms = &core->mods;
    int32_t i, k;
    for (k = 0; k < BOX_AUDIO_MOD_MAX; k ++) {
        box_audio_mod_t *m = &ms->mod[k];
        float env, rise, fall;
        if (!m->on || m->cmd.source != BOX_AUDIO_MOD_FOLLOWER || m->cmd.key != bus) {
            continue;
        }

        env = m->env;
        rise = m->env_rise;
        fall = m->env_fall;
        for (i = 0; i < frames; i ++) {
            float a = l[i] < 0 ? -l[i] : l[i], b = r[i] < 0 ? -r[i] : r[i];
            float x = a > b ? a : b;
            env += (x > env ? rise : fall) * (x - env);
        }

        m->env = env > 1e-12f && env <= BOX_AUDIO_HUGE ? env : 0.0f;
    }
}

void box_audio_mods_changed(
    box_audio_core_t *core,
    int32_t effect,
    int32_t bus)
{
    box_audio_mods_t *ms = &core->mods;
    if (ms->on || ms->maps || (effect >= 0 && ((ms->effects >> effect) & 1)) ||
        (bus >= 0 && ((ms->buses >> bus) & 1)))
    {
        ms->dirty = true;
    }
}
