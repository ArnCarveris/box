/**
 * @file audio/sequencer.c
 * @brief Note clips, audio clips and the tracks that place them on the timeline: every frame
 * the Transport plays, the placements that overlap a segment of the lookahead window schedule
 * the notes that start in it, as notes of the instrument of their track with the frame of
 * their start, and the passes of their audio clips, as plays of the sound on the bus of the
 * track with the frame of their start and, when they start in the middle, a POSITION on it.
 *
 * The module keeps the notes that sound with the beat at which each ends and sends their
 * RELEASE once that beat falls in a window, so a tempo change moves the end of a long note; a
 * voice of an audio clip gets its STOP the same way, or on the frame where its sound ends.
 *
 * A stop, a seek and a new output release the notes, stop the voices and cancel the unstarted
 * ones and the ramps of the expression of notes; a jump starts the passes of audio clips that
 * cover the new beat in their middle. The placements are in an index sorted by start.
 */

#include "module.h"
#include <math.h>
#include <stdlib.h>

/** Largest beat of a placement, the one of the timeline */
#define BOX_AUDIO_SEQ_BEAT_MAX 2147483648.0

/** Beats two segments may differ by at their seam and still follow each other */
#define BOX_AUDIO_SEQ_SEAM 1e-9

/** Largest swing of a track */
#define BOX_AUDIO_SEQ_SWING_MAX 0.75

/** Largest humanized timing of a track, in milliseconds */
#define BOX_AUDIO_SEQ_TIMING_MAX 50.0

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
    bool alive = ecs_is_alive(world, clip->entity);
    const NoteClip *nc = alive ? ecs_get(world, clip->entity, NoteClip) : NULL;
    const AudioClip *ac = alive ? ecs_get(world, clip->entity, AudioClip) : NULL;
    const Note *notes = nc ? ecs_vec_first(&nc->notes) : NULL;
    int32_t i, count = nc ? ecs_vec_count(&nc->notes) : 0, kept = 0;
    box_audio_seq_sort_t *sorted = count ?
        ecs_os_malloc_n(box_audio_seq_sort_t, count) : NULL;
    Note *dst;
    clip->generation = seq->generation;
    clip->length = nc && isfinite(nc->length) && nc->length > 0 ? (double)nc->length : 0.0;
    clip->sound = ac ? ac->sound : 0;
    clip->offset = ac && isfinite(ac->offset) && ac->offset > 0 ? (double)ac->offset : 0.0;
    if (ac && !nc) {
        clip->length = isfinite(ac->length) && ac->length > 0 ? (double)ac->length : 0.0;
    }

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

    dst = ecs_vec_first(&clip->notes);
    for (i = 0; i < ecs_vec_count(&clip->notes); i ++) {
        box_audio_note_fini(&dst[i]);
    }

    ecs_vec_set_count_t(NULL, &clip->notes, Note, kept);
    dst = ecs_vec_first(&clip->notes);
    for (i = 0; i < kept; i ++) {
        dst[i] = sorted[i].note;
        box_audio_note_dup(&dst[i]);
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
    Note *notes = ecs_vec_first(&clip->notes);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&clip->notes); i ++) {
        box_audio_note_fini(&notes[i]);
    }

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

/** Order placements by entity.
 *
 * @param a A placement
 * @param b Another placement
 * @return Negative when a comes first
 */
static int box_audio_placement_compare_entity(
    const void *a,
    const void *b)
{
    const box_audio_placement_t *pa = a, *pb = b;

    return pa->entity < pb->entity ? -1 : (pa->entity > pb->entity);
}

/** Rebuild the index of the placements: every ClipPlacement on a child of a Track, disabled
 * ones included, sorted by start, each with the largest end up to it and the beat up to which
 * it was scanned before the rebuild.
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
    count = ecs_vec_count(&seq->index);
    ecs_vec_set_count_t(NULL, &seq->scans, box_audio_placement_t, count);
    if (count) {
        memcpy(ecs_vec_first(&seq->scans), ecs_vec_first(&seq->index),
            sizeof(box_audio_placement_t) * (size_t)count);
        qsort(ecs_vec_first(&seq->scans), (size_t)count, sizeof(box_audio_placement_t),
            box_audio_placement_compare_entity);
    }

    ecs_vec_clear(&seq->index);
    it = ecs_query_iter(world, seq->placements);
    while (ecs_query_next(&it)) {
        const ClipPlacement *p = ecs_field(&it, ClipPlacement, 0);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t track = ecs_get_parent(world, it.entities[i]);
            const box_audio_placement_t *prev;
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
                .start = start, .end = start + span, .scanned = -INFINITY };
            prev = bsearch(e, ecs_vec_first(&seq->scans), (size_t)ecs_vec_count(&seq->scans),
                sizeof(box_audio_placement_t), box_audio_placement_compare_entity);
            if (prev) {
                e->scanned = prev->scanned;
            }
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

/** Remember that a ramp of the expression of a note was sent, up to its frame.
 *
 * @param state The state
 * @param r The ramp
 * @param when Frame it was sent for
 */
