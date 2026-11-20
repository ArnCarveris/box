/**
 * @file WindHierarchy.c
 * @brief Tests for a WindSway with pivots: one mesh whose per vertex data holds the pivot, level,
 * phase, stiffness and flutter weight of its branches sways like a hierarchy of shapes, checked
 * on PNG captures.
 *
 * The side cases look along -z from 12 units in front of a tree at the origin into a 128x128
 * capture where a unit at z 0 is about 9.2 pixels: image right is +x and image up is +y, the
 * center of the image at height HIER_EYE. The tree is one Mesh3 of three boxes: a trunk of level
 * 0 from y 0 to 3, a branch of level 1 along +x at y 1.5 with its pivot where it joins the trunk,
 * and a branch of level 2 standing up from near the tip of that branch with its pivot at its
 * foot.
 *
 * The shadow case looks straight down from 12 units above a white floor, a sun at 45 degrees
 * casting shadows along +z, at a 3 unit pole of level 1 with its pivot at its foot.
 *
 * The wind blows along +x without gusts at full speed. The branches swing with a frequency of 0.
 * Their phases are hashed from their pivots (wind_branch_phase of wind.wgsl, hier_phase), which
 * are picked so the phases are about HIER_PHASE, at which the swing about the wind axis is 0, so
 * the branches lean downwind by about HIER_TURN radians every frame and every run.
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

/** Edge of the captures in pixels */
#define HIER_SIZE (128)

/** Seconds every frame of the cases advances the world */
#define HIER_DT (1.0f / 60.0f)

/** Pixels of one unit 12 units from the camera */
#define HIER_UNIT ((float)HIER_SIZE / (24.0f * tanf(GLM_PIf / 6.0f)))

/** Height of the camera of the side cases, at the center of their captures */
#define HIER_EYE (1.5f)

/** Radians the branches turn about their pivots at full wind */
#define HIER_BRANCH (0.5f)

/** Phase of the branches: sin(2 phase 2 pi + 1.3) is 0, so they do not swing across the wind */
#define HIER_PHASE (0.14655f)

/** Gust scale of the wind of the cases */
#define HIER_GUST_SCALE (20.0f)

/** Radians a branch with its pivot at x 0 leans downwind at HIER_PHASE: the branch angle times
 * 0.6 + 0.4 sin(phase 2 pi) */
#define HIER_TURN (HIER_BRANCH * (0.6f + 0.4f * sinf(2.0f * GLM_PIf * HIER_PHASE)))

/** Pivots of the branch, the twig and the pole, whose hashed phases are about HIER_PHASE */
static const vec3 hier_branch_pivot = { 0, 1.50998f, 0 };
static const vec3 hier_twig_pivot = { 2, 1.60404f, 0 };
static const vec3 hier_pole_pivot = { 0, -0.01103f, 0 };

/** Phase of a branch hashed from the bits of its object space pivot, as wind_branch_phase of
 * wind.wgsl hashes it.
 *
 * @param pivot The pivot
 * @return The phase, 0..1
 */
static float hier_phase(
    const vec3 pivot)
{
    uint32_t b[3];
    memcpy(b, pivot, sizeof(b));
    uint32_t h = b[0] * 0x8da6b343u + b[1] * 0xd8163841u + b[2] * 0xcb1ab31fu;
    h = (h ^ (h >> 16)) * 0x7feb352du;
    h = (h ^ (h >> 15)) * 0x846ca68bu;
    h = h ^ (h >> 16);
    return (float)(h >> 8) / 16777216.0f;
}

/** Radians a branch with a pivot at a world x leans downwind at full wind with a frequency of 0,
 * by its hashed phase and the travel of the sway along the wind.
 *
 * @param pivot The pivot
 * @return The angle
 */
static float hier_turn(
    const vec3 pivot)
{
    return HIER_BRANCH * (0.6f + 0.4f * sinf(2.0f * GLM_PIf * hier_phase(pivot) -
        0.5f * pivot[0] / HIER_GUST_SCALE));
}

/** Turn a point in the xy plane about a pivot by an angle, clockwise seen along -z as a branch
 * leans along +x.
 *
 * @param p The point, turned in place
 * @param pivot The pivot
 * @param angle The angle
 */
static void hier_rotate(
    vec3 p,
    const vec3 pivot,
    float angle)
{
    float x = p[0] - pivot[0], y = p[1] - pivot[1];
    p[0] = pivot[0] + x * cosf(angle) + y * sinf(angle);
    p[1] = pivot[1] - x * sinf(angle) + y * cosf(angle);
}

