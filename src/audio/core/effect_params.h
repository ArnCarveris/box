/**
 * @file audio/core/effect_params.h
 * @brief Kinds of effects of the audio core, their parameters and the effect commands.
 */

#ifndef BOX_AUDIO_CORE_EFFECT_PARAMS_H
#define BOX_AUDIO_CORE_EFFECT_PARAMS_H

#include "commands.h"

/** Kinds of effects. A kind is one file of src/audio/core and one row of the table of
 * effect.c */
typedef enum box_audio_effect_kind_t {
    BOX_AUDIO_EFFECT_NONE = 0,    /**< No effect: takes an effect off, its tail fades out */
    BOX_AUDIO_EFFECT_REVERB = 1,  /**< A reverb, box_audio_reverb_params_t */
    BOX_AUDIO_EFFECT_EQ = 2,      /**< An equalizer, BOX_AUDIO_EQ_BANDS box_audio_eq_band_t */
    BOX_AUDIO_EFFECT_FILTER = 3,  /**< A filter, box_audio_filter_params_t */
    BOX_AUDIO_EFFECT_COMPRESSOR = 4, /**< A compressor, box_audio_compressor_params_t, keyed on
                                   *   the key bus of its command */
    BOX_AUDIO_EFFECT_GATE = 5,    /**< A gate, box_audio_gate_params_t */
    BOX_AUDIO_EFFECT_DELAY = 6,   /**< A delay, box_audio_delay_params_t */
    BOX_AUDIO_EFFECT_PHASER = 7,  /**< A phaser, box_audio_phaser_params_t */
    BOX_AUDIO_EFFECT_CHORUS = 8,  /**< A chorus, box_audio_chorus_params_t */
    BOX_AUDIO_EFFECT_SATURATOR = 9, /**< A saturator, box_audio_saturator_params_t */
    BOX_AUDIO_EFFECT_TRANSIENT_SHAPER = 10, /**< A transient shaper,
                                  *   box_audio_transient_shaper_params_t */
    BOX_AUDIO_EFFECT_FREQUENCY_SHIFTER = 11, /**< A frequency shifter,
                                  *   box_audio_frequency_shifter_params_t */
    BOX_AUDIO_EFFECT_PITCH_SHIFTER = 12, /**< A pitch shifter, box_audio_pitch_shifter_params_t */
    BOX_AUDIO_EFFECT_PATTERN_GATE = 13, /**< A pattern gate, box_audio_pattern_gate_params_t */
    BOX_AUDIO_EFFECT_ROTARY = 14, /**< A rotating speaker, box_audio_rotary_params_t */
    BOX_AUDIO_EFFECT_BEAT_REPEAT = 15, /**< A beat repeat, box_audio_beat_repeat_params_t */
    BOX_AUDIO_EFFECT_TAPE_STOP = 16, /**< A tape stop, box_audio_tape_stop_params_t */
    BOX_AUDIO_EFFECT_VOCODER = 17, /**< A vocoder, box_audio_vocoder_params_t, its modulator
                                  *   the key bus of its command */
    BOX_AUDIO_EFFECT_KINDS = 18   /**< Number of kinds, NONE included */
} box_audio_effect_kind_t;

/** The parameters of a reverb, the first floats of the params of its effect command.
 *
 * Shimmer pitch shifts a share of the feedback of the network (shift.h) by shimmer_pitch
 * semitones on each pass, so the tail rises; a shimmer of 0 leaves the reverb unchanged */
typedef struct box_audio_reverb_params_t {
    float decay;     /**< Seconds until -60 dB, BOX_AUDIO_REVERB_DECAY_MIN..MAX */
    float damping;   /**< High frequency loss, 0..1 */
    float predelay;  /**< Seconds before the first reflection, 0..BOX_AUDIO_REVERB_PREDELAY_MAX */
    float size;      /**< Room scale, 0..1, which sets the lengths of the delay lines */
    float mix;       /**< Wet level, 0..1; the dry signal passes at 1 - mix */
    float shimmer;   /**< Share of the feedback that is pitch shifted, 0..1 */
    float shimmer_pitch; /**< Semitones of the shift of the shimmer,
                      *   -BOX_AUDIO_PITCH_SHIFTER_SEMITONES..BOX_AUDIO_PITCH_SHIFTER_SEMITONES */
} box_audio_reverb_params_t;

/** Window of the shifter of the shimmer of a reverb in seconds */
#define BOX_AUDIO_REVERB_SHIMMER_WINDOW 0.06f

/** Seconds of a change of the shimmer or its pitch */
#define BOX_AUDIO_REVERB_SHIMMER_RAMP 0.05f

/** Bands of an equalizer; each takes 4 floats of the params of its effect command */
#define BOX_AUDIO_EQ_BANDS 4

/** Lowest frequency of a band of an equalizer in Hz; the highest is 0.49 of the rate */
#define BOX_AUDIO_EQ_FREQUENCY_MIN 10.0f

/** Largest gain of a band of an equalizer in dB, either way */
#define BOX_AUDIO_EQ_GAIN_MAX 24.0f

/** Lowest and highest Q of a band of an equalizer */
#define BOX_AUDIO_EQ_Q_MIN 0.1f
#define BOX_AUDIO_EQ_Q_MAX 18.0f

/** Seconds over which the coefficients of a band of an equalizer move to a change */
#define BOX_AUDIO_EQ_RAMP 0.02f

/** Kinds of a band of an equalizer, each an RBJ cookbook biquad */
typedef enum box_audio_eq_kind_t {
    BOX_AUDIO_EQ_OFF = 0,         /**< Off, passes its input */
    BOX_AUDIO_EQ_PEAK = 1,        /**< A peak or a dip around its frequency, as wide as its q */
    BOX_AUDIO_EQ_LOW_SHELF = 2,   /**< The gain under its frequency */
    BOX_AUDIO_EQ_HIGH_SHELF = 3,  /**< The gain above its frequency */
    BOX_AUDIO_EQ_LOW_CUT = 4,     /**< A highpass of 12 dB per octave, -3 dB at its frequency
                                   *   at a q of 0.7071 */
    BOX_AUDIO_EQ_HIGH_CUT = 5     /**< A lowpass of 12 dB per octave */
} box_audio_eq_kind_t;

