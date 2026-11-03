/**
 * @file Spatial.c
 * @brief Spatial audio of box.audio, measured through a null output: the distance curve, the
 * equal power pan, the listener, sources that follow an entity, one shots of
 * box_audio_play_at, loops out of range and the ramps that keep a jump from clicking.
 */

#include "world.h"

/** Frames the levels of a case are measured over, the second half of a frame */
#define SPATIAL_MEASURE (AUDIO_FRAME / 2)

/** Create an AudioListener at a place, turned by a yaw about Y.
 *
 * @param world The world
 * @param x X
 * @param z Z
 * @param yaw Yaw in radians
 * @return The listener
 */
static ecs_entity_t spatial_listener(
    ecs_world_t *world,
    float x,
    float z,
    float yaw)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, z});
    ecs_set(world, e, Rotation, {0, yaw, 0});
    ecs_add(world, e, AudioListener);

    return e;
}

/** Create a spatial source at a place.
 *
 * @param world The world
 * @param sound The sound
 * @param loop Whether it loops
 * @param x X
 * @param z Z
 * @param min min_distance
 * @param max max_distance
 * @param rolloff rolloff
 * @return The source
 */
static ecs_entity_t spatial_source(
    ecs_world_t *world,
    ecs_entity_t sound,
    bool loop,
    float x,
    float z,
    float min,
    float max,
    float rolloff)
{
    ecs_entity_t e = audio_source(world, sound, 1.0f, loop, 0);
    ecs_set(world, e, Position, {x, 0, z});
    ecs_set(world, e, AudioSpatial, {min, max, rolloff});

    return e;
}

/** Move an entity.
 *
 * @param world The world
 * @param e The entity
 * @param x X
 * @param z Z
 */
static void spatial_move(
    ecs_world_t *world,
    ecs_entity_t e,
    float x,
    float z)
{
    ecs_set(world, e, Position, {x, 0, z});
}

/** The gain the model gives at a distance.
 *
 * @param d Distance
 * @param min min_distance
 * @param max max_distance
 * @param rolloff rolloff
 * @return The gain
 */
static float spatial_expected(
    float d,
    float min,
    float max,
    float rolloff)
{
    float g = d <= min ? 1.0f : min / (min + rolloff * (d - min));
    float end = min / (min + rolloff * (max - min));

    return d >= max ? 0.0f : (g - end) / (1.0f - end);
}

/** Check that the last frames of both channels are at levels.
 *
 * @param left Expected mean of the left channel
 * @param right Expected mean of the right channel
 */
static void spatial_expect(
    float left,
    float right)
{
    float l = audio_level(SPATIAL_MEASURE), r = audio_level_right(SPATIAL_MEASURE);
    if (fabsf(l - left) > 1e-4f || fabsf(r - right) > 1e-4f) {
        printf("left %f (expected %f), right %f (expected %f)\n", (double)l, (double)left,
            (double)r, (double)right);
    }

    test_assert(fabsf(l - left) <= 1e-4f);
    test_assert(fabsf(r - right) <= 1e-4f);
}

/** The levels of each channel of a DC of 0.5 at a pan, as the core pans.
 *
 * @param pan The pan
 * @param gain The gain
 * @param left Receives the left level
 * @param right Receives the right level
 */
static void spatial_pan(
    float pan,
    float gain,
    float *left,
    float *right)
{
    double a = (pan + 1.0) * 0.25 * AUDIO_PI;
    *left = (float)(0.5 * gain * sqrt(2.0) * cos(a));
    *right = (float)(0.5 * gain * sqrt(2.0) * sin(a));
}

