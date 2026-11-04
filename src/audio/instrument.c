/**
 * @file audio/instrument.c
 * @brief Notes of instruments: a synth or a sampler that plays a sound at a pitch, a velocity,
 * a pan and a duration, ad hoc with box_audio_play_note and from later parts of box.audio
 * through box_audio_instrument_note.
 *
 * A note is a PLAY at its start frame and, with a gate, a RELEASE at its end frame, both
 * scheduled by the core. A gated synth sustains until the release and releases with its
 * envelopes; a sampler fades out over the release of the instrument.
 */

#include "module.h"
#include <math.h>

/** Tempo without a Transport, in quarter notes per minute */
#define BOX_AUDIO_NOTE_TEMPO 120.0

/** Slowest and fastest tempo of a note, those of the timeline */
#define BOX_AUDIO_NOTE_TEMPO_MIN 20.0
#define BOX_AUDIO_NOTE_TEMPO_MAX 999.0

/** Longest duration of a note in beats */
#define BOX_AUDIO_NOTE_BEATS_MAX 1000000.0

/** Largest exponent of the velocity in the gain of a note */
#define BOX_AUDIO_NOTE_CURVE_MAX 16.0f

/** The pitch ratio of a note, clamped to the limits of the core with one warning per
 * instrument.
 *
 * @param world The world
 * @param instrument The entity with Instrument
 * @param inst Its Instrument
 * @param pitch MIDI note number
 * @return The ratio, 2^((pitch - root) / 12)
 */
static float box_audio_note_ratio(
    ecs_world_t *world,
    ecs_entity_t instrument,
    const Instrument *inst,
    float pitch)
{
    double ratio = exp2(((double)pitch - (double)inst->root) / 12.0);
    float result = (float)ratio;
    AudioInstrumentState *s;
    if (!(ratio >= BOX_AUDIO_PITCH_MIN && ratio <= BOX_AUDIO_PITCH_MAX)) {
        result = ratio > BOX_AUDIO_PITCH_MAX ? BOX_AUDIO_PITCH_MAX : BOX_AUDIO_PITCH_MIN;
        s = ecs_get_mut(world, instrument, AudioInstrumentState);
        if (!s || !s->clamped) {
            char *path = ecs_get_path(world, instrument);
            if (s) {
                s->clamped = true;
            }

            ecs_warn("box.audio: note %g of instrument '%s' with root %g is past the pitch "
                "range of 1/64 to 16, clamped", (double)pitch, path, (double)inst->root);
            ecs_os_free(path);
        }
    }

    return result;
}

/** The gain of a note: its velocity, 0..1, to the power of the velocity curve.
 *
 * @param inst The instrument
 * @param velocity The velocity
 * @return The gain
 */
static float box_audio_note_gain(
    const Instrument *inst,
    float velocity)
{
    float v = velocity > 0 ? (velocity < 1 ? velocity : 1.0f) : 0.0f;
    float curve = inst->velocity_curve > 0 ? (inst->velocity_curve < BOX_AUDIO_NOTE_CURVE_MAX ?
        inst->velocity_curve : BOX_AUDIO_NOTE_CURVE_MAX) : 0.0f;

    return v > 0 ? powf(v, curve) : 0.0f;
}

