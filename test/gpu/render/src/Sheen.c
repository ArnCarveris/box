/**
 * @file Sheen.c
 * @brief Tests for how a Sheen shades and updates, checked on PNG captures.
 *
 * Most cases look at a dark rough sphere of radius 1, 4 units in front of the camera, lit by a
 * directional light along the view; its silhouette is a circle of about 45 pixels around the
 * center of the image, where the sheen shows as a bright rim.
 *
 * The center keeps 1 - 0.157 of its base, plus in indirect light the small bias the albedo fit
 * has above a roughness of 0.25. Under a coat at normal incidence the sheen loses about 4%.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "draw.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Width and height of the captures of the sheen cases */
#define SHEEN_W (128)

/** Inner radius in pixels of the ring of grazing pixels, inside the silhouette */
#define SHEEN_RIM_IN (37.0f)

/** Outer radius in pixels of the ring of grazing pixels */
#define SHEEN_RIM_OUT (42.0f)

/** Radius in pixels of the disc at the center of the sphere */
#define SHEEN_CENTER (6.0f)

/** Linear channel values of a ring of pixels */
typedef struct sheen_ring_t {
    float c[3];     /**< Mean linear red, green and blue */
    float sum;      /**< Mean of their sum */
} sheen_ring_t;

/** Create the world of the sheen cases: a camera 4 units in front of the origin looking down -z
 * into a 128x128 surface.
 *
 * It has no ambient light and a directional light along -z of the given intensity, so there is
 * no default sun.
 *
 * @param lit Whether the view is lit
 * @param intensity Intensity of the directional light, 0 for none
 * @return The world
 */
static ecs_world_t* sheen_world(
    bool lit,
    float intensity)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 4});
    ecs_set(world, camera, Camera, {.fov = 40, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = lit});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0, GLM_PIf, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = intensity});

    return world;
}

/** Light the world of a sheen case by a hemisphere ambient, bright from above.
 *
 * @param world The world
 */
static void sheen_ambient(
    ecs_world_t *world)
{
    ecs_singleton_set(world, AmbientLight, {.sky = {220, 220, 230}, .ground = {120, 110, 100},
        .intensity = 1});
}

/** Render two frames of a sheen case into a 128x128 PNG and load it.
 *
 * @param world The world
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* sheen_render(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = SHEEN_W, .height = SHEEN_W,
        .write_to_file = ECS_CONST_CAST(char*, name), .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_delete(world, e);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, SHEEN_W);
    test_int(h, SHEEN_W);

    return px;
}

/** Add a dark gray rough dielectric sphere of radius 1.
 *
 * @param world The world
 * @param x Position of the center along x
 * @param sheen The Sheen of the sphere, NULL for none
 * @return The sphere
 */
static ecs_entity_t sheen_sphere(
    ecs_world_t *world,
    float x,
    const Sheen *sheen)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Sphere, {.segments = 64, .smooth = true, .radius = 1});
    ecs_set(world, e, Rgb, {60, 60, 60});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});
    if (sheen) {
        ecs_set_ptr(world, e, Sheen, sheen);
    }

    return e;
}

/** Render the sphere of a sheen case under the directional light along the view.
 *
 * @param sheen The Sheen of the sphere, NULL for none
 * @param intensity Intensity of the light
 * @param lit Whether the view is lit
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* sheen_lit(
    const Sheen *sheen,
    float intensity,
    bool lit,
    const char *name)
{
    ecs_world_t *world = sheen_world(lit, intensity);
    sheen_sphere(world, 0, sheen);
    unsigned char *px = sheen_render(world, name);
    ecs_fini(world);

    return px;
}

/** Render the sphere of a sheen case lit only by a hemisphere ambient.
 *
 * @param sheen The Sheen of the sphere, NULL for none
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* sheen_indirect(
    const Sheen *sheen,
    const char *name)
{
    ecs_world_t *world = sheen_world(true, 0);
    sheen_ambient(world);
    sheen_sphere(world, 0, sheen);
    unsigned char *px = sheen_render(world, name);
    ecs_fini(world);

    return px;
}

/** Convert a display encoded byte to linear light.
 *
 * @param v The byte
 * @return The linear value
 */
