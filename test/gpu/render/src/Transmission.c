/**
 * @file Transmission.c
 * @brief Tests for Transmission: what a transmissive shape shows of the opaque snapshot of its
 * view, its counters and the views and targets it renders in, checked on PNG captures.
 *
 * Most cases run in a lit view without lights and with a black ambient, from a camera at the
 * origin looking down -Z. The background is an emissive wall of black albedo and the glass is
 * white and smooth, so a pixel through it is the background times what the glass lets through.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge of the square captures */
#define TR_SIZE (64)

/** Create a headless world that renders on the main thread, with no light but a point light of
 * intensity 0 out of reach (which replaces the default sun) and a black ambient.
 *
 * @return The world
 */
static ecs_world_t* tr_world(void)
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
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {1000, 1000, 1000});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});

    return world;
}

/** Add a camera at a position, looking down -Z.
 *
 * @param world The world
 * @param parent Parent of the camera, 0 for none
 * @return The camera
 */
static ecs_entity_t tr_camera(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_entity_t camera = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});

    return camera;
}

/** Add a view of a camera that clears to black.
 *
 * @param world The world
 * @param camera The camera
 * @param lit Whether the view is lit
 * @param occlusion Whether the view culls by occlusion, else it draws every instance
 * @return The view
 */
static ecs_entity_t tr_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    bool lit,
    bool occlusion)
{
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = lit, .depth_prepass = true},
        .culling = {.enabled = occlusion, .occlusion = occlusion, .min_dimension_px = 1}});

    return view;
}

/** Add a wall of black albedo that shows its emission.
 *
 * @param world The world
 * @param x X of its center
 * @param z Z of its center
 * @param w Width
 * @param h Height
 * @param r Red of the emission
 * @param g Green of the emission
 * @param b Blue of the emission
 * @return The wall
 */
static ecs_entity_t tr_wall(
    ecs_world_t *world,
    float x,
    float z,
    float w,
    float h,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, z});
    ecs_set(world, e, Box, {w, h, 0.1f});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {r, g, b}});

    return e;
}

/** Add a smooth white glass slab facing the camera.
 *
 * @param world The world
 * @param x X of its center
 * @param z Z of its center
 * @param w Width
 * @param h Height
 * @param factor transmission_factor
 * @param ior Index of refraction
 * @param thickness thickness_factor
 * @return The glass
 */
static ecs_entity_t tr_glass(
    ecs_world_t *world,
    float x,
    float z,
    float w,
    float h,
    float factor,
    float ior,
    float thickness)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, z});
    ecs_set(world, e, Box, {w, h, 0.2f});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0});
    ecs_set(world, e, Transmission, {.transmission_factor = factor, .ior = ior,
        .thickness_factor = thickness, .attenuation_distance = 1e10f,
        .attenuation_color = {255, 255, 255}});

    return e;
}

/** Add a surface that renders the views into a PNG.
 *
 * @param world The world
 * @param width Width in pixels
 * @param height Height in pixels
 * @param file Name of the PNG in the temporary directory
 * @return The surface
 */
static ecs_entity_t tr_surface(
    ecs_world_t *world,
    int32_t width,
    int32_t height,
    const char *file)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = width, .height = height,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    return e;
}

/** Run frames and read the counters of the GPU backend after them. Before the first frame
 * with a surface there is no backend and the counters are all 0.
 *
 * @param world The world
 * @param count Frames to run
 * @return The counters
 */
static box_render_gpu_stats_t tr_frames(
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

/** Load a capture of the temporary directory.
 *
 * @param world The world
 * @param file Name of the PNG
 * @param width Expected width
 * @param height Expected height
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* tr_load(
    ecs_world_t *world,
    const char *file,
    int32_t width,
    int32_t height)
{
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, width);
    test_int(h, height);
    ecs_os_free(path);

    return px;
}

/** Render frames into a capture and load it.
 *
 * @param world The world
 * @param file Name of the PNG
 * @param width Width
 * @param height Height
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* tr_render(
    ecs_world_t *world,
    const char *file,
    int32_t width,
    int32_t height)
{
    box_render_gpu_stats_t before = tr_frames(world, 0), after;
    tr_surface(world, width, height, file);
    after = tr_frames(world, 3);
    test_int(after.errors, before.errors);

    return tr_load(world, file, width, height);
}

/** Run a scene script in a fresh world and render it into a capture.
 *
 * @param script The scene
 * @param file Name of the PNG
 * @param width Width
 * @param height Height
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* tr_script(
    const char *script,
    const char *file,
    int32_t width,
    int32_t height)
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
    test_assert(ecs_script_run(world, file, script, NULL) == 0);
    unsigned char *result = tr_render(world, file, width, height);
    ecs_fini(world);

    return result;
}

/** Convert a display encoded byte to linear light.
 *
 * @param c The byte
 * @return The linear value
 */
static float tr_lin(
    int32_t c)
{
    float v = (float)c / 255.0f;

    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

/** Convert linear light to a display encoded byte.
 *
 * @param lin The linear value
 * @return The byte
 */
static int32_t tr_srgb(
    float lin)
{
    lin = lin < 0.0f ? 0.0f : (lin > 1.0f ? 1.0f : lin);
    float v = lin <= 0.0031308f ? lin * 12.92f : 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f;

    return (int32_t)(v * 255.0f + 0.5f);
}

/** Get a pixel of a capture.
 *
 * @param px The pixels
 * @param width Width of the capture
 * @param x Column
 * @param y Row
 * @return The pixel, RGBA
 */
static const unsigned char* tr_px(
    const unsigned char *px,
    int32_t width,
    int32_t x,
    int32_t y)
{
    return &px[(y * width + x) * 4];
}

/** Check a pixel against expected display encoded channels.
 *
 * @param p The pixel
 * @param r Expected red
 * @param g Expected green
 * @param b Expected blue
 * @param tolerance Largest difference per channel
 */
static void tr_expect(
    const unsigned char *p,
    int32_t r,
    int32_t g,
    int32_t b,
    int32_t tolerance)
{
    int32_t want[3] = { r, g, b }, c;
    for (c = 0; c < 3; c ++) {
        if (abs((int32_t)p[c] - want[c]) > tolerance) {
            printf("channel %d: got %d, want %d (pixel %d %d %d)\n", c, p[c], want[c],
                p[0], p[1], p[2]);
        }

        test_assert(abs((int32_t)p[c] - want[c]) <= tolerance);
    }
}

/** Render a white glass slab of factor 1 over a background and get its center pixel.
 *
 * @param file Name of the PNG
 * @param bg Emission of the background wall
 * @param t The Transmission of the glass
 * @param albedo Base color of the glass
 * @param out Receives the center pixel and the background pixel at a corner, RGBA
 */
static void tr_center(
    const char *file,
    const uint8_t bg[3],
    const Transmission *t,
    const uint8_t albedo[3],
    unsigned char out[8])
{
    ecs_world_t *world = tr_world();
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -10, 40, 40, bg[0], bg[1], bg[2]);
    ecs_entity_t glass = tr_glass(world, 0, -4, 3, 3, 1, 1, 0);
    ecs_set_ptr(world, glass, Transmission, t);
    ecs_set(world, glass, Rgb, {albedo[0], albedo[1], albedo[2]});
    unsigned char *px = tr_render(world, file, TR_SIZE, TR_SIZE);
    ecs_os_memcpy(out, tr_px(px, TR_SIZE, TR_SIZE / 2, TR_SIZE / 2), 4);
    ecs_os_memcpy(out + 4, tr_px(px, TR_SIZE, 2, 2), 4);
    stbi_image_free(px);
    ecs_fini(world);
}

void Transmission_factor_zero_matches_blended_path(void) {
    // materials/transmission
    unsigned char *images[2];
    int32_t i, k, c, wrong = 0, lit = 0;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = tr_world();
        ecs_singleton_set(world, AmbientLight, {.sky = {120, 140, 160},
            .ground = {60, 50, 40}, .intensity = 1});
        ecs_entity_t sun = ecs_new(world);
        ecs_set(world, sun, PointLight, {.intensity = 30, .range = 50});
        ecs_set(world, sun, Position, {2, 3, 0});
        tr_view(world, tr_camera(world, 0), true, false);
        tr_wall(world, 0, -10, 40, 40, 40, 60, 80);
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {0, 0, -4});
        ecs_set(world, e, Sphere, {.segments = 32, .smooth = true, .radius = 1.2f});
        ecs_set(world, e, Rgb, {200, 120, 60});
        ecs_set(world, e, PbrMaterial, {.metallic = 0.2f, .roughness = 0.4f});
        ecs_set(world, e, Emissive, {.strength = 0.1f, .color = {0, 0, 255}});
        if (k) {
            ecs_set(world, e, Transmission, {.transmission_factor = 0, .ior = 1.5f,
                .thickness_factor = 1, .attenuation_distance = 1,
                .attenuation_color = {255, 0, 0}});
        }

        images[k] = tr_render(world, k ? "factor_zero.png" : "factor_none.png",
            TR_SIZE, TR_SIZE);
        ecs_fini(world);
    }

    for (i = 0; i < TR_SIZE * TR_SIZE; i ++) {
        bool differs = false;
        for (c = 0; c < 3; c ++) {
            differs |= abs((int)images[0][i * 4 + c] - (int)images[1][i * 4 + c]) > 1;
        }

        if (differs && wrong < 8) {
            printf("pixel %d, %d differs: %d %d %d without, %d %d %d with factor 0\n",
                i % TR_SIZE, i / TR_SIZE, images[0][i * 4], images[0][i * 4 + 1],
                images[0][i * 4 + 2], images[1][i * 4], images[1][i * 4 + 1],
                images[1][i * 4 + 2]);
        }

        wrong += differs;
    }

    lit = images[0][(TR_SIZE / 2 * TR_SIZE + TR_SIZE / 2) * 4];
    test_int(wrong, 0);
    test_assert(lit > 40);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

