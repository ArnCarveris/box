/**
 * @file audio/module_mix.h
 * @brief Functions of box.audio for its state, songs, effects, modulators, macros, meters, synths
 * and wavetables.
 */

#ifndef BOX_AUDIO_MODULE_MIX_H
#define BOX_AUDIO_MODULE_MIX_H

#include "module.h"

/** Get the state of the module of a world.
 *
 * @param world The world
 * @return The state, NULL when box.audio is not imported
 */
box_audio_module_t* box_audio_module_get(
    const ecs_world_t *world);

/** Install the lifecycle hooks of Song, SongLane, AudioSong and AudioSongStatus, right after
 * their registration.
 *
 * @param world The world
 */
void box_audio_song_hooks(
    ecs_world_t *world);

/** Register the song singletons, the root of the song scripts, the observer of what the song
 * scripts own and the system that loads and unloads songs.
 *
 * The root of the songs owns box.audio.Song, so no script takes it and deletes it when it
 * unloads.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_song_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free what the song of the module holds.
 *
 * @param state The state
 */
void box_audio_song_fini(
    box_audio_module_t *state);

/** Tell the song that the core freed a sound id, from the events of the output.
 *
 * @param state The state
 * @param id The sound id
 */
void box_audio_song_sound_free(
    box_audio_module_t *state,
    uint32_t id);

/** Count the slots a swap still waits for: the bus, effect and sound slots of the song that
 * unloaded that are not reusable yet, for the tests.
 *
 * @param world The world
 * @return Slots, 0 without a swap
 */
int32_t box_audio_song_held(
    const ecs_world_t *world);

/** Get the counters of the songs, for the tests.
 *
 * @param world The world
 * @return The counters, zero when box.audio is not imported
 */
box_audio_song_stats_t box_audio_song_stats(
    const ecs_world_t *world);

/** Count the bus indices in use, the master excluded, for the tests.
 *
 * @param world The world
 * @return Bus slots that have an entity
 */
int32_t box_audio_module_buses(
    const ecs_world_t *world);

/** Count the effect slots in use, for the tests.
 *
 * @param world The world
 * @return Effect slots that have an entity
 */
int32_t box_audio_module_effects(
    const ecs_world_t *world);

/** Get the counters of the module of a world, for the tests.
 *
 * @param world The world
 * @return The counters, zero when box.audio is not imported
 */
box_audio_module_stats_t box_audio_module_stats(
    const ecs_world_t *world);

/** Get the buses of the last valid order, in processing order, without the master.
 *
 * @param world The world
 * @param buses Receives the bus entities
 * @param max Room in buses
 * @return Number of buses written
 */
int32_t box_audio_module_order(
    const ecs_world_t *world,
    ecs_entity_t *buses,
    int32_t max);

/** Get the index of a bus in the core.
 *
 * @param world The world
 * @param bus The bus entity
 * @return The index, 0 for the master or an entity that is no bus
 */
int32_t box_audio_module_bus_index(
    const ecs_world_t *world,
    ecs_entity_t bus);

/** Get the clock of the output of a world: the frame of its core that renders now, as far as
 * the main thread can tell, which commands with a when are scheduled against.
 *
 * @param world The world
 * @return Frames of the core clock, 0 when the world has no output
 */
uint64_t box_audio_module_clock(
    const ecs_world_t *world);

/** Find the listener of the frame: the first AudioListener, else the camera of the first
 * enabled View that has one.
 *
 * @param world The world
 * @param state The state
 * @param listener Receives the listener
 */
void box_audio_listener_find(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_listener_t *listener);

/** Get the slot of an effect entity in the pool of the core.
 *
 * @param world The world
 * @param e The entity with an effect component
 * @return The slot, -1 when it has none
 */
