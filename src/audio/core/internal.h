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

/** Synth layers the voices of a core play at once: 4 for every voice slot, so patches of up to
 * 4 layers never run out, and a voice of more takes the room fewer leave. A play that does not
 * find a part for each of its layers is ignored */
#define BOX_AUDIO_SYNTH_PARTS (BOX_AUDIO_VOICE_SLOTS * 4)

/** Lowest frequency of an oscillator in Hz */
#define BOX_AUDIO_SYNTH_FREQUENCY_MIN 1.0f

/** Lowest cutoff in Hz */
#define BOX_AUDIO_SYNTH_CUTOFF_MIN 10.0f

/** Highest frequency or cutoff a patch keeps in Hz, before the clamp to the rate */
#define BOX_AUDIO_SYNTH_HZ_MAX 1000000.0f

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
    BOX_AUDIO_SOURCE_SYNTH = 2,  /**< A synth patch, box_audio_synth_source_t */
    BOX_AUDIO_SOURCE_STREAM = 3  /**< The ring of chunks of a stream, box_audio_stream_source_t */
} box_audio_source_kind_t;

/** A source that plays a sound, mono or stereo, with looping and linear interpolation */
typedef struct box_audio_sample_source_t {
    uint32_t sound;    /**< Id of the sound */
    double position;   /**< Position in frames of the sound */
    double rate;       /**< Frames of the sound per frame of the core at pitch 1 */
    double step;       /**< Frames of the sound per frame of the core */
    bool loop;         /**< Whether it starts over at its end */
} box_audio_sample_source_t;

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
    float curve[4];       /**< Curves of the attack, the decay, the hold (0) and the release */
    bool curved;          /**< The segment follows level, not step */
    double level;         /**< Current level of a curved segment */
    double mul;           /**< Factor per frame of a curved segment */
    double add;           /**< Term per frame of a curved segment */
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
    double glide;         /**< Factor of inc per frame during a glide */
    int32_t target;       /**< Layer it modulates, -1 for none */
    bool ring;            /**< Whether it multiplies its target instead of moving its phase */
    bool audible;         /**< Whether a modulator is heard as well */
    float index;          /**< Phase: turns of its target per unit of its signal, limited
                           * against the frequency; ring: depth */
    float feedback;       /**< Turns of its phase per unit of its own output */
    float fb1, fb2;       /**< Its last two outputs, for the feedback */
    double width;         /**< Square: share of the period it is high */
    double width_base;    /**< Square: the pulse width of its layer, without the LFOs */
    double width_step;    /**< Change of width per frame from the LFOs */
    double vibrato;       /**< Factor of inc from the LFOs, 1 without */
    double vibrato_step;  /**< Change of vibrato per frame */
    float tremolo;        /**< Factor of its level from the LFOs, 1 without */
    float tremolo_step;   /**< Change of tremolo per frame */
} box_audio_synth_osc_t;

/** An LFO of a synth voice, advanced per control block of BOX_AUDIO_SYNTH_CONTROL frames */
typedef struct box_audio_synth_mod_t {
    int32_t wave;         /**< A box_audio_synth_lfo_wave_t */
    double phase;         /**< Phase in turns, 0..1 */
    double inc;           /**< Turns per control block */
    double start;         /**< Phase at the start of a pass, random per play or fixed */
    int32_t delay;        /**< Frames left of its delay */
    float gain;           /**< Level of its fade in, 0..1 */
    float fade_step;      /**< Change of gain per control block */
    float value;          /**< Its wave, -1..1, smoothed for the waves that jump */
    float held;           /**< Value the random wave holds */
    bool smooth;          /**< Whether its wave jumps and is smoothed */
} box_audio_synth_mod_t;

/** A stage of the filter of a synth voice: a trapezoidal state variable filter */
typedef struct box_audio_synth_svf_t {
    float k;              /**< Damping, 2 / Q */
    float a1, a2, a3;     /**< Coefficients */
    float ic1, ic2;       /**< State */
} box_audio_synth_svf_t;

