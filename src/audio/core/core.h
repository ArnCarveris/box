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

/** Commands of the byte stream. Each starts with a box_audio_cmd_header_t */
typedef enum box_audio_cmd_type_t {
    BOX_AUDIO_CMD_PLAY = 1,          /**< box_audio_cmd_play_t */
    BOX_AUDIO_CMD_STOP = 2,          /**< box_audio_cmd_stop_t */
    BOX_AUDIO_CMD_VOICE_SET = 3,     /**< box_audio_cmd_set_t on a voice */
    BOX_AUDIO_CMD_BUS_SET = 4,       /**< box_audio_cmd_set_t on a bus */
    BOX_AUDIO_CMD_ORDER = 5,         /**< box_audio_cmd_order_t and its routes */
    BOX_AUDIO_CMD_SOUND_RELEASE = 6  /**< box_audio_cmd_release_t */
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

/** Write a command that releases a sound.
 *
 * @param writer The writer
 * @param sound Id of the sound
 * @return Whether the command fit
 */
bool box_audio_write_release(
    box_audio_writer_t *writer,
    uint32_t sound);

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
