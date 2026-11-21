/**
 * @file Ablate.c
 * @brief Tests for the ablate command.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

static const char *ablate_scene =
    "using box.camera\n"
    "using box.view\n"
    "using box.effects\n"
    "camera { Camera: {fov: 50} }\n"
    "view {\n"
    "  bloom { Bloom: {intensity: 0.2} }\n"
    "  View: {camera: camera, shadows: {enabled: true}, effects: [{effect: bloom}]}\n"
    "}\n"
    "$ { Scene: {reflections: true,\n"
    "  shader_features: {Extended: {alpha_cutout: true, wind: true}}} }\n";

void Ablate_report(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "scene", ablate_scene, NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "ablate frames=3 settle=2 rounds=2", &error);
    test_bool(error, false);
    test_assert(!strncmp(out, "ablate: started on view, 8 variants in 2 rounds", 47));
    ecs_os_free(out);

    test_flt(ecs_singleton_get(world, App)->fps, 100000);
    test_bool(ecs_singleton_get(world, App)->unpaced, true);

    int32_t i;
    for (i = 0; i < 2000; i ++) {
        ecs_progress(world, 0);
        out = box_command_run(world, s, "ablate", &error);
        test_bool(error, false);
        if (strncmp(out, "ablate: step", 12)) {
            break;
        }

        ecs_os_free(out);
    }

    test_assert(i < 2000);
    test_assert(strstr(out, "ablate view (camera camera), 2 rounds of 3 frames") != NULL);
    test_assert(strstr(out, "\nLighting ") != NULL);
    test_assert(strstr(out, "  lighting off ") != NULL);
    test_assert(strstr(out, "    shadows off ") != NULL);
    test_assert(strstr(out, "    depth_prepass off ") != NULL);
    test_assert(strstr(out, "    reflections off ") != NULL);
    test_assert(strstr(out, "      Scene.reflections off ") != NULL);
    test_assert(strstr(out, "\nEffects ") != NULL);
    test_assert(strstr(out, "  bloom off ") != NULL);
    test_assert(strstr(out, "(Bloom)") != NULL);
    test_assert(strstr(out, "\nScene features ") != NULL);
    test_assert(strstr(out, "  Extended.alpha_cutout off ") != NULL);
    test_assert(strstr(out, "  Extended.wind off ") != NULL);
    test_assert(strstr(out, "Extended.water") == NULL);
    test_assert(strstr(out, "atmosphere off") == NULL);
    test_assert(strstr(out, "fog_volumes off") == NULL);
    test_assert(strstr(out, "baked off") == NULL);
    test_assert(strstr(out, "Resolution") == NULL);
    test_assert(strstr(out, "not timed") == NULL);
    test_assert(strstr(out, "\nLargest costs\n") != NULL);
    ecs_os_free(out);

    ecs_entity_t view = ecs_lookup(world, "view");
    const View *v = ecs_get(world, view, View);
    test_bool(v->lighting.enabled, true);
    test_bool(v->lighting.depth_prepass, true);
    test_bool(v->lighting.reflections, true);
    test_bool(v->shadows.enabled, true);
    test_int(ecs_vec_count(&v->effects), 1);
    test_bool(ecs_vec_first_t(&v->effects, ViewEffect)->enabled, true);

    const Scene *scene = ecs_singleton_get(world, Scene);
    test_bool(scene->shader_features.Extended.alpha_cutout, true);
    test_bool(scene->shader_features.Extended.wind, true);
    test_bool(scene->reflections, true);

    test_flt(ecs_singleton_get(world, App)->fps, 0);
    test_bool(ecs_singleton_get(world, App)->unpaced, false);

    ecs_fini(world);
}

void Ablate_skips_lighting_when_unlit(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "scene", ablate_scene, NULL) == 0);
    ecs_entity_t view = ecs_lookup(world, "view");
    ecs_get_mut(world, view, View)->lighting.enabled = false;
    ecs_modified(world, view, View);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "ablate frames=2 settle=1 rounds=1", &error);
    test_bool(error, false);
    ecs_os_free(out);

    int32_t i;
    for (i = 0; i < 2000; i ++) {
        ecs_progress(world, 0);
        out = box_command_run(world, s, "ablate", &error);
        test_bool(error, false);
        if (strncmp(out, "ablate: step", 12)) {
            break;
        }

        ecs_os_free(out);
    }

    test_assert(i < 2000);
    test_assert(strstr(out, "Lighting") == NULL);
    test_assert(strstr(out, "shadows off") == NULL);
    test_assert(strstr(out, "Scene.reflections off") == NULL);
    test_assert(strstr(out, "  bloom off ") != NULL);
    test_assert(strstr(out, "  Extended.alpha_cutout off ") != NULL);
    ecs_os_free(out);

    test_bool(ecs_get(world, view, View)->lighting.enabled, false);

    ecs_fini(world);
}

void Ablate_stop_restores(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "scene", ablate_scene, NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "ablate frames=50 settle=1", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_entity_t view = ecs_lookup(world, "view");
    int32_t i;
    for (i = 0; i < 2000; i ++) {
        ecs_progress(world, 0);
        if (!ecs_get(world, view, View)->lighting.enabled) {
            break;
        }
    }

    test_assert(i < 2000);
    out = box_command_run(world, s, "ablate stop", &error);
    test_bool(error, false);
    test_str(out, "ablate: stopped\n");
    ecs_os_free(out);
    test_bool(ecs_get(world, view, View)->lighting.enabled, true);
    test_flt(ecs_singleton_get(world, App)->fps, 0);

    out = box_command_run(world, s, "ablate", &error);
    test_bool(error, false);
    test_assert(strstr(out, "stopped after ") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "ablate stop", &error);
    test_bool(error, false);
    test_str(out, "ablate: idle\n");
    ecs_os_free(out);

    ecs_fini(world);
}

void Ablate_no_view(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "ablate", &error);
    test_bool(error, true);
    test_assert(!strncmp(out, BOX_COMMAND_ERROR, strlen(BOX_COMMAND_ERROR)));
    ecs_os_free(out);

    ecs_fini(world);
}

void Ablate_unknown_option(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "scene", ablate_scene, NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "ablate fast", &error);
    test_bool(error, true);
    test_assert(strstr(out, "unknown option 'fast'") != NULL);
    ecs_os_free(out);
    test_flt(ecs_singleton_get(world, App)->fps, 0);

    ecs_fini(world);
}

void Ablate_invalid_rounds(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "scene", ablate_scene, NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "ablate rounds=10", &error);
    test_bool(error, true);
    test_assert(strstr(out, "unknown option 'rounds=10'") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "ablate rounds=0", &error);
    test_bool(error, true);
    ecs_os_free(out);
    test_flt(ecs_singleton_get(world, App)->fps, 0);

    ecs_fini(world);
}

void Ablate_not_a_view(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "scene", ablate_scene, NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "ablate view=camera", &error);
    test_bool(error, true);
    test_assert(strstr(out, "'camera' is not a view") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}
