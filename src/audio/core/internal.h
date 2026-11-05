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
 * find a part for each of its layers ends the quietest synth voices until it does */
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

#include "svf.h"

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

/** Input under this magnitude counts as silence for the effects with a tail */
#define BOX_AUDIO_EFFECT_FLOOR 1e-9f

/** A reverb: a feedback delay network of BOX_AUDIO_REVERB_LINES lines on the memory of its
 * effect slot, with a predelay line in front of it */
typedef struct box_audio_reverb_t {
    bool swap;                /**< Waits for the fade to reach 0 to apply the target */
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
    int32_t still;            /**< Frames in a row in which the lines summed under silence */
    box_audio_ramp_t mix;     /**< Wet level */
    box_audio_ramp_t fade;    /**< Fade of the wet signal around a swap */
    box_audio_ramp_t feed;    /**< Fade in of the input after a swap, so it enters the clear
                               * lines without a step */
} box_audio_reverb_t;

/** Coefficients of a biquad: b0, b1, b2, a1 and a2, normalized by a0 */
#define BOX_AUDIO_EQ_COEFS 5

/** A band of an equalizer: a biquad in transposed direct form II per channel, whose
 * coefficients move linearly to those of the last command */
typedef struct box_audio_eq_filter_t {
    float c[BOX_AUDIO_EQ_COEFS];     /**< Coefficients that run */
    float to[BOX_AUDIO_EQ_COEFS];    /**< Coefficients of the last command */
    float step[BOX_AUDIO_EQ_COEFS];  /**< Change of each coefficient per frame */
    int32_t left;             /**< Frames left of the move */
    float s[2][2];            /**< The two states of the left and the right channel */
    bool active;              /**< Whether it runs: it is not flat, moves or its state did not
                               * drain yet */
} box_audio_eq_filter_t;

/** An equalizer: BOX_AUDIO_EQ_BANDS biquads in a row */
typedef struct box_audio_eq_t {
    box_audio_eq_filter_t band[BOX_AUDIO_EQ_BANDS]; /**< The bands */
} box_audio_eq_t;

/** A filter effect: the state variable filter of the synth on both channels, its cutoff and
 * resonance smoothed per control block */
typedef struct box_audio_filter_t {
    box_audio_filter_params_t target; /**< Parameters of the last command, clamped */
    int32_t kind;             /**< A box_audio_synth_filter_t that runs */
    uint32_t slope;           /**< A box_audio_synth_slope_t that runs */
    int32_t stages;           /**< Stages that run, 1 or 2 */
    box_audio_svf_t svf[2];   /**< Stages of the left channel */
    box_audio_svf_t svf_r[2]; /**< Stages of the right channel */
    double octave;            /**< log2 of the cutoff that runs */
    double octave_target;     /**< log2 of the cutoff of the target */
    float resonance;          /**< Resonance that runs */
    float smooth;             /**< Share of the way to the target a control block moves */
    int32_t control;          /**< Frames until the next control block */
    bool moving;              /**< Whether the cutoff or the resonance moves */
    bool swap;                /**< Waits for the fade to reach 0 to apply a kind or a slope */
    box_audio_ramp_t mix;     /**< Wet level */
    box_audio_ramp_t drive;   /**< Saturation, 0 (linear) to 1 */
    box_audio_ramp_t fade;    /**< Fade of the wet signal around a swap */
} box_audio_filter_t;

/** The peak detector of the dynamics: the largest absolute sample of the louder channel over
 * the current window of BOX_AUDIO_DYNAMICS_WINDOW and the one before */
typedef struct box_audio_peak_t {
    float current;            /**< Largest sample of the current window */
    float last;               /**< Largest sample of the window before */
    int32_t left;             /**< Frames left of the current window */
    int32_t frames;           /**< Frames of a window */
} box_audio_peak_t;

/** A compressor: the peak detector, its level smoothed in dB and a soft knee gain computer */
typedef struct box_audio_compressor_t {
    box_audio_compressor_params_t target; /**< Parameters of the last command, clamped */
    box_audio_peak_t peak;    /**< Detector */
    double level;             /**< Detected level in dB from the start of the knee up, smoothed */
    double attack;            /**< Share of the distance that stays per frame on the way up */
    double release;           /**< Share of the distance that stays per frame on the way down */
    box_audio_ramp_t threshold; /**< Threshold in dB */
    box_audio_ramp_t knee;    /**< Knee in dB */
    box_audio_ramp_t slope;   /**< 1 / ratio - 1, the gain per dB over the threshold */
    box_audio_ramp_t makeup;  /**< Makeup in dB */
    box_audio_ramp_t mix;     /**< Wet level */
} box_audio_compressor_t;

