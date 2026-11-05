/**
 * @file Spatial.c
 * @brief Spatial audio of box.audio, measured through a null output: the distance curve, the
 * equal power pan, the listener, sources that follow an entity, one shots of
 * box_audio_play_at of every kind of sound, loops out of range, the ramps that keep a jump
 * from clicking and the distance filter.
 */

#include "world.h"
#include "../../../../src/jobs/jobs.h"

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

void Spatial_play_at_synth_and_file(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    test_int(box_jobs_set_workers(world, 0), 0);
    test_assert(ecs_script_run(world, "beep",
        "using box.audio\n"
        "prefab Beep {\n"
        "  SynthSound: {}\n"
        "  SynthEnvelope: {attack: 0.001, sustain: 1, hold: 0.5, release: 0.01}\n"
        "  SynthOscillator: {frequency: 500, level: 0.5}\n"
        "  AudioSpatial: {min_distance: 1, max_distance: 50, rolloff: 0}\n"
        "}\n"
        "chime {\n"
        "  SoundFile: {path: \"test/assets/audio/sine_16.wav\"}\n"
        "  AudioSpatial: {min_distance: 1, max_distance: 50, rolloff: 0}\n"
        "}\n", NULL) == 0);
    ecs_entity_t sounds[] = { ecs_lookup(world, "Beep"), ecs_lookup(world, "chime") };
    int32_t i, start;
    spatial_listener(world, 0, 0, 0);
    audio_run(world, 2);
    test_int(ecs_get(world, sounds[1], SoundStatus)->state, SoundReady);
    for (i = 0; i < 2; i ++) {
        test_bool(box_audio_play_at(world, sounds[i], (vec3){-5, 0, 0}, 1.0f), true);
        start = audio_tap_count;
        audio_run(world, 6);
        test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.6f);
        test_assert(audio_peak(audio_tap_r, start, audio_tap_count) < 1e-4f);
        audio_run(world, 60);
        test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    }

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

/** Frames the levels of a case of the distance filter are measured over, four frames */
#define SPATIAL_FILTER_MEASURE (AUDIO_FRAME * 4)

/** Create a spatial source with a distance filter at a place in front of the listener.
 *
 * @param world The world
 * @param sound The sound
 * @param d Distance in front of the listener at the origin
 * @param min min_distance
 * @param max max_distance
 * @param cutoff cutoff
 * @return The source, which loops and does not fall off with distance
 */
static ecs_entity_t spatial_filtered(
    ecs_world_t *world,
    ecs_entity_t sound,
    float d,
    float min,
    float max,
    float cutoff)
{
    ecs_entity_t e = audio_source(world, sound, 1.0f, true, 0);
    ecs_set(world, e, Position, {0, 0, -d});
    ecs_set(world, e, AudioSpatial, {min, max, 0, cutoff});

    return e;
}

/** Create a mono sound of white noise from a fixed seed.
 *
 * @param world The world
 * @param level Peak level
 * @param frames Frames of the sound
 * @return The sound
 */
static ecs_entity_t spatial_noise(
    ecs_world_t *world,
    float level,
    int32_t frames)
{
    float *data = ecs_os_malloc_n(float, frames);
    uint32_t seed = 12345;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        seed = seed * 1664525u + 1013904223u;
        data[i] = level * ((float)(seed >> 8) / 8388608.0f - 1.0f);
    }

    ecs_entity_t e = box_audio_sound_from_pcm(world, 0, data, frames, 1, AUDIO_RATE);
    ecs_os_free(data);
    test_assert(e != 0);

    return e;
}

/** Root mean square of a range.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The RMS
 */
static float spatial_rms(
    const float *x,
    int32_t from,
    int32_t to)
{
    double sum = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        sum += (double)x[i] * (double)x[i];
    }

    return to > from ? (float)sqrt(sum / (double)(to - from)) : 0.0f;
}

/** Gain of the 12 dB per octave low-pass of the core at a frequency: a Butterworth response
 * through the bilinear transform, prewarped at the cutoff.
 *
 * @param f Frequency in Hz
 * @param fc Cutoff in Hz
 * @return The gain
 */
static double spatial_lowpass(
    double f,
    double fc)
{
    double w = tan(AUDIO_PI * f / AUDIO_RATE) / tan(AUDIO_PI * fc / AUDIO_RATE);

    return 1.0 / sqrt(1.0 + w * w * w * w);
}

