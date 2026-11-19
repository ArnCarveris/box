/**
 * @file Portal.c
 * @brief Tests for the portal commands on a rendered world of test/assets/portal/rooms.flecs:
 * the traversal of the view they set, and the lines they draw, checked on the captured image,
 * and the water a capture of the view keeps out of the rooms by its stencil.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
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

/** Fill the rooms and the outdoors with water up to y 1: green water over a black floor.
 *
 * @param t The world
 */
static void portal_gpu_flood(
    portal_gpu_t *t)
{
    ecs_world_t *world = t->world;
    ecs_singleton_set(world, Scene, {.transmission = true, .water = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {255, 255, 255},
        .ground = {255, 255, 255}, .intensity = 1});

    ecs_entity_t sea = ecs_entity(world, { .name = "sea" });
    ecs_set(world, sea, Position, {0, 1, 0});
    ecs_set(world, sea, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, sea, Quad, {200, 200});
    ecs_set(world, sea, Rgb, {255, 0, 255});
    ecs_set(world, sea, PbrMaterial, {.metallic = 0, .roughness = 0.3f});
    ecs_set(world, sea, Water, {
        .shallow_color = {0, 255, 0},
        .deep_color = {0, 255, 0},
        .depth = 3,
        .visibility = 0.001f,
        .foam_color = {255, 255, 255},
        .foam_scale = 1
    });
}

/** What the ray of a pixel of a look through the entry of the hall meets */
typedef enum portal_gpu_ray_t {
    PortalGpuRayOther = 0,  /**< Something else, or too close to an edge to tell */
    PortalGpuRayHall,       /**< The water in the hall, through the entry */
    PortalGpuRayOutdoors,   /**< The water outdoors, before the entry */
    PortalGpuRays           /**< Kinds of rays */
} portal_gpu_ray_t;

/** Tell what the ray of a pixel of a camera meets: the water at y 1 in the hall through the
 * entry, x -1 to 1 and y 0 to 2.4 at z 0, or outdoors away from the entry. The camera has a
 * vertical field of view of 60 degrees and a square image.
 *
 * Water outdoors on the way to the entry does not count: the mark of the entry tests the depth
 * of what is opaque, which the water is not.
 *
 * @param at Position of the camera
 * @param pitch Pitch of the camera
 * @param yaw Yaw of the camera
 * @param x Column of the pixel
 * @param y Row of the pixel, 0 at the top
 * @return What the ray meets
 */
static portal_gpu_ray_t portal_gpu_ray(
    const vec3 at,
    float pitch,
    float yaw,
    int32_t x,
    int32_t y)
{
    portal_gpu_ray_t result = PortalGpuRayOther;
    float spread = tanf(GLM_PIf / 6.0f);
    float u = (2.0f * ((float)x + 0.5f) / PORTAL_GPU_SIZE - 1.0f) * spread;
    float v = (1.0f - 2.0f * ((float)y + 0.5f) / PORTAL_GPU_SIZE) * spread;
    float dy = v * cosf(pitch) + sinf(pitch);
    float dz0 = v * sinf(pitch) - cosf(pitch);
    float dx = u * cosf(yaw) + dz0 * sinf(yaw);
    float dz = -u * sinf(yaw) + dz0 * cosf(yaw);
    if (dy >= -0.05f || dz <= 0) {
        goto done;
    }

    float k = (1.0f - at[1]) / dy;
    float hit_x = at[0] + dx * k;
    float hit_z = at[2] + dz * k;
    float door = -at[2] / dz;
    float door_x = at[0] + dx * door;
    float door_y = at[1] + dy * door;
    bool near = fabsf(door_x) < 1.2f && door_y > -0.2f && door_y < 2.6f;
    bool through = fabsf(door_x) < 0.8f && door_y > 0.2f && door_y < 2.2f;
    if (!near && hit_z < -0.3f && k < 20.0f) {
        result = PortalGpuRayOutdoors;
    } else if (through && hit_z > 0.3f && hit_z < 9.5f && fabsf(hit_x) < 4.5f) {
        result = PortalGpuRayHall;
    }

done:
    return result;
}

/** Place a camera, capture it and count the pixels the water colors.
 *
 * @param t The world
 * @param camera The camera
 * @param at Position of the camera
 * @param pitch Pitch of the camera
 * @param yaw Yaw of the camera, 0 looking towards -z and pi towards +z
 * @param file Name of the PNG in the temporary folder of the app
 * @param rays Receives per portal_gpu_ray_t its pixels, NULL for none
 * @param water Receives per portal_gpu_ray_t its pixels the water colors, NULL for none
 * @return Pixels whose green outweighs red and blue
 */