void Transmission_sees_background(void) {
    // materials/transmission
    uint8_t bg[3] = { 200, 120, 60 }, white[3] = { 255, 255, 255 };
    Transmission t = { .transmission_factor = 1, .ior = 1, .thickness_factor = 0,
        .attenuation_distance = 1e10f, .attenuation_color = { 255, 255, 255 } };
    unsigned char p[8];
    tr_center("sees.png", bg, &t, white, p);
    tr_expect(p + 4, bg[0], bg[1], bg[2], 1);
    tr_expect(p, bg[0], bg[1], bg[2], 2);
}

void Transmission_fresnel_ior(void) {
    // materials/transmission
    uint8_t bg[3] = { 200, 160, 120 }, white[3] = { 255, 255, 255 };
    Transmission t = { .transmission_factor = 1, .ior = 1.5f, .thickness_factor = 0,
        .attenuation_distance = 1e10f, .attenuation_color = { 255, 255, 255 } };
    unsigned char p[8];
    tr_center("fresnel.png", bg, &t, white, p);
    tr_expect(p, tr_srgb(tr_lin(bg[0]) * 0.96f), tr_srgb(tr_lin(bg[1]) * 0.96f),
        tr_srgb(tr_lin(bg[2]) * 0.96f), 2);
    test_assert(p[0] < p[4]);
}

/** A thickness equal to the attenuation distance gives the attenuation color, half of it keeps
 * the square root of the color. */
void Transmission_attenuation_color(void) {
    // materials/transmission
    uint8_t bg[3] = { 230, 230, 230 }, white[3] = { 255, 255, 255 };
    Transmission t = { .transmission_factor = 1, .ior = 1, .thickness_factor = 2,
        .attenuation_distance = 2, .attenuation_color = { 128, 200, 60 } };
    unsigned char p[8];
    int32_t c;
    tr_center("attenuation.png", bg, &t, white, p);
    for (c = 0; c < 3; c ++) {
        int32_t want = tr_srgb(tr_lin(bg[c]) * tr_lin((&t.attenuation_color.r)[c]));
        if (abs((int32_t)p[c] - want) > 2) {
            printf("channel %d: got %d, want %d\n", c, p[c], want);
        }

        test_assert(abs((int32_t)p[c] - want) <= 2);
    }

    t.thickness_factor = 1;
    tr_center("attenuation_half.png", bg, &t, white, p);
    for (c = 0; c < 3; c ++) {
        int32_t want = tr_srgb(tr_lin(bg[c]) * sqrtf(tr_lin((&t.attenuation_color.r)[c])));
        test_assert(abs((int32_t)p[c] - want) <= 2);
    }
}

void Transmission_albedo_tints(void) {
    // materials/transmission
    uint8_t bg[3] = { 230, 230, 230 }, albedo[3] = { 255, 128, 64 };
    Transmission t = { .transmission_factor = 1, .ior = 1, .thickness_factor = 0,
        .attenuation_distance = 1e10f, .attenuation_color = { 255, 255, 255 } };
    unsigned char p[8];
    tr_center("albedo.png", bg, &t, albedo, p);
    tr_expect(p, tr_srgb(tr_lin(bg[0]) * tr_lin(albedo[0])),
        tr_srgb(tr_lin(bg[1]) * tr_lin(albedo[1])),
        tr_srgb(tr_lin(bg[2]) * tr_lin(albedo[2])), 2);
}

/** Width of the captures of the edge cases */
#define TR_EDGE_W (256)

/** Height of the captures of the edge cases */
#define TR_EDGE_H (128)

/** Render a red and blue background split at x = 0 behind a glass slab turned by 0.5 about Y,
 * and find the edge on the middle row inside the glass.
 *
 * @param file Name of the PNG
 * @param ior Index of refraction
 * @param thickness thickness_factor
 * @param scale Uniform scale of the glass
 * @param roughness Roughness of the glass
 * @param sharpness Receives the largest step of the blue share between two columns
 * @return First column whose blue share is at least a half, -1 for none
 */
static int32_t tr_edge(
    const char *file,
    float ior,
    float thickness,
    float scale,
    float roughness,
    float *sharpness)
{
    ecs_world_t *world = tr_world();
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, -10, -10, 20, 40, 255, 0, 0);
    tr_wall(world, 10, -10, 20, 40, 0, 0, 255);
    ecs_entity_t glass = tr_glass(world, 0, -4, 4, 3, 1, ior, thickness);
    ecs_set(world, glass, Rotation, {0, 0.5f, 0});
    ecs_set(world, glass, Scale, {scale, scale, scale});
    ecs_set(world, glass, PbrMaterial, {.metallic = 0, .roughness = roughness});
    unsigned char *px = tr_render(world, file, TR_EDGE_W, TR_EDGE_H);
    int32_t x, result = -1;
    float last = -1.0f;
    *sharpness = 0;
    for (x = 100; x < 176; x ++) {
        const unsigned char *p = tr_px(px, TR_EDGE_W, x, TR_EDGE_H / 2);
        float r = tr_lin(p[0]), b = tr_lin(p[2]);
        float share = b / (r + b > 1e-4f ? r + b : 1e-4f);
        if (result < 0 && share >= 0.5f) {
            result = x;
        }

        if (last >= 0.0f && fabsf(share - last) > *sharpness) {
            *sharpness = fabsf(share - last);
        }

        last = share;
    }

    stbi_image_free(px);
    ecs_fini(world);

    return result;
}

/** Without refraction the edge stays at the middle of the capture. The front face looks toward
 * +X, so the refracted ray bends toward -X: the glass samples the background left of each pixel
 * and the edge it shows moves right, further for a thicker glass.
 *
 * The thickness is in the frame of the instance, whose scale includes the size of the box:
 * scaling the glass by 2 moves the edge as far as twice the thickness. */
void Transmission_refraction_offsets_edge(void) {
    // materials/transmission
    float sharp;
    int32_t straight = tr_edge("edge_straight.png", 1, 2, 1, 0, &sharp);
    int32_t bent = tr_edge("edge_bent.png", 1.5f, 2, 1, 0, &sharp);
    int32_t thin = tr_edge("edge_thin.png", 1.5f, 1, 1, 0, &sharp);
    int32_t scaled = tr_edge("edge_scaled.png", 1.5f, 1, 2, 0, &sharp);
    printf("edge: straight %d, bent %d, thin %d, thin scaled 2 %d\n", straight, bent, thin,
        scaled);
    test_assert(straight > 0 && bent > 0 && thin > 0 && scaled > 0);

    test_assert(abs(straight - TR_EDGE_W / 2) <= 1);

    test_assert(bent > straight + 3);
    test_assert(thin > straight + 1);
    test_assert(bent > thin);

    test_assert(scaled > thin + 1);
    test_assert(abs(scaled - bent) <= 2);
}

void Transmission_roughness_blurs(void) {
    // materials/transmission
    float sharp = 0, blurred = 0;
    int32_t a = tr_edge("rough_0.png", 1, 0, 1, 0, &sharp);
    int32_t b = tr_edge("rough_1.png", 1, 0, 1, 1, &blurred);
    printf("roughness 0: edge %d step %f, roughness 1: edge %d step %f\n", a, sharp, b,
        blurred);
    test_assert(sharp > 0.4f);
    test_assert(blurred < sharp * 0.5f);
}

/** Factor f is own diffuse * (1 - f) + specular + transmitted * f, so the half is the mean of
 * the opaque and the fully transmissive surface in linear light. */
