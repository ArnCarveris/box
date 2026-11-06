/**
 * @file command/cmd_audio_analyze.c
 * @brief The audio analyze action: a text report of a WAV, a resource of the session or a
 * file, with loudness, peaks, bands, stereo, onsets, tail, silence, clicks, discontinuities
 * and pitch, and a table per bar for a capture of a song.
 *
 * The measures are those of audio/analyze.c; this file loads the WAV, cuts the span and
 * prints them. The same WAV and span give the same text.
 */

#include "command.h"
#include "../audio/analyze.h"
#include "../render/preprocess.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Largest WAV file read, in bytes */
#define BOX_CMD_ANALYZE_FILE_MAX (512 * 1024 * 1024)

/** Bars listed at most */
#define BOX_CMD_ANALYZE_BARS (64)

/** Lowest band level compared between a bar and the song, in dB against the total: a change
 * of a band below it does not matter to the mix */
#define BOX_CMD_ANALYZE_BAND_FLOOR (-40.0f)

/** Round a value to a number of decimals so that it never prints as -0.
 *
 * @param value The value
 * @param decimals Decimals printed
 * @return The value, 0 when it prints as 0
 */
static double box_cmd_analyze_round(
    double value,
    int decimals)
{
    double limit = 0.5 * pow(10.0, -decimals);

    return fabs(value) < limit ? 0.0 : value;
}

/** Append a level, -inf at the floor of the analysis.
 *
 * @param buf The output
 * @param value The level
 * @param decimals Decimals printed
 * @param unit The unit, such as "dBFS"
 */
static void box_cmd_analyze_level(
    ecs_strbuf_t *buf,
    float value,
    int decimals,
    const char *unit)
{
    if (value <= BOX_AUDIO_ANALYZE_FLOOR + 0.001f) {
        ecs_strbuf_append(buf, "-inf %s", unit);
    } else {
        ecs_strbuf_append(buf, "%.*f %s", decimals, box_cmd_analyze_round(value, decimals),
            unit);
    }
}

/** Parse seconds of the span.
 *
 * @param text The text, NULL or empty to keep the default
 * @param out Receives the seconds
 * @return Whether the text is empty or a number of seconds of at least 0
 */
static bool box_cmd_analyze_seconds(
    const char *text,
    double *out)
{
    char *end = NULL;
    double v;
    bool result = true;
    if (text && text[0]) {
        v = strtod(text, &end);
        result = end && !end[0] && v >= 0 && isfinite(v);
        *out = result ? v : *out;
    }

    return result;
}

/** Read a file into memory.
 *
 * @param path The file
 * @param size Receives its size
 * @return The bytes, which the caller frees, NULL when the file cannot be opened
 */
static uint8_t* box_cmd_analyze_read_file(
    const char *path,
    int64_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *result = NULL;
    long len = -1;
    *size = -1;
    if (!f) {
        goto done;
    }

    if (!fseek(f, 0, SEEK_END)) {
        len = ftell(f);
    }

    if (len < 0 || len > BOX_CMD_ANALYZE_FILE_MAX || fseek(f, 0, SEEK_SET)) {
        *size = len > BOX_CMD_ANALYZE_FILE_MAX ? len : -1;
        goto close;
    }

    result = ecs_os_malloc((ecs_size_t)(len ? len : 1));
    if (result && len && fread(result, 1, (size_t)len, f) != (size_t)len) {
        ecs_os_free(result);
        result = NULL;
        goto close;
    }

    *size = len;
close:
    fclose(f);
done:
    return result;
}

/** Find a resource of the session of the command.
 *
 * @param world The world
 * @param name The name of the resource
 * @return Whether the session has a resource of that name
 */
static bool box_cmd_analyze_is_resource(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t session = ecs_shell_get_session(world);
    const EcsCommandSession *s = session && ecs_is_alive(world, session) ?
        ecs_get(world, session, EcsCommandSession) : NULL;
    const ecs_shell_resource_t *r = s ? ecs_vec_first(&s->resources) : NULL;
    int32_t i, count = s ? ecs_vec_count(&s->resources) : 0;
    bool result = false;
    for (i = 0; i < count && !result; i ++) {
        result = r[i].name && !ecs_os_strcmp(r[i].name, name);
    }

    return result;
}

