/**
 * @file PlanetSurfaceLight.c
 * @brief Tests for the sunlight of a PlanetAtmosphere on its meshes: the terminator of a white
 * planet turns red while its subsolar point stays white, through the mesh shader, the blended
 * cloud sphere, the extended mesh shader and transmission; a mesh that is not a descendant and a
 * scene without PlanetAtmosphere are lit as before. The sky of the atmosphere lights the
 * ground in the twilight past the terminator with a tint of the medium, but not deep in the
 * night nor without scattering. Checked on PNG captures.
 *
 * The planet is a white sphere of radius 1 with the Earth medium, 6360 km, whose shell draws no
 * light (intensity 0), which also turns off its sky light, so only the attenuation of the sun
 * colors the surface; the shell darkens the image by a gray transmittance only. The camera looks down -Z from Z 4 with a 60 degree
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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Edge of the square captures */
#define PS_SIZE (128)

/** Z of the camera */
#define PS_CAMERA_Z (4.0f)

/** Intensity of the sun */
#define PS_SUN (3.0f)

/** Cosine of the angle between the normal and the sun of the sample near the terminator */
#define PS_TERMINATOR_MU (0.2f)

/** Z of the camera of the bumpy globe, close to the surface so the terminator fills the image */
#define PS_CLOSE_Z (1.5f)

/** Width in cosine to the sun of the bins of the profile across the terminator */
#define PS_BIN (0.01f)

/** First bin of the profile and the number of bins: mu from -0.1 to 0.3 */
#define PS_BIN_MIN (-0.1f)
#define PS_BINS (40)

/** Intensity of the sun of the sky light cases, bright enough that the sky light of the
 * twilight shows in the 8 bit captures */
#define PS_SKY_SUN (300.0f)

/** Intensity of the white ambient light of the sky light cases */
#define PS_SKY_AMBIENT (0.005f)

/** Cosine of the sun zenith angle on the ground of the twilight case, just past the
 * terminator */
#define PS_TWILIGHT_MU (-0.05f)

/** Cosine of the sun zenith angle on the ground of the night case */
#define PS_NIGHT_MU (-0.45f)

/** Height of the camera of the sky light cases above the ground, about 1.3 km */
#define PS_SKY_CAMERA (2e-4f)

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
    ecs_singleton_set(world, Scene, {.shader_features.Extended.alpha_cutout = true,
        .shader_features.Extended.blend_modes = true, .shader_features.Extended.transmission = true,
        .shader_features.Extended.clear_coat = true, .planet_light = planet_light});
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
    ecs_set_name(world, camera, "camera");
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

/** Take the scattering out of the medium of a PlanetAtmosphere: its air still absorbs by its
 * ozone but its sky is black, so its meshes are lit by the direct sun alone.
 *
 * @param world The world
 * @param planet The entity with the PlanetAtmosphere
 */
