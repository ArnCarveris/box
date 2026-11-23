/**
 * @file FogVolume.c
 * @brief Tests for the FogVolume component and the fog volume records the light extract gives
 * the views: defaults, resolved records, order, the limit, culling and the views without any,
 * the fog distance of the views and the size and slices of their froxel grids.
 *
 * The camera of the views stands at (0, 0, 10) and looks along -Z with a 60 degree field of
 * view; the worlds render into a null backend.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

/** Warnings logged while the counting log hook is set */
static int32_t fog_warnings;

/** Log hook that counts the warnings.
 *
 * @param level Level of the message
 * @param file File that logged it
 * @param line Line that logged it
 * @param msg The message
 */
static void fog_count_warnings(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    (void)msg;
    if (level == -2) {
        fog_warnings ++;
    }
}

/** Create a world that renders into a null backend.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);

    return world;
}

/** Create a view with the defaults of the View constructor and its camera.
 *
 * @param world The world
 * @param parent Parent of the camera, its scene, 0 for the default scene
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_entity_t camera = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    ecs_modified(world, view, View);

    return view;
}

/** Create a fog volume with the defaults at a position.
 *
 * @param world The world
 * @param parent Parent of the volume, 0 for none
 * @param x World x
 * @param y World y
 * @param z World z
 * @return The volume
 */
static ecs_entity_t volume_new(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x,
    float y,
    float z)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {x, y, z});
    ecs_add(world, e, FogVolume);

    return e;
}

/** Run a frame and wait until the backend rendered it.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Find the view of the last packet that renders a view entity.
 *
 * @param null The backend
 * @param view The view entity
 * @return The view of the packet
 */
static const box_render_view_t* packet_view(
    box_render_null_t *null,
    ecs_entity_t view)
{
    const box_render_view_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.views); i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&null->last.views, box_render_view_t, i);
        if (v->view == view) {
            result = v;
        }
    }

    test_assert(result != NULL);

    return result;
}

/** Get a fog volume record of a view of the last packet.
 *
 * @param null The backend
 * @param v The view of the packet
 * @param index Index of the record among those of the view
 * @return The record
 */
static const box_render_fog_volume_t* record_of(
    box_render_null_t *null,
    const box_render_view_t *v,
    int32_t index)
{
    test_assert(index < v->fog_count);
    test_assert(v->fog_first + index < ecs_vec_count(&null->last.fog_volumes));

    return ecs_vec_get_t(&null->last.fog_volumes, box_render_fog_volume_t,
        v->fog_first + index);
}

void FogVolume_defaults(void) {
    // fog_volume/component
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "fog",
        "using box.light\n"
        "fog { FogVolume: {} }\n", NULL) == 0);
    const FogVolume *f = ecs_get(world, ecs_lookup(world, "fog"), FogVolume);
    test_assert(f != NULL);
    test_flt(f->extent[0], 5);
    test_flt(f->extent[1], 5);
    test_flt(f->extent[2], 5);
    test_flt(f->density, 0.1f);
    test_int(f->color.r, 255);
    test_int(f->color.g, 255);
    test_int(f->color.b, 255);
    test_flt(f->absorption, 0);
    test_flt(f->asymmetry, 0.3f);
    test_flt(f->edge_fade, 1);
    test_flt(f->sun_inscatter, 1);
    test_flt(f->ambient_inscatter, 1);
    test_flt(f->light_inscatter, 1);
    test_flt(f->noise_strength, 0);
    test_flt(f->noise_scale, 4);
    test_flt(f->wind[0], 0);
    test_flt(f->wind[1], 0);
    test_flt(f->wind[2], 0);
    test_int(f->shape, FogVolumeBox);
    test_flt(f->height_falloff, 0);
    test_flt(f->edge_noise, 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, FogVolume);
    ecs_set(world, added, Position, {0, 0, 0});
    f = ecs_get(world, added, FogVolume);
    test_flt(f->density, 0.1f);
    test_flt(f->light_inscatter, 1);
    test_flt(f->noise_strength, 0);
    test_flt(f->noise_scale, 4);
    test_flt(f->wind[0], 0);
    test_int(f->shape, FogVolumeBox);
    test_flt(f->height_falloff, 0);
    test_flt(f->edge_noise, 0);

    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    test_bool(ecs_get(world, view, View)->lighting.fog_volumes, true);
    test_flt(ecs_get(world, view, View)->lighting.fog_distance, 100);
    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, View, {.lighting.enabled = true});
    test_bool(ecs_get(world, literal, View)->lighting.fog_volumes, false);
    test_flt(ecs_get(world, literal, View)->lighting.fog_distance, 0);

    ecs_fini(world);
}