static void box_audio_seq_expressed(
    box_audio_module_t *state,
    const box_audio_seq_ramp_t *r,
    uint64_t when)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    box_audio_seq_expressed_t *e = ecs_vec_first(&seq->expressed);
    int32_t i;
    for (i = ecs_vec_count(&seq->expressed) - 1; i >= 0; i --) {
        if (e[i].handle == r->handle && e[i].serial == r->serial) {
            e[i].last = when > e[i].last ? when : e[i].last;
            goto done;
        }
    }

    e = ecs_vec_append_t(NULL, &seq->expressed, box_audio_seq_expressed_t);
    *e = (box_audio_seq_expressed_t){ .handle = r->handle, .serial = r->serial, .last = when,
        .track = r->track, .placement = r->placement };

done:
    return;
}

/** Write a VOICE_RAMP of the expression of a note.
 *
 * @param state The state
 * @param handle The voice
 * @param param The parameter
 * @param value Value at the end of the ramp
 * @param frames Frames of the ramp, 0 to jump
 * @param when Frame it starts at
 * @return Whether it was written
 */
static bool box_audio_seq_ramp(
    box_audio_module_t *state,
    uint32_t handle,
    uint32_t param,
    float value,
    uint32_t frames,
    uint64_t when)
{
    bool result = false;
    if (box_audio_reserve(state)) {
        box_audio_write_voice_ramp(&state->writer, handle, (box_audio_param_t)param, value,
            frames, when);
        state->sequencer.stats.ramps ++;
        result = true;
    }

    return result;
}

/** Write the CANCEL of the ramps of a voice that the core holds for later frames.
 *
 * @param state The state
 * @param handle The voice
 */
static void box_audio_seq_ramp_cancel(
    box_audio_module_t *state,
    uint32_t handle)
{
    if (box_audio_reserve(state)) {
        box_audio_write_cancel(&state->writer, BOX_AUDIO_CMD_VOICE_RAMP, handle, 0);
        state->sequencer.stats.ramp_cancels ++;
    }
}

/** Drop the ramps of the expression of notes that were not sent yet.
 *
 * @param state The state
 */
static void box_audio_seq_ramps_drop(
    box_audio_module_t *state)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    seq->stats.ramps_dropped += ecs_vec_count(&seq->ramps);
    ecs_vec_clear(&seq->ramps);
}

/** Forget the ramps of the notes of an old output, drop those of the tracks and placements
 * that were deleted or disabled and cancel the ones the core holds for them, and forget the
 * notes whose ramps all applied.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_seq_prune_ramps(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    box_audio_seq_ramp_t *r = ecs_vec_first(&seq->ramps);
    box_audio_seq_expressed_t *e;
    ecs_entity_t track = 0, placement = 0;
    uint64_t clock = state->timeline.clock;
    int32_t i, kept = 0, count = ecs_vec_count(&seq->ramps);
    bool live = false;
    for (i = 0; i < count; i ++) {
        if (r[i].track != track || r[i].placement != placement) {
            track = r[i].track;
            placement = r[i].placement;
            live = box_audio_seq_live(world, track, placement);
        }

        if (r[i].serial == state->serial && live) {
            r[kept ++] = r[i];
        }
    }

    seq->stats.ramps_dropped += count - kept;
    ecs_vec_set_count_t(NULL, &seq->ramps, box_audio_seq_ramp_t, kept);

    e = ecs_vec_first(&seq->expressed);
    for (i = ecs_vec_count(&seq->expressed) - 1; i >= 0; i --) {
        bool remove = e[i].serial != state->serial || e[i].last < clock;
        if (!remove && !box_audio_seq_live(world, e[i].track, e[i].placement)) {
            box_audio_seq_ramp_cancel(state, e[i].handle);
            remove = true;
        }

        if (remove) {
            ecs_vec_remove_t(&seq->expressed, box_audio_seq_expressed_t, i);
        }
    }
}

/** Send the ramps of the expression of notes that start in a segment, at the frame of their
 * beat on the piece of the segment, no earlier than the start of their note.
 *
 * @param state The state
 * @param seg The segment
 */
static void box_audio_seq_ramps(
    box_audio_module_t *state,
    const box_audio_segment_t *seg)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    box_audio_seq_ramp_t *r = ecs_vec_first(&seq->ramps);
    int32_t i, kept = 0, count = ecs_vec_count(&seq->ramps);
    double frames;
    uint64_t when;
    int64_t f;
    for (i = 0; i < count; i ++) {
        if (r[i].beat >= seg->end) {
            r[kept ++] = r[i];
            continue;
        }

        f = box_audio_anchor_frame(&seg->anchor, r[i].beat);
        when = f > (int64_t)seg->frame ? (uint64_t)f : seg->frame;
        when = when > r[i].first ? when : r[i].first;
        frames = round((r[i].end - r[i].beat) * seg->anchor.frames_per_beat);
        frames = frames > 0 ? (frames < (double)INT32_MAX ? frames : (double)INT32_MAX) : 0.0;
        if (box_audio_seq_ramp(state, r[i].handle, r[i].param, r[i].value, (uint32_t)frames,
            when))
        {
            box_audio_seq_expressed(state, &r[i], when + (uint64_t)frames);
        } else {
            seq->stats.ramps_dropped ++;
        }
    }

    ecs_vec_set_count_t(NULL, &seq->ramps, box_audio_seq_ramp_t, kept);
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

