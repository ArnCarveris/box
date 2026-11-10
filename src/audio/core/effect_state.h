/**
 * @file audio/core/effect_state.h
 * @brief State of the effect instances of the core and the table of their kinds.
 */

#ifndef BOX_AUDIO_CORE_EFFECT_STATE_H
#define BOX_AUDIO_CORE_EFFECT_STATE_H

#include "core.h"
#include "clean.h"
#include "ramp.h"
#include "svf.h"
#include "saturate.h"
#include "shift.h"

/** Read a delay line of an effect frames back from its write position, with linear
 * interpolation; frames older than the ones written since the line was silent read as silence,
 * so its memory needs no clearing. Shared by the chorus and the rotary.
 *
 * @param line The line
 * @param length Frames of the line
 * @param pos Frame the next input is written to
 * @param valid Frames written since the line was silent
 * @param back Frames back, 1..length - 2
 * @return The sample
 */
static inline float box_audio_line_read(
    const float *line,
    int32_t length,
    int32_t pos,
    int32_t valid,
    double back)
{
    int32_t whole = (int32_t)back, i0 = pos - whole, i1 = i0 - 1;
    float frac = (float)(back - (double)whole), a, b;
    i0 += i0 < 0 ? length : 0;
    i1 += i1 < 0 ? length : 0;
    a = whole <= valid ? line[i0] : 0.0f;
    b = whole < valid ? line[i1] : 0.0f;

    return a + frac * (b - a);
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
    box_audio_shift_t shimmer; /**< Shifter of the shimmer, on the memory after the lines */
    box_audio_ramp_t shimmer_amount; /**< Share of the feedback that is shifted */
    box_audio_ramp_t shimmer_ratio; /**< Pitch ratio of the shimmer */
    float shimmer_window;     /**< Window of the shifter of the shimmer in frames */
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
 * BOX_AUDIO_CHORUS_VOICES_MAX taps that a sine LFO each moves, the first of which feeds back
 * into the line */
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
    int32_t still;            /**< Frames in a row in which nothing fed back */
    box_audio_ramp_t depth;   /**< Depth in frames, per control block */
    box_audio_ramp_t delay;   /**< Delay in frames, per control block */
    box_audio_ramp_t spread;  /**< Spread, per control block */
    box_audio_ramp_t mix;     /**< Wet level */
    box_audio_ramp_t fade;    /**< Fade of the wet signal around a change of the voices */
    box_audio_ramp_t feedback; /**< Share of the first tap fed back into the lines */
    box_audio_ramp_t feed;    /**< Fade in of the input after a move */
} box_audio_chorus_t;

/** A saturator: the filters before the clipper of each channel, the oversampled clipper, its
 * DC blocker, the filters and the sample and hold after it, and the line that delays the dry
 * signal as much as the oversampler delays the wet one */
typedef struct box_audio_saturator_t {
    box_audio_saturator_params_t target; /**< Parameters of the last command, clamped */
    box_audio_oversample_t os[2]; /**< Oversampler of each channel */
    box_audio_dc_t dc[2];     /**< DC blocker of each channel */
    box_audio_oversample_t fold[2]; /**< Second oversampler of each channel, at the doubled
                               * rate, of the wavefolder */
    float late[2];            /**< Output of the wavefolder at the doubled rate held back a
                               * sample, of each channel */
    float dry[2][32];         /**< Dry input of each channel, a ring */
    uint32_t pos;             /**< Write position of the dry rings */
    float dc_coef;            /**< Pole of the DC blockers */
    box_audio_saturate_curve_t curve; /**< Clipper at the drive and asymmetry of curve_at */
    float curve_at[2];        /**< Drive and asymmetry the curve is for */
    int32_t mode;             /**< The box_audio_saturator_mode_t that runs */
    box_audio_saturate_shape_t shape; /**< Clipper of the other modes at the drive and
                               * asymmetry of shape_at */
    float shape_at[2];        /**< Drive and asymmetry the shape is for */
    double adaa[2];           /**< Driven input of the last sample at the oversampled rate of
                               * each channel, for the ADAA of the hard clipper and the folder */
    box_audio_svf_t low[2];   /**< Highpass of the low cut of each channel */
    box_audio_svf_t high[2];  /**< Lowpass of the high cut of each channel */
    box_audio_saturate_tilt_t pre[2]; /**< Tone tilt before the clipper of each channel */
    box_audio_saturate_tilt_t post[2]; /**< Inverse tilt after it */
    float tilt[6];            /**< Coefficients of the tilt before and of the one after */
    float low_at;             /**< Low cut the highpass is for, 0 while it is off */
    float high_at;            /**< High cut the lowpass is for, high_off while it is off */
    float tone_at;            /**< Tone the tilts are for, 0 while they are off */
    float high_off;           /**< High cut that counts as off,
                               * BOX_AUDIO_SATURATOR_CUT_NYQUIST times the rate */
    float rate_off;           /**< Rate of the sample and hold that counts as off, the rate */
    float rate_at;            /**< Rate the step of the sample and hold is for */
    uint32_t rate_step;       /**< Turns of the sample and hold per frame, times 2^32 */
    uint32_t phase;           /**< Turns of the sample and hold times 2^32; it takes a sample
                               * each time it wraps */
    float hold[2];            /**< Sample each channel holds */
    float scale;              /**< Steps of each sign of the bit crusher, 2^(bits - 1); 0 when
                               * it is off */
    float scale_at;           /**< Bits the scale is for */
    box_audio_ramp_t drive;   /**< Drive */
    box_audio_ramp_t asymmetry; /**< Asymmetry */
    box_audio_ramp_t bits;    /**< Bits */
    box_audio_ramp_t mix;     /**< Wet level */
    box_audio_ramp_t low_cut; /**< Low cut in Hz, 0 when off */
    box_audio_ramp_t high_cut; /**< High cut in Hz, high_off when off */
    box_audio_ramp_t tone;    /**< Tone */
    box_audio_ramp_t rate;    /**< Rate of the sample and hold in Hz, rate_off when off */
} box_audio_saturator_t;

