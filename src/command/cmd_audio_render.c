/**
 * @file command/cmd_audio_render.c
 * @brief audio render: a sound, an instrument, a bus stem or a section of the song rendered on
 * its own and faster than real time to a WAV resource, without touching the world that plays.
 *
 * A render runs in an offline world that imports box and evaluates the managed scripts of the
 * scene and the song file again; the same box.audio sync, sequencer and core render it on a
 * null transport at fixed deltas, so the live world is only read.
 *
 * The offline world loads without an output and opens one once loaded, so every render starts
 * from an empty core and is bit identical. A system advances it within a budget of wall time
 * per frame, so the live sequencer keeps its lookahead.
 */

#include "command.h"
#include "../audio/module.h"
#include "../audio/output.h"
#include <stdlib.h>
#include <math.h>

/** Frames per second of a render, the rate of a headless capture */
#define BOX_CMD_AUDIO_RENDER_RATE (48000)

/** Delta of a frame of the offline world, 800 frames at the rate */
#define BOX_CMD_AUDIO_RENDER_DT (1.0f / 60.0f)

/** Seconds of wall time a frame of the app spends on a render at most */
#define BOX_CMD_AUDIO_RENDER_BUDGET (0.008)

/** Seconds of wall time the offline world may take to load its scripts, song and sounds */
#define BOX_CMD_AUDIO_RENDER_LOAD_MAX (30.0)

/** Frames of the offline world a timeline may take to start */
#define BOX_CMD_AUDIO_RENDER_START_MAX (600)

/** Seconds of the song rendered at most before a section, from beat 0 */
#define BOX_CMD_AUDIO_RENDER_PREROLL_MAX (600.0)

/** Level under which an automatic tail ends, -90 dB */
#define BOX_CMD_AUDIO_RENDER_SILENCE (3.1622777e-5f)

/** Seconds of a stem of a scene without a song by default */
#define BOX_CMD_AUDIO_RENDER_SCENE_SECONDS (10.0f)

/** Folder of the renders in the file system of the page */
#define BOX_CMD_AUDIO_RENDER_WEB_DIR "/tmp"

/** What a render plays */
typedef enum box_cmd_audio_render_mode_t {
    BOX_CMD_AUDIO_RENDER_NOTE = 0,     /**< One note of a sound or an instrument */
    BOX_CMD_AUDIO_RENDER_TIMELINE = 1, /**< A section of the timeline, the song */
    BOX_CMD_AUDIO_RENDER_SCENE = 2     /**< The scene from its start, for a stem without song */
} box_cmd_audio_render_mode_t;

/** How far a render is */
typedef enum box_cmd_audio_render_phase_t {
    BOX_CMD_AUDIO_RENDER_SETUP = 0,    /**< The offline world does not exist yet */
    BOX_CMD_AUDIO_RENDER_LOAD = 1,     /**< The offline world loads, without an output */
    BOX_CMD_AUDIO_RENDER_START = 2,    /**< The output opens and the subject starts */
    BOX_CMD_AUDIO_RENDER_RUN = 3       /**< The frames are recorded */
} box_cmd_audio_render_phase_t;

/** A managed script of the live world, evaluated again in the offline world */
typedef struct box_cmd_audio_render_script_t {
    char *filename;         /**< File of the script, NULL for none */
    char *code;             /**< Code of the script, NULL to read the file */
} box_cmd_audio_render_script_t;

/** A render that waits for its frames */
typedef struct box_cmd_audio_render_job_t {
    box_cmd_audio_render_mode_t mode; /**< What it plays */
    box_cmd_audio_render_phase_t phase; /**< How far it is */
    char *subject;          /**< Path of the sound, instrument or bus, NULL for the song */
    bool instrument;        /**< The subject is an Instrument */
    bool has_note;          /**< note was given */
    float note;             /**< MIDI note */
    float velocity;         /**< Velocity, 0..1 */
    float duration;         /**< Seconds held before the release, negative for the natural
                             *   length; seconds of a scene stem */
    float tail;             /**< Seconds after the release or the end, negative to render
                             *   until the level falls under -90 dB */
    double from;            /**< First beat of a section */
    double to;              /**< Beat after the section */
    char *solo;             /**< Buses to solo, comma separated, NULL for none */
    char *mute;             /**< Buses to mute, comma separated, NULL for none */
    char *song;             /**< Song file of the live world, NULL for none */
    ecs_vec_t scripts;      /**< box_cmd_audio_render_script_t, oldest first */
    float master_volume;    /**< Master volume of the live world */
    float *pcm;             /**< Samples of a sound made in C, NULL for none */
    int32_t pcm_count;      /**< Frames of pcm */
    int32_t pcm_channels;   /**< Channels of pcm */
    float pcm_rate;         /**< Rate of pcm */
    char *path;             /**< File to write */
    ecs_world_t *world;     /**< The offline world, NULL before the setup */
    ecs_time_t load_start;  /**< Start of the load */
    int32_t waited;         /**< Frames the timeline did not start */
    bool anchored;          /**< A timeline render knows the frame of its first beat */
    ecs_entity_t play;      /**< Instrument of a note in the offline world */
    uint64_t clock;         /**< Frames the core rendered */
    uint64_t start;         /**< First frame of the clock that is kept */
    uint64_t end;           /**< Frame of the clock after the last one kept */
    uint64_t release;       /**< Frame of the release of a note, UINT64_MAX until known */
    float peak;             /**< Peak of the frames of the last frame of the offline world */
    ecs_vec_t frames;       /**< Interleaved stereo frames kept */
    char *log;              /**< First error the offline world logged, NULL for none */
} box_cmd_audio_render_job_t;