void Transmission_partial_factor_keeps_diffuse(void) {
    // materials/transmission
    float factors[3] = { 0, 0.5f, 1 };
    const char *files[3] = { "factor_0.png", "factor_half.png", "factor_1.png" };
    unsigned char p[3][4];
    int32_t k, c;
    for (k = 0; k < 3; k ++) {
        ecs_world_t *world = tr_world();
        ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
            .punctual_shadows = true, .cookies = true, .environment = true,
            .aerial_perspective = true, .shader_features.Extended.alpha_cutout = true,
            .shader_features.Extended.blend_modes = true,
            .shader_features.Extended.transmission = true,
            .shader_features.Extended.texture_blend = true,
            .shader_features.Extended.triplanar = true, .shader_features.Extended.sheen = true,
            .shader_features.Extended.clear_coat = true,
            .shader_features.Extended.anisotropy = true,
            .shader_features.Extended.diffuse_transmission = true});
        ecs_singleton_set(world, AmbientLight, {.sky = {200, 200, 200},
            .ground = {200, 200, 200}, .intensity = 0.6f});
        tr_view(world, tr_camera(world, 0), true, false);
        tr_wall(world, 0, -10, 40, 40, 40, 160, 90);
        ecs_entity_t glass = tr_glass(world, 0, -4, 3, 3, factors[k], 1.5f, 0);
        ecs_set(world, glass, Rgb, {220, 200, 180});
        ecs_set(world, glass, PbrMaterial, {.metallic = 0, .roughness = 0.5f});
        unsigned char *px = tr_render(world, files[k], TR_SIZE, TR_SIZE);
        ecs_os_memcpy(p[k], tr_px(px, TR_SIZE, TR_SIZE / 2, TR_SIZE / 2), 4);
        stbi_image_free(px);
        ecs_fini(world);
    }

    bool apart = false;
    for (c = 0; c < 3; c ++) {
        int32_t want = tr_srgb((tr_lin(p[0][c]) + tr_lin(p[2][c])) * 0.5f);
        if (abs((int32_t)p[1][c] - want) > 2) {
            printf("channel %d: got %d, want %d (factor 0 %d, factor 1 %d)\n", c, p[1][c],
                want, p[0][c], p[2][c]);
        }

        test_assert(abs((int32_t)p[1][c] - want) <= 2);
        apart |= abs((int32_t)p[0][c] - (int32_t)p[2][c]) > 20;
    }

    test_assert(apart);
}

/** Without a light the sphere shows the background, with one the highlight adds to it. */
void Transmission_specular_on_top(void) {
    // materials/transmission
    unsigned char p[2][4];
    int32_t k, c;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = tr_world();
        ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
            .punctual_shadows = true, .cookies = true, .environment = true,
            .aerial_perspective = true, .shader_features.Extended.alpha_cutout = true,
            .shader_features.Extended.blend_modes = true,
            .shader_features.Extended.transmission = true,
            .shader_features.Extended.texture_blend = true,
            .shader_features.Extended.triplanar = true, .shader_features.Extended.sheen = true,
            .shader_features.Extended.clear_coat = true,
            .shader_features.Extended.anisotropy = true,
            .shader_features.Extended.diffuse_transmission = true});
        tr_view(world, tr_camera(world, 0), true, false);
        tr_wall(world, 0, -10, 40, 40, 30, 30, 30);
        ecs_entity_t glass = ecs_new(world);
        ecs_set(world, glass, Position, {0, 0, -4});
        ecs_set(world, glass, Sphere, {.segments = 32, .smooth = true, .radius = 1.5f});
        ecs_set(world, glass, Rgb, {255, 255, 255});
        ecs_set(world, glass, PbrMaterial, {.metallic = 0, .roughness = 0.2f});
        ecs_set(world, glass, Transmission, {.transmission_factor = 1, .ior = 1,
            .thickness_factor = 0, .attenuation_distance = 1e10f,
            .attenuation_color = {255, 255, 255}});
        if (k) {
            ecs_entity_t light = ecs_new(world);
            ecs_set(world, light, Position, {0, 0, 0});
            ecs_set(world, light, PointLight, {.intensity = 40, .range = 100});
        }

        unsigned char *px = tr_render(world, k ? "specular_lit.png" : "specular_dark.png",
            TR_SIZE, TR_SIZE);
        ecs_os_memcpy(p[k], tr_px(px, TR_SIZE, TR_SIZE / 2, TR_SIZE / 2), 4);
        stbi_image_free(px);
        ecs_fini(world);
    }

    tr_expect(p[0], 30, 30, 30, 3);
    for (c = 0; c < 3; c ++) {
        test_assert(p[1][c] > p[0][c] + 30);
    }
}

/** Two blended quads around a deep glass box (front face z -3, origin z -6): one behind its
 * front face that sorts after it shows over it, as the glass writes no depth; one behind it that
 * sorts before it is in the snapshot and shows through the glass as it does beside it.
 *
 * Elsewhere the glass shows the green wall.
 *
 * The glass casts no shadow: the ground around it looks as it does without it, while an opaque
 * box in its place shades it. */
void Transmission_no_depth_write_no_shadow(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -12, 40, 40, 0, 200, 0);

    ecs_entity_t glass = tr_glass(world, 0, -6, 2, 4, 1, 1, 0);
    ecs_set(world, glass, Box, {2, 4, 6});

    ecs_entity_t late = tr_wall(world, 0.8f, -4.5f, 1, 1, 200, 0, 0);
    ecs_set(world, late, Opacity, {0.5f});

    ecs_entity_t early = tr_wall(world, -2.5f, -9.5f, 4, 2, 0, 0, 200);
    ecs_set(world, early, Opacity, {0.5f});
    unsigned char *px = tr_render(world, "order.png", TR_SIZE, TR_SIZE);

    tr_expect(tr_px(px, TR_SIZE, 32, 32), 0, 200, 0, 3);
    tr_expect(tr_px(px, TR_SIZE, 20, 32), 0, tr_srgb(tr_lin(200) * 0.5f),
        tr_srgb(tr_lin(200) * 0.5f), 3);

    int32_t x = 32 + (int32_t)(0.8f / 4.5f / 0.57735f * 32.0f);
    tr_expect(tr_px(px, TR_SIZE, x, 32), tr_srgb(tr_lin(200) * 0.5f),
        tr_srgb(tr_lin(200) * 0.5f), 0, 3);

    tr_expect(tr_px(px, TR_SIZE, 10, 32), 0, tr_srgb(tr_lin(200) * 0.5f),
        tr_srgb(tr_lin(200) * 0.5f), 3);
    stbi_image_free(px);
    ecs_fini(world);

    const char *head =
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.light\n"
        "camera { Position: {0, 12, 0}\n Rotation: {-1.5708, 0, 0}\n"
        "  Camera: {fov: 40, near_: 0.1, far_: 100} }\n"
        "main_view { View: {camera: camera, clear: {enabled: true, color: {0, 0, 0, 255}},"
        " shadows: {enabled: true}} }\n"
        "$ { AmbientLight: {intensity: 0} }\n"
        "sun { Rotation: {0.9, 0.7, 0}\n DirectionalLight: {intensity: 3} }\n"
        "ground { Position: {0, -0.1, 0}\n Box: {30, 0.2, 30}\n Rgb: {200, 200, 200} }\n";
    char *bare = flecs_asprintf("%s", head);
    char *with_glass = flecs_asprintf("%s"
        "glass { Position: {0, 3, 0}\n Box: {2, 2, 2}\n Rgb: {255, 255, 255}\n"
        "  Transmission: {transmission_factor: 1, ior: 1} }\n", head);
    char *with_box = flecs_asprintf("%s"
        "caster { Position: {0, 3, 0}\n Box: {2, 2, 2}\n Rgb: {255, 255, 255} }\n", head);
    unsigned char *a = tr_script(bare, "shadow_bare.png", TR_SIZE, TR_SIZE);
    unsigned char *b = tr_script(with_glass, "shadow_glass.png", TR_SIZE, TR_SIZE);
    unsigned char *c = tr_script(with_box, "shadow_box.png", TR_SIZE, TR_SIZE);
    int32_t i, k, glass_diff = 0, box_diff = 0;
    for (i = 0; i < TR_SIZE * TR_SIZE; i ++) {
        int32_t px_x = i % TR_SIZE, px_y = i / TR_SIZE;
        if (abs(px_x - TR_SIZE / 2) <= 14 && abs(px_y - TR_SIZE / 2) <= 14) {
            continue;
        }

        for (k = 0; k < 3; k ++) {
            glass_diff += abs((int)a[i * 4 + k] - (int)b[i * 4 + k]) > 2;
            box_diff += abs((int)a[i * 4 + k] - (int)c[i * 4 + k]) > 30;
        }
    }

    printf("ground pixels changed: %d by the glass, %d by the box\n", glass_diff, box_diff);
    test_int(glass_diff, 0);
    test_assert(box_diff > 0);
    stbi_image_free(a);
    stbi_image_free(b);
    stbi_image_free(c);
    ecs_os_free(bare);
    ecs_os_free(with_glass);
    ecs_os_free(with_box);
}

