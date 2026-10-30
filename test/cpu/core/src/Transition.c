/**
 * @file Transition.c
 * @brief Tests for the Transition component, its timing functions and its validation.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>

/** Error messages logged since the collector was installed */
static ecs_strbuf_t transition_errors = ECS_STRBUF_INIT;

/** Log function the collector passes messages on to */
static ecs_os_api_log_t transition_log = NULL;

/** Collect logged errors without printing them.
 *
 * @param level Level of the message
 * @param file File that logged it
 * @param line Line that logged it
 * @param msg The message
 */
static void transition_collect_errors(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level == -3) {
        ecs_strbuf_appendstr(&transition_errors, msg);
        ecs_strbuf_appendch(&transition_errors, '\n');
    }
}

/** Start collecting errors */
static void transition_capture_begin(void) {
    ecs_strbuf_reset(&transition_errors);
    transition_log = ecs_os_api.log_;
    ecs_os_api.log_ = transition_collect_errors;
}

/** Stop collecting errors.
 *
 * @return The collected errors, to free with ecs_os_free, NULL when none
 */
static char* transition_capture_end(void) {
    ecs_os_api.log_ = transition_log;

    return ecs_strbuf_get(&transition_errors);
}

/** Allocations not freed yet, counted by the flecs os api.
 *
 * @return Allocations minus frees
 */
static int64_t transition_live_allocs(void) {
    return ecs_os_api_malloc_count + ecs_os_api_calloc_count - ecs_os_api_free_count;
}

/** Whether two floats are within a tolerance.
 *
 * @param a First value
 * @param b Second value
 * @param tolerance Largest accepted difference
 * @return True when close
 */
static bool transition_near(
    float a,
    float b,
    float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

/** The script of docs/transitions.md */
static const char *transition_design_script =
    "using box.transform\n"
    "using box.material\n"
    "using box.transition\n"
    "\n"
    "e {\n"
    "  Position: {0, 1, 0}\n"
    "  Rgb: {255, 0, 0}\n"
    "  Transition: [\n"
    "    Position: {duration: 0.5, timing_function: EaseInOut},\n"
    "    Rgb: {duration: 1.0, delay: 0.25, timing_function: Linear}\n"
    "  ]\n"
    "}\n";

void Transition_design_script(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "design", transition_design_script, NULL) == 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    test_assert(e != 0);
    test_assert(ecs_has(world, e, Transition));

    const Transition *map = ecs_get(world, e, Transition);
    test_assert(map != NULL);
    test_int(ecs_map_count(map), 2);

    const TransitionSpec *p = ecs_map_get_deref(map, TransitionSpec, ecs_id(Position));
    test_assert(p != NULL);
    test_flt(p->duration, 0.5f);
    test_flt(p->delay, 0.0f);
    test_int(p->timing_function, EaseInOut);

    const TransitionSpec *c = box_transition_get(world, e, ecs_id(Rgb));
    test_assert(c != NULL);
    test_flt(c->duration, 1.0f);
    test_flt(c->delay, 0.25f);
    test_int(c->timing_function, Linear);

    test_assert(box_transition_get(world, e, ecs_id(Scale)) == NULL);

    test_flt(ecs_get(world, e, Position)->y, 1.0f);
    test_int(ecs_get(world, e, Rgb)->r, 255);

    ecs_fini(world);
}

void Transition_omitted_fields(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "omitted",
        "using box.transform\n"
        "using box.transition\n"
        "e {\n"
        "  Transition: [Scale: {}, Rotation: {delay: 2}]\n"
        "}\n", NULL) == 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    test_assert(e != 0);

    const TransitionSpec *s = box_transition_get(world, e, ecs_id(Scale));
    test_assert(s != NULL);
    test_flt(s->duration, 0.0f);
    test_flt(s->delay, 0.0f);
    test_int(s->timing_function, Linear);

    const TransitionSpec *r = box_transition_get(world, e, ecs_id(Rotation));
    test_assert(r != NULL);
    test_flt(r->duration, 0.0f);
    test_flt(r->delay, 2.0f);
    test_int(r->timing_function, Linear);

    ecs_fini(world);
}

