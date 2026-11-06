/**
 * @file command/cmd_audio_notes.c
 * @brief The audio notes and audio chords commands: the notes of a track per bar or as a text
 * piano roll, and the chords and the key of a section, from the note data of the timeline.
 *
 * They read only the Track, ClipPlacement, NoteClip and Instrument data and the time signature
 * of the Song, else of the Transport, so they work without an output and without a device.
 * Positions are the programmed ones, before swing and humanize.
 */

#include "command.h"
#include "../audio/harmony.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <string.h>

/** Ticks of a beat of the time signature in a bar.beat.tick position */
#define BOX_CMD_NOTES_TICKS (480)

/** Notes a command collects at most, so a loop of a tiny clip stays bounded */
#define BOX_CMD_NOTES_MAX (1 << 20)

/** Columns of the piano roll per quarter note: one per sixteenth */
#define BOX_CMD_NOTES_ROLL_STEPS (4)

/** Pitch classes that sound per beat on average from which chords are named per beat */
#define BOX_CMD_NOTES_DENSITY (3.0)

/** Positions listed per pitch class outside the key */
#define BOX_CMD_NOTES_OUTSIDE (4)

/** Margin of a beat compared with a grid, for floats written in a script */
#define BOX_CMD_NOTES_EPSILON (1e-6)

/** A note on the timeline, placed and transposed */
typedef struct box_cmd_note_t {
    double start;          /**< Beats on the timeline */
    double end;            /**< Beats on the timeline, cut at the end of its pass */
    float pitch;           /**< MIDI note, with the transpose of the placement */
    float velocity;        /**< 0..1 */
    int32_t track;         /**< Index of the track in the command */
} box_cmd_note_t;

/** A track of a command */
typedef struct box_cmd_track_t {
    ecs_entity_t e;        /**< The track */
    char *path;            /**< Its path, owned */
    const char *sound;     /**< Name of its sound, for a drum track */
    bool drum;             /**< Its notes are separate sounds, not pitches */
    bool pitches;          /**< It plays more than one pitch */
} box_cmd_track_t;

/** The time signature and the length of the timeline */
typedef struct box_cmd_meter_t {
    int32_t beats;         /**< Beats per bar */
    int32_t unit;          /**< Bottom of the time signature */
    double beat;           /**< Quarter notes of a beat */
    double bar;            /**< Quarter notes of a bar */
    float tempo;           /**< Quarter notes per minute */
    double length;         /**< Beats of the song, 0 for unknown */
} box_cmd_meter_t;

/** The bars a command covers */
typedef struct box_cmd_bars_t {
    int32_t first;         /**< First bar, from 1 */
    int32_t last;          /**< Last bar, included */
    bool set;              /**< Given with bars=, else the whole timeline */
} box_cmd_bars_t;

/** Words in the name of a track, an instrument or a sound that make it a drum track */
static const char *box_cmd_notes_drum_words[] = {
    "kick", "bd", "snare", "sd", "hat", "hats", "hihat", "openhat", "closedhat", "hh",
    "clap", "tom", "toms", "ride", "crash", "cymbal", "perc", "percussion", "drum", "drums",
    "rim", "rimshot", "shaker", "tamb", "tambourine", "cowbell", "conga", "bongo", "clave",
    "kit", NULL
};

/** Get the time signature of the Song, else of the Transport, else 4/4 at 120.
 *
 * @param world The world
 * @return The meter
 */
static box_cmd_meter_t box_cmd_notes_meter(
    ecs_world_t *world)
{
    const Song *song = ecs_id(Song) ? ecs_singleton_get(world, Song) : NULL;
    const Transport *tr = ecs_id(Transport) ? ecs_singleton_get(world, Transport) : NULL;
    box_cmd_meter_t result = { .beats = 4, .unit = 4, .tempo = 120 };
    if (song && song->title) {
        result.beats = song->beats_per_bar;
        result.unit = song->beat_unit;
        result.tempo = song->tempo;
        result.length = isfinite(song->beats) && song->beats > 0 ? song->beats : 0;
    } else if (tr) {
        result.beats = tr->beats_per_bar;
        result.unit = tr->beat_unit;
        result.tempo = tr->tempo;
    }

    result.beats = result.beats >= 1 && result.beats <= 64 ? result.beats : 4;
    result.unit = result.unit >= 1 && result.unit <= 64 ? result.unit : 4;
    result.beat = 4.0 / (double)result.unit;
    result.bar = result.beat * (double)result.beats;

    return result;
}

/** Get the ticks of a beat of the timeline from beat 0.
 *
 * @param m The meter
 * @param beat Beats on the timeline
 * @return Ticks, BOX_CMD_NOTES_TICKS per beat of the time signature
 */
static int64_t box_cmd_notes_ticks(
    const box_cmd_meter_t *m,
    double beat)
{
    return (int64_t)llround(beat / m->beat * (double)BOX_CMD_NOTES_TICKS);
}

/** Get the bar of a beat of the timeline, from 1, the bar of its position.
 *
 * @param m The meter
 * @param beat Beats on the timeline
 * @return The bar
 */
static int64_t box_cmd_notes_bar(
    const box_cmd_meter_t *m,
    double beat)
{
    int64_t ticks = box_cmd_notes_ticks(m, beat);
    int64_t per_bar = (int64_t)m->beats * BOX_CMD_NOTES_TICKS;
    int64_t bar = ticks >= 0 ? ticks / per_bar : -((-ticks + per_bar - 1) / per_bar);

    return bar + 1;
}

