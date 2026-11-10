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

#include "clean.h"
#include "ramp.h"
#include "svf.h"
#include "formant.h"
#include "ladder.h"
#include "saturate.h"
#include "shift.h"

#include "effect_state.h"

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
    double bend;       /**< Factor of the pitch from the bend of the voice, 1 without */
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

/** A curve from one value to another over a number of steps, linear or curved as a segment of
 * an envelope, the last step taking the target */
typedef struct box_audio_synth_curve_t {
    double level;         /**< Current value */
    double target;        /**< Value at the end */
    double mul;           /**< Factor per step, 1 for a linear curve */
    double add;           /**< Term per step */
    int32_t left;         /**< Steps left, 0 once it is at its target */
} box_audio_synth_curve_t;

/** The pitch envelope of a layer of a synth voice, advanced per control block of
 * BOX_AUDIO_SYNTH_CONTROL frames with the LFOs */
typedef struct box_audio_synth_bend_t {
    bool on;              /**< Whether the layer has a pitch envelope that moves something */
    box_audio_synth_curve_t decay;   /**< Share of amount and sync left, from 1 to 0 */
    box_audio_synth_curve_t release; /**< Share of the way left to release_amount, from 1 to
                                      * 0, once released */
    bool released;        /**< Whether the amplitude envelope of the layer reached its release */
    double from;          /**< Semitones of the offset at the start of the release */
} box_audio_synth_bend_t;

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
    bool synced;          /**< Whether its wave is hard synced to its phase, the master: a
                           * ratio over 1 or an LFO of its own on the ratio */
    double sync;          /**< Ratio of its wave to its phase, 1..BOX_AUDIO_SYNTH_SYNC_MAX */
    double sync_base;     /**< The ratio of its layer, without the LFOs; what moves the ratio
                           * per control block aims from it */
    double sync_step;     /**< Change of sync per frame from the LFOs */
    const float *table;   /**< Wavetable: the levels of its table after the header, NULL for
                           * none, which renders a sine */
    int32_t table_frames; /**< Wavetable: frames of its table */
    uint32_t table_sound; /**< Wavetable: id of the sound of its table, whose users it counts,
                           * 0 for none */
    double position;      /**< Wavetable: frame it reads at in this frame, 0..frames - 1 */
    double position_base; /**< Wavetable: position of its layer, clamped */
    double position_lfo;  /**< Wavetable: frames the LFOs move it by */
    double position_step; /**< Wavetable: change of position_lfo per frame */
    float position_env;   /**< Wavetable: frames it moves at level 1 of its envelope */
    float index_route;    /**< Radians the routes add to the index of a modulator, or to the
                           * depth of a ring modulator; 0 without */
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

/** A smooth random walk of the analog drift of a synth voice, -1..1: a random target held
 * for a random 0.2 to 2 seconds, which two one pole lowpasses follow per control block */
typedef struct box_audio_synth_walk_t {
    float target;         /**< Value it moves toward, -1..1 */
    float a;              /**< Output of the first lowpass */
    float value;          /**< Output of the second, the value, -1..1 */
    int32_t left;         /**< Control blocks until the next target */
} box_audio_synth_walk_t;

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
    box_audio_synth_walk_t walk[BOX_AUDIO_SYNTH_UNISON_MAX]; /**< Drift of each voice */
    double drift[BOX_AUDIO_SYNTH_UNISON_MAX];  /**< Factor of the pitch of each voice from its
                                                * drift, with drift */
    double drift_step[BOX_AUDIO_SYNTH_UNISON_MAX]; /**< Change of drift per frame */
    double spread;        /**< Cents from the lowest voice to the highest that detune holds,
                           * the routes included */
} box_audio_synth_unison_t;
/** Most sections of the filter that colors noise */
#define BOX_AUDIO_SYNTH_COLOR_SECTIONS 10

/** The filter that colors the noise of a layer of a synth voice, pink or brown.
 *
 * A cascade of first order sections y = x - zero x1 + pole y1 with poles and zeros mapped to
 * the rate, times a gain that matches the RMS level of white noise. Each unison voice filters
 * its own noise with its own state; a layer without unison uses that of the first */
typedef struct box_audio_synth_color_filter_t {
    int32_t sections;     /**< Sections, 0 for white noise, which it leaves alone */
    float zero[BOX_AUDIO_SYNTH_COLOR_SECTIONS]; /**< Zero of each section */
    float pole[BOX_AUDIO_SYNTH_COLOR_SECTIONS]; /**< Pole of each section */
    float gain;           /**< Gain of the output, 1 over the RMS gain of the sections */
    float state[BOX_AUDIO_SYNTH_UNISON_MAX][BOX_AUDIO_SYNTH_COLOR_SECTIONS + 1]; /**< Per
                           * unison voice: its last noise, then the last output of each
                           * section, the last input of the next */
} box_audio_synth_color_filter_t;

