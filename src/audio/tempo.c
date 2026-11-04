/**
 * @file audio/tempo.c
 * @brief The timeline of the Transport singleton: the position in beats mapped onto the frames
 * of the core clock, play, stop, seeks, the loop, the lookahead window and the metronome.
 *
 * The timeline is a list of anchors, linear pieces of (frame, beat) at one tempo, set at a
 * start, a seek, a new output, a tempo or loop change and every loop wrap. Each frame it covers
 * the frames from the end of the last window to the clock plus the lookahead.
 */

#include "module.h"
#include <math.h>

/** Slowest and fastest tempo in quarter notes per minute */
#define BOX_AUDIO_TEMPO_MIN 20.0
#define BOX_AUDIO_TEMPO_MAX 999.0

/** Shortest and longest lookahead in seconds */
#define BOX_AUDIO_LOOKAHEAD_MIN 0.05
#define BOX_AUDIO_LOOKAHEAD_MAX 0.5

/** Largest top and bottom of a time signature */
#define BOX_AUDIO_SIGNATURE_MAX 64

/** Volume and pitch of the click of the metronome on the first beat of a bar */
#define BOX_AUDIO_CLICK_ACCENT_VOLUME 0.8f
#define BOX_AUDIO_CLICK_ACCENT_PITCH 1.5f

/** Volume of the click on the other beats, at pitch 1 */
#define BOX_AUDIO_CLICK_VOLUME 0.4f

/** Clamp a value, a NaN to the low end.
 *
 * @param value The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static double box_audio_tempo_clamp(
    double value,
    double lo,
    double hi)
{
    double result = value;
    if (!(result >= lo)) {
        result = lo;
    } else if (result > hi) {
        result = hi;
    }

    return result;
}

/** Get a beat of a component as a finite value.
 *
 * @param beat The beat
 * @return The beat, 0 when it is not finite
 */
static double box_audio_beat_finite(
    double beat)
{
    return isfinite(beat) ? beat : 0.0;
}

/** Get the beats of a bar of a time signature: beats_per_bar * 4 / beat_unit.
 *
 * @param tr The transport
 * @return Beats of a bar
 */
static double box_audio_bar_beats(
    const Transport *tr)
{
    double top = box_audio_tempo_clamp(tr->beats_per_bar, 1, BOX_AUDIO_SIGNATURE_MAX);
    double unit = box_audio_tempo_clamp(tr->beat_unit, 1, BOX_AUDIO_SIGNATURE_MAX);

    return floor(top) * 4.0 / floor(unit);
}

int64_t box_audio_anchor_frame(
    const box_audio_anchor_t *anchor,
    double beat)
{
    return (int64_t)anchor->frame + llround((beat - anchor->beat) * anchor->frames_per_beat);
}

double box_audio_anchor_beat(
    const box_audio_anchor_t *anchor,
    uint64_t frame)
{
    return anchor->beat + (double)((int64_t)frame - (int64_t)anchor->frame) /
        anchor->frames_per_beat;
}

/** Get the frames of a beat at the tempo of the timeline.
 *
 * @param tl The timeline
 * @return rate * 60 / tempo
 */
static double box_audio_frames_per_beat(
    const box_audio_timeline_t *tl)
{
    return (double)tl->rate * 60.0 / (double)tl->tempo;
}

/** Find the piece of a frame: the last anchor at or before it, else the first.
 *
 * @param tl The timeline
 * @param frame The frame
 * @return The anchor, NULL when the timeline has none
 */
static const box_audio_anchor_t* box_audio_anchor_at(
    const box_audio_timeline_t *tl,
    uint64_t frame)
{
    const box_audio_anchor_t *anchors = ecs_vec_first(&tl->anchors);
    const box_audio_anchor_t *result = NULL;
    int32_t i, count = ecs_vec_count(&tl->anchors);
    for (i = 0; i < count; i ++) {
        if (!result || anchors[i].frame <= frame) {
            result = &anchors[i];
        }
    }

    return result;
}

/** Get the beat at a frame through the pieces of the timeline.
 *
 * @param tl The timeline
 * @param frame The frame
 * @return The beat, the position written last without a piece
 */
static double box_audio_beat_of(
    const box_audio_timeline_t *tl,
    uint64_t frame)
{
    const box_audio_anchor_t *a = box_audio_anchor_at(tl, frame);

    return a ? box_audio_anchor_beat(a, frame) : tl->written;
}