/** State of the audio render command of a world */
typedef struct box_cmd_audio_render_state_t {
    ecs_vec_t jobs;         /**< box_cmd_audio_render_job_t*, oldest first */
} box_cmd_audio_render_state_t;

/** The render whose offline world runs, which receives its log */
static box_cmd_audio_render_job_t *box_cmd_audio_render_logging;

/** Free a render and its offline world.
 *
 * @param job The render
 */
static void box_cmd_audio_render_job_free(
    box_cmd_audio_render_job_t *job)
{
    int32_t i;
    box_cmd_audio_render_script_t *scripts = ecs_vec_first(&job->scripts);
    if (job->world) {
        ecs_fini(job->world);
    }

    for (i = 0; i < ecs_vec_count(&job->scripts); i ++) {
        ecs_os_free(scripts[i].filename);
        ecs_os_free(scripts[i].code);
    }

    ecs_vec_fini_t(NULL, &job->scripts, box_cmd_audio_render_script_t);
    ecs_vec_fini_t(NULL, &job->frames, float);
    ecs_os_free(job->subject);
    ecs_os_free(job->solo);
    ecs_os_free(job->mute);
    ecs_os_free(job->song);
    ecs_os_free(job->pcm);
    ecs_os_free(job->path);
    ecs_os_free(job->log);
    ecs_os_free(job);
}

/** Free the state of the audio render command.
 *
 * @param ctx The state
 */
static void box_cmd_audio_render_free(
    void *ctx)
{
    box_cmd_audio_render_state_t *state = ctx;
    box_cmd_audio_render_job_t **jobs = ecs_vec_first(&state->jobs);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->jobs); i ++) {
        box_cmd_audio_render_job_free(jobs[i]);
    }

    ecs_vec_fini_t(NULL, &state->jobs, box_cmd_audio_render_job_t*);
    ecs_os_free(state);
}

/** Log hook while an offline world runs: keep its first error, print nothing.
 *
 * @param level The flecs log level
 * @param file The file that logged
 * @param line The line that logged
 * @param msg The message
 */
static void box_cmd_audio_render_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    box_cmd_audio_render_job_t *job = box_cmd_audio_render_logging;
    (void)file;
    (void)line;
    if (job && level <= -3 && !job->log) {
        job->log = ecs_os_strdup(msg ? msg : "");
    }
}

/** Receive the frames the null transport of an offline world renders, a box_audio_output_t.
 * Keeps the frames of start..end of the clock and the peak of the block.
 *
 * @param left Left channel
 * @param right Right channel
 * @param frames Number of frames
 * @param ctx The render
 */
static void box_cmd_audio_render_tap(
    const float *left,
    const float *right,
    int32_t frames,
    void *ctx)
{
    box_cmd_audio_render_job_t *job = ctx;
    uint64_t from = job->clock > job->start ? job->clock : job->start;
    uint64_t to = job->clock + (uint64_t)frames;
    int32_t i;
    to = to < job->end ? to : job->end;
    for (i = 0; i < frames; i ++) {
        float l = fabsf(left[i]), r = fabsf(right[i]);
        job->peak = l > job->peak ? l : job->peak;
        job->peak = r > job->peak ? r : job->peak;
    }

    if (to > from) {
        int32_t n = (int32_t)(to - from), at = (int32_t)(from - job->clock);
        float *out = ecs_vec_grow_t(NULL, &job->frames, float, n * 2);
        for (i = 0; i < n; i ++) {
            out[i * 2] = left[at + i];
            out[i * 2 + 1] = right[at + i];
        }
    }

    job->clock += (uint64_t)frames;
}

/** Convert seconds to frames of a render.
 *
 * @param seconds The seconds
 * @return The frames
 */
static uint64_t box_cmd_audio_render_frames(
    double seconds)
{
    return seconds > 0 ? (uint64_t)llround(seconds * BOX_CMD_AUDIO_RENDER_RATE) : 0;
}

/** Parse a note: a MIDI number or a name such as C2, F#3 or Bb-1, where C4 is 60.
 *
 * @param text The note
 * @param out Receives the MIDI note
 * @return 0 on success
 */
static int box_cmd_audio_render_note(
    const char *text,
    float *out)
{
    static const int8_t semitones[] = { 9, 11, 0, 2, 4, 5, 7 };
    char *end = NULL;
    double value = strtod(text, &end);
    int result = -1;
    if (end != text && !end[0]) {
        result = value >= 0 && value <= 127 ? 0 : -1;
        *out = (float)value;
        goto done;
    }

    char letter = text[0] >= 'a' && text[0] <= 'g' ? (char)(text[0] - 'a' + 'A') : text[0];
    if (letter < 'A' || letter > 'G') {
        goto done;
    }

    int32_t semitone = semitones[letter - 'A'];
    const char *ptr = text + 1;
    if (ptr[0] == '#') {
        semitone ++;
        ptr ++;
    } else if (ptr[0] == 'b') {
        semitone --;
        ptr ++;
    }

    long octave = strtol(ptr, &end, 10);
    if (end == ptr || end[0]) {
        goto done;
    }

    value = (double)((octave + 1) * 12 + semitone);
    result = value >= 0 && value <= 127 ? 0 : -1;
    *out = (float)value;
done:
    return result;
}

