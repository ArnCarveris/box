/**
 * @file AnimationClip.c
 * @brief Tests for box.animation: sampling tracks, resolving and writing members through
 * reflection, the errors of invalid tracks, clips in script and the lifecycle of clips.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "animation.h"

/** Component with members of every interesting kind, reflected by the tests */
typedef struct AnimTest {
    double d;       /**< A double member */
    int32_t i;      /**< An int32 member */
    bool flag;      /**< A bool member */
    char *name;     /**< A string member */
    Position pos;   /**< A struct member */
} AnimTest;

/** Component without reflection */
typedef struct AnimPlain {
    float value;    /**< Not reflected */
} AnimPlain;

static ECS_COMPONENT_DECLARE(AnimTest);
static ECS_COMPONENT_DECLARE(AnimPlain);

/** Errors logged while capturing */
static char anim_errors[4096];

/** Log callback that appends errors to anim_errors.
 *
 * @param level The log level
 * @param file The file
 * @param line The line
 * @param msg The message
 */
static void AnimCaptureLog(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level == -3 &&
        strlen(anim_errors) + strlen(msg) + 2 < sizeof(anim_errors))
    {
        strcat(anim_errors, msg);
        strcat(anim_errors, "\n");
    }
}

/** Previous log state while capturing */
static struct {
    int32_t level;              /**< Log level */
    bool colors;                /**< Colors enabled */
    ecs_os_api_log_t log;       /**< Log callback */
} anim_log;

/** Start capturing errors. */
static void anim_capture_begin(void) {
    anim_errors[0] = '\0';
    anim_log.level = ecs_log_set_level(-1);
    anim_log.colors = ecs_log_enable_colors(false);
    anim_log.log = ecs_os_api.log_;
    ecs_os_api.log_ = AnimCaptureLog;
}

/** Stop capturing errors. */
static void anim_capture_end(void) {
    ecs_os_api.log_ = anim_log.log;
    ecs_log_set_level(anim_log.level);
    ecs_log_enable_colors(anim_log.colors);
}

/** Make a world with box and the test components.
 *
 * @return The world
 */
static ecs_world_t* anim_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ECS_COMPONENT_DEFINE(world, AnimTest);
    ecs_struct(world, {
        .entity = ecs_id(AnimTest),
        .members = {
            { .name = "d", .type = ecs_id(ecs_f64_t) },
            { .name = "i", .type = ecs_id(ecs_i32_t) },
            { .name = "flag", .type = ecs_id(ecs_bool_t) },
            { .name = "name", .type = ecs_id(ecs_string_t) },
            { .name = "pos", .type = ecs_id(Position) }
        }
    });
    ECS_COMPONENT_DEFINE(world, AnimPlain);

    return world;
}

/** Set a track on an entity, the AnimationTrack copying the member and the keys.
 *
 * @param world The world
 * @param track The track entity
 * @param component The component
 * @param member The member path
 * @param kind The curve kind
 * @param keys The keys
 * @param count Number of keys
 */
static void anim_track_set(
    ecs_world_t *world,
    ecs_entity_t track,
    ecs_entity_t component,
    const char *member,
    CurveKind kind,
    const CurveKey *keys,
    int32_t count)
{
    AnimationTrack t = {
        .component = component,
        .member = (char*)member,
        .curve = { .kind = kind }
    };
    int32_t i;
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &t.curve.keys, CurveKey) = keys[i];
    }

    ecs_set_ptr(world, track, AnimationTrack, &t);
    ecs_vec_fini_t(NULL, &t.curve.keys, CurveKey);
}

/** Make a named track as a child of a clip.
 *
 * @param world The world
 * @param clip The clip
 * @param name The name of the track
 * @param component The component
 * @param member The member path
 * @param kind The curve kind
 * @param keys The keys
 * @param count Number of keys
 * @return The track
 */
static ecs_entity_t anim_track(
    ecs_world_t *world,
    ecs_entity_t clip,
    const char *name,
    ecs_entity_t component,
    const char *member,
    CurveKind kind,
    const CurveKey *keys,
    int32_t count)
{
    ecs_entity_t track = ecs_entity(world, { .name = name, .parent = clip });
    anim_track_set(world, track, component, member, kind, keys, count);

    return track;
}

/** Make a named clip.
 *
 * @param world The world
 * @param name The name
 * @param duration The duration of its AnimationClip
 * @return The clip
 */