void FogVolume_partial_script_keeps_others(void) {
    // fog_volume/component
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "fog",
        "using box.light\n"
        "fog { FogVolume: {density: 0.5, color: {255, 0, 0}} }\n", NULL) == 0);
    const FogVolume *f = ecs_get(world, ecs_lookup(world, "fog"), FogVolume);
    test_flt(f->density, 0.5f);
    test_int(f->color.r, 255);
    test_int(f->color.g, 0);
    test_int(f->color.b, 0);
    test_flt(f->extent[0], 5);
    test_flt(f->asymmetry, 0.3f);
    test_flt(f->edge_fade, 1);
    test_flt(f->sun_inscatter, 1);
    test_flt(f->ambient_inscatter, 1);
    test_flt(f->light_inscatter, 1);

    ecs_fini(world);
}

void FogVolume_light_inscatter_script_keeps_others(void) {
    // fog_volume/component
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "fog",
        "using box.light\n"
        "fog { FogVolume: {light_inscatter: 0} }\n", NULL) == 0);
    const FogVolume *f = ecs_get(world, ecs_lookup(world, "fog"), FogVolume);
    test_flt(f->light_inscatter, 0);
    test_flt(f->extent[0], 5);
    test_flt(f->density, 0.1f);
    test_int(f->color.r, 255);
    test_flt(f->asymmetry, 0.3f);
    test_flt(f->edge_fade, 1);
    test_flt(f->sun_inscatter, 1);
    test_flt(f->ambient_inscatter, 1);
    test_flt(f->noise_strength, 0);
    test_flt(f->noise_scale, 4);

    ecs_fini(world);
}

void FogVolume_partial_noise_keeps_others(void) {
    // fog_volume/noise
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "fog",
        "using box.light\n"
        "fog { FogVolume: {noise_strength: 0.6, wind: {1, 0, -2}} }\n", NULL) == 0);
    const FogVolume *f = ecs_get(world, ecs_lookup(world, "fog"), FogVolume);
    test_flt(f->noise_strength, 0.6f);
    test_flt(f->wind[0], 1);
    test_flt(f->wind[1], 0);
    test_flt(f->wind[2], -2);
    test_flt(f->noise_scale, 4);
    test_flt(f->density, 0.1f);
    test_flt(f->extent[0], 5);
    test_flt(f->edge_fade, 1);

    test_assert(ecs_script_run(world, "fog2",
        "using box.light\n"
        "fog2 { FogVolume: {noise_scale: 9} }\n", NULL) == 0);
    f = ecs_get(world, ecs_lookup(world, "fog2"), FogVolume);
    test_flt(f->noise_scale, 9);
    test_flt(f->noise_strength, 0);
    test_flt(f->wind[0], 0);
    test_flt(f->wind[2], 0);
    test_flt(f->density, 0.1f);

    ecs_fini(world);
}

void FogVolume_noise_record(void) {
    // fog_volume/noise
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t plain = volume_new(world, 0, 0, 0, -5);
    ecs_entity_t noisy = volume_new(world, 0, 0, 0, -20);
    ecs_entity_t low = volume_new(world, 0, 0, 0, -35);
    ecs_entity_t high = volume_new(world, 0, 0, 0, -50);
    (void)plain;
    FogVolume *f = ecs_get_mut(world, noisy, FogVolume);
    f->noise_strength = 0.5f;
    f->noise_scale = 7;
    f->wind[0] = 1;
    f->wind[1] = -2;
    f->wind[2] = 3;
    ecs_modified(world, noisy, FogVolume);
    f = ecs_get_mut(world, low, FogVolume);
    f->noise_strength = -1;
    f->noise_scale = -3;
    ecs_modified(world, low, FogVolume);
    f = ecs_get_mut(world, high, FogVolume);
    f->noise_strength = 4;
    f->noise_scale = 0;
    ecs_modified(world, high, FogVolume);
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->fog_count, 4);
    const box_render_fog_volume_t *r_high = record_of(null, v, 0);
    const box_render_fog_volume_t *r_low = record_of(null, v, 1);
    const box_render_fog_volume_t *r_noisy = record_of(null, v, 2);
    const box_render_fog_volume_t *r_plain = record_of(null, v, 3);
    test_flt(r_plain->center[2], -5);
    test_flt(r_plain->noise_strength, 0);
    test_flt(r_plain->noise_scale, 4);
    test_flt(r_plain->offset[0], 0);
    test_flt(r_plain->offset[1], 0);
    test_flt(r_plain->offset[2], 0);

    test_flt(r_noisy->center[2], -20);
    test_flt(r_noisy->noise_strength, 0.5f);
    test_flt(r_noisy->noise_scale, 7);
    float expected[3];
    box_render_fog_offset(&(FogVolume){.noise_scale = 7, .wind = {1, -2, 3}},
        null->last.time, expected);
    int32_t k;
    for (k = 0; k < 3; k ++) {
        test_assert(fabsf(r_noisy->offset[k] - expected[k]) < 1e-4f);
        test_assert(r_noisy->offset[k] >= 0 && r_noisy->offset[k] < 70);
    }

    test_flt(r_noisy->density, 0.1f);

    test_flt(r_low->center[2], -35);
    test_flt(r_low->noise_strength, 0);
    test_flt(r_low->noise_scale, BOX_RENDER_FOG_NOISE_SCALE_MIN);
    test_flt(r_high->center[2], -50);
    test_flt(r_high->noise_strength, 1);
    test_flt(r_high->noise_scale, BOX_RENDER_FOG_NOISE_SCALE_MIN);

    ecs_fini(world);
}

