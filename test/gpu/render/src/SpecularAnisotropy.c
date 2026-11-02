/**
 * @file SpecularAnisotropy.c
 * @brief Tests for how a SpecularAnisotropy shades and updates, checked on PNG captures.
 *
 * Most cases look at a smooth metal quad facing the camera under a point light 3 units in front
 * of it. The U direction of a Quad runs along +x, so a rotation of 0 stretches its highlight
 * along the x axis of the image and a rotation of pi / 2 along the y axis.
 *
 * Rotation turns counter clockwise from +U: pi / 4 stretches the highlight toward the top right.
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

/** Width of the captures of the anisotropy cases */
#define ANISO_W (128)

/** Height of the captures of the anisotropy cases */
#define ANISO_H (96)

/** Width of the captures of the reflection case */
#define PROBE_W (64)

/** Height of the captures of the reflection case */
#define PROBE_H (48)

/** Create the world of the anisotropy cases: a camera 6 units in front of the origin looking
 * down -z into a 128x96 surface, where a unit at the origin is about 14 pixels and the origin
 * is pixel 64, 48.
 *
 * It has no ambient light and a directional light of intensity 0, so there is no default sun.
 *
 * @param lit Whether the view is lit
 * @return The world
 */
static ecs_world_t* aniso_world(
    bool lit)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = lit});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});

    return world;
}

/** Render two frames of an anisotropy case into a 128x96 PNG and load it.
 *
 * @param world The world
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* aniso_render(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = ANISO_W, .height = ANISO_H,
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
    test_int(w, ANISO_W);
    test_int(h, ANISO_H);

    return px;
}

/** Give a shape the material of the anisotropy cases: a light gray smooth metal.
 *
 * @param world The world
 * @param e The shape
 * @param anisotropy The SpecularAnisotropy of the shape, NULL for none
 */
static void aniso_material(
    ecs_world_t *world,
    ecs_entity_t e,
    const SpecularAnisotropy *anisotropy)
{
    ecs_set(world, e, Rgb, {220, 220, 220});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 0.4f});
    if (anisotropy) {
        ecs_set_ptr(world, e, SpecularAnisotropy, anisotropy);
    }
}

/** Add a smooth metal 4x4 quad facing the camera, centered on a point of the x axis.
 *
 * @param world The world
 * @param x Position of the center along x
 * @param anisotropy The SpecularAnisotropy of the quad, NULL for none
 * @return The quad
 */
static ecs_entity_t aniso_quad(
    ecs_world_t *world,
    float x,
    const SpecularAnisotropy *anisotropy)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Quad, {4, 4});
    aniso_material(world, e, anisotropy);

    return e;
}

/** Add a point light 3 units in front of a point of the x axis.
 *
 * @param world The world
 * @param x Position along x
 * @param intensity Intensity of the light
 */
static void aniso_light(
    ecs_world_t *world,
    float x,
    float intensity)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 3});
    ecs_set(world, e, PointLight, {.intensity = intensity, .range = 20});
}

/** Render a smooth metal quad on the origin under a point light 3 units in front of it.
 *
 * @param anisotropy The SpecularAnisotropy of the quad, NULL for none
 * @param lit Whether the view is lit
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* aniso_highlight(
    const SpecularAnisotropy *anisotropy,
    bool lit,
    const char *name)
{
    ecs_world_t *world = aniso_world(lit);
    aniso_quad(world, 0, anisotropy);
    aniso_light(world, 0, 10);
    unsigned char *px = aniso_render(world, name);
    ecs_fini(world);

    return px;
}

/** Sum of the channels of a pixel of an anisotropy capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return Red plus green plus blue
 */
static int aniso_sum(
    const unsigned char *px,
    int x,
    int y)
{
    const unsigned char *p = &px[(y * ANISO_W + x) * 4];

    return p[0] + p[1] + p[2];
}

/** Measure the highlight in a range of columns of an anisotropy capture: the columns and the
 * rows that hold a pixel at least half as bright as the brightest one of the range.
 *
 * @param px The pixels
 * @param x0 First column
 * @param x1 Column after the last
 * @param wx Receives the number of columns
 * @param wy Receives the number of rows
 * @return The channel sum of the brightest pixel
 */