/** Append the position of a beat: bar.beat.tick, or bar.beat without the ticks.
 *
 * @param buf The output
 * @param m The meter
 * @param beat Beats on the timeline
 * @param ticks Append the ticks
 */
static void box_cmd_notes_pos(
    ecs_strbuf_t *buf,
    const box_cmd_meter_t *m,
    double beat,
    bool ticks)
{
    int64_t t = box_cmd_notes_ticks(m, beat);
    int64_t per_bar = (int64_t)m->beats * BOX_CMD_NOTES_TICKS;
    int64_t bar = box_cmd_notes_bar(m, beat) - 1;
    int64_t rem = t - bar * per_bar;
    if (ticks) {
        ecs_strbuf_append(buf, "%lld.%lld.%03lld", (long long)(bar + 1),
            (long long)(rem / BOX_CMD_NOTES_TICKS + 1), (long long)(rem % BOX_CMD_NOTES_TICKS));
    } else {
        ecs_strbuf_append(buf, "%lld.%lld", (long long)(bar + 1),
            (long long)(rem / BOX_CMD_NOTES_TICKS + 1));
    }
}

/** Get the greatest common divisor of two positive numbers.
 *
 * @param a A number
 * @param b Another number
 * @return The divisor
 */
static int64_t box_cmd_notes_gcd(
    int64_t a,
    int64_t b)
{
    int64_t t;
    while (b) {
        t = a % b;
        a = b;
        b = t;
    }

    return a;
}

/** Append a length: a note value of a whole note when it is exact, such as 1/8 for an eighth,
 * 3/8 for a dotted quarter and 1/12 for an eighth triplet, else beats.
 *
 * @param buf The output
 * @param beats Quarter notes
 */
static void box_cmd_notes_length(
    ecs_strbuf_t *buf,
    double beats)
{
    static const int64_t denominators[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 };
    double n;
    int64_t num, g;
    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(denominators) / sizeof(denominators[0])); i ++) {
        n = beats / 4.0 * (double)denominators[i];
        if (n >= 0.5 && fabs(n - round(n)) < 1e-4) {
            num = (int64_t)llround(n);
            g = box_cmd_notes_gcd(num, denominators[i]);
            ecs_strbuf_append(buf, "%lld/%lld", (long long)(num / g),
                (long long)(denominators[i] / g));
            goto done;
        }
    }

    ecs_strbuf_append(buf, "%.3g beats", beats);
done:
    return;
}

/** Append the notes of one pass of a clip that start inside a placement.
 *
 * @param p The placement
 * @param clip The clip
 * @param base Beat of the timeline where the clip starts in the pass
 * @param pass_end Beat where the pass ends
 * @param limit Notes that start at or past this beat of the clip do not play
 * @param track Index of the track
 * @param out The notes
 */
static void box_cmd_notes_pass(
    const ClipPlacement *p,
    const NoteClip *clip,
    double base,
    double pass_end,
    double limit,
    int32_t track,
    ecs_vec_t *out)
{
    const Note *notes = ecs_vec_first(&clip->notes);
    int32_t i, count = ecs_vec_count(&clip->notes);
    double start = isfinite(p->start) ? p->start : 0.0;
    double offset = isfinite(p->offset) && p->offset > 0 ? p->offset : 0.0;
    double length = isfinite(clip->length) && clip->length > 0 ? (double)clip->length : 0.0;
    double span = isfinite(p->duration) && p->duration > 0 ? p->duration : length - offset;
    double end = start + (span > 0 ? span : 0.0), at, stop, duration;
    float transpose = isfinite(p->transpose) ? p->transpose : 0.0f;
    box_cmd_note_t *n;
    for (i = 0; i < count && ecs_vec_count(out) < BOX_CMD_NOTES_MAX; i ++) {
        if (!isfinite(notes[i].start) || !isfinite(notes[i].pitch) ||
            (double)notes[i].start >= limit)
        {
            continue;
        }

        at = base + (double)notes[i].start;
        if (at < start - BOX_CMD_NOTES_EPSILON || at >= end - BOX_CMD_NOTES_EPSILON) {
            continue;
        }

        duration = isfinite(notes[i].duration) && notes[i].duration > 0 ?
            (double)notes[i].duration : 0.0;
        stop = at + duration;
        stop = stop < pass_end ? stop : pass_end;
        stop = stop < end ? stop : end;
        n = ecs_vec_append_t(NULL, out, box_cmd_note_t);
        *n = (box_cmd_note_t){ .start = at, .end = stop, .pitch = notes[i].pitch + transpose,
            .velocity = isfinite(notes[i].velocity) ? notes[i].velocity : 0.0f,
            .track = track };
    }
}

/** Append the notes of a placement of a NoteClip, every pass of a loop.
 *
 * @param p The placement
 * @param clip The clip
 * @param track Index of the track
 * @param out The notes
 */
static void box_cmd_notes_place(
    const ClipPlacement *p,
    const NoteClip *clip,
    int32_t track,
    ecs_vec_t *out)
{
    double start = isfinite(p->start) ? p->start : 0.0;
    double offset = isfinite(p->offset) && p->offset > 0 ? p->offset : 0.0;
    double length = isfinite(clip->length) && clip->length > 0 ? (double)clip->length : 0.0;
    double span = isfinite(p->duration) && p->duration > 0 ? p->duration : length - offset;
    double end = start + (span > 0 ? span : 0.0), step, phase, base;
    int64_t k;
    if (p->loop && length > 0) {
        step = length > 1.0 / 64.0 ? length : 1.0 / 64.0;
        phase = fmod(offset, step);
        for (k = 0; ecs_vec_count(out) < BOX_CMD_NOTES_MAX; k ++) {
            base = start - phase + (double)k * step;
            if (base >= end) {
                break;
            }

            box_cmd_notes_pass(p, clip, base, base + step, length, track, out);
        }
    } else {
        box_cmd_notes_pass(p, clip, start - offset, INFINITY, INFINITY, track, out);
    }
}