/** Write the STOP of a voice of an audio clip.
 *
 * @param state The state
 * @param cv The voice
 * @param when Frame of the stop, 0 for now
 */
static void box_audio_seq_stop(
    box_audio_module_t *state,
    box_audio_clip_voice_t *cv,
    uint64_t when)
{
    if (box_audio_reserve(state)) {
        box_audio_write_stop(&state->writer, cv->handle, when);
        state->sequencer.stats.stops ++;
    }

    cv->stop = when ? when : (state->timeline.clock ? state->timeline.clock : 1);
}

/** Forget the voices of audio clips whose stop went out and passed, and stop the voices of
 * the tracks and placements that were deleted or disabled at once.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_seq_prune_voices(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    uint64_t clock = state->timeline.clock;
    int32_t i;
    for (i = ecs_vec_count(&seq->voices) - 1; i >= 0; i --) {
        box_audio_clip_voice_t *cv = ecs_vec_get_t(&seq->voices, box_audio_clip_voice_t, i);
        bool remove = cv->serial != state->serial || (cv->stop && cv->stop < clock);
        if (!remove && !box_audio_seq_live(world, cv->track, cv->placement)) {
            if (!cv->stop || cv->stop > clock) {
                box_audio_seq_stop(state, cv, 0);
            }

            box_audio_seq_unschedule(state, cv->handle);
            remove = true;
        }

        if (remove) {
            ecs_vec_remove_t(&seq->voices, box_audio_clip_voice_t, i);
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

    box_audio_seq_prune_voices(world, state);
    box_audio_seq_prune_ramps(world, state);
}

/** Release every sounding note without a release and stop every voice of an audio clip
 * without a stop at a frame: after a jump of the beats of the window, a loop wrap or a skip,
 * the beats of their ends will not come.
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
    box_audio_clip_voice_t *cv = ecs_vec_first(&seq->voices);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&seq->sounding); i ++) {
        if (!s[i].release) {
            box_audio_seq_release(state, &s[i], when > s[i].start ? when : s[i].start);
        }
    }

    for (i = 0; i < ecs_vec_count(&seq->voices); i ++) {
        if (!cv[i].stop) {
            box_audio_seq_stop(state, &cv[i], when > cv[i].start ? when : cv[i].start);
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

/** Send the stops of the voices of audio clips whose end beat falls in a segment, at the frame
 * of that beat on the piece of the segment, or whose sound ends in it, at that frame.
 *
 * @param state The state
 * @param seg The segment
 */
static void box_audio_seq_stops(
    box_audio_module_t *state,
    const box_audio_segment_t *seg)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    box_audio_clip_voice_t *cv = ecs_vec_first(&seq->voices);
    int64_t f;
    uint64_t when;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&seq->voices); i ++) {
        if (cv[i].stop) {
            continue;
        }

        when = UINT64_MAX;
        if (cv[i].end <= seg->end) {
            f = box_audio_anchor_frame(&seg->anchor, cv[i].end);
            when = f > (int64_t)seg->frame ? (uint64_t)f : seg->frame;
            when = when < seg->frame_end ? when : seg->frame_end;
        }

        if (cv[i].sound_end < seg->frame_end && cv[i].sound_end < when) {
            when = cv[i].sound_end > seg->frame ? cv[i].sound_end : seg->frame;
        }

        if (when != UINT64_MAX) {
            box_audio_seq_stop(state, &cv[i], when > cv[i].start ? when : cv[i].start);
        }
    }
}

/** Get the priority of the voices of a track, clamped to 0..255.
 *
 * @param world The world
 * @param track The track
 * @return The priority
 */
static uint8_t box_audio_track_priority(
    ecs_world_t *world,
    ecs_entity_t track)
{
    const Track *tk = ecs_get(world, track, Track);
    int32_t priority = tk ? tk->priority : BOX_AUDIO_TRACK_PRIORITY;

    return (uint8_t)(priority > 0 ? (priority < 255 ? priority : 255) : 0);
}

/** Get the frames a note start of a track moves at most either way.
 *
 * @param tk The Track, NULL for none
 * @param rate Frames per second of the timeline
 * @return The frames, 0 when its timing is not humanized
 */
static double box_audio_seq_timing(
    const Track *tk,
    double rate)
{
    double result = 0.0;
    if (tk && isfinite(tk->humanize.timing) && tk->humanize.timing > 0) {
        result = (tk->humanize.timing < BOX_AUDIO_SEQ_TIMING_MAX ? tk->humanize.timing :
            BOX_AUDIO_SEQ_TIMING_MAX) / 1000.0 * rate;
    }

    return result;
}

