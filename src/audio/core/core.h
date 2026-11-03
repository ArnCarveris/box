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

/** Frames the web renders per call, the render quantum of an AudioWorklet */
#define BOX_AUDIO_CORE_QUANTUM 128

/** Frames the core mixes at once; a render of more frames runs in blocks of this size */
#define BOX_AUDIO_CORE_BLOCK 128

/** Seconds a change of a volume, pan, send level or mute takes, so changes do not click */
#define BOX_AUDIO_CORE_RAMP 0.005f

/** Seconds a stolen voice takes to fade out */
#define BOX_AUDIO_CORE_STEAL_FADE 0.002f

/** Voices that play at once, the voice cap */
#define BOX_AUDIO_VOICE_MAX 64

/** Extra voice slots for voices that fade out after a stop or a steal */
#define BOX_AUDIO_VOICE_TAIL_MAX 16

/** Buses of a core; bus 0 is the master */
#define BOX_AUDIO_BUS_MAX 32

/** Sounds a core knows at once; a sound id is below this */
#define BOX_AUDIO_SOUND_MAX 1024

/** Events a core holds until read_events. The last slot is kept for BOX_AUDIO_EVENT_LOST:
 * newer events are lost when the others are full, and that event counts them.
 * BOX_AUDIO_EVENT_SOUND_FREE is never lost; it waits in the core until there is room */
#define BOX_AUDIO_EVENT_MAX 256

/** Largest volume of a voice or a bus */
#define BOX_AUDIO_VOLUME_MAX 4.0f

/** Smallest and largest pitch ratio of a voice */
#define BOX_AUDIO_PITCH_MIN (1.0f / 64.0f)
#define BOX_AUDIO_PITCH_MAX 16.0f

/** Level over which the limiter of the master reduces the gain; quieter signals pass as is */
#define BOX_AUDIO_LIMITER_THRESHOLD 0.95f

/** Seconds the limiter takes to return from full reduction to a gain of 1 */
#define BOX_AUDIO_LIMITER_RELEASE 0.1f

/** No bus, for the output of the master and a bus without a send */
#define BOX_AUDIO_BUS_NONE 0xFF

/** Streams a core reads at once: one that plays and one that fades out after a change */
#define BOX_AUDIO_STREAM_MAX 2

/** Chunks the ring of a stream holds */
#define BOX_AUDIO_STREAM_CHUNKS 32

/** Frames of a chunk at most */
#define BOX_AUDIO_STREAM_CHUNK 4096

/** Seconds of the fade of a stream that runs out of chunks, and of its recovery */
#define BOX_AUDIO_STREAM_FADE 0.005f
/** Reverbs that run at once; a reverb is addressed by a slot below this */
#define BOX_AUDIO_REVERB_MAX 1

/** Delay lines of the feedback delay network of a reverb */
#define BOX_AUDIO_REVERB_LINES 8

/** Longest predelay of a reverb in seconds */
#define BOX_AUDIO_REVERB_PREDELAY_MAX 0.5f

/** Shortest and longest decay of a reverb in seconds, the time until -60 dB */
#define BOX_AUDIO_REVERB_DECAY_MIN 0.05f
#define BOX_AUDIO_REVERB_DECAY_MAX 60.0f

/** Seconds of the fade of the wet signal around a change of size or predelay, out and in */
#define BOX_AUDIO_REVERB_FADE 0.1f

/** Oscillators of a synth patch, each with an envelope, mixed into one filter and an amp */
#define BOX_AUDIO_SYNTH_LAYERS 4

/** Synth patches a core holds at once */
#define BOX_AUDIO_SYNTH_MAX 256

/** Longest pass of a synth envelope in seconds; a longer one loses its hold first */
#define BOX_AUDIO_SYNTH_LENGTH_MAX 60.0f

/** Shortest attack, and shortest decay and release that change the level, in seconds, so an
 * envelope never jumps */
#define BOX_AUDIO_SYNTH_EDGE 0.0005f

/** Highest frequency of an oscillator in Hz, the top of the audible range */
#define BOX_AUDIO_SYNTH_FREQUENCY_MAX 20000.0f

/** Largest index of phase modulation in radians, before the limit against the frequency */
#define BOX_AUDIO_SYNTH_INDEX_MAX 32.0f

/** Largest feedback of an oscillator onto its own phase in radians, under the point where the
 * tone turns to noise */
