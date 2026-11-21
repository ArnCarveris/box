/**
 * @file PlanetAtmosphereShell.c
 * @brief Tests for the atmosphere shells of PlanetAtmosphere: the limb glow of the day side, the
 * dark night side, the haze over the surface, nothing outside the top sphere or without
 * scattering, geometry in front of a planet, two planets with different media, a camera
 * inside the atmosphere and a smooth twilight past the terminator; checked on PNG captures.
 *
 * The planets have a ground radius of 1 world unit, 300 km, and a 60 km atmosphere, so the top
 * sphere is 1.2 units; the camera looks down -Z with a 60 degree field of view over a black
 * clear color, without ambient light, and a white sun of intensity 8 lights the planets.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge of the square captures */
#define PA_SIZE (128)

/** Intensity of the sun */
#define PA_SUN (8.0f)

/** Ground radius of the planets in km */
#define PA_PLANET_KM (300.0f)

/** Atmosphere thickness of the planets in km */
#define PA_THICKNESS_KM (60.0f)

/** Ratio of the closest distance of a limb ray to the center over the ground radius, 6 km above
 * the ground */
#define PA_LIMB (1.02f)

/** Rows on each side of the center row that PlanetAtmosphereShell_terminator_smooth averages,
 * which evens out the per pixel jitter of the samples but keeps stripes parallel to the
 * terminator */
#define PA_TWILIGHT_ROWS (8)

/** Brightness, the sum of the color channels, below which the twilight is too faint to check */
#define PA_TWILIGHT_FAINT (30.0f)

/** Largest change of the fall of the twilight brightness between neighboring columns */
#define PA_TWILIGHT_STEP (5.0f)

/** Create a headless world that renders on the main thread, without ambient light, with a white
 * sun turned by a pitch and a yaw.
 *
 * @param pitch Rotation of the sun about +X in degrees
 * @param yaw Rotation of the sun about +Y in degrees
 * @return The world
 */
static ecs_world_t* pa_world(
    float pitch,
    float yaw)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {255, 255, 255}, .ground = {255, 255, 255},
        .intensity = 0});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {glm_rad(pitch), glm_rad(yaw), 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = PA_SUN});
    ecs_set(world, sun, Rgb, {255, 255, 255});

    return world;
}

/** Add a lit view of a perspective camera at a position, turned by a pitch, that clears to
 * black.
 *
 * @param world The world
 * @param z Z of the camera
 * @param pitch Rotation of the camera about +X in degrees, 0 looks down -Z
 * @return The view
 */
static ecs_entity_t pa_view(
    ecs_world_t *world,
    float z,
    float pitch)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, z});
    ecs_set(world, camera, Rotation, {glm_rad(pitch), 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.01f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    return view;
}

/** Add a gray sphere of radius 1, the ground of a planet.
 *
 * @param world The world
 * @param x X of its center
 * @param y Y of its center
 * @return The planet
 */
static ecs_entity_t pa_planet(
    ecs_world_t *world,
    float x,
    float y)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, 0});
    ecs_set(world, e, Sphere, {.segments = 96, .smooth = true, .radius = 1});
    ecs_set(world, e, Rgb, {128, 128, 128});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});

    return e;
}

/** Give a planet the default PlanetAtmosphere of the cases: radius 1 for PA_PLANET_KM and a
 * PA_THICKNESS_KM atmosphere.
 *
 * @param world The world
 * @param planet The planet
 * @return The values, to change and set again
 */
static PlanetAtmosphere* pa_atmosphere(
    ecs_world_t *world,
    ecs_entity_t planet)
{
    ecs_add(world, planet, PlanetAtmosphere);
    PlanetAtmosphere *a = ecs_get_mut(world, planet, PlanetAtmosphere);
    a->radius = 1;
    a->planet_radius = PA_PLANET_KM;
    a->thickness = PA_THICKNESS_KM;
    ecs_modified(world, planet, PlanetAtmosphere);

    return ecs_get_mut(world, planet, PlanetAtmosphere);
}

/** Add an emissive gray bar that faces the camera.
 *
 * @param world The world
 * @param z Z of its center
 * @param width Width along X
 * @param height Height along Y
 * @return The bar
 */
