/**
 * @file StartupOverlay.c
 * @brief The startup overlay: it opens while a task of InProgressTask blocks, lists the tasks,
 * closes once none blocks and stays closed after that. The overlay loads the copy of its script
 * in test/assets/startup.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>

/** The script of the overlay the tests load */
#define STARTUP_SCRIPT "test/assets/startup/startup.flecs"

/** Frames a test waits at most for the overlay to close, while its fonts load */
#define STARTUP_WAIT (2000)

/** Create a world with box whose overlay loads the script of the tests.
 *
 * @return The world
 */
static ecs_world_t* startup_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_startup_script(world, STARTUP_SCRIPT);

    return world;
}

/** Let the overlay open in a world without box_app_run.
 *
 * @param world The world
 */
static void startup_enable(
    ecs_world_t *world)
{
    StartupOverlay *o = ecs_singleton_get_mut(world, StartupOverlay);
    o->enabled = true;
    ecs_singleton_modified(world, StartupOverlay);
}

/** Run frames.
 *
 * @param world The world
 * @param count Number of frames
 */
static void startup_frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 0);
    }
}

/** Get whether the overlay is open.
 *
 * @param world The world
 * @return StartupOverlay.open
 */
static bool startup_open(
    ecs_world_t *world)
{
    const StartupOverlay *o = ecs_singleton_get(world, StartupOverlay);
    test_assert(o != NULL);

    return o->open;
}

/** Run frames until the overlay closed, at most STARTUP_WAIT.
 *
 * @param world The world
 * @return Whether it closed
 */
static bool startup_wait_closed(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < STARTUP_WAIT && startup_open(world); i ++) {
        ecs_progress(world, 0);
    }

    return !startup_open(world);
}

/** Get the value of a Text.
 *
 * @param world The world
 * @param path Path of the entity with the Text
 * @return The value, NULL when there is none
 */
static const char* startup_text(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t e = ecs_lookup(world, path);
    const Text *t = e ? ecs_get(world, e, Text) : NULL;

    return t ? t->value : NULL;
}

void StartupOverlay_closed_by_default(void) {
    ecs_world_t *world = startup_world();
    const StartupOverlay *o = ecs_singleton_get(world, StartupOverlay);
    test_assert(o != NULL);
    test_bool(o->enabled, false);
    test_bool(o->open, false);
    test_bool(o->finished, false);

    box_task_start(world, "Blocking work", true, 1);
    startup_frames(world, 3);
    test_bool(startup_open(world), false);
    test_assert(ecs_lookup(world, BOX_STARTUP_VIEW) == 0);
    test_assert(ecs_lookup(world, BOX_STARTUP_UI) == 0);

    ecs_fini(world);
}

void StartupOverlay_opens_while_blocking(void) {
    ecs_world_t *world = startup_world();
    box_task_start(world, "Blocking work", true, 3);
    startup_enable(world);
    startup_frames(world, 2);

    test_bool(startup_open(world), true);
    ecs_entity_t view = ecs_lookup(world, BOX_STARTUP_VIEW);
    test_assert(view != 0);
    const View *v = ecs_get(world, view, View);
    test_assert(v != NULL);
    test_int(v->order, BOX_VIEW_ORDER_STARTUP);
    test_assert(v->camera == ecs_lookup(world, BOX_STARTUP_UI ".camera"));
    test_bool(v->clear.enabled, false);
    test_assert(ecs_has(world, ecs_lookup(world, BOX_STARTUP_UI), Scene));
    ecs_entity_t panel = ecs_lookup(world, BOX_STARTUP_PANEL);
    test_assert(panel != 0);
    test_assert(ecs_has_id(world, panel, ecs_lookup(world, BOX_STARTUP_TEMPLATE)));

    ecs_fini(world);
}

void StartupOverlay_not_for_non_blocking(void) {
    ecs_world_t *world = startup_world();
    box_task_start(world, "Baking", false, 3);
    startup_enable(world);
    startup_frames(world, 3);

    test_bool(startup_open(world), false);
    test_assert(ecs_lookup(world, BOX_STARTUP_VIEW) == 0);

    ecs_fini(world);
}

void StartupOverlay_lists_tasks(void) {
    ecs_world_t *world = startup_world();
    box_task_id_t refine = box_task_start(world, "Refining", false, 0);
    box_task_id_t block = box_task_start(world, "Blocking work", true, 4);
    box_task_advance(world, block, 1);
    startup_enable(world);
    startup_frames(world, 3);
    test_bool(startup_open(world), true);

    test_str(startup_text(world, BOX_STARTUP_PANEL ".task_0.label.name"), "Blocking work");
    test_str(startup_text(world, BOX_STARTUP_PANEL ".task_0.label.progress"), "1 / 4");
    const InProgressTask *t = ecs_singleton_get(world, InProgressTask);
    int32_t last = t->count - 1;
    test_assert(last >= 1);
    test_str(t->tasks[last].name, "Refining");
    char path[256];
    snprintf(path, sizeof(path), BOX_STARTUP_PANEL ".task_%d.label.name", last);
    test_str(startup_text(world, path), "Refining");
    snprintf(path, sizeof(path), BOX_STARTUP_PANEL ".task_%d.label.progress", last);
    test_assert(ecs_lookup(world, path) == 0);

    box_task_set(world, block, 3, 4);
    startup_frames(world, 2);
    test_str(startup_text(world, BOX_STARTUP_PANEL ".task_0.label.progress"), "3 / 4");

    box_task_end(world, refine);
    box_task_end(world, block);
    ecs_fini(world);
}

