/**
 * @file Portal.c
 * @brief Tests for the portal commands on a rendered world of test/assets/portal/rooms.flecs:
 * the traversal of the view they set, and the lines they draw, checked on the captured image.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "../../../../src/posix.h"
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width and height of the capture */
#define PORTAL_GPU_SIZE (128)

/** File the surface writes every frame to, in the temporary folder of the app */
#define PORTAL_GPU_FILE "portal_commands.png"

/** What a test world holds */
typedef struct portal_gpu_t {
    ecs_world_t *world;     /**< The world */
    ecs_entity_t camera;    /**< The camera, in the hall looking at the side room */
    ecs_entity_t view;      /**< The view that renders it */
    ecs_entity_t session;   /**< The shell session */
} portal_gpu_t;

/** Create the world of the rooms, a camera in the hall that looks at the door to the side room
 * and the view that culls by portals.
 *
 * @param t Receives the world
 */
static void portal_gpu_init(
    portal_gpu_t *t)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = box_app_locate("test/assets/portal/rooms.flecs");
    test_assert(path != NULL);
    test_int(ecs_script_run_file(world, path), 0);
    ecs_os_free(path);

    t->world = world;
    t->camera = ecs_entity(world, { .name = "eye" });
    ecs_set(world, t->camera, Position, {0, 1.6f, 4});
    ecs_set(world, t->camera, Rotation, {0, GLM_PIf, 0});
    ecs_set(world, t->camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    t->view = ecs_entity(world, { .name = "main_view" });
    ecs_set(world, t->view, View, {
        .camera = t->camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .culling = {.enabled = true, .portals = true}
    });
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {
        .width = PORTAL_GPU_SIZE, .height = PORTAL_GPU_SIZE, .resolution_scale = 1,
        .write_to_file = ECS_CONST_CAST(char*, PORTAL_GPU_FILE)});
    t->session = ecs_shell_session_init(world, NULL);

    ecs_progress(world, 0.1f);
    ecs_progress(world, 0.1f);
}

/** Run a command line that has to succeed.
 *
 * @param t The world
 * @param line The command line
 * @return The output, which the caller frees
 */
static char* portal_gpu_run(
    portal_gpu_t *t,
    const char *line)
{
    bool error = true;
    char *out = box_command_run(t->world, t->session, line, &error);
    test_bool(error, false);

    return out;
}

/** Run frames so that the traversal the commands changed is extracted and set.
 *
 * @param t The world
 */
static void portal_gpu_frames(
    portal_gpu_t *t)
{
    ecs_progress(t->world, 0.1f);
    ecs_progress(t->world, 0.1f);
    ecs_progress(t->world, 0.1f);
}

/** Render the view and count the pixels of its image that are lit in any channel.
 *
 * @param t The world
 * @return Lit pixels: the background is black
 */
static int32_t portal_gpu_count(
    portal_gpu_t *t)
{
    int w = 0, h = 0, n = 0, i, k, result = 0;

    portal_gpu_frames(t);
    box_render_sync(t->world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(t->world, AppStatus)->tmp_dir,
        PORTAL_GPU_FILE);
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, PORTAL_GPU_SIZE);
    test_int(h, PORTAL_GPU_SIZE);

    for (i = 0; i < w * h; i ++) {
        bool lit = false;

        for (k = 0; k < 3; k ++) {
            lit = lit || px[i * 4 + k] > 120;
        }

        result += lit;
    }

    stbi_image_free(px);
    ecs_os_free(path);

    return result;
}

/** Free a world.
 *
 * @param t The world
 */
static void portal_gpu_fini(
    portal_gpu_t *t)
{
    ecs_fini(t->world);
}

void Portal_status_follows_camera(void) {
    // command/portal
    portal_gpu_t t;
    portal_gpu_init(&t);

    char *out = portal_gpu_run(&t, "portal.cull on");
    ecs_os_free(out);
    portal_gpu_frames(&t);

    const PortalStatus *status = ecs_get(t.world, t.view, PortalStatus);
    test_assert(status != NULL);
    test_assert(status->area == ecs_lookup(t.world, "rooms.hall"));
    test_assert(status->entries >= 2);
    test_bool(status->culling, true);
    test_bool(status->frozen, false);

    out = portal_gpu_run(&t, "portal");
    test_assert(strstr(out, "camera area hall") != NULL);
    ecs_os_free(out);

    out = portal_gpu_run(&t, "portal.tree");
    test_assert(strstr(out, "PORTAL TREE") != NULL);
    test_assert(strstr(out, "\nhall") != NULL);
    test_assert(strstr(out, "- door side") != NULL);
    ecs_os_free(out);

    ecs_set(t.world, t.camera, Position, {0, 1.6f, 15});
    portal_gpu_frames(&t);
    status = ecs_get(t.world, t.view, PortalStatus);
    test_assert(status->area == ecs_lookup(t.world, "rooms.side"));
    out = portal_gpu_run(&t, "portal");
    test_assert(strstr(out, "camera area side") != NULL);
    ecs_os_free(out);

    portal_gpu_fini(&t);
}

