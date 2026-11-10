/**
 * @file audio/core/core.h
 * @brief The audio core: a mixer of voices and buses, driven by a byte stream of commands.
 *
 * Plain C without flecs, box or platform code, so the same sources build into the main module
 * and into the standalone wasm the web build runs in an AudioWorklet. Rendering does not
 * allocate, lock, log or wait. See docs/audio.md for the commands and the transports.
 */

#ifndef BOX_AUDIO_CORE_H
#define BOX_AUDIO_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "commands.h"
#include "effect_params.h"
#include "modulation.h"
#include "patch.h"

/** The levels of a bus since the last box_audio_core_meters, measured at its output: after
 * its inserts, width, pan, volume and mute, what it sends on. The master is measured before the
 * limiter */
typedef struct box_audio_meter_t {
    float peak_left;      /**< Largest absolute sample of the left channel */
    float peak_right;     /**< Largest absolute sample of the right channel */
    double square_left;   /**< Sum of the squares of the left samples */
    double square_right;  /**< Sum of the squares of the right samples */
    uint32_t frames;      /**< Frames the sums hold, 0 for a bus that did not render */
    float loudness;       /**< Sum of the K-weighted squares of both channels, 0 unless the
                           *   bus has BOX_AUDIO_PARAM_LOUDNESS on */
} box_audio_meter_t;

/** The values of an effect since the last box_audio_core_effect_meters, which the kinds that
 * have them fill */
typedef struct box_audio_effect_meter_t {
    float reduction;      /**< Largest gain reduction in dB, 0 for none or a kind without */
} box_audio_effect_meter_t;

/** Events the core reports to its transport */
typedef enum box_audio_event_type_t {
    BOX_AUDIO_EVENT_VOICE_END = 1,      /**< A voice ended, id is its handle; value is 0, or
                                         * the refusals so far for a play the priorities
                                         * refused */
    BOX_AUDIO_EVENT_VOICE_STOLEN = 2,   /**< The cap or a play short of synth parts stole
                                         * a voice, id is its handle, value the steals so
                                         * far */
    BOX_AUDIO_EVENT_SOUND_FREE = 3,     /**< No voice uses a released sound, id is the sound */
    BOX_AUDIO_EVENT_ORDER_REFUSED = 4,  /**< An invalid order was ignored, value its count */
    BOX_AUDIO_EVENT_IGNORED = 5,        /**< A command was ignored, id is its type */
    BOX_AUDIO_EVENT_LOST = 6,           /**< The ring was full, value is the events lost since
                                         * the last LOST event was read; the last event of
                                         * the ring when it is full */
    BOX_AUDIO_EVENT_LATE = 7            /**< A command with a future when found the schedule
                                         * full and applied at once, id is its type */
} box_audio_event_type_t;

/** An event of the core */
typedef struct box_audio_event_t {
    int32_t type;    /**< A box_audio_event_type_t */
    uint32_t id;     /**< Voice handle, sound id or command type */
    int32_t value;   /**< Extra value of the event */
} box_audio_event_t;

/** How to create a core */
typedef struct box_audio_core_desc_t {
    float sample_rate;  /**< Frames per second of the output, 48000 when not positive */
    void* (*alloc)(size_t size, void *ctx);  /**< Allocator, malloc when NULL on native */
    void (*free)(void *ptr, void *ctx);      /**< Free of the allocator, free when NULL */
    void *ctx;          /**< Context of the allocator */
} box_audio_core_desc_t;

/** A core, opaque */
typedef struct box_audio_core_t box_audio_core_t;

/** A buffer that commands are written into, owned by the caller */
typedef struct box_audio_writer_t {
    uint8_t *data;     /**< The bytes */
    int32_t size;      /**< Bytes written */
    int32_t capacity;  /**< Bytes the buffer holds */
} box_audio_writer_t;

/** Create a core. The only allocation of the core, besides box_audio_core_free.
 *
 * @param desc How to create it
 * @return The core, NULL when the allocation failed
 */