uint8_t* box_cmd_audio_wav_load(
    ecs_world_t *world,
    const char *wav,
    int64_t *size)
{
    uint8_t *result = NULL;
    char *found = NULL;
    if (box_cmd_analyze_is_resource(world, wav)) {
        ecs_size_t len = 0;
        result = ecs_shell_resource_read(world, ecs_shell_get_session(world), wav, &len,
            NULL);
        *size = len;
        if (!result || len <= 0) {
            ecs_err("resource '%s' is empty: a capture writes it when it finishes recording",
                wav);
            ecs_os_free(result);
            result = NULL;
        }

        goto done;
    }

    result = box_cmd_analyze_read_file(wav, size);
    if (!result && *size < 0) {
        found = box_shader_locate(wav);
        result = found ? box_cmd_analyze_read_file(found, size) : NULL;
    }

    if (!result && *size > BOX_CMD_ANALYZE_FILE_MAX) {
        ecs_err("cannot analyze '%s': larger than %d MB", wav,
            BOX_CMD_ANALYZE_FILE_MAX / (1024 * 1024));
    } else if (!result) {
        ecs_err("cannot find '%s': no resource of the session and no file", wav);
    }

done:
    ecs_os_free(found);

    return result;
}

int box_cmd_audio_wav_load(
    ecs_world_t *world,
    const char *wav,
    const char *action,
    box_audio_wav_t *out)
{
    int64_t size = 0;
    uint8_t *bytes = box_cmd_analyze_load(world, wav, &size);
    const char *error = NULL;
    int result = -1;
    memset(out, 0, sizeof(*out));
    if (!bytes) {
        goto done;
    }

    error = box_audio_wav_read(bytes, (size_t)size, out);
    if (error) {
        ecs_err("cannot %s '%s': %s", action, wav, error);
        goto done;
    }

    result = 0;
done:
    ecs_os_free(bytes);

    return result;
}

/** Append the loudness and the peaks.
 *
 * @param buf The output
 * @param a The analysis
 */
static void box_cmd_analyze_loudness(
    ecs_strbuf_t *buf,
    const box_audio_analysis_t *a)
{
    ecs_strbuf_appendstr(buf, "\nloudness: integrated ");
    box_cmd_analyze_level(buf, a->integrated, 2, "LUFS");
    ecs_strbuf_append(buf, ", range %.1f LU, short-term max ",
        box_cmd_analyze_round(a->range, 1));
    box_cmd_analyze_level(buf, a->short_term_max, 1, "LUFS");
    ecs_strbuf_appendstr(buf, ", momentary max ");
    box_cmd_analyze_level(buf, a->momentary_max, 1, "LUFS");
    ecs_strbuf_appendstr(buf, "\npeak: sample ");
    box_cmd_analyze_level(buf, a->sample_peak, 2, "dBFS");
    ecs_strbuf_appendstr(buf, ", true ");
    box_cmd_analyze_level(buf, a->true_peak, 2, "dBFS");
    ecs_strbuf_appendstr(buf, ", rms ");
    box_cmd_analyze_level(buf, a->rms, 2, "dBFS");
    ecs_strbuf_append(buf, ", crest %.2f dB, clipped %d samples",
        box_cmd_analyze_round(a->crest, 2), a->clipped);
    if (a->channels == 2) {
        ecs_strbuf_append(buf, "\ndc offset: L %+.5f FS, R %+.5f FS",
            box_cmd_analyze_round(a->dc[0], 5), box_cmd_analyze_round(a->dc[1], 5));
    } else {
        ecs_strbuf_append(buf, "\ndc offset: %+.5f FS", box_cmd_analyze_round(a->dc[0], 5));
    }
}

/** Append the bands, the spectral centroid and rolloff and the stereo measures.
 *
 * @param buf The output
 * @param a The analysis
 */
