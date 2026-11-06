/**
 * @file Harmony.c
 * @brief Names of pitches, chords and keys from note data: chord templates on sets of pitch
 * classes, inversions as slash chords, sets that fit no chord and Krumhansl-Kessler keys.
 */

#include "util.h"
#include <harmony.h>

/** A set of pitch classes from up to four classes, -1 for none */
static box_audio_pcs_t harmony_set(
    int32_t a,
    int32_t b,
    int32_t c,
    int32_t d)
{
    box_audio_pcs_t result = 0;
    result |= a >= 0 ? (box_audio_pcs_t)(1u << a) : 0;
    result |= b >= 0 ? (box_audio_pcs_t)(1u << b) : 0;
    result |= c >= 0 ? (box_audio_pcs_t)(1u << c) : 0;
    result |= d >= 0 ? (box_audio_pcs_t)(1u << d) : 0;

    return result;
}

/** Check the name of the chord of a set over a bass, without weights */
static void harmony_check(
    box_audio_pcs_t set,
    int32_t bass,
    const char *expect)
{
    char name[BOX_AUDIO_HARMONY_NAME];
    box_audio_chord_t chord = box_audio_chord_find(set, NULL, bass);
    box_audio_chord_name(&chord, false, name);
    test_str(name, expect);
}

void Harmony_pitch_names(void) {
    char name[BOX_AUDIO_HARMONY_NAME];
    box_audio_pitch_name(60, false, name);
    test_str(name, "C4");
    box_audio_pitch_name(61, false, name);
    test_str(name, "C#4");
    box_audio_pitch_name(61, true, name);
    test_str(name, "Db4");
    box_audio_pitch_name(21, false, name);
    test_str(name, "A0");
    box_audio_pitch_name(0, false, name);
    test_str(name, "C-1");
    box_audio_pitch_name(69.25f, false, name);
    test_str(name, "A4+25c");
    box_audio_pitch_name(70.75f, false, name);
    test_str(name, "B4-25c");
}

void Harmony_triads(void) {
    harmony_check(harmony_set(0, 4, 7, -1), 0, "C");
    harmony_check(harmony_set(9, 0, 4, -1), 9, "Am");
    harmony_check(harmony_set(5, 9, 0, -1), 5, "F");
    harmony_check(harmony_set(11, 2, 5, -1), 11, "Bdim");
    harmony_check(harmony_set(0, 4, 8, -1), 0, "Caug");
    harmony_check(harmony_set(0, 2, 7, -1), 0, "Csus2");
    harmony_check(harmony_set(0, 5, 7, -1), 0, "Csus4");
    harmony_check(harmony_set(0, 7, -1, -1), 0, "C5");
    harmony_check(harmony_set(0, 4, 7, -1), -1, "C");
}

void Harmony_sevenths(void) {
    harmony_check(harmony_set(7, 11, 2, 5), 7, "G7");
    harmony_check(harmony_set(0, 4, 7, 11), 0, "Cmaj7");
    harmony_check(harmony_set(9, 0, 4, 7), 9, "Am7");
    harmony_check(harmony_set(11, 2, 5, 9), 11, "Bm7b5");
    harmony_check(harmony_set(0, 2, 4, 7), 0, "Cadd9");
    harmony_check(harmony_set(0, 3, 6, 9), 0, "Cdim7");
}

void Harmony_seventh_without_fifth(void) {
    harmony_check(harmony_set(7, 11, 5, -1), 7, "G7");
}

void Harmony_slash_chord(void) {
    harmony_check(harmony_set(0, 4, 7, -1), 4, "C/E");
    harmony_check(harmony_set(0, 4, 7, -1), 7, "C/G");
    harmony_check(harmony_set(7, 11, 2, 5), 11, "G7/B");
}

void Harmony_no_chord(void) {
    harmony_check(harmony_set(0, 1, 2, -1), 0, "[C C# D]");
    harmony_check(harmony_set(4, -1, -1, -1), 4, "[E]");
    harmony_check(0, -1, "N.C.");
}

void Harmony_weakest_left_out(void) {
    float weights[12] = { [0] = 4, [4] = 4, [6] = 1, [7] = 4 };
    char name[BOX_AUDIO_HARMONY_NAME];
    box_audio_chord_t chord = box_audio_chord_find(harmony_set(0, 4, 6, 7), weights, 0);
    box_audio_chord_name(&chord, false, name);
    test_str(name, "C");
    test_int(chord.set, harmony_set(0, 4, 6, 7));
}

void Harmony_set_from_weights(void) {
    float weights[12] = { [0] = 10, [4] = 5, [7] = 2, [2] = 1.5f };
    float silent[12] = {0};
    test_int(box_audio_pcs_from_weights(weights, BOX_AUDIO_HARMONY_SHARE),
        harmony_set(0, 4, 7, -1));
    test_int(box_audio_pcs_from_weights(silent, BOX_AUDIO_HARMONY_SHARE), 0);
    test_int(box_audio_pcs_count(harmony_set(0, 4, 7, 11)), 4);
}

void Harmony_key_major(void) {
    float weights[12] = { 4, 0, 2, 0, 3, 2, 0, 3, 0, 2, 0, 1 };
    char name[BOX_AUDIO_HARMONY_NAME];
    box_audio_key_t second;
    box_audio_key_t key = box_audio_key_find(weights, &second);
    box_audio_key_name(key, name);
    test_str(name, "C major");
    box_audio_key_name(second, name);
    test_str(name, "A minor");
    test_assert(key.score > second.score);
}

void Harmony_key_minor(void) {
    float weights[12] = { 3, 0, 2, 0, 4, 1, 0, 1, 1, 5, 0, 1 };
    char name[BOX_AUDIO_HARMONY_NAME];
    box_audio_key_t key = box_audio_key_find(weights, NULL);
    box_audio_key_name(key, name);
    test_str(name, "A minor");
}

void Harmony_key_none(void) {
    float weights[12] = {0};
    char name[BOX_AUDIO_HARMONY_NAME];
    box_audio_key_t key = box_audio_key_find(weights, NULL);
    test_int(key.tonic, -1);
    box_audio_key_name(key, name);
    test_str(name, "none");
    test_int(box_audio_key_scale(key), 0);
}

void Harmony_key_scale(void) {
    char name[BOX_AUDIO_HARMONY_NAME];
    box_audio_key_t c = { .tonic = 0 }, a = { .tonic = 9, .minor = true };
    box_audio_key_t bb = { .tonic = 10 }, fs = { .tonic = 6, .minor = true };
    box_audio_key_t d = { .tonic = 2, .minor = true }, g = { .tonic = 7 };
    test_int(box_audio_key_scale(c), 0xAB5);
    test_int(box_audio_key_scale(a), 0xBB5);
    test_bool(box_audio_key_flats(bb), true);
    test_bool(box_audio_key_flats(d), true);
    test_bool(box_audio_key_flats(g), false);
    test_bool(box_audio_key_flats(fs), false);
    box_audio_key_name(bb, name);
    test_str(name, "Bb major");
    box_audio_key_name(fs, name);
    test_str(name, "F# minor");
}

void Harmony_deterministic(void) {
    float weights[12] = { 1.5f, 0.25f, 3, 0, 2, 0.75f, 0, 4, 0, 1, 0.5f, 2 };
    box_audio_key_t a = box_audio_key_find(weights, NULL);
    box_audio_key_t b = box_audio_key_find(weights, NULL);
    test_int(a.tonic, b.tonic);
    test_bool(a.minor, b.minor);
    test_assert(a.score == b.score);
}
