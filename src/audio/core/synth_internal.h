/**
 * @file audio/core/synth_internal.h
 * @brief The private parts of the synth source that synth.c, synth_voice.c and synth_render.c
 * share: its constants and the functions one of them defines and another calls.
 */

#ifndef BOX_AUDIO_CORE_SYNTH_INTERNAL_H
#define BOX_AUDIO_CORE_SYNTH_INTERNAL_H

#include "internal.h"

/** Natural logarithm of 2 */
#define BOX_AUDIO_SYNTH_LN2 0.69314718055994530942

/** Square root of 2 */
#define BOX_AUDIO_SYNTH_SQRT2 1.41421356237309504880

/** Frames between two updates of the coefficients of a filter whose cutoff an envelope moves */
#define BOX_AUDIO_SYNTH_CONTROL 8

/** Largest share of the sample rate an oscillator plays at, under Nyquist with room for the
 * correction of the band limited waves */
#define BOX_AUDIO_SYNTH_NYQUIST 0.48

/** Largest share of the sample rate of a cutoff; a higher one, Nyquist included, is clamped */
#define BOX_AUDIO_SYNTH_CUTOFF_NYQUIST 0.49

/** Smallest pitch factor of a play, which the pitch variance cannot go under */
#define BOX_AUDIO_SYNTH_VARIANCE_MIN 0.01f

/** Seconds over which an LFO wave that jumps, square, saw or random, moves to its new value */
#define BOX_AUDIO_SYNTH_LFO_SMOOTH 0.002

/** Seconds each of the two lowpasses of a drift walk takes to move 1 - 1/e of the way to its
 * target. A jump of the target from -1 to 1 moves the value, -1..1, by at most
 * 2 / (e * this) per second, so a drift of 20 cents moves about 1.2 cents per 50 ms */
#define BOX_AUDIO_SYNTH_DRIFT_SMOOTH 0.6

/** Shortest and longest seconds a drift walk holds a target */
#define BOX_AUDIO_SYNTH_DRIFT_HOLD_MIN 0.2
#define BOX_AUDIO_SYNTH_DRIFT_HOLD_MAX 2.0

/** Share of the drift of the pitch the cutoff drifts by, in cents */
#define BOX_AUDIO_SYNTH_DRIFT_CUTOFF 0.25

/** Shortest fade in seconds of an LFO with a delay, so its depth does not jump in at once */
#define BOX_AUDIO_SYNTH_LFO_FADE_MIN 0.005f

/** Pi */
#define BOX_AUDIO_SYNTH_PI 3.14159265358979323846

/** Hz of the lowest pole of the filter of pink noise, under which it is flat */
#define BOX_AUDIO_SYNTH_PINK_LOW 5.0

/** Ratio of the frequencies of two poles of the filter of pink noise, two octaves; the zero of
 * each section lies half way between, an octave over its pole */
#define BOX_AUDIO_SYNTH_PINK_SPACING 4.0

/** Hz of the leak of the integrator of brown noise, under which it is flat */
#define BOX_AUDIO_SYNTH_BROWN_LEAK 10.0

/** Hz of the highpass that keeps brown noise from its DC */
#define BOX_AUDIO_SYNTH_BROWN_DC 5.0

/** Turns per radian */
#define BOX_AUDIO_SYNTH_TURNS (1.0 / 6.28318530717958647692)

/** Gain of each of n unison voices, 1 / sqrt(n), so their power sums to that of one */
static const double box_audio_synth_unison_gain[BOX_AUDIO_SYNTH_UNISON_MAX + 1] = {
    1.0, 1.0, 0.70710678118654752440, 0.57735026918962576451, 0.5, 0.44721359549995793928,
    0.40824829046386301637, 0.37796447300922722721 };

/** Base 2 exponent of a fall of 60 dB, 3 log2(10) */
#define BOX_AUDIO_SYNTH_DB60 9.96578428466208704361

/** Shortest period of a string in frames: a delay line of 2 frames, the lowpass and the
 * allpass */