static void box_cmd_analyze_spectrum(
    ecs_strbuf_t *buf,
    const box_audio_analysis_t *a)
{
    int32_t b;
    ecs_strbuf_appendstr(buf, "\nbands:");
    for (b = 0; b < BOX_AUDIO_ANALYZE_BANDS; b ++) {
        ecs_strbuf_append(buf, "%s %s ", b ? "," : "", box_audio_analyze_band_name(b));
        box_cmd_analyze_level(buf, a->bands[b], 1, "dB");
    }

    ecs_strbuf_append(buf, "\nspectrum: centroid %.0f Hz, rolloff %.0f Hz",
        (double)a->centroid, (double)a->rolloff);
    ecs_strbuf_append(buf, "\nstereo: %scorrelation %+.2f, side/mid ",
        a->channels == 1 ? "mono file, " : "", box_cmd_analyze_round(a->correlation, 2));
    box_cmd_analyze_level(buf, a->side_mid, 1, "dB");
    ecs_strbuf_append(buf, ", mono loss %.1f dB", box_cmd_analyze_round(a->mono_loss, 1));
}

/** Append the onsets, attack, tail, silence, clicks and discontinuities.
 *
 * @param buf The output
 * @param a The analysis
 */
static void box_cmd_analyze_time(
    ecs_strbuf_t *buf,
    const box_audio_analysis_t *a)
{
    float click = powf(10.0f, BOX_AUDIO_ANALYZE_CLICK_DB / 20.0f);
    int32_t i, listed = a->onset_count < BOX_AUDIO_ANALYZE_ONSETS ?
        a->onset_count : BOX_AUDIO_ANALYZE_ONSETS;
    ecs_strbuf_append(buf, "\nonsets: %d", a->onset_count);
    for (i = 0; i < listed; i ++) {
        ecs_strbuf_append(buf, "%s %.3f s +%.1f dB", i ? "," : ":",
            (double)a->onsets[i].time, box_cmd_analyze_round(a->onsets[i].strength, 1));
    }

    if (a->onset_count > listed) {
        ecs_strbuf_append(buf, ", %d more", a->onset_count - listed);
    }

    if (a->attack >= 0) {
        ecs_strbuf_append(buf, "\nattack: %.1f ms", (double)a->attack * 1000.0);
    } else {
        ecs_strbuf_appendstr(buf, "\nattack: none");
    }

    if (a->tail < 0) {
        ecs_strbuf_appendstr(buf, "\ntail: none");
    } else if (a->tail_cut) {
        ecs_strbuf_append(buf, "\ntail: cut short, only -%.1f dB at the end, %.3f s after "
            "the onset at %.3f s", box_cmd_analyze_round(a->tail_drop, 1), (double)a->tail,
            (double)a->tail_onset);
    } else {
        ecs_strbuf_append(buf, "\ntail: %.3f s to -60 dB after the onset at %.3f s",
            (double)a->tail, (double)a->tail_onset);
    }

    ecs_strbuf_append(buf, "\nsilence: leading %.3f s, trailing %.3f s",
        (double)a->leading, (double)a->trailing);
    ecs_strbuf_appendstr(buf, "\nclicks: start ");
    if (a->first >= click) {
        ecs_strbuf_append(buf, "%.3f (%.1f dBFS)", (double)a->first,
            box_cmd_analyze_round(20.0 * log10(a->first), 1));
    } else {
        ecs_strbuf_appendstr(buf, "none");
    }

    ecs_strbuf_appendstr(buf, ", end ");
    if (a->last >= click) {
        ecs_strbuf_append(buf, "%.3f (%.1f dBFS)", (double)a->last,
            box_cmd_analyze_round(20.0 * log10(a->last), 1));
    } else {
        ecs_strbuf_appendstr(buf, "none");
    }

    listed = a->jump_count < BOX_AUDIO_ANALYZE_JUMPS ? a->jump_count : BOX_AUDIO_ANALYZE_JUMPS;
    ecs_strbuf_append(buf, "\ndiscontinuities: %d", a->jump_count);
    for (i = 0; i < listed; i ++) {
        ecs_strbuf_append(buf, "%s %.4f s step %.1f dBFS", i ? "," : ":",
            (double)a->jumps[i].time, box_cmd_analyze_round(a->jumps[i].size, 1));
    }

    if (a->jump_count > listed) {
        ecs_strbuf_append(buf, ", %d more", a->jump_count - listed);
    }
}

