/**
 * @file Animator.c
 * @brief Tests for Animator playback: modes, speed, seeking and pausing, targets in the
 * hierarchy, prefab instances with their own time and writes that react as ordinary writes.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "animation.h"

/** Warnings and errors logged while capturing */
static char animator_log[4096];

/** Number of warnings logged while capturing */
static int32_t animator_warnings;

/** Log callback that appends warnings and errors to animator_log.
 *
 * @param level The log level
 * @param file The file
 * @param line The line
 * @param msg The message
 */
static void AnimatorCaptureLog(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level == -2) {
        animator_warnings ++;
    }

    if ((level == -2 || level == -3) &&
        strlen(animator_log) + strlen(msg) + 2 < sizeof(animator_log))
    {
        strcat(animator_log, msg);
        strcat(animator_log, "\n");
    }
}

/** Previous log state while capturing */
static struct {
    int32_t level;              /**< Log level */
    bool colors;                /**< Colors enabled */
    ecs_os_api_log_t log;       /**< Log callback */
} animator_prev;

/** Start capturing warnings and errors. */
static void animator_capture_begin(void) {
    animator_log[0] = '\0';
    animator_warnings = 0;
    animator_prev.level = ecs_log_set_level(-1);
    animator_prev.colors = ecs_log_enable_colors(false);
    animator_prev.log = ecs_os_api.log_;
    ecs_os_api.log_ = AnimatorCaptureLog;
}

/** Stop capturing. */
static void animator_capture_end(void) {
    ecs_os_api.log_ = animator_prev.log;
    ecs_log_set_level(animator_prev.level);
    ecs_log_enable_colors(animator_prev.colors);
}

/** The child of an instance spawned from a child of its prefab. A prefab child made by a
 * script spawns as a child without a name, with an IsA pair to the prefab child.
 *
 * @param world The world
 * @param instance The instance
 * @param base_child The child of the prefab
 * @return The child of the instance, 0 when none
 */
static ecs_entity_t animator_spawned(
    ecs_world_t *world,
    ecs_entity_t instance,
    ecs_entity_t base_child)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_children(world, instance);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (ecs_has_pair(world, it.entities[i], EcsIsA, base_child)) {
                result = it.entities[i];
            }
        }
    }

    return result;
}

/** Make a world with box.
 *
 * @return The world
 */
static ecs_world_t* animator_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    return world;
}

/** Set a linear track on an entity, the AnimationTrack copying the strings and the keys.
 *
 * @param world The world
 * @param track The track entity
 * @param component The component
 * @param member The member path
 * @param target The target path, NULL for the animated entity
 * @param keys The keys
 * @param count Number of keys
 */
static void animator_track_set(
    ecs_world_t *world,
    ecs_entity_t track,
    ecs_entity_t component,
    const char *member,
    const char *target,
    const CurveKey *keys,
    int32_t count)
{
    AnimationTrack t = {
        .component = component,
        .member = (char*)member,
        .target = (char*)target,
        .curve = { .kind = CurveLinear }
    };
    int32_t i;
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &t.curve.keys, CurveKey) = keys[i];
    }

    ecs_set_ptr(world, track, AnimationTrack, &t);
    ecs_vec_fini_t(NULL, &t.curve.keys, CurveKey);
}

/** Make a named linear track as a child of a clip.
 *
 * @param world The world
 * @param clip The clip
 * @param name The name of the track
 * @param component The component
 * @param member The member path
 * @param target The target path, NULL for the animated entity
 * @param keys The keys
 * @param count Number of keys
 * @return The track
 */
static ecs_entity_t animator_track(
    ecs_world_t *world,
    ecs_entity_t clip,
    const char *name,
    ecs_entity_t component,
    const char *member,
    const char *target,
    const CurveKey *keys,
    int32_t count)
{
    ecs_entity_t track = ecs_entity(world, { .name = name, .parent = clip });
    animator_track_set(world, track, component, member, target, keys, count);

    return track;
}

/** Make a named clip.
 *
 * @param world The world
 * @param name The name
 * @param duration The duration of its AnimationClip, 0 for the last key
 * @return The clip
 */
