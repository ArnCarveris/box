/**
 * @file WaterRipples.c
 * @brief Tests for the fields of WaterRipples on the GPU, checked on the heights and the foam read
 * back from the field (box_render_gpu_read_ripples, box_render_gpu_read_ripple_foam).
 *
 * The cases look down on a pond, a 16x16 quad at y = 0 with a Water and a field of 16 world
 * units and 128 cells (an eighth of a unit each) that runs frames of 1/60 s, two steps each.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Seconds of a frame */
#define RIPPLES_DT (1.0f / 60)

/** Cells per side of the fields */
#define RIPPLES_RES (128)

/** Create a headless world that renders on the main thread, with Scene.water, a sun and a camera
 * above the origin looking down, drawn into a small offscreen surface.
 *
 * @return The world
 */
static ecs_world_t* ripples_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.reflections = true, .environment = true,
        .transmission = true, .fresnel = true, .water = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {GLM_PI_4f, GLM_PI_2f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64, .resolution_scale = 1});

    return world;
}

/** Add the pond: a 16x16 quad at y = 0 facing up with a Water and a field of 16 world units and
 * RIPPLES_RES cells.
 *
 * @param world The world
 * @param wave_speed WaterRipples.wave_speed
 * @param damping WaterRipples.damping
 * @return The pond
 */
static ecs_entity_t ripples_pond(
    ecs_world_t *world,
    float wave_speed,
    float damping)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, e, Quad, {16, 16});
    ecs_set(world, e, Rgb, {200, 200, 200});
    ecs_add(world, e, Water);
    ecs_set(world, e, WaterRipples, {.size = 16, .resolution = RIPPLES_RES,
        .wave_speed = wave_speed, .damping = damping, .strength = 1});

    return e;
}

/** Run frames of RIPPLES_DT without new device errors.
 *
 * @param world The world
 * @param count Frames to run
 */
static void ripples_frames(
    ecs_world_t *world,
    int32_t count)
{
    box_render_gpu_stats_t before, after;
    bool started = box_render_gpu_stats(world, &before);
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, RIPPLES_DT);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &after), true);
    if (started) {
        test_int(after.errors, before.errors);
    } else {
        test_int(after.errors, 0);
    }
}

/** A field read back from the GPU */
typedef struct ripples_field_t {
    box_render_ripples_read_t info;     /**< Its window */
    float heights[RIPPLES_RES * RIPPLES_RES]; /**< Its heights, in the order of the window */
} ripples_field_t;

/** Read the field of a pond.
 *
 * @param world The world
 * @param pond The pond
 * @return The field, freed by the caller
 */
static ripples_field_t* ripples_read(
    ecs_world_t *world,
    ecs_entity_t pond)
{
    ripples_field_t *result = ecs_os_calloc_t(ripples_field_t);
    test_bool(box_render_gpu_read_ripples(world, pond, &result->info, result->heights,
        RIPPLES_RES * RIPPLES_RES), true);
    test_int(result->info.resolution, RIPPLES_RES);
    test_flt(result->info.cell, 16.0f / RIPPLES_RES);

    return result;
}

/** Read the foam of the field of a pond, in the heights of the result.
 *
 * @param world The world
 * @param pond The pond
 * @return The field, freed by the caller
 */
static ripples_field_t* ripples_read_foam(
    ecs_world_t *world,
    ecs_entity_t pond)
{
    ripples_field_t *result = ecs_os_calloc_t(ripples_field_t);
    test_bool(box_render_gpu_read_ripple_foam(world, pond, &result->info, result->heights,
        RIPPLES_RES * RIPPLES_RES), true);
    test_int(result->info.resolution, RIPPLES_RES);

    return result;
}

/** Set the foam terms of the WaterRipples of a pond.
 *
 * @param world The world
 * @param pond The pond
 * @param decay WaterRipples.foam_decay
 * @param spread WaterRipples.foam_spread
 * @param crest WaterRipples.crest_foam
 */
static void ripples_foam_terms(
    ecs_world_t *world,
    ecs_entity_t pond,
    float decay,
    float spread,
    float crest)
{
    WaterRipples *r = ecs_ensure(world, pond, WaterRipples);
    r->foam_decay = decay;
    r->foam_spread = spread;
    r->crest_foam = crest;
    ecs_modified(world, pond, WaterRipples);
}

/** Get the sum of the values of a field, its total foam or height.
 *
 * @param f The field
 * @return The sum
 */
static double ripples_sum(
    const ripples_field_t *f)
{
    double result = 0;
    int32_t i;
    for (i = 0; i < RIPPLES_RES * RIPPLES_RES; i ++) {
        result += f->heights[i];
    }

    return result;
}

