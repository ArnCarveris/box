/**
 * @file PlanetSurfaceLight.c
 * @brief Tests for the sunlight of a PlanetAtmosphere on its meshes: the terminator of a white
 * planet turns red while its subsolar point stays white, through the mesh shader, the blended
 * cloud sphere, the extended mesh shader and transmission; a mesh that is not a descendant and a
 * scene without PlanetAtmosphere are lit as before. Checked on PNG captures.
 *
 * The planet is a white sphere of radius 1 with the Earth medium, 6360 km, whose shell draws no
 * light (intensity 0), so only the attenuation of the sun colors the surface; the shell darkens
 * the image by a gray transmittance only. The camera looks down -Z from Z 4 with a 60 degree
 * field of view over a black clear color, without ambient light, and a white sun lights the
 * planet from the right, half way between +X and +Z.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge of the square captures */
#define PS_SIZE (128)

/** Z of the camera */
#define PS_CAMERA_Z (4.0f)

/** Intensity of the sun */
#define PS_SUN (3.0f)

/** Cosine of the angle between the normal and the sun of the sample near the terminator */
#define PS_TERMINATOR_MU (0.2f)

/** Create a headless world that renders on the main thread, without ambient light, with the
 * Scene features of the cases and a white sun toward +X and +Z.
 *
 * @param planet_light Scene.planet_light
 * @return The world
 */
static ecs_world_t* ps_world(
    bool planet_light)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.alpha_cutout = true, .blend_modes = true,
        .transmission = true, .clear_coat = true, .planet_light = planet_light});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {255, 255, 255}, .ground = {255, 255, 255},
        .intensity = 0});
    ecs_entity_t sun = ecs_new(world);
    ecs_set_name(world, sun, "sun");
    ecs_set(world, sun, Rotation, {0, glm_rad(-135.0f), 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = PS_SUN});
    ecs_set(world, sun, Rgb, {255, 255, 255});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, PS_CAMERA_Z});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.01f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    return world;
}

/** Add a white sphere of radius 1 at the origin.
 *
 * @param world The world
 * @param parent Its parent, 0 for none
 * @return The sphere
 */
static ecs_entity_t ps_sphere(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Sphere, {.segments = 96, .smooth = true, .radius = 1});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});

    return e;
}

/** Give an entity a PlanetAtmosphere of radius 1 with the Earth medium whose shell draws no
 * light.
 *
 * @param world The world
 * @param planet The entity
 */
static void ps_atmosphere(
    ecs_world_t *world,
    ecs_entity_t planet)
{
    ecs_add(world, planet, PlanetAtmosphere);
    PlanetAtmosphere *a = ecs_get_mut(world, planet, PlanetAtmosphere);
    a->radius = 1;
    a->intensity = 0;
    ecs_modified(world, planet, PlanetAtmosphere);
}

/** Run frames and read the counters of the GPU backend after them.
 *
 * @param world The world
 * @param count Frames to run
 * @return The counters, all 0 before the first frame with a surface
 */
static box_render_gpu_stats_t ps_frames(
    ecs_world_t *world,
    int32_t count)
{
    box_render_gpu_stats_t stats;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &stats) || !count, true);

    return stats;
}

/** Render frames into a square capture and load it, without new device errors.
 *
 * @param world The world
 * @param file Name of the PNG in the temporary directory
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* ps_render(
    ecs_world_t *world,
    const char *file)
{
    box_render_gpu_stats_t before = ps_frames(world, 0), after;
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = PS_SIZE, .height = PS_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    after = ps_frames(world, 4);
    test_int(after.errors, before.errors);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, PS_SIZE);
    test_int(h, PS_SIZE);
    ecs_os_free(path);

    return px;
}

/** Get the direction toward the sun from the world transform of the light, which shines along
 * its local +Z.
 *
 * @param world The world, after a frame
 * @param out Receives the normalized direction
 */
