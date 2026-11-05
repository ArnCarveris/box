/**
 * @file LightFixture.c
 * @brief Tests that a LightFixture shape casts no shadow into the tile of a spot or point light
 * inside its world bounds and casts as usual for every other light, with the GPU backend.
 *
 * Most cases look down from 4 units above a white floor into a 64x64 image with a 90 degree
 * camera, 8 pixels a unit, the origin on pixel (32, 32).
 *
 * A shadowed light 1.5 units above the origin sits inside a sphere of radius 0.3 or a box of
 * 0.6; the floor 1.5 units (12 pixels) from the origin is not hidden by the fixture.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge in pixels of the images */
#define FIXTURE_SIZE (64)

/** Red channel at most of a shadowed pixel */
#define FIXTURE_DARK (40)

/** Red channel at least of a lit pixel */
#define FIXTURE_LIT (100)

/** Largest red channel difference of a floor pixel lit through a fixture and without it */
#define FIXTURE_TOLERANCE (8)

/** Height of the light inside the fixture */
#define FIXTURE_HEIGHT (1.5f)

/** Radius of the fixture spheres */
#define FIXTURE_RADIUS (0.3f)

/** Floor pixels 12 pixels (1.5 units) from the origin along x and z */
static const int32_t fixture_ring[4][2] = {{44, 32}, {20, 32}, {32, 44}, {32, 20}};

/** Floor pixels 8 pixels (1 unit) from the origin, inside the cone of the spot light */
static const int32_t fixture_cone[4][2] = {{40, 32}, {24, 32}, {32, 40}, {32, 24}};

/** Create a headless world on the main thread without ambient light, with a white rough floor
 * on y = 0 and the camera looking straight down from 4 units with a view that has shadows.
 *
 * @return The world
 */
static ecs_world_t* fixture_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {40, 0.2f, 40});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 4, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 90, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);

    return world;
}

/** Set a shadowed point light on an entity.
 *
 * @param world The world
 * @param e The entity
 * @param range Range of the light
 * @param intensity Intensity of the light
 */
static void fixture_point_on(
    ecs_world_t *world,
    ecs_entity_t e,
    float range,
    float intensity)
{
    ecs_set(world, e, PointLight, {
        .intensity = intensity, .range = range, .bake = LightBakeModeMixed,
        .shadows = true, .shadow_resolution = 512
    });
}

/** Create a shadowed point light.
 *
 * @param world The world
 * @param position World position of the light
 * @param range Range of the light
 * @param intensity Intensity of the light
 * @return The light
 */
static ecs_entity_t fixture_point(
    ecs_world_t *world,
    vec3 position,
    float range,
    float intensity)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    fixture_point_on(world, e, range, intensity);

    return e;
}

/** Create a shadowed spot light shining straight down with a 50 degree cone.
 *
 * @param world The world
 * @param position World position of the light
 * @return The light
 */
static ecs_entity_t fixture_spot(
    ecs_world_t *world,
    vec3 position)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, e, SpotLight, {
        .intensity = 4 * GLM_PIf, .range = 10, .inner_angle = 45, .outer_angle = 50,
        .shadows = true, .shadow_resolution = 512
    });

    return e;
}

/** Make an entity a white sphere.
 *
 * @param world The world
 * @param e The entity
 * @param radius Radius of the sphere
 */
static void fixture_sphere_on(
    ecs_world_t *world,
    ecs_entity_t e,
    float radius)
{
    ecs_set(world, e, Sphere, {.segments = 16, .smooth = true, .radius = radius});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.roughness = 1});
}

/** Create a white sphere.
 *
 * @param world The world
 * @param position World position of its center
 * @param radius Its radius
 * @param fixture Whether it has LightFixture
 * @return The sphere
 */
static ecs_entity_t fixture_sphere(
    ecs_world_t *world,
    vec3 position,
    float radius,
    bool fixture)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    fixture_sphere_on(world, e, radius);
    if (fixture) {
        ecs_add(world, e, LightFixture);
    }

    return e;
}

/** Create a white box.
 *
 * @param world The world
 * @param position World position of its center
 * @param size Its edges
 * @param fixture Whether it has LightFixture
 * @return The box
 */
static ecs_entity_t fixture_box(
    ecs_world_t *world,
    vec3 position,
    vec3 size,
    bool fixture)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Box, {size[0], size[1], size[2]});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.roughness = 1});
    if (fixture) {
        ecs_add(world, e, LightFixture);
    }

    return e;
}

