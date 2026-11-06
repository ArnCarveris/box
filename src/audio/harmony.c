/**
 * @file audio/harmony.c
 * @brief Names of pitches, chords and keys from note data: chord templates matched on a set of
 * pitch classes and the Krumhansl-Kessler key profiles correlated with weights of the classes.
 */

#include "harmony.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** All twelve pitch classes */
#define BOX_AUDIO_PCS_ALL (0xFFF)

/** Bits of pitch classes, relative to a root */
#define BOX_AUDIO_PC(i) ((box_audio_pcs_t)(1u << (i)))

/** A chord quality: its suffix and its pitch classes from the root */
typedef struct box_audio_quality_t {
    const char *suffix;    /**< Suffix after the root, "" for major */
    box_audio_pcs_t mask;  /**< Pitch classes relative to the root */
    bool open_fifth;       /**< The chord still fits without its fifth */
} box_audio_quality_t;

/** The qualities, in the order a tie picks them */
static const box_audio_quality_t box_audio_qualities[] = {
    { "", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(4) | BOX_AUDIO_PC(7), false },
    { "m", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(3) | BOX_AUDIO_PC(7), false },
    { "7", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(4) | BOX_AUDIO_PC(7) | BOX_AUDIO_PC(10), true },
    { "maj7", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(4) | BOX_AUDIO_PC(7) | BOX_AUDIO_PC(11), true },
    { "m7", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(3) | BOX_AUDIO_PC(7) | BOX_AUDIO_PC(10), true },
    { "m7b5", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(3) | BOX_AUDIO_PC(6) | BOX_AUDIO_PC(10), false },
    { "dim", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(3) | BOX_AUDIO_PC(6), false },
    { "aug", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(4) | BOX_AUDIO_PC(8), false },
    { "sus4", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(5) | BOX_AUDIO_PC(7), false },
    { "sus2", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(2) | BOX_AUDIO_PC(7), false },
    { "add9", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(2) | BOX_AUDIO_PC(4) | BOX_AUDIO_PC(7), false },
    { "dim7", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(3) | BOX_AUDIO_PC(6) | BOX_AUDIO_PC(9), false },
    { "5", BOX_AUDIO_PC(0) | BOX_AUDIO_PC(7), false }
};

/** Number of qualities */
#define BOX_AUDIO_QUALITY_COUNT \
    ((int32_t)(sizeof(box_audio_qualities) / sizeof(box_audio_qualities[0])))

/** Names of the pitch classes with sharps */
static const char *box_audio_sharp_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/** Names of the pitch classes with flats */
static const char *box_audio_flat_names[12] = {
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
};

/** The Krumhansl-Kessler profile of a major key, from its tonic */
static const float box_audio_major_profile[12] = {
    6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
};

/** The Krumhansl-Kessler profile of a minor key, from its tonic */
static const float box_audio_minor_profile[12] = {
    6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
};

/** Wrap a pitch class to 0..11.
 *
 * @param pc The pitch class
 * @return The class in 0..11
 */
static int32_t box_audio_pc_wrap(
    int32_t pc)
{
    return ((pc % 12) + 12) % 12;
}

/** Rotate a set of pitch classes up.
 *
 * @param set The set
 * @param r Semitones, 0..11
 * @return The rotated set
 */
static box_audio_pcs_t box_audio_pcs_rotate(
    box_audio_pcs_t set,
    int32_t r)
{
    uint32_t s = set & BOX_AUDIO_PCS_ALL;

    return (box_audio_pcs_t)(((s << r) | (s >> (12 - r))) & BOX_AUDIO_PCS_ALL);
}

const char* box_audio_pc_name(
    int32_t pc,
    bool flats)
{
    int32_t i = box_audio_pc_wrap(pc);

    return flats ? box_audio_flat_names[i] : box_audio_sharp_names[i];
}

void box_audio_pitch_name(
    float pitch,
    bool flats,
    char *out)
{
    float p = isfinite(pitch) ? pitch : 0.0f;
    int32_t n = (int32_t)lroundf(p);
    int32_t cents = (int32_t)lroundf((p - (float)n) * 100.0f);
    int32_t octave = (int32_t)floor((double)n / 12.0) - 1;
    if (cents) {
        snprintf(out, BOX_AUDIO_HARMONY_NAME, "%s%d%+dc", box_audio_pc_name(n, flats), octave,
            cents);
    } else {
        snprintf(out, BOX_AUDIO_HARMONY_NAME, "%s%d", box_audio_pc_name(n, flats), octave);
    }
}