static ecs_entity_t pa_bar(
    ecs_world_t *world,
    float z,
    float width,
    float height)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, z});
    ecs_set(world, e, Box, {width, height, 0.02f});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {150, 150, 150}});

    return e;
}

/** Run frames and read the counters of the GPU backend after them.
 *
 * @param world The world
 * @param count Frames to run
 * @return The counters, all 0 before the first frame with a surface
 */
static box_render_gpu_stats_t pa_frames(
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
static unsigned char* pa_render(
    ecs_world_t *world,
    const char *file)
{
    box_render_gpu_stats_t before = pa_frames(world, 0), after;
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = PA_SIZE, .height = PA_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    after = pa_frames(world, 4);
    test_int(after.errors, before.errors);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, PA_SIZE);
    test_int(h, PA_SIZE);
    ecs_os_free(path);

    return px;
}

/** Get a pixel of a capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return The pixel, RGBA
 */
static const unsigned char* pa_px(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return &px[(y * PA_SIZE + x) * 4];
}

/** Get the largest difference of the color channels of two pixels.
 *
 * @param a The first pixel
 * @param b The second pixel
 * @return The difference
 */
static int32_t pa_diff(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t c, result = 0;
    for (c = 0; c < 3; c ++) {
        int32_t d = abs((int32_t)a[c] - (int32_t)b[c]);
        result = d > result ? d : result;
    }

    return result;
}

/** Print a pixel with a label.
 *
 * @param label The label
 * @param p The pixel
 */
static void pa_print(
    const char *label,
    const unsigned char *p)
{
    printf("%s: %d %d %d\n", label, p[0], p[1], p[2]);
}

/** Get the closest distance to a point of the ray of a pixel of a camera at (0, 0, z) that looks
 * down -Z with a 60 degree field of view.
 *
 * @param z Z of the camera
 * @param x Column of the pixel
 * @param y Row of the pixel
 * @param center The point
 * @return The distance, a large one for a point behind the camera
 */
static float pa_ray_distance(
    float z,
    int32_t x,
    int32_t y,
    const float *center)
{
    float t = tanf(glm_rad(30.0f));
    vec3 dir = {
        (((float)x + 0.5f) / PA_SIZE * 2.0f - 1.0f) * t,
        (1.0f - ((float)y + 0.5f) / PA_SIZE * 2.0f) * t,
        -1.0f
    };
    vec3 rel = {center[0], center[1], center[2] - z};
    float result = 1e9f;
    glm_vec3_normalize(dir);
    float along = glm_vec3_dot(rel, dir);
    if (along > 0) {
        vec3 closest;
        glm_vec3_scale(dir, along, closest);
        result = glm_vec3_distance(rel, closest);
    }

    return result;
}

/** Find the first pixel from the projection of a planet center outward along a step whose ray
 * passes at least a distance from the center.
 *
 * @param z Z of the camera at (0, 0, z) looking down -Z
 * @param cx X of the center
 * @param cy Y of the center, which is at Z 0
 * @param dx Step along the columns
 * @param dy Step along the rows
 * @param distance The distance
 * @param out Receives the column and the row
 */
static void pa_limb(
    float z,
    float cx,
    float cy,
    int32_t dx,
    int32_t dy,
    float distance,
    int32_t *out)
{
    const float center[3] = {cx, cy, 0};
    float t = tanf(glm_rad(30.0f));
    int32_t x = (int32_t)((cx / z / t + 1.0f) * 0.5f * PA_SIZE);
    int32_t y = (int32_t)((1.0f - cy / z / t) * 0.5f * PA_SIZE);
    while (x > 0 && x < PA_SIZE - 1 && y > 0 && y < PA_SIZE - 1 &&
        pa_ray_distance(z, x, y, center) < distance)
    {
        x += dx;
        y += dy;
    }

    test_assert(pa_ray_distance(z, x, y, center) >= distance);
    out[0] = x;
    out[1] = y;
}

/** Render the day case: a planet at the origin lit from +X and +Z, seen from z 3.
 *
 * @param atmosphere Whether the planet has a PlanetAtmosphere
 * @param file Name of the PNG
 * @return The pixels
 */