/** Render a frame into an image file and load its pixels.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @return RGBA pixels of a FIXTURE_SIZE square image, freed with stbi_image_free
 */
static unsigned char* fixture_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = FIXTURE_SIZE, .height = FIXTURE_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, FIXTURE_SIZE);
    test_int(h, FIXTURE_SIZE);
    ecs_os_free(path);

    return px;
}

/** Run frames so the shaders and pipelines the scene needs exist, then render a frame.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @return RGBA pixels of a FIXTURE_SIZE square image, freed with stbi_image_free
 */
static unsigned char* fixture_render(
    ecs_world_t *world,
    const char *file)
{
    int32_t i;
    for (i = 0; i < 4; i ++) {
        stbi_image_free(fixture_capture(world, "fixture_warm.png"));
    }

    return fixture_capture(world, file);
}

/** Get the red channel of a pixel.
 *
 * @param px The pixels
 * @param x The column
 * @param y The row
 * @return The channel
 */
static int32_t fixture_red(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return px[(y * FIXTURE_SIZE + x) * 4];
}

/** Check that the pixels of a list are lit.
 *
 * @param px The pixels
 * @param pixels The pixels to check
 * @param count Number of pixels
 */
static void fixture_lit(
    const unsigned char *px,
    const int32_t (*pixels)[2],
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        test_assert(fixture_red(px, pixels[i][0], pixels[i][1]) > FIXTURE_LIT);
    }
}

/** Check that the pixels of a list are dark.
 *
 * @param px The pixels
 * @param pixels The pixels to check
 * @param count Number of pixels
 */
static void fixture_dark(
    const unsigned char *px,
    const int32_t (*pixels)[2],
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        test_assert(fixture_red(px, pixels[i][0], pixels[i][1]) < FIXTURE_DARK);
    }
}

/** Check that the pixels of a list match those of a reference render within tolerance.
 *
 * @param px The pixels
 * @param ref The pixels of the reference
 * @param pixels The pixels to check
 * @param count Number of pixels
 */
static void fixture_matches(
    const unsigned char *px,
    const unsigned char *ref,
    const int32_t (*pixels)[2],
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        int32_t a = fixture_red(px, pixels[i][0], pixels[i][1]);
        int32_t b = fixture_red(ref, pixels[i][0], pixels[i][1]);
        test_assert(abs(a - b) <= FIXTURE_TOLERANCE);
    }
}

/** Check that a world has no device errors.
 *
 * @param world The world
 */
static void fixture_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
}

/** Render the floor under the light 1.5 units up without a shadow cache, with nothing around
 * the light, for the lit reference.
 *
 * @param spot Whether the light is the spot light, else the point light
 * @return RGBA pixels of a FIXTURE_SIZE square image, freed with stbi_image_free
 */
static unsigned char* fixture_reference(
    bool spot)
{
    ecs_world_t *world = fixture_world();
    test_bool(box_render_shadow_cache(world, false), true);
    if (spot) {
        fixture_spot(world, (vec3){0, FIXTURE_HEIGHT, 0});
    } else {
        fixture_point(world, (vec3){0, FIXTURE_HEIGHT, 0}, 10, 4 * GLM_PIf);
    }

    unsigned char *px = fixture_render(world, spot ? "fixture_ref_spot.png" :
        "fixture_ref_point.png");
    fixture_no_errors(world);
    ecs_fini(world);

    return px;
}

void LightFixture_point_inside_fixture_lit(void) {
    // lighting/shadows
    unsigned char *ref = fixture_reference(false);
    fixture_lit(ref, fixture_ring, 4);

    int32_t round;
    for (round = 0; round < 2; round ++) {
        bool tag = round == 0;
        ecs_world_t *world = fixture_world();
        test_bool(box_render_shadow_cache(world, false), true);
        fixture_point(world, (vec3){0, FIXTURE_HEIGHT, 0}, 10, 4 * GLM_PIf);
        fixture_sphere(world, (vec3){0, FIXTURE_HEIGHT, 0}, FIXTURE_RADIUS, tag);
        unsigned char *px = fixture_render(world, tag ? "fixture_point_tag.png" :
            "fixture_point_none.png");
        if (tag) {
            fixture_matches(px, ref, fixture_ring, 4);
        } else {
            fixture_dark(px, fixture_ring, 4);
        }

        stbi_image_free(px);
        test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
        fixture_no_errors(world);
        ecs_fini(world);
    }

    stbi_image_free(ref);
}

