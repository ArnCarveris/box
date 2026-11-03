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

/** Move a ramp to a target over a number of frames.
 *
 * @param ramp The ramp
 * @param target The target
 * @param frames Frames to reach it, at least 1
 */
static inline void box_audio_ramp_to(
    box_audio_ramp_t *ramp,
    float target,
    int32_t frames)
{
    ramp->target = target;
    if (ramp->value == target) {
        ramp->left = 0;
        ramp->step = 0;
    } else {
        ramp->left = frames > 0 ? frames : 1;
        ramp->step = (target - ramp->value) / (float)ramp->left;
    }
}

/** Set a ramp to a value at once.
 *
 * @param ramp The ramp
 * @param value The value
 */
static inline void box_audio_ramp_set(
    box_audio_ramp_t *ramp,
    float value)
{
    ramp->value = value;
    ramp->target = value;
    ramp->step = 0;
    ramp->left = 0;
}

/** Advance a ramp by a frame.
 *
 * @param ramp The ramp
 * @return Its value for the frame
 */
static inline float box_audio_ramp_next(
    box_audio_ramp_t *ramp)
{
    if (ramp->left > 0) {
        ramp->left --;
        ramp->value = ramp->left ? ramp->value + ramp->step : ramp->target;
    }

    return ramp->value;
}

/** A reverb: a feedback delay network of BOX_AUDIO_REVERB_LINES lines on the memory the
 * transport gave its slot, with a predelay line in front of it */
typedef struct box_audio_reverb_t {
    float *memory;            /**< Delay memory of the slot, NULL until it arrives */
    int32_t floats;           /**< Floats of memory */
    bool attached;            /**< Whether it renders: on a bus, or its tail fades out */
    bool idle;                /**< The tail decayed to silence and the lines are clear */
    bool swap;                /**< Waits for the fade to reach 0 to apply the target */
    uint32_t bus;             /**< Bus it processes, BUS_NONE while its tail fades out */
    uint32_t target_bus;      /**< Bus of the last command */
    box_audio_reverb_params_t target;  /**< Parameters of the last command, clamped */
    float size;               /**< Size the lines have */
    float predelay;           /**< Predelay the predelay line has */
    float *line[BOX_AUDIO_REVERB_LINES];   /**< The delay lines in memory */
    int32_t length[BOX_AUDIO_REVERB_LINES]; /**< Delay of each line in frames, all prime */
    int32_t pos[BOX_AUDIO_REVERB_LINES];   /**< Position of each line */
    float lp[BOX_AUDIO_REVERB_LINES];      /**< State of the damping filter of each line */
    float gain[BOX_AUDIO_REVERB_LINES];    /**< Feedback gain of each line */
    float gain_target[BOX_AUDIO_REVERB_LINES]; /**< Gain the decay asks for */
    float damp;               /**< Pole of the damping filters */
    float damp_target;        /**< Pole the damping asks for */
    float *pre;               /**< The predelay line in memory */
    int32_t pre_length;       /**< Delay of the predelay line in frames, 0 for none */
    int32_t pre_pos;          /**< Position of the predelay line */
    int32_t quiet;            /**< Frames of silent input in a row */
    int32_t still;            /**< Frames in a row in which the lines summed under silence */
    box_audio_ramp_t mix;     /**< Wet level */
    box_audio_ramp_t fade;    /**< Fade of the wet signal around a swap */
    box_audio_ramp_t feed;    /**< Fade in of the input after a swap, so it enters the clear
                               * lines without a step */
} box_audio_reverb_t;

/** Kinds of sources a voice plays */
typedef enum box_audio_source_kind_t {
    BOX_AUDIO_SOURCE_NONE = 0,   /**< No source, silent */
    BOX_AUDIO_SOURCE_SAMPLE = 1, /**< A sample buffer, box_audio_sample_source_t */
<<<<<<< HEAD
    BOX_AUDIO_SOURCE_STREAM = 2  /**< The ring of chunks of a stream, box_audio_stream_source_t */
=======
    BOX_AUDIO_SOURCE_SYNTH = 2   /**< A synth patch, box_audio_synth_source_t */
>>>>>>> 433c1626425e0da16a562b984a8e59e5297632c4
} box_audio_source_kind_t;