/** Swing a beat of the timeline: within each pair of steps from beat 0 the first step
 * stretches by 1 + swing and the second shrinks by 1 - swing, so the off-beat moves later by
 * swing * step and the downbeats stay put.
 *
 * @param beat The beat
 * @param swing The swing, 0..0.75
 * @param step Beats of a step of the grid
 * @return The swung beat
 */
static double box_audio_seq_swing(
    double beat,
    double swing,
    double step)
{
    double pair = 2.0 * step, origin = floor(beat / pair) * pair, p = beat - origin;
    p = p < step ? p * (1.0 + swing) : step * (1.0 + swing) + (p - step) * (1.0 - swing);

    return origin + p;
}

/** Mix a value into a hash, splitmix64.
 *
 * @param x The value
 * @return The hash
 */
static uint64_t box_audio_seq_mix(
    uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;

    return x ^ (x >> 31);
}

/** Get a uniform value in [-1, 1) from a hash.
 *
 * @param h The hash
 * @return The value
 */
static double box_audio_seq_uniform(
    uint64_t h)
{
    return (double)(h >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0;
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
    double start;           /**< Start of the placement */
    double lo;              /**< First beat of the timeline it schedules */
    double hi;              /**< Beat after the last */
    double end;             /**< End of the placement */
    double loop_end;        /**< loop_end of a transport that wraps, INFINITY otherwise */
    uint8_t bus;            /**< Bus index of the track */
    uint8_t priority;       /**< Priority of the voices of the track */
    double swing;           /**< Swing of the track, 0..0.75 */
    double step;            /**< Beats of a step of its swing grid */
    double timing;          /**< Frames a note start moves at most either way */
    double vary;            /**< Share the velocity changes at most either way */
    bool jump;              /**< The segment does not follow the one before: a pass that
                             *   covers its start starts in its middle */
} box_audio_seq_visit_t;

/** Most ramps of one curved span of the expression of a note */
#define BOX_AUDIO_SEQ_RAMPS_MAX 65536

/** Start the expression of a note that was scheduled: write the values of its curves at the
 * start of the note, and keep a ramp for each linear span and ramps of at most
 * BOX_AUDIO_EXPRESS_STEP for each curved span within the note, to send in their windows.
 *
 * @param state The state
 * @param v The visit
 * @param n The note
 * @param handle Its voice
 * @param when Frame of its start
 * @param at Beat of the timeline at which it starts, with swing and humanize
 * @param end Beat at which it ends
 */
static void box_audio_seq_express(
    box_audio_module_t *state,
    const box_audio_seq_visit_t *v,
    const Note *n,
    uint32_t handle,
    uint64_t when,
    double at,
    double end)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    const Curve *curves[3] = { &n->bend, &n->mod, &n->pressure };
    const uint32_t params[3] = {
        BOX_AUDIO_PARAM_BEND, BOX_AUDIO_PARAM_MOD, BOX_AUDIO_PARAM_PRESSURE };
    const float defaults[3] = { 0.0f, 1.0f, 0.0f };
    double length = end - at, step, a, b, t0, t1;
    box_audio_seq_ramp_t *r;
    int32_t c, k, p, count, pieces;
    float value;
    length = length < (double)n->duration ? length : (double)n->duration;
    step = BOX_AUDIO_EXPRESS_STEP * (double)state->timeline.rate /
        v->seg->anchor.frames_per_beat;
    for (c = 0; c < 3; c ++) {
        const Curve *curve = curves[c];
        const CurveKey *keys = ecs_vec_first(&curve->keys);
        count = ecs_vec_count(&curve->keys);
        if (!count) {
            continue;
        }

        value = box_curve_value(curve, 0.0f, defaults[c]);
        box_audio_seq_ramp(state, handle, params[c], isfinite(value) ? value : defaults[c], 0,
            when);
        for (k = 0; k + 1 < count; k ++) {
            a = (double)keys[k].time > 0 ? (double)keys[k].time : 0.0;
            b = (double)keys[k + 1].time < length ? (double)keys[k + 1].time : length;
            if (!(b > a)) {
                continue;
            }

            pieces = 1;
            if (curve->kind != CurveLinear && step > 0) {
                t0 = ceil((b - a) / step);
                pieces = t0 > 1 ? (t0 < BOX_AUDIO_SEQ_RAMPS_MAX ? (int32_t)t0 :
                    BOX_AUDIO_SEQ_RAMPS_MAX) : 1;
            }

            for (p = 0; p < pieces; p ++) {
                t0 = a + (b - a) * (double)p / (double)pieces;
                t1 = p + 1 < pieces ? a + (b - a) * (double)(p + 1) / (double)pieces : b;
                value = box_curve_value(curve, (float)t1, defaults[c]);
                r = ecs_vec_append_t(NULL, &seq->ramps, box_audio_seq_ramp_t);
                *r = (box_audio_seq_ramp_t){
                    .handle = handle,
                    .serial = state->serial,
                    .param = params[c],
                    .value = isfinite(value) ? value : defaults[c],
                    .beat = at + t0,
                    .end = at + t1,
                    .first = when,
                    .track = v->track,
                    .placement = v->placement
                };
            }
        }
    }
}