/** A string of a synth voice, of a pluck or a bowed layer.
 *
 * A pluck string is a Karplus-Strong loop of a delay line, a symmetric lowpass of one frame of
 * delay and a first order allpass of the fraction of a frame left, with a gain that sets its
 * decay, and the old read point that crossfades into the new one at a crossing.
 *
 * A bowed string is a waveguide split at the bow: line is the round trip to the nut, bridge the
 * one to the bridge, read with 3rd order Lagrange interpolation; the bow joins them through its
 * friction. A pluck or bowed layer has one per unison voice, chained by next */
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
    float bridge[BOX_AUDIO_SYNTH_STRING_FRAMES]; /**< Bowed: the delay line of the bridge side,
                           * a ring written at write as line, which holds the nut side */
    double loop;          /**< Bowed: frames of both delay lines together, the period less
                           * the phase delay of the lowpass at the fundamental */
    double lag;           /**< Bowed: phase delay of the lowpass at the fundamental in frames,
                           * less the advance of the corner by the friction */
    float tone;           /**< Bowed: octaves the brightness of its layer raises its lowpass */
    float pressure;       /**< Bowed: pressure of the bow of its last exact tuning */
    float a;              /**< Bowed: share of the way the lowpass moves per frame, 1 - pole */
    float lp;             /**< Bowed: last output of the lowpass */
    box_audio_dc_t dc;    /**< Bowed: DC blocker of the output */
    bool bowed;           /**< Bowed: a frame of the window that runs had the bow on it */
    struct box_audio_synth_string_t *next; /**< String of the next unison voice of its layer,
                                            * NULL for the last */
} box_audio_synth_string_t;

/** The bow of a BOX_AUDIO_SYNTH_BOWED layer of a synth voice, which its strings share: its
 * pressure, speed and position, those of the layer moved by the routes, which the values its
 * strings play at follow smoothly. */
typedef struct box_audio_synth_bow_t {
    float pressure;       /**< Pressure of the layer, 0..1, velocity included */
    float speed;          /**< Speed of the layer, 0..1, velocity included */
    float position;       /**< Position of the layer, a share of the string */
    float noise;          /**< Level of the noise of the hair, the noise of the layer times
                           * BOX_AUDIO_SYNTH_BOW_HAIR */
    float pressure_aim;   /**< Pressure the routes give, clamped 0..1 */
    float speed_aim;      /**< Speed the routes give, clamped 0..1 */
    float position_aim;   /**< Position the routes give, clamped */
    float pressure_play;  /**< Pressure that plays */
    float speed_play;     /**< Speed that plays */
    float position_play;  /**< Position that plays, a share of the loop */
    float slope;          /**< Slope of the friction curve of pressure_play times the velocity
                           * of the bow: the stick region is the narrower the steeper it is */
    float smooth;         /**< Share of the way the values that play move to their aims per
                           * frame */
    float dc;             /**< Pole of the DC blockers of the outputs of its strings */
} box_audio_synth_bow_t;

/** The delay line of a channel of the comb filter of a synth voice, and the state of the one
 * pole lowpass in its loop. A voice with a BOX_AUDIO_SYNTH_COMB filter takes one of the core
 * for each of its channels */
typedef struct box_audio_synth_comb_t {
    float line[BOX_AUDIO_SYNTH_COMB_FRAMES]; /**< The delay line of the output, a ring */
    int32_t write;        /**< Index the next frame is written to */
    float damp;           /**< Last output of the lowpass in the loop */
    float peak;           /**< Largest magnitude of the output of the window that runs */
} box_audio_synth_comb_t;

/** A layer of a synth voice: its layer of the patch, its oscillator and its own envelope. The
 * core holds BOX_AUDIO_SYNTH_PARTS of them for all its voices, so a voice holds only the layers
 * its patch has */
