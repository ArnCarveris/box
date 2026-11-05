/**
 * @file audio/effect_sync.c
 * @brief The effects of the module: the entities that the insert slots of the channels name,
 * their slots of the pool of the core, their memory and their commands.
 *
 * An effect entity is any entity with an effect component: Reverb, Equalizer, AudioFilter,
 * Compressor, Gate, Delay, Phaser, Chorus, Saturator, TransientShaper or Rotary, the first in
 * that order. A compressor gets the key bus that the order compile accepted for its insert.
 *
 * The module sends an EFFECT command when the kind, the channel, the slot or a parameter of an
 * effect changes, and takes it off when no insert slot names it any more.
 */

#include "module.h"

/** Tempo of a synced delay without a Transport, in quarter notes per minute */
#define BOX_AUDIO_DELAY_TEMPO 120.0f

/** Slowest and fastest tempo of a synced delay, those of the timeline */
#define BOX_AUDIO_DELAY_TEMPO_MIN 20.0f
#define BOX_AUDIO_DELAY_TEMPO_MAX 999.0f

/** Get the time of a delay in seconds: sync beats at the tempo of the Transport when sync is
 * positive, else time. A change of the tempo changes this parameter, which sends a new time.
 *
 * @param world The world
 * @param delay The delay
 * @return Seconds, clamped to 0..BOX_AUDIO_DELAY_MAX
 */
static float box_audio_delay_time(
    const ecs_world_t *world,
    const Delay *delay)
{
    const Transport *tr = NULL;
    float tempo = BOX_AUDIO_DELAY_TEMPO, result = delay->time;
    if (delay->sync > 0) {
        tr = ecs_singleton_get(world, Transport);
        tempo = tr ? tr->tempo : BOX_AUDIO_DELAY_TEMPO;
        tempo = tempo >= BOX_AUDIO_DELAY_TEMPO_MIN ? (tempo < BOX_AUDIO_DELAY_TEMPO_MAX ?
            tempo : BOX_AUDIO_DELAY_TEMPO_MAX) : BOX_AUDIO_DELAY_TEMPO_MIN;
        result = delay->sync * 60.0f / tempo;
    }

    if (!(result >= 0)) {
        result = 0;
    } else if (result > BOX_AUDIO_DELAY_MAX) {
        result = BOX_AUDIO_DELAY_MAX;
    }

    return result;
}

int32_t box_audio_module_effect_slot(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    box_audio_module_t *state = box_audio_module_get(world);
    const AudioEffectState *s = NULL;
    int32_t result = -1;
    if (!state || !e || !ecs_is_alive(world, e)) {
        goto done;
    }

    s = ecs_get(world, e, AudioEffectState);
    if (s && s->slot && state->effects[s->slot - 1].entity == e) {
        result = s->slot - 1;
    }

done:
    return result;
}