/** The noise texture is the same on every call, fills 0 to 255 around a median of 128 and tiles:
 * the step across an edge is no larger than the steps inside. */
void FogVolume_noise_texture(void) {
    // fog_volume/noise
    const int32_t n = BOX_RENDER_FOG_NOISE_SIZE;
    int32_t count = n * n * n, i, x, y, z, lo = 255, hi = 0, wrap = 0, inner = 0, below = 0;
    ecs_os_set_api_defaults();
    uint8_t *a = ecs_os_malloc(count), *b = ecs_os_malloc(count);
    box_render_fog_noise(a);
    box_render_fog_noise(b);
    test_assert(!memcmp(a, b, (size_t)count));
    double sum = 0;
    for (i = 0; i < count; i ++) {
        lo = a[i] < lo ? a[i] : lo;
        hi = a[i] > hi ? a[i] : hi;
        sum += a[i];
        below += a[i] < 128;
    }

    test_int(lo, 0);
    test_int(hi, 255);
    test_assert(sum / count > 96 && sum / count < 160);
    test_assert(below > count * 0.45 && below < count * 0.55);

    for (z = 0; z < n; z ++) {
        for (y = 0; y < n; y ++) {
            const uint8_t *row = &a[(z * n + y) * n];
            int32_t d = abs((int32_t)row[0] - (int32_t)row[n - 1]);
            wrap = d > wrap ? d : wrap;
            for (x = 1; x < n; x ++) {
                d = abs((int32_t)row[x] - (int32_t)row[x - 1]);
                inner = d > inner ? d : inner;
            }

            d = abs((int32_t)a[(z * n) * n + y] - (int32_t)a[(z * n + n - 1) * n + y]);
            wrap = d > wrap ? d : wrap;
            d = abs((int32_t)a[y * n + z] - (int32_t)a[((n - 1) * n + y) * n + z]);
            wrap = d > wrap ? d : wrap;
        }
    }

    test_assert(inner > 0);
    test_assert(wrap <= inner);

    ecs_os_free(a);
    ecs_os_free(b);
}

void FogVolume_rotated_record(void) {
    // fog_volume/extract
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t e = volume_new(world, 0, 1, 2, -3);
    ecs_set(world, e, Rotation, {0, (float)GLM_PI_4, 0});
    ecs_set(world, e, Scale, {2, 3, 4});
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->extent[0] = 1;
    f->extent[1] = 2;
    f->extent[2] = 3;
    f->color = (rgb){255, 0, 128};
    f->light_inscatter = 2.5f;
    ecs_modified(world, e, FogVolume);
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->fog_count, 1);
    const box_render_fog_volume_t *r = record_of(null, v, 0);
    test_flt(r->center[0], 1);
    test_flt(r->center[1], 2);
    test_flt(r->center[2], -3);
    test_flt(r->extent[0], 1);
    test_flt(r->extent[1], 2);
    test_flt(r->extent[2], 3);

    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    int32_t k, c;
    for (k = 0; k < 3; k ++) {
        vec3 axis;
        glm_vec3_normalize_to(ECS_CONST_CAST(float*, wt->m[k]), axis);
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(r->axes[k][c] - axis[c]) < 1e-5f);
        }

        test_assert(fabsf(glm_vec3_norm(ECS_CONST_CAST(float*, r->axes[k])) - 1.0f) < 1e-5f);
    }

    test_assert(fabsf(r->axes[1][1] - 1.0f) < 1e-5f);
    test_assert(fabsf(fabsf(r->axes[0][0]) - sqrtf(0.5f)) < 1e-5f);
    test_assert(fabsf(fabsf(r->axes[0][2]) - sqrtf(0.5f)) < 1e-5f);
    test_assert(fabsf(glm_vec3_dot(ECS_CONST_CAST(float*, r->axes[0]),
        ECS_CONST_CAST(float*, r->axes[2]))) < 1e-5f);
    test_flt(r->color[0], 1);
    test_flt(r->color[1], 0);
    test_assert(fabsf(r->color[2] - 0.2158605f) < 1e-4f);
    test_flt(r->density, 0.1f);
    test_flt(r->asymmetry, 0.3f);
    test_flt(r->light_inscatter, 2.5f);

    ecs_fini(world);
}