box_audio_core_t* box_audio_core_new(
    const box_audio_core_desc_t *desc);

/** Free a core. The sounds it knows stay with their owner.
 *
 * @param core The core, may be NULL
 */
void box_audio_core_free(
    box_audio_core_t *core);

/** Get the sample rate of a core.
 *
 * @param core The core
 * @return Frames per second
 */
float box_audio_core_sample_rate(
    const box_audio_core_t *core);

/** Give the core a sound, on the thread that renders, between renders.
 *
 * The data is in the address space of the core and stays with the side that runs the core;
 * BOX_AUDIO_EVENT_SOUND_FREE after a release says when it may be freed.
 *
 * A mono sound of BOX_AUDIO_TABLE_FLOATS(n) frames that starts with the header of a wavetable
 * is a wavetable of n frames that synth layers name by its id. A synth voice that plays it
 * counts as a voice of the sound, so a released table is freed once no voice plays it.
 *
 * @param core The core
 * @param id Id of the sound, below BOX_AUDIO_SOUND_MAX and not in use
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param sample_rate Frames per second of the sound, the rate of the core when not positive
 * @return Whether the core took the sound
 */
bool box_audio_core_sound(
    box_audio_core_t *core,
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate);

/** Queue a chunk of a stream, on the thread that renders, between renders.
 *
 * The data is in the address space of the core and stays with the side that runs the core; the
 * consumed count of box_audio_core_stream_status says when it may be freed, and so does a stream
 * that is no longer open. Does not allocate.
 *
 * @param core The core
 * @param stream Index of an open stream
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames, at most BOX_AUDIO_STREAM_CHUNK
 * @param last Whether the stream ends with this chunk
 * @return Whether the core took the chunk; false when the stream is not open, its ring is
 *         full or its last chunk came already
 */
bool box_audio_core_stream_chunk(
    box_audio_core_t *core,
    uint32_t stream,
    const float *data,
    int32_t frames,
    bool last);

/** Get what a stream does. Does not allocate.
 *
 * @param core The core
 * @param stream Index of the stream
 * @param status Receives the status, zero for an invalid stream
 */
void box_audio_core_stream_status(
    const box_audio_core_t *core,
    uint32_t stream,
    box_audio_stream_status_t *status);

/** Apply a batch of commands.
 *
 * They take effect at the start of the next render block, but a timed command with a future
 * when waits in the schedule and takes effect on exactly its frame: the render splits there.
 *
 * Commands of an unknown type are skipped by their size; a truncated command ends the batch.
 * Does not allocate.
 *
 * @param core The core
 * @param commands The bytes
 * @param size Number of bytes
 */
void box_audio_core_apply(
    box_audio_core_t *core,
    const void *commands,
    int32_t size);

/** Render frames into the left and right channel. Does not allocate, lock, log or wait.
 *
 * Advances the clock by frames. The commands of the schedule whose frame is reached apply at
 * that frame, whatever the render sizes.
 *
 * @param core The core
 * @param left Receives frames samples of the left channel
 * @param right Receives frames samples of the right channel
 * @param frames Number of frames, any count
 */
void box_audio_core_render(
    box_audio_core_t *core,
    float *left,
    float *right,
    int32_t frames);

/** Take the events of the core, oldest first.
 *
 * A BOX_AUDIO_EVENT_SOUND_FREE that found the ring full is reported once there is room again,
 * in this call or a later one. Does not allocate.
 *
 * @param core The core
 * @param events Receives the events
 * @param max Room in events
 * @return Number of events written
 */
int32_t box_audio_core_read_events(
    box_audio_core_t *core,
    box_audio_event_t *events,
    int32_t max);

/** Get the beat of the beat clock of a core at a frame (box_audio_cmd_beat_t): the beat of the
 * last BEAT command that applied, moved on at its tempo. A frame before that command reads the
 * same line backwards.
 *
 * Frames are multiplied by the tempo before the division by the rate, so a beat on a whole
 * frame comes out exact and the edges of a step fall on the same frames every time.
 *
 * @param core The core
 * @param frame A frame of the core clock
 * @return The beat
 */