static unsigned char* pa_day(
    bool atmosphere,
    const char *file)
{
    ecs_world_t *world = pa_world(0, -135);
    pa_view(world, 3, 0);
    ecs_entity_t planet = pa_planet(world, 0, 0);
    if (atmosphere) {
        pa_atmosphere(world, planet);
    }

    unsigned char *result = pa_render(world, file);
    ecs_fini(world);

    return result;
}

/** With the default Rayleigh scattering the limb of the day side, just outside the ground, is
 * brighter than the black background and blue dominant. */
void PlanetAtmosphereShell_day_limb_blue(void) {
    // planet_atmosphere/shell
    int32_t limb[2];
    unsigned char *px = pa_day(true, "planet_day_limb.png");
    pa_limb(3, 0, 0, 1, 0, PA_LIMB, limb);
    const unsigned char *p = pa_px(px, limb[0], limb[1]);
    pa_print("day limb", p);
    test_assert(p[0] + p[1] + p[2] > 60);
    test_assert(p[2] > p[0] + 10);
    test_assert(p[2] >= p[1]);
    stbi_image_free(px);
}

/** A pixel whose ray passes outside the top sphere keeps the background. */
void PlanetAtmosphereShell_outside_top_unchanged(void) {
    // planet_atmosphere/shell
    int32_t out[2];
    unsigned char *px = pa_day(true, "planet_outside.png");
    unsigned char *base = pa_day(false, "planet_outside_base.png");
    pa_limb(3, 0, 0, 1, 0, 1.3f, out);
    test_assert(pa_diff(pa_px(px, out[0], out[1]), pa_px(base, out[0], out[1])) <= 1);
    test_assert(pa_px(px, out[0], out[1])[2] <= 1);
    pa_limb(3, 0, 0, 0, -1, 1.3f, out);
    test_assert(pa_diff(pa_px(px, out[0], out[1]), pa_px(base, out[0], out[1])) <= 1);
    test_assert(pa_diff(pa_px(px, 2, 2), pa_px(base, 2, 2)) <= 1);
    stbi_image_free(px);
    stbi_image_free(base);
}

/** The limb of the night side, away from the sun, stays dark. The sun lights the planet from +X,
 * so the whole ray of the -X limb runs through the shadow of the planet. */
void PlanetAtmosphereShell_night_limb_dark(void) {
    // planet_atmosphere/shell
    int32_t day[2], night[2];
    ecs_world_t *world = pa_world(0, -90);
    pa_view(world, 3, 0);
    pa_atmosphere(world, pa_planet(world, 0, 0));
    unsigned char *px = pa_render(world, "planet_night_limb.png");
    ecs_fini(world);
    pa_limb(3, 0, 0, 1, 0, PA_LIMB, day);
    pa_limb(3, 0, 0, -1, 0, PA_LIMB, night);
    const unsigned char *d = pa_px(px, day[0], day[1]);
    const unsigned char *n = pa_px(px, night[0], night[1]);
    pa_print("day limb", d);
    pa_print("night limb", n);
    test_assert(n[0] <= 6 && n[1] <= 6 && n[2] <= 8);
    test_assert(d[2] > 4 * (n[2] + 1));
    stbi_image_free(px);
}

/** The lit surface at the center of the disk is hazed: it differs from the same scene without
 * the component. */
void PlanetAtmosphereShell_surface_hazed(void) {
    // planet_atmosphere/shell
    unsigned char *px = pa_day(true, "planet_surface.png");
    unsigned char *base = pa_day(false, "planet_surface_base.png");
    const unsigned char *p = pa_px(px, PA_SIZE / 2, PA_SIZE / 2);
    const unsigned char *b = pa_px(base, PA_SIZE / 2, PA_SIZE / 2);
    pa_print("hazed", p);
    pa_print("bare", b);
    test_assert(b[0] > 20);
    test_assert(pa_diff(p, b) >= 3);
    test_assert((int32_t)p[2] - (int32_t)b[2] > (int32_t)p[0] - (int32_t)b[0]);
    stbi_image_free(px);
    stbi_image_free(base);
}