void Spatial_distance_curve(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    spatial_listener(world, 0, 0, 0);
    ecs_entity_t src = spatial_source(world, dc, true, 0, -2, 2, 20, 2);
    audio_run(world, 3);
    spatial_expect(0.5f, 0.5f);

    spatial_move(world, src, 0, -1);
    audio_run(world, 3);
    spatial_expect(0.5f, 0.5f);

    spatial_move(world, src, 0, -11);
    audio_run(world, 3);
    float mid = 0.5f * spatial_expected(11, 2, 20, 2);
    test_assert(fabsf(mid - 0.025f) < 1e-4f);
    spatial_expect(mid, mid);

    spatial_move(world, src, 0, -5);
    audio_run(world, 3);
    float near_ = 0.5f * spatial_expected(5, 2, 20, 2);
    spatial_expect(near_, near_);
    test_assert(near_ > mid);

    ecs_set(world, src, AudioSpatial, {2, 20, 0.5f});
    audio_run(world, 3);
    float soft = 0.5f * spatial_expected(5, 2, 20, 0.5f);
    spatial_expect(soft, soft);
    test_assert(soft > near_);

    spatial_move(world, src, 0, -20);
    audio_run(world, 3);
    spatial_expect(0, 0);
    ecs_fini(world);
}

void Spatial_silent_at_max(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    spatial_listener(world, 0, 0, 0);
    ecs_entity_t loop = spatial_source(world, dc, true, 0, -20, 1, 20, 1);
    audio_run(world, 3);
    test_assert(audio_peak(audio_tap_l, 0, audio_tap_count) == 0.0f);
    test_assert(audio_peak(audio_tap_r, 0, audio_tap_count) == 0.0f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_int(ecs_get(world, loop, AudioVoice)->handle, 0);
    test_bool(ecs_get(world, loop, AudioSource)->playing, true);

    ecs_entity_t once = spatial_source(world, dc, false, 30, 0, 1, 20, 1);
    audio_run(world, 2);
    test_assert(audio_peak(audio_tap_l, 0, audio_tap_count) == 0.0f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_bool(ecs_get(world, once, AudioSource)->playing, false);
    test_assert(box_audio_module_stats(world).out_of_range >= 1);

    spatial_move(world, loop, 0, -19.5f);
    audio_run(world, 3);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_assert(audio_level(SPATIAL_MEASURE) > 0);
    test_assert(audio_level(SPATIAL_MEASURE) < 0.01f);
    ecs_fini(world);
}

void Spatial_pan_left_right_center(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t listener = spatial_listener(world, 0, 0, 0);
    ecs_entity_t src = spatial_source(world, dc, true, 5, 0, 1, 50, 0);
    float l, r;
    audio_run(world, 3);
    spatial_pan(1, 1, &l, &r);
    spatial_expect(l, r);
    test_assert(r > 0.7f && l < 1e-4f);

    spatial_move(world, src, -5, 0);
    audio_run(world, 3);
    spatial_pan(-1, 1, &l, &r);
    spatial_expect(l, r);

    spatial_move(world, src, 0, -5);
    audio_run(world, 3);
    spatial_expect(0.5f, 0.5f);

    spatial_move(world, src, 0, 5);
    audio_run(world, 3);
    spatial_expect(0.5f, 0.5f);

    spatial_move(world, src, 5, -5);
    audio_run(world, 3);
    spatial_pan((float)(1.0 / sqrt(2.0)), 1, &l, &r);
    spatial_expect(l, r);
    test_assert(fabsf(l * l + r * r - 0.5f) < 1e-4f);

    spatial_move(world, src, 0, -5);
    ecs_set(world, listener, Rotation, {0, (float)(AUDIO_PI / 2), 0});
    audio_run(world, 3);
    spatial_pan(1, 1, &l, &r);
    spatial_expect(l, r);

    ecs_set(world, listener, Rotation, {0, 0, 0});
    spatial_move(world, listener, 10, -5);
    audio_run(world, 3);
    spatial_pan(-1, 1, &l, &r);
    spatial_expect(l, r);
    ecs_fini(world);
}

void Spatial_follows_parent(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    spatial_listener(world, 0, 0, 0);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t src = spatial_source(world, dc, true, 2, 0, 1, 50, 0);
    ecs_add_pair(world, src, EcsChildOf, parent);
    float l, r;
    audio_run(world, 3);
    spatial_pan(1, 1, &l, &r);
    spatial_expect(l, r);

    spatial_move(world, parent, -10, 0);
    audio_run(world, 3);
    spatial_pan(-1, 1, &l, &r);
    spatial_expect(l, r);

    spatial_move(world, parent, -100, 0);
    audio_run(world, 3);
    spatial_expect(0, 0);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

void Spatial_follows_listener(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_add(world, camera, Camera);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, { .camera = camera });
    spatial_source(world, dc, true, 5, 0, 1, 50, 0);
    float l, r;
    audio_run(world, 3);
    spatial_pan(1, 1, &l, &r);
    spatial_expect(l, r);

    spatial_move(world, camera, 10, 0);
    audio_run(world, 3);
    spatial_pan(-1, 1, &l, &r);
    spatial_expect(l, r);

    ecs_set(world, camera, Rotation, {0, (float)AUDIO_PI, 0});
    audio_run(world, 3);
    spatial_pan(1, 1, &l, &r);
    spatial_expect(l, r);
    ecs_fini(world);
}

void Spatial_listener_default_view(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t near_cam = ecs_new(world);
    ecs_set(world, near_cam, Position, {0, 0, 0});
    ecs_entity_t far_cam = ecs_new(world);
    ecs_set(world, far_cam, Position, {10, 0, 0});
    ecs_entity_t overlay = ecs_new(world);
    ecs_set(world, overlay, View, { .camera = far_cam, .order = 5 });
    ecs_entity_t game = ecs_new(world);
    ecs_set(world, game, View, { .camera = near_cam, .order = 0 });
    ecs_entity_t empty = ecs_new(world);
    ecs_set(world, empty, View, { .camera = 0, .order = -10 });
    spatial_source(world, dc, true, 5, 0, 1, 50, 0);
    float l, r;
    audio_run(world, 3);

    spatial_pan(1, 1, &l, &r);
    spatial_expect(l, r);

    ecs_enable(world, game, false);
    audio_run(world, 3);
    spatial_pan(-1, 1, &l, &r);
    spatial_expect(l, r);

    ecs_enable(world, game, true);
    audio_run(world, 3);
    spatial_pan(1, 1, &l, &r);
    spatial_expect(l, r);
    ecs_fini(world);
}

void Spatial_listener_explicit(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {10, 0, 0});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, { .camera = camera });
    ecs_entity_t listener = spatial_listener(world, 0, 0, 0);
    spatial_source(world, dc, true, 5, 0, 1, 50, 0);
    float l, r;
    audio_run(world, 3);

    spatial_pan(1, 1, &l, &r);
    spatial_expect(l, r);

    ecs_remove(world, listener, AudioListener);
    audio_run(world, 3);
    spatial_pan(-1, 1, &l, &r);
    spatial_expect(l, r);
    ecs_fini(world);
}