double box_audio_core_beat(
    const box_audio_core_t *core,
    uint64_t frame);

/** Get the tempo of the beat clock of a core.
 *
 * @param core The core
 * @return Beats per second
 */
double box_audio_core_tempo(
    const box_audio_core_t *core);

/** Get the maps of macros of a core that run or fade out.
 *
 * @param core The core
 * @return Number of maps
 */
int32_t box_audio_core_macro_maps(
    const box_audio_core_t *core);

/** Get the modulators of a core that run or fade out.
 *
 * @param core The core
 * @return Number of modulators
 */
int32_t box_audio_core_modulators(
    const box_audio_core_t *core);

/** Get the clock of a core: the frames it rendered since it was created.
 *
 * @param core The core
 * @return Number of frames, 0 for a new core
 */
uint64_t box_audio_core_clock(
    const box_audio_core_t *core);

/** Get the number of commands that wait in the schedule for their frame.
 *
 * @param core The core
 * @return Number of commands
 */
int32_t box_audio_core_scheduled(
    const box_audio_core_t *core);

/** Get the number of events lost since the last BOX_AUDIO_EVENT_LOST was read.
 *
 * @param core The core
 * @return Number of events, 0 when none were lost
 */
int32_t box_audio_core_events_lost(
    const box_audio_core_t *core);

/** Get the number of voices that play, without the ones that fade out.
 *
 * @param core The core
 * @return Number of voices
 */
int32_t box_audio_core_voice_count(
    const box_audio_core_t *core);

/** Get the number of plays refused so far because every playing voice had a higher priority.
 *
 * @param core The core
 * @return Number of plays, legatos and streams
 */
int32_t box_audio_core_refused(
    const box_audio_core_t *core);

/** Take the levels of the buses since the last call and start them over. Every bus in the
 * order is measured per block at its output, a compare and a multiply-add per sample and
 * channel. Does not allocate.
 *
 * @param core The core
 * @param meters Receives the levels of buses 0 to max - 1, by index
 * @param max Room in meters, at most BOX_AUDIO_BUS_MAX are written
 * @return Number of meters written
 */
int32_t box_audio_core_meters(
    box_audio_core_t *core,
    box_audio_meter_t *meters,
    int32_t max);

/** Take the values of the effects since the last call and start them over. Does not
 * allocate.
 *
 * @param core The core
 * @param meters Receives the values of effect slots 0 to max - 1
 * @param max Room in meters, at most BOX_AUDIO_EFFECT_MAX are written
 * @return Number of meters written
 */
int32_t box_audio_core_effect_meters(
    box_audio_core_t *core,
    box_audio_effect_meter_t *meters,
    int32_t max);

/** Get the floats of memory an effect of a kind needs at a sample rate.
 *
 * It sizes the lines of a kind for its longest setting: for the delay BOX_AUDIO_DELAY_MAX
 * seconds of both channels, for the chorus BOX_AUDIO_CHORUS_MAX milliseconds, and so on.
 *
 * @param kind A box_audio_effect_kind_t
 * @param sample_rate Frames per second of the core
 * @return Number of floats, 0 for a kind that needs none
 */
int32_t box_audio_effect_floats(
    uint32_t kind,
    float sample_rate);

/** Give an effect slot its memory, on the thread that renders, between renders.
 *
 * The memory is in the address space of the core and stays with the side that runs the core,
 * which frees it after the core. The core clears it.
 *
 * A slot takes memory when it has none, or larger memory for a kind that needs more; the
 * effect then starts over on it, and the old memory is kept until the core is freed.
 *
 * @param core The core
 * @param slot Slot of the effect, below BOX_AUDIO_EFFECT_MAX
 * @param data The memory
 * @param floats Floats of the memory
 * @return Whether the core took the memory
 */
bool box_audio_core_effect_memory(
    box_audio_core_t *core,
    uint32_t slot,
    float *data,
    int32_t floats);

