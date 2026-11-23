/**
 * @file StarField.c
 * @brief Tests for the star fields a lit view draws on its background: stars where the CPU
 * generator puts them with the energy it gives them, the same image for a seed, opaque geometry
 * hiding them, the rotation of the entity moving them, count 0 drawing nothing and an additive
 * shape in front adding to them; checked on PNG captures.
 *
 * The cases run in a lit view without lights that clears to black, from a camera at the origin
 * looking down -Z with a 60 degree field of view, into a 64 by 64 capture without effects.
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
#define SF_SIZE (64)

/** Pixels from the border inside which a star is checked */
#define SF_MARGIN (3)

/** Create a headless world that renders on the main thread, without ambient light or a sun.
 *
 * @return The world
 */
static ecs_world_t* sf_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.blend_modes = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {0, 0, 0}, .ground = {0, 0, 0},
        .intensity = 0});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {1000, 1000, 1000});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    return world;
}

/** Add a white star field of equal stars a pixel in radius, uniform on the sphere.
 *
 * @param world The world
 * @param count Stars
 * @param seed Seed
 * @param brightness Emission of every star
 * @return The entity
 */
static ecs_entity_t sf_field(
    ecs_world_t *world,
    int32_t count,
    uint32_t seed,
    float brightness)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, StarField, {
        .count = count, .seed = seed, .brightness = brightness, .falloff = 0, .size = 1,
        .temperature_min = 6600, .temperature_max = 6600, .saturation = 0, .band = 0,
        .band_width = 0});

    return e;
}

/** Add a wall of black albedo that shows its gray emission, facing the camera.
 *
 * @param world The world
 * @param x X of its center
 * @param z Z of its center
 * @param size Width and height
 * @param gray Emission, display encoded
 * @return The wall
 */
static ecs_entity_t sf_wall(
    ecs_world_t *world,
    float x,
    float z,
    float size,
    uint8_t gray)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, z});
    ecs_set(world, e, Box, {size, size, 0.2f});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {gray, gray, gray}});

    return e;
}

/** Run frames and read the counters of the GPU backend after them.
 *
 * @param world The world
 * @param count Frames to run
 * @return The counters, all 0 before the first frame with a surface
 */
static box_render_gpu_stats_t sf_frames(
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
static unsigned char* sf_render(
    ecs_world_t *world,
    const char *file)
{
    box_render_gpu_stats_t before = sf_frames(world, 0), after;
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = SF_SIZE, .height = SF_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    after = sf_frames(world, 3);
    test_int(after.errors, before.errors);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, SF_SIZE);
    test_int(h, SF_SIZE);
    ecs_os_free(path);

    return px;
}

/** Convert a display encoded byte to linear light.
 *
 * @param c The byte
 * @return The linear value
 */
