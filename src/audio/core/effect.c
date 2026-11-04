/**
 * @file audio/core/effect.c
 * @brief The pool of effect instances of the core: their commands, their memory, their places
 * in the insert slots of the buses, their moves and their tails.
 *
 * A kind of effect is a row of the table below and a file of its own: it starts on the memory
 * of its slot, smooths its params, processes a block in place and tells when its tail decayed.
 *
 * This file does what every kind shares. A move to another kind, bus or index blends the
 * effect out of its bus, so the bus passes its dry signal, and into its new place. The tail of
 * an effect whose bus left the order fades out into the master.
 */

#include "internal.h"

/** The kinds of effects by box_audio_effect_kind_t */
static const box_audio_effect_def_t box_audio_effect_kinds[BOX_AUDIO_EFFECT_KINDS] = {
    [BOX_AUDIO_EFFECT_NONE] = { 0 },
    [BOX_AUDIO_EFFECT_REVERB] = {
        .tail = true,
        .floats = box_audio_reverb_floats,
        .init = box_audio_reverb_init,
        .start = box_audio_reverb_start,
        .command = box_audio_reverb_command,
        .render = box_audio_reverb_render,
        .rest = box_audio_reverb_rest
    }
};

/** Get the row of a kind.
 *
 * @param kind A box_audio_effect_kind_t
 * @return The row, the one of NONE for an unknown kind
 */
static const box_audio_effect_def_t* box_audio_effect_kind(
    uint32_t kind)
{
    return &box_audio_effect_kinds[kind < BOX_AUDIO_EFFECT_KINDS ? kind : 0];
}

int32_t box_audio_effect_floats(
    uint32_t kind,
    float sample_rate)
{
    const box_audio_effect_def_t *k = box_audio_effect_kind(kind);
    float rate = sample_rate > 0 && sample_rate < 1e7f ? sample_rate : 48000.0f;

    return k->floats ? k->floats(rate) : 0;
}

/** Check whether the memory of an effect holds a kind.
 *
 * @param core The core
 * @param fx The effect
 * @param kind A box_audio_effect_kind_t
 * @return Whether the kind needs no memory or the slot has enough
 */
static bool box_audio_effect_ready(
    const box_audio_core_t *core,
    const box_audio_effect_t *fx,
    uint32_t kind)
{
    int32_t need = box_audio_effect_floats(kind, core->sample_rate);

    return !need || (fx->memory && fx->floats >= need);
}

/** Put an effect in the mask of the bus it renders on, or of the tails, or in none.
 *
 * @param core The core
 * @param slot Slot of the effect
 */
static void box_audio_effect_place(
    box_audio_core_t *core,
    uint32_t slot)
{
    const box_audio_effect_t *fx = &core->effects[slot];
    uint64_t bit = (uint64_t)1 << slot;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        core->bus_effects[i] &= ~bit;
    }

    core->effect_tails &= ~bit;
    if (fx->attached && fx->bus == BOX_AUDIO_BUS_NONE) {
        core->effect_tails |= bit;
    } else if (fx->attached && fx->bus < BOX_AUDIO_BUS_MAX) {
        core->bus_effects[fx->bus] |= bit;
    }
}

/** Apply the target of an effect: its kind, bus, index and key, the kind started on clear
 * memory. An effect whose kind lacks its memory waits for it.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @param fade_in Whether the kind fades its input in, after a move
 */
static void box_audio_effect_apply(
    box_audio_core_t *core,
    uint32_t slot,
    bool fade_in)
{
    box_audio_effect_t *fx = &core->effects[slot];
    const box_audio_effect_def_t *k = box_audio_effect_kind(fx->target_kind);
    if (fx->kind != fx->target_kind) {
        uint8_t *bytes = (uint8_t*)&fx->data;
        size_t i;
        for (i = 0; i < sizeof(fx->data); i ++) {
            bytes[i] = 0;
        }
    }

    fx->kind = fx->target_kind;
    fx->bus = fx->kind ? fx->target_bus : BOX_AUDIO_BUS_NONE;
    fx->index = fx->target_index;
    fx->key = fx->target_key;
    fx->moving = false;
    fx->quiet = 0;
    fx->idle = k->tail;
    fx->attached = fx->kind != BOX_AUDIO_EFFECT_NONE && fx->bus != BOX_AUDIO_BUS_NONE &&
        box_audio_effect_ready(core, fx, fx->kind);
    if (fx->attached && !fx->entering) {
        k->start(core, fx, fade_in);
    }

    box_audio_effect_place(core, slot);
}