static void ps_sun_direction(
    ecs_world_t *world,
    vec3 out)
{
    ecs_entity_t sun = ecs_lookup(world, "sun");
    const WorldTransform *t = ecs_get(world, sun, WorldTransform);
    test_assert(t != NULL);
    glm_vec3_negate_to(ECS_CONST_CAST(float*, t->m[2]), out);
    glm_vec3_normalize(out);
}

/** Get the column of the image of a point of the sphere on the plane y = 0.
 *
 * @param n The point, its normal
 * @return The column
 */
static int32_t ps_column(
    const vec3 n)
{
    float ndc = n[0] / ((PS_CAMERA_Z - n[2]) * tanf(glm_rad(30.0f)));

    return (int32_t)floorf((ndc * 0.5f + 0.5f) * (float)PS_SIZE);
}

/** Get the ratio of red to blue of the mean of the 3x3 pixels around a pixel of the middle row.
 *
 * @param px The pixels
 * @param x Column
 * @param label Printed with the mean
 * @return The ratio
 */
static float ps_ratio(
    const unsigned char *px,
    int32_t x,
    const char *label)
{
    float sum[3] = {0, 0, 0};
    int32_t dx, dy, c;
    for (dy = -1; dy <= 1; dy ++) {
        for (dx = -1; dx <= 1; dx ++) {
            const unsigned char *p = &px[((PS_SIZE / 2 + dy) * PS_SIZE + x + dx) * 4];
            for (c = 0; c < 3; c ++) {
                sum[c] += (float)p[c] / 9.0f;
            }
        }
    }

    printf("%s: %.1f %.1f %.1f\n", label, (double)sum[0], (double)sum[1], (double)sum[2]);
    test_assert(sum[0] > 8.0f);
    test_assert(sum[2] > 2.0f);

    return sum[0] / sum[2];
}

/** Render a scene and measure the red to blue ratios of the subsolar point and of the point near
 * the terminator on the plane y = 0, where the cosine to the sun is PS_TERMINATOR_MU.
 *
 * @param world The world
 * @param file Name of the PNG
 * @param subsolar Receives the ratio at the subsolar point
 * @param terminator Receives the ratio near the terminator
 */
static void ps_ratios(
    ecs_world_t *world,
    const char *file,
    float *subsolar,
    float *terminator)
{
    unsigned char *px = ps_render(world, file);
    vec3 s, n;
    ps_sun_direction(world, s);
    test_assert(s[2] > 0.3f);
    float angle = atan2f(s[2], s[0]);
    float a = angle + acosf(PS_TERMINATOR_MU);
    float b = angle - acosf(PS_TERMINATOR_MU);
    float t = sinf(a) > sinf(b) ? a : b;
    n[0] = cosf(t);
    n[1] = 0;
    n[2] = sinf(t);
    *terminator = ps_ratio(px, ps_column(n), "terminator");
    s[1] = 0;
    glm_vec3_normalize(s);
    *subsolar = ps_ratio(px, ps_column(s), "subsolar");
    stbi_image_free(px);
}

void PlanetSurfaceLight_terminator_reddens(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(true);
    ecs_entity_t planet = ps_sphere(world, 0);
    ps_atmosphere(world, planet);
    float subsolar, terminator;
    ps_ratios(world, "planet_light_terminator.png", &subsolar, &terminator);
    test_assert(terminator > subsolar * 1.25f);
    test_assert(terminator > 1.3f);
    ecs_fini(world);
}

void PlanetSurfaceLight_without_atmosphere_neutral(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(true);
    ps_sphere(world, 0);
    float subsolar, terminator;
    ps_ratios(world, "planet_light_bare.png", &subsolar, &terminator);
    test_assert(fabsf(terminator - subsolar) < 0.08f);
    test_assert(fabsf(subsolar - 1) < 0.08f);
    ecs_fini(world);
}

