/**
 * @file command/cmd_audio_check.c
 * @brief audio check: a lint of the loaded song, or of the tracks of the scene, that lists the
 * problems visible in its data and in how the engine plays it, each with where it occurs.
 *
 * The static checks read the tracks, clips, notes, instruments, buses, effects and lanes. The
 * dynamic checks play the section once in the offline world of audio render and read its
 * counters, meters and master frames.
 *
 * Positions are bar.beat from 1, with the ticks of a beat (480) when off the beat, and the
 * programmed ones, before swing and humanize.
 */

#include "command.h"
#include "../audio/module.h"
#include "../audio/analyze.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/** Highest frequency a note may play: the Nyquist of 44.1 kHz, in Hz */
#define BOX_CMD_CHECK_NYQUIST (22050.0)

/** Lowest frequency a note may play to be heard, in Hz */
#define BOX_CMD_CHECK_LOWEST (20.0)

/** Largest pitch ratio of a note, the core clamps past it */
#define BOX_CMD_CHECK_RATIO_MAX (16.0)

/** Smallest pitch ratio of a note, the core clamps under it */
#define BOX_CMD_CHECK_RATIO_MIN (1.0 / 64.0)

/** Findings of one kind listed per track, the others are counted on one line */
#define BOX_CMD_CHECK_REPEAT (3)

/** Margin of a beat compared with another, for floats written in a script */
#define BOX_CMD_CHECK_EPSILON (1e-6)

/** Notes of a track the checks expand at most, so a loop of a tiny clip stays bounded */
#define BOX_CMD_CHECK_NOTES_MAX (1 << 20)

/** Ticks of a beat of the time signature in a bar.beat.tick position */
#define BOX_CMD_CHECK_TICKS (480)

/** Frames per second of the render of the dynamic checks, the rate of audio render */
#define BOX_CMD_CHECK_RATE (48000.0f)

/** Peak under which a track counts as silent, -100 dBFS */
#define BOX_CMD_CHECK_SILENT (1e-5f)

/** Level over which the limiter of the master reduces the gain, as the core has it */
#define BOX_CMD_CHECK_LIMITER (0.95f)

/** dB of gain reduction of the limiter over which it is a warning rather than a note */
#define BOX_CMD_CHECK_LIMITER_DB (6.0f)

/** Usual integrated loudness of a master, in LUFS */
#define BOX_CMD_CHECK_LUFS (-14.0f)

/** LU from BOX_CMD_CHECK_LUFS from which the loudness is noted */
#define BOX_CMD_CHECK_LUFS_RANGE (5.0f)

/** Share of real time the core may take at its peak before a warning */
#define BOX_CMD_CHECK_LOAD (0.5f)

/** How bad a finding is */
typedef enum box_cmd_check_level_t {
    BOX_CMD_CHECK_ERROR = 0,    /**< It does not play as written */
    BOX_CMD_CHECK_WARNING = 1,  /**< It plays, likely not as meant */
    BOX_CMD_CHECK_INFO = 2      /**< Worth a look, often on purpose */
} box_cmd_check_level_t;

/** An entity and its path */
typedef struct box_cmd_check_entity_t {
    ecs_entity_t e;         /**< The entity */
    char *path;             /**< Its path, owned */
} box_cmd_check_entity_t;

/** A placement of a clip on a track */
typedef struct box_cmd_check_place_t {
    ecs_entity_t e;         /**< The entity with ClipPlacement */
    ClipPlacement p;        /**< Its value, the pointers not owned */
    ecs_entity_t clip;      /**< Its clip */
    double start;           /**< First beat on the timeline */
    double end;             /**< Beat after its last one */
    bool notes;             /**< The clip is a NoteClip */
    bool audio;             /**< The clip is an AudioClip */
} box_cmd_check_place_t;

/** A note placed on the timeline */
typedef struct box_cmd_check_note_t {
    double start;           /**< Beats on the timeline */
    double end;             /**< Beats on the timeline, cut at the end of its pass */
    float pitch;            /**< MIDI note, with the transpose of the placement */
    ecs_entity_t clip;      /**< Its clip */
    int32_t index;          /**< Index of the note in its clip */
} box_cmd_check_note_t;

/** What the checks know of the sound of an instrument */
typedef struct box_cmd_check_sound_t {
    ecs_entity_t sound;     /**< The sound, 0 for none */
    bool synth;             /**< It is a SynthSound */
    bool legato;            /**< It plays as a mono voice: SynthGlide.legato */
    double frequency;       /**< Hz of its heard oscillator at the root, 0 when unknown */
    float root;             /**< Instrument.root */
    bool gate;              /**< Instrument.gate */
} box_cmd_check_sound_t;

/** A count of the findings of one kind of a track */
typedef struct box_cmd_check_cap_t {
    int32_t shown;          /**< Findings listed */
    int32_t more;           /**< Findings counted only */
} box_cmd_check_cap_t;

/** The state of a check */
typedef struct box_cmd_check_t {
    ecs_world_t *world;     /**< The world */
    int32_t beats;          /**< Beats per bar */
    double beat;            /**< Quarter notes of a beat */
    double bar;             /**< Quarter notes of a bar */
    double length;          /**< Song.beats of the loaded song, 0 for none or unknown */
    double end;             /**< Largest end of a placement */
    bool song;              /**< A song is loaded */
    bool bars;              /**< bars=a..b was given */
    double from;            /**< First beat of the bars */
    double to;              /**< Beat after the bars, INFINITY without bars */
    int32_t first_bar;      /**< First bar of the bars */
    int32_t last_bar;       /**< Last bar of the bars */
    int32_t counts[3];      /**< Findings per box_cmd_check_level_t */
    ecs_strbuf_t buf;       /**< The findings */
    ecs_vec_t named;        /**< ecs_entity_t, effects an insert slot names */
    ecs_vec_t sounding;     /**< char*, owned: paths of the tracks with notes in the bars */
} box_cmd_check_t;

/** The levels of a bus in the play of the dynamic checks */
typedef struct box_cmd_check_meter_t {
    char *path;             /**< Path of the bus, owned */
    ecs_entity_t e;         /**< The bus in the offline world, 0 when it is not there */
    bool looked;            /**< e was looked up */
    bool track;             /**< The bus is a track */
    bool notes;             /**< The track has notes in the section */
    float peak;             /**< Largest peak */
    double peak_beat;       /**< Beat of the largest peak */
    double clip_beat;       /**< First beat at which the peak was over full scale, -1 */
} box_cmd_check_meter_t;

/** What the dynamic checks read while the section plays */
typedef struct box_cmd_check_play_t {
    double from;            /**< First beat of the section */
    bool started;           /**< The section plays */
    int64_t stolen_start;   /**< Voices stolen before the section */
    int64_t refused_start;  /**< Plays refused before the section */
    int64_t stolen;         /**< Voices stolen so far */
    int64_t refused;        /**< Plays refused so far */
    double steal_beat;      /**< Beat of the first steal in the section, -1 */
    double refuse_beat;     /**< Beat of the first refusal in the section, -1 */
    int32_t voices;         /**< Peak voice count */
    double voices_beat;     /**< Beat of the peak voice count */
    float load;             /**< Peak core load */
    float master;           /**< Peak of the master before the limiter */
    double master_beat;     /**< Beat of that peak */
    box_cmd_check_meter_t *meters; /**< The buses */
    int32_t meter_count;    /**< Buses in meters */
    ecs_vec_t refused_effects; /**< char*, owned: paths of the effects the pool refused */
    box_audio_loudness_meter_t loudness; /**< Loudness of the master */
} box_cmd_check_play_t;

/** Order entities by path.
 *
 * @param a An entity
 * @param b Another entity
 * @return Negative when a comes first
 */
static int box_cmd_check_entity_cmp(
    const void *a,
    const void *b)
{
    const box_cmd_check_entity_t *x = a, *y = b;
    int result = strcmp(x->path, y->path);

    return result ? result : (x->e > y->e) - (x->e < y->e);
}