/** A gate: the peak detector against two thresholds and a gain that moves to open or closed */
typedef struct box_audio_gate_t {
    box_audio_gate_params_t target; /**< Parameters of the last command, clamped */
    box_audio_peak_t peak;    /**< Detector */
    float open_level;         /**< Linear level at which it opens */
    float close_level;        /**< Linear level under which an open gate starts its hold */
    float floor;              /**< Linear gain when closed, the range */
    double attack;            /**< Share of the distance to open that stays per frame */
    double release;           /**< Share of the distance to closed that stays per frame */
    int32_t hold_frames;      /**< Frames of the hold */
    int32_t hold;             /**< Frames left of the hold of an open gate under close_level */
    bool open;                /**< Whether it is open */
    double gain;              /**< Linear gain that runs */
} box_audio_gate_t;

/** A read position of a delay: whole frames back and the fraction toward the frame before */
typedef struct box_audio_delay_tap_t {
    float time;               /**< Seconds it was set for */
    int32_t frames;           /**< Whole frames back, at least 1 */
    float frac;               /**< Share of the frame before it, 0..1 */
} box_audio_delay_tap_t;

/** A delay: a line per channel on the memory of its effect slot, read at one position, or at
 * two while a change of the time crossfades */
typedef struct box_audio_delay_t {
    box_audio_delay_params_t target; /**< Parameters of the last command, clamped */
    float *line[2];           /**< The lines of the left and the right channel in memory */
    int32_t length;           /**< Frames of each line */
    int32_t pos;              /**< Frame the next input is written to */
    int32_t valid;            /**< Frames written since the line was last silent; older frames
                               * read as silence, so the memory needs no clearing */
    box_audio_delay_tap_t from; /**< Read position a crossfade leaves */
    box_audio_delay_tap_t to; /**< Read position that runs, or a crossfade reaches */
    float pending;            /**< Time the next crossfade goes to, when it differs from to */
    bool crossfade;           /**< Whether it crossfades from from to to */
    box_audio_ramp_t xfade;   /**< Share of to in the output of a crossfade */
    float lp[2];              /**< State of the damping filter of each channel */
    float damp;               /**< Coefficient of the damping filters, 1 for none */
    int32_t still;            /**< Frames in a row in which it read under silence */
    box_audio_ramp_t feedback; /**< Feedback gain */
    box_audio_ramp_t pong;    /**< Share of the ping pong routing of the feedback, 0..1 */
    box_audio_ramp_t mix;     /**< Wet level */
    box_audio_ramp_t feed;    /**< Fade in of the input after a move */
} box_audio_delay_t;

/** A phaser: a chain of first order allpasses per channel, each a single state, swept by a
 * sine LFO whose phase is a fixed point count of turns */
typedef struct box_audio_phaser_t {
    box_audio_phaser_params_t target; /**< Parameters of the last command, clamped */
    int32_t stages;           /**< Allpasses per channel that run */
    float state[2][BOX_AUDIO_PHASER_STAGES_MAX]; /**< State of each allpass per channel */
    float last[2];            /**< Output of the chain of each channel in the frame before, which
                               * feeds back */
    float coef[2];            /**< Coefficient of the allpasses of each channel */
    uint32_t phase;           /**< Phase of the LFO in 2^-32 turns */
    uint32_t step;            /**< Phase it advances per frame */
    int32_t control;          /**< Frames until the next update of the coefficients */
    bool stale;               /**< Skipped blocks left the coefficients behind the phase */
    bool swap;                /**< Waits for the fade to reach 0 to apply the stages */
    box_audio_ramp_t depth;   /**< Depth, per control block */
    box_audio_ramp_t octave;  /**< log2 of the center, per control block */
    box_audio_ramp_t stereo;  /**< Phase offset of the right channel in turns, per control
                               * block */
    box_audio_ramp_t feedback; /**< Feedback gain */
    box_audio_ramp_t mix;     /**< Wet level */
    box_audio_ramp_t fade;    /**< Fade of the wet signal around a change of the stages */
} box_audio_phaser_t;