static void ps_no_scattering(
    ecs_world_t *world,
    ecs_entity_t planet)
{
    PlanetAtmosphere *a = ecs_get_mut(world, planet, PlanetAtmosphere);
    a->rayleigh.scattering_scale = 0;
    a->mie.scattering_scale = 0;
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
    ecs_set(world, planet, Sphere, {.segments = 96, .smooth = true, .radius = 0.98f});
    ps_atmosphere(world, planet);
    ecs_entity_t clouds = ps_sphere(world, planet);
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

/** Write a tangent space normal map of 32x32 texels whose normals tilt along +U and -U, toward
 * and away from a light along U, by up to about 39 degrees, the tilt changing with the row: two
 * periods of a sine down the image, the same in every column.
 *
 * @param world The world
 * @return The texture, loaded
 */
static ecs_entity_t ps_bumps(
    ecs_world_t *world)
{
    char *file = flecs_asprintf("%s/planet_bumps.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t px[32 * 32 * 4];
    int32_t x, y;
    for (y = 0; y < 32; y ++) {
        for (x = 0; x < 32; x ++) {
            vec3 n = {0.8f * sinf(((float)y + 0.5f) * GLM_PIf / 8.0f), 0, 1};
            glm_vec3_normalize(n);
            uint8_t *p = &px[(y * 32 + x) * 4];
            p[0] = (uint8_t)roundf((n[0] * 0.5f + 0.5f) * 255.0f);
            p[1] = (uint8_t)roundf((n[1] * 0.5f + 0.5f) * 255.0f);
            p[2] = (uint8_t)roundf((n[2] * 0.5f + 0.5f) * 255.0f);
            p[3] = 255;
        }
    }

    test_assert(stbi_write_png(file, 32, 32, 4, px, 32 * 4) != 0);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    ecs_os_free(file);

    return tex;
}

/** Point the sun along a direction in the XZ plane at an angle from +X toward +Z.
 *
 * @param world The world
 * @param elevation The angle in radians
 */
static void ps_sun_at(
    ecs_world_t *world,
    float elevation)
{
    ecs_set(world, ecs_lookup(world, "sun"), Rotation, {0, -(float)GLM_PI_2 - elevation, 0});
}

/** Linear value of the red channel of a display encoded pixel.
 *
 * @param p The pixel
 * @return The value, 0..1
 */
static float ps_linear(
    const unsigned char *p)
{
    float c = (float)p[0] / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Render the unit sphere from PS_CLOSE_Z and average the linear brightness of its pixels in
 * bins of the cosine between their geometric normal and the sun, found by casting the ray of
 * each pixel at the sphere. Pixels of every row fall in a bin, so a bin averages every phase of
 * the bumps of ps_bumps.
 *
 * @param world The world
 * @param file Name of the PNG
 * @param profile Receives the mean of each bin, -1 for an empty bin
 */
static void ps_profile(
    ecs_world_t *world,
    const char *file,
    float *profile)
{
    unsigned char *px = ps_render(world, file);
    vec3 s;
    ps_sun_direction(world, s);
    float sum[PS_BINS] = {0};
    int32_t count[PS_BINS] = {0};
    float t = tanf(glm_rad(30.0f));
    int32_t x, y, i;
    for (y = 0; y < PS_SIZE; y ++) {
        for (x = 0; x < PS_SIZE; x ++) {
            vec3 d = {
                (((float)x + 0.5f) / PS_SIZE * 2 - 1) * t,
                (1 - ((float)y + 0.5f) / PS_SIZE * 2) * t,
                -1
            };
            glm_vec3_normalize(d);
            vec3 o = {0, 0, PS_CLOSE_Z};
            float b = glm_vec3_dot(o, d);
            float c = glm_vec3_dot(o, o) - 1;
            float disc = b * b - c;
            if (disc < 0) {
                continue;
            }

            float hit = -b - sqrtf(disc);
            vec3 n;
            glm_vec3_scale(d, hit, n);
            glm_vec3_add(o, n, n);
            int32_t bin = (int32_t)floorf((glm_vec3_dot(n, s) - PS_BIN_MIN) / PS_BIN);
            if (bin < 0 || bin >= PS_BINS) {
                continue;
            }

            sum[bin] += ps_linear(&px[(y * PS_SIZE + x) * 4]);
            count[bin] ++;
        }
    }

    for (i = 0; i < PS_BINS; i ++) {
        profile[i] = count[i] ? sum[i] / (float)count[i] : -1;
        printf("mu %.3f: %.4f (%d)\n", (double)(PS_BIN_MIN + ((float)i + 0.5f) * PS_BIN),
            (double)profile[i], count[i]);
    }

    stbi_image_free(px);
}

/** Bin of a cosine to the sun.
 *
 * @param mu The cosine
 * @return The bin
 */
static int32_t ps_bin(
    float mu)
{
    return (int32_t)floorf((mu - PS_BIN_MIN) / PS_BIN);
}

void PlanetSurfaceLight_bumpy_terminator_continuous(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(true);
    ps_sun_at(world, 0);
    ecs_set(world, ecs_lookup(world, "camera"), Position, {0, 0, PS_CLOSE_Z});
    ecs_entity_t planet = ps_sphere(world, 0);
    ps_atmosphere(world, planet);
    ecs_set(world, planet, PbrTextures, {.normal = ps_bumps(world)});
    ecs_set(world, planet, TextureTransform, {.scale_x = 64, .scale_y = 32});
    float profile[PS_BINS];
    ps_profile(world, "planet_light_bumpy_terminator.png", profile);

    // The light of the day side, from which the bumps fall off toward the night without a
    // step: at most twice the mean slope of the day side between neighbouring bins.
    float lit = profile[ps_bin(0.2f)];
    test_assert(lit > 0.02f);
    int32_t i;
    for (i = ps_bin(-0.08f); i < ps_bin(0.1f); i ++) {
        test_assert(profile[i] >= 0);
        test_assert(profile[i + 1] >= 0);
        float step = fabsf(profile[i + 1] - profile[i]);
        printf("step %d: %.4f, bound %.4f\n", i, (double)step,
            (double)(2.0f * lit * PS_BIN / 0.2f));
        test_assert(step <= 2.0f * lit * PS_BIN / 0.2f);
    }

    // Close to the terminator the light fades with the geometric surface: the bumps that face
    // the sun are not lit where the ground under them barely is, and past the penumbra of the
    // sun disc the surface is dark.
    printf("near terminator: %.4f, day side: %.4f\n", (double)profile[ps_bin(0.015f)],
        (double)lit);
    test_assert(profile[ps_bin(0.015f)] < 0.08f * lit);
    for (i = 0; i < ps_bin(-0.01f); i ++) {
        test_assert(profile[i] < 0.005f * lit);
    }

    ecs_fini(world);
}

/** Render a white bumpy quad facing the camera, lit by the sun at an elevation above its plane,
 * and average the linear brightness of the middle of the image.
 *
 * @param world The world
 * @param elevation Angle of the sun above the plane in degrees, below it when negative
 * @param file Name of the PNG
 * @return The mean
 */
static float ps_plane_mean(
    ecs_world_t *world,
    float elevation,
    const char *file)
{
    ps_sun_at(world, glm_rad(elevation));
    unsigned char *px = ps_render(world, file);
    float sum = 0;
    int32_t x, y, count = 0;
    for (y = PS_SIZE / 4; y < PS_SIZE * 3 / 4; y ++) {
        for (x = PS_SIZE / 4; x < PS_SIZE * 3 / 4; x ++) {
            sum += ps_linear(&px[(y * PS_SIZE + x) * 4]);
            count ++;
        }
    }

    stbi_image_free(px);
    printf("elevation %.1f: %.4f\n", (double)elevation, (double)(sum / (float)count));

    return sum / (float)count;
}

void PlanetSurfaceLight_bumpy_plane_grazing(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(false);
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Position, {0, 0, 0});
    ecs_set(world, quad, Quad, {4, 4});
    ecs_set(world, quad, Rgb, {255, 255, 255});
    ecs_set(world, quad, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, quad, PbrTextures, {.normal = ps_bumps(world)});
    ecs_set(world, quad, TextureTransform, {.scale_x = 4, .scale_y = 4});

    // The bumps facing a sun that grazes the plane fall off with it like the plane does, and a
    // sun below the plane leaves the plane dark: no light through the geometric horizon.
    float high = ps_plane_mean(world, 20, "planet_light_plane_20.png");
    float low = ps_plane_mean(world, 1, "planet_light_plane_1.png");
    float below = ps_plane_mean(world, -1, "planet_light_plane_m1.png");
    float under = ps_plane_mean(world, -4, "planet_light_plane_m4.png");
    test_assert(high > 0.02f);
    test_assert(low < 0.25f * high);
    test_assert(below < 0.02f * high + 0.002f);
    test_assert(under < 0.02f * high + 0.002f);
    ecs_fini(world);
}

/** Render a small white quad facing the camera on the ground of a planet whose up there is +X,
 * lit by a sun at an angle above the horizon of the planet there, and average the linear
 * brightness of the middle of the quad. The quad faces the sun almost head on, so only the
 * shadow of the planet and the transmittance change with the angle.
 *
 * @param world The world
 * @param angle Angle of the sun above the horizon of the planet in degrees
 * @param file Name of the PNG
 * @return The mean
 */
static float ps_horizon_mean(
    ecs_world_t *world,
    float angle,
    const char *file)
{
    ps_sun_at(world, glm_rad(90.0f - angle));
    unsigned char *px = ps_render(world, file);

    float sum = 0;
    int32_t x, y, count = 0;
    for (y = PS_SIZE / 2 - 3; y < PS_SIZE / 2 + 3; y ++) {
        for (x = PS_SIZE / 2 - 3; x < PS_SIZE / 2 + 3; x ++) {
            sum += ps_linear(&px[(y * PS_SIZE + x) * 4]);
            count ++;
        }
    }

    stbi_image_free(px);
    printf("sun %.1f above the horizon: %.4f\n", (double)angle, (double)(sum / (float)count));

    return sum / (float)count;
}

/** Create a planet of radius 1 centered at (-1, 0, 0) without a mesh of its own and a white
 * quad of it where its up is +X, facing the camera at Z 1 in front of the quad. The quad ends at
 * the ground at the origin, below it: a point under the ground has the light of the ground, whose
 * horizon is flat, while a point above it sees the sun past the horizon by the dip of its
 * altitude, degrees for a quad a tenth of the radius high.
 *
 * @param world The world
 * @param sun_radius PlanetAtmosphere.sun_radius
 * @return The planet
 */
static ecs_entity_t ps_horizon_scene(
    ecs_world_t *world,
    float sun_radius)
{
    ecs_set(world, ecs_lookup(world, "camera"), Position, {-0.05f, 0, 1});
    ecs_entity_t planet = ecs_new(world);
    ecs_set(world, planet, Position, {-1, 0, 0});
    ps_atmosphere(world, planet);
    ecs_get_mut(world, planet, PlanetAtmosphere)->sun_radius = sun_radius;
    ecs_modified(world, planet, PlanetAtmosphere);
    ecs_entity_t quad = ecs_new_w_pair(world, EcsChildOf, planet);
    ecs_set(world, quad, Position, {0.95f, 0, 0});
    ecs_set(world, quad, Quad, {0.1f, 0.1f});
    ecs_set(world, quad, Rgb, {255, 255, 255});
    ecs_set(world, quad, PbrMaterial, {.metallic = 0, .roughness = 1});

    return planet;
}

void PlanetSurfaceLight_penumbra_sun_radius(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(true);
    ps_horizon_scene(world, 2.0f);

    // A sun disk of 2 degrees is half set with its center on the horizon and still shows a
    // fifth of its disk a degree below it, the penumbra of the shadow of the planet; three
    // degrees below it has set.
    float above = ps_horizon_mean(world, 1, "planet_light_penumbra_1.png");
    float on = ps_horizon_mean(world, 0, "planet_light_penumbra_0.png");
    float below = ps_horizon_mean(world, -1, "planet_light_penumbra_m1.png");
    float set = ps_horizon_mean(world, -3, "planet_light_penumbra_m3.png");
    test_assert(above > 0.005f);
    test_assert(on < above);
    test_assert(on > 0.25f * above);
    test_assert(below < on);
    test_assert(below > 0.05f * above);
    test_assert(set < 0.01f * above + 0.002f);
    ecs_fini(world);
}

void PlanetSurfaceLight_penumbra_point_sun(void) {
    // planet_atmosphere/surface_light
    ecs_world_t *world = ps_world(true);
    ps_horizon_scene(world, 0);

    // A sun without a disk sets at once at the horizon.
    float above = ps_horizon_mean(world, 1, "planet_light_point_1.png");
    float below = ps_horizon_mean(world, -1, "planet_light_point_m1.png");
    test_assert(above > 0.005f);
    test_assert(below < 0.01f * above + 0.002f);
    ecs_fini(world);
}

/** Create a planet of radius 1 centered at (-1, 0, 0) without a mesh of its own, with a point
 * sun of PS_SKY_SUN and a white ambient light of PS_SKY_AMBIENT, and a white quad of it on the
 * ground at the origin facing up, +X, which the camera looks down at from PS_SKY_CAMERA. The
 * camera is low enough to be in the shadow of the planet in the twilight, so the shell, which
 * draws its light (intensity 1) and which the sky light scales with, adds little in front of
 * the quad.
 *
 * @param world The world
 * @return The planet
 */
static ecs_entity_t ps_sky_scene(
    ecs_world_t *world)
{
    ecs_set(world, ecs_lookup(world, "sun"), DirectionalLight, {.intensity = PS_SKY_SUN});
    ecs_singleton_set(world, AmbientLight, {.sky = {255, 255, 255}, .ground = {255, 255, 255},
        .intensity = PS_SKY_AMBIENT});
    ecs_entity_t camera = ecs_lookup(world, "camera");
    ecs_set(world, camera, Position, {PS_SKY_CAMERA, 0, 0});
    ecs_set(world, camera, Rotation, {0, (float)GLM_PI_2, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = PS_SKY_CAMERA * 0.1f, .far_ = 1});
    ecs_entity_t planet = ecs_new(world);
    ecs_set(world, planet, Position, {-1, 0, 0});
    ps_atmosphere(world, planet);
    ecs_get_mut(world, planet, PlanetAtmosphere)->intensity = 1;
    ecs_modified(world, planet, PlanetAtmosphere);
    ecs_entity_t quad = ecs_new_w_pair(world, EcsChildOf, planet);
    ecs_set_name(world, quad, "quad");
    ecs_set(world, quad, Position, {1, 0, 0});
    ecs_set(world, quad, Rotation, {0, (float)GLM_PI_2, 0});
    ecs_set(world, quad, Quad, {0.01f, 0.01f});
    ecs_set(world, quad, Rgb, {255, 255, 255});
    ecs_set(world, quad, PbrMaterial, {.metallic = 0, .roughness = 1});

    return planet;
}

/** Render the quad of ps_sky_scene with the sun at a cosine of its zenith angle on the ground
 * and average the linear color of the middle of the quad.
 *
 * @param world The world
 * @param mu The cosine of the sun zenith angle, +X being up
 * @param file Name of the PNG
 * @param out Receives the mean linear red, green and blue
 */
static void ps_sky_mean(
    ecs_world_t *world,
    float mu,
    const char *file,
    float *out)
{
    ps_sun_at(world, (float)GLM_PI_2 - asinf(mu));
    unsigned char *px = ps_render(world, file);
    int32_t x, y, c, count = 0;
    out[0] = out[1] = out[2] = 0;
    for (y = PS_SIZE / 2 - 3; y < PS_SIZE / 2 + 3; y ++) {
        for (x = PS_SIZE / 2 - 3; x < PS_SIZE / 2 + 3; x ++) {
            const unsigned char *p = &px[(y * PS_SIZE + x) * 4];
            for (c = 0; c < 3; c ++) {
                float v = (float)p[c] / 255.0f;
                out[c] += v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
            }

            count ++;
        }
    }

    for (c = 0; c < 3; c ++) {
        out[c] /= (float)count;
    }

    stbi_image_free(px);
    printf("%s, mu %.2f: %.4f %.4f %.4f\n", file, (double)mu, (double)out[0], (double)out[1],
        (double)out[2]);
}

/** Render the quad of ps_sky_scene with the sun at a cosine of its zenith angle white and
 * black and take the difference, the diffuse light of the quad: the light the shell draws in
 * front of it and the specular cancel.
 *
 * @param world The world
 * @param planet The planet of ps_sky_scene
 * @param mu The cosine of the sun zenith angle
 * @param name Name of the PNGs
 * @param out Receives the mean linear diffuse color
 */
static void ps_sky_diffuse(
    ecs_world_t *world,
    ecs_entity_t planet,
    float mu,
    const char *name,
    float *out)
{
    ecs_entity_t quad = ecs_lookup_child(world, planet, "quad");
    float black[3];
    test_assert(quad != 0);
    char *file = flecs_asprintf("%s.png", name);
    ps_sky_mean(world, mu, file, out);
    ecs_os_free(file);
    ecs_set(world, quad, Rgb, {0, 0, 0});
    file = flecs_asprintf("%s_black.png", name);
    ps_sky_mean(world, mu, file, black);
    ecs_os_free(file);
    ecs_set(world, quad, Rgb, {255, 255, 255});
    glm_vec3_sub(out, black, out);
}

/** Get the diffuse light of the quad of ps_sky_scene with the sun at a cosine of its zenith
 * angle, once lit by the sun and once with the sun turned off, by the ambient light alone.
 *
 * @param world The world
 * @param planet The planet of ps_sky_scene
 * @param mu The cosine of the sun zenith angle
 * @param lit Receives the mean linear diffuse color lit by the sun
 * @param ambient Receives the mean linear diffuse color lit by the ambient light alone
 */
static void ps_sky_pair(
    ecs_world_t *world,
    ecs_entity_t planet,
    float mu,
    float *lit,
    float *ambient)
{
    ecs_entity_t sun = ecs_lookup(world, "sun");
    char *name = flecs_asprintf("planet_sky_%d", (int)roundf(-mu * 100));
    ps_sky_diffuse(world, planet, mu, name, lit);
    ecs_os_free(name);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});
    name = flecs_asprintf("planet_sky_%d_ambient", (int)roundf(-mu * 100));
    ps_sky_diffuse(world, planet, mu, name, ambient);
    ecs_os_free(name);
    ecs_set(world, sun, DirectionalLight, {.intensity = PS_SKY_SUN});
}