static ecs_entity_t animator_clip(
    ecs_world_t *world,
    const char *name,
    float duration)
{
    ecs_entity_t clip = ecs_entity(world, { .name = name });
    ecs_set(world, clip, AnimationClip, { duration });

    return clip;
}

/** Give an entity an Animator.
 *
 * @param world The world
 * @param e The entity
 * @param clip The clip
 * @param mode The mode
 * @param speed The speed
 */
static void animator_play(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t clip,
    AnimationMode mode,
    float speed)
{
    ecs_set(world, e, Animator, { .clip = clip, .mode = mode, .speed = speed });
}

/** Change the time of an animator, as an application seeking does.
 *
 * @param world The world
 * @param e The entity
 * @param time The time
 */
static void animator_seek(
    ecs_world_t *world,
    ecs_entity_t e,
    float time)
{
    Animator *a = ecs_get_mut(world, e, Animator);
    a->time = time;
    ecs_modified(world, e, Animator);
}

/** Pause or resume an animator.
 *
 * @param world The world
 * @param e The entity
 * @param paused Whether it is paused
 */
static void animator_pause(
    ecs_world_t *world,
    ecs_entity_t e,
    bool paused)
{
    Animator *a = ecs_get_mut(world, e, Animator);
    a->paused = paused;
    ecs_modified(world, e, Animator);
}

/** Read a file of the repository.
 *
 * @param file Path from the root of the repository
 * @return The contents, which the caller frees
 */
static char* animator_read(
    const char *file)
{
    char *path = box_app_locate(file);
    test_assert(path != NULL);
    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *result = ecs_os_malloc((ecs_size_t)size + 1);
    test_assert(fread(result, 1, (size_t)size, f) == (size_t)size);
    result[size] = '\0';
    fclose(f);
    ecs_os_free(path);

    return result;
}

/** Two keys from 0 to 4 over two seconds */
static const CurveKey animator_ramp[] = {{0, 0}, {2, 4}};

/** Values of Position.y the OnSet observer of onset_fires saw */
static float animator_seen[8];

/** Number of values in animator_seen */
static int32_t animator_seen_count;

/** Record the Position.y of entities whose Position is set.
 *
 * @param it The iterator
 */
static void AnimatorOnSetPosition(
    ecs_iter_t *it)
{
    const Position *p = ecs_field(it, Position, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (animator_seen_count < 8) {
            animator_seen[animator_seen_count ++] = p[i].y;
        }
    }
}

/** Number of times an observer of no_animators_no_cost ran */
static int32_t animator_touched;

/** Count the events of the clip and its tracks.
 *
 * @param it The iterator
 */
static void AnimatorOnTouch(
    ecs_iter_t *it)
{
    animator_touched += it->count;
}

/* Values */

void Animator_defaults(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Animator);

    const Animator *a = ecs_get(world, e, Animator);
    test_uint(a->clip, 0);
    test_flt(a->time, 0);
    test_flt(a->speed, 1);
    test_int(a->mode, AnimationLoop);
    test_bool(a->paused, false);
    test_bool(a->finished, false);

    ecs_fini(world);
}

void Animator_position_moves(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, e, Position);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);
    test_flt(p->x, 1);
    test_flt(p->y, 1);
    test_flt(p->z, 3);

    ecs_progress(world, 0.5f);
    p = ecs_get(world, e, Position);
    test_flt(ecs_get(world, e, Animator)->time, 1);
    test_flt(p->y, 2);

    ecs_fini(world);
}

void Animator_rgb_changes(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    CurveKey red[] = {{0, 0}, {1, 200}};
    CurveKey blue[] = {{0, 100}, {1, 0}};
    animator_track(world, clip, "r", ecs_id(Rgb), "r", NULL, red, 2);
    animator_track(world, clip, "b", ecs_id(Rgb), "b", NULL, blue, 2);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Rgb, {1, 2, 3});
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.25f);
    const Rgb *c = ecs_get(world, e, Rgb);
    test_int(c->r, 50);
    test_int(c->g, 2);
    test_int(c->b, 75);

    ecs_fini(world);
}