/** A chorus: a line per channel on the memory of its effect slot, read by up to
 * BOX_AUDIO_CHORUS_VOICES_MAX taps that a sine LFO each moves */
typedef struct box_audio_chorus_t {
    box_audio_chorus_params_t target; /**< Parameters of the last command, clamped */
    float *line[2];           /**< The lines of the left and the right channel in memory */
    int32_t length;           /**< Frames of each line */
    int32_t pos;              /**< Frame the next input is written to */
    int32_t valid;            /**< Frames written since the line was last silent; older frames
                               * read as silence, so the memory needs no clearing */
    int32_t voices;           /**< Taps that run */
    float gain;               /**< Gain of each tap, 1 / sqrt(voices) */
    double tap[BOX_AUDIO_CHORUS_VOICES_MAX];  /**< Frames back of each tap in this frame */
    double slope[BOX_AUDIO_CHORUS_VOICES_MAX]; /**< Change of each tap per frame up to the next
                               * control block */
    double next[BOX_AUDIO_CHORUS_VOICES_MAX]; /**< Frames back of each tap at the next control
                               * block */
    float pan[BOX_AUDIO_CHORUS_VOICES_MAX][4]; /**< Gains of each tap: left into left, right
                               * into left, left into right and right into right */
    uint32_t phase;           /**< Phase of the LFO of the first voice in 2^-32 turns */
    uint32_t step;            /**< Phase it advances per frame */
    int32_t control;          /**< Frames until the next control block */
    bool stale;               /**< Skipped blocks left the taps behind the phase */
    bool swap;                /**< Waits for the fade to reach 0 to apply the voices */
    box_audio_ramp_t depth;   /**< Depth in frames, per control block */
    box_audio_ramp_t delay;   /**< Delay in frames, per control block */
    box_audio_ramp_t spread;  /**< Spread, per control block */
    box_audio_ramp_t mix;     /**< Wet level */
    box_audio_ramp_t fade;    /**< Fade of the wet signal around a change of the voices */
    box_audio_ramp_t feed;    /**< Fade in of the input after a move */
} box_audio_chorus_t;

/** State of the kinds of effects */
typedef union box_audio_effect_data_t {
    box_audio_reverb_t reverb;  /**< BOX_AUDIO_EFFECT_REVERB */
    box_audio_eq_t eq;          /**< BOX_AUDIO_EFFECT_EQ */
    box_audio_filter_t filter;  /**< BOX_AUDIO_EFFECT_FILTER */
    box_audio_compressor_t compressor; /**< BOX_AUDIO_EFFECT_COMPRESSOR */
    box_audio_gate_t gate;      /**< BOX_AUDIO_EFFECT_GATE */
    box_audio_delay_t delay;    /**< BOX_AUDIO_EFFECT_DELAY */
    box_audio_phaser_t phaser;  /**< BOX_AUDIO_EFFECT_PHASER */
    box_audio_chorus_t chorus;  /**< BOX_AUDIO_EFFECT_CHORUS */
} box_audio_effect_data_t;

/** An effect instance of the pool of the core: a kind in an insert slot of a bus, on the
 * memory the transport gave its slot */
typedef struct box_audio_effect_t {
    uint32_t kind;            /**< Kind that runs, a box_audio_effect_kind_t */
    uint32_t bus;             /**< Bus it processes, BUS_NONE while its tail fades out into the
                               * master */
    uint32_t index;           /**< Insert slot within its bus */
    uint32_t key;             /**< Sidechain bus, BUS_NONE for none */
    uint32_t target_kind;     /**< Kind of the last command, NONE when it took the effect off */
    uint32_t target_bus;      /**< Bus of the last command, BUS_NONE when off */
    uint32_t target_index;    /**< Index of the last command */
    uint32_t target_key;      /**< Sidechain bus of the last command */
    float params[BOX_AUDIO_EFFECT_PARAMS]; /**< Parameters of the last command that put it on,
                               * as sent; the kind clamps them */
    float *memory;            /**< Memory of the slot, NULL until it arrives */
    int32_t floats;           /**< Floats of memory */
    bool attached;            /**< Whether it renders: on a bus, or its tail fades out */
    bool idle;                /**< A kind with a tail decayed to silence and is clear */
    bool moving;              /**< Fades out of its place to apply the target */
    bool entering;            /**< Blends in from the dry signal after it was put on a bus,
                               * unless its first block is silent */
    int32_t quiet;            /**< Frames of silent input in a row */
    box_audio_ramp_t blend;   /**< Share of its output in its bus, the rest is its input; the
                               * gain of a tail that fades out into the master */
    box_audio_effect_data_t data; /**< State of its kind */
} box_audio_effect_t;