#define BOX_AUDIO_SYNTH_FEEDBACK_MAX 1.5f

/** Commands of the byte stream. Each starts with a box_audio_cmd_header_t */
typedef enum box_audio_cmd_type_t {
    BOX_AUDIO_CMD_PLAY = 1,          /**< box_audio_cmd_play_t */
    BOX_AUDIO_CMD_STOP = 2,          /**< box_audio_cmd_stop_t */
    BOX_AUDIO_CMD_VOICE_SET = 3,     /**< box_audio_cmd_set_t on a voice */
    BOX_AUDIO_CMD_BUS_SET = 4,       /**< box_audio_cmd_set_t on a bus */
    BOX_AUDIO_CMD_ORDER = 5,         /**< box_audio_cmd_order_t and its routes */
    BOX_AUDIO_CMD_SOUND_RELEASE = 6, /**< box_audio_cmd_release_t */
    BOX_AUDIO_CMD_REVERB = 7,        /**< box_audio_cmd_reverb_t */
    BOX_AUDIO_CMD_SYNTH = 8,         /**< box_audio_cmd_synth_t */
    BOX_AUDIO_CMD_STREAM = 9         /**< box_audio_cmd_stream_t */
} box_audio_cmd_type_t;

/** Parameters of BOX_AUDIO_CMD_VOICE_SET and BOX_AUDIO_CMD_BUS_SET */
typedef enum box_audio_param_t {
    BOX_AUDIO_PARAM_VOLUME = 0,  /**< Volume, 0..BOX_AUDIO_VOLUME_MAX, ramped */
    BOX_AUDIO_PARAM_PAN = 1,     /**< Pan, -1 left .. 1 right, equal power, ramped */
    BOX_AUDIO_PARAM_PITCH = 2,   /**< Pitch ratio of a voice */
    BOX_AUDIO_PARAM_BUS = 3,     /**< Bus a voice plays into */
    BOX_AUDIO_PARAM_MUTE = 4,    /**< Mute of a bus, nonzero mutes, ramped */
    BOX_AUDIO_PARAM_SEND = 5,    /**< Level of the send of a bus, 0..BOX_AUDIO_VOLUME_MAX */
    BOX_AUDIO_PARAM_POSITION = 6 /**< Read position of a voice in frames of its sound, wrapped
                                  *   into a looping sound, at once */
} box_audio_param_t;

/** Flags of BOX_AUDIO_CMD_PLAY */
#define BOX_AUDIO_PLAY_LOOP (1u << 0)

/** Header of every command: its type and its size in bytes, header included */
typedef struct box_audio_cmd_header_t {
    uint16_t type;  /**< A box_audio_cmd_type_t; an unknown type is skipped */
    uint16_t size;  /**< Bytes of the command, a multiple of 4, at least 4 */
} box_audio_cmd_header_t;

/** Start a voice that plays a sound */
typedef struct box_audio_cmd_play_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_PLAY */
    uint32_t voice;                 /**< Handle the sender picks to address the voice */
    uint32_t sound;                 /**< Id of the sound */
    uint32_t bus;                   /**< Bus the voice plays into */
    float volume;                   /**< Volume, ramped from 0 so the start does not click */
    float pan;                      /**< Pan, -1..1 */
    float pitch;                    /**< Pitch ratio, 1 plays the sound as is */
    uint32_t flags;                 /**< BOX_AUDIO_PLAY_ flags */
} box_audio_cmd_play_t;

/** Stop a voice: it fades out and ends */
typedef struct box_audio_cmd_stop_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_STOP */
    uint32_t voice;                 /**< Handle of the voice */
} box_audio_cmd_stop_t;

/** Set a parameter of a voice or a bus */
typedef struct box_audio_cmd_set_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_VOICE_SET or BOX_AUDIO_CMD_BUS_SET */
    uint32_t target;                /**< Handle of the voice or index of the bus */
    uint32_t param;                 /**< A box_audio_param_t */
    float value;                    /**< The new value */
} box_audio_cmd_set_t;

/** A bus in the processing order, where it outputs and where it sends */
typedef struct box_audio_route_t {
    uint8_t bus;     /**< Index of the bus */
    uint8_t output;  /**< Bus the bus outputs to, BOX_AUDIO_BUS_NONE for the master */
    uint8_t send;    /**< Bus of the send, BOX_AUDIO_BUS_NONE for none */
    uint8_t unused;  /**< Padding, 0 */
} box_audio_route_t;

