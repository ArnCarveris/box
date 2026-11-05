/**
 * @file audio/automation.c
 * @brief Automation clips: every frame the Transport plays, the placements of AutomationClip
 * on the tracks sample their curves within the segments of the lookahead window, every
 * BOX_AUDIO_AUTOMATION_STEP seconds of frames and at their keys, and schedule each value at
 * its frame: a BUS_SET for a parameter of a bus, an EFFECT_PARAMS for an effect, whose
 * parameters are built from a copy of its component with the automated members written
 * through a meta cursor.
 *
 * The automation never writes a component. A target no lane covers gets the value of its
 * component once, when its last lane ends or the transport stops.
 *
 * A stop, a seek and a change of a clip, a placement, a track or the component of a target
 * cancel what was scheduled ahead with a CANCEL; a change schedules again from the clock.
 */

#include "module.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Longest segment of a member path */
#define BOX_AUDIO_AUTOMATION_SEGMENT (64)

/** Beats two segments may differ by at their seam and still follow each other */
#define BOX_AUDIO_AUTOMATION_SEAM 1e-9

/** Largest beat of a placement, the one of the timeline */
#define BOX_AUDIO_AUTOMATION_BEAT_MAX 2147483648.0

/** Get a beat of a component as a finite value in -2^31..2^31.
 *
 * @param beat The beat
 * @return The clamped beat, 0 when it is NaN
 */
static double box_audio_automation_beat(
    double beat)
{
    double result = beat;
    if (isnan(result)) {
        result = 0.0;
    } else if (result < -BOX_AUDIO_AUTOMATION_BEAT_MAX) {
        result = -BOX_AUDIO_AUTOMATION_BEAT_MAX;
    } else if (result > BOX_AUDIO_AUTOMATION_BEAT_MAX) {
        result = BOX_AUDIO_AUTOMATION_BEAT_MAX;
    }

    return result;
}

/** Walk a member path, segments separated by dots and elements in brackets such as
 * bands[2].gain, with a meta cursor to a float member.
 *
 * @param world The world
 * @param component The component
 * @param ptr A value of the component
 * @param member The path
 * @param cur Receives the cursor at the member
 * @return NULL when the member is a float, else the reason, to free with ecs_os_free
 */
static char* box_audio_member_cursor(
    const ecs_world_t *world,
    ecs_entity_t component,
    void *ptr,
    const char *member,
    ecs_meta_cursor_t *cur)
{
    char name[BOX_AUDIO_AUTOMATION_SEGMENT];
    const char *p = member;
    char *result = NULL, *end = NULL, *type_name;
    const EcsMember *m;
    ecs_entity_t type, id;
    size_t len;
    long index;
    *cur = ecs_meta_cursor(world, component, ptr);
    if (!member || !member[0]) {
        result = flecs_asprintf("no member");
    }

    while (!result && *p) {
        len = strcspn(p, ".[");
        type = ecs_meta_get_type(cur);
        if (!len || len >= sizeof(name)) {
            result = flecs_asprintf("member path '%s' is not valid", member);
            break;
        }

        ecs_os_memcpy(name, p, (ecs_size_t)len);
        name[len] = '\0';
        p += len;
        if (!ecs_has(world, type, EcsStruct) || ecs_meta_push(cur)) {
            result = flecs_asprintf("'%s' of member '%s' is not in a struct", name, member);
        } else if (ecs_meta_try_member(cur, name)) {
            type_name = ecs_get_path(world, type);
            result = flecs_asprintf("'%s' is not a member of %s", name, type_name);
            ecs_os_free(type_name);
        } else {
            id = ecs_meta_get_member_id(cur);
            m = id ? ecs_get(world, id, EcsMember) : NULL;
            if (*p == '[') {
                index = strtol(p + 1, &end, 10);
                if (!m || m->count <= 1 || !end || end == p + 1 || *end != ']' ||
                    index < 0 || index >= m->count)
                {
                    result = flecs_asprintf("'%s' of member '%s' has no such element", name,
                        member);
                } else if (ecs_meta_push(cur) || ecs_meta_elem(cur, (int32_t)index)) {
                    result = flecs_asprintf("member path '%s' is not valid", member);
                } else {
                    p = end + 1;
                }
            } else if (m && m->count > 1) {
                result = flecs_asprintf("'%s' of member '%s' is an array, name an element "
                    "such as %s[0]", name, member, name);
            }
        }

        if (!result && *p == '.') {
            p ++;
            if (!*p) {
                result = flecs_asprintf("member path '%s' is not valid", member);
            }
        } else if (!result && *p) {
            result = flecs_asprintf("member path '%s' is not valid", member);
        }
    }

    if (!result && ecs_meta_get_type(cur) != ecs_id(ecs_f32_t)) {
        result = flecs_asprintf("member '%s' is not continuous, only float members are "
            "automatable", member);
    }

    return result;
}

