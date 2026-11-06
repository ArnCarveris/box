/**
 * @file command/cmd_audio_meter.c
 * @brief The audio.meter action: per bus the peak, RMS and loudness over a span of the live
 * output, the gain reduction of its effects and the headroom, for gain staging.
 *
 * The span renders in real time: the first call starts a measurement (meter.c of box.audio),
 * a call without arguments prints its progress and, once it is measured, the table.
 * A measurement leaves the AudioMeter of the entities, and which ones have it, alone.
 */

#include "command.h"
#include "../audio/module.h"
#include <math.h>
#include <stdlib.h>

/** Seconds measured when no span is given */
#define BOX_CMD_METER_SECONDS (3.0)

/** The span of a measurement */
typedef struct box_cmd_meter_span_t {
    double seconds;        /**< Seconds of output, 0 when not given */
    int32_t first;         /**< First bar of bars=, 0 when not given */
    int32_t last;          /**< Last bar of bars= */
} box_cmd_meter_span_t;

/** Round a value to one decimal so that it never prints as -0.
 *
 * @param value The value
 * @return The value, 0 when it prints as 0
 */
static double box_cmd_meter_round(
    double value)
{
    return fabs(value) < 0.05 ? 0.0 : value;
}

/** Append a level with one decimal, -inf at the floor of the analysis.
 *
 * @param buf The output
 * @param value The level
 * @param unit The unit, such as "dBFS"
 */
static void box_cmd_meter_level(
    ecs_strbuf_t *buf,
    float value,
    const char *unit)
{
    if (value <= BOX_AUDIO_ANALYZE_FLOOR + 0.001f) {
        ecs_strbuf_append(buf, "-inf %s", unit);
    } else {
        ecs_strbuf_append(buf, "%.1f %s", box_cmd_meter_round(value), unit);
    }
}

/** Append the name of a channel: master or its path.
 *
 * @param world The world
 * @param buf The output
 * @param e The channel
 * @param master The master entity
 */
static void box_cmd_meter_name(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t e,
    ecs_entity_t master)
{
    char *path = e == master ? NULL : ecs_get_path(world, e);
    ecs_strbuf_appendstr(buf, path ? path : "master");
    ecs_os_free(path);
}

/** Find a bus by its path from the scope of the session, else by its name.
 *
 * @param world The world
 * @param name The path, the name or master
 * @param master The master entity
 * @return The bus, 0 with an error logged when there is none
 */
static ecs_entity_t box_cmd_meter_find(
    ecs_world_t *world,
    const char *name,
    ecs_entity_t master)
{
    ecs_entity_t result = 0;
    ecs_iter_t it;
    int32_t i;
    if (!ecs_os_strcmp(name, "master")) {
        result = master;
        goto done;
    }

    result = ecs_lookup_path_w_sep(world, box_command_scope(world), name, ".", NULL, true);
    if (result && ecs_has(world, result, AudioBus)) {
        goto done;
    }

    result = 0;
    it = ecs_each(world, AudioBus);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            const char *n = ecs_get_name(world, it.entities[i]);
            if (!result && n && !ecs_os_strcmp(n, name)) {
                result = it.entities[i];
            }
        }
    }

    if (!result) {
        ecs_err("no bus '%s'", name);
    }

done:
    return result;
}

/** Parse bars=a..b or bars=a.
 *
 * @param arg The argument
 * @param span Receives the bars
 * @return Zero on success, nonzero with an error logged
 */
static int box_cmd_meter_bars(
    const char *arg,
    box_cmd_meter_span_t *span)
{
    const char *ptr = arg + 5;
    char *end = NULL;
    long a = strtol(ptr, &end, 10), b = a;
    int result = -1;
    if (end == ptr) {
        goto error;
    }

    if (end[0] == '.' && end[1] == '.') {
        ptr = end + 2;
        b = strtol(ptr, &end, 10);
        if (end == ptr) {
            goto error;
        }
    }

    if (end[0] || a < 1 || b < a || b > 100000) {
        goto error;
    }

    span->first = (int32_t)a;
    span->last = (int32_t)b;
    result = 0;
    goto done;
error:
    ecs_err("invalid bars '%s', expected bars=a..b with 1 <= a <= b", arg + 5);
done:
    return result;
}

