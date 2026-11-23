/**
 * @file Time.c
 * @brief Tests for the time command.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

/** Run a command line in a session and check that it succeeds with an output.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @param expect The output it must print
 */
static void time_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line,
    const char *expect)
{
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, false);
    test_str(out, expect);
    ecs_os_free(out);
}

/** Run a command line in a session and check that it fails with a message.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @param text Text the error contains
 */
static void time_fail(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line,
    const char *text)
{
    bool error = false;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, true);
    test_assert(!strncmp(out, BOX_COMMAND_ERROR, strlen(BOX_COMMAND_ERROR)));
    test_assert(strstr(out, text) != NULL);
    ecs_os_free(out);
}

/** Make an entity with a time of day.
 *
 * @param world The world
 * @param hour Hour of the day
 * @param scale Simulated hours per second
 * @return The entity
 */
static ecs_entity_t time_sky(
    ecs_world_t *world,
    float hour,
    float scale)
{
    ecs_entity_t sky = ecs_entity(world, { .name = "sky" });

    ecs_set(world, sky, TimeOfDay, { .hour = hour, .day_of_year = 172, .latitude = 21,
        .time_scale = scale });
    return sky;
}

void Time_print(void) {
    // command/time
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    time_sky(world, 9.5f, 0);

    bool error = true;
    char *out = box_command_run(world, s, "time", &error);
    test_bool(error, false);
    test_assert(strstr(out, "time 9.50 h, day 172, speed 0.0000 h/s (paused)") != NULL);
    test_assert(strstr(out, "sun ") != NULL);
    test_assert(strstr(out, "daylight ") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void Time_set(void) {
    // command/time
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t sky = time_sky(world, 9.5f, 0);

    time_run(world, s, "time set 13.5", "time 13.50 h, day 172, speed 0.0000 h/s (paused)");
    test_assert(ecs_get(world, sky, TimeOfDay)->hour == 13.5f);
    test_assert(ecs_get(world, sky, TimeOfDayStatus)->hour == 13.5f);

    time_run(world, s, "time set 25", "time 1.00 h, day 172, speed 0.0000 h/s (paused)");
    test_assert(ecs_get(world, sky, TimeOfDayStatus)->hour == 1.0f);

    ecs_fini(world);
}

void Time_pause_keeps_the_clock(void) {
    // command/time
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t sky = time_sky(world, 9.5f, 2.0f);

    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    float hour = ecs_get(world, sky, TimeOfDayStatus)->hour;
    test_assert(hour > 9.5f);

    bool error = true;
    char *out = box_command_run(world, s, "time pause", &error);
    test_bool(error, false);
    test_assert(strstr(out, "(paused)") != NULL);
    ecs_os_free(out);

    test_assert(ecs_get(world, sky, TimeOfDay)->time_scale == 0);
    test_assert(ecs_get(world, sky, TimeOfDayStatus)->hour == hour);

    ecs_progress(world, 0.5f);
    test_assert(ecs_get(world, sky, TimeOfDayStatus)->hour == hour);

    ecs_fini(world);
}

void Time_speed_continues_from_now(void) {
    // command/time
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t sky = time_sky(world, 9.5f, 1.0f);

    ecs_progress(world, 1.0f);
    float hour = ecs_get(world, sky, TimeOfDayStatus)->hour;
    test_assert(hour > 9.5f);

    bool error = true;
    char *out = box_command_run(world, s, "time speed 0.25", &error);
    test_bool(error, false);
    test_assert(strstr(out, "speed 0.2500 h/s") != NULL);
    test_assert(strstr(out, "(paused)") == NULL);
    ecs_os_free(out);

    test_assert(ecs_get(world, sky, TimeOfDay)->time_scale == 0.25f);
    test_assert(ecs_get(world, sky, TimeOfDayStatus)->hour == hour);

    ecs_progress(world, 1.0f);
    test_assert(ecs_get(world, sky, TimeOfDayStatus)->hour > hour);

    ecs_fini(world);
}

void Time_invalid(void) {
    // command/time
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    time_fail(world, s, "time", "TimeOfDay");

    time_sky(world, 9.5f, 0);
    time_fail(world, s, "time later", "later");
    time_fail(world, s, "time set", "hour");
    time_fail(world, s, "time speed", "hours per real second");

    ecs_fini(world);
}