/** Create the world of the cases, in a scene with Scene.wind.
 *
 * @param side Whether the camera looks along -z at the tree in front of a black background,
 * else straight down at a white floor with shadows
 * @return The world
 */
static ecs_world_t* hier_world(
    bool side)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = side ? 1.0f : 0.2f});
    ecs_singleton_set(world, Scene, {.wind = true});

    ecs_entity_t camera = ecs_new(world);
    if (side) {
        ecs_set(world, camera, Position, {0, HIER_EYE, 12});
    } else {
        ecs_set(world, camera, Position, {0, 12, 0});
        ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    }
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = !side;
    ecs_modified(world, view, View);

    if (!side) {
        ecs_entity_t sun = ecs_new(world);
        ecs_set(world, sun, Rotation, {(float)GLM_PI_4, 0, 0});
        ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

        ecs_entity_t floor = ecs_new(world);
        ecs_set(world, floor, Position, {0, -0.1f, 0});
        ecs_set(world, floor, Box, {40, 0.2f, 40});
        ecs_set(world, floor, Rgb, {255, 255, 255});
        ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    }

    return world;
}

/** Set the wind of the cases: along +x without gusts.
 *
 * @param world The world
 * @param speed The speed
 */
static void hier_wind(
    ecs_world_t *world,
    float speed)
{
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = speed, .gust = 0,
        .gust_scale = HIER_GUST_SCALE});
}

/** Append an axis aligned box of 24 vertices facing out to a mesh, every vertex with the same
 * data.
 *
 * @param m The mesh
 * @param lo Minimum corner
 * @param hi Maximum corner
 * @param data0 Pivot, level and flutter weight of the box, NULL for a mesh without data
 * @param data1 Parent pivot and stiffnesses of the box
 */
static void hier_box(
    Mesh3 *m,
    const float lo[3],
    const float hi[3],
    const float data0[4],
    const float data1[4])
{
    static const float corners[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    static const uint32_t quad[6] = { 0, 1, 2, 0, 2, 3 };
    int32_t axis, side, k;
    for (axis = 0; axis < 3; axis ++) {
        for (side = 0; side < 2; side ++) {
            /* The face spans u and v with u x v along its normal */
            int32_t u = side ? (axis + 1) % 3 : (axis + 2) % 3;
            int32_t w = side ? (axis + 2) % 3 : (axis + 1) % 3;
            uint32_t first = (uint32_t)ecs_vec_count(&m->vertices);
            for (k = 0; k < 4; k ++) {
                float *p = *ecs_vec_append_t(NULL, &m->vertices, vec3);
                float *n = *ecs_vec_append_t(NULL, &m->normals, vec3);
                p[axis] = side ? hi[axis] : lo[axis];
                p[u] = corners[k][0] ? hi[u] : lo[u];
                p[w] = corners[k][1] ? hi[w] : lo[w];
                glm_vec3_zero(n);
                n[axis] = side ? 1.0f : -1.0f;
                if (data0) {
                    glm_vec4_copy(ECS_CONST_CAST(float*, data0),
                        *ecs_vec_append_t(NULL, &m->data0, vec4));
                    glm_vec4_copy(ECS_CONST_CAST(float*, data1),
                        *ecs_vec_append_t(NULL, &m->data1, vec4));
                }
            }

            for (k = 0; k < 6; k ++) {
                *ecs_vec_append_t(NULL, &m->indices, uint32_t) = first + quad[k];
            }
        }
    }
}

/** Set the Mesh3 of the tree of the side cases on an entity.
 *
 * @param world The world
 * @param e The entity
 * @param data Whether the vertices have data
 * @param stiffness Stiffness of both branches
 */
static void hier_tree_set(
    ecs_world_t *world,
    ecs_entity_t e,
    bool data,
    float stiffness)
{
    Mesh3 m = {0};
    vec4 trunk0, trunk1, branch0, branch1, twig0, twig1;
    box_wind_sway_vertex(trunk0, trunk1, (vec3){ 0, 0, 0 }, 0, 1, 0, NULL, 0);
    box_wind_sway_vertex(branch0, branch1, hier_branch_pivot, 1, stiffness, 0, NULL, 0);
    box_wind_sway_vertex(twig0, twig1, hier_twig_pivot, 2, stiffness, 1, hier_branch_pivot,
        stiffness);
    hier_box(&m, (float[3]){ -0.1f, 0, -0.1f }, (float[3]){ 0.1f, 3, 0.1f },
        data ? trunk0 : NULL, trunk1);
    hier_box(&m, (float[3]){ 0.1f, 1.4f, -0.1f }, (float[3]){ 2.5f, 1.6f, 0.1f },
        data ? branch0 : NULL, branch1);
    hier_box(&m, (float[3]){ 1.9f, 1.6f, -0.1f }, (float[3]){ 2.1f, 3, 0.1f },
        data ? twig0 : NULL, twig1);
    ecs_set_ptr(world, e, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_vec_fini_t(NULL, &m.normals, vec3);
    ecs_vec_fini_t(NULL, &m.indices, uint32_t);
    ecs_vec_fini_t(NULL, &m.data0, vec4);
    ecs_vec_fini_t(NULL, &m.data1, vec4);
}

/** Add the tree of the side cases at the origin with a WindSway with pivots, no bend and no
 * flutter.
 *
 * @param world The world
 * @param data Whether the vertices have data
 * @param stiffness Stiffness of both branches
 * @return The tree
 */
static ecs_entity_t hier_tree(
    ecs_world_t *world,
    bool data,
    float stiffness)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rgb, {200, 60, 40});
    hier_tree_set(world, e, data, stiffness);
    ecs_set(world, e, WindSway, {.bend = 0, .height = 3, .base = 0,
        .flutter_frequency = 2, .flutter_reach = 0.3f, .pivots = true,
        .branch = HIER_BRANCH, .branch_frequency = 0});

    return e;
}