#define BOX_AUDIO_SYNTH_STRING_PERIOD_MIN 4.0

/** Longest period of a string in frames, within its delay line */
#define BOX_AUDIO_SYNTH_STRING_PERIOD_MAX ((double)(BOX_AUDIO_SYNTH_STRING_FRAMES - 2))

/** Largest gain of the loop of a string, under 1 so that every string decays */
#define BOX_AUDIO_SYNTH_STRING_GAIN_MAX 0.99999

/** Frames between the exact tunings of a string while its pitch moves, the control block of
 * the LFOs: between them a cheap tuning follows the pitch every frame, so a vibrato costs its
 * two sines and exponential once a block and not every frame */
#define BOX_AUDIO_SYNTH_STRING_RETUNE 8

/** Shortest period of a string in frames the cheap tuning serves, under which the series of
 * its sines would lose accuracy and a moving pitch tunes exactly every frame */
#define BOX_AUDIO_SYNTH_STRING_CHEAP 16.0

/** Frames of the crossfade of a string from its old read point to the new one when its delay
 * line moves by whole frames */
#define BOX_AUDIO_SYNTH_STRING_FADE 8

/** Frames of the past a new read point of a string runs through its lowpass and allpass
 * before it plays, so the allpass starts settled: its coefficient is at most a third, so the
 * error of its start falls under 2 % */
#define BOX_AUDIO_SYNTH_STRING_PRIME 4

/** Peak of a period of a string under which it is silent, -80 dB of its pluck */
#define BOX_AUDIO_SYNTH_STRING_SILENCE 1e-4f

/** Shortest period of a bowed string in frames: room for its two delay lines of
 * BOX_AUDIO_SYNTH_BOW_DELAY_MIN frames and the phase delay of its lowpass, 4 kHz at 48 kHz */
#define BOX_AUDIO_SYNTH_BOW_PERIOD_MIN 12.0

/** Shortest delay of a side of a bowed string in frames, so the 4 taps of its interpolation lie
 * in the past */
#define BOX_AUDIO_SYNTH_BOW_DELAY_MIN 2.0

/** Longest delay of a side of a bowed string in frames, so its last tap lies within its line */
#define BOX_AUDIO_SYNTH_BOW_DELAY_MAX ((double)(BOX_AUDIO_SYNTH_STRING_FRAMES - 3))

/** Velocity of the bow at speed 1 and level 1 of its envelope, in the unit of the waves */
#define BOX_AUDIO_SYNTH_BOW_VELOCITY 0.25f

/** Slope of the friction curve times the velocity of the bow at pressure 0, and how much
 * pressure 1 lowers it: the stick region of the curve is the wider the lower the slope.
 *
 * The slope is over the velocity of the bow, so the motion of the string scales with the speed
 * of the bow, and 0.05..0.2 keeps it in the Helmholtz motion over the range of the pressure */
#define BOX_AUDIO_SYNTH_BOW_SLOPE 0.2f
#define BOX_AUDIO_SYNTH_BOW_SLOPE_RANGE 0.15f

/** Smallest and largest reflection of the friction of the bow, slip and stick */
#define BOX_AUDIO_SYNTH_BOW_SLIP 0.01f
#define BOX_AUDIO_SYNTH_BOW_STICK 0.98f

/** Level of the envelope of a bowed layer under which the bow lifts off the string: its
 * contact is the level over this, up to 1 */
#define BOX_AUDIO_SYNTH_BOW_LIFT 0.25f

/** Noise of the bow hair at noise 1, a share of the velocity of the bow */
#define BOX_AUDIO_SYNTH_BOW_HAIR 0.5f

/** Harmonic of the note the lowpass at the bridge of a bowed string cuts at, at brightness 0 and
 * pressure 0.5, and the octaves brightness 1 raises it by.
 *
 * The lowpass follows the pitch, so the loop damps each harmonic the same at every note, which
 * keeps the Helmholtz motion stable from the lowest to the highest notes */