/** Check whether an effect renders: it has a kind, the memory of its kind, a bus or a tail
 * that fades, and, for a kind with a tail, a tail that has not decayed to silence or a signal
 * that comes in.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @return Whether it renders
 */
bool box_audio_core_effect_running(
    const box_audio_core_t *core,
    uint32_t slot);

/** Start a writer on a buffer of the caller.
 *
 * @param writer The writer
 * @param data The buffer
 * @param capacity Bytes of the buffer
 */
void box_audio_writer_init(
    box_audio_writer_t *writer,
    void *data,
    int32_t capacity);

/** Write a play command, at velocity 1.
 *
 * @param writer The writer
 * @param voice Handle of the new voice
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param flags BOX_AUDIO_PLAY_ flags
 * @param when Frame of the core clock of its first sample, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_play(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    uint32_t flags,
    uint64_t when);

/** Write a play command with a velocity and a priority.
 *
 * @param writer The writer
 * @param voice Handle of the new voice
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param velocity Velocity of the note, 0..1, 1 for a play that is not a note
 * @param flags BOX_AUDIO_PLAY_ flags
 * @param priority Priority of the voice, 0..BOX_AUDIO_PRIORITY_MAX
 * @param when Frame of the core clock of its first sample, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_play_priority(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    float velocity,
    uint32_t flags,
    uint32_t priority,
    uint64_t when);

/** Write a play command of a note: with a velocity, a priority and the beat and tempo of the
 * Transport at its frame, which synced synth LFOs lock to.
 *
 * @param writer The writer
 * @param voice Handle of the new voice
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param velocity Velocity of the note, 0..1, 1 for a play that is not a note
 * @param beat Beat at its first sample, wrapped into 0..BOX_AUDIO_BEAT_WRAP
 * @param tempo Beats per second, 0 for BOX_AUDIO_SYNTH_TEMPO
 * @param flags BOX_AUDIO_PLAY_ flags
 * @param priority Priority of the voice, 0..BOX_AUDIO_PRIORITY_MAX
 * @param when Frame of the core clock of its first sample, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_play_note(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    float velocity,
    float beat,
    float tempo,
    uint32_t flags,
    uint32_t priority,
    uint64_t when);

/** Write a legato command at velocity 1: a note that follows the voice of a handle.
 *
 * @param writer The writer
 * @param voice New handle of the voice
 * @param from Handle of the voice the note follows
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param flags BOX_AUDIO_PLAY_ flags of a new voice
 * @return Whether the command fit
 */
bool box_audio_write_legato(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t from,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    uint32_t flags);

/** Write a legato command at a frame and velocity 1: a note that follows the voice of a
 * handle.
 *
 * @param writer The writer
 * @param voice New handle of the voice
 * @param from Handle of the voice the note follows
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param flags BOX_AUDIO_PLAY_ flags
 * @param when Frame of the core clock of the note, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_legato_at(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t from,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    uint32_t flags,
    uint64_t when);

/** Write a legato command at a frame with a velocity and a priority.
 *
 * @param writer The writer
 * @param voice New handle of the voice
 * @param from Handle of the voice the note follows
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param velocity Velocity of the note, 0..1, of a new voice; a retargeted voice keeps its own
 * @param flags BOX_AUDIO_PLAY_ flags
 * @param priority Priority of the voice, 0..BOX_AUDIO_PRIORITY_MAX
 * @param when Frame of the core clock of the note, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_legato_priority(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t from,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    float velocity,
    uint32_t flags,
    uint32_t priority,
    uint64_t when);

/** Write a legato command of a note: with a velocity, a priority and the beat and tempo of the
 * Transport at its frame, which a new voice locks its synced LFOs to.
 *
 * @param writer The writer
 * @param voice New handle of the voice
 * @param from Handle of the voice the note follows
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param velocity Velocity of the note, 0..1, of a new voice; a retargeted voice keeps its own
 * @param beat Beat of the note, wrapped into 0..BOX_AUDIO_BEAT_WRAP
 * @param tempo Beats per second, 0 for BOX_AUDIO_SYNTH_TEMPO
 * @param flags BOX_AUDIO_PLAY_ flags
 * @param priority Priority of the voice, 0..BOX_AUDIO_PRIORITY_MAX
 * @param when Frame of the core clock of the note, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_legato_note(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t from,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pan,
    float pitch,
    float velocity,
    float beat,
    float tempo,
    uint32_t flags,
    uint32_t priority,
    uint64_t when);

/** Write a release command, the end of the note of a voice.
 *
 * @param writer The writer
 * @param voice Handle of the voice
 * @param fade Seconds of the fade of a sample voice
 * @param when Frame of the core clock of the release, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_voice_release(
    box_audio_writer_t *writer,
    uint32_t voice,
    float fade,
    uint64_t when);

/** Write a stop command.
 *
 * @param writer The writer
 * @param voice Handle of the voice
 * @param when Frame of the core clock its fade starts at, 0 for now, which also drops the
 *     scheduled commands of the handle
 * @return Whether the command fit
 */