/** Append the notes of every enabled placement of a NoteClip on a track.
 *
 * @param world The world
 * @param track The track
 * @param index Index of the track in the command
 * @param out The notes
 */
static void box_cmd_notes_collect(
    ecs_world_t *world,
    ecs_entity_t track,
    int32_t index,
    ecs_vec_t *out)
{
    const ClipPlacement *p;
    const NoteClip *clip;
    int32_t i;
    ecs_iter_t it = ecs_children(world, track);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            p = ecs_get(world, it.entities[i], ClipPlacement);
            if (!p || ecs_has_id(world, it.entities[i], EcsDisabled) || !p->clip ||
                !ecs_is_alive(world, p->clip))
            {
                continue;
            }

            clip = ecs_get(world, p->clip, NoteClip);
            if (clip) {
                box_cmd_notes_place(p, clip, index, out);
            }
        }
    }
}

/** Order notes by start, pitch, end, velocity and track.
 *
 * @param a A note
 * @param b Another note
 * @return Negative when a comes first
 */
static int box_cmd_notes_compare(
    const void *a,
    const void *b)
{
    const box_cmd_note_t *x = a, *y = b;
    int result = (x->start > y->start) - (x->start < y->start);
    result = result ? result : (x->pitch > y->pitch) - (x->pitch < y->pitch);
    result = result ? result : (x->end > y->end) - (x->end < y->end);
    result = result ? result : (x->velocity > y->velocity) - (x->velocity < y->velocity);
    result = result ? result : (x->track > y->track) - (x->track < y->track);

    return result;
}

/** Tell whether a name holds a drum word, such as kick in dk_kick_sound or MvKick: a run of
 * letters, split at a capital after a small letter, equal to a word without case.
 *
 * @param name The name, NULL for none
 * @return Whether it names a drum
 */
static bool box_cmd_notes_drum_name(
    const char *name)
{
    char token[32];
    int32_t len = 0, i, w;
    bool result = false;
    for (i = 0; name && !result; i ++) {
        char ch = name[i];
        bool letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        bool split = letter && len > 0 && ch >= 'A' && ch <= 'Z' &&
            token[len - 1] >= 'a' && token[len - 1] <= 'z' && name[i - 1] >= 'a' &&
            name[i - 1] <= 'z';
        if ((!letter || split) && len > 0) {
            token[len] = '\0';
            for (w = 0; box_cmd_notes_drum_words[w] && !result; w ++) {
                result = !ecs_os_strcmp(token, box_cmd_notes_drum_words[w]);
            }

            len = 0;
        }

        if (letter && len < (int32_t)sizeof(token) - 1) {
            token[len ++] = (char)(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
        }

        if (!ch) {
            break;
        }
    }

    return result;
}

/** Decide whether a track is a drum track and name its sound.
 *
 * A drum track names a drum word in the name of the track, its instrument or its sound, or has
 * an instrument without a gate that plays a single pitch, a one shot hit.
 *
 * @param world The world
 * @param t The track
 * @param notes Every note of the command
 * @param count Number of notes
 * @param index Index of the track
 */
static void box_cmd_notes_classify(
    ecs_world_t *world,
    box_cmd_track_t *t,
    const box_cmd_note_t *notes,
    int32_t count,
    int32_t index)
{
    const Track *tk = ecs_get(world, t->e, Track);
    ecs_entity_t inst = tk && tk->instrument && ecs_is_alive(world, tk->instrument) ?
        tk->instrument : 0;
    const Instrument *in = inst ? ecs_get(world, inst, Instrument) : NULL;
    ecs_entity_t sound = in && in->sound && ecs_is_alive(world, in->sound) ? in->sound : 0;
    long first = LONG_MIN;
    int32_t i, played = 0;
    for (i = 0; i < count; i ++) {
        if (notes[i].track != index) {
            continue;
        }

        if (played && lroundf(notes[i].pitch) != first) {
            t->pitches = true;
        }

        first = played ? first : lroundf(notes[i].pitch);
        played ++;
    }

    t->drum = box_cmd_notes_drum_name(ecs_get_name(world, t->e)) ||
        (inst && box_cmd_notes_drum_name(ecs_get_name(world, inst))) ||
        (sound && box_cmd_notes_drum_name(ecs_get_name(world, sound))) ||
        (in && !in->gate && played && !t->pitches);
    t->sound = sound ? ecs_get_name(world, sound) : NULL;
    t->sound = !t->sound && inst ? ecs_get_name(world, inst) : t->sound;
    t->sound = t->sound ? t->sound : ecs_get_name(world, t->e);
    t->sound = t->sound ? t->sound : "sound";
}

/** Write the label of a note: its pitch, or the sound of a drum track, with the pitch when the
 * track plays several.
 *
 * @param t The track
 * @param pitch The pitch
 * @param out Receives the label, BOX_AUDIO_HARMONY_NAME * 2 bytes
 */
static void box_cmd_notes_label(
    const box_cmd_track_t *t,
    float pitch,
    char *out)
{
    char name[BOX_AUDIO_HARMONY_NAME];
    box_audio_pitch_name(pitch, false, name);
    if (!t->drum) {
        snprintf(out, BOX_AUDIO_HARMONY_NAME * 2, "%s", name);
    } else if (t->pitches) {
        snprintf(out, BOX_AUDIO_HARMONY_NAME * 2, "%.40s:%s", t->sound, name);
    } else {
        snprintf(out, BOX_AUDIO_HARMONY_NAME * 2, "%.80s", t->sound);
    }
}

/** Find a track by its path from the root, else by its name.
 *
 * @param world The world
 * @param name The path or the name
 * @return The track, 0 with an error logged when there is none
 */
static ecs_entity_t box_cmd_notes_find(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t result = ecs_lookup(world, name);
    ecs_query_t *q;
    ecs_iter_t it;
    const char *n;
    int32_t i;
    if (result && ecs_has(world, result, Track)) {
        goto done;
    }

    result = 0;
    q = ecs_query(world, {
        .terms = {{ .id = ecs_id(Track), .inout = EcsIn, .src.id = EcsSelf }},
        .flags = EcsQueryMatchDisabled
    });
    it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count && !result; i ++) {
            n = ecs_get_name(world, it.entities[i]);
            result = n && !ecs_os_strcmp(n, name) ? it.entities[i] : 0;
        }
    }

    ecs_query_fini(q);
    if (!result) {
        ecs_err("no track '%s'", name);
    }