/** Replace the processing order, followed by count box_audio_route_t.
 *
 * The master comes last and outputs nowhere; every other bus outputs and sends only to buses
 * after it, so the routes have no cycle. A bus not in the order is silent.
 */
typedef struct box_audio_cmd_order_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_ORDER */
    uint32_t count;                 /**< Routes that follow, 1..BOX_AUDIO_BUS_MAX */
} box_audio_cmd_order_t;

/** Release a sound: once no voice plays it, BOX_AUDIO_EVENT_SOUND_FREE tells the owner */
typedef struct box_audio_cmd_release_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_SOUND_RELEASE */
    uint32_t sound;                 /**< Id of the sound */
} box_audio_cmd_release_t;

/** Open a stream and start a voice that reads it. The chunks of the stream come with
 * box_audio_core_stream_chunk; a stop fades the voice out and closes the stream */
typedef struct box_audio_cmd_stream_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_STREAM */
    uint32_t voice;                 /**< Handle the sender picks to address the voice */
    uint32_t stream;                /**< Index of the stream, below BOX_AUDIO_STREAM_MAX, closed */
    uint32_t bus;                   /**< Bus the voice plays into */
    float volume;                   /**< Volume, ramped from 0 so the start does not click */
    uint32_t channels;              /**< 1 or 2, interleaved in the chunks */
    float sample_rate;              /**< Frames per second of the chunks, the rate of the core
                                     * when not positive */
} box_audio_cmd_stream_t;

/** What a stream does, for the side that owns its chunks */
typedef struct box_audio_stream_status_t {
    uint32_t voice;      /**< Handle of the voice that opened the stream last, 0 for none */
    int32_t open;        /**< 1 while that voice reads the stream; once 0 the core holds none of
                          * its chunks */
    int32_t ended;       /**< 1 when the voice read the last chunk to its end */
    uint32_t consumed;   /**< Chunks read to their end since the stream opened, oldest first;
                          * the core holds none of them any more */
    int32_t at;          /**< Frames read of the oldest chunk that is not consumed */
    int32_t underruns;   /**< Times the stream ran out of chunks since it opened */
    int32_t starved;     /**< 1 while it plays silence for want of chunks */
} box_audio_stream_status_t;
/** The parameters of a reverb */
typedef struct box_audio_reverb_params_t {
    float decay;     /**< Seconds until -60 dB, BOX_AUDIO_REVERB_DECAY_MIN..MAX */
    float damping;   /**< High frequency loss, 0..1 */
    float predelay;  /**< Seconds before the first reflection, 0..BOX_AUDIO_REVERB_PREDELAY_MAX */
    float size;      /**< Room scale, 0..1, which sets the lengths of the delay lines */
    float mix;       /**< Wet level, 0..1; the dry signal passes at 1 - mix */
} box_audio_reverb_params_t;

/** Put a reverb on a bus, change its parameters or take it off.
 *
 * Decay, damping and mix change smoothly. A change of size, predelay or bus fades the wet
 * signal out, swaps and fades it in. Bus BOX_AUDIO_BUS_NONE takes the reverb off: its tail
 * fades out into the master. A reverb runs once the memory of its slot arrived.
 */
typedef struct box_audio_cmd_reverb_t {
    box_audio_cmd_header_t header;     /**< BOX_AUDIO_CMD_REVERB */
    uint32_t slot;                     /**< Slot of the reverb, below BOX_AUDIO_REVERB_MAX */
    uint32_t bus;                      /**< Bus whose signal it processes, or BUS_NONE */
    box_audio_reverb_params_t params;  /**< The parameters, clamped by the core */
} box_audio_cmd_reverb_t;

/** Waves of a synth oscillator */
typedef enum box_audio_synth_wave_t {
    BOX_AUDIO_SYNTH_SINE = 0,      /**< Sine */
    BOX_AUDIO_SYNTH_SAW = 1,       /**< Saw, band limited */
    BOX_AUDIO_SYNTH_SQUARE = 2,    /**< Square, band limited */
    BOX_AUDIO_SYNTH_TRIANGLE = 3,  /**< Triangle */
    BOX_AUDIO_SYNTH_NOISE = 4      /**< White noise, seeded per play */
} box_audio_synth_wave_t;