void LightFixture_spot_inside_fixture_lit(void) {
    // lighting/shadows
    unsigned char *ref = fixture_reference(true);
    fixture_lit(ref, fixture_cone, 4);

    int32_t round;
    for (round = 0; round < 2; round ++) {
        bool tag = round == 0;
        ecs_world_t *world = fixture_world();
        test_bool(box_render_shadow_cache(world, false), true);
        fixture_spot(world, (vec3){0, FIXTURE_HEIGHT, 0});
        fixture_box(world, (vec3){0, FIXTURE_HEIGHT, 0}, (vec3){0.6f, 0.6f, 0.6f}, tag);
        unsigned char *px = fixture_render(world, tag ? "fixture_spot_tag.png" :
            "fixture_spot_none.png");
        if (tag) {
            fixture_matches(px, ref, fixture_cone, 4);
        } else {
            fixture_dark(px, fixture_cone, 4);
        }

        stbi_image_free(px);
        test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
        fixture_no_errors(world);
        ecs_fini(world);
    }

    stbi_image_free(ref);
}

/** Create the scene of the cases with a second light: a fixture sphere 1 unit up around a
 * light of range 1.2, which reaches the floor only within 0.66 units of the origin.
 *
 * The second point light at (2, 3, 0) is outside the sphere, which shadows it on the floor
 * around (-1, 0, 0), pixel (24, 32).
 *
 * @param sphere Receives the sphere
 * @return The world
 */
static ecs_world_t* fixture_two_lights(
    ecs_entity_t *sphere)
{
    ecs_world_t *world = fixture_world();
    test_bool(box_render_shadow_cache(world, false), true);
    fixture_point(world, (vec3){0, 1, 0}, 1.2f, 0.5f * GLM_PIf);
    fixture_point(world, (vec3){2, 3, 0}, 10, 30 * GLM_PIf);
    *sphere = fixture_sphere(world, (vec3){0, 1, 0}, FIXTURE_RADIUS, true);

    return world;
}

void LightFixture_fixture_shadows_other_light(void) {
    // lighting/shadows
    ecs_entity_t sphere;
    ecs_world_t *world = fixture_two_lights(&sphere);
    unsigned char *px = fixture_render(world, "fixture_other.png");
    test_assert(fixture_red(px, 24, 32) < FIXTURE_DARK);
    test_assert(fixture_red(px, 24, 24) > FIXTURE_LIT);
    test_assert(fixture_red(px, 24, 40) > FIXTURE_LIT);
    test_assert(fixture_red(px, 40, 32) > FIXTURE_LIT);
    stbi_image_free(px);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 2);
    fixture_no_errors(world);
    ecs_fini(world);
}

/* The sun shines down at 45 degrees along z, so a fixture slab 2 units up shadows the floor
 * 2 units off the origin on one side of z, pixel (32, 16) or (32, 48), the other is lit; the
 * light inside the slab does not reach either */
void LightFixture_fixture_shadows_sun(void) {
    // lighting/shadows
    ecs_world_t *world = fixture_world();
    test_bool(box_render_shadow_cache(world, false), true);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_4, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    fixture_point(world, (vec3){0, 2, 0}, 2.5f, 2 * GLM_PIf);
    fixture_box(world, (vec3){0, 2, 0}, (vec3){1, 0.1f, 1}, true);
    unsigned char *px = fixture_render(world, "fixture_sun.png");
    int32_t a = fixture_red(px, 32, 16), b = fixture_red(px, 32, 48);
    test_assert((a < b ? a : b) < FIXTURE_DARK);
    test_assert((a > b ? a : b) > FIXTURE_LIT);
    test_assert(fixture_red(px, 8, 32) > FIXTURE_LIT);
    stbi_image_free(px);
    fixture_no_errors(world);
    ecs_fini(world);
}

void LightFixture_fixture_on_light_entity(void) {
    // lighting/shadows
    ecs_world_t *world = fixture_world();
    test_bool(box_render_shadow_cache(world, false), true);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, FIXTURE_HEIGHT, 0});
    fixture_point_on(world, e, 10, 4 * GLM_PIf);
    fixture_sphere_on(world, e, FIXTURE_RADIUS);
    ecs_add(world, e, LightFixture);
    unsigned char *px = fixture_render(world, "fixture_entity.png");
    fixture_lit(px, fixture_ring, 4);
    stbi_image_free(px);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    fixture_no_errors(world);
    ecs_fini(world);
}

