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

    ecs_entity_t origin = ecs_new(world);
    ecs_set(world, origin, SetAt, { .frame = 2, .entity = e });
    ecs_set(world, origin, Position, {0, 0, 0});
    ecs_set(world, origin, Rotation, {0, 0.5f, 0});

    ecs_progress(world, 0.1f);
    p = ecs_get(world, e, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, 0);
    const Rotation *r = ecs_get(world, e, Rotation);
    test_flt(r->x, 0);
    test_flt(r->y, 0.5f);
    test_flt(r->z, 0);
    test_bool(ecs_is_alive(world, origin), false);

    ecs_fini(world);
}

void SetAt_added_not_copied(void) {
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {1, 2, 3});
    ecs_progress(world, 0.1f);
    MaterialId id = *ecs_get(world, e, MaterialId);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, SetAt, { .frame = 1, .entity = e });
    ecs_set(world, s, Rotation, {0, 0.5f, 0});
    ecs_set(world, s, Rgb, {40, 50, 60});
    test_bool(ecs_has(world, s, Position), true);
    test_bool(ecs_has(world, s, MaterialId), true);

    ecs_progress(world, 0.1f);
    test_bool(ecs_is_alive(world, s), false);
    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);
    test_flt(ecs_get(world, e, Rotation)->y, 0.5f);
    test_int(ecs_get(world, e, Rgb)->r, 40);
    test_uint(ecs_get(world, e, MaterialId)->value, id.value);

    ecs_progress(world, 0.1f);
    ecs_fini(world);
}

void SetAt_set_last(void) {
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {1, 2, 3});
    ecs_progress(world, 0.1f);
    MaterialId id = *ecs_get(world, e, MaterialId);

    ecs_entity_t turn = ecs_new(world);
    ecs_set(world, turn, Rotation, {0, 0.5f, 0});
    ecs_set(world, turn, Rgb, {40, 50, 60});
    ecs_set(world, turn, SetAt, { .frame = 1, .entity = e });
    ecs_entity_t move = ecs_new(world);
    ecs_set(world, move, Position, {5, 0, 0});
    ecs_set(world, move, Scale, {2, 2, 2});
    ecs_set(world, move, SetAt, { .frame = 2, .entity = e });

    ecs_progress(world, 0.1f);
    test_bool(ecs_is_alive(world, turn), false);
    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);
    test_flt(ecs_get(world, e, Rotation)->y, 0.5f);
    test_int(ecs_get(world, e, Rgb)->r, 40);
    test_uint(ecs_get(world, e, MaterialId)->value, id.value);

    ecs_progress(world, 0.1f);
    test_bool(ecs_is_alive(world, move), false);
    p = ecs_get(world, e, Position);
    test_flt(p->x, 5);
    test_flt(p->y, 0);
    test_flt(ecs_get(world, e, Scale)->x, 2);

    ecs_fini(world);
}

/** Run a scene that changes e with SetAt at frame 1, then check e after the frame.
 *
 * @param scene The scene, with SetAt entities whose entity is e
 * @param x Expected Position.x of e
 * @param y Expected Rotation.y of e
 */
static void set_at_script_check(
    const char *scene,
    float x,
    float y)
{
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {1, 2, 3});
    ecs_progress(world, 0.1f);
    MaterialId id = *ecs_get(world, e, MaterialId);

    test_int(ecs_script_run(world, "scene", scene, NULL), 0);
    ecs_progress(world, 0.1f);
    test_assert(ecs_lookup(world, "change") == 0);

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, x);
    test_flt(p->y, x == 1 ? 2 : 0);
    test_flt(p->z, x == 1 ? 3 : 0);
    test_flt(ecs_get(world, e, Rotation)->y, y);
    test_int(ecs_get(world, e, Rgb)->r, 40);
    test_uint(ecs_get(world, e, MaterialId)->value, id.value);

    ecs_progress(world, 0.1f);
    ecs_fini(world);
}

void SetAt_script_first_zero(void) {
    set_at_script_check(
        "using box.app\n"
        "using box.transform\n"
        "using box.material\n"
        "change {\n"
        "  SetAt: {frame: 1, entity: e}\n"
        "  Position: {0, 0, 0}\n"
        "  Rotation: {0, 0.5, 0}\n"
        "  Rgb: {40, 50, 60}\n"
        "}\n", 0, 0.5f);
}