/** A kind of effect: a row of the table of effect.c */
typedef struct box_audio_effect_def_t {
    bool tail;                /**< It rings on after its input stopped. Such a kind gets the
                               * silence rule: a block of silent input after it decayed is not
                               * rendered, which costs a scan of the input */
    int32_t (*floats)(float sample_rate);  /**< Floats of memory it needs, NULL for none */
    void (*init)(box_audio_core_t *core);  /**< Constants of a new core, NULL for none */
    void (*start)(box_audio_core_t *core, box_audio_effect_t *fx, bool fade_in);
                              /**< Start on clear memory at the params of fx, fading its input
                               * in when fade_in, as after a move */
    void (*command)(box_audio_core_t *core, box_audio_effect_t *fx);
                              /**< New params of fx while it runs: clamp and smooth them */
    bool (*render)(box_audio_core_t *core, box_audio_effect_t *fx, float *l, float *r,
        int32_t frames, bool silent);
                              /**< Process a block in place; silent tells that the input is
                               * silent. Returns whether a kind with a tail decayed: it cleared
                               * itself and is idle */
    void (*rest)(box_audio_core_t *core, box_audio_effect_t *fx, int32_t frames);
                              /**< A block of frames skipped while idle and the input is
                               * silent */
} box_audio_effect_def_t;

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
    bool gate;            /**< Sustains in its hold until a release, BOX_AUDIO_PLAY_GATE */
    int32_t release;      /**< Frames of the release a note release starts, at least the edge,
                           * also when a sustain of 0 skips the release of a pass */
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
    double shift;         /**< Turns its modulation and feedback moved its phase by in the
                           * last frame, for the speed of its edges */
    double width;         /**< Square: share of the period it is high */
    double width_base;    /**< Square: the pulse width of its layer, without the LFOs */
    double width_step;    /**< Change of width per frame from the LFOs */
    double vibrato;       /**< Factor of inc from the LFOs, 1 without */
    double vibrato_step;  /**< Change of vibrato per frame */
    float tremolo;        /**< Factor of its level from the LFOs, 1 without */
    float tremolo_step;   /**< Change of tremolo per frame */
    float sub;            /**< Level of its sub oscillator, 0 for none */
    int32_t sub_octave;   /**< Octaves the sub is under it, 1 or 2 */
    uint32_t wraps;       /**< Times its phase wrapped, the phase of the sub above its own */
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
    uint32_t wraps[BOX_AUDIO_SYNTH_UNISON_MAX]; /**< Times the phase of each voice wrapped,
                                                * for its sub */
} box_audio_synth_unison_t;
/** A pluck string of a synth voice: a Karplus-Strong loop of a delay line, a symmetric lowpass
 * of one frame of delay and a first order allpass of the fraction of a frame left, with a gain
 * that sets its decay, and the old read point that crossfades into the new one at a crossing.
 *
 * A pluck layer has one for each of its unison voices, chained by next */
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
    double period;        /**< Frames of the period it is tuned to */
    int32_t retune;       /**< Frames of a moving pitch until its next exact tuning */
    int32_t fade;         /**< Frames left of the crossfade from the old read point, 0 for none */
    int32_t delay_old;    /**< Frames of the delay line of the old read point */
    float c_old;          /**< Coefficient of the allpass of the old read point */
    float x1_old, x2_old; /**< The last two frames out of the old read point */
    float f1_old, a1_old; /**< The last input and output of the allpass of the old read point */
    bool rough;           /**< Whether a crossing of a whole frame jumps without the crossfade,
                           * as before it, a switch for tests that a pluck keeps */
    double decay_frames;  /**< Frames of its decay to -60 dB */
    double period_max;    /**< Longest period in frames, at BOX_AUDIO_SYNTH_PLUCK_MIN */
    float peak;           /**< Largest magnitude of the window that runs */
    int32_t window;       /**< Frames left of the window, a period */
    bool ringing;         /**< Whether it is still over the silence */
    struct box_audio_synth_string_t *next; /**< String of the next unison voice of its layer,
                                            * NULL for the last */
} box_audio_synth_string_t;