void LightFixture_fixture_parent_of_light(void) {
    // lighting/shadows
    ecs_world_t *world = fixture_world();
    test_bool(box_render_shadow_cache(world, false), true);
    ecs_entity_t parent = fixture_sphere(world, (vec3){0, FIXTURE_HEIGHT, 0}, FIXTURE_RADIUS,
        true);
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, light, Position, {0, 0, 0});
    fixture_point_on(world, light, 10, 4 * GLM_PIf);
    unsigned char *px = fixture_render(world, "fixture_parent.png");
    fixture_lit(px, fixture_ring, 4);
    stbi_image_free(px);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    fixture_no_errors(world);
    ecs_fini(world);
}

void LightFixture_fixture_sibling_of_light(void) {
    // lighting/shadows
    ecs_world_t *world = fixture_world();
    test_bool(box_render_shadow_cache(world, false), true);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, FIXTURE_HEIGHT, 0});
    ecs_entity_t glass = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, glass, Position, {0, 0, 0});
    fixture_sphere_on(world, glass, FIXTURE_RADIUS);
    ecs_add(world, glass, LightFixture);
    ecs_entity_t flame = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, flame, Position, {0, 0, 0});
    fixture_point_on(world, flame, 10, 4 * GLM_PIf);
    unsigned char *px = fixture_render(world, "fixture_sibling.png");
    fixture_lit(px, fixture_ring, 4);
    stbi_image_free(px);
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 1);
    fixture_no_errors(world);
    ecs_fini(world);
}

/* Two spheres around the light, a bulb and a glass, are children of a parent with the tag:
 * both let the light through, and both block it once the parent loses the tag */
void LightFixture_inherited_from_parent(void) {
    // lighting/shadows
    ecs_world_t *world = fixture_world();
    test_bool(box_render_shadow_cache(world, false), true);
    fixture_point(world, (vec3){0, FIXTURE_HEIGHT, 0}, 10, 4 * GLM_PIf);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, FIXTURE_HEIGHT, 0});
    ecs_add(world, parent, LightFixture);
    ecs_entity_t bulb = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, bulb, Position, {0, 0, 0});
    fixture_sphere_on(world, bulb, 0.15f);
    ecs_entity_t glass = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, glass, Position, {0, 0, 0});
    fixture_sphere_on(world, glass, FIXTURE_RADIUS);
    unsigned char *px = fixture_render(world, "fixture_inherited.png");
    fixture_lit(px, fixture_ring, 4);
    stbi_image_free(px);

    ecs_remove(world, parent, LightFixture);
    px = fixture_capture(world, "fixture_inherited_off.png");
    fixture_dark(px, fixture_ring, 4);
    stbi_image_free(px);
    fixture_no_errors(world);
    ecs_fini(world);
}

/* With NoShadow as well the sphere casts for neither light: the floor where it shadows the
 * second light is lit */
void LightFixture_no_shadow_wins(void) {
    // lighting/shadows
    ecs_entity_t sphere;
    ecs_world_t *world = fixture_two_lights(&sphere);
    ecs_add(world, sphere, NoShadow);
    unsigned char *px = fixture_render(world, "fixture_no_shadow.png");
    test_assert(fixture_red(px, 24, 32) > FIXTURE_LIT);
    test_assert(fixture_red(px, 40, 32) > FIXTURE_LIT);
    stbi_image_free(px);
    fixture_no_errors(world);
    ecs_fini(world);
}

/* A sphere of radius 0.5 around the light: lit at (-1.25, 0, 0), pixel (22, 32); the light
 * moved to (0.8, 2.5, 0), outside the sphere, puts the floor there in the shadow of the sphere
 * in the frame of the move */