/** A transient shaper: the peak detector of the dynamics, a fast and a slow envelope of it and
 * the gain in dB that their difference gives, smoothed */
typedef struct box_audio_transient_shaper_t {
    box_audio_transient_shaper_params_t target; /**< Parameters of the last command, clamped */
    box_audio_peak_t peak;    /**< Detector */
    double fast;              /**< Linear level of the fast envelope */
    double slow;              /**< Linear level of the slow envelope */
    double coef[4];           /**< Share of the distance that stays per frame: fast attack,
                               * fast release, slow attack, slow release */
    double smooth;            /**< Share of the distance that stays per frame of the gain */
    double gain;              /**< Gain in dB that runs, smoothed */
    box_audio_ramp_t attack;  /**< Attack */
    box_audio_ramp_t sustain; /**< Sustain */
} box_audio_transient_shaper_t;

/** A band of a vocoder on the memory of its slot, floats only: the two cascaded state
 * variable bandpasses of the modulator and the carrier per channel, the envelope of the
 * modulator per channel and the coefficients of both */
typedef struct box_audio_vocoder_band_t {
    float mod[2][4];          /**< States ic1 and ic2 of each modulator stage per channel */
    float car[2][4];          /**< States ic1 and ic2 of each carrier stage per channel */
    float env[2];             /**< Envelope of the modulator band per channel */
    float mod_c[4];           /**< a1, a2, a3 and k of the modulator stages */
    float car_c[4];           /**< a1, a2, a3 and k of the carrier stages */
    float gain;               /**< Gain of the band: the normalization of its envelope and of
                               * the sum */
    float pad[3];             /**< Fills the band to 32 floats */
} box_audio_vocoder_band_t;

/** A vocoder: BOX_AUDIO_VOCODER_BANDS_MAX bands on the memory of its slot, the noise of its
 * unvoiced carrier and the ramps of its parameters */
typedef struct box_audio_vocoder_t {
    box_audio_vocoder_params_t target; /**< Parameters of the last command, clamped */
    box_audio_vocoder_band_t *band; /**< The bands in memory */
    int32_t bands;            /**< Bands that run */
    int32_t unvoiced_from;    /**< First band whose carrier takes the noise */
    uint32_t rng;             /**< State of the noise, xorshift32, never 0 */
    float attack;             /**< Share of the distance an envelope moves per frame up */
    float release;            /**< Share of the distance an envelope moves per frame down */
    int32_t control;          /**< Frames until the next update of the coefficients */
    bool swap;                /**< Waits for the fade to reach 0 to apply the bands */
    box_audio_ramp_t low;     /**< log2 of the low band, per control block */
    box_audio_ramp_t high;    /**< log2 of the high band, per control block */
    box_audio_ramp_t q;       /**< Sharpness, per control block */
    box_audio_ramp_t shift;   /**< Shift, per control block */
    box_audio_ramp_t unvoiced; /**< Level of the noise */
    box_audio_ramp_t mix;     /**< Wet level */
    box_audio_ramp_t fade;    /**< Fade of the wet signal around a change of the bands */
} box_audio_vocoder_t;
/** Sections of the Hilbert transformer of a frequency shifter in each of its two paths */
#define BOX_AUDIO_FREQUENCY_SHIFTER_SECTIONS (BOX_AUDIO_FREQUENCY_SHIFTER_ALLPASSES / 2)