static ecs_entity_t anim_clip(
    ecs_world_t *world,
    const char *name,
    float duration)
{
    ecs_entity_t clip = ecs_entity(world, { .name = name });
    ecs_set(world, clip, AnimationClip, { duration });

    return clip;
}

/** Read a file of the repository.
 *
 * @param file Path from the root of the repository
 * @return The contents, which the caller frees
 */
static char* anim_read(
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

/** Set a track that is expected to be invalid and check the error it logs.
 *
 * @param world The world
 * @param component The component
 * @param member The member path
 * @param keys The keys
 * @param count Number of keys
 * @param reason Text the error must contain
 */
static void anim_expect_error(
    ecs_world_t *world,
    ecs_entity_t component,
    const char *member,
    const CurveKey *keys,
    int32_t count,
    const char *reason)
{
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    anim_capture_begin();
    ecs_entity_t track = anim_track(world, clip, "track", component, member,
        CurveLinear, keys, count);
    anim_capture_end();

    test_assert(strstr(anim_errors, "'clip'") != NULL);
    test_assert(strstr(anim_errors, "'track'") != NULL);
    test_assert(strstr(anim_errors, reason) != NULL);
    test_assert(strchr(anim_errors, '\n') == strrchr(anim_errors, '\n'));

    float value = -1;
    test_bool(box_animation_sample(world, track, 0.5f, &value), false);
    test_flt(value, -1);
    test_flt(box_animation_clip_duration(world, clip), 0);
}

/** Two keys from 0 to 4 over two seconds */
static const CurveKey anim_ramp[] = {{0, 0}, {2, 4}};

/* Sampling */

void AnimationClip_sample_linear(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    ecs_entity_t track = anim_track(world, clip, "track", ecs_id(Position), "y",
        CurveLinear, anim_ramp, 2);

    float value;
    test_assert(box_animation_sample(world, track, 0, &value));
    test_flt(value, 0);
    test_assert(box_animation_sample(world, track, 1, &value));
    test_flt(value, 2);
    test_assert(box_animation_sample(world, track, 2, &value));
    test_flt(value, 4);
    test_assert(box_animation_sample(world, track, 0.5f, &value));
    test_flt(value, 1);

    ecs_fini(world);
}

void AnimationClip_sample_catmull_rom(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    CurveKey keys[] = {{0, 0}, {1, 2}, {2, 0}};
    ecs_entity_t track = anim_track(world, clip, "track", ecs_id(Position), "y",
        CurveCatmullRom, keys, 3);

    float value;
    test_assert(box_animation_sample(world, track, 0, &value));
    test_flt(value, 0);
    test_assert(box_animation_sample(world, track, 1, &value));
    test_flt(value, 2);
    test_assert(box_animation_sample(world, track, 2, &value));
    test_flt(value, 0);
    test_assert(box_animation_sample(world, track, 0.5f, &value));
    test_assert(fabsf(value - 1.125f) < 1e-5f);
    test_assert(box_animation_sample(world, track, 1.5f, &value));
    test_assert(fabsf(value - 1.125f) < 1e-5f);

    ecs_fini(world);
}

void AnimationClip_sample_bezier(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    CurveKey keys[] = {{0, 0, 0, 3}, {1, 1, 0, 0}};
    ecs_entity_t track = anim_track(world, clip, "track", ecs_id(Position), "y",
        CurveBezier, keys, 2);

    float value;
    test_assert(box_animation_sample(world, track, 0, &value));
    test_flt(value, 0);
    test_assert(box_animation_sample(world, track, 1, &value));
    test_flt(value, 1);
    test_assert(box_animation_sample(world, track, 0.5f, &value));
    test_assert(fabsf(value - 0.875f) < 1e-5f);

    ecs_fini(world);
}

void AnimationClip_sample_before_first(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    CurveKey keys[] = {{1, 3}, {2, 5}};
    ecs_entity_t track = anim_track(world, clip, "track", ecs_id(Position), "y",
        CurveCatmullRom, keys, 2);

    float value;
    test_assert(box_animation_sample(world, track, 0, &value));
    test_flt(value, 3);
    test_assert(box_animation_sample(world, track, -10, &value));
    test_flt(value, 3);

    ecs_fini(world);
}

void AnimationClip_sample_after_last(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    ecs_entity_t track = anim_track(world, clip, "track", ecs_id(Position), "y",
        CurveLinear, anim_ramp, 2);

    float value;
    test_assert(box_animation_sample(world, track, 3, &value));
    test_flt(value, 4);
    test_assert(box_animation_sample(world, track, 100, &value));
    test_flt(value, 4);

    ecs_fini(world);
}

void AnimationClip_sample_one_key(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    CurveKey keys[] = {{0.5f, 7}};
    ecs_entity_t track = anim_track(world, clip, "track", ecs_id(Position), "y",
        CurveCatmullRom, keys, 1);

    float value;
    test_assert(box_animation_sample(world, track, 0, &value));
    test_flt(value, 7);
    test_assert(box_animation_sample(world, track, 0.5f, &value));
    test_flt(value, 7);
    test_assert(box_animation_sample(world, track, 3, &value));
    test_flt(value, 7);
    test_flt(box_animation_clip_duration(world, clip), 0.5f);

    ecs_fini(world);
}

void AnimationClip_duration_from_keys(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    CurveKey keys[] = {{0, 0}, {3.5f, 1}};
    anim_track(world, clip, "a", ecs_id(Position), "x", CurveLinear, anim_ramp, 2);
    anim_track(world, clip, "b", ecs_id(Position), "y", CurveLinear, keys, 2);
    anim_track(world, clip, "c", ecs_id(Position), "z", CurveLinear, anim_ramp, 2);

    test_flt(box_animation_clip_duration(world, clip), 3.5f);

    ecs_entity_t empty = anim_clip(world, "empty", 0);
    test_flt(box_animation_clip_duration(world, empty), 0);

    ecs_fini(world);
}

void AnimationClip_duration_from_component(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t clip = anim_clip(world, "clip", 1.25f);
    anim_track(world, clip, "a", ecs_id(Position), "x", CurveLinear, anim_ramp, 2);

    test_flt(box_animation_clip_duration(world, clip), 1.25f);

    ecs_fini(world);
}

/* Resolving and writing */

/** Make a clip with one track, sample it at a time and write the value into an entity.
 *
 * @param world The world
 * @param e The animated entity
 * @param component The component
 * @param member The member path
 * @param kind The curve kind
 * @param keys The keys
 * @param count Number of keys
 * @param time The time
 * @return The sampled value
 */
static float anim_apply(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component,
    const char *member,
    CurveKind kind,
    const CurveKey *keys,
    int32_t count,
    float time)
{
    ecs_entity_t clip = ecs_new(world);
    ecs_set(world, clip, AnimationClip, { 0 });
    ecs_entity_t track = anim_track(world, clip, NULL, component, member, kind, keys, count);
    float value = 0;
    test_assert(box_animation_sample(world, track, time, &value));
    test_assert(box_animation_write(world, track, e, value));

    return value;
}

void AnimationClip_write_position_y(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    CurveKey keys[] = {{0, 0}, {1, 10}};

    anim_apply(world, e, ecs_id(Position), "y", CurveLinear, keys, 2, 0.5f);
    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 5);
    test_flt(p->z, 3);

    ecs_fini(world);
}

