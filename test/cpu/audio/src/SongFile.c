/**
 * @file SongFile.c
 * @brief Songs as .flecs files: AudioSong loads one song at a time, a song that is not loaded
 * holds nothing in the engine, and a swap waits until the slots of the old song are reusable
 * before it creates the new one. The song files are in test/assets/audio/songs.
 */

#include "world.h"
#include <stdio.h>

/** Frames a load, a swap or an unload gets at most */
#define SONGFILE_FRAMES 600

/** Folder of the song files of the tests */
#define SONGFILE_DIR "test/assets/audio/songs/"

/** Buses of big_a.flecs and big_b.flecs */
#define SONGFILE_BIG_BUSES 40

/** Buses, effects and synth sounds of fx_a.flecs and fx_b.flecs */
#define SONGFILE_FX_BUSES 10
#define SONGFILE_FX_EFFECTS 40
#define SONGFILE_FX_SOUNDS 200

/** What the engine holds: bus, effect and sound slots, voices and decoded sound files */
typedef struct songfile_counts_t {
    int32_t buses;      /**< Bus slots in use, the master excluded */
    int32_t effects;    /**< Effect slots in use */
    int32_t sounds;     /**< Sound ids in use */
    int32_t voices;     /**< Voices that play */
    int32_t samples;    /**< Decoded sound files */
} songfile_counts_t;

/** Get the state of AudioSongStatus.
 *
 * @param world The world
 * @return The state
 */
static SongState songfile_state(
    ecs_world_t *world)
{
    const AudioSongStatus *s = ecs_singleton_get(world, AudioSongStatus);
    test_assert(s != NULL);

    return s->state;
}

/** Run frames until the song is no longer loading.
 *
 * @param world The world
 * @return Frames it took
 */
static int32_t songfile_settle(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < SONGFILE_FRAMES; i ++) {
        audio_run(world, 1);
        if (songfile_state(world) != SongLoading) {
            break;
        }
    }

    test_assert(i < SONGFILE_FRAMES);

    return i + 1;
}

/** Load a song of the tests and run until it no longer loads.
 *
 * @param world The world
 * @param name File name in SONGFILE_DIR
 * @return The state it ended in
 */
static SongState songfile_load(
    ecs_world_t *world,
    const char *name)
{
    char file[256];
    snprintf(file, sizeof(file), "%s%s", SONGFILE_DIR, name);
    box_audio_song_load(world, file);
    songfile_settle(world);

    return songfile_state(world);
}

/** Unload the song and run until it is gone.
 *
 * @param world The world
 */
static void songfile_unload(
    ecs_world_t *world)
{
    box_audio_song_unload(world);
    songfile_settle(world);
    test_int(songfile_state(world), SongNone);
}

/** Take the counts of what the engine holds.
 *
 * @param world The world
 * @return The counts
 */
static songfile_counts_t songfile_counts(
    ecs_world_t *world)
{
    const AudioStatus *status = ecs_singleton_get(world, AudioStatus);

    return (songfile_counts_t){
        .buses = box_audio_module_buses(world),
        .effects = box_audio_module_effects(world),
        .sounds = box_audio_module_sounds(world),
        .voices = status ? status->voices : 0,
        .samples = box_audio_sample_count(world)
    };
}

/** Count the managed scripts below an entity, the entity included.
 *
 * @param world The world
 * @param e The entity
 * @return Script entities
 */
static int32_t songfile_scripts_below(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_iter_t it = ecs_children(world, e);
    ecs_entity_t children[64];
    int32_t count = 0, result = ecs_has(world, e, EcsScript), i;
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count && count < 64; i ++) {
            children[count ++] = it.entities[i];
        }
    }

    for (i = 0; i < count; i ++) {
        result += songfile_scripts_below(world, children[i]);
    }

    return result;
}

/** Count the song scripts the engine holds, the includes included.
 *
 * @param world The world
 * @return Script entities below box.audio.songs
 */
static int32_t songfile_scripts(
    ecs_world_t *world)
{
    ecs_entity_t root = ecs_lookup(world, "box.audio.songs");
    test_assert(root != 0);

    return songfile_scripts_below(world, root);
}