void Animator_member_only_keeps_other_members(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "z", ecs_id(Position), "z", NULL, animator_ramp, 2);
    CurveKey green[] = {{0, 0}, {2, 100}};
    animator_track(world, clip, "g", ecs_id(Rgb), "g", NULL, green, 2);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {5, 6, 7});
    ecs_set(world, e, Rgb, {10, 20, 30});
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 1);
    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 5);
    test_flt(p->y, 6);
    test_flt(p->z, 2);
    const Rgb *c = ecs_get(world, e, Rgb);
    test_int(c->r, 10);
    test_int(c->g, 50);
    test_int(c->b, 30);

    ecs_fini(world);
}

void Animator_light_intensity(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    CurveKey keys[] = {{0, 1}, {1, 9}};
    animator_track(world, clip, "intensity", ecs_id(PointLight), "intensity", NULL, keys, 2);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 2, 0});
    ecs_add(world, e, PointLight);
    float range = ecs_get(world, e, PointLight)->range;
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.25f);
    const PointLight *l = ecs_get(world, e, PointLight);
    test_flt(l->intensity, 3);
    test_flt(l->range, range);

    ecs_fini(world);
}

/* Speed, pause and seek */

void Animator_speed(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, clip, AnimationLoop, 2);

    ecs_progress(world, 0.25f);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);

    Animator *a = ecs_get_mut(world, e, Animator);
    a->speed = 0.5f;
    ecs_modified(world, e, Animator);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 0.75f);
    test_flt(ecs_get(world, e, Position)->y, 1.5f);

    ecs_fini(world);
}

void Animator_negative_speed(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t loop = ecs_new(world);
    animator_play(world, loop, clip, AnimationLoop, -1);
    ecs_entity_t once = ecs_new(world);
    animator_play(world, once, clip, AnimationOnce, -1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, loop, Animator)->time, 1.5f);
    test_flt(ecs_get(world, loop, Position)->y, 3);
    test_flt(ecs_get(world, once, Animator)->time, 1.5f);
    test_flt(ecs_get(world, once, Position)->y, 3);
    test_bool(ecs_get(world, once, Animator)->finished, false);

    ecs_progress(world, 1);
    test_flt(ecs_get(world, loop, Animator)->time, 0.5f);
    test_flt(ecs_get(world, once, Animator)->time, 0.5f);

    ecs_progress(world, 1);
    test_flt(ecs_get(world, loop, Animator)->time, 1.5f);
    test_flt(ecs_get(world, loop, Position)->y, 3);
    test_flt(ecs_get(world, once, Animator)->time, 0);
    test_flt(ecs_get(world, once, Position)->y, 0);
    test_bool(ecs_get(world, once, Animator)->finished, true);

    ecs_fini(world);
}

void Animator_paused(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);

    ecs_set(world, e, Position, {0, 100, 0});
    animator_pause(world, e, true);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 100);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 100);

    animator_pause(world, e, false);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 1);
    test_flt(ecs_get(world, e, Position)->y, 2);

    ecs_fini(world);
}

void Animator_seek_while_paused(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, clip, AnimationLoop, 1);
    animator_pause(world, e, true);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 0);
    test_flt(ecs_get(world, e, Position)->y, 0);

    animator_seek(world, e, 1.5f);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 1.5f);
    test_flt(ecs_get(world, e, Position)->y, 3);

    ecs_set(world, e, Position, {0, 100, 0});
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 100);

    animator_seek(world, e, 0.5f);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);

    ecs_fini(world);
}

void Animator_paused_ignores_other_clip(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t other = animator_clip(world, "other", 0);
    ecs_entity_t track = animator_track(world, other, "x", ecs_id(Position), "x", NULL,
        animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);

    animator_pause(world, e, true);
    ecs_set(world, e, Position, {0, 100, 0});
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 100);

    CurveKey keys[] = {{0, 0}, {2, 40}};
    animator_track_set(world, track, ecs_id(Position), "x", NULL, keys, 2);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 100);

    ecs_set(world, other, AnimationClip, { 3 });
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 100);

    ecs_fini(world);
}

void Animator_paused_ignores_new_child(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);

    animator_pause(world, e, true);
    ecs_set(world, e, Position, {0, 100, 0});
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 100);

    ecs_new_w_pair(world, EcsChildOf, e);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 100);

    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, EcsParent, {e});
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 100);

    ecs_fini(world);
}