bool box_audio_write_stop(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint64_t when);

/** Write a command that sets a parameter of a voice.
 *
 * @param writer The writer
 * @param voice Handle of the voice
 * @param param A box_audio_param_t
 * @param value The value
 * @param when Frame of the core clock its ramp starts at, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_voice_set(
    box_audio_writer_t *writer,
    uint32_t voice,
    box_audio_param_t param,
    float value,
    uint64_t when);

/** Write a command that sets a parameter of a bus.
 *
 * @param writer The writer
 * @param bus Index of the bus
 * @param param A box_audio_param_t
 * @param value The value
 * @param when Frame of the core clock its ramp starts at, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_bus_set(
    box_audio_writer_t *writer,
    uint32_t bus,
    box_audio_param_t param,
    float value,
    uint64_t when);

/** Write a command that replaces the processing order.
 *
 * @param writer The writer
 * @param routes The buses in processing order, the master last
 * @param count Number of routes
 * @return Whether the command fit
 */
bool box_audio_write_order(
    box_audio_writer_t *writer,
    const box_audio_route_t *routes,
    int32_t count);

/** Write a command that opens a stream and starts a voice that reads it.
 *
 * @param writer The writer
 * @param voice Handle of the new voice
 * @param stream Index of the stream
 * @param bus Bus of the voice
 * @param volume Volume
 * @param channels 1 or 2
 * @param sample_rate Frames per second of the chunks
 * @param priority Priority of the voice, 0..BOX_AUDIO_PRIORITY_MAX
 * @return Whether the command fit
 */
bool box_audio_write_stream(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t stream,
    uint32_t bus,
    float volume,
    uint32_t channels,
    float sample_rate,
    uint32_t priority);

/** Write a command that releases a sound.
 *
 * @param writer The writer
 * @param sound Id of the sound
 * @return Whether the command fit
 */
bool box_audio_write_release(
    box_audio_writer_t *writer,
    uint32_t sound);

/** Write a command that puts an effect into an insert slot, changes it, moves it or takes it
 * off.
 *
 * @param writer The writer
 * @param slot Slot of the effect
 * @param kind A box_audio_effect_kind_t, BOX_AUDIO_EFFECT_NONE to take it off
 * @param bus Bus of the effect, BOX_AUDIO_BUS_NONE to take it off
 * @param index Insert slot within the bus
 * @param key Sidechain bus, BOX_AUDIO_BUS_NONE for none
 * @param params BOX_AUDIO_EFFECT_PARAMS parameters, NULL for all 0
 * @return Whether the command fit
 */
bool box_audio_write_effect(
    box_audio_writer_t *writer,
    uint32_t slot,
    uint32_t kind,
    uint32_t bus,
    uint32_t index,
    uint32_t key,
    const float *params);