done:
    return result;
}

/** Parse bars=a..b or bars=a.
 *
 * @param arg The argument
 * @param bars Receives the bars
 * @return Zero on success, nonzero with an error logged
 */
static int box_cmd_notes_parse_bars(
    const char *arg,
    box_cmd_bars_t *bars)
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

    bars->first = (int32_t)a;
    bars->last = (int32_t)b;
    bars->set = true;
    result = 0;
    goto done;
error:
    ecs_err("invalid bars '%s', expected bars=a..b with 1 <= a <= b", arg + 5);
done:
    return result;
}

/** Fill in the bars of the whole timeline when bars= is not given: from bar 1 to the bar of the
 * end of the song or of the last note.
 *
 * @param bars The bars
 * @param m The meter
 * @param notes The notes
 * @param count Number of notes
 */
static void box_cmd_notes_default_bars(
    box_cmd_bars_t *bars,
    const box_cmd_meter_t *m,
    const box_cmd_note_t *notes,
    int32_t count)
{
    double end = m->length;
    int32_t i;
    for (i = 0; i < count; i ++) {
        end = notes[i].end > end ? notes[i].end : end;
        end = notes[i].start + BOX_CMD_NOTES_EPSILON > end ?
            notes[i].start + BOX_CMD_NOTES_EPSILON : end;
    }

    if (!bars->set) {
        bars->first = 1;
        bars->last = (int32_t)ceil(end / m->bar - BOX_CMD_NOTES_EPSILON);
        bars->last = bars->last >= 1 ? (bars->last <= 100000 ? bars->last : 100000) : 1;
    }
}

/** Free the tracks of a command.
 *
 * @param tracks The tracks
 */
static void box_cmd_notes_tracks_fini(
    ecs_vec_t *tracks)
{
    box_cmd_track_t *t = ecs_vec_first(tracks);
    int32_t i, count = ecs_vec_count(tracks);
    for (i = 0; i < count; i ++) {
        ecs_os_free(t[i].path);
    }

    ecs_vec_fini_t(NULL, tracks, box_cmd_track_t);
}

/** Append the header of a track: its kind, instrument, meter, bars and count of notes.
 *
 * @param world The world
 * @param buf The output
 * @param what "notes" or "roll"
 * @param t The track
 * @param m The meter
 * @param bars The bars
 * @param count Notes in the bars
 */
static void box_cmd_notes_header(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    const char *what,
    const box_cmd_track_t *t,
    const box_cmd_meter_t *m,
    const box_cmd_bars_t *bars,
    int32_t count)
{
    const Track *tk = ecs_get(world, t->e, Track);
    char *inst = tk && tk->instrument && ecs_is_alive(world, tk->instrument) ?
        ecs_get_path(world, tk->instrument) : NULL;
    ecs_strbuf_append(buf, "%s of %s: %s, instrument %s, %d/%d at %.4g bpm, bars %d..%d, "
        "%d notes", what, t->path, t->drum ? "drums, one row per sound" : "pitched",
        inst ? inst : "none", m->beats, m->unit, (double)m->tempo, bars->first, bars->last,
        count);
    ecs_os_free(inst);
}

/** Append the notes of a track per bar: position, pitch or sound, length and velocity.
 *
 * @param buf The output
 * @param t The track
 * @param m The meter
 * @param bars The bars
 * @param notes The notes of the track, sorted
 * @param count Number of notes
 */
static void box_cmd_notes_list(
    ecs_strbuf_t *buf,
    const box_cmd_track_t *t,
    const box_cmd_meter_t *m,
    const box_cmd_bars_t *bars,
    const box_cmd_note_t *notes,
    int32_t count)
{
    char label[BOX_AUDIO_HARMONY_NAME * 2];
    int32_t bar, i = 0, listed;
    int64_t b;
    ecs_strbuf_appendstr(buf, "; position bar.beat.tick (480 ticks a beat), length as a note "
        "value (1/4 a quarter note) or in beats, velocity 0..1");
    while (i < count && box_cmd_notes_bar(m, notes[i].start) < bars->first) {
        i ++;
    }

    for (bar = bars->first; bar <= bars->last; bar ++) {
        ecs_strbuf_append(buf, "\nbar %d:", bar);
        for (listed = 0; i < count; i ++, listed ++) {
            b = box_cmd_notes_bar(m, notes[i].start);
            if (b != bar) {
                break;
            }

            ecs_strbuf_appendstr(buf, listed ? ", " : " ");
            box_cmd_notes_pos(buf, m, notes[i].start, true);
            box_cmd_notes_label(t, notes[i].pitch, label);
            ecs_strbuf_append(buf, " %s ", label);
            box_cmd_notes_length(buf, notes[i].end - notes[i].start);
            ecs_strbuf_append(buf, " v%.2f", (double)notes[i].velocity);
        }

        if (!listed) {
            ecs_strbuf_appendstr(buf, " -");
        }
    }
}