void Spatial_listener_two_warns(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t first = spatial_listener(world, 0, 0, 0);
    ecs_entity_t second = spatial_listener(world, 10, 0, 0);
    test_assert(first < second);
    spatial_source(world, dc, true, 5, 0, 1, 50, 0);
    float l, r;
    audio_log_start();
    audio_run(world, 10);
    audio_log_stop();
    test_int(audio_log_warnings, 1);
    test_int(audio_log_errors, 0);

    spatial_pan(1, 1, &l, &r);
    spatial_expect(l, r);
    ecs_fini(world);
}

void Spatial_no_listener(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.25f, AUDIO_RATE);
    audio_log_start();
    spatial_source(world, dc, true, 30, 0, 1, 50, 1);
    spatial_source(world, dc, true, -100, 0, 1, 50, 1);
    audio_run(world, 3);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_int(audio_log_warnings, 0);

    test_assert(fabsf(audio_level(SPATIAL_MEASURE) - 0.5f) < 1e-4f);
    test_assert(fabsf(audio_level_right(SPATIAL_MEASURE) - 0.5f) < 1e-4f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 2);
    ecs_fini(world);
}

void Spatial_at_listener(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t listener = spatial_listener(world, 3, 4, 0.5f);
    ecs_entity_t src = spatial_source(world, dc, true, 3, 4, 1, 50, 1);
    int32_t i;
    audio_run(world, 3);
    spatial_expect(0.5f, 0.5f);

    ecs_set(world, src, AudioSpatial, {0, 50, 1});
    ecs_set(world, listener, Scale, {0, 0, 0});
    audio_run(world, 3);
    for (i = 0; i < audio_tap_count; i ++) {
        test_assert(audio_tap_l[i] == audio_tap_l[i]);
        test_assert(audio_tap_r[i] == audio_tap_r[i]);
    }

    test_assert(fabsf(audio_level(SPATIAL_MEASURE) - audio_level_right(SPATIAL_MEASURE)) < 1e-6f);
    test_assert(audio_level(SPATIAL_MEASURE) > 0.4f);
    ecs_fini(world);
}

