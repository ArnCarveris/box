#include <box.h>
#include <bake_test.h>
#include "render.h"
#include <math.h>

static ecs_world_t *design_world(box_render_null_t **backend)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *backend = box_render_null_new();
    box_render_backend_t api = box_render_null_backend(*backend);
    box_render_set_backend(world, &api);
    ecs_singleton_set(world, App, {
        .headless = true, .frames = 1, .design = true,
        .unguarded = true, .unpaced = true,
        .surface_width = 800, .surface_height = 600
    });
    return world;
}

void Design_parse(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *argv[] = {"boxrun", "--design"};
    test_int(box_app_parse(world, 2, argv), 0);
    test_bool(ecs_singleton_get(world, App)->design, true);
    ecs_fini(world);
}

void Design_camera_parse(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *argv[] = {"boxrun", "--camera", "scene.camera"};
    test_int(box_app_parse(world, 3, argv), 0);
    test_str(ecs_singleton_get(world, App)->camera, "scene.camera");
    ecs_entity_t copy = ecs_new(world);
    ecs_set_ptr(world, copy, App, ecs_singleton_get(world, App));
    test_assert(ecs_get(world, copy, App)->camera != ecs_singleton_get(world, App)->camera);
    test_str(ecs_get(world, copy, App)->camera, "scene.camera");
    ecs_fini(world);
}

void Design_invalid_options(void)
{
    const char *args[][4] = {
        {"boxrun", "--camera"},
        {"boxrun", "--camera", ""},
        {"boxrun", "--camera", "--headless"},
        {"boxrun", "--design", "--camera", "camera"}
    };
    int counts[] = {2, 3, 3, 4};
    ecs_log_set_level(-4);
    for (int i = 0; i < 4; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        test_assert(box_app_parse(world, counts[i], (char**)args[i]) != 0);
        ecs_fini(world);
    }
}

static void design_check_fit(ecs_world_t *world, vec3 min, vec3 max)
{
    const char *names[] = {
        "top", "bottom", "left", "right", "front", "back",
        "perspective", "perspective-back"
    };
    vec3 directions[] = {
        {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}, {1, 0, 0},
        {0, 0, 1}, {0, 0, -1}, {1, 0.75f, 1}, {-1, 0.75f, -1}
    };
    ecs_entity_t root = ecs_lookup(world, "scene.DesignCameras");
    test_assert(root != 0);
    for (int i = 0; i < 8; i ++) {
        ecs_entity_t entity = ecs_lookup_child(world, root, names[i]);
        const Camera *camera = ecs_get(world, entity, Camera);
        const WorldTransform *transform = ecs_get(world, entity, WorldTransform);
        test_assert(camera != NULL);
        test_assert(transform != NULL);
        test_bool(camera->ortho, i < 6);
        glm_vec3_normalize(directions[i]);
        test_assert(glm_vec3_dot((float*)transform->m[2], directions[i]) > 0.999f);
        test_assert(camera->near_ > 0);
        test_assert(camera->far_ > camera->near_);
        mat4 projection, view, vp;
        box_camera_projection(camera, 400, 600, projection);
        glm_mat4_inv((vec4*)transform->m, view);
        glm_mat4_mul(projection, view, vp);
        float edge = 0;
        for (int c = 0; c < 8; c ++) {
            vec4 point = {
                c & 1 ? max[0] : min[0],
                c & 2 ? max[1] : min[1],
                c & 4 ? max[2] : min[2], 1
            }, clip;
            glm_mat4_mulv(vp, point, clip);
            test_assert(clip[3] > 0);
            test_assert(fabsf(clip[0] / clip[3]) < 1);
            test_assert(fabsf(clip[1] / clip[3]) < 1);
            test_assert(clip[2] / clip[3] >= 0);
            test_assert(clip[2] / clip[3] <= 1);
            edge = fmaxf(edge, fmaxf(fabsf(clip[0] / clip[3]), fabsf(clip[1] / clip[3])));
        }
        test_assert(edge > 0.8f);
    }
}