void AnimationClip_write_nested_member(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Emissive, {2, {10, 20, 30}});
    CurveKey keys[] = {{0, 0}, {1, 200}};

    anim_apply(world, e, ecs_id(Emissive), "color.r", CurveLinear, keys, 2, 0.5f);
    const Emissive *em = ecs_get(world, e, Emissive);
    test_flt(em->strength, 2);
    test_int(em->color.r, 100);
    test_int(em->color.g, 20);
    test_int(em->color.b, 30);

    ecs_fini(world);
}

void AnimationClip_write_rotation_y(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Rotation, {0.1f, 0, 0.3f});
    CurveKey keys[] = {{0, 0}, {2, 6.0f}};

    anim_apply(world, e, ecs_id(Rotation), "y", CurveLinear, keys, 2, 1.5f);
    const Rotation *r = ecs_get(world, e, Rotation);
    test_flt(r->x, 0.1f);
    test_flt(r->y, 4.5f);
    test_flt(r->z, 0.3f);

    ecs_fini(world);
}

void AnimationClip_write_double(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, AnimTest, { .d = 9, .i = 3 });
    CurveKey keys[] = {{0, 0}, {1, 1}};

    anim_apply(world, e, ecs_id(AnimTest), "d", CurveLinear, keys, 2, 0.25f);
    const AnimTest *t = ecs_get(world, e, AnimTest);
    test_flt(t->d, 0.25);
    test_int(t->i, 3);

    anim_apply(world, e, ecs_id(AnimTest), "pos.z", CurveLinear, keys, 2, 0.75f);
    t = ecs_get(world, e, AnimTest);
    test_flt(t->pos.z, 0.75f);
    test_flt(t->d, 0.25);

    ecs_fini(world);
}

