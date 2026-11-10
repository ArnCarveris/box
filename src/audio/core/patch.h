/**
 * @file audio/core/patch.h
 * @brief Synth patches of the audio core and the command that makes a sound a synth.
 */

#ifndef BOX_AUDIO_CORE_PATCH_H
#define BOX_AUDIO_CORE_PATCH_H

#include <stddef.h>
#include "modulation.h"

/** Waves of a synth oscillator */
typedef enum box_audio_synth_wave_t {
    BOX_AUDIO_SYNTH_SINE = 0,      /**< Sine */
    BOX_AUDIO_SYNTH_SAW = 1,       /**< Saw, band limited */
    BOX_AUDIO_SYNTH_SQUARE = 2,    /**< Square, band limited */
    BOX_AUDIO_SYNTH_TRIANGLE = 3,  /**< Triangle */
    BOX_AUDIO_SYNTH_NOISE = 4,     /**< White noise, seeded per play */
    BOX_AUDIO_SYNTH_PLUCK = 5,     /**< A Karplus-Strong string plucked with seeded noise */
    BOX_AUDIO_SYNTH_WAVETABLE = 6, /**< The frames of a wavetable, morphed by a position; a
                                    *   sine without a table */
    BOX_AUDIO_SYNTH_BOWED = 7      /**< A waveguide string driven by a friction bow, whose speed
                                    *   follows the envelope of the layer */
} box_audio_synth_wave_t;

/** Colors of the noise of a synth oscillator, each at about the RMS level of white noise */
typedef enum box_audio_synth_color_t {
    BOX_AUDIO_SYNTH_WHITE = 0,     /**< White, flat */
    BOX_AUDIO_SYNTH_PINK = 1,      /**< Pink, falls by 3 dB per octave */
    BOX_AUDIO_SYNTH_BROWN = 2      /**< Brown, falls by 6 dB per octave */
} box_audio_synth_color_t;

/** Kinds of the filter of a synth patch */
typedef enum box_audio_synth_filter_t {
    BOX_AUDIO_SYNTH_FILTER_NONE = 0,  /**< No filter */
    BOX_AUDIO_SYNTH_LOWPASS = 1,      /**< Lowpass, 12 dB per octave */
    BOX_AUDIO_SYNTH_HIGHPASS = 2,     /**< Highpass, 12 dB per octave */
    BOX_AUDIO_SYNTH_BANDPASS = 3,     /**< Bandpass, unity at the cutoff */
    BOX_AUDIO_SYNTH_NOTCH = 4,        /**< Notch, silent at the cutoff */
    BOX_AUDIO_SYNTH_FORMANT = 5,      /**< Three bandpasses in parallel at the formants of a
                                       * vowel, see formant.h; the cutoff, the slope and the
                                       * model do nothing */
    BOX_AUDIO_SYNTH_COMB = 6          /**< A feedback comb at the note times the tune, see
                                       * synth.c; the cutoff damps it, the resonance, the
                                       * slope and the model do nothing */
} box_audio_synth_filter_t;

/** Slopes of the filter of a synth patch */
typedef enum box_audio_synth_slope_t {
    BOX_AUDIO_SYNTH_SLOPE_12 = 0,  /**< 12 dB per octave, one stage */
    BOX_AUDIO_SYNTH_SLOPE_24 = 1   /**< 24 dB per octave, two cascaded stages */
} box_audio_synth_slope_t;

/** Models of the filter of a synth patch */
typedef enum box_audio_synth_model_t {
    BOX_AUDIO_SYNTH_SVF = 0,     /**< The state variable filter of svf.h */
    BOX_AUDIO_SYNTH_LADDER = 1   /**< The transistor ladder of ladder.h: 4 saturating poles
                                  * that oscillate at resonance 1; the slope picks the 4-pole
                                  * or the 2-pole output */
} box_audio_synth_model_t;

/** Waves of a synth LFO; each starts at 0 rising at phase 0 but the square, which starts at 1,
 * and the random one, which holds a new random value each period */