/** Append the piano roll of a track: a row per pitch it plays in the bars, highest first, and
 * a column per sixteenth, x where a note starts, = while it holds and . where it is silent.
 *
 * @param buf The output
 * @param t The track
 * @param m The meter
 * @param bars The bars
 * @param notes The notes of the track, sorted
 * @param count Number of notes
 */
static void box_cmd_notes_roll(
    ecs_strbuf_t *buf,
    const box_cmd_track_t *t,
    const box_cmd_meter_t *m,
    const box_cmd_bars_t *bars,
    const box_cmd_note_t *notes,
    int32_t count)
{
    double from = (double)(bars->first - 1) * m->bar, to = (double)bars->last * m->bar;
    int32_t cols = (int32_t)llround((to - from) * BOX_CMD_NOTES_ROLL_STEPS);
    int32_t i, j, c, c0, c1, width = 0, rows = 0, bar, k;
    long pitch, row[128 + 1], p;
    char label[BOX_AUDIO_HARMONY_NAME * 2];
    char *line = ecs_os_calloc(cols + 1);
    ecs_strbuf_append(buf, "; a column per 1/16, x starts, = holds, . silent");
    for (p = 127; p >= 0; p --) {
        for (i = 0; i < count; i ++) {
            if (lroundf(notes[i].pitch) == p && notes[i].end > from && notes[i].start < to) {
                row[rows ++] = p;
                box_cmd_notes_label(t, (float)p, label);
                width = (int32_t)strlen(label) > width ? (int32_t)strlen(label) : width;
                break;
            }
        }
    }

    ecs_strbuf_append(buf, "\n%*s", width, "");
    for (bar = bars->first; bar <= bars->last; bar ++) {
        c0 = (int32_t)llround((double)(bar - bars->first) * m->bar * BOX_CMD_NOTES_ROLL_STEPS);
        c1 = (int32_t)llround((double)(bar - bars->first + 1) * m->bar *
            BOX_CMD_NOTES_ROLL_STEPS);
        ecs_strbuf_append(buf, " |%-*d", c1 - c0 - 1 > 0 ? c1 - c0 - 1 : 0, bar);
    }

    for (j = 0; j < rows; j ++) {
        pitch = row[j];
        ecs_os_memset(line, '.', cols);
        for (i = 0; i < count; i ++) {
            if (lroundf(notes[i].pitch) != pitch || notes[i].end <= from ||
                notes[i].start >= to)
            {
                continue;
            }

            c0 = (int32_t)floor((notes[i].start - from) * BOX_CMD_NOTES_ROLL_STEPS +
                BOX_CMD_NOTES_EPSILON);
            c1 = (int32_t)ceil((notes[i].end - from) * BOX_CMD_NOTES_ROLL_STEPS -
                BOX_CMD_NOTES_EPSILON);
            c1 = c1 > c0 + 1 ? c1 : c0 + 1;
            for (c = c0 > 0 ? c0 : 0; c < c1 && c < cols; c ++) {
                line[c] = c == c0 ? 'x' : (line[c] == 'x' ? 'x' : '=');
            }
        }

        box_cmd_notes_label(t, (float)pitch, label);
        ecs_strbuf_append(buf, "\n%-*s ", width, label);
        for (bar = bars->first, k = 0; bar <= bars->last; bar ++) {
            c0 = (int32_t)llround((double)(bar - bars->first) * m->bar *
                BOX_CMD_NOTES_ROLL_STEPS);
            c1 = (int32_t)llround((double)(bar - bars->first + 1) * m->bar *
                BOX_CMD_NOTES_ROLL_STEPS);
            ecs_strbuf_append(buf, "|%.*s", c1 - c0, line + c0);
            k = c1;
        }

        ecs_strbuf_appendstr(buf, k ? "|" : "");
    }

    ecs_os_free(line);
}

char* box_cmd_audio_notes(
    ecs_world_t *world,
    int32_t argc,
    const char **argv)
{
    box_cmd_meter_t m = box_cmd_notes_meter(world);
    box_cmd_bars_t bars = {0};
    box_cmd_track_t t = {0};
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_vec_t notes;
    box_cmd_note_t *n;
    int32_t i, count, first, last;
    bool roll = false;
    char *result = NULL;
    ecs_vec_init_t(NULL, &notes, box_cmd_note_t, 0);
    if (argc < 1 || !argv[0] || !argv[0][0]) {
        ecs_err("missing track, usage: audio notes <track> [bars=a..b] [roll]");
        goto done;
    }

    for (i = 1; i < argc; i ++) {
        if (!ecs_os_strncmp(argv[i], "bars=", 5)) {
            if (box_cmd_notes_parse_bars(argv[i], &bars)) {
                goto done;
            }
        } else if (!ecs_os_strcmp(argv[i], "roll")) {
            roll = true;
        } else {
            ecs_err("unknown argument '%s', expected bars=a..b or roll", argv[i]);
            goto done;
        }
    }

    t.e = box_cmd_notes_find(world, argv[0]);
    if (!t.e) {
        goto done;
    }

    t.path = ecs_get_path(world, t.e);
    box_cmd_notes_collect(world, t.e, 0, &notes);
    n = ecs_vec_first(&notes);
    count = ecs_vec_count(&notes);
    qsort(n, (size_t)count, sizeof(box_cmd_note_t), box_cmd_notes_compare);
    box_cmd_notes_classify(world, &t, n, count, 0);
    box_cmd_notes_default_bars(&bars, &m, n, count);
    first = 0;
    while (first < count && box_cmd_notes_bar(&m, n[first].start) < bars.first) {
        first ++;
    }

    last = first;
    while (last < count && box_cmd_notes_bar(&m, n[last].start) <= bars.last) {
        last ++;
    }

    box_cmd_notes_header(world, &buf, roll ? "roll" : "notes", &t, &m, &bars, last - first);
    if (roll) {
        box_cmd_notes_roll(&buf, &t, &m, &bars, n, count);
    } else {
        box_cmd_notes_list(&buf, &t, &m, &bars, n + first, last - first);
    }

    result = ecs_strbuf_get(&buf);
done:
    ecs_strbuf_reset(&buf);
    ecs_os_free(t.path);
    ecs_vec_fini_t(NULL, &notes, box_cmd_note_t);

    return result;
}