void FogVolume_clamped_values(void) {
    // fog_volume/extract
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t low = volume_new(world, 0, 0, 0, -5);
    ecs_entity_t high = volume_new(world, 0, 0, 0, -20);
    ecs_set(world, low, FogVolume, {
        .extent = {-1, 2, 2}, .density = -1, .absorption = -0.5f, .asymmetry = -5,
        .edge_fade = -1, .sun_inscatter = -1, .ambient_inscatter = -2, .light_inscatter = -3});
    ecs_set(world, high, FogVolume, {
        .extent = {2, 2, 2}, .density = 3, .absorption = 2, .asymmetry = 5,
        .edge_fade = 4, .sun_inscatter = 2, .ambient_inscatter = 3, .light_inscatter = 4});
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->fog_count, 2);
    const box_render_fog_volume_t *far_ = record_of(null, v, 0);
    const box_render_fog_volume_t *near_ = record_of(null, v, 1);
    test_flt(near_->center[2], -5);
    test_flt(near_->extent[0], 0);
    test_flt(near_->density, 0);
    test_flt(near_->absorption, 0);
    test_flt(near_->asymmetry, -0.9f);
    test_flt(near_->edge_fade, 0);
    test_flt(near_->sun_inscatter, 0);
    test_flt(near_->ambient_inscatter, 0);
    test_flt(near_->light_inscatter, 0);

    test_flt(far_->center[2], -20);
    test_flt(far_->density, 3);
    test_flt(far_->absorption, 1);
    test_flt(far_->asymmetry, 0.9f);
    test_flt(far_->edge_fade, 4);
    test_flt(far_->sun_inscatter, 2);
    test_flt(far_->ambient_inscatter, 3);
    test_flt(far_->light_inscatter, 4);

    ecs_fini(world);
}

void FogVolume_back_to_front(void) {
    // fog_volume/order
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    volume_new(world, 0, 0, 0, -10);
    volume_new(world, 0, 1, 0, -40);
    volume_new(world, 0, -1, 0, 0);
    volume_new(world, 0, 0, 1, -25);
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->fog_count, 4);
    test_flt(record_of(null, v, 0)->center[2], -40);
    test_flt(record_of(null, v, 1)->center[2], -25);
    test_flt(record_of(null, v, 2)->center[2], -10);
    test_flt(record_of(null, v, 3)->center[2], 0);

    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.fog_volumes, 4);
    test_int(stats.fog_dropped, 0);

    ecs_fini(world);
}

void FogVolume_limit_keeps_nearest(void) {
    // fog_volume/order
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    int32_t i, count = BOX_RENDER_FOG_VOLUMES_MAX + 8;
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = volume_new(world, 0, 0, 0, -1.0f - 2.0f * (float)i);
        ecs_set(world, e, FogVolume, {.extent = {0.5f, 0.5f, 0.5f}, .density = 0.1f});
    }

    ecs_os_api_log_t log = ecs_os_api.log_;
    fog_warnings = 0;
    ecs_os_api.log_ = fog_count_warnings;
    frame(world);
    frame(world);
    frame(world);
    ecs_os_api.log_ = log;
    test_int(fog_warnings, 1);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->fog_count, BOX_RENDER_FOG_VOLUMES_MAX);
    test_flt(record_of(null, v, 0)->center[2],
        -1.0f - 2.0f * (float)(BOX_RENDER_FOG_VOLUMES_MAX - 1));
    test_flt(record_of(null, v, BOX_RENDER_FOG_VOLUMES_MAX - 1)->center[2], -1);

    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.fog_volumes, BOX_RENDER_FOG_VOLUMES_MAX);
    test_int(stats.fog_dropped, 8);

    ecs_fini(world);
}

void FogVolume_frustum_culled(void) {
    // fog_volume/cull
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    volume_new(world, 0, 0, 0, -10);
    volume_new(world, 0, 0, 0, 40);
    volume_new(world, 0, 500, 0, -10);
    volume_new(world, 0, 0, -500, -10);
    ecs_entity_t inside = volume_new(world, 0, 0, 0, 12);
    ecs_set(world, inside, FogVolume, {.extent = {5, 5, 5}, .density = 0.1f});
    volume_new(world, 0, 0, 0, 16);
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->fog_count, 2);
    test_flt(record_of(null, v, 0)->center[2], -10);
    test_flt(record_of(null, v, 1)->center[2], 12);

    ecs_fini(world);
}

void FogVolume_unlit_view_none(void) {
    // fog_volume/views
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    View *v = ecs_get_mut(world, view, View);
    v->lighting.enabled = false;
    ecs_modified(world, view, View);
    volume_new(world, 0, 0, 0, -10);
    frame(world);

    test_int(packet_view(null, view)->fog_count, 0);
    test_int(ecs_vec_count(&null->last.fog_volumes), 0);

    ecs_fini(world);
}

void FogVolume_view_disabled_none(void) {
    // fog_volume/views
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t other = view_new(world, 0);
    View *v = ecs_get_mut(world, view, View);
    v->lighting.fog_volumes = false;
    ecs_modified(world, view, View);
    volume_new(world, 0, 0, 0, -10);
    frame(world);

    test_int(packet_view(null, view)->fog_count, 0);
    test_int(packet_view(null, other)->fog_count, 1);
    test_int(ecs_vec_count(&null->last.fog_volumes), 1);

    ecs_fini(world);
}