/** Advance the world by frames of HIER_DT.
 *
 * @param world The world
 * @param frames Frames
 */
static void hier_frames(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, HIER_DT);
        box_render_sync(world);
    }
}

/** Render two frames into a PNG and load it.
 *
 * @param world The world
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* hier_render(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = HIER_SIZE, .height = HIER_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, name), .resolution_scale = 1});
    hier_frames(world, 2);
    ecs_delete(world, e);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, HIER_SIZE);
    test_int(h, HIER_SIZE);

    return px;
}

/** Whether a pixel of a capture shows the red tree.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return Whether it is red
 */
static bool hier_red(
    const unsigned char *px,
    int x,
    int y)
{
    const unsigned char *p = &px[(y * HIER_SIZE + x) * 4];

    return p[0] > 2 * p[1] + 20 && p[0] > 60;
}

/** Column of a world x at z 0 in a side capture.
 *
 * @param x World x
 * @return The column
 */
static int hier_column(
    float x)
{
    return (int)floorf((float)HIER_SIZE * 0.5f + x * HIER_UNIT);
}

/** Row of a world y at z 0 in a side capture.
 *
 * @param y World y
 * @return The row
 */
static int hier_row(
    float y)
{
    return (int)floorf((float)HIER_SIZE * 0.5f - (y - HIER_EYE) * HIER_UNIT);
}

/** Mean row of the red pixels in a band of columns.
 *
 * @param px The pixels
 * @param left First column
 * @param right Last column
 * @return The mean row, -1 when no pixel is red
 */
static float hier_mean_row(
    const unsigned char *px,
    int left,
    int right)
{
    int x, y, count = 0;
    float sum = 0.0f;
    for (y = 0; y < HIER_SIZE; y ++) {
        for (x = left; x <= right; x ++) {
            if (hier_red(px, x, y)) {
                sum += (float)y;
                count ++;
            }
        }
    }

    return count ? sum / (float)count : -1.0f;
}

/** Mean column of the red pixels in a rectangle of a capture.
 *
 * @param px The pixels
 * @param left First column
 * @param right Last column
 * @param top First row
 * @param bottom Last row
 * @return The mean column, -1 when no pixel is red
 */
static float hier_mean_column(
    const unsigned char *px,
    int left,
    int right,
    int top,
    int bottom)
{
    int x, y, count = 0;
    float sum = 0.0f;
    for (y = top; y <= bottom; y ++) {
        for (x = left; x <= right; x ++) {
            if (hier_red(px, x, y)) {
                sum += (float)x;
                count ++;
            }
        }
    }

    return count ? sum / (float)count : -1.0f;
}

/** Mean column of the shadowed floor pixels in a band of rows: the pixels darker than half the
 * brightest pixel of the band.
 *
 * @param px The pixels
 * @param top First row
 * @param bottom Last row
 * @return The mean column, -1 when no pixel is shadowed
 */
