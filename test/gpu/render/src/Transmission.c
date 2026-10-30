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
        .lighting = {.enabled = lit},
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
 * sorts before it shows only outside it, where the glass shows the green wall.
 *
 * The glass casts no shadow: the ground around it looks as it does without it, while an opaque
 * box in its place shades it. */
void Transmission_no_depth_write_no_shadow(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -12, 40, 40, 0, 200, 0);

    ecs_entity_t glass = tr_glass(world, 0, -6, 2, 4, 1, 1, 0);
    ecs_set(world, glass, Box, {2, 4, 6});

    ecs_entity_t late = tr_wall(world, 0.8f, -4.5f, 1, 1, 200, 0, 0);
    ecs_set(world, late, Opacity, {0.5f});

    ecs_entity_t early = tr_wall(world, -2.5f, -9.5f, 4, 2, 0, 0, 200);
    ecs_set(world, early, Opacity, {0.5f});
    unsigned char *px = tr_render(world, "order.png", TR_SIZE, TR_SIZE);

    tr_expect(tr_px(px, TR_SIZE, 26, 32), 0, 200, 0, 3);
    tr_expect(tr_px(px, TR_SIZE, 20, 32), 0, 200, 0, 3);

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

/** Through the front glass only the wall shows. Beside it the shape behind shows: the blended
 * quad over the wall, or the glass behind, which tints the green wall by its blue albedo to
 * black. */
void Transmission_blended_not_in_snapshot(void) {
    // materials/transmission
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = tr_world();
        tr_view(world, tr_camera(world, 0), true, false);
        tr_wall(world, 0, -10, 40, 40, 0, 200, 0);
        tr_glass(world, 0, -4, 2, 2, 1, 1, 0);
        if (k == 0) {
            ecs_entity_t quad = tr_wall(world, 0, -7, 8, 8, 200, 0, 0);
            ecs_set(world, quad, Opacity, {0.5f});
        } else {
            ecs_entity_t behind = tr_glass(world, 0, -7, 8, 8, 1, 1, 0);
            ecs_set(world, behind, Rgb, {0, 0, 255});
        }

        unsigned char *px = tr_render(world, k ? "behind_glass.png" : "behind_blend.png",
            TR_SIZE, TR_SIZE);

        tr_expect(tr_px(px, TR_SIZE, 32, 32), 0, 200, 0, 3);

        const unsigned char *beside = tr_px(px, TR_SIZE, 12, 32);
        if (k == 0) {
            tr_expect(beside, tr_srgb(tr_lin(200) * 0.5f), tr_srgb(tr_lin(200) * 0.5f), 0, 3);
        } else {
            tr_expect(beside, 0, 0, 0, 3);
        }

        stbi_image_free(px);
        ecs_fini(world);
    }
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
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -10, 40, 40, 200, 120, 60);
    ecs_entity_t glass = tr_glass(world, 0, -4, 3, 3, 1, 1, 0);

    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
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

/** The first view has effects and renders into its HDR image: a white glass over the wall. The
 * second, without effects, renders into the target without clearing it, in a scene of its own
 * with a smaller glass tinted by its albedo, which shows the image of the first view through it.
 *
 * Each samples the snapshot of its own format. The target keeps both, so once they exist no
 * snapshot texture is made again while each view still records one per frame. */
void Transmission_two_formats_one_target(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    ecs_entity_t gtao = ecs_new(world);
    ecs_add(world, gtao, Gtao);
    View value = {.camera = tr_camera(world, 0),
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = gtao };
    ecs_entity_t hdr = ecs_new(world);
    ecs_set_ptr(world, hdr, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    tr_wall(world, 0, -10, 40, 40, 200, 160, 120);
    tr_glass(world, 0, -4, 3, 3, 1, 1, 0);

    uint8_t tint[3] = { 255, 128, 255 };
    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
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

/** Frames rendered from a fresh world in first_frame_not_dark */
#define TR_FIRST_FRAMES (12)

/** A rough glass tinted green-less by its albedo over a bright wall, rendered from a fresh
 * world. The first frame that shows the glass (the wall drawn, the center darker than it in
 * green) must not be darker than the frames after it: before the downsample shader exists the
 * glass samples the sharp snapshot, not blur mips that were never written. */
void Transmission_first_frame_not_dark(void) {
    // materials/transmission
    ecs_world_t *world = tr_world();
    tr_view(world, tr_camera(world, 0), true, false);
    tr_wall(world, 0, -10, 40, 40, 230, 230, 230);
    ecs_entity_t glass = tr_glass(world, 0, -4, 3, 3, 1, 1, 0);
    ecs_set(world, glass, Rgb, {255, 128, 255});
    ecs_set(world, glass, PbrMaterial, {.metallic = 0, .roughness = 1});
    tr_surface(world, TR_SIZE, TR_SIZE, "first_frame.png");
    char *path = flecs_asprintf("%s/first_frame.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    unsigned char seen[TR_FIRST_FRAMES][4];
    int32_t i, c, count = 0;
    for (i = 0; i < TR_FIRST_FRAMES; i ++) {
        tr_frames(world, 1);
        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        if (!px) {
            continue;
        }

        remove(path);
        test_int(w, TR_SIZE);
        test_int(h, TR_SIZE);
        const unsigned char *center = tr_px(px, TR_SIZE, TR_SIZE / 2, TR_SIZE / 2);
        const unsigned char *corner = tr_px(px, TR_SIZE, 2, 2);
        printf("frame %d: center %d %d %d, corner %d %d %d\n", i, center[0], center[1],
            center[2], corner[0], corner[1], corner[2]);
        if (count || (corner[1] > 150 && center[1] + 40 < corner[1])) {
            ecs_os_memcpy(seen[count], center, 4);
            count ++;
        }

        stbi_image_free(px);
    }

    test_assert(count >= 3);
    for (i = 1; i < count; i ++) {
        for (c = 0; c < 3; c ++) {
            if ((int32_t)seen[0][c] + 3 < (int32_t)seen[i][c]) {
                printf("channel %d: first frame %d, frame %d after it %d\n", c, seen[0][c],
                    i, seen[i][c]);
            }

            test_assert((int32_t)seen[0][c] + 3 >= (int32_t)seen[i][c]);
        }
    }

    test_int(tr_frames(world, 0).errors, 0);
    ecs_os_free(path);
    ecs_fini(world);
}