/** The unison voices of a layer of a synth voice: copies of its oscillator, each at its own
 * phase, at a factor of the turns per frame of the oscillator and with a gain per channel. The
 * oscillator keeps the sweep and the glide they all follow */
typedef struct box_audio_synth_unison_t {
    int32_t voices;       /**< Voices, 1 for a layer without unison, which renders the
                           * oscillator alone */
    double phase[BOX_AUDIO_SYNTH_UNISON_MAX];  /**< Phase of each voice in turns, 0..1 */
    double detune[BOX_AUDIO_SYNTH_UNISON_MAX]; /**< Factor of inc of each voice */
    float left[BOX_AUDIO_SYNTH_UNISON_MAX];    /**< Gain of each voice on the left channel */
    float right[BOX_AUDIO_SYNTH_UNISON_MAX];   /**< Gain of each voice on the right channel */
} box_audio_synth_unison_t;
/** A pluck string of a synth voice: a Karplus-Strong loop of a delay line, a symmetric lowpass
 * of one frame of delay and a first order allpass of the fraction of a frame left, with a gain
 * that sets its decay */
typedef struct box_audio_synth_string_t {
    float line[BOX_AUDIO_SYNTH_STRING_FRAMES]; /**< The delay line, a ring */
    int32_t write;        /**< Index the next frame is written to */
    int32_t delay;        /**< Frames of the delay line in the loop, at least 2 */
    float c;              /**< Coefficient of the allpass */
    float gain;           /**< Gain of the loop */
    float b0, b1;         /**< Taps of the lowpass: b0 on the outer frames, b1 on the middle */
    float x1, x2;         /**< The last two frames out of the delay line */
    float f1, a1;         /**< The last input and output of the allpass */
    double inc;           /**< Turns per frame it is tuned to */
    double decay_frames;  /**< Frames of its decay to -60 dB */
    double period_max;    /**< Longest period in frames, at BOX_AUDIO_SYNTH_PLUCK_MIN */
    float peak;           /**< Largest magnitude of the window that runs */
    int32_t window;       /**< Frames left of the window, a period */
    bool ringing;         /**< Whether it is still over the silence */
} box_audio_synth_string_t;

/** A layer of a synth voice: its layer of the patch, its oscillator and its own envelope. The
 * core holds BOX_AUDIO_SYNTH_PARTS of them for all its voices, so a voice holds only the layers
 * its patch has */
typedef struct box_audio_synth_part_t {
    box_audio_synth_layer_t layer;  /**< The layer of the patch, clamped */
    box_audio_synth_osc_t osc;      /**< The oscillator */
    box_audio_synth_env_t env;      /**< Its own envelope, when the layer has one */
    box_audio_synth_unison_t unison; /**< Its unison voices */
    box_audio_synth_string_t *string; /**< Its string, a BOX_AUDIO_SYNTH_PLUCK layer, or NULL */
} box_audio_synth_part_t;

/** A source that plays a synth patch, a copy of it, so it never allocates: the patch without
 * its layers, and a part of the core for each of its layers */