void SetAt_script_first_zero_after_rotation(void) {
    set_at_script_check(
        "using box.app\n"
        "using box.transform\n"
        "using box.material\n"
        "change {\n"
        "  SetAt: {frame: 1, entity: e}\n"
        "  Rotation: {0, 0.5, 0}\n"
        "  Position: {0, 0, 0}\n"
        "  Rgb: {40, 50, 60}\n"
        "}\n", 0, 0.5f);
}

void SetAt_script_first_added(void) {
    set_at_script_check(
        "using box.app\n"
        "using box.transform\n"
        "using box.material\n"
        "change {\n"
        "  SetAt: {frame: 1, entity: e}\n"
        "  Rotation: {0, 0.5, 0}\n"
        "  Rgb: {40, 50, 60}\n"
        "}\n", 1, 0.5f);
}

void SetAt_script_last_added(void) {
    set_at_script_check(
        "using box.app\n"
        "using box.transform\n"
        "using box.material\n"
        "change {\n"
        "  Rotation: {0, 0.5, 0}\n"
        "  Rgb: {40, 50, 60}\n"
        "  SetAt: {frame: 1, entity: e}\n"
        "}\n", 1, 0.5f);
}

void SetAt_script_last_written(void) {
    set_at_script_check(
        "using box.app\n"
        "using box.transform\n"
        "using box.material\n"
        "change {\n"
        "  Position: {5, 0, 0}\n"
        "  Rotation: {0, 0.5, 0}\n"
        "  Rgb: {40, 50, 60}\n"
        "  SetAt: {frame: 1, entity: e}\n"
        "}\n", 5, 0.5f);
}

/** A component without hooks or observers. */
typedef struct SetAtValue {
    int32_t value;  /**< The value */
} SetAtValue;

/** A second component without hooks or observers, never written to a SetAt entity. */
typedef struct SetAtOther {
    int32_t value;  /**< The value */
} SetAtOther;

/** Check whether the writes to SetAt entities are recorded: AppSetAtOnSet, the parent of their
 * observers, exists only while the world has SetAt entities.
 *
 * @param world The world
 * @return Whether AppSetAtOnSet exists
 */
static bool set_at_active(
    ecs_world_t *world)
{
    return ecs_lookup(world, "box.app.AppSetAtOnSet") != 0;
}

/** Check whether the writes to a component of SetAt entities are recorded.
 *
 * @param world The world
 * @param id The component
 * @return Whether an observer of AppSetAtOnSet records the writes to id
 */
static bool set_at_observes(
    ecs_world_t *world,
    ecs_id_t id)
{
    ecs_entity_t scope = ecs_lookup(world, "box.app.AppSetAtOnSet");
    bool result = false;
    if (scope) {
        ecs_iter_t it = ecs_children(world, scope);
        while (ecs_children_next(&it)) {
            int32_t i;
            for (i = 0; i < it.count; i ++) {
                const ecs_observer_t *o = ecs_observer_get(world, it.entities[i]);
                test_assert(o != NULL);
                result |= o->query->terms[0].id == id;
            }
        }
    }

    return result;
}

/** Check whether flecs emits OnSet for the sets of the table of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return Whether the table of e emits OnSet
 */
static bool set_at_emits(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_table_has_flags(ecs_get_table(world, e), EcsTableHasOnSet);
}