/** Schedule the notes of one pass of a clip that start in [lo, hi) of a visit.
 *
 * @param world The world
 * @param state The state
 * @param v The visit
 * @param base Beat of the timeline at which the pass starts its clip
 * @param pass_end Beat at which the pass ends, INFINITY for no end
 * @param limit Beat of the clip at which the notes of the pass stop, INFINITY for none
 * @param pass Index of the pass of the clip, 0 for a placement that does not loop
 *
 * The notes are picked by their programmed beat; swing and humanize then move the frame, so a
 * note may start after its window, and humanize may move it before, no earlier than the pass
 * began and than the first frame of a segment after a jump.
 */
static void box_audio_seq_pass(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_seq_visit_t *v,
    double base,
    double pass_end,
    double limit,
    int64_t pass)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    const Note *notes = ecs_vec_first(&v->clip->notes);
    int32_t i = box_audio_clip_find(v->clip, v->lo - base - BOX_AUDIO_SEQ_SEAM);
    int32_t count = ecs_vec_count(&v->clip->notes);
    float transpose = isfinite(v->p->transpose) ? v->p->transpose : 0.0f;
    box_audio_scheduled_t *sch;
    box_audio_sounding_t *s;
    double beat, at, end, r;
    float velocity;
    int64_t f, first;
    uint64_t when, h;
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

        at = beat;
        velocity = n->velocity;
        if (v->swing > 0) {
            at = box_audio_seq_swing(beat, v->swing, v->step);
            f = box_audio_anchor_frame(&v->seg->anchor, at);
        }

        if (v->timing > 0 || v->vary > 0) {
            h = box_audio_seq_mix((uint64_t)v->track);
            h = box_audio_seq_mix(h ^ (uint64_t)v->placement);
            h = box_audio_seq_mix(h ^ (uint64_t)pass);
            h = box_audio_seq_mix(h ^ (uint64_t)i);
            if (v->timing > 0) {
                f += (int64_t)llround(box_audio_seq_uniform(h) * v->timing);
                first = box_audio_anchor_frame(&v->seg->anchor,
                    base > v->start ? base : v->start);
                f = f > first ? f : first;
                f = f > 0 ? f : 0;
                at += (double)(f - box_audio_anchor_frame(&v->seg->anchor, at)) /
                    v->seg->anchor.frames_per_beat;
            }

            if (v->vary > 0) {
                r = box_audio_seq_uniform(box_audio_seq_mix(h ^ 0x5EEDull));
                r = (double)velocity * (1.0 + r * v->vary);
                velocity = (float)(r > 0 ? (r < 1 ? r : 1) : 0);
            }
        }

        when = (uint64_t)f > v->seg->frame ? (uint64_t)f : v->seg->frame;
        end = at + (double)n->duration;
        end = end < pass_end ? end : pass_end;
        end = end < v->end ? end : v->end;
        end = beat < v->loop_end && end > v->loop_end ? v->loop_end : end;
        handle = box_audio_instrument_voice(world, state, v->instrument, n->pitch + transpose,
            velocity, v->p->gain, n->pan, when, 0, v->bus, 0, false, v->priority);
        if (!handle) {
            seq->stats.dropped ++;
            continue;
        }

        seq->stats.notes ++;
        sch = ecs_vec_append_t(NULL, &state->timeline.scheduled, box_audio_scheduled_t);
        *sch = (box_audio_scheduled_t){ .handle = handle, .when = when };
        if (ecs_vec_count(&n->bend.keys) || ecs_vec_count(&n->mod.keys) ||
            ecs_vec_count(&n->pressure.keys))
        {
            box_audio_seq_express(state, v, n, handle, when, at, end);
        }

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

/** Check that the sound of an audio clip can play, and log once per track when it cannot.
 *
 * @param world The world
 * @param state The state
 * @param v The visit
 * @param frames Receives the frames of the sound
 * @param rate Receives its frames per second
 * @return Its sound id, 0 when it is not ready
 */