/** Parse a range a..b of two numbers.
 *
 * @param text The range
 * @param from Receives a
 * @param to Receives b
 * @return 0 on success
 */
static int box_cmd_audio_render_range(
    const char *text,
    double *from,
    double *to)
{
    const char *dots = strstr(text, "..");
    char *left = NULL, *end = NULL;
    int result = -1;
    if (!dots || dots == text || !dots[2]) {
        goto done;
    }

    left = ecs_os_malloc((ecs_size_t)(dots - text) + 1);
    ecs_os_memcpy(left, text, (ecs_size_t)(dots - text));
    left[dots - text] = '\0';
    *from = strtod(left, &end);
    if (end == left || end[0]) {
        goto done;
    }

    *to = strtod(dots + 2, &end);
    if (end == dots + 2 || end[0] || !(*to > *from)) {
        goto done;
    }

    result = 0;
done:
    ecs_os_free(left);

    return result;
}

/** Parse a number option.
 *
 * @param key Name of the option
 * @param text The value
 * @param min Smallest value
 * @param max Largest value
 * @param out Receives the value
 * @return 0 on success, nonzero with an error logged
 */
static int box_cmd_audio_render_number(
    const char *key,
    const char *text,
    double min,
    double max,
    float *out)
{
    char *end = NULL;
    double value = strtod(text, &end);
    int result = 0;
    if (end == text || end[0] || !(value >= min) || !(value <= max)) {
        ecs_err("invalid %s '%s', expected %g to %g", key, text, min, max);
        result = -1;
    }

    *out = (float)value;

    return result;
}

/** Find a bus of the live world by its path.
 *
 * @param world The world
 * @param name The path
 * @return The bus, 0 with an error logged when there is no such bus
 */
static ecs_entity_t box_cmd_audio_render_bus(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t result = ecs_lookup_path_w_sep(world, box_command_scope(world), name, ".",
        NULL, true);
    if (!result || !ecs_has(world, result, AudioBus)) {
        ecs_err("'%s' is not a bus", name);
        result = 0;
    }

    return result;
}

/** Check that every bus of a comma separated list is a bus of the live world.
 *
 * @param world The world
 * @param list The list
 * @return 0 when they all are
 */
static int box_cmd_audio_render_buses(
    ecs_world_t *world,
    const char *list)
{
    char *copy = ecs_os_strdup(list), *name = copy, *comma;
    int result = 0;
    while (name && !result) {
        comma = strchr(name, ',');
        if (comma) {
            comma[0] = '\0';
        }

        result = box_cmd_audio_render_bus(world, name) ? 0 : -1;
        name = comma ? comma + 1 : NULL;
    }

    ecs_os_free(copy);

    return result;
}

/** Parse the key=value options of a render into it, for its mode.
 *
 * @param world The live world
 * @param job The render
 * @param argc Number of options
 * @param options The options
 * @return 0 on success, nonzero with an error logged
 */
static int box_cmd_audio_render_options(
    ecs_world_t *world,
    box_cmd_audio_render_job_t *job,
    int32_t argc,
    const char **options)
{
    int32_t i;
    int result = -1;
    bool note = job->mode == BOX_CMD_AUDIO_RENDER_NOTE;
    for (i = 0; i < argc; i ++) {
        const char *eq = options[i] ? strchr(options[i], '=') : NULL;
        const char *value = eq ? eq + 1 : NULL;
        int32_t len = eq ? (int32_t)(eq - options[i]) : 0;
        if (!eq || !value[0]) {
            ecs_err("invalid option '%s', expected key=value", options[i] ? options[i] : "");
            goto done;
        }

        if (len == 4 && !ecs_os_strncmp(options[i], "note", 4) && note) {
            job->has_note = true;
            if (box_cmd_audio_render_note(value, &job->note)) {
                ecs_err("invalid note '%s', expected a name such as C2 or a MIDI number "
                    "0 to 127", value);
                goto done;
            }
        } else if (len == 3 && !ecs_os_strncmp(options[i], "vel", 3) && note) {
            if (box_cmd_audio_render_number("vel", value, 0, 1, &job->velocity)) {
                goto done;
            }
        } else if (len == 3 && !ecs_os_strncmp(options[i], "dur", 3) &&
            job->mode != BOX_CMD_AUDIO_RENDER_TIMELINE)
        {
            if (box_cmd_audio_render_number("dur", value, 0.001,
                BOX_AUDIO_CAPTURE_MAX, &job->duration))
            {
                goto done;
            }
        } else if (len == 4 && !ecs_os_strncmp(options[i], "tail", 4)) {
            if (box_cmd_audio_render_number("tail", value, 0, BOX_AUDIO_CAPTURE_MAX,
                &job->tail))
            {
                goto done;
            }
        } else if (len == 4 && !ecs_os_strncmp(options[i], "bars", 4) && !note) {
            double a = 0, b = 0;
            const Transport *tr = ecs_singleton_get(world, Transport);
            double top = tr && tr->beats_per_bar > 0 ? tr->beats_per_bar : 4;
            double unit = tr && tr->beat_unit > 0 ? tr->beat_unit : 4;
            if (box_cmd_audio_render_range(value, &a, &b) || a < 1 || a != floor(a) ||
                b != floor(b))
            {
                ecs_err("invalid bars '%s', expected a..b, from bar 1 on", value);
                goto done;
            }

            job->from = (a - 1) * top * 4.0 / unit;
            job->to = b * top * 4.0 / unit;
            job->mode = BOX_CMD_AUDIO_RENDER_TIMELINE;
        } else if (len == 5 && !ecs_os_strncmp(options[i], "beats", 5) && !note) {
            if (box_cmd_audio_render_range(value, &job->from, &job->to) || job->from < 0) {
                ecs_err("invalid beats '%s', expected a..b, from beat 0 on", value);
                goto done;
            }

            job->mode = BOX_CMD_AUDIO_RENDER_TIMELINE;
        } else if (len == 4 && !ecs_os_strncmp(options[i], "solo", 4) && !note) {
            if (box_cmd_audio_render_buses(world, value)) {
                goto done;
            }

            ecs_os_strset(&job->solo, value);
        } else if (len == 4 && !ecs_os_strncmp(options[i], "mute", 4) && !note) {
            if (box_cmd_audio_render_buses(world, value)) {
                goto done;
            }

            ecs_os_strset(&job->mute, value);
        } else {
            ecs_err("invalid option '%s': a sound or an instrument takes note, vel, dur and "
                "tail, a bus or the song bars, beats, solo, mute and tail", options[i]);
            goto done;
        }
    }

    result = 0;
done:
    return result;
}