/** Snapshots of transmission a view takes at most in a frame, BOX_GPU_TRANSMISSION_SNAPSHOTS of
 * src/render/gpu.h */
#define TR_SNAPSHOTS (4)

/** Add an emissive quad facing the camera, alpha blended at an opacity, at a height.
 *
 * @param world The world
 * @param x X of its center
 * @param y Y of its center
 * @param z Z of its center
 * @param size Width and height
 * @param r Red of the emission
 * @param g Green of the emission
 * @param b Blue of the emission
 * @param opacity Opacity, 1 for an opaque wall
 * @return The quad
 */
static ecs_entity_t tr_quad(
    ecs_world_t *world,
    float x,
    float y,
    float z,
    float size,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    float opacity)
{
    ecs_entity_t e = tr_wall(world, x, z, size, size, r, g, b);
    ecs_set(world, e, Position, {x, y, z});
    if (opacity < 1) {
        ecs_set(world, e, Opacity, {opacity});
    }

    return e;
}

/** Count the snapshots and forward passes of a scene over 2 frames once it renders.
 *
 * @param world The world, with its view and shapes
 * @param snapshots Receives the snapshots of the 2 frames
 * @param passes Receives the forward passes of the 2 frames
 */
static void tr_count(
    ecs_world_t *world,
    int32_t *snapshots,
    int32_t *passes)
{
    tr_surface(world, TR_SIZE, TR_SIZE, NULL);
    box_render_gpu_stats_t before = tr_frames(world, 4);
    box_render_gpu_stats_t after = tr_frames(world, 2);
    *snapshots = after.transmission_snapshots - before.transmission_snapshots;
    *passes = after.forward_passes - before.forward_passes;
    test_int(after.errors, 0);
}

/** A red quad at half opacity behind the glass shows through it as it does beside the glass,
 * and as an opaque red wall of half the emission does through the glass: the snapshot is taken
 * right before the glass, after the blended quad sorted behind it. */
void Transmission_blended_behind_shows(void) {
    // materials/transmission
    unsigned char *images[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = tr_world();
        ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
            .punctual_shadows = true, .cookies = true, .environment = true,
            .aerial_perspective = true, .shader_features.Extended.alpha_cutout = true,
            .shader_features.Extended.blend_modes = true,
            .shader_features.Extended.transmission = true,
            .shader_features.Extended.texture_blend = true,
            .shader_features.Extended.triplanar = true, .shader_features.Extended.sheen = true,
            .shader_features.Extended.clear_coat = true,
            .shader_features.Extended.anisotropy = true,
            .shader_features.Extended.diffuse_transmission = true});
        tr_view(world, tr_camera(world, 0), true, false);
        tr_glass(world, 0, -4, 2, 2, 1, 1, 0);
        ecs_entity_t quad = tr_quad(world, 0, 0, -7, 8, 200, 0, 0, k ? 1 : 0.5f);
        if (k) {
            ecs_set(world, quad, Emissive, {.strength = 0.5f, .color = {200, 0, 0}});
        }

        images[k] = tr_render(world, k ? "behind_opaque.png" : "behind_blend.png",
            TR_SIZE, TR_SIZE);
        ecs_fini(world);
    }

    const unsigned char *blended = tr_px(images[0], TR_SIZE, 32, 32);
    const unsigned char *opaque = tr_px(images[1], TR_SIZE, 32, 32);
    tr_expect(blended, tr_srgb(tr_lin(200) * 0.5f), 0, 0, 3);
    tr_expect(blended, opaque[0], opaque[1], opaque[2], 3);
    tr_expect(tr_px(images[0], TR_SIZE, 12, 32), blended[0], blended[1], blended[2], 3);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

/** A large red tinted glass behind a small white one shows through it: the green and red wall
 * turns red through both panes as it does beside the front one.
 *
 * Glass right behind glass shares the snapshot of the front pane (adjacent_runs_one_snapshot),
 * so a small blended chip sorted between the two panes, at the top left away from the pixels
 * checked, makes the front pane take a second snapshot that holds the back one. */
void Transmission_transmissive_behind_shows(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -10, 40, 40, 200, 200, 0);
    tr_glass(world, 0, -4, 2, 2, 1, 1, 0);
    ecs_entity_t behind = tr_glass(world, 0, -7, 6, 6, 1, 1, 0);
    ecs_set(world, behind, Rgb, {255, 0, 0});
    tr_quad(world, -2.8f, 2.4f, -5.5f, 0.2f, 0, 0, 200, 0.5f);
    box_render_gpu_stats_t before = tr_frames(world, 0);
    unsigned char *px = tr_render(world, "behind_glass.png", TR_SIZE, TR_SIZE);
    box_render_gpu_stats_t after = tr_frames(world, 1);

    tr_expect(tr_px(px, TR_SIZE, 12, 32), 200, 0, 0, 3);
    tr_expect(tr_px(px, TR_SIZE, 32, 32), 200, 0, 0, 3);
    tr_expect(tr_px(px, TR_SIZE, 2, 32), 200, 200, 0, 3);
    stbi_image_free(px);
    before = after;
    after = tr_frames(world, 1);
    test_int(after.transmission_snapshots - before.transmission_snapshots, 2);
    test_int(after.errors, 0);
    ecs_fini(world);
}

/** A red quad at half opacity in front of the glass draws over it, with the pixels it has over
 * the wall without the glass: the glass of ior 1 shows the wall unchanged under it. */
void Transmission_blended_in_front_draws_over(void) {
    // materials/transmission
    unsigned char *images[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = tr_world();
        ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
            .punctual_shadows = true, .cookies = true, .environment = true,
            .aerial_perspective = true, .shader_features.Extended.alpha_cutout = true,
            .shader_features.Extended.blend_modes = true,
            .shader_features.Extended.transmission = true,
            .shader_features.Extended.texture_blend = true,
            .shader_features.Extended.triplanar = true, .shader_features.Extended.sheen = true,
            .shader_features.Extended.clear_coat = true,
            .shader_features.Extended.anisotropy = true,
            .shader_features.Extended.diffuse_transmission = true});
        tr_view(world, tr_camera(world, 0), true, false);
        tr_wall(world, 0, -10, 40, 40, 0, 200, 0);
        if (k) {
            tr_glass(world, 0, -4, 2, 2, 1, 1, 0);
        }

        tr_quad(world, 0, 0, -3, 1, 200, 0, 0, 0.5f);
        images[k] = tr_render(world, k ? "front_glass.png" : "front_bare.png",
            TR_SIZE, TR_SIZE);
        ecs_fini(world);
    }

    const unsigned char *bare = tr_px(images[0], TR_SIZE, 32, 32);
    tr_expect(bare, tr_srgb(tr_lin(200) * 0.5f), tr_srgb(tr_lin(200) * 0.5f), 0, 3);
    tr_expect(tr_px(images[1], TR_SIZE, 32, 32), bare[0], bare[1], bare[2], 3);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

/** One glass pane between a blended quad behind it and one in front of it takes one snapshot
 * a frame, and its view one more forward pass than without the glass. */
void Transmission_one_snapshot_single_layer(void) {
    // materials/transmission
    int32_t snapshots[2], passes[2], k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = tr_world();
        ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
            .punctual_shadows = true, .cookies = true, .environment = true,
            .aerial_perspective = true, .shader_features.Extended.alpha_cutout = true,
            .shader_features.Extended.blend_modes = true,
            .shader_features.Extended.transmission = true,
            .shader_features.Extended.texture_blend = true,
            .shader_features.Extended.triplanar = true, .shader_features.Extended.sheen = true,
            .shader_features.Extended.clear_coat = true,
            .shader_features.Extended.anisotropy = true,
            .shader_features.Extended.diffuse_transmission = true});
        tr_view(world, tr_camera(world, 0), true, false);
        tr_wall(world, 0, -10, 40, 40, 0, 200, 0);
        tr_quad(world, 0, 0, -7, 4, 200, 0, 0, 0.5f);
        tr_quad(world, 0.5f, 0, -3, 1, 0, 0, 200, 0.5f);
        if (k) {
            tr_glass(world, 0, -4, 2, 2, 1, 1, 0);
        }

        tr_count(world, &snapshots[k], &passes[k]);
        ecs_fini(world);
    }

    printf("2 frames: %d forward passes without the glass, %d with, %d snapshots\n",
        passes[0], passes[1], snapshots[1]);
    test_int(snapshots[0], 0);
    test_int(snapshots[1], 2);
    test_int(passes[1] - passes[0], 2);
}

