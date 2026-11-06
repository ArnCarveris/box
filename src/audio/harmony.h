/**
 * @file audio/harmony.h
 * @brief Names of pitches, chords and keys from note data, plain C without Flecs: the pitch
 * classes that sound in a span weighted by beats, the chord they make and the most likely key
 * by the Krumhansl-Kessler profiles.
 *
 * The functions are pure: the same weights give the same names, byte for byte.
 */

#ifndef BOX_AUDIO_HARMONY_H
#define BOX_AUDIO_HARMONY_H

#include <stdbool.h>
#include <stdint.h>

/** Bytes of a name that the functions write, enough for a slash chord or a set of 12 classes */
#define BOX_AUDIO_HARMONY_NAME (64)

/** Share of the weight of the strongest pitch class a class needs to be part of a chord */
#define BOX_AUDIO_HARMONY_SHARE (0.2f)

/** A set of pitch classes, bit i for pitch class i, 0 for C */
typedef uint16_t box_audio_pcs_t;

/** A key: a tonic and a mode */
typedef struct box_audio_key_t {
    int32_t tonic;         /**< Pitch class of the tonic, 0 for C, -1 for no key */
    bool minor;            /**< A minor key, else major */
    float score;           /**< Correlation of the weights with the profile of the key */
} box_audio_key_t;

/** A chord: a root, a quality and a bass, or only a set of pitch classes when none fits */
typedef struct box_audio_chord_t {
    int32_t root;          /**< Pitch class of the root, -1 when no chord fits */
    int32_t quality;       /**< Index of the quality, see box_audio_chord_quality */
    int32_t bass;          /**< Pitch class of the lowest note, -1 for none */
    box_audio_pcs_t set;   /**< The pitch classes the chord is named from */
} box_audio_chord_t;

/** Get the name of a pitch class.
 *
 * @param pc Pitch class, wrapped to 0..11
 * @param flats Spell black keys with flats, else with sharps
 * @return The name, such as "C#" or "Db"
 */
const char* box_audio_pc_name(
    int32_t pc,
    bool flats);

/** Write the name of a MIDI pitch with its octave, C4 for 60, with the cents of a microtone.
 *
 * @param pitch MIDI note number
 * @param flats Spell black keys with flats
 * @param out Receives the name, such as "C4", "F#2" or "A4+25c", BOX_AUDIO_HARMONY_NAME bytes
 */
void box_audio_pitch_name(
    float pitch,
    bool flats,
    char *out);

/** Get the set of the pitch classes whose weight is at least share times the largest one.
 *
 * @param weights Weight of each pitch class, such as the beats it sounds
 * @param share Share of the largest weight, such as BOX_AUDIO_HARMONY_SHARE
 * @return The set, empty when every weight is 0
 */
box_audio_pcs_t box_audio_pcs_from_weights(
    const float weights[12],
    float share);

/** Count the pitch classes of a set.
 *
 * @param set The set
 * @return The count, 0..12
 */
int32_t box_audio_pcs_count(
    box_audio_pcs_t set);

/** Get the suffix of a chord quality.
 *
 * The qualities are "" (major), "m", "7", "maj7", "m7", "m7b5", "dim", "aug", "sus4", "sus2",
 * "add9", "dim7" and "5" (a power chord).
 *
 * @param quality Index of the quality
 * @return The suffix, NULL past the last quality
 */
const char* box_audio_chord_quality(
    int32_t quality);

/** Name the chord of a set of pitch classes.
 *
 * A chord fits when its notes are the set, or the set without the fifth of a seventh chord;
 * when none fits four or more classes, the weakest class but the bass is left out once.
 *
 * Of chords that fit, the one with the bass as root wins, then the first quality.
 *
 * @param set The pitch classes
 * @param weights Weight of each pitch class, to find the weakest, NULL for none
 * @param bass Pitch class of the lowest note, -1 for none
 * @return The chord, its root -1 when no chord fits
 */
box_audio_chord_t box_audio_chord_find(
    box_audio_pcs_t set,
    const float weights[12],
    int32_t bass);

/** Write the name of a chord: "C", "Am7", "G7/B", a set such as "[C D F#]" when no chord
 * fits, or "N.C." for an empty set.
 *
 * @param chord The chord
 * @param flats Spell black keys with flats
 * @param out Receives the name, BOX_AUDIO_HARMONY_NAME bytes
 */
void box_audio_chord_name(
    const box_audio_chord_t *chord,
    bool flats,
    char *out);

/** Find the most likely key of the weights of the pitch classes: the major or minor
 * Krumhansl-Kessler profile, rotated to each tonic, that correlates best with them.
 *
 * @param weights Weight of each pitch class, such as the beats it sounds
 * @param second Receives the next most likely key, NULL to ignore
 * @return The key, tonic -1 when every weight is 0
 */
box_audio_key_t box_audio_key_find(
    const float weights[12],
    box_audio_key_t *second);

/** Get the pitch classes of a key: its major scale, or its natural minor scale and the
 * raised seventh, the leading tone.
 *
 * @param key The key
 * @return The set, empty for no key
 */
box_audio_pcs_t box_audio_key_scale(
    box_audio_key_t key);

/** Tell whether a key is spelled with flats: F, Bb, Eb, Ab and Db major and D, G, C, F, Bb and
 * Eb minor.
 *
 * @param key The key
 * @return Whether its black keys are flats
 */
bool box_audio_key_flats(
    box_audio_key_t key);

/** Write the name of a key, such as "C major", "F# minor" or "none".
 *
 * @param key The key
 * @param out Receives the name, BOX_AUDIO_HARMONY_NAME bytes
 */
void box_audio_key_name(
    box_audio_key_t key,
    char *out);

#endif