/** Collect the entities with an id that are no prefabs, sorted by path.
 *
 * @param world The world
 * @param id The id
 * @param out The entities, box_cmd_check_entity_t, appended to
 */
static void box_cmd_check_entities(
    ecs_world_t *world,
    ecs_id_t id,
    ecs_vec_t *out)
{
    int32_t i, k;
    ecs_iter_t it = ecs_each_id(world, id);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            box_cmd_check_entity_t *ent = ecs_vec_first(out);
            bool seen = ecs_has_id(world, it.entities[i], EcsPrefab);
            for (k = 0; k < ecs_vec_count(out) && !seen; k ++) {
                seen = ent[k].e == it.entities[i];
            }

            if (!seen) {
                box_cmd_check_entity_t *e = ecs_vec_append_t(NULL, out, box_cmd_check_entity_t);
                e->e = it.entities[i];
                e->path = ecs_get_path(world, it.entities[i]);
            }
        }
    }

    if (ecs_vec_count(out) > 1) {
        qsort(ecs_vec_first(out), (size_t)ecs_vec_count(out), sizeof(box_cmd_check_entity_t),
            box_cmd_check_entity_cmp);
    }
}

/** Free a list of entities.
 *
 * @param v The entities, box_cmd_check_entity_t
 */
static void box_cmd_check_entities_fini(
    ecs_vec_t *v)
{
    box_cmd_check_entity_t *e = ecs_vec_first(v);
    int32_t i;
    for (i = 0; i < ecs_vec_count(v); i ++) {
        ecs_os_free(e[i].path);
    }

    ecs_vec_fini_t(NULL, v, box_cmd_check_entity_t);
}

/** Write the position of a beat: bar.beat, with .tick when it is off the beat.
 *
 * @param c The check
 * @param beat Beats on the timeline
 * @param out Receives the text
 * @param size Size of out
 */
static void box_cmd_check_pos(
    const box_cmd_check_t *c,
    double beat,
    char *out,
    size_t size)
{
    int64_t t = (int64_t)llround(beat / c->beat * (double)BOX_CMD_CHECK_TICKS);
    int64_t per_bar = (int64_t)c->beats * BOX_CMD_CHECK_TICKS;
    int64_t bar = t >= 0 ? t / per_bar : -((-t + per_bar - 1) / per_bar);
    int64_t rem = t - bar * per_bar;
    if (rem % BOX_CMD_CHECK_TICKS) {
        snprintf(out, size, "%lld.%lld.%03lld", (long long)(bar + 1),
            (long long)(rem / BOX_CMD_CHECK_TICKS + 1),
            (long long)(rem % BOX_CMD_CHECK_TICKS));
    } else {
        snprintf(out, size, "%lld.%lld", (long long)(bar + 1),
            (long long)(rem / BOX_CMD_CHECK_TICKS + 1));
    }
}

/** Add a finding: its level, its check, where it is and what it is with a hint.
 *
 * @param c The check
 * @param level The level
 * @param check Name of the check
 * @param where Where it is
 * @param fmt Format of what it is and the hint
 * @param ... Arguments of the format
 */
static void box_cmd_check_add(
    box_cmd_check_t *c,
    box_cmd_check_level_t level,
    const char *check,
    const char *where,
    const char *fmt,
    ...)
{
    static const char *levels[] = { "error", "warning", "info" };
    va_list args;
    ecs_strbuf_append(&c->buf, "%s %s %s: ", levels[level], check, where);
    va_start(args, fmt);
    ecs_strbuf_vappend(&c->buf, fmt, args);
    va_end(args);
    ecs_strbuf_appendch(&c->buf, '\n');
    c->counts[level] ++;
}

/** Tell whether a finding of a kind of a track is listed, and count it when not.
 *
 * @param cap The count of the kind
 * @return Whether to list it
 */
static bool box_cmd_check_cap(
    box_cmd_check_cap_t *cap)
{
    bool result = cap->shown < BOX_CMD_CHECK_REPEAT;
    if (result) {
        cap->shown ++;
    } else {
        cap->more ++;
    }

    return result;
}

/** Add one finding for the findings of a kind that were counted only.
 *
 * @param c The check
 * @param cap The count of the kind
 * @param level The level
 * @param check Name of the check
 * @param where Where they are
 */
static void box_cmd_check_cap_end(
    box_cmd_check_t *c,
    const box_cmd_check_cap_t *cap,
    box_cmd_check_level_t level,
    const char *check,
    const char *where)
{
    if (cap->more) {
        box_cmd_check_add(c, level, check, where, "%d more like the ones above", cap->more);
        c->counts[level] += cap->more - 1;
    }
}

/** Tell whether a beat is in the bars of the check.
 *
 * @param c The check
 * @param beat The beat
 * @return Whether it is, always without bars
 */
static bool box_cmd_check_in(
    const box_cmd_check_t *c,
    double beat)
{
    return !c->bars || (beat >= c->from - BOX_CMD_CHECK_EPSILON &&
        beat < c->to - BOX_CMD_CHECK_EPSILON);
}

/** Find the frequency of the oscillator of a synth sound that is heard, the fundamental of
 * the sound at its root: the root layer, else its children, depth first.
 *
 * @param world The world
 * @param e The layer
 * @param depth Layers above it
 * @return Hz, 0 when no pitched layer is heard
 */
static double box_cmd_check_layer(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t depth)
{
    const SynthOscillator *osc = ecs_get(world, e, SynthOscillator);
    double result = 0;
    int32_t i;
    if (osc && osc->wave != SynthNoise && (osc->modulation == SynthMix || osc->audible) &&
        isfinite(osc->frequency) && osc->frequency > 0)
    {
        result = osc->frequency;
        goto done;
    }

    if (depth >= 2) {
        goto done;
    }

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count && !(result > 0); i ++) {
            result = box_cmd_check_layer(world, it.entities[i], depth + 1);
        }
    }

done:
    return result;
}

/** Describe the sound of an instrument.
 *
 * @param world The world
 * @param in The instrument, NULL for none
 * @param out Receives the description
 */
static void box_cmd_check_sound(
    ecs_world_t *world,
    const Instrument *in,
    box_cmd_check_sound_t *out)
{
    *out = (box_cmd_check_sound_t){ .root = 69, .gate = true };
    if (!in) {
        goto done;
    }

    out->root = isfinite(in->root) ? in->root : 69;
    out->gate = in->gate;
    if (!in->sound || !ecs_is_alive(world, in->sound)) {
        goto done;
    }

    out->sound = in->sound;
    out->synth = ecs_has(world, in->sound, SynthSound);
    if (out->synth) {
        const SynthGlide *glide = ecs_get(world, in->sound, SynthGlide);
        out->legato = glide && glide->legato;
        out->frequency = box_cmd_check_layer(world, in->sound, 0);
    }

done:
    return;
}

/** Get the beats a placement covers.
 *
 * @param p The placement
 * @param length Length of its clip, 0 when unknown
 * @param start Receives its first beat
 * @param end Receives the beat after its last one
 */
static void box_cmd_check_span(
    const ClipPlacement *p,
    double length,
    double *start,
    double *end)
{
    double offset = isfinite(p->offset) && p->offset > 0 ? p->offset : 0.0;
    double span = isfinite(p->duration) && p->duration > 0 ? p->duration : length - offset;
    *start = isfinite(p->start) ? p->start : 0.0;
    *end = *start + (span > 0 ? span : 0.0);
}

/** Order placements by start, then by entity.
 *
 * @param a A placement
 * @param b Another placement
 * @return Negative when a comes first
 */
static int box_cmd_check_place_cmp(
    const void *a,
    const void *b)
{
    const box_cmd_check_place_t *x = a, *y = b;
    int result = (x->start > y->start) - (x->start < y->start);

    return result ? result : (x->e > y->e) - (x->e < y->e);
}

/** Collect the enabled placements of a track, sorted by start.
 *
 * @param c The check
 * @param track The track
 * @param out The placements, box_cmd_check_place_t
 */
