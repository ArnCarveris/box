/**
 * @file audio/sequencer.c
 * @brief Note clips and the tracks that place them on the timeline: every frame the Transport
 * plays, the placements that overlap a segment of the lookahead window schedule the notes that
 * start in it, as notes of the instrument of their track with the frame of their start.
 *
 * The module keeps the notes that sound with the beat at which each ends and sends their
 * RELEASE once that beat falls in a window, so a tempo change moves the end of a long note. A
 * stop, a seek and a new output release the notes that sound and cancel the unstarted ones.
 *
 * The placements are kept in an index sorted by start, rebuilt when a placement, a track or a
 * clip changes, so a window visits only the placements that overlap it.
 */

#include "module.h"
#include <math.h>
#include <stdlib.h>

/** Largest beat of a placement, the one of the timeline */
#define BOX_AUDIO_SEQ_BEAT_MAX 2147483648.0

/** Beats two segments may differ by at their seam and still follow each other */
#define BOX_AUDIO_SEQ_SEAM 1e-9

/** Get a beat of a component as a finite value in -2^31..2^31.
 *
 * @param beat The beat
 * @return The clamped beat, 0 when it is NaN
 */
static double box_audio_seq_beat(
    double beat)
{
    double result = beat;
    if (isnan(result)) {
        result = 0.0;
    } else if (result < -BOX_AUDIO_SEQ_BEAT_MAX) {
        result = -BOX_AUDIO_SEQ_BEAT_MAX;
    } else if (result > BOX_AUDIO_SEQ_BEAT_MAX) {
        result = BOX_AUDIO_SEQ_BEAT_MAX;
    }

    return result;
}

/** A note of a clip and its place in the clip, for a stable sort.
 */
typedef struct box_audio_seq_sort_t {
    Note note;              /**< The note */
    int32_t index;          /**< Its index in NoteClip.notes */
} box_audio_seq_sort_t;

/** Order notes by start, then by their index in the clip.
 *
 * @param a A note
 * @param b Another note
 * @return Negative when a comes first
 */
static int box_audio_seq_compare(
    const void *a,
    const void *b)
{
    const box_audio_seq_sort_t *na = a, *nb = b;
    int result = na->index < nb->index ? -1 : (na->index > nb->index);
    if (na->note.start != nb->note.start) {
        result = na->note.start < nb->note.start ? -1 : 1;
    }

    return result;
}

/** Rebuild the sorted copy of the notes of a clip from its NoteClip. Notes with a start that
 * is not finite or negative, or a pitch that is not finite, are left out.
 *
 * @param world The world
 * @param seq The sequencer
 * @param clip The copy
 */
static void box_audio_clip_build(
    const ecs_world_t *world,
    box_audio_sequencer_t *seq,
    box_audio_clip_t *clip)
{
    const NoteClip *nc = ecs_is_alive(world, clip->entity) ?
        ecs_get(world, clip->entity, NoteClip) : NULL;
    const Note *notes = nc ? ecs_vec_first(&nc->notes) : NULL;
    int32_t i, count = nc ? ecs_vec_count(&nc->notes) : 0, kept = 0;
    box_audio_seq_sort_t *sorted = count ?
        ecs_os_malloc_n(box_audio_seq_sort_t, count) : NULL;
    Note *dst;
    clip->generation = seq->generation;
    clip->length = nc && isfinite(nc->length) && nc->length > 0 ? (double)nc->length : 0.0;
    for (i = 0; i < count; i ++) {
        if (isfinite(notes[i].start) && notes[i].start >= 0 && isfinite(notes[i].pitch)) {
            sorted[kept].note = notes[i];
            sorted[kept].note.duration = isfinite(notes[i].duration) &&
                notes[i].duration > 0 ? notes[i].duration : 0.0f;
            sorted[kept].index = i;
            kept ++;
        }
    }

    if (kept > 1) {
        qsort(sorted, (size_t)kept, sizeof(box_audio_seq_sort_t), box_audio_seq_compare);
    }

    ecs_vec_set_count_t(NULL, &clip->notes, Note, kept);
    dst = ecs_vec_first(&clip->notes);
    for (i = 0; i < kept; i ++) {
        dst[i] = sorted[i].note;
    }

    ecs_os_free(sorted);
    seq->stats.clips ++;
}