uint32_t box_audio_module_effect_params(
    const ecs_world_t *world,
    ecs_entity_t component,
    const void *ptr,
    float *params)
{
    uint32_t result = BOX_AUDIO_EFFECT_NONE;
    int32_t i;
    for (i = 0; params && i < BOX_AUDIO_EFFECT_PARAMS; i ++) {
        params[i] = 0;
    }

    if (!component) {
        goto done;
    }

    if (component == ecs_id(Reverb)) {
        const Reverb *reverb = ptr;
        result = BOX_AUDIO_EFFECT_REVERB;
        if (params && ptr) {
            params[0] = reverb->decay;
            params[1] = reverb->damping;
            params[2] = reverb->predelay;
            params[3] = reverb->size;
            params[4] = reverb->mix;
        }
    } else if (component == ecs_id(Equalizer)) {
        const Equalizer *eq = ptr;
        result = BOX_AUDIO_EFFECT_EQ;
        for (i = 0; params && ptr && i < BOX_AUDIO_EQ_BANDS; i ++) {
            params[i * 4] = (float)eq->bands[i].kind;
            params[i * 4 + 1] = eq->bands[i].frequency;
            params[i * 4 + 2] = eq->bands[i].gain;
            params[i * 4 + 3] = eq->bands[i].q;
        }
    } else if (component == ecs_id(AudioFilter)) {
        const AudioFilter *filter = ptr;
        result = BOX_AUDIO_EFFECT_FILTER;
        if (params && ptr) {
            params[0] = filter->kind >= SynthFormant ? (float)BOX_AUDIO_SYNTH_LOWPASS :
                (float)((uint32_t)filter->kind + 1);
            params[1] = filter->cutoff;
            params[2] = filter->resonance;
            params[3] = (float)filter->slope;
            params[4] = filter->mix;
            params[5] = filter->drive;
        }
    } else if (component == ecs_id(Compressor)) {
        const Compressor *comp = ptr;
        result = BOX_AUDIO_EFFECT_COMPRESSOR;
        if (params && ptr) {
            params[0] = comp->threshold;
            params[1] = comp->ratio;
            params[2] = comp->attack;
            params[3] = comp->release;
            params[4] = comp->knee;
            params[5] = comp->makeup;
            params[6] = comp->mix;
        }
    } else if (component == ecs_id(Gate)) {
        const Gate *gate = ptr;
        result = BOX_AUDIO_EFFECT_GATE;
        if (params && ptr) {
            params[0] = gate->threshold;
            params[1] = gate->attack;
            params[2] = gate->hold;
            params[3] = gate->release;
            params[4] = gate->range;
        }
    } else if (component == ecs_id(Delay)) {
        const Delay *delay = ptr;
        result = BOX_AUDIO_EFFECT_DELAY;
        if (params && ptr) {
            params[0] = box_audio_delay_time(world, delay);
            params[1] = delay->feedback;
            params[2] = delay->damping;
            params[3] = delay->ping_pong ? 1.0f : 0.0f;
            params[4] = delay->mix;
        }
    } else if (component == ecs_id(Phaser)) {
        const Phaser *phaser = ptr;
        result = BOX_AUDIO_EFFECT_PHASER;
        if (params && ptr) {
            params[0] = (float)phaser->stages;
            params[1] = phaser->rate;
            params[2] = phaser->depth;
            params[3] = phaser->center;
            params[4] = phaser->feedback;
            params[5] = phaser->mix;
            params[6] = phaser->stereo;
        }
    } else if (component == ecs_id(Chorus)) {
        const Chorus *chorus = ptr;
        result = BOX_AUDIO_EFFECT_CHORUS;
        if (params && ptr) {
            params[0] = (float)chorus->voices;
            params[1] = chorus->rate;
            params[2] = chorus->depth;
            params[3] = chorus->delay;
            params[4] = chorus->mix;
            params[5] = chorus->spread;
        }
    } else if (component == ecs_id(Saturator)) {
        const Saturator *saturator = ptr;
        result = BOX_AUDIO_EFFECT_SATURATOR;
        if (params && ptr) {
            params[0] = saturator->drive;
            params[1] = saturator->asymmetry;
            params[2] = saturator->bits;
            params[3] = saturator->mix;
        }
    } else if (component == ecs_id(TransientShaper)) {
        const TransientShaper *shaper = ptr;
        result = BOX_AUDIO_EFFECT_TRANSIENT_SHAPER;
        if (params && ptr) {
            params[0] = shaper->attack;
            params[1] = shaper->sustain;
        }
    } else if (component == ecs_id(Rotary)) {
        const Rotary *rotary = ptr;
        result = BOX_AUDIO_EFFECT_ROTARY;
        if (params && ptr) {
            params[0] = rotary->speed;
            params[1] = rotary->horn_slow;
            params[2] = rotary->horn_fast;
            params[3] = rotary->drum_slow;
            params[4] = rotary->drum_fast;
            params[5] = rotary->acceleration;
            params[6] = rotary->balance;
            params[7] = rotary->spread;
            params[8] = rotary->drive;
            params[9] = rotary->mix;
        }
    }

done:
    return result;
}

int32_t box_audio_module_effect_components(
    ecs_entity_t *out)
{
    const ecs_entity_t components[] = {
        ecs_id(Reverb), ecs_id(Equalizer), ecs_id(AudioFilter), ecs_id(Compressor),
        ecs_id(Gate), ecs_id(Delay), ecs_id(Phaser), ecs_id(Chorus), ecs_id(Saturator),
        ecs_id(TransientShaper), ecs_id(Rotary)
    };
    int32_t i, count = (int32_t)(sizeof(components) / sizeof(components[0]));
    count = count < BOX_AUDIO_EFFECT_COMPONENTS ? count : BOX_AUDIO_EFFECT_COMPONENTS;
    for (i = 0; i < count; i ++) {
        out[i] = components[i];
    }

    return count;
}

ecs_entity_t box_audio_module_effect_component(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t components[BOX_AUDIO_EFFECT_COMPONENTS], result = 0;
    int32_t i, count;
    if (!e || !ecs_is_alive(world, e)) {
        goto done;
    }

    count = box_audio_module_effect_components(components);
    for (i = 0; i < count && !result; i ++) {
        if (ecs_has_id(world, e, components[i])) {
            result = components[i];
        }
    }

done:
    return result;
}