/** Seek the transport to the first bar of a span and play, and get its seconds.
 *
 * @param world The world
 * @param span The span, its seconds set
 * @param buf Receives the bars, tempo and signature
 * @return Zero on success, nonzero with an error logged
 */
static int box_cmd_meter_section(
    ecs_world_t *world,
    box_cmd_meter_span_t *span,
    ecs_strbuf_t *buf)
{
    const AudioSongStatus *status = ecs_singleton_get(world, AudioSongStatus);
    Transport *tr;
    double bar, tempo;
    int32_t top, unit;
    int result = -1;
    if (!status || status->state != SongReady) {
        ecs_err("bars= measures a section of a song, and no song is loaded "
            "(audio.song <file>)");
        goto done;
    }

    tr = ecs_singleton_ensure(world, Transport);
    top = tr->beats_per_bar >= 1 && tr->beats_per_bar <= 64 ? tr->beats_per_bar : 4;
    unit = tr->beat_unit >= 1 && tr->beat_unit <= 64 ? tr->beat_unit : 4;
    tempo = tr->tempo >= 20 && tr->tempo <= 999 ? (double)tr->tempo : 120.0;
    bar = (double)top * 4.0 / (double)unit;
    span->seconds = (double)(span->last - span->first + 1) * bar * 60.0 / tempo;
    if (span->seconds > BOX_AUDIO_METER_JOB_SECONDS) {
        ecs_err("bars %d..%d take %.1f s, a measurement takes at most %.0f seconds",
            span->first, span->last, span->seconds, BOX_AUDIO_METER_JOB_SECONDS);
        goto done;
    }

    tr->position = (double)(span->first - 1) * bar;
    tr->playing = true;
    ecs_singleton_modified(world, Transport);
    ecs_strbuf_append(buf, ", bars %d..%d at %.1f bpm %d/%d from beat %.3f", span->first,
        span->last, tempo, top, unit, (double)(span->first - 1) * bar);
    result = 0;
done:
    return result;
}

/** Start a measurement of buses over a span.
 *
 * @param world The world
 * @param argc Number of arguments
 * @param argv The buses, the seconds and bars=a..b in any order; empty ones are skipped
 * @return The text, which the caller frees, NULL with an error logged
 */