/** Check whether a piece wraps at the end of the loop: the loop is on, at least a frame long,
 * and the piece starts before its end.
 *
 * @param tl The timeline
 * @param a The piece
 * @return Whether it wraps
 */
static bool box_audio_wraps(
    const box_audio_timeline_t *tl,
    const box_audio_anchor_t *a)
{
    return tl->loop && tl->loop_end > tl->loop_start && a->beat < tl->loop_end &&
        llround((tl->loop_end - tl->loop_start) * a->frames_per_beat) >= 1;
}

/** Start a piece at a frame: the pieces at or after the frame are replaced.
 *
 * @param tl The timeline
 * @param frame The frame
 * @param beat The beat at the frame
 * @param frames_per_beat The tempo of the piece
 */
static void box_audio_anchor_push(
    box_audio_timeline_t *tl,
    uint64_t frame,
    double beat,
    double frames_per_beat)
{
    box_audio_anchor_t *anchors = ecs_vec_first(&tl->anchors);
    int32_t count = ecs_vec_count(&tl->anchors);
    while (count && anchors[count - 1].frame >= frame) {
        count --;
    }

    ecs_vec_set_count_t(NULL, &tl->anchors, box_audio_anchor_t, count);

    *ecs_vec_append_t(NULL, &tl->anchors, box_audio_anchor_t) = (box_audio_anchor_t){
        .frame = frame, .beat = beat, .frames_per_beat = frames_per_beat };
    tl->stats.anchors ++;
}

/** Anchor the timeline anew: one piece from a frame, the window starting there.
 *
 * @param tl The timeline
 * @param frame The frame, the clock
 * @param beat The beat at the frame
 */
static void box_audio_anchor_reset(
    box_audio_timeline_t *tl,
    uint64_t frame,
    double beat)
{
    ecs_vec_clear(&tl->anchors);
    box_audio_anchor_push(tl, frame, beat, box_audio_frames_per_beat(tl));
    tl->window_end = frame;
}

/** Add a segment of a piece to the window of the frame.
 *
 * @param tl The timeline
 * @param a The piece
 * @param from First frame
 * @param to Frame after the last
 * @param wrap Whether the segment ends at the wrap of the loop
 */
static void box_audio_segment_add(
    box_audio_timeline_t *tl,
    const box_audio_anchor_t *a,
    uint64_t from,
    uint64_t to,
    bool wrap)
{
    box_audio_segment_t *s;
    if (from < to) {
        s = ecs_vec_append_t(NULL, &tl->segments, box_audio_segment_t);
        *s = (box_audio_segment_t){
            .start = box_audio_anchor_beat(a, from),
            .end = wrap ? tl->loop_end : box_audio_anchor_beat(a, to),
            .frame = from,
            .frame_end = to,
            .anchor = *a
        };
    }
}

/** Extend the pieces from the end of the window to a frame, with a piece at every loop wrap,
 * and add the segments from a frame on to the window.
 *
 * @param tl The timeline
 * @param to The new end of the window
 * @param emit First frame that gets segments, UINT64_MAX for none
 */
static void box_audio_timeline_advance(
    box_audio_timeline_t *tl,
    uint64_t to,
    uint64_t emit)
{
    uint64_t f = tl->window_end, end;
    box_audio_anchor_t a;
    int64_t w;
    bool wrap;
    while (f < to) {
        a = *ecs_vec_last_t(&tl->anchors, box_audio_anchor_t);
        end = to;
        wrap = false;
        if (box_audio_wraps(tl, &a)) {
            w = box_audio_anchor_frame(&a, tl->loop_end);
            w = w < (int64_t)f ? (int64_t)f : w;
            if ((uint64_t)w <= to) {
                end = (uint64_t)w;
                wrap = true;
            }
        }

        box_audio_segment_add(tl, &a, f > emit ? f : emit, end, wrap);
        if (wrap) {
            box_audio_anchor_push(tl, end, tl->loop_start, a.frames_per_beat);
        }

        f = end;
    }

    tl->window_end = to > tl->window_end ? to : tl->window_end;
}

/** Forget the pieces before the one of the clock and the plays that started.
 *
 * @param tl The timeline
 * @param clock The clock
 */