/** A source that plays a sound, mono or stereo, with looping and linear interpolation */
typedef struct box_audio_sample_source_t {
    uint32_t sound;    /**< Id of the sound */
    double position;   /**< Position in frames of the sound */
    double rate;       /**< Frames of the sound per frame of the core at pitch 1 */
    double step;       /**< Frames of the sound per frame of the core */
    bool loop;         /**< Whether it starts over at its end */
} box_audio_sample_source_t;

<<<<<<< HEAD
/** Phases of a stream source */
typedef enum box_audio_stream_phase_t {
    BOX_AUDIO_STREAM_PRIMING = 0,    /**< Waits for the first chunks; the voice ramp fades it in */
    BOX_AUDIO_STREAM_PLAYING = 1,    /**< Reads frames at full gain */
    BOX_AUDIO_STREAM_FADING = 2,     /**< Ran short of frames and fades out over the fade */
    BOX_AUDIO_STREAM_STARVED = 3,    /**< Plays silence until chunks arrive */
    BOX_AUDIO_STREAM_RECOVERING = 4, /**< Chunks arrived; fades back in over the fade */
    BOX_AUDIO_STREAM_ENDED = 5       /**< Read the last frame of the last chunk */
} box_audio_stream_phase_t;

/** A source that reads the ring of a stream with linear interpolation between two frames. It
 * never waits: a ring that runs short fades out to silence and back in when chunks arrive */
typedef struct box_audio_stream_source_t {
    uint32_t stream;   /**< Index of the stream */
    double frac;       /**< Position between frame a and frame b, 0..1 */
    double step;       /**< Frames of the stream per frame of the core */
    float a[2];        /**< Frame at the position */
    float b[2];        /**< Frame after it */
    float gain;        /**< Gain of the fade of an underrun, 0..1 */
    float fade_step;   /**< Change of gain per frame of a fade */
    int32_t need;      /**< Frames the ring holds at least to play on without a fade */
    int32_t phase;     /**< A box_audio_stream_phase_t */
    bool drained;      /**< Frame b repeats a, the last frame of the stream */
} box_audio_stream_source_t;
=======
/** Segments of a synth envelope */
typedef enum box_audio_synth_segment_t {
    BOX_AUDIO_SYNTH_ATTACK = 0,   /**< From 0 to 1 */
    BOX_AUDIO_SYNTH_DECAY = 1,    /**< From 1 to the sustain level */
    BOX_AUDIO_SYNTH_HOLD = 2,     /**< At the sustain level */
    BOX_AUDIO_SYNTH_RELEASE = 3,  /**< From the sustain level to 0 */
    BOX_AUDIO_SYNTH_DONE = 4      /**< Silent, the envelope ended */
} box_audio_synth_segment_t;

/** An envelope of a synth voice, advanced per frame */
typedef struct box_audio_synth_env_t {
    int32_t segment;      /**< A box_audio_synth_segment_t, -1 before the attack */
    int32_t left;         /**< Frames left in the segment */
    float value;          /**< Current level */
    float target;         /**< Level at the end of the segment */
    float step;           /**< Change per frame */
    float sustain;        /**< Sustain level */
    int32_t frames[4];    /**< Frames of the attack, the decay, the hold and the release */
} box_audio_synth_env_t;

/** An oscillator of a synth voice */
typedef struct box_audio_synth_osc_t {
    int32_t wave;         /**< A box_audio_synth_wave_t */
    int32_t env;          /**< Envelope it uses: 0 the one of the patch, 1 + layer its own */
    float level;          /**< Linear gain */
    double phase;         /**< Phase in turns, 0..1 */
    double inc;           /**< Turns per frame */
    double ratio;         /**< Factor of inc per frame during the sweep */
    double inc_end;       /**< Turns per frame at the end of the sweep */
    int32_t sweep;        /**< Frames left of the sweep */
} box_audio_synth_osc_t;