static float hier_dark_column(
    const unsigned char *px,
    int top,
    int bottom)
{
    int x, y, bright = 0, count = 0;
    float sum = 0.0f;
    for (y = top; y <= bottom; y ++) {
        for (x = 0; x < HIER_SIZE; x ++) {
            const unsigned char *p = &px[(y * HIER_SIZE + x) * 4];
            int s = p[0] + p[1] + p[2];
            bright = s > bright ? s : bright;
        }
    }

    for (y = top; y <= bottom; y ++) {
        for (x = 0; x < HIER_SIZE; x ++) {
            const unsigned char *p = &px[(y * HIER_SIZE + x) * 4];
            if ((p[0] + p[1] + p[2]) * 2 < bright) {
                sum += (float)x;
                count ++;
            }
        }
    }

    return count ? sum / (float)count : -1.0f;
}

/** The trunk stays, the branch of level 1 turns down about its pivot at the trunk, and the
 * branch of level 2 turns downwind about its own pivot at its foot and with the branch about the
 * pivot of the branch, so it stays attached to it */
void WindHierarchy_branches_turn_about_pivots(void) {
    // materials/wind_sway
    test_assert(fabsf(hier_phase(hier_branch_pivot) - HIER_PHASE) < 1e-3f);
    test_assert(fabsf(hier_phase(hier_twig_pivot) - HIER_PHASE) < 1e-3f);

    ecs_world_t *world = hier_world(true);
    hier_wind(world, 0);
    hier_tree(world, true, 0);
    hier_frames(world, 3);
    unsigned char *calm = hier_render(world, "hier_turn_calm.png");

    hier_wind(world, 10);
    hier_frames(world, 30);
    unsigned char *windy = hier_render(world, "hier_turn_windy.png");

    /* The trunk below the branch, level 0 without a bend, does not move */
    int x, y;
    for (y = hier_row(1.2f); y <= hier_row(0.2f); y ++) {
        for (x = hier_column(-0.3f); x <= hier_column(0.3f); x ++) {
            test_bool(hier_red(windy, x, y), hier_red(calm, x, y));
        }
    }

    /* Near its pivot the branch of level 1 hardly moves; at 1.3 units out it turns down by
     * 1.3 tan HIER_TURN units, about 0.64 */
    float near_rest = hier_mean_row(calm, hier_column(0.25f), hier_column(0.4f));
    float near_moved = hier_mean_row(windy, hier_column(0.25f), hier_column(0.4f));
    test_assert(near_rest > 0.0f && near_moved > 0.0f);
    test_assert(fabsf(near_moved - near_rest) < 0.3f * HIER_UNIT);
    float far_rest = hier_mean_row(calm, hier_column(1.2f), hier_column(1.4f));
    float far_moved = hier_mean_row(windy, hier_column(1.2f), hier_column(1.4f));
    test_assert(far_rest > 0.0f && far_moved > 0.0f);
    test_assert(fabsf(far_moved - far_rest - 1.3f * tanf(HIER_TURN) * HIER_UNIT) <
        0.25f * HIER_UNIT);

    /* The middle of the branch of level 2 at rest at y 2.3 turns about its foot, then with the
     * branch: far down and downwind of where it would be with its own turn only */
    vec3 rest = { 2, 2.3f, 0 }, own = { 2, 2.3f, 0 }, carried;
    hier_rotate(own, hier_twig_pivot, hier_turn(hier_twig_pivot));
    glm_vec3_copy(own, carried);
    hier_rotate(carried, hier_branch_pivot, hier_turn(hier_branch_pivot));
    test_assert(own[1] - carried[1] > 0.8f);
    int top = hier_row(carried[1] + 0.1f), bottom = hier_row(carried[1] - 0.1f);
    float twig_rest = hier_mean_column(calm, hier_column(1.0f), HIER_SIZE - 1,
        hier_row(rest[1] + 0.1f), hier_row(rest[1] - 0.1f));
    float twig_moved = hier_mean_column(windy, hier_column(1.6f), HIER_SIZE - 1, top, bottom);
    test_assert(twig_rest > 0.0f && twig_moved > 0.0f);
    test_assert(fabsf(twig_rest - ((float)hier_column(2.0f) + 0.5f)) < 0.3f * HIER_UNIT);
    test_assert(fabsf(twig_moved - (float)hier_column(carried[0]) - 0.5f) <
        0.3f * HIER_UNIT);

    /* Nothing is left where the branch of level 2 would be with its own turn only */
    float alone = hier_mean_column(windy, hier_column(own[0] - 0.15f),
        hier_column(own[0] + 0.15f), hier_row(own[1] + 0.1f), hier_row(own[1] - 0.1f));
    test_assert(alone < 0.0f);

    stbi_image_free(calm);
    stbi_image_free(windy);
    ecs_fini(world);
}