/** Write a command that replaces the parameters of the effect in a slot at a frame.
 *
 * @param writer The writer
 * @param slot Slot of the effect
 * @param kind The kind the slot must hold
 * @param params BOX_AUDIO_EFFECT_PARAMS parameters
 * @param when Frame of the core clock it applies at, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_effect_params(
    box_audio_writer_t *writer,
    uint32_t slot,
    uint32_t kind,
    const float *params,
    uint64_t when);

/** Write a command that moves a parameter of a voice to a value over a number of frames.
 *
 * @param writer The writer
 * @param voice Handle of the voice
 * @param param BOX_AUDIO_PARAM_BEND, BOX_AUDIO_PARAM_MOD or BOX_AUDIO_PARAM_PRESSURE
 * @param value Value at the end of the ramp
 * @param frames Frames of the ramp, 0 to jump
 * @param when Frame of the core clock the ramp starts at, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_voice_ramp(
    box_audio_writer_t *writer,
    uint32_t voice,
    box_audio_param_t param,
    float value,
    uint32_t frames,
    uint64_t when);

/** Write a command that drops the scheduled sets of a parameter of a bus, the parameters of
 * an effect slot or the ramps of a voice.
 *
 * @param writer The writer
 * @param type BOX_AUDIO_CMD_BUS_SET, BOX_AUDIO_CMD_EFFECT_PARAMS, BOX_AUDIO_CMD_VOICE_RAMP or
 *        BOX_AUDIO_CMD_MACRO
 * @param target Index of the bus, slot of the effect or of the macro, or handle of the voice
 * @param param The box_audio_param_t of a bus, unused for an effect
 * @return Whether the command fit
 */
bool box_audio_write_cancel(
    box_audio_writer_t *writer,
    uint32_t type,
    uint32_t target,
    uint32_t param);

/** Write a command that sets the beat clock of the core at a frame.
 *
 * @param writer The writer
 * @param beat The beat of the Transport at the frame
 * @param tempo Beats per second from the frame on, 0 for BOX_AUDIO_SYNTH_TEMPO
 * @param when Frame of the beat, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_beat(
    box_audio_writer_t *writer,
    double beat,
    float tempo,
    uint64_t when);

/** Write a modulator command; the writer fills its header.
 *
 * @param writer The writer
 * @param cmd The command
 * @return Whether the command fit
 */
bool box_audio_write_modulator(
    box_audio_writer_t *writer,
    const box_audio_cmd_modulator_t *cmd);

/** Write a command that sets the value of a macro at a frame.
 *
 * @param writer The writer
 * @param macro Slot of the macro
 * @param value The value, 0..1
 * @param when Frame of the core clock it applies at, 0 for now
 * @return Whether the command fit
 */
bool box_audio_write_macro(
    box_audio_writer_t *writer,
    uint32_t macro,
    float value,
    uint64_t when);

/** Write a map command of a macro; the writer fills its header.
 *
 * @param writer The writer
 * @param cmd The command
 * @return Whether the command fit
 */
bool box_audio_write_macro_map(
    box_audio_writer_t *writer,
    const box_audio_cmd_macro_map_t *cmd);

/** Write a command that makes a sound id a synth patch or replaces its patch, of
 * BOX_AUDIO_CMD_SYNTH_SIZE bytes for the layers of the patch.
 *
 * @param writer The writer
 * @param sound Id of the sound
 * @param patch The patch
 * @return Whether the command fit
 */
bool box_audio_write_synth(
    box_audio_writer_t *writer,
    uint32_t sound,
    const box_audio_synth_patch_t *patch);

/** Write raw bytes, for commands the writer has no function for.
 *
 * @param writer The writer
 * @param data The bytes
 * @param size Number of bytes
 * @return Whether the bytes fit
 */
bool box_audio_write_bytes(
    box_audio_writer_t *writer,
    const void *data,
    int32_t size);

/** Sine of a phase in turns, without libm, so the wasm of the core needs no imports.
 *
 * @param turns The phase in turns, one turn is 2 pi
 * @return The sine, within 1e-6 of sin(2 pi turns)
 */
float box_audio_core_sin(
    double turns);

#endif
