/**
 * @file DiffuseTransmission.c
 * @brief Tests for how a DiffuseTransmission shades and updates, checked on PNG captures.
 *
 * Most cases look at a rough 2x2 quad on the origin facing a camera 4 units in front of it, of a
 * 128x128 capture where a unit at the origin is about 44 pixels and the origin is pixel 64, 64.
 * A light behind the quad reaches only its back lobe: without the component the quad is black.
 *
 * The back lobe of a light from direction l with a radiance is albedo * color * factor / pi *
 * radiance * dot(-n, l), in linear light, and the view writes the display encoding of it.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width and height of the captures of the diffuse transmission cases */
#define DT_W (128)

/** Pixels per world unit at the origin: 64 over 4 * tan(20 degrees) */
#define DT_SCALE (43.96f)

/** Display encoded gray of the quads, about 0.5 in linear light */
#define DT_GRAY (188)

/** Mean linear color of a disc of pixels */
typedef struct dt_mean_t {
    float c[3];     /**< Mean linear red, green and blue */
    float sum;      /**< Sum of the three */
} dt_mean_t;

/** Create the world of the diffuse transmission cases: a camera 4 units in front of the origin
 * looking down -z into a 128x128 surface, without ambient light.
 *
 * @param lit Whether the view is lit
 * @param shadows Whether the view renders shadows, with the default settings
 * @return The world
 */
static ecs_world_t* dt_world(
    bool lit,
    bool shadows)
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
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->lighting.enabled = lit;
    v->shadows.enabled = shadows;
    ecs_modified(world, view, View);
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});

    return world;
}

/** Add a directional light, behind the quads (travelling along +z, toward the camera) or in
 * front of them (along -z).
 *
 * @param world The world
 * @param behind Whether the light comes from behind the quads
 * @param intensity Intensity of the light, 0 for one that lights nothing
 * @return The light
 */
static ecs_entity_t dt_sun(
    ecs_world_t *world,
    bool behind,
    float intensity)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Rotation, {0, behind ? 0 : GLM_PIf, 0});
    ecs_set(world, e, DirectionalLight, {.intensity = intensity});

    return e;
}

/** Add a rough dielectric quad facing the camera.
 *
 * @param world The world
 * @param x Position of the center along x
 * @param size Width and height
 * @param gray Display encoded gray of the quad
 * @param dt The DiffuseTransmission of the quad, NULL for none
 * @return The quad
 */
static ecs_entity_t dt_quad(
    ecs_world_t *world,
    float x,
    float size,
    uint8_t gray,
    const DiffuseTransmission *dt)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Quad, {size, size});
    ecs_set(world, e, Rgb, {gray, gray, gray});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});
    if (dt) {
        ecs_set_ptr(world, e, DiffuseTransmission, dt);
    }

    return e;
}

/** Render two frames of a diffuse transmission case into a 128x128 PNG and load it.
 *
 * @param world The world
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* dt_render(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = DT_W, .height = DT_W,
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
    test_int(w, DT_W);
    test_int(h, DT_W);

    return px;
}

/** Convert a display encoded byte to linear light.
 *
 * @param v The byte
 * @return The linear value
 */