void FogVolume_other_scene_none(void) {
    // fog_volume/views
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t scene_view = view_new(world, scene);
    volume_new(world, scene, 0, 0, -10);
    frame(world);

    test_int(packet_view(null, view)->fog_count, 0);
    const box_render_view_t *v = packet_view(null, scene_view);
    test_int(v->fog_count, 1);
    test_flt(record_of(null, v, 0)->center[2], -10);

    volume_new(world, 0, 0, 1, -20);
    frame(world);
    v = packet_view(null, view);
    test_int(v->fog_count, 1);
    test_flt(record_of(null, v, 0)->center[2], -20);
    v = packet_view(null, scene_view);
    test_int(v->fog_count, 1);
    test_flt(record_of(null, v, 0)->center[2], -10);

    ecs_fini(world);
}

void FogVolume_moved_and_removed(void) {
    // fog_volume/extract
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t e = volume_new(world, 0, 0, 0, -10);
    frame(world);
    test_flt(record_of(null, packet_view(null, view), 0)->center[2], -10);

    ecs_set(world, e, Position, {2, 0, -15});
    frame(world);
    const box_render_fog_volume_t *r = record_of(null, packet_view(null, view), 0);
    test_flt(r->center[0], 2);
    test_flt(r->center[2], -15);

    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->density = 0.25f;
    ecs_modified(world, e, FogVolume);
    frame(world);
    test_flt(record_of(null, packet_view(null, view), 0)->density, 0.25f);

    ecs_remove(world, e, FogVolume);
    frame(world);
    test_int(packet_view(null, view)->fog_count, 0);
    test_int(ecs_vec_count(&null->last.fog_volumes), 0);

    ecs_add(world, e, FogVolume);
    frame(world);
    test_int(packet_view(null, view)->fog_count, 1);
    ecs_delete(world, e);
    frame(world);
    test_int(packet_view(null, view)->fog_count, 0);

    ecs_fini(world);
}

/** The noise offset of a FogVolume at a large time equals the offset at that time modulo the
 * period of each axis, 10 noise_scale over the speed of the wind along it. */
void FogVolume_offset_wraps(void) {
    // fog_volume/noise
    const FogVolume fog = {.noise_scale = 3, .wind = {2.5f, -0.75f, 7}};
    const double times[3] = { 1e6, 86400.0 * 3 + 0.123, 123456.789 };
    int32_t i, k;
    for (i = 0; i < 3; i ++) {
        float big[3];
        box_render_fog_offset(&fog, times[i], big);
        for (k = 0; k < 3; k ++) {
            double period = 30.0 / fabs((double)fog.wind[k]);
            float small[3];
            box_render_fog_offset(&fog, fmod(times[i], period), small);
            float d = fabsf(big[k] - small[k]);
            // Both may sit at either end of [0, 30), which is the same noise
            d = d > 15.0f ? 30.0f - d : d;
            test_assert(d < 1e-3f);
            test_assert(big[k] >= 0 && big[k] < 30);
        }
    }

    float at[3], at_next[3];
    box_render_fog_offset(&fog, 1e6, at);
    box_render_fog_offset(&fog, 1e6 + 1.0 / 60, at_next);
    test_assert(fabsf(at_next[0] - at[0] - 2.5f / 60) < 1e-4f);
}

/** A negative wind gives an offset in [0, 10 noise_scale) at every time, also at negative
 * products near the end of the period. */
void FogVolume_offset_negative_wind(void) {
    // fog_volume/noise
    const FogVolume fog = {.noise_scale = 0.5f, .wind = {-1.3f, -4, -1e-7f}};
    const double times[6] = { 0.001, 1, 3.7, 3600, 1e6, 1e9 };
    int32_t i, k;
    for (i = 0; i < 6; i ++) {
        float out[3];
        box_render_fog_offset(&fog, times[i], out);
        for (k = 0; k < 3; k ++) {
            test_assert(out[k] >= 0 && out[k] < 5);
        }
    }

    float out[3];
    box_render_fog_offset(&fog, 1, out);
    test_assert(fabsf(out[0] - 3.7f) < 1e-4f);
    test_assert(fabsf(out[1] - 1.0f) < 1e-4f);
}

/** A volume without wind has a noise offset of 0 at every time, also through the extract. */
void FogVolume_offset_zero_wind(void) {
    // fog_volume/noise
    const FogVolume fog = {.noise_scale = 4};
    const double times[4] = { 0, 1, 1e6, 1e9 };
    int32_t i, k;
    for (i = 0; i < 4; i ++) {
        float out[3] = {1, 1, 1};
        box_render_fog_offset(&fog, times[i], out);
        for (k = 0; k < 3; k ++) {
            test_flt(out[k], 0);
        }
    }
}

/** The extract writes the offset at the time of the packet into the record. */
void FogVolume_offset_record(void) {
    // fog_volume/noise
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t e = volume_new(world, 0, 0, 0, -5);
    FogVolume *f = ecs_get_mut(world, e, FogVolume);
    f->noise_strength = 1;
    f->noise_scale = 2;
    f->wind[0] = 0.3f;
    f->wind[1] = -5;
    f->wind[2] = 0;
    ecs_modified(world, e, FogVolume);
    FogVolume fog = *f;
    ecs_progress(world, 1000);
    box_render_sync(world);
    ecs_progress(world, 1000);
    box_render_sync(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->fog_count, 1);
    const box_render_fog_volume_t *r = record_of(null, v, 0);
    test_assert(null->last.time > 0);
    float expected[3];
    box_render_fog_offset(&fog, null->last.time, expected);
    int32_t k;
    for (k = 0; k < 3; k ++) {
        test_assert(fabsf(r->offset[k] - expected[k]) < 1e-4f);
        test_assert(r->offset[k] >= 0 && r->offset[k] < 20);
    }

    test_flt(r->offset[2], 0);

    ecs_fini(world);
}