/** Order tracks by path.
 *
 * @param a A track
 * @param b Another track
 * @return Negative when a comes first
 */
static int box_cmd_notes_track_compare(
    const void *a,
    const void *b)
{
    const box_cmd_track_t *x = a, *y = b;

    return strcmp(x->path, y->path);
}

/** Add the tracks of a chords command: the named ones, else every enabled track, by path.
 *
 * @param world The world
 * @param argc Number of arguments
 * @param argv The arguments, tracks and bars=
 * @param tracks Receives the tracks
 * @param bars Receives the bars
 * @return Zero on success, nonzero with an error logged
 */
static int box_cmd_chords_tracks(
    ecs_world_t *world,
    int32_t argc,
    const char **argv,
    ecs_vec_t *tracks,
    box_cmd_bars_t *bars)
{
    box_cmd_track_t *t;
    ecs_entity_t e;
    int32_t i, named = 0, result = -1;
    for (i = 0; i < argc; i ++) {
        if (!ecs_os_strncmp(argv[i], "bars=", 5)) {
            if (box_cmd_notes_parse_bars(argv[i], bars)) {
                goto done;
            }

            continue;
        }

        e = box_cmd_notes_find(world, argv[i]);
        if (!e) {
            goto done;
        }

        t = ecs_vec_append_t(NULL, tracks, box_cmd_track_t);
        *t = (box_cmd_track_t){ .e = e, .path = ecs_get_path(world, e) };
        named ++;
    }

    if (!named) {
        ecs_iter_t it = ecs_each(world, Track);
        while (ecs_each_next(&it)) {
            for (i = 0; i < it.count; i ++) {
                t = ecs_vec_append_t(NULL, tracks, box_cmd_track_t);
                *t = (box_cmd_track_t){ .e = it.entities[i],
                    .path = ecs_get_path(world, it.entities[i]) };
            }
        }

        qsort(ecs_vec_first(tracks), (size_t)ecs_vec_count(tracks), sizeof(box_cmd_track_t),
            box_cmd_notes_track_compare);
    }

    result = 0;
done:
    return result;
}

/** Name the chord of a span from the pitched notes that sound in it, weighted by the beats each
 * sounds; the bass is the lowest note whose pitch class is part of the chord.
 *
 * @param notes The notes, sorted
 * @param count Number of notes
 * @param pitched Whether each track is pitched
 * @param from Start of the span in beats
 * @param to End of the span in beats
 * @return The chord
 */
static box_audio_chord_t box_cmd_chords_span(
    const box_cmd_note_t *notes,
    int32_t count,
    const bool *pitched,
    double from,
    double to)
{
    float weights[12] = {0};
    double a, b;
    box_audio_pcs_t set;
    long bass = LONG_MAX, p;
    int32_t i;
    for (i = 0; i < count && notes[i].start < to; i ++) {
        a = notes[i].start > from ? notes[i].start : from;
        b = notes[i].end < to ? notes[i].end : to;
        if (b > a && pitched[notes[i].track]) {
            weights[((lroundf(notes[i].pitch) % 12) + 12) % 12] += (float)(b - a);
        }
    }

    set = box_audio_pcs_from_weights(weights, BOX_AUDIO_HARMONY_SHARE);
    for (i = 0; i < count && notes[i].start < to; i ++) {
        p = lroundf(notes[i].pitch);
        if (notes[i].end > from && pitched[notes[i].track] && ((set >> ((p % 12 + 12) % 12)) & 1)
            && p < bass)
        {
            bass = p;
        }
    }

    return box_audio_chord_find(set, weights,
        bass == LONG_MAX ? -1 : (int32_t)((bass % 12 + 12) % 12));
}

/** Pick the span of the chords: a beat when the pitched notes hold at least
 * BOX_CMD_NOTES_DENSITY pitch classes per beat on average and some bar holds two chords for
 * at least two of its beats each (one in a bar of fewer than four beats), else a bar.
 *
 * @param m The meter
 * @param bars The bars
 * @param notes The notes, sorted
 * @param count Number of notes
 * @param pitched Whether each track is pitched
 * @param density Receives the pitch classes per beat
 * @return Whether to name a chord per beat
 */