/** A source that plays a synth patch, a copy of it, so it never allocates */
typedef struct box_audio_synth_source_t {
    box_audio_synth_patch_t patch;  /**< The patch, clamped, for the restart of a loop */
    box_audio_synth_osc_t osc[BOX_AUDIO_SYNTH_LAYERS];  /**< The oscillators */
    box_audio_synth_env_t env[1 + BOX_AUDIO_SYNTH_LAYERS]; /**< The envelope of the patch,
                                     * then the own envelope of each layer */
    uint32_t envs;        /**< Bit of each envelope a layer uses */
    double inc_max;       /**< Largest turns per frame of an oscillator */
    uint32_t rng;         /**< State of the noise, xorshift32, never 0 */
    float pitch;          /**< Pitch ratio of the voice, the variance of the play included */
    float variance;       /**< Factor of the pitch that the variance of the play picked */
    float amp;            /**< Gain of the amp, the level variance of the play included */
    bool loop;            /**< Whether it starts over at the end of its envelopes */
    int32_t filter;       /**< A box_audio_synth_filter_t */
    float k;              /**< Damping of the filter, 2 / Q */
    float a1, a2, a3;     /**< Coefficients of the filter */
    float ic1, ic2;       /**< State of the filter */
    double cutoff;        /**< Cutoff in Hz */
    double cutoff_ratio;  /**< Factor of the cutoff per frame during the sweep */
    double cutoff_end;    /**< Cutoff at the end of the sweep */
    int32_t cutoff_sweep; /**< Frames left of the sweep */
    int32_t control;      /**< Frames until the filter takes the swept cutoff */
} box_audio_synth_source_t;
>>>>>>> 433c1626425e0da16a562b984a8e59e5297632c4

/** State of the kinds of sources */
typedef union box_audio_source_data_t {
    box_audio_sample_source_t sample;  /**< BOX_AUDIO_SOURCE_SAMPLE */
<<<<<<< HEAD
    box_audio_stream_source_t stream;  /**< BOX_AUDIO_SOURCE_STREAM */
=======
    box_audio_synth_source_t synth;    /**< BOX_AUDIO_SOURCE_SYNTH */
>>>>>>> 433c1626425e0da16a562b984a8e59e5297632c4
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
    int32_t synth;      /**< Slot of its patch plus 1 for a synth, 0 for frames */
} box_audio_sound_t;

<<<<<<< HEAD
/** A chunk of a stream: frames in the memory of the side that runs the core */
typedef struct box_audio_chunk_t {
    const float *data;  /**< Frames, interleaved when stereo */
    int32_t frames;     /**< Number of frames */
} box_audio_chunk_t;

/** A stream: a fixed ring of chunks that one voice reads */
typedef struct box_audio_stream_t {
    box_audio_chunk_t chunks[BOX_AUDIO_STREAM_CHUNKS]; /**< The ring */
    int32_t head;        /**< Index of the oldest chunk */
    int32_t count;       /**< Chunks in the ring */
    int32_t at;          /**< Frames read of the oldest chunk */
    int32_t avail;       /**< Frames in the ring that were not read */
    int32_t channels;    /**< 1 or 2 */
    double rate;         /**< Frames of the stream per frame of the core */
    uint32_t voice;      /**< Handle of the voice that opened it last */
    bool open;           /**< Whether a voice reads it */
    bool last;           /**< Whether its last chunk is in the ring */
    bool ended;          /**< Whether the voice read the last chunk to its end */
    uint32_t consumed;   /**< Chunks read to their end since it opened */
    int32_t underruns;   /**< Times it ran out of chunks since it opened */
    bool starved;        /**< Whether it plays silence for want of chunks */
} box_audio_stream_t;
=======
/** A synth patch the core holds for a sound id */
typedef struct box_audio_synth_slot_t {
    box_audio_synth_patch_t patch;  /**< The patch, clamped */
    uint32_t plays;     /**< Plays so far, which seed the noise and the variances */
    bool used;          /**< Whether a sound has the slot */
} box_audio_synth_slot_t;
>>>>>>> 433c1626425e0da16a562b984a8e59e5297632c4

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
    box_audio_stream_t streams[BOX_AUDIO_STREAM_MAX]; /**< Streams by index */
    float src_l[BOX_AUDIO_CORE_BLOCK];  /**< Left or mono output of a source */
    float src_r[BOX_AUDIO_CORE_BLOCK];  /**< Right output of a stereo source */
    box_audio_reverb_t reverbs[BOX_AUDIO_REVERB_MAX]; /**< Reverbs by slot */
    float reverb_smooth;  /**< Share of the way to their targets a decay and a damping move
                           * per block */
    float limiter_gain;   /**< Current gain of the limiter, 0..1 */
    float limiter_step;   /**< Largest rise of the gain of the limiter per frame */
    box_audio_synth_slot_t synths[BOX_AUDIO_SYNTH_MAX]; /**< Patches of synth sounds */
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