static float sheen_linear(
    unsigned char v)
{
    float c = (float)v / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Mean linear color of the pixels of a capture between two distances from its center.
 *
 * @param px The pixels
 * @param r0 Smallest distance in pixels
 * @param r1 Distance in pixels the pixels are below
 * @return The mean
 */
static sheen_ring_t sheen_ring(
    const unsigned char *px,
    float r0,
    float r1)
{
    sheen_ring_t result = {{0, 0, 0}, 0};
    int count = 0, x, y, i;
    float half = (float)SHEEN_W * 0.5f;
    for (y = 0; y < SHEEN_W; y ++) {
        for (x = 0; x < SHEEN_W; x ++) {
            float dx = (float)x + 0.5f - half, dy = (float)y + 0.5f - half;
            float d = sqrtf(dx * dx + dy * dy);
            if (d >= r0 && d < r1) {
                const unsigned char *p = &px[(y * SHEEN_W + x) * 4];
                for (i = 0; i < 3; i ++) {
                    result.c[i] += sheen_linear(p[i]);
                }

                count ++;
            }
        }
    }

    test_assert(count > 0);
    for (i = 0; i < 3; i ++) {
        result.c[i] /= (float)count;
    }

    result.sum = result.c[0] + result.c[1] + result.c[2];

    return result;
}

/** Mean linear color of the grazing ring of the sphere of a capture.
 *
 * @param px The pixels
 * @return The mean
 */
static sheen_ring_t sheen_rim(
    const unsigned char *px)
{
    return sheen_ring(px, SHEEN_RIM_IN, SHEEN_RIM_OUT);
}

/** Mean linear color of the center of the sphere of a capture.
 *
 * @param px The pixels
 * @return The mean
 */
static sheen_ring_t sheen_center(
    const unsigned char *px)
{
    return sheen_ring(px, 0, SHEEN_CENTER);
}

/** Sum of the linear channels of a half of a capture.
 *
 * @param px The pixels
 * @param right Whether the right half, else the left one
 * @return The sum
 */
static float sheen_half(
    const unsigned char *px,
    bool right)
{
    float sum = 0;
    int x, y, i;
    for (y = 0; y < SHEEN_W; y ++) {
        for (x = right ? SHEEN_W / 2 : 0; x < (right ? SHEEN_W : SHEEN_W / 2); x ++) {
            for (i = 0; i < 3; i ++) {
                sum += sheen_linear(px[(y * SHEEN_W + x) * 4 + i]);
            }
        }
    }

    return sum;
}

/** Check that two sheen captures are identical and free them.
 *
 * @param a The first capture
 * @param b The second capture
 */
static void sheen_expect_same(
    unsigned char *a,
    unsigned char *b)
{
    test_int(memcmp(a, b, SHEEN_W * SHEEN_W * 4), 0);
    stbi_image_free(a);
    stbi_image_free(b);
}

void Sheen_black_unchanged(void) {
    // materials/pbr
    Sheen black = {.color = {0, 0, 0}, .roughness = 0.5f};
    sheen_expect_same(sheen_lit(NULL, 3, true, "sheen_black_plain.png"),
        sheen_lit(&black, 3, true, "sheen_black.png"));

    sheen_expect_same(sheen_indirect(NULL, "sheen_black_indirect_plain.png"),
        sheen_indirect(&black, "sheen_black_indirect.png"));
}

void Sheen_brightens_grazing(void) {
    // materials/pbr
    Sheen white = {.color = {255, 255, 255}, .roughness = 0.5f};
    unsigned char *plain = sheen_lit(NULL, 3, true, "sheen_grazing_plain.png");
    unsigned char *sheen = sheen_lit(&white, 3, true, "sheen_grazing.png");
    sheen_ring_t plain_rim = sheen_rim(plain), sheen_rim_ = sheen_rim(sheen);
    sheen_ring_t plain_center = sheen_center(plain), sheen_center_ = sheen_center(sheen);

    test_assert(plain_center.sum > 0.05f);
    test_assert(sheen_rim_.sum > plain_rim.sum * 2.0f + 0.1f);

    test_assert(sheen_center_.sum <= plain_center.sum * 1.01f + 0.002f);
    test_assert(sheen_center_.sum >= plain_center.sum * 0.78f);

    stbi_image_free(plain);
    stbi_image_free(sheen);
}

void Sheen_color_tints(void) {
    // materials/pbr
    Sheen red = {.color = {255, 0, 0}, .roughness = 0.5f};
    unsigned char *plain = sheen_lit(NULL, 3, true, "sheen_tint_plain.png");
    unsigned char *sheen = sheen_lit(&red, 3, true, "sheen_tint.png");
    sheen_ring_t plain_rim = sheen_rim(plain), red_rim = sheen_rim(sheen);
    sheen_ring_t plain_center = sheen_center(plain), red_center = sheen_center(sheen);

    test_assert(red_rim.c[0] > plain_rim.c[0] * 2.0f + 0.05f);
    test_assert(red_rim.c[0] > red_rim.c[1] * 2.0f);
    int i;
    for (i = 1; i < 3; i ++) {
        test_assert(red_rim.c[i] <= plain_rim.c[i] + 0.001f);
        test_assert(red_rim.c[i] >= plain_rim.c[i] * 0.75f);
        test_assert(red_center.c[i] < plain_center.c[i]);
        test_assert(red_center.c[i] >= plain_center.c[i] * 0.80f);
    }

    stbi_image_free(plain);
    stbi_image_free(sheen);
}

void Sheen_roughness_broadens(void) {
    // materials/pbr
    Sheen tight = {.color = {255, 255, 255}, .roughness = 0.2f};
    Sheen broad = {.color = {255, 255, 255}, .roughness = 1.0f};
    unsigned char *plain = sheen_lit(NULL, 3, true, "sheen_broad_plain.png");
    unsigned char *a = sheen_lit(&tight, 3, true, "sheen_tight.png");
    unsigned char *b = sheen_lit(&broad, 3, true, "sheen_broad.png");

    sheen_ring_t plain_mid = sheen_ring(plain, 20, 30);
    sheen_ring_t tight_mid = sheen_ring(a, 20, 30);
    sheen_ring_t broad_mid = sheen_ring(b, 20, 30);
    test_assert(broad_mid.sum > plain_mid.sum + 0.05f);
    test_assert(broad_mid.sum > tight_mid.sum + 0.05f);
    test_assert(tight_mid.sum < plain_mid.sum + 0.02f);

    test_assert(sheen_rim(a).sum > sheen_rim(plain).sum + 0.05f);

    stbi_image_free(plain);
    stbi_image_free(a);
    stbi_image_free(b);
}

void Sheen_indirect_only(void) {
    // materials/pbr
    Sheen white = {.color = {255, 255, 255}, .roughness = 0.5f};
    unsigned char *plain = sheen_indirect(NULL, "sheen_indirect_plain.png");
    unsigned char *sheen = sheen_indirect(&white, "sheen_indirect.png");
    sheen_ring_t plain_rim = sheen_rim(plain), sheen_rim_ = sheen_rim(sheen);
    sheen_ring_t plain_center = sheen_center(plain), sheen_center_ = sheen_center(sheen);

    test_assert(plain_center.sum > 0.02f);
    test_assert(sheen_rim_.sum > plain_rim.sum * 1.5f + 0.03f);
    test_assert(sheen_rim_.sum > sheen_center_.sum);

    test_assert(sheen_center_.sum >= plain_center.sum * 0.8f);
    test_assert(sheen_rim_.sum - plain_rim.sum > (sheen_center_.sum - plain_center.sum) * 2.0f);

    stbi_image_free(plain);
    stbi_image_free(sheen);
}

/** Write a 4x4 ORM texture of an occlusion of 0, a roughness of 1 and a metallic of 0 and load it.
 *
 * @param world The world
 * @param path Receives the path of the PNG, freed by the caller after the world
 * @return The texture entity
 */
static ecs_entity_t sheen_orm(
    ecs_world_t *world,
    char **path)
{
    uint8_t px[4 * 4 * 4];
    int32_t i;
    *path = flecs_asprintf("%s/sheen_orm.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 0;
        px[i * 4 + 1] = 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(*path, 4, 4, 4, px, 4 * 4) != 0);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = *path});

    return tex;
}

