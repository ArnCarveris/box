/**
 * @file InProgressTask.c
 * @brief The InProgressTask singleton: reports started, advanced and ended through the task API
 * give its entries, counts and blocking counts, and reports of the same name combine.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>

/** Create a world with box.
 *
 * @return The world
 */
static ecs_world_t* task_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    return world;
}

/** Run a frame and get the tasks it wrote.
 *
 * @param world The world
 * @return The tasks
 */
static const InProgressTask* task_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    const InProgressTask *result = ecs_singleton_get(world, InProgressTask);
    test_assert(result != NULL);

    return result;
}

void InProgressTask_empty_by_default(void) {
    ecs_world_t *world = task_world();
    const InProgressTask *t = ecs_singleton_get(world, InProgressTask);
    test_assert(t != NULL);
    test_int(t->count, 0);
    test_int(t->blocking, 0);
    test_assert(t->tasks[0].name == NULL);

    t = task_frame(world);
    test_int(t->count, 0);
    test_int(t->blocking, 0);

    ecs_fini(world);
}

void InProgressTask_start_lists_task(void) {
    ecs_world_t *world = task_world();
    box_task_id_t id = box_task_start(world, "Loading level", true, 4);
    test_assert(id != 0);

    const InProgressTask *t = task_frame(world);
    test_int(t->count, 1);
    test_int(t->blocking, 1);
    test_str(t->tasks[0].name, "Loading level");
    test_bool(t->tasks[0].blocking, true);
    test_int(t->tasks[0].done, 0);
    test_int(t->tasks[0].total, 4);
    test_assert(t->tasks[1].name == NULL);

    ecs_fini(world);
}

void InProgressTask_advance_and_set(void) {
    ecs_world_t *world = task_world();
    box_task_id_t id = box_task_start(world, "Loading level", false, 10);
    box_task_advance(world, id, 3);
    const InProgressTask *t = task_frame(world);
    test_int(t->count, 1);
    test_int(t->blocking, 0);
    test_int(t->tasks[0].done, 3);
    test_int(t->tasks[0].total, 10);

    box_task_advance(world, id, 2);
    t = task_frame(world);
    test_int(t->tasks[0].done, 5);

    box_task_set(world, id, 7, 20);
    t = task_frame(world);
    test_int(t->tasks[0].done, 7);
    test_int(t->tasks[0].total, 20);

    box_task_set(world, id, 1, 0);
    t = task_frame(world);
    test_int(t->tasks[0].done, 1);
    test_int(t->tasks[0].total, 0);

    ecs_fini(world);
}

void InProgressTask_end_removes_task(void) {
    ecs_world_t *world = task_world();
    box_task_id_t a = box_task_start(world, "First", true, 1);
    box_task_id_t b = box_task_start(world, "Second", false, 1);
    test_assert(a != b);
    const InProgressTask *t = task_frame(world);
    test_int(t->count, 2);

    box_task_end(world, a);
    t = task_frame(world);
    test_int(t->count, 1);
    test_int(t->blocking, 0);
    test_str(t->tasks[0].name, "Second");
    test_assert(t->tasks[1].name == NULL);

    box_task_end(world, a);
    box_task_end(world, 0);
    box_task_advance(world, a, 1);
    t = task_frame(world);
    test_int(t->count, 1);

    box_task_end(world, b);
    t = task_frame(world);
    test_int(t->count, 0);
    test_int(t->blocking, 0);
    test_assert(t->tasks[0].name == NULL);

    ecs_fini(world);
}