/** A band of an equalizer, 4 floats of the params of its effect command, band after band */
typedef struct box_audio_eq_band_t {
    float kind;       /**< A box_audio_eq_kind_t; an unknown one is off */
    float frequency;  /**< Center or corner in Hz, BOX_AUDIO_EQ_FREQUENCY_MIN..0.49 of the rate */
    float gain;       /**< dB, -BOX_AUDIO_EQ_GAIN_MAX..BOX_AUDIO_EQ_GAIN_MAX, for a peak and the
                       *   shelves; a peak or a shelf at 0 dB is flat and costs nothing */
    float q;          /**< BOX_AUDIO_EQ_Q_MIN..BOX_AUDIO_EQ_Q_MAX: the width of a peak, the
                       *   resonance of a shelf or a cut at its corner */
} box_audio_eq_band_t;

/** Lowest cutoff of a filter effect in Hz; the highest is 0.49 of the rate */
#define BOX_AUDIO_FILTER_CUTOFF_MIN 10.0f

/** Seconds in which the cutoff and the resonance of a filter effect move most of the way to a
 * new value, exponentially in frequency */
#define BOX_AUDIO_FILTER_SMOOTH 0.01f

/** Seconds of a change of the mix or the drive of a filter effect, and of each half of the
 * crossfade through the dry signal of a change of its kind or slope */
#define BOX_AUDIO_FILTER_RAMP 0.02f

/** The parameters of a filter effect, the first floats of the params of its effect command:
 * the state variable filter of the synth */
typedef struct box_audio_filter_params_t {
    float kind;       /**< A box_audio_synth_filter_t, LOWPASS..NOTCH; NONE or an unknown one is
                       *   a lowpass */
    float cutoff;     /**< Hz, BOX_AUDIO_FILTER_CUTOFF_MIN..0.49 of the rate */
    float resonance;  /**< 0 (a Butterworth response) to 1 (rings the longest) */
    float slope;      /**< A box_audio_synth_slope_t: 0 for 12 dB per octave, 1 for 24 */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
    float drive;      /**< Saturation of the input and the resonance, 0 (linear) to 1; moves
                       *   over BOX_AUDIO_FILTER_RAMP */
} box_audio_filter_params_t;

/** Seconds of the windows of the peak detector of the compressor and the gate.
 *
 * The detector holds the largest sample of the current and the last window, so it reads the
 * peak of a sine of 100 Hz or more without ripple; a drop reaches it after one to two windows */
#define BOX_AUDIO_DYNAMICS_WINDOW 0.005f

/** Lowest level the compressor detects in dB; silence reads as this */
#define BOX_AUDIO_DYNAMICS_FLOOR (-120.0f)

/** Seconds of a change of the threshold, ratio, knee, makeup or mix of a compressor */
#define BOX_AUDIO_COMPRESSOR_RAMP 0.02f

/** Lowest threshold of a compressor in dB; the highest is 0 */
#define BOX_AUDIO_COMPRESSOR_THRESHOLD_MIN (-80.0f)

/** Largest ratio of a compressor; the smallest is 1, no compression */
#define BOX_AUDIO_COMPRESSOR_RATIO_MAX 20.0f

/** Widest knee of a compressor in dB */
#define BOX_AUDIO_COMPRESSOR_KNEE_MAX 24.0f

/** Largest makeup gain of a compressor in dB */
#define BOX_AUDIO_COMPRESSOR_MAKEUP_MAX 24.0f

/** Shortest and longest attack of a compressor or a gate in seconds */
#define BOX_AUDIO_DYNAMICS_ATTACK_MIN 0.0001f
#define BOX_AUDIO_DYNAMICS_ATTACK_MAX 1.0f

/** Shortest and longest release of a compressor or a gate in seconds */
#define BOX_AUDIO_DYNAMICS_RELEASE_MIN 0.001f
#define BOX_AUDIO_DYNAMICS_RELEASE_MAX 5.0f

/** dB under its threshold that the level of an open gate falls before it closes, so that a
 * signal at the threshold does not open and close it over and over */
#define BOX_AUDIO_GATE_HYSTERESIS 4.0f

/** Lowest threshold and range of a gate in dB; the highest are 0 */
#define BOX_AUDIO_GATE_MIN (-100.0f)

/** Longest hold of a gate in seconds */
#define BOX_AUDIO_GATE_HOLD_MAX 5.0f

/** The parameters of a compressor, the first floats of the params of its effect command.
 *
 * A feed-forward, stereo linked compressor that detects the peak of the louder channel of its
 * input, or of its key bus when that comes before its bus in the order */
typedef struct box_audio_compressor_params_t {
    float threshold;  /**< dB, BOX_AUDIO_COMPRESSOR_THRESHOLD_MIN..0 */
    float ratio;      /**< 1..BOX_AUDIO_COMPRESSOR_RATIO_MAX */
    float attack;     /**< Seconds in which the detected level rises 63% of a step up,
                       *   BOX_AUDIO_DYNAMICS_ATTACK_MIN..MAX */
    float release;    /**< Seconds in which it falls 63% of a step down,
                       *   BOX_AUDIO_DYNAMICS_RELEASE_MIN..MAX */
    float knee;       /**< Width of the soft knee around the threshold in dB, 0 for a hard
                       *   knee, 0..BOX_AUDIO_COMPRESSOR_KNEE_MAX */
    float makeup;     /**< Gain after the compression in dB, 0..BOX_AUDIO_COMPRESSOR_MAKEUP_MAX */
    float mix;        /**< Wet level, 0..1, for parallel compression; the dry signal passes at
                       *   1 - mix */
} box_audio_compressor_params_t;

/** The parameters of a gate, the first floats of the params of its effect command.
 *
 * It opens when the peak of the louder channel reaches its threshold, stays open while it is
 * above the threshold less BOX_AUDIO_GATE_HYSTERESIS and for hold seconds after, then closes */