/** A view gives its packet view its fog distance: 100 from the View constructor and for 0, at
 * least 1. */
void FogVolume_fog_distance(void) {
    // fog_volume/froxels
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    volume_new(world, 0, 0, 0, -10);
    frame(world);
    test_flt(packet_view(null, view)->fog_distance, 100);

    View *v = ecs_get_mut(world, view, View);
    v->lighting.fog_distance = 40;
    ecs_modified(world, view, View);
    frame(world);
    test_flt(packet_view(null, view)->fog_distance, 40);

    v = ecs_get_mut(world, view, View);
    v->lighting.fog_distance = 0.25f;
    ecs_modified(world, view, View);
    frame(world);
    test_flt(packet_view(null, view)->fog_distance, 1);

    v = ecs_get_mut(world, view, View);
    v->lighting.fog_distance = 0;
    ecs_modified(world, view, View);
    frame(world);
    test_flt(packet_view(null, view)->fog_distance, 100);

    test_flt(box_render_fog_distance(-5), 1);
    test_flt(box_render_fog_distance(250), 250);

    ecs_fini(world);
}

/** Check the froxel grid of a render size.
 *
 * @param width Render width
 * @param height Render height
 * @param x Expected cells across
 * @param y Expected cells down
 * @param tile Expected tile edge in pixels
 */
static void froxel_expect(
    int32_t width,
    int32_t height,
    int32_t x,
    int32_t y,
    int32_t tile)
{
    int32_t grid[3];
    box_render_froxel_grid(width, height, grid);
    test_int(grid[0], x);
    test_int(grid[1], y);
    test_int(grid[2], tile);
}

/** The grid has a cell per 8x8 pixels up to 1920 pixels along the larger edge, larger tiles
 * beyond, so a 4K or 8K view has no more cells than a 1080p one. */
void FogVolume_froxel_grid_size(void) {
    // fog_volume/froxels
    froxel_expect(64, 64, 8, 8, 8);
    froxel_expect(100, 50, 13, 7, 8);
    froxel_expect(1280, 720, 160, 90, 8);
    froxel_expect(1920, 1080, 240, 135, 8);
    froxel_expect(1921, 1080, 214, 120, 9);
    froxel_expect(2560, 1440, 233, 131, 11);
    froxel_expect(3840, 2160, 240, 135, 16);
    froxel_expect(7680, 4320, 240, 135, 32);
    froxel_expect(1080, 1920, 135, 240, 8);
    froxel_expect(2160, 3840, 135, 240, 16);
    froxel_expect(1, 1, 1, 1, 8);
    froxel_expect(0, 0, 1, 1, 8);
}

/** The slices of a perspective grid run from the near plane to the far end, thin near the
 * camera and thicker with the distance, and the share of a depth inverts them; those of an
 * orthographic grid are even. */
void FogVolume_froxel_slices(void) {
    // fog_volume/froxels
    const float near_ = 0.1f, far_ = 100.1f;
    float last = near_, first = 0;
    int32_t i;
    test_flt(box_render_froxel_depth(0, near_, far_, false), near_);
    test_assert(fabsf(box_render_froxel_depth(1, near_, far_, false) - far_) < 1e-3f);
    test_flt(box_render_froxel_share(near_ - 1, near_, far_, false), 0);
    test_flt(box_render_froxel_share(far_ + 50, near_, far_, false), 1);
    for (i = 1; i <= BOX_FROXEL_SLICES; i ++) {
        float t = (float)i / (float)BOX_FROXEL_SLICES;
        float d = box_render_froxel_depth(t, near_, far_, false);
        float thickness = d - last;
        test_assert(thickness > 0);
        test_assert(fabsf(box_render_froxel_share(d, near_, far_, false) - t) < 1e-4f);
        if (i == 1) {
            first = thickness;
        } else {
            test_assert(thickness >= first);
        }

        last = d;
    }

    // Under 5 cm for the first slice, about half a meter at 5 m
    test_assert(first < 0.05f);
    float at5 = floorf(box_render_froxel_share(5, near_, far_, false) * BOX_FROXEL_SLICES);
    float d0 = box_render_froxel_depth(at5 / BOX_FROXEL_SLICES, near_, far_, false);
    float d1 = box_render_froxel_depth((at5 + 1) / BOX_FROXEL_SLICES, near_, far_, false);
    test_assert(d1 - d0 < 0.6f);

    for (i = 0; i <= BOX_FROXEL_SLICES; i ++) {
        float t = (float)i / (float)BOX_FROXEL_SLICES;
        test_assert(fabsf(box_render_froxel_depth(t, -2, 98, true) - (-2 + 100 * t)) < 1e-3f);
        test_assert(fabsf(box_render_froxel_share(-2 + 100 * t, -2, 98, true) - t) < 1e-5f);
    }
}