static void box_cmd_check_places(
    box_cmd_check_t *c,
    ecs_entity_t track,
    ecs_vec_t *out)
{
    ecs_world_t *world = c->world;
    int32_t i;
    ecs_iter_t it = ecs_children(world, track);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            const ClipPlacement *p = ecs_get(world, it.entities[i], ClipPlacement);
            const NoteClip *nc;
            const AudioClip *ac;
            const AutomationClip *auc;
            double length = 0;
            if (!p || ecs_has_id(world, it.entities[i], EcsDisabled) || !p->clip ||
                !ecs_is_alive(world, p->clip))
            {
                continue;
            }

            nc = ecs_get(world, p->clip, NoteClip);
            ac = ecs_get(world, p->clip, AudioClip);
            auc = ecs_get(world, p->clip, AutomationClip);
            length = nc ? nc->length : ac ? ac->length : auc ? auc->length : 0;
            length = isfinite(length) && length > 0 ? length : 0;
            box_cmd_check_place_t *out_p = ecs_vec_append_t(NULL, out, box_cmd_check_place_t);
            *out_p = (box_cmd_check_place_t){ .e = it.entities[i], .p = *p, .clip = p->clip,
                .notes = nc != NULL, .audio = ac != NULL };
            box_cmd_check_span(p, length, &out_p->start, &out_p->end);
            c->end = out_p->end > c->end ? out_p->end : c->end;
        }
    }

    if (ecs_vec_count(out) > 1) {
        qsort(ecs_vec_first(out), (size_t)ecs_vec_count(out), sizeof(box_cmd_check_place_t),
            box_cmd_check_place_cmp);
    }
}

/** Append the notes of one pass of a clip that start inside a placement.
 *
 * @param place The placement
 * @param clip The clip
 * @param base Beat of the timeline where the clip starts in the pass
 * @param pass_end Beat where the pass ends
 * @param limit Notes that start at or past this beat of the clip do not play
 * @param out The notes, box_cmd_check_note_t
 */
static void box_cmd_check_pass(
    const box_cmd_check_place_t *place,
    const NoteClip *clip,
    double base,
    double pass_end,
    double limit,
    ecs_vec_t *out)
{
    const Note *notes = ecs_vec_first(&clip->notes);
    int32_t i, count = ecs_vec_count(&clip->notes);
    float transpose = isfinite(place->p.transpose) ? place->p.transpose : 0.0f;
    double at, stop;
    for (i = 0; i < count && ecs_vec_count(out) < BOX_CMD_CHECK_NOTES_MAX; i ++) {
        if (!isfinite(notes[i].start) || !isfinite(notes[i].pitch) ||
            (double)notes[i].start >= limit)
        {
            continue;
        }

        at = base + (double)notes[i].start;
        if (at < place->start - BOX_CMD_CHECK_EPSILON ||
            at >= place->end - BOX_CMD_CHECK_EPSILON)
        {
            continue;
        }

        stop = at + (isfinite(notes[i].duration) && notes[i].duration > 0 ?
            (double)notes[i].duration : 0.0);
        stop = stop < pass_end ? stop : pass_end;
        stop = stop < place->end ? stop : place->end;
        *ecs_vec_append_t(NULL, out, box_cmd_check_note_t) = (box_cmd_check_note_t){
            .start = at, .end = stop, .pitch = notes[i].pitch + transpose,
            .clip = place->clip, .index = i };
    }
}

/** Append the notes of a placement of a NoteClip, every pass of a loop.
 *
 * @param place The placement
 * @param clip The clip
 * @param out The notes, box_cmd_check_note_t
 */
static void box_cmd_check_expand(
    const box_cmd_check_place_t *place,
    const NoteClip *clip,
    ecs_vec_t *out)
{
    double offset = isfinite(place->p.offset) && place->p.offset > 0 ? place->p.offset : 0.0;
    double length = isfinite(clip->length) && clip->length > 0 ? (double)clip->length : 0.0;
    double step, phase, base;
    int64_t k;
    if (place->p.loop && length > 0) {
        step = length > 1.0 / 64.0 ? length : 1.0 / 64.0;
        phase = fmod(offset, step);
        for (k = 0; ecs_vec_count(out) < BOX_CMD_CHECK_NOTES_MAX; k ++) {
            base = place->start - phase + (double)k * step;
            if (base >= place->end) {
                break;
            }

            box_cmd_check_pass(place, clip, base, base + step, length, out);
        }
    } else {
        box_cmd_check_pass(place, clip, place->start - offset, INFINITY, INFINITY, out);
    }
}

/** Check the notes of a clip once per track: their range on the sound, their length and their
 * velocity, at the place of the first placement of the clip.
 *
 * @param c The check
 * @param track Path of the track
 * @param sound The sound of the instrument of the track
 * @param place The first placement of the clip on the track
 * @param caps Counts of the range, length and velocity findings of the track
 */
static void box_cmd_check_clip_notes(
    box_cmd_check_t *c,
    const char *track,
    const box_cmd_check_sound_t *sound,
    const box_cmd_check_place_t *place,
    box_cmd_check_cap_t caps[3])
{
    ecs_world_t *world = c->world;
    const NoteClip *clip = ecs_get(world, place->clip, NoteClip);
    const Note *notes = ecs_vec_first(&clip->notes);
    char *cpath = ecs_get_path(world, place->clip);
    float transpose = isfinite(place->p.transpose) ? place->p.transpose : 0.0f;
    double offset = isfinite(place->p.offset) && place->p.offset > 0 ? place->p.offset : 0.0;
    char pos[48], where[512];
    int32_t i;
    for (i = 0; i < ecs_vec_count(&clip->notes); i ++) {
        const Note *n = &notes[i];
        double at = place->start - offset + (isfinite(n->start) ? n->start : 0.0);
        float pitch = n->pitch + transpose;
        double ratio, f;
        if (!isfinite(n->pitch) || !box_cmd_check_in(c, at)) {
            continue;
        }

        box_cmd_check_pos(c, at, pos, sizeof(pos));
        snprintf(where, sizeof(where), "track %s, clip %s, note %d at %s", track, cpath, i, pos);
        ratio = pow(2.0, ((double)pitch - (double)sound->root) / 12.0);
        f = sound->frequency * (ratio > BOX_CMD_CHECK_RATIO_MAX ? BOX_CMD_CHECK_RATIO_MAX :
            ratio < BOX_CMD_CHECK_RATIO_MIN ? BOX_CMD_CHECK_RATIO_MIN : ratio);
        if (sound->sound && sound->frequency > 0 && f > BOX_CMD_CHECK_NYQUIST) {
            if (box_cmd_check_cap(&caps[0])) {
                box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "note-range", where,
                    "pitch %g plays %.1f kHz, over the Nyquist of 22.05 kHz, so it aliases or "
                    "is lost; transpose it down", (double)pitch, f / 1000.0);
            }
        } else if (sound->sound && sound->frequency > 0 && f < BOX_CMD_CHECK_LOWEST) {
            if (box_cmd_check_cap(&caps[0])) {
                box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "note-range", where,
                    "pitch %g plays %.1f Hz, under the 20 Hz anyone hears; transpose it up",
                    (double)pitch, f);
            }
        } else if (sound->sound && (ratio > BOX_CMD_CHECK_RATIO_MAX ||
            ratio < BOX_CMD_CHECK_RATIO_MIN))
        {
            if (box_cmd_check_cap(&caps[0])) {
                box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "note-range", where,
                    "pitch %g is %g semitones from the root %g, past the 4 octaves up and 6 "
                    "down the core plays, so it plays clamped; transpose it or move "
                    "Instrument.root", (double)pitch, (double)(pitch - sound->root),
                    (double)sound->root);
            }
        }

        if (sound->gate && !(isfinite(n->duration) && n->duration > 0) &&
            box_cmd_check_cap(&caps[1]))
        {
            box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "note-length", where,
                "zero length, a gated instrument does not sound it; give it a duration");
        }

        if (!(isfinite(n->velocity) && n->velocity > 0) && box_cmd_check_cap(&caps[2])) {
            box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "note-velocity", where,
                "velocity 0, it does not sound; raise it or remove the note");
        }
    }

    ecs_os_free(cpath);
}

