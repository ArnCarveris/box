/**
 * @file AudioCheck.c
 * @brief Tests for audio.check, the lint of the loaded song: the findings of a song with one
 * instance of each problem and their places, a clean song, voice steals and a clipping bus.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>

/** Delta of a frame of the tests */
#define CHECK_DT (1.0f / 60.0f)

/** Run a command that must succeed.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @return The output, which the caller frees
 */
static char* check_ok(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line)
{
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    if (error) {
        printf("%s: %s\n", line, out);
    }

    test_bool(error, false);

    return out;
}

/** Load a song of test/assets/audio/songs in a world without output and wait until it is ready.
 *
 * @param session Receives a shell session
 * @param file The song file
 * @return The world
 */
static ecs_world_t* check_world(
    ecs_entity_t *session,
    const char *file)
{
    int32_t i;
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *session = ecs_shell_session_init(world, NULL);
    box_audio_song_load(world, file);
    for (i = 0; i < 600; i ++) {
        test_bool(ecs_progress(world, CHECK_DT), true);
        const AudioSongStatus *st = ecs_singleton_get(world, AudioSongStatus);
        if (st && st->state != SongLoading) {
            break;
        }
    }

    test_int(ecs_singleton_get(world, AudioSongStatus)->state, SongReady);

    return world;
}

/** Load the song with one instance of each problem and empty slot 1 of check_fx_bus.
 *
 * @param session Receives a shell session
 * @return The world
 */
static ecs_world_t* check_problems(
    ecs_entity_t *session)
{
    ecs_world_t *world = check_world(session,
        "test/assets/audio/songs/check_problems.flecs");
    ecs_entity_t bus = ecs_lookup(world, "check_fx_bus");
    test_assert(bus != 0);
    AudioBus *b = ecs_get_mut(world, bus, AudioBus);
    test_assert(b != NULL);
    test_assert(b->effects[1] == ecs_lookup(world, "check_gap"));
    b->effects[1] = 0;
    ecs_modified(world, bus, AudioBus);

    return world;
}

/** Get the last line of a text.
 *
 * @param text The text
 * @return The last line
 */
static const char* check_last(
    const char *text)
{
    const char *nl = strrchr(text, '\n');

    return nl ? nl + 1 : text;
}

/** The lines of the static findings of check_problems.flecs */
static const char *check_static_lines[] = {
    "error note-range track check_high, clip check_high_notes, note 0 at 1.1: ",
    "warning note-range track check_low, clip check_low_notes, note 0 at 2.1: ",
    "warning note-length track check_low, clip check_low_notes, note 1 at 2.2: ",
    "warning note-velocity track check_low, clip check_low_notes, note 2 at 2.3: ",
    "warning note-overlap track check_dup, clip check_dup_notes, note 1 at 1.2: ",
    "info note-legato track check_glide, clip check_glide_notes, note 1 at 1.2: ",
    "warning clip-late track check_late, clip check_late_notes at 4.3: ",
    "warning note-late track check_late, clip check_late_notes, note 0 at 5.2: ",
    "warning clip-overlap track check_stack, clip check_tone_notes at 1.3: ",
    "error track-instrument track check_no_inst: ",
    "error instrument-sound instrument check_soundless_inst: ",
    "error bus-send bus check_send_bus: ",
    "warning bus-unreachable bus check_orphan_bus: ",
    "info bus-slot bus check_fx_bus, slot 1: ",
    "warning effect-disabled effect check_reverb in slot 2 of bus check_fx_bus: ",
    "warning effect-unused effect check_gap: ",
    "error song-lane song lane 0 \"Ghost\": ",
    NULL
};

void AudioCheck_problems(void) {
    ecs_entity_t s;
    ecs_world_t *world = check_problems(&s);
    char *out = check_ok(world, s, "audio.check");
    int32_t i;
    for (i = 0; check_static_lines[i]; i ++) {
        if (!strstr(out, check_static_lines[i])) {
            printf("missing: %s\n%s\n", check_static_lines[i], out);
        }

        test_assert(strstr(out, check_static_lines[i]) != NULL);
    }

    if (!strstr(out, "warning track-silent track check_silent: ")) {
        printf("%s\n", out);
    }

    test_assert(strstr(out, "warning track-silent track check_silent: ") != NULL);
    test_assert(strstr(out, "track-silent track check_dup:") == NULL);
    test_assert(strstr(out, "note-overlap track check_glide") == NULL);
    test_assert(strstr(out, "note-range track check_dup") == NULL);
    test_assert(strstr(out, "effect-unused effect check_eq:") == NULL);
    const char *last = check_last(out);
    test_assert(strstr(last, " errors, ") != NULL);
    test_assert(strstr(last, "; played bars 1..4: peak ") != NULL);

    char *again = check_ok(world, s, "audio.check");
    test_str(again, out);
    ecs_os_free(again);
    ecs_os_free(out);
    ecs_fini(world);
}