static float dt_linear(
    unsigned char v)
{
    float c = (float)v / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Display encode a linear value to a byte.
 *
 * @param v The linear value
 * @return The byte
 */
static int dt_encode(
    float v)
{
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    float c = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;

    return (int)(c * 255.0f + 0.5f);
}

/** Mean linear color of a disc of pixels of a capture.
 *
 * @param px The pixels
 * @param cx Column of the center
 * @param cy Row of the center
 * @param r Radius in pixels
 * @return The mean
 */
static dt_mean_t dt_mean(
    const unsigned char *px,
    int cx,
    int cy,
    float r)
{
    dt_mean_t result = {{0, 0, 0}, 0};
    int count = 0, x, y, i;
    for (y = 0; y < DT_W; y ++) {
        for (x = 0; x < DT_W; x ++) {
            float dx = (float)(x - cx), dy = (float)(y - cy);
            if (dx * dx + dy * dy <= r * r) {
                const unsigned char *p = &px[(y * DT_W + x) * 4];
                for (i = 0; i < 3; i ++) {
                    result.c[i] += dt_linear(p[i]);
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

/** Mean linear color of the center of a capture.
 *
 * @param px The pixels
 * @return The mean
 */
static dt_mean_t dt_center(
    const unsigned char *px)
{
    return dt_mean(px, 64, 64, 3);
}

/** Sum of the linear channels of a half of a capture.
 *
 * @param px The pixels
 * @param right Whether the right half, else the left one
 * @return The sum
 */
static float dt_half(
    const unsigned char *px,
    bool right)
{
    float sum = 0;
    int x, y, i;
    for (y = 0; y < DT_W; y ++) {
        for (x = right ? DT_W / 2 : 0; x < (right ? DT_W : DT_W / 2); x ++) {
            for (i = 0; i < 3; i ++) {
                sum += dt_linear(px[(y * DT_W + x) * 4 + i]);
            }
        }
    }

    return sum;
}

/** Check that two diffuse transmission captures are identical and free them.
 *
 * @param a The first capture
 * @param b The second capture
 */
static void dt_expect_same(
    unsigned char *a,
    unsigned char *b)
{
    test_int(memcmp(a, b, DT_W * DT_W * 4), 0);
    stbi_image_free(a);
    stbi_image_free(b);
}

/** Check that the center pixel of a capture has a gray within a tolerance.
 *
 * @param px The pixels
 * @param expect Display encoded value of every channel
 * @param tolerance Largest channel difference
 */
static void dt_expect_center(
    const unsigned char *px,
    int expect,
    int tolerance)
{
    const unsigned char *p = &px[(64 * DT_W + 64) * 4];
    int i;
    for (i = 0; i < 3; i ++) {
        test_assert(abs((int)p[i] - expect) <= tolerance);
    }
}

/** Render the gray quad lit from behind by a directional light of intensity pi.
 *
 * @param dt The DiffuseTransmission of the quad, NULL for none
 * @param lit Whether the view is lit
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* dt_backlit(
    const DiffuseTransmission *dt,
    bool lit,
    const char *name)
{
    ecs_world_t *world = dt_world(lit, false);
    dt_sun(world, true, GLM_PIf);
    dt_quad(world, 0, 2, DT_GRAY, dt);
    unsigned char *px = dt_render(world, name);
    ecs_fini(world);

    return px;
}

/** Render a quad lit from the front by a point light 3 units in front of it, where it has a
 * highlight at the center.
 *
 * @param dt The DiffuseTransmission of the quad, NULL for none
 * @param gray Display encoded gray of the quad
 * @param roughness Roughness of the quad
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* dt_front(
    const DiffuseTransmission *dt,
    uint8_t gray,
    float roughness,
    const char *name)
{
    ecs_world_t *world = dt_world(true, false);
    dt_sun(world, false, 0);
    ecs_entity_t q = dt_quad(world, 0, 2, gray, dt);
    ecs_set(world, q, PbrMaterial, {.metallic = 0, .roughness = roughness});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, 3});
    ecs_set(world, light, PointLight, {.intensity = 9 * GLM_PIf, .range = 20});
    unsigned char *px = dt_render(world, name);
    ecs_fini(world);

    return px;
}

void DiffuseTransmission_factor_zero_unchanged(void) {
    // materials/pbr
    DiffuseTransmission zero = {.factor = 0, .color = {255, 255, 255}, .thickness = 0.5f};
    dt_expect_same(dt_backlit(NULL, true, "dt_zero_back_plain.png"),
        dt_backlit(&zero, true, "dt_zero_back.png"));
    dt_expect_same(dt_front(NULL, DT_GRAY, 0.4f, "dt_zero_front_plain.png"),
        dt_front(&zero, DT_GRAY, 0.4f, "dt_zero_front.png"));

    ecs_world_t *world = dt_world(true, false);
    ecs_singleton_set(world, AmbientLight, {.sky = {80, 120, 255}, .ground = {60, 50, 40},
        .intensity = 1});
    dt_sun(world, true, GLM_PIf);
    dt_quad(world, 0, 2, DT_GRAY, NULL);
    unsigned char *plain = dt_render(world, "dt_zero_ambient_plain.png");
    ecs_fini(world);

    world = dt_world(true, false);
    ecs_singleton_set(world, AmbientLight, {.sky = {80, 120, 255}, .ground = {60, 50, 40},
        .intensity = 1});
    dt_sun(world, true, GLM_PIf);
    dt_quad(world, 0, 2, DT_GRAY, &zero);
    unsigned char *with = dt_render(world, "dt_zero_ambient.png");
    ecs_fini(world);
    dt_expect_same(plain, with);
}

void DiffuseTransmission_backlit_by_directional(void) {
    // materials/pbr
    DiffuseTransmission half = {.factor = 0.5f, .color = {255, 255, 255}};
    unsigned char *plain = dt_backlit(NULL, true, "dt_directional_plain.png");
    unsigned char *lit = dt_backlit(&half, true, "dt_directional.png");

    dt_expect_center(plain, 0, 1);
    float expect = dt_linear(DT_GRAY) * 0.5f / GLM_PIf * GLM_PIf;
    dt_expect_center(lit, dt_encode(expect), 3);

    const unsigned char *p = &lit[((64 + 30) * DT_W + 64 - 30) * 4];
    test_assert(abs((int)p[0] - dt_encode(expect)) <= 3);

    stbi_image_free(plain);
    stbi_image_free(lit);
}

/** Render the gray quad lit from behind by a point or spot light 2 units behind its center.
 *
 * @param dt The DiffuseTransmission of the quad, NULL for none
 * @param spot Whether the light is a spot light along +z, else a point light
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* dt_punctual(
    const DiffuseTransmission *dt,
    bool spot,
    const char *name)
{
    ecs_world_t *world = dt_world(true, false);
    dt_sun(world, true, 0);
    dt_quad(world, 0, 2, DT_GRAY, dt);
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, -2});
    if (spot) {
        ecs_set(world, light, SpotLight, {.intensity = 4 * GLM_PIf, .range = 20,
            .inner_angle = 20, .outer_angle = 30});
    } else {
        ecs_set(world, light, PointLight, {.intensity = 4 * GLM_PIf, .range = 20});
    }

    unsigned char *px = dt_render(world, name);
    ecs_fini(world);

    return px;
}

/** Linear radiance of a point or spot light of intensity 4 pi and range 20 at 2 units, on its
 * axis, as attenuation() of mesh.wgsl. */
static float dt_punctual_radiance(void) {
    float ratio = 2.0f / 20.0f;
    float window = 1.0f - ratio * ratio * ratio * ratio;

    return 4 * GLM_PIf * window * window / 4.0f;
}

void DiffuseTransmission_backlit_by_point(void) {
    // materials/pbr
    DiffuseTransmission full = {.factor = 1, .color = {255, 255, 255}};
    unsigned char *plain = dt_punctual(NULL, false, "dt_point_plain.png");
    unsigned char *lit = dt_punctual(&full, false, "dt_point.png");

    dt_expect_center(plain, 0, 1);
    float expect = dt_linear(DT_GRAY) / GLM_PIf * dt_punctual_radiance();
    dt_expect_center(lit, dt_encode(expect), 3);

    float edge = dt_mean(lit, 64 + 40, 64, 1).sum;
    test_assert(edge < dt_center(lit).sum * 0.85f);
    test_assert(edge > 0.01f);

    stbi_image_free(plain);
    stbi_image_free(lit);
}

void DiffuseTransmission_backlit_by_spot(void) {
    // materials/pbr
    DiffuseTransmission full = {.factor = 1, .color = {255, 255, 255}};
    unsigned char *plain = dt_punctual(NULL, true, "dt_spot_plain.png");
    unsigned char *lit = dt_punctual(&full, true, "dt_spot.png");

    dt_expect_center(plain, 0, 1);
    float expect = dt_linear(DT_GRAY) / GLM_PIf * dt_punctual_radiance();
    dt_expect_center(lit, dt_encode(expect), 3);

    test_assert(dt_mean(lit, 64 + 40, 64 - 40, 1).sum < 0.01f);

    stbi_image_free(plain);
    stbi_image_free(lit);
}

void DiffuseTransmission_factor_splits_energy(void) {
    // materials/pbr
    DiffuseTransmission full = {.factor = 1, .color = {255, 255, 255}};
    DiffuseTransmission half = {.factor = 0.5f, .color = {255, 255, 255}};
    unsigned char *specular = dt_front(NULL, 0, 0.4f, "dt_split_specular.png");
    unsigned char *plain = dt_front(NULL, DT_GRAY, 0.4f, "dt_split_plain.png");
    unsigned char *a = dt_front(&full, DT_GRAY, 0.4f, "dt_split_full.png");
    unsigned char *b = dt_front(&half, DT_GRAY, 0.4f, "dt_split_half.png");

    int points[3][2] = {{64, 64}, {64 + 25, 64}, {64 - 20, 64 + 20}};
    int i, j;
    for (i = 0; i < 3; i ++) {
        int x = points[i][0], y = points[i][1];
        dt_mean_t s = dt_mean(specular, x, y, 1);
        dt_mean_t p = dt_mean(plain, x, y, 1);
        dt_mean_t f = dt_mean(a, x, y, 1);
        dt_mean_t h = dt_mean(b, x, y, 1);
        test_assert(p.sum > s.sum + 0.1f);
        for (j = 0; j < 3; j ++) {
            float diffuse = p.c[j] - s.c[j];
            test_assert(fabsf(f.c[j] - s.c[j]) < 0.01f + s.c[j] * 0.02f);
            test_assert(fabsf((h.c[j] - s.c[j]) - diffuse * 0.5f) < 0.01f + diffuse * 0.05f);
        }
    }

    test_assert(dt_center(specular).sum > dt_mean(specular, 64 + 25, 64, 1).sum + 0.05f);

    stbi_image_free(specular);
    stbi_image_free(plain);
    stbi_image_free(a);
    stbi_image_free(b);
}

void DiffuseTransmission_color_tints(void) {
    // materials/pbr
    DiffuseTransmission green = {.factor = 0.5f, .color = {0, 255, 0}};
    unsigned char *back = dt_backlit(&green, true, "dt_tint_back.png");
    dt_mean_t glow = dt_center(back);
    float expect = dt_linear(DT_GRAY) * 0.5f;
    test_assert(fabsf(glow.c[1] - expect) < expect * 0.05f + 0.005f);
    test_assert(glow.c[0] < 0.002f);
    test_assert(glow.c[2] < 0.002f);

    ecs_world_t *world = dt_world(true, false);
    dt_sun(world, false, GLM_PIf);
    dt_quad(world, 0, 2, DT_GRAY, NULL);
    unsigned char *plain = dt_render(world, "dt_tint_front_plain.png");
    ecs_fini(world);

    world = dt_world(true, false);
    dt_sun(world, false, GLM_PIf);
    dt_quad(world, 0, 2, DT_GRAY, &green);
    unsigned char *front = dt_render(world, "dt_tint_front.png");
    ecs_fini(world);

    dt_mean_t p = dt_center(plain), f = dt_center(front);
    int i;
    test_assert(p.c[0] > 0.2f);
    for (i = 0; i < 3; i ++) {
        test_assert(fabsf(f.c[i] - f.c[0]) < 0.003f);
        test_assert(f.c[i] < p.c[i] * 0.6f);
        test_assert(f.c[i] > p.c[i] * 0.4f);
    }

    stbi_image_free(back);
    stbi_image_free(plain);
    stbi_image_free(front);
}

void DiffuseTransmission_metal_has_none(void) {
    // materials/pbr
    DiffuseTransmission full = {.factor = 1, .color = {255, 255, 255}};
    unsigned char *a[2];
    int i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = dt_world(true, false);
        dt_sun(world, true, GLM_PIf);
        ecs_entity_t q = dt_quad(world, 0, 2, DT_GRAY, i ? &full : NULL);
        ecs_set(world, q, PbrMaterial, {.metallic = 1, .roughness = 0.5f});
        a[i] = dt_render(world, i ? "dt_metal.png" : "dt_metal_plain.png");
        ecs_fini(world);
    }

    dt_expect_center(a[1], 0, 1);
    dt_expect_same(a[0], a[1]);
}

/** Render the gray quad with a full diffuse transmission lit from behind by a shadow casting
 * directional light, with a 0.6 unit wide box between the light and its center, or without it.
 *
 * @param caster Whether the box is there
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* dt_caster(
    bool caster,
    const char *name)
{
    ecs_world_t *world = dt_world(true, true);
    dt_sun(world, true, GLM_PIf);
    dt_quad(world, 0, 2, DT_GRAY, &(DiffuseTransmission){
        .factor = 1, .color = {255, 255, 255}});
    if (caster) {
        ecs_entity_t box = ecs_new(world);
        ecs_set(world, box, Position, {0, 0, -1.5f});
        ecs_set(world, box, Box, {0.6f, 0.6f, 0.2f});
        ecs_set(world, box, Rgb, {255, 0, 0});
    }

    unsigned char *px = dt_render(world, name);
    ecs_fini(world);

    return px;
}

void DiffuseTransmission_shadow_on_back(void) {
    // materials/pbr
    unsigned char *open = dt_caster(false, "dt_shadow_open.png");
    unsigned char *shadowed = dt_caster(true, "dt_shadow.png");
    float glow = dt_center(open).sum;
    test_assert(glow > 0.5f);
    test_assert(dt_center(shadowed).sum < glow * 0.1f);

    float outside = dt_mean(shadowed, 64 + 30, 64 - 30, 2).sum;
    test_assert(fabsf(outside - dt_mean(open, 64 + 30, 64 - 30, 2).sum) < glow * 0.05f);
    test_assert(outside > glow * 0.9f);

    stbi_image_free(open);
    stbi_image_free(shadowed);
}

/** Render the gray quad with a full thin diffuse transmission lit from behind by a directional
 * light turned by an angle around y, with or without the shadows of the view.
 *
 * @param angle Angle of the light from the axis of the quad in radians
 * @param shadows Whether the view renders shadows
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* dt_sheet(
    float angle,
    bool shadows,
    const char *name)
{
    ecs_world_t *world = dt_world(true, shadows);
    ecs_entity_t sun = dt_sun(world, true, GLM_PIf);
    ecs_set(world, sun, Rotation, {0, angle, 0});
    dt_quad(world, 0, 2, DT_GRAY, &(DiffuseTransmission){
        .factor = 1, .color = {255, 255, 255}, .thickness = 0});
    unsigned char *px = dt_render(world, name);
    ecs_fini(world);

    return px;
}

void DiffuseTransmission_sheet_no_self_shadow(void) {
    // materials/pbr
    float angles[2] = {0, 1.1f};
    const char *names[2][2] = {
        {"dt_sheet.png", "dt_sheet_unshadowed.png"},
        {"dt_sheet_oblique.png", "dt_sheet_oblique_unshadowed.png"}};
    int points[5][2] = {{64, 64}, {64 + 30, 64}, {64 - 30, 64}, {64, 64 + 30}, {64, 64 - 30}};
    int a, i;
    for (a = 0; a < 2; a ++) {
        float expect = dt_linear(DT_GRAY) * cosf(angles[a]);
        unsigned char *px = dt_sheet(angles[a], true, names[a][0]);
        unsigned char *unshadowed = dt_sheet(angles[a], false, names[a][1]);
        for (i = 0; i < 5; i ++) {
            dt_mean_t m = dt_mean(px, points[i][0], points[i][1], 2);
            test_assert(m.c[0] > expect * 0.9f);
            test_assert(fabsf(m.sum - dt_mean(unshadowed, points[i][0], points[i][1], 2).sum) <
                m.sum * 0.05f);
        }

        stbi_image_free(px);
        stbi_image_free(unshadowed);
    }
}

/** Render a shadow casting sphere of radius 1 with a full diffuse transmission lit from behind by
 * a directional light.
 *
 * @param thickness Thickness of the diffuse transmission
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* dt_sphere(
    float thickness,
    const char *name)
{
    ecs_world_t *world = dt_world(true, true);
    dt_sun(world, true, GLM_PIf);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, {.segments = 64, .smooth = true, .radius = 1});
    ecs_set(world, e, Rgb, {DT_GRAY, DT_GRAY, DT_GRAY});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, e, DiffuseTransmission, {
        .factor = 1, .color = {255, 255, 255}, .thickness = thickness});
    unsigned char *px = dt_render(world, name);
    ecs_fini(world);

    return px;
}

void DiffuseTransmission_thickness_closed_shape(void) {
    // materials/pbr
    unsigned char *thick = dt_sphere(2.2f, "dt_sphere_thick.png");
    unsigned char *thin = dt_sphere(0, "dt_sphere_thin.png");
    float expect = dt_linear(DT_GRAY);
    dt_mean_t a = dt_center(thick), b = dt_center(thin);
    test_assert(a.c[0] > expect * 0.8f);
    test_assert(b.sum < a.sum * 0.25f);

    dt_mean_t ring_thick = dt_mean(thick, 64 + 20, 64, 2);
    dt_mean_t ring_thin = dt_mean(thin, 64 + 20, 64, 2);
    test_assert(ring_thick.sum > 0.3f);
    test_assert(ring_thin.sum < ring_thick.sum * 0.25f);

    stbi_image_free(thick);
    stbi_image_free(thin);
}

/** Render a white quad facing down at the origin, seen from a camera 4 units below it, lit only
 * by a hemisphere ambient of a blue sky over a gray ground.
 *
 * @param dt The DiffuseTransmission of the quad, NULL for none
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* dt_sky(
    const DiffuseTransmission *dt,
    const char *name)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, -4, 0});
    ecs_set(world, camera, Rotation, {GLM_PIf / 2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 40, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {60, 120, 255}, .ground = {100, 100, 100},
        .intensity = 1});
    dt_sun(world, true, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Rotation, {GLM_PIf / 2, 0, 0});
    ecs_set(world, e, Quad, {2, 2});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});
    if (dt) {
        ecs_set_ptr(world, e, DiffuseTransmission, dt);
    }

    unsigned char *px = dt_render(world, name);
    ecs_fini(world);

    return px;
}

void DiffuseTransmission_indirect_from_behind(void) {
    // materials/pbr
    DiffuseTransmission half = {.factor = 0.5f, .color = {255, 255, 255}};
    unsigned char *plain = dt_sky(NULL, "dt_sky_plain.png");
    unsigned char *lit = dt_sky(&half, "dt_sky.png");
    dt_mean_t p = dt_center(plain), l = dt_center(lit);

    test_assert(p.c[0] > 0.02f);
    test_assert(fabsf(p.c[2] - p.c[0]) < p.c[0] * 0.2f + 0.005f);
    test_assert(l.c[2] > p.c[2] * 1.5f);
    test_assert(l.c[2] - l.c[0] > (p.c[2] - p.c[0]) + 0.05f);
    test_assert(l.c[0] < p.c[0] * 1.2f);

    stbi_image_free(plain);
    stbi_image_free(lit);
}

void DiffuseTransmission_ignored_by_unlit(void) {
    // materials/pbr
    DiffuseTransmission full = {.factor = 1, .color = {0, 255, 0}};
    unsigned char *plain = dt_backlit(NULL, false, "dt_unlit_plain.png");
    dt_expect_center(plain, DT_GRAY, 1);
    dt_expect_same(plain, dt_backlit(&full, false, "dt_unlit.png"));
}

/** Render a 2D rectangle in a lit view under a light from behind and from the front and an
 * ambient.
 *
 * @param dt The DiffuseTransmission of the rectangle, NULL for none
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* dt_rect(
    const DiffuseTransmission *dt,
    const char *name)
{
    ecs_world_t *world = dt_world(true, false);
    ecs_singleton_set(world, AmbientLight, {.sky = {220, 220, 230}, .ground = {120, 110, 100},
        .intensity = 1});
    dt_sun(world, false, 3);
    dt_sun(world, true, 3);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rect, {2, 1});
    ecs_set(world, e, Rgb, {200, 120, 40});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.5f});
    if (dt) {
        ecs_set_ptr(world, e, DiffuseTransmission, dt);
    }

    unsigned char *px = dt_render(world, name);
    ecs_fini(world);

    return px;
}

void DiffuseTransmission_ignored_by_2d(void) {
    // materials/pbr
    DiffuseTransmission full = {.factor = 1, .color = {0, 255, 0}};
    unsigned char *plain = dt_rect(NULL, "dt_rect_plain.png");
    const unsigned char *p = &plain[(64 * DT_W + 64) * 4];
    test_assert(p[0] + p[1] + p[2] > 30);
    dt_expect_same(plain, dt_rect(&full, "dt_rect.png"));
}

void DiffuseTransmission_prefab(void) {
    // materials/pbr
    ecs_world_t *world = dt_world(true, false);
    dt_sun(world, true, GLM_PIf);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Quad, {1, 1});
    ecs_set(world, prefab, Rgb, {DT_GRAY, DT_GRAY, DT_GRAY});
    ecs_set(world, prefab, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, prefab, DiffuseTransmission, {.factor = 1, .color = {255, 255, 255}});
    ecs_entity_t shared = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, shared, Position, {-0.7f, 0, 0});
    ecs_entity_t own = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, own, Position, {0.7f, 0, 0});
    ecs_set(world, own, DiffuseTransmission, {.factor = 1, .color = {255, 255, 255}});
    test_assert(ecs_owns(world, own, DiffuseTransmission));
    test_assert(!ecs_owns(world, shared, DiffuseTransmission));

    unsigned char *px = dt_render(world, "dt_prefab.png");
    float left = dt_half(px, false), right = dt_half(px, true);
    test_assert(right > 1.0f);
    test_assert(fabsf(left - right) < right * 0.02f);
    stbi_image_free(px);

    ecs_set(world, prefab, DiffuseTransmission, {.factor = 0, .color = {255, 255, 255}});
    px = dt_render(world, "dt_prefab_off.png");
    float left_off = dt_half(px, false), right_off = dt_half(px, true);
    test_assert(left_off < left * 0.05f);
    test_assert(fabsf(right_off - right) < right * 0.02f);
    stbi_image_free(px);

    ecs_fini(world);
}

void DiffuseTransmission_change_and_remove(void) {
    // materials/pbr
    unsigned char *before = dt_backlit(NULL, true, "dt_before.png");

    ecs_world_t *world = dt_world(true, false);
    dt_sun(world, true, GLM_PIf);
    ecs_entity_t quad = dt_quad(world, 0, 2, DT_GRAY,
        &(DiffuseTransmission){.factor = 1, .color = {255, 255, 255}});
    unsigned char *px = dt_render(world, "dt_change.png");
    dt_mean_t white = dt_center(px);
    test_assert(white.sum > 1.0f);
    stbi_image_free(px);

    ecs_set(world, quad, DiffuseTransmission, {.factor = 1, .color = {255, 0, 0}});
    px = dt_render(world, "dt_change_red.png");
    dt_mean_t red = dt_center(px);
    test_assert(fabsf(red.c[0] - white.c[0]) < white.c[0] * 0.05f);
    test_assert(red.c[1] < 0.002f);
    stbi_image_free(px);

    ecs_remove(world, quad, DiffuseTransmission);
    px = dt_render(world, "dt_change_removed.png");
    test_int(memcmp(px, before, DT_W * DT_W * 4), 0);
    stbi_image_free(px);
    ecs_fini(world);

    world = dt_world(true, false);
    dt_sun(world, true, GLM_PIf);
    quad = dt_quad(world, 0, 2, DT_GRAY,
        &(DiffuseTransmission){.factor = 0, .color = {255, 255, 255}});
    ecs_add(world, quad, DynamicMaterial);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = DT_W, .height = DT_W,
        .write_to_file = "dt_dynamic.png", .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/dt_dynamic.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(dt_center(px).sum < 0.01f);
    stbi_image_free(px);

    DiffuseTransmission *dt = ecs_get_mut(world, quad, DiffuseTransmission);
    dt->factor = 1;
    ecs_modified(world, quad, DiffuseTransmission);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(dt_center(px).sum > white.sum * 0.9f);
    stbi_image_free(px);

    ecs_os_free(path);
    ecs_fini(world);
    stbi_image_free(before);
}