/** Play a tone from a distance through a filtered source and measure the RMS of the left
 * channel once it settled. The source is deleted after.
 *
 * @param world The world
 * @param frequency Hz of the tone, a whole number so that it loops
 * @param d Distance
 * @param spatial The distances and the cutoff
 * @return The RMS
 */
static float spatial_tone_rms(
    ecs_world_t *world,
    double frequency,
    float d,
    const AudioSpatial *spatial)
{
    ecs_entity_t tone = audio_tone(world, NULL, frequency, 0.5f, AUDIO_RATE);
    ecs_entity_t src = spatial_filtered(world, tone, d, spatial->min_distance,
        spatial->max_distance, spatial->cutoff);
    float result;
    audio_run(world, 7);
    result = spatial_rms(audio_tap_l, audio_tap_count - SPATIAL_FILTER_MEASURE,
        audio_tap_count);
    ecs_delete(world, src);
    audio_run(world, 2);

    return result;
}

/** Check that every sample of the tap since a frame is finite and within a bound.
 *
 * @param from First frame
 * @param bound Largest magnitude
 */
static void spatial_bounded(
    int32_t from,
    float bound)
{
    int32_t i;
    for (i = from; i < audio_tap_count; i ++) {
        test_assert(isfinite(audio_tap_l[i]));
        test_assert(isfinite(audio_tap_r[i]));
    }

    test_assert(audio_peak(audio_tap_l, from, audio_tap_count) <= bound);
    test_assert(audio_peak(audio_tap_r, from, audio_tap_count) <= bound);
}

void Spatial_cutoff_curve(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    AudioSpatial s = { .min_distance = 2, .max_distance = 20, .rolloff = 0, .cutoff = 500 };
    double open = 0.5 / sqrt(2.0), mid, end, expected;
    float rms;
    spatial_listener(world, 0, 0, 0);

    test_flt(box_audio_spatial_cutoff(&s, 1), 0);
    test_flt(box_audio_spatial_cutoff(&s, 2), 0);
    test_assert(fabsf(box_audio_spatial_cutoff(&s, 20) - 500.0f) < 0.5f);
    test_assert(fabsf(box_audio_spatial_cutoff(&s, 100) - 500.0f) < 0.5f);
    mid = sqrt(20000.0 * 500.0);
    test_assert(fabs(box_audio_spatial_cutoff(&s, 11) - mid) < 1.0);
    test_assert(box_audio_spatial_cutoff(&s, 5) > box_audio_spatial_cutoff(&s, 11));
    test_assert(box_audio_spatial_cutoff(&s, 11) > box_audio_spatial_cutoff(&s, 17));
    test_flt(box_audio_spatial_cutoff(NULL, 30), 0);

    /* Within min_distance the filter is open: a high tone passes as is */
    rms = spatial_tone_rms(world, 8000, 1, &s);
    test_assert(fabs(rms - open) < 0.01 * open);
    rms = spatial_tone_rms(world, 8000, 2, &s);
    test_assert(fabs(rms - open) < 0.01 * open);

    /* Midway a tone at the cutoff is 3 dB down */
    mid = box_audio_spatial_cutoff(&s, 11);
    expected = open * spatial_lowpass(3162, mid);
    rms = spatial_tone_rms(world, 3162, 11, &s);
    if (fabs(rms - expected) > 0.03 * expected) {
        printf("midway %f, expected %f\n", (double)rms, expected);
    }

    test_assert(fabs(rms - expected) < 0.03 * expected);

    /* Close to max_distance a tone at cutoff is 3 dB down, and an octave above 12 dB down */
    end = box_audio_spatial_cutoff(&s, 19.9f);
    test_assert(end > 500 && end < 520);
    expected = open * spatial_lowpass(500, end);
    rms = spatial_tone_rms(world, 500, 19.9f, &s);
    if (fabs(rms - expected) > 0.03 * expected) {
        printf("end %f, expected %f\n", (double)rms, expected);
    }

    test_assert(fabs(rms - expected) < 0.03 * expected);
    expected = open * spatial_lowpass(1000, end);
    rms = spatial_tone_rms(world, 1000, 19.9f, &s);
    test_assert(fabs(rms - expected) < 0.03 * expected);
    test_assert(rms < open * 0.3);
    ecs_fini(world);
}

/** Run a moving noise source with a set of distances and keep the tap.
 *
 * @param spatial The distances
 * @param out Receives the left channel, AUDIO_FRAME * 30 frames
 * @param out_r Receives the right channel
 */