void Animator_paused_own_track_edit_writes(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    ecs_entity_t track = animator_track(world, clip, "y", ecs_id(Position), "y", NULL,
        animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);

    animator_pause(world, e, true);
    ecs_set(world, e, Position, {0, 100, 0});
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 100);

    CurveKey keys[] = {{0, 0}, {2, 40}};
    animator_track_set(world, track, ecs_id(Position), "y", NULL, keys, 2);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 10);

    ecs_set(world, e, Position, {0, 100, 0});
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 100);

    ecs_fini(world);
}

void Animator_seek_while_playing(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.25f);
    test_flt(ecs_get(world, e, Animator)->time, 0.25f);

    animator_seek(world, e, 1);
    ecs_progress(world, 0.25f);
    test_flt(ecs_get(world, e, Animator)->time, 1.25f);
    test_flt(ecs_get(world, e, Position)->y, 2.5f);

    ecs_fini(world);
}

/* Modes */

void Animator_once_holds_and_finishes(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, clip, AnimationOnce, 1);

    ecs_progress(world, 1.5f);
    test_flt(ecs_get(world, e, Animator)->time, 1.5f);
    test_bool(ecs_get(world, e, Animator)->finished, false);

    ecs_progress(world, 1.5f);
    test_flt(ecs_get(world, e, Animator)->time, 2);
    test_flt(ecs_get(world, e, Position)->y, 4);
    test_bool(ecs_get(world, e, Animator)->finished, true);

    ecs_progress(world, 1.5f);
    test_flt(ecs_get(world, e, Animator)->time, 2);
    test_flt(ecs_get(world, e, Position)->y, 4);
    test_bool(ecs_get(world, e, Animator)->finished, true);

    animator_seek(world, e, 0);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);
    test_bool(ecs_get(world, e, Animator)->finished, false);

    ecs_fini(world);
}

void Animator_loop_wraps(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.75f);
    ecs_progress(world, 0.75f);
    test_flt(ecs_get(world, e, Animator)->time, 1.5f);
    ecs_progress(world, 0.75f);
    test_flt(ecs_get(world, e, Animator)->time, 0.25f);
    test_flt(ecs_get(world, e, Position)->y, 0.5f);
    test_bool(ecs_get(world, e, Animator)->finished, false);

    ecs_fini(world);
}

void Animator_ping_pong_turns(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, clip, AnimationPingPong, 1);

    ecs_progress(world, 0.75f);
    ecs_progress(world, 0.75f);
    test_flt(ecs_get(world, e, Animator)->time, 1.5f);
    ecs_progress(world, 0.75f);
    test_flt(ecs_get(world, e, Animator)->time, 1.75f);
    test_flt(ecs_get(world, e, Position)->y, 3.5f);
    ecs_progress(world, 0.75f);
    test_flt(ecs_get(world, e, Animator)->time, 1);
    test_flt(ecs_get(world, e, Position)->y, 2);
    ecs_progress(world, 0.75f);
    test_flt(ecs_get(world, e, Animator)->time, 0.25f);
    ecs_progress(world, 0.75f);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);
    test_bool(ecs_get(world, e, Animator)->finished, false);

    ecs_fini(world);
}

void Animator_large_delta_boundary(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t once = ecs_new(world);
    animator_play(world, once, clip, AnimationOnce, 1);
    ecs_entity_t loop = ecs_new(world);
    animator_play(world, loop, clip, AnimationLoop, 1);
    ecs_entity_t pong = ecs_new(world);
    animator_play(world, pong, clip, AnimationPingPong, 1);
    ecs_entity_t back = ecs_new(world);
    animator_play(world, back, clip, AnimationPingPong, 1);
    animator_seek(world, back, 1);

    ecs_progress(world, 4.5f);
    test_flt(ecs_get(world, once, Animator)->time, 2);
    test_flt(ecs_get(world, once, Position)->y, 4);
    test_bool(ecs_get(world, once, Animator)->finished, true);
    test_flt(ecs_get(world, loop, Animator)->time, 0.5f);
    test_flt(ecs_get(world, loop, Position)->y, 1);
    test_flt(ecs_get(world, pong, Animator)->time, 0.5f);
    test_flt(ecs_get(world, pong, Position)->y, 1);
    test_flt(ecs_get(world, back, Animator)->time, 1.5f);
    test_flt(ecs_get(world, back, Position)->y, 3);

    ecs_progress(world, 2.25f);
    test_flt(ecs_get(world, loop, Animator)->time, 0.75f);
    test_flt(ecs_get(world, pong, Animator)->time, 1.25f);
    test_flt(ecs_get(world, pong, Position)->y, 2.5f);
    test_flt(ecs_get(world, back, Animator)->time, 0.25f);
    test_flt(ecs_get(world, back, Position)->y, 0.5f);

    ecs_fini(world);
}