/** The scene of layers_take_snapshots: a green wall, a white glass behind, a red quad at half
 * opacity, then a small white glass in front, all over the center.
 *
 * @param world The world, with its view
 */
static void tr_layers(
    ecs_world_t *world)
{
    tr_wall(world, 0, -10, 40, 40, 0, 200, 0);
    tr_glass(world, 0, -8, 3, 3, 1, 1, 0);
    tr_quad(world, 0, 0, -6, 2, 200, 0, 0, 0.5f);
    tr_glass(world, 0, -4, 1, 1, 1, 1, 0);
}

/** Two overlapping glass panes with a blended quad between them take a snapshot each, and the
 * quad shows through the front pane as it does beside it. */
void Transmission_layers_take_snapshots(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    tr_view(world, tr_camera(world, 0), true, false);
    tr_layers(world);
    box_render_gpu_stats_t before = tr_frames(world, 0);
    unsigned char *px = tr_render(world, "layers.png", TR_SIZE, TR_SIZE);
    box_render_gpu_stats_t after = tr_frames(world, 0);
    test_int(after.errors, before.errors);

    int32_t half = tr_srgb(tr_lin(200) * 0.5f);
    tr_expect(tr_px(px, TR_SIZE, 32, 32), half, half, 0, 3);
    tr_expect(tr_px(px, TR_SIZE, 26, 32), half, half, 0, 3);
    tr_expect(tr_px(px, TR_SIZE, 2, 2), 0, 200, 0, 1);
    stbi_image_free(px);

    before = after;
    after = tr_frames(world, 2);
    test_int(after.transmission_snapshots - before.transmission_snapshots, 4);
    test_int(after.errors, 0);
    ecs_fini(world);
}

/** Six glass panes interleaved with six blended quads, all over the center, take the budget of
 * snapshots a frame and no more; the front panes sample the last one. */
void Transmission_snapshot_budget(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -12, 40, 40, 0, 200, 0);
    int32_t i, snapshots, passes;
    for (i = 0; i < 6; i ++) {
        float z = -10.0f + (float)i * 1.2f;
        tr_glass(world, 0, z, 2, 2, 1, 1, 0);
        tr_quad(world, 0, 0, z + 0.6f, 1.5f, 200, 0, 0, 0.2f);
    }

    tr_count(world, &snapshots, &passes);
    printf("2 frames: %d snapshots, %d forward passes\n", snapshots, passes);
    test_int(snapshots, 2 * TR_SNAPSHOTS);
    ecs_fini(world);
}

/** A glass box and a glass sphere side by side, two batches sorted next to each other with
 * nothing drawn between them, share one snapshot. */
void Transmission_adjacent_runs_one_snapshot(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -10, 40, 40, 0, 200, 0);
    tr_quad(world, 0, 0, -8, 2, 200, 0, 0, 0.5f);
    tr_glass(world, -1.2f, -4, 1, 1, 1, 1, 0);
    ecs_entity_t sphere = tr_glass(world, 1.2f, -4.5f, 1, 1, 1, 1, 0);
    ecs_remove(world, sphere, Box);
    ecs_set(world, sphere, Sphere, {.segments = 16, .smooth = true, .radius = 0.5f});
    int32_t snapshots, passes;
    tr_count(world, &snapshots, &passes);
    test_int(snapshots, 2);
    ecs_fini(world);
}

/** Add a large white sphere of ior 1 in front of the camera, which shows what it encloses
 * unchanged.
 *
 * @param world The world
 * @param z Z of its center
 * @param radius Radius
 * @return The sphere
 */
static ecs_entity_t tr_bubble(
    ecs_world_t *world,
    float z,
    float radius)
{
    ecs_entity_t e = tr_glass(world, 0, z, 1, 1, 1, 1, 0);
    ecs_remove(world, e, Box);
    ecs_set(world, e, Sphere, {.segments = 32, .smooth = true, .radius = radius});

    return e;
}

/** A large transmissive sphere around a red glass box that sits behind the center of the
 * sphere, with nothing else blended between them, shows the red glass through it as it is
 * without the sphere: the sphere takes a snapshot that holds the glass. */
void Transmission_enclosing_sphere_shows_glass(void) {
    // materials/transmission
    unsigned char *images[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = tr_world();
        tr_view(world, tr_camera(world, 0), true, false);
        tr_wall(world, 0, -14, 60, 60, 200, 200, 0);
        ecs_entity_t glass = tr_glass(world, 0, -8, 3, 3, 1, 1, 0);
        ecs_set(world, glass, Rgb, {255, 0, 0});
        if (k) {
            tr_bubble(world, -6, 5);
        }

        images[k] = tr_render(world, k ? "bubble_glass.png" : "bubble_glass_bare.png",
            TR_SIZE, TR_SIZE);
        ecs_fini(world);
    }

    const unsigned char *bare = tr_px(images[0], TR_SIZE, 32, 32);
    tr_expect(bare, 200, 0, 0, 3);
    tr_expect(tr_px(images[1], TR_SIZE, 32, 32), bare[0], bare[1], bare[2], 3);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

/** Render a large transmissive sphere around more glass panes interleaved with blended quads
 * than the budget of snapshots, all behind its center, and check that it shows the front layers
 * as they are without the sphere: the sphere does not sample a snapshot taken before them.
 *
 * The quads behind the last two panes stay off the center pixel, as the panes in the middle
 * that the budget leaves without a snapshot of their own erase them.
 *
 * @param front Small overlapping glass panes inside the sphere in front of its center, off the
 * center pixel, each with a small quad in front, so each is a draw that needs a snapshot after
 * the sphere
 * @param file Name of the PNG with the sphere
 */
static void tr_bubble_layers(
    int32_t front,
    const char *file)
{
    unsigned char *images[2];
    int32_t i, k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = tr_world();
        tr_view(world, tr_camera(world, 0), true, false);
        tr_wall(world, 0, -14, 60, 60, 0, 200, 0);
        for (i = 0; i < 6; i ++) {
            float z = -10.5f + (float)i * 0.7f;
            tr_glass(world, 0, z, 2, 2, 1, 1, 0);
            tr_quad(world, 0, i < 4 ? 0.8f : 0.0f, z + 0.35f, i < 4 ? 0.5f : 1.5f,
                i == 5 ? 0 : 200, 0, i == 5 ? 200 : 0, 0.5f);
        }

        for (i = 0; i < front; i ++) {
            float z = -3.0f + (float)i * 0.4f;
            ecs_entity_t pane = tr_glass(world, 0.8f, z, 0.5f, 0.5f, 1, 1, 0);
            ecs_set(world, pane, Position, {0.8f, 0.8f, z});
            tr_quad(world, 0.8f, 0.8f, z + 0.2f, 0.2f, 200, 0, 0, 0.5f);
        }

        if (k) {
            tr_bubble(world, -6, 5);
        }

        images[k] = tr_render(world, k ? file : "bubble_budget_bare.png", TR_SIZE, TR_SIZE);
        ecs_fini(world);
    }

    const unsigned char *bare = tr_px(images[0], TR_SIZE, 32, 32);
    test_assert(bare[2] > 100);
    tr_expect(tr_px(images[1], TR_SIZE, 32, 32), bare[0], bare[1], bare[2], 3);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

/** A large transmissive sphere around more glass panes interleaved with blended quads than the
 * budget of snapshots, all behind its center, shows the front layers through it as they are
 * without the sphere. */
void Transmission_enclosing_sphere_after_budget(void) {
    // materials/transmission
    tr_bubble_layers(0, "bubble_budget.png");
}

/** The sphere of enclosing_sphere_after_budget with more small glass panes in front of its
 * center than the budget, each needing a snapshot after it, still gets a snapshot of its own:
 * the budget goes to the draws that cover most of the image, not to the last ones. */
void Transmission_enclosing_sphere_before_glass(void) {
    // materials/transmission
    tr_bubble_layers(TR_SNAPSHOTS, "bubble_before_glass.png");
}

/** The scene of layers_take_snapshots in a view that culls by occlusion (unlit, an early and a
 * late pass, which takes the snapshots) and in one with a depth prepass (lit) looks as it does
 * in an unlit view without either.
 *
 * Without lights and with a black ambient the lit view shows only the emission. */
void Transmission_occlusion_view_layers(void) {
    // materials/transmission
    unsigned char *images[3];
    int32_t k, i, c, wrong = 0;
    for (k = 0; k < 3; k ++) {
        ecs_world_t *world = tr_world();
        ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
            .punctual_shadows = true, .cookies = true, .environment = true,
            .aerial_perspective = true, .shader_features.Extended.alpha_cutout = true,
            .shader_features.Extended.blend_modes = true,
            .shader_features.Extended.transmission = true,
            .shader_features.Extended.texture_blend = true,
            .shader_features.Extended.triplanar = true, .shader_features.Extended.sheen = true,
            .shader_features.Extended.clear_coat = true,
            .shader_features.Extended.anisotropy = true,
            .shader_features.Extended.diffuse_transmission = true});
        tr_view(world, tr_camera(world, 0), k == 2, k == 1);

        tr_layers(world);
        const char *file = k == 0 ? "layers_plain.png" :
            (k == 1 ? "layers_occlusion.png" : "layers_prepass.png");
        box_render_gpu_stats_t before = tr_frames(world, 0);
        images[k] = tr_render(world, file, TR_SIZE, TR_SIZE);
        box_render_gpu_stats_t after = tr_frames(world, 1);
        test_int(after.errors, before.errors);
        ecs_fini(world);
    }

    for (k = 1; k < 3; k ++) {
        for (i = 0; i < TR_SIZE * TR_SIZE; i ++) {
            bool differs = false;
            for (c = 0; c < 3; c ++) {
                differs |= abs((int)images[0][i * 4 + c] - (int)images[k][i * 4 + c]) > 3;
            }

            if (differs && wrong < 8) {
                printf("image %d, pixel %d, %d differs: %d %d %d, want %d %d %d\n", k,
                    i % TR_SIZE, i / TR_SIZE, images[k][i * 4], images[k][i * 4 + 1],
                    images[k][i * 4 + 2], images[0][i * 4], images[0][i * 4 + 1],
                    images[0][i * 4 + 2]);
            }

            wrong += differs;
        }
    }

    test_int(wrong, 0);
    for (k = 0; k < 3; k ++) {
        stbi_image_free(images[k]);
    }
}