void Transition_set_from_c(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    box_transition_set(world, e, ecs_id(Position),
        &(TransitionSpec){ .duration = 0.5f, .timing_function = EaseOut });
    box_transition_set(world, e, ecs_id(Opacity),
        &(TransitionSpec){ .duration = 2.0f, .delay = 1.0f });
    test_assert(ecs_owns(world, e, Transition));
    test_int(ecs_map_count(ecs_get(world, e, Transition)), 2);

    const TransitionSpec *p = box_transition_get(world, e, ecs_id(Position));
    test_assert(p != NULL);
    test_flt(p->duration, 0.5f);
    test_int(p->timing_function, EaseOut);

    box_transition_set(world, e, ecs_id(Position),
        &(TransitionSpec){ .duration = 3.0f, .timing_function = EaseIn });
    test_int(ecs_map_count(ecs_get(world, e, Transition)), 2);
    p = box_transition_get(world, e, ecs_id(Position));
    test_flt(p->duration, 3.0f);
    test_int(p->timing_function, EaseIn);
    test_flt(box_transition_get(world, e, ecs_id(Opacity))->delay, 1.0f);

    ecs_fini(world);
}

void Transition_prefab_inherited(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_has_pair(world, ecs_id(Transition), EcsOnInstantiate, EcsInherit));
    test_assert(ecs_script_run(world, "prefab",
        "using box.transform\n"
        "using box.material\n"
        "using box.transition\n"
        "prefab Lamp {\n"
        "  Transition: [Position: {duration: 1}]\n"
        "}\n"
        "a : Lamp {}\n"
        "b : Lamp {\n"
        "  Transition: [Rgb: {duration: 2}]\n"
        "}\n", NULL) == 0);

    ecs_entity_t lamp = ecs_lookup(world, "Lamp");
    ecs_entity_t a = ecs_lookup(world, "a");
    ecs_entity_t b = ecs_lookup(world, "b");
    test_assert(lamp != 0 && a != 0 && b != 0);

    test_assert(ecs_has(world, a, Transition));
    test_assert(!ecs_owns(world, a, Transition));
    test_assert(ecs_get(world, a, Transition) == ecs_get(world, lamp, Transition));
    test_flt(box_transition_get(world, a, ecs_id(Position))->duration, 1.0f);

    test_assert(ecs_owns(world, b, Transition));
    test_int(ecs_map_count(ecs_get(world, b, Transition)), 1);
    test_assert(box_transition_get(world, b, ecs_id(Position)) == NULL);
    test_flt(box_transition_get(world, b, ecs_id(Rgb))->duration, 2.0f);

    test_int(ecs_map_count(ecs_get(world, lamp, Transition)), 1);
    test_assert(box_transition_get(world, lamp, ecs_id(Rgb)) == NULL);

    ecs_fini(world);
}