static void box_audio_timeline_prune(
    box_audio_timeline_t *tl,
    uint64_t clock)
{
    box_audio_anchor_t *anchors = ecs_vec_first(&tl->anchors);
    box_audio_scheduled_t *s = ecs_vec_first(&tl->scheduled);
    int32_t i, first = 0, count = ecs_vec_count(&tl->anchors);
    for (i = 1; i < count; i ++) {
        if (anchors[i].frame <= clock) {
            first = i;
        }
    }

    if (first) {
        ecs_os_memmove_n(anchors, anchors + first, box_audio_anchor_t, count - first);
        ecs_vec_set_count_t(NULL, &tl->anchors, box_audio_anchor_t, count - first);
    }

    for (i = ecs_vec_count(&tl->scheduled) - 1; i >= 0; i --) {
        if (s[i].when < clock) {
            ecs_vec_remove_t(&tl->scheduled, box_audio_scheduled_t, i);
        }
    }
}

/** Cancel the plays scheduled ahead with an immediate STOP of each handle that has not
 * started, then tell the hooks.
 *
 * @param world The world
 * @param state The state
 * @param why Why
 * @param clock The clock
 */
static void box_audio_timeline_cancel(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_timeline_reset_t why,
    uint64_t clock)
{
    box_audio_timeline_t *tl = &state->timeline;
    box_audio_scheduled_t *s = ecs_vec_first(&tl->scheduled);
    int32_t i;
    for (i = 0; why != BOX_AUDIO_TIMELINE_OUTPUT && i < ecs_vec_count(&tl->scheduled); i ++) {
        if (s[i].when >= clock && box_audio_reserve(state)) {
            box_audio_write_stop(&state->writer, s[i].handle, 0);
            tl->stats.cancels ++;
        }
    }

    ecs_vec_clear(&tl->scheduled);
    for (i = 0; i < tl->hook_count; i ++) {
        if (tl->hooks[i].reset) {
            tl->hooks[i].reset(world, state, why, tl->hooks[i].ctx);
        }
    }
}

/** Take the tempo and the loop of the transport, clamped.
 *
 * @param tl The timeline
 * @param tr The transport
 * @return Whether one of them changed
 */
static bool box_audio_timeline_settings(
    box_audio_timeline_t *tl,
    const Transport *tr)
{
    float tempo = (float)box_audio_tempo_clamp(
        tr->tempo, BOX_AUDIO_TEMPO_MIN, BOX_AUDIO_TEMPO_MAX);
    double loop_start = box_audio_beat_finite(tr->loop_start);
    double loop_end = box_audio_beat_finite(tr->loop_end);
    bool result = tempo != tl->tempo || tr->loop != tl->loop ||
        loop_start != tl->loop_start || loop_end != tl->loop_end;
    tl->tempo = tempo;
    tl->loop = tr->loop;
    tl->loop_start = loop_start;
    tl->loop_end = loop_end;

    return result;
}

/** Write the position the module computed into the transport, when it differs.
 *
 * @param world The world
 * @param tl The timeline
 * @param position The position
 */
static void box_audio_timeline_write(
    ecs_world_t *world,
    box_audio_timeline_t *tl,
    double position)
{
    Transport *tr = ecs_singleton_ensure(world, Transport);
    if (tr->position != position) {
        tr->position = position;
        ecs_singleton_modified(world, Transport);
    }

    tl->written = position;
}

/** Send the click of the metronome to the output once: a short sine that decays in 30 ms.
 *
 * @param state The state
 * @return The sound id, 0 when there is none
 */
static uint32_t box_audio_click(
    box_audio_module_t *state)
{
    box_audio_timeline_t *tl = &state->timeline;
    box_audio_synth_patch_t patch = {
        .layers = 1,
        .envelope = { .attack = BOX_AUDIO_SYNTH_EDGE, .decay = 0.03f, .curve = 4.0f },
        .layer = {{ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 1600.0f, .level = 1.0f }}
    };
    uint32_t id;
    if (tl->click && tl->click_serial == state->serial) {
        goto done;
    }

    tl->click = 0;
    id = box_audio_sound_id(state);
    if (id && box_audio_reserve(state)) {
        box_audio_write_synth(&state->writer, id, &patch);
        state->sound_used[id] = 1;
        tl->click = id;
        tl->click_serial = state->serial;
    }

done:
    return tl->click;
}

/** Schedule a click on every beat of the window, counted from the start of each bar, the
 * first of a bar accented.
 *
 * @param state The state
 * @param tr The transport
 */