box_audio_pcs_t box_audio_pcs_from_weights(
    const float weights[12],
    float share)
{
    box_audio_pcs_t result = 0;
    float max = 0;
    int32_t i;
    for (i = 0; i < 12; i ++) {
        max = weights[i] > max ? weights[i] : max;
    }

    for (i = 0; i < 12 && max > 0; i ++) {
        if (weights[i] > 0 && weights[i] >= share * max) {
            result |= BOX_AUDIO_PC(i);
        }
    }

    return result;
}

int32_t box_audio_pcs_count(
    box_audio_pcs_t set)
{
    int32_t i, result = 0;
    for (i = 0; i < 12; i ++) {
        result += (set >> i) & 1;
    }

    return result;
}

const char* box_audio_chord_quality(
    int32_t quality)
{
    return quality >= 0 && quality < BOX_AUDIO_QUALITY_COUNT ?
        box_audio_qualities[quality].suffix : NULL;
}

/** Find the chord whose notes are a set, or the set without the fifth of a seventh chord.
 *
 * @param set The pitch classes
 * @param bass Pitch class of the lowest note, -1 for none
 * @param chord Receives the root and the quality when one fits
 * @return Whether a chord fits
 */
static bool box_audio_chord_match(
    box_audio_pcs_t set,
    int32_t bass,
    box_audio_chord_t *chord)
{
    int32_t pass, q, i, r, score, best = -1;
    box_audio_pcs_t mask;
    for (pass = 0; pass < 2; pass ++) {
        for (q = 0; q < BOX_AUDIO_QUALITY_COUNT; q ++) {
            mask = box_audio_qualities[q].mask;
            if (pass && !box_audio_qualities[q].open_fifth) {
                continue;
            }

            mask = pass ? (box_audio_pcs_t)(mask & ~BOX_AUDIO_PC(7)) : mask;
            for (i = 0; i < 12; i ++) {
                r = bass >= 0 ? (bass + i) % 12 : i;
                if (box_audio_pcs_rotate(mask, r) != set) {
                    continue;
                }

                score = (r == bass ? 0 : 10000) + pass * 1000 + q * 12 + i;
                if (best < 0 || score < best) {
                    best = score;
                    chord->root = r;
                    chord->quality = q;
                }
            }
        }
    }

    return best >= 0;
}

box_audio_chord_t box_audio_chord_find(
    box_audio_pcs_t set,
    const float weights[12],
    int32_t bass)
{
    box_audio_chord_t result = { .root = -1, .quality = 0, .bass = -1,
        .set = (box_audio_pcs_t)(set & BOX_AUDIO_PCS_ALL) };
    int32_t i, weakest = -1;
    result.bass = bass >= 0 && ((result.set >> (bass % 12)) & 1) ? bass % 12 : -1;
    if (box_audio_chord_match(result.set, result.bass, &result)) {
        goto done;
    }

    if (!weights || box_audio_pcs_count(result.set) < 4) {
        goto done;
    }

    for (i = 0; i < 12; i ++) {
        if (((result.set >> i) & 1) && i != result.bass &&
            (weakest < 0 || weights[i] < weights[weakest]))
        {
            weakest = i;
        }
    }

    if (weakest >= 0) {
        box_audio_chord_match((box_audio_pcs_t)(result.set & ~BOX_AUDIO_PC(weakest)),
            result.bass, &result);
    }

done:
    return result;
}

void box_audio_chord_name(
    const box_audio_chord_t *chord,
    bool flats,
    char *out)
{
    size_t len = 0;
    int32_t i;
    out[0] = '\0';
    if (chord->root >= 0) {
        len = (size_t)snprintf(out, BOX_AUDIO_HARMONY_NAME, "%s%s",
            box_audio_pc_name(chord->root, flats), box_audio_chord_quality(chord->quality));
        if (chord->bass >= 0 && chord->bass != chord->root) {
            snprintf(out + len, BOX_AUDIO_HARMONY_NAME - len, "/%s",
                box_audio_pc_name(chord->bass, flats));
        }
    } else if (!chord->set) {
        snprintf(out, BOX_AUDIO_HARMONY_NAME, "N.C.");
    } else {
        out[len ++] = '[';
        for (i = 0; i < 12; i ++) {
            if ((chord->set >> i) & 1) {
                len += (size_t)snprintf(out + len, BOX_AUDIO_HARMONY_NAME - len, "%s%s",
                    len > 1 ? " " : "", box_audio_pc_name(i, flats));
            }
        }

        snprintf(out + len, BOX_AUDIO_HARMONY_NAME - len, "]");
    }
}