static int aniso_extent(
    const unsigned char *px,
    int x0,
    int x1,
    int *wx,
    int *wy)
{
    int best = 0, i, j;
    for (j = 0; j < ANISO_H; j ++) {
        for (i = x0; i < x1; i ++) {
            int sum = aniso_sum(px, i, j);
            best = sum > best ? sum : best;
        }
    }

    *wx = 0;
    *wy = 0;
    for (i = x0; i < x1; i ++) {
        bool hit = false;
        for (j = 0; j < ANISO_H && !hit; j ++) {
            hit = 2 * aniso_sum(px, i, j) >= best;
        }

        *wx += hit;
    }

    for (j = 0; j < ANISO_H; j ++) {
        bool hit = false;
        for (i = x0; i < x1 && !hit; i ++) {
            hit = 2 * aniso_sum(px, i, j) >= best;
        }

        *wy += hit;
    }

    return best;
}

/** Check that two anisotropy captures are identical and free them.
 *
 * @param a The first capture
 * @param b The second capture
 */
static void aniso_expect_same(
    unsigned char *a,
    unsigned char *b)
{
    test_int(memcmp(a, b, ANISO_W * ANISO_H * 4), 0);
    stbi_image_free(a);
    stbi_image_free(b);
}

void SpecularAnisotropy_strength_zero_unchanged(void) {
    // materials/pbr
    SpecularAnisotropy anisotropy = {.strength = 0, .rotation = 1};
    unsigned char *plain = aniso_highlight(NULL, true, "aniso_zero_plain.png");
    test_assert(aniso_sum(plain, 64, 48) > 100);
    aniso_expect_same(plain, aniso_highlight(&anisotropy, true, "aniso_zero.png"));
}

void SpecularAnisotropy_stretches_highlight(void) {
    // materials/pbr
    SpecularAnisotropy anisotropy = {.strength = 0.6f, .rotation = 0};
    unsigned char *plain = aniso_highlight(NULL, true, "aniso_round.png");
    unsigned char *stretched = aniso_highlight(&anisotropy, true, "aniso_stretched.png");
    int px = 0, py = 0, sx = 0, sy = 0;
    int plain_peak = aniso_extent(plain, 0, ANISO_W, &px, &py);
    int stretched_peak = aniso_extent(stretched, 0, ANISO_W, &sx, &sy);
    test_assert(plain_peak > 300);
    test_assert(stretched_peak > 150);

    test_assert(px >= 3 && py >= 3);
    test_assert(abs(px - py) <= 2 + px / 5);

    test_assert(sx * 2 >= sy * 3);
    test_assert(sx > px);

    stbi_image_free(plain);
    stbi_image_free(stretched);
}

void SpecularAnisotropy_rotation_turns_highlight(void) {
    // materials/pbr
    SpecularAnisotropy along = {.strength = 0.6f, .rotation = 0};
    SpecularAnisotropy across = {.strength = 0.6f, .rotation = GLM_PI_2f};
    unsigned char *a = aniso_highlight(&along, true, "aniso_along.png");
    unsigned char *b = aniso_highlight(&across, true, "aniso_across.png");
    int ax = 0, ay = 0, bx = 0, by = 0;
    aniso_extent(a, 0, ANISO_W, &ax, &ay);
    aniso_extent(b, 0, ANISO_W, &bx, &by);
    test_assert(ax * 2 >= ay * 3);
    test_assert(by * 2 >= bx * 3);
    test_assert(abs(ax - by) <= 2 + ax / 8);
    test_assert(abs(ay - bx) <= 2 + ay / 8);

    SpecularAnisotropy diagonal = {.strength = 0.6f, .rotation = GLM_PI_4f};
    unsigned char *c = aniso_highlight(&diagonal, true, "aniso_diagonal.png");
    test_assert(aniso_sum(c, 76, 36) > aniso_sum(c, 76, 60) * 2);
    test_assert(aniso_sum(c, 52, 60) > aniso_sum(c, 52, 36) * 2);

    stbi_image_free(a);
    stbi_image_free(b);
    stbi_image_free(c);
}