void PlanetSurfaceLight_twilight_sky_light(void) {
    // planet_atmosphere/sky_light
    ecs_world_t *world = ps_world(true);
    ecs_entity_t planet = ps_sky_scene(world);

    // Just past the terminator the sun is below the horizon of the ground, so no direct light
    // reaches it, but the air above is still lit: the ground takes more than the ambient, and
    // the light the sky adds is blue, the Rayleigh scattering of the Earth medium.
    float lit[3], ambient[3], sky[3];
    ps_sky_pair(world, planet, PS_TWILIGHT_MU, lit, ambient);
    glm_vec3_sub(lit, ambient, sky);
    test_assert(ambient[1] > 0.001f);
    test_assert(lit[0] + lit[1] + lit[2] > 1.5f * (ambient[0] + ambient[1] + ambient[2]));
    test_assert(sky[2] > 0);
    test_assert(sky[2] > sky[0]);
    ecs_fini(world);
}

void PlanetSurfaceLight_night_no_sky_light(void) {
    // planet_atmosphere/sky_light
    ecs_world_t *world = ps_world(true);
    ecs_entity_t planet = ps_sky_scene(world);

    // Deep in the night the air above the ground is in the shadow of the planet: the ground
    // keeps the ambient alone.
    float lit[3], ambient[3];
    ps_sky_pair(world, planet, PS_NIGHT_MU, lit, ambient);
    int32_t c;
    test_assert(ambient[1] > 0.001f);
    for (c = 0; c < 3; c ++) {
        test_assert(fabsf(lit[c] - ambient[c]) < 0.1f * ambient[c] + 0.0005f);
    }

    ecs_fini(world);
}