#define BOX_AUDIO_SYNTH_BOW_HARMONIC 4.0
#define BOX_AUDIO_SYNTH_BOW_OCTAVES 2.5

/** Octaves the lowpass at the bridge rises by from pressure 0 to 1, half of it either side of
 * pressure 0.5: a harder bow sharpens the corner of the Helmholtz motion that the losses round */
#define BOX_AUDIO_SYNTH_BOW_PRESSURE_OCTAVES 3.0

/** Largest share of the rate of the lowpass at the bridge */
#define BOX_AUDIO_SYNTH_BOW_CUTOFF_MAX 0.45

/** Share of the time constant of the lowpass at the bridge by which the friction advances the
 * corner of the Helmholtz motion it sharpens, which would make the note sharp: the loop is
 * that much longer */
#define BOX_AUDIO_SYNTH_BOW_ADVANCE 0.17

/** Periods the fundamental of a bowed string takes to fall 60 dB once the bow lifted, from the
 * gain of its loop, up to BOX_AUDIO_SYNTH_BOW_DECAY_MAX seconds; the lowpass at the bridge
 * shortens it */
#define BOX_AUDIO_SYNTH_BOW_DECAY 220.0
#define BOX_AUDIO_SYNTH_BOW_DECAY_MAX 4.0

/** Share of the wave at the bridge a bow position earlier that the output takes off it, so the
 * bow position notches the harmonics at its multiples of 1 / position */
#define BOX_AUDIO_SYNTH_BOW_NOTCH 0.5f

/** Hz of the DC blocker of the output of a bowed string */
#define BOX_AUDIO_SYNTH_BOW_DC 20.0

/** Seconds the bow takes to move 1 - 1/e of the way to the pressure, speed and position the
 * routes give */
#define BOX_AUDIO_SYNTH_BOW_SMOOTH 0.005

/** Gain of the output of a bowed string, taken at the bridge */
#define BOX_AUDIO_SYNTH_BOW_LEVEL 1.0f

/** Bits of the targets of the routes that move the bow of a bowed layer */
#define BOX_AUDIO_SYNTH_BOW_TARGETS ((1u << BOX_AUDIO_SYNTH_TO_BOW_PRESSURE) |\
    (1u << BOX_AUDIO_SYNTH_TO_BOW_SPEED) | (1u << BOX_AUDIO_SYNTH_TO_BOW_POSITION))

/** Envelope of the layers of strings without one, a level of 1 that never ends */
#define BOX_AUDIO_SYNTH_ENV_FREE (BOX_AUDIO_SYNTH_LAYERS + 1)

/** Shortest delay of a comb filter in frames, so the 4 taps of its interpolation lie in the
 * past: Nyquist */
#define BOX_AUDIO_SYNTH_COMB_DELAY_MIN 2.0

/** Longest delay of a comb filter in frames, so its last tap lies within its delay line */
#define BOX_AUDIO_SYNTH_COMB_DELAY_MAX ((double)(BOX_AUDIO_SYNTH_COMB_FRAMES - 3))

/** The factor per frame of an exponential sweep.
 *
 * @param from Value at the start, positive
 * @param to Value at the end, positive
 * @param frames Frames of the sweep, at least 1
 * @return The factor
 */
double box_audio_synth_ratio(
    double from,
    double to,
    int32_t frames);

/** Frames of a time at a rate, rounded to the nearest.
 *
 * @param seconds The time, 0..BOX_AUDIO_SYNTH_LENGTH_MAX
 * @param rate Frames per second
 * @return Number of frames
 */
int32_t box_audio_synth_frames(
    float seconds,
    float rate);

/** Start an envelope: its segments in frames, a pass at most BOX_AUDIO_SYNTH_LENGTH_MAX long,
 * and every segment that changes the level at least BOX_AUDIO_SYNTH_EDGE long, so it neither
 * clicks nor runs forever.
 *
 * With a sustain of 0 the envelope ends after its decay, gated or not.
 *
 * @param e The envelope of the voice
 * @param d The envelope of the patch
 * @param rate Frames per second
 * @param gate Whether it sustains in its hold until a release
 */