typedef struct box_audio_synth_source_t {
    int32_t layers;       /**< Layers of the patch, 0..BOX_AUDIO_SYNTH_LAYERS */
    box_audio_synth_part_t *part[BOX_AUDIO_SYNTH_LAYERS]; /**< The part of each layer */
    box_audio_synth_envelope_t envelope; /**< Envelope of the patch, for the restart of a loop */
    box_audio_synth_envelope_t filter_envelope; /**< Filter envelope of the patch */
    box_audio_synth_env_t env;  /**< The envelope of the patch, envelope 0 */
    uint32_t envs;        /**< Bit of each envelope a layer uses: 0 the one of the patch, 1 +
                           * layer its own */
    bool modulated;       /**< Whether a layer modulates another or itself, which renders
                           * through box_audio_synth_modulate */
    double inc_max;       /**< Largest turns per frame of an oscillator */
    uint32_t rng;         /**< State of the noise, xorshift32, never 0 */
    float pitch;          /**< Pitch ratio of the voice, the variance of the play included */
    float variance;       /**< Factor of the pitch that the variance of the play picked */
    float amp;            /**< Gain of the amp, the level variance of the play included */
    bool loop;            /**< Whether it starts over at the end of its envelopes */
    int32_t filter;       /**< A box_audio_synth_filter_t */
    int32_t stages;       /**< Stages of the filter: 1 for 12 dB per octave, 2 for 24 */
    box_audio_synth_svf_t svf[2]; /**< The resonant stage, then the stage before it at 24 dB */
    box_audio_synth_svf_t svf_r[2]; /**< The stages of the right channel, when stereo */
    bool unison;          /**< Whether a layer has unison voices, which renders through
                           * box_audio_synth_render_wide */
    bool stereo;          /**< Whether a layer with unison voices has width, so the source
                           * renders a right channel too */
    double cutoff;        /**< Cutoff in Hz */
    double cutoff_base;   /**< Cutoff of the patch, clamped, in Hz */
    double cutoff_top;    /**< Highest cutoff in Hz, under Nyquist */
    float amount;         /**< Octaves of the filter envelope, 0 without one */
    box_audio_synth_env_t fenv; /**< The filter envelope, when amount is not 0 */
    int32_t control;      /**< Frames until the filter takes the cutoff of the envelope */
    int32_t glide_frames; /**< Frames of a glide of the patch, 0 when a pitch jumps */
    int32_t glide_left;   /**< Frames left of the glide that runs, 0 for none */
    int32_t lfos;         /**< LFOs that run, 0 without, which renders as before */
    box_audio_synth_lfo_t lfo_patch[BOX_AUDIO_SYNTH_LFOS]; /**< The LFOs of the patch */
    box_audio_synth_mod_t lfo[BOX_AUDIO_SYNTH_LFOS]; /**< The LFOs */
    uint32_t lfo_rng;     /**< State of the random phases and waves of the LFOs, never 0 */
    float lfo_smooth;     /**< Share of the way a jumping wave moves per control block */
    int32_t lfo_control;  /**< Frames until the next control block of the LFOs */
    bool lfo_cutoff;      /**< Whether an LFO moves the cutoff */
    double lfo_octaves;   /**< Octaves the LFOs move the cutoff by, 0 without */
    float tremolo;        /**< Factor of the output from the LFOs of every layer, 1 without */
    float tremolo_step;   /**< Change of tremolo per frame */
    int32_t strings;      /**< Pluck layers, each with a string, 0 for none */
    int32_t ringing;      /**< Strings that are still over the silence */
    int32_t ringing_free; /**< Strings of layers without an envelope still over the silence */
} box_audio_synth_source_t;