uint32_t box_audio_module_effect_kind(
    const ecs_world_t *world,
    ecs_entity_t e,
    float *params)
{
    ecs_entity_t component = box_audio_module_effect_component(world, e);

    return box_audio_module_effect_params(world, component,
        component ? ecs_get_id(world, e, component) : NULL, params);
}

/** Write an effect command and remember what it sent.
 *
 * @param state The state
 * @param slot Slot of the effect
 * @param kind Kind, BOX_AUDIO_EFFECT_NONE to take the effect off
 * @param bus Bus index
 * @param index Insert slot within the bus
 * @param key Sidechain bus, BOX_AUDIO_BUS_NONE for none
 * @param params The parameters
 */
static void box_audio_effect_send(
    box_audio_module_t *state,
    uint32_t slot,
    uint32_t kind,
    uint32_t bus,
    uint32_t index,
    uint32_t key,
    const float *params)
{
    box_audio_effect_sent_t *sent = &state->effects[slot];
    if (box_audio_reserve(state)) {
        box_audio_write_effect(&state->writer, slot, kind,
            kind ? bus : BOX_AUDIO_BUS_NONE, index, key, params);
        state->stats.effects ++;
    }

    sent->serial = state->serial;
    sent->kind = kind;
    sent->bus = bus;
    sent->index = index;
    sent->key = key;
    ecs_os_memcpy_n(sent->params, params, float, BOX_AUDIO_EFFECT_PARAMS);
}

/** Note the effect entities the insert slots of a bus name this frame. An entity that an
 * earlier slot of the frame named logs one error that names both channels, and this slot stays
 * empty.
 *
 * @param world The world
 * @param state The state
 * @param channel The bus entity
 * @param bus The bus
 * @param index Index of the bus in the core
 * @param claimed Receives the entities named for the first time this frame
 * @param count Entities in claimed
 */
static void box_audio_effect_claims(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t channel,
    const AudioBus *bus,
    uint8_t index,
    ecs_entity_t *claimed,
    int32_t *count)
{
    int32_t j;
    for (j = 0; j < BOX_AUDIO_EFFECT_SLOTS; j ++) {
        ecs_entity_t e = bus->effects[j];
        AudioEffectState *es;
        if (!e || !ecs_is_alive(world, e) ||
            box_audio_module_effect_kind(world, e, NULL) == BOX_AUDIO_EFFECT_NONE)
        {
            continue;
        }

        es = ecs_get_mut(world, e, AudioEffectState);
        if (!es) {
            continue;
        }

        if (es->frame == state->effect_frame) {
            es->twice_frame = state->effect_frame;
            if (!es->twice) {
                char *path = ecs_get_path(world, e);
                char *first = ecs_get_path(world, es->channel);
                char *second = ecs_get_path(world, channel);
                ecs_err("box.audio: the effect '%s' is in an insert slot of '%s' and of '%s', "
                    "the slot of '%s' stays empty", path, first, second, second);
                ecs_os_free(path);
                ecs_os_free(first);
                ecs_os_free(second);
                es->twice = true;
            }

            continue;
        }

        es->frame = state->effect_frame;
        es->channel = channel;
        es->bus = index;
        es->index = (uint8_t)j;
        if (*count < BOX_AUDIO_BUS_MAX * BOX_AUDIO_EFFECT_SLOTS) {
            claimed[(*count) ++] = e;
        }
    }
}

/** Free the effect slots whose entity was deleted, is no effect any more or no insert slot
 * names this frame; they are taken off and their tails fade out.
 *
 * @param world The world
 * @param state The state
 * @return Slots freed, a bit each
 */
static uint64_t box_audio_effects_free(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    uint64_t result = 0;
    uint32_t i;
    for (i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        box_audio_effect_sent_t *sent = &state->effects[i];
        ecs_entity_t e = sent->entity;
        AudioEffectState *es = e && ecs_is_alive(world, e) ?
            ecs_get_mut(world, e, AudioEffectState) : NULL;
        if (!e || (es && es->slot == i + 1 && es->frame == state->effect_frame &&
            box_audio_module_effect_kind(world, e, NULL) != BOX_AUDIO_EFFECT_NONE))
        {
            continue;
        }

        if (state->serial && sent->serial == state->serial &&
            sent->kind != BOX_AUDIO_EFFECT_NONE)
        {
            box_audio_effect_send(state, i, BOX_AUDIO_EFFECT_NONE, sent->bus, sent->index,
                BOX_AUDIO_BUS_NONE, sent->params);
        }

        if (es && es->slot == i + 1) {
            es->slot = 0;
            es->twice = false;
        }

        sent->entity = 0;
        result |= (uint64_t)1 << i;
    }

    return result;
}