typedef struct box_audio_gate_params_t {
    float threshold;  /**< dB, BOX_AUDIO_GATE_MIN..0 */
    float attack;     /**< Seconds in which the gain rises 63% of the way to open,
                       *   BOX_AUDIO_DYNAMICS_ATTACK_MIN..MAX */
    float hold;       /**< Seconds it stays open after the level fell, 0..BOX_AUDIO_GATE_HOLD_MAX */
    float release;    /**< Seconds in which the gain falls 63% of the way to closed,
                       *   BOX_AUDIO_DYNAMICS_RELEASE_MIN..MAX */
    float range;      /**< Gain when closed in dB, BOX_AUDIO_GATE_MIN..0 */
} box_audio_gate_params_t;

/** Longest time of a delay in seconds, which sizes its memory */
#define BOX_AUDIO_DELAY_MAX 4.0f

/** Largest feedback of a delay */
#define BOX_AUDIO_DELAY_FEEDBACK_MAX 0.95f

/** Lowest damping of a delay in Hz; a damping at 0.49 of the rate or above leaves the repeats
 * undamped */
#define BOX_AUDIO_DELAY_DAMPING_MIN 20.0f

/** Seconds of the crossfade from the old to the new read position of a change of the time of
 * a delay */
#define BOX_AUDIO_DELAY_CROSSFADE 0.05f

/** Seconds of a change of the feedback, the mix or the ping pong of a delay */
#define BOX_AUDIO_DELAY_RAMP 0.02f

/** The parameters of a delay, the first floats of the params of its effect command.
 *
 * A stereo delay line with a one pole lowpass in its feedback path. A change of the time
 * crossfades to the new read position over BOX_AUDIO_DELAY_CROSSFADE, without a pitch glide */
typedef struct box_audio_delay_params_t {
    float time;       /**< Seconds, 0..BOX_AUDIO_DELAY_MAX; at least a frame */
    float feedback;   /**< Share of a repeat in the next, 0..BOX_AUDIO_DELAY_FEEDBACK_MAX */
    float damping;    /**< Cutoff of the lowpass in the feedback path in Hz,
                       *   BOX_AUDIO_DELAY_DAMPING_MIN and up */
    float ping_pong;  /**< 1 bounces the repeats between left and right, from a mono sum of
                       *   the input into the left; 0 keeps each channel to itself */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
} box_audio_delay_params_t;

/** Fastest LFO of a phaser or a chorus in Hz */
#define BOX_AUDIO_MODULATION_RATE_MAX 20.0f

/** Seconds of a change of a parameter of a phaser or a chorus.
 *
 * Also each half of the fade of the wet signal around a change of the stages of a phaser or
 * the voices of a chorus */
#define BOX_AUDIO_MODULATION_RAMP 0.02f

/** Frames between two updates of the coefficients of a phaser and of the read positions of a
 * chorus, which a chorus moves to linearly frame by frame */
#define BOX_AUDIO_MODULATION_CONTROL 8

/** Fewest and most allpass stages of a phaser; the count is even */
#define BOX_AUDIO_PHASER_STAGES_MIN 2
#define BOX_AUDIO_PHASER_STAGES_MAX 12

/** Lowest and highest center of a phaser in Hz; a swept frequency stays within 10 Hz and 0.45
 * of the rate */
#define BOX_AUDIO_PHASER_CENTER_MIN 20.0f
#define BOX_AUDIO_PHASER_CENTER_MAX 20000.0f

/** Octaves a phaser sweeps either way of its center at a depth of 1 */
#define BOX_AUDIO_PHASER_OCTAVES 2.0f

/** Largest feedback of a phaser, either way */
#define BOX_AUDIO_PHASER_FEEDBACK_MAX 0.9f

/** The parameters of a phaser, the first floats of the params of its effect command.
 *
 * A chain of first order allpasses per channel, swept exponentially around the center by a sine
 * LFO, fed back and mixed with the dry signal; stages allpasses give stages / 2 notches.
 *
 * The LFO starts at phase 0 when the effect is put on and stays continuous through a change of
 * the rate */
typedef struct box_audio_phaser_params_t {
    float stages;     /**< Allpasses per channel, BOX_AUDIO_PHASER_STAGES_MIN..MAX, rounded to
                       *   an even count */
    float rate;       /**< Hz of the LFO, 0..BOX_AUDIO_MODULATION_RATE_MAX */
    float depth;      /**< 0..1: the sweep reaches BOX_AUDIO_PHASER_OCTAVES times depth octaves
                       *   either way of the center */
    float center;     /**< Break frequency of the allpasses at the middle of the sweep in Hz,
                       *   BOX_AUDIO_PHASER_CENTER_MIN..MAX */
    float feedback;   /**< Share of the output of the chain fed back into its input,
                       *   -BOX_AUDIO_PHASER_FEEDBACK_MAX..BOX_AUDIO_PHASER_FEEDBACK_MAX */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix. 0.5 gives the
                       *   deepest notches */
    float stereo;     /**< Phase offset of the LFO of the right channel in half turns, 0..1 */
} box_audio_phaser_params_t;

/** Most voices of a chorus */
#define BOX_AUDIO_CHORUS_VOICES_MAX 4

/** Longest read of a chorus in milliseconds, which sizes its memory */
#define BOX_AUDIO_CHORUS_MAX 50.0f

/** Largest depth of a chorus in milliseconds */
#define BOX_AUDIO_CHORUS_DEPTH_MAX 25.0f

/** Largest feedback of a chorus, either way */
#define BOX_AUDIO_CHORUS_FEEDBACK_MAX 0.95f

/** The parameters of a chorus, the first floats of the params of its effect command.
 *
 * Voices taps of a stereo line, each read at delay plus depth times a sine LFO, spread evenly
 * in phase and panned by the spread. The LFO starts at phase 0 and stays continuous through a
 * change of the rate.
 *
 * The feedback adds the first tap of each channel to the input of its line, which turns a delay
 * of 0.1 to 10 ms into a flanger. A feedback of 0 renders as without it */