/** Check that every bus of a numbered set exists and has a bus index.
 *
 * @param world The world
 * @param format Name of the buses with a %d for the number
 * @param count Number of buses
 */
static void songfile_buses_live(
    ecs_world_t *world,
    const char *format,
    int32_t count)
{
    char name[64];
    int32_t i;
    for (i = 0; i < count; i ++) {
        snprintf(name, sizeof(name), format, i);
        ecs_entity_t e = ecs_lookup(world, name);
        test_assert(e != 0);
        test_assert(box_audio_module_bus_index(world, e) > 0);
    }
}

/** Check that the Song singleton has its default value and no colour.
 *
 * @param world The world
 */
static void songfile_song_default(
    ecs_world_t *world)
{
    const Song *song = ecs_singleton_get(world, Song);
    test_assert(song != NULL);
    test_assert(song->title == NULL);
    test_flt(song->tempo, 120);
    test_int(song->beats_per_bar, 4);
    test_int(song->beat_unit, 4);
    test_flt(song->beats, 0);
    test_bool(song->loop, true);
    test_int(ecs_vec_count(&song->lanes), 0);
    test_bool(ecs_has(world, ecs_id(Song), Rgb), false);
}

/** Start or stop the Transport.
 *
 * @param world The world
 * @param playing Whether it plays
 */
static void songfile_play(
    ecs_world_t *world,
    bool playing)
{
    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->playing = playing;
    ecs_singleton_modified(world, Transport);
}

void SongFile_load(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    songfile_counts_t base = songfile_counts(world);
    test_int(songfile_state(world), SongNone);
    songfile_song_default(world);

    test_int(songfile_load(world, "basic.flecs"), SongReady);
    ecs_entity_t lead = ecs_lookup(world, "basic_lead");
    ecs_entity_t group = ecs_lookup(world, "basic_group");
    ecs_entity_t room = ecs_lookup(world, "basic_room");
    ecs_entity_t tone = ecs_lookup(world, "basic_tone");
    test_assert(lead != 0 && group != 0 && room != 0 && tone != 0);
    test_assert(ecs_has(world, lead, Track));
    test_assert(ecs_lookup(world, "basic_lead.phrase") != 0);
    test_assert(box_audio_module_bus_index(world, lead) > 0);
    test_assert(box_audio_module_bus_index(world, group) > 0);
    test_assert(box_audio_module_bus_index(world, room) > 0);
    test_assert(box_audio_module_effect_slot(world, room) >= 0);
    test_assert(box_audio_sound_of(world, box_audio_module_get(world), tone) != 0);

    songfile_counts_t loaded = songfile_counts(world);
    test_int(loaded.buses, base.buses + 3);
    test_int(loaded.effects, base.effects + 1);
    test_int(loaded.sounds, base.sounds + 1);

    const Song *song = ecs_singleton_get(world, Song);
    test_str(song->title, "Basic");
    test_flt(song->tempo, 96);
    test_int(song->beats_per_bar, 3);
    test_int(song->beat_unit, 4);
    test_flt(song->beats, 12);
    test_bool(song->loop, true);
    test_int(ecs_vec_count(&song->lanes), 1);
    const SongLane *lanes = ecs_vec_first(&song->lanes);
    test_str(lanes[0].name, "Lead");
    test_uint(lanes[0].bus, lead);

    const Rgb *rgb = ecs_get(world, ecs_id(Song), Rgb);
    test_assert(rgb != NULL);
    test_int(rgb->r, 200);
    test_int(rgb->g, 100);
    test_int(rgb->b, 50);

    const Transport *tr = ecs_singleton_get(world, Transport);
    test_flt(tr->tempo, 96);
    test_int(tr->beats_per_bar, 3);
    test_int(tr->beat_unit, 4);
    test_flt(tr->position, 0);
    test_bool(tr->loop, true);
    test_flt(tr->loop_start, 0);
    test_flt(tr->loop_end, 12);
    test_bool(tr->playing, false);

    const AudioSongStatus *status = ecs_singleton_get(world, AudioSongStatus);
    test_str(status->file, SONGFILE_DIR "basic.flecs");
    test_assert(status->error == NULL);
    test_int(status->foreign, 0);
    test_int(songfile_scripts(world), 1);
    ecs_entity_t script = ecs_lookup(world, "box.audio.songs.basic");
    test_assert(script != 0);
    test_assert(ecs_has(world, script, EcsScript));
    test_assert(ecs_has_pair(world, lead, ecs_id(EcsScript), script));
    ecs_fini(world);
}