typedef enum box_audio_synth_lfo_wave_t {
    BOX_AUDIO_SYNTH_LFO_SINE = 0,      /**< Sine */
    BOX_AUDIO_SYNTH_LFO_TRIANGLE = 1,  /**< Triangle */
    BOX_AUDIO_SYNTH_LFO_SQUARE = 2,    /**< Square, its edges smoothed over 2 ms */
    BOX_AUDIO_SYNTH_LFO_SAW = 3,       /**< Rising saw, its drop smoothed over 2 ms */
    BOX_AUDIO_SYNTH_LFO_RANDOM = 4     /**< Sample and hold of random values, seeded per play,
                                        * its steps smoothed over 2 ms */
} box_audio_synth_lfo_wave_t;

/** Sources of a route of the modulation matrix of a synth patch, in the order of the
 * SynthModSource enum of box/audio.h */
typedef enum box_audio_synth_source_kind_t {
    BOX_AUDIO_SYNTH_FROM_AMP = 0,       /**< The amp envelope: of the layer of a layer route,
                                         * else of the patch, 0..1 */
    BOX_AUDIO_SYNTH_FROM_FILTER = 1,    /**< The filter envelope, 0..1, which runs for the route
                                         * without a filter too */
    BOX_AUDIO_SYNTH_FROM_MOD_ENV = 2,   /**< The mod envelope of the patch, 0..1 */
    BOX_AUDIO_SYNTH_FROM_LFO1 = 3,      /**< The first LFO, -1..1, after its delay and fade */
    BOX_AUDIO_SYNTH_FROM_LFO2 = 4,      /**< The second LFO, the same way */
    BOX_AUDIO_SYNTH_FROM_VELOCITY = 5,  /**< Velocity of the note, 0..1 */
    BOX_AUDIO_SYNTH_FROM_KEY = 6,       /**< Key of the voice in octaves from the root */
    BOX_AUDIO_SYNTH_FROM_RANDOM = 7,    /**< A random value per note, -1..1, seeded */
    BOX_AUDIO_SYNTH_FROM_MOD = 8,       /**< Mod of the voice, 0..1 */
    BOX_AUDIO_SYNTH_FROM_PRESSURE = 9   /**< Pressure of the voice, 0..1 */
} box_audio_synth_source_kind_t;

/** Sources of a route of a synth patch, one past the last */
#define BOX_AUDIO_SYNTH_SOURCES 10

/** Targets of a route of the modulation matrix of a synth patch, in the order of the
 * SynthModTarget enum of box/audio.h, with the unit of the amount at source value 1.
 *
 * A new target takes the next value, a case in box_audio_synth_routes of synth.c and its clamp
 * in synth_patch.c */
typedef enum box_audio_synth_target_t {
    BOX_AUDIO_SYNTH_TO_CUTOFF = 0,      /**< Octaves of the cutoff */
    BOX_AUDIO_SYNTH_TO_RESONANCE = 1,   /**< Share of the resonance, clamped 0..1 */
    BOX_AUDIO_SYNTH_TO_INDEX = 2,       /**< Radians of the index of a modulator layer, or the
                                         * depth of a ring modulator */
    BOX_AUDIO_SYNTH_TO_PITCH = 3,       /**< Semitones of the pitch */
    BOX_AUDIO_SYNTH_TO_PAN = 4,         /**< Pan of the voice, -1..1, smoothed */
    BOX_AUDIO_SYNTH_TO_DETUNE = 5,      /**< Cents of the unison detune */
    BOX_AUDIO_SYNTH_TO_PULSE_WIDTH = 6, /**< Share of the period of the pulse width */
    BOX_AUDIO_SYNTH_TO_LEVEL = 7,       /**< Share of the level of a layer, clamped at 0,
                                         * smoothed */
    BOX_AUDIO_SYNTH_TO_SYNC = 8,        /**< Semitones of the ratio of hard sync of a synced
                                         * layer, clamped 1..BOX_AUDIO_SYNTH_SYNC_MAX */
    BOX_AUDIO_SYNTH_TO_POSITION = 9,    /**< Frames of the position of a wavetable layer,
                                         * clamped to its frames */
    BOX_AUDIO_SYNTH_TO_VOWEL = 10,      /**< Vowels of a formant filter, clamped
                                         * 0..BOX_AUDIO_SYNTH_VOWEL_MAX */
    BOX_AUDIO_SYNTH_TO_BOW_PRESSURE = 11, /**< Share of the bow pressure of a bowed layer,
                                         * clamped 0..1 */
    BOX_AUDIO_SYNTH_TO_BOW_SPEED = 12,  /**< Share of the bow speed of a bowed layer, clamped
                                         * 0..1 */
    BOX_AUDIO_SYNTH_TO_BOW_POSITION = 13 /**< Share of the string of the bow position of a
                                         * bowed layer, clamped BOX_AUDIO_SYNTH_BOW_POSITION_MIN
                                         * ..BOX_AUDIO_SYNTH_BOW_POSITION_MAX */
} box_audio_synth_target_t;

