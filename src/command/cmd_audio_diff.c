/**
 * @file command/cmd_audio_diff.c
 * @brief The audio diff action: a text report of how two WAVs differ, resources of the
 * session or files: bit identity, length, loudness, true peak, bands, stereo, the residual of
 * a null test after aligning by cross-correlation, its loudest spans and, for a capture of a
 * song, the bars that differ most.
 *
 * The measures are those of audio/analyze.c and audio/diff.c; this file loads the WAVs, cuts
 * the span and prints them. WAVs of different sample rates are an error; a mono and a stereo
 * WAV are compared as mono sums. The same WAVs and span give the same text.
 */

#include "command.h"
#include "../audio/analyze.h"
#include "../audio/diff.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** Largest offset the alignment tries, in milliseconds */
#define BOX_CMD_DIFF_MAX_LAG_MS (20.0)

/** Length of a span of the residual, in seconds */
#define BOX_CMD_DIFF_SPAN_S (0.1)

/** Loudest spans of the residual listed at most */
#define BOX_CMD_DIFF_SPANS (5)

/** Spans of the residual within this many dB of the loudest one are listed, in dB */
#define BOX_CMD_DIFF_SPANS_DB (6.0)

/** Bars compared at most */
#define BOX_CMD_DIFF_BARS (256)

/** Bars that differ most listed at most */
#define BOX_CMD_DIFF_BARS_LISTED (4)

/** Lowest band level compared, in dB against the total: a change of a band below it does
 * not matter to the mix, a band below it in both is printed as quiet */
#define BOX_CMD_DIFF_BAND_FLOOR (-40.0f)

/** Smallest change of the balance of the bands that is reported, in dB */
#define BOX_CMD_DIFF_BALANCE_MIN (0.05f)

/** Round a value to a number of decimals so that it never prints as -0.
 *
 * @param value The value
 * @param decimals Decimals printed
 * @return The value, 0 when it prints as 0
 */
static double box_cmd_diff_round(
    double value,
    int decimals)
{
    double limit = 0.5 * pow(10.0, -decimals);

    return fabs(value) < limit ? 0.0 : value;
}

/** Whether a level of the analysis is at its floor.
 *
 * @param value The level
 * @return Whether it is silence
 */
static bool box_cmd_diff_silent(
    float value)
{
    return value <= BOX_AUDIO_ANALYZE_FLOOR + 0.001f;
}

/** Append a level, -inf at the floor of the analysis.
 *
 * @param buf The output
 * @param value The level
 * @param decimals Decimals printed
 * @param unit The unit, such as "dBFS"
 */
static void box_cmd_diff_level(
    ecs_strbuf_t *buf,
    float value,
    int decimals,
    const char *unit)
{
    if (box_cmd_diff_silent(value)) {
        ecs_strbuf_append(buf, "-inf %s", unit);
    } else {
        ecs_strbuf_append(buf, "%.*f %s", decimals, box_cmd_diff_round(value, decimals), unit);
    }
}

/** Append the change from one level to another in dB: -inf when only b is silent, +inf
 * when only a is, 0 when both are.
 *
 * @param buf The output
 * @param a The level of a
 * @param b The level of b
 */
static void box_cmd_diff_delta(
    ecs_strbuf_t *buf,
    float a,
    float b)
{
    bool sa = box_cmd_diff_silent(a), sb = box_cmd_diff_silent(b);
    if (sa && sb) {
        ecs_strbuf_appendstr(buf, "+0.0 dB");
    } else if (sa) {
        ecs_strbuf_appendstr(buf, "+inf dB");
    } else if (sb) {
        ecs_strbuf_appendstr(buf, "-inf dB");
    } else {
        ecs_strbuf_append(buf, "%+.1f dB", box_cmd_diff_round(b - a, 1));
    }
}

/** Append a ratio of powers in dB: -inf when the power is 0 or below the floor of the
 * analysis, +inf when only the reference is 0.
 *
 * @param buf The output
 * @param power The power
 * @param reference The power it is relative to
 * @param unit The unit, such as "dB against a"
 */
static void box_cmd_diff_ratio(
    ecs_strbuf_t *buf,
    double power,
    double reference,
    const char *unit)
{
    double db = power > 0 && reference > 0 ? 10.0 * log10(power / reference) : 0;
    if (!(power > 0) || (reference > 0 && db <= BOX_AUDIO_ANALYZE_FLOOR)) {
        ecs_strbuf_append(buf, "-inf %s", unit);
    } else if (!(reference > 0)) {
        ecs_strbuf_append(buf, "+inf %s", unit);
    } else {
        ecs_strbuf_append(buf, "%.1f %s", box_cmd_diff_round(db, 1), unit);
    }
}