static bool box_cmd_chords_per_beat(
    const box_cmd_meter_t *m,
    const box_cmd_bars_t *bars,
    const box_cmd_note_t *notes,
    int32_t count,
    const bool *pitched,
    double *density)
{
    int32_t held[12 * 16];
    int32_t bar, beat, i, sounding = 0, classes = 0, chords, need = m->beats >= 4 ? 2 : 1;
    box_audio_chord_t c;
    bool changes = false;
    double at;
    for (bar = bars->first; bar <= bars->last; bar ++) {
        ecs_os_memset(held, 0, sizeof(held));
        for (beat = 0; beat < m->beats; beat ++) {
            at = (double)(bar - 1) * m->bar + (double)beat * m->beat;
            c = box_cmd_chords_span(notes, count, pitched, at, at + m->beat);
            if (c.set) {
                sounding ++;
                classes += box_audio_pcs_count(c.set);
            }

            if (c.root >= 0 && c.quality < 16) {
                held[c.root * 16 + c.quality] ++;
            }
        }

        for (i = 0, chords = 0; i < 12 * 16; i ++) {
            chords += held[i] >= need;
        }

        changes = changes || chords >= 2;
    }

    *density = sounding ? (double)classes / (double)sounding : 0.0;

    return changes && *density >= BOX_CMD_NOTES_DENSITY;
}

/** Append the pitch classes of a set from a root, spelled for a key.
 *
 * @param buf The output
 * @param set The set
 * @param root The class to start from
 * @param flats Spell black keys with flats
 */
static void box_cmd_chords_set(
    ecs_strbuf_t *buf,
    box_audio_pcs_t set,
    int32_t root,
    bool flats)
{
    int32_t i, pc, listed = 0;
    for (i = 0; i < 12; i ++) {
        pc = (root + i) % 12;
        if ((set >> pc) & 1) {
            ecs_strbuf_append(buf, "%s%s", listed ++ ? " " : "", box_audio_pc_name(pc, flats));
        }
    }
}

/** Append the chords of the bars, one line per chord with its start, its length in beats and
 * its pitch classes; equal chords in a row make one line.
 *
 * @param buf The output
 * @param m The meter
 * @param bars The bars
 * @param notes The notes, sorted
 * @param count Number of notes
 * @param pitched Whether each track is pitched
 * @param per_beat Name a chord per beat, else per bar
 * @param flats Spell black keys with flats
 */
static void box_cmd_chords_list(
    ecs_strbuf_t *buf,
    const box_cmd_meter_t *m,
    const box_cmd_bars_t *bars,
    const box_cmd_note_t *notes,
    int32_t count,
    const bool *pitched,
    bool per_beat,
    bool flats)
{
    double step = per_beat ? m->beat : m->bar, from = (double)(bars->first - 1) * m->bar;
    double to = (double)bars->last * m->bar, at, start = from;
    int32_t spans = (int32_t)llround((to - from) / step), i;
    char name[BOX_AUDIO_HARMONY_NAME] = "", next[BOX_AUDIO_HARMONY_NAME] = "";
    box_audio_chord_t c = {0}, cur = {0};
    box_audio_pcs_t set = 0;
    for (i = 0; i <= spans; i ++) {
        at = from + (double)i * step;
        if (i < spans) {
            c = box_cmd_chords_span(notes, count, pitched, at, at + step);
            box_audio_chord_name(&c, flats, next);
        }

        if (i && (i == spans || ecs_os_strcmp(next, name))) {
            ecs_strbuf_appendstr(buf, "\n");
            box_cmd_notes_pos(buf, m, start, false);
            ecs_strbuf_append(buf, " %s %.4g beats", name, at - start);
            if (cur.root >= 0) {
                ecs_strbuf_appendstr(buf, ": ");
                box_cmd_chords_set(buf, set, cur.root, flats);
            }

            start = at;
            set = 0;
        }

        if (i < spans && (!i || ecs_os_strcmp(next, name))) {
            memcpy(name, next, sizeof(name));
            cur = c;
        }

        set = i < spans ? (box_audio_pcs_t)(set | c.set) : set;
    }
}

/** Append a key and the next most likely one, from the weights of the pitch classes of the
 * pitched notes that sound in a span.
 *
 * @param buf The output, NULL to only find the key
 * @param what What the key is of
 * @param notes The notes
 * @param count Number of notes
 * @param pitched Whether each track is pitched
 * @param from Start of the span in beats
 * @param to End of the span in beats
 * @return The key
 */
static box_audio_key_t box_cmd_chords_key(
    ecs_strbuf_t *buf,
    const char *what,
    const box_cmd_note_t *notes,
    int32_t count,
    const bool *pitched,
    double from,
    double to)
{
    float weights[12] = {0};
    char name[BOX_AUDIO_HARMONY_NAME];
    box_audio_key_t second, result;
    double a, b;
    int32_t i;
    for (i = 0; i < count; i ++) {
        a = notes[i].start > from ? notes[i].start : from;
        b = notes[i].end < to ? notes[i].end : to;
        if (b > a && pitched[notes[i].track]) {
            weights[((lroundf(notes[i].pitch) % 12) + 12) % 12] += (float)(b - a);
        }
    }

    result = box_audio_key_find(weights, &second);
    box_audio_key_name(result, name);
    if (buf) {
        ecs_strbuf_append(buf, "\nkey of %s: %s", what, name);
    }

    if (buf && result.tonic >= 0) {
        ecs_strbuf_append(buf, " (r %.2f)", (double)result.score);
        box_audio_key_name(second, name);
        ecs_strbuf_append(buf, ", then %s (r %.2f)", name, (double)second.score);
    }

    return result;
}

/** Append the pitched notes that start in the bars outside a key: per pitch class the count
 * and the first positions.
 *
 * @param buf The output
 * @param m The meter
 * @param key The key
 * @param notes The notes, sorted
 * @param count Number of notes
 * @param pitched Whether each track is pitched
 * @param from Start of the bars in beats
 * @param to End of the bars in beats
 */