static uint32_t box_audio_seq_sound(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_seq_visit_t *v,
    double *frames,
    double *rate)
{
    ecs_entity_t sound = v->clip->sound;
    bool alive = ecs_is_alive(world, sound);
    const box_audio_sample_t *sample = alive ? box_audio_sample_of(world, sound) : NULL;
    const AudioPcm *pcm = alive && !sample ? ecs_get(world, sound, AudioPcm) : NULL;
    int32_t channels = pcm && pcm->channels > 0 ? pcm->channels : 1;
    AudioTrackState *ts = ecs_get_mut(world, v->track, AudioTrackState);
    uint32_t result = alive ? box_audio_sound_of(world, state, sound) : 0;
    *frames = sample && sample->state == SoundReady ? (double)sample->frames :
        pcm ? (double)(ecs_vec_count(&pcm->samples) / channels) : 0.0;
    *rate = sample && sample->sample_rate > 0 ? (double)sample->sample_rate :
        pcm && pcm->sample_rate > 0 ? (double)pcm->sample_rate : (double)state->rate;
    if (!(*frames > 0) || !(*rate > 0)) {
        result = 0;
    }

    if (!result && ts && !ts->audio_warned) {
        char *path = ecs_get_path(world, v->track);
        char *clip = ecs_get_path(world, v->clip->entity);
        ecs_warn("box.audio: the sound of audio clip '%s' on track '%s' is not ready, its "
            "passes are skipped", clip, path);
        ecs_os_free(clip);
        ecs_os_free(path);
        ts->audio_warned = true;
    } else if (result && ts) {
        ts->audio_warned = false;
    }

    return result;
}

/** Start the voice of one pass of an audio clip that starts in [lo, hi) of a visit, or, after
 * a jump, of the pass that covers lo in its middle.
 *
 * The voice plays the sound from the offset of the clip plus the beats of the pass before its
 * start, converted at the tempo and the pitch ratio, with a POSITION at the frame of its PLAY
 * when that is not the start of the sound.
 *
 * @param world The world
 * @param state The state
 * @param v The visit
 * @param base Beat of the timeline at which the pass starts its clip
 * @param pass_end Beat at which the pass ends, INFINITY for no end
 */
static void box_audio_seq_audio_pass(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_seq_visit_t *v,
    double base,
    double pass_end)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    const box_audio_timeline_t *tl = &state->timeline;
    float transpose = isfinite(v->p->transpose) ? v->p->transpose : 0.0f, beat, tempo;
    double from = base > v->start ? base : v->start, end, frames, rate, ratio, position, left;
    box_audio_scheduled_t *sch;
    box_audio_clip_voice_t *cv;
    uint32_t sound, handle;
    int64_t f;
    uint64_t when;
    end = pass_end < v->end ? pass_end : v->end;
    end = from < v->loop_end && end > v->loop_end ? v->loop_end : end;
    if (from < v->lo) {
        if (!v->jump || !(end > v->lo)) {
            goto done;
        }

        from = v->lo;
    }

    if (from >= v->hi || !(end > from)) {
        goto done;
    }

    f = box_audio_anchor_frame(&v->seg->anchor, from);
    if (f < 0) {
        goto done;
    }

    sound = box_audio_seq_sound(world, state, v, &frames, &rate);
    if (!sound) {
        seq->stats.skipped ++;
        goto done;
    }

    when = (uint64_t)f > v->seg->frame ? (uint64_t)f : v->seg->frame;
    ratio = exp2((double)transpose / 12.0);
    ratio = ratio > BOX_AUDIO_PITCH_MIN ? ratio : BOX_AUDIO_PITCH_MIN;
    ratio = ratio < BOX_AUDIO_PITCH_MAX ? ratio : BOX_AUDIO_PITCH_MAX;
    position = v->clip->offset * rate;
    if (from > base) {
        position += (from - base) * v->seg->anchor.frames_per_beat / (double)tl->rate *
            rate * ratio;
    }

    if (!(position < frames) || !(position < 1e9)) {
        goto done;
    }

    if (!box_audio_reserve(state)) {
        seq->stats.skipped ++;
        goto done;
    }

    handle = box_audio_new_voice(state);
    box_audio_timeline_grid(world, state, when, &beat, &tempo);
    box_audio_write_play_note(&state->writer, handle, sound, v->bus, v->p->gain, 0.0f,
        (float)ratio, 1.0f, beat, tempo, BOX_AUDIO_PLAY_ATTACK, v->priority, when);
    if (position > 0) {
        box_audio_write_voice_set(&state->writer, handle, BOX_AUDIO_PARAM_POSITION,
            (float)position, when);
        seq->stats.seeks ++;
    }

    left = ceil((frames - position) / (rate * ratio) * (double)tl->rate);
    seq->stats.passes ++;
    sch = ecs_vec_append_t(NULL, &state->timeline.scheduled, box_audio_scheduled_t);
    *sch = (box_audio_scheduled_t){ .handle = handle, .when = when };
    cv = ecs_vec_append_t(NULL, &seq->voices, box_audio_clip_voice_t);
    *cv = (box_audio_clip_voice_t){
        .handle = handle,
        .serial = state->serial,
        .start = when,
        .sound_end = when + (uint64_t)(left > 1 ? left : 1),
        .end = end,
        .track = v->track,
        .placement = v->placement
    };

done:
    return;
}

/** Schedule the passes of the clip of a placement that start in a segment: its notes or the
 * voices of its sound.
 *
 * @param world The world
 * @param state The state
 * @param v The visit
 * @param audio Whether to start the voices of the AudioClip, else the notes of the NoteClip
 */