/** Apply the target of an effect after it faded out of its place, and fade it into the new
 * one.
 *
 * @param core The core
 * @param slot Slot of the effect
 */
static void box_audio_effect_arrive(
    box_audio_core_t *core,
    uint32_t slot)
{
    box_audio_effect_t *fx = &core->effects[slot];
    box_audio_effect_apply(core, slot, true);
    box_audio_ramp_set(&fx->blend, fx->attached ? 0.0f : 1.0f);
    if (fx->attached) {
        box_audio_ramp_to(&fx->blend, 1, core->effect_fade);
    }
}

/** Put an effect that is not attached on its bus. Its kind starts at its first block: when
 * that is silent it starts at once, since its output would be silent either way. Otherwise it
 * fades in from the dry signal as after a move, so that enabling it does not click.
 *
 * @param core The core
 * @param slot Slot of the effect
 */
static void box_audio_effect_enter(
    box_audio_core_t *core,
    uint32_t slot)
{
    box_audio_effect_t *fx = &core->effects[slot];
    fx->entering = true;
    box_audio_effect_apply(core, slot, false);
    fx->entering = fx->attached;
    box_audio_ramp_set(&fx->blend, 1);
}

void box_audio_effects_init(
    box_audio_core_t *core)
{
    int32_t i;
    core->effect_fade = (int32_t)(BOX_AUDIO_EFFECT_FADE * 0.5f * core->sample_rate);
    for (i = 0; i < BOX_AUDIO_EFFECT_KINDS; i ++) {
        if (box_audio_effect_kinds[i].init) {
            box_audio_effect_kinds[i].init(core);
        }
    }

    for (i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        box_audio_effect_t *fx = &core->effects[i];
        fx->bus = BOX_AUDIO_BUS_NONE;
        fx->key = BOX_AUDIO_BUS_NONE;
        fx->target_bus = BOX_AUDIO_BUS_NONE;
        fx->target_key = BOX_AUDIO_BUS_NONE;
        fx->idle = true;
        box_audio_ramp_set(&fx->blend, 1);
    }
}

bool box_audio_core_effect_memory(
    box_audio_core_t *core,
    uint32_t slot,
    float *data,
    int32_t floats)
{
    bool result = false;
    box_audio_effect_t *fx;
    if (!core || slot >= BOX_AUDIO_EFFECT_MAX || !data || floats <= 0 ||
        (core->effects[slot].memory && floats <= core->effects[slot].floats))
    {
        goto done;
    }

    fx = &core->effects[slot];
    fx->memory = data;
    fx->floats = floats;
    if (fx->target_kind && box_audio_effect_ready(core, fx, fx->target_kind)) {
        box_audio_effect_enter(core, slot);
    } else if (fx->attached && box_audio_effect_floats(fx->kind, core->sample_rate)) {
        fx->attached = false;
        box_audio_effect_place(core, slot);
    }

    result = true;
done:
    return result;
}

bool box_audio_core_effect_running(
    const box_audio_core_t *core,
    uint32_t slot)
{
    const box_audio_effect_t *fx = core && slot < BOX_AUDIO_EFFECT_MAX ?
        &core->effects[slot] : NULL;

    return fx && fx->attached && fx->kind != BOX_AUDIO_EFFECT_NONE && !fx->idle;
}