void FogVolume_falloff_script_keeps_others(void) {
    // fog_volume/falloff
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "fog",
        "using box.light\n"
        "fog { FogVolume: {shape: FogVolumeEllipsoid, edge_noise: 0.5} }\n", NULL) == 0);
    const FogVolume *f = ecs_get(world, ecs_lookup(world, "fog"), FogVolume);
    test_int(f->shape, FogVolumeEllipsoid);
    test_flt(f->edge_noise, 0.5f);
    test_flt(f->height_falloff, 0);
    test_flt(f->extent[0], 5);
    test_flt(f->density, 0.1f);
    test_flt(f->edge_fade, 1);
    test_flt(f->noise_strength, 0);
    test_flt(f->noise_scale, 4);

    test_assert(ecs_script_run(world, "fog2",
        "using box.light\n"
        "fog2 { FogVolume: {height_falloff: 1.5, density: 0.3} }\n", NULL) == 0);
    f = ecs_get(world, ecs_lookup(world, "fog2"), FogVolume);
    test_flt(f->height_falloff, 1.5f);
    test_flt(f->density, 0.3f);
    test_int(f->shape, FogVolumeBox);
    test_flt(f->edge_noise, 0);
    test_flt(f->edge_fade, 1);
    test_flt(f->asymmetry, 0.3f);

    ecs_fini(world);
}

void FogVolume_falloff_record(void) {
    // fog_volume/falloff
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t plain = volume_new(world, 0, 0, 0, -20);
    ecs_entity_t round = volume_new(world, 0, 0, 0, -5);
    ecs_set(world, round, FogVolume, {
        .extent = {2, 1, 3}, .density = 0.4f, .edge_fade = 0.5f, .noise_strength = 0.5f,
        .noise_scale = 4, .shape = FogVolumeEllipsoid, .height_falloff = 0.75f,
        .edge_noise = 0.25f});
    (void)plain;
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->fog_count, 2);
    const box_render_fog_volume_t *far_ = record_of(null, v, 0);
    const box_render_fog_volume_t *near_ = record_of(null, v, 1);
    test_flt(far_->center[2], -20);
    test_int(far_->shape, FogVolumeBox);
    test_flt(far_->height_falloff, 0);
    test_flt(far_->edge_noise, 0);

    test_flt(near_->center[2], -5);
    test_int(near_->shape, FogVolumeEllipsoid);
    test_flt(near_->height_falloff, 0.75f);
    test_flt(near_->edge_noise, 0.25f);
    test_flt(near_->extent[0], 2);
    test_flt(near_->extent[1], 1);
    test_flt(near_->extent[2], 3);
    test_flt(near_->density, 0.4f);
    test_flt(near_->edge_fade, 0.5f);
    test_flt(near_->noise_strength, 0.5f);
    test_assert(sizeof(box_render_fog_volume_t) % 16 == 0);

    ecs_fini(world);
}

void FogVolume_falloff_clamped(void) {
    // fog_volume/falloff
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t low = volume_new(world, 0, 0, 0, -5);
    ecs_entity_t high = volume_new(world, 0, 0, 0, -20);
    ecs_set(world, low, FogVolume, {
        .extent = {2, 2, 2}, .density = 0.1f, .edge_fade = 1,
        .shape = (FogVolumeShape)7, .height_falloff = -2, .edge_noise = -0.5f});
    ecs_set(world, high, FogVolume, {
        .extent = {2, 2, 2}, .density = 0.1f, .edge_fade = 1,
        .shape = (FogVolumeShape)-1, .height_falloff = 3, .edge_noise = 4});
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->fog_count, 2);
    const box_render_fog_volume_t *far_ = record_of(null, v, 0);
    const box_render_fog_volume_t *near_ = record_of(null, v, 1);
    test_flt(near_->center[2], -5);
    test_int(near_->shape, FogVolumeBox);
    test_flt(near_->height_falloff, 0);
    test_flt(near_->edge_noise, 0);

    test_flt(far_->center[2], -20);
    test_int(far_->shape, FogVolumeBox);
    test_flt(far_->height_falloff, 3);
    test_flt(far_->edge_noise, 1);

    ecs_fini(world);
}

/** Create an unbounded fog volume at a position.
 *
 * @param world The world
 * @param parent Parent of the volume, 0 for none
 * @param x World x
 * @param y World y
 * @param z World z
 * @return The volume
 */
static ecs_entity_t unbounded_new(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x,
    float y,
    float z)
{
    ecs_entity_t e = volume_new(world, parent, x, y, z);
    ecs_set(world, e, FogVolume, {
        .extent = {0.5f, 0.5f, 0.5f}, .density = 0.05f, .shape = FogVolumeUnbounded,
        .height_falloff = 0.2f});

    return e;
}