/** Get the sorted copy of the notes of a clip, rebuilt when a clip changed since.
 *
 * @param world The world
 * @param seq The sequencer
 * @param entity The clip
 * @return The copy, NULL for no entity
 */
static box_audio_clip_t* box_audio_clip_get(
    const ecs_world_t *world,
    box_audio_sequencer_t *seq,
    ecs_entity_t entity)
{
    box_audio_clip_t *clip = NULL;
    if (!entity) {
        goto done;
    }

    clip = ecs_map_get_ptr(&seq->clips, entity);
    if (!clip) {
        clip = ecs_os_calloc_t(box_audio_clip_t);
        clip->entity = entity;
        clip->generation = seq->generation - 1;
        ecs_vec_init_t(NULL, &clip->notes, Note, 0);
        ecs_map_insert_ptr(&seq->clips, entity, clip);
    }

    if (clip->generation != seq->generation) {
        box_audio_clip_build(world, seq, clip);
    }

done:
    return clip;
}

/** Free the copy of a clip.
 *
 * @param clip The copy
 */
static void box_audio_clip_free(
    box_audio_clip_t *clip)
{
    ecs_vec_fini_t(NULL, &clip->notes, Note);
    ecs_os_free(clip);
}

/** Get the beats a placement lasts: its duration, or with 0 the length of its clip minus its
 * offset.
 *
 * @param p The placement
 * @param clip The sorted copy of its clip, NULL for none
 * @return Beats, at least 0
 */
static double box_audio_placement_span(
    const ClipPlacement *p,
    const box_audio_clip_t *clip)
{
    double offset = isfinite(p->offset) && p->offset > 0 ? p->offset : 0.0;
    double result = isfinite(p->duration) && p->duration > 0 ? p->duration :
        (clip ? clip->length - offset : 0.0);

    return result > 0 ? (result < BOX_AUDIO_SEQ_BEAT_MAX ? result : BOX_AUDIO_SEQ_BEAT_MAX) :
        0.0;
}

/** Order placements by start.
 *
 * @param a A placement
 * @param b Another placement
 * @return Negative when a comes first
 */
static int box_audio_placement_compare(
    const void *a,
    const void *b)
{
    const box_audio_placement_t *pa = a, *pb = b;
    int result = pa->entity < pb->entity ? -1 : (pa->entity > pb->entity);
    if (pa->start != pb->start) {
        result = pa->start < pb->start ? -1 : 1;
    }

    return result;
}

/** Rebuild the index of the placements: every ClipPlacement on a child of a Track, disabled
 * ones included, sorted by start, each with the largest end up to it.
 *
 * @param world The world
 * @param seq The sequencer
 */
static void box_audio_sequencer_index(
    ecs_world_t *world,
    box_audio_sequencer_t *seq)
{
    box_audio_placement_t *index;
    double reach = -INFINITY;
    int32_t i, count;
    ecs_iter_t it;
    if (!seq->dirty) {
        goto done;
    }

    seq->dirty = false;
    seq->stats.rebuilds ++;
    ecs_vec_clear(&seq->index);
    it = ecs_query_iter(world, seq->placements);
    while (ecs_query_next(&it)) {
        const ClipPlacement *p = ecs_field(&it, ClipPlacement, 0);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t track = ecs_get_parent(world, it.entities[i]);
            box_audio_placement_t *e;
            double start, span;
            if (!track || !ecs_has(world, track, Track)) {
                continue;
            }

            start = box_audio_seq_beat(p[i].start);
            span = box_audio_placement_span(&p[i], box_audio_clip_get(world, seq, p[i].clip));
            if (!(span > 0)) {
                continue;
            }

            e = ecs_vec_append_t(NULL, &seq->index, box_audio_placement_t);
            *e = (box_audio_placement_t){ .entity = it.entities[i], .track = track,
                .start = start, .end = start + span };
        }
    }

    index = ecs_vec_first(&seq->index);
    count = ecs_vec_count(&seq->index);
    if (count > 1) {
        qsort(index, (size_t)count, sizeof(box_audio_placement_t),
            box_audio_placement_compare);
    }

    for (i = 0; i < count; i ++) {
        reach = index[i].end > reach ? index[i].end : reach;
        index[i].reach = reach;
    }

done:
    return;
}