static float sf_lin(
    int32_t c)
{
    float v = (float)c / 255.0f;

    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

/** Get the red channel of a pixel of a capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return The red channel
 */
static int32_t sf_red(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return px[(y * SF_SIZE + x) * 4];
}

/** Sum the linear red light of a capture.
 *
 * @param px The pixels
 * @return The sum over every pixel
 */
static float sf_energy(
    const unsigned char *px)
{
    float result = 0;
    int32_t i;
    for (i = 0; i < SF_SIZE * SF_SIZE; i ++) {
        result += sf_lin(px[i * 4]);
    }

    return result;
}

/** Project the direction of a star to the capture of the camera at the origin.
 *
 * @param d The direction
 * @param out Receives the column and row of its center, continuous
 * @return Whether it is in front of the camera
 */
static bool sf_project(
    const float *d,
    float *out)
{
    float k = 1.0f / tanf(30.0f * 3.14159265f / 180.0f);
    bool result = d[2] < -1e-3f;
    if (result) {
        out[0] = (1.0f + d[0] / -d[2] * k) * 0.5f * SF_SIZE;
        out[1] = (1.0f - d[1] / -d[2] * k) * 0.5f * SF_SIZE;
    }

    return result;
}

/** The stars of a field on the capture of the last frame, as the CPU generator places them.
 *
 * @param world The world
 * @param e The entity with the StarField
 * @param r Receives the record of the field
 */
static void sf_record(
    ecs_world_t *world,
    ecs_entity_t e,
    box_render_star_field_t *r)
{
    box_render_star_record(ecs_get(world, e, StarField), ecs_get(world, e, WorldTransform), e,
        r);
}

/** Get the energy of a star, its emission summed over the pixels it covers.
 *
 * @param s The star
 * @return The energy
 */
static float sf_star_energy(
    const box_render_star_t *s)
{
    return s->drawn > 0
        ? s->peak * 3.14159265f * (s->drawn * s->drawn + 1.0f / 12.0f)
        : s->peak;
}

/** Get the least emission of the pixel the center of a star falls in: half the peak of a disc,
 * a quarter of the energy of a point, whose tent puts at least that on the nearest pixel.
 *
 * @param s The star
 * @return The emission
 */
static float sf_star_min(
    const box_render_star_t *s)
{
    return s->drawn > 0 ? 0.5f * s->peak : 0.25f * s->peak;
}

/** Count the stars of a field whose center is inside the capture by a margin, check that the
 * pixel of each is lit and add up the energy of the stars that reach into the capture.
 *
 * @param r The record of the field
 * @param px The capture
 * @param lit Whether the pixels of the stars must be lit, else left alone
 * @param inner Receives the energy of the stars inside by the margin
 * @param edge Receives the energy of the stars that reach into the capture from near its border
 * @return Stars inside by the margin
 */
static int32_t sf_check_stars(
    const box_render_star_field_t *r,
    const unsigned char *px,
    bool lit,
    float *inner,
    float *edge)
{
    int32_t i, result = 0;
    *inner = 0;
    *edge = 0;
    for (i = 0; i < r->count; i ++) {
        box_render_star_t s;
        float p[2];
        box_render_star_sample(r, (uint32_t)i, &s);
        if (!sf_project(s.direction, p)) {
            continue;
        }

        float energy = sf_star_energy(&s);
        if (p[0] < SF_MARGIN || p[1] < SF_MARGIN || p[0] > SF_SIZE - SF_MARGIN ||
            p[1] > SF_SIZE - SF_MARGIN)
        {
            if (p[0] > -2 && p[1] > -2 && p[0] < SF_SIZE + 2 && p[1] < SF_SIZE + 2) {
                *edge += energy;
            }

            continue;
        }

        *inner += energy;
        if (lit) {
            int32_t red = sf_red(px, (int32_t)p[0], (int32_t)p[1]);
            if (sf_lin(red) < sf_star_min(&s)) {
                printf("star %d at %.1f %.1f: red %d\n", i, p[0], p[1], red);
            }

            test_assert(sf_lin(red) >= sf_star_min(&s));
        }

        result ++;
    }

    return result;
}

/** A field of equal white stars shows each where the CPU generator puts it, with the energy it
 * gives it, the same image again for the seed and another one for another seed; each frame
 * draws the field once. */
void StarField_points_deterministic(void) {
    const char *files[3] = { "stars_seed_a.png", "stars_seed_b.png", "stars_seed_c.png" };
    const uint32_t seeds[3] = { 11, 11, 12 };
    unsigned char *px[3];
    int32_t k;
    for (k = 0; k < 3; k ++) {
        ecs_world_t *world = sf_world();
        ecs_entity_t e = sf_field(world, 600, seeds[k], 0.5f);
        px[k] = sf_render(world, files[k]);
        box_render_star_field_t r;
        float inner, edge;
        sf_record(world, e, &r);
        int32_t stars = sf_check_stars(&r, px[k], true, &inner, &edge);
        test_assert(stars > 10);
        float energy = sf_energy(px[k]);
        if (energy < inner * 0.85f || energy > (inner + edge) * 1.15f) {
            printf("energy %f, stars inside %f, near the border %f\n", energy, inner, edge);
        }

        test_assert(energy >= inner * 0.85f);
        test_assert(energy <= (inner + edge) * 1.15f);

        box_render_gpu_stats_t before = sf_frames(world, 0);
        box_render_gpu_stats_t after = sf_frames(world, 2);
        test_int(after.star_fields - before.star_fields, 2);
        test_int(after.errors, before.errors);
        ecs_fini(world);
    }

    test_assert(!memcmp(px[0], px[1], SF_SIZE * SF_SIZE * 4));
    test_assert(memcmp(px[0], px[2], SF_SIZE * SF_SIZE * 4) != 0);
    for (k = 0; k < 3; k ++) {
        stbi_image_free(px[k]);
    }
}

/** Stars below half a pixel in radius are points: each lights at most the two by two pixels
 * nearest its center, and their energy is the one of their size, so smaller stars are dimmer,
 * not smaller. */
void StarField_subpixel_keeps_energy(void) {
    const float sizes[2] = { 0.45f, 0.25f };
    float energies[2];
    int32_t k, i;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = sf_world();
        ecs_entity_t e = sf_field(world, 300, 3, 1.0f);
        StarField *f = ecs_get_mut(world, e, StarField);
        f->size = sizes[k];
        ecs_modified(world, e, StarField);
        unsigned char *px = sf_render(world, k ? "stars_subpixel_b.png" : "stars_subpixel_a.png");
        box_render_star_field_t r;
        float inner, edge;
        sf_record(world, e, &r);
        int32_t stars = sf_check_stars(&r, px, true, &inner, &edge);
        test_assert(stars > 10);
        energies[k] = sf_energy(px);
        if (energies[k] < inner * 0.85f || energies[k] > (inner + edge) * 1.15f) {
            printf("energy %f, stars inside %f, near the border %f\n", energies[k], inner,
                edge);
        }

        test_assert(energies[k] >= inner * 0.85f);
        test_assert(energies[k] <= (inner + edge) * 1.15f);

        int32_t lit = 0;
        for (i = 0; i < SF_SIZE * SF_SIZE; i ++) {
            lit += px[i * 4] > 0;
        }

        test_assert(lit <= 4 * (stars + 20));
        stbi_image_free(px);
        ecs_fini(world);
    }

    float expect = (0.25f * 0.25f) / (0.45f * 0.45f);
    test_assert(fabsf(energies[1] / energies[0] - expect) < 0.15f * expect);
}