int32_t box_audio_module_effect_slot(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Get the kind of effect of an entity: the kind of its effect component.
 *
 * @param world The world
 * @param e The entity
 * @param params Receives the BOX_AUDIO_EFFECT_PARAMS parameters of its kind, may be NULL
 * @return A box_audio_effect_kind_t, BOX_AUDIO_EFFECT_NONE for an entity that is no effect
 */
uint32_t box_audio_module_effect_kind(
    const ecs_world_t *world,
    ecs_entity_t e,
    float *params);

/** Largest number of effect components */
#define BOX_AUDIO_EFFECT_COMPONENTS (17)

/** Get the effect components, in the order an entity that has several takes the first of.
 *
 * Reverb, Equalizer, AudioFilter, Compressor, Gate, Delay, Phaser, Chorus, Saturator,
 * TransientShaper, FrequencyShifter, PitchShifter, PatternGate, Rotary, BeatRepeat, TapeStop and
 * Vocoder. A new effect is added here and to box_audio_module_effect_params.
 *
 * @param out Receives up to BOX_AUDIO_EFFECT_COMPONENTS components
 * @return Number of components
 */
int32_t box_audio_module_effect_components(
    ecs_entity_t *out);

/** Get the effect component of an entity: the first of box_audio_module_effect_components it
 * has, which gives its kind.
 *
 * @param world The world
 * @param e The entity
 * @return The component, 0 for an entity that is no effect
 */
ecs_entity_t box_audio_module_effect_component(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Build the parameters of an effect from a value of its component, the builder of the EFFECT
 * commands; the automation of the timeline runs it on a copy with an automated member.
 *
 * @param world The world, for the tempo of a synced Delay
 * @param component The effect component, such as ecs_id(AudioFilter)
 * @param ptr The value of the component, NULL for the kind alone
 * @param params Receives the BOX_AUDIO_EFFECT_PARAMS parameters, may be NULL
 * @return A box_audio_effect_kind_t, BOX_AUDIO_EFFECT_NONE for a component that is no effect
 */
uint32_t box_audio_module_effect_params(
    const ecs_world_t *world,
    ecs_entity_t component,
    const void *ptr,
    float *params);

/** Walk a member path, segments separated by dots and elements in brackets such as
 * bands[2].gain, with a meta cursor to a float member.
 *
 * @param world The world
 * @param component The component
 * @param ptr A value of the component
 * @param member The path
 * @param cur Receives the cursor at the member
 * @return NULL when the member is a float, else the reason, to free with ecs_os_free
 */
char* box_audio_member_cursor(
    const ecs_world_t *world,
    ecs_entity_t component,
    void *ptr,
    const char *member,
    ecs_meta_cursor_t *cur);

/** Resolve a float member of a bus, an effect or a macro that automation, modulators and the
 * maps of macros move.
 *
 * Volume, pan, width and sends[i].level of an AudioBus or of the master, a float member of the
 * effect component of an effect entity, or the value of an AudioMacro, which the maps of a macro
 * may not move.
 *
 * @param world The world
 * @param state The state
 * @param target The entity with the AudioBus or the effect component
 * @param component AudioBus or the effect component
 * @param member Path of the member
 * @param out Receives the kind, entity, component, parameter and send of the target
 * @return NULL when the member can be moved, else the reason, to free with ecs_os_free
 */
char* box_audio_target_resolve(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t target,
    ecs_entity_t component,
    const char *member,
    box_audio_target_t *out);

/** Send the AudioModulator entities to the modulators of the core: resolve their target, give
 * them a slot, send what changed and release what no longer runs.
 *
 * Each one that cannot run logs one error that names it, until it changes. Sets whether the
 * timeline keeps the beat clock of the core, so it runs before box_audio_timeline_update.
 *
 * @param world The world
 * @param state The state, whose bus indices and effect slots are current
 * @param t The transport
 */
void box_audio_modulators_sync(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t);

/** Register the query and the observer of the modulators.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_modulators_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free what the modulators hold.
 *
 * @param state The state
 */
void box_audio_modulators_fini(
    box_audio_module_t *state);

/** Describe what became of a modulator, for the audio command.
 *
 * @param world The world
 * @param e The modulator entity
 * @return A text that lives as long as the program
 */
const char* box_audio_modulator_status(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Find the index of the parameter of an effect a float member of its component becomes, by
 * writing two marks into copies of the component and looking for them in the params its
 * builder makes.
 *
 * @param world The world
 * @param component The effect component
 * @param ptr The value of the component
 * @param size Bytes of the component, at most BOX_AUDIO_AUTOMATION_COMPONENT
 * @param member The path of the member
 * @return The index, -1 when the member reaches no parameter as it is
 */
int32_t box_audio_effect_member_param(
    const ecs_world_t *world,
    ecs_entity_t component,
    const void *ptr,
    ecs_size_t size,
    const char *member);

/** Check whether a member of an effect is a frequency, which AudioModOctaves applies to: a
 * cutoff, a center, the frequency of a band or the damping of a Delay.
 *
 * @param component The effect component
 * @param member The path of the member
 * @return Whether it is a frequency
 */
bool box_audio_member_frequency(
    ecs_entity_t component,
    const char *member);

/** Get the macro slot of the core of an AudioMacro, as of the last box_audio_macros_sync.
 *
 * @param world The world
 * @param e The macro entity
 * @return The slot, -1 when it has none: disabled, past the limit or not looked at yet
 */
int32_t box_audio_macro_slot(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Send the AudioMacro entities and their AudioMacroMap children to the core.
 *
 * Gives each macro a slot and sends its value when its component changes; resolves each map, lets
 * the newer of two maps of one member win, sends what changed and releases what no longer runs.
 * What cannot run logs one error that names it, a map whose member a newer map takes one warning.
 *
 * Runs before box_audio_modulators_sync and box_audio_timeline_update, which address the macros
 * by their slots.
 *
 * @param world The world
 * @param state The state, whose bus indices and effect slots are current
 * @param t The transport
 */
void box_audio_macros_sync(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t);

/** Register the queries and the observer of the macros.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_macros_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free what the macros hold.
 *
 * @param state The state
 */
void box_audio_macros_fini(
    box_audio_module_t *state);

/** Describe what became of a macro or a map, for the audio command.
 *
 * @param world The world
 * @param e The macro or map entity
 * @return A text that lives as long as the program
 */
const char* box_audio_macro_status(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Give the effects that the insert slots of the channels name their slots of the pool and
 * their memory, and send their kind, place and parameters when they changed, into the batch of
 * the frame. A Disabled effect is sent as taken off, so it is bypassed with a fade.
 *
 * @param world The world
 * @param state The state, whose bus indices are current
 * @param t The transport
 */
void box_audio_effects_sync(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t);

/** Take the meters of the output, update the levels of every bus and write them into each
 * entity with AudioMeter: the master, a channel with a bus index, an effect with a slot.
 *
 * @param world The world
 * @param state The state, whose bus indices and effect slots are current
 * @param t The transport, NULL for none
 */
void box_audio_meters_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t);

/** Turn the loudness meters of the core on for the buses of a measurement over a span that
 * runs, and off for the others, into the batch of the frame.
 *
 * @param state The state, whose bus indices are current
 * @param serial Serial of the output
 */
void box_audio_meter_job_sync(
    box_audio_module_t *state,
    uint32_t serial);

/** Free a measurement over a span.
 *
 * @param state The state
 */
void box_audio_meter_job_fini(
    box_audio_module_t *state);

/** Start a measurement of the levels of channels over a span of the output. A measurement that
 * waits to be read is dropped.
 *
 * @param world The world
 * @param channels Bus entities and the master to measure
 * @param count Number of channels
 * @param seconds Seconds of output to measure
 * @param skip Reads of the meters to skip first, at least 2 so the loudness meters apply
 * @return Zero on success, nonzero with an error logged
 */
int box_audio_meter_job_start(
    ecs_world_t *world,
    const ecs_entity_t *channels,
    int32_t count,
    double seconds,
    int32_t skip);

/** Get the measurement over a span.
 *
 * @param world The world
 * @return The measurement, NULL when box.audio is not imported
 */
const box_audio_meter_job_t* box_audio_meter_job_get(
    const ecs_world_t *world);

/** Drop the measurement over a span, measured or not, and turn its loudness meters off.
 *
 * @param world The world
 */
void box_audio_meter_job_clear(
    ecs_world_t *world);

/** Get the levels of a channel the module measured in the last frame, with or without an
 * AudioMeter.
 *
 * @param world The world
 * @param channel The master or a bus entity
 * @param meter Receives the levels, zero for an entity that is no channel
 * @return Whether the entity is a channel with a bus index
 */
bool box_audio_module_meter(
    const ecs_world_t *world,
    ecs_entity_t channel,
    AudioMeter *meter);

/** Register the sync system and the observers of the module.
 *
 * @param world The world
 * @param state The state, freed when the world is
 */
void box_audio_sync_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Make box.audio.synth, a child module of box.audio, the scope of new entities and Synth
 * their name prefix, creating it the first time.
 *
 * @param world The world
 * @return The scope before, for box_audio_synth_scope_pop
 */
ecs_entity_t box_audio_synth_scope_push(
    ecs_world_t *world);

/** Restore the scope of box_audio_synth_scope_push and the Audio name prefix.
 *
 * @param world The world
 * @param scope The scope before
 */
void box_audio_synth_scope_pop(
    ecs_world_t *world,
    ecs_entity_t scope);

/** Register the observers that mark a synth sound for a new compile when a component of it
 * or of one of its layers changes.
 *
 * @param world The world
 */
void box_audio_synth_init(
    ecs_world_t *world);

/** Compile a synth sound into a patch: the root and its children with a SynthOscillator as
 * layers, the root first. Logs one error per problem: too many layers, a layer that would
 * never end, an envelope longer than the core plays.
 *
 * @param world The world
 * @param state The state, for the counters
 * @param e The root, with SynthSound
 * @param synth What was compiled for it, receives the patch
 * @return Whether the patch differs from the one compiled before
 */
bool box_audio_synth_compile(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    AudioSynth *synth);

/** Look up the sound ids of the wavetables of the layers of a compiled synth sound in the
 * current output: the id of the Wavetable of each SynthTable.table, 0 while it has none.
 *
 * @param world The world
 * @param state The state
 * @param synth What was compiled for the sound, whose patch takes the ids
 * @return Whether an id of the patch changed
 */
bool box_audio_synth_tables(
    ecs_world_t *world,
    const box_audio_module_t *state,
    AudioSynth *synth);

/** Frames of a built-in wavetable.
 *
 * @param kind A WavetableKind
 * @return Frames, 1..BOX_AUDIO_TABLE_FRAMES_MAX
 */
int32_t box_audio_wavetable_frames(
    int32_t kind);

/** Build the mip levels of wavetable frames from their harmonics, in the layout the core reads
 * (BOX_AUDIO_TABLE_FLOATS): level k of each frame keeps the harmonics up to
 * BOX_AUDIO_TABLE_HARMONICS >> k, without DC, so a loaded table fits as well as a built-in one.
 *
 * @param frames Frames of the table, 1..BOX_AUDIO_TABLE_FRAMES_MAX
 * @param cosines Amplitude of the cosine of harmonic n of frame j at
 *        j (BOX_AUDIO_TABLE_HARMONICS + 1) + n, n 1..BOX_AUDIO_TABLE_HARMONICS
 * @param sines Amplitude of the sine of each harmonic, the same way
 * @return The floats, freed with ecs_os_free, NULL for an invalid count of frames
 */
float* box_audio_wavetable_build(
    int32_t frames,
    const double *cosines,
    const double *sines);

/** Samples per frame of the sound of a Wavetable: 0 for the default, else clamped.
 *
 * @param frame_size Wavetable.frame_size
 * @return Samples, BOX_AUDIO_WAVETABLE_FRAME_SIZE_MIN..BOX_AUDIO_WAVETABLE_FRAME_SIZE_MAX
 */
int32_t box_audio_wavetable_frame_size(
    int32_t frame_size);

/** Build a wavetable from the frames of a sound: cut into frames of frame_size samples, at
 * most BOX_AUDIO_TABLE_FRAMES_MAX, summed to mono, each without its DC, normalized to a peak
 * of 1 over all frames.
 *
 * A forward FFT band limits each frame to the harmonics under the Nyquist of a frame, at most
 * BOX_AUDIO_TABLE_HARMONICS; a frame_size that is not a power of 2 is resampled to the next
 * one first.
 *
 * @param data Frames of the sound, interleaved when it has more than one channel
 * @param frames Number of frames of the sound
 * @param channels Channels, 1 or more
 * @param frame_size Samples per frame, from box_audio_wavetable_frame_size
 * @param table_frames Receives the frames of the table
 * @param floats Receives the floats of the table, BOX_AUDIO_TABLE_FLOATS of its frames
 * @return The floats, freed with ecs_os_free, NULL when the sound is shorter than a frame
 */
float* box_audio_wavetable_load(
    const float *data,
    int32_t frames,
    int32_t channels,
    int32_t frame_size,
    int32_t *table_frames,
    int32_t *floats);

/** Build the table of a Wavetable: of its kind, or from its sound when it has one. A sound
 * that fails to load, is not a sound or is shorter than one frame logs one error that names
 * the Wavetable, one of more frames than a table holds one warning.
 *
 * @param world The world
 * @param e The Wavetable entity
 * @param table Its Wavetable
 * @param w What was sent for it
 * @param frames Receives the frames of the table
 * @param floats Receives the floats of the table
 * @param loading Receives whether the sound still decodes
 * @return The floats, freed with ecs_os_free, NULL for none
 */
float* box_audio_wavetable_of(
    ecs_world_t *world,
    ecs_entity_t e,
    const Wavetable *table,
    AudioWavetable *w,
    int32_t *frames,
    int32_t *floats,
    bool *loading);

/** Register the observers that build the Wavetables of a sound again when it becomes ready,
 * changes or is removed. Needs the query of the wavetables of the state.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_wavetable_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Build a built-in wavetable by additive synthesis.
 *
 * @param kind A WavetableKind
 * @param floats Receives the floats of the table, BOX_AUDIO_TABLE_FLOATS of its frames
 * @return The floats, freed with ecs_os_free
 */
float* box_audio_wavetable_kind(
    int32_t kind,
    int32_t *floats);

/** Write a command that sets a parameter of a bus.
 *
 * @param state The state
 * @param bus Index of the bus
 * @param param The parameter
 * @param value The value
 */
void box_audio_bus_set(
    box_audio_module_t *state,
    uint32_t bus,
    box_audio_param_t param,
    float value);

/** Give buses their indices, notice route changes and note the solos and the solo safe buses.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_buses(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Compute the buses the solo keeps: without a solo all of them, else the soloed buses, the
 * buses that feed them, the returns and SFX buses, and every bus downstream of these. Walks the
 * routes of the last compile until nothing changes, so a cycle ends too.
 *
 * @param state The state
 */
void box_audio_solo(
    box_audio_module_t *state);

/** Send the parameters of the buses that changed, the solo in their mute.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_buses_send(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Compile the buses into a processing order: every bus before its output and its sends, the
 * deepest first, the master last. A cycle logs an error and keeps the last valid order.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_compile(
    ecs_world_t *world,
    box_audio_module_t *state);

#endif