void SongFile_second_song(void) {
    // audio/song
    ecs_world_t *alone = audio_world();
    test_int(songfile_load(alone, "other.flecs"), SongReady);
    songfile_play(alone, true);
    audio_run(alone, 30);
    songfile_counts_t expect = songfile_counts(alone);
    test_assert(expect.voices > 0);
    ecs_fini(alone);

    ecs_world_t *world = audio_world();
    songfile_play(world, true);
    test_int(songfile_load(world, "basic.flecs"), SongReady);
    audio_run(world, 30);
    test_assert(songfile_counts(world).voices > 0);
    songfile_play(world, false);
    test_int(songfile_load(world, "other.flecs"), SongReady);
    songfile_play(world, true);
    audio_run(world, 30);
    songfile_counts_t got = songfile_counts(world);
    test_int(got.buses, expect.buses);
    test_int(got.effects, expect.effects);
    test_int(got.sounds, expect.sounds);
    test_int(got.voices, expect.voices);
    test_int(got.samples, expect.samples);
    test_assert(ecs_lookup(world, "basic_lead") == 0);
    test_assert(ecs_lookup(world, "basic_tone") == 0);
    test_assert(ecs_lookup(world, "other_keys") != 0);
    test_int(songfile_scripts(world), 1);
    test_str(ecs_singleton_get(world, Song)->title, "Other");
    const Rgb *rgb = ecs_get(world, ecs_id(Song), Rgb);
    test_assert(rgb != NULL);
    test_int(rgb->r, 40);
    ecs_fini(world);
}

void SongFile_unload(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    audio_run(world, 2);
    songfile_counts_t base = songfile_counts(world);
    songfile_play(world, true);
    test_int(songfile_load(world, "basic.flecs"), SongReady);
    audio_run(world, 30);
    test_assert(songfile_counts(world).buses > base.buses);

    songfile_unload(world);
    audio_run(world, 2);
    songfile_counts_t after = songfile_counts(world);
    test_int(after.buses, base.buses);
    test_int(after.effects, base.effects);
    test_int(after.sounds, base.sounds);
    test_int(after.voices, base.voices);
    test_int(after.samples, base.samples);
    songfile_song_default(world);
    test_assert(ecs_lookup(world, "basic_lead") == 0);
    test_assert(ecs_lookup(world, "basic_room") == 0);
    test_assert(ecs_lookup(world, "basic_tone") == 0);
    test_int(songfile_scripts(world), 0);
    test_assert(ecs_singleton_get(world, AudioSongStatus)->file == NULL);
    test_int(box_audio_song_held(world), 0);
    test_int(box_audio_song_stats(world).unloads, 1);

    test_int(songfile_load(world, "basic.flecs"), SongReady);
    box_audio_song_load(world, "");
    songfile_settle(world);
    test_int(songfile_state(world), SongNone);
    test_int(box_audio_module_buses(world), base.buses);
    songfile_song_default(world);
    ecs_fini(world);
}

/** Swap two songs back and forth: 20 swaps that each wait for the song, then 20 changes on
 * consecutive frames, which end with the last one.
 *
 * @param world The world
 * @param a First song file name
 * @param b Second song file name
 * @param a_bus Name of the buses of a with a %d
 * @param b_bus Name of the buses of b with a %d
 * @param buses Buses of each song
 */
