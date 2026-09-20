/**
 * @file Render.c
 * @brief Tests for the render command: framing, scratch instances and cleanup.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "../../../src/command/command_internal.h"
#include "../../../src/surface/surface_internal.h"
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;

void Render_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    session = ecs_entity(w, {
        .name = "agent0", .parent = ecs_lookup(w, "terminals")
    });

    ecs_add(w, session, Terminal);
}

void Render_teardown(void)
{
    ecs_fini(w);
}

void Render_prints_the_image_path(void)
{
    // views/render_command
    ecs_entity_t thing = ecs_entity(w, { .name = "thing" });
    ecs_set(w, thing, Position, {0, 0, 0});
    ecs_set(w, thing, Box, {1, 1, 1});

    bool error = true;
    char *out = box_command_run(w, session, "render thing", &error);
    test_bool(error, false);
    test_str(out, "render_0_0.png\n");
    ecs_os_free(out);

    TerminalSession *s = ecs_get_mut(w, session, TerminalSession);
    test_int(ecs_vec_count(&s->renders), 1);
    box_render_job_t *job = ecs_vec_get_t(&s->renders, box_render_job_t, 0);
    char *expect = flecs_asprintf("%s/resources/render_0_0.png",
        ecs_get(w, session, Terminal)->session_dir);
    test_str(job->path, expect);
    test_uint(job->subject, thing);
    test_uint(job->scratch, 0);
    test_assert(ecs_has(w, job->camera, Camera));
    test_assert(ecs_has(w, job->view, View));
    test_assert(ecs_has(w, job->light, DirectionalLight));
    test_int(ecs_get(w, job->target, RenderTarget)->width, 512);
    test_int(ecs_get(w, job->target, RenderTarget)->height, 512);
    ecs_os_free(expect);
}

void Render_numbers_the_images_per_session(void)
{
    // views/render_command
    ecs_entity_t thing = ecs_entity(w, { .name = "thing" });
    ecs_set(w, thing, Position, {0, 0, 0});

    char *out = box_command_run(w, session, "render thing", NULL);
    test_str(out, "render_0_0.png\n");
    ecs_os_free(out);

    out = box_command_run(w, session, "render thing", NULL);
    test_str(out, "render_0_1.png\n");
    ecs_os_free(out);

    TerminalSession *s = ecs_get_mut(w, session, TerminalSession);
    test_int(ecs_vec_count(&s->renders), 2);
}

void Render_unknown_subject(void)
{
    // views/render_command
    bool error = false;
    char *out = box_command_run(w, session, "render nope", &error);
    test_bool(error, true);
    test_str(out, "unknown entity 'nope'");
    ecs_os_free(out);

    TerminalSession *s = ecs_get_mut(w, session, TerminalSession);
    test_int(ecs_vec_count(&s->renders), 0);
}

void Render_without_a_subject(void)
{
    // views/render_command
    bool error = false;
    char *out = box_command_run(w, session, "render", &error);
    test_bool(error, true);
    test_str(out, "command 'render' takes 1 arguments, got 0");
    ecs_os_free(out);
}

void Render_subject_relative_to_cwd(void)
{
    // views/render_command
    ecs_entity_t thing = ecs_entity(w, { .name = "scene.thing" });
    ecs_set(w, thing, Position, {0, 0, 0});
    ecs_os_free(box_command_run(w, session, "cd scene", NULL));

    bool error = true;
    char *out = box_command_run(w, session, "render thing", &error);
    test_bool(error, false);
    test_str(out, "render_0_0.png\n");
    ecs_os_free(out);

    TerminalSession *s = ecs_get_mut(w, session, TerminalSession);
    test_uint(ecs_vec_get_t(&s->renders, box_render_job_t, 0)->subject, thing);
}

void Render_frames_the_camera_on_the_bounds(void)
{
    // views/render_command
    ecs_entity_t thing = ecs_entity(w, { .name = "thing" });
    ecs_set(w, thing, Position, {2, 1, 0});
    ecs_set(w, thing, AABB, {{1, 0, -1}, {3, 2, 1}});
    ecs_os_free(box_command_run(w, session, "render thing", NULL));

    TerminalSession *s = ecs_get_mut(w, session, TerminalSession);
    box_render_job_t *job = ecs_vec_get_t(&s->renders, box_render_job_t, 0);
    ecs_entity_t camera = job->camera, light = job->light;
    ecs_progress(w, 0);

    float radius = vec3_length(vec3_sub(vec3_new(3, 2, 1), vec3_new(1, 0, -1))) * 0.5f;
    float distance = radius / sinf(45.0f * 0.5f * (3.14159265f / 180.0f));
    const Position *p = ecs_get(w, camera, Position);
    test_assert(p != NULL);
    test_flt(p->x, 2 + 0.6124f * distance);
    test_flt(p->y, 1 + 0.5f * distance);
    test_flt(p->z, 0 - 0.6124f * distance);

    const LookAt *l = ecs_get(w, camera, LookAt);
    test_assert(l != NULL);
    test_flt(l->x, 2);
    test_flt(l->y, 1);
    test_flt(l->z, 0);

    const LookAt *sun = ecs_get(w, light, LookAt);
    test_assert(sun != NULL);
    test_flt(sun->x, 2);
    test_flt(sun->y, 1);
    test_flt(sun->z, 0);
    test_assert(ecs_get(w, light, Position)->y > 1);
    test_assert(ecs_get(w, light, Position)->x < 2);
    test_assert(ecs_has(w, camera, Capture));
}

void Render_instantiates_a_prefab(void)
{
    // views/render_command
    ecs_entity_t prefab = ecs_entity(w, { .name = "Ball" });
    ecs_add_id(w, prefab, EcsPrefab);
    ecs_set(w, prefab, Position, {0, 0, 0});
    ecs_set(w, prefab, Sphere, {.segments = 16, .smooth = true, .radius = 1});

    char *out = box_command_run(w, session, "render Ball", NULL);
    test_str(out, "render_0_0.png\n");
    ecs_os_free(out);

    TerminalSession *s = ecs_get_mut(w, session, TerminalSession);
    box_render_job_t *job = ecs_vec_get_t(&s->renders, box_render_job_t, 0);
    test_assert(job->scratch != 0);
    test_uint(job->subject, job->scratch);
    test_assert(ecs_has_pair(w, job->scratch, EcsIsA, prefab));
    test_uint(ecs_get_parent(w, job->scratch), ecs_lookup(w, "terminals.agent0.scratch"));
    test_assert(ecs_has_pair(w, job->scratch, Layer, job->layer));
    test_assert(!ecs_has_pair(w, prefab, Layer, job->layer));
}

void Render_instantiates_a_template(void)
{
    // views/render_command
    ecs_script(w, { .code =
        "using box.transform\n"
        "using box.geometry\n"
        "template Crate {\n"
        "  Position: {0, 0, 0}\n"
        "  Box: {1, 1, 1}\n"
        "}\n"
    });

    ecs_entity_t tmpl = ecs_lookup(w, "Crate");
    test_assert(tmpl != 0);

    char *out = box_command_run(w, session, "render Crate", NULL);
    test_str(out, "render_0_0.png\n");
    ecs_os_free(out);

    TerminalSession *s = ecs_get_mut(w, session, TerminalSession);
    box_render_job_t *job = ecs_vec_get_t(&s->renders, box_render_job_t, 0);
    test_assert(job->scratch != 0);
    test_assert(ecs_has_id(w, job->scratch, tmpl));
    test_assert(ecs_has(w, job->scratch, Box));
    test_uint(ecs_get_parent(w, job->scratch), ecs_lookup(w, "terminals.agent0.scratch"));
}

void Render_cleans_up_when_the_image_appears(void)
{
    // views/render_command
    ecs_entity_t prefab = ecs_entity(w, { .name = "Ball" });
    ecs_add_id(w, prefab, EcsPrefab);
    ecs_set(w, prefab, Position, {0, 0, 0});
    ecs_set(w, prefab, Sphere, {.segments = 16, .smooth = true, .radius = 1});
    ecs_os_free(box_command_run(w, session, "render Ball", NULL));

    TerminalSession *s = ecs_get_mut(w, session, TerminalSession);
    box_render_job_t *job = ecs_vec_get_t(&s->renders, box_render_job_t, 0);
    ecs_entity_t scratch = job->scratch, view = job->view, camera = job->camera;
    ecs_entity_t light = job->light, target = job->target, layer = job->layer;
    char *path = ecs_os_strdup(job->path);

    int32_t i;
    for (i = 0; i < 8; i ++) {
        ecs_progress(w, 0);
    }

    test_int(ecs_vec_count(&s->renders), 1);
    test_assert(ecs_is_alive(w, scratch));

    FILE *file = fopen(path, "wb");
    test_assert(file != NULL);
    fclose(file);
    ecs_progress(w, 0);

    test_int(ecs_vec_count(&s->renders), 0);
    test_assert(!ecs_is_alive(w, scratch));
    test_assert(!ecs_is_alive(w, view));
    test_assert(!ecs_is_alive(w, camera));
    test_assert(!ecs_is_alive(w, light));
    test_assert(!ecs_is_alive(w, target));
    test_assert(!ecs_is_alive(w, layer));
    test_assert(ecs_lookup(w, "terminals.agent0.scratch") != 0);
    remove(path);
    ecs_os_free(path);
}

void Render_live_entity_joins_and_leaves_the_layer(void)
{
    // views/render_command
    ecs_entity_t thing = ecs_entity(w, { .name = "thing" });
    ecs_set(w, thing, Position, {0, 0, 0});
    ecs_set(w, thing, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_entity_t child = ecs_entity(w, { .name = "thing.part" });
    ecs_set(w, child, Position, {0, 1, 0});

    ecs_os_free(box_command_run(w, session, "render thing", NULL));

    TerminalSession *s = ecs_get_mut(w, session, TerminalSession);
    box_render_job_t *job = ecs_vec_get_t(&s->renders, box_render_job_t, 0);
    ecs_entity_t layer = job->layer;
    char *path = ecs_os_strdup(job->path);
    test_assert(ecs_has_pair(w, thing, Layer, layer));
    test_assert(ecs_has_pair(w, child, Layer, layer));
    test_assert(ecs_has_pair(w, job->light, Layer, layer));
    test_uint(ecs_get(w, job->view, View)->layers[0], layer);

    ecs_progress(w, 0);
    FILE *file = fopen(path, "wb");
    test_assert(file != NULL);
    fclose(file);
    ecs_progress(w, 0);

    test_int(ecs_vec_count(&s->renders), 0);
    test_assert(ecs_is_alive(w, thing));
    test_assert(ecs_is_alive(w, child));
    test_uint(ecs_get_target(w, thing, Layer, 0), 0);
    test_uint(ecs_get_target(w, child, Layer, 0), 0);
    remove(path);
    ecs_os_free(path);
}

void Render_headless_run_writes_the_png(void)
{
    // views/render_command
    App *app = ecs_singleton_ensure(w, App);
    app->headless = true;
    ecs_singleton_modified(w, App);

    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 64;
    s->height = 32;
    ecs_singleton_modified(w, Surface);

    ecs_entity_t prefab = ecs_entity(w, { .name = "Ball" });
    ecs_add_id(w, prefab, EcsPrefab);
    ecs_set(w, prefab, Position, {0, 0, 0});
    ecs_set(w, prefab, Sphere, {.segments = 16, .smooth = true, .radius = 1});

    const SurfaceImpl *impl = ecs_get(w, ecs_id(Surface), SurfaceImpl);
    if (impl && impl->renderer) {
        char *out = box_command_run(w, session, "render Ball", NULL);
        char *file = ecs_os_strdup(out);
        char *end = strchr(file, '\n');
        end[0] = '\0';
        char *path = flecs_asprintf("%s/resources/%s",
            ecs_get(w, session, Terminal)->session_dir, file);
        bool written = false;
        int32_t i;
        for (i = 0; i < 200 && !written; i ++) {
            ecs_progress(w, 0);
            FILE *f = fopen(path, "rb");
            if (f) {
                unsigned char magic[4] = {0};
                size_t got = fread(magic, 1, 4, f);
                fclose(f);
                written = got == 4 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G';
            }
        }

        test_bool(written, true);
        ecs_progress(w, 0);
        TerminalSession *state = ecs_get_mut(w, session, TerminalSession);
        test_int(ecs_vec_count(&state->renders), 0);
        ecs_os_free(path);
        ecs_os_free(file);
        ecs_os_free(out);
    }
}

void Render_live_subject_is_rendered_alone(void)
{
    // views/strict_layers
    ecs_entity_t thing = ecs_entity(w, { .name = "thing" });
    ecs_set(w, thing, Position, {0, 0, 0});
    ecs_set(w, thing, Box, {1, 1, 1});
    ecs_entity_t floor = ecs_entity(w, { .name = "floor" });
    ecs_set(w, floor, Position, {10, 0, 0});
    ecs_set(w, floor, Box, {1, 1, 1});
    ecs_entity_t ghost = ecs_entity(w, { .name = "ghost" });
    ecs_set(w, ghost, Position, {0, 0, 10});
    ecs_set(w, ghost, Box, {1, 1, 1});
    ecs_add_id(w, ghost, DynamicTransform);

    ecs_os_free(box_command_run(w, session, "render thing", NULL));
    ecs_progress(w, 0);

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 512;
    packet.height = 512;
    box_render_extract(w, &packet);

    box_view_t *v = NULL;
    uint32_t i;
    for (i = 0; i < packet.views.count; i ++) {
        box_view_t *candidate = box_vec_at(&packet.views, i);
        if (candidate->strict_layers) {
            v = candidate;
        }
    }

    test_assert(v != NULL);
    test_assert(v->layer_mask != BOX_VIEW_LAYER_ALL);
    test_int(v->dynamic_count, 0);
    test_bool(box_view_shows(v, 0), false);
    test_bool(box_view_shows(v, ecs_get(w, thing, RenderInstance)->layer_mask), true);
    test_bool(box_view_shows(v, ecs_get(w, floor, RenderInstance)->layer_mask), false);
    box_packet_fini(&packet);
}