/** Find the first note of a clip that starts at or after a beat of the clip.
 *
 * @param clip The sorted copy
 * @param from Beat of the clip
 * @return Index of the note, the count when there is none
 */
static int32_t box_audio_clip_find(
    const box_audio_clip_t *clip,
    double from)
{
    const Note *notes = ecs_vec_first(&clip->notes);
    int32_t lo = 0, hi = ecs_vec_count(&clip->notes);
    while (lo < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        if ((double)notes[mid].start < from) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

/** Check whether a track or a placement still plays: alive, not Disabled, and a placement
 * still a child of its track.
 *
 * @param world The world
 * @param track The track
 * @param placement The placement
 * @return Whether its notes go on
 */
static bool box_audio_seq_live(
    const ecs_world_t *world,
    ecs_entity_t track,
    ecs_entity_t placement)
{
    return ecs_is_alive(world, track) && ecs_is_alive(world, placement) &&
        ecs_has(world, track, Track) && ecs_has(world, placement, ClipPlacement) &&
        !ecs_has_id(world, track, EcsDisabled) && !ecs_has_id(world, placement, EcsDisabled) &&
        ecs_get_parent(world, placement) == track;
}

/** Write the RELEASE of a sounding note.
 *
 * @param state The state
 * @param s The note
 * @param when Frame of the release, 0 for now
 */
static void box_audio_seq_release(
    box_audio_module_t *state,
    box_audio_sounding_t *s,
    uint64_t when)
{
    if (box_audio_reserve(state)) {
        box_audio_write_voice_release(&state->writer, s->handle, s->fade, when);
        state->sequencer.stats.releases ++;
    }

    s->release = when ? when : (state->timeline.clock ? state->timeline.clock : 1);
}

/** Forget a handle among the plays the timeline scheduled ahead.
 *
 * @param state The state
 * @param handle The handle
 */
static void box_audio_seq_unschedule(
    box_audio_module_t *state,
    uint32_t handle)
{
    box_audio_scheduled_t *s = ecs_vec_first(&state->timeline.scheduled);
    int32_t i;
    for (i = ecs_vec_count(&state->timeline.scheduled) - 1; i >= 0; i --) {
        if (s[i].handle == handle) {
            ecs_vec_remove_t(&state->timeline.scheduled, box_audio_scheduled_t, i);
            break;
        }
    }
}

/** Forget the notes whose release went out and passed, and end the notes of the tracks and
 * placements that were deleted or disabled: a release now for a note that sounds, a STOP for
 * one that has not started.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_seq_prune(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    uint64_t clock = state->timeline.clock;
    int32_t i;
    for (i = ecs_vec_count(&seq->sounding) - 1; i >= 0; i --) {
        box_audio_sounding_t *s = ecs_vec_get_t(&seq->sounding, box_audio_sounding_t, i);
        bool remove = s->serial != state->serial || (s->release && s->release < clock);
        if (!remove && !box_audio_seq_live(world, s->track, s->placement)) {
            if (s->start >= clock) {
                if (box_audio_reserve(state)) {
                    box_audio_write_stop(&state->writer, s->handle, 0);
                }

                box_audio_seq_unschedule(state, s->handle);
            } else if (!s->release || s->release > clock) {
                box_audio_seq_release(state, s, 0);
            }

            remove = true;
        }

        if (remove) {
            ecs_vec_remove_t(&seq->sounding, box_audio_sounding_t, i);
        }
    }
}

/** Release every sounding note without a release at a frame: after a jump of the beats of the
 * window, a loop wrap or a skip, the beats of their ends will not come.
 *
 * @param state The state
 * @param when The frame
 */
static void box_audio_seq_release_all(
    box_audio_module_t *state,
    uint64_t when)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    box_audio_sounding_t *s = ecs_vec_first(&seq->sounding);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&seq->sounding); i ++) {
        if (!s[i].release) {
            box_audio_seq_release(state, &s[i], when > s[i].start ? when : s[i].start);
        }
    }
}

/** Send the releases of the sounding notes whose end falls in a segment, at the frame of that
 * beat on the piece of the segment.
 *
 * @param state The state
 * @param seg The segment
 */