/** A glass with a DiffuseTransmission blur in front of a red quad at half opacity, in front of
 * a green wall. Through the glass red and green mix, and nothing is invalid.
 *
 * The quad writes no depth, so the depth copy of the snapshot holds the wall behind it: the quad
 * blurs by its distance to the wall, as the wall does, not by its own distance to the glass. */
void Transmission_diffuse_blur_behind_blended(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -10, 40, 40, 0, 200, 0);
    tr_quad(world, 0, 0, -5, 1, 200, 0, 0, 0.5f);
    ecs_entity_t glass = tr_glass(world, 0, -4, 3, 3, 1, 1, 0);
    ecs_set(world, glass, DiffuseTransmission, {.factor = 0, .color = {255, 255, 255},
        .blur = 0.3f});
    box_render_gpu_stats_t before = tr_frames(world, 0);
    unsigned char *px = tr_render(world, "blur_blended.png", TR_SIZE, TR_SIZE);
    box_render_gpu_stats_t after = tr_frames(world, 1);
    test_int(after.errors, before.errors);

    const unsigned char *center = tr_px(px, TR_SIZE, 32, 32);
    printf("center through the blur: %d %d %d\n", center[0], center[1], center[2]);
    test_assert(center[0] > 5);
    test_assert(center[1] > 20);
    test_assert(center[0] < tr_srgb(tr_lin(200) * 0.5f) + 3);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A sunlit atmosphere over an emissive red box. A literal, because no world exists yet to
 * give flecs_asprintf its allocator. */
#define TR_SKY_HEAD \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.material\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.light\n" \
    "using box.atmosphere\n" \
    "camera { Position: {0, 2, 0}\n Camera: {fov: 60, near_: 0.1, far_: 0} }\n" \
    "sun { Rotation: {0.6, 0.5, 0}\n" \
    "  DirectionalLight: {intensity: 3 * flecs.script.math.PI} }\n" \
    "sky { Atmosphere: {units_per_km: 100, sun: sun} }\n" \
    "main_view { View: {camera: camera, clear: {enabled: true, color: {0, 0, 0, 255}}," \
    " effects: [{effect: sky}]} }\n" \
    "red { Position: {0, 1, -8}\n Box: {3, 1.6, 0.5}\n Rgb: {0, 0, 0}\n" \
    "  PbrMaterial: {metallic: 1, roughness: 1}\n" \
    "  Emissive: {strength: 1, color: {255, 0, 0}} }\n"

/** Below the middle the red box, above it the sky, both through the glass. The glass adds its
 * own reflection of the sky on top, which lifts the dark channels of the box: the specular of a
 * dielectric is about 4% of the sky at normal incidence. */
void Transmission_opaque_and_sky_in_snapshot(void) {
    // materials/transmission
    const char *head = TR_SKY_HEAD;
    const char *with_glass = TR_SKY_HEAD
        "glass { Position: {0, 2, -3}\n Box: {2.4, 2.4, 0.1}\n Rgb: {255, 255, 255}\n"
        "  PbrMaterial: {roughness: 0}\n"
        "  Transmission: {transmission_factor: 1, ior: 1} }\n";
    unsigned char *a = tr_script(head, "sky_bare.png", TR_SIZE, TR_SIZE);
    unsigned char *b = tr_script(with_glass, "sky_glass.png", TR_SIZE, TR_SIZE);

    const unsigned char *box = tr_px(a, TR_SIZE, 32, 38);
    const unsigned char *sky = tr_px(a, TR_SIZE, 32, 22);
    test_assert(box[0] > box[2] + 40);
    test_assert(sky[2] > sky[0]);
    tr_expect(tr_px(b, TR_SIZE, 32, 22), sky[0], sky[1], sky[2], 12);

    const unsigned char *seen = tr_px(b, TR_SIZE, 32, 38);
    int32_t c;
    for (c = 0; c < 3; c ++) {
        float extra = tr_lin(seen[c]) - tr_lin(box[c]);
        printf("channel %d: box %d, through the glass %d\n", c, box[c], seen[c]);
        test_assert(extra >= -0.01f);
        test_assert(extra <= 0.06f * tr_lin(sky[c]) + 0.01f);
    }

    stbi_image_free(a);
    stbi_image_free(b);
}

/** A second view of a scene without transmissive shapes copies nothing, and once the glass
 * loses its Transmission no view copies its image.
 *
 * Both views are lit and draw opaque meshes, so they draw a depth prepass and one forward pass
 * each (phase 3); the view with the glass continues in one more after its snapshot. */
void Transmission_snapshot_counter(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -10, 40, 40, 200, 120, 60);
    ecs_entity_t glass = tr_glass(world, 0, -4, 3, 3, 1, 1, 0);

    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_entity_t other = tr_view(world, tr_camera(world, scene), true, false);
    ecs_set(world, other, View, {.camera = ecs_get(world, other, View)->camera,
        .clear = {.enabled = false}, .lighting = {.enabled = true}, .order = 1});
    ecs_entity_t box = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, box, Position, {0, 0, -4});
    ecs_set(world, box, Box, {1, 1, 1});
    tr_surface(world, TR_SIZE, TR_SIZE, NULL);

    box_render_gpu_stats_t before = tr_frames(world, 3);
    box_render_gpu_stats_t after = tr_frames(world, 1);
    test_int(after.transmission_snapshots - before.transmission_snapshots, 1);
    test_int(after.passes - before.passes, 2);
    test_int(after.forward_passes - before.forward_passes, 3);
    after = tr_frames(world, 2);
    test_int(after.transmission_snapshots - before.transmission_snapshots, 3);
    test_int(after.forward_passes - before.forward_passes, 9);
    test_int(after.errors, before.errors);

    ecs_remove(world, glass, Transmission);
    tr_frames(world, 2);
    before = tr_frames(world, 0);
    after = tr_frames(world, 3);
    test_int(after.transmission_snapshots - before.transmission_snapshots, 0);
    test_int(after.passes - before.passes, 6);
    test_int(after.forward_passes - before.forward_passes, 6);
    test_int(after.errors, before.errors);

    ecs_fini(world);
}

void Transmission_resize_recreates_snapshot(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -10, 40, 40, 200, 120, 60);
    tr_glass(world, 0, -4, 3, 3, 1, 1, 0);
    ecs_entity_t s = tr_surface(world, TR_SIZE, TR_SIZE, "resize_a.png");
    box_render_gpu_stats_t before = tr_frames(world, 3);
    unsigned char *px = tr_load(world, "resize_a.png", TR_SIZE, TR_SIZE);
    tr_expect(tr_px(px, TR_SIZE, 32, 32), 200, 120, 60, 2);
    stbi_image_free(px);

    ecs_set(world, s, Surface, {.width = 96, .height = 48,
        .write_to_file = "resize_b.png", .resolution_scale = 1});
    box_render_gpu_stats_t after = tr_frames(world, 3);
    px = tr_load(world, "resize_b.png", 96, 48);
    tr_expect(tr_px(px, 96, 48, 24), 200, 120, 60, 2);
    stbi_image_free(px);
    test_int(after.errors, before.errors);
    test_int(after.transmission_snapshots - before.transmission_snapshots, 3);

    ecs_fini(world);
}