void PlanetSurfaceLight_no_scattering_no_sky_light(void) {
    // planet_atmosphere/sky_light
    ecs_world_t *world = ps_world(true);
    ecs_entity_t planet = ps_sky_scene(world);
    ps_no_scattering(world, planet);

    // A medium that does not scatter has a black sky, which lights nothing in the twilight.
    float lit[3], ambient[3];
    ps_sky_pair(world, planet, PS_TWILIGHT_MU, lit, ambient);
    int32_t c;
    test_assert(ambient[1] > 0.001f);
    for (c = 0; c < 3; c ++) {
        test_assert(fabsf(lit[c] - ambient[c]) < 0.1f * ambient[c] + 0.0005f);
    }

    ecs_fini(world);
}

void PlanetSurfaceLight_shell_off_no_sky_light(void) {
    // planet_atmosphere/sky_light
    ecs_world_t *world = ps_world(true);
    ecs_entity_t planet = ps_sky_scene(world);
    ecs_get_mut(world, planet, PlanetAtmosphere)->intensity = 0;
    ecs_modified(world, planet, PlanetAtmosphere);

    // The sky light scales with the light the shell draws, so a shell that draws none lights
    // nothing in the twilight.
    float lit[3], ambient[3];
    ps_sky_pair(world, planet, PS_TWILIGHT_MU, lit, ambient);
    int32_t c;
    test_assert(ambient[1] > 0.001f);
    for (c = 0; c < 3; c ++) {
        test_assert(fabsf(lit[c] - ambient[c]) < 0.1f * ambient[c] + 0.0005f);
    }

    ecs_fini(world);
}