void InProgressTask_same_name_combines(void) {
    ecs_world_t *world = task_world();
    char name[32];
    strcpy(name, "Compiling");
    box_task_id_t a = box_task_start(world, name, false, 4);
    strcpy(name, "Other");
    box_task_id_t b = box_task_start(world, "Compiling", true, 6);
    box_task_advance(world, a, 1);
    box_task_advance(world, b, 2);

    const InProgressTask *t = task_frame(world);
    test_int(t->count, 1);
    test_int(t->blocking, 1);
    test_str(t->tasks[0].name, "Compiling");
    test_bool(t->tasks[0].blocking, true);
    test_int(t->tasks[0].done, 3);
    test_int(t->tasks[0].total, 10);

    box_task_end(world, b);
    t = task_frame(world);
    test_int(t->count, 1);
    test_int(t->blocking, 0);
    test_bool(t->tasks[0].blocking, false);
    test_int(t->tasks[0].done, 1);
    test_int(t->tasks[0].total, 4);

    box_task_end(world, a);
    t = task_frame(world);
    test_int(t->count, 0);

    ecs_fini(world);
}

void InProgressTask_combined_unknown_total(void) {
    ecs_world_t *world = task_world();
    box_task_id_t a = box_task_start(world, "Work", true, 4);
    box_task_id_t b = box_task_start(world, "Work", true, 0);
    box_task_advance(world, a, 1);
    box_task_advance(world, b, 1);

    const InProgressTask *t = task_frame(world);
    test_int(t->count, 1);
    test_int(t->tasks[0].done, 2);
    test_int(t->tasks[0].total, 0);

    box_task_end(world, b);
    t = task_frame(world);
    test_int(t->tasks[0].done, 1);
    test_int(t->tasks[0].total, 4);

    ecs_fini(world);
}

void InProgressTask_blocking_first(void) {
    ecs_world_t *world = task_world();
    box_task_start(world, "Refine A", false, 0);
    box_task_start(world, "Block A", true, 0);
    box_task_start(world, "Refine B", false, 0);
    box_task_start(world, "Block B", true, 0);

    const InProgressTask *t = task_frame(world);
    test_int(t->count, 4);
    test_int(t->blocking, 2);
    test_str(t->tasks[0].name, "Block A");
    test_str(t->tasks[1].name, "Block B");
    test_str(t->tasks[2].name, "Refine A");
    test_str(t->tasks[3].name, "Refine B");
    test_bool(t->tasks[0].blocking, true);
    test_bool(t->tasks[1].blocking, true);
    test_bool(t->tasks[2].blocking, false);
    test_bool(t->tasks[3].blocking, false);

    ecs_fini(world);
}

void InProgressTask_limit(void) {
    ecs_world_t *world = task_world();
    char name[32];
    int32_t i;
    for (i = 0; i < BOX_TASK_MAX + 4; i ++) {
        snprintf(name, sizeof(name), "Task %d", i);
        box_task_start(world, name, i == BOX_TASK_MAX + 2, 1);
    }

    const InProgressTask *t = task_frame(world);
    test_int(t->count, BOX_TASK_MAX);
    test_int(t->blocking, 0);
    test_str(t->tasks[0].name, "Task 0");
    test_str(t->tasks[BOX_TASK_MAX - 1].name, "Task 15");

    ecs_fini(world);
}

void InProgressTask_remaining(void) {
    ecs_world_t *world = task_world();
    box_task_track_t track = {0};
    box_task_remaining(world, &track, "Decoding", false, 0);
    test_assert(track.id == 0);
    const InProgressTask *t = task_frame(world);
    test_int(t->count, 0);

    box_task_remaining(world, &track, "Decoding", false, 3);
    test_assert(track.id != 0);
    t = task_frame(world);
    test_int(t->count, 1);
    test_int(t->tasks[0].done, 0);
    test_int(t->tasks[0].total, 3);

    box_task_remaining(world, &track, "Decoding", false, 2);
    t = task_frame(world);
    test_int(t->tasks[0].done, 1);
    test_int(t->tasks[0].total, 3);

    box_task_remaining(world, &track, "Decoding", false, 4);
    t = task_frame(world);
    test_int(t->tasks[0].done, 1);
    test_int(t->tasks[0].total, 5);

    box_task_remaining(world, &track, "Decoding", false, 0);
    test_assert(track.id == 0);
    t = task_frame(world);
    test_int(t->count, 0);

    box_task_remaining(world, &track, "Decoding", false, 2);
    t = task_frame(world);
    test_int(t->tasks[0].done, 0);
    test_int(t->tasks[0].total, 2);

    ecs_fini(world);
}