void box_audio_synth_env_init(
    box_audio_synth_env_t *e,
    const box_audio_synth_envelope_t *d,
    float rate,
    bool gate);

/** Enter a segment of an envelope; one without frames takes its level at once, which the
 * edge of BOX_AUDIO_SYNTH_EDGE keeps to segments that do not change it.
 *
 * The hold of a gated envelope with a sustain lasts until a release: box_audio_synth_env_step
 * renews its frames.
 *
 * A linear segment adds a step per frame. A curved one of curve c over n frames follows
 * box_audio_synth_curve_coefs: one multiply and one add per frame, exp only here, and the last
 * frame takes the target, so the segment ends on time.
 *
 * @param e The envelope
 * @param segment A box_audio_synth_segment_t
 */
void box_audio_synth_env_enter(
    box_audio_synth_env_t *e,
    int32_t segment);

/** Advance an envelope by a frame other than one inside a linear segment: one that enters the
 * next segments, the last frame of a segment, which takes its target, or one of a curve.
 *
 * @param e The envelope
 * @return Its level for the frame
 */
float box_audio_synth_env_step(
    box_audio_synth_env_t *e);

/** Set the coefficients of the stages of the filter from its cutoff: g = tan(pi cutoff / rate)
 * of the trapezoidal state variable filter, or of the ladder, which stay stable at any cutoff
 * and while it moves. Each stage is stable on its own, so their cascade is too.
 *
 * A comb filter takes the share of its lowpass instead, 1 - exp(-2 pi cutoff / rate), and 1,
 * undamped, at the top of the cutoff.
 *
 * @param src The source
 * @param rate Frames per second
 */
void box_audio_synth_coefs(
    box_audio_synth_source_t *src,
    float rate);

/** Set the indexes and the feedback of the oscillators from the patch and the routes.
 *
 * FM is not band limited, so the index of phase modulation is limited: by Carson's rule the
 * carrier plus (index + 1) times the modulator, the higher ends of sweeps, stays under the top.
 *
 * @param src The source
 */
void box_audio_synth_limits(
    box_audio_synth_source_t *src);

/** Move the cutoff by the level of the filter envelope, exponentially in frequency.
 *
 * The cutoff is that of the patch times 2 to the amount times the level, to the octaves of the
 * LFOs and the routes, to the keytrack times the key and to the pressure, unless a route takes the
 * pressure, clamped to the range of a cutoff.
 *
 * @param src The source
 * @param level Level of the filter envelope, 0..1
 * @param rate Frames per second
 */
void box_audio_synth_cutoff(
    box_audio_synth_source_t *src,
    float level,
    float rate);

/** Move the vowel of a formant filter by the level of the filter envelope: the vowel of the
 * patch plus vowel_amount times the level and the vowels of the LFOs, clamped to
 * 0..BOX_AUDIO_SYNTH_VOWEL_MAX.
 *
 * @param src The source, with a formant filter
 * @param level Level of the filter envelope, 0..1
 * @param rate Frames per second
 */
void box_audio_synth_vowel(
    box_audio_synth_source_t *src,
    float level,
    float rate);

/** Hash the seed of a patch with the count of its plays into the state of a play.
 *
 * @param seed The seed
 * @param plays The count
 * @return The state, never 0
 */
uint32_t box_audio_synth_hash(
    uint32_t seed,
    uint32_t plays);

/** Set the filter that colors the noise of a layer for a rate and clear its state.
 *
 * Pink: sections of a pole and a zero an octave over it, the poles two octaves apart up to
 * Nyquist. Brown: a leaky integrator, then a highpass of its DC. The gain is 1 over the RMS
 * gain, from the partial fractions of the cascade: H = d + sum r_k / (1 - p_k / z).
 *
 * @param f The filter
 * @param color A box_audio_synth_color_t
 * @param rate Frames per second
 */
void box_audio_synth_color_init(
    box_audio_synth_color_filter_t *f,
    uint32_t color,
    float rate);