void SetAt_observer_on_demand(void) {
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    ECS_COMPONENT(world, SetAtValue);
    ECS_COMPONENT(world, SetAtOther);
    ecs_progress(world, 0.1f);
    test_bool(set_at_active(world), false);
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, SetAtValue, {1});
    test_bool(set_at_emits(world, plain), false);

    ecs_entity_t first = ecs_new(world);
    ecs_set(world, first, SetAt, { .frame = 2, .entity = e });
    test_bool(set_at_active(world), true);
    ecs_set(world, first, Position, {1, 2, 3});
    ecs_set(world, first, SetAtValue, {5});
    test_bool(set_at_observes(world, ecs_id(Position)), true);
    test_bool(set_at_observes(world, ecs_id(SetAtValue)), true);
    test_bool(set_at_observes(world, ecs_id(SetAtOther)), false);
    ecs_entity_t other_plain = ecs_new(world);
    ecs_set(world, other_plain, SetAtOther, {1});
    test_bool(set_at_emits(world, other_plain), false);

    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, SetAt, { .frame = 3, .entity = e });
    ecs_set(world, other, Scale, {2, 2, 2});
    ecs_delete(world, other);
    test_bool(set_at_active(world), true);

    ecs_progress(world, 0.1f);
    test_bool(ecs_is_alive(world, first), true);
    test_bool(set_at_active(world), true);
    ecs_progress(world, 0.1f);
    test_bool(ecs_is_alive(world, first), false);
    test_flt(ecs_get(world, e, Position)->x, 1);
    test_int(ecs_get(world, e, SetAtValue)->value, 5);
    test_bool(ecs_has(world, e, Scale), false);
    test_bool(set_at_active(world), false);

    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, SetAt, { .frame = 4, .entity = e });
    test_bool(set_at_active(world), true);
    test_bool(set_at_observes(world, ecs_id(SetAtValue)), true);
    ecs_set(world, second, Position, {0, 0, 0});
    ecs_set(world, second, SetAtValue, {0});
    ecs_set(world, second, Rotation, {0, 0.5f, 0});
    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, e, Position)->x, 1);
    ecs_progress(world, 0.1f);
    test_bool(ecs_is_alive(world, second), false);
    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, 0);
    test_flt(ecs_get(world, e, Rotation)->y, 0.5f);
    test_int(ecs_get(world, e, SetAtValue)->value, 0);
    test_bool(set_at_active(world), false);

    ecs_entity_t removed = ecs_new(world);
    ecs_set(world, removed, SetAt, { .frame = 9, .entity = e });
    test_bool(set_at_active(world), true);
    ecs_remove(world, removed, SetAt);
    test_bool(set_at_active(world), false);

    ecs_fini(world);
}

void SetAt_deferred_first(void) {
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    ECS_COMPONENT(world, SetAtValue);
    ECS_COMPONENT(world, SetAtOther);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_progress(world, 0.1f);
    test_bool(set_at_active(world), false);

    ecs_entity_t s = ecs_new(world);
    ecs_readonly_begin(world, false);
    ecs_world_t *stage = ecs_get_stage(world, 0);
    ecs_set(stage, s, SetAt, { .frame = 1, .entity = e });
    ecs_set(stage, s, Rotation, {0, 0.5f, 0});
    ecs_set(stage, s, Position, {0, 0, 0});
    ecs_set(stage, s, Rgb, {40, 50, 60});
    ecs_set(stage, s, SetAtValue, {7});
    ecs_add(stage, s, SetAtOther);
    ecs_set(stage, s, PointLight, { .intensity = 3, .range = 20 });
    ecs_add(stage, s, SpotLight);
    test_bool(ecs_has(world, s, SetAt), false);
    ecs_readonly_end(world);
    test_bool(ecs_has(world, s, SetAtValue), true);
    test_bool(set_at_observes(world, ecs_id(SetAtValue)), true);

    ecs_progress(world, 0.1f);
    test_bool(ecs_is_alive(world, s), false);
    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, 0);
    test_flt(ecs_get(world, e, Rotation)->y, 0.5f);
    test_int(ecs_get(world, e, Rgb)->r, 40);
    test_int(ecs_get(world, e, SetAtValue)->value, 7);
    test_bool(ecs_has(world, e, SetAtOther), false);
    const PointLight *light = ecs_get(world, e, PointLight);
    test_assert(light != NULL);
    test_flt(light->intensity, 3);
    test_flt(light->range, 20);
    test_bool(ecs_has(world, e, SpotLight), false);
    test_bool(set_at_active(world), false);

    ecs_fini(world);
}

void SetAt_prefab_instance(void) {
    ecs_entity_t e;
    ecs_world_t *world = set_at_world(&e);
    ECS_COMPONENT(world, SetAtValue);
    ecs_progress(world, 0.1f);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, SetAt, { .frame = 1, .entity = e });
    ecs_set(world, prefab, Rgb, {40, 50, 60});
    test_bool(set_at_active(world), false);

    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(set_at_active(world), true);
    test_bool(ecs_has_id(world, inst, EcsDisabled), true);
    test_bool(ecs_owns(world, inst, Rgb), false);
    ecs_set(world, inst, Position, {4, 5, 6});
    ecs_set(world, inst, SetAtValue, {8});

    ecs_progress(world, 0.1f);
    test_bool(ecs_is_alive(world, inst), false);
    test_flt(ecs_get(world, e, Position)->x, 4);
    test_int(ecs_get(world, e, SetAtValue)->value, 8);
    test_bool(ecs_has(world, e, Rgb), false);
    test_bool(ecs_is_alive(world, prefab), true);
    test_bool(set_at_active(world), false);

    ecs_fini(world);
}