/** Find the parameter of an automatable member of an AudioBus.
 *
 * @param member The path: volume, pan, width or sends[n].level
 * @param param Receives the box_audio_param_t
 * @param send Receives the index of the send of a send level, else -1
 * @return NULL when the member is automatable, else the reason, to free with ecs_os_free
 */
static char* box_audio_bus_member(
    const char *member,
    uint32_t *param,
    int32_t *send)
{
    char *result = NULL;
    int32_t j = -1, n = 0;
    *send = -1;
    if (!member || !member[0]) {
        result = flecs_asprintf("no member");
    } else if (!strcmp(member, "volume")) {
        *param = BOX_AUDIO_PARAM_VOLUME;
    } else if (!strcmp(member, "pan")) {
        *param = BOX_AUDIO_PARAM_PAN;
    } else if (!strcmp(member, "width")) {
        *param = BOX_AUDIO_PARAM_WIDTH;
    } else if (sscanf(member, "sends[%d].level%n", &j, &n) == 1 && n > 0 && !member[n] &&
        j >= 0 && j < BOX_AUDIO_SENDS)
    {
        *param = j ? (uint32_t)(BOX_AUDIO_PARAM_SEND_1 + j - 1) : BOX_AUDIO_PARAM_SEND;
        *send = j;
    } else {
        result = flecs_asprintf("member '%s' of AudioBus is not automatable, only volume, pan, "
            "width and sends[0..%d].level are", member, BOX_AUDIO_SENDS - 1);
    }

    return result;
}

/** Resolve the target of an automation clip.
 *
 * @param world The world
 * @param state The state
 * @param ac The clip
 * @param out Receives the kind, entity, component, parameter and send of the target
 * @return NULL when the clip can play, else the reason, to free with ecs_os_free
 */
static char* box_audio_automation_resolve(
    ecs_world_t *world,
    box_audio_module_t *state,
    const AutomationClip *ac,
    box_audio_target_t *out)
{
    uint64_t copy[BOX_AUDIO_AUTOMATION_COMPONENT / 8];
    const EcsComponent *c = NULL;
    ecs_meta_cursor_t cur;
    char *result = NULL, *name = NULL, *component = NULL;
    const void *ptr;
    ecs_os_zeromem(out);
    out->send = -1;
    out->entity = ac->target;
    out->component = ac->component;
    if (!ac->target) {
        result = flecs_asprintf("no target");
    } else if (!ecs_is_alive(world, ac->target)) {
        result = flecs_asprintf("the target #%u is deleted", (uint32_t)ac->target);
    } else if (!ac->component) {
        result = flecs_asprintf("no component");
    } else if (!ecs_is_alive(world, ac->component) ||
        !(c = ecs_get(world, ac->component, EcsComponent)) || !c->size)
    {
        result = flecs_asprintf("the component #%u is not a component with data",
            (uint32_t)ac->component);
    }

    if (result) {
        goto done;
    }

    name = ecs_get_path(world, ac->target);
    component = ecs_get_path(world, ac->component);
    if (ac->component == ecs_id(AudioBus)) {
        out->kind = BOX_AUDIO_TARGET_BUS;
        if (ac->target != state->master && !ecs_has(world, ac->target, AudioBus)) {
            result = flecs_asprintf("the target '%s' has no AudioBus", name);
        } else {
            result = box_audio_bus_member(ac->member, &out->param, &out->send);
        }

        if (!result && ac->target == state->master && out->send >= 0) {
            result = flecs_asprintf("the master has no sends");
        }
    } else if (box_audio_module_effect_params(world, ac->component, NULL, NULL) ==
        BOX_AUDIO_EFFECT_NONE)
    {
        result = flecs_asprintf("the component '%s' is neither AudioBus nor an effect",
            component);
    } else if (!(ptr = ecs_get_id(world, ac->target, ac->component))) {
        result = flecs_asprintf("the target '%s' has no %s", name, component);
    } else if (c->size > BOX_AUDIO_AUTOMATION_COMPONENT) {
        result = flecs_asprintf("the component '%s' is too large", component);
    } else {
        out->kind = BOX_AUDIO_TARGET_EFFECT;
        ecs_os_memcpy(copy, ptr, c->size);
        result = box_audio_member_cursor(world, ac->component, copy, ac->member, &cur);
    }

    if (!result) {
        char *curve = box_curve_validate(&ac->curve);
        if (curve) {
            result = flecs_asprintf("curve: %s", curve);
            ecs_os_free(curve);
        }
    }

done:
    ecs_os_free(name);
    ecs_os_free(component);

    return result;
}