/** Aim every destination of the LFOs of a source at their values: the factors of the pitch,
 * the level and the pulse width of each oscillator, which move there linearly over a control
 * block or take them at once, and the cutoff, which takes them at once.
 *
 * The pitch envelope of a layer adds to its pitch and to its ratio of hard sync, and the routes
 * of the modulation matrix to the destinations they share with the LFOs. A route from mod turns
 * off the depth of the vibrato that mod scales.
 *
 * The pitch takes 2 to the cents over 1200, one exp2 per pitched oscillator. The level dips
 * from 1 at the top of the wave to 1 - amplitude at its bottom, so an LFO in its delay leaves
 * it at 1.
 *
 * @param src The source
 * @param rate Frames per second
 * @param now Whether the destinations take their values at once, at the start of a pass
 */
void box_audio_synth_lfo_aim(
    box_audio_synth_source_t *src,
    float rate,
    bool now);

/** Value of the wave of an LFO at its phase, before its smoothing and its fade.
 *
 * @param m The LFO
 * @return The value, -1..1
 */
float box_audio_synth_lfo_wave(
    const box_audio_synth_mod_t *m);

/** Start the LFOs of a pass: each from its start phase, before its delay and faded out, but
 * one without either at its full depth. The pass then sets the destinations to their values.
 *
 * @param src The source
 * @param rate Frames per second
 */
void box_audio_synth_lfo_pass(
    box_audio_synth_source_t *src,
    float rate);

/** Start the drift of a play: its own generator, seeded from the seed of the patch and the
 * count of its plays apart from the noise and the LFOs, so they stay as without drift.
 *
 * It picks the start phase of each oscillator with free_phase, and with drift a walk for each
 * layer, each of its unison voices and the cutoff.
 *
 * @param core The core
 * @param src The source, its layers set
 * @param p The patch, clamped
 * @param plays Count of the plays of the patch
 */
void box_audio_synth_drift_start(
    const box_audio_core_t *core,
    box_audio_synth_source_t *src,
    const box_audio_synth_patch_t *p,
    uint32_t plays);

/** Advance the drift of a source by a control block: the walk of each layer, each of its
 * unison voices and the cutoff.
 *
 * @param src The source, with drift
 * @param rate Frames per second
 */
void box_audio_synth_drift_block(
    box_audio_synth_source_t *src,
    float rate);

/** Start the pitch envelopes of a pass: each at its amount, which decays to 0 over its decay
 * in control blocks, not released.
 *
 * @param src The source
 * @param rate Frames per second
 */
void box_audio_synth_bend_pass(
    box_audio_synth_source_t *src,
    float rate);

/** Advance the pitch envelopes of a source by a control block. One whose layer has an
 * amplitude envelope, its own or that of the patch, starts its release once that envelope
 * reached its release, over the frames left of it, from its offset toward release_amount.
 *
 * @param src The source
 */
void box_audio_synth_bend_block(
    box_audio_synth_source_t *src);

/** Retune a string whose pitch moves: exactly every BOX_AUDIO_SYNTH_STRING_RETUNE frames and
 * at a short period, cheaply on the frames between.
 *
 * When the delay line changes by whole frames the allpass fraction jumps from one end of its
 * range to the other, and the read point by a frame. The old read point and its allpass play
 * on for BOX_AUDIO_SYNTH_STRING_FADE frames and crossfade into the new, primed one.
 *
 * A crossing during a crossfade ends it: the read point that was new becomes the old one.
 *
 * @param s The string
 * @param inc Turns per frame of its oscillator
 */
void box_audio_synth_string_move(
    box_audio_synth_string_t *s,
    double inc);