static void box_cmd_chords_outside(
    ecs_strbuf_t *buf,
    const box_cmd_meter_t *m,
    box_audio_key_t key,
    const box_cmd_note_t *notes,
    int32_t count,
    const bool *pitched,
    double from,
    double to)
{
    box_audio_pcs_t scale = box_audio_key_scale(key);
    char name[BOX_AUDIO_HARMONY_NAME];
    int32_t i, pc, total, listed, outside = 0;
    box_audio_key_name(key, name);
    ecs_strbuf_append(buf, "\noutside %s:", name);
    for (pc = 0; pc < 12 && key.tonic >= 0; pc ++) {
        if ((scale >> pc) & 1) {
            continue;
        }

        for (i = 0, total = 0, listed = 0; i < count; i ++) {
            if (!pitched[notes[i].track] || notes[i].start < from || notes[i].start >= to ||
                ((lroundf(notes[i].pitch) % 12) + 12) % 12 != pc)
            {
                continue;
            }

            if (!total) {
                ecs_strbuf_append(buf, "%s %s", outside ++ ? ";" : "",
                    box_audio_pc_name(pc, box_audio_key_flats(key)));
            }

            total ++;
            if (listed < BOX_CMD_NOTES_OUTSIDE) {
                ecs_strbuf_appendstr(buf, listed ++ ? ", " : " at ");
                box_cmd_notes_pos(buf, m, notes[i].start, true);
            }
        }

        if (total) {
            ecs_strbuf_append(buf, "%s (%d notes)", total > listed ? ", ..." : "", total);
        }
    }

    ecs_strbuf_appendstr(buf, outside ? "" : " none");
}

/** Append the header of a chords command: the bars, the pitched tracks, the drum tracks left
 * out and the span of a chord.
 *
 * @param buf The output
 * @param tracks The tracks
 * @param count Number of tracks
 * @param bars The bars
 * @param per_beat A chord per beat, else per bar
 * @param density Pitch classes per beat
 */
static void box_cmd_chords_header(
    ecs_strbuf_t *buf,
    const box_cmd_track_t *tracks,
    int32_t count,
    const box_cmd_bars_t *bars,
    bool per_beat,
    double density)
{
    int32_t i, listed = 0;
    ecs_strbuf_append(buf, "chords of bars %d..%d from", bars->first, bars->last);
    for (i = 0; i < count; i ++) {
        if (!tracks[i].drum) {
            ecs_strbuf_append(buf, "%s %s", listed ++ ? "," : "", tracks[i].path);
        }
    }

    ecs_strbuf_appendstr(buf, listed ? "" : " no pitched track");
    for (i = 0, listed = 0; i < count; i ++) {
        if (tracks[i].drum) {
            ecs_strbuf_append(buf, "%s %s", listed ++ ? "," : "; drums left out:",
                tracks[i].path);
        }
    }

    ecs_strbuf_append(buf, "; a chord per %s (%.1f pitch classes per beat)",
        per_beat ? "beat" : "bar", density);
}

char* box_cmd_audio_chords(
    ecs_world_t *world,
    int32_t argc,
    const char **argv)
{
    box_cmd_meter_t m = box_cmd_notes_meter(world);
    box_cmd_bars_t bars = {0};
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_vec_t tracks, notes;
    box_cmd_track_t *t;
    box_cmd_note_t *n;
    box_audio_key_t key;
    bool *pitched = NULL, per_beat;
    double density, from, to;
    int32_t i, count, track_count;
    char *result = NULL;
    ecs_vec_init_t(NULL, &tracks, box_cmd_track_t, 0);
    ecs_vec_init_t(NULL, &notes, box_cmd_note_t, 0);
    if (box_cmd_chords_tracks(world, argc, argv, &tracks, &bars)) {
        goto done;
    }

    t = ecs_vec_first(&tracks);
    track_count = ecs_vec_count(&tracks);
    for (i = 0; i < track_count; i ++) {
        box_cmd_notes_collect(world, t[i].e, i, &notes);
    }

    n = ecs_vec_first(&notes);
    count = ecs_vec_count(&notes);
    qsort(n, (size_t)count, sizeof(box_cmd_note_t), box_cmd_notes_compare);
    pitched = ecs_os_calloc_n(bool, track_count + 1);
    for (i = 0; i < track_count; i ++) {
        box_cmd_notes_classify(world, &t[i], n, count, i);
        pitched[i] = !t[i].drum;
    }

    box_cmd_notes_default_bars(&bars, &m, n, count);
    from = (double)(bars.first - 1) * m.bar;
    to = (double)bars.last * m.bar;
    per_beat = box_cmd_chords_per_beat(&m, &bars, n, count, pitched, &density);
    box_cmd_chords_header(&buf, t, track_count, &bars, per_beat, density);
    key = box_cmd_chords_key(NULL, "the bars", n, count, pitched, from, to);
    box_cmd_chords_list(&buf, &m, &bars, n, count, pitched, per_beat, box_audio_key_flats(key));
    box_cmd_chords_key(&buf, "the bars", n, count, pitched, from, to);
    box_cmd_chords_key(&buf, "the song", n, count, pitched, -INFINITY, INFINITY);
    box_cmd_chords_outside(&buf, &m, key, n, count, pitched, from, to);
    result = ecs_strbuf_get(&buf);
done:
    ecs_strbuf_reset(&buf);
    ecs_os_free(pitched);
    box_cmd_notes_tracks_fini(&tracks);
    ecs_vec_fini_t(NULL, &notes, box_cmd_note_t);

    return result;
}