/** Copy the managed scripts of the live world that the offline world evaluates again: the
 * scripts of the scene, in the order they were created, not those of box, of flecs, of
 * templates or that another script owns, such as the scripts of the song.
 *
 * @param world The live world
 * @param job The render
 */
static void box_cmd_audio_render_scripts(
    ecs_world_t *world,
    box_cmd_audio_render_job_t *job)
{
    ecs_iter_t it = ecs_each(world, EcsScript);
    ecs_vec_t entities = {0};
    int32_t i, j;
    while (ecs_each_next(&it)) {
        const EcsScript *s = ecs_field(&it, EcsScript, 0);
        for (i = 0; i < it.count; i ++) {
            char *path = ecs_get_path(world, it.entities[i]);
            bool skip = s[i].template_ || (!s[i].code && !s[i].filename) ||
                ecs_get_target(world, it.entities[i], ecs_id(EcsScript), 0) ||
                !ecs_os_strncmp(path, "box.", 4) || !ecs_os_strncmp(path, "flecs.", 6);
            ecs_os_free(path);
            if (!skip) {
                ecs_vec_append_t(NULL, &entities, ecs_entity_t)[0] = it.entities[i];
            }
        }
    }

    ecs_entity_t *e = ecs_vec_first(&entities);
    int32_t count = ecs_vec_count(&entities);
    for (i = 1; i < count; i ++) {
        for (j = i; j > 0 && (uint32_t)e[j - 1] > (uint32_t)e[j]; j --) {
            ecs_entity_t tmp = e[j];
            e[j] = e[j - 1];
            e[j - 1] = tmp;
        }
    }

    for (i = 0; i < count; i ++) {
        const EcsScript *s = ecs_get(world, e[i], EcsScript);
        box_cmd_audio_render_script_t *out = ecs_vec_append_t(NULL, &job->scripts,
            box_cmd_audio_render_script_t);
        out->filename = s->filename ? ecs_os_strdup(s->filename) : NULL;
        out->code = s->code ? ecs_os_strdup(s->code) : NULL;
    }

    ecs_vec_fini_t(NULL, &entities, ecs_entity_t);
}

/** Check the length of a render against the cap, with the tempo of the live world.
 *
 * @param world The live world
 * @param job The render
 * @return 0 when it fits, nonzero with an error logged
 */
static int box_cmd_audio_render_length(
    ecs_world_t *world,
    const box_cmd_audio_render_job_t *job)
{
    const Transport *tr = ecs_singleton_get(world, Transport);
    double tempo = tr && tr->tempo > 0 ? tr->tempo : 120;
    double seconds = 0, preroll = 0;
    int result = 0;
    if (job->mode == BOX_CMD_AUDIO_RENDER_TIMELINE) {
        seconds = (job->to - job->from) * 60.0 / tempo;
        preroll = job->from * 60.0 / tempo;
    } else if (job->duration >= 0 && job->tail >= 0) {
        seconds = job->duration;
    }

    seconds += job->tail > 0 ? job->tail : 0;
    if (seconds > BOX_AUDIO_CAPTURE_MAX) {
        ecs_err("the render takes %.2f s, more than the %.0f s a render takes at most",
            seconds, (double)BOX_AUDIO_CAPTURE_MAX);
        result = -1;
    } else if (preroll > BOX_CMD_AUDIO_RENDER_PREROLL_MAX) {
        ecs_err("the section starts %.2f s into the song, more than the %.0f s a render "
            "plays before a section", preroll, BOX_CMD_AUDIO_RENDER_PREROLL_MAX);
        result = -1;
    }

    return result;
}

/** Find what a render plays in the live world and set its mode, its subject and the
 * defaults of its options.
 *
 * @param world The live world
 * @param job The render
 * @param subject The subject: song, a sound, an instrument or a bus
 * @return 0 on success, nonzero with an error logged naming why it cannot be rendered
 */