/** Get the height of a field at a world XZ point, the cell under it.
 *
 * @param f The field
 * @param x World X
 * @param z World Z
 * @return The height, 0 outside the window
 */
static float ripples_at(
    const ripples_field_t *f,
    float x,
    float z)
{
    int32_t cx = (int32_t)floorf((x - f->info.corner[0]) / f->info.cell);
    int32_t cz = (int32_t)floorf((z - f->info.corner[1]) / f->info.cell);
    float result = 0;
    if (cx >= 0 && cz >= 0 && cx < RIPPLES_RES && cz < RIPPLES_RES) {
        result = f->heights[cz * RIPPLES_RES + cx];
    }

    return result;
}

/** Get the largest absolute height of a field within a world XZ box.
 *
 * @param f The field
 * @param x0 Smallest world X
 * @param z0 Smallest world Z
 * @param x1 Largest world X
 * @param z1 Largest world Z
 * @return The largest absolute height
 */
static float ripples_max(
    const ripples_field_t *f,
    float x0,
    float z0,
    float x1,
    float z1)
{
    float result = 0, x, z, step = f->info.cell;
    for (z = z0; z <= z1; z += step) {
        for (x = x0; x <= x1; x += step) {
            result = fmaxf(result, fabsf(ripples_at(f, x, z)));
        }
    }

    return result;
}

/** Get the distance from a world XZ point of the crest of the ring around it along +X: the
 * largest absolute height of the row through it.
 *
 * @param f The field
 * @param x World X of the center
 * @param z World Z of the center
 * @return World units from the center to the crest
 */
static float ripples_crest(
    const ripples_field_t *f,
    float x,
    float z)
{
    float best = 0, result = 0, d;
    for (d = 0; d < 7.5f; d += f->info.cell) {
        float h = fabsf(ripples_at(f, x + d, z));
        if (h > best) {
            best = h;
            result = d;
        }
    }

    return result;
}

/** Get the centroid along X of the absolute heights of a field.
 *
 * @param f The field
 * @return World X of the centroid
 */
static float ripples_centroid_x(
    const ripples_field_t *f)
{
    double sum = 0, weight = 0;
    int32_t x, z;
    for (z = 0; z < RIPPLES_RES; z ++) {
        for (x = 0; x < RIPPLES_RES; x ++) {
            double h = fabs(f->heights[z * RIPPLES_RES + x]);
            sum += h * (f->info.corner[0] + (x + 0.5) * f->info.cell);
            weight += h;
        }
    }

    return weight > 0 ? (float)(sum / weight) : 0;
}

void WaterRipples_ring_spreads(void) {
    ecs_world_t *world = ripples_world();
    ecs_entity_t pond = ripples_pond(world, 2, 0);
    ripples_frames(world, 2);

    float at[3] = {0, 0, 0};
    test_bool(box_material_ripple_at(world, pond, at, 0.5f, 0.1f), true);
    ripples_frames(world, 30);
    ripples_field_t *f = ripples_read(world, pond);

    /* After half a second at 2 units per second the ring is about a unit out */
    float crest = ripples_crest(f, 0, 0);
    test_assert(crest > 0.6f && crest < 1.6f);
    test_assert(ripples_max(f, -7.5f, -7.5f, 7.5f, 7.5f) > 1e-3f);

    /* and as far along Z and the diagonals: a ring */
    test_assert(ripples_max(f, -0.25f, 0.5f, 0.25f, 1.75f) > 0.3f * ripples_max(
        f, 0.5f, -0.25f, 1.75f, 0.25f));

    ripples_frames(world, 30);
    ripples_field_t *g = ripples_read(world, pond);
    float later = ripples_crest(g, 0, 0);
    test_assert(later > crest + 0.6f);
    test_assert(later > 1.4f && later < 2.8f);

    ecs_os_free(f);
    ecs_os_free(g);
    ecs_fini(world);
}

void WaterRipples_ring_decays(void) {
    float peaks[2];
    float dampings[2] = {0, 0.9f};
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ripples_world();
        ecs_entity_t pond = ripples_pond(world, 2, dampings[i]);
        ripples_frames(world, 2);
        float at[3] = {0, 0, 0};
        test_bool(box_material_ripple_at(world, pond, at, 0.5f, 0.1f), true);
        ripples_frames(world, 60);
        ripples_field_t *f = ripples_read(world, pond);
        peaks[i] = ripples_max(f, -7, -7, 7, 7);
        ecs_os_free(f);
        ecs_fini(world);
    }

    /* 90% of the energy lost in a second leaves about a third of the amplitude */
    test_assert(peaks[0] > 1e-3f);
    test_assert(peaks[1] < 0.5f * peaks[0]);
    test_assert(peaks[1] > 0.1f * peaks[0]);
}

