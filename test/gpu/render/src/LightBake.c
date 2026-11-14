/**
 * @file LightBake.c
 * @brief Tests for the baked lighting settings and the status the engine keeps next to them.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"
#include "render_backend.h"
#include "bake.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void LightBake_script_defaults(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "defaults",
        "using box.light\n"
        "using box.view\n"
        "scene {\n Scene: {}\n Bake: {}\n}\n"
        "custom {\n Scene: {}\n Bake: {probe_spacing: 4, bounces: 3, capture_size: 32, "
        "budget_ms: 5, bounds_padding: 2, cache: \"other\"}\n}\n", NULL) == 0);

    const LightBake *bake = ecs_get(world, ecs_lookup(world, "scene"), LightBake);
    test_assert(bake != NULL);
    test_flt(bake->probe_spacing, 2);
    test_int(bake->bounces, 2);
    test_int(bake->capture_size, 16);
    test_flt(bake->budget_ms, 2);
    test_flt(bake->bounds_padding, 1);
    test_str(bake->cache, ".box/bake");

    bake = ecs_get(world, ecs_lookup(world, "custom"), LightBake);
    test_assert(bake != NULL);
    test_flt(bake->probe_spacing, 4);
    test_int(bake->bounces, 3);
    test_int(bake->capture_size, 32);
    test_flt(bake->budget_ms, 5);
    test_flt(bake->bounds_padding, 2);
    test_str(bake->cache, "other");

    ecs_fini(world);
}

void LightBake_c_defaults(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, LightBake);
    const LightBake *bake = ecs_get(world, scene, LightBake);
    test_assert(bake != NULL);
    test_flt(bake->probe_spacing, 2);
    test_int(bake->bounces, 2);
    test_int(bake->capture_size, 16);
    test_flt(bake->budget_ms, 2);
    test_flt(bake->bounds_padding, 1);
    test_str(bake->cache, ".box/bake");

    ecs_fini(world);
}

void LightBake_zero_is_literal(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "zero",
        "using box.light\n"
        "using box.view\n"
        "scene {\n Scene: {}\n Bake: {bounces: 0, budget_ms: 0, probe_spacing: 0, "
        "capture_size: 0, bounds_padding: 0, cache: \"\"}\n}\n", NULL) == 0);

    const LightBake *bake = ecs_get(world, ecs_lookup(world, "scene"), LightBake);
    test_assert(bake != NULL);
    test_int(bake->bounces, 0);
    test_flt(bake->budget_ms, 0);
    test_flt(bake->probe_spacing, 0);
    test_int(bake->capture_size, 0);
    test_flt(bake->bounds_padding, 0);
    test_assert(bake->cache == NULL || bake->cache[0] == '\0');

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, LightBake, {0});
    bake = ecs_get(world, literal, LightBake);
    test_int(bake->bounces, 0);
    test_flt(bake->budget_ms, 0);
    test_flt(bake->probe_spacing, 0);

    ecs_fini(world);
}

void LightBake_status_follows_bake(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    test_assert(!ecs_has(world, scene, LightBakeStatus));

    ecs_add(world, scene, LightBake);
    test_assert(ecs_has(world, scene, LightBakeStatus));
    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_int(status->probes, 0);
    test_int(status->baked, 0);
    test_int(status->pass, 0);
    test_bool(status->converged, false);
    test_bool(status->cache_hit, false);

    ecs_remove(world, scene, LightBake);
    test_assert(!ecs_has(world, scene, LightBakeStatus));

    ecs_set(world, scene, LightBake, {.bounces = 1, .compute = true});
    test_assert(ecs_has(world, scene, LightBakeStatus));

    test_assert(ecs_script_run(world, "status",
        "using box.light\n"
        "using box.view\n"
        "other {\n Scene: {}\n Bake: {}\n}\n", NULL) == 0);
    test_assert(ecs_has(world, ecs_lookup(world, "other"), LightBakeStatus));

    ecs_delete(world, scene);
    ecs_fini(world);
}

void LightBake_singleton_bake_has_status(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, LightBake, {.probe_spacing = 3, .bounces = 1, .compute = true});
    test_assert(ecs_has(world, ecs_id(LightBake), LightBakeStatus));
    test_flt(ecs_singleton_get(world, LightBake)->probe_spacing, 3);

    ecs_singleton_remove(world, LightBake);
    test_assert(!ecs_has(world, ecs_id(LightBake), LightBakeStatus));

    ecs_fini(world);
}

void LightBake_no_bake_tag(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t shape = ecs_new(world);
    ecs_add(world, shape, NoBake);
    test_assert(ecs_has(world, shape, NoBake));

    test_assert(ecs_script_run(world, "tag",
        "using box.light\n"
        "wall { NoBake }\n", NULL) == 0);
    test_assert(ecs_has(world, ecs_lookup(world, "wall"), NoBake));

    ecs_fini(world);
}

void LightBake_cache_is_owned(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = ecs_os_strdup("cache/dir");
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, LightBake, {.cache = path, .compute = true});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, LightBake, {.cache = ecs_os_strdup("second"), .compute = true});
    ecs_set(world, b, LightBake, {.cache = ecs_os_strdup("third"), .compute = true});

    test_str(ecs_get(world, b, LightBake)->cache, "third");
    test_str(ecs_get(world, a, LightBake)->cache, "cache/dir");

    ecs_entity_t c = ecs_clone(world, 0, a, true);
    ecs_delete(world, a);
    test_str(ecs_get(world, c, LightBake)->cache, "cache/dir");

    ecs_fini(world);
}

void LightBake_view_baked_defaults(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "view",
        "using box.view\n"
        "view { View: {} }\n"
        "off { View: {lighting: {enabled: true, baked: false}} }\n", NULL) == 0);

    const View *view = ecs_get(world, ecs_lookup(world, "view"), View);
    test_assert(view != NULL);
    test_bool(view->lighting.enabled, true);
    test_bool(view->lighting.baked, true);
    view = ecs_get(world, ecs_lookup(world, "off"), View);
    test_bool(view->lighting.enabled, true);
    test_bool(view->lighting.baked, false);

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, View, {.lighting.enabled = true});
    test_bool(ecs_get(world, literal, View)->lighting.baked, false);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, View);
    test_bool(ecs_get(world, added, View)->lighting.baked, true);

    ecs_fini(world);
}

void LightBake_scene_renders_identically(void) {
    // baked_lighting/bake_settings
    unsigned char *images[2] = {NULL, NULL};
    int w[2] = {0, 0}, h[2] = {0, 0};
    int i;

    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
            ? ecs_singleton_get(world, Scene) : &(Scene){0});
        if (i == 1) {
            ecs_set(world, scene, LightBake, {.bounces = 1, .compute = true});
        }

        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {0, 1, 6});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {10, 10, 20, 255}}, .lighting.enabled = true, .lighting.baked = true});
        ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, floor, Position, {0, -1, 0});
        ecs_set(world, floor, Box, {10, 0.2f, 10});
        ecs_set(world, floor, Rgb, {200, 200, 200});
        ecs_entity_t ball = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, ball, Position, {0, 0.5f, 0});
        ecs_set(world, ball, Rgb, {220, 60, 40});
        ecs_set(world, ball, Sphere, {.segments = 24, .smooth = true, .radius = 1});
        ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, light, Position, {2, 3, 2});
        ecs_set(world, light, PointLight, {.intensity = 4 * GLM_PIf, .range = 12});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "bake.png",
            .resolution_scale = 1});
        ecs_progress(world, 0);
        box_render_sync(world);

        char *path = flecs_asprintf("%s/bake.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        int n = 0;
        images[i] = stbi_load(path, &w[i], &h[i], &n, 4);
        test_assert(images[i] != NULL);
        ecs_os_free(path);
        ecs_fini(world);
    }

    test_int(w[0], w[1]);
    test_int(h[0], h[1]);
    test_int(memcmp(images[0], images[1], (size_t)w[0] * (size_t)h[0] * 4), 0);

    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

void LightBake_projected_anchor_not_baked(void) {
    // core_fixes/projection_position
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 1, .compute = true});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {2, 0.2f, 2});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frame;
    const LightBakeStatus *status = NULL;
    for (frame = 0; frame < 5000; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);

    ecs_entity_t anchor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, anchor, ProjectedAnchor, {
        .target = floor, .view = view, .offset = {0, 1.1f, 0}});
    ecs_set(world, anchor, Box, {0.5f, 0.5f, 0.5f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, anchor);
    ecs_set(world, child, Position, {0.5f, 0, 0});
    ecs_set(world, child, Box, {0.3f, 0.3f, 0.3f});
    for (frame = 0; frame < 5; frame ++) {
        ecs_set(world, camera, Position, {0.01f * (float)frame, 1, 5});
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        test_assert(status->converged);
        test_assert(fabsf(ecs_get(world, anchor, WorldTransform)->m[3][0]) < 1.0f);
    }

    ecs_entity_t plain = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, plain, Box, {0.5f, 0.5f, 0.5f});
    ecs_set(world, plain, Position, {0, 0, 0});
    ecs_progress(world, 0);
    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, false);

    ecs_fini(world);
}

/* A floor with an emissive box on it, its bake seen by a view of its scene. */
typedef struct lb_scene_t {
    ecs_world_t *world;
    ecs_entity_t scene;
    ecs_entity_t view;
    ecs_entity_t box;
} lb_scene_t;