/** Branches with a stiffness of 1 do not turn: the tree draws in the wind as in a calm */
void WindHierarchy_stiff_branches_still(void) {
    // materials/wind_sway
    ecs_world_t *world = hier_world(true);
    hier_wind(world, 0);
    hier_tree(world, true, 1);
    hier_frames(world, 3);
    unsigned char *calm = hier_render(world, "hier_stiff_calm.png");

    hier_wind(world, 10);
    hier_frames(world, 30);
    unsigned char *windy = hier_render(world, "hier_stiff_windy.png");
    test_assert(!memcmp(calm, windy, HIER_SIZE * HIER_SIZE * 4));

    stbi_image_free(calm);
    stbi_image_free(windy);
    ecs_fini(world);
}

/** Render the tree in a fresh world after the same frames of a bending, fluttering wind.
 *
 * @param data Whether the vertices have data
 * @param pivots Whether the WindSway reads the pivots
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* hier_bent(
    bool data,
    bool pivots,
    const char *name)
{
    ecs_world_t *world = hier_world(true);
    hier_wind(world, 10);
    ecs_entity_t tree = hier_tree(world, data, 0);
    ecs_set(world, tree, WindSway, {.bend = 0.05f, .height = 3, .base = 0,
        .flutter = 0.05f, .flutter_frequency = 2, .flutter_reach = 0.3f, .pivots = pivots,
        .branch = HIER_BRANCH, .branch_frequency = 0.5f});
    hier_frames(world, 30);
    unsigned char *px = hier_render(world, name);
    ecs_fini(world);

    return px;
}

/** Without per vertex data a WindSway with pivots bends and flutters the tree as one without,
 * and without pivots the data changes nothing */
void WindHierarchy_without_data_unchanged(void) {
    // materials/wind_sway
    unsigned char *plain = hier_bent(false, false, "hier_plain.png");
    unsigned char *pivots = hier_bent(false, true, "hier_plain_pivots.png");
    unsigned char *data = hier_bent(true, false, "hier_data_plain.png");
    test_assert(!memcmp(plain, pivots, HIER_SIZE * HIER_SIZE * 4));
    test_assert(!memcmp(plain, data, HIER_SIZE * HIER_SIZE * 4));

    /* With both the branches turn */
    unsigned char *tree = hier_bent(true, true, "hier_data_pivots.png");
    test_assert(memcmp(plain, tree, HIER_SIZE * HIER_SIZE * 4) != 0);

    stbi_image_free(plain);
    stbi_image_free(pivots);
    stbi_image_free(data);
    stbi_image_free(tree);
}

/** Add the 3 unit pole of the shadow case: one box of level 1 with its pivot at its foot.
 *
 * @param world The world
 * @return The pole
 */
static ecs_entity_t hier_pole(
    ecs_world_t *world)
{
    Mesh3 m = {0};
    vec4 data0, data1;
    test_assert(fabsf(hier_phase(hier_pole_pivot) - HIER_PHASE) < 1e-3f);
    box_wind_sway_vertex(data0, data1, hier_pole_pivot, 1, 0, 0, NULL, 0);
    hier_box(&m, (float[3]){ -0.15f, 0, -0.15f }, (float[3]){ 0.15f, 3, 0.15f }, data0, data1);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rgb, {200, 60, 40});
    ecs_set_ptr(world, e, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_vec_fini_t(NULL, &m.normals, vec3);
    ecs_vec_fini_t(NULL, &m.indices, uint32_t);
    ecs_vec_fini_t(NULL, &m.data0, vec4);
    ecs_vec_fini_t(NULL, &m.data1, vec4);
    ecs_set(world, e, WindSway, {.bend = 0, .height = 3, .base = 0,
        .flutter_frequency = 2, .flutter_reach = 0.3f, .pivots = true,
        .branch = HIER_BRANCH, .branch_frequency = 0});

    return e;
}

/** The shadow pass turns the pole about its pivot as the color pass does: seen from above the
 * pole leans downwind, and its shadow 1.8 to 2.4 units along +z moves by 2.1 tan HIER_TURN
 * units, where the shadow of the turned pole falls */