/** Render the sphere of a sheen case lit only by a hemisphere ambient, with an ORM texture of
 * an occlusion of 0.
 *
 * @param sheen The Sheen of the sphere, NULL for none
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* sheen_occluded(
    const Sheen *sheen,
    const char *name)
{
    char *path = NULL;
    ecs_world_t *world = sheen_world(true, 0);
    sheen_ambient(world);
    ecs_entity_t e = sheen_sphere(world, 0, sheen);
    ecs_entity_t tex = sheen_orm(world, &path);
    ecs_set(world, e, PbrTextures, {.orm = tex});
    int32_t frames = 0;
    while (frames < 100 && ecs_get(world, tex, TextureStatus)->state == TextureLoading) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    test_assert(ecs_get(world, tex, TextureStatus)->state == TextureReady);
    unsigned char *px = sheen_render(world, name);
    ecs_fini(world);
    ecs_os_free(path);

    return px;
}

void Sheen_occluded_by_orm(void) {
    // materials/pbr
    Sheen white = {.color = {255, 255, 255}, .roughness = 0.5f};
    unsigned char *open = sheen_indirect(&white, "sheen_open.png");
    unsigned char *open_plain = sheen_indirect(NULL, "sheen_open_plain.png");
    test_assert(sheen_rim(open).sum > sheen_rim(open_plain).sum + 0.03f);
    stbi_image_free(open);
    stbi_image_free(open_plain);

    sheen_expect_same(sheen_occluded(NULL, "sheen_occluded_plain.png"),
        sheen_occluded(&white, "sheen_occluded.png"));
}

/** Render an emissive black quad with a sheen in a lit view without any light.
 *
 * @param sheen The Sheen of the quad, NULL for none
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* sheen_emissive(
    const Sheen *sheen,
    const char *name)
{
    ecs_world_t *world = sheen_world(true, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Quad, {2, 2});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {200, 100, 50}});
    if (sheen) {
        ecs_set_ptr(world, e, Sheen, sheen);
    }

    unsigned char *px = sheen_render(world, name);
    ecs_fini(world);

    return px;
}

void Sheen_emission_not_scaled(void) {
    // materials/pbr
    Sheen white = {.color = {255, 255, 255}, .roughness = 0.5f};
    unsigned char *plain = sheen_emissive(NULL, "sheen_emissive_plain.png");
    unsigned char *sheen = sheen_emissive(&white, "sheen_emissive.png");
    const unsigned char *p = &sheen[(64 * SHEEN_W + 64) * 4];
    test_assert(abs((int)p[0] - 200) <= 2);
    test_assert(abs((int)p[1] - 100) <= 2);
    test_assert(abs((int)p[2] - 50) <= 2);
    sheen_expect_same(plain, sheen);
}

/** Render the sphere of a sheen case under the light along the view with a smooth full coat,
 * whose Fresnel is that of normal incidence everywhere, as the half vector is the view.
 *
 * @param sheen The Sheen of the sphere, NULL for none
 * @param coat Whether the sphere has a coat
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* sheen_coated(
    const Sheen *sheen,
    bool coat,
    const char *name)
{
    ecs_world_t *world = sheen_world(true, 3);
    ecs_entity_t e = sheen_sphere(world, 0, sheen);
    if (coat) {
        ecs_set(world, e, ClearCoat, {.strength = 1, .roughness = 1});
    }

    unsigned char *px = sheen_render(world, name);
    ecs_fini(world);

    return px;
}

void Sheen_under_clear_coat(void) {
    // materials/pbr
    Sheen white = {.color = {255, 255, 255}, .roughness = 0.5f};
    unsigned char *plain = sheen_coated(NULL, false, "sheen_uncoated_plain.png");
    unsigned char *sheen = sheen_coated(&white, false, "sheen_uncoated.png");
    unsigned char *coated_plain = sheen_coated(NULL, true, "sheen_coated_plain.png");
    unsigned char *coated = sheen_coated(&white, true, "sheen_coated.png");

    float bare = sheen_rim(sheen).sum - sheen_rim(plain).sum;
    float under = sheen_rim(coated).sum - sheen_rim(coated_plain).sum;
    test_assert(bare > 0.1f);
    test_assert(under < bare);
    test_assert(under > bare * 0.92f);
    test_assert(under < bare * 0.99f);

    stbi_image_free(plain);
    stbi_image_free(sheen);
    stbi_image_free(coated_plain);
    stbi_image_free(coated);
}

void Sheen_ignored_by_unlit(void) {
    // materials/pbr
    Sheen white = {.color = {255, 255, 255}, .roughness = 0.5f};
    sheen_expect_same(sheen_lit(NULL, 3, false, "sheen_unlit_plain.png"),
        sheen_lit(&white, 3, false, "sheen_unlit.png"));
}

/** Render a 2D rectangle in a lit view under the light along the view and an ambient.
 *
 * @param sheen The Sheen of the rectangle, NULL for none
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* sheen_rect(
    const Sheen *sheen,
    const char *name)
{
    ecs_world_t *world = sheen_world(true, 3);
    sheen_ambient(world);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rect, {2, 1});
    ecs_set(world, e, Rgb, {200, 120, 40});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.5f});
    if (sheen) {
        ecs_set_ptr(world, e, Sheen, sheen);
    }

    unsigned char *px = sheen_render(world, name);
    ecs_fini(world);

    return px;
}

void Sheen_ignored_by_2d(void) {
    // materials/pbr
    Sheen white = {.color = {255, 255, 255}, .roughness = 0.5f};
    unsigned char *plain = sheen_rect(NULL, "sheen_rect_plain.png");
    const unsigned char *p = &plain[(64 * SHEEN_W + 64) * 4];
    test_assert(p[0] + p[1] + p[2] > 30);
    sheen_expect_same(plain, sheen_rect(&white, "sheen_rect.png"));
}

void Sheen_prefab(void) {
    // materials/pbr
    ecs_world_t *world = sheen_world(true, 3);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Sphere, {.segments = 64, .smooth = true, .radius = 0.6f});
    ecs_set(world, prefab, Rgb, {60, 60, 60});
    ecs_set(world, prefab, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, prefab, Sheen, {.color = {255, 255, 255}, .roughness = 0.5f});
    ecs_entity_t shared = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, shared, Position, {-0.7f, 0, 0});
    ecs_entity_t own = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, own, Position, {0.7f, 0, 0});
    ecs_set(world, own, Sheen, {.color = {255, 255, 255}, .roughness = 0.5f});
    test_assert(ecs_owns(world, own, Sheen));
    test_assert(!ecs_owns(world, shared, Sheen));

    unsigned char *px = sheen_render(world, "sheen_prefab.png");
    float left = sheen_half(px, false), right = sheen_half(px, true);
    test_assert(right > 1.0f);
    test_assert(fabsf(left - right) < right * 0.02f);
    stbi_image_free(px);

    ecs_set(world, prefab, Sheen, {.color = {0, 0, 0}, .roughness = 0.5f});
    px = sheen_render(world, "sheen_prefab_off.png");
    float left_off = sheen_half(px, false), right_off = sheen_half(px, true);
    test_assert(left_off < left * 0.8f);
    test_assert(fabsf(right_off - right) < right * 0.02f);
    stbi_image_free(px);

    ecs_fini(world);
}

void Sheen_change_and_remove(void) {
    // materials/pbr
    unsigned char *before = sheen_lit(NULL, 3, true, "sheen_before.png");
    sheen_ring_t base = sheen_rim(before);

    ecs_world_t *world = sheen_world(true, 3);
    ecs_entity_t ball = sheen_sphere(world, 0,
        &(Sheen){.color = {255, 255, 255}, .roughness = 0.5f});
    unsigned char *px = sheen_render(world, "sheen_change.png");
    sheen_ring_t white = sheen_rim(px);
    test_assert(white.sum > base.sum * 2.0f + 0.1f);
    stbi_image_free(px);

    ecs_set(world, ball, Sheen, {.color = {255, 0, 0}, .roughness = 0.5f});
    px = sheen_render(world, "sheen_change_red.png");
    sheen_ring_t red = sheen_rim(px);
    test_assert(red.c[0] > base.c[0] * 2.0f + 0.05f);
    test_assert(red.c[1] < white.c[1] * 0.5f);
    stbi_image_free(px);

    ecs_remove(world, ball, Sheen);
    px = sheen_render(world, "sheen_change_removed.png");
    test_int(memcmp(px, before, SHEEN_W * SHEEN_W * 4), 0);
    stbi_image_free(px);
    ecs_fini(world);

    world = sheen_world(true, 3);
    ball = sheen_sphere(world, 0, &(Sheen){.color = {0, 0, 0}, .roughness = 0.5f});
    ecs_add(world, ball, DynamicMaterial);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = SHEEN_W, .height = SHEEN_W,
        .write_to_file = "sheen_dynamic.png", .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/sheen_dynamic.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(sheen_rim(px).sum < base.sum * 1.05f + 0.005f);
    stbi_image_free(px);

    Sheen *sheen = ecs_get_mut(world, ball, Sheen);
    sheen->color = (Rgb){255, 255, 255};
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(sheen_rim(px).sum > base.sum * 2.0f + 0.1f);
    stbi_image_free(px);

    ecs_os_free(path);
    ecs_fini(world);
    stbi_image_free(before);
}

/** Seconds every frame of the transition case advances the world: its transition takes a
 * second, so the frames after a change render at progress 0, 1/4, 1/2, 3/4 and 1 */