/** Targets of a route of a synth patch, one past the last */
#define BOX_AUDIO_SYNTH_TARGETS 14

/** How a synth oscillator that modulates another one changes it */
typedef enum box_audio_synth_modulation_t {
    BOX_AUDIO_SYNTH_PHASE = 0,  /**< Adds to the phase of the carrier, FM as DX synths do it */
    BOX_AUDIO_SYNTH_RING = 1    /**< Multiplies the carrier */
} box_audio_synth_modulation_t;

/** An envelope of a synth patch: an attack to 1, a decay to the sustain level, a hold there
 * and a release to 0, each linear or curved and ending on time */
typedef struct box_audio_synth_envelope_t {
    float attack;        /**< Seconds from 0 to 1 */
    float decay;         /**< Seconds from 1 to the sustain level */
    float sustain;       /**< Level after the decay, 0..1 */
    float release;       /**< Seconds from the sustain level to 0 */
    float hold;          /**< Seconds at the sustain level before the release */
    float curve;         /**< Curve of the decay and the release, 0 linear, positive fast at
                          *   first, -BOX_AUDIO_SYNTH_CURVE_MAX..BOX_AUDIO_SYNTH_CURVE_MAX */
    float attack_curve;  /**< Curve of the attack, the same way */
    float key_scale;     /**< 0..1: the decay and the release are times 2^(-key_scale key / 12)
                          *   at the key of a note, 12 log2 of its pitch ratio, fixed at its
                          *   start; 0 for none and always 0 for the filter envelope */
} box_audio_synth_envelope_t;

/** A pitch envelope of a synth patch: an offset of the pitch in semitones relative to the note,
 * amount at its start, which returns to 0 over the decay with a curve, and moves to
 * release_amount over the release of the amplitude envelope of the layer. All 0 for none */