void AudioCheck_static(void) {
    ecs_entity_t s;
    ecs_world_t *world = check_problems(&s);
    char *out = check_ok(world, s, "audio.check static");
    int32_t i;
    for (i = 0; check_static_lines[i]; i ++) {
        test_assert(strstr(out, check_static_lines[i]) != NULL);
    }

    test_assert(strstr(out, "track-silent") == NULL);
    test_assert(strstr(out, "played bars") == NULL);
    test_assert(strstr(check_last(out), "(static checks only)") != NULL);
    ecs_os_free(out);

    out = check_ok(world, s, "audio.check bars=2..2 static");
    test_assert(strstr(out, "note-range track check_low, clip check_low_notes, note 0 at 2.1")
        != NULL);
    test_assert(strstr(out, "track check_high") == NULL);
    test_assert(strstr(out, "track check_dup") == NULL);
    test_assert(strstr(out, "error bus-send bus check_send_bus: ") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void AudioCheck_clean(void) {
    ecs_entity_t s;
    ecs_world_t *world = check_world(&s, "test/assets/audio/songs/check_clean.flecs");
    char *out = check_ok(world, s, "audio.check");
    const char *last = check_last(out);
    if (strncmp(last, "no problems", 11)) {
        printf("%s\n", out);
    }

    test_assert(strncmp(last, "no problems", 11) == 0);
    test_assert(strstr(out, "error ") == NULL);
    test_assert(strstr(out, "warning ") == NULL);
    test_assert(strstr(last, "; played bars 1..2: peak ") != NULL);
    ecs_os_free(out);

    out = check_ok(world, s, "audio.check static");
    test_str(out, "no problems (static checks only)");
    ecs_os_free(out);
    ecs_fini(world);
}

void AudioCheck_voice_steals(void) {
    ecs_entity_t s;
    ecs_world_t *world = check_world(&s, "test/assets/audio/songs/check_voices.flecs");
    char *out = check_ok(world, s, "audio.check");
    if (!strstr(out, "warning voice-steal song at 3.")) {
        printf("%s\n", out);
    }

    test_assert(strstr(out, "warning voice-steal song at 3.") != NULL);
    test_assert(strstr(out, "at the voice cap of 64 (peak ") != NULL);
    ecs_os_free(out);

    out = check_ok(world, s, "audio.check bars=1..2");
    test_assert(strstr(out, "voice-steal") == NULL);
    test_assert(strstr(check_last(out), "played bars 1..2") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void AudioCheck_clipping(void) {
    ecs_entity_t s;
    ecs_world_t *world = check_world(&s, "test/assets/audio/songs/check_clip.flecs");
    char *out = check_ok(world, s, "audio.check");
    if (!strstr(out, "warning bus-clip bus check_hot at 1.1") ||
        !strstr(out, " limiter master at 1.1: the master limiter reduces the gain"))
    {
        printf("%s\n", out);
    }

    test_assert(strstr(out, "warning bus-clip bus check_hot at 1.1") != NULL);
    test_assert(strstr(out, "its output peaks at +") != NULL);
    test_assert(strstr(out, "bus-clip track check_clip_track") == NULL);
    test_assert(strstr(out, " limiter master at 1.1: the master limiter reduces the gain") !=
        NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void AudioCheck_errors(void) {
    ecs_entity_t s;
    ecs_world_t *world = check_world(&s, "test/assets/audio/songs/check_clean.flecs");
    bool error = false;
    char *out = box_command_run(world, s, "audio.check loud", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid argument 'loud'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio.check bars=0..2", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid bars") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio.check bars=3..2", &error);
    test_bool(error, true);
    ecs_os_free(out);
    ecs_fini(world);
}