/** A layer of a synth voice: its layer of the patch, its oscillator and its own envelope. The
 * core holds BOX_AUDIO_SYNTH_PARTS of them for all its voices, so a voice holds only the layers
 * its patch has */
typedef struct box_audio_synth_part_t {
    box_audio_synth_layer_t layer;  /**< The layer of the patch, clamped */
    box_audio_synth_osc_t osc;      /**< The oscillator */
    box_audio_synth_env_t env;      /**< Its own envelope, when the layer has one */
    box_audio_synth_unison_t unison; /**< Its unison voices */
    box_audio_synth_string_t *string; /**< The string of its first unison voice, the others
                                       * chained after it, for a BOX_AUDIO_SYNTH_PLUCK layer;
                                       * NULL for another */
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
    float velocity;       /**< Velocity of the note that started it, 0..1; a legato keeps it */
    bool loop;            /**< Whether it starts over at the end of its envelopes */
    bool gate;            /**< Whether its envelopes sustain until a release */
    int32_t filter;       /**< A box_audio_synth_filter_t */
    int32_t stages;       /**< Stages of the filter: 1 for 12 dB per octave, 2 for 24 */
    box_audio_svf_t svf[2]; /**< The resonant stage, then the stage before it at 24 dB */
    box_audio_svf_t svf_r[2]; /**< The stages of the right channel, when stereo */
    float drive;          /**< Saturation of the filter, 0 (linear) to 1 */
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
    int32_t strings;      /**< Pluck layers, each with a string per unison voice, 0 for none */
    int32_t ringing;      /**< Strings that are still over the silence, of every unison voice */
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
    uint32_t priority;        /**< Priority, 0..BOX_AUDIO_PRIORITY_MAX: only a play of at
                               * least this priority steals it */
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
    int32_t strings;    /**< Strings a play of the patch takes: one for each unison voice of
                         * each of its pluck layers */
    bool used;          /**< Whether a sound has the slot */
} box_audio_synth_slot_t;

/** A command that takes effect on a frame: PLAY, STOP, VOICE_SET, BUS_SET, RELEASE or LEGATO */
typedef union box_audio_timed_cmd_t {
    box_audio_cmd_header_t header;  /**< The header, first in each of them */
    box_audio_cmd_play_t play;      /**< BOX_AUDIO_CMD_PLAY */
    box_audio_cmd_stop_t stop;      /**< BOX_AUDIO_CMD_STOP */
    box_audio_cmd_set_t set;        /**< BOX_AUDIO_CMD_VOICE_SET or BOX_AUDIO_CMD_BUS_SET */
    box_audio_cmd_voice_release_t release; /**< BOX_AUDIO_CMD_RELEASE */
    box_audio_cmd_legato_t legato;  /**< BOX_AUDIO_CMD_LEGATO */
} box_audio_timed_cmd_t;

/** A command that waits in the schedule for its frame */
typedef struct box_audio_scheduled_t {
    uint64_t when;              /**< Frame of the core clock it applies at */
    uint64_t order;             /**< Arrival count, which keeps commands of a frame in order */
    box_audio_timed_cmd_t cmd;  /**< The command */
} box_audio_scheduled_t;

/** A bus: a volume, a pan, a width, a mute, an output and up to BOX_AUDIO_SENDS sends, with a
 * block of samples */
