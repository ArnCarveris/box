/**
 * @file Async.c
 * @brief Tests for script tasks that await while templates are instantiated manually.
 */

#include <box.h>
#include <bake_test.h>

void Async_a_waiter_in_a_manual_template_resumes(void)
{
    // ore_engine/waiter_resume
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxScript);

    test_assert(0 != ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "Fired {}\n"
        "actions { Forward { InputAction } }\n"
        "template Waiter {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    await on.pressed(actions.Forward)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Waiter first\n" }));

    ecs_entity_t forward = ecs_lookup(world, "actions.Forward");
    ecs_entity_t fired = ecs_lookup(world, "Fired");
    ecs_set(world, ecs_new(world), Binding, { .mask = BoxKeyW, .action = forward });
    ecs_progress(world, 0);

    box_input_inject(world, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_bool(ecs_has_id(world, ecs_lookup(world, "first"), fired), true);

    ecs_fini(world);
}

void Async_several_waiters_resume_on_one_action(void)
{
    // ore_engine/waiter_resume
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxScript);

    test_assert(0 != ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "Fired {}\n"
        "actions { Forward { InputAction } }\n"
        "template Waiter {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    await on.pressed(actions.Forward)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Waiter first\n"
        "Waiter second\n" }));

    ecs_entity_t forward = ecs_lookup(world, "actions.Forward");
    ecs_entity_t fired = ecs_lookup(world, "Fired");
    ecs_set(world, ecs_new(world), Binding, { .mask = BoxKeyW, .action = forward });
    ecs_progress(world, 0);

    box_input_inject(world, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_bool(ecs_has_id(world, ecs_lookup(world, "first"), fired), true);
    test_bool(ecs_has_id(world, ecs_lookup(world, "second"), fired), true);

    ecs_fini(world);
}