static void songfile_swaps(
    ecs_world_t *world,
    const char *a,
    const char *b,
    const char *a_bus,
    const char *b_bus,
    int32_t buses)
{
    char name[64];
    int32_t i;
    songfile_counts_t base = songfile_counts(world);
    for (i = 0; i < 20; i ++) {
        bool second = (i % 2) == 1;
        test_int(songfile_load(world, second ? b : a), SongReady);
        songfile_buses_live(world, second ? b_bus : a_bus, buses);
        snprintf(name, sizeof(name), second ? a_bus : b_bus, 0);
        test_assert(ecs_lookup(world, name) == 0);
        test_int(box_audio_module_buses(world), base.buses + buses);
        test_int(songfile_scripts(world), 1);
    }

    for (i = 0; i < 20; i ++) {
        char file[256];
        snprintf(file, sizeof(file), "%s%s", SONGFILE_DIR, (i % 2) ? b : a);
        box_audio_song_load(world, file);
        audio_run(world, 1);
    }

    songfile_settle(world);
    test_int(songfile_state(world), SongReady);
    songfile_buses_live(world, b_bus, buses);
    snprintf(name, sizeof(name), a_bus, 0);
    test_assert(ecs_lookup(world, name) == 0);
    test_int(box_audio_module_buses(world), base.buses + buses);
    test_int(songfile_scripts(world), 1);
}

void SongFile_swap_buses(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    audio_log_start();
    songfile_swaps(world, "big_a.flecs", "big_b.flecs", "big_a_%d", "big_b_%d",
        SONGFILE_BIG_BUSES);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_assert(box_audio_song_stats(world).waits > 0);
    ecs_fini(world);
}

void SongFile_swap_effects_sounds(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    songfile_counts_t base = songfile_counts(world);
    char name[64];
    int32_t b, k;
    audio_log_start();
    songfile_swaps(world, "fx_a.flecs", "fx_b.flecs", "fx_a_bus_%d", "fx_b_bus_%d",
        SONGFILE_FX_BUSES);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    songfile_counts_t got = songfile_counts(world);
    test_int(got.effects, base.effects + SONGFILE_FX_EFFECTS);
    test_int(got.sounds, base.sounds + SONGFILE_FX_SOUNDS);
    for (b = 0; b < SONGFILE_FX_BUSES; b ++) {
        for (k = 0; k < 4; k ++) {
            snprintf(name, sizeof(name), "fx_b_fx_%d_%d", b, k);
            ecs_entity_t e = ecs_lookup(world, name);
            test_assert(e != 0);
            test_assert(box_audio_module_effect_slot(world, e) >= 0);
        }
    }

    for (b = 0; b < SONGFILE_FX_SOUNDS; b ++) {
        snprintf(name, sizeof(name), "fx_b_sound_%d", b);
        ecs_entity_t e = ecs_lookup(world, name);
        test_assert(e != 0);
        test_assert(box_audio_sound_of(world, box_audio_module_get(world), e) != 0);
    }

    ecs_fini(world);
}

void SongFile_swap_waits(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    int32_t i, held_frames = 0;
    bool seen = false;
    test_int(songfile_load(world, "fx_a.flecs"), SongReady);
    box_audio_song_load(world, SONGFILE_DIR "fx_b.flecs");
    for (i = 0; i < SONGFILE_FRAMES && !seen; i ++) {
        int32_t held = box_audio_song_held(world);
        audio_run(world, 1);
        bool a = ecs_lookup(world, "fx_a_bus_0") != 0;
        bool b = ecs_lookup(world, "fx_b_bus_0") != 0;
        test_assert(!(a && b));
        if (b) {
            test_int(box_audio_song_held(world), 0);
            test_assert(ecs_lookup(world, "fx_a_sound_0") == 0);
            test_assert(ecs_lookup(world, "fx_a_fx_0_0") == 0);
            songfile_buses_live(world, "fx_b_bus_%d", SONGFILE_FX_BUSES);
            seen = true;
        } else {
            held_frames += held > 0;
        }
    }

    test_bool(seen, true);
    test_assert(held_frames > 0);
    test_assert(box_audio_song_stats(world).waits > 0);
    songfile_settle(world);
    test_int(songfile_state(world), SongReady);
    ecs_fini(world);
}

