/**
 * @file ScriptAt.c
 * @brief Tests for ScriptAt: script code a scene runs once at a known frame.
 */

#include <box.h>
#include <bake_test.h>

/** Create a world with box imported and an entity e at the origin.
 *
 * @param e Receives the entity
 * @return The world
 */
static ecs_world_t* script_at_world(
    ecs_entity_t *e)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *e = ecs_entity(world, { .name = "e" });
    ecs_set(world, *e, Position, {0, 0, 0});
    return world;
}

void ScriptAt_runs_at_frame(void) {
    ecs_entity_t e;
    ecs_world_t *world = script_at_world(&e);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, ScriptAt, {
        .frame = 2, .code = "using box.transform\ne { Position: {1, 2, 3} }" });

    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->x, 0);
    test_bool(ecs_has(world, s, ScriptAt), true);
    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->x, 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 2);

    // Runs at the start of the third frame, before its systems
    ecs_progress(world, 0.1f);
    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);
    test_bool(ecs_has(world, s, ScriptAt), false);

    // Runs once
    ecs_set(world, e, Position, {5, 5, 5});
    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->x, 5);

    ecs_fini(world);
}

void ScriptAt_frame_zero_runs_first(void) {
    ecs_entity_t e;
    ecs_world_t *world = script_at_world(&e);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, ScriptAt, {
        .frame = 0, .code = "using box.transform\ne { Position: {4, 0, 0} }" });

    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->x, 4);
    test_bool(ecs_has(world, s, ScriptAt), false);

    ecs_fini(world);
}

void ScriptAt_from_script(void) {
    ecs_entity_t e;
    ecs_world_t *world = script_at_world(&e);
    test_int(ecs_script_run(world, "scene",
        "using box.app\n"
        "change {\n"
        "  ScriptAt: {frame: 1, code: `\n"
        "    using box.transform\n"
        "    e { Position: {0, 7, 0} }\n"
        "  `}\n"
        "}\n", NULL), 0);

    ecs_entity_t s = ecs_lookup(world, "change");
    test_assert(s != 0);
    test_assert(ecs_get(world, s, ScriptAt) != NULL);
    test_int(ecs_get(world, s, ScriptAt)->frame, 1);

    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->y, 0);
    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->y, 7);

    ecs_fini(world);
}

void ScriptAt_error_removed(void) {
    ecs_entity_t e;
    ecs_world_t *world = script_at_world(&e);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, ScriptAt, { .frame = 0, .code = "e { NotAComponent: {1} }" });

    ecs_log_set_level(-4);
    ecs_progress(world, 0.1f);
    ecs_log_set_level(-1);
    test_bool(ecs_has(world, s, ScriptAt), false);
    test_flt(ecs_get(world, e, Position)->x, 0);

    ecs_progress(world, 0.1f);
    ecs_fini(world);
}