void box_audio_effect_command(
    box_audio_core_t *core,
    const box_audio_cmd_effect_t *cmd)
{
    box_audio_effect_t *fx;
    bool off;
    int32_t i;
    if (cmd->slot >= BOX_AUDIO_EFFECT_MAX || cmd->kind >= BOX_AUDIO_EFFECT_KINDS ||
        cmd->index >= BOX_AUDIO_EFFECT_SLOTS ||
        (cmd->bus >= BOX_AUDIO_BUS_MAX && cmd->bus != BOX_AUDIO_BUS_NONE) ||
        (cmd->key >= BOX_AUDIO_BUS_MAX && cmd->key != BOX_AUDIO_BUS_NONE))
    {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_EFFECT, 0);
        goto done;
    }

    fx = &core->effects[cmd->slot];
    off = cmd->kind == BOX_AUDIO_EFFECT_NONE || cmd->bus == BOX_AUDIO_BUS_NONE;
    fx->target_kind = off ? BOX_AUDIO_EFFECT_NONE : cmd->kind;
    fx->target_bus = off ? BOX_AUDIO_BUS_NONE : cmd->bus;
    fx->target_index = cmd->index;
    fx->target_key = cmd->key;
    for (i = 0; !off && i < BOX_AUDIO_EFFECT_PARAMS; i ++) {
        fx->params[i] = cmd->params[i];
    }

    if (fx->target_kind && !box_audio_effect_ready(core, fx, fx->target_kind)) {
        goto done;
    }

    if (!fx->attached || fx->entering) {
        if (fx->target_kind) {
            box_audio_effect_enter(core, cmd->slot);
        } else if (fx->entering) {
            fx->entering = false;
            box_audio_effect_apply(core, cmd->slot, false);
        }

        goto done;
    }

    if (fx->target_kind == fx->kind && fx->bus != BOX_AUDIO_BUS_NONE) {
        box_audio_effect_kind(fx->kind)->command(core, fx);
    }

    if (fx->target_kind == fx->kind && fx->target_bus == fx->bus &&
        fx->target_index == fx->index)
    {
        fx->key = fx->target_key;
        if (fx->moving) {
            fx->moving = false;
            box_audio_ramp_to(&fx->blend, 1, core->effect_fade);
        }

        goto done;
    }

    if (fx->bus != BOX_AUDIO_BUS_NONE && !core->bus_active[fx->bus]) {
        fx->bus = BOX_AUDIO_BUS_NONE;
        fx->quiet = 0;
        box_audio_effect_place(core, cmd->slot);
    }

    if (!fx->moving) {
        fx->moving = true;
        box_audio_ramp_to(&fx->blend, 0, core->effect_fade);
    }

done:
    return;
}

void box_audio_effects_order(
    box_audio_core_t *core)
{
    uint32_t slot;
    for (slot = 0; slot < BOX_AUDIO_EFFECT_MAX; slot ++) {
        box_audio_effect_t *fx = &core->effects[slot];
        if (fx->attached && fx->moving && fx->bus != BOX_AUDIO_BUS_NONE &&
            !core->bus_active[fx->bus])
        {
            fx->bus = BOX_AUDIO_BUS_NONE;
            fx->quiet = 0;
            box_audio_effect_place(core, slot);
        }
    }
}

/** Check whether a block of input is silent.
 *
 * @param l Left samples
 * @param r Right samples
 * @param frames Frames of the block
 * @return Whether every sample is under BOX_AUDIO_EFFECT_FLOOR
 */
static bool box_audio_effect_silent(
    const float *l,
    const float *r,
    int32_t frames)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < frames && result; i ++) {
        float a = l[i] < 0 ? -l[i] : l[i], b = r[i] < 0 ? -r[i] : r[i];
        result = a < BOX_AUDIO_EFFECT_FLOOR && b < BOX_AUDIO_EFFECT_FLOOR;
    }

    return result;
}

/** Run an effect over a block of its bus, in place, or skip it when it decayed and nothing
 * comes in. While it blends, its output is mixed with its input by the blend.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @param l Left samples
 * @param r Right samples
 * @param frames Frames of the block
 */