void SongFile_three_changes(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    songfile_counts_t base = songfile_counts(world);
    box_audio_song_load(world, SONGFILE_DIR "first.flecs");
    audio_run(world, 1);
    box_audio_song_load(world, SONGFILE_DIR "second.flecs");
    audio_run(world, 1);
    box_audio_song_load(world, SONGFILE_DIR "third.flecs");
    audio_run(world, 1);
    songfile_settle(world);
    test_int(songfile_state(world), SongReady);
    test_assert(ecs_lookup(world, "first_bus") == 0);
    test_assert(ecs_lookup(world, "second_bus") == 0);
    test_assert(ecs_lookup(world, "third_bus") != 0);
    test_assert(ecs_lookup(world, "first_tone") == 0);
    test_assert(ecs_lookup(world, "second_tone") == 0);
    test_int(songfile_scripts(world), 1);
    test_int(box_audio_module_buses(world), base.buses + 1);
    test_str(ecs_singleton_get(world, Song)->title, "Third");
    test_str(ecs_singleton_get(world, AudioSongStatus)->file, SONGFILE_DIR "third.flecs");
    ecs_fini(world);
}

void SongFile_too_big(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    audio_run(world, 2);
    songfile_counts_t base = songfile_counts(world);
    char limit[16];
    audio_log_start();
    test_int(songfile_load(world, "huge.flecs"), SongFailed);
    audio_run(world, 5);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "is silent") == NULL);
    const AudioSongStatus *status = ecs_singleton_get(world, AudioSongStatus);
    test_assert(status->error != NULL);
    snprintf(limit, sizeof(limit), "%d", BOX_AUDIO_BUS_MAX - 1);
    test_assert(strstr(status->error, limit) != NULL);
    test_assert(strstr(status->error, "70") != NULL);
    test_str(status->file, SONGFILE_DIR "huge.flecs");
    test_assert(ecs_lookup(world, "huge_0") == 0);
    test_assert(ecs_lookup(world, "huge_69") == 0);
    test_int(songfile_scripts(world), 0);
    songfile_counts_t after = songfile_counts(world);
    test_int(after.buses, base.buses);
    test_int(after.effects, base.effects);
    test_int(after.sounds, base.sounds);
    songfile_song_default(world);
    test_bool(box_audio_module_get(world)->song.hold, false);

    test_int(songfile_load(world, "basic.flecs"), SongReady);
    ecs_fini(world);
}

void SongFile_include(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    songfile_counts_t base = songfile_counts(world);
    test_int(songfile_load(world, "with_include.flecs"), SongReady);
    ecs_entity_t part = ecs_lookup(world, "include_part_bus");
    test_assert(part != 0);
    test_assert(ecs_lookup(world, "include_main_bus") != 0);
    test_assert(ecs_lookup(world, "include_part_tone") != 0);
    test_assert(box_audio_module_bus_index(world, part) > 0);
    test_int(songfile_scripts(world), 2);
    test_int(box_audio_module_buses(world), base.buses + 2);
    const Song *song = ecs_singleton_get(world, Song);
    test_int(ecs_vec_count(&song->lanes), 1);
    test_uint(ecs_vec_get_t(&song->lanes, SongLane, 0)->bus, part);

    songfile_unload(world);
    test_assert(ecs_lookup(world, "include_part_bus") == 0);
    test_assert(ecs_lookup(world, "include_main_bus") == 0);
    test_assert(ecs_lookup(world, "include_part_tone") == 0);
    test_int(songfile_scripts(world), 0);
    test_int(box_audio_module_buses(world), base.buses);
    test_int(box_audio_module_sounds(world), base.sounds);

    test_int(songfile_load(world, "with_include.flecs"), SongReady);
    test_assert(ecs_lookup(world, "include_part_bus") != 0);
    test_int(songfile_scripts(world), 2);
    ecs_fini(world);
}

/** Load a song file that fails and check that nothing of it is left.
 *
 * @param world The world
 * @param base The counts with no song loaded
 * @param name File name in SONGFILE_DIR
 * @param bus A bus the file declares
 */
