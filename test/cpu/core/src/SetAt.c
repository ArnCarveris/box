/**
 * @file SetAt.c
 * @brief Tests for SetAt: values a scene sets once at a known frame.
 */

#include <box.h>
#include <bake_test.h>

/** Create a world with box imported and an entity e at the origin.
 *
 * @param e Receives the entity
 * @return The world
 */
static ecs_world_t* set_at_world(
    ecs_entity_t *e)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *e = ecs_entity(world, { .name = "e" });
    ecs_set(world, *e, Position, {0, 0, 0});

    return world;
}

void SetAt_runs_at_frame(void) {
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, SetAt, { .frame = 2, .entity = e });
    ecs_set(world, s, Position, {1, 2, 3});

    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->x, 0);
    test_bool(ecs_is_alive(world, s), true);
    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->x, 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 2);

    ecs_progress(world, 0.1f);
    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);
    test_bool(ecs_is_alive(world, s), false);
    test_bool(ecs_has_id(world, e, EcsDisabled), false);
    test_bool(ecs_has(world, e, SetAt), false);

    ecs_set(world, e, Position, {5, 5, 5});
    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->x, 5);

    ecs_fini(world);
}

void SetAt_frame_zero_runs_first(void) {
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, SetAt, { .frame = 0, .entity = e });
    ecs_set(world, s, Position, {4, 0, 0});

    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->x, 4);
    test_bool(ecs_is_alive(world, s), false);

    ecs_fini(world);
}

void SetAt_from_script(void) {
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    test_int(ecs_script_run(world, "scene",
        "using box.app\n"
        "using box.transform\n"
        "using box.material\n"
        "parent {\n"
        "  child {}\n"
        "}\n"
        "change_e {\n"
        "  SetAt: {frame: 1, entity: e}\n"
        "  Position: {0, 7, 0}\n"
        "}\n"
        "change_child {\n"
        "  SetAt: {frame: 1, entity: parent.child}\n"
        "  Rgb: {10, 20, 30}\n"
        "}\n", NULL), 0);

    ecs_entity_t child = ecs_lookup(world, "parent.child");
    test_assert(child != 0);
    const SetAt *s = ecs_get(world, ecs_lookup(world, "change_child"), SetAt);
    test_assert(s != NULL);
    test_int(s->frame, 1);
    test_uint(s->entity, child);

    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->y, 0);
    test_bool(ecs_has(world, child, Rgb), false);
    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->y, 7);
    const Rgb *c = ecs_get(world, child, Rgb);
    test_assert(c != NULL);
    test_int(c->r, 10);
    test_int(c->g, 20);
    test_int(c->b, 30);
    test_assert(ecs_lookup(world, "change_e") == 0);
    test_assert(ecs_lookup(world, "change_child") == 0);

    ecs_fini(world);
}

void SetAt_no_entity(void) {
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, SetAt, { .frame = 0 });
    ecs_set(world, s, Position, {4, 0, 0});

    ecs_log_set_level(-4);
    ecs_progress(world, 0.1f);
    ecs_log_set_level(-1);
    test_bool(ecs_is_alive(world, s), false);
    test_flt(ecs_get(world, e, Position)->x, 0);

    ecs_progress(world, 0.1f);
    ecs_fini(world);
}

void SetAt_implied_not_copied(void) {
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {1, 2, 3});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, SetAt, { .frame = 1, .entity = e });
    ecs_set(world, s, Rgb, {40, 50, 60});

    test_bool(ecs_has_id(world, s, EcsDisabled), true);
    test_bool(ecs_has(world, s, MaterialId), true);
    ecs_progress(world, 0.1f);
    MaterialId id = *ecs_get(world, e, MaterialId);

    ecs_progress(world, 0.1f);
    test_bool(ecs_is_alive(world, s), false);
    test_int(ecs_get(world, e, Rgb)->r, 40);
    test_bool(ecs_has_id(world, e, EcsDisabled), false);
    test_uint(ecs_get(world, e, MaterialId)->value, id.value);

    ecs_progress(world, 0.1f);
    ecs_fini(world);
}

void SetAt_implied_authored(void) {
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_entity_t turn = ecs_new(world);
    ecs_set(world, turn, SetAt, { .frame = 0, .entity = e });
    ecs_set(world, turn, Rotation, {0, 0, 1});
    ecs_entity_t both = ecs_new(world);
    ecs_set(world, both, SetAt, { .frame = 1, .entity = e });
    ecs_set(world, both, Position, {7, 8, 9});
    ecs_set(world, both, Scale, {2, 2, 2});

    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Rotation)->z, 1);
    test_flt(ecs_get(world, e, Position)->x, 1);

    ecs_progress(world, 0.1f);
    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 7);
    test_flt(p->y, 8);
    test_flt(p->z, 9);
    test_flt(ecs_get(world, e, Scale)->x, 2);

    ecs_fini(world);
}