typedef struct box_audio_chorus_params_t {
    float voices;     /**< Taps, 1..BOX_AUDIO_CHORUS_VOICES_MAX, rounded */
    float rate;       /**< Hz of the LFOs, 0..BOX_AUDIO_MODULATION_RATE_MAX */
    float depth;      /**< Milliseconds the taps move either way, 0..BOX_AUDIO_CHORUS_DEPTH_MAX */
    float delay;      /**< Milliseconds of the taps at the middle, 0..BOX_AUDIO_CHORUS_MAX */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
    float spread;     /**< 0 keeps every voice in the center, so a mono input stays mono; 1 pans
                       *   the first voice left and the last right */
    float feedback;   /**< Share of the first tap fed back into the lines,
                       *   -BOX_AUDIO_CHORUS_FEEDBACK_MAX..BOX_AUDIO_CHORUS_FEEDBACK_MAX; NaN
                       *   is 0 */
} box_audio_chorus_params_t;

/** Largest shift of a pitch shifter in semitones, either way */
#define BOX_AUDIO_PITCH_SHIFTER_SEMITONES 24.0f

/** Largest detune of a pitch shifter in cents, either way */
#define BOX_AUDIO_PITCH_SHIFTER_CENTS 100.0f

/** Shortest and longest window of a pitch shifter in milliseconds; the longest sizes its
 * memory */
#define BOX_AUDIO_PITCH_SHIFTER_WINDOW_MIN 10.0f
#define BOX_AUDIO_PITCH_SHIFTER_WINDOW_MAX 100.0f

/** Largest feedback of a pitch shifter */
#define BOX_AUDIO_PITCH_SHIFTER_FEEDBACK_MAX 0.9f

/** Seconds of a change of the pitch, the window, the feedback or the mix of a pitch shifter */
#define BOX_AUDIO_PITCH_SHIFTER_RAMP 0.02f

/** The parameters of a pitch shifter, the first floats of the params of its effect command.
 *
 * A delay line pitch shifter per channel (shift.h). The left channel shifts by semitones plus
 * cents, the right by semitones plus cents times 1 - 2 stereo, so a stereo of 1 mirrors them.
 *
 * Feedback sends the shifted signal, soft clipped, back into the line, so each pass shifts
 * again. The wet signal comes up to a window late; the dry signal is not delayed */
typedef struct box_audio_pitch_shifter_params_t {
    float semitones;  /**< -BOX_AUDIO_PITCH_SHIFTER_SEMITONES..BOX_AUDIO_PITCH_SHIFTER_SEMITONES */
    float cents;      /**< -BOX_AUDIO_PITCH_SHIFTER_CENTS..BOX_AUDIO_PITCH_SHIFTER_CENTS */
    float stereo;     /**< 0..1: the cents of the right channel go from cents to -cents */
    float window;     /**< Milliseconds of the window, BOX_AUDIO_PITCH_SHIFTER_WINDOW_MIN..MAX */
    float feedback;   /**< Share of the shifted signal fed back into the line,
                       *   0..BOX_AUDIO_PITCH_SHIFTER_FEEDBACK_MAX */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
} box_audio_pitch_shifter_params_t;

/** Gain before the clipper of a saturator at a drive of 1 in dB; a drive of 0 is 0 dB */
#define BOX_AUDIO_SATURATOR_DRIVE_DB 30.0f

/** Offset a saturator at an asymmetry of 1 adds to its driven input before the clipper */
#define BOX_AUDIO_SATURATOR_BIAS 1.0f

/** Fewest and most bits of the bit crusher of a saturator; at the most it is off */
#define BOX_AUDIO_SATURATOR_BITS_MIN 1.0f
#define BOX_AUDIO_SATURATOR_BITS_MAX 16.0f

/** Cutoff of the DC blocker after the clipper of a saturator in Hz */
#define BOX_AUDIO_SATURATOR_DC 5.0f

/** Seconds of a change of the drive, the asymmetry, the bits, the mix, the cuts, the tone or
 * the rate of a saturator */
#define BOX_AUDIO_SATURATOR_RAMP 0.02f

/** Highest low cut and high cut of a saturator in Hz; a high cut is also held under
 * BOX_AUDIO_SATURATOR_CUT_NYQUIST times the rate */
#define BOX_AUDIO_SATURATOR_CUT_MAX 20000.0f

/** Share of the rate under which the high cut of a saturator stays, where it counts as off */
#define BOX_AUDIO_SATURATOR_CUT_NYQUIST 0.45f

/** Pivot of the tone tilt of a saturator in Hz */
#define BOX_AUDIO_SATURATOR_TONE_HZ 1000.0f

/** dB the tone tilt of a saturator at a tone of 1 raises the highs and lowers the lows */
#define BOX_AUDIO_SATURATOR_TONE_DB 9.0f

/** Lowest rate of the sample and hold of a saturator in Hz, when it is on */
#define BOX_AUDIO_SATURATOR_RATE_MIN 100.0f

/** Curve of the clipper of a saturator */
typedef enum box_audio_saturator_mode_t {
    BOX_AUDIO_SATURATOR_TANH = 0,    /**< tanh(g x + b) - tanh(b) */
    BOX_AUDIO_SATURATOR_TUBE = 1,    /**< tanh of the driven input with a level dependent
                                      *   offset, box_audio_saturate_tube */
    BOX_AUDIO_SATURATOR_HARD = 2,    /**< Hard clip with a rounded corner, with ADAA */
    BOX_AUDIO_SATURATOR_FOLD = 3,    /**< Sine wavefolder, with ADAA */
    BOX_AUDIO_SATURATOR_RECTIFY = 4, /**< tanh, then half to full wave rectified */
    BOX_AUDIO_SATURATOR_MODES = 5    /**< Number of modes */
} box_audio_saturator_mode_t;

/** The parameters of a saturator, the first floats of the params of its effect command: each
 * channel runs through a highpass, a tone tilt, the oversampled clipper of its mode
 * (saturate.h), a DC blocker, the inverse tilt, a lowpass, a sample and hold and a bit crusher.
 *
 * The dry signal is as late as the wet one, so that a mix does not comb. The reduction is the
 * largest drop in dB of the peak of the clipped signal against the driven input */