static void songfile_fails(
    ecs_world_t *world,
    songfile_counts_t base,
    const char *name,
    const char *bus)
{
    audio_log_start();
    test_int(songfile_load(world, name), SongFailed);
    audio_run(world, 5);
    audio_log_stop();
    test_assert(audio_log_errors > 0);
    const AudioSongStatus *status = ecs_singleton_get(world, AudioSongStatus);
    test_assert(status->error != NULL);
    test_assert(ecs_lookup(world, bus) == 0);
    test_int(songfile_scripts(world), 0);
    songfile_counts_t after = songfile_counts(world);
    test_int(after.buses, base.buses);
    test_int(after.effects, base.effects);
    test_int(after.sounds, base.sounds);
    songfile_song_default(world);
}

void SongFile_parse_error(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    audio_run(world, 2);
    songfile_counts_t base = songfile_counts(world);
    songfile_fails(world, base, "broken.flecs", "broken_bus");
    songfile_fails(world, base, "eval_error.flecs", "eval_error_bus");
    test_assert(ecs_lookup(world, "eval_error_tone") == 0);
    test_assert(ecs_lookup(world, "eval_error_bad") == 0);

    test_int(songfile_load(world, "basic.flecs"), SongReady);
    songfile_fails(world, base, "broken.flecs", "broken_bus");
    test_assert(ecs_lookup(world, "basic_lead") == 0);
    ecs_fini(world);
}

void SongFile_sound_files_wait(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    int32_t i, loading = 0;
    songfile_play(world, true);
    box_audio_song_load(world, SONGFILE_DIR "sampled.flecs");
    for (i = 0; i < SONGFILE_FRAMES; i ++) {
        audio_run(world, 1);
        if (songfile_state(world) != SongLoading) {
            break;
        }

        loading ++;
        test_bool(state->timeline.playing, false);
        test_flt(ecs_singleton_get(world, Transport)->position, 0);
    }

    test_assert(loading > 0);
    test_int(songfile_state(world), SongReady);
    ecs_entity_t sample = ecs_lookup(world, "sampled_long");
    ecs_entity_t kick = ecs_lookup(world, "sampled_kick");
    test_assert(sample != 0 && kick != 0);
    test_int(ecs_get(world, sample, SoundStatus)->state, SoundReady);
    test_int(ecs_get(world, kick, SoundStatus)->state, SoundReady);
    test_assert(box_audio_sound_of(world, state, sample) != 0);
    test_bool(ecs_singleton_get(world, Transport)->playing, true);

    audio_run(world, 10);
    test_bool(state->timeline.playing, true);
    test_assert(ecs_singleton_get(world, Transport)->position > 0);
    test_assert(state->sequencer.stats.passes > 0);

    songfile_unload(world);
    test_int(box_audio_sample_count(world), 0);
    ecs_fini(world);
}

void SongFile_old_voices_fade(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    songfile_play(world, true);
    test_int(songfile_load(world, "ringing.flecs"), SongReady);
    audio_run(world, 30);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count) > 0.05f);
    test_assert(ecs_vec_count(&state->oneshots) > 0);

    test_int(songfile_load(world, "quiet.flecs"), SongReady);
    audio_run(world, 2);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count) <
        0.0001f);
    test_assert(audio_peak(audio_tap_r, audio_tap_count - AUDIO_FRAME, audio_tap_count) <
        0.0001f);
    test_int(ecs_vec_count(&state->oneshots), 0);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_assert(ecs_lookup(world, "ringing_bell") == 0);
    ecs_fini(world);
}

void SongFile_foreign(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    ecs_entity_t host = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "host_scene" }),
        .code = "using box.audio\nhost_room { Bus: {volume: 0.9} }\n"
    });
    test_assert(host != 0);
    ecs_entity_t room = ecs_lookup(world, "host_room");
    test_assert(room != 0);
    audio_run(world, 2);

    audio_log_start();
    test_int(songfile_load(world, "foreign.flecs"), SongReady);
    audio_log_stop();
    test_assert(audio_log_warnings >= 1);
    const AudioSongStatus *status = ecs_singleton_get(world, AudioSongStatus);
    test_int(status->foreign, 1);
    test_flt(ecs_get(world, room, AudioBus)->volume, 0.25f);
    test_assert(ecs_lookup(world, "foreign_bus") != 0);

    songfile_unload(world);
    test_assert(ecs_is_alive(world, room));
    test_assert(ecs_has(world, room, AudioBus));
    test_assert(ecs_lookup(world, "foreign_bus") == 0);
    test_assert(ecs_has_pair(world, room, ecs_id(EcsScript), host));
    ecs_fini(world);
}