static int32_t portal_gpu_water_at(
    portal_gpu_t *t,
    ecs_entity_t camera,
    const vec3 at,
    float pitch,
    float yaw,
    const char *file,
    int32_t *rays,
    int32_t *water)
{
    int w = 0, h = 0, n = 0, i, result = 0;
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(t->world, AppStatus)->tmp_dir, file);

    ecs_set(t->world, camera, Position, {at[0], at[1], at[2]});
    ecs_set(t->world, camera, Rotation, {pitch, yaw, 0});
    portal_gpu_frames(t);
    test_int(box_render_capture(t->world, &(box_render_capture_desc_t){
        .camera = camera,
        .width = PORTAL_GPU_SIZE,
        .height = PORTAL_GPU_SIZE,
        .path = path
    }), 0);
    portal_gpu_frames(t);
    box_render_sync(t->world);

    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, PORTAL_GPU_SIZE);

    for (i = 0; i < w * h; i ++) {
        const unsigned char *c = &px[i * 4];
        bool wet = c[1] > 60 && c[1] > 2 * c[0] && c[1] > 2 * c[2];
        result += wet;
        if (rays && water) {
            portal_gpu_ray_t ray = portal_gpu_ray(at, pitch, yaw, i % w, i / w);
            rays[ray] ++;
            water[ray] += wet;
        }
    }

    stbi_image_free(px);
    ecs_os_free(path);

    return result;
}

/** Look down from a point with the camera of the view, capture it and count the pixels the
 * water colors.
 *
 * @param t The world
 * @param x X of the camera
 * @param z Z of the camera
 * @param file Name of the PNG in the temporary folder of the app
 * @return Pixels whose green outweighs red and blue
 */
static int32_t portal_gpu_water(
    portal_gpu_t *t,
    float x,
    float z,
    const char *file)
{
    vec3 at = {x, 2.5f, z};

    return portal_gpu_water_at(t, t->camera, at, -1.2f, 0, file, NULL, NULL);
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

void Portal_capture_keeps_water_out_of_rooms(void) {
    // command/portal
    portal_gpu_t t;
    portal_gpu_init(&t);
    portal_gpu_flood(&t);

    int32_t outdoors = portal_gpu_water(&t, 0, -6, "portal_water_out.png");
    int32_t hall = portal_gpu_water(&t, 0, 5, "portal_water_hall.png");
    printf("water pixels: outdoors %d, hall %d\n", outdoors, hall);
    test_assert(outdoors > PORTAL_GPU_SIZE * PORTAL_GPU_SIZE / 2);
    test_int(hall, 0);

    portal_gpu_fini(&t);
}

/** Look into the hall from outdoors through its entry, capture it and check that the water
 * colors the pixels whose rays meet it outdoors and none of those that meet it in the hall.
 *
 * @param t The world
 * @param camera The camera
 * @param file Name of the PNG in the temporary folder of the app
 */
static void portal_gpu_entry_dry(
    portal_gpu_t *t,
    ecs_entity_t camera,
    const char *file)
{
    vec3 at = {0, 2.2f, -4};
    int32_t rays[PortalGpuRays] = {0};
    int32_t water[PortalGpuRays] = {0};

    portal_gpu_water_at(t, camera, at, -0.3f, GLM_PIf, file, rays, water);
    printf("%s: hall %d of %d wet, outdoors %d of %d wet\n", file, water[PortalGpuRayHall],
        rays[PortalGpuRayHall], water[PortalGpuRayOutdoors], rays[PortalGpuRayOutdoors]);
    test_assert(rays[PortalGpuRayHall] > 200);
    test_assert(rays[PortalGpuRayOutdoors] > 1000);
    test_int(water[PortalGpuRayHall], 0);
    test_assert(water[PortalGpuRayOutdoors] * 10 > rays[PortalGpuRayOutdoors] * 9);
}

void Portal_bare_camera_capture_keeps_water_out_of_rooms(void) {
    // command/portal: a camera that no view renders sees through the portals itself, so its
    // capture keeps the water out of the hall, seen from outdoors through the entry, as the
    // capture of a view does
    portal_gpu_t t;
    portal_gpu_init(&t);
    portal_gpu_flood(&t);
    ecs_entity_t bare = ecs_entity(t.world, { .name = "bare" });
    ecs_set(t.world, bare, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});

    portal_gpu_entry_dry(&t, t.camera, "portal_water_view_entry.png");
    portal_gpu_entry_dry(&t, bare, "portal_water_bare_entry.png");

    vec3 above = {0, 2.5f, 5};
    test_int(portal_gpu_water_at(&t, bare, above, -1.2f, 0, "portal_water_bare_hall.png", NULL,
        NULL), 0);

    portal_gpu_fini(&t);
}