void Spatial_play_at(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE * 2);
    ecs_set(world, dc, AudioSpatial, {1, 50, 0});
    ecs_entity_t listener = spatial_listener(world, 0, 0, 0);
    float l, r;
    audio_run(world, 1);
    test_bool(box_audio_play_at(world, dc, (vec3){5, 0, 0}, 1.0f), true);
    audio_run(world, 3);
    spatial_pan(1, 1, &l, &r);
    spatial_expect(l, r);

    spatial_move(world, listener, 10, 0);
    audio_run(world, 3);
    spatial_pan(-1, 1, &l, &r);
    spatial_expect(l, r);

    int64_t skipped = box_audio_module_stats(world).out_of_range;
    test_bool(box_audio_play_at(world, dc, (vec3){100, 0, 0}, 1.0f), true);
    audio_run(world, 2);
    test_int(box_audio_module_stats(world).out_of_range, skipped + 1);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);

    audio_run(world, 120);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    spatial_expect(0, 0);
    ecs_fini(world);
}

void Spatial_out_of_range_loop(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    float *ramp = ecs_os_malloc_n(float, AUDIO_RATE);
    int32_t i, from, to;
    for (i = 0; i < AUDIO_RATE; i ++) {
        ramp[i] = 0.5f * (float)i / (float)AUDIO_RATE;
    }

    ecs_entity_t saw = box_audio_sound_from_pcm(world, 0, ramp, AUDIO_RATE, 1, AUDIO_RATE);
    ecs_os_free(ramp);
    spatial_listener(world, 0, 0, 0);
    ecs_entity_t src = spatial_source(world, saw, true, 0, -5, 1, 50, 0);
    audio_run(world, 6);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);

    spatial_move(world, src, 0, -100);
    audio_run(world, 2);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_int(ecs_get(world, src, AudioVoice)->handle, 0);
    audio_run(world, 12);
    spatial_expect(0, 0);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    spatial_move(world, src, 0, -5);
    audio_run(world, 5);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    from = audio_tap_count - SPATIAL_MEASURE;
    to = audio_tap_count;
    test_assert(to < AUDIO_RATE);
    for (i = from; i < to; i ++) {
        float expected = 0.5f * (float)(i % AUDIO_RATE) / (float)AUDIO_RATE;
        if (fabsf(audio_tap_l[i] - expected) > 2e-3f) {
            printf("frame %d: %f, expected %f\n", i, (double)audio_tap_l[i], (double)expected);
        }

        test_assert(fabsf(audio_tap_l[i] - expected) <= 2e-3f);
    }

    test_bool(ecs_get(world, src, AudioSource)->playing, true);
    ecs_fini(world);
}

void Spatial_teleport_no_click(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t listener = spatial_listener(world, 0, 0, 0);
    ecs_entity_t src = spatial_source(world, dc, true, -3, 0, 1, 50, 0);
    int32_t from;
    audio_run(world, 3);

    from = audio_tap_count;
    spatial_move(world, src, 3, 0);
    audio_run(world, 2);
    test_assert(audio_max_step(audio_tap_l, from, audio_tap_count) < 0.01f);
    test_assert(audio_max_step(audio_tap_r, from, audio_tap_count) < 0.01f);

    from = audio_tap_count;
    ecs_set(world, src, AudioSpatial, {1, 50, 1});
    spatial_move(world, src, 0, -1);
    audio_run(world, 2);
    spatial_move(world, src, 0, -40);
    audio_run(world, 2);
    test_assert(audio_max_step(audio_tap_l, from, audio_tap_count) < 0.01f);
    test_assert(audio_max_step(audio_tap_r, from, audio_tap_count) < 0.01f);

    from = audio_tap_count;
    spatial_move(world, listener, 0, -1);
    audio_run(world, 2);
    spatial_move(world, listener, 1000, 0);
    audio_run(world, 2);
    spatial_move(world, listener, 0, 0);
    audio_run(world, 2);
    test_assert(audio_max_step(audio_tap_l, from, audio_tap_count) < 0.01f);
    test_assert(audio_max_step(audio_tap_r, from, audio_tap_count) < 0.01f);
    ecs_fini(world);
}