/** Render a smooth white metal cylinder along y in a view that samples reflection probes, with
 * an infinitely distant probe that captures a black enclosure with a bright vertical bar on its
 * wall toward +z, the one the cylinder reflects back toward the camera.
 *
 * A flat mirror reflects the same directions about the bent normal as about its own, so the
 * stretch only shows on a curved surface. U runs around the axis of the cylinder: a rotation of
 * 0 spreads the thin reflection of the bar across it, one of pi / 2 keeps it thin.
 *
 * @param anisotropy The SpecularAnisotropy of the cylinder, NULL for none
 * @param name File name of the capture
 * @return The pixels, 64x48, freed with stbi_image_free
 */
static unsigned char* aniso_probe(
    const SpecularAnisotropy *anisotropy,
    const char *name)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, ReflectionBake, {.resolution = 64, .budget_ms = 1,
        .scene_probe = false});
    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});

    int32_t i;
    for (i = 0; i < 6; i ++) {
        int32_t axis = i / 2;
        float position[3] = {6, 0, 0}, size[3] = {3.2f, 3.2f, 3.2f};
        position[axis] += i % 2 ? -1.4f : 1.4f;
        size[axis] = 0.2f;
        ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, wall, Position, {position[0], position[1], position[2]});
        ecs_set(world, wall, Box, {size[0], size[1], size[2]});
        ecs_set(world, wall, Rgb, {0, 0, 0});
    }

    ecs_entity_t bar = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, bar, Position, {6, 0, 1.2f});
    ecs_set(world, bar, Box, {0.15f, 2.6f, 0.1f});
    ecs_set(world, bar, Rgb, {0, 0, 0});
    ecs_set(world, bar, Emissive, {.strength = 2, .color = {255, 255, 255}});

    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 0, 0});
    ecs_set(world, probe, ReflectionProbe, {.extent = {3, 3, 3}, .offset = {6, 0, 0}});

    ecs_entity_t cylinder = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, cylinder, Position, {0, 0, 0});
    ecs_set(world, cylinder, Cylinder, {.segments = 64, .smooth = true, .length = 3,
        .radius = 0.8f});
    ecs_set(world, cylinder, Rgb, {255, 255, 255});
    ecs_set(world, cylinder, PbrMaterial, {.metallic = 1, .roughness = 0.05f});
    if (anisotropy) {
        ecs_set_ptr(world, cylinder, SpecularAnisotropy, anisotropy);
    }

    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 0, 4});
    ecs_set(world, camera, Camera, {.fov = 30, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .lighting = {.enabled = true, .reflections = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = PROBE_W, .height = PROBE_H,
        .write_to_file = ECS_CONST_CAST(char*, name), .resolution_scale = 1});
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, PROBE_W);
    test_int(h, PROBE_H);
    ecs_os_free(path);
    ecs_fini(world);

    return px;
}

/** Width of the reflection of the bar along the middle row of a reflection capture: the
 * pixels at least half as bright as the brightest one of the row.
 *
 * @param px The pixels
 * @param peak Receives the channel sum of the brightest pixel
 * @return The width in pixels
 */
static int aniso_bar_width(
    const unsigned char *px,
    int *peak)
{
    const unsigned char *row = &px[(PROBE_H / 2) * PROBE_W * 4];
    int best = 0, count = 0, i;
    for (i = 0; i < PROBE_W; i ++) {
        int sum = row[i * 4] + row[i * 4 + 1] + row[i * 4 + 2];
        best = sum > best ? sum : best;
    }

    for (i = 0; i < PROBE_W; i ++) {
        count += 2 * (row[i * 4] + row[i * 4 + 1] + row[i * 4 + 2]) >= best;
    }

    *peak = best;

    return count;
}