void SongFile_external_reload(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    songfile_counts_t base = songfile_counts(world);
    test_int(songfile_load(world, "basic.flecs"), SongReady);
    ecs_entity_t script = ecs_lookup(world, "box.audio.songs.basic");
    test_assert(script != 0);
    test_int(ecs_script_update(world, script, 0,
        "using box.audio\n"
        "edited_bus { Bus: {} }\n"
        "$ { Song: {title: \"Edited\", tempo: 90, beats: 8, "
            "lanes: [{name: \"Solo\", bus: edited_bus}]} }\n"), 0);
    songfile_settle(world);
    test_int(songfile_state(world), SongReady);
    test_int(box_audio_song_stats(world).reloads, 1);
    const Song *song = ecs_singleton_get(world, Song);
    test_str(song->title, "Edited");
    test_flt(song->tempo, 90);
    test_int(ecs_vec_count(&song->lanes), 1);
    ecs_entity_t edited = ecs_lookup(world, "edited_bus");
    test_assert(edited != 0);
    test_uint(ecs_vec_get_t(&song->lanes, SongLane, 0)->bus, edited);
    test_assert(box_audio_module_bus_index(world, edited) > 0);
    test_bool(ecs_has(world, ecs_id(Song), Rgb), false);
    test_assert(ecs_lookup(world, "basic_lead") == 0);
    test_int(box_audio_module_buses(world), base.buses + 1);
    test_int(box_audio_module_effects(world), base.effects);
    test_flt(ecs_singleton_get(world, Transport)->tempo, 90);
    test_int(songfile_scripts(world), 1);
    ecs_fini(world);
}

/** Write a song file with one bus, for the hot reload. Names of other lengths give files of
 * other sizes, which the file watch sees as a change within the same second.
 *
 * @param path The file
 * @param name Name of the bus and title of the song
 */
static void songfile_write(
    const char *path,
    const char *name)
{
    FILE *f = fopen(path, "wb");
    test_assert(f != NULL);
    fprintf(f,
        "using box.audio\n"
        "using box.material\n"
        "box { audio { Song { Rgb: {10, 20, 30} } } }\n"
        "%s { Bus: {}; Meter: {} }\n"
        "$ { Song: {title: \"%s\", tempo: 100, beats: 8, lanes: [{name: \"Main\", bus: %s}]} }\n",
        name, name, name);
    fclose(f);
}

void SongFile_hot_reload(void) {
    // audio/song
    const char *path = "songfile_hot_reload.flecs";
    ecs_world_t *world = audio_world();
    int32_t i;
    ecs_singleton_set(world, FileWatch, { .interval = 0 });
    songfile_counts_t base = songfile_counts(world);
    songfile_write(path, "hot_first");
    box_audio_song_load(world, path);
    songfile_settle(world);
    test_int(songfile_state(world), SongReady);
    test_assert(ecs_lookup(world, "hot_first") != 0);

    songfile_write(path, "hot_second_edit");
    for (i = 0; i < SONGFILE_FRAMES && !ecs_lookup(world, "hot_second_edit"); i ++) {
        audio_run(world, 1);
    }

    songfile_settle(world);
    test_int(songfile_state(world), SongReady);
    test_assert(ecs_lookup(world, "hot_first") == 0);
    ecs_entity_t bus = ecs_lookup(world, "hot_second_edit");
    test_assert(bus != 0);
    test_assert(box_audio_module_bus_index(world, bus) > 0);
    const Song *song = ecs_singleton_get(world, Song);
    test_str(song->title, "hot_second_edit");
    test_int(ecs_vec_count(&song->lanes), 1);
    test_uint(ecs_vec_get_t(&song->lanes, SongLane, 0)->bus, bus);
    test_int(ecs_get(world, ecs_id(Song), Rgb)->g, 20);
    test_int(box_audio_module_buses(world), base.buses + 1);
    test_int(songfile_scripts(world), 1);
    test_assert(box_audio_song_stats(world).reloads >= 1);

    songfile_unload(world);
    test_int(box_audio_module_buses(world), base.buses);
    songfile_song_default(world);
    remove(path);
    ecs_fini(world);
}