/** Find the target of a resolved clip among the targets, or add it.
 *
 * @param a The automation
 * @param spec The kind, entity, component, parameter and send
 * @return Index of the target
 */
static int32_t box_audio_target_find(
    box_audio_automation_t *a,
    const box_audio_target_t *spec)
{
    box_audio_target_t *t = ecs_vec_first(&a->targets);
    int32_t i, count = ecs_vec_count(&a->targets);
    for (i = 0; i < count; i ++) {
        if (t[i].kind == spec->kind && t[i].entity == spec->entity &&
            t[i].component == spec->component &&
            (spec->kind == BOX_AUDIO_TARGET_EFFECT || t[i].param == spec->param))
        {
            break;
        }
    }

    if (i == count) {
        t = ecs_vec_append_t(NULL, &a->targets, box_audio_target_t);
        *t = *spec;
        t->held = true;
    }

    return i;
}

/** Order lanes by start, then by placement.
 *
 * @param a A lane
 * @param b Another lane
 * @return Negative when a comes first
 */
static int box_audio_lane_compare(
    const void *a,
    const void *b)
{
    const box_audio_lane_t *la = a, *lb = b;
    int result = la->placement < lb->placement ? -1 : (la->placement > lb->placement);
    if (la->start != lb->start) {
        result = la->start < lb->start ? -1 : 1;
    }

    return result;
}

/** Log the error of a clip once, until it changes.
 *
 * @param world The world
 * @param a The automation
 * @param clip The clip
 * @param track The track of its placement
 * @param error The reason
 */
static void box_audio_automation_error(
    ecs_world_t *world,
    box_audio_automation_t *a,
    ecs_entity_t clip,
    ecs_entity_t track,
    const char *error)
{
    char *path, *track_path;
    if (ecs_map_get(&a->logged, clip)) {
        goto done;
    }

    path = ecs_get_path(world, clip);
    track_path = ecs_get_path(world, track);
    ecs_err("box.audio: automation clip '%s' on track '%s' does nothing: %s", path,
        track_path, error);
    ecs_os_free(path);
    ecs_os_free(track_path);
    ecs_map_insert(&a->logged, clip, 1);
    a->stats.errors ++;

done:
    return;
}

/** Rebuild the lanes: every ClipPlacement of an AutomationClip on a child of a Track,
 * disabled ones included, sorted by start, each with its target. A clip that cannot play logs
 * its error once.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_automation_rebuild(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_automation_t *a = &state->automation;
    box_audio_target_t *targets = ecs_vec_first(&a->targets), spec;
    box_audio_lane_t *lane;
    ecs_iter_t it;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&a->targets); i ++) {
        targets[i].live = false;
    }

    ecs_vec_clear(&a->lanes);
    it = ecs_query_iter(world, state->sequencer.placements);
    while (ecs_query_next(&it)) {
        const ClipPlacement *p = ecs_field(&it, ClipPlacement, 0);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t track = ecs_get_parent(world, it.entities[i]);
            const AutomationClip *ac;
            double offset, length, span, start;
            char *error;
            if (!track || !ecs_has(world, track, Track) || !p[i].clip ||
                !ecs_is_alive(world, p[i].clip) ||
                !(ac = ecs_get(world, p[i].clip, AutomationClip)))
            {
                continue;
            }

            error = box_audio_automation_resolve(world, state, ac, &spec);
            if (error) {
                box_audio_automation_error(world, a, p[i].clip, track, error);
                ecs_os_free(error);
                continue;
            }

            offset = isfinite(p[i].offset) && p[i].offset > 0 ? p[i].offset : 0.0;
            length = isfinite(ac->length) && ac->length > 0 ? (double)ac->length : 0.0;
            span = isfinite(p[i].duration) && p[i].duration > 0 ? p[i].duration :
                length - offset;
            span = span < BOX_AUDIO_AUTOMATION_BEAT_MAX ? span : BOX_AUDIO_AUTOMATION_BEAT_MAX;
            if (!(span > 0)) {
                continue;
            }

            start = box_audio_automation_beat(p[i].start);
            lane = ecs_vec_append_t(NULL, &a->lanes, box_audio_lane_t);
            *lane = (box_audio_lane_t){
                .placement = it.entities[i],
                .track = track,
                .clip = p[i].clip,
                .start = start,
                .end = start + span,
                .offset = offset,
                .length = p[i].loop && length > 0 ? (length > BOX_AUDIO_CLIP_LENGTH_MIN ?
                    length : BOX_AUDIO_CLIP_LENGTH_MIN) : 0.0,
                .target = box_audio_target_find(a, &spec)
            };
            ecs_vec_get_t(&a->targets, box_audio_target_t, lane->target)->live = true;
        }
    }

    if (ecs_vec_count(&a->lanes) > 1) {
        qsort(ecs_vec_first(&a->lanes), (size_t)ecs_vec_count(&a->lanes),
            sizeof(box_audio_lane_t), box_audio_lane_compare);
    }

    a->stats.rebuilds ++;
}

/** Check for every lane whether its track and placement play and its clip still exists.
 *
 * @param world The world
 * @param a The automation
 */