void SpecularAnisotropy_indirect_stretches_reflection(void) {
    // materials/pbr
    SpecularAnisotropy across = {.strength = 1, .rotation = 0};
    SpecularAnisotropy along = {.strength = 1, .rotation = GLM_PI_2f};
    unsigned char *plain = aniso_probe(NULL, "aniso_probe_plain.png");
    unsigned char *wide = aniso_probe(&across, "aniso_probe_across.png");
    unsigned char *same = aniso_probe(&along, "aniso_probe_along.png");
    int plain_peak = 0, wide_peak = 0, same_peak = 0;
    int plain_width = aniso_bar_width(plain, &plain_peak);
    int wide_width = aniso_bar_width(wide, &wide_peak);
    int same_width = aniso_bar_width(same, &same_peak);
    test_assert(plain_peak > 200);
    test_assert(wide_peak > 100);
    test_assert(plain_width >= 2);
    test_assert(plain_width < PROBE_W / 3);
    test_assert(wide_width >= plain_width * 3);
    test_assert(abs(same_width - plain_width) <= 2);

    stbi_image_free(plain);
    stbi_image_free(wide);
    stbi_image_free(same);
}

/** Render a smooth metal mesh square of two triangles facing the camera, without UVs, so every
 * vertex has the UV 0, 0 and the derivatives of the UVs give no direction.
 *
 * @param anisotropy The SpecularAnisotropy of the mesh, NULL for none
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* aniso_degenerate(
    const SpecularAnisotropy *anisotropy,
    const char *name)
{
    ecs_world_t *world = aniso_world(true);
    aniso_light(world, 0, 10);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    Mesh3 *m = ecs_ensure(world, e, Mesh3);
    ecs_vec_init_t(NULL, &m->vertices, vec3, 4);
    ecs_vec_set_count_t(NULL, &m->vertices, vec3, 4);
    vec3 *p = ecs_vec_first_t(&m->vertices, vec3);
    glm_vec3_copy((vec3){ -2.0f, -2.0f, 0.0f }, p[0]);
    glm_vec3_copy((vec3){ 2.0f, -2.0f, 0.0f }, p[1]);
    glm_vec3_copy((vec3){ 2.0f, 2.0f, 0.0f }, p[2]);
    glm_vec3_copy((vec3){ -2.0f, 2.0f, 0.0f }, p[3]);
    ecs_vec_init_t(NULL, &m->normals, vec3, 4);
    ecs_vec_set_count_t(NULL, &m->normals, vec3, 4);
    vec3 *nrm = ecs_vec_first_t(&m->normals, vec3);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        glm_vec3_copy((vec3){ 0.0f, 0.0f, 1.0f }, nrm[i]);
    }

    ecs_vec_init_t(NULL, &m->indices, uint32_t, 6);
    ecs_vec_set_count_t(NULL, &m->indices, uint32_t, 6);
    uint32_t *idx = ecs_vec_first_t(&m->indices, uint32_t);
    uint32_t order[6] = { 0, 1, 2, 0, 2, 3 };
    for (i = 0; i < 6; i ++) {
        idx[i] = order[i];
    }

    ecs_modified(world, e, Mesh3);
    aniso_material(world, e, anisotropy);
    unsigned char *px = aniso_render(world, name);
    ecs_fini(world);

    return px;
}

void SpecularAnisotropy_degenerate_uvs_isotropic(void) {
    // materials/pbr
    SpecularAnisotropy anisotropy = {.strength = 1, .rotation = 0.5f};
    unsigned char *plain = aniso_degenerate(NULL, "aniso_degenerate_plain.png");
    unsigned char *flat = aniso_degenerate(&anisotropy, "aniso_degenerate.png");
    int wx = 0, wy = 0;
    test_assert(aniso_extent(plain, 0, ANISO_W, &wx, &wy) > 300);
    test_assert(abs(wx - wy) <= 2 + wx / 5);
    aniso_expect_same(plain, flat);
}

void SpecularAnisotropy_ignored_by_unlit(void) {
    // materials/pbr
    SpecularAnisotropy anisotropy = {.strength = 1, .rotation = 0};
    aniso_expect_same(aniso_highlight(NULL, false, "aniso_unlit_plain.png"),
        aniso_highlight(&anisotropy, false, "aniso_unlit.png"));
}

/** Render a 2D rectangle in a lit view under a point light.
 *
 * @param anisotropy The SpecularAnisotropy of the rectangle, NULL for none
 * @param name File name of the capture
 * @return The pixels
 */