static void spatial_noise_run(
    const AudioSpatial *spatial,
    float *out,
    float *out_r)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t noise = spatial_noise(world, 0.5f, AUDIO_RATE);
    spatial_listener(world, 0, 0, 0);
    ecs_entity_t src = audio_source(world, noise, 1.0f, true, 0);
    int32_t i;
    ecs_set(world, src, Position, {0, 0, -1});
    ecs_set_ptr(world, src, AudioSpatial, spatial);
    for (i = 0; i < 30; i ++) {
        spatial_move(world, src, (float)(i % 7) - 3.0f, -1.0f - (float)i);
        audio_run(world, 1);
    }

    test_int(audio_tap_count, AUDIO_FRAME * 30);
    ecs_os_memcpy_n(out, audio_tap_l, float, audio_tap_count);
    ecs_os_memcpy_n(out_r, audio_tap_r, float, audio_tap_count);
    ecs_fini(world);
}

void Spatial_cutoff_zero_unchanged(void) {
    // audio/spatial
    int32_t n = AUDIO_FRAME * 30, k;
    float *ref = ecs_os_malloc_n(float, n), *ref_r = ecs_os_malloc_n(float, n);
    float *run = ecs_os_malloc_n(float, n), *run_r = ecs_os_malloc_n(float, n);
    AudioSpatial base = { .min_distance = 2, .max_distance = 50, .rolloff = 1 };
    float cutoffs[] = { 0, -10, -INFINITY, NAN, 20000, 1e9f };
    spatial_noise_run(&base, ref, ref_r);
    test_assert(audio_peak(ref, 0, n) > 0.05f);

    for (k = 0; k < (int32_t)(sizeof(cutoffs) / sizeof(cutoffs[0])); k ++) {
        AudioSpatial s = base;
        s.cutoff = cutoffs[k];
        spatial_noise_run(&s, run, run_r);
        test_assert(audio_hash(run, n) == audio_hash(ref, n));
        test_assert(audio_hash(run_r, n) == audio_hash(ref_r, n));
    }

    /* A filtered source differs */
    {
        AudioSpatial s = base;
        s.cutoff = 300;
        spatial_noise_run(&s, run, run_r);
        test_assert(audio_hash(run, n) != audio_hash(ref, n));
    }

    ecs_os_free(ref);
    ecs_os_free(ref_r);
    ecs_os_free(run);
    ecs_os_free(run_r);
}

void Spatial_cutoff_moving_no_click(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t tone = audio_tone(world, NULL, 100, 0.5f, AUDIO_RATE);
    spatial_listener(world, 0, 0, 0);
    ecs_entity_t src = spatial_filtered(world, tone, 1, 1, 50, 120);
    float bound = 0.5f * 2.0f * (float)AUDIO_PI * 100.0f / AUDIO_RATE * 1.5f;
    int32_t from, i;
    audio_run(world, 3);

    from = audio_tap_count;
    for (i = 0; i < 60; i ++) {
        float d = (i & 1) ? 1.0f + (float)(i % 13) * 3.5f : 45.0f - (float)(i % 11) * 4.0f;
        spatial_move(world, src, (float)(i % 5) - 2.0f, -d);
        audio_run(world, 1);
    }

    if (audio_max_step(audio_tap_l, from, audio_tap_count) >= bound) {
        printf("step %f, bound %f\n",
            (double)audio_max_step(audio_tap_l, from, audio_tap_count), (double)bound);
    }

    test_assert(audio_max_step(audio_tap_l, from, audio_tap_count) < bound);
    test_assert(audio_max_step(audio_tap_r, from, audio_tap_count) < bound);
    spatial_bounded(from, 0.75f);
    ecs_fini(world);
}

void Spatial_cutoff_teleport_no_click(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t tone = audio_tone(world, NULL, 100, 0.5f, AUDIO_RATE);
    ecs_entity_t listener = spatial_listener(world, 0, 0, 0);
    ecs_entity_t src = spatial_filtered(world, tone, 0.5f, 1, 50, 60);
    float bound = 0.5f * 2.0f * (float)AUDIO_PI * 100.0f / AUDIO_RATE * 1.5f;
    int32_t from;
    audio_run(world, 3);

    from = audio_tap_count;
    spatial_move(world, src, 0, -49);
    audio_run(world, 3);
    spatial_move(world, src, 0, -0.5f);
    audio_run(world, 3);
    test_assert(audio_max_step(audio_tap_l, from, audio_tap_count) < bound);
    test_assert(audio_max_step(audio_tap_r, from, audio_tap_count) < bound);

    from = audio_tap_count;
    spatial_move(world, listener, 0, -48);
    audio_run(world, 3);
    spatial_move(world, listener, 30, 30);
    audio_run(world, 3);
    spatial_move(world, listener, 0, 0);
    audio_run(world, 3);
    test_assert(audio_max_step(audio_tap_l, from, audio_tap_count) < bound);
    test_assert(audio_max_step(audio_tap_r, from, audio_tap_count) < bound);
    spatial_bounded(from, 0.75f);
    ecs_fini(world);
}