/** Append the pitch.
 *
 * @param buf The output
 * @param a The analysis
 */
static void box_cmd_analyze_pitch(
    ecs_strbuf_t *buf,
    const box_audio_analysis_t *a)
{
    char note[8];
    float cents;
    if (!a->pitched) {
        ecs_strbuf_append(buf, "\npitch: no stable pitch (voiced %.0f%%)",
            (double)a->voiced * 100.0);
    } else {
        cents = box_audio_note_name(a->pitch, note);
        ecs_strbuf_append(buf, "\npitch: %.1f Hz %s %+.0f cents, spread %.1f cents, "
            "hnr %.1f dB, voiced %.0f%%", (double)a->pitch, note,
            box_cmd_analyze_round(cents, 0), box_cmd_analyze_round(a->pitch_spread, 1),
            box_cmd_analyze_round(a->hnr, 1), (double)a->voiced * 100.0);
    }
}

/** Append a bar: its loudness and the band that changed the most against the song.
 *
 * @param buf The output
 * @param bar Number of the bar, from 1
 * @param partial Whether the span holds only a part of the bar
 * @param seg The measures of the bar
 * @param a The analysis of the span
 */
static void box_cmd_analyze_bar(
    ecs_strbuf_t *buf,
    int64_t bar,
    bool partial,
    const box_audio_analyze_segment_t *seg,
    const box_audio_analysis_t *a)
{
    int32_t b, best = -1;
    float change = 0;
    ecs_strbuf_append(buf, "\nbar %lld%s: ", (long long)bar, partial ? " (partial)" : "");
    box_cmd_analyze_level(buf, seg->loudness, 1, "LUFS");
    for (b = 0; b < BOX_AUDIO_ANALYZE_BANDS; b ++) {
        float x = seg->bands[b] > BOX_CMD_ANALYZE_BAND_FLOOR ?
            seg->bands[b] : BOX_CMD_ANALYZE_BAND_FLOOR;
        float y = a->bands[b] > BOX_CMD_ANALYZE_BAND_FLOOR ?
            a->bands[b] : BOX_CMD_ANALYZE_BAND_FLOOR;
        if (best < 0 || fabsf(x - y) > fabsf(change)) {
            best = b;
            change = x - y;
        }
    }

    if (seg->loudness <= BOX_AUDIO_ANALYZE_FLOOR + 0.001f) {
        ecs_strbuf_appendstr(buf, ", silent");
    } else {
        ecs_strbuf_append(buf, ", %s %+.1f dB", box_audio_analyze_band_name(best),
            box_cmd_analyze_round(change, 1));
    }
}

/** Append the table per bar of a capture of a song: the bars the span covers from the beat
 * and tempo the capture started at.
 *
 * @param buf The output
 * @param wav The WAV with its song chunk
 * @param start First frame of the span
 * @param end Frame after the span
 * @param a The analysis of the span
 */