void PlanetSurfaceLight_feature_off_neutral(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(false);
    ecs_entity_t planet = ps_sphere(world, 0);
    ps_atmosphere(world, planet);
    float subsolar, terminator;
    ps_ratios(world, "planet_light_off.png", &subsolar, &terminator);
    test_assert(fabsf(terminator - subsolar) < 0.08f);
    ecs_fini(world);
}

void PlanetSurfaceLight_not_descendant_unaffected(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(true);
    ecs_entity_t planet = ecs_new(world);
    ecs_set(world, planet, Position, {0, 0, 0});
    ps_atmosphere(world, planet);
    ps_sphere(world, 0);
    float subsolar, terminator;
    ps_ratios(world, "planet_light_sibling.png", &subsolar, &terminator);
    test_assert(fabsf(terminator - subsolar) < 0.08f);
    ecs_fini(world);
}

void PlanetSurfaceLight_child_affected(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(true);
    ecs_entity_t planet = ecs_new(world);
    ecs_set(world, planet, Position, {0, 0, 0});
    ps_atmosphere(world, planet);
    ps_sphere(world, planet);
    float subsolar, terminator;
    ps_ratios(world, "planet_light_child.png", &subsolar, &terminator);
    test_assert(terminator > subsolar * 1.25f);
    ecs_fini(world);
}

void PlanetSurfaceLight_no_planet_unchanged(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(false);
    ps_sphere(world, 0);
    unsigned char *off = ps_render(world, "planet_light_none_off.png");
    ecs_fini(world);

    world = ps_world(true);
    ps_sphere(world, 0);
    unsigned char *on = ps_render(world, "planet_light_none_on.png");
    int32_t i, c, diff = 0;
    for (i = 0; i < PS_SIZE * PS_SIZE; i ++) {
        for (c = 0; c < 3; c ++) {
            int32_t d = abs((int32_t)off[i * 4 + c] - (int32_t)on[i * 4 + c]);
            diff = d > diff ? d : diff;
        }
    }

    printf("max difference: %d\n", diff);
    test_assert(diff <= 1);
    stbi_image_free(off);
    stbi_image_free(on);
    ecs_fini(world);
}

void PlanetSurfaceLight_cloud_sphere_reddens(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(true);
    ecs_entity_t planet = ps_sphere(world, 0);
    ecs_set(world, planet, Rgb, {0, 0, 0});
    ps_atmosphere(world, planet);
    ecs_entity_t clouds = ps_sphere(world, planet);
    ecs_set(world, clouds, Sphere, {.segments = 96, .smooth = true, .radius = 1.005f});
    ecs_set(world, clouds, Opacity, {0.6f});
    float subsolar, terminator;
    ps_ratios(world, "planet_light_clouds.png", &subsolar, &terminator);
    test_assert(terminator > subsolar * 1.25f);
    ecs_fini(world);
}

void PlanetSurfaceLight_extended_reddens(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(true);
    ecs_entity_t planet = ps_sphere(world, 0);
    ecs_set(world, planet, ClearCoat, {.strength = 0.3f, .roughness = 0.9f});
    ps_atmosphere(world, planet);
    float subsolar, terminator;
    ps_ratios(world, "planet_light_extended.png", &subsolar, &terminator);
    test_assert(terminator > subsolar * 1.25f);
    ecs_fini(world);
}

void PlanetSurfaceLight_transmission_reddens(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(true);
    ecs_entity_t planet = ps_sphere(world, 0);
    ecs_set(world, planet, Transmission, {.transmission_factor = 0.4f, .ior = 1.5f,
        .thickness_factor = 0.1f, .attenuation_distance = 1,
        .attenuation_color = {255, 255, 255}});
    ps_atmosphere(world, planet);
    float subsolar, terminator;
    ps_ratios(world, "planet_light_transmission.png", &subsolar, &terminator);
    test_assert(terminator > subsolar * 1.25f);
    ecs_fini(world);
}
