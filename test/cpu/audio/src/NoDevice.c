/**
 * @file NoDevice.c
 * @brief No device is not an error: a test world and a headless run open none, and a failed
 * open leaves the app running with no error logged.
 */

#include "util.h"
#include <box.h>
#include <output.h>

/** Errors, and warnings of box.audio, logged while a case captures the log */
static int32_t no_device_errors;
static int32_t no_device_warnings;

static void no_device_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level <= -3) {
        no_device_errors ++;
    } else if (level == -2 && msg && strstr(msg, "box.audio")) {
        no_device_warnings ++;
    }
}

/** Saved log settings of a capture */
typedef struct no_device_capture_t {
    int32_t level;          /**< Log level before the capture */
    bool colors;            /**< Colors before the capture */
    ecs_os_api_log_t log;   /**< Log function before the capture */
} no_device_capture_t;

static no_device_capture_t no_device_capture_start(void) {
    no_device_capture_t c;
    no_device_errors = 0;
    no_device_warnings = 0;
    c.level = ecs_log_set_level(-1);
    c.colors = ecs_log_enable_colors(false);
    c.log = ecs_os_api.log_;
    ecs_os_api.log_ = no_device_log;

    return c;
}

static void no_device_capture_stop(
    no_device_capture_t c)
{
    ecs_os_api.log_ = c.log;
    ecs_log_set_level(c.level);
    ecs_log_enable_colors(c.colors);
}

/* The cases without a world call no flecs function that would set up the OS API they allocate
 * and log with. */
void NoDevice_setup(void) {
    ecs_os_set_api_defaults();
}

void NoDevice_test_world(void) {
    // audio/native
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t i;
    for (i = 0; i < 5; i ++) {
        test_bool(ecs_progress(world, 1.0f / 60.0f), true);
    }

    test_assert(box_audio_output_transport(world) == NULL);
    test_int(box_audio_output_state(world), BOX_AUDIO_STATE_OFF);
    ecs_fini(world);
}

void NoDevice_headless(void) {
    // audio/native
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *argv[] = {"boxrun", "--headless", "--frames", "3"};
    box_audio_transport_t *t;
    test_int(box_app_parse(world, 4, argv), 0);
    ecs_singleton_get_mut(world, App)->unguarded = true;
    ecs_singleton_get_mut(world, App)->unpaced = true;

    no_device_capture_t c = no_device_capture_start();
    test_int(box_app_run(world), 0);
    no_device_capture_stop(c);

    t = box_audio_output_transport(world);
    test_assert(t != NULL);
    test_int(t->kind, BOX_AUDIO_TRANSPORT_NULL);
    test_int(box_audio_output_state(world), BOX_AUDIO_STATE_NO_DEVICE);
    test_int(no_device_errors, 0);
    test_int(no_device_warnings, 0);
    ecs_fini(world);
}

void NoDevice_no_audio(void) {
    // audio/native
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *argv[] = {"boxrun", "--headless", "--no-audio", "--frames", "2"};
    test_int(box_app_parse(world, 5, argv), 0);
    test_bool(ecs_singleton_get(world, App)->no_audio, true);
    ecs_singleton_get_mut(world, App)->unguarded = true;
    ecs_singleton_get_mut(world, App)->unpaced = true;
    test_int(box_app_run(world), 0);

    test_assert(box_audio_output_transport(world) == NULL);
    test_int(box_audio_output_state(world), BOX_AUDIO_STATE_OFF);
    ecs_fini(world);
}

/* A run that is not headless, as the frame loop tests of core start one, opens no device with
 * no_audio. */
void NoDevice_app_no_audio(void) {
    // audio/native
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, App, { .frames = 3, .delta_time = 0.02f, .no_audio = true });

    no_device_capture_t c = no_device_capture_start();
    test_int(box_app_run(world), 0);
    no_device_capture_stop(c);

    test_assert(box_audio_output_transport(world) == NULL);
    test_int(box_audio_output_state(world), BOX_AUDIO_STATE_OFF);
    test_int(no_device_errors, 0);
    test_int(no_device_warnings, 0);
    ecs_fini(world);
}

void NoDevice_failed_open(void) {
    // audio/native
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_audio_transport_t *t;
    int32_t i;

    no_device_capture_t c = no_device_capture_start();
    box_audio_output_open(world, &(box_audio_output_desc_t){
        .backend = BOX_AUDIO_BACKEND_UNAVAILABLE });
    for (i = 0; i < 5; i ++) {
        test_bool(ecs_progress(world, 1.0f / 60.0f), true);
    }

    no_device_capture_stop(c);

    test_int(no_device_errors, 0);
    test_assert(no_device_warnings <= 1);
    t = box_audio_output_transport(world);
    test_assert(t != NULL);
    test_int(t->kind, BOX_AUDIO_TRANSPORT_NULL);
    test_int(box_audio_output_state(world), BOX_AUDIO_STATE_NO_DEVICE);
    test_bool(ecs_should_quit(world), false);

    box_audio_output_close(world);
    test_int(box_audio_output_state(world), BOX_AUDIO_STATE_OFF);
    ecs_fini(world);
}

void NoDevice_native_unavailable(void) {
    // audio/native
    box_audio_transport_t *t;
    no_device_capture_t c = no_device_capture_start();
    t = box_audio_transport_native_new(&(box_audio_native_desc_t){
        .backend = BOX_AUDIO_BACKEND_UNAVAILABLE });
    no_device_capture_stop(c);

    test_assert(t == NULL);
    test_int(no_device_errors, 0);
    test_assert(no_device_warnings <= 1);
}