static unsigned char* aniso_rect(
    const SpecularAnisotropy *anisotropy,
    const char *name)
{
    ecs_world_t *world = aniso_world(true);
    aniso_light(world, 0, 100);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rect, {4, 2});
    ecs_set(world, e, Rgb, {200, 120, 40});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.5f});
    if (anisotropy) {
        ecs_set_ptr(world, e, SpecularAnisotropy, anisotropy);
    }

    unsigned char *px = aniso_render(world, name);
    ecs_fini(world);

    return px;
}

void SpecularAnisotropy_ignored_by_2d(void) {
    // materials/pbr
    SpecularAnisotropy anisotropy = {.strength = 1, .rotation = 0};
    unsigned char *plain = aniso_rect(NULL, "aniso_rect_plain.png");
    test_assert(aniso_sum(plain, 64, 48) > 30);
    aniso_expect_same(plain, aniso_rect(&anisotropy, "aniso_rect.png"));
}

void SpecularAnisotropy_prefab(void) {
    // materials/pbr
    ecs_world_t *world = aniso_world(true);
    aniso_light(world, -2.5f, 10);
    aniso_light(world, 2.5f, 10);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Quad, {4, 4});
    aniso_material(world, prefab, &(SpecularAnisotropy){.strength = 0.6f, .rotation = 0});
    ecs_entity_t shared = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, shared, Position, {-2.5f, 0, 0});
    ecs_entity_t own = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, own, Position, {2.5f, 0, 0});
    ecs_set(world, own, SpecularAnisotropy, {.strength = 0.6f, .rotation = 0});
    test_assert(ecs_owns(world, own, SpecularAnisotropy));
    test_assert(!ecs_owns(world, shared, SpecularAnisotropy));

    int lx = 0, ly = 0, rx = 0, ry = 0;
    unsigned char *px = aniso_render(world, "aniso_prefab.png");
    test_assert(aniso_extent(px, 0, 64, &lx, &ly) > 150);
    test_assert(aniso_extent(px, 64, ANISO_W, &rx, &ry) > 150);
    test_assert(lx * 2 >= ly * 3);
    test_assert(rx * 2 >= ry * 3);
    stbi_image_free(px);

    ecs_set(world, prefab, SpecularAnisotropy, {.strength = 0, .rotation = 0});
    px = aniso_render(world, "aniso_prefab_off.png");
    test_assert(aniso_extent(px, 0, 64, &lx, &ly) > 300);
    aniso_extent(px, 64, ANISO_W, &rx, &ry);
    test_assert(abs(lx - ly) <= 2 + lx / 5);
    test_assert(rx * 2 >= ry * 3);
    stbi_image_free(px);

    ecs_fini(world);
}

void SpecularAnisotropy_change_and_remove(void) {
    // materials/pbr
    unsigned char *before = aniso_highlight(NULL, true, "aniso_before.png");
    int bx = 0, by = 0;
    aniso_extent(before, 0, ANISO_W, &bx, &by);

    ecs_world_t *world = aniso_world(true);
    ecs_entity_t quad = aniso_quad(world, 0,
        &(SpecularAnisotropy){.strength = 0.6f, .rotation = 0});
    aniso_light(world, 0, 10);
    int x = 0, y = 0;
    unsigned char *px = aniso_render(world, "aniso_change.png");
    aniso_extent(px, 0, ANISO_W, &x, &y);
    test_assert(x * 2 >= y * 3);
    stbi_image_free(px);

    ecs_set(world, quad, SpecularAnisotropy, {.strength = 0.6f, .rotation = GLM_PI_2f});
    px = aniso_render(world, "aniso_change_turned.png");
    aniso_extent(px, 0, ANISO_W, &x, &y);
    test_assert(y * 2 >= x * 3);
    stbi_image_free(px);

    ecs_remove(world, quad, SpecularAnisotropy);
    px = aniso_render(world, "aniso_change_removed.png");
    test_int(memcmp(px, before, ANISO_W * ANISO_H * 4), 0);
    stbi_image_free(px);
    ecs_fini(world);

    world = aniso_world(true);
    quad = aniso_quad(world, 0, &(SpecularAnisotropy){.strength = 0, .rotation = 0});
    ecs_add(world, quad, DynamicMaterial);
    aniso_light(world, 0, 10);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = ANISO_W, .height = ANISO_H,
        .write_to_file = "aniso_dynamic.png", .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/aniso_dynamic.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    aniso_extent(px, 0, ANISO_W, &x, &y);
    test_assert(abs(x - y) <= 2 + x / 5);
    stbi_image_free(px);

    SpecularAnisotropy *anisotropy = ecs_get_mut(world, quad, SpecularAnisotropy);
    anisotropy->strength = 0.6f;
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    aniso_extent(px, 0, ANISO_W, &x, &y);
    test_assert(x * 2 >= y * 3);
    stbi_image_free(px);

    ecs_os_free(path);
    ecs_fini(world);
    stbi_image_free(before);
}

