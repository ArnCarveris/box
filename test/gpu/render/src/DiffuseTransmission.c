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

/* DiffuseTransmission.blur: what a Transmission pane shows of the opaque snapshot blurs by the
 * distance behind it. These cases run in a lit view without lights and with a black ambient,
 * from a camera at the origin looking down -Z (fov 60) into a 256x128 capture, as the
 * Transmission suite does: emissive walls of black albedo behind a smooth white pane of
 * transmission factor 1, ior 1 and thickness 0 whose front face is at z -2.99, so a pixel
 * through it is the snapshot at that pixel, sampled at the mip of its blur. */

/** Width of the captures of the blur cases */
#define DB_W (256)

/** Height of the captures of the blur cases */
#define DB_H (128)

/** Tangent of half the vertical field of view of the blur cases, 60 degrees */
#define DB_TAN (0.57735f)

/** Create a headless world that renders on the main thread, with no light but a point light of
 * intensity 0 out of reach (which replaces the default sun) and a black ambient.
 *
 * @return The world
 */
static ecs_world_t* db_world(void)
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

/** Add a camera looking down -Z and a lit view of it that clears to a color.
 *
 * @param world The world
 * @param parent Parent of the camera, 0 for none
 * @param z Z of the camera
 * @param ortho Whether the camera is orthographic, 4 units high
 * @param clear Display encoded clear color, the sky of the view
 * @return The view
 */
static ecs_entity_t db_view(
    ecs_world_t *world,
    ecs_entity_t parent,
    float z,
    bool ortho,
    const uint8_t clear[3])
{
    ecs_entity_t camera = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, z});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000,
        .ortho = ortho, .ortho_height = 4});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {clear[0], clear[1], clear[2], 255}},
        .lighting = {.enabled = true}});

    return view;
}

/** Add an emissive box of black albedo.
 *
 * @param world The world
 * @param parent Parent of the box, 0 for none
 * @param x X of its center
 * @param z Z of its center
 * @param w Width
 * @param h Height
 * @param d Depth
 * @param red Whether it emits red, else blue
 * @return The box
 */
static ecs_entity_t db_wall(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x,
    float z,
    float w,
    float h,
    float d,
    bool red)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {x, 0, z});
    ecs_set(world, e, Box, {w, h, d});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1,
        .color = {red ? 255 : 0, 0, red ? 0 : 255}});

    return e;
}

/** Add a red wall left of x and a blue one right of it, their front faces a distance behind
 * the pane.
 *
 * @param world The world
 * @param parent Parent of the walls, 0 for none
 * @param x X of the edge between them
 * @param behind Distance of their front faces behind the front face of the pane
 * @param left Width of the red wall
 * @param right Width of the blue wall
 */
static void db_edge_walls(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x,
    float behind,
    float left,
    float right)
{
    float z = -2.99f - behind - 0.05f;
    db_wall(world, parent, x - left * 0.5f, z, left, 40, 0.1f, true);
    db_wall(world, parent, x + right * 0.5f, z, right, 40, 0.1f, false);
}

/** Add a smooth white pane of transmission factor 1, ior 1 and thickness 0, 0.02 thick with
 * its front face at z -2.99.
 *
 * @param world The world
 * @param parent Parent of the pane, 0 for none
 * @param x X of its center
 * @param w Width
 * @param roughness Roughness of the pane
 * @param dt The DiffuseTransmission of the pane, NULL for none
 * @return The pane
 */
static ecs_entity_t db_pane(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x,
    float w,
    float roughness,
    const DiffuseTransmission *dt)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {x, 0, -3});
    ecs_set(world, e, Box, {w, 4, 0.02f});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = roughness});
    ecs_set(world, e, Transmission, {.transmission_factor = 1, .ior = 1,
        .thickness_factor = 0, .attenuation_distance = 1e10f,
        .attenuation_color = {255, 255, 255}});
    if (dt) {
        ecs_set_ptr(world, e, DiffuseTransmission, dt);
    }

    return e;
}

/** A DiffuseTransmission that adds no lobe, with a blur.
 *
 * @param blur The blur
 * @return The component
 */
static DiffuseTransmission db_blur(
    float blur)
{
    return (DiffuseTransmission){ .factor = 0, .color = {255, 255, 255}, .blur = blur };
}

/** Run frames and read the counters of the GPU backend after them.
 *
 * @param world The world
 * @param count Frames to run
 * @return The counters
 */