void Design_fitted_views(void)
{
    box_render_null_t *backend;
    ecs_world_t *world = design_world(&backend);
    ecs_entity_t scene = ecs_entity(world, {.name = "scene"});
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, Position, {10, 20, 30});
    ecs_set(world, scene, Scale, {2, 3, -4});
    ecs_entity_t shape = ecs_entity(world, {.parent = scene});
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {2, 2, 2});
    ecs_entity_t moving = ecs_entity(world, {.parent = scene});
    ecs_add(world, moving, DynamicTransform);
    ecs_set(world, moving, Position, {3, 0, 0});
    ecs_set(world, moving, Box, {2, 2, 2});
    ecs_entity_t camera = ecs_entity(world, {.parent = scene});
    ecs_add(world, camera, Camera);
    ecs_entity_t effect = ecs_new(world);
    ecs_add(world, effect, Bloom);
    ViewEffect fx = {.enabled = true, .effect = effect};
    ecs_entity_t original = ecs_new(world);
    View view = {.camera = camera};
    ecs_vec_append_t(NULL, &view.effects, ViewEffect)[0] = fx;
    ecs_set_ptr(world, original, View, &view);
    ecs_vec_fini_t(NULL, &view.effects, ViewEffect);
    ecs_set(world, scene, AmbientLight, {.intensity = 2});
    ecs_set(world, scene, LightBake, {.probe_spacing = 1});
    ecs_entity_t light = ecs_entity(world, {.parent = scene});
    ecs_set(world, light, PointLight, {.intensity = 100, .range = 100});
    ecs_entity_t outside = ecs_new(world);
    ecs_set(world, outside, Position, {10000, 0, 0});
    ecs_set(world, outside, Box, {1000, 1000, 1000});

    test_int(box_app_run(world), 0);
    design_check_fit(world, (vec3){8, 17, 26}, (vec3){18, 23, 34});
    test_int(ecs_count(world, Surface), 9);
    test_int(ecs_count(world, DirectionalLight), 8);
    test_int(ecs_count(world, PointLight), 0);
    test_assert(!ecs_has(world, scene, LightBake));
    test_flt(ecs_get(world, scene, AmbientLight)->intensity, 0.08f);
    int count = 0;
    ecs_iter_t it = ecs_each(world, View);
    while (ecs_each_next(&it)) {
        const View *views = ecs_field(&it, View, 0);
        for (int i = 0; i < it.count; i ++) {
            if (views[i].shadows.enabled) {
                count ++;
                const Camera *c = ecs_get(world, views[i].camera, Camera);
                test_flt(views[i].shadows.max_distance, c->far_);
                test_bool(views[i].lighting.enabled, true);
                const WorldTransform *light = ecs_get(world, views[i].lighting.scene, WorldTransform);
                const WorldTransform *camera = ecs_get(world, views[i].camera, WorldTransform);
                test_assert(light != NULL);
                test_assert(-glm_vec3_dot((float*)light->m[2], (float*)camera->m[2]) > 0.8f);
                test_bool(views[i].lighting.baked, false);
                test_bool(views[i].lighting.reflections, false);
                test_bool(views[i].lighting.atmosphere, false);
                test_int(views[i].lighting.environment.source, 0);
                test_int(ecs_vec_count(&views[i].effects), 0);
            }
        }
    }
    test_int(count, 8);
    ecs_set(world, moving, Position, {5, 0, 0});
    ecs_progress(world, 0.016f);
    design_check_fit(world, (vec3){8, 17, 26}, (vec3){22, 23, 34});
    ecs_fini(world);
    box_render_null_free(backend);
}

void Design_empty_scene(void)
{
    box_render_null_t *backend;
    ecs_world_t *world = design_world(&backend);
    test_int(box_app_run(world), 0);
    test_int(ecs_count(world, Surface), 9);
    ecs_entity_t camera = ecs_lookup(world, "DesignCameras.perspective");
    test_assert(camera != 0);
    test_assert(box_camera_has_depth(ecs_get(world, camera, Camera)));
    ecs_fini(world);
    box_render_null_free(backend);
}

void Design_capture_names(void)
{
    box_render_null_t *backend;
    ecs_world_t *world = design_world(&backend);
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {
        .width = 800, .height = 600, .resolution_scale = 1, .pixel_ratio = 1,
        .write_to_file = "folder.with.dot/model.png"
    });
    test_int(box_app_run(world), 0);
    int count = 0;
    ecs_iter_t it = ecs_each(world, Surface);
    while (ecs_each_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (int i = 0; i < it.count; i ++) {
            if (s[i].texture) {
                ecs_iter_t children = ecs_children(world, s[i].view_root);
                test_assert(ecs_children_next(&children));
                const char *name = ecs_get_name(world, children.entities[0]);
                char expected[128];
                ecs_os_snprintf(expected, sizeof(expected), "folder.with.dot/model-%s.png", name);
                test_str(s[i].write_to_file, expected);
                ecs_iter_fini(&children);
                count ++;
            }
        }
    }
    test_int(count, 8);
    ecs_fini(world);
    box_render_null_free(backend);
}

void Design_select_camera(void)
{
    box_render_null_t *backend;
    ecs_world_t *world = design_world(&backend);
    ecs_singleton_get_mut(world, App)->design = false;
    char *argv[] = {"boxrun", "--camera", "scene.selected"};
    test_int(box_app_parse(world, 3, argv), 0);
    ecs_entity_t scene = ecs_entity(world, {.name = "scene"});
    ecs_entity_t camera = ecs_entity(world, {.parent = scene, .name = "selected"});
    ecs_set(world, camera, Camera, {.fov = 45, .near_ = 1, .far_ = 200});
    ecs_entity_t other = ecs_new(world);
    ecs_add(world, other, Camera);
    ecs_entity_t original = ecs_new(world);
    ecs_set(world, original, View, {
        .camera = other, .clear = {.enabled = true, .color = {1, 2, 3, 255}}
    });
    test_int(box_app_run(world), 0);
    test_int(ecs_count(world, Surface), 1);
    ecs_iter_t it = ecs_each(world, Surface);
    test_assert(ecs_each_next(&it));
    ecs_entity_t root = ecs_field(&it, Surface, 0)->view_root;
    ecs_iter_fini(&it);
    ecs_entity_t view = ecs_lookup_child(world, root, "camera");
    test_int(ecs_get(world, view, View)->camera, camera);
    test_int(ecs_get(world, view, View)->clear.color.g, 2);
    test_flt(ecs_get(world, camera, Camera)->fov, 45);
    test_int(ecs_get(world, original, View)->camera, other);
    ecs_fini(world);
    box_render_null_free(backend);
}

void Design_invalid_camera(void)
{
    ecs_log_set_level(-4);
    for (int i = 0; i < 2; i ++) {
        box_render_null_t *backend;
        ecs_world_t *world = design_world(&backend);
        ecs_singleton_get_mut(world, App)->design = false;
        if (i) {
            ecs_entity(world, {.name = "missing"});
        }
        char *argv[] = {"boxrun", "--camera", "missing"};
        test_int(box_app_parse(world, 3, argv), 0);
        test_assert(box_app_run(world) != 0);
        test_int(ecs_count(world, Surface), 0);
        ecs_fini(world);
        box_render_null_free(backend);
    }
}