/** Without Rayleigh and Mie scattering and ozone the shell renders nothing. */
void PlanetAtmosphereShell_no_scattering_unchanged(void) {
    // planet_atmosphere/shell
    ecs_world_t *world = pa_world(0, -135);
    pa_view(world, 3, 0);
    ecs_entity_t planet = pa_planet(world, 0, 0);
    PlanetAtmosphere *a = pa_atmosphere(world, planet);
    a->rayleigh.scattering_scale = 0;
    a->mie.scattering_scale = 0;
    a->ozone_scale = 0;
    ecs_modified(world, planet, PlanetAtmosphere);
    unsigned char *px = pa_render(world, "planet_no_scattering.png");
    ecs_fini(world);
    unsigned char *base = pa_day(false, "planet_no_scattering_base.png");
    int32_t x, y, worst = 0;
    for (y = 0; y < PA_SIZE; y ++) {
        for (x = 0; x < PA_SIZE; x ++) {
            int32_t d = pa_diff(pa_px(px, x, y), pa_px(base, x, y));
            worst = d > worst ? d : worst;
        }
    }

    printf("largest difference %d\n", worst);
    test_assert(worst <= 1);
    stbi_image_free(px);
    stbi_image_free(base);
}

/** Render a planet with a bar in front of it, between the planet and the camera.
 *
 * @param atmosphere Whether the planet has a PlanetAtmosphere
 * @param file Name of the PNG
 * @return The pixels
 */
static unsigned char* pa_front(
    bool atmosphere,
    const char *file)
{
    ecs_world_t *world = pa_world(0, -135);
    pa_view(world, 3, 0);
    ecs_entity_t planet = pa_planet(world, 0, 0);
    if (atmosphere) {
        pa_atmosphere(world, planet);
    }

    pa_bar(world, 2, 3, 0.4f);
    unsigned char *result = pa_render(world, file);
    ecs_fini(world);

    return result;
}

/** An opaque bar in front of the planet is not hazed, the planet around it is. */
void PlanetAtmosphereShell_front_geometry_unchanged(void) {
    // planet_atmosphere/order
    int32_t day[2], night[2];
    unsigned char *px = pa_front(true, "planet_front.png");
    unsigned char *base = pa_front(false, "planet_front_base.png");
    pa_limb(3, 0, 0, 1, 0, PA_LIMB, day);
    pa_limb(3, 0, 0, -1, 0, PA_LIMB, night);
    const unsigned char *p = pa_px(px, PA_SIZE / 2, PA_SIZE / 2);
    pa_print("bar", p);
    test_assert(p[0] > 100);
    test_assert(pa_diff(p, pa_px(base, PA_SIZE / 2, PA_SIZE / 2)) <= 1);
    test_assert(pa_diff(pa_px(px, day[0], day[1]), pa_px(base, day[0], day[1])) <= 1);
    test_assert(pa_diff(pa_px(px, night[0], night[1]), pa_px(base, night[0], night[1])) <= 1);
    test_assert(pa_diff(pa_px(px, PA_SIZE / 2, PA_SIZE / 4), pa_px(base, PA_SIZE / 2,
        PA_SIZE / 4)) >= 2);
    stbi_image_free(px);
    stbi_image_free(base);
}

/** Render the day case with a white, nearly opaque, alpha blended layer: a sphere around the
 * ground inside the atmosphere, such as clouds, or a bar in front of the planet outside it.
 *
 * @param atmosphere Whether the planet has a PlanetAtmosphere
 * @param inside Whether the layer is the sphere of the planet, else the bar
 * @param file Name of the PNG
 * @return The pixels
 */
static unsigned char* pa_layer(
    bool atmosphere,
    bool inside,
    const char *file)
{
    ecs_world_t *world = pa_world(0, -135);
    pa_view(world, 3, 0);
    ecs_entity_t planet = pa_planet(world, 0, 0);
    if (atmosphere) {
        pa_atmosphere(world, planet);
    }

    ecs_entity_t layer;
    if (inside) {
        layer = ecs_new_w_pair(world, EcsChildOf, planet);
        ecs_set(world, layer, Position, {0, 0, 0});
        ecs_set(world, layer, Sphere, {.segments = 96, .smooth = true, .radius = 1.05f});
        ecs_set(world, layer, Rgb, {160, 160, 160});
        ecs_set(world, layer, PbrMaterial, {.metallic = 0, .roughness = 1});
    } else {
        layer = pa_bar(world, 2, 3, 0.4f);
    }

    ecs_set(world, layer, BlendMode, {AlphaBlend});
    ecs_set(world, layer, Opacity, {0.98f});
    unsigned char *result = pa_render(world, file);
    ecs_fini(world);

    return result;
}

