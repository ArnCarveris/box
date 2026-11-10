/**
 * @file audio/module_timeline.h
 * @brief Functions of box.audio for the timeline, plays, sidechains, instruments, emitters, the
 * sequencer and automation.
 */

#ifndef BOX_AUDIO_MODULE_TIMELINE_H
#define BOX_AUDIO_MODULE_TIMELINE_H

#include "module.h"

/** Set up the timeline of the module.
 *
 * @param state The state
 */
void box_audio_timeline_init(
    box_audio_module_t *state);

/** Free the timeline of the module.
 *
 * @param state The state
 */
void box_audio_timeline_fini(
    box_audio_module_t *state);

/** Once per frame, into the batch of the frame: follow the Transport (start, stop, seek,
 * tempo and loop changes), compute the lookahead window, schedule the metronome, call the
 * hooks and write the position from the clock.
 *
 * While a song swaps or loads the timeline stays stopped, whatever Transport.playing says.
 *
 * @param world The world
 * @param state The state
 * @param t The transport
 * @param waiting Whether the page waits for a gesture, which schedules nothing
 */
void box_audio_timeline_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t,
    bool waiting);

/** Add a user of the timeline.
 *
 * @param state The state
 * @param hook The callbacks, copied
 * @return Whether there was room, BOX_AUDIO_TIMELINE_HOOKS
 */
bool box_audio_timeline_hook(
    box_audio_module_t *state,
    const box_audio_timeline_hook_t *hook);

/** Get the lookahead window of the last frame.
 *
 * @param world The world
 * @param count Receives the number of segments
 * @return The segments, NULL when there are none
 */
const box_audio_segment_t* box_audio_timeline_window(
    const ecs_world_t *world,
    int32_t *count);

/** Get the frame of a beat on the line of a piece: anchor frame + round((beat - anchor beat)
 * * frames_per_beat).
 *
 * @param anchor The piece
 * @param beat The beat
 * @return The frame, negative before the clock started
 */
int64_t box_audio_anchor_frame(
    const box_audio_anchor_t *anchor,
    double beat);

/** Get the beat at a frame on the line of a piece.
 *
 * @param anchor The piece
 * @param frame The frame
 * @return The beat
 */
double box_audio_anchor_beat(
    const box_audio_anchor_t *anchor,
    uint64_t frame);

/** Convert a frame of the core clock to a beat of the timeline, through the pieces up to the
 * end of the window, loop wraps included; a later frame goes on along the last piece.
 *
 * @param world The world
 * @param frame The frame
 * @return The beat, the position while the transport is stopped
 */
double box_audio_timeline_beat(
    const ecs_world_t *world,
    uint64_t frame);

/** Get the beat and the tempo of a play at a frame, which synced synth LFOs lock to: through
 * the pieces of the timeline while the Transport plays, its position and tempo while it is
 * stopped, beat 0 at 120 bpm without one.
 *
 * @param world The world
 * @param state The state
 * @param when Frame of the play, 0 or a past one for the clock
 * @param beat Receives the beat, wrapped into 0..BOX_AUDIO_BEAT_WRAP
 * @param tempo Receives the beats per second
 */
void box_audio_timeline_grid(
    const ecs_world_t *world,
    const box_audio_module_t *state,
    uint64_t when,
    float *beat,
    float *tempo);

/** Convert a beat of the timeline to a frame of the core clock: the first piece that plays the
 * beat at or after the clock, else the first that played it, else the line of the last piece.
 *
 * @param world The world
 * @param beat The beat
 * @return The frame, -1 while the transport is stopped
 */
int64_t box_audio_timeline_frame(
    const ecs_world_t *world,
    double beat);

/** Send the plays of box_audio_play_quantized, after the timeline followed the Transport of
 * the frame, each with the frame of its boundary as its when, then clear the plays. The
 * timeline does not remember them, so a stop or a seek of the transport leaves them be.
 *
 * A play starts on the first beat or bar boundary at or after the clock, through the pieces of
 * the window and the loop wraps after it; a loop without a boundary starts it at its first
 * wrap. Without a transport, or a stopped one, it starts at once.
 *
 * @param world The world
 * @param state The state
 * @param waiting Whether the page waits for a gesture
 */
void box_audio_timeline_quantized(
    ecs_world_t *world,
    box_audio_module_t *state,
    bool waiting);