void Portal_freeze_keeps_traversal(void) {
    // command/portal
    portal_gpu_t t;
    portal_gpu_init(&t);

    portal_gpu_frames(&t);
    char *out = portal_gpu_run(&t, "portal.freeze on");
    test_str(out, "portal.freeze on");
    ecs_os_free(out);
    portal_gpu_frames(&t);

    ecs_set(t.world, t.camera, Position, {0, 1.6f, 15});
    portal_gpu_frames(&t);
    const PortalStatus *status = ecs_get(t.world, t.view, PortalStatus);
    test_assert(status != NULL);
    test_bool(status->frozen, true);
    test_assert(status->area == ecs_lookup(t.world, "rooms.hall"));
    out = portal_gpu_run(&t, "portal");
    test_assert(strstr(out, "camera area hall  [frozen]") != NULL);
    ecs_os_free(out);

    out = portal_gpu_run(&t, "portal.freeze off");
    ecs_os_free(out);
    portal_gpu_frames(&t);
    status = ecs_get(t.world, t.view, PortalStatus);
    test_bool(status->frozen, false);
    test_assert(status->area == ecs_lookup(t.world, "rooms.side"));

    portal_gpu_fini(&t);
}

void Portal_lines_are_drawn(void) {
    // command/portal
    portal_gpu_t t;
    portal_gpu_init(&t);

    portal_gpu_frames(&t);
    int32_t base = portal_gpu_count(&t);

    char *out = portal_gpu_run(&t, "portal.lines on");
    ecs_os_free(out);
    portal_gpu_frames(&t);
    test_assert(portal_gpu_count(&t) > base + 20);

    out = portal_gpu_run(&t, "portal.lines off");
    ecs_os_free(out);
    portal_gpu_frames(&t);
    test_int(portal_gpu_count(&t), base);

    portal_gpu_fini(&t);
}

void Portal_lines_without_culling(void) {
    // command/portal
    portal_gpu_t t;
    portal_gpu_init(&t);

    char *out = portal_gpu_run(&t, "portal.cull off");
    test_str(out, "portal.cull off");
    ecs_os_free(out);
    out = portal_gpu_run(&t, "portal.lines on");
    ecs_os_free(out);
    portal_gpu_frames(&t);

    const PortalStatus *status = ecs_get(t.world, t.view, PortalStatus);
    test_assert(status != NULL);
    test_bool(status->culling, false);
    int32_t lit = portal_gpu_count(&t);

    out = portal_gpu_run(&t, "portal.lines off");
    ecs_os_free(out);
    portal_gpu_frames(&t);
    test_assert(portal_gpu_count(&t) + 20 < lit);
    test_assert(!ecs_has(t.world, t.view, PortalStatus));
    out = portal_gpu_run(&t, "portal");
    test_assert(strstr(out, "no traversal") != NULL);
    ecs_os_free(out);

    portal_gpu_fini(&t);
}

void Portal_areas_and_occluders_are_drawn(void) {
    // command/portal
    portal_gpu_t t;
    portal_gpu_init(&t);

    ecs_entity_t screen = ecs_entity(t.world, { .name = "rooms.screen" });
    ecs_set(t.world, screen, Position, {0, 1.5f, 6});
    ecs_set(t.world, screen, VisOccluder, {.size = {1, 1}, .normal = {0, 0, -1}});
    portal_gpu_frames(&t);
    int32_t base = portal_gpu_count(&t);

    char *out = portal_gpu_run(&t, "portal.areas on");
    ecs_os_free(out);
    portal_gpu_frames(&t);
    int32_t areas = portal_gpu_count(&t);
    test_assert(areas > base + 20);

    out = portal_gpu_run(&t, "portal.areas off");
    ecs_os_free(out);
    portal_gpu_frames(&t);
    test_int(portal_gpu_count(&t), base);

    out = portal_gpu_run(&t, "portal.occluders on");
    test_str(out, "portal.occluders on");
    ecs_os_free(out);
    portal_gpu_frames(&t);
    test_assert(portal_gpu_count(&t) > base + 10);

    portal_gpu_fini(&t);
}