void WindHierarchy_shadow_follows_branch(void) {
    // materials/wind_sway
    ecs_world_t *world = hier_world(false);
    hier_wind(world, 0);
    hier_pole(world);
    hier_frames(world, 3);
    unsigned char *calm = hier_render(world, "hier_shadow_calm.png");

    hier_wind(world, 10);
    hier_frames(world, 30);
    unsigned char *windy = hier_render(world, "hier_shadow_windy.png");

    float floor_unit = HIER_UNIT;
    float pole_rest = hier_mean_column(calm, 0, HIER_SIZE - 1, 0, HIER_SIZE - 1);
    float pole_moved = hier_mean_column(windy, 0, HIER_SIZE - 1, 0, HIER_SIZE - 1);
    test_assert(pole_rest > 0.0f && pole_moved > 0.0f);
    test_assert(pole_moved - pole_rest > 0.4f * floor_unit);

    int top = HIER_SIZE / 2 + (int)(1.8f * floor_unit);
    int bottom = HIER_SIZE / 2 + (int)(2.4f * floor_unit);
    float rest = hier_dark_column(calm, top, bottom);
    float moved = hier_dark_column(windy, top, bottom);
    test_assert(rest > 0.0f && moved > 0.0f);
    test_assert(fabsf(rest - HIER_SIZE / 2.0f) < 0.3f * floor_unit);
    test_assert(fabsf(moved - rest - 2.1f * tanf(HIER_TURN) * floor_unit) <
        0.35f * floor_unit);

    stbi_image_free(calm);
    stbi_image_free(windy);
    ecs_fini(world);
}

/** A small broadleaf whose twigs of level 2 carry leaf cards (Cards), as a script */
#define HIER_BROADLEAF "test/assets/mesh/broadleaf.flecs"

/** Half edge of the marker boxes at the vertices of the cards */
#define HIER_MARK (0.08f)

/** Flutter of the cases of the cards: world units at full wind */
#define HIER_FLUTTER (0.3f)

/** Which vertex of every card a marker box stands at */
typedef enum hier_mark_t {
    HierStems,  /**< The stem of the card, its vertex of the lowest flutter weight, 0 */
    HierTips,   /**< The tip of the card, its vertex of the highest flutter weight */
    HierPivots  /**< The pivot of the twig of the card, with the data of its stem */
} hier_mark_t;

/** Flutter weight of a vertex of foliage, from its data channel BOX_MESH_DATA_FLUTTER.
 *
 * @param packed The level plus 0.5 times the flutter weight
 * @return The flutter weight, 0..1
 */
static float hier_weight(
    float packed)
{
    return 2.0f * (packed - floorf(packed));
}

/** Append the marker box of one vertex of the cards to a mesh, with the data of the vertex.
 *
 * @param m The mesh
 * @param data The data channels of the cards
 * @param v The vertex whose data the box carries
 * @param at Center of the box
 * @param scramble Whether data1 is replaced by values that are no pivot or stiffness
 */
static void hier_mark_box(
    Mesh3 *m,
    float *const *data,
    int32_t v,
    const vec3 at,
    bool scramble)
{
    vec4 d0 = { data[0][v], data[1][v], data[2][v], data[3][v] };
    vec4 d1 = { data[4][v], data[5][v], data[6][v], data[7][v] };
    if (scramble) {
        glm_vec4_copy((vec4){ -3.7f, 5.2f, 0.4f, 40000.0f + (float)v }, d1);
    }

    hier_box(m, (float[3]){ at[0] - HIER_MARK, at[1] - HIER_MARK, at[2] - HIER_MARK },
        (float[3]){ at[0] + HIER_MARK, at[1] + HIER_MARK, at[2] + HIER_MARK }, d0, d1);
}

/** Add a red Mesh3 at the origin of marker boxes at a vertex of every leaf card of the
 * broadleaf, each box with the per vertex data Cards wrote for that vertex, so it sways as that
 * vertex of the card does. Checks that the cards grow on twigs of level 2, that every card has
 * a stem of flutter weight 0 and a tip of flutter weight 1.
 *
 * @param world The world
 * @param mark Which vertex of every card
 * @param scramble Whether data1 is replaced by values that are no pivot or stiffness
 * @return The entity
 */