typedef struct box_audio_saturator_params_t {
    float drive;      /**< 0..1: a gain of 0 to BOX_AUDIO_SATURATOR_DRIVE_DB dB before the
                       *   clipper */
    float asymmetry;  /**< 0..1: a bias of up to BOX_AUDIO_SATURATOR_BIAS before the clipper,
                       *   shared by both channels, for even harmonics */
    float bits;       /**< BOX_AUDIO_SATURATOR_BITS_MIN..MAX: the wet signal is rounded to
                       *   2^bits levels over -1..1, 2^(bits - 1) steps of each sign; at
                       *   BOX_AUDIO_SATURATOR_BITS_MAX it is not rounded */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
    float mode;       /**< A box_audio_saturator_mode_t, rounded; others are
                       *   BOX_AUDIO_SATURATOR_TANH */
    float low_cut;    /**< Hz of a 12 dB Butterworth highpass before the clipper,
                       *   0..BOX_AUDIO_SATURATOR_CUT_MAX, 0 is off */
    float high_cut;   /**< Hz of a 12 dB Butterworth lowpass after the DC blocker,
                       *   0..BOX_AUDIO_SATURATOR_CUT_MAX, 0 is off */
    float tone;       /**< -1..1: a first order tilt around BOX_AUDIO_SATURATOR_TONE_HZ of
                       *   up to BOX_AUDIO_SATURATOR_TONE_DB at the ends before the clipper,
                       *   its exact inverse after it; 0 is off */
    float rate;       /**< Hz of a sample and hold after the high cut, 0 is off, otherwise
                       *   BOX_AUDIO_SATURATOR_RATE_MIN up to the rate, where it is off */
} box_audio_saturator_params_t;

/** Attack and release in seconds of the fast envelope of a transient shaper */
#define BOX_AUDIO_TRANSIENT_FAST_ATTACK 0.001f
#define BOX_AUDIO_TRANSIENT_FAST_RELEASE 0.02f

/** Attack and release in seconds of the slow envelope of a transient shaper */
#define BOX_AUDIO_TRANSIENT_SLOW_ATTACK 0.02f
#define BOX_AUDIO_TRANSIENT_SLOW_RELEASE 0.2f

/** Largest gain of a transient shaper either way in dB, at an attack or sustain of 1 or -1 */
#define BOX_AUDIO_TRANSIENT_RANGE 12.0f

/** Seconds of the one pole that smooths the gain of a transient shaper in dB; the peak
 * detector and the envelopes already keep it from following the cycles of a low tone */
#define BOX_AUDIO_TRANSIENT_SMOOTH 0.0005f

/** Seconds of a change of the attack or the sustain of a transient shaper */
#define BOX_AUDIO_TRANSIENT_RAMP 0.02f

/** The parameters of a transient shaper, the first floats of the params of its effect
 * command: stereo linked, it follows the peak of the louder channel with a fast and a slow
 * envelope (BOX_AUDIO_TRANSIENT_FAST_ATTACK and so on), the same gain on both channels.
 *
 * The fast one over the slow one in dB is a transient; the slow one over the fast one, or over
 * the peak when that is higher, is sustain, which ends as soon as the next hit comes up.
 *
 * The gain in dB is attack times the transient plus sustain times the sustain, each at most
 * BOX_AUDIO_TRANSIENT_RANGE and smoothed; 0 and 0 pass the signal unchanged */
typedef struct box_audio_transient_shaper_params_t {
    float attack;     /**< -1..1: above 0 boosts transients, below 0 softens them */
    float sustain;    /**< -1..1: above 0 boosts the tail, below 0 shortens it */
} box_audio_transient_shaper_params_t;

/** Fewest, most and default bands of a vocoder */
#define BOX_AUDIO_VOCODER_BANDS_MIN 8
#define BOX_AUDIO_VOCODER_BANDS_MAX 32
#define BOX_AUDIO_VOCODER_BANDS 16

/** Lowest band of a vocoder in Hz; the highest stays under 0.45 of the rate */
#define BOX_AUDIO_VOCODER_FREQUENCY_MIN 20.0f

/** Fewest octaves between the lowest and the highest band of a vocoder */
#define BOX_AUDIO_VOCODER_SPAN_MIN 1.0f

/** Lowest and highest q of a vocoder */
#define BOX_AUDIO_VOCODER_Q_MIN 0.25f
#define BOX_AUDIO_VOCODER_Q_MAX 8.0f

/** Octaves the carrier bands of a vocoder move at a shift of 1, either way */
#define BOX_AUDIO_VOCODER_SHIFT_OCTAVES 1.0f

/** Hz from which up the carrier bands of a vocoder take its unvoiced noise; the top quarter of
 * the bands takes it in any case */
#define BOX_AUDIO_VOCODER_UNVOICED_FROM 3000.0f

/** Peak of the white noise a vocoder adds to its carrier at an unvoiced of 1 */
#define BOX_AUDIO_VOCODER_NOISE 0.5f

/** RMS level of a full band modulator at which a vocoder gives about the level of its
 * carrier: that of white noise that fills -1..1 evenly, 1 / sqrt(3) */
#define BOX_AUDIO_VOCODER_REFERENCE 0.57735026918962576451f

/** Seconds of a change of the low, the high, the q, the shift, the unvoiced or the mix of a
 * vocoder */
#define BOX_AUDIO_VOCODER_RAMP 0.02f

/** Frames between two updates of the coefficients of a vocoder while its low, high, q or
 * shift moves */
#define BOX_AUDIO_VOCODER_CONTROL 16

/** The parameters of a vocoder, the first floats of the params of its effect command.
 *
 * The carrier is its own input, the modulator its key bus. Both run through bands matched
 * log spaced bandpasses; an envelope follower on each modulator band scales the carrier band of
 * the same index, and the carrier bands are summed, normalized by their width.
 *
 * The key bus must come before the bus of the vocoder in the order; otherwise, or without a
 * key, the vocoder passes its input */