static void box_audio_lanes_on(
    const ecs_world_t *world,
    box_audio_automation_t *a)
{
    box_audio_lane_t *lanes = ecs_vec_first(&a->lanes);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&a->lanes); i ++) {
        box_audio_lane_t *l = &lanes[i];
        l->on = ecs_is_alive(world, l->track) && ecs_is_alive(world, l->placement) &&
            ecs_is_alive(world, l->clip) && ecs_has(world, l->clip, AutomationClip) &&
            ecs_has(world, l->placement, ClipPlacement) &&
            !ecs_has_id(world, l->track, EcsDisabled) &&
            !ecs_has_id(world, l->placement, EcsDisabled) &&
            ecs_get_parent(world, l->placement) == l->track;
    }
}

/** Get the beat of the clip of a lane at a beat of the timeline.
 *
 * @param l The lane
 * @param beat The beat of the timeline
 * @param local Receives the beat of the clip
 * @return Whether the lane covers the beat
 */
static bool box_audio_lane_at(
    const box_audio_lane_t *l,
    double beat,
    double *local)
{
    double t, phase;
    bool result = l->on && beat >= l->start && beat < l->end;
    if (result && l->length > 0) {
        phase = fmod(l->offset, l->length);
        t = beat - (l->start - phase);
        *local = t - floor(t / l->length) * l->length;
    } else if (result) {
        *local = l->offset + (beat - l->start);
    }

    return result;
}

/** Get the value of the curve of a lane at a beat of its clip.
 *
 * @param world The world
 * @param l The lane
 * @param local The beat of the clip
 * @param fallback The value of the component, for a curve without keys
 * @return The value
 */
static float box_audio_lane_value(
    const ecs_world_t *world,
    const box_audio_lane_t *l,
    double local,
    float fallback)
{
    const AutomationClip *ac = ecs_get(world, l->clip, AutomationClip);
    float result = fallback;
    if (ac) {
        result = box_curve_loop_value(&ac->curve, (float)l->length, (float)local, fallback);
    }

    return isfinite(result) ? result : fallback;
}

/** Get the value of a parameter of a bus in its component.
 *
 * @param world The world
 * @param t The target
 * @return The value
 */
static float box_audio_bus_component_value(
    const ecs_world_t *world,
    const box_audio_target_t *t)
{
    const AudioBus *bus = ecs_is_alive(world, t->entity) ?
        ecs_get(world, t->entity, AudioBus) : NULL;
    const AudioSend *sends = bus ? ecs_vec_first(&bus->sends) : NULL;
    float result = 0.0f;
    if (t->send >= 0) {
        result = bus && t->send < ecs_vec_count(&bus->sends) ? sends[t->send].level : 0.0f;
    } else if (t->param == BOX_AUDIO_PARAM_VOLUME) {
        result = bus ? bus->volume : 1.0f;
    } else if (t->param == BOX_AUDIO_PARAM_PAN) {
        result = bus ? bus->pan : 0.0f;
    } else if (t->param == BOX_AUDIO_PARAM_WIDTH) {
        result = bus ? bus->width : 1.0f;
    }

    return result;
}

/** Send the value of a target at a beat, at a frame: the value of the lanes that cover the
 * beat, the last of them winning, else the value of its component. A target whose last value
 * was the one of its component sends nothing while no lane covers it.
 *
 * @param world The world
 * @param state The state
 * @param index Index of the target
 * @param beat The beat
 * @param when Frame of the value, 0 for now
 * @param lanes Whether the lanes count, false for the value of the component
 */
