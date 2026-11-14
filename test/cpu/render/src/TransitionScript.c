/**
 * @file TransitionScript.c
 * @brief Tests of the TransitionScript suite: transitions of values a reactive script template
 * writes when one of its props changes, read from the resolver with the null backend.
 *
 * A template that scales an Emissive with a prop keeps the entity when the prop changes, so the
 * change animates instead of snapping, also when the Transition map comes from a prefab the
 * entity is an instance of, and when the same update switches the Transition spec.
 *
 * The cases step the world with a fixed delta of a quarter second.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "transition_test.h"

/** Difference a resolved float may have from the expected one */
#define TS_EPS (1e-3f)

/** Load a managed script, which is reactive, like a scene.
 *
 * @param world The world
 * @param code The script
 */
static void ts_script(
    ecs_world_t *world,
    const char *code)
{
    ecs_entity_t s = ecs_script(world, { .code = code });
    test_assert(s != 0);
    const EcsScript *loaded = ecs_get(world, s, EcsScript);
    test_assert(loaded != NULL);
    test_assert(loaded->script != NULL);
    test_assert(!loaded->error);
}

/** Set a prop of a template instance, as a parent template does when it passes a new value.
 *
 * @param world The world
 * @param instance The template instance
 * @param template_name The name of the template, its component
 * @param prop The prop
 * @param value The new value
 */
static void ts_set_prop(
    ecs_world_t *world,
    ecs_entity_t instance,
    const char *template_name,
    const char *prop,
    double value)
{
    ecs_entity_t t = ecs_lookup(world, template_name);
    test_assert(t != 0);
    const ecs_type_info_t *ti = ecs_get_type_info(world, t);
    test_assert(ti != NULL);
    void *v = ecs_os_calloc(ti->size);
    const void *cur = ecs_get_id(world, instance, t);
    test_assert(cur != NULL);
    ecs_os_memcpy(v, cur, ti->size);
    ecs_meta_cursor_t c = ecs_meta_cursor(world, t, v);
    test_int(ecs_meta_push(&c), 0);
    test_int(ecs_meta_member(&c, prop), 0);
    test_int(ecs_meta_set_float(&c, value), 0);
    test_int(ecs_meta_pop(&c), 0);
    ecs_set_id(world, instance, t, (size_t)ti->size, v);
    ecs_os_free(v);
}

/** Read the Emissive strength the resolver computed for an entity in the last packet.
 *
 * @param world The world
 * @param e The entity
 * @return The rendered strength
 */
static float ts_strength(
    ecs_world_t *world,
    ecs_entity_t e)
{
    Emissive result = {0};
    box_render_sync(world);
    test_assert(box_render_transition_test_value(world, e, ecs_id(Emissive), &result));

    return result.strength;
}

void TransitionScript_template_prop_animates_owned_emissive(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    int32_t i;
    ts_script(world,
        "using box.geometry\n"
        "using box.material\n"
        "using box.transition\n"
        "prefab Window { Rgb: {64, 60, 54}; Transition: [Emissive: {duration: 1}] }\n"
        "template Panel {\n"
        "  prop power: f32 = 1\n"
        "  lamp : Window { Box: {1, 1, 1}; Emissive: {strength: 4 * power, color: {255, 200, 100}} }\n"
        "}\n"
        "Panel panel(power: 1)\n");
    tr_frame(world);
    tr_frame(world);

    ecs_entity_t panel = ecs_lookup(world, "panel");
    test_assert(panel != 0);
    ecs_entity_t lamp = ecs_lookup(world, "panel.lamp");
    test_assert(lamp != 0);
    test_assert(ecs_owns(world, lamp, Emissive));
    test_assert(ecs_has(world, lamp, Transition));
    test_assert(box_render_transition_test_node(world, lamp) != 0);
    test_flt(ecs_get(world, lamp, Emissive)->strength, 4);
    test_assert(fabsf(ts_strength(world, lamp) - 4) < TS_EPS);

    ts_set_prop(world, panel, "Panel", "power", 0.5);
    tr_frame(world);
    tr_frame(world);
    tr_frame(world);

    /* The template update rewrote the value of the same entity */
    test_assert(ecs_lookup(world, "panel.lamp") == lamp);
    test_assert(ecs_is_alive(world, lamp));
    test_flt(ecs_get(world, lamp, Emissive)->strength, 2);
    float mid = ts_strength(world, lamp);
    test_assert(mid < 4 - TS_EPS);
    test_assert(mid > 2 + TS_EPS);

    for (i = 0; i < 6; i ++) {
        tr_frame(world);
    }
    test_assert(fabsf(ts_strength(world, lamp) - 2) < TS_EPS);

    tr_fini(world, null);
}

void TransitionScript_template_spec_switch_with_value(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    int32_t i;
    ts_script(world,
        "using box.geometry\n"
        "using box.material\n"
        "using box.transition\n"
        "template Nozzle {\n"
        "  prop power: f32 = 1\n"
        "  shell {\n"
        "    Box: {1, 1, 1}\n"
        "    Rgb: {200, 200, 200}\n"
        "    Emissive: {strength: 4 * power, color: {255, 255, 255}}\n"
        "    if power > 0 {\n"
        "      Transition: [Emissive: {duration: 1}]\n"
        "    } else {\n"
        "      Transition: [Emissive: {duration: 4}]\n"
        "    }\n"
        "  }\n"
        "}\n"
        "Nozzle nozzle(power: 1)\n");
    tr_frame(world);
    tr_frame(world);

    ecs_entity_t nozzle = ecs_lookup(world, "nozzle");
    ecs_entity_t shell = ecs_lookup(world, "nozzle.shell");
    test_assert(nozzle != 0);
    test_assert(shell != 0);
    test_assert(fabsf(ts_strength(world, shell) - 4) < TS_EPS);

    /* Cutting the power switches to the four second spec in the same update as the value, so
     * after more than one second the shell is still well above its target */
    ts_set_prop(world, nozzle, "Nozzle", "power", 0);
    for (i = 0; i < 7; i ++) {
        tr_frame(world);
    }
    test_assert(ecs_lookup(world, "nozzle.shell") == shell);
    test_flt(ecs_get(world, shell, Emissive)->strength, 0);
    test_assert(ts_strength(world, shell) > 1);

    for (i = 0; i < 14; i ++) {
        tr_frame(world);
    }
    test_assert(fabsf(ts_strength(world, shell)) < TS_EPS);

    /* Powering up again uses the one second spec */
    ts_set_prop(world, nozzle, "Nozzle", "power", 1);
    for (i = 0; i < 7; i ++) {
        tr_frame(world);
    }
    test_assert(fabsf(ts_strength(world, shell) - 4) < TS_EPS);

    tr_fini(world, null);
}