typedef struct box_audio_vocoder_params_t {
    float bands;      /**< BOX_AUDIO_VOCODER_BANDS_MIN..MAX, rounded; a change fades the wet
                       *   signal out, restarts the bands from silence and fades it back in
                       *   over twice BOX_AUDIO_VOCODER_RAMP */
    float low;        /**< Center of the lowest band in Hz, BOX_AUDIO_VOCODER_FREQUENCY_MIN up
                       *   to 0.45 of the rate less BOX_AUDIO_VOCODER_SPAN_MIN octaves */
    float high;       /**< Center of the highest band in Hz, BOX_AUDIO_VOCODER_SPAN_MIN octaves
                       *   over low up to 0.45 of the rate */
    float q;          /**< Sharpness of the bands, BOX_AUDIO_VOCODER_Q_MIN..MAX: at 1
                       *   neighboring bands cross at -3 dB, above 1 they are narrower */
    float attack;     /**< Seconds in which a band envelope rises 63% of a step,
                       *   BOX_AUDIO_DYNAMICS_ATTACK_MIN..MAX */
    float release;    /**< Seconds in which it falls 63% of a step,
                       *   BOX_AUDIO_DYNAMICS_RELEASE_MIN..MAX */
    float shift;      /**< -1..1: moves the carrier bands BOX_AUDIO_VOCODER_SHIFT_OCTAVES times
                       *   shift octaves against the modulator bands, for a formant shift */
    float unvoiced;   /**< 0..1: white noise of BOX_AUDIO_VOCODER_NOISE times this peak added
                       *   to the carrier of the high bands, so consonants come through */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
} box_audio_vocoder_params_t;
/** Largest shift of a frequency shifter in Hz, either way */
#define BOX_AUDIO_FREQUENCY_SHIFTER_MAX 5000.0f

/** Largest feedback of a frequency shifter, either way */
#define BOX_AUDIO_FREQUENCY_SHIFTER_FEEDBACK_MAX 0.95f

/** Lowest frequency in Hz from which the Hilbert transformer of a frequency shifter keeps its
 * two paths 90 degrees apart, up to as far under the Nyquist frequency */
#define BOX_AUDIO_FREQUENCY_SHIFTER_LOW 20.0f

/** Allpasses of second order of the Hilbert transformer of a frequency shifter, over both of
 * its paths; 12 keep the image of a shift 60 dB under it from 20 Hz to 20 kHz at 48 kHz */
#define BOX_AUDIO_FREQUENCY_SHIFTER_ALLPASSES 12

/** Seconds of a change of the shift, the mode, the stereo, the feedback or the mix of a
 * frequency shifter */
#define BOX_AUDIO_FREQUENCY_SHIFTER_RAMP 0.02f

/** Modes of a frequency shifter */
typedef enum box_audio_frequency_shifter_mode_t {
    BOX_AUDIO_FREQUENCY_SHIFT = 0, /**< Every partial moves by the shift in Hz: single sideband
                                    *   modulation of the analytic signal */
    BOX_AUDIO_FREQUENCY_RING = 1   /**< Ring modulation by a sine at the magnitude of the
                                    *   shift: both sidebands at half the level, no carrier */
} box_audio_frequency_shifter_mode_t;

/** The parameters of a frequency shifter, the first floats of the params of its effect
 * command.
 *
 * The analytic signal of each channel, from an allpass pair Hilbert transformer, times a
 * complex oscillator at the shift, fed back through a DC blocker and mixed with the dry signal.
 *
 * The oscillators start at phase 0 when the effect is put on and stay continuous through a
 * change of the shift */
typedef struct box_audio_frequency_shifter_params_t {
    float shift;      /**< Hz, -BOX_AUDIO_FREQUENCY_SHIFTER_MAX..BOX_AUDIO_FREQUENCY_SHIFTER_MAX */
    float mode;       /**< A box_audio_frequency_shifter_mode_t, rounded; a change crossfades */
    float stereo;     /**< 0..1: the right channel shifts by shift times 1 - 2 stereo, so at 1
                       *   the opposite way */
    float feedback;   /**< Share of the output fed back into the input,
                       *   -BOX_AUDIO_FREQUENCY_SHIFTER_FEEDBACK_MAX..MAX */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
} box_audio_frequency_shifter_params_t;

/** Steps of the pattern of a pattern gate */
#define BOX_AUDIO_PATTERN_GATE_STEPS 16

/** Shortest and longest step of a pattern gate in beats */
#define BOX_AUDIO_PATTERN_GATE_RATE_MIN (1.0f / 64.0f)
#define BOX_AUDIO_PATTERN_GATE_RATE_MAX 16.0f

/** Largest swing of a pattern gate, that of Track */
#define BOX_AUDIO_PATTERN_GATE_SWING_MAX 0.75f

/** Shortest and longest attack or release of a pattern gate in seconds */
#define BOX_AUDIO_PATTERN_GATE_EDGE_MIN 0.0005f
#define BOX_AUDIO_PATTERN_GATE_EDGE_MAX 2.0f

/** Smallest share of a step a pattern gate is open */
#define BOX_AUDIO_PATTERN_GATE_GATE_MIN 0.05f

/** Lowest floor of a pattern gate in dB; the highest is 0 */
#define BOX_AUDIO_PATTERN_GATE_FLOOR_MIN (-80.0f)

/** Seconds of a change of the floor or the mix of a pattern gate */
#define BOX_AUDIO_PATTERN_GATE_RAMP 0.02f

/** The parameters of a pattern gate, the floats of the params of its effect command: a level
 * pattern locked to the beat clock of the core (box_audio_cmd_beat_t), the same gain on both
 * channels.
 *
 * At each frame the step floor(beat / rate) mod length sounds, on a grid that swing bends as a
 * Track swings its notes. A step is open for the first gate share of it, at its level, and
 * closed after; a level of 0 is closed all along.
 *
 * The gain is floor + (1 - floor) times the openness, which moves linearly to the level of the
 * step in attack seconds and back to 0 in release seconds, so changes do not click */
typedef struct box_audio_pattern_gate_params_t {
    float steps[BOX_AUDIO_PATTERN_GATE_STEPS]; /**< Level of each step, 0..1 */
    float length;     /**< Steps that play, rounded, 1..BOX_AUDIO_PATTERN_GATE_STEPS */
    float rate;       /**< Beats per step, BOX_AUDIO_PATTERN_GATE_RATE_MIN..MAX */
    float swing;      /**< 0..BOX_AUDIO_PATTERN_GATE_SWING_MAX: the second step of each pair
                       *   starts later by swing steps */
    float attack;     /**< Seconds of an edge from closed to open,
                       *   BOX_AUDIO_PATTERN_GATE_EDGE_MIN..MAX */
    float release;    /**< Seconds of an edge from open to closed,
                       *   BOX_AUDIO_PATTERN_GATE_EDGE_MIN..MAX */
    float gate;       /**< Share of each step that is open, BOX_AUDIO_PATTERN_GATE_GATE_MIN..1 */
    float floor;      /**< Gain when closed in dB, BOX_AUDIO_PATTERN_GATE_FLOOR_MIN..0 */
    float mix;        /**< Wet level, 0..1; the dry signal passes at 1 - mix */
} box_audio_pattern_gate_params_t;