typedef struct box_audio_synth_pitch_envelope_t {
    float amount;          /**< Semitones at the start of a note,
                            *   -BOX_AUDIO_SYNTH_PITCH_SEMITONES..max */
    float decay;           /**< Seconds from amount back to the note */
    float curve;           /**< Curve of the decay and the release, as the curve of a
                            *   box_audio_synth_envelope_t */
    float release_amount;  /**< Semitones the offset moves to over the release, the same range */
    float sync;            /**< Semitones of the ratio of hard sync at the start of a note, which
                            *   return to 0 with the decay; only on layers with a ratio over 1 */
} box_audio_synth_pitch_envelope_t;

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
    float pulse_width;                    /**< Square: share of the period it is high,
                                           * BOX_AUDIO_SYNTH_PULSE_MIN..1 minus it; 0 for
                                           * BOX_AUDIO_SYNTH_PULSE_DEFAULT */
    uint32_t voices;                      /**< Unison voices, 1..BOX_AUDIO_SYNTH_UNISON_MAX,
                                           * 0 counts as 1 */
    float detune;                         /**< Cents from the lowest unison voice to the
                                           * highest, 0..BOX_AUDIO_SYNTH_DETUNE_MAX */
    float width;                          /**< Stereo width of the unison voices, 0 (mono)
                                           * to 1 (from left to right) */
    float decay;                          /**< BOX_AUDIO_SYNTH_PLUCK: seconds the fundamental
                                           * of the string takes to fall by 60 dB */
    float brightness;                     /**< BOX_AUDIO_SYNTH_PLUCK: 0..1, 0 a lowpass in the
                                           * loop that damps the harmonics fast, 1 almost none */
    float hardness;                       /**< BOX_AUDIO_SYNTH_PLUCK: 0..1, lowpass of the noise
                                           * that plucks the string, 0 a soft finger, 1 a pick */
    uint32_t rings_free;                  /**< BOX_AUDIO_SYNTH_PLUCK: 1 when it uses no
                                           * envelope at all and the string alone ends it */
    float sub;                            /**< Level of the sub oscillator, a band limited
                                           * square under it, 0..1, 0 for none; ignored by
                                           * noise and pluck */
    uint32_t sub_octave;                  /**< Octaves the sub is under it, 1 or 2, 0 counts
                                           * as 1 */
    float index_velocity;                 /**< Share 0..1 of the index of a modulator that
                                           * velocity scales: index (1 - share + share v) */
    float hardness_velocity;              /**< BOX_AUDIO_SYNTH_PLUCK: share 0..1 of the
                                           * hardness that velocity scales the same way */
    float sync;                           /**< Hard sync: ratio of its wave to its frequency,
                                           * 1..BOX_AUDIO_SYNTH_SYNC_MAX; 0 counts as 1, off.
                                           * Ignored by noise and pluck */
    uint32_t has_pitch_envelope;          /**< 1 when it has its own pitch envelope, 0 for the
                                           * one of the patch */
    box_audio_synth_pitch_envelope_t pitch_envelope; /**< Its own pitch envelope, when
                                           * has_pitch_envelope */
    uint32_t color;                       /**< BOX_AUDIO_SYNTH_NOISE: a
                                           * box_audio_synth_color_t, 0 white; ignored by
                                           * the other waves */
    uint32_t table;                       /**< BOX_AUDIO_SYNTH_WAVETABLE: id of the sound of
                                           * its wavetable, 0 for none, a sine; ignored by
                                           * the other waves */
    float position;                       /**< BOX_AUDIO_SYNTH_WAVETABLE: frame of the table
                                           * it plays, 0..frames - 1, between two frames a
                                           * linear mix of them */
    float position_envelope;              /**< BOX_AUDIO_SYNTH_WAVETABLE: frames the position
                                           * moves at level 1 of its envelope, either way */
    float bow_pressure;                   /**< BOX_AUDIO_SYNTH_BOWED: force of the bow, 0..1:
                                           * the width of the stick region of its friction */
    float bow_speed;                      /**< BOX_AUDIO_SYNTH_BOWED: velocity of the bow at
                                           * level 1 of its envelope, 0..1; 0 is silent */
    float bow_position;                   /**< BOX_AUDIO_SYNTH_BOWED: distance of the bow from
                                           * the bridge as a share of the string, MIN..MAX of
                                           * BOX_AUDIO_SYNTH_BOW_POSITION; 0 for the default */
    float bow_noise;                      /**< BOX_AUDIO_SYNTH_BOWED: noise of the bow hair
                                           * added at the contact point, 0..1 */
    float bow_velocity;                   /**< BOX_AUDIO_SYNTH_BOWED: share 0..1 of the
                                           * pressure and the speed that velocity scales */
} box_audio_synth_layer_t;

/** An LFO of a synth patch: a wave at a rate that starts after a delay and fades in, with a
 * depth per destination; a depth of 0 leaves that destination alone */