static box_render_gpu_stats_t db_frames(
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
static unsigned char* db_load(
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

/** Add a surface, render three frames into a capture without validation errors and load it.
 *
 * @param world The world
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* db_render(
    ecs_world_t *world,
    const char *file)
{
    box_render_gpu_stats_t before = db_frames(world, 0), after;
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = DB_W, .height = DB_H,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    after = db_frames(world, 3);
    test_int(after.errors, before.errors);

    return db_load(world, file, DB_W, DB_H);
}

/** Blue share of a pixel, blue over red plus blue in linear light, -1 for a dark pixel.
 *
 * @param px The pixels
 * @param width Width of the capture
 * @param x Column
 * @param y Row
 * @return The share
 */
static float db_share(
    const unsigned char *px,
    int32_t width,
    int32_t x,
    int32_t y)
{
    const unsigned char *p = &px[(y * width + x) * 4];
    float r = dt_linear(p[0]), b = dt_linear(p[2]);

    return r + b > 0.01f ? b / (r + b) : -1.0f;
}

/** Width in pixels of the red to blue edge of a row: the columns of a range whose blue share
 * lies between 0.1 and 0.9.
 *
 * @param px The pixels
 * @param width Width of the capture
 * @param y Row
 * @param x0 First column of the range
 * @param x1 Column past the range
 * @return The width
 */
static int32_t db_edge(
    const unsigned char *px,
    int32_t width,
    int32_t y,
    int32_t x0,
    int32_t x1)
{
    int32_t x, result = 0;
    for (x = x0; x < x1; x ++) {
        float s = db_share(px, width, x, y);
        if (s > 0.1f && s < 0.9f) {
            result ++;
        }
    }

    return result;
}

/** Largest step of the blue share between two neighbor columns of a range of a row, skipping
 * dark pixels.
 *
 * @param px The pixels
 * @param width Width of the capture
 * @param y Row
 * @param x0 First column of the range
 * @param x1 Column past the range
 * @return The step
 */
static float db_sharpness(
    const unsigned char *px,
    int32_t width,
    int32_t y,
    int32_t x0,
    int32_t x1)
{
    float result = 0, last = -1.0f;
    int32_t x;
    for (x = x0; x < x1; x ++) {
        float s = db_share(px, width, x, y);
        if (s >= 0.0f && last >= 0.0f && fabsf(s - last) > result) {
            result = fabsf(s - last);
        }

        last = s;
    }

    return result;
}

/** Render a red to blue edge at the center of the capture a distance behind a pane.
 *
 * @param file Name of the PNG
 * @param behind Distance of the walls behind the pane
 * @param ortho Whether the camera is orthographic
 * @param camera_z Z of the camera
 * @param dt The DiffuseTransmission of the pane, NULL for none
 * @return The width of the edge in pixels
 */
static int32_t db_edge_behind(
    const char *file,
    float behind,
    bool ortho,
    float camera_z,
    const DiffuseTransmission *dt)
{
    const uint8_t black[3] = { 0, 0, 0 };
    ecs_world_t *world = db_world();
    db_view(world, 0, camera_z, ortho, black);
    db_edge_walls(world, 0, 0, behind, 40, 40);
    db_pane(world, 0, 0, 6, 0, dt);
    unsigned char *px = db_render(world, file);
    int32_t result = db_edge(px, DB_W, DB_H / 2, DB_W / 4, DB_W * 3 / 4);
    stbi_image_free(px);
    ecs_fini(world);

    return result;
}

void DiffuseTransmission_blur_zero_unchanged(void) {
    // materials/transmission
    const uint8_t black[3] = { 0, 0, 0 };
    DiffuseTransmission none = db_blur(0);
    unsigned char *images[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = db_world();
        db_view(world, 0, 0, false, black);
        db_edge_walls(world, 0, 0, 2, 40, 40);
        db_pane(world, 0, 0, 6, 0.3f, i ? &none : NULL);
        images[i] = db_render(world, i ? "db_zero_blur.png" : "db_zero_plain.png");
        ecs_fini(world);
    }

    test_int(memcmp(images[0], images[1], DB_W * DB_H * 4), 0);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

/** A quad with only DiffuseTransmission draws in the opaque pass, which has no snapshot: its
 * blur changes nothing. */
void DiffuseTransmission_blur_ignored_without_transmission(void) {
    // materials/transmission
    DiffuseTransmission sharp = {.factor = 1, .color = {255, 255, 255}, .blur = 0};
    DiffuseTransmission blurred = {.factor = 1, .color = {255, 255, 255}, .blur = 2};
    unsigned char *a = dt_backlit(&sharp, true, "db_ignored_0.png");
    unsigned char *b = dt_backlit(&blurred, true, "db_ignored_2.png");
    test_assert(dt_center(a).sum > 0.1f);
    dt_expect_same(a, b);
}

/** A blur of 0.25 at 4 units behind the pane is about 16 pixels of mip 0, at 0.1 behind under
 * one: the far edge is wide, the near one stays as sharp as without the blur. */
void DiffuseTransmission_blur_grows_with_distance(void) {
    // materials/transmission
    DiffuseTransmission dt = db_blur(0.25f);
    int32_t plain = db_edge_behind("db_grows_plain.png", 0.1f, false, 0, NULL);
    int32_t near_ = db_edge_behind("db_grows_near.png", 0.1f, false, 0, &dt);
    int32_t far_ = db_edge_behind("db_grows_far.png", 4, false, 0, &dt);
    printf("edge width: plain %d, 0.1 behind %d, 4 behind %d\n", plain, near_, far_);
    test_assert(plain <= 3);
    test_assert(near_ <= plain + 1);
    test_assert(far_ >= near_ + 6);
    test_assert(far_ >= 8);
}

/** Column of a world point in a capture of the camera at the origin.
 *
 * @param x X of the point
 * @param z Z of the point, negative
 * @return The column
 */
static int32_t db_column(
    float x,
    float z)
{
    float aspect = (float)DB_W / (float)DB_H;

    return (int32_t)((0.5f + x / -z / (DB_TAN * aspect) * 0.5f) * (float)DB_W);
}

/** A panel of red and blue stripes half a unit wide touches the back of the pane at its left
 * end and turns 0.6 radians away from it, with a blur of 0.15: the stripes are sharp at the
 * contact (about a pixel of blur) and washed out at the far end, where they are more than 3
 * units behind (about 8 pixels, wider than a stripe there). */
void DiffuseTransmission_blur_contact_sharp(void) {
    // materials/transmission
    const uint8_t black[3] = { 0, 0, 0 };
    const float angle = 0.6f, x0 = -1.5f, z0 = -3.03f;
    DiffuseTransmission dt = db_blur(0.15f);
    ecs_world_t *world = db_world();
    db_view(world, 0, 0, false, black);
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {x0, 0, z0});
    ecs_set(world, panel, Rotation, {0, angle, 0});
    int32_t i;
    for (i = 0; i < 12; i ++) {
        db_wall(world, panel, ((float)i + 0.5f) * 0.5f, 0, 0.5f, 3, 0.02f, (i & 1) == 0);
    }

    db_pane(world, 0, 0, 7, 0, &dt);
    unsigned char *px = db_render(world, "db_contact.png");

    float c = cosf(angle), s = sinf(angle);
    int32_t near0 = db_column(x0 + 0.25f * c, z0 - 0.25f * s);
    int32_t near1 = db_column(x0 + 2.0f * c, z0 - 2.0f * s);
    int32_t far0 = db_column(x0 + 4.0f * c, z0 - 4.0f * s);
    int32_t far1 = db_column(x0 + 5.75f * c, z0 - 5.75f * s);
    float near_ = db_sharpness(px, DB_W, DB_H / 2, near0, near1);
    float far_ = db_sharpness(px, DB_W, DB_H / 2, far0, far1);
    printf("contact: columns %d..%d step %f, far end: columns %d..%d step %f\n",
        near0, near1, near_, far0, far1, far_);
    test_assert(near1 > near0 + 8 && far1 > far0 + 8);
    test_assert(near_ > 0.25f);
    test_assert(far_ < near_ * 0.5f);
    stbi_image_free(px);
    ecs_fini(world);
}

/** Render a pane over a red wall that covers the left of the capture up to column 183 and
 * the sky (a blue clear color) right of it.
 *
 * @param file Name of the PNG
 * @param roughness Roughness of the pane
 * @param dt The DiffuseTransmission of the pane, NULL for none
 * @return The pixels
 */
static unsigned char* db_sky(
    const char *file,
    float roughness,
    const DiffuseTransmission *dt)
{
    const uint8_t sky[3] = { 0, 0, 120 };
    ecs_world_t *world = db_world();
    db_view(world, 0, 0, false, sky);
    db_wall(world, 0, -7.5f, -10, 25, 40, 0.1f, true);
    db_pane(world, 0, 0, 6, roughness, dt);
    unsigned char *px = db_render(world, file);
    ecs_fini(world);

    return px;
}

/** Through the blurred pane the sky (depth 0) shows the top mip, a mix of the wall and the sky,
 * as a pane of roughness 1 does, while the wall far left keeps its color; no pixel is NaN, which
 * the capture would show as black. */
void DiffuseTransmission_blur_sky_max(void) {
    // materials/transmission
    DiffuseTransmission dt = db_blur(0.05f);
    unsigned char *sharp = db_sky("db_sky_sharp.png", 0, NULL);
    unsigned char *blurred = db_sky("db_sky_blur.png", 0, &dt);
    unsigned char *rough = db_sky("db_sky_rough.png", 1, NULL);
    const unsigned char *s = &sharp[((DB_H / 2) * DB_W + 220) * 4];
    const unsigned char *b = &blurred[((DB_H / 2) * DB_W + 220) * 4];
    const unsigned char *r = &rough[((DB_H / 2) * DB_W + 220) * 4];
    const unsigned char *wall = &blurred[((DB_H / 2) * DB_W + 30) * 4];
    const unsigned char *wall_sharp = &sharp[((DB_H / 2) * DB_W + 30) * 4];
    int32_t x, y, c, dark = 0;
    printf("sky: sharp %d %d %d, blur %d %d %d, rough %d %d %d\n", s[0], s[1], s[2],
        b[0], b[1], b[2], r[0], r[1], r[2]);
    test_assert(s[0] <= 2);
    test_assert(b[0] > 10);
    for (c = 0; c < 3; c ++) {
        test_assert(abs((int32_t)b[c] - (int32_t)r[c]) <= 4);
        test_assert(abs((int32_t)wall[c] - (int32_t)wall_sharp[c]) <= 3);
    }

    for (y = 0; y < DB_H; y ++) {
        for (x = 0; x < DB_W; x ++) {
            const unsigned char *p = &blurred[(y * DB_W + x) * 4];
            dark += p[0] + p[1] + p[2] == 0;
        }
    }

    test_int(dark, 0);
    stbi_image_free(sharp);
    stbi_image_free(blurred);
    stbi_image_free(rough);
}

/** Walls 2 behind the pane are 5 from a camera at the origin and 10 from one 5 units back: the
 * edge is about half as many pixels there, the same width in world units at the walls. */
void DiffuseTransmission_blur_perspective(void) {
    // materials/transmission
    DiffuseTransmission dt = db_blur(0.18f);
    int32_t close = db_edge_behind("db_persp_close.png", 2, false, 0, &dt);
    int32_t back = db_edge_behind("db_persp_back.png", 2, false, 5, &dt);
    float unit = 2.0f * DB_TAN / (float)DB_H;
    float world_close = (float)close * 5.0f * unit, world_back = (float)back * 10.0f * unit;
    printf("edge: %d pixels at 5 (%f units), %d pixels at 10 (%f units)\n", close,
        world_close, back, world_back);
    test_assert(close >= 6);
    test_assert((float)close > (float)back * 1.4f);
    test_assert(fabsf(world_close - world_back) <
        0.35f * (world_close > world_back ? world_close : world_back));
}

/** An orthographic view 4 units high: 0.25 * 4 is a radius of 1 unit, 32 pixels, and 0.25 *
 * 0.1 under one pixel. */
void DiffuseTransmission_blur_orthographic(void) {
    // materials/transmission
    DiffuseTransmission dt = db_blur(0.25f);
    int32_t near_ = db_edge_behind("db_ortho_near.png", 0.1f, true, 0, &dt);
    int32_t far_ = db_edge_behind("db_ortho_far.png", 4, true, 0, &dt);
    printf("orthographic edge: 0.1 behind %d, 4 behind %d\n", near_, far_);
    test_assert(near_ <= 3);
    test_assert(far_ >= near_ + 8);
}

/** A resize recreates the snapshot with its depth copy at the new size, which the blur reads
 * without validation errors. */
void DiffuseTransmission_blur_resize(void) {
    // materials/transmission
    const uint8_t black[3] = { 0, 0, 0 };
    DiffuseTransmission dt = db_blur(0.25f);
    ecs_world_t *world = db_world();
    db_view(world, 0, 0, false, black);
    db_edge_walls(world, 0, 0, 4, 40, 40);
    db_pane(world, 0, 0, 6, 0, &dt);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = DB_W, .height = DB_H,
        .write_to_file = "db_resize_a.png", .resolution_scale = 1});
    box_render_gpu_stats_t before = db_frames(world, 3);
    unsigned char *px = db_load(world, "db_resize_a.png", DB_W, DB_H);
    int32_t a = db_edge(px, DB_W, DB_H / 2, DB_W / 4, DB_W * 3 / 4);
    stbi_image_free(px);

    ecs_set(world, s, Surface, {.width = 192, .height = 96,
        .write_to_file = "db_resize_b.png", .resolution_scale = 1});
    box_render_gpu_stats_t after = db_frames(world, 3);
    px = db_load(world, "db_resize_b.png", 192, 96);
    int32_t b = db_edge(px, 192, 48, 48, 144);
    stbi_image_free(px);
    printf("resize: edge %d at %dx%d, %d at 192x96\n", a, DB_W, DB_H, b);
    test_int(after.errors, before.errors);
    test_int(after.transmission_snapshots - before.transmission_snapshots, 3);
    test_assert(after.transmission_textures > before.transmission_textures);
    test_assert(a >= 8);
    test_assert(b >= 6);
    test_assert(b < a);

    ecs_fini(world);
}