static void box_audio_metronome(
    box_audio_module_t *state,
    const Transport *tr)
{
    box_audio_timeline_t *tl = &state->timeline;
    const box_audio_segment_t *segs = ecs_vec_first(&tl->segments);
    double bar = box_audio_bar_beats(tr), n, k, b;
    uint32_t click = box_audio_click(state);
    int32_t i;
    int64_t when;
    bool accent;
    for (i = 0; click && i < ecs_vec_count(&tl->segments); i ++) {
        const box_audio_segment_t *s = &segs[i];
        for (n = floor(s->start / bar); n * bar < s->end; n += 1.0) {
            k = ceil(s->start - n * bar);
            for (k = k > 0 ? k : 0; k < bar && n * bar + k < s->end; k += 1.0) {
                b = n * bar + k;
                when = box_audio_anchor_frame(&s->anchor, b);
                if (b < s->start || when < 0) {
                    continue;
                }

                accent = k == 0;
                box_audio_timeline_play(state, click, 0,
                    accent ? BOX_AUDIO_CLICK_ACCENT_VOLUME : BOX_AUDIO_CLICK_VOLUME,
                    accent ? BOX_AUDIO_CLICK_ACCENT_PITCH : 1.0f, (uint64_t)when);
                tl->stats.clicks ++;
                tl->stats.accents += accent;
            }
        }
    }
}

/** Follow a start, a stop, a seek and a new output.
 *
 * @param world The world
 * @param state The state
 * @param tr The transport
 * @param clock The clock
 * @return Whether the anchor is new, so a change of the tempo or the loop is taken already
 */
static bool box_audio_timeline_follow(
    ecs_world_t *world,
    box_audio_module_t *state,
    const Transport *tr,
    uint64_t clock)
{
    box_audio_timeline_t *tl = &state->timeline;
    double position = box_audio_beat_finite(tr->position);
    bool seek = position != tl->written || position != tr->position, result = false;
    if (tl->serial != state->serial) {
        tl->serial = state->serial;
        if (tl->playing) {
            box_audio_timeline_cancel(world, state, BOX_AUDIO_TIMELINE_OUTPUT, clock);
            box_audio_anchor_reset(tl, clock, seek ? position : tl->written);
            seek = false;
            result = true;
        } else {
            ecs_vec_clear(&tl->scheduled);
        }
    }

    if (tr->playing && !tl->playing) {
        box_audio_anchor_reset(tl, clock, position);
        tl->playing = true;
        result = true;
    } else if (!tr->playing && tl->playing) {
        position = seek ? position : box_audio_beat_of(tl, clock);
        box_audio_timeline_cancel(world, state, BOX_AUDIO_TIMELINE_STOP, clock);
        ecs_vec_clear(&tl->anchors);
        tl->playing = false;
        box_audio_timeline_write(world, tl, position);
    } else if (tl->playing && seek) {
        box_audio_timeline_cancel(world, state, BOX_AUDIO_TIMELINE_SEEK, clock);
        box_audio_anchor_reset(tl, clock, position);
        result = true;
    } else if (!tl->playing) {
        tl->written = position;
    }

    return result;
}

void box_audio_timeline_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t,
    bool waiting)
{
    box_audio_timeline_t *tl = &state->timeline;
    const Transport *ptr = ecs_singleton_get(world, Transport);
    Transport tr;
    uint64_t clock, ahead;
    bool changed, anchored;
    int32_t i;
    ecs_vec_clear(&tl->segments);
    if (!ptr || !(t->sample_rate > 0)) {
        goto done;
    }

    tr = *ptr;
    clock = t->clock(t);
    tl->clock = clock;
    tl->rate = t->sample_rate;
    changed = box_audio_timeline_settings(tl, &tr);
    anchored = box_audio_timeline_follow(world, state, &tr, clock);
    if (!tl->playing) {
        goto done;
    }

    if (tl->window_end < clock) {
        box_audio_timeline_advance(tl, clock, UINT64_MAX);
    }

    if (changed && !anchored) {
        box_audio_anchor_push(tl, tl->window_end, box_audio_beat_of(tl, tl->window_end),
            box_audio_frames_per_beat(tl));
    }

    ahead = (uint64_t)llround(box_audio_tempo_clamp(tr.lookahead,
        BOX_AUDIO_LOOKAHEAD_MIN, BOX_AUDIO_LOOKAHEAD_MAX) * (double)tl->rate);
    box_audio_timeline_advance(tl, clock + ahead, clock);
    if (!waiting && tr.metronome) {
        box_audio_metronome(state, &tr);
    }

    for (i = 0; !waiting && ecs_vec_count(&tl->segments) && i < tl->hook_count; i ++) {
        if (tl->hooks[i].window) {
            tl->hooks[i].window(world, state, ecs_vec_first(&tl->segments),
                ecs_vec_count(&tl->segments), tl->hooks[i].ctx);
        }
    }

    box_audio_timeline_prune(tl, clock);
    box_audio_timeline_write(world, tl, box_audio_beat_of(tl, clock));
done:
    return;
}