/** A frequency shifter: the two allpass paths of the Hilbert transformer of each channel, a
 * phase of the oscillator per channel and the output that feeds back */
typedef struct box_audio_frequency_shifter_t {
    box_audio_frequency_shifter_params_t target; /**< Parameters of the last command, clamped */
    double coef[BOX_AUDIO_FREQUENCY_SHIFTER_ALLPASSES]; /**< Coefficients of the allpasses at
                               * the rate, the even ones in the real path, the odd ones in the
                               * imaginary one */
    double node[2][2][BOX_AUDIO_FREQUENCY_SHIFTER_SECTIONS + 1][2]; /**< Per channel and path,
                               * the input and the output of each section one and two frames
                               * back */
    double prev[2];           /**< Input of each channel in the frame before, which the
                               * imaginary path takes */
    float last[2];            /**< Output of each channel in the frame before, which feeds
                               * back */
    box_audio_dc_t dc[2];     /**< DC blocker of the feedback of each channel */
    float dc_coef;            /**< Pole of the DC blockers */
    uint32_t phase[2];        /**< Phase of the oscillator of each channel in 2^-32 turns */
    double turn;              /**< Units of the phase per frame of a shift of 1 Hz */
    box_audio_ramp_t shift;   /**< Shift in Hz */
    box_audio_ramp_t ring;    /**< Share of the ring modulation, 0 shifts, 1 rings */
    box_audio_ramp_t stereo;  /**< Stereo */
    box_audio_ramp_t feedback; /**< Feedback gain */
    box_audio_ramp_t mix;     /**< Wet level */
} box_audio_frequency_shifter_t;
/** A pitch shifter: a shifter per channel on the memory of its effect slot, and the last
 * shifted sample of each channel for the feedback */
typedef struct box_audio_pitch_shifter_t {
    box_audio_pitch_shifter_params_t target; /**< Parameters of the last command, clamped */
    box_audio_shift_t shift[2]; /**< Shifter of each channel */
    float back[2];            /**< Last shifted sample of each channel, soft clipped */
    int32_t still;            /**< Frames in a row in which the wet signal was silent */
    box_audio_ramp_t ratio[2]; /**< Pitch ratio of each channel */
    box_audio_ramp_t window;  /**< Window in frames */
    box_audio_ramp_t feedback; /**< Feedback gain */
    box_audio_ramp_t mix;     /**< Wet level */
    box_audio_ramp_t feed;    /**< Fade in of the input after a move */
} box_audio_pitch_shifter_t;

/** A pattern gate: the openness that follows the steps of its pattern on the beat clock,
 * and its floor and mix */
typedef struct box_audio_pattern_gate_t {
    box_audio_pattern_gate_params_t target; /**< Parameters of the last command, clamped */
    int32_t length;           /**< Steps that play */
    double rate;              /**< Beats per step */
    double swing;             /**< Swing */
    float up;                 /**< Openness the gain gains per frame on the way up */
    float down;               /**< Openness the gain loses per frame on the way down */
    float open;               /**< Openness that runs, 0..1 */
    box_audio_ramp_t floor;   /**< Linear gain when closed */
    box_audio_ramp_t mix;     /**< Wet level */
} box_audio_pattern_gate_t;

/** A virtual mic of a rotor of a rotary: where it reads the line of the rotor, its gain and
 * how dull its tone is, each moved linearly from control block to control block */
typedef struct box_audio_rotary_mic_t {
    double tap;               /**< Frames back of the read in this frame */
    double tap_slope;         /**< Change of the read per frame */
    double tap_next;          /**< Frames back of the read at the next control block */
    float gain;               /**< Gain in this frame */
    float gain_slope;         /**< Change of the gain per frame */
    float gain_next;          /**< Gain at the next control block */
    float dull;               /**< Share of the lowpass in this frame */
    float dull_slope;         /**< Change of the share per frame */
    float dull_next;          /**< Share at the next control block */
    float lp;                 /**< State of the lowpass */
} box_audio_rotary_mic_t;