void StartupOverlay_closes_when_none_blocks(void) {
    ecs_world_t *world = startup_world();
    box_task_id_t block = box_task_start(world, "Blocking work", true, 1);
    box_task_id_t refine = box_task_start(world, "Refining", false, 0);
    startup_enable(world);
    startup_frames(world, 2);
    test_bool(startup_open(world), true);

    box_task_end(world, block);
    test_bool(startup_wait_closed(world), true);
    const StartupOverlay *o = ecs_singleton_get(world, StartupOverlay);
    test_bool(o->finished, true);
    test_assert(ecs_lookup(world, BOX_STARTUP_VIEW) == 0);
    test_assert(ecs_lookup(world, BOX_STARTUP_UI) == 0);
    test_assert(box_task_find(ecs_singleton_get(world, InProgressTask), "Refining") != NULL);

    box_task_end(world, refine);
    ecs_fini(world);
}

void StartupOverlay_does_not_reopen(void) {
    ecs_world_t *world = startup_world();
    box_task_id_t block = box_task_start(world, "Blocking work", true, 1);
    startup_enable(world);
    startup_frames(world, 2);
    test_bool(startup_open(world), true);
    box_task_end(world, block);
    test_bool(startup_wait_closed(world), true);

    block = box_task_start(world, "Rebaking", true, 1);
    startup_frames(world, 5);
    test_bool(startup_open(world), false);
    test_bool(ecs_singleton_get(world, StartupOverlay)->finished, true);
    test_assert(ecs_lookup(world, BOX_STARTUP_VIEW) == 0);
    test_int(ecs_singleton_get(world, InProgressTask)->blocking, 1);

    box_task_end(world, block);
    ecs_fini(world);
}

void StartupOverlay_no_task_in_first_frames(void) {
    ecs_world_t *world = startup_world();
    startup_enable(world);
    startup_frames(world, BOX_STARTUP_FRAMES + 2);
    test_bool(startup_open(world), false);
    test_bool(ecs_singleton_get(world, StartupOverlay)->finished, true);

    box_task_id_t block = box_task_start(world, "Late work", true, 1);
    startup_frames(world, 3);
    test_bool(startup_open(world), false);
    test_assert(ecs_lookup(world, BOX_STARTUP_VIEW) == 0);

    box_task_end(world, block);
    ecs_fini(world);
}

void StartupOverlay_disabled(void) {
    ecs_world_t *world = startup_world();
    box_task_start(world, "Blocking work", true, 1);
    StartupOverlay *o = ecs_singleton_get_mut(world, StartupOverlay);
    o->enabled = true;
    o->disabled = true;
    ecs_singleton_modified(world, StartupOverlay);
    startup_frames(world, 3);
    test_bool(startup_open(world), false);
    test_assert(ecs_lookup(world, BOX_STARTUP_VIEW) == 0);

    ecs_fini(world);
}

void StartupOverlay_app_run_with_window(void) {
    ecs_world_t *world = startup_world();
    box_task_start(world, "Blocking work", true, 1);
    AppStatus *status = ecs_singleton_get_mut(world, AppStatus);
    status->running = true;
    ecs_singleton_modified(world, AppStatus);
    startup_frames(world, 2);
    test_bool(startup_open(world), true);
    test_assert(ecs_lookup(world, BOX_STARTUP_VIEW) != 0);

    status = ecs_singleton_get_mut(world, AppStatus);
    status->running = false;
    ecs_singleton_modified(world, AppStatus);
    ecs_fini(world);
}

void StartupOverlay_headless_never_opens(void) {
    ecs_world_t *world = startup_world();
    box_task_start(world, "Blocking work", true, 1);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    AppStatus *status = ecs_singleton_get_mut(world, AppStatus);
    status->running = true;
    ecs_singleton_modified(world, AppStatus);
    startup_frames(world, 3);
    test_bool(startup_open(world), false);
    test_assert(ecs_lookup(world, BOX_STARTUP_VIEW) == 0);

    status = ecs_singleton_get_mut(world, AppStatus);
    status->running = false;
    ecs_singleton_modified(world, AppStatus);
    ecs_fini(world);
}

void StartupOverlay_script_from_test_folder(void) {
    ecs_world_t *world = startup_world();
    box_task_start(world, "Blocking work", true, 1);
    startup_enable(world);
    startup_frames(world, 2);

    ecs_entity_t script = ecs_lookup(world, BOX_STARTUP_SCRIPT);
    test_assert(script != 0);
    const EcsScript *loaded = ecs_get(world, script, EcsScript);
    test_assert(loaded != NULL);
    test_assert(loaded->error == NULL);
    test_assert(loaded->filename != NULL);
    test_assert(strstr(loaded->filename, STARTUP_SCRIPT) != NULL);
    test_str(startup_text(world, BOX_STARTUP_PANEL ".heading"), "Starting");

    ecs_fini(world);
}