/** Give an effect a free slot of the pool, or log once that every slot is taken. Prefers a
 * slot whose memory holds the kind, then one that never had memory, then any slot that was not
 * freed this frame, so a tail that fades out keeps its slot when it can.
 *
 * @param world The world
 * @param state The state
 * @param e The entity
 * @param es What was done for it
 * @param need Floats of memory its kind needs
 * @param freed Slots freed this frame, a bit each
 * @return The slot, -1 when the pool is full
 */
static int32_t box_audio_effect_alloc(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    AudioEffectState *es,
    int32_t need,
    uint64_t freed)
{
    int32_t i, pass, result = -1;
    for (pass = 0; pass < 4 && result < 0; pass ++) {
        for (i = 0; i < BOX_AUDIO_EFFECT_MAX && result < 0; i ++) {
            const box_audio_effect_sent_t *sent = &state->effects[i];
            bool fresh = !(freed & ((uint64_t)1 << i));
            bool fits = sent->memory == state->serial && sent->floats >= need;
            bool empty = sent->memory != state->serial || !sent->floats;
            if (sent->entity || (pass == 0 && !(fresh && fits)) ||
                (pass == 1 && !(fresh && empty)) || (pass == 2 && !fresh))
            {
                continue;
            }

            result = i;
        }
    }

    if (result >= 0) {
        state->effects[result].entity = e;
        state->effects[result].serial = 0;
        es->slot = (uint8_t)(result + 1);
        es->refused = false;
    } else if (!es->refused) {
        char *path = ecs_get_path(world, e);
        ecs_err("box.audio: more than %d effects, '%s' does nothing", BOX_AUDIO_EFFECT_MAX,
            path);
        ecs_os_free(path);
        es->refused = true;
        es->slot = 0;
    }

    return result;
}

void box_audio_effects_sync(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t)
{
    ecs_entity_t claimed[BOX_AUDIO_BUS_MAX * BOX_AUDIO_EFFECT_SLOTS];
    ecs_iter_t it = ecs_query_iter(world, state->buses);
    int32_t count = 0, i;
    uint64_t freed;
    state->effect_frame ++;
    if (!state->effect_frame) {
        state->effect_frame = 1;
    }

    while (ecs_query_next(&it)) {
        const AudioBus *bus = ecs_field(&it, AudioBus, 0);
        const AudioBusState *s = ecs_field(&it, AudioBusState, 1);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            if (e == state->master) {
                box_audio_effect_claims(world, state, e, &bus[i], 0, claimed, &count);
            } else if (s[i].index && state->bus_entity[s[i].index] == e) {
                box_audio_effect_claims(world, state, e, &bus[i], s[i].index, claimed, &count);
            }
        }
    }

    freed = box_audio_effects_free(world, state);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = claimed[i];
        AudioEffectState *es = ecs_get_mut(world, e, AudioEffectState);
        float params[BOX_AUDIO_EFFECT_PARAMS];
        uint32_t kind = box_audio_module_effect_kind(world, e, params), send, key;
        int32_t slot, need = box_audio_effect_floats(kind, t->sample_rate);
        box_audio_effect_sent_t *sent;
        if (es->twice_frame != state->effect_frame) {
            es->twice = false;
        }

        slot = es->slot && state->effects[es->slot - 1].entity == e ? es->slot - 1 :
            box_audio_effect_alloc(world, state, e, es, need, freed);
        if (slot < 0) {
            continue;
        }

        sent = &state->effects[slot];
        if (need > 0 && (sent->memory != state->serial || sent->floats < need) &&
            t->sample_rate > 0 && t->effect_memory(t, (uint32_t)slot, need))
        {
            sent->memory = state->serial;
            sent->floats = need;
        }

        send = ecs_has_id(world, e, EcsDisabled) ? BOX_AUDIO_EFFECT_NONE : kind;
        key = send == BOX_AUDIO_EFFECT_COMPRESSOR ? state->keys[es->bus][es->index] :
            BOX_AUDIO_BUS_NONE;
        if (sent->serial != state->serial || sent->kind != send || sent->bus != es->bus ||
            sent->index != es->index || sent->key != key ||
            ecs_os_memcmp(sent->params, params, ECS_SIZEOF(float) * BOX_AUDIO_EFFECT_PARAMS))
        {
            box_audio_effect_send(state, (uint32_t)slot, send, es->bus, es->index, key,
                params);
        }
    }
}