typedef struct box_audio_bus_t {
    float volume;            /**< Volume */
    float pan;               /**< Pan, -1..1 */
    float width;             /**< Stereo width, 0..2 */
    bool mute;               /**< Whether it is muted */
    uint8_t pre_fader;       /**< Pre-fader sends of its route, bit j for send j */
    float send_level[BOX_AUDIO_SENDS]; /**< Level of each send */
    box_audio_ramp_t left;   /**< Gain of the left channel: volume, pan and mute */
    box_audio_ramp_t right;  /**< Gain of the right channel */
    box_audio_ramp_t side;   /**< Gain of the side signal, the width */
    box_audio_ramp_t send[BOX_AUDIO_SENDS]; /**< Gain of each send, with the mute on a
                              *   pre-fader one */
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
    int32_t refused;      /**< Plays refused so far because every playing voice ranked
                           * higher */
    box_audio_bus_t buses[BOX_AUDIO_BUS_MAX];       /**< Buses, 0 is the master */
    box_audio_route_t order[BOX_AUDIO_BUS_MAX];     /**< Processing order, the master last */
    int32_t order_count;  /**< Routes in order */
    bool bus_active[BOX_AUDIO_BUS_MAX];             /**< Whether a bus is in the order */
    uint8_t bus_position[BOX_AUDIO_BUS_MAX];        /**< Position of each bus in the order */
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
    box_audio_effect_t effects[BOX_AUDIO_EFFECT_MAX]; /**< Effects by slot */
    uint64_t bus_effects[BOX_AUDIO_BUS_MAX]; /**< Effects that run on each bus, a bit per slot */
    uint64_t effect_tails; /**< Effects whose tail fades out into the master, a bit per slot */
    int32_t effect_fade;  /**< Frames of half the fade of a move */
    float effect_l[BOX_AUDIO_CORE_BLOCK]; /**< Left input of an effect that blends, or output of
                           * a tail */
    float effect_r[BOX_AUDIO_CORE_BLOCK]; /**< Right input or output */
    float reverb_smooth;  /**< Share of the way to their targets a decay and a damping move
                           * per block */
    box_audio_meter_t meters[BOX_AUDIO_BUS_MAX]; /**< Levels of each bus since the last read */
    box_audio_effect_meter_t effect_meters[BOX_AUDIO_EFFECT_MAX]; /**< Values of each effect
                           * since the last read */
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

/** Advance a ramp by the frames of a block without using its values, frame by frame so that
 * it ends the same whatever the render sizes; nothing once it reached its target.
 *
 * @param ramp The ramp
 * @param frames Frames of the block
 */
static inline void box_audio_ramp_skip(
    box_audio_ramp_t *ramp,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames && ramp->left > 0; i ++) {
        box_audio_ramp_next(ramp);
    }
}

/** Report the gain reduction of an effect in a block, for its meter: the largest since the
 * last read is kept.
 *
 * @param core The core
 * @param fx The effect, in core->effects
 * @param db Reduction in dB, positive; anything else counts as none
 */
static inline void box_audio_effect_reduction(
    box_audio_core_t *core,
    const box_audio_effect_t *fx,
    float db)
{
    box_audio_effect_meter_t *m = &core->effect_meters[fx - core->effects];
    if (db > m->reduction && db <= BOX_AUDIO_HUGE) {
        m->reduction = db;
    }
}

/** Set the effects of a new core to no kind and no memory, and the constants of the kinds.
 *
 * @param core The core, zeroed
 */
void box_audio_effects_init(
    box_audio_core_t *core);

/** Apply an effect command.
 *
 * @param core The core
 * @param cmd The command
 */
void box_audio_effect_command(
    box_audio_core_t *core,
    const box_audio_cmd_effect_t *cmd);

/** Let the effects that fade out of a bus that left the order fade out into the master
 * instead, after an ORDER command. An effect that stays on such a bus waits with it.
 *
 * @param core The core
 */
void box_audio_effects_order(
    box_audio_core_t *core);

/** Run the inserts of a bus over a block of it, in place, in the order of their index. On the
 * master it also adds the tails that fade out after their bus went.
 *
 * @param core The core
 * @param bus Index of the bus
 * @param l Left samples of the bus
 * @param r Right samples of the bus
 * @param frames Frames of the block
 */
void box_audio_effects_render(
    box_audio_core_t *core,
    uint32_t bus,
    float *l,
    float *r,
    int32_t frames);

/** The reverb kind, in reverb.c: floats of its memory at a rate */
int32_t box_audio_reverb_floats(
    float sample_rate);

/** The reverb kind: the smoothing of a core */
void box_audio_reverb_init(
    box_audio_core_t *core);

/** The reverb kind: start on clear memory */
void box_audio_reverb_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The reverb kind: new parameters while it runs */
void box_audio_reverb_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The reverb kind: process a block */
bool box_audio_reverb_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The reverb kind: a block skipped while idle */
void box_audio_reverb_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The delay kind, in delay.c: floats of its memory at a rate */
int32_t box_audio_delay_floats(
    float sample_rate);