void SongFile_component(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    ecs_singleton_set(world, AudioSong, {
        .file = ECS_CONST_CAST(char*, SONGFILE_DIR "basic.flecs") });
    songfile_settle(world);
    test_int(songfile_state(world), SongReady);
    test_assert(ecs_lookup(world, "basic_lead") != 0);

    ecs_singleton_set(world, AudioSong, {
        .file = ECS_CONST_CAST(char*, SONGFILE_DIR "other.flecs") });
    songfile_settle(world);
    test_int(songfile_state(world), SongReady);
    test_assert(ecs_lookup(world, "basic_lead") == 0);
    test_assert(ecs_lookup(world, "other_keys") != 0);

    ecs_singleton_remove(world, AudioSong);
    songfile_settle(world);
    test_int(songfile_state(world), SongNone);
    test_assert(ecs_lookup(world, "other_keys") == 0);

    audio_log_start();
    ecs_singleton_set(world, AudioSong, {
        .file = ECS_CONST_CAST(char*, SONGFILE_DIR "missing.flecs") });
    songfile_settle(world);
    audio_log_stop();
    test_int(songfile_state(world), SongFailed);
    test_assert(strstr(ecs_singleton_get(world, AudioSongStatus)->error, "missing.flecs") !=
        NULL);
    ecs_fini(world);
}

void SongFile_command(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "audio.song " SONGFILE_DIR "basic.flecs", &error);
    test_bool(error, false);
    test_assert(strstr(out, "basic.flecs") != NULL);
    ecs_os_free(out);
    songfile_settle(world);
    test_int(songfile_state(world), SongReady);

    out = box_command_run(world, s, "audio.song", &error);
    test_bool(error, false);
    test_assert(strstr(out, "song: ready") != NULL);
    test_assert(strstr(out, "\"Basic\"") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio", &error);
    test_bool(error, false);
    test_assert(strstr(out, "\nsong: ready '" SONGFILE_DIR "basic.flecs'") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.song none", &error);
    test_bool(error, false);
    ecs_os_free(out);
    songfile_settle(world);
    test_int(songfile_state(world), SongNone);
    test_assert(ecs_lookup(world, "basic_lead") == 0);
    ecs_fini(world);
}

void SongFile_jukebox_style(void) {
    // audio/song
    ecs_world_t *world = audio_world();
    ecs_entity_t host = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "host_scene" }),
        .code =
            "using box.audio\n"
            "using box.material\n"
            "host_view { Rgb: {Song[Song].beats_per_bar, Song[Song].lanes.count(), "
                "Song[Song].tempo} }\n"
    });
    test_assert(host != 0);
    const EcsScript *hs = ecs_get(world, host, EcsScript);
    test_assert(hs != NULL && hs->error == NULL);
    ecs_entity_t view = ecs_lookup(world, "host_view");
    test_assert(view != 0);
    test_int(ecs_get(world, view, Rgb)->b, 120);

    test_int(songfile_load(world, "other.flecs"), SongReady);
    audio_run(world, 2);
    test_assert(ecs_is_alive(world, view));
    test_int(ecs_get(world, view, Rgb)->g, 2);
    test_int(ecs_get(world, view, Rgb)->b, 140);

    test_int(songfile_load(world, "basic.flecs"), SongReady);
    audio_run(world, 2);
    test_int(ecs_get(world, view, Rgb)->r, 3);
    test_int(ecs_get(world, view, Rgb)->g, 1);
    test_int(ecs_get(world, view, Rgb)->b, 96);

    songfile_unload(world);
    audio_run(world, 2);
    hs = ecs_get(world, host, EcsScript);
    test_assert(hs != NULL && hs->error == NULL);
    test_assert(ecs_lookup(world, "host_view") == view);
    test_int(ecs_get(world, view, Rgb)->r, 4);
    test_int(ecs_get(world, view, Rgb)->g, 0);
    test_int(ecs_get(world, view, Rgb)->b, 120);
    ecs_fini(world);
}