static char* box_cmd_meter_start(
    ecs_world_t *world,
    int32_t argc,
    const char **argv)
{
    const box_audio_meter_job_t *job = box_audio_meter_job_get(world);
    ecs_entity_t master = ecs_lookup(world, "box.audio.master");
    ecs_entity_t channels[BOX_AUDIO_METER_JOB_MAX];
    box_cmd_meter_span_t span = {0};
    ecs_strbuf_t buf = ECS_STRBUF_INIT, section = ECS_STRBUF_INIT;
    bool replaces = job && job->running, has_seconds = false;
    char *result = NULL, *text;
    int32_t count = 0, i;
    for (i = 0; i < argc; i ++) {
        const char *arg = argv[i];
        char *end = NULL;
        double number;
        if (!arg || !arg[0]) {
            continue;
        }

        number = strtod(arg, &end);
        if (!ecs_os_strncmp(arg, "bars=", 5)) {
            if (box_cmd_meter_bars(arg, &span)) {
                goto error;
            }
        } else if (end != arg && !end[0]) {
            if (!(number > 0) || number > BOX_AUDIO_METER_JOB_SECONDS) {
                ecs_err("invalid seconds '%s', expected more than 0 and at most %.0f", arg,
                    BOX_AUDIO_METER_JOB_SECONDS);
                goto error;
            }

            span.seconds = number;
            has_seconds = true;
        } else if (count < BOX_AUDIO_METER_JOB_MAX) {
            channels[count] = box_cmd_meter_find(world, arg, master);
            if (!channels[count]) {
                goto error;
            }

            count ++;
        }
    }

    if (has_seconds && span.first) {
        ecs_err("give either seconds or bars=a..b, not both");
        goto error;
    }

    if (!count) {
        count = box_audio_module_order(world, channels, BOX_AUDIO_METER_JOB_MAX - 1);
        channels[count ++] = master;
    }

    if (span.first) {
        if (box_cmd_meter_section(world, &span, &section)) {
            goto error;
        }
    } else if (!has_seconds) {
        span.seconds = BOX_CMD_METER_SECONDS;
    }

    if (box_audio_meter_job_start(world, channels, count, span.seconds, span.first ? 3 : 2)) {
        goto error;
    }

    job = box_audio_meter_job_get(world);
    ecs_strbuf_append(&buf, "meter: measuring %.3f s of the live output in real time",
        span.seconds);
    text = ecs_strbuf_get(&section);
    if (text) {
        ecs_strbuf_appendstr(&buf, text);
        ecs_os_free(text);
    }

    ecs_strbuf_appendstr(&buf, ": ");
    for (i = 0; i < job->count; i ++) {
        if (job->channels[i] == master && !job->master_listed) {
            continue;
        }

        if (i) {
            ecs_strbuf_appendstr(&buf, ", ");
        }

        box_cmd_meter_name(world, &buf, job->channels[i], master);
    }

    if (replaces) {
        ecs_strbuf_appendstr(&buf, "; replaces the measurement that ran");
    }

    ecs_strbuf_append(&buf, "\nrun audio.meter after %.1f s for the table", span.seconds);
    result = ecs_strbuf_get(&buf);
    goto done;
error:
    ecs_strbuf_reset(&buf);
    ecs_strbuf_reset(&section);
done:
    return result;
}

/** Get the name of a kind of effect with a gain reduction.
 *
 * @param kind The kind
 * @return The name, NULL for a kind without a reduction
 */
static const char* box_cmd_meter_kind(
    uint32_t kind)
{
    const char *result = NULL;
    switch (kind) {
    case BOX_AUDIO_EFFECT_COMPRESSOR: result = "compressor"; break;
    case BOX_AUDIO_EFFECT_GATE: result = "gate"; break;
    case BOX_AUDIO_EFFECT_SATURATOR: result = "saturator"; break;
    case BOX_AUDIO_EFFECT_PATTERN_GATE: result = "pattern gate"; break;
    default: break;
    }

    return result;
}

/** Append the lines of the effects of a channel that have a gain reduction.
 *
 * @param world The world
 * @param buf The output
 * @param job The measurement
 * @param e The channel
 */
static void box_cmd_meter_effects(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    const box_audio_meter_job_t *job,
    ecs_entity_t e)
{
    const AudioBus *bus = ecs_get(world, e, AudioBus);
    int32_t j, k;
    for (j = 0; bus && j < BOX_AUDIO_EFFECT_SLOTS; j ++) {
        ecs_entity_t fx = bus->effects[j];
        const box_audio_span_reduction_t *r = NULL;
        const char *kind = fx && ecs_is_alive(world, fx) ?
            box_cmd_meter_kind(box_audio_module_effect_kind(world, fx, NULL)) : NULL;
        char *path;
        if (!kind) {
            continue;
        }

        for (k = 0; k < BOX_AUDIO_EFFECT_MAX && !r; k ++) {
            r = job->effects[k] == fx ? &job->reduction[k] : NULL;
        }

        path = ecs_get_path(world, fx);
        ecs_strbuf_append(buf, "\n  insert %d %s %s: ", j, path, kind);
        ecs_os_free(path);
        if (!r || !(r->frames > 0)) {
            ecs_strbuf_appendstr(buf, "did not run");
        } else if (r->max < BOX_AUDIO_SPAN_ENGAGED) {
            ecs_strbuf_appendstr(buf, "never engages");
        } else {
            ecs_strbuf_append(buf, "reduction avg %.1f dB, max %.1f dB",
                box_cmd_meter_round(box_audio_span_reduction_mean(r)),
                box_cmd_meter_round(r->max));
        }
    }
}