void Spatial_cutoff_play_at(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t tone = audio_tone(world, NULL, 8000, 0.5f, AUDIO_RATE * 3);
    ecs_set(world, tone, AudioSpatial, {1, 50, 0, 200});
    ecs_entity_t listener = spatial_listener(world, 0, 0, 0);
    double open = 0.5 / sqrt(2.0);
    int32_t from;
    audio_run(world, 1);

    /* Near, the one shot is bright */
    from = audio_tap_count;
    test_bool(box_audio_play_at(world, tone, (vec3){0, 0, -0.5f}, 1.0f), true);
    audio_run(world, 6);
    test_assert(fabs(spatial_rms(audio_tap_l, audio_tap_count - SPATIAL_FILTER_MEASURE,
        audio_tap_count) - open) < 0.01 * open);
    test_assert(audio_peak(audio_tap_l, from, audio_tap_count) > 0.45f);
    audio_run(world, 200);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    /* Far, it is dark from its first frame */
    from = audio_tap_count;
    test_bool(box_audio_play_at(world, tone, (vec3){0, 0, -49}, 1.0f), true);
    audio_run(world, 6);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_assert(audio_peak(audio_tap_l, from, audio_tap_count) < 0.01f);
    test_assert(audio_peak(audio_tap_r, from, audio_tap_count) < 0.01f);

    /* The listener walks up to it and it opens */
    spatial_move(world, listener, 0, -48.8f);
    audio_run(world, 6);
    test_assert(fabs(spatial_rms(audio_tap_l, audio_tap_count - SPATIAL_FILTER_MEASURE,
        audio_tap_count) - open) < 0.01 * open);
    ecs_fini(world);
}

void Spatial_cutoff_stereo(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    float *data = ecs_os_malloc_n(float, AUDIO_RATE * 2);
    double open = 0.5 / sqrt(2.0);
    int32_t i;
    for (i = 0; i < AUDIO_RATE; i ++) {
        data[i * 2] = 0.5f * (float)sin(2 * AUDIO_PI * 8000.0 * i / AUDIO_RATE);
        data[i * 2 + 1] = 0.5f * (float)sin(2 * AUDIO_PI * 6000.0 * i / AUDIO_RATE);
    }

    ecs_entity_t sound = box_audio_sound_from_pcm(world, 0, data, AUDIO_RATE, 2, AUDIO_RATE);
    ecs_os_free(data);
    spatial_listener(world, 0, 0, 0);
    ecs_entity_t src = spatial_filtered(world, sound, 0.5f, 1, 50, 200);
    float l, r;
    audio_run(world, 7);
    l = spatial_rms(audio_tap_l, audio_tap_count - SPATIAL_FILTER_MEASURE, audio_tap_count);
    r = spatial_rms(audio_tap_r, audio_tap_count - SPATIAL_FILTER_MEASURE, audio_tap_count);
    test_assert(l > 0.5 * open);
    test_assert(r > 0.5 * open);

    spatial_move(world, src, 0, -49);
    audio_run(world, 7);
    l = spatial_rms(audio_tap_l, audio_tap_count - SPATIAL_FILTER_MEASURE, audio_tap_count);
    r = spatial_rms(audio_tap_r, audio_tap_count - SPATIAL_FILTER_MEASURE, audio_tap_count);
    test_assert(l < 0.01 * open);
    test_assert(r < 0.01 * open);
    test_assert(l > 0);
    test_assert(r > 0);
    ecs_fini(world);
}

void Spatial_cutoff_synth(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    test_assert(ecs_script_run(world, "beep",
        "using box.audio\n"
        "prefab Beep {\n"
        "  SynthSound: {}\n"
        "  SynthEnvelope: {attack: 0.001, sustain: 1, hold: 1, release: 0.01}\n"
        "  SynthOscillator: {frequency: 8000, level: 0.5}\n"
        "  AudioSpatial: {min_distance: 1, max_distance: 50, rolloff: 0, cutoff: 200}\n"
        "}\n", NULL) == 0);
    ecs_entity_t beep = ecs_lookup(world, "Beep");
    float near_, far_;
    spatial_listener(world, 0, 0, 0);
    audio_run(world, 2);

    test_bool(box_audio_play_at(world, beep, (vec3){0, 0, -0.5f}, 1.0f), true);
    audio_run(world, 8);
    near_ = spatial_rms(audio_tap_l, audio_tap_count - SPATIAL_FILTER_MEASURE, audio_tap_count);
    audio_run(world, 60);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    test_bool(box_audio_play_at(world, beep, (vec3){0, 0, -49}, 1.0f), true);
    audio_run(world, 8);
    far_ = spatial_rms(audio_tap_l, audio_tap_count - SPATIAL_FILTER_MEASURE, audio_tap_count);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_assert(near_ > 0.2f);
    test_assert(far_ > 0);
    test_assert(far_ < 0.01f * near_);
    ecs_fini(world);
}