static lb_scene_t lb_scene(
    bool headless,
    float budget_ms,
    int32_t bounces,
    bool compute,
    const char *file)
{
    lb_scene_t result = {0};
    ecs_world_t *world = result.world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
        .punctual_shadows = true, .cookies = true, .environment = true,
        .aerial_perspective = true, .alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = headless;
    ecs_singleton_modified(world, App);

    result.scene = ecs_new(world);
    ecs_set_ptr(world, result.scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, result.scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = budget_ms, .bounces = bounces, .compute = compute});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, result.scene);
    ecs_set(world, camera, Position, {0, 4, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    result.view = ecs_new(world);
    ecs_set(world, result.view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, result.scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {48, 0.2f, 4});
    result.box = ecs_new_w_pair(world, EcsChildOf, result.scene);
    ecs_set(world, result.box, Position, {-6, 0.5f, 0});
    ecs_set(world, result.box, Box, {1, 1, 1});
    ecs_set(world, result.box, Emissive, {.strength = 2, .color = {255, 200, 100}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.texture = e, .width = 64, .height = 48,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    return result;
}

static void lb_set_compute(
    const lb_scene_t *s,
    bool compute)
{
    LightBake *bake = ecs_get_mut(s->world, s->scene, LightBake);
    bake->compute = compute;
    ecs_modified(s->world, s->scene, LightBake);
}

static void lb_frame(
    const lb_scene_t *s)
{
    ecs_progress(s->world, 0);
    box_render_sync(s->world);
}

static const LightBakeStatus* lb_status(
    const lb_scene_t *s)
{
    return ecs_get(s->world, s->scene, LightBakeStatus);
}

static const box_bake_volume_t* lb_volume(
    const lb_scene_t *s)
{
    const box_bake_volume_t *v = box_bake_find(box_bake_state(s->world), s->scene);
    test_assert(v != NULL);
    return v;
}

/* Probe captures the backend reported for the bake in progress, over every pass */
static int32_t lb_reported(
    const box_bake_volume_t *v)
{
    int32_t i, result = 0;
    for (i = 0; i < v->passes; i ++) {
        result += v->reported[i];
    }

    return result;
}

static void lb_converge(
    const lb_scene_t *s)
{
    int32_t frames = 0;
    while (!lb_status(s)->converged && frames < 5000) {
        lb_frame(s);
        frames ++;
    }

    test_bool(lb_status(s)->converged, true);
}

static unsigned char* lb_image(
    const lb_scene_t *s,
    const char *file,
    int *w,
    int *h)
{
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(s->world, AppStatus)->tmp_dir,
        file);
    int n = 0;
    unsigned char *result = stbi_load(path, w, h, &n, 4);
    test_assert(result != NULL);
    ecs_os_free(path);
    return result;
}

static int lb_largest(
    const unsigned char *a,
    const unsigned char *b,
    int w,
    int h)
{
    int i, largest = 0;
    for (i = 0; i < w * h * 4; i ++) {
        if ((i & 3) != 3) {
            int d = abs((int)a[i] - (int)b[i]);
            largest = d > largest ? d : largest;
        }
    }

    return largest;
}

void LightBake_compute_default_true(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, LightBake);
    test_bool(ecs_get(world, scene, LightBake)->compute, true);
    test_bool(ecs_get(world, scene, LightBakeStatus)->held, false);

    test_assert(ecs_script_run(world, "compute",
        "using box.light\n"
        "using box.view\n"
        "on {\n Scene: {}\n Bake: {}\n}\n"
        "off {\n Scene: {}\n Bake: {compute: false}\n}\n", NULL) == 0);
    test_bool(ecs_get(world, ecs_lookup(world, "on"), LightBake)->compute, true);
    test_bool(ecs_get(world, ecs_lookup(world, "off"), LightBake)->compute, false);

    ecs_fini(world);
}

void LightBake_compute_off_schedules_nothing(void) {
    // baked_lighting/bake_settings
    lb_scene_t s = lb_scene(true, 2, 1, false, NULL);
    int32_t frame;
    for (frame = 0; frame < 10; frame ++) {
        lb_frame(&s);
        const LightBakeStatus *status = lb_status(&s);
        const box_bake_volume_t *v = lb_volume(&s);
        test_assert(status->probes > 0);
        test_int(status->baked, 0);
        test_int(status->pass, 0);
        test_bool(status->converged, false);
        test_bool(status->held, true);
        test_int(ecs_vec_count(&v->flight), 0);
        test_int(lb_reported(v), 0);
        test_int(box_render_bake_pending(s.world), 0);
    }

    ecs_fini(s.world);
}

void LightBake_compute_off_in_progress_finishes(void) {
    // baked_lighting/bake_settings
    lb_scene_t s = lb_scene(false, 0.001f, 1, true, NULL);
    int32_t frame;
    for (frame = 0; frame < 5000; frame ++) {
        lb_frame(&s);
        if (lb_status(&s)->baked >= 3 && ecs_vec_count(&lb_volume(&s)->flight)) {
            break;
        }
    }

    test_bool(lb_status(&s)->converged, false);
    test_assert(box_render_bake_pending(s.world) > 0);
    const box_bake_volume_t *v = lb_volume(&s);
    int32_t reported = lb_reported(v), flight = ecs_vec_count(&v->flight);
    uint32_t generation = v->generation;
    test_assert(flight > 0);

    lb_set_compute(&s, false);
    for (frame = 0; frame < 100 && ecs_vec_count(&lb_volume(&s)->flight); frame ++) {
        lb_frame(&s);
    }

    v = lb_volume(&s);
    test_int(ecs_vec_count(&v->flight), 0);
    test_assert(v->generation == generation);
    test_int(lb_reported(v), reported + flight);
    test_int(box_render_bake_pending(s.world), 0);

    LightBakeStatus held = *lb_status(&s);
    test_assert(held.baked >= 3);
    test_bool(held.converged, false);
    test_bool(held.held, true);
    for (frame = 0; frame < 20; frame ++) {
        lb_frame(&s);
        const LightBakeStatus *status = lb_status(&s);
        v = lb_volume(&s);
        test_int(ecs_vec_count(&v->flight), 0);
        test_int(lb_reported(v), reported + flight);
        test_int(status->probes, held.probes);
        test_int(status->baked, held.baked);
        test_int(status->pass, held.pass);
        test_bool(status->converged, false);
        test_int(box_render_bake_pending(s.world), 0);
    }

    ecs_fini(s.world);
}

void LightBake_compute_on_resumes(void) {
    // baked_lighting/bake_settings
    lb_scene_t s[2];
    float (*sh[2])[BOX_RENDER_SH_COEFFICIENTS][4] = {NULL, NULL};
    int32_t i, j, k, c, probes = 0;

    for (i = 0; i < 2; i ++) {
        s[i] = lb_scene(false, 0.001f, 1, true, NULL);
        ecs_singleton_set(s[i].world, RenderSettings, {.in_flight = 2, .main_thread = true});
        if (i == 1) {
            int32_t frame;
            for (frame = 0; frame < 5000 && lb_status(&s[i])->baked < 3; frame ++) {
                lb_frame(&s[i]);
            }

            test_bool(lb_status(&s[i])->converged, false);
            lb_set_compute(&s[i], false);
            for (frame = 0; frame < 30; frame ++) {
                lb_frame(&s[i]);
            }

            test_bool(lb_status(&s[i])->converged, false);
            lb_set_compute(&s[i], true);
        }

        lb_converge(&s[i]);
        test_bool(lb_status(&s[i])->held, false);
        lb_frame(&s[i]);
        const box_bake_volume_t *v = lb_volume(&s[i]);
        if (i == 0) {
            probes = v->probes;
        }

        test_int(v->probes, probes);
        sh[i] = ecs_os_calloc(probes * (int32_t)sizeof(sh[i][0]));
        for (j = 0; j < probes; j ++) {
            if (box_bake_valid(v, (uint32_t)j)) {
                test_assert(box_render_gpu_read_volume_sh(s[i].world, v->handle,
                    (uint32_t)v->display, (uint32_t)j, sh[i][j]));
            }
        }
    }

    float largest = 0, scale = 0;
    for (j = 0; j < probes; j ++) {
        for (k = 0; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
            for (c = 0; c < 3; c ++) {
                largest = glm_max(largest, fabsf(sh[0][j][k][c] - sh[1][j][k][c]));
                scale = glm_max(scale, fabsf(sh[0][j][k][c]));
            }
        }
    }

    test_assert(scale > 0);
    test_assert(largest <= scale * 0.01f);
    for (i = 0; i < 2; i ++) {
        ecs_os_free(sh[i]);
        ecs_fini(s[i].world);
    }
}

void LightBake_compute_off_records_changes(void) {
    // baked_lighting/bake_settings
    lb_scene_t s = lb_scene(true, 2, 0, true, NULL);
    lb_converge(&s);
    LightBakeStatus converged = *lb_status(&s);
    test_assert(converged.probes > 50);

    lb_set_compute(&s, false);
    lb_frame(&s);
    test_bool(lb_status(&s)->converged, true);
    test_bool(lb_status(&s)->held, false);

    ecs_set(s.world, s.box, Position, {-3, 0.5f, 0});
    lb_frame(&s);
    LightBakeStatus held = *lb_status(&s);
    test_bool(held.converged, false);
    test_bool(held.held, true);
    test_int(held.probes, converged.probes);
    test_assert(held.baked < converged.baked);
    test_assert((converged.baked - held.baked) * 4 < converged.probes);

    int32_t frame;
    for (frame = 0; frame < 10; frame ++) {
        lb_frame(&s);
        const LightBakeStatus *status = lb_status(&s);
        const box_bake_volume_t *v = lb_volume(&s);
        test_int(ecs_vec_count(&v->flight), 0);
        test_int(lb_reported(v), 0);
        test_int(status->baked, held.baked);
        test_bool(status->converged, false);
        test_int(box_render_bake_pending(s.world), 0);
    }

    lb_set_compute(&s, true);
    lb_frame(&s);
    const box_bake_volume_t *v = lb_volume(&s);
    test_assert(v->count > 0);
    test_assert(v->count * 4 < v->probes);
    lb_converge(&s);
    test_int(lb_status(&s)->baked, converged.baked);
    test_int(lb_status(&s)->probes, converged.probes);

    ecs_fini(s.world);
}

void LightBake_compute_off_still_applied(void) {
    // baked_lighting/bake_settings
    lb_scene_t s = lb_scene(true, 2, 0, true, "applied.png");
    int w[3] = {0}, h[3] = {0};
    unsigned char *images[3];
    lb_converge(&s);
    lb_frame(&s);
    images[0] = lb_image(&s, "applied.png", &w[0], &h[0]);

    lb_set_compute(&s, false);
    lb_frame(&s);
    lb_frame(&s);
    images[1] = lb_image(&s, "applied.png", &w[1], &h[1]);
    test_int(w[1], w[0]);
    test_int(h[1], h[0]);
    test_assert(lb_largest(images[0], images[1], w[0], h[0]) <= 1);

    View *view = ecs_get_mut(s.world, s.view, View);
    view->lighting.baked = false;
    ecs_modified(s.world, s.view, View);
    lb_frame(&s);
    lb_frame(&s);
    images[2] = lb_image(&s, "applied.png", &w[2], &h[2]);
    test_int(w[2], w[0]);
    test_int(h[2], h[0]);
    test_assert(lb_largest(images[0], images[2], w[0], h[0]) > 8);

    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
    stbi_image_free(images[2]);
    ecs_fini(s.world);
}

void LightBake_compute_off_view_unbaked(void) {
    // baked_lighting/bake_settings
    lb_scene_t s = lb_scene(true, 2, 1, true, NULL);
    View *view = ecs_get_mut(s.world, s.view, View);
    view->lighting.baked = false;
    ecs_modified(s.world, s.view, View);
    lb_converge(&s);
    const LightBakeStatus *status = lb_status(&s);
    test_assert(status->probes > 0);
    test_assert(status->baked > 0);
    test_int(status->pass, 2);
    test_bool(status->held, false);
    test_int(box_render_bake_pending(s.world), 0);

    ecs_fini(s.world);
}

void LightBake_compute_change_no_rebuild(void) {
    // baked_lighting/bake_settings
    lb_scene_t s = lb_scene(true, 2, 1, true, NULL);
    lb_converge(&s);
    lb_frame(&s);
    const box_bake_volume_t *v = lb_volume(&s);
    box_bake_grid_t grid = v->grid;
    uint32_t handle = v->handle, generation = v->generation;
    int32_t probes = v->probes, display = v->display, switches = v->switches;
    LightBakeStatus converged = *lb_status(&s);

    int32_t i, frame;
    for (i = 0; i < 4; i ++) {
        lb_set_compute(&s, i % 2 == 0 ? false : true);
        for (frame = 0; frame < 3; frame ++) {
            lb_frame(&s);
            v = lb_volume(&s);
            test_assert(!ecs_os_memcmp_t(&grid, &v->grid, box_bake_grid_t));
            test_assert(v->handle == handle);
            test_assert(v->generation == generation);
            test_int(v->probes, probes);
            test_int(v->display, display);
            test_int(v->switches, switches);
            test_int(ecs_vec_count(&v->dirty), 0);
            test_int(ecs_vec_count(&v->flight), 0);

            const LightBakeStatus *status = lb_status(&s);
            test_bool(status->converged, true);
            test_bool(status->held, false);
            test_int(status->probes, converged.probes);
            test_int(status->baked, converged.baked);
            test_int(status->pass, converged.pass);
        }
    }

    ecs_fini(s.world);
}

/* Set Checkbox.mut current of the checkbox opts.baked as the scenes see a click of it */
static void lb_set_checkbox(
    ecs_world_t *world,
    bool value)
{
    ecs_entity_t session = ecs_shell_session_init(world, NULL);
    char *line = flecs_asprintf("set opts.baked box.widgets.Checkbox.mut {current: %s}",
        value ? "true" : "false");
    bool error = true;
    char *out = box_command_run(world, session, line, &error);
    if (error) {
        printf("command '%s' failed: %s\n", line, out);
    }

    test_bool(error, false);
    ecs_os_free(out);
    ecs_os_free(line);
}

void LightBake_compute_follows_checkbox(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *widgets = box_app_locate("test/assets/ui/widgets.flecs");
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "box.ui.widgets" }),
        .filename = widgets
    }) != 0);
    ecs_os_free(widgets);

    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "follows" }),
        .code =
        "using box.widgets\n"
        "using box.light\n"
        "using box.view\n"
        "using box.camera\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.surface\n"
        "opts {\n Checkbox baked(label: \"Baked lighting\", value: true)\n}\n"
        "scene {\n"
        " Scene: {}\n"
        " Bake: {probe_spacing: 2, bounds_padding: 1, capture_size: 8, bounces: 0, "
        "budget_ms: 2, compute: opts.baked[Checkbox.mut].current}\n"
        " camera { Position: {0, 4, 10}\n Camera: {fov: 60, near_: 0.1} }\n"
        " floor { Position: {0, -0.1, 0}\n Box: {16, 0.2, 4} }\n"
        " lamp { Position: {-2, 0.5, 0}\n Box: {1, 1, 1}\n "
        "Emissive: {strength: 2, color: {255, 200, 100}} }\n"
        "}\n"
        "view {\n View: {camera: scene.camera, lighting: {enabled: true, "
        "baked: opts.baked[Checkbox.mut].current}}\n}\n"
        "window {\n Surface: {width: 64, height: 48, resolution_scale: 1}\n}\n"
    }) != 0);

    ecs_entity_t scene = ecs_lookup(world, "scene");
    ecs_entity_t view = ecs_lookup(world, "view");
    test_assert(scene != 0);
    test_assert(view != 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(ecs_get(world, scene, LightBake)->compute, true);
    test_bool(ecs_get(world, view, View)->lighting.baked, true);

    int32_t frame;
    for (frame = 0; frame < 5000 && !ecs_get(world, scene, LightBakeStatus)->converged;
        frame ++)
    {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    const box_bake_volume_t *v = box_bake_find(box_bake_state(world), scene);
    test_assert(v != NULL);
    uint32_t handle = v->handle, generation = v->generation;
    int32_t probes = v->probes;

    int32_t i;
    for (i = 0; i < 4; i ++) {
        bool value = i % 2 != 0;
        lb_set_checkbox(world, value);
        for (frame = 0; frame < 3; frame ++) {
            ecs_progress(world, 0);
            box_render_sync(world);
        }

        test_bool(ecs_get(world, view, View)->lighting.baked, value);
        test_bool(ecs_get(world, scene, LightBake)->compute, value);
        v = box_bake_find(box_bake_state(world), scene);
        test_assert(v != NULL);
        test_assert(v->handle == handle);
        test_assert(v->generation == generation);
        test_int(v->probes, probes);
        test_int(ecs_vec_count(&v->dirty), 0);
        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    }

    ecs_fini(world);
}