static void box_audio_seq_releases(
    box_audio_module_t *state,
    const box_audio_segment_t *seg)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    box_audio_sounding_t *s = ecs_vec_first(&seq->sounding);
    int64_t f;
    uint64_t when;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&seq->sounding); i ++) {
        if (s[i].release || s[i].end > seg->end) {
            continue;
        }

        f = box_audio_anchor_frame(&seg->anchor, s[i].end);
        when = f > (int64_t)seg->frame ? (uint64_t)f : seg->frame;
        when = when < seg->frame_end ? when : seg->frame_end;
        box_audio_seq_release(state, &s[i], when > s[i].start ? when : s[i].start);
    }
}

/** What a placement plays in a segment, gathered once per visit */
typedef struct box_audio_seq_visit_t {
    ecs_entity_t track;     /**< The track */
    ecs_entity_t placement; /**< The placement */
    ecs_entity_t instrument; /**< The instrument of the track */
    const Instrument *inst; /**< Its Instrument */
    const ClipPlacement *p; /**< The placement */
    const box_audio_clip_t *clip; /**< The sorted notes of its clip */
    const box_audio_segment_t *seg; /**< The segment */
    double lo;              /**< First beat of the timeline it schedules */
    double hi;              /**< Beat after the last */
    double end;             /**< End of the placement */
    double loop_end;        /**< loop_end of a transport that wraps, INFINITY otherwise */
    uint8_t bus;            /**< Bus index of the track */
} box_audio_seq_visit_t;

/** Schedule the notes of one pass of a clip that start in [lo, hi) of a visit.
 *
 * @param world The world
 * @param state The state
 * @param v The visit
 * @param base Beat of the timeline at which the pass starts its clip
 * @param pass_end Beat at which the pass ends, INFINITY for no end
 * @param limit Beat of the clip at which the notes of the pass stop, INFINITY for none
 */
static void box_audio_seq_pass(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_seq_visit_t *v,
    double base,
    double pass_end,
    double limit)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    const Note *notes = ecs_vec_first(&v->clip->notes);
    int32_t i = box_audio_clip_find(v->clip, v->lo - base - BOX_AUDIO_SEQ_SEAM);
    int32_t count = ecs_vec_count(&v->clip->notes);
    float transpose = isfinite(v->p->transpose) ? v->p->transpose : 0.0f;
    box_audio_scheduled_t *sch;
    box_audio_sounding_t *s;
    double beat, end;
    int64_t f;
    uint64_t when;
    uint32_t handle;
    for (; i < count; i ++) {
        const Note *n = &notes[i];
        beat = base + (double)n->start;
        if (beat >= v->hi || (double)n->start >= limit) {
            break;
        }

        if (beat < v->lo) {
            continue;
        }

        f = box_audio_anchor_frame(&v->seg->anchor, beat);
        if (f < 0) {
            continue;
        }

        when = (uint64_t)f > v->seg->frame ? (uint64_t)f : v->seg->frame;
        end = beat + (double)n->duration;
        end = end < pass_end ? end : pass_end;
        end = end < v->end ? end : v->end;
        end = beat < v->loop_end && end > v->loop_end ? v->loop_end : end;
        handle = box_audio_instrument_voice(world, state, v->instrument, n->pitch + transpose,
            n->velocity, v->p->gain, n->pan, when, 0, v->bus, 0, false);
        if (!handle) {
            seq->stats.dropped ++;
            continue;
        }

        seq->stats.notes ++;
        sch = ecs_vec_append_t(NULL, &state->timeline.scheduled, box_audio_scheduled_t);
        *sch = (box_audio_scheduled_t){ .handle = handle, .when = when };
        if (v->inst->gate) {
            s = ecs_vec_append_t(NULL, &seq->sounding, box_audio_sounding_t);
            *s = (box_audio_sounding_t){
                .handle = handle,
                .serial = state->serial,
                .start = when,
                .end = end,
                .fade = v->inst->release,
                .pitch = n->pitch + transpose,
                .track = v->track,
                .placement = v->placement
            };
        }
    }
}

/** Check that the instrument of a track can play, and log once per track when it cannot.
 *
 * @param world The world
 * @param state The state
 * @param track The track
 * @param instrument Receives the instrument entity
 * @return Its Instrument, NULL when its sound is not ready
 */