static void box_audio_effect_run(
    box_audio_core_t *core,
    uint32_t slot,
    float *l,
    float *r,
    int32_t frames)
{
    box_audio_effect_t *fx = &core->effects[slot];
    const box_audio_effect_def_t *k = box_audio_effect_kind(fx->kind);
    bool silent = (k->tail || fx->entering) && box_audio_effect_silent(l, r, frames);
    bool blend;
    int32_t i;
    if (fx->entering) {
        fx->entering = false;
        k->start(core, fx, !silent);
        if (!silent) {
            box_audio_ramp_set(&fx->blend, 0);
            box_audio_ramp_to(&fx->blend, 1, core->effect_fade);
        }

        silent = silent && k->tail;
    }

    blend = fx->blend.left > 0 || fx->blend.value != 1.0f;
    if (fx->idle && silent) {
        k->rest(core, fx);
        box_audio_ramp_skip(&fx->blend, frames);
    } else {
        fx->idle = false;
        fx->quiet = !silent ? 0 : (fx->quiet < (1 << 30) ? fx->quiet + frames : fx->quiet);
        for (i = 0; blend && i < frames; i ++) {
            core->effect_l[i] = l[i];
            core->effect_r[i] = r[i];
        }

        if (k->render(core, fx, l, r, frames, silent)) {
            fx->idle = true;
        }

        for (i = 0; blend && i < frames; i ++) {
            float g = box_audio_ramp_next(&fx->blend);
            l[i] = core->effect_l[i] + (l[i] - core->effect_l[i]) * g;
            r[i] = core->effect_r[i] + (r[i] - core->effect_r[i]) * g;
        }
    }

    if (fx->moving && fx->blend.left == 0 && fx->blend.value == 0) {
        box_audio_effect_arrive(core, slot);
    }
}

/** Add the tail of an effect whose bus went to the master, faded out by its blend, and apply
 * its target once it is silent.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @param l Left samples of the master
 * @param r Right samples of the master
 * @param frames Frames of the block
 */
static void box_audio_effect_tail(
    box_audio_core_t *core,
    uint32_t slot,
    float *l,
    float *r,
    int32_t frames)
{
    box_audio_effect_t *fx = &core->effects[slot];
    const box_audio_effect_def_t *k = box_audio_effect_kind(fx->kind);
    bool decayed = fx->idle;
    int32_t i;
    if (!decayed) {
        for (i = 0; i < frames; i ++) {
            core->effect_l[i] = 0;
            core->effect_r[i] = 0;
        }

        fx->quiet = fx->quiet < (1 << 30) ? fx->quiet + frames : fx->quiet;
        decayed = k->render(core, fx, core->effect_l, core->effect_r, frames, true);
        fx->idle = decayed;
        for (i = 0; i < frames; i ++) {
            float g = box_audio_ramp_next(&fx->blend);
            l[i] += core->effect_l[i] * g;
            r[i] += core->effect_r[i] * g;
        }
    }

    if (decayed || (fx->blend.left == 0 && fx->blend.value == 0)) {
        box_audio_effect_arrive(core, slot);
    }
}

void box_audio_effects_render(
    box_audio_core_t *core,
    uint32_t bus,
    float *l,
    float *r,
    int32_t frames)
{
    uint64_t mask = core->bus_effects[bus];
    uint8_t list[BOX_AUDIO_EFFECT_MAX];
    int32_t count = 0, i;
    uint32_t index, s;
    for (index = 0; mask && index < BOX_AUDIO_EFFECT_SLOTS; index ++) {
        for (s = 0; s < BOX_AUDIO_EFFECT_MAX && (mask >> s); s ++) {
            if (((mask >> s) & 1) && core->effects[s].index == index) {
                list[count ++] = (uint8_t)s;
            }
        }
    }

    for (i = 0; i < count; i ++) {
        const box_audio_effect_t *fx = &core->effects[list[i]];
        if (fx->attached && fx->bus == bus) {
            box_audio_effect_run(core, list[i], l, r, frames);
        }
    }

    mask = bus == 0 ? core->effect_tails : 0;
    for (s = 0; s < BOX_AUDIO_EFFECT_MAX && (mask >> s); s ++) {
        const box_audio_effect_t *fx = &core->effects[s];
        if (((mask >> s) & 1) && fx->attached && fx->bus == BOX_AUDIO_BUS_NONE) {
            box_audio_effect_tail(core, s, l, r, frames);
        }
    }
}