void AnimationClip_write_int32(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, AnimTest, { .d = 9 });
    CurveKey keys[] = {{0, 0}, {1, 7}};

    anim_apply(world, e, ecs_id(AnimTest), "i", CurveLinear, keys, 2, 0.25f);
    test_int(ecs_get(world, e, AnimTest)->i, 2);
    anim_apply(world, e, ecs_id(AnimTest), "i", CurveLinear, keys, 2, 0.5f);
    test_int(ecs_get(world, e, AnimTest)->i, 4);

    CurveKey negative[] = {{0, 0}, {1, -7}};
    anim_apply(world, e, ecs_id(AnimTest), "i", CurveLinear, negative, 2, 0.5f);
    test_int(ecs_get(world, e, AnimTest)->i, -4);

    CurveKey huge[] = {{0, 0}, {1, 1e20f}};
    anim_apply(world, e, ecs_id(AnimTest), "i", CurveLinear, huge, 2, 1);
    test_int(ecs_get(world, e, AnimTest)->i, INT32_MAX);
    test_flt(ecs_get(world, e, AnimTest)->d, 9);

    ecs_fini(world);
}

void AnimationClip_write_rgb_round_clamp(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Rgb, {1, 2, 3});

    CurveKey up[] = {{0, 0}, {1, 255}, {2, 255}};
    float value = anim_apply(world, e, ecs_id(Rgb), "r", CurveCatmullRom, up, 3, 1.5f);
    test_assert(value > 255);
    test_int(ecs_get(world, e, Rgb)->r, 255);

    CurveKey down[] = {{0, 255}, {1, 0}, {2, 0}};
    value = anim_apply(world, e, ecs_id(Rgb), "b", CurveCatmullRom, down, 3, 1.5f);
    test_assert(value < 0);
    test_int(ecs_get(world, e, Rgb)->b, 0);

    CurveKey ramp[] = {{0, 0}, {1, 3}};
    value = anim_apply(world, e, ecs_id(Rgb), "g", CurveLinear, ramp, 2, 0.5f);
    test_flt(value, 1.5f);
    test_int(ecs_get(world, e, Rgb)->g, 2);
    value = anim_apply(world, e, ecs_id(Rgb), "g", CurveLinear, ramp, 2, 0.4f);
    test_int(ecs_get(world, e, Rgb)->g, 1);

    ecs_fini(world);
}

void AnimationClip_changed_track(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    CurveKey keys[] = {{0, 0}, {1, 10}};
    ecs_entity_t track = anim_track(world, clip, "track", ecs_id(Position), "y",
        CurveLinear, keys, 2);

    float value;
    test_assert(box_animation_sample(world, track, 0.5f, &value));
    test_flt(value, 5);
    test_flt(box_animation_clip_duration(world, clip), 1);

    CurveKey changed[] = {{0, 0}, {2, 40}};
    anim_track_set(world, track, ecs_id(Position), "x", CurveLinear, changed, 2);
    test_assert(box_animation_sample(world, track, 0.5f, &value));
    test_flt(value, 10);
    test_flt(box_animation_clip_duration(world, clip), 2);
    test_assert(box_animation_write(world, track, e, value));
    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 10);
    test_flt(p->y, 2);

    AnimationTrack *t = ecs_get_mut(world, track, AnimationTrack);
    ecs_os_free(t->member);
    t->member = ecs_os_strdup("z");
    ecs_modified(world, track, AnimationTrack);
    test_assert(box_animation_write(world, track, e, 7));
    p = ecs_get(world, e, Position);
    test_flt(p->x, 10);
    test_flt(p->z, 7);

    ecs_fini(world);
}