/** The delay kind: start on its memory */
void box_audio_delay_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The delay kind: new parameters while it runs */
void box_audio_delay_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The delay kind: process a block */
bool box_audio_delay_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The delay kind: a block skipped while idle */
void box_audio_delay_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The phaser kind, in phaser.c: start at the params of fx */
void box_audio_phaser_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The phaser kind: new parameters while it runs */
void box_audio_phaser_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The phaser kind: process a block */
bool box_audio_phaser_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The phaser kind: a block skipped while idle */
void box_audio_phaser_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The chorus kind, in chorus.c: floats of its memory at a rate */
int32_t box_audio_chorus_floats(
    float sample_rate);

/** The chorus kind: start on its memory */
void box_audio_chorus_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The chorus kind: new parameters while it runs */
void box_audio_chorus_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The chorus kind: process a block */
bool box_audio_chorus_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The chorus kind: a block skipped while idle */
void box_audio_chorus_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The equalizer kind, in eq.c: start at the params of fx */
void box_audio_eq_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The equalizer kind: new parameters while it runs */
void box_audio_eq_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The equalizer kind: process a block */
bool box_audio_eq_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The equalizer kind: a block skipped while idle, which it never is */
void box_audio_eq_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The filter kind, in filter.c: start at the params of fx */
void box_audio_filter_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The filter kind: new parameters while it runs */
void box_audio_filter_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The filter kind: process a block */
bool box_audio_filter_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The filter kind: a block skipped while idle, which it never is */
void box_audio_filter_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** The compressor kind, in dynamics.c: start at the params of fx */
void box_audio_compressor_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The compressor kind: new parameters while it runs */
void box_audio_compressor_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The compressor kind: process a block, keyed on the output of its key bus when that came
 * before its bus in the order */
bool box_audio_compressor_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The gate kind, in dynamics.c: start at the params of fx */
void box_audio_gate_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in);

/** The gate kind: new parameters while it runs */
void box_audio_gate_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx);

/** The gate kind: process a block */
bool box_audio_gate_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent);

/** The compressor and the gate: a block skipped while idle, which they never are */
void box_audio_dynamics_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames);

/** Base 2 logarithm without libm, in synth.c, from the exponent of the double and the series
 * of atanh.
 *
 * @param x A positive finite value
 * @return The logarithm, within 1e-12
 */
double box_audio_synth_log2(
    double x);

/** Power of 2 without libm, in synth.c.
 *
 * @param y The exponent, clamped to -1000..1000
 * @return 2 to the y
 */
double box_audio_synth_exp2(
    double y);

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

/** The parts of the core a voice of a synth sound takes.
 *
 * @param core The core
 * @param sound Id of a ready synth sound
 * @return Number of parts
 */
int32_t box_audio_synth_parts(
    const box_audio_core_t *core,
    uint32_t sound);

/** Whether the free strings of a core hold the pluck layers of a synth sound.
 *
 * @param core The core
 * @param sound Id of a ready synth sound
 * @return Whether they do
 */
bool box_audio_synth_strings_fit(
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
 * layers and a string for each unison voice of each pluck layer, which box_audio_synth_fits
 * found. Takes no use of the sound: the source plays a copy of the patch.
 *
 * @param core The core
 * @param source The source
 * @param sound Id of a ready synth sound
 * @param pitch Pitch ratio
 * @param velocity Velocity of the note, 0..1, for the velocity fields of the patch
 * @param loop Whether it starts over at the end of its envelopes
 * @param gate Whether its envelopes sustain until a release, BOX_AUDIO_PLAY_GATE
 */
void box_audio_source_synth(
    box_audio_core_t *core,
    box_audio_source_t *source,
    uint32_t sound,
    float pitch,
    float velocity,
    bool loop,
    bool gate);

/** Gate or ungate the envelopes of a synth source that have not reached their release: a
 * gated one sustains in its hold until a release, an ungated one in its hold holds for the
 * hold of its envelope from now. An envelope that already has the gate stays as it is.
 *
 * @param src The source
 * @param gate Whether to gate them
 */
void box_audio_synth_gate(
    box_audio_synth_source_t *src,
    bool gate);

/** End the note of a synth source: every envelope it has, the filter envelope included, that
 * has not reached its release moves into it from its current level, curves included, and the
 * source stops looping.
 *
 * @param src The source
 */
void box_audio_synth_release(
    box_audio_synth_source_t *src);

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