/** Delay in frames a comb filter aims at for its note: the period of the first layer times
 * the pitch of the comb and its tune, within BOX_AUDIO_SYNTH_COMB_MIN and the delay line.
 *
 * Less the phase delay of the lowpass of its loop at its lowest peak, the note with positive
 * feedback and half of it with negative, so that peak stays in tune.
 *
 * The lowpass y = a x + b y1, b = 1 - a, delays a frequency w by
 * atan(b sin w / (1 - b cos w)) / w frames.
 *
 * @param src The source, with a comb filter
 * @param rate Frames per second
 * @return The delay in frames
 */
double box_audio_synth_comb_aim(
    const box_audio_synth_source_t *src,
    float rate);

/** Retune a bowed string whose pitch or bow pressure moves: exactly every
 * BOX_AUDIO_SYNTH_STRING_RETUNE frames, cheaply on the frames between, as
 * box_audio_synth_string_move does a pluck.
 *
 * The string goes on from where it is: its delay lines are read by an interpolation of their
 * fractional delay, so a read point that moves by whole frames moves on smoothly and needs no
 * crossfade.
 *
 * @param s The string, bowed
 * @param inc Turns per frame of its oscillator
 * @param pressure Pressure of the bow, 0..1
 */
void box_audio_synth_bow_move(
    box_audio_synth_string_t *s,
    double inc,
    float pressure);

/** Start a pass of the envelopes, the oscillators and the filter envelope, at the start of a
 * play and at each restart of a loop, and pluck the strings. The state of the filter goes on;
 * a comb filter takes the delay of the note at once.
 *
 * @param core The core
 * @param src The source
 */
void box_audio_synth_pass(
    box_audio_core_t *core,
    box_audio_synth_source_t *src);

/** Advance the key of a source by a frame toward its target, and move the cutoff with it
 * every BOX_AUDIO_SYNTH_CONTROL frames and at its end. An LFO of the cutoff or a filter
 * envelope that runs takes the key in its own update of the cutoff instead.
 *
 * @param src The source, whose key moves
 * @param rate Frames per second
 */
void box_audio_synth_key_next(
    box_audio_synth_source_t *src,
    float rate);

/** Advance an envelope by a frame. A frame inside a linear segment adds the step here, as
 * before curves, so a patch without curves pays a test of curved per frame and nothing else;
 * every other frame takes box_audio_synth_env_step.
 *
 * @param e The envelope
 * @return Its level for the frame
 */
static inline float box_audio_synth_env_next(
    box_audio_synth_env_t *e)
{
    float result;
    if (e->left > 1 && !e->curved) {
        e->left --;
        e->value = e->value + e->step;
        result = e->value;
    } else {
        result = box_audio_synth_env_step(e);
    }

    return result;
}

/** Next value of the white noise of a source, xorshift32.
 *
 * @param state The state, never 0
 * @return A value in -1..1
 */
static inline float box_audio_synth_noise(
    uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;

    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

/** Color a frame of white noise of a unison voice by the filter of its layer.
 *
 * @param f The filter, with sections
 * @param voice The unison voice, 0 for a layer without unison
 * @param x The white noise
 * @return The colored noise, at about the RMS level of the white
 */
static inline float box_audio_synth_color_next(
    box_audio_synth_color_filter_t *f,
    int32_t voice,
    float x)
{
    float *s = f->state[voice], in;
    int32_t i, n = f->sections;
    for (i = 0; i < n; i ++) {
        in = x;
        x = in - f->zero[i] * s[i] + f->pole[i] * s[i + 1];
        s[i] = in;
    }

    s[n] = x;

    return x * f->gain;
}

/** Run a sample of the mix of the layers of a channel through the drive.
 *
 * @param src The source, with a drive
 * @param c The channel, 0 left or mono, 1 right
 * @param x The mix
 * @return The driven mix, BOX_AUDIO_SATURATE_LATENCY frames late
 */
static inline float box_audio_synth_drive_next(
    box_audio_synth_source_t *src,
    int32_t c,
    float x)
{
    float y = box_audio_saturate_next(&src->drive_os[c], &src->drive_curve, x);
    if (src->drive_dc) {
        y = box_audio_dc_next(&src->drive_blocker[c], y, src->drive_dc_coef);
    }

    return y;
}

#endif