/** Parse seconds of the span.
 *
 * @param text The text, NULL or empty to keep the default
 * @param out Receives the seconds
 * @return Whether the text is empty or a number of seconds of at least 0
 */
static bool box_cmd_diff_seconds(
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

/** Sum a stereo WAV to mono in place, the mean of left and right.
 *
 * @param wav The WAV
 */
static void box_cmd_diff_mono(
    box_audio_wav_t *wav)
{
    int64_t i;
    if (wav->channels == 2) {
        for (i = 0; i < wav->frames; i ++) {
            wav->data[i] = 0.5f * (wav->data[i * 2] + wav->data[i * 2 + 1]);
        }

        wav->channels = 1;
    }
}

/** Cut the span of seconds from..to out of a WAV.
 *
 * @param wav The WAV
 * @param from_s First second
 * @param to_s Last second, below 0 for the end
 * @param start Receives the first frame
 * @param end Receives the frame after the span
 * @return Whether the span holds a frame
 */
static bool box_cmd_diff_span(
    const box_audio_wav_t *wav,
    double from_s,
    double to_s,
    int32_t *start,
    int32_t *end)
{
    double length = wav->frames / (double)wav->sample_rate;
    to_s = to_s < 0 || to_s > length ? length : to_s;
    *start = (int32_t)floor(from_s * wav->sample_rate + 0.5);
    *end = (int32_t)floor(to_s * wav->sample_rate + 0.5);
    *end = *end < wav->frames ? *end : wav->frames;

    return *end > *start;
}

/** Append whether the spans are bit identical, or how many samples differ.
 *
 * @param buf The output
 * @param a The first WAV
 * @param a_data The span of a
 * @param a_frames Frames of the span of a
 * @param b_data The span of b
 * @param b_frames Frames of the span of b
 * @param channels_differ Whether one WAV is mono and the other stereo
 */
static void box_cmd_diff_identity(
    ecs_strbuf_t *buf,
    const box_audio_wav_t *a,
    const float *a_data,
    int32_t a_frames,
    const float *b_data,
    int32_t b_frames,
    bool channels_differ)
{
    int32_t frames = a_frames < b_frames ? a_frames : b_frames;
    int64_t first = -1, count = (int64_t)frames * a->channels;
    int64_t differ = channels_differ ? 0 : box_audio_diff_samples(a_data, b_data, count, &first);
    ecs_strbuf_appendstr(buf, "\nidentical: ");
    if (channels_differ) {
        ecs_strbuf_appendstr(buf, "no, one is mono and the other stereo");
    } else if (!differ && a_frames == b_frames) {
        ecs_strbuf_appendstr(buf, "yes, bit identical");
    } else {
        ecs_strbuf_append(buf, "no, %lld of %lld samples%s differ", (long long)differ,
            (long long)count, a_frames == b_frames ? "" : " of the common length");
        if (first >= 0) {
            ecs_strbuf_append(buf, ", first at %.4f s",
                (double)(first / a->channels) / a->sample_rate);
        }

        if (a_frames != b_frames) {
            ecs_strbuf_appendstr(buf, ", lengths differ");
        }
    }
}

/** Append the length, loudness, true peak, bands and stereo of both and their change.
 *
 * @param buf The output
 * @param wa The first WAV
 * @param wb The second WAV
 * @param a The analysis of the span of a
 * @param b The analysis of the span of b
 */
static void box_cmd_diff_levels(
    ecs_strbuf_t *buf,
    const box_audio_wav_t *wa,
    const box_audio_wav_t *wb,
    const box_audio_analysis_t *a,
    const box_audio_analysis_t *b)
{
    double la = wa->frames / (double)wa->sample_rate, lb = wb->frames / (double)wb->sample_rate;
    float change = 0;
    int32_t i, best = -1;
    ecs_strbuf_append(buf, "\nlength: a %.3f s, b %.3f s, b - a %+.3f s (%+d frames)", la, lb,
        box_cmd_diff_round(lb - la, 3), wb->frames - wa->frames);
    ecs_strbuf_appendstr(buf, "\nloudness: integrated ");
    box_cmd_diff_level(buf, a->integrated, 2, "LUFS");
    ecs_strbuf_appendstr(buf, " -> ");
    box_cmd_diff_level(buf, b->integrated, 2, "LUFS");
    ecs_strbuf_appendstr(buf, " (");
    box_cmd_diff_delta(buf, a->integrated, b->integrated);
    ecs_strbuf_appendstr(buf, "), true peak ");
    box_cmd_diff_level(buf, a->true_peak, 2, "dBFS");
    ecs_strbuf_appendstr(buf, " -> ");
    box_cmd_diff_level(buf, b->true_peak, 2, "dBFS");
    ecs_strbuf_appendstr(buf, " (");
    box_cmd_diff_delta(buf, a->true_peak, b->true_peak);
    ecs_strbuf_appendstr(buf, ")\nbands (b - a):");
    for (i = 0; i < BOX_AUDIO_ANALYZE_BANDS; i ++) {
        float ea = box_cmd_diff_silent(a->bands[i]) || box_cmd_diff_silent(a->rms) ?
            BOX_AUDIO_ANALYZE_FLOOR : a->bands[i] + a->rms;
        float eb = box_cmd_diff_silent(b->bands[i]) || box_cmd_diff_silent(b->rms) ?
            BOX_AUDIO_ANALYZE_FLOOR : b->bands[i] + b->rms;
        float x = a->bands[i] > BOX_CMD_DIFF_BAND_FLOOR ? a->bands[i] : BOX_CMD_DIFF_BAND_FLOOR;
        float y = b->bands[i] > BOX_CMD_DIFF_BAND_FLOOR ? b->bands[i] : BOX_CMD_DIFF_BAND_FLOOR;
        ecs_strbuf_append(buf, "%s %s ", i ? "," : "", box_audio_analyze_band_name(i));
        if (a->bands[i] < BOX_CMD_DIFF_BAND_FLOOR && b->bands[i] < BOX_CMD_DIFF_BAND_FLOOR) {
            ecs_strbuf_appendstr(buf, "quiet");
        } else {
            box_cmd_diff_delta(buf, ea, eb);
        }

        if (best < 0 || fabsf(y - x) > fabsf(change)) {
            best = i;
            change = y - x;
        }
    }

    if (fabsf(change) < BOX_CMD_DIFF_BALANCE_MIN) {
        ecs_strbuf_appendstr(buf, "\nbalance: unchanged, every band moved with the total");
    } else {
        ecs_strbuf_append(buf, "\nbalance: %s changed most, %+.1f dB against the total",
            box_audio_analyze_band_name(best), box_cmd_diff_round(change, 1));
    }

    if (a->channels == 1) {
        ecs_strbuf_appendstr(buf, "\nstereo: mono, not compared");
    } else {
        ecs_strbuf_append(buf, "\nstereo: correlation %+.2f -> %+.2f (%+.2f), width side/mid ",
            box_cmd_diff_round(a->correlation, 2), box_cmd_diff_round(b->correlation, 2),
            box_cmd_diff_round(b->correlation - a->correlation, 2));
        box_cmd_diff_level(buf, a->side_mid, 1, "dB");
        ecs_strbuf_appendstr(buf, " -> ");
        box_cmd_diff_level(buf, b->side_mid, 1, "dB");
        ecs_strbuf_appendstr(buf, " (");
        box_cmd_diff_delta(buf, a->side_mid, b->side_mid);
        ecs_strbuf_appendstr(buf, ")");
    }
}

/** Get the residual power per frame of a span, 0 when it compares no frame.
 *
 * @param seg The residual of the span
 * @return The power per frame
 */
static double box_cmd_diff_density(
    const box_audio_diff_segment_t *seg)
{
    return seg->frames > 0 ? seg->power / seg->frames : 0;
}

/** Find the run of spans of the residual from a span that are within BOX_CMD_DIFF_SPANS_DB
 * of the loudest one.
 *
 * @param segs The residual per span
 * @param count Number of spans
 * @param threshold Lowest power per frame of a span of a run
 * @param i The first span of the run
 * @param peak Receives the highest power per frame of the run
 * @return The span after the run
 */
static int32_t box_cmd_diff_run(
    const box_audio_diff_segment_t *segs,
    int32_t count,
    double threshold,
    int32_t i,
    double *peak)
{
    *peak = 0;
    for (; i < count && box_cmd_diff_density(&segs[i]) >= threshold; i ++) {
        double d = box_cmd_diff_density(&segs[i]);
        *peak = d > *peak ? d : *peak;
    }

    return i;
}

/** Append the loudest spans of the residual: the runs of spans of BOX_CMD_DIFF_SPAN_S within
 * BOX_CMD_DIFF_SPANS_DB of the loudest one, at most BOX_CMD_DIFF_SPANS of them with the
 * loudest peaks, in the order of time.
 *
 * @param buf The output
 * @param segs The residual per span
 * @param count Number of spans
 * @param channels Channels compared
 * @param rate Frames per second
 */
static void box_cmd_diff_spans(
    ecs_strbuf_t *buf,
    const box_audio_diff_segment_t *segs,
    int32_t count,
    int32_t channels,
    float rate)
{
    int32_t picked[BOX_CMD_DIFF_SPANS], n = 0, runs = 0, i, j, k, m;
    double loudest = 0, threshold, peak, best_peak;
    for (i = 0; i < count; i ++) {
        double d = box_cmd_diff_density(&segs[i]);
        loudest = d > loudest ? d : loudest;
    }

    threshold = loudest * pow(10.0, -BOX_CMD_DIFF_SPANS_DB / 10.0);
    for (k = 0; loudest > 0 && k < BOX_CMD_DIFF_SPANS; k ++) {
        int32_t best = -1;
        best_peak = 0;
        for (i = 0; i < count; i = j > i ? j : i + 1) {
            bool taken = false;
            j = box_cmd_diff_run(segs, count, threshold, i, &peak);
            for (m = 0; m < n; m ++) {
                taken = taken || picked[m] == i;
            }

            if (j > i && !taken && peak > best_peak) {
                best = i;
                best_peak = peak;
            }
        }

        if (best >= 0) {
            picked[n ++] = best;
        }
    }

    for (i = 0; loudest > 0 && i < count; i = j > i ? j : i + 1) {
        j = box_cmd_diff_run(segs, count, threshold, i, &peak);
        runs += j > i;
    }

    for (i = 1; i < n; i ++) {
        for (j = i; j > 0 && picked[j - 1] > picked[j]; j --) {
            k = picked[j];
            picked[j] = picked[j - 1];
            picked[j - 1] = k;
        }
    }

    ecs_strbuf_appendstr(buf, "\nresidual spans:");
    if (!n) {
        ecs_strbuf_appendstr(buf, " none");
    }

    for (k = 0; k < n; k ++) {
        double power = 0, reference = 0, frames = 0;
        j = box_cmd_diff_run(segs, count, threshold, picked[k], &peak);
        for (i = picked[k]; i < j; i ++) {
            power += segs[i].power;
            reference += segs[i].reference;
            frames += segs[i].frames;
        }

        ecs_strbuf_append(buf, "%s %.3f to %.3f s ", k ? "," : "",
            segs[picked[k]].start / (double)rate, segs[j - 1].end / (double)rate);
        box_cmd_diff_ratio(buf, power, frames * channels, "dBFS");
        ecs_strbuf_appendstr(buf, " (");
        box_cmd_diff_ratio(buf, power, reference, "dB against a");
        ecs_strbuf_appendstr(buf, ")");
    }

    if (runs > n) {
        ecs_strbuf_append(buf, ", %d more spans", runs - n);
    }
}

/** Append the null test: the offset of b, the residual against a and its loudest spans.
 *
 * @param buf The output
 * @param a The span of a
 * @param a_frames Frames of the span of a
 * @param b The span of b
 * @param b_frames Frames of the span of b
 * @param channels Channels of both
 * @param rate Frames per second
 * @param offset Receives the offset of b
 * @return 0 on success, -1 when out of memory
 */
static int box_cmd_diff_null(
    ecs_strbuf_t *buf,
    const float *a,
    int32_t a_frames,
    const float *b,
    int32_t b_frames,
    int32_t channels,
    float rate,
    int32_t *offset)
{
    int32_t max_lag = (int32_t)(rate * BOX_CMD_DIFF_MAX_LAG_MS / 1000.0 + 0.5);
    int32_t hop = (int32_t)(rate * BOX_CMD_DIFF_SPAN_S + 0.5), hops, i;
    box_audio_diff_segment_t total = { .start = 0, .end = a_frames }, *segs = NULL;
    int result = -1;
    hop = hop > 0 ? hop : 1;
    hops = (a_frames + hop - 1) / hop;
    segs = ecs_os_malloc_n(box_audio_diff_segment_t, hops);
    if (!segs) {
        goto done;
    }

    *offset = box_audio_diff_align(a, a_frames, b, b_frames, channels, max_lag);
    box_audio_diff_residual(a, a_frames, b, b_frames, channels, *offset, &total, 1);
    ecs_strbuf_append(buf, "\nnull test: offset %+d frames (%+.3f ms), residual ", *offset,
        box_cmd_diff_round(*offset * 1000.0 / rate, 3));
    box_cmd_diff_ratio(buf, total.power, total.reference, "dB against a");
    ecs_strbuf_appendstr(buf, ", ");
    box_cmd_diff_ratio(buf, total.power, (double)total.frames * channels, "dBFS");
    ecs_strbuf_append(buf, " over %.3f s", total.frames / (double)rate);
    for (i = 0; i < hops; i ++) {
        segs[i].start = i * hop;
        segs[i].end = (i + 1) * hop < a_frames ? (i + 1) * hop : a_frames;
    }

    box_audio_diff_residual(a, a_frames, b, b_frames, channels, *offset, segs, hops);
    box_cmd_diff_spans(buf, segs, hops, channels, rate);
    result = 0;
done:
    ecs_os_free(segs);

    return result;
}

/** Append a bar that differs: its residual, the change of its loudness and of the band that
 * changed most against the total.
 *
 * @param buf The output
 * @param bar Number of the bar, from 1
 * @param partial Whether the span holds only a part of the bar
 * @param d The residual of the bar
 * @param sa The measures of the bar in a
 * @param sb The measures of the bar in b
 */
static void box_cmd_diff_bar(
    ecs_strbuf_t *buf,
    int64_t bar,
    bool partial,
    const box_audio_diff_segment_t *d,
    const box_audio_analyze_segment_t *sa,
    const box_audio_analyze_segment_t *sb)
{
    int32_t i, best = -1;
    float change = 0;
    ecs_strbuf_append(buf, "\nbar %lld%s: residual ", (long long)bar,
        partial ? " (partial)" : "");
    box_cmd_diff_ratio(buf, d->power, d->reference, "dB against a");
    ecs_strbuf_appendstr(buf, ", loudness ");
    box_cmd_diff_level(buf, sa->loudness, 1, "LUFS");
    ecs_strbuf_appendstr(buf, " -> ");
    box_cmd_diff_level(buf, sb->loudness, 1, "LUFS");
    ecs_strbuf_appendstr(buf, " (");
    box_cmd_diff_delta(buf, sa->loudness, sb->loudness);
    ecs_strbuf_appendstr(buf, ")");
    for (i = 0; i < BOX_AUDIO_ANALYZE_BANDS; i ++) {
        float x = sa->bands[i] > BOX_CMD_DIFF_BAND_FLOOR ? sa->bands[i] : BOX_CMD_DIFF_BAND_FLOOR;
        float y = sb->bands[i] > BOX_CMD_DIFF_BAND_FLOOR ? sb->bands[i] : BOX_CMD_DIFF_BAND_FLOOR;
        if (best < 0 || fabsf(y - x) > fabsf(change)) {
            best = i;
            change = y - x;
        }
    }

    if (!box_cmd_diff_silent(sa->loudness) && !box_cmd_diff_silent(sb->loudness)) {
        ecs_strbuf_append(buf, ", %s %+.1f dB", box_audio_analyze_band_name(best),
            box_cmd_diff_round(change, 1));
    }
}

/** The bars of the span of a and their measures in a and b */
typedef struct box_cmd_diff_bars_t {
    box_audio_analyze_segment_t a[BOX_CMD_DIFF_BARS]; /**< The bars in a */
    box_audio_analyze_segment_t b[BOX_CMD_DIFF_BARS]; /**< The bars in b at the offset */
    box_audio_diff_segment_t d[BOX_CMD_DIFF_BARS]; /**< The residual of the bars */
    bool partial[BOX_CMD_DIFF_BARS]; /**< Whether the span holds only a part of a bar */
    int64_t number[BOX_CMD_DIFF_BARS]; /**< Number of a bar, from 1 */
    int32_t count;          /**< Bars in the span, at most BOX_CMD_DIFF_BARS */
    int64_t more;           /**< Bars of the span past BOX_CMD_DIFF_BARS */
} box_cmd_diff_bars_t;

/** Find the bars of the span of a from the beat and tempo of a capture of a song.
 *
 * @param bars Receives the bars, their frames in a and in b at the offset
 * @param song The song chunk
 * @param rate Frames per second
 * @param start First frame of the span of a in its WAV
 * @param a_frames Frames of the span of a
 * @param b_frames Frames of the span of b
 * @param offset Frame n of a goes with frame n + offset of b
 */
static void box_cmd_diff_bar_frames(
    box_cmd_diff_bars_t *bars,
    const box_audio_wav_song_t *song,
    float rate,
    int32_t start,
    int32_t a_frames,
    int32_t b_frames,
    int32_t offset)
{
    double beats_per_frame = song->tempo / 60.0 / rate;
    double first_beat = song->start_beat + start * beats_per_frame;
    double last_beat = song->start_beat + ((double)start + a_frames) * beats_per_frame;
    int64_t first = (int64_t)floor(first_beat / song->bar_beats);
    int64_t last = (int64_t)ceil(last_beat / song->bar_beats) - 1, bar;
    int32_t n = 0;
    for (bar = first; bar <= last && n < BOX_CMD_DIFF_BARS; bar ++) {
        double s = (bar * song->bar_beats - song->start_beat) / beats_per_frame - start;
        double e = ((bar + 1) * song->bar_beats - song->start_beat) / beats_per_frame - start;
        int32_t fs = (int32_t)floor(s + 0.5), fe = (int32_t)floor(e + 0.5), bs, be;
        bars->partial[n] = fs < 0 || fe > a_frames;
        fs = fs < 0 ? 0 : fs;
        fe = fe > a_frames ? a_frames : fe;
        if (fe <= fs) {
            continue;
        }

        bs = fs + offset < 0 ? 0 : (fs + offset > b_frames ? b_frames : fs + offset);
        be = fe + offset < 0 ? 0 : (fe + offset > b_frames ? b_frames : fe + offset);
        bars->a[n].start = bars->d[n].start = fs;
        bars->a[n].end = bars->d[n].end = fe;
        bars->b[n].start = bs;
        bars->b[n].end = be;
        bars->number[n] = (int64_t)floor(((start + fs) * beats_per_frame + song->start_beat) /
            song->bar_beats + 1e-6) + 1;
        n ++;
    }

    bars->count = n;
    bars->more = last - first + 1 > n ? last - first + 1 - n : 0;
}

/** Append the bars that differ most for a capture of a song, by the power of their
 * residual.
 *
 * @param buf The output
 * @param song The song chunk
 * @param a The span of a
 * @param a_frames Frames of the span of a
 * @param b The span of b
 * @param b_frames Frames of the span of b
 * @param channels Channels of both
 * @param rate Frames per second
 * @param start First frame of the span of a in its WAV
 * @param offset Frame n of a goes with frame n + offset of b
 * @return 0 on success, -1 when out of memory
 */
static int box_cmd_diff_bars(
    ecs_strbuf_t *buf,
    const box_audio_wav_song_t *song,
    const float *a,
    int32_t a_frames,
    const float *b,
    int32_t b_frames,
    int32_t channels,
    float rate,
    int32_t start,
    int32_t offset)
{
    box_cmd_diff_bars_t *bars = ecs_os_calloc_t(box_cmd_diff_bars_t);
    bool listed[BOX_CMD_DIFF_BARS] = {false};
    int32_t i, k, differ = 0;
    int result = -1;
    if (!bars) {
        goto done;
    }

    box_cmd_diff_bar_frames(bars, song, rate, start, a_frames, b_frames, offset);
    box_audio_diff_residual(a, a_frames, b, b_frames, channels, offset, bars->d, bars->count);
    if (box_audio_analyze_segments(a, a_frames, channels, rate, bars->a, bars->count) ||
        box_audio_analyze_segments(b, b_frames, channels, rate, bars->b, bars->count))
    {
        goto done;
    }

    for (i = 0; i < bars->count; i ++) {
        differ += bars->d[i].power > 0;
    }

    ecs_strbuf_append(buf, "\nbars: %.1f bpm, %.2f beats per bar, %d of %d bars differ",
        (double)song->tempo, (double)song->bar_beats, differ, bars->count);
    if (bars->more) {
        ecs_strbuf_append(buf, ", %lld more bars not compared", (long long)bars->more);
    }

    for (k = 0; k < BOX_CMD_DIFF_BARS_LISTED; k ++) {
        int32_t best = -1;
        for (i = 0; i < bars->count; i ++) {
            if (!listed[i] && bars->d[i].power > 0 &&
                (best < 0 || bars->d[i].power > bars->d[best].power))
            {
                best = i;
            }
        }

        if (best >= 0) {
            listed[best] = true;
            box_cmd_diff_bar(buf, bars->number[best], bars->partial[best], &bars->d[best],
                &bars->a[best], &bars->b[best]);
        }
    }

    result = 0;
done:
    ecs_os_free(bars);

    return result;
}

char* box_cmd_audio_diff(
    ecs_world_t *world,
    const char *a,
    const char *b,
    const char *from,
    const char *to)
{
    box_audio_wav_t wa = {0}, wb = {0};
    box_audio_analysis_t xa, xb;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    const box_audio_wav_song_t *song = NULL;
    double from_s = 0, to_s = -1;
    int32_t a_start, a_end, b_start, b_end, offset = 0;
    bool channels_differ;
    const float *da, *db;
    char *result = NULL;
    if (!a || !a[0] || !b || !b[0]) {
        ecs_err("audio diff needs two WAVs: resources of the session or files");
        goto done;
    }

    if (!box_cmd_diff_seconds(from, &from_s) || !box_cmd_diff_seconds(to, &to_s)) {
        ecs_err("invalid span '%s' to '%s', expected seconds", from ? from : "",
            to ? to : "");
        goto done;
    }

    if (box_cmd_audio_wav_load(world, a, "diff", &wa) ||
        box_cmd_audio_wav_load(world, b, "diff", &wb))
    {
        goto done;
    }

    if (wa.sample_rate != wb.sample_rate) {
        ecs_err("cannot diff '%s' at %.0f Hz with '%s' at %.0f Hz: the sample rates must be "
            "the same", a, (double)wa.sample_rate, b, (double)wb.sample_rate);
        goto done;
    }

    channels_differ = wa.channels != wb.channels;
    if (channels_differ) {
        box_cmd_diff_mono(&wa);
        box_cmd_diff_mono(&wb);
    }

    if (!box_cmd_diff_span(&wa, from_s, to_s, &a_start, &a_end) ||
        !box_cmd_diff_span(&wb, from_s, to_s, &b_start, &b_end))
    {
        ecs_err("empty span %.3f to %.3f s of '%s' (%.3f s) or '%s' (%.3f s)", from_s, to_s,
            a, wa.frames / (double)wa.sample_rate, b, wb.frames / (double)wb.sample_rate);
        goto done;
    }

    da = wa.data + (int64_t)a_start * wa.channels;
    db = wb.data + (int64_t)b_start * wb.channels;
    if (box_audio_analyze(da, a_end - a_start, wa.channels, wa.sample_rate, &xa) ||
        box_audio_analyze(db, b_end - b_start, wb.channels, wb.sample_rate, &xb))
    {
        ecs_err("cannot diff '%s' and '%s': out of memory", a, b);
        goto done;
    }

    ecs_strbuf_append(&buf, "diff %s %s: %.0f Hz, %s%s, span %.3f to %.3f s", a, b,
        (double)wa.sample_rate, wa.channels == 1 ? "mono" : "stereo",
        channels_differ ? " sums of a mono and a stereo WAV" : "",
        a_start / (double)wa.sample_rate, a_end / (double)wa.sample_rate);
    box_cmd_diff_identity(&buf, &wa, da, a_end - a_start, db, b_end - b_start,
        channels_differ);
    box_cmd_diff_levels(&buf, &wa, &wb, &xa, &xb);
    if (box_cmd_diff_null(&buf, da, a_end - a_start, db, b_end - b_start, wa.channels,
        wa.sample_rate, &offset))
    {
        ecs_err("cannot diff '%s' and '%s': out of memory", a, b);
        goto done;
    }

    song = wa.has_song ? &wa.song : (wb.has_song ? &wb.song : NULL);
    if (song && box_cmd_diff_bars(&buf, song, da, a_end - a_start, db, b_end - b_start,
        wa.channels, wa.sample_rate, a_start, offset))
    {
        ecs_err("cannot diff '%s' and '%s': out of memory", a, b);
        goto done;
    }

    result = ecs_strbuf_get(&buf);
done:
    if (!result) {
        ecs_strbuf_reset(&buf);
    }

    box_audio_wav_fini(&wa);
    box_audio_wav_fini(&wb);

    return result;
}