void Spatial_disabled_silent(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    spatial_listener(world, 0, 0, 0);
    ecs_entity_t src = spatial_source(world, dc, true, 0, -2, 2, 50, 1);
    audio_run(world, 3);
    spatial_expect(0.5f, 0.5f);

    ecs_enable(world, src, false);
    audio_run(world, 3);
    spatial_expect(0, 0);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);

    spatial_move(world, src, 0, -1);
    audio_run(world, 3);
    spatial_expect(0, 0);

    ecs_enable(world, src, true);
    audio_run(world, 3);
    spatial_expect(0.5f, 0.5f);
    ecs_fini(world);
}

void Spatial_core_position(void) {
    // audio/spatial
    box_audio_core_t *core = audio_new();
    int32_t i;
    for (i = 0; i < AUDIO_RATE; i ++) {
        audio_sound_a[i] = (float)i / (float)AUDIO_RATE;
    }

    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_POSITION, (float)(AUDIO_RATE + 1000));
    audio_render(core, 0, 1024);
    test_assert(fabsf(audio_l[1000] - (float)2000 / AUDIO_RATE) < 1e-4f);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);

    audio_voice_set(core, 1, BOX_AUDIO_PARAM_POSITION, NAN);
    audio_render(core, 0, 1024);
    test_assert(fabsf(audio_l[1000] - (float)1000 / AUDIO_RATE) < 1e-4f);
    box_audio_core_free(core);
}

void Spatial_cost_changed_only(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.01f, AUDIO_RATE);
    ecs_entity_t listener = spatial_listener(world, 0, 0, 0);
    ecs_entity_t parent = ecs_new(world);
    ecs_entity_t sources[100], dynamic;
    int64_t before;
    int32_t i;
    ecs_set(world, parent, Position, {0, 0, 0});
    for (i = 0; i < 100; i ++) {
        sources[i] = spatial_source(world, dc, true, (float)i, -200, 1, 50, 1);
    }

    ecs_add_pair(world, sources[99], EcsChildOf, parent);
    audio_run(world, 3);

    before = box_audio_module_stats(world).spatial_updates;
    audio_run(world, 3);
    test_int(box_audio_module_stats(world).spatial_updates, before);

    spatial_move(world, sources[3], 3, -10);
    audio_run(world, 2);
    test_assert(box_audio_module_stats(world).spatial_updates - before >= 1);
    test_assert(box_audio_module_stats(world).spatial_updates - before <= 2);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);

    before = box_audio_module_stats(world).spatial_updates;
    spatial_move(world, parent, -99, 190);
    audio_run(world, 2);
    test_assert(box_audio_module_stats(world).spatial_updates - before >= 1);
    test_assert(box_audio_module_stats(world).spatial_updates - before <= 2);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 2);

    before = box_audio_module_stats(world).spatial_updates;
    spatial_move(world, listener, 1, 0);
    audio_run(world, 1);
    test_int(box_audio_module_stats(world).spatial_updates - before, 100);

    dynamic = spatial_source(world, dc, true, 0, -300, 1, 50, 1);
    ecs_add(world, dynamic, DynamicTransform);
    audio_run(world, 2);
    before = box_audio_module_stats(world).spatial_updates;
    audio_run(world, 3);
    test_assert(box_audio_module_stats(world).spatial_updates - before <= 3);
    ecs_fini(world);
}