static void box_cmd_analyze_bars(
    ecs_strbuf_t *buf,
    const box_audio_wav_t *wav,
    int32_t start,
    int32_t end,
    const box_audio_analysis_t *a)
{
    box_audio_analyze_segment_t segs[BOX_CMD_ANALYZE_BARS];
    const box_audio_wav_song_t *song = &wav->song;
    double beats_per_frame = song->tempo / 60.0 / wav->sample_rate;
    double first_beat = song->start_beat + start * beats_per_frame;
    double last_beat = song->start_beat + end * beats_per_frame;
    int64_t first = (int64_t)floor(first_beat / song->bar_beats);
    int64_t last = (int64_t)ceil(last_beat / song->bar_beats) - 1, bar;
    int32_t count = 0, i, frames = end - start;
    bool partial[BOX_CMD_ANALYZE_BARS];
    ecs_strbuf_append(buf, "\nbars: %.1f bpm, %.2f beats per bar, span from beat %.2f",
        (double)song->tempo, (double)song->bar_beats, box_cmd_analyze_round(first_beat, 2));
    for (bar = first; bar <= last && count < BOX_CMD_ANALYZE_BARS; bar ++) {
        double s = (bar * song->bar_beats - song->start_beat) / beats_per_frame - start;
        double e = ((bar + 1) * song->bar_beats - song->start_beat) / beats_per_frame - start;
        int32_t fs = (int32_t)floor(s + 0.5), fe = (int32_t)floor(e + 0.5);
        partial[count] = fs < 0 || fe > frames;
        segs[count].start = fs < 0 ? 0 : fs;
        segs[count].end = fe > frames ? frames : fe;
        if (segs[count].end > segs[count].start) {
            count ++;
        }
    }

    if (box_audio_analyze_segments(wav->data + (int64_t)start * wav->channels, frames,
        wav->channels, wav->sample_rate, segs, count))
    {
        ecs_strbuf_appendstr(buf, "\nbars: out of memory");
        goto done;
    }

    for (i = 0; i < count; i ++) {
        double s = (start + segs[i].start) * beats_per_frame + song->start_beat;
        bar = (int64_t)floor(s / song->bar_beats + 1e-6) + 1;
        box_cmd_analyze_bar(buf, bar, partial[i], &segs[i], a);
    }

    if (last - first + 1 > count) {
        ecs_strbuf_append(buf, "\n%lld more bars", (long long)(last - first + 1 - count));
    }

done:
    return;
}

char* box_cmd_audio_analyze(
    ecs_world_t *world,
    const char *wav,
    const char *from,
    const char *to)
{
    box_audio_wav_t file = {0};
    box_audio_analysis_t a;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    uint8_t *bytes = NULL;
    int64_t size = 0;
    const char *error;
    double from_s = 0, to_s = -1, length;
    int32_t start, end;
    char *result = NULL;
    if (!wav || !wav[0]) {
        ecs_err("audio analyze needs a WAV: a resource of the session or a file");
        goto done;
    }

    if (!box_cmd_analyze_seconds(from, &from_s) || !box_cmd_analyze_seconds(to, &to_s)) {
        ecs_err("invalid span '%s' to '%s', expected seconds", from ? from : "",
            to ? to : "");
        goto done;
    }

    bytes = box_cmd_audio_wav_load(world, wav, &size);
    if (!bytes) {
        goto done;
    }

    error = box_audio_wav_read(bytes, (size_t)size, &file);
    if (error) {
        ecs_err("cannot analyze '%s': %s", wav, error);
        goto done;
    }

    length = file.frames / (double)file.sample_rate;
    to_s = to_s < 0 || to_s > length ? length : to_s;
    start = (int32_t)floor(from_s * file.sample_rate + 0.5);
    end = (int32_t)floor(to_s * file.sample_rate + 0.5);
    end = end < file.frames ? end : file.frames;
    if (end <= start) {
        ecs_err("empty span %.3f to %.3f s of '%s', which is %.3f s long", from_s, to_s, wav,
            length);
        goto done;
    }

    if (box_audio_analyze(file.data + (int64_t)start * file.channels, end - start,
        file.channels, file.sample_rate, &a))
    {
        ecs_err("cannot analyze '%s': out of memory", wav);
        goto done;
    }

    ecs_strbuf_append(&buf, "analyze %s: %.0f Hz, %s, %.3f s, span %.3f to %.3f s", wav,
        (double)file.sample_rate, file.channels == 1 ? "mono" : "stereo", length,
        start / (double)file.sample_rate, end / (double)file.sample_rate);
    box_cmd_analyze_loudness(&buf, &a);
    box_cmd_analyze_spectrum(&buf, &a);
    box_cmd_analyze_time(&buf, &a);
    box_cmd_analyze_pitch(&buf, &a);
    if (file.has_song) {
        box_cmd_analyze_bars(&buf, &file, start, end, &a);
    }

    result = ecs_strbuf_get(&buf);
done:
    ecs_os_free(bytes);
    box_audio_wav_fini(&file);

    return result;
}