void AnimationClip_write_target(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t arm = ecs_entity(world, { .name = "arm", .parent = e });
    ecs_set(world, arm, Position, {0, 0, 0});
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    ecs_entity_t track = anim_track(world, clip, "track", ecs_id(Position), "x",
        CurveLinear, anim_ramp, 2);
    AnimationTrack *t = ecs_get_mut(world, track, AnimationTrack);
    t->target = ecs_os_strdup("arm");
    ecs_modified(world, track, AnimationTrack);

    test_assert(box_animation_write(world, track, e, 3));
    test_flt(ecs_get(world, arm, Position)->x, 3);
    test_assert(!ecs_has(world, e, Position));

    ecs_entity_t other = ecs_new(world);
    test_bool(box_animation_write(world, track, other, 3), false);

    ecs_fini(world);
}

/* Errors */

void AnimationClip_error_no_reflection(void) {
    ecs_world_t *world = anim_world();
    anim_expect_error(world, ecs_id(AnimPlain), "value", anim_ramp, 2, "has no reflection");
    ecs_fini(world);
}

void AnimationClip_error_unknown_member(void) {
    ecs_world_t *world = anim_world();
    anim_expect_error(world, ecs_id(Position), "w", anim_ramp, 2,
        "'w' is not a member of");
    ecs_fini(world);
}

void AnimationClip_error_struct_member(void) {
    ecs_world_t *world = anim_world();
    anim_expect_error(world, ecs_id(Emissive), "color", anim_ramp, 2, "is a struct");
    ecs_fini(world);
}

void AnimationClip_error_not_numeric(void) {
    ecs_world_t *world = anim_world();
    anim_expect_error(world, ecs_id(AnimTest), "flag", anim_ramp, 2, "a bool, not a number");
    ecs_fini(world);

    world = anim_world();
    anim_expect_error(world, ecs_id(AnimTest), "name", anim_ramp, 2,
        "a string, not a number");
    ecs_fini(world);
}

void AnimationClip_error_no_keys(void) {
    ecs_world_t *world = anim_world();
    anim_expect_error(world, ecs_id(Position), "y", NULL, 0, "curve has no keys");
    ecs_fini(world);
}

void AnimationClip_error_keys_out_of_order(void) {
    ecs_world_t *world = anim_world();
    CurveKey keys[] = {{0, 0}, {1, 1}, {0.5f, 2}};
    anim_expect_error(world, ecs_id(Position), "y", keys, 3, "key 2");
    ecs_fini(world);
}

void AnimationClip_error_deleted_component(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t temp = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Temp" }),
        .members = {{ .name = "v", .type = ecs_id(ecs_f32_t) }}
    });
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    ecs_entity_t track = anim_track(world, clip, "track", temp, "v",
        CurveLinear, anim_ramp, 2);

    float value;
    test_assert(box_animation_sample(world, track, 1, &value));
    test_flt(value, 2);

    ecs_delete(world, temp);
    test_bool(box_animation_sample(world, track, 1, &value), false);
    test_bool(box_animation_write(world, track, ecs_new(world), 1), false);

    anim_capture_begin();
    anim_track_set(world, track, temp, "v", CurveLinear, anim_ramp, 2);
    anim_capture_end();
    test_assert(strstr(anim_errors, "'clip'") != NULL);
    test_assert(strstr(anim_errors, "'track'") != NULL);
    test_assert(strstr(anim_errors, "is deleted") != NULL);
    test_bool(box_animation_sample(world, track, 1, &value), false);

    ecs_fini(world);
}

/* Script */