/** Send a play of box_audio_play, box_audio_play_at, box_audio_play_quantized or an
 * AudioEmitter as a one shot.
 *
 * @param world The world
 * @param state The state
 * @param play The play
 * @param waiting Whether the page waits for a gesture, which drops it
 * @param when Frame of its first sample, 0 for now; a later play does not glide a legato voice
 * @return The handle of the voice, 0 when the play was dropped: a sound that is not ready, a
 * gesture that is awaited, a spatial play beyond max_distance
 */
uint32_t box_audio_play_send(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_play_t *play,
    bool waiting,
    uint64_t when);

/** Mark the order dirty when the sidechain of a Compressor, or the modulator of a Vocoder, in
 * an insert slot changed since the last compile. Costs nothing while no Compressor and no
 * Vocoder exists and none had a sidechain.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_sidechains_check(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Find the sidechains of the compressors and the modulators of the vocoders in the insert
 * slots, after the routes of the compile are known: an edge from each key to the bus it ducks
 * or vocodes, so the key runs first.
 *
 * A sidechain that would form a cycle, or that is the master, logs one error and keys on the own
 * input; so does such a modulator, or one that is the bus of its vocoder, and the vocoder passes
 * its input.
 *
 * @param world The world
 * @param state The state, with route_out and route_sends of this compile
 * @param keyed Receives the buses each bus index is the sidechain of, a bit each
 */
void box_audio_sidechains_compile(
    ecs_world_t *world,
    box_audio_module_t *state,
    uint64_t *keyed);

/** Get the bus index of a bus entity.
 *
 * @param world The world
 * @param state The state
 * @param bus The entity
 * @return The index, 0 when the entity is not a live bus with an index
 */
uint8_t box_audio_bus_of(
    ecs_world_t *world,
    const box_audio_module_t *state,
    ecs_entity_t bus);

/** Get the sound id of a sound entity in the current output.
 *
 * @param world The world
 * @param state The state
 * @param sound The sound entity
 * @return The id, 0 when the output does not have the sound
 */
uint32_t box_audio_sound_of(
    ecs_world_t *world,
    const box_audio_module_t *state,
    ecs_entity_t sound);

/** Check whether the SoundFile of a sound still decodes.
 *
 * @param world The world
 * @param sound The sound entity
 * @return Whether its sample is loading
 */
bool box_audio_sound_loading(
    ecs_world_t *world,
    ecs_entity_t sound);

/** Get the bus a sound plays on by default: the bus of its AudioOutput.
 *
 * @param world The world
 * @param state The state
 * @param sound The sound entity
 * @return The bus index, 0 for the master
 */
uint8_t box_audio_sound_bus(
    ecs_world_t *world,
    const box_audio_module_t *state,
    ecs_entity_t sound);

/** Check whether a sound is a legato synth sound.
 *
 * @param world The world
 * @param sound The sound entity
 * @param channel Receives its SynthGlide.channel, may be NULL
 * @return Whether it has SynthSound and a SynthGlide with legato
 */
bool box_audio_legato_of(
    ecs_world_t *world,
    ecs_entity_t sound,
    int32_t *channel);

/** Write a note of an Instrument, in instrument.c: the PLAY of its sound at the pitch ratio
 * 2^((pitch - root) / 12) and the gain velocity^velocity_curve from the start frame, and with
 * a gate its RELEASE at the end frame.
 *
 * A synth plays with BOX_AUDIO_PLAY_GATE when gated, a sampler with BOX_AUDIO_PLAY_ATTACK. A
 * note of a legato synth sound follows the voice of its channel with a LEGATO at the start
 * frame, which glides that voice on when it holds.
 *
 * The note is a one shot of the module, so a deleted bus reroutes it and its end is read. The
 * transport does not cancel it.
 *
 * @param world The world
 * @param state The state
 * @param instrument The entity with Instrument
 * @param pitch MIDI note number, fractions for microtones
 * @param velocity Velocity, 0..1
 * @param pan Pan of the voice, -1..1
 * @param start Frame of the core clock the note starts at
 * @param end Frame its release starts at, ignored without a gate
 * @param bus Bus index of the voice
 * @param handle Handle of the voice, 0 to pick a new one
 * @return The handle, 0 when the note did not go out: no instrument, a sound the output does
 *     not have or a full batch
 */
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
    uint32_t handle);