static int box_cmd_audio_render_subject(
    ecs_world_t *world,
    box_cmd_audio_render_job_t *job,
    const char *subject)
{
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t e = 0;
    int result = -1;
    job->velocity = 1;
    job->duration = -1;
    job->tail = -1;
    if (!ecs_os_strcmp(subject, "song")) {
        job->mode = BOX_CMD_AUDIO_RENDER_TIMELINE;
        result = 0;
        goto done;
    }

    e = ecs_lookup_path_w_sep(world, box_command_scope(world), subject, ".", NULL, true);
    if (!e) {
        ecs_err("'%s' not found, expected song, a sound, an instrument or a bus", subject);
        goto done;
    }

    job->subject = ecs_get_path(world, e);
    if (ecs_has(world, e, Instrument)) {
        job->mode = BOX_CMD_AUDIO_RENDER_NOTE;
        job->instrument = true;
    } else if (ecs_has(world, e, SynthSound) || ecs_has(world, e, AudioPcm) ||
        ecs_has(world, e, SoundFile))
    {
        job->mode = BOX_CMD_AUDIO_RENDER_NOTE;
    } else if (ecs_has(world, e, AudioBus)) {
        job->mode = job->song ? BOX_CMD_AUDIO_RENDER_TIMELINE : BOX_CMD_AUDIO_RENDER_SCENE;
    } else {
        ecs_err("'%s' cannot be rendered: it is not a sound, an instrument or a bus", subject);
        goto done;
    }

    if (e != state->master && !ecs_get_target(world, e, ecs_id(EcsScript), 0)) {
        const AudioPcm *pcm = ecs_get(world, e, AudioPcm);
        int32_t channels = pcm && pcm->channels > 1 ? 2 : 1;
        if (!pcm || job->mode != BOX_CMD_AUDIO_RENDER_NOTE || job->instrument) {
            ecs_err("'%s' cannot be rendered: no managed script or song file made it, so the "
                "offline world of the render cannot make it again", subject);
            goto done;
        }

        job->pcm_count = ecs_vec_count(&pcm->samples) / channels;
        job->pcm_channels = channels;
        job->pcm_rate = pcm->sample_rate;
        job->pcm = ecs_os_memdup(ecs_vec_first(&pcm->samples),
            ecs_vec_count(&pcm->samples) * ECS_SIZEOF(float));
    }

    result = 0;
done:
    return result;
}

/** Set up a render from the command: its subject, its options, the scripts and the song of
 * the live world, and check that it can be rendered.
 *
 * @param world The live world
 * @param subject The subject
 * @param argc Number of options
 * @param options The options
 * @return The render, NULL with an error logged
 */
static box_cmd_audio_render_job_t* box_cmd_audio_render_job(
    ecs_world_t *world,
    const char *subject,
    int32_t argc,
    const char **options)
{
    box_cmd_audio_render_job_t *job = ecs_os_calloc_t(box_cmd_audio_render_job_t);
    const AudioSong *song = ecs_singleton_get(world, AudioSong);
    const AudioSongStatus *status = ecs_singleton_get(world, AudioSongStatus);
    const AudioSettings *settings = ecs_singleton_get(world, AudioSettings);
    const Song *s = ecs_singleton_get(world, Song);
    job->release = UINT64_MAX;
    job->end = UINT64_MAX;
    job->start = UINT64_MAX;
    job->master_volume = settings ? settings->master_volume : 1.0f;
    if (song && song->file && song->file[0]) {
        job->song = ecs_os_strdup(song->file);
    }

    if (box_cmd_audio_render_subject(world, job, subject) ||
        box_cmd_audio_render_options(world, job, argc, options))
    {
        goto error;
    }

    if (job->mode == BOX_CMD_AUDIO_RENDER_TIMELINE && job->song && status &&
        status->state == SongFailed)
    {
        ecs_err("the song cannot be rendered: it failed to load: %s",
            status->error ? status->error : "");
        goto error;
    }

    if (job->mode == BOX_CMD_AUDIO_RENDER_TIMELINE && !(job->to > job->from)) {
        if (!job->song || !s || !(s->beats > 0)) {
            ecs_err("%s cannot be rendered without a range: %s, give bars=a..b or beats=a..b",
                job->subject ? job->subject : "the song",
                job->song ? "the song has no length (Song.beats)" : "no song is loaded");
            goto error;
        }

        job->from = 0;
        job->to = s->beats;
    }

    if (job->mode == BOX_CMD_AUDIO_RENDER_SCENE && job->duration < 0) {
        job->duration = BOX_CMD_AUDIO_RENDER_SCENE_SECONDS;
    }

    if (box_cmd_audio_render_length(world, job)) {
        goto error;
    }

    box_cmd_audio_render_scripts(world, job);
    goto done;
error:
    box_cmd_audio_render_job_free(job);
    job = NULL;
done:
    return job;
}

/** Silence what the scene of the offline world plays by itself, for the render of a note:
 * its sources, streams and emitters, and the transport.
 *
 * @param world The offline world
 */
static void box_cmd_audio_render_quiet(
    ecs_world_t *world)
{
    ecs_id_t ids[] = { ecs_id(AudioSource), ecs_id(AudioStream), ecs_id(AudioEmitter) };
    ecs_vec_t entities = {0};
    int32_t i, k;
    for (k = 0; k < 3; k ++) {
        ecs_iter_t it = ecs_each_id(world, ids[k]);
        while (ecs_each_next(&it)) {
            for (i = 0; i < it.count; i ++) {
                ecs_vec_append_t(NULL, &entities, ecs_entity_t)[0] = it.entities[i];
            }
        }
    }

    ecs_entity_t *e = ecs_vec_first(&entities);
    for (i = 0; i < ecs_vec_count(&entities); i ++) {
        ecs_enable(world, e[i], false);
    }

    ecs_vec_fini_t(NULL, &entities, ecs_entity_t);
    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->playing = false;
    ecs_singleton_modified(world, Transport);
}

