/**
 * @file audio/core/internal.h
 * @brief State of the audio core: ramps, sources, voices, buses and the core itself.
 *
 * Private to src/audio/core. A source kind adds a member to box_audio_source_data_t and a case
 * to the functions of source.c; the voice around it, its gains and its bus stay the same.
 */

#ifndef BOX_AUDIO_CORE_INTERNAL_H
#define BOX_AUDIO_CORE_INTERNAL_H

#include "core.h"

/** Voice slots: the cap plus the slots of voices that fade out */
#define BOX_AUDIO_VOICE_SLOTS (BOX_AUDIO_VOICE_MAX + BOX_AUDIO_VOICE_TAIL_MAX)

/** Smallest magnitude a sample keeps; smaller ones are flushed to 0, so no denormal forms */
#define BOX_AUDIO_TINY 1e-30f

/** Largest magnitude a sample keeps; larger ones, infinities and NaNs become 0 */
#define BOX_AUDIO_HUGE 1e30f

/** A value that moves linearly to its target over a number of frames */
typedef struct box_audio_ramp_t {
    float value;   /**< Current value */
    float target;  /**< Value it moves to */
    float step;    /**< Change per frame */
    int32_t left;  /**< Frames until it reaches the target, 0 when it is there */
} box_audio_ramp_t;

/** Kinds of sources a voice plays */
typedef enum box_audio_source_kind_t {
    BOX_AUDIO_SOURCE_NONE = 0,   /**< No source, silent */
    BOX_AUDIO_SOURCE_SAMPLE = 1  /**< A sample buffer, box_audio_sample_source_t */
} box_audio_source_kind_t;

/** A source that plays a sound, mono or stereo, with looping and linear interpolation */
typedef struct box_audio_sample_source_t {
    uint32_t sound;    /**< Id of the sound */
    double position;   /**< Position in frames of the sound */
    double rate;       /**< Frames of the sound per frame of the core at pitch 1 */
    double step;       /**< Frames of the sound per frame of the core */
    bool loop;         /**< Whether it starts over at its end */
} box_audio_sample_source_t;

/** State of the kinds of sources */
typedef union box_audio_source_data_t {
    box_audio_sample_source_t sample;  /**< BOX_AUDIO_SOURCE_SAMPLE */
} box_audio_source_data_t;

/** What a voice plays */
typedef struct box_audio_source_t {
    int32_t kind;                  /**< A box_audio_source_kind_t */
    box_audio_source_data_t data;  /**< State of the kind */
} box_audio_source_t;

/** States of a voice slot */
typedef enum box_audio_voice_state_t {
    BOX_AUDIO_VOICE_FREE = 0,     /**< The slot is free */
    BOX_AUDIO_VOICE_PLAYING = 1,  /**< The voice plays and counts toward the cap */
    BOX_AUDIO_VOICE_STOPPING = 2  /**< The voice fades out and ends when it is silent */
} box_audio_voice_state_t;

/** A voice: a source with a bus, a volume, a pan and a pitch */
typedef struct box_audio_voice_t {
    int32_t state;            /**< A box_audio_voice_state_t */
    uint32_t handle;          /**< Handle the sender picked */
    uint32_t bus;             /**< Bus it plays into */
    float volume;             /**< Volume */
    float pan;                /**< Pan, -1..1 */
    float pitch;              /**< Pitch ratio */
    box_audio_ramp_t left;    /**< Gain of the left channel: volume, pan and fades */
    box_audio_ramp_t right;   /**< Gain of the right channel */
    box_audio_source_t source; /**< What it plays */
} box_audio_voice_t;

/** States of a sound slot */
typedef enum box_audio_sound_state_t {
    BOX_AUDIO_SOUND_FREE = 0,     /**< The slot is free */
    BOX_AUDIO_SOUND_READY = 1,    /**< Voices can play it */
    BOX_AUDIO_SOUND_RELEASED = 2, /**< Released, freed once no voice plays it */
    BOX_AUDIO_SOUND_PENDING = 3   /**< Freed, but its SOUND_FREE found the ring full and waits
                                   * for room; the id stays taken until it is reported */
} box_audio_sound_state_t;

/** A sound: frames in the memory of the side that runs the core */
typedef struct box_audio_sound_t {
    const float *data;  /**< Frames, interleaved when stereo */
    int32_t frames;     /**< Number of frames */
    int32_t channels;   /**< 1 or 2 */
    double rate;        /**< Frames of the sound per frame of the core */
    int32_t users;      /**< Voices that play it */
    int32_t state;      /**< A box_audio_sound_state_t */
} box_audio_sound_t;