uint32_t box_audio_timeline_play(
    box_audio_module_t *state,
    uint32_t sound,
    uint32_t bus,
    float volume,
    float pitch,
    uint64_t when)
{
    box_audio_scheduled_t *s;
    uint32_t result = 0;
    if (box_audio_reserve(state)) {
        result = box_audio_new_voice(state);
        box_audio_write_play(&state->writer, result, sound, bus, volume, 0.0f, pitch, 0, when);
        s = ecs_vec_append_t(NULL, &state->timeline.scheduled, box_audio_scheduled_t);
        *s = (box_audio_scheduled_t){ .handle = result, .when = when };
    }

    return result;
}

bool box_audio_timeline_hook(
    box_audio_module_t *state,
    const box_audio_timeline_hook_t *hook)
{
    box_audio_timeline_t *tl = &state->timeline;
    bool result = false;
    if (tl->hook_count < BOX_AUDIO_TIMELINE_HOOKS) {
        tl->hooks[tl->hook_count ++] = *hook;
        result = true;
    }

    return result;
}

const box_audio_segment_t* box_audio_timeline_window(
    const ecs_world_t *world,
    int32_t *count)
{
    box_audio_module_t *state = box_audio_module_get(world);
    const box_audio_segment_t *result = NULL;
    *count = 0;
    if (state && ecs_vec_count(&state->timeline.segments)) {
        *count = ecs_vec_count(&state->timeline.segments);
        result = ecs_vec_first(&state->timeline.segments);
    }

    return result;
}

double box_audio_timeline_beat(
    const ecs_world_t *world,
    uint64_t frame)
{
    box_audio_module_t *state = box_audio_module_get(world);

    return state ? box_audio_beat_of(&state->timeline, frame) : 0.0;
}

int64_t box_audio_timeline_frame(
    const ecs_world_t *world,
    double beat)
{
    box_audio_module_t *state = box_audio_module_get(world);
    const box_audio_anchor_t *a = NULL;
    int64_t result = -1, f;
    int32_t i, count;
    bool found = false;
    if (!state || !state->timeline.playing) {
        goto done;
    }

    a = ecs_vec_first(&state->timeline.anchors);
    count = ecs_vec_count(&state->timeline.anchors);
    for (i = 0; i < count; i ++) {
        if (beat < a[i].beat ||
            (i < count - 1 && beat >= box_audio_anchor_beat(&a[i], a[i + 1].frame)))
        {
            continue;
        }

        f = box_audio_anchor_frame(&a[i], beat);
        if (!found || (result < (int64_t)state->timeline.clock &&
            f >= (int64_t)state->timeline.clock))
        {
            result = f;
            found = true;
        }
    }

    if (!found && count) {
        result = box_audio_anchor_frame(&a[count - 1], beat);
    }

done:
    return result;
}

void box_audio_timeline_init(
    box_audio_module_t *state)
{
    box_audio_timeline_t *tl = &state->timeline;
    ecs_vec_init_t(NULL, &tl->anchors, box_audio_anchor_t, 0);
    ecs_vec_init_t(NULL, &tl->segments, box_audio_segment_t, 0);
    ecs_vec_init_t(NULL, &tl->scheduled, box_audio_scheduled_t, 0);
}

void box_audio_timeline_fini(
    box_audio_module_t *state)
{
    box_audio_timeline_t *tl = &state->timeline;
    ecs_vec_fini_t(NULL, &tl->anchors, box_audio_anchor_t);
    ecs_vec_fini_t(NULL, &tl->segments, box_audio_segment_t);
    ecs_vec_fini_t(NULL, &tl->scheduled, box_audio_scheduled_t);
}