/** Print the table of a measurement that is done.
 *
 * @param world The world
 * @param job The measurement
 * @return The text, which the caller frees
 */
static char* box_cmd_meter_table(
    ecs_world_t *world,
    const box_audio_meter_job_t *job)
{
    ecs_entity_t master = ecs_lookup(world, "box.audio.master");
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_entity_t loudest = 0;
    float loudest_peak = BOX_AUDIO_ANALYZE_FLOOR;
    double rate = job->sample_rate > 0 ? (double)job->sample_rate : 48000.0;
    int32_t i;
    ecs_strbuf_append(&buf, "meter: live output, %.3f s at %.0f Hz", job->frames / rate, rate);
    for (i = 0; i < job->count; i ++) {
        ecs_entity_t e = job->channels[i];
        box_audio_span_result_t r;
        if (e == master && !job->master_listed) {
            continue;
        }

        box_audio_span_level_get(&job->levels[i], &r);
        ecs_strbuf_appendch(&buf, '\n');
        box_cmd_meter_name(world, &buf, e, master);
        if (!(job->levels[i].frames > 0)) {
            ecs_strbuf_appendstr(&buf, ": did not render (no bus index or not in the order)");
            continue;
        }

        ecs_strbuf_appendstr(&buf, ": peak ");
        box_cmd_meter_level(&buf, r.peak, "dBFS");
        ecs_strbuf_appendstr(&buf, ", rms ");
        box_cmd_meter_level(&buf, r.rms, "dB");
        if (r.loudness) {
            ecs_strbuf_appendstr(&buf, ", integrated ");
            box_cmd_meter_level(&buf, r.integrated, "LUFS");
            ecs_strbuf_appendstr(&buf, ", short-term max ");
            box_cmd_meter_level(&buf, r.short_term_max, "LUFS");
        } else if (r.peak > BOX_AUDIO_ANALYZE_FLOOR) {
            ecs_strbuf_appendstr(&buf, ", loudness not measured");
        } else {
            ecs_strbuf_appendstr(&buf, ", integrated -inf LUFS, short-term max -inf LUFS");
        }

        if (job->lost[i]) {
            ecs_strbuf_appendstr(&buf, " (lost its bus index during the span)");
        }

        if (r.peak > loudest_peak) {
            loudest_peak = r.peak;
            loudest = e;
        }

        box_cmd_meter_effects(world, &buf, job, e);
    }

    if (loudest) {
        ecs_strbuf_append(&buf, "\nheadroom: %.1f dB, loudest ",
            box_cmd_meter_round(-(double)loudest_peak));
        box_cmd_meter_name(world, &buf, loudest, master);
        ecs_strbuf_appendstr(&buf, " at ");
        box_cmd_meter_level(&buf, loudest_peak, "dBFS");
    } else {
        ecs_strbuf_appendstr(&buf, "\nheadroom: every bus is silent");
    }

    ecs_strbuf_append(&buf, "; master limiter max reduction %.1f dB",
        box_cmd_meter_round((double)job->limiter));

    return ecs_strbuf_get(&buf);
}

char* box_cmd_audio_meter_span(
    ecs_world_t *world,
    int32_t argc,
    const char **argv)
{
    const box_audio_meter_job_t *job = box_audio_meter_job_get(world);
    char *result = NULL;
    bool any = false;
    int32_t i;
    for (i = 0; i < argc; i ++) {
        any = any || (argv[i] && argv[i][0]);
    }

    if (!job) {
        ecs_err("box.audio is not imported");
    } else if (any) {
        result = box_cmd_meter_start(world, argc, argv);
    } else if (job->done) {
        result = box_cmd_meter_table(world, job);
        box_audio_meter_job_clear(world);
    } else if (job->running) {
        double rate = job->sample_rate > 0 ? (double)job->sample_rate : 48000.0;
        result = flecs_asprintf("meter: measuring, %.3f of %.3f s of the live output done; "
            "run audio.meter after %.1f s for the table", job->frames / rate,
            job->target / rate, (job->target - job->frames) / rate);
    } else {
        result = box_cmd_meter_start(world, argc, argv);
    }

    return result;
}