void FogVolume_unbounded_script(void) {
    // fog_volume/unbounded
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "fog",
        "using box.light\n"
        "fog { FogVolume: {shape: FogVolumeUnbounded, height_falloff: 0.1} }\n", NULL) == 0);
    const FogVolume *f = ecs_get(world, ecs_lookup(world, "fog"), FogVolume);
    test_assert(f != NULL);
    test_int(f->shape, FogVolumeUnbounded);
    test_flt(f->height_falloff, 0.1f);
    test_flt(f->density, 0.1f);
    test_flt(f->extent[0], 5);

    ecs_fini(world);
}

void FogVolume_unbounded_not_culled(void) {
    // fog_volume/unbounded
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    unbounded_new(world, 0, 0, 0, 40);
    unbounded_new(world, 0, 500, 0, -10);
    volume_new(world, 0, 0, 0, 40);
    volume_new(world, 0, 0, 0, -10);
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->fog_count, 3);
    test_flt(record_of(null, v, 0)->center[2], -10);
    test_int(record_of(null, v, 0)->shape, FogVolumeBox);
    test_int(record_of(null, v, 1)->shape, FogVolumeUnbounded);
    test_int(record_of(null, v, 2)->shape, FogVolumeUnbounded);
    test_flt(record_of(null, v, 1)->center[0], 500);
    test_flt(record_of(null, v, 2)->center[2], 40);
    test_flt(record_of(null, v, 2)->height_falloff, 0.2f);
    test_flt(record_of(null, v, 2)->density, 0.05f);

    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.fog_volumes, 3);
    test_int(stats.fog_dropped, 0);

    ecs_fini(world);
}

void FogVolume_unbounded_kept_over_limit(void) {
    // fog_volume/unbounded
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    int32_t i, count = BOX_RENDER_FOG_VOLUMES_MAX + 4;
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = volume_new(world, 0, 0, 0, -1.0f - 2.0f * (float)i);
        ecs_set(world, e, FogVolume, {.extent = {0.5f, 0.5f, 0.5f}, .density = 0.1f});
    }

    unbounded_new(world, 0, 0, 0, 400);
    unbounded_new(world, 0, 0, -300, -1000);

    ecs_os_api_log_t log = ecs_os_api.log_;
    fog_warnings = 0;
    ecs_os_api.log_ = fog_count_warnings;
    frame(world);
    ecs_os_api.log_ = log;
    test_int(fog_warnings, 1);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->fog_count, BOX_RENDER_FOG_VOLUMES_MAX);
    int32_t unbounded = 0;
    float farthest = 0;
    for (i = 0; i < v->fog_count; i ++) {
        const box_render_fog_volume_t *r = record_of(null, v, i);
        if (r->shape == FogVolumeUnbounded) {
            unbounded ++;
        } else {
            farthest = glm_min(farthest, r->center[2]);
        }
    }

    test_int(unbounded, 2);
    test_flt(farthest, -1.0f - 2.0f * (float)(BOX_RENDER_FOG_VOLUMES_MAX - 3));
    test_int(record_of(null, v, BOX_RENDER_FOG_VOLUMES_MAX - 1)->shape, FogVolumeUnbounded);
    test_int(record_of(null, v, BOX_RENDER_FOG_VOLUMES_MAX - 2)->shape, FogVolumeUnbounded);
    test_flt(record_of(null, v, BOX_RENDER_FOG_VOLUMES_MAX - 3)->center[2], -1);

    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.fog_volumes, BOX_RENDER_FOG_VOLUMES_MAX);
    test_int(stats.fog_dropped, 6);

    ecs_fini(world);
}

void FogVolume_unbounded_other_scene_none(void) {
    // fog_volume/unbounded
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t scene_view = view_new(world, scene);
    unbounded_new(world, scene, 0, 0, 40);
    frame(world);

    test_int(packet_view(null, view)->fog_count, 0);
    const box_render_view_t *v = packet_view(null, scene_view);
    test_int(v->fog_count, 1);
    test_int(record_of(null, v, 0)->shape, FogVolumeUnbounded);

    ecs_fini(world);
}

void FogVolume_unbounded_unlit_view_none(void) {
    // fog_volume/unbounded
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    View *v = ecs_get_mut(world, view, View);
    v->lighting.enabled = false;
    ecs_modified(world, view, View);
    unbounded_new(world, 0, 0, 0, 0);
    frame(world);

    test_int(packet_view(null, view)->fog_count, 0);
    test_int(ecs_vec_count(&null->last.fog_volumes), 0);

    ecs_fini(world);
}

void FogVolume_unbounded_view_disabled_none(void) {
    // fog_volume/unbounded
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t other = view_new(world, 0);
    View *v = ecs_get_mut(world, view, View);
    v->lighting.fog_volumes = false;
    ecs_modified(world, view, View);
    unbounded_new(world, 0, 0, 0, 0);
    frame(world);

    test_int(packet_view(null, view)->fog_count, 0);
    test_int(packet_view(null, other)->fog_count, 1);
    test_int(ecs_vec_count(&null->last.fog_volumes), 1);

    ecs_fini(world);
}