/** Set the solo or the mute of the buses of a comma separated list in the offline world.
 *
 * @param world The offline world
 * @param list The list, NULL for none
 * @param solo Set the solo, else the mute
 * @return 0 on success, nonzero with an error logged when a bus is missing
 */
static int box_cmd_audio_render_mark(
    ecs_world_t *world,
    const char *list,
    bool solo)
{
    char *copy = list ? ecs_os_strdup(list) : NULL, *name = copy, *comma;
    int result = 0;
    while (name && !result) {
        comma = strchr(name, ',');
        if (comma) {
            comma[0] = '\0';
        }

        ecs_entity_t e = ecs_lookup(world, name);
        AudioBus *bus = e ? ecs_get_mut(world, e, AudioBus) : NULL;
        if (!bus) {
            ecs_err("bus '%s' is not in the offline world: no script or song file makes it",
                name);
            result = -1;
        } else if (e != box_audio_module_get(world)->master) {
            if (solo) {
                bus->solo = true;
            } else {
                bus->mute = true;
            }

            ecs_modified(world, e, AudioBus);
        }

        name = comma ? comma + 1 : NULL;
    }

    ecs_os_free(copy);

    return result;
}

/** Create the offline world: box, the scripts of the scene, the master volume, a sound made in
 * C and the song. It has no output yet.
 *
 * @param job The render
 */
static void box_cmd_audio_render_setup(
    box_cmd_audio_render_job_t *job)
{
    ecs_world_t *world = job->world = ecs_init();
    box_cmd_audio_render_script_t *scripts = ecs_vec_first(&job->scripts);
    int32_t i;
    ECS_IMPORT(world, Box);
    for (i = 0; i < ecs_vec_count(&job->scripts); i ++) {
        ecs_script(world, { .filename = scripts[i].filename, .code = scripts[i].code });
    }

    AudioSettings *settings = ecs_singleton_ensure(world, AudioSettings);
    settings->master_volume = job->master_volume;
    settings->mute = false;
    ecs_singleton_modified(world, AudioSettings);
    if (job->pcm) {
        ecs_entity_t e = ecs_new_from_path(world, 0, job->subject);
        box_audio_sound_from_pcm(world, e, job->pcm, job->pcm_count, job->pcm_channels,
            job->pcm_rate);
    }

    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->playing = false;
    ecs_singleton_modified(world, Transport);
    if (job->song) {
        box_audio_song_load(world, job->song);
    }

    ecs_os_get_time(&job->load_start);
    job->phase = BOX_CMD_AUDIO_RENDER_LOAD;
}

/** Run a frame of the offline world while it loads, and tell whether it is done: the song
 * no longer loads and no sound file decodes.
 *
 * @param job The render
 * @param error Receives why the render fails
 * @return Whether the load is done or failed
 */
static bool box_cmd_audio_render_load(
    box_cmd_audio_render_job_t *job,
    char **error)
{
    ecs_world_t *world = job->world;
    const AudioSongStatus *status;
    ecs_time_t t = job->load_start;
    bool result = false;
    int32_t i;
    ecs_progress(world, BOX_CMD_AUDIO_RENDER_DT);
    status = ecs_singleton_get(world, AudioSongStatus);
    if (job->song && status && status->state == SongFailed) {
        *error = flecs_asprintf("the song failed to load in the offline world: %s",
            status->error ? status->error : "");
        result = true;
        goto done;
    }

    if (ecs_time_measure(&t) > BOX_CMD_AUDIO_RENDER_LOAD_MAX) {
        *error = flecs_asprintf("the offline world did not load within %.0f s",
            BOX_CMD_AUDIO_RENDER_LOAD_MAX);
        result = true;
        goto done;
    }

    if (job->song && (!status || status->state != SongReady)) {
        goto done;
    }

    ecs_iter_t it = ecs_each(world, SoundFile);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            if (box_audio_sound_loading(world, it.entities[i])) {
                ecs_iter_fini(&it);
                goto done;
            }
        }
    }

    job->phase = BOX_CMD_AUDIO_RENDER_START;
    result = true;
done:
    return result;
}

/** Open the output of the offline world and play the note: an instrument plays as it is, a
 * sound through an instrument of its own whose root is A4, so that it plays as authored.
 *
 * @param job The render
 * @param error Receives why the render fails
 */