static void box_audio_target_send(
    ecs_world_t *world,
    box_audio_module_t *state,
    int32_t index,
    double beat,
    uint64_t when,
    bool lanes)
{
    box_audio_automation_t *a = &state->automation;
    box_audio_target_t *t = ecs_vec_get_t(&a->targets, box_audio_target_t, index);
    const box_audio_lane_t *l = ecs_vec_first(&a->lanes);
    uint64_t copy[BOX_AUDIO_AUTOMATION_COMPONENT / 8];
    float params[BOX_AUDIO_EFFECT_PARAMS], value = 0.0f, *member;
    const AudioSettings *settings;
    const EcsComponent *c;
    const AutomationClip *ac;
    ecs_meta_cursor_t cur;
    const void *ptr;
    uint32_t target = 0, kind = 0;
    int32_t i, slot;
    bool covered = false;
    double local;
    char *error;
    if (!ecs_is_alive(world, t->entity) || !state->serial) {
        goto done;
    }

    if (t->kind == BOX_AUDIO_TARGET_BUS) {
        target = t->entity == state->master ? 0 :
            box_audio_bus_of(world, state, t->entity);
        if (!target && t->entity != state->master) {
            goto done;
        }

        value = box_audio_bus_component_value(world, t);
        for (i = 0; lanes && i < ecs_vec_count(&a->lanes); i ++) {
            if (l[i].target == index && box_audio_lane_at(&l[i], beat, &local)) {
                value = box_audio_lane_value(world, &l[i], local, value);
                covered = true;
            }
        }

        if (!covered && t->held && t->serial == state->serial) {
            goto done;
        }

        if (t->entity == state->master && t->param == BOX_AUDIO_PARAM_VOLUME) {
            settings = ecs_singleton_get(world, AudioSettings);
            value *= settings ? settings->master_volume : 1.0f;
        }

        if (!box_audio_reserve(state)) {
            goto done;
        }

        box_audio_write_bus_set(&state->writer, target, (box_audio_param_t)t->param, value,
            when);
        if (covered) {
            a->stats.bus_sets ++;
        } else {
            a->stats.restores ++;
        }
    } else {
        slot = box_audio_module_effect_slot(world, t->entity);
        ptr = ecs_get_id(world, t->entity, t->component);
        c = ecs_get(world, t->component, EcsComponent);
        if (slot < 0 || !ptr || !c || c->size > BOX_AUDIO_AUTOMATION_COMPONENT ||
            box_audio_module_effect_component(world, t->entity) != t->component ||
            ecs_has_id(world, t->entity, EcsDisabled))
        {
            goto done;
        }

        ecs_os_memcpy(copy, ptr, c->size);
        for (i = 0; lanes && i < ecs_vec_count(&a->lanes); i ++) {
            if (l[i].target != index || !box_audio_lane_at(&l[i], beat, &local)) {
                continue;
            }

            ac = ecs_get(world, l[i].clip, AutomationClip);
            error = ac ? box_audio_member_cursor(world, t->component, copy, ac->member, &cur) :
                NULL;
            if (!ac || error) {
                ecs_os_free(error);
                continue;
            }

            member = ecs_meta_get_ptr(&cur);
            value = box_audio_lane_value(world, &l[i], local, *member);
            ecs_meta_set_float(&cur, (double)value);
            covered = true;
        }

        if (!covered && t->held && t->serial == state->serial) {
            goto done;
        }

        kind = box_audio_module_effect_params(world, t->component, copy, params);
        if (!box_audio_reserve(state)) {
            goto done;
        }

        target = (uint32_t)slot;
        box_audio_write_effect_params(&state->writer, target, kind, params, when);
        if (covered) {
            a->stats.effect_sets ++;
        } else {
            a->stats.restores ++;
        }
    }

    t->index = target;
    t->effect = kind;
    t->serial = state->serial;
    t->held = !covered;
    t->ahead = when > t->ahead ? when : t->ahead;

done:
    return;
}

/** Cancel the values scheduled ahead for a target. The output then holds a value the module
 * does not know, so the target is no longer held.
 *
 * @param state The state
 * @param t The target
 */
static void box_audio_target_cancel(
    box_audio_module_t *state,
    box_audio_target_t *t)
{
    if (t->serial == state->serial && state->serial && t->ahead &&
        t->ahead >= state->timeline.clock)
    {
        if (box_audio_reserve(state)) {
            box_audio_write_cancel(&state->writer, t->kind == BOX_AUDIO_TARGET_BUS ?
                BOX_AUDIO_CMD_BUS_SET : BOX_AUDIO_CMD_EFFECT_PARAMS, t->index, t->param);
            state->automation.stats.cancels ++;
        }

        t->held = false;
    }

    t->ahead = 0;
}

/** Add a point a segment samples at, the frame of a beat, when the beat is in the segment.
 *
 * @param a The automation
 * @param seg The segment
 * @param beat The beat
 */
static void box_audio_automation_point(
    box_audio_automation_t *a,
    const box_audio_segment_t *seg,
    double beat)
{
    box_audio_automation_point_t *p;
    int64_t f;
    if (!(beat >= seg->start && beat < seg->end)) {
        goto done;
    }

    f = box_audio_anchor_frame(&seg->anchor, beat);
    f = f > (int64_t)seg->frame ? f : (int64_t)seg->frame;
    f = f < (int64_t)seg->frame_end - 1 ? f : (int64_t)seg->frame_end - 1;
    if (f < 0) {
        goto done;
    }

    p = ecs_vec_append_t(NULL, &a->points, box_audio_automation_point_t);
    p->frame = (uint64_t)f;
    p->beat = beat;

done:
    return;
}