/** The first view has a pane over the left half with an edge 0.1 behind it, the second (of
 * another scene, drawn over the first without clearing) one over the right half with an edge 4
 * behind it. Each copies its own depth: the left edge stays sharp and the right one blurs, which
 * it would not if it read the depth of the first view. */
void DiffuseTransmission_blur_two_views(void) {
    // materials/transmission
    const uint8_t black[3] = { 0, 0, 0 };
    DiffuseTransmission dt = db_blur(0.25f);
    ecs_world_t *world = db_world();
    db_view(world, 0, 0, false, black);
    db_edge_walls(world, 0, -1.5f, 0.1f, 20, 20);
    db_pane(world, 0, -1.8f, 3.4f, 0, &dt);

    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
    ecs_entity_t second = db_view(world, scene, 0, false, black);
    ecs_set(world, second, View, {.camera = ecs_get(world, second, View)->camera,
        .clear = {.enabled = false}, .lighting = {.enabled = true}, .order = 1});
    db_edge_walls(world, scene, 3, 4, 2.5f, 5);
    db_pane(world, scene, 1.8f, 3.4f, 0, &dt);

    box_render_gpu_stats_t before = db_frames(world, 0);
    unsigned char *px = db_render(world, "db_two_views.png");
    box_render_gpu_stats_t after = db_frames(world, 1);
    int32_t left_edge = db_column(-1.5f, -3.09f), right_edge = db_column(3, -6.99f);
    int32_t left = db_edge(px, DB_W, DB_H / 2, left_edge - 30, left_edge + 30);
    int32_t right = db_edge(px, DB_W, DB_H / 2, right_edge - 30, right_edge + 30);
    printf("two views: left edge at %d width %d, right edge at %d width %d\n", left_edge,
        left, right_edge, right);
    test_assert(left <= 3);
    test_assert(right >= left + 6);
    test_int(after.errors, before.errors);
    before = after;
    after = db_frames(world, 1);
    test_int(after.transmission_snapshots - before.transmission_snapshots, 2);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A view whose only DiffuseTransmission with a blur has no Transmission draws no transmissive
 * batch: it records no snapshot and no depth copy, and creates no snapshot texture. */
void DiffuseTransmission_blur_no_snapshot_no_copy(void) {
    // materials/transmission
    const uint8_t black[3] = { 0, 0, 0 };
    DiffuseTransmission dt = db_blur(0.5f);
    dt.factor = 0.5f;
    ecs_world_t *world = db_world();
    db_view(world, 0, 0, false, black);
    db_edge_walls(world, 0, 0, 2, 40, 40);
    ecs_entity_t quad = db_pane(world, 0, 0, 6, 0, &dt);
    ecs_remove(world, quad, Transmission);
    unsigned char *px = db_render(world, "db_no_snapshot.png");
    stbi_image_free(px);
    box_render_gpu_stats_t before = db_frames(world, 0);
    box_render_gpu_stats_t after = db_frames(world, 3);
    test_int(after.transmission_snapshots, 0);
    test_int(after.transmission_textures, 0);
    test_int(after.errors, before.errors);
    test_assert(after.passes > before.passes);
    ecs_fini(world);
}