static void box_cmd_audio_render_start_note(
    box_cmd_audio_render_job_t *job,
    char **error)
{
    ecs_world_t *world = job->world;
    ecs_entity_t e = ecs_lookup(world, job->subject);
    if (!e) {
        *error = flecs_asprintf("'%s' is not in the offline world: no script or song file makes "
            "it%s%s", job->subject, job->log ? "; first error: " : "", job->log ? job->log : "");
        goto done;
    }

    box_cmd_audio_render_quiet(world);
    ecs_entity_t inst = job->instrument ? e : ecs_new(world);
    Instrument *in = ecs_ensure(world, inst, Instrument);
    if (!job->instrument) {
        in->sound = e;
        in->root = 69;
    }

    in->gate = job->duration >= 0;
    job->note = job->has_note ? job->note : in->root;
    ecs_modified(world, inst, Instrument);
    job->play = inst;
    ecs_progress(world, BOX_CMD_AUDIO_RENDER_DT);

    const Transport *tr = ecs_singleton_get(world, Transport);
    double tempo = tr && tr->tempo > 0 ? tr->tempo : 120;
    float beats = job->duration >= 0 ? (float)(job->duration * tempo / 60.0) : 1.0f;
    job->start = job->clock;
    if (!box_audio_play_note(world, inst, job->note, job->velocity, beats)) {
        *error = flecs_asprintf("the note of '%s' did not play%s%s", job->subject,
            job->log ? ": " : "", job->log ? job->log : "");
        goto done;
    }

    if (job->duration >= 0) {
        job->release = job->start + box_cmd_audio_render_frames(job->duration);
    }

    job->end = job->start + box_cmd_audio_render_frames(BOX_AUDIO_CAPTURE_MAX);
    if (job->duration >= 0 && job->tail >= 0) {
        job->end = job->release + box_cmd_audio_render_frames(job->tail);
    }

done:
    return;
}

/** Open the output of the offline world and start what a render plays.
 *
 * @param job The render
 * @param error Receives why the render fails
 */
static void box_cmd_audio_render_start(
    box_cmd_audio_render_job_t *job,
    char **error)
{
    ecs_world_t *world = job->world;
    box_audio_output_open(world, &(box_audio_output_desc_t){
        .headless = true, .output = box_cmd_audio_render_tap, .ctx = job });
    job->phase = BOX_CMD_AUDIO_RENDER_RUN;
    if (job->mode == BOX_CMD_AUDIO_RENDER_NOTE) {
        box_cmd_audio_render_start_note(job, error);
        goto done;
    }

    if (box_cmd_audio_render_mark(world, job->solo, true) ||
        box_cmd_audio_render_mark(world, job->mute, false) ||
        box_cmd_audio_render_mark(world, job->subject, true))
    {
        *error = flecs_asprintf("a bus of the render is missing in the offline world%s%s",
            job->log ? "; first error: " : "", job->log ? job->log : "");
        goto done;
    }

    job->start = job->clock;
    if (job->mode == BOX_CMD_AUDIO_RENDER_SCENE) {
        job->end = job->start + box_cmd_audio_render_frames(job->duration +
            (job->tail > 0 ? job->tail : 0));
        goto done;
    }

    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->position = 0;
    tr->loop = false;
    tr->playing = true;
    ecs_singleton_modified(world, Transport);
done:
    return;
}

/** Find the frame of the first beat of a section once the timeline of the offline world plays:
 * drop the frames recorded before it and set the end.
 *
 * @param job The render
 * @param error Receives why the render fails
 */
static void box_cmd_audio_render_anchor(
    box_cmd_audio_render_job_t *job,
    char **error)
{
    const box_audio_timeline_t *tl = &box_audio_module_get(job->world)->timeline;
    const box_audio_anchor_t *a = ecs_vec_first(&tl->anchors);
    if (!tl->playing || !a) {
        if (++ job->waited > BOX_CMD_AUDIO_RENDER_START_MAX) {
            *error = ecs_os_strdup("the timeline of the offline world did not start");
        } else {
            ecs_vec_clear(&job->frames);
            job->start = job->clock;
        }

        goto done;
    }

    int64_t from = box_audio_anchor_frame(a, job->from);
    int64_t to = box_audio_anchor_frame(a, job->to);
    uint64_t first = from > (int64_t)job->start ? (uint64_t)from : job->start;
    uint64_t drop = first - job->start;
    int32_t kept = ecs_vec_count(&job->frames) / 2;
    if (drop >= (uint64_t)kept) {
        ecs_vec_clear(&job->frames);
    } else {
        float *f = ecs_vec_first(&job->frames);
        ecs_os_memmove_n(f, f + drop * 2, float, (kept - (int32_t)drop) * 2);
        ecs_vec_set_count_t(NULL, &job->frames, float, (kept - (int32_t)drop) * 2);
    }

    job->start = first;
    job->end = (uint64_t)(to > from ? to : from) + box_cmd_audio_render_frames(
        job->tail > 0 ? job->tail : 0);
    job->anchored = true;
done:
    return;
}

/** Run a frame of the offline world that records, and tell whether the render is done.
 *
 * @param job The render
 * @param error Receives why the render fails
 * @return Whether the render is done or failed
 */
static bool box_cmd_audio_render_run(
    box_cmd_audio_render_job_t *job,
    char **error)
{
    ecs_world_t *world = job->world;
    box_audio_transport_t *t = box_audio_output_transport(world);
    box_audio_transport_stats_t stats = {0};
    bool result = false;
    job->peak = 0;
    ecs_progress(world, BOX_CMD_AUDIO_RENDER_DT);
    if (job->mode == BOX_CMD_AUDIO_RENDER_TIMELINE && !job->anchored) {
        box_cmd_audio_render_anchor(job, error);
        result = *error != NULL;
        goto done;
    }

    if (job->mode == BOX_CMD_AUDIO_RENDER_NOTE && t) {
        t->stats(t, &stats);
        if (job->release == UINT64_MAX && !stats.voices) {
            job->release = job->clock;
            if (job->tail >= 0) {
                uint64_t end = job->clock + box_cmd_audio_render_frames(job->tail);
                job->end = end < job->end ? end : job->end;
            }
        }

        if (job->tail < 0 && job->clock >= job->release && !stats.voices &&
            job->peak < BOX_CMD_AUDIO_RENDER_SILENCE)
        {
            job->end = job->clock;
        }
    }

    result = job->clock >= job->end;
done:
    return result;
}