/** Order points by frame, then by beat.
 *
 * @param a A point
 * @param b Another point
 * @return Negative when a comes first
 */
static int box_audio_point_compare(
    const void *a,
    const void *b)
{
    const box_audio_automation_point_t *pa = a, *pb = b;
    int result = pa->beat < pb->beat ? -1 : (pa->beat > pb->beat);
    if (pa->frame != pb->frame) {
        result = pa->frame < pb->frame ? -1 : 1;
    }

    return result;
}

/** Add the points of the keys, the passes and the ends of a lane within a segment.
 *
 * @param world The world
 * @param a The automation
 * @param l The lane
 * @param seg The segment
 */
static void box_audio_lane_points(
    const ecs_world_t *world,
    box_audio_automation_t *a,
    const box_audio_lane_t *l,
    const box_audio_segment_t *seg)
{
    const AutomationClip *ac = ecs_get(world, l->clip, AutomationClip);
    const CurveKey *keys = ac ? ecs_vec_first(&ac->curve.keys) : NULL;
    int32_t i, count = ac ? ecs_vec_count(&ac->curve.keys) : 0;
    double lo = seg->start > l->start ? seg->start : l->start;
    double hi = seg->end < l->end ? seg->end : l->end, phase, base, beat;
    int64_t k;
    box_audio_automation_point(a, seg, l->start);
    box_audio_automation_point(a, seg, l->end);
    if (l->length > 0) {
        phase = fmod(l->offset, l->length);
        k = (int64_t)floor((lo - (l->start - phase)) / l->length);
        for (; ; k ++) {
            base = l->start - phase + (double)k * l->length;
            if (base >= hi) {
                break;
            }

            if (base >= lo) {
                box_audio_automation_point(a, seg, base);
            }

            for (i = 0; i < count; i ++) {
                beat = base + (double)keys[i].time;
                if ((double)keys[i].time < l->length && beat >= lo && beat < hi) {
                    box_audio_automation_point(a, seg, beat);
                }
            }
        }
    } else {
        base = l->start - l->offset;
        for (i = 0; i < count; i ++) {
            beat = base + (double)keys[i].time;
            if (beat >= lo && beat < hi) {
                box_audio_automation_point(a, seg, beat);
            }
        }
    }
}

/** Schedule the values of the targets in a segment: at its first frame after a jump, every
 * BOX_AUDIO_AUTOMATION_STEP seconds of frames of the clock, at the keys, at the start of
 * every pass and at the ends of the lanes.
 *
 * @param world The world
 * @param state The state
 * @param seg The segment
 * @param jump Whether the segment does not follow the one before
 */
static void box_audio_automation_segment(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_segment_t *seg,
    bool jump)
{
    box_audio_automation_t *a = &state->automation;
    box_audio_target_t *targets = ecs_vec_first(&a->targets);
    const box_audio_lane_t *lanes = ecs_vec_first(&a->lanes);
    box_audio_automation_point_t *points, *p;
    int32_t i, j, count, kept;
    uint64_t step, f;
    bool any = false;
    double beat;
    ecs_vec_clear(&a->points);
    if (!(seg->frame_end > seg->frame)) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&a->targets); i ++) {
        targets[i].busy = !targets[i].held && targets[i].live;
        any |= targets[i].busy;
    }

    for (i = 0; i < ecs_vec_count(&a->lanes); i ++) {
        if (lanes[i].on && lanes[i].end > seg->start && lanes[i].start < seg->end) {
            targets[lanes[i].target].busy = true;
            box_audio_lane_points(world, a, &lanes[i], seg);
            any = true;
        }
    }

    if (!any) {
        goto done;
    }

    if (jump) {
        p = ecs_vec_append_t(NULL, &a->points, box_audio_automation_point_t);
        p->frame = seg->frame;
        p->beat = seg->start;
    }

    step = (uint64_t)llround(BOX_AUDIO_AUTOMATION_STEP * (double)state->timeline.rate);
    step = step > 0 ? step : 1;
    for (f = (seg->frame + step - 1) / step * step; f < seg->frame_end; f += step) {
        beat = box_audio_anchor_beat(&seg->anchor, f);
        beat = beat > seg->start ? beat : seg->start;
        beat = beat < seg->end ? beat : nextafter(seg->end, seg->start);
        p = ecs_vec_append_t(NULL, &a->points, box_audio_automation_point_t);
        p->frame = f;
        p->beat = beat;
    }

    points = ecs_vec_first(&a->points);
    count = ecs_vec_count(&a->points);
    if (count > 1) {
        qsort(points, (size_t)count, sizeof(box_audio_automation_point_t),
            box_audio_point_compare);
    }

    for (i = 0, kept = 0; i < count; i ++) {
        if (kept && points[kept - 1].frame == points[i].frame) {
            points[kept - 1] = points[i];
        } else {
            points[kept ++] = points[i];
        }
    }

    for (i = 0; i < kept; i ++) {
        for (j = 0; j < ecs_vec_count(&a->targets); j ++) {
            if (ecs_vec_get_t(&a->targets, box_audio_target_t, j)->busy) {
                box_audio_target_send(world, state, j, points[i].beat, points[i].frame,
                    true);
            }
        }
    }