/** A blended layer inside the atmosphere, such as clouds, is hazed like the ground, while one in
 * front of the planet is not. */
void PlanetAtmosphereShell_blended_inside_hazed(void) {
    // planet_atmosphere/order
    unsigned char *px = pa_layer(true, true, "planet_layer.png");
    unsigned char *base = pa_layer(false, true, "planet_layer_base.png");
    const unsigned char *p = pa_px(px, PA_SIZE / 2, PA_SIZE / 2);
    const unsigned char *b = pa_px(base, PA_SIZE / 2, PA_SIZE / 2);
    pa_print("hazed layer", p);
    pa_print("bare layer", b);
    test_assert(b[0] > 20);
    test_assert(pa_diff(p, b) >= 3);
    test_assert((int32_t)p[2] - (int32_t)b[2] > (int32_t)p[0] - (int32_t)b[0]);
    stbi_image_free(px);
    stbi_image_free(base);

    px = pa_layer(true, false, "planet_layer_front.png");
    base = pa_layer(false, false, "planet_layer_front_base.png");
    p = pa_px(px, PA_SIZE / 2, PA_SIZE / 2);
    pa_print("front layer", p);
    test_assert(p[0] > 100);
    test_assert(pa_diff(p, pa_px(base, PA_SIZE / 2, PA_SIZE / 2)) <= 1);
    test_assert(pa_diff(pa_px(px, PA_SIZE / 2, PA_SIZE / 4), pa_px(base, PA_SIZE / 2,
        PA_SIZE / 4)) >= 2);
    stbi_image_free(px);
    stbi_image_free(base);
}

/** Render planets lit from the camera side and above, seen from z 6: a blue one at x -1.6 and
 * an orange one without Rayleigh scattering at x 1.6.
 *
 * @param blue Whether the blue planet is in the scene
 * @param orange Whether the orange planet is in the scene
 * @param file Name of the PNG
 * @return The pixels
 */
static unsigned char* pa_two(
    bool blue,
    bool orange,
    const char *file)
{
    ecs_world_t *world = pa_world(45, 180);
    pa_view(world, 6, 0);
    if (blue) {
        pa_atmosphere(world, pa_planet(world, -1.6f, 0));
    }

    if (orange) {
        ecs_entity_t planet = pa_planet(world, 1.6f, 0);
        PlanetAtmosphere *a = pa_atmosphere(world, planet);
        a->rayleigh.scattering_scale = 0;
        a->mie.scale_height = 8;
        a->mie.anisotropy = 0;
        a->mie.scattering_scale = 4;
        a->mie.tint = (rgb){255, 120, 40};
        a->ozone_scale = 0;
        ecs_modified(world, planet, PlanetAtmosphere);
    }

    unsigned char *result = pa_render(world, file);
    ecs_fini(world);

    return result;
}

/** Two planets with different media in one view each show their own haze color at the limb,
 * the same as when they are alone. */
void PlanetAtmosphereShell_two_planets_independent(void) {
    // planet_atmosphere/shell
    int32_t blue[2], orange[2];
    unsigned char *both = pa_two(true, true, "planet_two.png");
    unsigned char *only_blue = pa_two(true, false, "planet_two_blue.png");
    unsigned char *only_orange = pa_two(false, true, "planet_two_orange.png");
    pa_limb(6, -1.6f, 0, 0, -1, PA_LIMB, blue);
    pa_limb(6, 1.6f, 0, 0, -1, PA_LIMB, orange);
    const unsigned char *b = pa_px(both, blue[0], blue[1]);
    const unsigned char *o = pa_px(both, orange[0], orange[1]);
    pa_print("blue limb", b);
    pa_print("orange limb", o);
    test_assert(b[2] > b[0] + 10);
    test_assert(o[0] > o[2] + 10);
    test_assert(pa_diff(b, pa_px(only_blue, blue[0], blue[1])) <= 2);
    test_assert(pa_diff(o, pa_px(only_orange, orange[0], orange[1])) <= 2);
    test_assert(pa_px(only_blue, orange[0], orange[1])[0] <= 1);
    test_assert(pa_px(only_orange, blue[0], blue[1])[2] <= 1);
    stbi_image_free(both);
    stbi_image_free(only_blue);
    stbi_image_free(only_orange);
}