/** Order notes by pitch, then start, then clip and index.
 *
 * @param a A note
 * @param b Another note
 * @return Negative when a comes first
 */
static int box_cmd_check_pitch_cmp(
    const void *a,
    const void *b)
{
    const box_cmd_check_note_t *x = a, *y = b;
    int result = (x->pitch > y->pitch) - (x->pitch < y->pitch);
    result = result ? result : (x->start > y->start) - (x->start < y->start);
    result = result ? result : (x->clip > y->clip) - (x->clip < y->clip);

    return result ? result : (x->index > y->index) - (x->index < y->index);
}

/** Order notes by start, then pitch, then clip and index.
 *
 * @param a A note
 * @param b Another note
 * @return Negative when a comes first
 */
static int box_cmd_check_start_cmp(
    const void *a,
    const void *b)
{
    const box_cmd_check_note_t *x = a, *y = b;
    int result = (x->start > y->start) - (x->start < y->start);
    result = result ? result : (x->pitch > y->pitch) - (x->pitch < y->pitch);
    result = result ? result : (x->clip > y->clip) - (x->clip < y->clip);

    return result ? result : (x->index > y->index) - (x->index < y->index);
}

/** Write where a placed note is.
 *
 * @param c The check
 * @param track Path of the track
 * @param n The note
 * @param out Receives the text
 * @param size Size of out
 */
static void box_cmd_check_note_where(
    const box_cmd_check_t *c,
    const char *track,
    const box_cmd_check_note_t *n,
    char *out,
    size_t size)
{
    char pos[48];
    char *clip = ecs_get_path(c->world, n->clip);
    box_cmd_check_pos(c, n->start, pos, sizeof(pos));
    snprintf(out, size, "track %s, clip %s, note %d at %s", track, clip, n->index, pos);
    ecs_os_free(clip);
}

/** Check the placed notes of a track for overlaps: of one pitch, and of any pitch on a legato
 * instrument, where they glide into each other.
 *
 * @param c The check
 * @param track Path of the track
 * @param notes The notes, box_cmd_check_note_t, reordered
 * @param legato The instrument plays a mono voice
 */
static void box_cmd_check_overlaps(
    box_cmd_check_t *c,
    const char *track,
    ecs_vec_t *notes,
    bool legato)
{
    box_cmd_check_note_t *n = ecs_vec_first(notes);
    int32_t i, count = ecs_vec_count(notes), held = -1;
    box_cmd_check_cap_t same = {0}, glide = {0};
    char where[512], pos[48];
    qsort(n, (size_t)count, sizeof(box_cmd_check_note_t), box_cmd_check_pitch_cmp);
    for (i = 0; i < count; i ++) {
        if (held >= 0 && n[held].pitch == n[i].pitch &&
            n[i].start < n[held].end - BOX_CMD_CHECK_EPSILON && box_cmd_check_in(c, n[i].start))
        {
            if (box_cmd_check_cap(&same)) {
                box_cmd_check_note_where(c, track, &n[i], where, sizeof(where));
                box_cmd_check_pos(c, n[held].end, pos, sizeof(pos));
                box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "note-overlap", where,
                    "starts while note %d of the same pitch %g holds until %s, so the pitch "
                    "doubles or cuts off; shorten one of them", n[held].index,
                    (double)n[i].pitch, pos);
            }
        }

        if (held < 0 || n[held].pitch != n[i].pitch || n[i].end > n[held].end) {
            held = i;
        }
    }

    box_cmd_check_cap_end(c, &same, BOX_CMD_CHECK_WARNING, "note-overlap", track);
    if (!legato) {
        goto done;
    }

    qsort(n, (size_t)count, sizeof(box_cmd_check_note_t), box_cmd_check_start_cmp);
    held = -1;
    for (i = 0; i < count; i ++) {
        if (held >= 0 && n[i].pitch != n[held].pitch &&
            n[i].start < n[held].end - BOX_CMD_CHECK_EPSILON && box_cmd_check_in(c, n[i].start))
        {
            if (box_cmd_check_cap(&glide)) {
                box_cmd_check_note_where(c, track, &n[i], where, sizeof(where));
                box_cmd_check_add(c, BOX_CMD_CHECK_INFO, "note-legato", where,
                    "starts while note %d (pitch %g) holds on a legato instrument, so the "
                    "voice glides into it without a new attack; leave a gap for a new note",
                    n[held].index, (double)n[held].pitch);
            }
        }

        if (held < 0 || n[i].end > n[held].end) {
            held = i;
        }
    }

    box_cmd_check_cap_end(c, &glide, BOX_CMD_CHECK_INFO, "note-legato", track);
done:
    return;
}

/** Check the placed notes of a track against the end of the song, and note whether the track
 * has notes in the bars.
 *
 * @param c The check
 * @param track Path of the track
 * @param notes The notes, box_cmd_check_note_t
 */
static void box_cmd_check_late_notes(
    box_cmd_check_t *c,
    const char *track,
    const ecs_vec_t *notes)
{
    const box_cmd_check_note_t *n = ecs_vec_first(notes);
    int32_t i;
    box_cmd_check_cap_t late = {0};
    char where[512], pos[48];
    bool sounding = false;
    for (i = 0; i < ecs_vec_count(notes); i ++) {
        double to = c->bars ? c->to : (c->length > 0 ? c->length : INFINITY);
        sounding |= n[i].start >= (c->bars ? c->from : 0) - BOX_CMD_CHECK_EPSILON &&
            n[i].start < to - BOX_CMD_CHECK_EPSILON;
        if (!(c->length > 0) || n[i].start < c->length - BOX_CMD_CHECK_EPSILON ||
            !box_cmd_check_in(c, n[i].start) || !box_cmd_check_cap(&late))
        {
            continue;
        }

        box_cmd_check_note_where(c, track, &n[i], where, sizeof(where));
        box_cmd_check_pos(c, c->length, pos, sizeof(pos));
        box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "note-late", where,
            "starts after the end of the song at %s (Song.beats %g), so it never plays; move "
            "it or lengthen the song", pos, c->length);
    }

    box_cmd_check_cap_end(c, &late, BOX_CMD_CHECK_WARNING, "note-late", track);
    if (sounding) {
        *ecs_vec_append_t(NULL, &c->sounding, char*) = ecs_os_strdup(track);
    }
}

/** Check the placements of a track: clips that overlap and clips past the end of the song.
 *
 * @param c The check
 * @param track Path of the track
 * @param places The placements, box_cmd_check_place_t, sorted by start
 */
static void box_cmd_check_track_clips(
    box_cmd_check_t *c,
    const char *track,
    const ecs_vec_t *places)
{
    const box_cmd_check_place_t *p = ecs_vec_first(places);
    int32_t i, held = -1;
    box_cmd_check_cap_t overlap = {0}, late = {0};
    char where[512], pos[48], end[48];
    for (i = 0; i < ecs_vec_count(places); i ++) {
        char *clip = ecs_get_path(c->world, p[i].clip);
        box_cmd_check_pos(c, p[i].start, pos, sizeof(pos));
        snprintf(where, sizeof(where), "track %s, clip %s at %s", track, clip, pos);
        if ((p[i].notes || p[i].audio) && held >= 0 &&
            p[i].start < p[held].end - BOX_CMD_CHECK_EPSILON && box_cmd_check_in(c, p[i].start)
            && box_cmd_check_cap(&overlap))
        {
            char *other = ecs_get_path(c->world, p[held].clip);
            box_cmd_check_pos(c, p[held].end, end, sizeof(end));
            box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "clip-overlap", where,
                "starts while clip %s plays until %s, so both play at once; move or shorten "
                "one of them", other, end);
            ecs_os_free(other);
        }

        if (c->length > 0 && p[i].end > c->length + BOX_CMD_CHECK_EPSILON &&
            box_cmd_check_in(c, p[i].start) && box_cmd_check_cap(&late))
        {
            box_cmd_check_pos(c, p[i].end, end, sizeof(end));
            box_cmd_check_pos(c, c->length, pos, sizeof(pos));
            box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "clip-late", where,
                "runs until %s, past the end of the song at %s (Song.beats %g); shorten it or "
                "lengthen the song", end, pos, c->length);
        }

        if ((p[i].notes || p[i].audio) && (held < 0 || p[i].end > p[held].end)) {
            held = i;
        }

        ecs_os_free(clip);
    }

    box_cmd_check_cap_end(c, &overlap, BOX_CMD_CHECK_WARNING, "clip-overlap", track);
    box_cmd_check_cap_end(c, &late, BOX_CMD_CHECK_WARNING, "clip-late", track);
}