typedef struct box_audio_synth_part_t {
    box_audio_synth_layer_t layer;  /**< The layer of the patch, clamped */
    box_audio_synth_osc_t osc;      /**< The oscillator */
    box_audio_synth_env_t env;      /**< Its own envelope, when the layer has one */
    box_audio_synth_bend_t bend;    /**< Its pitch envelope, its own or that of the patch */
    box_audio_synth_unison_t unison; /**< Its unison voices */
    box_audio_synth_string_t *string; /**< The string of its first unison voice, the others
                                       * chained after it, for a BOX_AUDIO_SYNTH_PLUCK or
                                       * BOX_AUDIO_SYNTH_BOWED layer; NULL for another */
    box_audio_synth_bow_t bow;      /**< Its bow, for a BOX_AUDIO_SYNTH_BOWED layer */
    box_audio_synth_color_filter_t color; /**< The filter that colors its noise */
    box_audio_synth_walk_t walk;    /**< Drift of its pitch, without unison voices */
    double start;                   /**< Phase its oscillator starts at in turns, 0 without
                                     * free_phase */
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
    box_audio_formant_t formant; /**< The formant filter, when the filter is
                           * BOX_AUDIO_SYNTH_FORMANT */
    float vowel;          /**< Vowel of the patch, of a formant filter */
    float vowel_amount;   /**< Vowels of the filter envelope at its level 1, 0 without a formant
                           * filter */
    bool lfo_vowel;       /**< Whether an LFO moves the vowel of a formant filter */
    double lfo_vowels;    /**< Vowels the LFOs move the vowel by, 0 without */
    bool fenv_on;         /**< Whether the filter envelope runs: amount or vowel_amount not 0 */
    box_audio_svf_t svf[2]; /**< The resonant stage, then the stage before it at 24 dB */
    box_audio_svf_t svf_r[2]; /**< The stages of the right channel, when stereo */
    float drive;          /**< Saturation of the filter, 0 (linear) to 1 */
    int32_t model;        /**< A box_audio_synth_model_t of the filter */
    box_audio_ladder_coefs_t ladder_coefs; /**< Coefficients of the ladder, when the model */
    box_audio_ladder_t ladder[2]; /**< The ladder of the left channel, then the right one */
    bool has_drive;       /**< Whether the mix of the layers runs through the drive before the
                           * filter */
    bool drive_dc;        /**< Whether the drive has an asymmetry, so a DC blocker follows it */
    float drive_dc_coef;  /**< Pole of the DC blocker of the drive */
    box_audio_saturate_curve_t drive_curve; /**< Clipper of the drive */
    box_audio_oversample_t drive_os[2]; /**< Oversampler of the drive, left then right */
    box_audio_dc_t drive_blocker[2]; /**< DC blocker of the drive, left then right */
    bool unison;          /**< Whether a layer has unison voices, which renders through
                           * box_audio_synth_render_wide */
    bool stereo;          /**< Whether a layer with unison voices has width, so the source
                           * renders a right channel too */
    double cutoff;        /**< Cutoff in Hz */
    double cutoff_base;   /**< Cutoff of the patch, clamped, in Hz */
    double cutoff_top;    /**< Highest cutoff in Hz, under Nyquist */
    float amount;         /**< Octaves of the filter envelope, 0 without one */
    box_audio_synth_env_t fenv; /**< The filter envelope, when fenv_on */
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
    bool bends;           /**< Whether a layer has a pitch envelope, which runs with the LFOs */
    bool mods;            /**< Whether LFOs, pitch envelopes or drift run their control
                           * blocks and move the vibrato and tremolo of each layer; false
                           * renders as without them */
    double drift;         /**< Cents of the drift of the pitch, 0 without, which renders as
                           * before */
    bool drifts;          /**< Whether drift is over 0, so unison voices take their own */
    uint32_t drift_rng;   /**< State of the drift and the free phases, never 0 */
    float drift_coef;     /**< Share of the way each lowpass of a walk moves per control block */
    box_audio_synth_walk_t drift_cutoff; /**< Drift of the cutoff, a quarter of the pitch */
    float keytrack;       /**< Share 0..1 of the key in octaves the cutoff follows, 0 without a
                           * filter */
    double key;           /**< Key of the voice in octaves, log2 of its pitch ratio without the
                           * variance, moving with a glide */
    double key_target;    /**< Key the voice moves to */
    double key_step;      /**< Change of key per frame while it moves */
    int32_t key_left;     /**< Frames left until key reaches key_target, 0 when it stands or
                           * keytrack is 0 */
    int32_t key_control;  /**< Frames until the filter takes the key while it moves */
    float tremolo;        /**< Factor of the output from the LFOs of every layer, 1 without */
    float tremolo_step;   /**< Change of tremolo per frame */
    int32_t strings;      /**< Pluck and bowed layers, each with a string per unison voice, 0
                           * for none */
    int32_t ringing;      /**< Strings that are still over the silence, of every unison voice */
    int32_t ringing_free; /**< Strings of layers without an envelope still over the silence */
    int32_t bowing;       /**< Bowed strings still over the silence or under the bow, which keep
                           * the voice playing once its envelopes ended */
    bool tables;          /**< Whether a layer plays a wavetable, whose position moves per
                           * frame */
    double bend;          /**< Factor of the pitch from the bend of the voice, 1 without; the
                           * turns of the oscillators and the key carry it */
    double mod;           /**< Factor of the depth of the pitch of the LFOs from the mod of the
                           * voice, 1 without */
    float pressure;       /**< Pressure of the voice, 0..1, 0 without: the cutoff moves up by
                           * BOX_AUDIO_SYNTH_PRESSURE_OCTAVES times it */
    box_audio_synth_comb_t *comb[2]; /**< The delay lines of a BOX_AUDIO_SYNTH_COMB filter, of
                           * the left or mono channel, then of the right one when stereo; NULL
                           * for none */
    float comb_feedback;  /**< Feedback of the comb */
    float comb_gain;      /**< Gain of the output of the comb, 1 - |feedback| */
    double comb_tune;     /**< Ratio of the frequency of the comb to the note */
    double comb_pitch;    /**< Factor of the note from the LFOs of every layer and the pitch
                           * envelope of the first layer, 1 without */
    double comb_a;        /**< Share of the way the lowpass of the loop moves per frame, 1
                           * undamped */
    double comb_delay;    /**< Frames of the delay the comb reads at */
    double comb_step;     /**< Change of comb_delay per frame toward its aim */
    int32_t comb_control; /**< Frames until the comb aims at the delay of the note again */
    int32_t comb_window;  /**< Frames left of the window over which the comb rings on once
                           * the envelopes have ended, a period */
    bool comb_ringing;    /**< Whether the comb is still over the silence */
    int32_t routes;       /**< Routes of the modulation matrix that run, 0 without, which
                           * renders as before */
    box_audio_synth_route_t route[BOX_AUDIO_SYNTH_ROUTES]; /**< The routes of the patch */
    uint32_t route_sources; /**< Bit of each box_audio_synth_source_kind_t a route uses */
    uint32_t route_targets; /**< Bit of each box_audio_synth_target_t a route moves */
    float route_random;   /**< The random source of the play, -1..1 */
    box_audio_synth_envelope_t mod_envelope; /**< Mod envelope of the patch */
    box_audio_synth_env_t menv; /**< The mod envelope, when menv_on */
    bool menv_on;         /**< Whether a route uses the mod envelope, which then runs */
    bool fenv_routed;     /**< Whether the filter envelope runs for the routes alone, and so
                           * advances with the mod envelope instead of in the filter */
    uint32_t slope;       /**< A box_audio_synth_slope_t of the filter */
    float resonance_base; /**< Resonance of the patch, clamped */
    float resonance;      /**< Resonance the filter holds, the routes included */
    bool panned;          /**< Whether a route moves the pan, so the source renders a right
                           * channel too */
    float pan_l;          /**< Gain of the left channel from the pan of the routes */
    float pan_r;          /**< Gain of the right channel */
    float pan_l_step;     /**< Change of pan_l per frame */
    float pan_r_step;     /**< Change of pan_r per frame */
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
    box_audio_ramp_t bend;    /**< Pitch bend in semitones, BOX_AUDIO_PARAM_BEND */
    box_audio_ramp_t mod;     /**< Mod, BOX_AUDIO_PARAM_MOD, 1 without */
    box_audio_ramp_t pressure; /**< Pressure, BOX_AUDIO_PARAM_PRESSURE */
    bool expressive;          /**< Whether a ramp of its expression runs, which renders it in
                               * blocks of BOX_AUDIO_EXPRESS_FRAMES; false renders as before */
    float cutoff;             /**< Cutoff of BOX_AUDIO_PARAM_CUTOFF in Hz, clamped, 0 open */
    box_audio_ramp_t lowpass; /**< log2 of the g of the low-pass, tan(pi cutoff / rate) */
    bool filtered;            /**< Whether the low-pass runs; false skips it */
    bool fresh;               /**< Whether it has not rendered yet, so a cutoff applies at once */
    bool prime;               /**< Whether the low-pass started on a playing voice and takes the
                               * next input as its state */
    box_audio_svf_t lp[2];    /**< Low-pass of the left and right channel, a mono source uses
                               * the left */
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
    int32_t table;      /**< Frames of the wavetable its frames hold, 0 when they are not
                         * one */
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
                         * each of its pluck and bowed layers */
    int32_t combs;      /**< Comb lines a play of the patch takes: 0 without a comb filter, 1
                         * for one, 2 when it renders in stereo */
    bool used;          /**< Whether a sound has the slot */
    box_audio_saturate_curve_t drive; /**< Clipper of the drive of the patch, when it has one */
} box_audio_synth_slot_t;