void WaterRipples_wake_behind_source(void) {
    ecs_world_t *world = ripples_world();
    ecs_entity_t pond = ripples_pond(world, 1, 0.2f);
    ecs_entity_t boat = ecs_new(world);
    ecs_set(world, boat, Position, {-5, 0, 0});
    ecs_set(world, boat, RippleSource, {.extents = {0.4f, 0.2f}, .strength = 1});

    /* 3 units per second, three times the wave speed, for 1.5 s: from -5 to -0.5 */
    float x = -5;
    int32_t i;
    for (i = 0; i < 90; i ++) {
        ripples_frames(world, 1);
        x += 3 * RIPPLES_DT;
        ecs_set(world, boat, Position, {x, 0, 0});
    }

    ripples_frames(world, 1);
    ripples_field_t *f = ripples_read(world, pond);
    float behind = ripples_max(f, -4, -1.5f, -1.5f, 1.5f);
    float ahead = ripples_max(f, x + 0.75f, -1.5f, x + 4, 1.5f);
    test_assert(behind > 1e-4f);
    test_assert(ahead < 0.02f * behind);

    /* The wake spreads to the sides behind the boat: a V */
    test_assert(ripples_max(f, -3.5f, 0.6f, -2.5f, 1.6f) > 0.05f * behind);

    ecs_os_free(f);
    ecs_fini(world);
}

void WaterRipples_ring_drifts_with_flow(void) {
    ecs_world_t *world = ripples_world();
    ecs_entity_t pond = ripples_pond(world, 1, 0);
    ecs_set(world, pond, WaterFlow, {.velocity = {1, 0}});
    ripples_frames(world, 2);

    float at[3] = {-2, 0, 0};
    test_bool(box_material_ripple_at(world, pond, at, 0.5f, 0.1f), true);
    ripples_frames(world, 1);
    ripples_field_t *f = ripples_read(world, pond);
    float start = ripples_centroid_x(f);
    test_assert(fabsf(start + 2) < 0.2f);

    /* A second later the ring is a unit downstream */
    ripples_frames(world, 60);
    ripples_field_t *g = ripples_read(world, pond);
    float end = ripples_centroid_x(g);
    test_assert(fabsf(end - start - 1) < 0.2f);

    /* The window stays on the pond */
    test_assert(g->info.corner[0] <= -8 + 1e-4f);
    test_assert(g->info.corner[0] > -8 - g->info.cell - 1e-4f);

    ecs_os_free(f);
    ecs_os_free(g);
    ecs_fini(world);
}

void WaterRipples_follow_keeps_ripples(void) {
    ripples_field_t *fields[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ripples_world();
        ecs_entity_t pond = ripples_pond(world, 2, 0.2f);
        ecs_entity_t boat = ecs_new(world);
        ecs_set(world, boat, Position, {0, 0, 0});
        WaterRipples *r = ecs_ensure(world, pond, WaterRipples);
        r->follow = boat;
        ecs_modified(world, pond, WaterRipples);
        ripples_frames(world, 2);
        float at[3] = {0.5f, 0, 0.25f};
        test_bool(box_material_ripple_at(world, pond, at, 0.5f, 0.1f), true);
        ripples_frames(world, 10);

        /* The second world moves its window by 1.3 units along X and 0.7 along Z */
        if (i) {
            ecs_set(world, boat, Position, {1.3f, 0, -0.7f});
        }

        ripples_frames(world, 3);
        fields[i] = ripples_read(world, pond);
        ecs_fini(world);
    }

    test_assert(fields[1]->info.corner[0] > fields[0]->info.corner[0] + 1);
    test_assert(fields[1]->info.corner[1] < fields[0]->info.corner[1] - 0.5f);

    /* The ripples near the center did not move or change in the world */
    float x, z, most = 0;
    for (z = -2; z <= 2; z += 0.125f) {
        for (x = -2; x <= 2; x += 0.125f) {
            float a = ripples_at(fields[0], x + 0.0625f, z + 0.0625f);
            float b = ripples_at(fields[1], x + 0.0625f, z + 0.0625f);
            test_assert(fabsf(a - b) <= 1e-6f + 1e-4f * fabsf(a));
            most = fmaxf(most, fabsf(a));
        }
    }

    test_assert(most > 1e-3f);
    ecs_os_free(fields[0]);
    ecs_os_free(fields[1]);
}

