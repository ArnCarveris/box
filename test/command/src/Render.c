/**
 * @file Render.c
 * @brief Tests for the capture and render commands, on the null backend and headless on the GPU.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Render_capture_active_camera(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t s = box_terminal_new(world);

    bool error = true;
    char *out = box_terminal_run(world, s, "capture", &error);
    test_bool(error, false);
    test_str(out, "capture_0.png (pending)");
    ecs_os_free(out);
    test_int(box_render_captures_pending(world), 1);

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(box_render_captures_pending(world), 0);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    const box_render_view_t *main_view = ecs_vec_get_t(&p->views, box_render_view_t, 0);
    const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, 1);
    test_uint(main_view->view, view);
    test_uint(main_view->target, 0);
    test_uint(v->camera, camera);
    test_uint(v->scene, 0);
    test_assert(v->target >= BOX_RENDER_CAPTURE_TARGET_FIRST);

    test_int(ecs_vec_count(&p->targets), 1);
    const box_render_target_t *t = ecs_vec_first_t(&p->targets, box_render_target_t);
    test_uint(t->handle, v->target);
    test_bool(t->exclusive, true);
    test_assert(t->capture_size != 0);
    char *expect = flecs_asprintf("%s/resources/capture_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    test_str(box_render_packet_data(p, t->capture), expect);
    ecs_os_free(expect);

    int32_t i, created = 0;
    for (i = 0; i < ecs_vec_count(&p->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(&p->resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_TARGET && r->handle == t->handle) {
            const box_render_target_desc_t *desc = box_render_packet_data(p, r->data);
            test_int(r->op, BOX_RENDER_CREATE);
            test_assert(desc->layer == NULL);
            test_int(desc->width, BOX_SURFACE_WIDTH);
            test_int(desc->height, BOX_SURFACE_HEIGHT);
            test_int(desc->render_width, BOX_SURFACE_WIDTH);
            test_int(desc->render_height, BOX_SURFACE_HEIGHT);
            created ++;
        }
    }

    test_int(created, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_named_camera(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t main_camera = ecs_entity(world, { .name = "main_camera" });
    ecs_set(world, main_camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = main_camera});
    ecs_entity_t scene = ecs_entity(world, { .name = "level" });
    ecs_set(world, scene, Scene, {0});
    ecs_entity_t other = ecs_entity(world, { .name = "level.other" });
    ecs_set(world, other, Camera, {.fov = 30});
    ecs_set(world, other, Position, {1, 2, 3});
    ecs_entity_t s = box_terminal_new(world);

    char *out = box_terminal_run(world, s, "capture level.other", NULL);
    test_str(out, "capture_0.png (pending)");
    ecs_os_free(out);

    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, 1);
    test_uint(v->camera, other);
    test_uint(v->scene, scene);
    test_flt(v->camera_data.fov, 30);
    test_flt(v->camera_transform[3][0], 1);
    test_flt(v->camera_transform[3][1], 2);
    test_flt(v->camera_transform[3][2], 3);
    test_flt(v->view_matrix[3][0], -1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_batch(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Camera, {.fov = 60});
    ecs_entity_t s = box_terminal_new(world);

    char *out = box_terminal_run(world, s, "capture a, b", NULL);
    test_str(out, "capture_0.png (pending)\ncapture_1.png (pending)");
    ecs_os_free(out);
    test_int(box_render_captures_pending(world), 2);

    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    test_int(ecs_vec_count(&p->targets), 2);
    const box_render_view_t *va = ecs_vec_get_t(&p->views, box_render_view_t, 0);
    const box_render_view_t *vb = ecs_vec_get_t(&p->views, box_render_view_t, 1);
    const box_render_target_t *ta = ecs_vec_get_t(&p->targets, box_render_target_t, 0);
    const box_render_target_t *tb = ecs_vec_get_t(&p->targets, box_render_target_t, 1);
    test_uint(va->camera, a);
    test_uint(vb->camera, b);
    test_uint(va->target, ta->handle);
    test_uint(vb->target, tb->handle);
    test_assert(ta->handle != tb->handle);
    test_assert(strstr(box_render_packet_data(p, ta->capture), "/capture_0.png") != NULL);
    test_assert(strstr(box_render_packet_data(p, tb->capture), "/capture_1.png") != NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_bracket_list(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Camera, {.fov = 60});
    ecs_entity_t s = box_terminal_new(world);

    char *out = box_terminal_run(world, s, "capture [b, a, b]", NULL);
    test_str(out,
        "capture_0.png (pending)\ncapture_1.png (pending)\ncapture_2.png (pending)");
    ecs_os_free(out);

    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 3);
    test_uint(ecs_vec_get_t(&p->views, box_render_view_t, 0)->camera, b);
    test_uint(ecs_vec_get_t(&p->views, box_render_view_t, 1)->camera, a);
    test_uint(ecs_vec_get_t(&p->views, box_render_view_t, 2)->camera, b);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_size_of_surface(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 320, .height = 200});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    ecs_os_free(box_terminal_run(world, s, "capture", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    int32_t i, created = 0;
    for (i = 0; i < ecs_vec_count(&p->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(&p->resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_TARGET && r->handle >= BOX_RENDER_CAPTURE_TARGET_FIRST) {
            const box_render_target_desc_t *desc = box_render_packet_data(p, r->data);
            test_int(desc->width, 320);
            test_int(desc->height, 200);
            created ++;
        }
    }

    test_int(created, 1);
    test_int(ecs_vec_count(&p->targets), 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_clear_of_view(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Camera, {.fov = 60});
    ecs_entity_t view_a = ecs_new(world);
    ecs_set(world, view_a, View, {
        .camera = a, .clear = {.enabled = true, .color = {255, 0, 0, 255}}});
    ecs_entity_t view_b = ecs_new(world);
    ecs_set(world, view_b, View, {.camera = b});
    ecs_entity_t s = box_terminal_new(world);

    ecs_os_free(box_terminal_run(world, s, "capture b", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->targets), 1);
    const box_render_target_t *t = ecs_vec_first_t(&p->targets, box_render_target_t);
    test_flt(t->clear[0], 1);
    test_flt(t->clear[1], 0);
    test_flt(t->clear[2], 0);
    test_flt(t->clear[3], 1);
    const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, 2);
    test_uint(v->camera, b);
    test_bool(v->clear.enabled, true);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_not_a_camera(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Camera, {.fov = 60});
    ecs_entity(world, { .name = "thing" });
    ecs_entity_t s = box_terminal_new(world);

    bool error = false;
    char *out = box_terminal_run(world, s, "capture a, thing", &error);
    test_bool(error, true);
    test_str(out, "error: 'thing' is not a camera");
    ecs_os_free(out);
    test_int(box_render_captures_pending(world), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_unknown_camera(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "capture nope", &error);
    test_bool(error, true);
    test_str(out, "error: no entity 'nope'");
    ecs_os_free(out);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_no_active_camera(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "capture", &error);
    test_bool(error, true);
    test_str(out, "error: no active camera");
    ecs_os_free(out);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_view_root_of_surface(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Camera, {.fov = 60});
    ecs_entity_t view_a = ecs_new(world);
    ecs_set(world, view_a, View, {.camera = a});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t view_b = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, view_b, View, {.camera = b});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8, .view_root = root});
    ecs_entity_t s = box_terminal_new(world);

    ecs_os_free(box_terminal_run(world, s, "capture", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 3);
    const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, 2);
    test_uint(v->camera, b);
    test_assert(v->target != 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_target_destroyed_next_frame(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_entity(world, { .name = "cam" });
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t s = box_terminal_new(world);

    ecs_os_free(box_terminal_run(world, s, "capture cam", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);
    uint32_t handle = ecs_vec_first_t(&null->last.targets, box_render_target_t)->handle;

    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->targets), 0);
    test_int(ecs_vec_count(&p->views), 0);
    int32_t i, destroyed = 0;
    for (i = 0; i < ecs_vec_count(&p->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(&p->resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_TARGET) {
            test_int(r->op, BOX_RENDER_DESTROY);
            test_uint(r->handle, handle);
            destroyed ++;
        }
    }

    test_int(destroyed, 1);

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.resources), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_prefab_private_scene(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t prefab = ecs_entity(world, { .name = "Crate" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Position, {0, 0, 0});
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_entity_t lid = ecs_entity(world, { .name = "Crate.lid" });
    ecs_set(world, lid, Position, {0, 1, 0});
    ecs_set(world, lid, Sphere, {.radius = 0.5f});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, 0, 0});
    ecs_set(world, floor, Box, {10, 1, 10});
    ecs_entity_t s = box_terminal_new(world);

    bool error = true;
    char *out = box_terminal_run(world, s, "render Crate", &error);
    test_bool(error, false);
    test_str(out, "render_0.png (pending)");
    ecs_os_free(out);

    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 1);
    const box_render_view_t *v = ecs_vec_first_t(&p->views, box_render_view_t);
    test_assert(v->scene != 0);
    test_assert(ecs_has(world, v->scene, Scene));
    test_assert(ecs_has_pair(world, v->scene, EcsChildOf, s));
    test_uint(v->camera, 0);
    test_assert(v->target >= BOX_RENDER_CAPTURE_TARGET_FIRST);
    test_flt(v->camera_data.fov, 45);

    int32_t i, scene_instances = 0, default_instances = 0;
    for (i = 0; i < ecs_vec_count(&p->batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        if (b->scene == v->scene) {
            scene_instances += (int32_t)b->instance_count;
        } else if (!b->scene) {
            default_instances += (int32_t)b->instance_count;
        }
    }

    test_int(scene_instances, 2);
    test_int(default_instances, 1);

    const box_render_target_t *t = ecs_vec_first_t(&p->targets, box_render_target_t);
    char *expect = flecs_asprintf("%s/resources/render_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    test_str(box_render_packet_data(p, t->capture), expect);
    ecs_os_free(expect);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_scene_deleted_next_frame(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t prefab = ecs_entity(world, { .name = "Crate" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_entity_t s = box_terminal_new(world);

    ecs_os_free(box_terminal_run(world, s, "render Crate", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_entity_t scene = ecs_vec_first_t(&null->last.views, box_render_view_t)->scene;
    test_assert(ecs_is_alive(world, scene));

    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(!ecs_is_alive(world, scene));
    test_int(ecs_count(world, Scene), 0);
    test_assert(ecs_is_alive(world, prefab));
    test_assert(ecs_has(world, prefab, Box));

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.batches), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_template(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    test_assert(ecs_script(world, {
        .code =
            "using box.transform\n"
            "using box.geometry\n"
            "template Pillar {\n"
            "  prop height: f32 = 4\n"
            "  base { Position: {0, height / 2, 0}\n Box: {1, height, 1} }\n"
            "  cap { Position: {0, height, 0}\n Sphere: {radius: 0.5} }\n"
            "  top { Position: {0, height + 1, 0}\n Sphere: {radius: 0.5} }\n"
            "}\n"
    }) != 0);
    ecs_entity_t s = box_terminal_new(world);

    bool error = true;
    char *out = box_terminal_run(world, s, "render Pillar", &error);
    test_bool(error, false);
    test_str(out, "render_0.png (pending)");
    ecs_os_free(out);

    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 1);
    const box_render_view_t *v = ecs_vec_first_t(&p->views, box_render_view_t);
    int32_t i, instances = 0;
    for (i = 0; i < ecs_vec_count(&p->batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        if (b->scene == v->scene) {
            instances += (int32_t)b->instance_count;
        }
    }

    test_int(instances, 3);
    test_assert(v->camera_transform[3][1] > 2.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_entity_copy(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t car = ecs_entity(world, { .name = "car" });
    ecs_set(world, car, Position, {5, 0, 0});
    ecs_set(world, car, Box, {2, 1, 1});
    ecs_set(world, car, Rgb, {10, 20, 30});
    ecs_entity_t wheel = ecs_entity(world, { .name = "car.wheel" });
    ecs_set(world, wheel, Position, {1, -0.5f, 0});
    ecs_set(world, wheel, Sphere, {.radius = 0.3f});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Box, {1, 1, 1});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    char *out = box_terminal_run(world, s, "render car", NULL);
    test_str(out, "render_0.png (pending)");
    ecs_os_free(out);
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, 1);
    test_assert(v->scene != 0);
    ecs_entity_t copy = 0;
    ecs_iter_t it = ecs_children(world, v->scene);
    while (ecs_children_next(&it)) {
        test_int(it.count, 1);
        copy = it.entities[0];
    }

    test_assert(copy != 0);
    test_assert(copy != car);
    test_assert(ecs_get_name(world, copy) == NULL);
    test_flt(ecs_get(world, copy, Position)->x, 5);
    test_flt(ecs_get(world, copy, Box)->x, 2);
    test_int(ecs_get(world, copy, Rgb)->g, 20);
    test_int(ecs_count_id(world, ecs_childof(copy)), 1);
    test_assert(ecs_is_alive(world, wheel));
    test_assert(ecs_has_pair(world, wheel, EcsChildOf, car));
    test_flt(ecs_get(world, car, Position)->x, 5);

    int32_t i, scene_instances = 0;
    for (i = 0; i < ecs_vec_count(&p->batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&p->batches, box_render_batch_t, i);
        if (b->scene == v->scene) {
            scene_instances += (int32_t)b->instance_count;
        }
    }

    test_int(scene_instances, 2);

    ecs_progress(world, 0);
    test_assert(!ecs_is_alive(world, copy));
    test_assert(ecs_is_alive(world, car));
    test_assert(ecs_is_alive(world, wheel));

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_fits_bounds(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t e = ecs_entity(world, { .name = "cube" });
    ecs_set(world, e, Position, {10, 0, 0});
    ecs_set(world, e, Box, {2, 2, 2});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    ecs_os_free(box_terminal_run(world, s, "render cube", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 1);
    const box_render_view_t *v = ecs_vec_first_t(&p->views, box_render_view_t);
    float radius = sqrtf(12.0f) * 0.5f;
    float distance = 1.1f * radius / sinf(glm_rad(22.5f));
    vec3 eye = { v->camera_transform[3][0], v->camera_transform[3][1],
        v->camera_transform[3][2] };
    vec3 center = { 10, 0, 0 };
    test_assert(fabsf(glm_vec3_distance(eye, center) - distance) < 0.01f);
    test_assert(eye[0] > 10 && eye[1] > 0 && eye[2] > 0);

    vec4 local = { 0, 0, 0, 1 }, viewed;
    glm_vec3_copy(center, local);
    glm_mat4_mulv(ECS_CONST_CAST(vec4*, v->view_matrix), local, viewed);
    test_assert(fabsf(viewed[0]) < 0.001f);
    test_assert(fabsf(viewed[1]) < 0.001f);
    test_assert(viewed[2] < 0);
    test_assert(v->camera_data.near_ > 0);
    test_assert(v->camera_data.near_ < distance - radius);

    const box_render_resource_t *r = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&p->resources); i ++) {
        r = ecs_vec_get_t(&p->resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_TARGET) {
            break;
        }
    }

    test_assert(r != NULL && r->kind == BOX_RENDER_TARGET);
    const box_render_target_desc_t *desc = box_render_packet_data(p, r->data);
    test_int(desc->width, 512);
    test_int(desc->height, 512);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_unknown(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "render nope", &error);
    test_bool(error, true);
    test_str(out, "error: no entity 'nope'");
    ecs_os_free(out);

    ecs_progress(world, 0);
    test_int(ecs_count(world, Scene), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_builtin(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "render /flecs.core", &error);
    test_bool(error, true);
    test_str(out, "error: '/flecs.core' is builtin");
    ecs_os_free(out);
    test_int(ecs_count(world, Scene), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_session_closed(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t prefab = ecs_entity(world, { .name = "Crate" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_entity_t s = box_terminal_new(world);

    ecs_os_free(box_terminal_run(world, s, "render Crate", NULL));
    ecs_delete(world, s);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.views), 0);
    test_int(ecs_vec_count(&null->last.targets), 0);
    test_int(ecs_count(world, Scene), 0);

    ecs_progress(world, 0);
    test_assert(ecs_is_alive(world, prefab));

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_names_unique(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_entity(world, { .name = "cam" });
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t prefab = ecs_entity(world, { .name = "Crate" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_entity_t s = box_terminal_new(world);

    char *out = box_terminal_run(world, s, "capture cam", NULL);
    test_str(out, "capture_0.png (pending)");
    ecs_os_free(out);
    out = box_terminal_run(world, s, "capture cam", NULL);
    test_str(out, "capture_1.png (pending)");
    ecs_os_free(out);
    out = box_terminal_run(world, s, "render Crate", NULL);
    test_str(out, "render_0.png (pending)");
    ecs_os_free(out);
    out = box_terminal_run(world, s, "render Crate", NULL);
    test_str(out, "render_1.png (pending)");
    ecs_os_free(out);

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.targets), 4);
    test_int(ecs_vec_count(&null->last.views), 4);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_gpu_capture_png(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {255, 128, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 32});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    ecs_os_free(box_terminal_run(world, s, "capture", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/capture_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 32);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] != 255 || px[i * 4 + 1] != 128 || px[i * 4 + 2] != 0 ||
            px[i * 4 + 3] != 255;
    }

    test_int(wrong, 0);
    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.errors, 0);

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_gpu_stats(world, &stats);
    test_int(stats.targets, 1);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_gpu_render_isolated(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t prefab = ecs_entity(world, { .name = "Ball" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Sphere, {.radius = 1, .segments = 24, .smooth = true});
    ecs_set(world, prefab, Rgb, {0, 0, 255});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, 0});
    ecs_set(world, wall, Box, {50, 50, 50});
    ecs_set(world, wall, Rgb, {255, 0, 0});
    ecs_entity_t s = box_terminal_new(world);

    char *out = box_terminal_run(world, s, "render Ball", NULL);
    test_str(out, "render_0.png (pending)");
    ecs_os_free(out);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/render_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0, i, red = 0, blue = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 512);
    test_int(h, 512);
    for (i = 0; i < w * h; i ++) {
        red += px[i * 4] > 100 && px[i * 4 + 2] < 50;
        blue += px[i * 4 + 2] > 100 && px[i * 4] < 50;
    }

    test_int(red, 0);
    test_assert(blue > w * h / 10);
    test_int(px[0], 48);
    test_int(px[1], 52);
    test_int(px[2], 60);
    int32_t c = (h / 2 * w + w / 2) * 4;
    test_assert(px[c + 2] > 100);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_http_resource(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t prefab = ecs_entity(world, { .name = "Ball" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Sphere, {.radius = 1});
    ecs_entity_t s = box_terminal_new(world);

    char *path = flecs_asprintf("/terminal/exec?session=%s&cmd=render%%20Ball",
        ecs_get_name(world, s));
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", path, NULL, &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "render_0.png (pending)");
    ecs_os_free(body);
    ecs_os_free(path);

    ecs_progress(world, 0);
    box_render_sync(world);

    path = flecs_asprintf("/resource/render_0.png?session=%s", ecs_get_name(world, s));
    ecs_http_reply_t res = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", path, NULL, &res), 200);
    int32_t size = ecs_strbuf_written(&res.body);
    body = ecs_strbuf_get(&res.body);
    test_assert(size > 8);
    test_assert(!memcmp(body, "\x89PNG", 4));
    ecs_os_free(body);
    ecs_os_free(path);

    ecs_fini(world);
}