/** A bus: a volume, a pan, a mute, an output and one send, with a block of samples */
typedef struct box_audio_bus_t {
    float volume;            /**< Volume */
    float pan;               /**< Pan, -1..1 */
    bool mute;               /**< Whether it is muted */
    float send_level;        /**< Level of the send */
    box_audio_ramp_t left;   /**< Gain of the left channel: volume, pan and mute */
    box_audio_ramp_t right;  /**< Gain of the right channel */
    box_audio_ramp_t send;   /**< Gain of the send */
    float l[BOX_AUDIO_CORE_BLOCK];  /**< Left samples of the block */
    float r[BOX_AUDIO_CORE_BLOCK];  /**< Right samples of the block */
} box_audio_bus_t;

/** The core */
struct box_audio_core_t {
    float sample_rate;    /**< Frames per second */
    int32_t ramp_frames;  /**< Frames of a ramp */
    int32_t steal_frames; /**< Frames of the fade of a stolen voice */
    void* (*alloc)(size_t size, void *ctx);  /**< Allocator */
    void (*free)(void *ptr, void *ctx);      /**< Free of the allocator */
    void *ctx;            /**< Context of the allocator */
    box_audio_voice_t voices[BOX_AUDIO_VOICE_SLOTS];  /**< Voice slots */
    int32_t playing;      /**< Voices in BOX_AUDIO_VOICE_PLAYING */
    int32_t steals;       /**< Voices the cap stole so far */
    box_audio_bus_t buses[BOX_AUDIO_BUS_MAX];       /**< Buses, 0 is the master */
    box_audio_route_t order[BOX_AUDIO_BUS_MAX];     /**< Processing order, the master last */
    int32_t order_count;  /**< Routes in order */
    bool bus_active[BOX_AUDIO_BUS_MAX];             /**< Whether a bus is in the order */
    box_audio_sound_t sounds[BOX_AUDIO_SOUND_MAX];  /**< Sounds by id */
    box_audio_event_t events[BOX_AUDIO_EVENT_MAX];  /**< Ring of events */
    int32_t event_head;   /**< Index of the oldest event */
    int32_t event_count;  /**< Events in the ring */
    int32_t events_lost;  /**< Events dropped because the ring was full, since the last LOST
                           * event was read */
    int32_t event_lost_at; /**< Slot of the LOST event in the ring, -1 when it has none */
    int32_t free_pending; /**< Sounds in BOX_AUDIO_SOUND_PENDING */
    float src_l[BOX_AUDIO_CORE_BLOCK];  /**< Left or mono output of a source */
    float src_r[BOX_AUDIO_CORE_BLOCK];  /**< Right output of a stereo source */
    float limiter_gain;   /**< Current gain of the limiter, 0..1 */
    float limiter_step;   /**< Largest rise of the gain of the limiter per frame */
};

/** Flush a sample: NaN, infinities, huge values and denormals become 0.
 *
 * @param x The sample
 * @return The sample, or 0
 */
static inline float box_audio_clean(
    float x)
{
    float a = x < 0 ? -x : x;

    return a >= BOX_AUDIO_TINY && a <= BOX_AUDIO_HUGE ? x : 0.0f;
}

/** Push an event, or count it as lost when the ring is full. Does not allocate.
 *
 * Ordinary events take the slots but the last, which is kept for BOX_AUDIO_EVENT_LOST. When
 * they are full the event is counted in the one LOST event, pushed the first time.
 *
 * A BOX_AUDIO_EVENT_SOUND_FREE that finds them full marks its sound BOX_AUDIO_SOUND_PENDING
 * instead, and read_events reports it once there is room. The pending state keeps the id
 * taken, so a sound waits at most once and the states bound what waits outside the ring.
 *
 * @param core The core
 * @param type A box_audio_event_type_t
 * @param id Id of the event
 * @param value Value of the event
 */
void box_audio_core_event(
    box_audio_core_t *core,
    int32_t type,
    uint32_t id,
    int32_t value);

/** Start a sample source on a sound and take a use of it.
 *
 * @param core The core
 * @param source The source
 * @param sound Id of a ready sound
 * @param pitch Pitch ratio
 * @param loop Whether it loops
 */
void box_audio_source_sample(
    box_audio_core_t *core,
    box_audio_source_t *source,
    uint32_t sound,
    float pitch,
    bool loop);

/** Set the pitch ratio of a source, for kinds that have one.
 *
 * @param source The source
 * @param pitch Pitch ratio
 */
void box_audio_source_pitch(
    box_audio_source_t *source,
    float pitch);

/** Render a block of a source into core->src_l and, when stereo, core->src_r.
 *
 * @param core The core
 * @param source The source
 * @param frames Frames to render, at most BOX_AUDIO_CORE_BLOCK
 * @param stereo Set to whether the source wrote src_r
 * @return Frames it rendered; fewer than frames when it ended, the rest is 0
 */
int32_t box_audio_source_render(
    box_audio_core_t *core,
    box_audio_source_t *source,
    int32_t frames,
    bool *stereo);

/** End a source: give back what it uses, which can free a released sound.
 *
 * @param core The core
 * @param source The source
 */
void box_audio_source_end(
    box_audio_core_t *core,
    box_audio_source_t *source);

#endif
