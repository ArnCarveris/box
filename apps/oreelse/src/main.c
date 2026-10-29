/**
 * @file main.c
 * @brief Runs Ore Else with the settings of the command line.
 */

#include "game.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** Take --seed <n> out of the command line.
 *
 * @param argc Argument count, the remaining arguments on return
 * @param argv Argument array, compacted to the remaining arguments
 * @param seed Receives the seed, the time when the command line gives none
 * @return 0 on success, nonzero when the value of --seed is not a number
 */
static int ore_seed_parse(
    int *argc,
    char *argv[],
    uint64_t *seed)
{
    int i, kept = 1, result = 0;
    char *end = NULL;
    *seed = (uint64_t)time(NULL);
    for (i = 1; i < *argc; i ++) {
        if (strcmp(argv[i], "--seed")) {
            argv[kept ++] = argv[i];
        } else if (i + 1 < *argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
            *seed = strtoull(argv[i + 1], &end, 10);
            result = end[0] ? -1 : result;
            i ++;
        } else {
            result = -1;
        }
    }

    *argc = kept;
    if (result) {
        ecs_err("invalid value for --seed, expected a whole number");
    }

    return result;
}

/** Take --stresstest or --stresstest=<n> out of the command line.
 *
 * @param argc Argument count, the remaining arguments on return
 * @param argv Argument array, compacted to the remaining arguments
 * @param enabled Receives whether the command line asks for the stress test
 * @param minutes Receives n, 0 without it (cfg.stressMinutes)
 * @return 0 on success, nonzero when the value of --stresstest is not a number
 */
static int ore_stresstest_parse(
    int *argc,
    char *argv[],
    bool *enabled,
    float *minutes)
{
    int i, kept = 1, result = 0;
    char *end = NULL;
    *enabled = false;
    *minutes = 0;
    for (i = 1; i < *argc; i ++) {
        if (!strcmp(argv[i], "--stresstest")) {
            *enabled = true;
        } else if (!strncmp(argv[i], "--stresstest=", 13)) {
            *enabled = true;
            *minutes = strtof(argv[i] + 13, &end);
            result = !argv[i][13] || end[0] ? -1 : result;
        } else {
            argv[kept ++] = argv[i];
        }
    }

    *argc = kept;
    if (result) {
        ecs_err("invalid value for --stresstest, expected a number of minutes");
    }

    return result;
}

/** Load the widget library etc/scenes/ui/widgets.flecs, set the game scene as the default, take the seed of the session and the stress test from
 * the command line, parse the rest into the App singleton, where a scene argument replaces the
 * default, and run the world.
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 when the run completed, nonzero when it was refused or failed
 */
int main(
    int argc,
    char *argv[])
{
    ecs_world_t *world = ecs_init();
    char *widgets = NULL;
    uint64_t seed = 0;
    float minutes = 0;
    bool stresstest = false;
    int result = 0;

    ECS_IMPORT(world, Box);
    ECS_IMPORT(world, Oreelse);

    widgets = box_app_locate("etc/scenes/ui/widgets.flecs");
    if (!ecs_script(world, {
        .entity = ecs_entity(world, { .name = "box.ui.widgets" }),
        .filename = widgets
    })) {
        ecs_err("failed to load %s", widgets);
    }

    ecs_os_free(widgets);

    App *app = ecs_singleton_get_mut(world, App);
    ecs_os_free(app->scene);
    app->scene = ecs_os_strdup(OREELSE_SCENE);
    ecs_singleton_modified(world, App);

    result = ore_seed_parse(&argc, argv, &seed);
    if (!result) {
        result = ore_stresstest_parse(&argc, argv, &stresstest, &minutes);
    }

    if (!result) {
        ecs_singleton_set(world, OreSeed, { .seed = seed, .run = 0 });
        if (stresstest) {
            ecs_singleton_set(world, OreStressTest, { .minutes = minutes, .run = -1 });
        }

        result = box_app_parse(world, argc, argv);
    }

    if (!result) {
        result = box_app_run(world);
    }

    ecs_fini(world);

    return result ? 1 : 0;
}