void AnimationClip_script(void) {
    ecs_world_t *world = anim_world();
    char *code = anim_read("test/assets/animation/bounce.flecs");
    anim_capture_begin();
    test_int(ecs_script_run(world, "bounce", code, NULL), 0);
    anim_capture_end();
    ecs_os_free(code);
    test_str(anim_errors, "");

    ecs_entity_t clip = ecs_lookup(world, "bounce");
    test_assert(clip != 0);
    test_flt(ecs_get(world, clip, AnimationClip)->duration, 2);
    test_flt(box_animation_clip_duration(world, clip), 2);

    ecs_entity_t height = ecs_lookup(world, "bounce.height");
    test_assert(height != 0);
    const AnimationTrack *t = ecs_get(world, height, AnimationTrack);
    test_uint(t->component, ecs_id(Position));
    test_str(t->member, "y");
    test_assert(t->target == NULL);
    test_int(t->curve.kind, CurveCatmullRom);
    test_int(ecs_vec_count(&t->curve.keys), 3);
    const CurveKey *k = ecs_vec_first_t(&t->curve.keys, CurveKey);
    test_flt(k[0].time, 0);
    test_flt(k[0].value, 0);
    test_flt(k[1].time, 1);
    test_flt(k[1].value, 2);
    test_flt(k[2].time, 2);
    test_flt(k[2].value, 0);

    ecs_entity_t red = ecs_lookup(world, "bounce.red");
    t = ecs_get(world, red, AnimationTrack);
    test_uint(t->component, ecs_id(Rgb));
    test_str(t->member, "r");
    test_int(t->curve.kind, CurveLinear);
    test_int(ecs_vec_count(&t->curve.keys), 2);
    k = ecs_vec_first_t(&t->curve.keys, CurveKey);
    test_flt(k[0].value, 255);
    test_flt(k[1].time, 2);
    test_flt(k[1].value, 0);

    ecs_entity_t blue = ecs_lookup(world, "bounce.blue");
    t = ecs_get(world, blue, AnimationTrack);
    test_uint(t->component, ecs_id(Rgb));
    test_str(t->member, "b");
    test_int(ecs_vec_count(&t->curve.keys), 2);

    float value;
    test_assert(box_animation_sample(world, height, 1, &value));
    test_flt(value, 2);
    test_assert(box_animation_sample(world, red, 1, &value));
    test_flt(value, 127.5f);
    test_assert(box_animation_sample(world, blue, 2, &value));
    test_flt(value, 255);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Rgb, {0, 0, 0});
    test_assert(box_animation_write(world, red, e, 127.5f));
    test_int(ecs_get(world, e, Rgb)->r, 128);

    ecs_fini(world);
}

/* Lifecycle */

/** Make a clip with tracks that own strings and keys, then delete it.
 *
 * @param world The world
 */
static void anim_clip_round(
    ecs_world_t *world)
{
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    CurveKey keys[] = {{0, 0}, {1, 2}, {2, 0}};
    ecs_entity_t a = anim_track(world, clip, "a", ecs_id(Position), "y",
        CurveCatmullRom, keys, 3);
    AnimationTrack *t = ecs_get_mut(world, a, AnimationTrack);
    t->target = ecs_os_strdup("child");
    ecs_modified(world, a, AnimationTrack);
    anim_track(world, clip, "b", ecs_id(Rgb), "r", CurveLinear, keys, 3);
    ecs_delete(world, clip);
}

void AnimationClip_delete_clip(void) {
    ecs_world_t *world = anim_world();
    anim_clip_round(world);
    anim_clip_round(world);

    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count - ecs_os_api_free_count;
    anim_clip_round(world);
    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count - ecs_os_api_free_count;
    test_int(after, before);
    test_assert(ecs_lookup(world, "clip") == 0);

    ecs_fini(world);
}

void AnimationClip_sample_deleted_track(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    ecs_entity_t track = anim_track(world, clip, "track", ecs_id(Position), "y",
        CurveLinear, anim_ramp, 2);
    ecs_entity_t other = anim_track(world, clip, "other", ecs_id(Position), "x",
        CurveLinear, anim_ramp, 2);

    float value;
    test_assert(box_animation_sample(world, track, 1, &value));
    ecs_delete(world, track);
    test_bool(box_animation_sample(world, track, 1, &value), false);
    test_bool(box_animation_write(world, track, ecs_new(world), 1), false);
    test_assert(box_animation_sample(world, other, 1, &value));

    ecs_remove(world, other, AnimationTrack);
    test_bool(box_animation_sample(world, other, 1, &value), false);

    ecs_delete(world, clip);
    test_bool(box_animation_sample(world, other, 1, &value), false);

    ecs_fini(world);
}

void AnimationClip_sample_no_alloc(void) {
    ecs_world_t *world = anim_world();
    ecs_entity_t clip = anim_clip(world, "clip", 0);
    CurveKey keys[] = {{0, 0}, {1, 2}, {2, 0}, {3, 5}};
    ecs_entity_t track = anim_track(world, clip, "track", ecs_id(Position), "y",
        CurveCatmullRom, keys, 4);
    float value, sum = 0;
    test_assert(box_animation_sample(world, track, 0.5f, &value));

    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    int32_t i;
    for (i = 0; i < 1000; i ++) {
        test_assert(box_animation_sample(world, track, (float)i * 0.004f - 0.5f, &value));
        sum += value;
    }

    float duration = box_animation_clip_duration(world, clip);
    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    test_int(after, before);
    test_assert(sum > 0);
    test_flt(duration, 3);

    ecs_fini(world);
}