/** A command that takes effect on a frame: PLAY, STOP, VOICE_SET, BUS_SET, RELEASE, LEGATO,
 * EFFECT_PARAMS, VOICE_RAMP, BEAT or MACRO */
typedef union box_audio_timed_cmd_t {
    box_audio_cmd_header_t header;  /**< The header, first in each of them */
    box_audio_cmd_play_t play;      /**< BOX_AUDIO_CMD_PLAY */
    box_audio_cmd_stop_t stop;      /**< BOX_AUDIO_CMD_STOP */
    box_audio_cmd_set_t set;        /**< BOX_AUDIO_CMD_VOICE_SET or BOX_AUDIO_CMD_BUS_SET */
    box_audio_cmd_voice_release_t release; /**< BOX_AUDIO_CMD_RELEASE */
    box_audio_cmd_legato_t legato;  /**< BOX_AUDIO_CMD_LEGATO */
    box_audio_cmd_effect_params_t effect; /**< BOX_AUDIO_CMD_EFFECT_PARAMS */
    box_audio_cmd_ramp_t ramp;      /**< BOX_AUDIO_CMD_VOICE_RAMP */
    box_audio_cmd_beat_t beat;      /**< BOX_AUDIO_CMD_BEAT */
    box_audio_cmd_macro_t macro;    /**< BOX_AUDIO_CMD_MACRO */
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
    bool loudness;           /**< Its loudness meter runs, BOX_AUDIO_PARAM_LOUDNESS */
    double kweight[2][2][4]; /**< K-weighting of each channel: of the shelf and the high pass,
                              *   x[n-1], x[n-2], y[n-1] and y[n-2] */
} box_audio_bus_t;