/** Seconds every frame of the transition case advances the world: its transition takes a
 * second, so the frames after a change render at progress 0, 1/4, 1/2, 3/4 and 1 */
#define ANISO_DELTA (0.25f)

/** Render a frame of the transition case, that advances the world by ANISO_DELTA, into a PNG and
 * load it. The surface persists between the frames, so only the file name changes.
 *
 * @param world The world
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* aniso_step(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = ANISO_W, .height = ANISO_H,
        .write_to_file = ECS_CONST_CAST(char*, name), .resolution_scale = 1});
    ecs_progress(world, ANISO_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, ANISO_W);
    test_int(h, ANISO_H);

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
static unsigned char* aniso_steps(
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

        px = aniso_step(world, name);
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
static void aniso_expect_close(
    unsigned char *a,
    unsigned char *b,
    int tolerance)
{
    int i, worst = 0;
    for (i = 0; i < ANISO_W * ANISO_H * 4; i ++) {
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
static void aniso_same_batch(
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

/** The highlight of the smooth metal quad turns from along x (rotation 0) to along y (pi / 2)
 * over a second: at half the duration it lies along the diagonal of a rotation of pi / 4.
 *
 * At the end the pixels are those of a quad that has the target from the start. Its batch and
 * slot do not change.
 */
void SpecularAnisotropy_transition_mid(void) {
    // materials/pbr
    SpecularAnisotropy along = {.strength = 0.6f, .rotation = 0};
    SpecularAnisotropy across = {.strength = 0.6f, .rotation = GLM_PI_2f};

    ecs_world_t *still = aniso_world(true);
    ecs_singleton_set(still, RenderSettings, {.in_flight = 2, .main_thread = true});
    aniso_quad(still, 0, &across);
    aniso_light(still, 0, 10);
    unsigned char *target = aniso_steps(still, "aniso_transition_still", 0, 7);
    ecs_fini(still);

    ecs_world_t *world = aniso_world(true);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t e = aniso_quad(world, 0, &along);
    aniso_light(world, 0, 10);
    box_transition_set(world, e, ecs_id(SpecularAnisotropy),
        &(TransitionSpec){ .duration = 1.0f });
    unsigned char *px = aniso_steps(world, "aniso_transition", 0, 2);
    int ax = 0, ay = 0;
    aniso_extent(px, 0, ANISO_W, &ax, &ay);
    test_assert(ax * 2 >= ay * 3);
    stbi_image_free(px);
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    int32_t slot = ecs_get(world, e, DrawSlot)->slot;

    ecs_set_ptr(world, e, SpecularAnisotropy, &across);
    px = aniso_steps(world, "aniso_transition", 2, 3);
    test_flt(ecs_get(world, e, SpecularAnisotropy)->rotation, GLM_PI_2f);
    aniso_same_batch(world, e, batch, slot);
    test_assert(aniso_sum(px, 76, 36) > aniso_sum(px, 76, 60) * 2);
    test_assert(aniso_sum(px, 52, 60) > aniso_sum(px, 52, 36) * 2);
    stbi_image_free(px);

    px = aniso_steps(world, "aniso_transition", 5, 2);
    aniso_same_batch(world, e, batch, slot);
    aniso_expect_close(px, target, 1);
    ecs_fini(world);
}