void InProgressTask_find(void) {
    ecs_world_t *world = task_world();
    box_task_start(world, "Alpha", false, 2);
    box_task_start(world, "Beta", true, 3);
    const InProgressTask *t = task_frame(world);

    const InProgressTaskItem *item = box_task_find(t, "Alpha");
    test_assert(item != NULL);
    test_int(item->total, 2);
    item = box_task_find(t, "Beta");
    test_assert(item != NULL);
    test_bool(item->blocking, true);
    test_assert(box_task_find(t, "Gamma") == NULL);
    test_assert(box_task_find(NULL, "Alpha") == NULL);

    ecs_fini(world);
}

void InProgressTask_publish_now(void) {
    ecs_world_t *world = task_world();
    box_task_id_t id = box_task_start(world, "Now", true, 1);
    test_int(ecs_singleton_get(world, InProgressTask)->count, 0);

    box_task_publish(world);
    const InProgressTask *t = ecs_singleton_get(world, InProgressTask);
    test_int(t->count, 1);
    test_int(t->blocking, 1);

    box_task_end(world, id);
    box_task_publish(world);
    test_int(ecs_singleton_get(world, InProgressTask)->count, 0);

    ecs_fini(world);
}

/** Count the sets of InProgressTask.
 *
 * @param it Iterator of the observer, whose ctx is the counter
 */
static void task_count_set(
    ecs_iter_t *it)
{
    int32_t *count = it->ctx;
    (*count) ++;
}

void InProgressTask_unchanged_not_set(void) {
    ecs_world_t *world = task_world();
    int32_t sets = 0;
    box_task_id_t id = box_task_start(world, "Steady", true, 4);
    task_frame(world);
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(InProgressTask), .src.id = ecs_id(InProgressTask) }},
        .events = { EcsOnSet },
        .callback = task_count_set,
        .ctx = &sets
    });

    box_task_set(world, id, 0, 4);
    task_frame(world);
    task_frame(world);
    test_int(sets, 0);

    box_task_advance(world, id, 1);
    task_frame(world);
    test_int(sets, 1);
    test_int(ecs_singleton_get(world, InProgressTask)->tasks[0].done, 1);

    ecs_fini(world);
}

void InProgressTask_reflected(void) {
    ecs_world_t *world = task_world();
    box_task_id_t id = box_task_start(world, "Compiling shaders", true, 8);
    box_task_advance(world, id, 3);
    const InProgressTask *t = task_frame(world);

    char *json = ecs_ptr_to_json(world, ecs_id(InProgressTask), t);
    test_assert(json != NULL);
    test_assert(strstr(json, "\"count\":1") != NULL);
    test_assert(strstr(json, "\"blocking\":1") != NULL);
    test_assert(strstr(json, "\"name\":\"Compiling shaders\"") != NULL);
    test_assert(strstr(json, "\"done\":3") != NULL);
    test_assert(strstr(json, "\"total\":8") != NULL);
    ecs_os_free(json);

    char *expr = ecs_ptr_to_expr(world, ecs_id(InProgressTask), t);
    test_assert(expr != NULL);
    test_assert(strstr(expr, "Compiling shaders") != NULL);
    ecs_os_free(expr);

    ecs_fini(world);
}

void InProgressTask_without_world(void) {
    box_task_id_t id = box_task_start(NULL, "Nothing", true, 1);
    test_assert(id == 0);
    box_task_advance(NULL, id, 1);
    box_task_set(NULL, id, 1, 1);
    box_task_end(NULL, id);
    box_task_publish(NULL);
}