typedef struct box_audio_synth_lfo_t {
    uint32_t wave;          /**< A box_audio_synth_lfo_wave_t */
    float rate;             /**< Hz, 0..BOX_AUDIO_SYNTH_LFO_RATE_MAX */
    float delay;            /**< Seconds before it starts, at depth 0 meanwhile */
    float fade;             /**< Seconds it fades in over after its delay */
    float phase;            /**< Phase at its start in turns, 0..1 */
    uint32_t random_phase;  /**< 1 to start at a random phase per play instead, seeded */
    uint32_t layer;         /**< 1 + the layer it modulates, 0 for every layer; the cutoff is
                             * of the patch either way */
    float pitch;            /**< Cents of the pitch at the top of the wave */
    float amplitude;        /**< Share of the level it takes at the bottom of the wave, 0..1 */
    float cutoff;           /**< Octaves of the cutoff at the top of the wave */
    float pulse_width;      /**< Share of the period the pulse width of squares moves by */
    float hard_sync;        /**< Semitones the ratio of hard sync moves by at the top of the
                             * wave, -BOX_AUDIO_SYNTH_SYNC_SEMITONES..max; on a layer it syncs
                             * it, for every layer only those with a ratio over 1 */
    float vowel;            /**< Vowels of a formant filter at the top of the wave,
                             * -BOX_AUDIO_SYNTH_VOWEL_MAX..max; of the patch, as the cutoff */
    float position;         /**< Frames the position of wavetable layers moves by at the top
                             * of the wave, -BOX_AUDIO_TABLE_FRAMES_MAX..max; the moved
                             * position is clamped to the frames of the table */
    float sync;             /**< Cycle in beats, 0..BOX_AUDIO_SYNTH_LFO_SYNC_MAX, 0 for rate:
                             * when positive it runs at the tempo of the play over sync and
                             * starts at phase + fract(beat / sync) of the play */
} box_audio_synth_lfo_t;

/** A route of the modulation matrix of a synth patch: the value of a source times an amount,
 * added to a target on top of the fixed routes */
typedef struct box_audio_synth_route_t {
    uint32_t source;        /**< A box_audio_synth_source_kind_t */
    uint32_t target;        /**< A box_audio_synth_target_t */
    uint32_t layer;         /**< 1 + the layer it moves, and whose amp envelope is its source,
                             * 0 for every layer; the cutoff, resonance and pan are of the patch
                             * either way */
    float amount;           /**< Amount at source value 1, in the unit of the target */
} box_audio_synth_route_t;

/** A synth patch: up to BOX_AUDIO_SYNTH_LAYERS oscillators with envelopes, then one filter,
 * then an amp, and up to BOX_AUDIO_SYNTH_LFOS LFOs that move them. Plain values in seconds
 * and Hz, the same at every sample rate */
