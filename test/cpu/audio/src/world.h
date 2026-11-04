/**
 * @file world.h
 * @brief Helpers of the box.audio tests: a world with a tapped null output, sounds and levels.
 */

#ifndef AUDIO_TEST_WORLD_H
#define AUDIO_TEST_WORLD_H

#include "util.h"
#include <box.h>
#include <output.h>
#include <module.h>

/** Delta time of a frame of the tests, 800 frames at AUDIO_RATE */
#define AUDIO_DT (1.0f / 60.0f)

/** Frames the null output renders per frame of the tests */
#define AUDIO_FRAME 800

/** Frames the tap keeps, ten seconds */
#define AUDIO_TAP_MAX (AUDIO_RATE * 10)

/** Output of the null transport since the world was created */
extern float audio_tap_l[AUDIO_TAP_MAX];
extern float audio_tap_r[AUDIO_TAP_MAX];
extern int32_t audio_tap_count;

/** Errors logged while a capture of the log runs, and the last of them */
extern int32_t audio_log_errors;
extern char audio_log_last[1024];

/** Warnings logged while a capture of the log runs */
extern int32_t audio_log_warnings;

/** Append rendered frames to the tap, a box_audio_output_t.
 *
 * @param left Left channel
 * @param right Right channel
 * @param frames Number of frames
 * @param ctx Unused
 */
void audio_world_tap(
    const float *left,
    const float *right,
    int32_t frames,
    void *ctx);

/** Create a world with box and a null output at AUDIO_RATE whose frames go to the tap.
 *
 * @return The world
 */
ecs_world_t* audio_world(void);

/** Run frames of AUDIO_DT.
 *
 * @param world The world
 * @param frames Number of frames
 */
void audio_run(
    ecs_world_t *world,
    int32_t frames);

/** Create a mono sound of one value.
 *
 * @param world The world
 * @param name Name of the sound entity, NULL for none
 * @param level The value
 * @param frames Frames of the sound
 * @return The sound
 */
ecs_entity_t audio_dc(
    ecs_world_t *world,
    const char *name,
    float level,
    int32_t frames);

/** Create a mono sine sound at AUDIO_RATE.
 *
 * @param world The world
 * @param name Name of the sound entity, NULL for none
 * @param frequency Hz
 * @param level Peak level
 * @param frames Frames of the sound
 * @return The sound
 */
ecs_entity_t audio_tone(
    ecs_world_t *world,
    const char *name,
    double frequency,
    float level,
    int32_t frames);

/** Create a source.
 *
 * @param world The world
 * @param sound The sound
 * @param volume Volume
 * @param loop Whether it loops
 * @param bus Bus of its AudioOutput, 0 for none
 * @return The source
 */
ecs_entity_t audio_source(
    ecs_world_t *world,
    ecs_entity_t sound,
    float volume,
    bool loop,
    ecs_entity_t bus);

/** Create a bus with default values but its output and volume.
 *
 * @param world The world
 * @param name Name, NULL for none
 * @param output Bus it feeds, 0 for the master
 * @param volume Volume
 * @return The bus
 */
ecs_entity_t audio_bus(
    ecs_world_t *world,
    const char *name,
    ecs_entity_t output,
    float volume);

/** Mean of the right channel of the tap over the last frames.
 *
 * @param frames Number of frames
 * @return The mean
 */
float audio_level_right(
    int32_t frames);

/** Mean of the left channel of the tap over the last frames.
 *
 * @param frames Number of frames
 * @return The mean
 */
float audio_level(
    int32_t frames);

/** Position of a bus in the processing order of the module.
 *
 * @param world The world
 * @param bus The bus
 * @return The position, -1 when it is not in the order
 */
int32_t audio_position(
    ecs_world_t *world,
    ecs_entity_t bus);

/** Start to count the errors and warnings that are logged, without printing them.
 */
void audio_log_start(void);

/** Stop to count errors and restore the log.
 */
void audio_log_stop(void);

/** Read a file of the repository.
 *
 * @param file Path from the root of the repository
 * @return The contents, which the caller frees
 */
char* audio_read(
    const char *file);

#endif