/** The second view renders last and sees its own snapshot, not that of the first. */
void Transmission_two_views_one_target(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t front = tr_camera(world, 0);
    ecs_entity_t back = tr_camera(world, 0);
    ecs_set(world, back, Rotation, {0, 3.14159265f, 0});
    tr_view(world, front, true, false);
    ecs_entity_t second = tr_view(world, back, true, false);
    ecs_set(world, second, View, {.camera = back,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}, .order = 1});
    tr_wall(world, 0, -10, 40, 40, 200, 0, 0);
    tr_glass(world, 0, -4, 3, 3, 1, 1, 0);
    tr_wall(world, 0, 10, 40, 40, 0, 200, 0);
    tr_glass(world, 0, 4, 3, 3, 1, 1, 0);
    box_render_gpu_stats_t before = tr_frames(world, 0);
    unsigned char *px = tr_render(world, "two_views.png", TR_SIZE, TR_SIZE);
    box_render_gpu_stats_t after = tr_frames(world, 1);

    tr_expect(tr_px(px, TR_SIZE, 32, 32), 0, 200, 0, 3);
    tr_expect(tr_px(px, TR_SIZE, 2, 2), 0, 200, 0, 1);
    stbi_image_free(px);
    test_int(after.errors, before.errors);
    before = after;
    after = tr_frames(world, 1);
    test_int(after.transmission_snapshots - before.transmission_snapshots, 2);

    ecs_fini(world);
}

/** An unlit view draws the emission of the wall as its color, behind the glass as well. A view
 * with effects renders into its HDR image, which the snapshot copies. */
void Transmission_view_without_effects(void) {
    // materials/transmission
    uint8_t bg[3] = { 200, 160, 120 };
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = tr_world();
        ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
            .punctual_shadows = true, .cookies = true, .environment = true,
            .aerial_perspective = true, .shader_features.Extended.alpha_cutout = true,
            .shader_features.Extended.blend_modes = true,
            .shader_features.Extended.transmission = true,
            .shader_features.Extended.texture_blend = true,
            .shader_features.Extended.triplanar = true, .shader_features.Extended.sheen = true,
            .shader_features.Extended.clear_coat = true,
            .shader_features.Extended.anisotropy = true,
            .shader_features.Extended.diffuse_transmission = true});
        tr_view(world, tr_camera(world, 0), k == 1, false);
        tr_wall(world, 0, -10, 40, 40, bg[0], bg[1], bg[2]);
        ecs_entity_t glass = tr_glass(world, 0, -4, 3, 3, 1, 1.5f, 0);
        ecs_set(world, glass, Rgb, {255, 255, 255});
        unsigned char *px = tr_render(world, k ? "plain_lit.png" : "plain_unlit.png",
            TR_SIZE, TR_SIZE);

        tr_expect(tr_px(px, TR_SIZE, 32, 32), tr_srgb(tr_lin(bg[0]) * 0.96f),
            tr_srgb(tr_lin(bg[1]) * 0.96f), tr_srgb(tr_lin(bg[2]) * 0.96f), 3);
        stbi_image_free(px);
        ecs_fini(world);
    }

    const char *script =
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.light\n"
        "using box.effects\n"
        "gtao { Gtao: {} }\n"
        "camera { Position: {0, 0, 0}\n Camera: {fov: 60, near_: 0.1, far_: 1000} }\n"
        "main_view { View: {camera: camera, clear: {enabled: true, color: {0, 0, 0, 255}},"
        " effects: [{effect: gtao}]} }\n"
        "$ { AmbientLight: {intensity: 0} }\n"
        "off { Position: {1000, 1000, 1000}\n PointLight: {intensity: 0, range: 0.1} }\n"
        "wall { Position: {0, 0, -10}\n Box: {40, 40, 0.1}\n Rgb: {0, 0, 0}\n"
        "  PbrMaterial: {metallic: 1, roughness: 1}\n"
        "  Emissive: {strength: 1, color: {200, 160, 120}} }\n"
        "glass { Position: {0, 0, -4}\n Box: {3, 3, 0.2}\n Rgb: {255, 255, 255}\n"
        "  PbrMaterial: {roughness: 0}\n"
        "  Transmission: {transmission_factor: 1, ior: 1} }\n";
    unsigned char *px = tr_script(script, "effects.png", TR_SIZE, TR_SIZE);
    const unsigned char *wall = tr_px(px, TR_SIZE, 2, 2);
    tr_expect(tr_px(px, TR_SIZE, 32, 32), wall[0], wall[1], wall[2], 3);
    stbi_image_free(px);
}

/** An opaque box behind the glass, which the occlusion test keeps, shows through it. */
void Transmission_occlusion_view_transmits(void) {
    // materials/transmission
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = tr_world();
        ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
            .punctual_shadows = true, .cookies = true, .environment = true,
            .aerial_perspective = true, .shader_features.Extended.alpha_cutout = true,
            .shader_features.Extended.blend_modes = true,
            .shader_features.Extended.transmission = true,
            .shader_features.Extended.texture_blend = true,
            .shader_features.Extended.triplanar = true, .shader_features.Extended.sheen = true,
            .shader_features.Extended.clear_coat = true,
            .shader_features.Extended.anisotropy = true,
            .shader_features.Extended.diffuse_transmission = true});
        tr_view(world, tr_camera(world, 0), k == 1, true);
        tr_wall(world, 0, -10, 40, 40, 200, 120, 60);
        tr_glass(world, 0, -4, 3, 3, 1, 1, 0);

        ecs_entity_t box = tr_wall(world, 0.8f, -7, 1, 1, 0, 0, 200);
        ecs_set(world, box, Box, {1, 1, 1});
        box_render_gpu_stats_t before = tr_frames(world, 0);
        unsigned char *px = tr_render(world, k ? "occlusion_lit.png" : "occlusion_unlit.png",
            TR_SIZE, TR_SIZE);
        box_render_gpu_stats_t after = tr_frames(world, 0);
        tr_expect(tr_px(px, TR_SIZE, 26, 32), 200, 120, 60, 2);
        int32_t x = 32 + (int32_t)(0.8f / 7.0f / 0.57735f * 32.0f);
        tr_expect(tr_px(px, TR_SIZE, x, 32), 0, 0, 200, 3);
        test_assert(after.transmission_snapshots - before.transmission_snapshots >= 3);
        stbi_image_free(px);
        ecs_fini(world);
    }
}

/** A view that culls by occlusion without a depth prepass (unlit) draws an early and a late
 * forward pass, one with a prepass (lit) a single one; the glass adds the pass its late or only
 * pass continues in after the snapshot. */
void Transmission_forward_passes_by_phase(void) {
    // materials/transmission
    int32_t k;
    for (k = 0; k < 2; k ++) {
        bool lit = k == 1;
        int32_t passes = lit ? 1 : 2;
        ecs_world_t *world = tr_world();
        ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
            .punctual_shadows = true, .cookies = true, .environment = true,
            .aerial_perspective = true, .shader_features.Extended.alpha_cutout = true,
            .shader_features.Extended.blend_modes = true,
            .shader_features.Extended.transmission = true,
            .shader_features.Extended.texture_blend = true,
            .shader_features.Extended.triplanar = true, .shader_features.Extended.sheen = true,
            .shader_features.Extended.clear_coat = true,
            .shader_features.Extended.anisotropy = true,
            .shader_features.Extended.diffuse_transmission = true});
        tr_view(world, tr_camera(world, 0), lit, true);
        tr_wall(world, 0, -10, 40, 40, 200, 120, 60);
        ecs_entity_t glass = tr_glass(world, 0, -4, 3, 3, 1, 1, 0);
        tr_surface(world, TR_SIZE, TR_SIZE, NULL);

        box_render_gpu_stats_t before = tr_frames(world, 4);
        box_render_gpu_stats_t after = tr_frames(world, 2);
        printf("%s view: %d forward passes, %d prepasses in 2 frames with the glass\n",
            lit ? "lit" : "unlit", after.forward_passes - before.forward_passes,
            after.prepasses - before.prepasses);
        test_int(after.transmission_snapshots - before.transmission_snapshots, 2);
        test_int(after.prepasses - before.prepasses, lit ? 4 : 0);
        test_int(after.forward_passes - before.forward_passes, 2 * (passes + 1));

        ecs_remove(world, glass, Transmission);
        tr_frames(world, 2);
        before = tr_frames(world, 0);
        after = tr_frames(world, 2);
        test_int(after.transmission_snapshots - before.transmission_snapshots, 0);
        test_int(after.forward_passes - before.forward_passes, 2 * passes);
        test_int(after.errors, before.errors);
        ecs_fini(world);
    }
}