/** Crossover of a rotary between its drum and its horn in Hz */
#define BOX_AUDIO_ROTARY_CROSSOVER 800.0f

/** Longest read of a rotary in milliseconds, which sizes its memory */
#define BOX_AUDIO_ROTARY_MAX 4.0f

/** Milliseconds of the read of the horn and of the drum of a rotary at the middle of a turn,
 * and how far the turn moves it either way: the Doppler shift */
#define BOX_AUDIO_ROTARY_HORN_DELAY 1.5f
#define BOX_AUDIO_ROTARY_HORN_DEPTH 0.35f
#define BOX_AUDIO_ROTARY_DRUM_DELAY 1.5f
#define BOX_AUDIO_ROTARY_DRUM_DEPTH 0.15f

/** Share of the level the horn and the drum of a rotary lose as they face away from a mic */
#define BOX_AUDIO_ROTARY_HORN_AM 0.6f
#define BOX_AUDIO_ROTARY_DRUM_AM 0.4f

/** Cutoff in Hz of the lowpass the horn and the drum of a rotary fade into as they face away
 * from a mic, and the share of it they reach facing away */
#define BOX_AUDIO_ROTARY_HORN_TONE 2500.0f
#define BOX_AUDIO_ROTARY_HORN_DULL 0.6f
#define BOX_AUDIO_ROTARY_DRUM_TONE 300.0f
#define BOX_AUDIO_ROTARY_DRUM_DULL 0.3f

/** Inertia of the drum of a rotary against that of the horn */
#define BOX_AUDIO_ROTARY_DRUM_INERTIA 3.0f

/** Shortest and longest acceleration of the horn of a rotary in seconds */
#define BOX_AUDIO_ROTARY_ACCELERATION_MIN 0.01f
#define BOX_AUDIO_ROTARY_ACCELERATION_MAX 20.0f

/** Gain before the clipper of the preamp of a rotary at a drive of 1 in dB */
#define BOX_AUDIO_ROTARY_DRIVE_DB 24.0f

/** Offset the preamp of a rotary at a drive of 1 adds before its clipper, for the even
 * harmonics of a tube */
#define BOX_AUDIO_ROTARY_BIAS 0.2f

/** Seconds of silent input after which a rotary clears itself and idles */
#define BOX_AUDIO_ROTARY_QUIET 0.05f

/** Level of its preamp, filters and lines below which a rotary that has been quiet idles, so
 * the tail of its DC blocker does not click when it is cleared */
#define BOX_AUDIO_ROTARY_SILENCE 1e-6f

/** The parameters of a rotary, the first floats of the params of its effect command: a
 * rotating speaker cabinet.
 *
 * The mono sum runs through a tube preamp and a crossover into a drum and a horn that turn
 * opposite ways. Two virtual mics per rotor, spread apart, read it with Doppler, and its level
 * drops and its tone dulls as the rotor faces away.
 *
 * Each rotor turns at its slow rate plus the speed times the way to its fast rate, moving to a
 * new rate exponentially: the horn within the acceleration, the drum
 * BOX_AUDIO_ROTARY_DRUM_INERTIA times slower. */
typedef struct box_audio_rotary_params_t {
    float speed;        /**< 0..1: 0 is slow (chorale), 1 fast (tremolo) */
    float horn_slow;    /**< Hz of the horn at speed 0, 0..BOX_AUDIO_MODULATION_RATE_MAX */
    float horn_fast;    /**< Hz of the horn at speed 1, 0..BOX_AUDIO_MODULATION_RATE_MAX */
    float drum_slow;    /**< Hz of the drum at speed 0, 0..BOX_AUDIO_MODULATION_RATE_MAX */
    float drum_fast;    /**< Hz of the drum at speed 1, 0..BOX_AUDIO_MODULATION_RATE_MAX */
    float acceleration; /**< Seconds of the horn to a new rate,
                         *   BOX_AUDIO_ROTARY_ACCELERATION_MIN..MAX */
    float balance;      /**< -1..1: -1 is the drum alone, 1 the horn alone, 0 both */
    float spread;       /**< 0..1: 0 puts both mics at the same place, so the output is mono;
                         *   1 puts them half a turn apart */
    float drive;        /**< 0..1: the share of the preamp, its gain 0 to
                         *   BOX_AUDIO_ROTARY_DRIVE_DB dB */
    float mix;          /**< Wet level, 0..1; the dry signal passes at 1 - mix */
} box_audio_rotary_params_t;

/** Longest slice of a beat repeat in seconds, which sizes its memory: the longest slice of
 * BOX_AUDIO_BEAT_REPEAT_SLICE_MAX beats at the slowest tempo is held to it */
#define BOX_AUDIO_BEAT_REPEAT_MAX 4.0f

/** Shortest and longest slice of a beat repeat in beats; the longest grid is the longest
 * slice */
#define BOX_AUDIO_BEAT_REPEAT_SLICE_MIN (1.0f / 32.0f)
#define BOX_AUDIO_BEAT_REPEAT_SLICE_MAX 4.0f

/** Largest pitch of a beat repeat per repeat in semitones, either way */
#define BOX_AUDIO_BEAT_REPEAT_PITCH_MAX 12.0f

/** Largest pitch a repeat of a beat repeat reaches in semitones, either way, the pitch of the
 * repeats added up: the speed of a read stays within 1/16..16 */
#define BOX_AUDIO_BEAT_REPEAT_PITCH_TOTAL 48.0f

/** Shortest gate of a beat repeat */
#define BOX_AUDIO_BEAT_REPEAT_GATE_MIN 0.1f