static const Instrument* box_audio_seq_instrument(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t track,
    ecs_entity_t *instrument)
{
    const Track *tk = ecs_get(world, track, Track);
    ecs_entity_t e = tk ? tk->instrument : 0;
    const Instrument *result = e && ecs_is_alive(world, e) ? ecs_get(world, e, Instrument) :
        NULL;
    AudioTrackState *ts = ecs_get_mut(world, track, AudioTrackState);
    if (result && !box_audio_sound_of(world, state, result->sound)) {
        result = NULL;
    }

    if (!result && ts && !ts->warned) {
        char *path = ecs_get_path(world, track);
        ecs_warn("box.audio: the instrument of track '%s' cannot play (%s), its notes are "
            "dropped", path, !e || !ecs_is_alive(world, e) || !ecs_has(world, e, Instrument) ?
                "no Instrument" : "its sound is not ready");
        ecs_os_free(path);
        ts->warned = true;
    } else if (result && ts) {
        ts->warned = false;
    }

    *instrument = e;

    return result;
}

/** Schedule the notes of a placement that start in a segment.
 *
 * @param world The world
 * @param state The state
 * @param pl The placement in the index
 * @param seg The segment
 */
static void box_audio_seq_visit(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_placement_t *pl,
    const box_audio_segment_t *seg)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    const box_audio_timeline_t *tl = &state->timeline;
    box_audio_seq_visit_t v = { .track = pl->track, .placement = pl->entity, .seg = seg };
    double offset, length, phase, base;
    int64_t k;
    seq->stats.visits ++;
    if (!box_audio_seq_live(world, pl->track, pl->entity)) {
        seq->dirty |= ecs_is_alive(world, pl->entity) &&
            ecs_get_parent(world, pl->entity) != pl->track;
        goto done;
    }

    v.p = ecs_get(world, pl->entity, ClipPlacement);
    v.clip = box_audio_clip_get(world, seq, v.p->clip);
    if (!v.clip || !ecs_vec_count(&v.clip->notes)) {
        goto done;
    }

    v.inst = box_audio_seq_instrument(world, state, pl->track, &v.instrument);
    if (!v.inst) {
        goto done;
    }

    v.lo = seg->start > pl->start ? seg->start : pl->start;
    v.hi = seg->end < pl->end ? seg->end : pl->end;
    v.end = pl->end;
    v.loop_end = tl->loop && tl->loop_end > tl->loop_start ? tl->loop_end : INFINITY;
    v.bus = box_audio_bus_of(world, state, pl->track);
    if (!(v.lo < v.hi)) {
        goto done;
    }

    offset = isfinite(v.p->offset) && v.p->offset > 0 ? v.p->offset : 0.0;
    length = v.clip->length;
    if (v.p->loop && length > 0) {
        length = length > BOX_AUDIO_CLIP_LENGTH_MIN ? length : BOX_AUDIO_CLIP_LENGTH_MIN;
        phase = fmod(offset, length);
        k = (int64_t)floor((v.lo - (pl->start - phase)) / length);
        for (k = k > 0 ? k : 0; ; k ++) {
            base = pl->start - phase + (double)k * length;
            if (base >= v.hi) {
                break;
            }

            box_audio_seq_pass(world, state, &v, base, base + length, v.clip->length);
        }
    } else {
        box_audio_seq_pass(world, state, &v, pl->start - offset, INFINITY, INFINITY);
    }

done:
    return;
}

/** The window hook of the timeline: for each segment, schedule the notes of the placements
 * that overlap it, then send the releases of the notes that end in it.
 *
 * @param world The world
 * @param state The state
 * @param segments The segments of the window
 * @param count Number of segments
 * @param ctx Unused
 */
static void box_audio_sequencer_window(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_segment_t *segments,
    int32_t count,
    void *ctx)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    const box_audio_placement_t *index;
    int32_t i, k, lo, hi, n;
    (void)ctx;
    box_audio_sequencer_index(world, seq);
    box_audio_seq_prune(world, state);
    index = ecs_vec_first(&seq->index);
    n = ecs_vec_count(&seq->index);
    if (!n && !ecs_vec_count(&seq->sounding)) {
        seq->expect = segments[count - 1].end;
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const box_audio_segment_t *seg = &segments[i];
        if (!isnan(seq->expect) && fabs(seg->start - seq->expect) > BOX_AUDIO_SEQ_SEAM) {
            box_audio_seq_release_all(state, seg->frame);
        }

        lo = 0;
        hi = n;
        while (lo < hi) {
            int32_t mid = lo + (hi - lo) / 2;
            if (index[mid].reach <= seg->start) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        for (k = lo; k < n && index[k].start < seg->end; k ++) {
            if (index[k].end > seg->start) {
                box_audio_seq_visit(world, state, &index[k], seg);
            }
        }

        box_audio_seq_releases(state, seg);
        seq->expect = seg->end;
    }

done:
    return;
}