/** A black wall in front of the left half of the view hides the stars there, those on the right
 * show; a wall over the whole view hides them all. */
void StarField_hidden_by_opaque(void) {
    ecs_world_t *world = sf_world();
    ecs_entity_t e = sf_field(world, 600, 5, 0.5f);
    ecs_entity_t wall = sf_wall(world, -10, -10, 20, 0);
    ecs_set(world, wall, Box, {20, 40, 0.2f});
    unsigned char *px = sf_render(world, "stars_half_hidden.png");
    box_render_star_field_t r;
    int32_t i, left = 0, right = 0, x, y;
    sf_record(world, e, &r);
    for (i = 0; i < r.count; i ++) {
        box_render_star_t s;
        float p[2];
        box_render_star_sample(&r, (uint32_t)i, &s);
        if (!sf_project(s.direction, p) || p[1] < SF_MARGIN || p[1] > SF_SIZE - SF_MARGIN) {
            continue;
        }

        if (p[0] > SF_MARGIN && p[0] < SF_SIZE / 2 - SF_MARGIN) {
            test_int(sf_red(px, (int32_t)p[0], (int32_t)p[1]), 0);
            left ++;
        } else if (p[0] > SF_SIZE / 2 + SF_MARGIN && p[0] < SF_SIZE - SF_MARGIN) {
            test_assert(sf_lin(sf_red(px, (int32_t)p[0], (int32_t)p[1])) >= sf_star_min(&s));
            right ++;
        }
    }

    test_assert(left > 3);
    test_assert(right > 3);
    stbi_image_free(px);
    ecs_fini(world);

    world = sf_world();
    sf_field(world, 600, 5, 0.5f);
    sf_wall(world, 0, -10, 40, 0);
    px = sf_render(world, "stars_hidden.png");
    for (y = 0; y < SF_SIZE; y ++) {
        for (x = 0; x < SF_SIZE; x ++) {
            test_int(sf_red(px, x, y), 0);
        }
    }

    stbi_image_free(px);
    ecs_fini(world);
}