/** Check a track: its instrument, its clips and its notes.
 *
 * @param c The check
 * @param e The track
 * @param path Its path
 */
static void box_cmd_check_track(
    box_cmd_check_t *c,
    ecs_entity_t e,
    const char *path)
{
    ecs_world_t *world = c->world;
    const Track *tk = ecs_get(world, e, Track);
    ecs_entity_t inst = tk->instrument;
    const Instrument *in = inst && ecs_is_alive(world, inst) ?
        ecs_get(world, inst, Instrument) : NULL;
    ecs_vec_t places = {0}, notes = {0}, clips = {0};
    box_cmd_check_place_t *p;
    box_cmd_check_sound_t sound;
    box_cmd_check_cap_t caps[3] = {{0}};
    char where[512];
    int32_t i, k;
    bool has_notes = false;
    box_cmd_check_places(c, e, &places);
    p = ecs_vec_first(&places);
    for (i = 0; i < ecs_vec_count(&places); i ++) {
        has_notes |= p[i].notes;
    }

    snprintf(where, sizeof(where), "track %s", path);
    if (has_notes && !inst) {
        box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "track-instrument", where,
            "plays note clips without an instrument, so they are silent; set "
            "Track.instrument");
    } else if (has_notes && !in) {
        box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "track-instrument", where,
            "its instrument is deleted or has no Instrument, so its notes are silent; set "
            "Track.instrument");
    }

    box_cmd_check_track_clips(c, path, &places);
    box_cmd_check_sound(world, in, &sound);
    for (i = 0; i < ecs_vec_count(&places); i ++) {
        bool seen = false;
        if (!p[i].notes) {
            continue;
        }

        box_cmd_check_expand(&p[i], ecs_get(world, p[i].clip, NoteClip), &notes);
        for (k = 0; k < ecs_vec_count(&clips) && !seen; k ++) {
            seen = ecs_vec_get_t(&clips, ecs_entity_t, k)[0] == p[i].clip;
        }

        if (!seen) {
            *ecs_vec_append_t(NULL, &clips, ecs_entity_t) = p[i].clip;
            if (in) {
                box_cmd_check_clip_notes(c, path, &sound, &p[i], caps);
            }
        }
    }

    box_cmd_check_cap_end(c, &caps[0], BOX_CMD_CHECK_WARNING, "note-range", where);
    box_cmd_check_cap_end(c, &caps[1], BOX_CMD_CHECK_WARNING, "note-length", where);
    box_cmd_check_cap_end(c, &caps[2], BOX_CMD_CHECK_WARNING, "note-velocity", where);
    box_cmd_check_late_notes(c, path, &notes);
    box_cmd_check_overlaps(c, path, &notes, sound.legato);
    ecs_vec_fini_t(NULL, &places, box_cmd_check_place_t);
    ecs_vec_fini_t(NULL, &notes, box_cmd_check_note_t);
    ecs_vec_fini_t(NULL, &clips, ecs_entity_t);
}

/** Check the instruments: each needs a sound.
 *
 * @param c The check
 */
static void box_cmd_check_instruments(
    box_cmd_check_t *c)
{
    ecs_world_t *world = c->world;
    ecs_vec_t list = {0};
    int32_t i;
    char where[512];
    box_cmd_check_entities(world, ecs_id(Instrument), &list);
    box_cmd_check_entity_t *e = ecs_vec_first(&list);
    for (i = 0; i < ecs_vec_count(&list); i ++) {
        const Instrument *in = ecs_get(world, e[i].e, Instrument);
        snprintf(where, sizeof(where), "instrument %s", e[i].path);
        if (!in->sound || !ecs_is_alive(world, in->sound)) {
            box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "instrument-sound", where,
                "has no sound, so its notes are silent; set Instrument.sound");
        } else if (!ecs_has(world, in->sound, SynthSound) &&
            !ecs_has(world, in->sound, AudioPcm) && !ecs_has(world, in->sound, SoundFile))
        {
            char *sound = ecs_get_path(world, in->sound);
            box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "instrument-sound", where,
                "its sound %s has no SynthSound, AudioPcm or SoundFile, so its notes are "
                "silent; point Instrument.sound at a sound", sound);
            ecs_os_free(sound);
        }
    }

    box_cmd_check_entities_fini(&list);
}

/** Check the sends and the output of a bus, and that its output path reaches the master.
 *
 * @param c The check
 * @param e The bus
 * @param where Where the bus is
 * @param master The master
 */
static void box_cmd_check_routing(
    box_cmd_check_t *c,
    ecs_entity_t e,
    const char *where,
    ecs_entity_t master)
{
    ecs_world_t *world = c->world;
    const AudioBus *bus = ecs_get(world, e, AudioBus);
    const AudioSend *sends = ecs_vec_first(&bus->sends);
    ecs_entity_t visited[BOX_AUDIO_BUS_MAX], cur = e, next;
    int32_t j, count = 0;
    for (j = 0; j < ecs_vec_count(&bus->sends); j ++) {
        ecs_entity_t to = sends[j].bus;
        char *name = to && ecs_is_alive(world, to) ? ecs_get_path(world, to) : NULL;
        if (j >= BOX_AUDIO_SENDS) {
            box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "bus-send", where,
                "send %d is past the %d sends of a bus and is ignored; remove it", j,
                BOX_AUDIO_SENDS);
        } else if (to && !name) {
            box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "bus-send", where,
                "send %d names a deleted return, so it sends nowhere; remove the send or "
                "create the return", j);
        } else if (to && !ecs_has(world, to, AudioBus)) {
            box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "bus-send", where,
                "send %d names %s, which is no bus, so it sends nowhere; point it at a return "
                "bus", j, name);
        }

        ecs_os_free(name);
    }

    if (bus->output && !ecs_is_alive(world, bus->output)) {
        box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "bus-output", where,
            "its output names a deleted bus, so it plays on the master; set AudioBus.output");
    } else if (bus->output && bus->output != master && !ecs_has(world, bus->output, AudioBus)) {
        char *name = ecs_get_path(world, bus->output);
        box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "bus-output", where,
            "its output %s is no bus, so it plays on the master; set AudioBus.output", name);
        ecs_os_free(name);
    }

    while (count < BOX_AUDIO_BUS_MAX) {
        const AudioBus *b = ecs_get(world, cur, AudioBus);
        next = b ? b->output : 0;
        if (!next || next == master || !ecs_is_alive(world, next) ||
            !ecs_has(world, next, AudioBus))
        {
            break;
        }

        char *name = ecs_get_path(world, next);
        for (j = 0; j < count && visited[j] != next; j ++) {
        }

        if (next == e || j < count) {
            box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "bus-unreachable", where,
                "its output path loops back through %s and never reaches the master; break "
                "the cycle of AudioBus.output", name);
            count = BOX_AUDIO_BUS_MAX;
        } else if (ecs_has_id(world, next, EcsDisabled)) {
            box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "bus-unreachable", where,
                "its output path goes through the disabled bus %s, so it never reaches the "
                "master; enable it or route the bus elsewhere", name);
            count = BOX_AUDIO_BUS_MAX;
        } else {
            visited[count ++] = next;
            cur = next;
        }

        ecs_os_free(name);
    }
}