/** Write a note of an Instrument as box_audio_instrument_note does, with a gain on the gain
 * of the velocity and, when release is false, without the RELEASE, which the caller sends.
 *
 * @param world The world
 * @param state The state
 * @param instrument The entity with Instrument
 * @param pitch MIDI note number
 * @param velocity Velocity, 0..1
 * @param gain Linear gain times the gain of the velocity
 * @param pan Pan of the voice, -1..1
 * @param start Frame of the core clock the note starts at
 * @param end Frame its release starts at, ignored without a gate or a release
 * @param bus Bus index of the voice
 * @param handle Handle of the voice, 0 to pick a new one
 * @param release Whether a gated note writes its RELEASE at end
 * @param priority Priority of the voice, 0 for a one shot, the priority of a Track for its
 *     notes
 * @return The handle, 0 when the note did not go out
 */
uint32_t box_audio_instrument_voice(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t instrument,
    float pitch,
    float velocity,
    float gain,
    float pan,
    uint64_t start,
    uint64_t end,
    uint8_t bus,
    uint32_t handle,
    bool release,
    uint8_t priority);

/** Seconds between the plays of an AudioEmitter at least */
#define BOX_AUDIO_EMITTER_INTERVAL_MIN (0.01)

/** Seconds a native or web output schedules the plays of emitters ahead of the frame, so a
 * batch arrives before their frame */
#define BOX_AUDIO_EMITTER_LOOKAHEAD (0.05)

/** Seconds a play of an emitter may be late, after a stall of the frame, before it is skipped
 * instead of sent at once */
#define BOX_AUDIO_EMITTER_LATE_MAX (0.1)

/** Register the query of the emitters.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_emitter_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Send the plays of the emitters that fall before the end of the next frame, each at its
 * frame of the core clock. An emitter that is not due costs one comparison.
 *
 * @param world The world
 * @param state The state
 * @param t The transport
 * @param waiting Whether the page waits for a gesture
 * @param delta Seconds of the frame, which the output renders next
 */
void box_audio_emitters_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t,
    bool waiting,
    float delta);

/** Forget a one shot that ended or is no longer tracked: its emitter has one play fewer that
 * sounds.
 *
 * @param world The world
 * @param state The state
 * @param shot The one shot
 */
void box_audio_oneshot_forget(
    ecs_world_t *world,
    const box_audio_module_t *state,
    const box_audio_oneshot_t *shot);

/** Point of an emitter shape in the space of the emitter, from three uniform numbers in [0, 1):
 * uniform over the area of a disc and the volume of the other shapes.
 *
 * @param shape The shape
 * @param radius Radius of disc, sphere and hemisphere, its absolute value is used
 * @param extents Half size of the box, their absolute values are used
 * @param u Three uniform numbers
 * @param out Receives the point
 */
void box_audio_emitter_point(
    AudioEmitterShape shape,
    float radius,
    const vec3 extents,
    const double u[3],
    vec3 out);

/** Register the observer that drops the spread note count of a removed Instrument.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_instrument_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Seconds of the longest ramp of a curved span of the expression of a note */
#define BOX_AUDIO_EXPRESS_STEP (0.01)

/** Give the curves of a note copies of their keys of its own, after a copy bit for bit.
 *
 * @param note The note
 */
void box_audio_note_dup(
    Note *note);

/** Free the keys of the curves of a note.
 *
 * @param note The note
 */
void box_audio_note_fini(
    Note *note);

/** Register the observers and the timeline hook of the clips and tracks.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_sequencer_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free the clips and tracks of the module.
 *
 * @param state The state
 */
void box_audio_sequencer_fini(
    box_audio_module_t *state);

/** Register the observers and the timeline hook of the automation clips. After
 * box_audio_sequencer_init, whose query of the placements it shares.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_automation_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free the automation lanes of the module.
 *
 * @param state The state
 */
void box_audio_automation_fini(
    box_audio_module_t *state);

/** Send the notes of box_audio_play_note, each at the clock of the output with its release
 * its duration later, on the bus of the AudioOutput of the instrument, else of its sound,
 * then clear them. Before a gesture and while the sound loads a note is dropped.
 *
 * @param world The world
 * @param state The state
 * @param waiting Whether the page waits for a gesture
 */
void box_audio_notes_send(
    ecs_world_t *world,
    box_audio_module_t *state,
    bool waiting);

/** Write a PLAY at a frame and remember its handle, so that a stop, a seek or a new output
 * cancels it while it has not started.
 *
 * @param state The state
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pitch Pitch ratio
 * @param when Frame of its first sample
 * @return The handle, 0 when the batch had no room
 */
uint32_t box_audio_timeline_play(
    box_audio_module_t *state,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pitch,
    uint64_t when);

#endif