done:
    return;
}

/** Follow a change of the clips, the placements, the tracks or the components of the targets:
 * cancel what was scheduled ahead, rebuild the lanes, restore the targets no lane moves any
 * more, and schedule the windows that did not play yet again from the clock on.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_automation_change(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_automation_t *a = &state->automation;
    box_audio_target_t *targets = ecs_vec_first(&a->targets);
    box_audio_lane_t *lanes;
    box_audio_segment_t *past, s;
    uint64_t clock = state->timeline.clock;
    int32_t i, j, count, *map;
    double expect = NAN;
    a->dirty = false;
    for (i = 0; i < ecs_vec_count(&a->targets); i ++) {
        box_audio_target_cancel(state, &targets[i]);
    }

    box_audio_automation_rebuild(world, state);
    targets = ecs_vec_first(&a->targets);
    count = ecs_vec_count(&a->targets);
    map = count ? ecs_os_malloc_n(int32_t, count) : NULL;
    for (i = 0, j = 0; i < count; i ++) {
        if (!targets[i].live) {
            if (!targets[i].held) {
                box_audio_target_send(world, state, i, 0.0, 0, false);
            }

            map[i] = -1;
        } else {
            targets[j] = targets[i];
            map[i] = j ++;
        }
    }

    ecs_vec_set_count_t(NULL, &a->targets, box_audio_target_t, j);
    lanes = ecs_vec_first(&a->lanes);
    for (i = 0; i < ecs_vec_count(&a->lanes); i ++) {
        lanes[i].target = map[lanes[i].target];
    }

    ecs_os_free(map);
    box_audio_lanes_on(world, a);
    past = ecs_vec_first(&a->past);
    for (i = 0; i < ecs_vec_count(&a->past); i ++) {
        s = past[i];
        if (s.frame_end <= clock) {
            continue;
        }

        if (s.frame < clock) {
            s.frame = clock;
            s.start = box_audio_anchor_beat(&s.anchor, clock);
            s.start = s.start < s.end ? s.start : nextafter(s.end, -INFINITY);
        }

        box_audio_automation_segment(world, state, &s, isnan(expect) ||
            fabs(s.start - expect) > BOX_AUDIO_AUTOMATION_SEAM);
        expect = s.end;
    }
}

/** The window hook of the timeline: follow the changes, then schedule the values of every
 * segment, and keep the segments until they played.
 *
 * @param world The world
 * @param state The state
 * @param segments The segments of the window
 * @param count Number of segments
 * @param ctx Unused
 */
static void box_audio_automation_window(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_segment_t *segments,
    int32_t count,
    void *ctx)
{
    box_audio_automation_t *a = &state->automation;
    box_audio_segment_t *past = ecs_vec_first(&a->past);
    uint64_t clock = state->timeline.clock;
    int32_t i;
    bool jump;
    (void)ctx;
    for (i = ecs_vec_count(&a->past) - 1; i >= 0; i --) {
        if (past[i].frame_end <= clock) {
            ecs_vec_remove_ordered_t(&a->past, box_audio_segment_t, i);
            past = ecs_vec_first(&a->past);
        }
    }

    if (a->dirty) {
        box_audio_automation_change(world, state);
    }

    if (ecs_vec_count(&a->lanes) || ecs_vec_count(&a->targets)) {
        box_audio_lanes_on(world, a);
        for (i = 0; i < count; i ++) {
            jump = isnan(a->expect) ||
                fabs(segments[i].start - a->expect) > BOX_AUDIO_AUTOMATION_SEAM;
            box_audio_automation_segment(world, state, &segments[i], jump);
            a->expect = segments[i].end;
        }
    }

    a->expect = segments[count - 1].end;
    for (i = 0; i < count; i ++) {
        ecs_vec_append_t(NULL, &a->past, box_audio_segment_t)[0] = segments[i];
    }
}

