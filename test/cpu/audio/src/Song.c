/**
 * @file Song.c
 * @brief The song of the guide, test/assets/audio/song.flecs, through the null transport at a
 * fixed delta: 8 bars play on every channel, the stinger ducks the music group, and two runs
 * of 4 bars capture bit identical audio. Also the first sound of the guide,
 * test/assets/audio/first_sound.flecs.
 */

#include "world.h"
#include "../../../../src/jobs/jobs.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of 8 bars of 4/4 at 120 bpm, 16 seconds at 60 frames a second */
#define SONG_FRAMES 960

/** Frames of the captures, 4 bars, 8 seconds: the start, the shaker, the stinger and the duck */
#define SONG_CAPTURE_FRAMES 480

/** The channels of the song, each with an AudioMeter */
static const char *song_channels[] = {
    "sfx", "music", "hall", "echo", "bass", "keys", "drums", "perc", "box.audio.master"
};

#define SONG_CHANNELS ((int32_t)(sizeof(song_channels) / sizeof(song_channels[0])))

<<<<<<< HEAD
/** Create a world and run test/assets/audio/song.flecs in it. The sound files decode inline at
 * the end of the first frame, so the music that starts at frame 3 never waits on a worker.
=======
/** Create a world and run test/assets/audio/song.flecs in it. The world is headless, so every
 * frame waits for the decodes of kick.wav and shaker.wav, and the first beat always has its kick
 * however the decode threads are scheduled.
>>>>>>> a11f27093a724fc40579089b23395f4d4f634d21
 *
 * @return The world
 */
static ecs_world_t* song_world(void)
{
    ecs_world_t *world = audio_world();
    test_int(box_jobs_set_workers(world, 0), 0);
    char *code = audio_read("test/assets/audio/song.flecs");
    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    test_assert(ecs_script_run(world, "song", code, NULL) == 0);
    ecs_os_free(code);

    return world;
}

/** Wait for a capture to be written.
 *
 * @param world The world
 */
static void song_capture_wait(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < 400 && box_audio_capture_pending(world); i ++) {
        audio_run(world, 1);
    }

    test_bool(box_audio_capture_pending(world), false);
}

/** Record 4 bars of the song.
 *
 * @param path The WAV to write
 */
static void song_scene(
    const char *path)
{
    ecs_world_t *world = song_world();
    test_int(box_audio_capture(world, (float)SONG_CAPTURE_FRAMES * AUDIO_DT, path), 0);
    audio_run(world, SONG_CAPTURE_FRAMES);
    song_capture_wait(world);
    ecs_fini(world);
}

/** Read a whole file.
 *
 * @param path The file
 * @param size Receives its size
 * @return The bytes, which the caller frees, NULL when it cannot be read
 */
static uint8_t* song_read(
    const char *path,
    long *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *result = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        *size = ftell(f);
        fseek(f, 0, SEEK_SET);
        result = ecs_os_malloc((ecs_size_t)*size);
        test_assert(fread(result, 1, (size_t)*size, f) == (size_t)*size);
        fclose(f);
    }

    return result;
}

/** The script sets up the timeline and the mixer the guide describes. */
void Song_script(void) {
    // audio/song
    ecs_world_t *world = song_world();
    const Transport *t = ecs_singleton_get(world, Transport);
    ecs_entity_t music = ecs_lookup(world, "music");
    ecs_entity_t keys = ecs_lookup(world, "keys");
    ecs_entity_t five = ecs_lookup(world, "keys.five");
    ecs_entity_t duck = ecs_lookup(world, "music_duck");
    ecs_entity_t echo = ecs_lookup(world, "echo");
    test_assert(t != NULL);
    test_flt(t->tempo, 120.0f);
    test_bool(t->loop, true);
    test_flt(t->loop_end, 16.0);
    test_assert(music != 0 && keys != 0 && five != 0 && duck != 0 && echo != 0);
    test_uint(ecs_get(world, keys, AudioBus)->output, music);
    test_int(ecs_vec_count(&ecs_get(world, keys, AudioBus)->sends), 2);
    test_flt(ecs_get(world, five, ClipPlacement)->transpose, 7.0f);
    test_uint(ecs_get(world, duck, Compressor)->sidechain, ecs_lookup(world, "sfx"));
    test_flt(ecs_get(world, echo, Delay)->sync, 0.75f);
    ecs_fini(world);
}

/** 8 bars play: every channel reads signal on its meter, the tracks play their notes and the
 * stinger plays on sfx and ducks the music group. */
void Song_meters(void) {
    // audio/song
    ecs_world_t *world = song_world();
    float peak[SONG_CHANNELS] = {0};
    float reduction = 0;
    ecs_entity_t duck = ecs_lookup(world, "music_duck");
    int32_t f, c;
    for (f = 0; f < SONG_FRAMES; f ++) {
        audio_run(world, 1);
        for (c = 0; c < SONG_CHANNELS; c ++) {
            ecs_entity_t e = ecs_lookup(world, song_channels[c]);
            const AudioMeter *m;
            test_assert(e != 0);
            m = ecs_get(world, e, AudioMeter);
            test_assert(m != NULL);
            if (m->peak_left > peak[c]) {
                peak[c] = m->peak_left;
            }

            if (m->peak_right > peak[c]) {
                peak[c] = m->peak_right;
            }
        }

        if (ecs_get(world, duck, AudioMeter)->reduction > reduction) {
            reduction = ecs_get(world, duck, AudioMeter)->reduction;
        }
    }

    for (c = 0; c < SONG_CHANNELS; c ++) {
        if (peak[c] < 0.01f) {
            printf("%s peaks at %f\n", song_channels[c], (double)peak[c]);
        }

        test_assert(peak[c] >= 0.01f);
    }

    test_assert(reduction > 1.0f);
    test_assert(box_audio_module_get(world)->sequencer.stats.notes > 50);
    test_int(box_audio_module_get(world)->sequencer.stats.dropped, 0);
    test_assert(ecs_singleton_get(world, Transport)->position < 16.0);
    test_int(ecs_singleton_get(world, AudioStatus)->voices_refused, 0);
    ecs_fini(world);
}

/** Two runs of 4 bars capture bit identical audio. */
void Song_bit_identical(void) {
    // audio/song
    const char *a = "audio_song_a.wav", *b = "audio_song_b.wav";
    long size_a = 0, size_b = 0;
    uint8_t *wav_a, *wav_b;
    int32_t i, loud = 0;
    song_scene(a);
    song_scene(b);
    wav_a = song_read(a, &size_a);
    wav_b = song_read(b, &size_b);
    test_assert(wav_a != NULL);
    test_assert(wav_b != NULL);
    test_int(size_a, 44 + 8 * 48000 * 4);
    test_int(size_a, size_b);
    test_assert(!memcmp(wav_a, wav_b, (size_t)size_a));
    for (i = 44; i < size_a; i += 2) {
        int16_t v = (int16_t)(wav_a[i] | (wav_a[i + 1] << 8));
        loud += v > 3000 || v < -3000;
    }

    test_assert(loud > 48000);
    ecs_os_free(wav_a);
    ecs_os_free(wav_b);
    remove(a);
    remove(b);
}

/** The first sound of the guide plays its beep once on sfx. */
void Song_first_sound(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/first_sound.flecs");
    test_assert(ecs_script_run(world, "first_sound", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 30);
    test_assert(audio_peak(audio_tap_l, 0, audio_tap_count) > 0.3f);
    test_assert(audio_peak(audio_tap_l, AUDIO_FRAME * 25, audio_tap_count) < 0.001f);
    ecs_fini(world);
}