/** Parameters of a bus a modulator moves, by the index the modulators use */
typedef enum box_audio_mod_bus_param_t {
    BOX_AUDIO_MOD_BUS_VOLUME = 0,   /**< Volume */
    BOX_AUDIO_MOD_BUS_PAN = 1,      /**< Pan */
    BOX_AUDIO_MOD_BUS_WIDTH = 2,    /**< Width */
    BOX_AUDIO_MOD_BUS_SEND = 3,     /**< Level of send 0, the others after it */
    BOX_AUDIO_MOD_BUS_PARAMS = 3 + BOX_AUDIO_SENDS /**< Number of them */
} box_audio_mod_bus_param_t;

/** A modulator of the core */
typedef struct box_audio_mod_t {
    box_audio_cmd_modulator_t cmd; /**< Its settings, clamped; target NONE for a free slot */
    box_audio_cmd_modulator_t next; /**< Settings it takes once it faded out of its target */
    bool on;                  /**< It runs or fades out */
    bool pending;             /**< next waits for the fade out */
    uint32_t bus_param;       /**< box_audio_mod_bus_param_t of a bus target */
    float gain0;              /**< Fade at fade_at, 0..1 */
    float fade_dir;           /**< Change of the fade per frame: positive while it fades in,
                               * negative while it fades out */
    uint64_t fade_at;         /**< Frame of gain0 */
    double cycles0;           /**< Cycles, or steps, of a free running source at cycles_at */
    uint64_t cycles_at;       /**< Frame of cycles0 */
    float env;                /**< Envelope of a follower, linear */
    float env_rise;           /**< Share of the way to a louder sample the envelope moves per
                               * frame */
    float env_fall;           /**< Share of the way to a quieter sample */
} box_audio_mod_t;

/** A map of a macro in the core */
typedef struct box_audio_macro_map_t {
    box_audio_cmd_macro_map_t cmd; /**< Its settings, clamped; target NONE for a free slot */
    box_audio_cmd_macro_map_t next; /**< Settings it takes once it faded out of its target */
    bool on;                  /**< It runs or fades out */
    bool pending;             /**< next waits for the fade out */
    bool held;                /**< It fades out at hold rather than at the value of its macro,
                               * which another macro may take over */
    float hold;               /**< Value of its macro when it was released, 0..1 */
    uint32_t bus_param;       /**< box_audio_mod_bus_param_t of a bus target */
    float gain0;              /**< Fade at fade_at, 0..1 */
    float fade_dir;           /**< Change of the fade per frame: positive while it fades in,
                               * negative while it fades out */
    uint64_t fade_at;         /**< Frame of gain0 */
} box_audio_macro_map_t;

