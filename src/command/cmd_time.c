/**
 * @file command/cmd_time.c
 * @brief The time command: read and set the time of day of the scene.
 *
 * It only writes the hour and time_scale of the first TimeOfDay, box.atmosphere moves the sun and
 * moon from that change in the next frame, as it does for a change from a script or the REST API.
 */

#include "command.h"
#include <math.h>

/** Hours in a day */
#define BOX_CMD_TIME_DAY_HOURS (24.0f)

/** Find the entity of the time of day of the world: the first entity with a TimeOfDay.
 *
 * @param world The world
 * @return The entity, 0 when no scene has a time of day
 */
static ecs_entity_t box_cmd_time_find(
    ecs_world_t *world)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_each(world, TimeOfDay);

    while (ecs_each_next(&it)) {
        if (it.count) {
            result = it.entities[0];
            ecs_iter_fini(&it);
            break;
        }
    }

    return result;
}

/** Make the authored hour and day the ones the clock shows now.
 *
 * Setting TimeOfDay restarts its status at its hour and day, so a change of the speed must first
 * write the current ones or the clock would jump back to where it started.
 *
 * @param world The world
 * @param entity The entity of the time of day
 * @param t The time of day to write
 */
static void box_cmd_time_sync(
    ecs_world_t *world,
    ecs_entity_t entity,
    TimeOfDay *t)
{
    const TimeOfDayStatus *status = ecs_get(world, entity, TimeOfDayStatus);

    if (status) {
        t->hour = status->hour;
        t->day_of_year = status->day_of_year;
    }
}

/** Format the clock of a time of day.
 *
 * @param world The world
 * @param entity The entity of the time of day
 * @param t The time of day as it is now
 * @param sky Whether to add where the sun and moon are
 * @return The text, which the caller frees
 */
static char* box_cmd_time_text(
    ecs_world_t *world,
    ecs_entity_t entity,
    const TimeOfDay *t,
    bool sky)
{
    const TimeOfDayStatus *status = ecs_get(world, entity, TimeOfDayStatus);
    char *result;

    if (sky && status) {
        result = flecs_asprintf(
            "time %.2f h, day %d, speed %.4f h/s%s, sun %.1f deg, moon %.1f deg, daylight %.2f",
            (double)status->hour, status->day_of_year, (double)t->time_scale,
            t->time_scale == 0 ? " (paused)" : "", (double)status->sun_altitude,
            (double)status->moon_altitude, (double)status->daylight);
    } else {
        result = flecs_asprintf("time %.2f h, day %d, speed %.4f h/s%s",
            (double)t->hour, t->day_of_year, (double)t->time_scale,
            t->time_scale == 0 ? " (paused)" : "");
    }

    return result;
}

/** The time command: print the time of day, or set the hour, pause or set the speed.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The action and its value
 * @param result Receives the text
 */
static void box_cmd_time(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    const char *action = argc > 0 ? *(const char**)argv[0].ptr : NULL;
    bool has_value = argc > 1;
    float value = has_value ? *(const float*)argv[1].ptr : 0;
    ecs_entity_t entity = box_cmd_time_find(world);
    TimeOfDay *t;
    bool query = !action || !action[0];

    if (!entity) {
        ecs_err("no scene has a TimeOfDay");
        goto done;
    }

    t = ecs_get_mut(world, entity, TimeOfDay);
    if (query) {
        *(char**)result->ptr = box_cmd_time_text(world, entity, t, true);
        goto done;
    }

    if (!ecs_os_strcmp(action, "set")) {
        if (!has_value || !isfinite(value)) {
            ecs_err("time set needs an hour, 0 to 24");
            goto done;
        }

        box_cmd_time_sync(world, entity, t);
        t->hour = fmodf(value, BOX_CMD_TIME_DAY_HOURS);
        if (t->hour < 0) {
            t->hour += BOX_CMD_TIME_DAY_HOURS;
        }
    } else if (!ecs_os_strcmp(action, "pause")) {
        box_cmd_time_sync(world, entity, t);
        t->time_scale = 0;
    } else if (!ecs_os_strcmp(action, "speed")) {
        if (!has_value || !isfinite(value)) {
            ecs_err("time speed needs the simulated hours per real second");
            goto done;
        }

        box_cmd_time_sync(world, entity, t);
        t->time_scale = value;
    } else {
        ecs_err("invalid action '%s', expected set, pause or speed", action);
        goto done;
    }

    ecs_modified(world, entity, TimeOfDay);
    *(char**)result->ptr = box_cmd_time_text(world, entity, t, false);
done:
    return;
}

void box_cmd_time_init(
    ecs_world_t *world)
{
    box_command_register(world,
        "Print the time of day, set the hour, pause it or set its speed",
        (ecs_function_desc_t){
            .name = "time",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "action", .type = ecs_id(ecs_string_t), .optional = true,
                  .brief = "set, pause or speed (default: print)" },
                { .name = "value", .type = ecs_id(ecs_f32_t), .optional = true,
                  .brief = "Hour 0 to 24 for set, simulated hours per second for speed" }
            },
            .callback = box_cmd_time
        });
}