static void box_audio_seq_passes(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_seq_visit_t *v,
    bool audio)
{
    double offset = isfinite(v->p->offset) && v->p->offset > 0 ? v->p->offset : 0.0;
    double length = v->clip->length, phase, base;
    int64_t k;
    if (v->p->loop && length > 0) {
        length = length > BOX_AUDIO_CLIP_LENGTH_MIN ? length : BOX_AUDIO_CLIP_LENGTH_MIN;
        phase = fmod(offset, length);
        k = (int64_t)floor((v->lo - (v->start - phase)) / length);
        for (k = k > 0 ? k : 0; ; k ++) {
            base = v->start - phase + (double)k * length;
            if (base >= v->hi) {
                break;
            }

            if (audio) {
                box_audio_seq_audio_pass(world, state, v, base, base + length);
            } else {
                box_audio_seq_pass(world, state, v, base, base + length, v->clip->length, k);
            }
        }
    } else if (audio) {
        base = v->start - offset;
        box_audio_seq_audio_pass(world, state, v, base, length > 0 ? base + length : INFINITY);
    } else {
        box_audio_seq_pass(world, state, v, v->start - offset, INFINITY, INFINITY, 0);
    }
}

/** Schedule the notes and the audio passes of a placement that start in a segment.
 *
 * The notes of a track with humanized timing are picked as far ahead of the segment as their
 * largest offset, up to loop_end when it wraps. The notes start from the beat the placement
 * was scanned up to, so each is picked once when that changes with the tempo or the timing.
 *
 * @param world The world
 * @param state The state
 * @param pl The placement in the index
 * @param seg The segment
 * @param jump Whether the segment does not follow the one before
 */
static void box_audio_seq_visit(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_placement_t *pl,
    const box_audio_segment_t *seg,
    bool jump)
{
    box_audio_sequencer_t *seq = &state->sequencer;
    const box_audio_timeline_t *tl = &state->timeline;
    box_audio_seq_visit_t v = { .track = pl->track, .placement = pl->entity, .seg = seg,
        .jump = jump };
    box_audio_seq_visit_t nv;
    const Track *tk;
    seq->stats.visits ++;
    if (!box_audio_seq_live(world, pl->track, pl->entity)) {
        seq->dirty |= ecs_is_alive(world, pl->entity) &&
            ecs_get_parent(world, pl->entity) != pl->track;
        goto done;
    }

    v.p = ecs_get(world, pl->entity, ClipPlacement);
    v.clip = box_audio_clip_get(world, seq, v.p->clip);
    if (!v.clip || (!ecs_vec_count(&v.clip->notes) && !v.clip->sound)) {
        goto done;
    }

    v.start = pl->start;
    v.lo = seg->start > pl->start ? seg->start : pl->start;
    v.hi = seg->end < pl->end ? seg->end : pl->end;
    v.end = pl->end;
    v.loop_end = tl->loop && tl->loop_end > tl->loop_start ? tl->loop_end : INFINITY;
    v.bus = box_audio_bus_of(world, state, pl->track);
    v.priority = box_audio_track_priority(world, pl->track);
    tk = ecs_get(world, pl->track, Track);
    if (isfinite(tk->swing) && tk->swing > 0) {
        v.swing = tk->swing < BOX_AUDIO_SEQ_SWING_MAX ? tk->swing : BOX_AUDIO_SEQ_SWING_MAX;
        v.step = tk->swing_grid == TrackSwing16 ? 0.25 : 0.5;
    }

    v.timing = box_audio_seq_timing(tk, (double)tl->rate);
    if (isfinite(tk->humanize.velocity) && tk->humanize.velocity > 0) {
        v.vary = tk->humanize.velocity < 1 ? tk->humanize.velocity : 1.0;
    }

    nv = v;
    nv.lo = pl->scanned > v.lo ? pl->scanned : v.lo;
    if (v.timing > 0) {
        nv.hi = seg->end + v.timing / seg->anchor.frames_per_beat;
        if (seg->start < v.loop_end && nv.hi > v.loop_end) {
            nv.hi = v.loop_end;
        }

        nv.hi = nv.hi < pl->end ? nv.hi : pl->end;
    }

    pl->scanned = nv.hi > pl->scanned ? nv.hi : pl->scanned;

    if (!(v.lo < v.hi) && !(nv.lo < nv.hi)) {
        goto done;
    }

    if (ecs_vec_count(&v.clip->notes) && nv.lo < nv.hi) {
        nv.inst = box_audio_seq_instrument(world, state, pl->track, &nv.instrument);
        if (nv.inst) {
            box_audio_seq_passes(world, state, &nv, false);
        }
    }

    if (v.clip->sound && v.lo < v.hi) {
        box_audio_seq_passes(world, state, &v, true);
    }

done:
    return;
}