/** A rotor of a rotary: its phase and its rate, which moves to its target with inertia */
typedef struct box_audio_rotary_rotor_t {
    double phase;             /**< Turns at the next control block, 0..1 */
    double rate;              /**< Hz it turns at */
    double target;            /**< Hz it moves to */
    double coef;              /**< Share of the way to the target it moves per control block */
} box_audio_rotary_rotor_t;

/** A rotary: a tube preamp, a crossover, and a drum and a horn, each a line on the memory of
 * its effect slot that two mics read */
typedef struct box_audio_rotary_t {
    box_audio_rotary_params_t target; /**< Parameters of the last command, clamped */
    float *line[2];           /**< The lines of the drum and the horn in memory */
    int32_t length;           /**< Frames of each line */
    int32_t pos;              /**< Frame the next input is written to */
    int32_t valid;            /**< Frames written since the lines were last silent */
    box_audio_rotary_rotor_t rotor[2]; /**< The drum and the horn */
    box_audio_rotary_mic_t mic[2][2]; /**< The left and the right mic of each rotor */
    float center[2];          /**< Frames back of the read of each rotor at the middle */
    float depth[2];           /**< Frames the read of each rotor moves either way */
    float tone[2];            /**< Coefficient of the lowpass of the mics of each rotor */
    box_audio_svf_t low[2];   /**< Lowpass stages of the crossover */
    box_audio_svf_t high[2];  /**< Highpass stages of the crossover */
    box_audio_oversample_t os; /**< Oversampler of the preamp */
    box_audio_dc_t dc;        /**< DC blocker after the clipper */
    float dc_coef;            /**< Pole of the DC blocker */
    float dry[16];            /**< Input of the preamp, a ring as late as its oversampler */
    uint32_t dry_pos;         /**< Write position of the dry ring */
    box_audio_saturate_curve_t curve; /**< Clipper at the drive of curve_at */
    float curve_at;           /**< Drive the curve is for */
    int32_t control;          /**< Frames until the next control block */
    bool stale;               /**< Skipped blocks left the mics behind the rotors */
    box_audio_ramp_t drive;   /**< Drive */
    box_audio_ramp_t spread;  /**< Spread, per control block */
    box_audio_ramp_t drum;    /**< Gain of the drum from the balance */
    box_audio_ramp_t horn;    /**< Gain of the horn from the balance */
    box_audio_ramp_t mix;     /**< Wet level */
    box_audio_ramp_t feed;    /**< Fade in of the input after a move */
} box_audio_rotary_t;

/** A repeat of a beat repeat: how it reads the slice */
typedef struct box_audio_beat_repeat_pass_t {
    double ratio;             /**< Frames of the slice per frame of output */
    float gain;               /**< Gain, decay to the number of repeats before it */
    float semitones;          /**< Pitch against the slice, the pitch of the repeats added up */
    int32_t sound;            /**< Frames that sound, before the fade out */
    bool reverse;             /**< Reads the slice from its end */
    bool fade_in;             /**< Fades in from the repeat before over the fade */
} box_audio_beat_repeat_pass_t;

/** States of a beat repeat */
typedef enum box_audio_beat_repeat_state_t {
    BOX_AUDIO_BEAT_REPEAT_OFF = 0,     /**< Passes the input and records it, until an armed
                                        *   capture starts */
    BOX_AUDIO_BEAT_REPEAT_HOLD = 1,    /**< Captures and repeats the slice */
    BOX_AUDIO_BEAT_REPEAT_RELEASE = 2  /**< Not active any more, crossfades to the input */
} box_audio_beat_repeat_state_t;

/** A beat repeat: a line per channel on the memory of its effect slot that records the input,
 * frozen once the capture holds the slice and what its crossfades read past its ends, and the
 * repeat that plays with the one before it, whose end crossfades */