/** A camera inside the atmosphere on the day side that looks up sees a blue sky, without device
 * errors. */
void PlanetAtmosphereShell_camera_inside(void) {
    // planet_atmosphere/shell
    ecs_world_t *world = pa_world(0, -135);
    pa_view(world, 1.05f, 90);
    pa_atmosphere(world, pa_planet(world, 0, 0));
    unsigned char *px = pa_render(world, "planet_inside.png");
    const unsigned char *p = pa_px(px, PA_SIZE / 2, PA_SIZE / 2);
    pa_print("zenith", p);
    test_assert(p[2] > 10);
    test_assert(p[2] > p[0]);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A backlit planet shows a bright ring around its dark disk. */
void PlanetAtmosphereShell_backlit_ring(void) {
    // planet_atmosphere/shell
    int32_t right[2], top[2];
    ecs_world_t *world = pa_world(0, 0);
    pa_view(world, 3, 0);
    pa_atmosphere(world, pa_planet(world, 0, 0));
    unsigned char *px = pa_render(world, "planet_backlit.png");
    pa_limb(3, 0, 0, 1, 0, PA_LIMB, right);
    pa_limb(3, 0, 0, 0, -1, PA_LIMB, top);
    const unsigned char *r = pa_px(px, right[0], right[1]);
    const unsigned char *t = pa_px(px, top[0], top[1]);
    const unsigned char *c = pa_px(px, PA_SIZE / 2, PA_SIZE / 2);
    pa_print("right", r);
    pa_print("top", t);
    pa_print("center", c);
    test_assert(r[0] + r[1] + r[2] > 30);
    test_assert(t[0] + t[1] + t[2] > 30);
    test_assert(c[0] + c[1] + c[2] < r[0] + r[1] + r[2]);
    stbi_image_free(px);
    ecs_fini(world);
}

/** Get the brightness of a column of a capture, the sum of the color channels averaged over the
 * PA_TWILIGHT_ROWS rows on each side of the center row and that row.
 *
 * @param px The pixels
 * @param x Column
 * @return The brightness
 */
static float pa_column(
    const unsigned char *px,
    int32_t x)
{
    int32_t y, sum = 0;
    for (y = PA_SIZE / 2 - PA_TWILIGHT_ROWS; y <= PA_SIZE / 2 + PA_TWILIGHT_ROWS; y ++) {
        const unsigned char *p = pa_px(px, x, y);
        sum += p[0] + p[1] + p[2];
    }

    return (float)sum / (2 * PA_TWILIGHT_ROWS + 1);
}

/** A planet lit from the side fades smoothly from the terminator into the night: along the
 * rows around its center the brightness of the night side falls by about the same amount from
 * column to column, while stripes of the twilight glow parallel to the terminator fall in
 * steps. */
void PlanetAtmosphereShell_terminator_smooth(void) {
    // planet_atmosphere/shell
    int32_t edge[2], x, center = PA_SIZE / 2, columns = 0;
    ecs_world_t *world = pa_world(0, -90);
    pa_view(world, 3, 0);
    pa_atmosphere(world, pa_planet(world, 0, 0));
    unsigned char *px = pa_render(world, "planet_terminator.png");
    pa_limb(3, 0, 0, -1, 0, 1.0f, edge);
    const unsigned char *lit = pa_px(px, center + (center - edge[0]) / 2, center);
    const unsigned char *night = pa_px(px, center - (center - edge[0]) * 7 / 10, center);
    pa_print("day", lit);
    pa_print("night", night);
    test_assert(lit[0] + lit[1] + lit[2] > night[0] + night[1] + night[2]);

    // From just past the terminator of the ground into the night, until the glow is faint
    float prev = pa_column(px, center - 2), drop = -1, worst = 0;
    for (x = center - 3; x > edge[0]; x --) {
        float value = pa_column(px, x);
        if (value < PA_TWILIGHT_FAINT) {
            break;
        }

        if (drop >= 0 && fabsf(prev - value - drop) > worst) {
            worst = fabsf(prev - value - drop);
        }

        drop = prev - value;
        prev = value;
        columns ++;
    }

    printf("twilight columns: %d, largest change of the falloff: %.1f\n", columns, worst);
    test_assert(columns >= 8);
    test_assert(worst < PA_TWILIGHT_STEP);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A view without planets records no shell draw, one with a planet records one per frame. */
void PlanetAtmosphereShell_draws_per_planet(void) {
    // planet_atmosphere/order
    ecs_world_t *world = pa_world(0, -135);
    pa_view(world, 3, 0);
    ecs_entity_t planet = pa_planet(world, 0, 0);
    unsigned char *px = pa_render(world, "planet_draws.png");
    stbi_image_free(px);
    box_render_gpu_stats_t before = pa_frames(world, 0);
    box_render_gpu_stats_t after = pa_frames(world, 2);
    int32_t passes = after.forward_passes - before.forward_passes;
    int32_t draws = after.draws - before.draws;
    pa_atmosphere(world, planet);
    pa_frames(world, 2);
    before = pa_frames(world, 0);
    after = pa_frames(world, 2);
    test_int(after.draws - before.draws, draws + 2);
    test_assert(after.forward_passes - before.forward_passes > passes);
    test_int(after.errors, before.errors);
    ecs_fini(world);
}

/** Render the day case at a scale: the planet at the origin scaled by a factor, lit from +X and
 * +Z, seen from z 3 times the factor.
 *
 * @param scale Uniform Scale of the planet, 1 for none
 * @param file Name of the PNG
 * @return The pixels
 */
static unsigned char* pa_scaled(
    float scale,
    const char *file)
{
    ecs_world_t *world = pa_world(0, -135);
    pa_view(world, 3 * scale, 0);
    ecs_entity_t planet = pa_planet(world, 0, 0);
    if (scale != 1.0f) {
        ecs_set(world, planet, Scale, {scale, scale, scale});
    }

    pa_atmosphere(world, planet);
    unsigned char *result = pa_render(world, file);
    ecs_fini(world);

    return result;
}

/** A planet scaled by 4 seen from 4 times farther looks as the unscaled planet: the shell, its
 * limb and the haze over the surface scale with the globe. */
void PlanetAtmosphereShell_scaled_planet_matches(void) {
    // planet_atmosphere/scale
    int32_t day[2], top[2], outside[2];
    unsigned char *px = pa_scaled(4, "planet_scaled.png");
    unsigned char *base = pa_scaled(1, "planet_scaled_base.png");
    pa_limb(3, 0, 0, 1, 0, PA_LIMB, day);
    pa_limb(3, 0, 0, 0, -1, PA_LIMB, top);
    pa_limb(3, 0, 0, 1, 0, 1.3f, outside);
    const unsigned char *p = pa_px(px, day[0], day[1]);
    pa_print("scaled limb", p);
    pa_print("unscaled limb", pa_px(base, day[0], day[1]));
    test_assert(p[0] + p[1] + p[2] > 60);
    test_assert(pa_diff(p, pa_px(base, day[0], day[1])) <= 2);
    test_assert(pa_diff(pa_px(px, top[0], top[1]), pa_px(base, top[0], top[1])) <= 2);
    test_assert(pa_diff(pa_px(px, outside[0], outside[1]),
        pa_px(base, outside[0], outside[1])) <= 1);
    test_assert(pa_diff(pa_px(px, PA_SIZE / 2, PA_SIZE / 2),
        pa_px(base, PA_SIZE / 2, PA_SIZE / 2)) <= 2);

    int32_t x, y, off = 0;
    for (y = 0; y < PA_SIZE; y ++) {
        for (x = 0; x < PA_SIZE; x ++) {
            off += pa_diff(pa_px(px, x, y), pa_px(base, x, y)) > 2;
        }
    }

    printf("pixels that differ by more than 2: %d\n", off);
    test_assert(off <= PA_SIZE * PA_SIZE / 100);
    stbi_image_free(px);
    stbi_image_free(base);
}