/** Check the insert slots of a bus: gaps, entities that are no effect and bypassed effects.
 *
 * @param c The check
 * @param e The bus
 * @param path Path of the bus
 * @param kind track or bus
 */
static void box_cmd_check_slots(
    box_cmd_check_t *c,
    ecs_entity_t e,
    const char *path,
    const char *kind)
{
    ecs_world_t *world = c->world;
    const AudioBus *bus = ecs_get(world, e, AudioBus);
    int32_t j, first = -1, last = -1;
    char where[512];
    for (j = 0; j < BOX_AUDIO_EFFECT_SLOTS; j ++) {
        if (bus->effects[j]) {
            first = first < 0 ? j : first;
            last = j;
        }
    }

    for (j = 0; j < BOX_AUDIO_EFFECT_SLOTS; j ++) {
        ecs_entity_t fx = bus->effects[j];
        char *name = fx && ecs_is_alive(world, fx) ? ecs_get_path(world, fx) : NULL;
        snprintf(where, sizeof(where), "%s %s, slot %d", kind, path, j);
        if (!fx && first >= 0 && j > first && j < last) {
            box_cmd_check_add(c, BOX_CMD_CHECK_INFO, "bus-slot", where,
                "empty between the effects of slots %d and %d; move the effects together",
                first, last);
        } else if (fx && !name) {
            box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "effect-missing", where,
                "names a deleted effect; clear the slot");
        } else if (fx && !box_audio_module_effect_component(world, fx)) {
            box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "effect-missing", where,
                "names %s, which has no effect component, so the slot does nothing; clear the "
                "slot or give it an effect", name);
        } else if (fx) {
            *ecs_vec_append_t(NULL, &c->named, ecs_entity_t) = fx;
            if (ecs_has_id(world, fx, EcsDisabled)) {
                snprintf(where, sizeof(where), "effect %s in slot %d of %s %s", name, j, kind,
                    path);
                box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "effect-disabled", where,
                    "is disabled, so it is bypassed; enable it or take it out of the slot");
            }
        }

        ecs_os_free(name);
    }
}

/** Check the buses: their routing and their insert slots.
 *
 * @param c The check
 */
static void box_cmd_check_buses(
    box_cmd_check_t *c)
{
    ecs_world_t *world = c->world;
    ecs_entity_t master = box_audio_module_get(world)->master;
    ecs_vec_t list = {0};
    int32_t i;
    char where[512];
    box_cmd_check_entities(world, ecs_id(AudioBus), &list);
    box_cmd_check_entity_t *e = ecs_vec_first(&list);
    for (i = 0; i < ecs_vec_count(&list); i ++) {
        const char *kind = e[i].e == master ? "master" : ecs_has(world, e[i].e, Track) ?
            "track" : "bus";
        snprintf(where, sizeof(where), "%s %s", kind, e[i].path);
        if (e[i].e != master) {
            box_cmd_check_routing(c, e[i].e, where, master);
        }

        box_cmd_check_slots(c, e[i].e, e[i].path, kind);
    }

    box_cmd_check_entities_fini(&list);
}

/** Check the effects: each needs an insert slot that names it.
 *
 * @param c The check
 */
static void box_cmd_check_effects(
    box_cmd_check_t *c)
{
    ecs_world_t *world = c->world;
    ecs_entity_t components[BOX_AUDIO_EFFECT_COMPONENTS];
    int32_t i, k, count = box_audio_module_effect_components(components);
    ecs_vec_t list = {0};
    const ecs_entity_t *named = ecs_vec_first(&c->named);
    char where[512];
    for (i = 0; i < count; i ++) {
        box_cmd_check_entities(world, components[i], &list);
    }

    box_cmd_check_entity_t *e = ecs_vec_first(&list);
    for (i = 0; i < ecs_vec_count(&list); i ++) {
        bool found = false;
        for (k = 0; k < ecs_vec_count(&c->named) && !found; k ++) {
            found = named[k] == e[i].e;
        }

        if (!found) {
            snprintf(where, sizeof(where), "effect %s", e[i].path);
            box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "effect-unused", where,
                "no insert slot of a bus names it, so it does nothing; add it to "
                "AudioBus.effects of a bus or remove it");
        }
    }

    box_cmd_check_entities_fini(&list);
}

/** Check the lanes of the song: each names a bus.
 *
 * @param c The check
 */
static void box_cmd_check_lanes(
    box_cmd_check_t *c)
{
    ecs_world_t *world = c->world;
    const Song *song = ecs_singleton_get(world, Song);
    const SongLane *lanes = song ? ecs_vec_first(&song->lanes) : NULL;
    int32_t i;
    char where[512];
    for (i = 0; song && i < ecs_vec_count(&song->lanes); i ++) {
        ecs_entity_t bus = lanes[i].bus;
        snprintf(where, sizeof(where), "song lane %d \"%s\"", i,
            lanes[i].name ? lanes[i].name : "");
        if (!bus || !ecs_is_alive(world, bus)) {
            box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "song-lane", where,
                "names no bus or a deleted one, so a host meters nothing; set SongLane.bus");
        } else if (!ecs_has(world, bus, AudioBus)) {
            char *name = ecs_get_path(world, bus);
            box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "song-lane", where,
                "names %s, which is no bus, so a host meters nothing; set SongLane.bus", name);
            ecs_os_free(name);
        }
    }
}

/** Receive the master frames of the play: measure their loudness.
 *
 * @param left The left channel
 * @param right The right channel
 * @param count Number of frames
 * @param ctx The play
 */
static void box_cmd_check_play_frames(
    const float *left,
    const float *right,
    int32_t count,
    void *ctx)
{
    box_cmd_check_play_t *p = ctx;
    box_audio_loudness_meter_add(&p->loudness, left, right, count);
}

/** Note the effects of the offline world that the pool refused.
 *
 * @param world The offline world
 * @param p The play
 */
static void box_cmd_check_play_effects(
    ecs_world_t *world,
    box_cmd_check_play_t *p)
{
    int32_t i, k;
    ecs_iter_t it = ecs_each(world, AudioEffectState);
    while (ecs_each_next(&it)) {
        const AudioEffectState *es = ecs_field(&it, AudioEffectState, 0);
        for (i = 0; i < it.count; i ++) {
            char **paths = ecs_vec_first(&p->refused_effects);
            char *path;
            bool seen = false;
            if (!es[i].refused) {
                continue;
            }

            path = ecs_get_path(world, it.entities[i]);
            for (k = 0; k < ecs_vec_count(&p->refused_effects) && !seen; k ++) {
                seen = !strcmp(paths[k], path);
            }

            if (seen) {
                ecs_os_free(path);
            } else {
                *ecs_vec_append_t(NULL, &p->refused_effects, char*) = path;
            }
        }
    }
}

/** Read the counters and the meters of the offline world after a frame of the play.
 *
 * @param world The offline world
 * @param beat Beat at the start of the frames of the frame
 * @param ctx The play
 */