static ecs_entity_t hier_cards(
    ecs_world_t *world,
    hier_mark_t mark,
    bool scramble)
{
    char *path = box_app_locate(HIER_BROADLEAF);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t plant = ecs_lookup(world, "broadleaf");
    test_assert(plant != 0);
    ecs_entity_t cards = ecs_lookup_child(world, plant, "cards");
    test_assert(cards != 0);
    char *error = NULL;
    box_mg_value_t *value = box_mesh_graph_evaluate(world, cards, &error);
    if (error) {
        printf("%s\n", error);
        ecs_os_free(error);
    }

    test_assert(value != NULL);
    const box_mg_mesh_t *cm = &value->mesh;
    const float *card = box_mg_attr_find(cm->attrs, cm->attr_count, "card");
    float *data[8];
    int32_t i, v;
    test_assert(card != NULL);
    for (i = 0; i < 8; i ++) {
        char name[BOX_MESH_ATTR_NAME];
        ecs_os_snprintf(name, ECS_SIZEOF(name), "%s%d", BOX_MESH_DATA_ATTR, i);
        data[i] = box_mg_attr_find(cm->attrs, cm->attr_count, name);
        test_assert(data[i] != NULL);
    }

    Mesh3 m = {0};
    int32_t first = 0, cards_count = 0, deep = 0;
    for (v = 1; v <= cm->vertex_count; v ++) {
        if (v < cm->vertex_count && card[v] == card[first]) {
            continue;
        }

        /* The vertices first..v-1 are one card */
        int32_t stem = first, tip = first;
        for (i = first; i < v; i ++) {
            float w = hier_weight(data[BOX_MESH_DATA_FLUTTER][i]);
            if (w < hier_weight(data[BOX_MESH_DATA_FLUTTER][stem])) {
                stem = i;
            }
            if (w > hier_weight(data[BOX_MESH_DATA_FLUTTER][tip])) {
                tip = i;
            }
        }

        test_assert(hier_weight(data[BOX_MESH_DATA_FLUTTER][stem]) < 1e-3f);
        test_assert(hier_weight(data[BOX_MESH_DATA_FLUTTER][tip]) > 0.95f);
        if (floorf(data[BOX_MESH_DATA_FLUTTER][stem]) >= 2.0f) {
            deep ++;
        }

        if (mark == HierStems) {
            hier_mark_box(&m, data, stem, cm->positions[stem], scramble);
        } else if (mark == HierTips) {
            hier_mark_box(&m, data, tip, cm->positions[tip], scramble);
        } else {
            vec3 pivot = { data[BOX_MESH_DATA_PIVOT][stem],
                data[BOX_MESH_DATA_PIVOT + 1][stem], data[BOX_MESH_DATA_PIVOT + 2][stem] };
            hier_mark_box(&m, data, stem, pivot, scramble);
        }

        cards_count ++;
        first = v;
    }

    test_assert(cards_count > 20);
    test_int(deep, cards_count);
    box_mg_value_release(value);
    ecs_delete(world, plant);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rgb, {200, 60, 40});
    ecs_set_ptr(world, e, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_vec_fini_t(NULL, &m.normals, vec3);
    ecs_vec_fini_t(NULL, &m.indices, uint32_t);
    ecs_vec_fini_t(NULL, &m.data0, vec4);
    ecs_vec_fini_t(NULL, &m.data1, vec4);

    return e;
}

/** Render the markers of the cards in a fresh world after frames of a wind, under the WindSway
 * of a crown with pivots, no bend and a flutter of HIER_FLUTTER.
 *
 * @param mark Which vertex of every card
 * @param scramble Whether data1 is replaced by values that are no pivot or stiffness
 * @param foliage Whether the WindSway reads the data as foliage
 * @param branch Radians the twigs turn about their pivots at full wind
 * @param speed Speed of the wind, 0 for a calm
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* hier_cards_render(
    hier_mark_t mark,
    bool scramble,
    bool foliage,
    float branch,
    float speed,
    const char *name)
{
    ecs_world_t *world = hier_world(true);
    hier_wind(world, speed);
    ecs_entity_t e = hier_cards(world, mark, scramble);
    ecs_set(world, e, WindSway, {.bend = 0, .height = 3, .base = 0,
        .flutter = HIER_FLUTTER, .flutter_frequency = 2, .flutter_reach = 0.3f,
        .pivots = true, .branch = branch, .branch_frequency = 0, .foliage = foliage});
    hier_frames(world, 30);
    unsigned char *px = hier_render(world, name);
    ecs_fini(world);

    return px;
}

/** Number of the red pixels of a capture.
 *
 * @param px The pixels
 * @return The count
 */
static int hier_red_count(
    const unsigned char *px)
{
    int x, y, count = 0;
    for (y = 0; y < HIER_SIZE; y ++) {
        for (x = 0; x < HIER_SIZE; x ++) {
            count += hier_red(px, x, y);
        }
    }

    return count;
}

/** Number of the pixels red in one capture and not in the other.
 *
 * @param a A capture
 * @param b The other capture
 * @return The count
 */