/* Targets */

void Animator_child_target(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", "arm.hand", animator_ramp, 2);
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t arm = ecs_entity(world, { .name = "arm", .parent = e });
    ecs_entity_t hand = ecs_entity(world, { .name = "hand", .parent = arm });
    ecs_set(world, hand, Position, {1, 0, 1});
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, hand, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 1);
    test_flt(p->z, 1);
    test_assert(!ecs_has(world, e, Position));

    ecs_fini(world);
}

void Animator_missing_target_warns_once(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "lost", ecs_id(Position), "x", "nowhere", animator_ramp, 2);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    animator_play(world, e, clip, AnimationLoop, 1);

    animator_capture_begin();
    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    ecs_entity(world, { .name = "other", .parent = e });
    ecs_progress(world, 0.5f);
    animator_capture_end();

    test_int(animator_warnings, 1);
    test_assert(strstr(animator_log, "nowhere") != NULL);
    test_flt(ecs_get(world, e, Position)->y, 3);
    test_flt(ecs_get(world, e, Position)->x, 0);

    ecs_fini(world);
}

void Animator_reparented_child_resolves(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", "arm", animator_ramp, 2);
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t elsewhere = ecs_entity(world, { .name = "elsewhere" });
    ecs_entity_t arm = ecs_entity(world, { .name = "arm", .parent = elsewhere });
    ecs_set(world, arm, Position, {0, 0, 0});
    animator_play(world, e, clip, AnimationLoop, 1);

    animator_capture_begin();
    ecs_progress(world, 0.5f);
    animator_capture_end();
    test_int(animator_warnings, 1);
    test_flt(ecs_get(world, arm, Position)->y, 0);

    ecs_add_pair(world, arm, EcsChildOf, e);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, arm, Position)->y, 2);

    ecs_add_pair(world, arm, EcsChildOf, elsewhere);
    animator_capture_begin();
    ecs_progress(world, 0.5f);
    animator_capture_end();
    test_flt(ecs_get(world, arm, Position)->y, 2);

    ecs_fini(world);
}

void Animator_deleted_child_skips(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "arm", ecs_id(Position), "y", "arm", animator_ramp, 2);
    animator_track(world, clip, "self", ecs_id(Position), "x", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t arm = ecs_entity(world, { .name = "arm", .parent = e });
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, arm, Position)->y, 1);
    test_flt(ecs_get(world, e, Position)->x, 1);

    ecs_delete(world, arm);
    animator_capture_begin();
    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    animator_capture_end();
    test_assert(!ecs_is_alive(world, arm));
    test_int(animator_warnings, 1);
    test_flt(ecs_get(world, e, Position)->x, 3);

    ecs_fini(world);
}

/* Prefabs */

void Animator_instances_own_time(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", "arm", animator_ramp, 2);
    ecs_entity_t prefab = ecs_entity(world, { .name = "Rig" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_entity_t prefab_arm = ecs_entity(world, { .name = "arm", .parent = prefab });
    ecs_set(world, prefab_arm, Position, {0, 0, 0});
    animator_play(world, prefab, clip, AnimationLoop, 1);

    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, prefab);
    test_assert(ecs_owns(world, a, Animator));
    test_assert(ecs_owns(world, b, Animator));
    animator_seek(world, b, 1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, a, Animator)->time, 0.5f);
    test_flt(ecs_get(world, b, Animator)->time, 1.5f);
    test_flt(ecs_get(world, prefab, Animator)->time, 0);

    ecs_entity_t a_arm = ecs_lookup_child(world, a, "arm");
    ecs_entity_t b_arm = ecs_lookup_child(world, b, "arm");
    test_assert(a_arm != 0 && a_arm != prefab_arm);
    test_assert(b_arm != 0 && b_arm != prefab_arm);
    test_flt(ecs_get(world, a_arm, Position)->y, 1);
    test_flt(ecs_get(world, b_arm, Position)->y, 3);
    test_flt(ecs_get(world, prefab_arm, Position)->y, 0);

    ecs_fini(world);
}