#define SHEEN_DELTA (0.25f)

/** Render a frame of the transition case, that advances the world by SHEEN_DELTA, into a PNG and
 * load it. The surface persists between the frames, so only the file name changes.
 *
 * @param world The world
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* sheen_step(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = SHEEN_W, .height = SHEEN_W,
        .write_to_file = ECS_CONST_CAST(char*, name), .resolution_scale = 1});
    ecs_progress(world, SHEEN_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, SHEEN_W);
    test_int(h, SHEEN_W);

    return px;
}

/** Render frames of the transition case and keep the last one.
 *
 * @param world The world
 * @param prefix Prefix of the file names, followed by the index of the frame
 * @param first Index of the first frame
 * @param count Frames to render, at least 1
 * @return The pixels of the last frame
 */
static unsigned char* sheen_steps(
    ecs_world_t *world,
    const char *prefix,
    int first,
    int count)
{
    unsigned char *px = NULL;
    int i;
    for (i = 0; i < count; i ++) {
        char *name = flecs_asprintf("%s_%d.png", prefix, first + i);
        if (px) {
            stbi_image_free(px);
        }

        px = sheen_step(world, name);
        ecs_os_free(name);
    }

    return px;
}

/** Check that two captures of the transition case differ by at most a tolerance per channel,
 * and free them.
 *
 * @param a The first capture
 * @param b The second capture
 * @param tolerance Largest channel difference
 */