/** Correlate the weights of the pitch classes with a key profile rotated to a tonic.
 *
 * @param weights Weight of each pitch class
 * @param profile The profile, from its tonic
 * @param tonic Pitch class of the tonic
 * @return The Pearson correlation, 0 when the weights are all equal
 */
static float box_audio_key_correlate(
    const float weights[12],
    const float profile[12],
    int32_t tonic)
{
    double mw = 0, mp = 0, sxy = 0, sxx = 0, syy = 0, x, y;
    int32_t i;
    for (i = 0; i < 12; i ++) {
        mw += (double)weights[i] / 12.0;
        mp += (double)profile[i] / 12.0;
    }

    for (i = 0; i < 12; i ++) {
        x = (double)weights[i] - mw;
        y = (double)profile[box_audio_pc_wrap(i - tonic)] - mp;
        sxy += x * y;
        sxx += x * x;
        syy += y * y;
    }

    return sxx > 0 && syy > 0 ? (float)(sxy / sqrt(sxx * syy)) : 0.0f;
}

box_audio_key_t box_audio_key_find(
    const float weights[12],
    box_audio_key_t *second)
{
    box_audio_key_t result = { .tonic = -1 }, next = { .tonic = -1 }, k;
    double sum = 0;
    int32_t i;
    for (i = 0; i < 12; i ++) {
        sum += weights[i] > 0 ? (double)weights[i] : 0.0;
    }

    for (i = 0; i < 24 && sum > 0; i ++) {
        k.tonic = i % 12;
        k.minor = i >= 12;
        k.score = box_audio_key_correlate(weights,
            k.minor ? box_audio_minor_profile : box_audio_major_profile, k.tonic);
        if (result.tonic < 0 || k.score > result.score) {
            next = result;
            result = k;
        } else if (next.tonic < 0 || k.score > next.score) {
            next = k;
        }
    }

    if (second) {
        *second = next;
    }

    return result;
}

box_audio_pcs_t box_audio_key_scale(
    box_audio_key_t key)
{
    box_audio_pcs_t major = BOX_AUDIO_PC(0) | BOX_AUDIO_PC(2) | BOX_AUDIO_PC(4) |
        BOX_AUDIO_PC(5) | BOX_AUDIO_PC(7) | BOX_AUDIO_PC(9) | BOX_AUDIO_PC(11);
    box_audio_pcs_t minor = BOX_AUDIO_PC(0) | BOX_AUDIO_PC(2) | BOX_AUDIO_PC(3) |
        BOX_AUDIO_PC(5) | BOX_AUDIO_PC(7) | BOX_AUDIO_PC(8) | BOX_AUDIO_PC(10) |
        BOX_AUDIO_PC(11);

    return key.tonic < 0 ? 0 :
        box_audio_pcs_rotate(key.minor ? minor : major, box_audio_pc_wrap(key.tonic));
}

bool box_audio_key_flats(
    box_audio_key_t key)
{
    static const bool major_flats[12] = {
        false, true, false, true, false, true, false, false, true, false, true, false
    };
    static const bool minor_flats[12] = {
        true, false, true, true, false, true, false, true, false, false, true, false
    };
    int32_t t = box_audio_pc_wrap(key.tonic);

    return key.tonic >= 0 && (key.minor ? minor_flats[t] : major_flats[t]);
}

void box_audio_key_name(
    box_audio_key_t key,
    char *out)
{
    if (key.tonic < 0) {
        snprintf(out, BOX_AUDIO_HARMONY_NAME, "none");
    } else {
        snprintf(out, BOX_AUDIO_HARMONY_NAME, "%s %s",
            box_audio_pc_name(key.tonic, box_audio_key_flats(key)),
            key.minor ? "minor" : "major");
    }
}