void Animator_prefab_write_overrides(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    CurveKey red[] = {{0, 0}, {1, 200}};
    animator_track(world, clip, "r", ecs_id(Rgb), "r", NULL, red, 2);
    ecs_entity_t prefab = ecs_entity(world, { .name = "Tinted" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Rgb, {10, 20, 30});
    ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, prefab);
    test_assert(ecs_has(world, e, Rgb));
    test_assert(!ecs_owns(world, e, Rgb));
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    test_assert(ecs_owns(world, e, Rgb));
    const Rgb *c = ecs_get(world, e, Rgb);
    test_int(c->r, 100);
    test_int(c->g, 20);
    test_int(c->b, 30);
    test_int(ecs_get(world, prefab, Rgb)->r, 10);

    ecs_set(world, prefab, Rgb, {40, 50, 60});
    ecs_progress(world, 0.25f);
    test_int(ecs_get(world, e, Rgb)->r, 150);
    test_int(ecs_get(world, e, Rgb)->g, 20);

    ecs_fini(world);
}

void Animator_script_prefab(void) {
    ecs_world_t *world = animator_world();
    char *code = animator_read("test/assets/animation/animator_prefab.flecs");
    animator_capture_begin();
    test_int(ecs_script_run(world, "animator_prefab", code, NULL), 0);
    animator_capture_end();
    ecs_os_free(code);
    test_str(animator_log, "");

    ecs_entity_t first = ecs_lookup(world, "first");
    ecs_entity_t second = ecs_lookup(world, "second");
    test_assert(first != 0 && second != 0);
    test_assert(ecs_owns(world, first, Animator));
    const Animator *a = ecs_get(world, first, Animator);
    test_flt(a->time, 0.5f);
    test_flt(a->speed, 1);
    test_int(a->mode, AnimationPingPong);
    test_flt(ecs_get(world, second, Animator)->time, 1.5f);

    ecs_progress(world, 0.25f);
    test_flt(ecs_get(world, first, Animator)->time, 0.75f);
    test_flt(ecs_get(world, second, Animator)->time, 1.75f);
    ecs_entity_t rig_arm = ecs_lookup(world, "Rig.arm");
    ecs_entity_t first_arm = animator_spawned(world, first, rig_arm);
    ecs_entity_t second_arm = animator_spawned(world, second, rig_arm);
    test_assert(first_arm != 0 && second_arm != 0);
    test_flt(ecs_get(world, first_arm, Position)->y, 1.5f);
    test_flt(ecs_get(world, second_arm, Position)->y, 3.5f);
    test_flt(ecs_get(world, rig_arm, Position)->y, 0);

    ecs_fini(world);
}

/* Writes */

void Animator_onset_fires(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    animator_play(world, e, clip, AnimationLoop, 1);
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Position) }},
        .events = { EcsOnSet },
        .callback = AnimatorOnSetPosition
    });
    animator_seen_count = 0;

    ecs_progress(world, 0.5f);
    test_int(animator_seen_count, 1);
    test_flt(animator_seen[0], 1);

    ecs_progress(world, 0.25f);
    test_int(animator_seen_count, 2);
    test_flt(animator_seen[1], 1.5f);

    ecs_fini(world);
}

void Animator_transform_follows(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "x", ecs_id(Position), "x", NULL, animator_ramp, 2);
    animator_track(world, clip, "arm", ecs_id(Position), "y", "arm", animator_ramp, 2);
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_entity_t arm = ecs_entity(world, { .name = "arm", .parent = e });
    ecs_set(world, arm, Position, {0, 0, 0});
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, WorldTransform)->m[3][0], 1);
    test_flt(ecs_get(world, arm, WorldTransform)->m[3][0], 1);
    test_flt(ecs_get(world, arm, WorldTransform)->m[3][1], 1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, WorldTransform)->m[3][0], 2);
    test_flt(ecs_get(world, arm, WorldTransform)->m[3][1], 2);

    ecs_fini(world);
}

