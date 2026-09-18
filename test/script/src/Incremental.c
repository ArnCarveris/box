/**
 * @file Incremental.c
 * @brief Tests for the manual template protocol and the per frame budget.
 */

#include <box.h>
#include <bake_test.h>

void Incremental_templates_are_marked_manual(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n", NULL), 0);

    ecs_entity_t t = ecs_lookup(world, "Tree");
    test_assert(t != 0);
    test_assert(ecs_has_id(world, t, EcsScriptTemplateManual));

    ecs_fini(world);
}

void Incremental_instances_are_pending(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "for i in 0..10 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Tree\n"
        "  }\n"
        "}\n", NULL), 0);

    ecs_entity_t t = ecs_lookup(world, "Tree");
    test_int(ecs_count_id(world, ecs_pair(EcsScriptTemplatePending, t)), 10);
    test_int(ecs_count_id(world, ecs_id(Rotation)), 0);

    ecs_fini(world);
}

void Incremental_non_template_entities_appear_immediately(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "ground {\n"
        "  Position: {0, 0, 0}\n"
        "  Scale: {100, 1, 100}\n"
        "}\n"
        "for i in 0..10 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Tree\n"
        "  }\n"
        "}\n", NULL), 0);

    ecs_entity_t ground = ecs_lookup(world, "ground");
    test_assert(ground != 0);
    const Scale *s = ecs_get(world, ground, Scale);
    test_assert(s != NULL);
    test_int(s->x, 100);
    test_int(ecs_count_id(world, ecs_id(Rotation)), 0);

    ecs_fini(world);
}

void Incremental_count_budget_per_frame(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 4;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "for i in 0..10 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Tree\n"
        "  }\n"
        "}\n", NULL), 0);

    ecs_progress(world, 0);
    test_int(ecs_count_id(world, ecs_id(Rotation)), 4);
    ecs_progress(world, 0);
    test_int(ecs_count_id(world, ecs_id(Rotation)), 8);
    ecs_progress(world, 0);
    test_int(ecs_count_id(world, ecs_id(Rotation)), 10);

    ecs_fini(world);
}

void Incremental_time_budget_stops_a_frame(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 0;
    settings->budget_ms = 0.0001;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Cluster {\n"
        "  for i in 0..200 {\n"
        "    _ { Rotation: {i, 0, 0} }\n"
        "  }\n"
        "}\n"
        "for i in 0..40 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Cluster\n"
        "  }\n"
        "}\n", NULL), 0);

    ecs_entity_t t = ecs_lookup(world, "Cluster");
    ecs_progress(world, 0);
    const ScriptStats *stats = ecs_singleton_get(world, ScriptStats);
    test_assert(stats->instantiated >= 1);
    test_assert(stats->instantiated < 40);
    test_assert(ecs_count_id(world, ecs_pair(EcsScriptTemplatePending, t)) > 0);
    test_assert(stats->worst_frame_ms > 0);

    ecs_fini(world);
}

void Incremental_instance_is_complete_when_it_appears(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 1;
    settings->budget_ms = 0;
    settings->nearest_first = false;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  Rotation: {0, 2, 0}\n"
        "  Scale: {3, 3, 3}\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "  crown { Scale: {2, 2, 2} }\n"
        "}\n"
        "a {\n"
        "  Position: {0, 0, 0}\n"
        "  Tree\n"
        "}\n"
        "b {\n"
        "  Position: {1, 0, 0}\n"
        "  Tree\n"
        "}\n", NULL), 0);

    ecs_entity_t a = ecs_lookup(world, "a");
    ecs_progress(world, 0);
    test_assert(ecs_has(world, a, Rotation));
    test_assert(ecs_has(world, a, Scale));
    test_assert(ecs_lookup(world, "a.trunk") != 0);
    test_assert(ecs_lookup(world, "a.crown") != 0);
    test_assert(ecs_lookup(world, "b.trunk") == 0);

    ecs_fini(world);
}

void Incremental_every_instance_is_expanded_eventually(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 3;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "for i in 0..10 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Tree\n"
        "  }\n"
        "}\n", NULL), 0);

    ecs_entity_t t = ecs_lookup(world, "Tree");
    int32_t i;
    for (i = 0; i < 8; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_count_id(world, ecs_id(Rotation)), 10);
    test_int(ecs_count_id(world, ecs_pair(EcsScriptTemplatePending, t)), 0);

    ecs_fini(world);
}

void Incremental_disabled_expands_everything_at_once(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 2;
    settings->budget_ms = 0.0001;
    settings->enabled = false;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "for i in 0..100 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Tree\n"
        "  }\n"
        "}\n", NULL), 0);

    ecs_progress(world, 0);
    test_int(ecs_count_id(world, ecs_id(Rotation)), 100);
    const ScriptStats *stats = ecs_singleton_get(world, ScriptStats);
    test_int(stats->instantiated, 100);
    test_int(stats->pending, 0);

    ecs_fini(world);
}

void Incremental_disabled_expands_nested_templates(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->enabled = false;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Leaf {\n"
        "  tip { Rotation: {0, 1, 0} }\n"
        "}\n"
        "template Branch {\n"
        "  leaf { Leaf }\n"
        "}\n"
        "for i in 0..5 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Branch\n"
        "  }\n"
        "}\n", NULL), 0);

    ecs_progress(world, 0);
    test_int(ecs_count_id(world, ecs_id(Rotation)), 5);
    const ScriptStats *stats = ecs_singleton_get(world, ScriptStats);
    test_int(stats->pending, 0);

    ecs_fini(world);
}

void Incremental_stats_report_the_queue(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 6;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "for i in 0..10 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Tree\n"
        "  }\n"
        "}\n", NULL), 0);

    ecs_progress(world, 0);
    const ScriptStats *stats = ecs_singleton_get(world, ScriptStats);
    test_int(stats->instantiated, 6);
    test_int(stats->pending, 4);

    ecs_progress(world, 0);
    test_int(stats->instantiated, 4);
    test_int(stats->pending, 0);

    ecs_progress(world, 0);
    test_int(stats->instantiated, 0);
    test_int(stats->pending, 0);

    ecs_fini(world);
}

void Incremental_stats_report_the_worst_frame(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "for i in 0..10 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Tree\n"
        "  }\n"
        "}\n", NULL), 0);

    const ScriptStats *stats = ecs_singleton_get(world, ScriptStats);
    test_int(stats->worst_frame_ms, 0);
    ecs_progress(world, 0);
    double worst = stats->worst_frame_ms;
    test_assert(worst > 0);

    ecs_progress(world, 0);
    test_assert(stats->worst_frame_ms >= worst);

    ecs_fini(world);
}

void Incremental_template_declared_before_import_is_marked(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n", NULL), 0);

    ecs_entity_t t = ecs_lookup(world, "Tree");
    test_assert(!ecs_has_id(world, t, EcsScriptTemplateManual));

    ECS_IMPORT(world, BoxScript);
    test_assert(ecs_has_id(world, t, EcsScriptTemplateManual));

    test_int(ecs_script_run(world, "test2",
        "a {\n"
        "  box.transform.Position: {0, 0, 0}\n"
        "  Tree\n"
        "}\n", NULL), 0);
    test_assert(ecs_has_pair(world, ecs_lookup(world, "a"), EcsScriptTemplatePending, t));

    ecs_fini(world);
}