typedef struct box_audio_synth_patch_t {
    uint32_t layers;                      /**< Oscillators that play, 0..BOX_AUDIO_SYNTH_LAYERS */
    uint32_t seed;                        /**< Seed of the noise and of the variances */
    float pitch_variance;                 /**< Random share of the pitch per play, 0..1 */
    float level_variance;                 /**< Random share of the level per play, 0..1 */
    float drift;                          /**< Cents the pitch of each layer and unison voice
                                           * wanders within, 0..BOX_AUDIO_SYNTH_DRIFT_MAX, and
                                           * the cutoff by a quarter of it; 0 for none */
    uint32_t free_phase;                  /**< 1 to start the oscillators of a play at a
                                           * random phase, 0 at phase 0 */
    box_audio_synth_envelope_t envelope;  /**< Envelope of the layers without their own */
    uint32_t has_drive;                   /**< 1 to run the mix of the layers through the
                                           * drive before the filter, 0 for none */
    float pre_drive;                      /**< Drive 0..1: a gain of BOX_AUDIO_SYNTH_DRIVE_DB_MIN
                                           * to _MAX dB before a tanh soft clipper at twice
                                           * the rate, level compensated to keep the RMS */
    float pre_asymmetry;                  /**< 0..1: an offset of up to
                                           * BOX_AUDIO_SYNTH_DRIVE_BIAS before the clipper,
                                           * even harmonics; its DC is blocked */
    uint32_t filter;                      /**< A box_audio_synth_filter_t */
    float cutoff;                         /**< Hz */
    float resonance;                      /**< 0..1, 1 rings the longest */
    uint32_t slope;                       /**< A box_audio_synth_slope_t */
    float drive;                          /**< Saturation of the filter, 0 (linear) to 1; the
                                           * gain into the ladder */
    uint32_t model;                       /**< A box_audio_synth_model_t, 0 the SVF */
    float filter_amount;                  /**< Octaves the filter envelope moves the cutoff at
                                           * its level 1, negative closes it; 0 for none.
                                           * Ignored by BOX_AUDIO_SYNTH_FORMANT */
    box_audio_synth_envelope_t filter_envelope; /**< Envelope of the cutoff, which does not
                                           * keep the voice playing */
    float filter_velocity;                /**< Octaves of the cutoff at velocity 1 over velocity
                                           * 0, -BOX_AUDIO_SYNTH_OCTAVES_MAX..max: the cutoff
                                           * is cutoff * 2^(filter_velocity (v - 1)); 0 for none */
    float filter_envelope_velocity;       /**< Share 0..1 of filter_amount that velocity
                                           * scales: amount (1 - share + share v) */
    float keytrack;                       /**< 0..1: the cutoff is times the current pitch
                                           * ratio of the voice, without its variance, to the
                                           * keytrack, at the control rate; 0 for none */
    float glide;                          /**< Seconds a change of the pitch of a voice takes,
                                           * exponential in frequency, 0 jumps at once */
    uint32_t lfos;                        /**< LFOs that run, 0..BOX_AUDIO_SYNTH_LFOS */
    box_audio_synth_lfo_t lfo[BOX_AUDIO_SYNTH_LFOS]; /**< The LFOs */
    box_audio_synth_pitch_envelope_t pitch_envelope; /**< Pitch envelope of the layers without
                                           * their own, all 0 for none */
    float vowel;                          /**< BOX_AUDIO_SYNTH_FORMANT: the vowel,
                                           * 0..BOX_AUDIO_SYNTH_VOWEL_MAX, fractions between */
    float filter_vowel;                   /**< BOX_AUDIO_SYNTH_FORMANT: vowels the filter
                                           * envelope moves the vowel by at its level 1, up to
                                           * BOX_AUDIO_SYNTH_VOWEL_MAX either way; 0 for none */
    float feedback;                       /**< BOX_AUDIO_SYNTH_COMB: gain of the delayed output
                                           * added to the input, up to
                                           * BOX_AUDIO_SYNTH_COMB_FEEDBACK_MAX either way.
                                           *
                                           * Positive peaks at the harmonics of the note,
                                           * negative at the odd harmonics of half of it */
    float tune;                           /**< BOX_AUDIO_SYNTH_COMB: ratio of the frequency of
                                           * the comb to the note,
                                           * BOX_AUDIO_SYNTH_TUNE_MIN..MAX; 0 counts as 1 */
    box_audio_synth_envelope_t mod_envelope; /**< The mod envelope, a source of routes, which
                                           * runs when a route uses it and does not keep the
                                           * voice playing */
    uint32_t routes;                      /**< Routes that run, 0..BOX_AUDIO_SYNTH_ROUTES */
    box_audio_synth_route_t route[BOX_AUDIO_SYNTH_ROUTES]; /**< The routes of the modulation
                                           * matrix, which add to the fixed ones */
    box_audio_synth_layer_t layer[BOX_AUDIO_SYNTH_LAYERS]; /**< The oscillators, last: a
                                           * command sends only the layers of its patch */
} box_audio_synth_patch_t;

/** Make a sound id a synth patch, or replace the patch of a synth sound. Voices that play it
 * keep the patch they started with; the next play takes the new one.
 *
 * The command ends after the layers of its patch: it is BOX_AUDIO_CMD_SYNTH_SIZE(layers)
 * bytes, so a patch of 2 layers does not send the room of BOX_AUDIO_SYNTH_LAYERS */
typedef struct box_audio_cmd_synth_t {
    box_audio_cmd_header_t header;  /**< BOX_AUDIO_CMD_SYNTH */
    uint32_t sound;                 /**< Id of the sound, free or a synth */
    box_audio_synth_patch_t patch;  /**< The patch, clamped by the core, up to its layers */
} box_audio_cmd_synth_t;

/** Bytes of a synth command whose patch has a number of layers, at most
 * BOX_AUDIO_SYNTH_LAYERS */
#define BOX_AUDIO_CMD_SYNTH_SIZE(layers) \
    ((int32_t)(offsetof(box_audio_cmd_synth_t, patch.layer) + \
    (size_t)(layers) * sizeof(box_audio_synth_layer_t)))

#endif