uint32_t box_audio_instrument_note(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t instrument,
    float pitch,
    float velocity,
    float pan,
    uint64_t start,
    uint64_t end,
    uint8_t bus,
    uint32_t handle)
{
    const Instrument *inst = instrument && ecs_is_alive(world, instrument) ?
        ecs_get(world, instrument, Instrument) : NULL;
    box_audio_oneshot_t *shots, *shot, next;
    uint32_t sound = 0, flags, result = 0, held;
    float ratio, gain;
    int32_t k = -1, channel = 0;
    bool synth, legato;
    if (inst) {
        sound = box_audio_sound_of(world, state, inst->sound);
    }

    if (!sound || !box_audio_reserve(state)) {
        goto done;
    }

    ratio = box_audio_note_ratio(world, instrument, inst, pitch);
    gain = box_audio_note_gain(inst, velocity);
    pan = pan > -1 ? (pan < 1 ? pan : 1.0f) : (pan <= -1 ? -1.0f : 0.0f);
    synth = ecs_has(world, inst->sound, SynthSound);
    flags = synth ? (inst->gate ? BOX_AUDIO_PLAY_GATE : 0) : BOX_AUDIO_PLAY_ATTACK;
    legato = synth && box_audio_legato_of(world, inst->sound, &channel);
    handle = handle ? handle : box_audio_new_voice(state);
    shots = ecs_vec_first(&state->oneshots);
    for (k = ecs_vec_count(&state->oneshots) - 1; legato && k >= 0; k --) {
        if (shots[k].legato && shots[k].channel == channel) {
            break;
        }
    }

    next = (box_audio_oneshot_t){
        .legato = legato,
        .channel = channel,
        .handle = handle,
        .sound = sound,
        .bus = bus,
        .distances = { .min_distance = 1.0f, .max_distance = 50.0f, .rolloff = 1.0f },
        .volume = gain,
        .sent_volume = gain,
        .sent_pan = pan
    };

    if (legato && k >= 0) {
        held = shots[k].handle;
        shots[k] = next;
        box_audio_write_legato_at(&state->writer, handle, held, sound, bus, gain, pan, ratio,
            flags, start);
    } else {
        if (ecs_vec_count(&state->oneshots) >= BOX_AUDIO_ONESHOT_MAX) {
            ecs_vec_remove_ordered_t(&state->oneshots, box_audio_oneshot_t, 0);
        }

        shot = ecs_vec_append_t(NULL, &state->oneshots, box_audio_oneshot_t);
        *shot = next;
        box_audio_write_play(&state->writer, handle, sound, bus, gain, pan, ratio, flags,
            start);
    }

    if (inst->gate && box_audio_reserve(state)) {
        box_audio_write_voice_release(&state->writer, handle, inst->release,
            end > start ? end : start);
    }

    result = handle;
done:
    return result;
}

void box_audio_notes_send(
    ecs_world_t *world,
    box_audio_module_t *state,
    bool waiting)
{
    const box_audio_note_t *notes = ecs_vec_first(&state->notes);
    const Transport *tr = ecs_singleton_get(world, Transport);
    double tempo = tr ? (double)tr->tempo : BOX_AUDIO_NOTE_TEMPO, beats, frames;
    uint64_t start = box_audio_module_clock(world);
    int32_t i;
    tempo = tempo >= BOX_AUDIO_NOTE_TEMPO_MIN ? (tempo < BOX_AUDIO_NOTE_TEMPO_MAX ? tempo :
        BOX_AUDIO_NOTE_TEMPO_MAX) : BOX_AUDIO_NOTE_TEMPO_MIN;
    for (i = 0; i < ecs_vec_count(&state->notes); i ++) {
        const box_audio_note_t *note = &notes[i];
        const Instrument *inst = ecs_is_alive(world, note->instrument) ?
            ecs_get(world, note->instrument, Instrument) : NULL;
        const AudioOutput *out;
        uint8_t bus;
        if (!inst || !box_audio_sound_of(world, state, inst->sound)) {
            state->stats.not_ready += inst && box_audio_sound_loading(world, inst->sound);
            continue;
        }

        if (waiting) {
            state->stats.dropped ++;
            continue;
        }

        out = ecs_get(world, note->instrument, AudioOutput);
        bus = out ? box_audio_bus_of(world, state, out->bus) :
            box_audio_sound_bus(world, state, inst->sound);
        beats = note->duration > 0 ? (double)note->duration : 0.0;
        beats = beats < BOX_AUDIO_NOTE_BEATS_MAX ? beats : BOX_AUDIO_NOTE_BEATS_MAX;
        frames = beats * 60.0 / tempo * (double)state->rate;
        if (box_audio_instrument_note(world, state, note->instrument, note->pitch,
            note->velocity, 0.0f, start, start + (uint64_t)llround(frames), bus, 0))
        {
            state->stats.notes ++;
        }
    }

    ecs_vec_clear(&state->notes);
}

bool box_audio_play_note(
    ecs_world_t *world,
    ecs_entity_t instrument,
    float pitch,
    float velocity,
    float duration)
{
    box_audio_module_t *state = box_audio_module_get(world);
    const Instrument *inst = state && instrument && ecs_is_alive(world, instrument) ?
        ecs_get(world, instrument, Instrument) : NULL;
    ecs_entity_t sound = inst ? inst->sound : 0;
    bool result = false;
    box_audio_note_t *note;
    if (!sound || !ecs_is_alive(world, sound) || !(ecs_has(world, sound, AudioPcm) ||
        ecs_has(world, sound, SoundFile) || ecs_has(world, sound, SynthSound)))
    {
        goto done;
    }

    note = ecs_vec_append_t(NULL, &state->notes, box_audio_note_t);
    *note = (box_audio_note_t){ .instrument = instrument, .pitch = pitch,
        .velocity = velocity, .duration = duration };
    result = true;
done:
    return result;
}