void LightFixture_light_leaves_fixture(void) {
    // lighting/shadows
    ecs_world_t *world = fixture_world();
    ecs_entity_t light = fixture_point(world, (vec3){0, FIXTURE_HEIGHT, 0}, 10, 4 * GLM_PIf);
    fixture_sphere(world, (vec3){0, FIXTURE_HEIGHT, 0}, 0.5f, true);
    unsigned char *px = fixture_render(world, "fixture_leaves_in.png");
    test_assert(fixture_red(px, 22, 32) > FIXTURE_LIT);
    test_assert(fixture_red(px, 44, 32) > FIXTURE_LIT);
    stbi_image_free(px);

    ecs_set(world, light, Position, {0.8f, 2.5f, 0});
    px = fixture_capture(world, "fixture_leaves_out.png");
    test_assert(fixture_red(px, 22, 32) < FIXTURE_DARK);
    stbi_image_free(px);

    ecs_set(world, light, Position, {0, FIXTURE_HEIGHT, 0});
    px = fixture_capture(world, "fixture_leaves_back.png");
    test_assert(fixture_red(px, 22, 32) > FIXTURE_LIT);
    stbi_image_free(px);
    fixture_no_errors(world);
    ecs_fini(world);
}

/* The tile of the light tests the floor, the fixture around the light and a sphere of the
 * same mesh next to it, and draws the floor and the sphere */
void LightFixture_tile_counts(void) {
    // lighting/shadows
    ecs_world_t *world = fixture_world();
    test_bool(box_render_shadow_cache(world, false), true);
    fixture_point(world, (vec3){0, FIXTURE_HEIGHT, 0}, 10, 4 * GLM_PIf);
    fixture_sphere(world, (vec3){0, FIXTURE_HEIGHT, 0}, FIXTURE_RADIUS, true);
    fixture_sphere(world, (vec3){1.5f, 1, 0}, FIXTURE_RADIUS, false);
    stbi_image_free(fixture_render(world, "fixture_counts.png"));

    box_render_cull_stats_t tile;
    test_bool(box_render_gpu_cull_tile(world, 0, &tile), true);
    test_int(tile.tested, 3);
    test_int(tile.visible, 2);
    test_bool(box_render_gpu_cull_tile(world, 1, &tile), false);
    fixture_no_errors(world);
    ecs_fini(world);
}

/** Run a frame and check that the shadow cache renders one tile and keeps the other.
 *
 * @param world The world
 * @param tile Index of the tile that renders
 */
static void fixture_renders_one(
    ecs_world_t *world,
    int32_t tile)
{
    box_render_gpu_stats_t before, after;
    box_render_cull_stats_t cull;
    int32_t i;
    test_assert(box_render_gpu_stats(world, &before));
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &after));
    test_int(after.shadow_tiles - before.shadow_tiles, 1);
    test_int(after.shadow_reused - before.shadow_reused, 1);
    for (i = 0; i < 2; i ++) {
        test_bool(box_render_gpu_cull_tile(world, i, &cull), i == tile);
    }
}

/** Run a frame and check that the shadow cache renders no tile.
 *
 * @param world The world
 */
static void fixture_renders_none(
    ecs_world_t *world)
{
    box_render_gpu_stats_t before, after;
    test_assert(box_render_gpu_stats(world, &before));
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &after));
    test_int(after.shadow_tiles - before.shadow_tiles, 0);
    test_int(after.shadow_reused - before.shadow_reused, 2);
}

/* A point light at the origin with a range of 4, tile 0, and a spot light at x = 5, tile 1,
 * as in ShadowCache, where tiles of point lights come first; the slab 2 units up is in the range
 * of the point light and above the spot light, out of what its tile holds */
void LightFixture_tag_change_renders_tile(void) {
    // lighting/shadows
    ecs_world_t *world = fixture_world();
    fixture_point(world, (vec3){0, FIXTURE_HEIGHT, 0}, 4, 4 * GLM_PIf);
    fixture_spot(world, (vec3){5, FIXTURE_HEIGHT, 0});
    ecs_entity_t slab = fixture_box(world, (vec3){0.5f, 2, 0.5f}, (vec3){0.4f, 0.1f, 0.4f},
        false);
    stbi_image_free(fixture_render(world, "fixture_tag_change.png"));
    test_int(ecs_singleton_get(world, RenderStatus)->shadow_tiles, 2);
    fixture_renders_none(world);

    ecs_add(world, slab, LightFixture);
    fixture_renders_one(world, 0);
    fixture_renders_none(world);

    ecs_remove(world, slab, LightFixture);
    fixture_renders_one(world, 0);
    fixture_renders_none(world);
    fixture_no_errors(world);
    ecs_fini(world);
}