/** Kinds of the filter of a synth patch */
typedef enum box_audio_synth_filter_t {
    BOX_AUDIO_SYNTH_FILTER_NONE = 0,  /**< No filter */
    BOX_AUDIO_SYNTH_LOWPASS = 1,      /**< Lowpass, 12 dB per octave */
    BOX_AUDIO_SYNTH_HIGHPASS = 2,     /**< Highpass, 12 dB per octave */
    BOX_AUDIO_SYNTH_BANDPASS = 3      /**< Bandpass, unity at the cutoff */
} box_audio_synth_filter_t;

/** How a synth oscillator that modulates another one changes it */
typedef enum box_audio_synth_modulation_t {
    BOX_AUDIO_SYNTH_PHASE = 0,  /**< Adds to the phase of the carrier, FM as DX synths do it */
    BOX_AUDIO_SYNTH_RING = 1    /**< Multiplies the carrier */
} box_audio_synth_modulation_t;

/** An envelope of a synth patch: a linear attack to 1, a decay to the sustain level, a hold
 * there and a release to 0 */
typedef struct box_audio_synth_envelope_t {
    float attack;   /**< Seconds from 0 to 1 */
    float decay;    /**< Seconds from 1 to the sustain level */
    float sustain;  /**< Level after the decay, 0..1 */
    float release;  /**< Seconds from the sustain level to 0 */
    float hold;     /**< Seconds at the sustain level before the release */
} box_audio_synth_envelope_t;

/** An oscillator of a synth patch */
typedef struct box_audio_synth_layer_t {
    uint32_t wave;                        /**< A box_audio_synth_wave_t */
    uint32_t has_envelope;                /**< 1 when it has its own envelope, 0 for the one
                                           * of the patch */
    float frequency;                      /**< Hz, ignored by noise */
    float frequency_end;                  /**< Hz the pitch sweeps to, 0 for no sweep */
    float sweep;                          /**< Seconds of the sweep */
    float level;                          /**< Linear gain */
    box_audio_synth_envelope_t envelope;  /**< Its own envelope, when has_envelope */
    uint32_t modulates;                   /**< 1 + the layer it modulates, which comes before
                                           * it, or 0: a modulator is not heard unless it is
                                           * audible */
    uint32_t modulation;                  /**< A box_audio_synth_modulation_t */
    float index;                          /**< Phase: peak index in radians at the top of its
                                           * envelope; ring: depth, 0..1 */
    float feedback;                       /**< Radians of its own output on its phase */
    uint32_t audible;                     /**< 1 when a modulator is heard as well, at level */
} box_audio_synth_layer_t;

/** A synth patch: up to BOX_AUDIO_SYNTH_LAYERS oscillators with envelopes, then one filter,
 * then an amp. Plain values in seconds and Hz, the same at every sample rate */
typedef struct box_audio_synth_patch_t {
    uint32_t layers;                      /**< Oscillators that play, 0..BOX_AUDIO_SYNTH_LAYERS */
    uint32_t seed;                        /**< Seed of the noise and of the variances */
    float pitch_variance;                 /**< Random share of the pitch per play, 0..1 */
    float level_variance;                 /**< Random share of the level per play, 0..1 */
    box_audio_synth_envelope_t envelope;  /**< Envelope of the layers without their own */
    uint32_t filter;                      /**< A box_audio_synth_filter_t */
    float cutoff;                         /**< Hz */
    float cutoff_end;                     /**< Hz the cutoff sweeps to, 0 for no sweep */
    float cutoff_sweep;                   /**< Seconds of the sweep */
    float resonance;                      /**< 0..1, 1 rings the longest */
    box_audio_synth_layer_t layer[BOX_AUDIO_SYNTH_LAYERS]; /**< The oscillators */
} box_audio_synth_patch_t;

/** Make a sound id a synth patch, or replace the patch of a synth sound. Voices that play it
 * keep the patch they started with; the next play takes the new one */
typedef struct box_audio_cmd_synth_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_SYNTH */
    uint32_t sound;                 /**< Id of the sound, free or a synth */
    box_audio_synth_patch_t patch;  /**< The patch, clamped by the core */
} box_audio_cmd_synth_t;