/** The reset hook of the timeline: on a stop and a seek release the notes that sound at once;
 * the timeline stopped the ones that have not started. A new output knows none of them.
 *
 * @param world The world
 * @param state The state
 * @param why Why
 * @param ctx Unused
 */
static void box_audio_sequencer_reset(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_timeline_reset_t why,
    void *ctx)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    box_audio_sounding_t *s = ecs_vec_first(&seq->sounding);
    uint64_t clock = state->timeline.clock;
    int32_t i;
    (void)world;
    (void)ctx;
    for (i = 0; why != BOX_AUDIO_TIMELINE_OUTPUT && i < ecs_vec_count(&seq->sounding); i ++) {
        if (s[i].serial == state->serial && s[i].start < clock &&
            (!s[i].release || s[i].release > clock))
        {
            box_audio_seq_release(state, &s[i], 0);
        }
    }

    ecs_vec_clear(&seq->sounding);
    seq->expect = NAN;
}

/** Mark the index of the placements for a rebuild when a placement or a track changes.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioPlacementChange(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    if (!ecs_is_fini(it->world)) {
        state->sequencer.dirty = true;
    }
}

/** Mark the sorted notes of every clip for a rebuild when a clip changes, and forget the copy
 * of a clip that is removed.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioClipChange(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    box_audio_sequencer_t *seq = &state->sequencer;
    box_audio_clip_t *clip;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    seq->generation ++;
    seq->dirty = true;
    for (i = 0; it->event == EcsOnRemove && i < it->count; i ++) {
        clip = ecs_map_get_ptr(&seq->clips, it->entities[i]);
        if (clip) {
            ecs_map_remove(&seq->clips, it->entities[i]);
            box_audio_clip_free(clip);
        }
    }

done:
    return;
}

void box_audio_sequencer_init(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    box_audio_timeline_hook_t hook = {
        .window = box_audio_sequencer_window,
        .reset = box_audio_sequencer_reset
    };
    ecs_vec_init_t(NULL, &seq->index, box_audio_placement_t, 0);
    ecs_vec_init_t(NULL, &seq->sounding, box_audio_sounding_t, 0);
    ecs_map_init(&seq->clips, NULL);
    seq->dirty = true;
    seq->expect = NAN;
    seq->placements = ecs_query(world, {
        .terms = {{ .id = ecs_id(ClipPlacement), .inout = EcsIn, .src.id = EcsSelf }},
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheNone
    });

    box_audio_timeline_hook(state, &hook);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioPlacementChange" }),
        .query.flags = EcsQueryMatchDisabled,
        .query.terms = {{ .id = ecs_id(ClipPlacement), .src.id = EcsSelf }},
        .events = { EcsOnAdd, EcsOnSet, EcsOnRemove },
        .callback = BoxAudioPlacementChange,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioTrackChange" }),
        .query.flags = EcsQueryMatchDisabled,
        .query.terms = {{ .id = ecs_id(Track), .src.id = EcsSelf }},
        .events = { EcsOnAdd, EcsOnSet, EcsOnRemove },
        .callback = BoxAudioPlacementChange,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioClipChange" }),
        .query.flags = EcsQueryMatchDisabled | EcsQueryMatchPrefab,
        .query.terms = {{ .id = ecs_id(NoteClip), .src.id = EcsSelf }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BoxAudioClipChange,
        .ctx = state
    });
}

void box_audio_sequencer_fini(
    box_audio_module_t *state)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    ecs_map_iter_t it = ecs_map_iter(&seq->clips);
    while (ecs_map_next(&it)) {
        box_audio_clip_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&seq->clips);
    ecs_vec_fini_t(NULL, &seq->index, box_audio_placement_t);
    ecs_vec_fini_t(NULL, &seq->sounding, box_audio_sounding_t);
}