/** The window hook of the timeline: for each segment, schedule the notes and audio passes of
 * the placements that overlap it, then send the releases of the notes and the stops of the
 * voices that end in it.
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
    box_audio_placement_t *index;
    double ahead;
    int32_t i, k, lo, hi, n;
    bool jump;
    (void)ctx;
    box_audio_sequencer_index(world, seq);
    box_audio_seq_prune(world, state);
    index = ecs_vec_first(&seq->index);
    n = ecs_vec_count(&seq->index);
    if (!n && !ecs_vec_count(&seq->sounding) && !ecs_vec_count(&seq->voices) &&
        !ecs_vec_count(&seq->ramps))
    {
        seq->expect = segments[count - 1].end;
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const box_audio_segment_t *seg = &segments[i];
        jump = isnan(seq->expect) || fabs(seg->start - seq->expect) > BOX_AUDIO_SEQ_SEAM;
        if (jump && !isnan(seq->expect)) {
            box_audio_seq_release_all(state, seg->frame);
            box_audio_seq_ramps_drop(state);
        }

        for (k = 0; jump && k < n; k ++) {
            index[k].scanned = -INFINITY;
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

        ahead = BOX_AUDIO_SEQ_TIMING_MAX / 1000.0 * (double)state->timeline.rate /
            seg->anchor.frames_per_beat;
        for (k = lo; k < n && index[k].start < seg->end + ahead; k ++) {
            if (index[k].end > seg->start && (index[k].start < seg->end ||
                (ecs_is_alive(world, index[k].track) && index[k].start < seg->end +
                box_audio_seq_timing(ecs_get(world, index[k].track, Track),
                (double)state->timeline.rate) / seg->anchor.frames_per_beat)))
            {
                box_audio_seq_visit(world, state, &index[k], seg, jump);
            }
        }

        box_audio_seq_ramps(state, seg);
        box_audio_seq_releases(state, seg);
        box_audio_seq_stops(state, seg);
        seq->expect = seg->end;
    }

done:
    return;
}

/** The reset hook of the timeline: on a stop and a seek release the notes that sound, stop the
 * voices of audio clips and cancel the ramps the core holds for later; the timeline stopped
 * what has not started. The ramps not sent are dropped. A new output knows none of them.
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
    box_audio_clip_voice_t *cv = ecs_vec_first(&seq->voices);
    box_audio_seq_expressed_t *e = ecs_vec_first(&seq->expressed);
    uint64_t clock = state->timeline.clock;
    int32_t i;
    (void)world;
    (void)ctx;
    for (i = 0; why != BOX_AUDIO_TIMELINE_OUTPUT && i < ecs_vec_count(&seq->expressed); i ++) {
        if (e[i].serial == state->serial && e[i].last >= clock) {
            box_audio_seq_ramp_cancel(state, e[i].handle);
        }
    }

    for (i = 0; why != BOX_AUDIO_TIMELINE_OUTPUT && i < ecs_vec_count(&seq->sounding); i ++) {
        if (s[i].serial == state->serial && s[i].start < clock &&
            (!s[i].release || s[i].release > clock))
        {
            box_audio_seq_release(state, &s[i], 0);
        }
    }

    for (i = 0; why != BOX_AUDIO_TIMELINE_OUTPUT && i < ecs_vec_count(&seq->voices); i ++) {
        if (cv[i].serial == state->serial && cv[i].start < clock &&
            (!cv[i].stop || cv[i].stop > clock))
        {
            box_audio_seq_stop(state, &cv[i], 0);
        }
    }

    ecs_vec_clear(&seq->sounding);
    ecs_vec_clear(&seq->voices);
    ecs_vec_clear(&seq->expressed);
    box_audio_seq_ramps_drop(state);
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

/** Mark the sorted notes of every clip for a rebuild when a NoteClip or an AudioClip changes,
 * and forget the copy of a clip that is removed.
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
    ecs_vec_init_t(NULL, &seq->scans, box_audio_placement_t, 0);
    ecs_vec_init_t(NULL, &seq->sounding, box_audio_sounding_t, 0);
    ecs_vec_init_t(NULL, &seq->voices, box_audio_clip_voice_t, 0);
    ecs_vec_init_t(NULL, &seq->ramps, box_audio_seq_ramp_t, 0);
    ecs_vec_init_t(NULL, &seq->expressed, box_audio_seq_expressed_t, 0);
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

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioClipSoundChange" }),
        .query.flags = EcsQueryMatchDisabled | EcsQueryMatchPrefab,
        .query.terms = {{ .id = ecs_id(AudioClip), .src.id = EcsSelf }},
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
    ecs_vec_fini_t(NULL, &seq->scans, box_audio_placement_t);
    ecs_vec_fini_t(NULL, &seq->sounding, box_audio_sounding_t);
    ecs_vec_fini_t(NULL, &seq->voices, box_audio_clip_voice_t);
    ecs_vec_fini_t(NULL, &seq->ramps, box_audio_seq_ramp_t);
    ecs_vec_fini_t(NULL, &seq->expressed, box_audio_seq_expressed_t);
}
