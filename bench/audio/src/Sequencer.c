/**
 * @file Sequencer.c
 * @brief Benchmarks for the clips and tracks of box.audio: 16 tracks of 4 placements of a clip
 * of 64 notes each, through the null output, one item per frame of 1/60 s.
 *
 * Each iteration runs one frame of a world with box, so the time is the time of the frame:
 * the audio.Sync system of the module with the window of the timeline and the render of the frame.
 * tracks_16_stopped is the same world with the transport stopped, tracks_0 one without tracks
 * whose transport plays.
 */

#include <box.h>
#include <output.h>
#include <bake_bench.h>

/** Tracks of the benchmarks */
#define BENCH_TRACKS 16

/** Placements of each track */
#define BENCH_PLACEMENTS 4

/** Notes of the clip, every sixteenth of a beat over its 4 beats */
#define BENCH_NOTES 64

/** Delta of a frame, 800 frames at 48 kHz */
#define BENCH_DT (1.0f / 60.0f)

/** Create a world with a sampler, a clip of 64 notes and tracks that place it back to back,
 * under a transport that loops over the placements.
 *
 * @param tracks Tracks to create
 * @param playing Whether the transport plays
 * @return The world
 */
static ecs_world_t* bench_world(
    int32_t tracks,
    bool playing)
{
    static float samples[4800];
    ecs_world_t *world = ecs_init();
    ecs_entity_t sound, inst, clip;
    NoteClip *c;
    Note *notes;
    Transport *tr;
    int32_t i, k;
    ECS_IMPORT(world, Box);
    box_audio_output_open(world, &(box_audio_output_desc_t){ .headless = true });
    for (i = 0; i < 4800; i ++) {
        samples[i] = 0.01f * (float)(i % 100) / 100.0f;
    }

    sound = box_audio_sound_from_pcm(world, 0, samples, 4800, 1, 48000);
    inst = ecs_new(world);
    ecs_set(world, inst, Instrument, { .sound = sound, .root = 60, .velocity_curve = 2,
        .gate = true, .release = 0.01f });
    clip = ecs_new(world);
    c = ecs_ensure(world, clip, NoteClip);
    c->length = 4;
    ecs_vec_set_count_t(NULL, &c->notes, Note, BENCH_NOTES);
    notes = ecs_vec_first(&c->notes);
    for (i = 0; i < BENCH_NOTES; i ++) {
        notes[i] = (Note){ .start = (float)i / 16.0f, .duration = 0.05f,
            .pitch = 48.0f + (float)(i % 24), .velocity = 0.3f };
    }

    ecs_modified(world, clip, NoteClip);
    for (i = 0; i < tracks; i ++) {
        ecs_entity_t track = ecs_new(world);
        ecs_set(world, track, Track, { .instrument = inst });
        for (k = 0; k < BENCH_PLACEMENTS; k ++) {
            ecs_entity_t p = ecs_new_w_pair(world, EcsChildOf, track);
            ClipPlacement *cp = ecs_ensure(world, p, ClipPlacement);
            cp->clip = clip;
            cp->start = 4.0 * (double)k;
            cp->transpose = (float)i;
            ecs_modified(world, p, ClipPlacement);
        }
    }

    tr = ecs_singleton_ensure(world, Transport);
    tr->loop = true;
    tr->loop_start = 0;
    tr->loop_end = 4.0 * BENCH_PLACEMENTS;
    tr->playing = playing;
    ecs_singleton_modified(world, Transport);
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, BENCH_DT);
    }

    return world;
}

/** Run frames of a world of tracks.
 *
 * @param b The benchmark
 * @param tracks Tracks to create
 * @param playing Whether the transport plays
 */
static void bench_sequencer(
    bench_t *b,
    int32_t tracks,
    bool playing)
{
    ecs_world_t *world = bench_world(tracks, playing);
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        ecs_progress(world, BENCH_DT);
        bench_keep(world);
    }

    ecs_fini(world);
}

void Sequencer_tracks_16_playing(
    bench_t *b)
{
    // audio/sequencer
    bench_sequencer(b, BENCH_TRACKS, true);
}

void Sequencer_tracks_16_stopped(
    bench_t *b)
{
    // audio/sequencer
    bench_sequencer(b, BENCH_TRACKS, false);
}

void Sequencer_tracks_0(
    bench_t *b)
{
    // audio/sequencer
    bench_sequencer(b, 0, true);
}