/** Events the core reports to its transport */
typedef enum box_audio_event_type_t {
    BOX_AUDIO_EVENT_VOICE_END = 1,      /**< A voice ended, id is its handle */
    BOX_AUDIO_EVENT_VOICE_STOLEN = 2,   /**< The cap stole a voice, id is its handle, value
                                         * the steals so far */
    BOX_AUDIO_EVENT_SOUND_FREE = 3,     /**< No voice uses a released sound, id is the sound */
    BOX_AUDIO_EVENT_ORDER_REFUSED = 4,  /**< An invalid order was ignored, value its count */
    BOX_AUDIO_EVENT_IGNORED = 5,        /**< A command was ignored, id is its type */
    BOX_AUDIO_EVENT_LOST = 6            /**< The ring was full, value is the events lost since
                                         * the last LOST event was read; the last event of
                                         * the ring when it is full */
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

/** Apply a batch of commands. They take effect at the start of the next render block.
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

/** Get the floats of delay memory a reverb needs at a sample rate: its delay lines at the
 * largest size and its longest predelay.
 *
 * @param sample_rate Frames per second of the core
 * @return Number of floats
 */
int32_t box_audio_reverb_floats(
    float sample_rate);

/** Give a reverb slot its delay memory, on the thread that renders, between renders.
 *
 * The memory is in the address space of the core and stays with the side that runs the core,
 * which frees it after the core. The core clears it. A slot takes memory once.
 *
 * @param core The core
 * @param slot Slot of the reverb, below BOX_AUDIO_REVERB_MAX
 * @param data The memory
 * @param floats Floats of the memory, at least box_audio_reverb_floats of the rate
 * @return Whether the core took the memory
 */
bool box_audio_core_reverb_memory(
    box_audio_core_t *core,
    uint32_t slot,
    float *data,
    int32_t floats);

/** Check whether a reverb renders: it has memory, a bus or a tail that fades, and a tail that
 * has not decayed to silence or a signal that comes in.
 *
 * @param core The core
 * @param slot Slot of the reverb
 * @return Whether it renders
 */
bool box_audio_core_reverb_running(
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

/** Write a play command.
 *
 * @param writer The writer
 * @param voice Handle of the new voice
 * @param sound Id of the sound
 * @param bus Bus of the voice
 * @param volume Volume
 * @param pan Pan, -1..1
 * @param pitch Pitch ratio
 * @param flags BOX_AUDIO_PLAY_ flags
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
    uint32_t flags);

/** Write a stop command.
 *
 * @param writer The writer
 * @param voice Handle of the voice
 * @return Whether the command fit
 */
bool box_audio_write_stop(
    box_audio_writer_t *writer,
    uint32_t voice);

/** Write a command that sets a parameter of a voice.
 *
 * @param writer The writer
 * @param voice Handle of the voice
 * @param param A box_audio_param_t
 * @param value The value
 * @return Whether the command fit
 */
bool box_audio_write_voice_set(
    box_audio_writer_t *writer,
    uint32_t voice,
    box_audio_param_t param,
    float value);

/** Write a command that sets a parameter of a bus.
 *
 * @param writer The writer
 * @param bus Index of the bus
 * @param param A box_audio_param_t
 * @param value The value
 * @return Whether the command fit
 */
bool box_audio_write_bus_set(
    box_audio_writer_t *writer,
    uint32_t bus,
    box_audio_param_t param,
    float value);

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
 * @return Whether the command fit
 */
bool box_audio_write_stream(
    box_audio_writer_t *writer,
    uint32_t voice,
    uint32_t stream,
    uint32_t bus,
    float volume,
    uint32_t channels,
    float sample_rate);

/** Write a command that releases a sound.
 *
 * @param writer The writer
 * @param sound Id of the sound
 * @return Whether the command fit
 */
bool box_audio_write_release(
    box_audio_writer_t *writer,
    uint32_t sound);

/** Write a command that puts a reverb on a bus, changes it or takes it off.
 *
 * @param writer The writer
 * @param slot Slot of the reverb
 * @param bus Bus of the reverb, BOX_AUDIO_BUS_NONE to take it off
 * @param params The parameters
 * @return Whether the command fit
 */
bool box_audio_write_reverb(
    box_audio_writer_t *writer,
    uint32_t slot,
    uint32_t bus,
    const box_audio_reverb_params_t *params);

/** Write a command that makes a sound id a synth patch or replaces its patch.
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
