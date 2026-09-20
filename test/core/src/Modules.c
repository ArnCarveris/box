/**
 * @file Modules.c
 * @brief Tests that the box module gives every world the same modules and ids.
 */

#include <box.h>
#include <bake_test.h>

static const char *box_module_names[] = {
    "types", "core", "job", "shader", "surface", "input", "transform", "camera",
    "view", "light", "material", "transition", "lod", "effects", "sky", "render",
    "geometry", "texture", "gltf", "movement", "collision", "picking", "spline",
    "particle", "controller", "ui", "monitor", "script", "checkpoint", "command"
};

void Modules_every_module_lives_under_box(void)
{
    // ore_engine/import_order_audit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t box = ecs_lookup(world, "box");
    test_assert(box != 0);
    test_assert(ecs_has_id(world, box, EcsModule));

    int32_t i, count = (int32_t)(sizeof(box_module_names) / sizeof(box_module_names[0]));
    for (i = 0; i < count; i ++) {
        char path[64];
        snprintf(path, sizeof(path), "box.%s", box_module_names[i]);
        ecs_entity_t module = ecs_lookup(world, path);
        test_assert(module != 0);
        test_assert(ecs_has_id(world, module, EcsModule));
    }

    ecs_fini(world);
}

void Modules_two_worlds_register_the_same_ids(void)
{
    // ore_engine/import_order_audit
    ecs_world_t *a = ecs_init();
    ECS_IMPORT(a, Box);
    ecs_entity_t app = ecs_id(App);
    ecs_entity_t sphere = ecs_id(Sphere);
    ecs_entity_t dynamic = DynamicTransform;
    ecs_entity_t hovered = Hovered;
    ecs_entity_t terminal = ecs_id(Terminal);
    ecs_entity_t button = ecs_lookup(a, "box.ui.widgets.Button");
    ecs_entity_t sweep = ecs_lookup(a, "box.texture.BoxSvgSweep");
    test_assert(app && sphere && dynamic && hovered && terminal && button && sweep);
    ecs_fini(a);

    ecs_world_t *b = ecs_init();
    ECS_IMPORT(b, Box);
    test_uint(ecs_id(App), app);
    test_uint(ecs_id(Sphere), sphere);
    test_uint(DynamicTransform, dynamic);
    test_uint(Hovered, hovered);
    test_uint(ecs_id(Terminal), terminal);
    test_uint(ecs_lookup(b, "box.ui.widgets.Button"), button);
    test_uint(ecs_lookup(b, "box.texture.BoxSvgSweep"), sweep);
    test_assert(ecs_has(b, ecs_id(App), EcsComponent));
    test_str(ecs_get_name(b, ecs_id(Sphere)), "Sphere");
    ecs_fini(b);
}

void Modules_the_module_imports_again_without_a_second_copy(void)
{
    // ore_engine/import_order_audit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_IMPORT(world, Box);
    ECS_IMPORT(world, BoxUi);
    ecs_entity_t sweep = ecs_lookup(world, "box.texture.BoxSvgSweep");
    test_assert(sweep != 0);
    test_int(ecs_count_id(world, ecs_id(JobSettings)), 1);
    ecs_fini(world);
}

void Modules_two_live_worlds_share_the_process(void)
{
    // ore_engine/import_order_audit
    ecs_world_t *a = ecs_init();
    ECS_IMPORT(a, Box);
    ecs_world_t *b = ecs_init();
    ECS_IMPORT(b, Box);
    test_assert(ecs_lookup(b, "box.command") != 0);
    ecs_progress(a, 0);
    ecs_progress(b, 0);
    ecs_fini(a);
    ecs_progress(b, 0);
    ecs_fini(b);
}