void Transition_instance_set_copies(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    box_transition_set(world, prefab, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    test_assert(!ecs_owns(world, inst, Transition));

    box_transition_set(world, inst, ecs_id(Rgb), &(TransitionSpec){ .duration = 2.0f });
    test_assert(ecs_owns(world, inst, Transition));
    test_int(ecs_map_count(ecs_get(world, inst, Transition)), 2);
    test_flt(box_transition_get(world, inst, ecs_id(Position))->duration, 1.0f);
    test_flt(box_transition_get(world, inst, ecs_id(Rgb))->duration, 2.0f);

    test_assert(box_transition_get(world, inst, ecs_id(Position)) !=
        box_transition_get(world, prefab, ecs_id(Position)));
    test_int(ecs_map_count(ecs_get(world, prefab, Transition)), 1);

    ecs_fini(world);
}

/** Create, copy, replace and delete entities with Transition.
 *
 * @param world The world
 * @param prefab Prefab with a Transition
 */
static void transition_churn(
    ecs_world_t *world,
    ecs_entity_t prefab)
{
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    box_transition_set(world, inst, ecs_id(Scale), &(TransitionSpec){ .duration = 3.0f });

    ecs_entity_t copy = ecs_clone(world, 0, inst, true);
    test_int(ecs_map_count(ecs_get(world, copy, Transition)), 3);
    test_assert(box_transition_get(world, copy, ecs_id(Scale)) !=
        box_transition_get(world, inst, ecs_id(Scale)));

    ecs_set_id(world, copy, ecs_id(Transition), sizeof(Transition),
        ecs_get(world, prefab, Transition));
    test_int(ecs_map_count(ecs_get(world, copy, Transition)), 2);
    test_assert(box_transition_get(world, copy, ecs_id(Scale)) == NULL);

    ecs_entity_t overriding = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add(world, overriding, Transition);
    test_assert(ecs_owns(world, overriding, Transition));
    test_int(ecs_map_count(ecs_get(world, overriding, Transition)), 2);
    test_assert(box_transition_get(world, overriding, ecs_id(Rgb)) !=
        box_transition_get(world, prefab, ecs_id(Rgb)));
    ecs_delete(world, overriding);

    ecs_entity_t plain = ecs_new(world);
    box_transition_set(world, plain, ecs_id(Tint), &(TransitionSpec){ .duration = 1.0f });
    ecs_remove(world, plain, Transition);

    ecs_delete(world, inst);
    ecs_delete(world, copy);
    ecs_delete(world, plain);
}

void Transition_no_leak(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    box_transition_set(world, prefab, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    box_transition_set(world, prefab, ecs_id(Rgb), &(TransitionSpec){ .duration = 2.0f });

    transition_churn(world, prefab);
    int64_t before = transition_live_allocs();
    int32_t i;
    for (i = 0; i < 4; i ++) {
        transition_churn(world, prefab);
    }

    test_int(transition_live_allocs(), before);

    ecs_delete(world, prefab);
    ecs_fini(world);
}

void Transition_not_transitionable(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    transition_capture_begin();
    int result = ecs_script_run(world, "textures",
        "using box.transform\n"
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Transition: [PbrTextures: {duration: 1}, Position: {duration: 2}]\n"
        "}\n", NULL);
    char *errors = transition_capture_end();
    test_int(result, 0);
    test_assert(errors != NULL);
    test_assert(strstr(errors, "PbrTextures") != NULL);
    test_assert(strstr(errors, "not transitionable") != NULL);
    test_assert(strstr(errors, "Position") == NULL);
    ecs_os_free(errors);

    ecs_entity_t e = ecs_lookup(world, "e");
    test_assert(e != 0);
    test_int(ecs_map_count(ecs_get(world, e, Transition)), 2);
    const TransitionSpec *t = box_transition_get(world, e, ecs_id(PbrTextures));
    test_assert(t != NULL);
    test_assert(!box_transition_valid(world, ecs_id(PbrTextures), t));
    test_assert(box_transition_valid(world, ecs_id(Position),
        box_transition_get(world, e, ecs_id(Position))));

    transition_capture_begin();
    box_transition_set(world, e, ecs_id(Scale), &(TransitionSpec){ .duration = 1.0f });
    errors = transition_capture_end();
    test_assert(errors != NULL);
    test_assert(strstr(errors, "PbrTextures") != NULL);
    test_assert(strstr(errors, "Scale") == NULL);
    ecs_os_free(errors);
    test_int(ecs_map_count(ecs_get(world, e, Transition)), 3);
    ecs_progress(world, 0.1f);
    ecs_delete(world, e);

    ecs_fini(world);
}

void Transition_negative_duration(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    transition_capture_begin();
    int result = ecs_script_run(world, "negative",
        "using box.transform\n"
        "using box.transition\n"
        "e {\n"
        "  Transition: [Position: {duration: -1}]\n"
        "}\n", NULL);
    char *errors = transition_capture_end();
    test_int(result, 0);
    test_assert(errors != NULL);
    test_assert(strstr(errors, "Position") != NULL);
    test_assert(strstr(errors, "negative") != NULL);
    ecs_os_free(errors);

    ecs_entity_t e = ecs_lookup(world, "e");
    const TransitionSpec *t = box_transition_get(world, e, ecs_id(Position));
    test_assert(t != NULL);
    test_flt(t->duration, -1.0f);
    test_assert(!box_transition_valid(world, ecs_id(Position), t));

    transition_capture_begin();
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    errors = transition_capture_end();
    test_assert(errors == NULL);
    test_assert(box_transition_valid(world, ecs_id(Position),
        box_transition_get(world, e, ecs_id(Position))));

    ecs_fini(world);
}

void Transition_negative_delay(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    transition_capture_begin();
    box_transition_set(world, e, ecs_id(Rgb), &(TransitionSpec){ .duration = 1, .delay = -0.5f });
    char *errors = transition_capture_end();
    test_assert(errors != NULL);
    test_assert(strstr(errors, "Rgb") != NULL);
    test_assert(strstr(errors, "negative") != NULL);
    ecs_os_free(errors);
    test_assert(ecs_has(world, e, Transition));

    ecs_fini(world);
}

void Transition_supported(void) {
    // engine_core/transition
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(box_transition_supported(world, ecs_id(Position)));
    test_assert(box_transition_supported(world, ecs_id(Rotation)));
    test_assert(box_transition_supported(world, ecs_id(Scale)));
    test_assert(box_transition_supported(world, ecs_id(Rgb)));
    test_assert(box_transition_supported(world, ecs_id(Opacity)));
    test_assert(box_transition_supported(world, ecs_id(AlphaCutoff)));
    test_assert(box_transition_supported(world, ecs_id(Tint)));
    test_assert(box_transition_supported(world, ecs_id(PbrMaterial)));
    test_assert(box_transition_supported(world, ecs_id(Emissive)));
    test_assert(box_transition_supported(world, ecs_id(TextureTransform)));

    test_assert(!box_transition_supported(world, ecs_id(PbrTextures)));
    test_assert(!box_transition_supported(world, ecs_id(WorldTransform)));
    test_assert(!box_transition_supported(world, ecs_id(MaterialId)));
    test_assert(!box_transition_supported(world, ecs_id(BlendMode)));
    test_assert(!box_transition_supported(world, DynamicTransform));
    test_assert(!box_transition_supported(world, ecs_id(Transition)));
    test_assert(!box_transition_supported(world, 0));

    ecs_fini(world);
}

void Transition_ease_end_points(void) {
    // engine_core/transition
    TimingFunction fns[] = { Linear, EaseIn, EaseOut, EaseInOut };
    int32_t i;
    for (i = 0; i < 4; i ++) {
        test_assert(box_transition_ease(fns[i], 0.0f) == 0.0f);
        test_assert(box_transition_ease(fns[i], 1.0f) == 1.0f);
        test_assert(box_transition_ease(fns[i], -0.5f) == 0.0f);
        test_assert(box_transition_ease(fns[i], 1.5f) == 1.0f);
        test_assert(box_transition_ease(fns[i], 1e-6f) < 0.01f);
        test_assert(box_transition_ease(fns[i], 1.0f - 1e-6f) > 0.99f);
    }
}

void Transition_ease_monotonic(void) {
    // engine_core/transition
    TimingFunction fns[] = { Linear, EaseIn, EaseOut, EaseInOut };
    int32_t i, s;
    for (i = 0; i < 4; i ++) {
        float prev = 0.0f;
        for (s = 1; s <= 1000; s ++) {
            float v = box_transition_ease(fns[i], (float)s / 1000.0f);
            test_assert(v >= prev - 1e-6f);
            test_assert(v >= 0.0f && v <= 1.0f);
            prev = v;
        }

        test_flt(prev, 1.0f);
    }
}

void Transition_ease_midpoints(void) {
    // engine_core/transition
    float tol = 1e-3f;
    test_assert(transition_near(box_transition_ease(Linear, 0.25f), 0.25f, 1e-6f));
    test_assert(transition_near(box_transition_ease(Linear, 0.5f), 0.5f, 1e-6f));

    test_assert(transition_near(box_transition_ease(EaseIn, 0.25f), 0.0935f, tol));
    test_assert(transition_near(box_transition_ease(EaseIn, 0.5f), 0.3154f, tol));
    test_assert(transition_near(box_transition_ease(EaseIn, 0.75f), 0.6219f, tol));

    test_assert(transition_near(box_transition_ease(EaseOut, 0.25f), 0.3781f, tol));
    test_assert(transition_near(box_transition_ease(EaseOut, 0.5f), 0.6846f, tol));
    test_assert(transition_near(box_transition_ease(EaseOut, 0.75f), 0.9065f, tol));

    test_assert(transition_near(box_transition_ease(EaseInOut, 0.25f), 0.1292f, tol));
    test_assert(transition_near(box_transition_ease(EaseInOut, 0.5f), 0.5f, tol));
    test_assert(transition_near(box_transition_ease(EaseInOut, 0.75f), 0.8708f, tol));

    int32_t s;
    for (s = 0; s <= 20; s ++) {
        float t = (float)s / 20.0f;
        test_assert(transition_near(box_transition_ease(EaseIn, t),
            1.0f - box_transition_ease(EaseOut, 1.0f - t), tol));
    }
}