/** The modulators of a core and what they move */
typedef struct box_audio_mods_t {
    box_audio_mod_t mod[BOX_AUDIO_MOD_MAX]; /**< Modulators by slot */
    uint64_t on;              /**< Modulators that run or fade out, a bit per slot */
    uint64_t buses;           /**< Buses the modulators moved at the last update, a bit each */
    uint64_t effects;         /**< Effect slots the modulators moved at the last update */
    uint64_t keys;            /**< Buses a follower measures, a bit each */
    uint64_t next;            /**< Frame of the next update */
    uint64_t edge;            /**< Frame of the next edge of a hard wave or a step */
    bool dirty;               /**< Update before the next frame renders */
    float bus_add[BOX_AUDIO_BUS_MAX][BOX_AUDIO_MOD_BUS_PARAMS]; /**< What the modulators add to
                               * each parameter of each bus */
    float fx_add[BOX_AUDIO_EFFECT_MAX][BOX_AUDIO_EFFECT_PARAMS]; /**< What they add to each
                               * parameter of each effect slot, linearly */
    float fx_octaves[BOX_AUDIO_EFFECT_MAX][BOX_AUDIO_EFFECT_PARAMS]; /**< Octaves they move each
                               * parameter of each effect slot by */
    float fx_macro[BOX_AUDIO_EFFECT_MAX][BOX_AUDIO_EFFECT_PARAMS]; /**< What the maps of the
                               * macros move each parameter of each effect slot by, from the
                               * value of the commands to theirs */
    float macro_value[BOX_AUDIO_MACRO_MAX]; /**< Value of each macro from the MACRO commands,
                               * 0..1 */
    float macro_add[BOX_AUDIO_MACRO_MAX]; /**< What the modulators added to each macro at the
                               * last update */
    box_audio_macro_map_t map[BOX_AUDIO_MACRO_MAP_MAX]; /**< Maps of the macros by slot */
    int32_t maps;             /**< Maps that run or fade out */
    uint64_t macro_buses;     /**< Buses a map moved at the last update, which ramp over at
                               * least the ramp of a set */
} box_audio_mods_t;

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
    double kweight[2][5]; /**< Coefficients of the K-weighting at the rate: of the shelf and
                           * the high pass, b0, b1, b2, a1 and a2 */
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
    box_audio_synth_comb_t synth_combs[BOX_AUDIO_SYNTH_COMBS]; /**< Delay lines of comb
                           * filters */
    uint16_t comb_free[BOX_AUDIO_SYNTH_COMBS]; /**< Indexes of the free comb lines, a stack */
    int32_t comb_free_count; /**< Free comb lines */
    uint64_t clock;       /**< Frames rendered since the core was created */
    int32_t schedule_count; /**< Commands in schedule */
    uint64_t schedule_order; /**< Commands that entered the schedule so far */
    box_audio_scheduled_t schedule[BOX_AUDIO_SCHEDULE_MAX]; /**< Commands that wait for their
                           * frame, a binary heap ordered by frame, then by arrival */
    uint64_t beat_frame;  /**< Frame of the last BEAT command that applied */
    double beat_at;       /**< Beat at beat_frame */
    double beat_tempo;    /**< Beats per second from beat_frame on */
    double beat_rate;     /**< Beats per frame from beat_frame on */
    box_audio_mods_t mods; /**< The modulators */
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

/** Turn the loudness meter of a bus on or off, in loudness.c; turned on, its K-weighting
 * starts from silence.
 *
 * @param b The bus
 * @param on Whether it measures
 */
void box_audio_core_loudness(
    box_audio_bus_t *b,
    bool on);

/** Set up the coefficients of the K-weighting of BS.1770 at the rate of a core, as
 * box_audio_kweight_init of analyze.c does, in loudness.c.
 *
 * @param core The core
 */
void box_audio_core_kweight_init(
    box_audio_core_t *core);

/** K-weight the output of a bus over a block for its loudness meter, in loudness.c.
 *
 * @param core The core
 * @param b The bus, after its gains
 * @param frames Frames of the block
 * @return The sum of the K-weighted squares of both channels
 */
float box_audio_core_kweight(
    box_audio_core_t *core,
    box_audio_bus_t *b,
    int32_t frames);

/** Set the beat clock and the modulators of a new core.
 *
 * @param core The core, zeroed
 */
void box_audio_mods_init(
    box_audio_core_t *core);

/** Apply a BEAT command.
 *
 * @param core The core
 * @param cmd The command
 */
void box_audio_beat_command(
    box_audio_core_t *core,
    const box_audio_cmd_beat_t *cmd);

/** Apply a MODULATOR command.
 *
 * @param core The core
 * @param cmd The command
 */
void box_audio_mod_command(
    box_audio_core_t *core,
    const box_audio_cmd_modulator_t *cmd);

/** Apply a MACRO command.
 *
 * @param core The core
 * @param cmd The command
 */
void box_audio_macro_command(
    box_audio_core_t *core,
    const box_audio_cmd_macro_t *cmd);