void Animator_change_clip(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t first = animator_clip(world, "first", 0);
    animator_track(world, first, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t second = animator_clip(world, "second", 0);
    CurveKey keys[] = {{0, 10}, {2, 30}};
    animator_track(world, second, "y", ecs_id(Position), "y", NULL, keys, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, first, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);

    Animator *a = ecs_get_mut(world, e, Animator);
    a->clip = second;
    ecs_modified(world, e, Animator);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 1);
    test_flt(ecs_get(world, e, Position)->y, 20);

    animator_pause(world, e, true);
    ecs_progress(world, 0.5f);
    a = ecs_get_mut(world, e, Animator);
    a->clip = first;
    ecs_modified(world, e, Animator);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 1);
    test_flt(ecs_get(world, e, Position)->y, 2);

    ecs_fini(world);
}

void Animator_changed_track(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    ecs_entity_t track = animator_track(world, clip, "y", ecs_id(Position), "y", NULL,
        animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);

    CurveKey keys[] = {{0, 0}, {2, 40}};
    animator_track_set(world, track, ecs_id(Position), "y", NULL, keys, 2);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 20);

    CurveKey longer[] = {{0, 0}, {4, 40}};
    animator_track_set(world, track, ecs_id(Position), "x", NULL, longer, 2);
    ecs_progress(world, 1.5f);
    test_flt(ecs_get(world, e, Animator)->time, 2.5f);
    test_flt(ecs_get(world, e, Position)->x, 25);
    test_flt(ecs_get(world, e, Position)->y, 20);

    ecs_fini(world);
}

void Animator_deleted_clip(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    animator_play(world, e, clip, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);

    ecs_delete(world, clip);
    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);

    ecs_fini(world);
}

/* Lifecycle */

void Animator_disabled_not_played(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 7, 0});
    animator_play(world, e, clip, AnimationLoop, 1);
    ecs_enable(world, e, false);

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 0);
    test_flt(ecs_get(world, e, Position)->y, 7);

    ecs_enable(world, e, true);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, e, Animator)->time, 0.5f);
    test_flt(ecs_get(world, e, Position)->y, 1);

    ecs_fini(world);
}

void Animator_no_clip_no_op(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t none = ecs_new(world);
    ecs_add(world, none, Animator);
    ecs_entity_t not_clip = ecs_new(world);
    ecs_entity_t invalid = ecs_new(world);
    animator_play(world, invalid, not_clip, AnimationLoop, 1);
    ecs_entity_t dead = ecs_new(world);
    ecs_delete(world, dead);
    ecs_entity_t deleted = ecs_new(world);
    animator_play(world, deleted, dead, AnimationLoop, 1);

    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, none, Animator)->time, 0);
    test_flt(ecs_get(world, invalid, Animator)->time, 0);
    test_flt(ecs_get(world, deleted, Animator)->time, 0);
    test_assert(!ecs_has(world, none, Position));
    test_assert(!ecs_has(world, invalid, Position));
    test_assert(!ecs_has(world, deleted, Position));

    ecs_fini(world);
}

void Animator_no_animators_no_cost(void) {
    ecs_world_t *world = animator_world();
    ecs_entity_t clip = animator_clip(world, "clip", 0);
    animator_track(world, clip, "y", ecs_id(Position), "y", NULL, animator_ramp, 2);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 5, 0});
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(AnimationTrack) }},
        .events = { EcsOnSet },
        .callback = AnimatorOnTouch
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(AnimationClip) }},
        .events = { EcsOnSet },
        .callback = AnimatorOnTouch
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Position) }},
        .events = { EcsOnSet },
        .callback = AnimatorOnTouch
    });
    animator_touched = 0;

    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    test_int(animator_touched, 0);
    test_flt(ecs_get(world, e, Position)->y, 5);

    ecs_entity_t system = ecs_lookup(world, "box.animation.AnimatorPlay");
    test_assert(system != 0);
    const ecs_system_t *s = ecs_system_get(world, system);
    test_assert(s != NULL);
    test_int(ecs_query_count(s->query).entities, 0);

    ecs_fini(world);
}
