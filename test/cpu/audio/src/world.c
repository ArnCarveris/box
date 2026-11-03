/**
 * @file world.c
 * @brief Helpers of the box.audio tests: a world with a tapped null output, sounds and levels.
 */

#include "world.h"
#include <stdio.h>

float audio_tap_l[AUDIO_TAP_MAX];
float audio_tap_r[AUDIO_TAP_MAX];
int32_t audio_tap_count;
int32_t audio_log_errors;
char audio_log_last[1024];
int32_t audio_log_warnings;

static int32_t audio_log_level;
static bool audio_log_colors;
static ecs_os_api_log_t audio_log_prev;

void audio_world_tap(
    const float *left,
    const float *right,
    int32_t frames,
    void *ctx)
{
    int32_t n = frames;
    (void)ctx;
    if (audio_tap_count + n > AUDIO_TAP_MAX) {
        n = AUDIO_TAP_MAX - audio_tap_count;
    }

    memcpy(audio_tap_l + audio_tap_count, left, (size_t)n * sizeof(float));
    memcpy(audio_tap_r + audio_tap_count, right, (size_t)n * sizeof(float));
    audio_tap_count += n;
}

ecs_world_t* audio_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    audio_tap_count = 0;
    box_audio_output_open(world, &(box_audio_output_desc_t){
        .headless = true, .output = audio_world_tap });
    test_assert(box_audio_output_transport(world) != NULL);

    return world;
}

void audio_run(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        test_bool(ecs_progress(world, AUDIO_DT), true);
    }
}

ecs_entity_t audio_dc(
    ecs_world_t *world,
    const char *name,
    float level,
    int32_t frames)
{
    float *data = ecs_os_malloc_n(float, frames);
    ecs_entity_t e = name ? ecs_entity(world, { .name = name }) : 0;
    audio_fill(data, frames, level);
    e = box_audio_sound_from_pcm(world, e, data, frames, 1, AUDIO_RATE);
    ecs_os_free(data);
    test_assert(e != 0);

    return e;
}

ecs_entity_t audio_tone(
    ecs_world_t *world,
    const char *name,
    double frequency,
    float level,
    int32_t frames)
{
    float *data = ecs_os_malloc_n(float, frames);
    ecs_entity_t e = name ? ecs_entity(world, { .name = name }) : 0;
    audio_sine(data, frames, frequency, level);
    e = box_audio_sound_from_pcm(world, e, data, frames, 1, AUDIO_RATE);
    ecs_os_free(data);
    test_assert(e != 0);

    return e;
}

ecs_entity_t audio_source(
    ecs_world_t *world,
    ecs_entity_t sound,
    float volume,
    bool loop,
    ecs_entity_t bus)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, AudioSource, {
        .sound = sound, .volume = volume, .pitch = 1.0f, .loop = loop, .playing = true });
    if (bus) {
        ecs_set(world, e, AudioOutput, { .bus = bus });
    }

    return e;
}

ecs_entity_t audio_bus(
    ecs_world_t *world,
    const char *name,
    ecs_entity_t output,
    float volume)
{
    ecs_entity_t e = name ? ecs_entity(world, { .name = name }) : ecs_new(world);
    AudioBus *bus = ecs_ensure(world, e, AudioBus);
    bus->output = output;
    bus->volume = volume;
    ecs_modified(world, e, AudioBus);

    return e;
}

float audio_level(
    int32_t frames)
{
    double sum = 0;
    int32_t i, from = audio_tap_count - frames;
    test_assert(from >= 0);
    for (i = from; i < audio_tap_count; i ++) {
        sum += audio_tap_l[i];
    }

    return (float)(sum / frames);
}

float audio_level_right(
    int32_t frames)
{
    double sum = 0;
    int32_t i, from = audio_tap_count - frames;
    test_assert(from >= 0);
    for (i = from; i < audio_tap_count; i ++) {
        sum += audio_tap_r[i];
    }

    return (float)(sum / frames);
}

int32_t audio_position(
    ecs_world_t *world,
    ecs_entity_t bus)
{
    ecs_entity_t order[BOX_AUDIO_BUS_MAX];
    int32_t i, count = box_audio_module_order(world, order, BOX_AUDIO_BUS_MAX);
    int32_t result = -1;
    for (i = 0; i < count; i ++) {
        if (order[i] == bus) {
            result = i;
        }
    }

    return result;
}

static void audio_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level <= -3) {
        audio_log_errors ++;
        snprintf(audio_log_last, sizeof(audio_log_last), "%s", msg ? msg : "");
    } else if (level == -2) {
        audio_log_warnings ++;
    }
}

void audio_log_start(void) {
    audio_log_errors = 0;
    audio_log_warnings = 0;
    audio_log_last[0] = '\0';
    audio_log_level = ecs_log_set_level(-1);
    audio_log_colors = ecs_log_enable_colors(false);
    audio_log_prev = ecs_os_api.log_;
    ecs_os_api.log_ = audio_log;
}

void audio_log_stop(void) {
    ecs_os_api.log_ = audio_log_prev;
    ecs_log_set_level(audio_log_level);
    ecs_log_enable_colors(audio_log_colors);
}