static void box_cmd_check_play_frame(
    ecs_world_t *world,
    double beat,
    void *ctx)
{
    box_cmd_check_play_t *p = ctx;
    const AudioStatus *status = ecs_singleton_get(world, AudioStatus);
    box_audio_module_t *state = box_audio_module_get(world);
    AudioMeter m;
    int32_t i;
    if (!status || !state) {
        goto done;
    }

    if (beat < p->from - BOX_CMD_CHECK_EPSILON) {
        p->stolen_start = p->stolen = status->voices_stolen;
        p->refused_start = p->refused = status->voices_refused;
        goto done;
    }

    p->started = true;
    if (status->voices_stolen > p->stolen) {
        p->steal_beat = p->steal_beat < 0 ? beat : p->steal_beat;
        p->stolen = status->voices_stolen;
    }

    if (status->voices_refused > p->refused) {
        p->refuse_beat = p->refuse_beat < 0 ? beat : p->refuse_beat;
        p->refused = status->voices_refused;
    }

    if (status->voices > p->voices) {
        p->voices = status->voices;
        p->voices_beat = beat;
    }

    p->load = status->load > p->load ? status->load : p->load;
    if (box_audio_module_meter(world, state->master, &m)) {
        float peak = m.peak_left > m.peak_right ? m.peak_left : m.peak_right;
        if (peak > p->master) {
            p->master = peak;
            p->master_beat = beat;
        }
    }

    for (i = 0; i < p->meter_count; i ++) {
        box_cmd_check_meter_t *bm = &p->meters[i];
        float peak;
        if (!bm->looked) {
            bm->e = ecs_lookup(world, bm->path);
            bm->looked = true;
        }

        if (!bm->e || !box_audio_module_meter(world, bm->e, &m)) {
            continue;
        }

        peak = m.peak_left > m.peak_right ? m.peak_left : m.peak_right;
        if (peak > bm->peak) {
            bm->peak = peak;
            bm->peak_beat = beat;
        }

        if (peak > 1.0f && bm->clip_beat < 0) {
            bm->clip_beat = beat;
        }
    }

    box_cmd_check_play_effects(world, p);
done:
    return;
}

/** Set up the play: a meter for every bus of the live world but the master.
 *
 * @param c The check
 * @param p The play
 */
static void box_cmd_check_play_init(
    box_cmd_check_t *c,
    box_cmd_check_play_t *p)
{
    ecs_world_t *world = c->world;
    ecs_entity_t master = box_audio_module_get(world)->master;
    ecs_vec_t list = {0};
    char **sounding = ecs_vec_first(&c->sounding);
    int32_t i, k;
    *p = (box_cmd_check_play_t){ .from = c->from, .steal_beat = -1, .refuse_beat = -1,
        .voices_beat = c->from, .master_beat = c->from };
    box_audio_loudness_meter_init(&p->loudness, 2, BOX_CMD_CHECK_RATE);
    box_cmd_check_entities(world, ecs_id(AudioBus), &list);
    box_cmd_check_entity_t *e = ecs_vec_first(&list);
    p->meters = ecs_os_calloc_n(box_cmd_check_meter_t, ecs_vec_count(&list) + 1);
    for (i = 0; i < ecs_vec_count(&list); i ++) {
        box_cmd_check_meter_t *m;
        if (e[i].e == master) {
            continue;
        }

        m = &p->meters[p->meter_count ++];
        m->path = ecs_os_strdup(e[i].path);
        m->track = ecs_has(world, e[i].e, Track);
        m->clip_beat = -1;
        for (k = 0; k < ecs_vec_count(&c->sounding) && m->track && !m->notes; k ++) {
            m->notes = !strcmp(sounding[k], e[i].path);
        }
    }

    box_cmd_check_entities_fini(&list);
}

/** Free the play.
 *
 * @param p The play
 */
static void box_cmd_check_play_fini(
    box_cmd_check_play_t *p)
{
    char **paths = ecs_vec_first(&p->refused_effects);
    int32_t i;
    for (i = 0; i < p->meter_count; i ++) {
        ecs_os_free(p->meters[i].path);
    }

    for (i = 0; i < ecs_vec_count(&p->refused_effects); i ++) {
        ecs_os_free(paths[i]);
    }

    ecs_os_free(p->meters);
    ecs_vec_fini_t(NULL, &p->refused_effects, char*);
    box_audio_loudness_meter_fini(&p->loudness);
}

/** Add the findings of the meters of the buses of the play: clipping and silent tracks.
 *
 * @param c The check
 * @param p The play
 */
static void box_cmd_check_play_buses(
    box_cmd_check_t *c,
    const box_cmd_check_play_t *p)
{
    char where[512], pos[48];
    int32_t i;
    for (i = 0; i < p->meter_count; i ++) {
        const box_cmd_check_meter_t *m = &p->meters[i];
        const char *kind = m->track ? "track" : "bus";
        if (m->clip_beat >= 0) {
            box_cmd_check_pos(c, m->clip_beat, pos, sizeof(pos));
            snprintf(where, sizeof(where), "%s %s at %s", kind, m->path, pos);
            box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "bus-clip", where,
                "its output peaks at %+.1f dBFS before the master limiter; lower its volume "
                "or what feeds it", 20.0 * log10((double)m->peak));
        }

        if (m->track && m->notes && m->e && m->peak < BOX_CMD_CHECK_SILENT) {
            snprintf(where, sizeof(where), "track %s", m->path);
            box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "track-silent", where,
                "has notes in the section but stays silent; check its instrument, sound, "
                "levels, mute and output");
        }
    }
}

/** Add the findings of the play: voices, the effect pool, the load, the buses, the limiter
 * and the loudness.
 *
 * @param c The check
 * @param p The play
 */
static void box_cmd_check_play_report(
    box_cmd_check_t *c,
    const box_cmd_check_play_t *p)
{
    char **paths = ecs_vec_first(&p->refused_effects);
    char where[512], pos[48], peak[48];
    float lufs = box_audio_loudness_meter_integrated(&p->loudness);
    int32_t i;
    box_cmd_check_pos(c, p->voices_beat, peak, sizeof(peak));
    if (p->stolen > p->stolen_start) {
        box_cmd_check_pos(c, p->steal_beat, pos, sizeof(pos));
        snprintf(where, sizeof(where), "song at %s", pos);
        box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "voice-steal", where,
            "%lld voices stolen from here on, at the voice cap of 64 (peak %d voices at %s); "
            "use fewer or shorter notes and releases, or a higher Track.priority for the "
            "parts that must not drop", (long long)(p->stolen - p->stolen_start), p->voices,
            peak);
    }

    if (p->refused > p->refused_start) {
        box_cmd_check_pos(c, p->refuse_beat, pos, sizeof(pos));
        snprintf(where, sizeof(where), "song at %s", pos);
        box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "voice-refused", where,
            "%lld plays refused from here on: every voice at the cap had a higher priority; "
            "use fewer voices or raise Track.priority",
            (long long)(p->refused - p->refused_start));
    }

    for (i = 0; i < ecs_vec_count(&p->refused_effects); i ++) {
        snprintf(where, sizeof(where), "effect %s", paths[i]);
        box_cmd_check_add(c, BOX_CMD_CHECK_ERROR, "effect-pool", where,
            "does not run: every one of the %d effects of the pool is taken; use fewer "
            "effects, share them on a return bus", BOX_AUDIO_EFFECT_MAX);
    }

    if (p->load > BOX_CMD_CHECK_LOAD) {
        box_cmd_check_add(c, BOX_CMD_CHECK_WARNING, "core-load", "song",
            "the core takes up to %.0f%% of real time, slower devices drop out; use fewer "
            "voices, unison voices or effects", (double)p->load * 100.0);
    }

    box_cmd_check_play_buses(c, p);
    if (p->master > BOX_CMD_CHECK_LIMITER) {
        double db = 20.0 * log10((double)p->master / BOX_CMD_CHECK_LIMITER);
        box_cmd_check_pos(c, p->master_beat, pos, sizeof(pos));
        snprintf(where, sizeof(where), "master at %s", pos);
        box_cmd_check_add(c, db > BOX_CMD_CHECK_LIMITER_DB ? BOX_CMD_CHECK_WARNING :
            BOX_CMD_CHECK_INFO, "limiter", where,
            "the master limiter reduces the gain by up to %.1f dB; lower the master volume or "
            "the buses so it does not pump", db);
    }

    if (lufs > BOX_AUDIO_ANALYZE_FLOOR &&
        fabsf(lufs - BOX_CMD_CHECK_LUFS) > BOX_CMD_CHECK_LUFS_RANGE)
    {
        box_cmd_check_add(c, BOX_CMD_CHECK_INFO, "loudness", "master",
            "integrated loudness %.1f LUFS, %s than the usual -14 LUFS; %s the master volume "
            "or the buses", (double)lufs, lufs < BOX_CMD_CHECK_LUFS ? "quieter" : "louder",
            lufs < BOX_CMD_CHECK_LUFS ? "raise" : "lower");
    }
}