/** Apply a MACRO_MAP command.
 *
 * @param core The core
 * @param cmd The command
 */
void box_audio_macro_map_command(
    box_audio_core_t *core,
    const box_audio_cmd_macro_map_t *cmd);

/** Update the modulators when an update is due and limit a block to the next update, so that
 * every update falls on the same frame whatever the render sizes.
 *
 * @param core The core
 * @param frames Frames the block would have
 * @return Frames of the block
 */
int32_t box_audio_mods_block(
    box_audio_core_t *core,
    int32_t frames);

/** Let the followers that measure a bus take a block of it.
 *
 * @param core The core
 * @param bus The bus
 * @param l Left samples
 * @param r Right samples
 * @param frames Frames of the block
 */
void box_audio_mods_follow(
    box_audio_core_t *core,
    uint32_t bus,
    const float *l,
    const float *r,
    int32_t frames);

/** Tell the modulators that a command set the parameters of an effect slot or of a bus, so
 * they add to the new values before the next frame renders.
 *
 * @param core The core
 * @param effect Slot of the effect, -1 for none
 * @param bus Index of the bus, -1 for none
 */
void box_audio_mods_changed(
    box_audio_core_t *core,
    int32_t effect,
    int32_t bus);

/** Set the gain targets of a bus from its parameters and what the modulators add, ramped over
 * a number of frames.
 *
 * @param core The core
 * @param b The bus
 * @param add What the modulators add to each box_audio_mod_bus_param_t, NULL for nothing
 * @param frames Frames of the ramps
 */
void box_audio_bus_gains_to(
    box_audio_core_t *core,
    box_audio_bus_t *b,
    const float *add,
    int32_t frames);

/** Run the kind of an effect on its params after they changed, when it runs.
 *
 * @param core The core
 * @param slot Slot of the effect
 */
void box_audio_effect_changed(
    box_audio_core_t *core,
    uint32_t slot);

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

/** Apply an effect params command: the parameters of the slot when it holds the kind of the
 * command, smoothed by the kind.
 *
 * @param core The core
 * @param cmd The command
 */
void box_audio_effect_params_command(
    box_audio_core_t *core,
    const box_audio_cmd_effect_params_t *cmd);

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

/** Frames of the wavetable a sound holds, in synth_patch.c: the frames of a mono sound that
 * starts with the header of a wavetable and has the floats of its frames.
 *
 * @param data Frames of the sound
 * @param frames Number of frames
 * @param channels 1 or 2
 * @return Frames of the table, 1..BOX_AUDIO_TABLE_FRAMES_MAX, 0 when it is not one
 */
int32_t box_audio_table_frames(
    const float *data,
    int32_t frames,
    int32_t channels);

/** Take the wavetable of a sound for an oscillator: a ready sound that holds one gains a
 * user, which box_audio_synth_end gives back. Any other id leaves the oscillator without a
 * table, a sine.
 *
 * @param core The core
 * @param o The oscillator
 * @param sound Id of the sound of the table, 0 for none
 */
void box_audio_synth_table_take(
    box_audio_core_t *core,
    box_audio_synth_osc_t *o,
    uint32_t sound);

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

/** Set the clipper of the drive of a synth patch: a gain of BOX_AUDIO_SYNTH_DRIVE_DB_MIN to
 * _MAX dB, an offset of up to BOX_AUDIO_SYNTH_DRIVE_BIAS, and a level compensation that keeps
 * the RMS of a sine at BOX_AUDIO_SYNTH_DRIVE_REFERENCE.
 *
 * @param curve Receives the clipper
 * @param drive 0..1
 * @param asymmetry 0..1
 */
void box_audio_synth_drive_curve(
    box_audio_saturate_curve_t *curve,
    float drive,
    float asymmetry);

/** Set the parts of the synth layers and the pluck strings of a new core free.
 *
 * @param core The core, zeroed
 */
void box_audio_synth_parts_init(
    box_audio_core_t *core);

/** Whether the free parts, strings and comb lines of a core hold the layers and the filter of
 * the patch of a synth sound.
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

/** Whether the free strings and comb lines of a core hold the pluck layers and the comb filter
 * of a synth sound.
 *
 * @param core The core
 * @param sound Id of a ready synth sound
 * @return Whether they do
 */
bool box_audio_synth_strings_fit(
    const box_audio_core_t *core,
    uint32_t sound);

/** Give the parts, the strings and the comb lines of a synth source back to its core.
 *
 * @param core The core
 * @param src The source
 */
void box_audio_synth_end(
    box_audio_core_t *core,
    box_audio_synth_source_t *src);