/** The reset hook of the timeline: on a stop and a seek cancel what was scheduled ahead and
 * send the value of its component to every target at once; the window after a seek schedules
 * the lanes from the new beat. A new output knows none of it, and has the components.
 *
 * @param world The world
 * @param state The state
 * @param why Why
 * @param ctx Unused
 */
static void box_audio_automation_reset(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_timeline_reset_t why,
    void *ctx)
{
    box_audio_automation_t *a = &state->automation;
    box_audio_target_t *t = ecs_vec_first(&a->targets);
    int32_t i;
    (void)ctx;
    a->expect = NAN;
    ecs_vec_clear(&a->past);
    for (i = 0; i < ecs_vec_count(&a->targets); i ++) {
        if (why == BOX_AUDIO_TIMELINE_OUTPUT) {
            t[i].serial = 0;
            t[i].ahead = 0;
            t[i].held = true;
            continue;
        }

        box_audio_target_cancel(state, &t[i]);
        if (!t[i].held) {
            box_audio_target_send(world, state, i, 0.0, 0, false);
        }
    }
}

/** Mark the lanes for a rebuild when a placement, a track or an automation clip changes, and
 * let a clip that changed log its error again.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioAutomationChange(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    state->automation.dirty = true;
    for (i = 0; it->event_id == ecs_id(AutomationClip) && i < it->count; i ++) {
        ecs_map_remove(&state->automation.logged, it->entities[i]);
    }

done:
    return;
}

/** Mark the lanes for a new schedule when the component of a target changes.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioAutomationTarget(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    const box_audio_target_t *t;
    int32_t i, j;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    t = ecs_vec_first(&state->automation.targets);
    for (i = 0; i < it->count; i ++) {
        for (j = 0; j < ecs_vec_count(&state->automation.targets); j ++) {
            if (t[j].entity == it->entities[i] && t[j].component == it->event_id) {
                state->automation.dirty = true;
            }
        }
    }

done:
    return;
}

void box_audio_automation_init(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_automation_t *a = &state->automation;
    box_audio_timeline_hook_t hook = {
        .window = box_audio_automation_window,
        .reset = box_audio_automation_reset
    };
    ecs_entity_t components[BOX_AUDIO_EFFECT_COMPONENTS + 1] = { ecs_id(AudioBus) };
    int32_t i, count = 1 + box_audio_module_effect_components(components + 1);
    ecs_vec_init_t(NULL, &a->lanes, box_audio_lane_t, 0);
    ecs_vec_init_t(NULL, &a->targets, box_audio_target_t, 0);
    ecs_vec_init_t(NULL, &a->past, box_audio_segment_t, 0);
    ecs_vec_init_t(NULL, &a->points, box_audio_automation_point_t, 0);
    ecs_map_init(&a->logged, NULL);
    a->dirty = true;
    a->expect = NAN;
    box_audio_timeline_hook(state, &hook);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioAutomationPlacement" }),
        .query.flags = EcsQueryMatchDisabled,
        .query.terms = {{ .id = ecs_id(ClipPlacement), .src.id = EcsSelf }},
        .events = { EcsOnAdd, EcsOnSet, EcsOnRemove },
        .callback = BoxAudioAutomationChange,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioAutomationTrack" }),
        .query.flags = EcsQueryMatchDisabled,
        .query.terms = {{ .id = ecs_id(Track), .src.id = EcsSelf }},
        .events = { EcsOnAdd, EcsOnSet, EcsOnRemove },
        .callback = BoxAudioAutomationChange,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioAutomationClip" }),
        .query.flags = EcsQueryMatchDisabled | EcsQueryMatchPrefab,
        .query.terms = {{ .id = ecs_id(AutomationClip), .src.id = EcsSelf }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BoxAudioAutomationChange,
        .ctx = state
    });

    for (i = 0; i < count; i ++) {
        ecs_observer(world, {
            .query.flags = EcsQueryMatchDisabled,
            .query.terms = {{ .id = components[i], .src.id = EcsSelf }},
            .events = { EcsOnSet },
            .callback = BoxAudioAutomationTarget,
            .ctx = state
        });
    }
}

void box_audio_automation_fini(
    box_audio_module_t *state)
{
    box_audio_automation_t *a = &state->automation;
    ecs_map_fini(&a->logged);
    ecs_vec_fini_t(NULL, &a->lanes, box_audio_lane_t);
    ecs_vec_fini_t(NULL, &a->targets, box_audio_target_t);
    ecs_vec_fini_t(NULL, &a->past, box_audio_segment_t);
    ecs_vec_fini_t(NULL, &a->points, box_audio_automation_point_t);
}