void WaterRipples_deterministic(void) {
    ripples_field_t *fields[2];
    int32_t i, j;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ripples_world();
        ecs_entity_t pond = ripples_pond(world, 1.5f, 0.3f);
        ecs_set(world, pond, WaterFlow, {.velocity = {0.3f, -0.2f}});
        ecs_entity_t boat = ecs_new(world);
        ecs_set(world, boat, Position, {-4, 0, 1});
        ecs_set(world, boat, Rotation, {0, 0.4f, 0});
        ecs_set(world, boat, RippleSource, {.extents = {0.6f, 0.25f}, .strength = 1});
        float at[3] = {2, 0, -1};
        test_bool(box_material_ripple_at(world, pond, at, 0.4f, 0.08f), true);
        for (j = 0; j < 45; j ++) {
            ripples_frames(world, 1);
            ecs_set(world, boat, Position, {-4 + 2.5f * RIPPLES_DT * (float)(j + 1), 0, 1});
        }

        fields[i] = ripples_read(world, pond);
        ecs_fini(world);
    }

    test_assert(ripples_max(fields[0], -7, -7, 7, 7) > 1e-4f);
    test_assert(memcmp(fields[0], fields[1], sizeof(ripples_field_t)) == 0);
    ecs_os_free(fields[0]);
    ecs_os_free(fields[1]);
}

void WaterRipples_no_field_without_component(void) {
    ecs_world_t *world = ripples_world();
    ecs_entity_t pond = ripples_pond(world, 2, 0.5f);
    ripples_frames(world, 2);
    test_bool(box_render_gpu_read_ripples(world, pond, NULL, NULL, 0), true);

    /* Removed, the field is freed */
    ecs_remove(world, pond, WaterRipples);
    ripples_frames(world, 2);
    test_bool(box_render_gpu_read_ripples(world, pond, NULL, NULL, 0), false);

    ecs_fini(world);
}

void WaterRipples_foam_trail_behind_source(void) {
    ecs_world_t *world = ripples_world();
    ecs_entity_t pond = ripples_pond(world, 1, 0.2f);
    ripples_foam_terms(world, pond, 0.2f, 0.01f, 0);
    ecs_entity_t boat = ecs_new(world);
    ecs_set(world, boat, Position, {-5, 0, 0});
    ecs_set(world, boat, RippleSource, {.extents = {0.4f, 0.2f}, .strength = 1, .foam = 1});

    /* 3 units per second for 1.5 s: from -5 to -0.5 */
    float x = -5;
    int32_t i;
    for (i = 0; i < 90; i ++) {
        ripples_frames(world, 1);
        x += 3 * RIPPLES_DT;
        ecs_set(world, boat, Position, {x, 0, 0});
    }

    ripples_frames(world, 1);
    ripples_field_t *f = ripples_read_foam(world, pond);

    /* Foam along the path behind it, none ahead of it and none far to the side */
    float trail = ripples_max(f, -4, -0.25f, -1.5f, 0.25f);
    test_assert(trail > 0.1f);
    test_assert(trail <= 1);
    test_assert(ripples_max(f, x + 0.75f, -1.5f, x + 4, 1.5f) < 1e-3f);
    test_assert(ripples_max(f, -4, 2, -1.5f, 3) < 1e-3f);

    /* The older end of the trail has faded more than the newer one */
    float old = ripples_max(f, -4.5f, -0.25f, -4, 0.25f);
    float young = ripples_max(f, -2, -0.25f, -1.5f, 0.25f);
    test_assert(old < young);

    /* Never negative */
    for (i = 0; i < RIPPLES_RES * RIPPLES_RES; i ++) {
        test_assert(f->heights[i] >= 0);
    }

    ecs_os_free(f);
    ecs_fini(world);
}

void WaterRipples_foam_fades(void) {
    ecs_world_t *world = ripples_world();
    ecs_entity_t pond = ripples_pond(world, 1, 0);
    ripples_foam_terms(world, pond, 0.5f, 0, 0);
    ripples_frames(world, 2);

    float at[3] = {0, 0, 0};
    test_bool(box_material_foam_at(world, pond, at, 1, 0.8f), true);
    ripples_frames(world, 1);
    ripples_field_t *f = ripples_read_foam(world, pond);
    float start = ripples_at(f, 0, 0);
    test_assert(start > 0.6f && start <= 0.8f + 1e-4f);

    /* A second later half of it is left */
    ripples_frames(world, 60);
    ripples_field_t *g = ripples_read_foam(world, pond);
    float end = ripples_at(g, 0, 0);
    test_assert(fabsf(end / start - 0.5f) < 0.05f);

    /* and after a few more seconds almost none */
    ripples_frames(world, 300);
    ripples_field_t *h = ripples_read_foam(world, pond);
    test_assert(ripples_max(h, -2, -2, 2, 2) < 0.05f);

    ecs_os_free(f);
    ecs_os_free(g);
    ecs_os_free(h);
    ecs_fini(world);
}