/** Advance a render by one step.
 *
 * @param job The render
 * @param error Receives why the render fails
 * @return Whether the render is done or failed
 */
static bool box_cmd_audio_render_step(
    box_cmd_audio_render_job_t *job,
    char **error)
{
    bool result = false;
    switch (job->phase) {
    case BOX_CMD_AUDIO_RENDER_SETUP:
        box_cmd_audio_render_setup(job);
        break;
    case BOX_CMD_AUDIO_RENDER_LOAD:
        result = box_cmd_audio_render_load(job, error) && *error;
        break;
    case BOX_CMD_AUDIO_RENDER_START:
        box_cmd_audio_render_start(job, error);
        result = *error != NULL;
        break;
    case BOX_CMD_AUDIO_RENDER_RUN:
        result = box_cmd_audio_render_run(job, error);
        break;
    }

    return result;
}

/** Write the frames of a finished render to its file.
 *
 * @param job The render
 * @return 0 on success
 */
static int box_cmd_audio_render_write(
    box_cmd_audio_render_job_t *job)
{
    int32_t frames = ecs_vec_count(&job->frames) / 2;
    uint64_t length = job->end > job->start ? job->end - job->start : 0;
    if ((uint64_t)frames > length) {
        frames = (int32_t)length;
    }

    return box_audio_wav_write(job->path, ecs_vec_first(&job->frames), frames,
        BOX_CMD_AUDIO_RENDER_RATE);
}

/** Advance the oldest render within the budget of wall time of a frame, and write its file
 * once its frames are in. The log of the offline world is kept out of the log of the app.
 *
 * @param it The iterator of the system
 */
static void BoxAudioRenderCommands(
    ecs_iter_t *it)
{
    box_cmd_audio_render_state_t *state = it->ctx;
    box_cmd_audio_render_job_t *job;
    ecs_os_api_log_t prev = ecs_os_api.log_;
    ecs_time_t start, t;
    char *error = NULL;
    bool done = false, written = true;
    if (!ecs_vec_count(&state->jobs)) {
        goto done;
    }

    job = ecs_vec_first_t(&state->jobs, box_cmd_audio_render_job_t*)[0];
    ecs_os_get_time(&start);
    box_cmd_audio_render_logging = job;
    ecs_os_api.log_ = box_cmd_audio_render_log;
    do {
        done = box_cmd_audio_render_step(job, &error);
        t = start;
    } while (!done && ecs_time_measure(&t) < BOX_CMD_AUDIO_RENDER_BUDGET);

    if (done && !error) {
        written = !box_cmd_audio_render_write(job);
    }

    if (done && job->world) {
        ecs_fini(job->world);
        job->world = NULL;
    }

    ecs_os_api.log_ = prev;
    box_cmd_audio_render_logging = NULL;
    if (!done) {
        goto done;
    }

    if (error) {
        ecs_err("audio render %s: %s", job->subject ? job->subject : "song", error);
    } else if (!written) {
        ecs_err("audio render %s: cannot write '%s'", job->subject ? job->subject : "song",
            job->path);
    }

    ecs_os_free(error);
    ecs_vec_remove_ordered_t(&state->jobs, box_cmd_audio_render_job_t*, 0);
    box_cmd_audio_render_job_free(job);
done:
    return;
}

char* box_cmd_audio_render(
    ecs_world_t *world,
    const char *subject,
    int32_t argc,
    const char **options)
{
    ecs_entity_t system = ecs_lookup(world, "box.command.AudioRenderCommands");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    box_cmd_audio_render_state_t *state = s ? s->ctx : NULL;
    box_cmd_audio_render_job_t *job = NULL;
    char *result = NULL;
    if (!state) {
        ecs_err("box.command is not imported");
        goto done;
    }

    if (!subject || !subject[0]) {
        ecs_err("missing subject, expected song, a sound, an instrument or a bus");
        goto done;
    }

    job = box_cmd_audio_render_job(world, subject, argc, options);
    if (!job) {
        goto done;
    }

#ifdef __EMSCRIPTEN__
    static int32_t box_cmd_audio_renders;
    job->path = flecs_asprintf("%s/audio_render_%d.wav", BOX_CMD_AUDIO_RENDER_WEB_DIR,
        ++ box_cmd_audio_renders);
    result = ecs_os_strdup(job->path);
#else
    ecs_shell_resource_t resource = {0};
    job->path = box_cmd_render_resource(world, "audio/wav", &resource);
    if (job->path) {
        result = ecs_os_strdup(resource.name);
    }

    ecs_os_free(resource.name);
    ecs_os_free(resource.content_type);
#endif
    if (!result) {
        box_cmd_audio_render_job_free(job);
        goto done;
    }

    ecs_vec_append_t(NULL, &state->jobs, box_cmd_audio_render_job_t*)[0] = job;
done:
    return result;
}

void box_cmd_audio_render_init(
    ecs_world_t *world)
{
    box_cmd_audio_render_state_t *state = ecs_os_calloc_t(box_cmd_audio_render_state_t);
    ecs_vec_init_t(NULL, &state->jobs, box_cmd_audio_render_job_t*, 0);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "AudioRenderCommands" }),
        .phase = EcsOnStore,
        .run = BoxAudioRenderCommands,
        .ctx = state,
        .ctx_free = box_cmd_audio_render_free
    });
}