void Spatial_cutoff_extreme_values(void) {
    // audio/spatial
    float values[] = { 0, -1, -1e9f, -INFINITY, NAN, 1e-9f, 1, 23999, 24000, 1e6f,
        INFINITY, 300 };
    int32_t k, i;

    /* In the core, every value of the parameter keeps a voice of noise finite and bounded */
    box_audio_core_t *core = audio_new();
    uint32_t seed = 7;
    for (i = 0; i < AUDIO_RATE; i ++) {
        seed = seed * 1664525u + 1013904223u;
        audio_sound_a[i] = 0.5f * ((float)(seed >> 8) / 8388608.0f - 1.0f);
    }

    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    for (k = 0; k < (int32_t)(sizeof(values) / sizeof(values[0])); k ++) {
        audio_voice_set(core, 1, BOX_AUDIO_PARAM_CUTOFF, values[k]);
        audio_render(core, 0, 1024);
        for (i = 0; i < 1024; i ++) {
            test_assert(isfinite(audio_l[i]));
            test_assert(isfinite(audio_r[i]));
            test_assert(fabsf(audio_l[i]) <= 1.0f);
            test_assert(fabsf(audio_r[i]) <= 1.0f);
        }
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    box_audio_core_free(core);

    /* Through the module, every cutoff of AudioSpatial at every distance */
    ecs_world_t *world = audio_world();
    ecs_entity_t noise = spatial_noise(world, 0.5f, AUDIO_RATE);
    spatial_listener(world, 0, 0, 0);
    ecs_entity_t src = spatial_filtered(world, noise, 10, 1, 50, 0);
    int32_t from = audio_tap_count;
    for (k = 0; k < (int32_t)(sizeof(values) / sizeof(values[0])); k ++) {
        ecs_set(world, src, AudioSpatial, {1, 50, 0, values[k]});
        for (i = 0; i < 4; i ++) {
            spatial_move(world, src, 0, -(float)(i * 16) - 0.5f);
            audio_run(world, 1);
        }

        test_assert(isfinite(box_audio_spatial_cutoff(
            &(AudioSpatial){1, 50, 0, values[k]}, 25)));
        test_assert(box_audio_spatial_cutoff(&(AudioSpatial){1, 50, 0, values[k]}, 25) >= 0);
        test_assert(box_audio_spatial_cutoff(&(AudioSpatial){1, 50, 0, values[k]}, 25) <
            BOX_AUDIO_SPATIAL_OPEN);
    }

    spatial_bounded(from, 1.0f);
    ecs_fini(world);
}

void Spatial_cutoff_first_frame(void) {
    // audio/spatial
    ecs_world_t *world = audio_world();
    ecs_entity_t tone = audio_tone(world, NULL, 8000, 0.5f, AUDIO_RATE);
    spatial_listener(world, 0, 0, 0);
    audio_run(world, 1);

    int32_t from = audio_tap_count;
    spatial_filtered(world, tone, 49, 1, 50, 200);
    audio_run(world, 3);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_assert(audio_peak(audio_tap_l, from, audio_tap_count) > 0);
    test_assert(audio_peak(audio_tap_l, from, audio_tap_count) < 0.01f);
    test_assert(audio_peak(audio_tap_r, from, audio_tap_count) < 0.01f);

    /* In the core, a voice and its cutoff in one batch: the first frames are dark already */
    box_audio_core_t *core = audio_new();
    audio_sine(audio_sound_a, AUDIO_RATE, 8000, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP | BOX_AUDIO_PLAY_ATTACK);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_CUTOFF, 200);
    audio_render(core, 0, 2048);
    test_assert(audio_peak(audio_l, 0, 2048) < 0.01f);
    test_assert(audio_peak(audio_l, 0, 2048) > 0);
    box_audio_core_free(core);
    ecs_fini(world);
}