void WaterRipples_foam_drifts_with_flow(void) {
    ecs_world_t *world = ripples_world();
    ecs_entity_t pond = ripples_pond(world, 1, 0);
    ripples_foam_terms(world, pond, 0, 0, 0);
    ecs_set(world, pond, WaterFlow, {.velocity = {1, 0}});
    ripples_frames(world, 2);

    float at[3] = {-2, 0, 0};
    test_bool(box_material_foam_at(world, pond, at, 0.5f, 0.5f), true);
    ripples_frames(world, 1);
    ripples_field_t *f = ripples_read_foam(world, pond);
    float start = ripples_centroid_x(f);
    test_assert(fabsf(start + 2) < 0.2f);
    double total = ripples_sum(f);
    test_assert(total > 0);

    /* A second later it is a unit downstream, all of it */
    ripples_frames(world, 60);
    ripples_field_t *g = ripples_read_foam(world, pond);
    float end = ripples_centroid_x(g);
    test_assert(fabsf(end - start - 1) < 0.2f);
    test_assert(fabs(ripples_sum(g) - total) < 0.02 * total);

    ecs_os_free(f);
    ecs_os_free(g);
    ecs_fini(world);
}

void WaterRipples_foam_spreads(void) {
    float peaks[2], reach[2];
    float spreads[2] = {0, 0.05f};
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ripples_world();
        ecs_entity_t pond = ripples_pond(world, 1, 0);
        ripples_foam_terms(world, pond, 0, spreads[i], 0);
        ripples_frames(world, 2);
        float at[3] = {0, 0, 0};
        test_bool(box_material_foam_at(world, pond, at, 0.5f, 0.8f), true);
        ripples_frames(world, 120);
        ripples_field_t *f = ripples_read_foam(world, pond);
        peaks[i] = ripples_at(f, 0, 0);
        reach[i] = ripples_max(f, 0.75f, -0.1f, 1.0f, 0.1f);
        ecs_os_free(f);
        ecs_fini(world);
    }

    /* Without spread it stays inside its radius; with it the peak drops and it reaches out */
    test_assert(peaks[0] > 0.7f);
    test_assert(reach[0] < 1e-4f);
    test_assert(peaks[1] < peaks[0]);
    test_assert(reach[1] > 1e-3f);
}

void WaterRipples_crest_foam(void) {
    double totals[2];
    float crests[2] = {0, 20};
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ripples_world();
        ecs_entity_t pond = ripples_pond(world, 2, 0);
        ripples_foam_terms(world, pond, 0, 0, crests[i]);
        ripples_frames(world, 2);
        float at[3] = {0, 0, 0};
        test_bool(box_material_ripple_at(world, pond, at, 0.5f, 0.3f), true);
        ripples_frames(world, 30);
        ripples_field_t *f = ripples_read_foam(world, pond);
        totals[i] = ripples_sum(f);
        ecs_os_free(f);
        ecs_fini(world);
    }

    /* A steep ring foams only with crest_foam */
    test_assert(totals[0] == 0);
    test_assert(totals[1] > 0);
}

void WaterRipples_no_foam_without_foam(void) {
    ecs_world_t *world = ripples_world();
    ecs_entity_t pond = ripples_pond(world, 1, 0.2f);
    ecs_entity_t boat = ecs_new(world);
    ecs_set(world, boat, Position, {-5, 0, 0});
    ecs_set(world, boat, RippleSource, {.extents = {0.4f, 0.2f}, .strength = 1});

    float x = -5, at[3] = {2, 0, 2};
    int32_t i;
    test_bool(box_material_ripple_at(world, pond, at, 0.5f, 0.1f), true);
    for (i = 0; i < 60; i ++) {
        ripples_frames(world, 1);
        x += 3 * RIPPLES_DT;
        ecs_set(world, boat, Position, {x, 0, 0});
    }

    /* The wake and the ring move the water but lay no foam */
    ripples_field_t *h = ripples_read(world, pond);
    test_assert(ripples_max(h, -7.5f, -7.5f, 7.5f, 7.5f) > 1e-4f);
    ripples_field_t *f = ripples_read_foam(world, pond);
    test_assert(ripples_max(f, -8, -8, 8, 8) == 0);

    ecs_os_free(h);
    ecs_os_free(f);
    ecs_fini(world);
}