typedef struct box_audio_beat_repeat_t {
    box_audio_beat_repeat_params_t target; /**< Parameters of the last command, clamped */
    float *line[2];           /**< The lines of the left and the right channel in memory */
    int32_t length;           /**< Frames of each line */
    int32_t pos;              /**< Frame the next input is written to */
    int32_t valid;            /**< Frames written in a row since the line last started to
                               * record; older frames read as silence, so the memory needs no
                               * clearing */
    int32_t room;             /**< Frames the crossfades read past either end of a slice at
                               * the fastest read */
    int32_t state;            /**< A box_audio_beat_repeat_state_t */
    bool on;                  /**< active was 0.5 or above at the last command */
    bool armed;               /**< A capture waits for its frame */
    bool frozen;              /**< The line stopped recording; it starts over when it records
                               * again */
    uint64_t begin;           /**< Frame of the core clock the armed capture starts at */
    int32_t slice;            /**< Frames of the slice of the capture */
    int32_t wait;             /**< Frames of the slice of the armed capture */
    int32_t fade;             /**< Frames of the crossfades of the capture */
    int32_t origin;           /**< Frame of the line the slice starts at */
    int32_t before;           /**< Frames of the line before origin that hold input */
    int32_t captured;         /**< Frames of the line from origin on that hold input */
    int32_t at;               /**< Frames into the repeat that plays */
    box_audio_beat_repeat_pass_t pass; /**< The repeat that plays */
    box_audio_beat_repeat_pass_t last; /**< The repeat before it, whose end fades out over
                               * the first frames of this one; its gain is 0 for none */
    box_audio_ramp_t share;   /**< Share of the repeats in the output, 0 to pass the input */
    box_audio_ramp_t mix;     /**< Wet level */
} box_audio_beat_repeat_t;

/** A tape stop: a line per channel on the memory of its effect slot, the phase of its ramp
 * and the lag of its read behind the input */
typedef struct box_audio_tape_stop_t {
    box_audio_tape_stop_params_t target; /**< Parameters of the last command, clamped */
    float *line[2];           /**< The lines of the left and the right channel in memory */
    int32_t length;           /**< Frames of each line */
    int32_t pos;              /**< Frame the next input is written to */
    int32_t valid;            /**< Frames written since the start; older frames read as
                               * silence */
    double phase;             /**< Phase of the ramp, 0 at full speed, 1 at a stop */
    double down;              /**< Phase per frame of a stop */
    double up;                /**< Phase per frame of a start */
    double power;             /**< Exponent of the phase in the speed, 4^curve */
    double lag;               /**< Frames the read lags behind the last input */
    double from;              /**< Lag of the read the crossfade leaves */
    bool crossfade;           /**< Crossfades from the read at from to the one at lag */
    box_audio_ramp_t xfade;   /**< Share of the read at lag in the crossfade */
} box_audio_tape_stop_t;

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
    box_audio_saturator_t saturator; /**< BOX_AUDIO_EFFECT_SATURATOR */
    box_audio_transient_shaper_t transient_shaper; /**< BOX_AUDIO_EFFECT_TRANSIENT_SHAPER */
    box_audio_vocoder_t vocoder; /**< BOX_AUDIO_EFFECT_VOCODER */
    box_audio_frequency_shifter_t frequency_shifter; /**< BOX_AUDIO_EFFECT_FREQUENCY_SHIFTER */
    box_audio_pitch_shifter_t pitch_shifter; /**< BOX_AUDIO_EFFECT_PITCH_SHIFTER */
    box_audio_pattern_gate_t pattern_gate; /**< BOX_AUDIO_EFFECT_PATTERN_GATE */
    box_audio_rotary_t rotary;  /**< BOX_AUDIO_EFFECT_ROTARY */
    box_audio_beat_repeat_t beat_repeat; /**< BOX_AUDIO_EFFECT_BEAT_REPEAT */
    box_audio_tape_stop_t tape_stop; /**< BOX_AUDIO_EFFECT_TAPE_STOP */
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
    float params[BOX_AUDIO_EFFECT_PARAMS]; /**< Parameters the kind runs at: those of the last
                               * command, as sent, plus what the modulators add; the kind clamps
                               * them */
    float base[BOX_AUDIO_EFFECT_PARAMS]; /**< Parameters of the last EFFECT or EFFECT_PARAMS
                               * command, without the modulators */
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
    void (*start)(box_audio_core_t *core, box_audio_effect_t *fx, bool fade_in); /**< Start
                              * on clear memory at the params of fx, fading its input in when
                              * fade_in, as after a move */
    void (*command)(box_audio_core_t *core, box_audio_effect_t *fx); /**< New params of fx
                              * while it runs: clamp and smooth them */
    bool (*render)(box_audio_core_t*, box_audio_effect_t*, float*, float*, int32_t, bool); /**<
                              * Process a block of left and right samples in place, given whether
                              * the input is silent; returns whether a kind with a tail decayed */
    void (*rest)(box_audio_core_t *core, box_audio_effect_t *fx, int32_t frames); /**< A block
                              * of frames skipped while idle and the input is silent */
} box_audio_effect_def_t;

#endif
