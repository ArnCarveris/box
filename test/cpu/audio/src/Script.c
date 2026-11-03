/**
 * @file Script.c
 * @brief A scene of buses, a source and box.audio.play from test/assets/audio, run with
 * ecs_script_run, renders the levels its comment gives.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Read a file of the repository.
 *
 * @param file Path from the root of the repository
 * @return The contents, which the caller frees
 */
static char* script_read(
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

void Script_mixer_levels(void) {
    // audio/script
    ecs_world_t *world = audio_world();
    char *code = script_read("test/assets/audio/mixer.flecs");
    test_assert(ecs_script_run(world, "mixer", code, NULL) == 0);
    ecs_os_free(code);

    ecs_entity_t music = ecs_lookup(world, "music");
    ecs_entity_t sfx = ecs_lookup(world, "sfx");
    test_assert(music != 0);
    test_assert(sfx != 0);
    test_flt(ecs_get(world, sfx, AudioBus)->volume, 0.5f);
    test_flt(ecs_get(world, sfx, AudioBus)->pan, 0.0f);

    audio_run(world, 9);
    test_assert(audio_position(world, sfx) < audio_position(world, music));
    test_flt(audio_level(AUDIO_FRAME), 0.525f);

    audio_run(world, 20);
    test_flt(audio_level(AUDIO_FRAME), 0.125f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    ecs_fini(world);
}

void Script_level_change(void) {
    // audio/script
    ecs_world_t *world = audio_world();
    char *code = script_read("test/assets/audio/mixer.flecs");
    test_assert(ecs_script_run(world, "mixer", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 30);
    test_flt(audio_level(AUDIO_FRAME), 0.125f);

    test_assert(ecs_script_run(world, "louder",
        "using box.audio\n"
        "music { AudioBus: {volume: 1.0} }\n", NULL) == 0);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);
    ecs_fini(world);
}

void Script_play_function(void) {
    // audio/script
    ecs_world_t *world = audio_world();
    test_assert(ecs_script_run(world, "play",
        "using box.audio\n"
        "dc { AudioPcm: {samples: [0.3, 0.3, 0.3, 0.3], sample_rate: 20} }\n"
        "const a: bool = box.audio.play(dc, 0.5)\n"
        "const b: bool = box.audio.play(dc, 0.5)\n", NULL) == 0);
    audio_run(world, 6);
    test_flt(audio_level(AUDIO_FRAME), 0.3f);
    test_int(ecs_count(world, AudioSource), 0);
    ecs_fini(world);
}

void Script_spatial_scene(void) {
    // audio/script
    ecs_world_t *world = audio_world();
    char *code = script_read("test/assets/audio/spatial.flecs");
    test_assert(ecs_script_run(world, "spatial", code, NULL) == 0);
    ecs_os_free(code);
    test_assert(ecs_lookup(world, "ears") != 0);
    test_flt(ecs_get(world, ecs_lookup(world, "click"), AudioSpatial)->max_distance, 50.0f);

    audio_run(world, 6);
    test_assert(fabsf(audio_level(AUDIO_FRAME / 2) - 0.5657f) < 1e-3f);
    test_assert(fabsf(audio_level_right(AUDIO_FRAME / 2) - 0.7071f) < 1e-3f);

    audio_run(world, 20);
    test_assert(fabsf(audio_level(AUDIO_FRAME / 2)) < 1e-4f);
    test_assert(fabsf(audio_level_right(AUDIO_FRAME / 2) - 0.7071f) < 1e-3f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    ecs_fini(world);
}