/** Turning the entity of the field turns its stars: they show where the CPU generator puts them
 * with the rotation, and the image differs from the one without it. */
void StarField_rotation_moves(void) {
    const char *files[2] = { "stars_unrotated.png", "stars_rotated.png" };
    unsigned char *px[2];
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = sf_world();
        ecs_entity_t e = sf_field(world, 600, 21, 0.5f);
        if (k) {
            ecs_set(world, e, Rotation, {0.2f, 0.5f, 0});
        }

        px[k] = sf_render(world, files[k]);
        box_render_star_field_t r;
        float inner, edge;
        sf_record(world, e, &r);
        if (k) {
            test_assert(fabsf(r.rotation[0][0] - 1) > 0.05f);
        }

        test_assert(sf_check_stars(&r, px[k], true, &inner, &edge) > 10);
        ecs_fini(world);
    }

    test_assert(memcmp(px[0], px[1], SF_SIZE * SF_SIZE * 4) != 0);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** A field of count 0 draws nothing: the image is the one of a world without a field, black,
 * and the backend counts no star field. */
void StarField_count_zero_draws_nothing(void) {
    const char *files[2] = { "stars_zero.png", "stars_none.png" };
    unsigned char *px[2];
    int32_t k, i;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = sf_world();
        if (!k) {
            sf_field(world, 0, 5, 0.5f);
        }

        px[k] = sf_render(world, files[k]);
        box_render_gpu_stats_t stats = sf_frames(world, 1);
        test_int(stats.star_fields, 0);
        ecs_fini(world);
    }

    test_assert(!memcmp(px[0], px[1], SF_SIZE * SF_SIZE * 4));
    for (i = 0; i < SF_SIZE * SF_SIZE; i ++) {
        test_int(px[0][i * 4], 0);
    }

    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

/** An additive wall in front of the stars adds its emission to them: a star shows its light plus
 * the wall, the pixels between the stars the wall alone. */
void StarField_under_additive(void) {
    ecs_world_t *world = sf_world();
    ecs_entity_t e = sf_field(world, 600, 5, 0.3f);
    ecs_entity_t veil = sf_wall(world, 0, -10, 40, 60);
    ecs_set(world, veil, BlendMode, {Additive});
    unsigned char *px = sf_render(world, "stars_additive.png");
    box_render_star_field_t r;
    int32_t i, checked = 0;
    sf_record(world, e, &r);
    for (i = 0; i < r.count; i ++) {
        box_render_star_t s;
        float p[2];
        box_render_star_sample(&r, (uint32_t)i, &s);
        if (!sf_project(s.direction, p) || p[0] < SF_MARGIN || p[1] < SF_MARGIN ||
            p[0] > SF_SIZE - SF_MARGIN || p[1] > SF_SIZE - SF_MARGIN)
        {
            continue;
        }

        float lin = sf_lin(sf_red(px, (int32_t)p[0], (int32_t)p[1]));
        test_assert(lin >= sf_lin(60) + sf_star_min(&s));
        checked ++;
    }

    test_assert(checked > 3);
    int32_t darkest = 255;
    for (i = 0; i < SF_SIZE * SF_SIZE; i ++) {
        darkest = px[i * 4] < darkest ? px[i * 4] : darkest;
    }

    test_assert(abs(darkest - 60) <= 3);
    stbi_image_free(px);
    ecs_fini(world);
}