/** A lit view that culls by occlusion but has no opaque draws, only a transmissive glass or a
 * blended pane, has no depth prepass either (box_gpu_draw_prepasses), so it draws an early and
 * a late forward pass, plus the pass the glass continues the late one in after its snapshot. */
void Transmission_forward_passes_lit_no_prepass(void) {
    // materials/transmission
    int32_t k;
    for (k = 0; k < 2; k ++) {
        bool transmissive = k == 0;
        ecs_world_t *world = tr_world();
        ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
            .punctual_shadows = true, .cookies = true, .environment = true,
            .aerial_perspective = true, .shader_features.Extended.alpha_cutout = true,
            .shader_features.Extended.blend_modes = true,
            .shader_features.Extended.transmission = true,
            .shader_features.Extended.texture_blend = true,
            .shader_features.Extended.triplanar = true, .shader_features.Extended.sheen = true,
            .shader_features.Extended.clear_coat = true,
            .shader_features.Extended.anisotropy = true,
            .shader_features.Extended.diffuse_transmission = true});
        tr_view(world, tr_camera(world, 0), true, true);
        ecs_entity_t glass = tr_glass(world, 0, -4, 3, 3, 1, 1, 0);
        if (!transmissive) {
            ecs_remove(world, glass, Transmission);
            ecs_set(world, glass, Opacity, {0.5f});
        }

        tr_surface(world, TR_SIZE, TR_SIZE, NULL);

        box_render_gpu_stats_t before = tr_frames(world, 4);
        box_render_gpu_stats_t after = tr_frames(world, 2);
        printf("%s shape: %d forward passes, %d prepasses, %d snapshots in 2 frames\n",
            transmissive ? "transmissive" : "blended",
            after.forward_passes - before.forward_passes, after.prepasses - before.prepasses,
            after.transmission_snapshots - before.transmission_snapshots);
        test_int(after.prepasses - before.prepasses, 0);
        test_int(after.prepass_draws - before.prepass_draws, 0);
        test_int(after.transmission_snapshots - before.transmission_snapshots,
            transmissive ? 2 : 0);
        test_int(after.forward_passes - before.forward_passes, 2 * (transmissive ? 3 : 2));
        test_int(after.errors, 0);
        ecs_fini(world);
    }
}

/** The first view has effects and renders into its HDR image: a white glass over the wall. The
 * second, without effects, renders into the target without clearing it, in a scene of its own
 * with a smaller glass tinted by its albedo, which shows the image of the first view through it.
 *
 * Each samples the snapshot of its own format. The target keeps both, so once they exist no
 * snapshot texture is made again while each view still records one per frame. */
void Transmission_two_formats_one_target(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    ecs_entity_t smaa = ecs_new(world);
    ecs_add(world, smaa, Smaa);
    View value = {.camera = tr_camera(world, 0),
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = smaa };
    ecs_entity_t hdr = ecs_new(world);
    ecs_set_ptr(world, hdr, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    tr_wall(world, 0, -10, 40, 40, 200, 160, 120);
    tr_glass(world, 0, -4, 3, 3, 1, 1, 0);

    uint8_t tint[3] = { 255, 128, 255 };
    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, View, {.camera = tr_camera(world, scene),
        .clear = {.enabled = false}, .order = 1});
    ecs_entity_t tinted = tr_glass(world, -0.8f, -4, 1, 1, 1, 1, 0);
    ecs_add_pair(world, tinted, EcsChildOf, scene);
    ecs_set(world, tinted, Rgb, {tint[0], tint[1], tint[2]});

    tr_surface(world, TR_SIZE, TR_SIZE, "two_formats.png");
    box_render_gpu_stats_t before = tr_frames(world, 4);
    box_render_gpu_stats_t after = tr_frames(world, 3);
    printf("snapshot textures: %d after warm up, %d made in 3 frames, %d snapshots\n",
        before.transmission_textures, after.transmission_textures -
        before.transmission_textures, after.transmission_snapshots -
        before.transmission_snapshots);
    test_int(before.transmission_textures, 2);
    test_int(after.transmission_textures - before.transmission_textures, 0);
    test_int(after.transmission_snapshots - before.transmission_snapshots, 6);
    test_int(after.errors, 0);

    unsigned char *px = tr_load(world, "two_formats.png", TR_SIZE, TR_SIZE);
    const unsigned char *wall = tr_px(px, TR_SIZE, 2, 2);
    test_assert(wall[0] > 150);
    tr_expect(tr_px(px, TR_SIZE, 44, 32), wall[0], wall[1], wall[2], 3);
    tr_expect(tr_px(px, TR_SIZE, 21, 32), tr_srgb(tr_lin(wall[0]) * tr_lin(tint[0])),
        tr_srgb(tr_lin(wall[1]) * tr_lin(tint[1])),
        tr_srgb(tr_lin(wall[2]) * tr_lin(tint[2])), 3);
    stbi_image_free(px);

    before = after;
    after = tr_frames(world, 2);
    test_int(after.transmission_textures - before.transmission_textures, 0);
    test_int(after.transmission_snapshots - before.transmission_snapshots, 4);
    test_int(after.errors, 0);
    ecs_fini(world);
}

/** Frames first_frame_not_dark renders after the one that first draws the glass */
#define TR_FIRST_FRAMES (6)

/** Render one frame into the capture of first_frame_not_dark and get its center pixel.
 *
 * @param world The world
 * @param path Path of the capture, removed once read
 * @param out Receives the center pixel, RGBA
 */
static void tr_first_frame(
    ecs_world_t *world,
    const char *path,
    unsigned char out[4])
{
    int w = 0, h = 0, n = 0;
    tr_frames(world, 1);
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    remove(path);
    test_int(w, TR_SIZE);
    test_int(h, TR_SIZE);
    ecs_os_memcpy(out, tr_px(px, TR_SIZE, TR_SIZE / 2, TR_SIZE / 2), 4);
    test_assert(tr_px(px, TR_SIZE, 2, 2)[1] > 200);
    stbi_image_free(px);
}

/** A rough glass, its green halved by its albedo, over a bright wall in a fresh world.
 *
 * The glass is added once the wall renders, with compile outcomes held as the browser holds
 * them until after the frame, so the frame that first draws it has no downsample shader yet
 * (etc/shaders/transmission.wgsl is only requested once a batch is transmissive).
 *
 * That frame samples the sharp snapshot, not blur mips that were never written, so it is not
 * darker than the frames after the shader arrives. */
void Transmission_first_frame_not_dark(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true, .punctual_shadows = true,
        .cookies = true, .environment = true, .aerial_perspective = true,
        .shader_features.Extended.alpha_cutout = true, .shader_features.Extended.blend_modes = true,
        .shader_features.Extended.transmission = true,
        .shader_features.Extended.texture_blend = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.sheen = true, .shader_features.Extended.clear_coat = true,
        .shader_features.Extended.anisotropy = true,
        .shader_features.Extended.diffuse_transmission = true});
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -10, 40, 40, 230, 230, 230);
    tr_surface(world, TR_SIZE, TR_SIZE, "first_frame.png");
    char *path = flecs_asprintf("%s/first_frame.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    unsigned char first[4], later[4];
    int32_t i, c;
    tr_frames(world, 3);
    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD), true);
    ecs_entity_t glass = tr_glass(world, 0, -4, 3, 3, 1, 1, 0);
    ecs_set(world, glass, Rgb, {255, 128, 255});
    ecs_set(world, glass, PbrMaterial, {.metallic = 0, .roughness = 1});
    box_render_gpu_stats_t before = tr_frames(world, 0);
    tr_first_frame(world, path, first);
    printf("first frame: center %d %d %d\n", first[0], first[1], first[2]);
    test_int(tr_frames(world, 0).transmission_snapshots - before.transmission_snapshots, 1);
    test_int(tr_frames(world, 0).compiles, before.compiles);

    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_AT_ONCE);
    for (i = 0; i < TR_FIRST_FRAMES; i ++) {
        tr_first_frame(world, path, later);
        printf("frame %d after it: center %d %d %d\n", i + 1, later[0], later[1], later[2]);
        test_assert(later[1] + 40 < 230);
        for (c = 0; c < 3; c ++) {
            test_assert((int32_t)first[c] + 3 >= (int32_t)later[c]);
        }
    }

    box_render_gpu_stats_t after = tr_frames(world, 0);
    test_assert(after.compiles > before.compiles);
    test_int(after.errors, 0);
    ecs_os_free(path);
    ecs_fini(world);
}