/** Play the bars through once offline and add the findings of the dynamic checks. When the
 * offline world does not play, such as when a sound file does not load, the summary says why
 * and the static findings stand.
 *
 * @param c The check
 * @param summary Receives what the play measured, for the summary line
 * @return 0 on success, nonzero with an error logged
 */
static int box_cmd_check_play(
    box_cmd_check_t *c,
    ecs_strbuf_t *summary)
{
    box_cmd_check_play_t play;
    box_cmd_audio_probe_t probe = { .frames = box_cmd_check_play_frames,
        .frame = box_cmd_check_play_frame, .ctx = &play };
    double to = c->bars ? c->to : (c->length > 0 ? c->length : c->end);
    char option[96], pos[48];
    const char *options[1] = { option };
    char *failed = NULL;
    float lufs;
    int result = 0;
    if (!(to > c->from)) {
        ecs_strbuf_appendstr(summary, "; nothing to play");
        goto done;
    }

    snprintf(option, sizeof(option), "beats=%.17g..%.17g", c->from, to);
    box_cmd_check_play_init(c, &play);
    result = box_cmd_audio_render_probe(c->world, 1, options, &probe, &failed);
    if (failed) {
        ecs_strbuf_append(summary, "; not played: %s (static checks only)", failed);
        ecs_os_free(failed);
        result = 0;
    } else if (!result) {
        box_cmd_check_play_report(c, &play);
        lufs = box_audio_loudness_meter_integrated(&play.loudness);
        box_cmd_check_pos(c, play.voices_beat, pos, sizeof(pos));
        ecs_strbuf_append(summary, "; played bars %d..%d: peak %d voices at %s, master peak "
            "%.1f dBFS, %.1f LUFS", (int32_t)floor(c->from / c->bar + BOX_CMD_CHECK_EPSILON) + 1,
            (int32_t)ceil(to / c->bar - BOX_CMD_CHECK_EPSILON), play.voices, pos,
            play.master > 0 ? 20.0 * log10((double)play.master) :
            (double)BOX_AUDIO_ANALYZE_FLOOR, (double)lufs);
    }

    box_cmd_check_play_fini(&play);
done:
    return result;
}

/** Parse the arguments of the check: static and bars=a..b.
 *
 * @param c The check
 * @param argc Number of arguments
 * @param argv The arguments
 * @param dynamic Receives whether the dynamic checks run
 * @return 0 on success, nonzero with an error logged
 */
static int box_cmd_check_args(
    box_cmd_check_t *c,
    int32_t argc,
    const char **argv,
    bool *dynamic)
{
    int32_t i;
    int result = -1;
    *dynamic = true;
    for (i = 0; i < argc; i ++) {
        const char *arg = argv[i] ? argv[i] : "";
        char *end = NULL;
        long a, b;
        if (!arg[0]) {
            continue;
        } else if (!ecs_os_strcmp(arg, "static")) {
            *dynamic = false;
            continue;
        } else if (ecs_os_strncmp(arg, "bars=", 5)) {
            ecs_err("invalid argument '%s', expected static or bars=a..b", arg);
            goto done;
        }

        a = strtol(arg + 5, &end, 10);
        b = a;
        if (end && end[0] == '.' && end[1] == '.') {
            const char *from = end + 2;
            b = strtol(from, &end, 10);
            end = end == from ? NULL : end;
        }

        if (!end || end == arg + 5 || end[0] || a < 1 || b < a) {
            ecs_err("invalid bars '%s', expected a..b, from bar 1 on", arg + 5);
            goto done;
        }

        c->bars = true;
        c->first_bar = (int32_t)a;
        c->last_bar = (int32_t)b;
        c->from = (double)(a - 1) * c->bar;
        c->to = (double)b * c->bar;
    }

    result = 0;
done:
    return result;
}

/** Set up a check: the time signature and the length of the song, else of the Transport.
 *
 * @param world The world
 * @param c The check
 */
static void box_cmd_check_init(
    ecs_world_t *world,
    box_cmd_check_t *c)
{
    const Song *song = ecs_singleton_get(world, Song);
    const Transport *tr = ecs_singleton_get(world, Transport);
    int32_t unit = 4;
    *c = (box_cmd_check_t){ .world = world, .beats = 4, .to = INFINITY };
    if (song && song->title) {
        c->song = true;
        c->beats = song->beats_per_bar;
        unit = song->beat_unit;
        c->length = isfinite(song->beats) && song->beats > 0 ? song->beats : 0;
    } else if (tr) {
        c->beats = tr->beats_per_bar;
        unit = tr->beat_unit;
    }

    c->beats = c->beats >= 1 && c->beats <= 64 ? c->beats : 4;
    unit = unit >= 1 && unit <= 64 ? unit : 4;
    c->beat = 4.0 / (double)unit;
    c->bar = c->beat * (double)c->beats;
}

/** Free a check.
 *
 * @param c The check
 */
static void box_cmd_check_fini(
    box_cmd_check_t *c)
{
    char **sounding = ecs_vec_first(&c->sounding);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&c->sounding); i ++) {
        ecs_os_free(sounding[i]);
    }

    ecs_vec_fini_t(NULL, &c->sounding, char*);
    ecs_vec_fini_t(NULL, &c->named, ecs_entity_t);
    ecs_strbuf_reset(&c->buf);
}

/** Append the counts of the findings.
 *
 * @param c The check
 * @param out The output
 */
static void box_cmd_check_counts(
    const box_cmd_check_t *c,
    ecs_strbuf_t *out)
{
    int32_t errors = c->counts[BOX_CMD_CHECK_ERROR], warnings = c->counts[BOX_CMD_CHECK_WARNING];
    int32_t infos = c->counts[BOX_CMD_CHECK_INFO];
    if (!errors && !warnings) {
        ecs_strbuf_appendstr(out, "no problems");
    } else {
        ecs_strbuf_append(out, "%d error%s, %d warning%s", errors, errors == 1 ? "" : "s",
            warnings, warnings == 1 ? "" : "s");
    }

    if (infos) {
        ecs_strbuf_append(out, ", %d info", infos);
    }
}

char* box_cmd_audio_check(
    ecs_world_t *world,
    int32_t argc,
    const char **argv)
{
    box_cmd_check_t c;
    ecs_strbuf_t summary = ECS_STRBUF_INIT, out = ECS_STRBUF_INIT;
    ecs_vec_t tracks = {0};
    char *result = NULL, *text;
    bool dynamic = true;
    int32_t i;
    box_cmd_check_init(world, &c);
    if (box_cmd_check_args(&c, argc, argv, &dynamic)) {
        goto done;
    }

    box_cmd_check_entities(world, ecs_id(Track), &tracks);
    box_cmd_check_entity_t *t = ecs_vec_first(&tracks);
    for (i = 0; i < ecs_vec_count(&tracks); i ++) {
        box_cmd_check_track(&c, t[i].e, t[i].path);
    }

    box_cmd_check_instruments(&c);
    box_cmd_check_buses(&c);
    box_cmd_check_effects(&c);
    box_cmd_check_lanes(&c);
    if (!dynamic) {
        ecs_strbuf_appendstr(&summary, " (static checks only)");
    } else if (!c.song && !ecs_vec_count(&tracks)) {
        ecs_strbuf_appendstr(&summary, "; no song and no tracks to play");
    } else if (box_cmd_check_play(&c, &summary)) {
        ecs_strbuf_reset(&summary);
        goto done;
    }

    text = ecs_strbuf_get(&c.buf);
    if (text) {
        ecs_strbuf_appendstr(&out, text);
        ecs_os_free(text);
    }

    box_cmd_check_counts(&c, &out);
    text = ecs_strbuf_get(&summary);
    if (text) {
        ecs_strbuf_appendstr(&out, text);
        ecs_os_free(text);
    }

    result = ecs_strbuf_get(&out);
done:
    box_cmd_check_entities_fini(&tracks);
    box_cmd_check_fini(&c);

    return result;
}