static int hier_red_diff(
    const unsigned char *a,
    const unsigned char *b)
{
    int x, y, count = 0;
    for (y = 0; y < HIER_SIZE; y ++) {
        for (x = 0; x < HIER_SIZE; x ++) {
            count += hier_red(a, x, y) != hier_red(b, x, y);
        }
    }

    return count;
}

/** The leaf cards of a Cards crown under a WindSway with pivots and foliage and twigs that do
 * not turn: the stems of the cards, of flutter weight 0, stay where they are, the tips flutter */
void WindHierarchy_card_stems_pinned_tips_flutter(void) {
    // materials/wind_sway
    unsigned char *stems_calm = hier_cards_render(HierStems, false, true, 0, 0,
        "hier_card_stems_calm.png");
    unsigned char *stems_windy = hier_cards_render(HierStems, false, true, 0, 10,
        "hier_card_stems_windy.png");
    int stems = hier_red_count(stems_calm);
    test_assert(stems > 50);
    test_assert(hier_red_diff(stems_calm, stems_windy) * 50 <= stems);

    unsigned char *tips_calm = hier_cards_render(HierTips, false, true, 0, 0,
        "hier_card_tips_calm.png");
    unsigned char *tips_windy = hier_cards_render(HierTips, false, true, 0, 10,
        "hier_card_tips_windy.png");
    int tips = hier_red_count(tips_calm);
    test_assert(tips > 50);
    test_assert(hier_red_diff(tips_calm, tips_windy) * 5 > tips);

    stbi_image_free(stems_calm);
    stbi_image_free(stems_windy);
    stbi_image_free(tips_calm);
    stbi_image_free(tips_windy);
}

/** With turning twigs every card of a Cards crown under a WindSway with pivots and foliage
 * turns about the pivot of its twig only: the pivots stay, the stems move with the twig, and
 * the seed, hue, growth and occlusion of the cards in data1 are not read as the pivot or the
 * stiffness of a parent, which a WindSway without foliage does read on the twigs of level 2 */
void WindHierarchy_card_stems_turn_with_twig(void) {
    // materials/wind_sway
    unsigned char *pivots_calm = hier_cards_render(HierPivots, false, true, HIER_BRANCH, 0,
        "hier_card_pivots_calm.png");
    unsigned char *pivots_windy = hier_cards_render(HierPivots, false, true, HIER_BRANCH, 10,
        "hier_card_pivots_windy.png");
    float row_rest = hier_mean_row(pivots_calm, 0, HIER_SIZE - 1);
    float row_moved = hier_mean_row(pivots_windy, 0, HIER_SIZE - 1);
    float col_rest = hier_mean_column(pivots_calm, 0, HIER_SIZE - 1, 0, HIER_SIZE - 1);
    float col_moved = hier_mean_column(pivots_windy, 0, HIER_SIZE - 1, 0, HIER_SIZE - 1);
    test_assert(row_rest > 0.0f && row_moved > 0.0f);
    test_assert(fabsf(row_moved - row_rest) < 0.3f);
    test_assert(fabsf(col_moved - col_rest) < 0.3f);

    unsigned char *stems_calm = hier_cards_render(HierStems, false, true, HIER_BRANCH, 0,
        "hier_card_turn_calm.png");
    unsigned char *stems_windy = hier_cards_render(HierStems, false, true, HIER_BRANCH, 10,
        "hier_card_turn_windy.png");
    int stems = hier_red_count(stems_calm);
    test_assert(stems > 50);
    test_assert(hier_red_diff(stems_calm, stems_windy) * 10 > stems);

    /* Other values in data1 change nothing with foliage */
    unsigned char *scrambled = hier_cards_render(HierStems, true, true, HIER_BRANCH, 10,
        "hier_card_turn_scrambled.png");
    test_assert(!memcmp(stems_windy, scrambled, HIER_SIZE * HIER_SIZE * 4));

    /* Without foliage data1 is read as the parent of the twigs of level 2 */
    unsigned char *parent = hier_cards_render(HierStems, false, false, HIER_BRANCH, 10,
        "hier_card_parent.png");
    unsigned char *parent_scrambled = hier_cards_render(HierStems, true, false, HIER_BRANCH,
        10, "hier_card_parent_scrambled.png");
    test_assert(memcmp(parent, parent_scrambled, HIER_SIZE * HIER_SIZE * 4) != 0);

    stbi_image_free(pivots_calm);
    stbi_image_free(pivots_windy);
    stbi_image_free(stems_calm);
    stbi_image_free(stems_windy);
    stbi_image_free(scrambled);
    stbi_image_free(parent);
    stbi_image_free(parent_scrambled);
}