/** Seconds of the crossfades of a beat repeat at the ends of its repeats and of the return
 * of the dry signal, at most a quarter of the slice */
#define BOX_AUDIO_BEAT_REPEAT_FADE 0.002f

/** Seconds of a change of the mix of a beat repeat */
#define BOX_AUDIO_BEAT_REPEAT_RAMP 0.02f

/** The parameters of a beat repeat, the first floats of the params of its effect command: when
 * active rises to 0.5 or above, it captures a slice of slice beats from the next grid point of
 * the beat clock, at most BOX_AUDIO_BEAT_REPEAT_MAX seconds.
 *
 * Each repeat after the first plays the slice decay times as loud and pitch semitones from the
 * one before, like a tape, reversed when reverse is set, for gate of the slice. Decay, pitch,
 * reverse and gate apply from the next repeat on; slice and grid at the next rise.
 *
 * When active falls under 0.5 the dry signal returns over BOX_AUDIO_BEAT_REPEAT_FADE. While it
 * repeats the output is the dry signal times 1 - mix plus the repeats times mix */
typedef struct box_audio_beat_repeat_params_t {
    float active;     /**< 0.5 and above repeats, under it passes the input */
    float slice;      /**< Beats of the slice, BOX_AUDIO_BEAT_REPEAT_SLICE_MIN..MAX */
    float grid;       /**< Beats of the grid the capture waits for,
                       *   0..BOX_AUDIO_BEAT_REPEAT_SLICE_MAX; 0 captures at once, a negative
                       *   grid is the slice */
    float decay;      /**< Gain of a repeat against the one before it, 0..1 */
    float pitch;      /**< Semitones of a repeat against the one before it,
                       *   -BOX_AUDIO_BEAT_REPEAT_PITCH_MAX..BOX_AUDIO_BEAT_REPEAT_PITCH_MAX */
    float reverse;    /**< 0.5 and above plays the repeats after the first from the end */
    float gate;       /**< Share of each repeat that sounds, BOX_AUDIO_BEAT_REPEAT_GATE_MIN..1 */
    float mix;        /**< Level of the repeats while it repeats, 0..1; the dry signal passes
                       *   at 1 - mix */
} box_audio_beat_repeat_params_t;

/** Longest lag of a tape stop behind its input in seconds, which sizes its memory */
#define BOX_AUDIO_TAPE_STOP_MAX 4.0f

/** Shortest and longest stop and start of a tape stop in seconds */
#define BOX_AUDIO_TAPE_STOP_TIME_MIN 0.01f
#define BOX_AUDIO_TAPE_STOP_TIME_MAX 4.0f

/** Speed under which the output of a tape stop fades out, linearly to silence at a speed of
 * 0 */
#define BOX_AUDIO_TAPE_STOP_FADE 0.1f

/** Seconds of the crossfade of a tape stop from the read that lags behind its input after a
 * start to the input */
#define BOX_AUDIO_TAPE_STOP_CROSSFADE 0.02f

/** The parameters of a tape stop, the first floats of the params of its effect command: a
 * stereo line that records the input, read at a speed with cubic interpolation, so the pitch
 * and the time slow down together.
 *
 * While active is 0.5 or above, the speed falls from 1 to 0 over stop_time seconds, else it
 * rises back over start_time. At a phase p of the ramp the speed is 1 - p^(4^curve).
 *
 * Under a speed of BOX_AUDIO_TAPE_STOP_FADE the output fades out. Once a start is up to speed
 * it crossfades to the input over BOX_AUDIO_TAPE_STOP_CROSSFADE, which then passes unchanged */
typedef struct box_audio_tape_stop_params_t {
    float active;     /**< 0.5 and above stops, under it starts */
    float stop_time;  /**< Seconds of the stop, BOX_AUDIO_TAPE_STOP_TIME_MIN..MAX */
    float start_time; /**< Seconds of the start, BOX_AUDIO_TAPE_STOP_TIME_MIN..MAX */
    float curve;      /**< Shape of the ramps, -1..1 */
} box_audio_tape_stop_params_t;

/** Put an effect into an insert slot of a bus, change its parameters, move it or take it off.
 *
 * The inserts of a bus run in the order of their index, before its width, pan and volume; those
 * of the master before the limiter. A change of kind, bus or index fades the effect out of its
 * bus and into its new place; kind NONE or bus BUS_NONE takes it off the same way.
 *
 * When its bus left the order its tail fades out into the master instead. An effect whose kind
 * needs memory runs once the memory of its slot arrived (box_audio_core_effect_memory).
 */
typedef struct box_audio_cmd_effect_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_EFFECT */
    uint32_t slot;                  /**< Slot of the effect, below BOX_AUDIO_EFFECT_MAX */
    uint32_t kind;                  /**< A box_audio_effect_kind_t */
    uint32_t bus;                   /**< Bus whose signal it processes, or BOX_AUDIO_BUS_NONE */
    uint32_t index;                 /**< Insert slot within the bus, below
                                     *   BOX_AUDIO_EFFECT_SLOTS */
    uint32_t key;                   /**< Sidechain bus, BOX_AUDIO_BUS_NONE for none: a
                                     *   compressor keys on its output in the same block, a
                                     *   vocoder takes it as its modulator */
    float params[BOX_AUDIO_EFFECT_PARAMS]; /**< The parameters of the kind, clamped by it, the
                                     *   ones it does not read 0 */
} box_audio_cmd_effect_t;

/** Replace the parameters of the effect in a slot at a frame.
 *
 * Acts as an EFFECT command that keeps its kind, bus, index and key; ignored when the slot does
 * not hold an effect of the kind by then. The automation schedules one per value it samples */
typedef struct box_audio_cmd_effect_params_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_EFFECT_PARAMS */
    uint32_t when_lo;               /**< Low word of the frame it applies at, 0 for now */
    uint32_t when_hi;               /**< High word of the frame */
    uint32_t slot;                  /**< Slot of the effect, below BOX_AUDIO_EFFECT_MAX */
    uint32_t kind;                  /**< The box_audio_effect_kind_t the slot must hold */
    float params[BOX_AUDIO_EFFECT_PARAMS]; /**< The parameters of the kind */
} box_audio_cmd_effect_params_t;

#endif
