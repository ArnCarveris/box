/**
 * @file ScriptHas.c
 * @brief Tests for a template prop bound to a has-expression, e?[Tag] and e?[Component], that is
 * updated when the tag or component is added or removed from C.
 */

#include <box.h>
#include <bake_test.h>

/** The value a Show instance was given for its hurt prop */
typedef struct {
    bool value; /**< The prop */
} Shown;

ECS_COMPONENT_DECLARE(Shown);
ECS_TAG_DECLARE(Hurt);

/** The template that copies its prop to Shown */
#define SCRIPT_HAS_TEMPLATE \
    "using box.transform\n" \
    "template Show {\n" \
    "  prop hurt: bool = false\n" \
    "  Shown: {value: hurt}\n" \
    "}\n"

/** Create a world with Box, Shown, the Hurt tag and player and cfg entities with a Position.
 *
 * @return The world
 */
static ecs_world_t* script_has_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_COMPONENT_DEFINE(world, Shown);
    ECS_TAG_DEFINE(world, Hurt);
    ecs_struct(world, {
        .entity = ecs_id(Shown),
        .members = {{ .name = "value", .type = ecs_id(ecs_bool_t) }}
    });
    ecs_entity_t player = ecs_entity(world, { .name = "player" });
    ecs_set(world, player, Position, {1, 2, 3});
    ecs_entity_t cfg = ecs_entity(world, { .name = "cfg" });
    ecs_set(world, cfg, Position, {1, 0, 0});

    return world;
}

/** Run a script and return the instance it declares.
 *
 * @param world The world
 * @param name The name of the script entity
 * @param code The code of the script
 * @return The inst entity
 */
static ecs_entity_t script_has_run(
    ecs_world_t *world,
    const char *name,
    const char *code)
{
    ecs_entity_t s = ecs_script(world, {
        .entity = ecs_entity(world, { .name = name }),
        .code = code
    });
    test_assert(s != 0);
    ecs_entity_t inst = ecs_lookup(world, "inst");
    test_assert(inst != 0);
    test_assert(ecs_has(world, inst, Shown));

    return inst;
}

/** The value of the hurt prop of the instance.
 *
 * @param world The world
 * @param inst The instance
 * @return The value
 */
static bool script_has_shown(
    ecs_world_t *world,
    ecs_entity_t inst)
{
    const Shown *shown = ecs_get(world, inst, Shown);
    test_assert(shown != NULL);

    return shown->value;
}

/** Add and remove Hurt on the player from C and check that the instance follows.
 *
 * @param code The code of the script
 */
static void script_has_tag_check(
    const char *code)
{
    ecs_world_t *world = script_has_world();
    ecs_entity_t player = ecs_lookup(world, "player");
    ecs_entity_t inst = script_has_run(world, "scene", code);
    test_bool(script_has_shown(world, inst), false);
    ecs_add(world, player, Hurt);
    ecs_progress(world, 0);
    test_bool(script_has_shown(world, inst), true);
    ecs_remove(world, player, Hurt);
    ecs_progress(world, 0);
    test_bool(script_has_shown(world, inst), false);
    ecs_fini(world);
}

/** Remove and add Position on the player from C and check that the instance follows.
 *
 * @param code The code of the script
 */
static void script_has_component_check(
    const char *code)
{
    ecs_world_t *world = script_has_world();
    ecs_entity_t player = ecs_lookup(world, "player");
    ecs_entity_t inst = script_has_run(world, "scene", code);
    test_bool(script_has_shown(world, inst), true);
    ecs_remove(world, player, Position);
    ecs_progress(world, 0);
    test_bool(script_has_shown(world, inst), false);
    ecs_set(world, player, Position, {4, 5, 6});
    ecs_progress(world, 0);
    test_bool(script_has_shown(world, inst), true);
    ecs_fini(world);
}

void ScriptHas_tag(void) {
    // engine_core/script_has
    script_has_tag_check(
        SCRIPT_HAS_TEMPLATE
        "inst { Show: {hurt: player?[Hurt]} }\n");
}

void ScriptHas_tag_entity_variable(void) {
    // engine_core/script_has
    script_has_tag_check(
        SCRIPT_HAS_TEMPLATE
        "const p = player\n"
        "inst { Show: {hurt: p?[Hurt]} }\n");
}

void ScriptHas_tag_next_to_dependency(void) {
    // engine_core/script_has
    script_has_tag_check(
        SCRIPT_HAS_TEMPLATE
        "inst { Show: {hurt: player?[Hurt] && player[Position].x > 0} }\n");
}

void ScriptHas_tag_declared_by_script(void) {
    // engine_core/script_has
    // flecs_script_observe_refs drops has-refs to entities the script owns
    test_quarantine("5 Nov 2026");
    script_has_tag_check(
        SCRIPT_HAS_TEMPLATE
        "player {}\n"
        "inst { Show: {hurt: player?[Hurt]} }\n");
}

void ScriptHas_tag_set_by_script(void) {
    // engine_core/script_has
    // flecs_script_observe_refs drops has-refs to entities the script owns
    test_quarantine("5 Nov 2026");
    script_has_tag_check(
        SCRIPT_HAS_TEMPLATE
        "player { Position: {1, 2, 3} }\n"
        "inst { Show: {hurt: player?[Hurt]} }\n");
}

void ScriptHas_tag_declared_by_script_next_to_dependency(void) {
    // engine_core/script_has
    // flecs_script_observe_refs drops has-refs to entities the script owns
    test_quarantine("5 Nov 2026");
    script_has_tag_check(
        SCRIPT_HAS_TEMPLATE
        "player {}\n"
        "inst { Show: {hurt: player?[Hurt] && player[Position].x > 0} }\n");
}

void ScriptHas_tag_declared_by_other_script(void) {
    // engine_core/script_has
    ecs_world_t *world = script_has_world();
    ecs_entity_t player = ecs_lookup(world, "player");
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "world" }),
        .code = "player {}\n"
    }) != 0);
    ecs_entity_t inst = script_has_run(world, "run",
        SCRIPT_HAS_TEMPLATE
        "player { Position: {1, 2, 3} }\n"
        "inst { Show: {hurt: player?[Hurt]} }\n");
    test_bool(script_has_shown(world, inst), false);
    ecs_add(world, player, Hurt);
    ecs_progress(world, 0);
    test_bool(script_has_shown(world, inst), true);
    ecs_remove(world, player, Hurt);
    ecs_progress(world, 0);
    test_bool(script_has_shown(world, inst), false);
    ecs_fini(world);
}

void ScriptHas_component(void) {
    // engine_core/script_has
    script_has_component_check(
        SCRIPT_HAS_TEMPLATE
        "inst { Show: {hurt: player?[Position]} }\n");
}

void ScriptHas_component_next_to_dependency(void) {
    // engine_core/script_has
    script_has_component_check(
        SCRIPT_HAS_TEMPLATE
        "inst { Show: {hurt: player?[Position] && cfg[Position].x > 0} }\n");
}

void ScriptHas_component_declared_by_script(void) {
    // engine_core/script_has
    // flecs_script_observe_refs drops has-refs to entities the script owns
    test_quarantine("5 Nov 2026");
    script_has_component_check(
        SCRIPT_HAS_TEMPLATE
        "player {}\n"
        "inst { Show: {hurt: player?[Position]} }\n");
}
