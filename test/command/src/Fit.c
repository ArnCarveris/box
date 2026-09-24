/**
 * @file Fit.c
 * @brief Tests for the fit command: framing the scene with the active or a named camera.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <math.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;

void Fit_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
}

void Fit_teardown(void)
{
    ecs_fini(w);
}

void Fit_frames_every_corner_of_the_scene(void)
{
    ecs_entity_t camera = ecs_entity(w, { .name = "camera" });
    ecs_set(w, camera, Camera, { .fov = 60, .aspect_ratio = 1 });
    ecs_set(w, camera, Position, {0, 0, -3});
    ecs_set(w, camera, LookAt, {0, 0, 0});
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {-10, 0, 0});
    ecs_set(w, a, Box, {2, 2, 2});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {10, 4, 0});
    ecs_set(w, b, Box, {2, 2, 2});
    ecs_progress(w, 0);

    bool error = true;
    char *out = box_command_run(w, session, "fit", &error);
    test_bool(error, false);
    test_assert(strstr(out, "fit camera to 2 entities, center (0.00, 2.00, 0.00)") == out);
    ecs_os_free(out);

    const LookAt *look = ecs_get(w, camera, LookAt);
    test_flt(look->x, 0);
    test_flt(look->y, 2);
    test_flt(look->z, 0);

    const Position *p = ecs_get(w, camera, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 2);
    test_assert(p->z < -11);

    float t = tanf(30.0f * (3.14159265f / 180.0f));
    float xs[2] = {-11, 11}, ys[2] = {-1, 5}, zs[2] = {-1, 1};
    int32_t i;
    for (i = 0; i < 8; i ++) {
        float depth = zs[(i >> 2) & 1] - p->z;
        test_assert(depth > 0);
        test_assert(fabsf(xs[i & 1] - p->x) <= depth * t);
        test_assert(fabsf(ys[(i >> 1) & 1] - p->y) <= depth * t);
    }
}

void Fit_keeps_the_view_direction(void)
{
    ecs_entity_t camera = ecs_entity(w, { .name = "camera" });
    ecs_set(w, camera, Camera, { .fov = 60 });
    ecs_set(w, camera, Position, {5, 5, 5});
    ecs_set(w, camera, LookAt, {6, 4, 6});
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_progress(w, 0);

    char *out = box_command_run(w, session, "fit", NULL);
    ecs_os_free(out);

    const Position *p = ecs_get(w, camera, Position);
    const LookAt *look = ecs_get(w, camera, LookAt);
    vec3 dir = vec3_normalize(vec3_new(look->x - p->x, look->y - p->y, look->z - p->z));
    float inv = 1.0f / sqrtf(3.0f);
    test_flt(dir.x, inv);
    test_flt(dir.y, -inv);
    test_flt(dir.z, inv);
    test_flt(look->x, 0);
    test_flt(look->y, 0);
    test_flt(look->z, 0);
}

void Fit_moves_a_camera_without_lookat_along_its_axis(void)
{
    ecs_entity_t camera = ecs_entity(w, { .name = "camera" });
    ecs_set(w, camera, Camera, { .fov = 60 });
    ecs_set(w, camera, Position, {100, 0, 0});
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 3, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_progress(w, 0);

    char *out = box_command_run(w, session, "fit", NULL);
    ecs_os_free(out);

    const Position *p = ecs_get(w, camera, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 3);
    test_assert(p->z < -0.5f);
    test_assert(!ecs_has(w, camera, LookAt));
}

void Fit_named_camera(void)
{
    ecs_entity_t active = ecs_entity(w, { .name = "active" });
    ecs_set(w, active, Camera, {0});
    ecs_set(w, active, Position, {0, 0, -50});
    ecs_entity_t other = ecs_entity(w, { .name = "other" });
    ecs_set(w, other, Camera, {0});
    ecs_set(w, other, Position, {0, 0, -60});
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_progress(w, 0);

    bool error = true;
    char *out = box_command_run(w, session, "fit other", &error);
    test_bool(error, false);
    test_assert(strstr(out, "fit other to 1 entities") == out);
    ecs_os_free(out);

    test_assert(ecs_get(w, other, Position)->z > -60);
    test_flt(ecs_get(w, active, Position)->z, -50);
}

void Fit_skips_hidden_entities(void)
{
    ecs_entity_t camera = ecs_entity(w, { .name = "camera" });
    ecs_set(w, camera, Camera, {0});
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, LookAt, {0, 0, 0});
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_entity_t parent = ecs_new(w);
    ecs_add(w, parent, Hidden);
    ecs_set(w, parent, Position, {0, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_set(w, b, Position, {50, 0, 0});
    ecs_set(w, b, Box, {1, 1, 1});
    ecs_progress(w, 0);

    char *out = box_command_run(w, session, "fit", NULL);
    test_assert(strstr(out, "fit camera to 1 entities, center (0.00, 0.00, 0.00)") == out);
    ecs_os_free(out);
}

void Fit_skips_children_of_the_camera(void)
{
    ecs_entity_t camera = ecs_entity(w, { .name = "camera" });
    ecs_set(w, camera, Camera, {0});
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_entity_t held = ecs_new_w_pair(w, EcsChildOf, camera);
    ecs_set(w, held, Position, {0, 0, 1});
    ecs_set(w, held, Box, {1, 1, 1});
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {4, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_progress(w, 0);

    char *out = box_command_run(w, session, "fit", NULL);
    test_assert(strstr(out, "fit camera to 1 entities, center (4.00, 0.00, 0.00)") == out);
    ecs_os_free(out);
}

void Fit_skips_layers_the_view_does_not_show(void)
{
    ecs_entity_t camera = ecs_entity(w, { .name = "camera" });
    ecs_set(w, camera, Camera, {0});
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_entity_t shown = ecs_new(w);
    ecs_entity_t other = ecs_new(w);
    ecs_entity_t view = ecs_new(w);
    View *v = ecs_ensure(w, view, View);
    v->camera = camera;
    v->layers[0] = shown;
    v->strict_layers = true;
    ecs_modified(w, view, View);
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {-3, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_add_pair(w, a, Layer, shown);
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {30, 0, 0});
    ecs_set(w, b, Box, {1, 1, 1});
    ecs_add_pair(w, b, Layer, other);
    ecs_entity_t c = ecs_new(w);
    ecs_set(w, c, Position, {60, 0, 0});
    ecs_set(w, c, Box, {1, 1, 1});
    ecs_progress(w, 0);

    char *out = box_command_run(w, session, "fit", NULL);
    test_assert(strstr(out, "fit camera to 1 entities, center (-3.00, 0.00, 0.00)") == out);
    ecs_os_free(out);
}

void Fit_orthographic_camera_sets_its_height(void)
{
    ecs_entity_t camera = ecs_entity(w, { .name = "camera" });
    ecs_set(w, camera, Camera, { .orthographic = true, .aspect_ratio = 1 });
    ecs_set(w, camera, Position, {0, 0, -100});
    ecs_set(w, camera, LookAt, {0, 0, 0});
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, 0});
    ecs_set(w, a, Box, {2, 8, 2});
    ecs_progress(w, 0);

    char *out = box_command_run(w, session, "fit", NULL);
    ecs_os_free(out);

    const Camera *c = ecs_get(w, camera, Camera);
    test_assert(c->ortho_height >= 8);
    test_assert(c->ortho_height < 10);
    const Position *p = ecs_get(w, camera, Position);
    test_assert(p->z < -1);
    test_assert(p->z > -2);
}

void Fit_reaims_the_camera_controller(void)
{
    ecs_entity_t camera = ecs_entity(w, { .name = "camera" });
    ecs_set(w, camera, Camera, {0});
    ecs_set(w, camera, Position, {0, 10, -10});
    ecs_set(w, camera, LookAt, {0, 0, 0});
    ecs_set(w, camera, CameraController, {0});
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {20, 0, 20});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_progress(w, 0);

    const CameraController *cc = ecs_get(w, camera, CameraController);
    float yaw = cc->yaw, pitch = cc->pitch;
    char *out = box_command_run(w, session, "fit", NULL);
    ecs_os_free(out);
    ecs_progress(w, 0.016f);

    cc = ecs_get(w, camera, CameraController);
    test_flt(cc->yaw, yaw);
    test_flt(cc->pitch, pitch);
    const LookAt *look = ecs_get(w, camera, LookAt);
    const Position *p = ecs_get(w, camera, Position);
    vec3 dir = vec3_normalize(vec3_new(20 - p->x, 0 - p->y, 20 - p->z));
    vec3 aim = vec3_normalize(vec3_new(look->x - p->x, look->y - p->y, look->z - p->z));
    test_flt(dir.x, aim.x);
    test_flt(dir.y, aim.y);
    test_flt(dir.z, aim.z);
}

void Fit_no_content_is_an_error(void)
{
    ecs_entity_t camera = ecs_entity(w, { .name = "camera" });
    ecs_set(w, camera, Camera, {0});
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_progress(w, 0);

    bool error = false;
    char *out = box_command_run(w, session, "fit", &error);
    test_bool(error, true);
    test_str(out, "no content to fit");
    ecs_os_free(out);
    test_flt(ecs_get(w, camera, Position)->z, -10);
}

void Fit_no_active_camera_is_an_error(void)
{
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_progress(w, 0);

    bool error = false;
    char *out = box_command_run(w, session, "fit", &error);
    test_bool(error, true);
    test_str(out, "no active camera");
    ecs_os_free(out);
}

void Fit_unknown_camera_is_an_error(void)
{
    ecs_entity_t thing = ecs_entity(w, { .name = "thing" });
    ecs_set(w, thing, Position, {0, 0, 0});

    bool error = false;
    char *out = box_command_run(w, session, "fit nope", &error);
    test_bool(error, true);
    test_str(out, "unknown camera 'nope'");
    ecs_os_free(out);

    out = box_command_run(w, session, "fit thing", &error);
    test_bool(error, true);
    test_str(out, "'thing' is not a camera");
    ecs_os_free(out);
}

void Fit_help_lists_the_command(void)
{
    bool error = true;
    char *out = box_command_run(w, session, "help", &error);
    test_bool(error, false);
    test_assert(strstr(out, "fit") != NULL);
    ecs_os_free(out);
}
