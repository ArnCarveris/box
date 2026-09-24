/**
 * @file Template.c
 * @brief Reproducers for flecs script template bugs the editor overlay hit.
 */

#include <box.h>
#include <bake_test.h>

/** Input of the reproducer scripts, set from C to rerun them */
typedef struct {
    bool flag; /**< Picks which template an entity is created with */
} Config;

ECS_COMPONENT_DECLARE(Config);

void Template_switching_templates_keeps_a_child_of_the_same_name(void)
{
    // flecs/template_switch
    ecs_world_t *world = ecs_init();
    ECS_COMPONENT_DEFINE(world, Config);
    ecs_struct(world, {
        .entity = ecs_id(Config),
        .members = {{ .name = "flag", .type = ecs_id(ecs_bool_t) }}
    });

    ecs_singleton_set(world, Config, { .flag = false });

    test_assert(0 != ecs_script(world, { .code =
        "Ta {}\n"
        "Tb {}\n"
        "template A {\n"
        "  child { Ta }\n"
        "}\n"
        "template B {\n"
        "  child { Tb }\n"
        "}\n"
        "const cfg = Config[Config]\n"
        "for i in 0..2 {\n"
        "  if cfg.flag {\n"
        "    A \"x_{i}\"()\n"
        "  } else {\n"
        "    B \"x_{i}\"()\n"
        "  }\n"
        "}\n" }));

    ecs_entity_t ta = ecs_lookup(world, "Ta");
    ecs_entity_t tb = ecs_lookup(world, "Tb");
    ecs_entity_t a = ecs_lookup(world, "A");
    ecs_entity_t b = ecs_lookup(world, "B");
    ecs_entity_t x = ecs_lookup(world, "x_0");
    test_assert(x != 0);
    test_assert(ecs_has_id(world, x, b));
    ecs_entity_t child = ecs_lookup(world, "x_0.child");
    test_assert(child != 0);
    test_assert(ecs_has_id(world, child, tb));

    ecs_singleton_set(world, Config, { .flag = true });

    x = ecs_lookup(world, "x_0");
    test_assert(x != 0);
    test_assert(ecs_has_id(world, x, a));
    test_assert(!ecs_has_id(world, x, b));
    child = ecs_lookup(world, "x_0.child");
    test_assert(child != 0);
    test_assert(ecs_has_id(world, child, ta));
    test_assert(!ecs_has_id(world, child, tb));

    ecs_fini(world);
}

void Template_switching_templates_without_a_loop_keeps_the_child(void)
{
    // flecs/template_switch
    ecs_world_t *world = ecs_init();
    ECS_COMPONENT_DEFINE(world, Config);
    ecs_struct(world, {
        .entity = ecs_id(Config),
        .members = {{ .name = "flag", .type = ecs_id(ecs_bool_t) }}
    });

    ecs_singleton_set(world, Config, { .flag = false });

    test_assert(0 != ecs_script(world, { .code =
        "Ta {}\n"
        "Tb {}\n"
        "template A {\n"
        "  child { Ta }\n"
        "}\n"
        "template B {\n"
        "  child { Tb }\n"
        "}\n"
        "const cfg = Config[Config]\n"
        "if cfg.flag {\n"
        "  A x()\n"
        "} else {\n"
        "  B x()\n"
        "}\n" }));

    ecs_entity_t ta = ecs_lookup(world, "Ta");
    ecs_entity_t tb = ecs_lookup(world, "Tb");
    ecs_entity_t a = ecs_lookup(world, "A");
    ecs_entity_t b = ecs_lookup(world, "B");
    ecs_entity_t x = ecs_lookup(world, "x");
    test_assert(x != 0);
    test_assert(ecs_has_id(world, x, b));
    ecs_entity_t child = ecs_lookup(world, "x.child");
    test_assert(child != 0);
    test_assert(ecs_has_id(world, child, tb));

    ecs_singleton_set(world, Config, { .flag = true });

    x = ecs_lookup(world, "x");
    test_assert(x != 0);
    test_assert(ecs_has_id(world, x, a));
    test_assert(!ecs_has_id(world, x, b));
    child = ecs_lookup(world, "x.child");
    test_assert(child != 0);
    test_assert(ecs_has_id(world, child, ta));
    test_assert(!ecs_has_id(world, child, tb));

    ecs_fini(world);
}

void Template_adding_a_tag_the_template_tests_for_reinstantiates(void)
{
    // flecs/template_has_ref
    ecs_world_t *world = ecs_init();

    test_assert(0 != ecs_script(world, { .code =
        "Hovered {}\n"
        "Hot {}\n"
        "Cold {}\n"
        "template A {\n"
        "  if this?[Hovered] {\n"
        "    Hot\n"
        "  } else {\n"
        "    Cold\n"
        "  }\n"
        "}\n"
        "A x()\n" }));

    ecs_entity_t hovered = ecs_lookup(world, "Hovered");
    ecs_entity_t hot = ecs_lookup(world, "Hot");
    ecs_entity_t cold = ecs_lookup(world, "Cold");
    ecs_entity_t x = ecs_lookup(world, "x");
    test_assert(x != 0);
    test_assert(ecs_has_id(world, x, cold));
    test_assert(!ecs_has_id(world, x, hot));
    ecs_add_id(world, x, hovered);
    test_assert(ecs_has_id(world, x, hot));
    test_assert(!ecs_has_id(world, x, cold));
    ecs_remove_id(world, x, hovered);
    test_assert(ecs_has_id(world, x, cold));
    test_assert(!ecs_has_id(world, x, hot));

    ecs_fini(world);
}

void Template_switching_templates_keeps_a_tag_both_templates_add(void)
{
    // flecs/template_switch
    ecs_world_t *world = ecs_init();
    ECS_COMPONENT_DEFINE(world, Config);
    ecs_struct(world, {
        .entity = ecs_id(Config),
        .members = {{ .name = "flag", .type = ecs_id(ecs_bool_t) }}
    });

    ecs_singleton_set(world, Config, { .flag = false });

    test_assert(0 != ecs_script(world, { .code =
        "Shared {}\n"
        "Ta {}\n"
        "Tb {}\n"
        "template A {\n"
        "  Shared\n"
        "  Ta\n"
        "}\n"
        "template B {\n"
        "  Shared\n"
        "  Tb\n"
        "}\n"
        "const cfg = Config[Config]\n"
        "if cfg.flag {\n"
        "  A x()\n"
        "} else {\n"
        "  B x()\n"
        "}\n" }));

    ecs_entity_t shared = ecs_lookup(world, "Shared");
    ecs_entity_t ta = ecs_lookup(world, "Ta");
    ecs_entity_t tb = ecs_lookup(world, "Tb");
    ecs_entity_t a = ecs_lookup(world, "A");
    ecs_entity_t b = ecs_lookup(world, "B");
    ecs_entity_t x = ecs_lookup(world, "x");
    test_assert(x != 0);
    test_assert(ecs_has_id(world, x, b));
    test_assert(ecs_has_id(world, x, shared));
    test_assert(ecs_has_id(world, x, tb));

    ecs_singleton_set(world, Config, { .flag = true });
    x = ecs_lookup(world, "x");
    test_assert(x != 0);
    test_assert(ecs_has_id(world, x, a));
    test_assert(!ecs_has_id(world, x, b));
    test_assert(ecs_has_id(world, x, ta));
    test_assert(!ecs_has_id(world, x, tb));
    test_assert(ecs_has_id(world, x, shared));

    ecs_fini(world);
}