/** Start a synth source on the patch of a synth sound.
 *
 * Takes a part of the core for each layer, a string for each unison voice of each pluck layer
 * and the comb lines of a comb filter, which box_audio_synth_fits found. Takes no use of the
 * sound: the source plays a copy of the patch.
 *
 * @param core The core
 * @param source The source
 * @param sound Id of a ready synth sound
 * @param pitch Pitch ratio
 * @param velocity Velocity of the note, 0..1, for the velocity fields of the patch
 * @param beat Beat of the Transport at its start, which its synced LFOs lock to
 * @param tempo Beats per second its synced LFOs run at, 0 or invalid for
 *  BOX_AUDIO_SYNTH_TEMPO
 * @param loop Whether it starts over at the end of its envelopes
 * @param gate Whether its envelopes sustain until a release, BOX_AUDIO_PLAY_GATE
 */
void box_audio_source_synth(
    box_audio_core_t *core,
    box_audio_source_t *source,
    uint32_t sound,
    float pitch,
    float velocity,
    float beat,
    float tempo,
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

/** Set the expression of a synth source: bend, mod and pressure.
 *
 * The bend moves its oscillators, strings and key at once, so a glide that runs is kept. The
 * mod scales the vibrato of its LFOs and the pressure opens its cutoff.
 *
 * @param src The source
 * @param bend Factor of the pitch, 1 without a bend
 * @param mod Factor of the pitch depth of the LFOs, 1 without
 * @param pressure Pressure, 0..1
 * @param rate Frames per second
 */
void box_audio_synth_express(
    box_audio_synth_source_t *src,
    double bend,
    float mod,
    float pressure,
    float rate);

/** Set the pitch ratio of a source, for kinds that have one.
 *
 * @param source The source
 * @param pitch Pitch ratio
 */
void box_audio_source_pitch(
    box_audio_source_t *source,
    float pitch);

/** Open the low-pass of a voice and clear it, for a voice that starts.
 *
 * @param v The voice
 */
void box_audio_voice_filter_reset(
    box_audio_voice_t *v);

/** Move the low-pass of a voice to a cutoff of BOX_AUDIO_PARAM_CUTOFF. The log2 of its g
 * ramps linearly over BOX_AUDIO_CUTOFF_RAMP, in octaves at low cutoffs, and the stages stay
 * stable while it moves; an open target ramps to near the Nyquist and stops the filter there.
 *
 * A voice that has not rendered takes the cutoff at once. A filter that starts on a playing
 * voice starts open, with its state at the next input, so it does not click.
 *
 * @param core The core
 * @param v The voice
 * @param value Cutoff in Hz
 */
void box_audio_voice_cutoff(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    float value);

/** Run the low-pass of a voice over a block of its source in core->src_l and src_r, in
 * place, and stop it once it opened.
 *
 * @param core The core
 * @param v The voice, whose filter runs
 * @param stereo Whether the source rendered both channels
 * @param frames Frames of the block
 */
void box_audio_voice_filter(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    bool stereo,
    int32_t frames);

/** Reset the expression of a voice to its defaults: no bend, mod 1, no pressure.
 *
 * @param core The core
 * @param v The voice
 * @param apply Whether the source takes it, for a voice whose expression may have moved
 */
void box_audio_voice_express_reset(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    bool apply);

/** Advance the ramps of the expression of a voice by a number of frames and give their
 * values to its source; once none runs the voice renders in one piece again.
 *
 * @param core The core
 * @param v The voice
 * @param frames The frames
 */
void box_audio_voice_express_next(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    int32_t frames);

/** Move a parameter of the expression of a voice to a value over a number of frames, from
 * where its ramp stands. A ramp of 0 frames jumps, and the source takes it at once.
 *
 * @param core The core
 * @param v The voice
 * @param param BOX_AUDIO_PARAM_BEND, BOX_AUDIO_PARAM_MOD or BOX_AUDIO_PARAM_PRESSURE
 * @param value The value, clamped
 * @param frames Frames of the ramp
 */
void box_audio_voice_ramp(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    uint32_t param,
    float value,
    uint32_t frames);

/** Set the expression of a source, for kinds that have one: the factor of the pitch from a
 * bend of a sample or a synth source, and the mod and the pressure of a synth source.
 *
 * @param core The core
 * @param source The source
 * @param bend Factor of the pitch, 1 without a bend
 * @param mod Mod, 0..1, 1 without
 * @param pressure Pressure, 0..1
 */
void box_audio_source_express(
    box_audio_core_t *core,
    box_audio_source_t *source,
    double bend,
    float mod,
    float pressure);

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

#include "effect_kinds.h"

#endif