static void sheen_expect_close(
    unsigned char *a,
    unsigned char *b,
    int tolerance)
{
    int i, worst = 0;
    for (i = 0; i < SHEEN_W * SHEEN_W * 4; i ++) {
        int d = abs((int)a[i] - (int)b[i]);
        worst = d > worst ? d : worst;
    }

    test_assert(worst <= tolerance);
    stbi_image_free(a);
    stbi_image_free(b);
}

/** Check that the shape of the transition case kept its batch and slot.
 *
 * @param world The world
 * @param e The shape
 * @param batch The batch it had before the change
 * @param slot The slot it had before the change
 */
static void sheen_same_batch(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t batch,
    int32_t slot)
{
    const DrawSlot *ds = ecs_get(world, e, DrawSlot);
    test_assert(ds != NULL);
    test_int(ds->batch, batch);
    test_int(ds->slot, slot);
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
}

/** The sheen of the dark sphere goes from black (none) to white over a second: at half the
 * duration its rim is brighter than at the start and darker than at the end.
 *
 * At the end the pixels are those of a sphere that has the white sheen from the start. Its batch
 * and slot do not change.
 */
void Sheen_transition_mid(void) {
    // materials/pbr
    Sheen black = {.color = {0, 0, 0}, .roughness = 0.5f};
    Sheen white = {.color = {255, 255, 255}, .roughness = 0.5f};

    ecs_world_t *still = sheen_world(true, 3);
    ecs_singleton_set(still, RenderSettings, {.in_flight = 2, .main_thread = true});
    sheen_sphere(still, 0, &white);
    unsigned char *target = sheen_steps(still, "sheen_transition_still", 0, 7);
    sheen_ring_t end = sheen_rim(target);
    ecs_fini(still);

    ecs_world_t *world = sheen_world(true, 3);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t e = sheen_sphere(world, 0, &black);
    box_transition_set(world, e, ecs_id(Sheen), &(TransitionSpec){ .duration = 1.0f });
    unsigned char *px = sheen_steps(world, "sheen_transition", 0, 2);
    sheen_ring_t start = sheen_rim(px);
    stbi_image_free(px);
    test_assert(end.sum > start.sum * 2.0f + 0.1f);
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    int32_t slot = ecs_get(world, e, DrawSlot)->slot;

    ecs_set_ptr(world, e, Sheen, &white);
    px = sheen_steps(world, "sheen_transition", 2, 3);
    test_int(ecs_get(world, e, Sheen)->color.r, 255);
    sheen_same_batch(world, e, batch, slot);
    sheen_ring_t mid = sheen_rim(px);
    float span = end.sum - start.sum;
    test_assert(mid.sum > start.sum + span * 0.05f);
    test_assert(mid.sum < end.sum - span * 0.25f);
    stbi_image_free(px);

    px = sheen_steps(world, "sheen_transition", 5, 2);
    sheen_same_batch(world, e, batch, slot);
    sheen_expect_close(px, target, 1);
    ecs_fini(world);
}