/** Set the reverbs of a new core to no bus and no memory.
 *
 * @param core The core, zeroed
 */
void box_audio_reverbs_init(
    box_audio_core_t *core);

/** Apply a reverb command.
 *
 * @param core The core
 * @param cmd The command
 */
void box_audio_reverb_command(
    box_audio_core_t *core,
    const box_audio_cmd_reverb_t *cmd);

/** Run the reverbs of a bus over a block of it, in place: the dry signal at 1 - mix and the
 * wet one at mix. On the master it also adds the tails that fade out after their bus went.
 *
 * @param core The core
 * @param bus Index of the bus
 * @param l Left samples of the bus
 * @param r Right samples of the bus
 * @param frames Frames of the block
 */
void box_audio_reverbs_render(
    box_audio_core_t *core,
    uint32_t bus,
    float *l,
    float *r,
    int32_t frames);

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

<<<<<<< HEAD
/** Start a stream source on an open stream.
 *
 * @param core The core
 * @param source The source
 * @param stream Index of the stream
 */
void box_audio_source_stream(
    box_audio_core_t *core,
    box_audio_source_t *source,
    uint32_t stream);
=======
/** Apply a synth command: give a free sound id a patch or replace the patch of a synth.
 *
 * @param core The core
 * @param cmd The command
 */
void box_audio_synth_command(
    box_audio_core_t *core,
    const box_audio_cmd_synth_t *cmd);

/** Start a synth source on the patch of a synth sound. Takes no use of the sound: the source
 * plays a copy of the patch.
 *
 * @param core The core
 * @param source The source
 * @param sound Id of a ready synth sound
 * @param pitch Pitch ratio
 * @param loop Whether it starts over at the end of its envelopes
 */
void box_audio_source_synth(
    box_audio_core_t *core,
    box_audio_source_t *source,
    uint32_t sound,
    float pitch,
    bool loop);

/** Render a block of a synth source into core->src_l.
 *
 * @param core The core
 * @param src The source
 * @param frames Frames to render, at most BOX_AUDIO_CORE_BLOCK
 * @return Frames it rendered; fewer than frames when its envelopes ended
 */
int32_t box_audio_synth_render(
    box_audio_core_t *core,
    box_audio_synth_source_t *src,
    int32_t frames);

/** Set the pitch ratio of a synth source.
 *
 * @param src The source
 * @param pitch Pitch ratio
 */
void box_audio_synth_pitch(
    box_audio_synth_source_t *src,
    float pitch);
>>>>>>> 433c1626425e0da16a562b984a8e59e5297632c4

/** Set the pitch ratio of a source, for kinds that have one.
 *
 * @param source The source
 * @param pitch Pitch ratio
 */
void box_audio_source_pitch(
    box_audio_source_t *source,
    float pitch);

/** Move the read position of a source, for kinds that have one. A looping sound wraps the
 * position, one that does not loop clamps it to its end; a NaN or a negative position is 0.
 *
 * @param core The core
 * @param source The source
 * @param position Position in frames of the sound
 */
void box_audio_source_seek(
    box_audio_core_t *core,
    box_audio_source_t *source,
    float position);

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

/** End a source: give back what it uses, which can free a released sound or close a stream.
 *
 * @param core The core
 * @param source The source
 */
void box_audio_source_end(
    box_audio_core_t *core,
    box_audio_source_t *source);

#endif