/** State of the kinds of sources */
typedef union box_audio_source_data_t {
    box_audio_sample_source_t sample;  /**< BOX_AUDIO_SOURCE_SAMPLE */
    box_audio_stream_source_t stream;  /**< BOX_AUDIO_SOURCE_STREAM */
    box_audio_synth_source_t synth;    /**< BOX_AUDIO_SOURCE_SYNTH */
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
/** A synth patch the core holds for a sound id */
typedef struct box_audio_synth_slot_t {
    box_audio_synth_patch_t patch;  /**< The patch, clamped */
    uint32_t plays;     /**< Plays so far, which seed the noise and the variances */
    int32_t strings;    /**< Pluck layers of the patch */
    bool used;          /**< Whether a sound has the slot */
} box_audio_synth_slot_t;

/** A command that takes effect on a frame: PLAY, STOP, VOICE_SET or BUS_SET */
typedef union box_audio_timed_cmd_t {
    box_audio_cmd_header_t header;  /**< The header, first in each of them */
    box_audio_cmd_play_t play;      /**< BOX_AUDIO_CMD_PLAY */
    box_audio_cmd_stop_t stop;      /**< BOX_AUDIO_CMD_STOP */
    box_audio_cmd_set_t set;        /**< BOX_AUDIO_CMD_VOICE_SET or BOX_AUDIO_CMD_BUS_SET */
} box_audio_timed_cmd_t;

/** A command that waits in the schedule for its frame */
typedef struct box_audio_scheduled_t {
    uint64_t when;              /**< Frame of the core clock it applies at */
    uint64_t order;             /**< Arrival count, which keeps commands of a frame in order */
    box_audio_timed_cmd_t cmd;  /**< The command */
} box_audio_scheduled_t;

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
    box_audio_synth_part_t synth_parts[BOX_AUDIO_SYNTH_PARTS]; /**< Layers of synth voices */
    uint16_t synth_free[BOX_AUDIO_SYNTH_PARTS]; /**< Indexes of the free parts, a stack */
    int32_t synth_free_count; /**< Free parts */
    box_audio_synth_string_t synth_strings[BOX_AUDIO_SYNTH_STRINGS]; /**< Pluck strings */
    uint16_t string_free[BOX_AUDIO_SYNTH_STRINGS]; /**< Indexes of the free strings, a stack */
    int32_t string_free_count; /**< Free strings */
    uint64_t clock;       /**< Frames rendered since the core was created */
    int32_t schedule_count; /**< Commands in schedule */
    uint64_t schedule_order; /**< Commands that entered the schedule so far */
    box_audio_scheduled_t schedule[BOX_AUDIO_SCHEDULE_MAX]; /**< Commands that wait for their
                           * frame, a binary heap ordered by frame, then by arrival */
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
/** Apply a synth command: give a free sound id a patch or replace the patch of a synth.
 *
 * @param core The core
 * @param cmd The command
 */
void box_audio_synth_command(
    box_audio_core_t *core,
    const box_audio_cmd_synth_t *cmd);

/** Set the parts of the synth layers and the pluck strings of a new core free.
 *
 * @param core The core, zeroed
 */
void box_audio_synth_parts_init(
    box_audio_core_t *core);

/** Whether the free parts and strings of a core hold the layers of the patch of a synth sound.
 *
 * @param core The core
 * @param sound Id of a ready synth sound
 * @return Whether a voice can play it
 */
bool box_audio_synth_fits(
    const box_audio_core_t *core,
    uint32_t sound);

/** Give the parts and the strings of a synth source back to its core.
 *
 * @param core The core
 * @param src The source
 */
void box_audio_synth_end(
    box_audio_core_t *core,
    box_audio_synth_source_t *src);

/** Start a synth source on the patch of a synth sound, with a part of the core for each of its
 * layers and a string for each pluck layer, which box_audio_synth_fits found. Takes no use of
 * the sound: the source plays a copy of the patch.
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

/** Render a block of a synth source into core->src_l, and core->src_r when it is stereo
 * (box_audio_synth_source_t.stereo).
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

/** Set the pitch ratio of a synth source: at once, or exponentially in frequency over the
 * glide of its patch, from where a glide that runs stands.
 *
 * @param src The source
 * @param pitch Pitch ratio
 */
void box_audio_synth_pitch(
    box_audio_synth_source_t *src,
    float pitch);

/** Retarget a synth source to the layers of another synth sound, a legato note: each layer
 * glides to the frequency of the same layer of the sound at a pitch, over its glide, without a
 * restart of the envelopes; one in its hold starts it over. Takes no use of the sound.
 *
 * @param core The core
 * @param src The source
 * @param sound Id of a ready synth sound
 * @param pitch Pitch ratio
 * @return Whether it retargeted; false when every envelope of the source releases or ended
 */
bool box_audio_synth_legato(
    box_audio_core_t *core,
    box_audio_synth_source_t *src,
    uint32_t sound,
    float pitch);

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
