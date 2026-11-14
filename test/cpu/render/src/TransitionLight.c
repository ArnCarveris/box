/**
 * @file TransitionLight.c
 * @brief Tests of the TransitionLight suite: transitions of PointLight, SpotLight and the Rgb of
 * a light, which the resolver writes into the light buffer upload of the last packet of the
 * null backend, on_add and on_remove of a light, and the shadow tile of a light that
 * transitions.
 *
 * The cases step the world with a fixed delta and a Linear transition of one second, so the
 * four frames after a change render at progress 0, 1/4, 1/2 and 3/4 and the fifth at the target.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "bake.h"
#include "transition_test.h"

/** Difference a patched float may have from the expected one */
#define TL_EPS (1e-3f)

/** A Linear transition of one second */
static const TransitionSpec tl_spec = { .duration = 1.0f };

/** A Linear transition of one second that animates adding and removing */
static const TransitionSpec tl_add_remove = { .duration = 1.0f, .on_add = true,
    .on_remove = true };

/** Convert a display encoded channel to linear, as the light extract does.
 *
 * @param v The channel
 * @return The linear value
 */
static float tl_linear(
    uint8_t v)
{
    float c = (float)v / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Get the index of a light in the view list of the default scene.
 *
 * @param world The world
 * @param e The light
 * @return The index, -1 when the light is not listed
 */
static int32_t tl_index(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_bake_light_set_t set;
    int32_t i, result = -1;
    box_bake_view_lights(box_render_lights_state(world), 0, &set);
    for (i = 0; i < set.count; i ++) {
        if (set.entities[i] == e) {
            result = i;
        }
    }

    return result;
}

/** Read a light as the backend got it in the last packet: from the upload of the light buffer
 * of the first view, which the resolver patched.
 *
 * @param world The world
 * @param null The backend
 * @param e The light
 * @param out Receives the light
 * @return Whether the light buffer was uploaded in the last packet
 */
static bool tl_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_light_t *out)
{
    const box_render_view_t *v;
    int32_t i, index;
    bool result = false;
    box_render_sync(world);
    index = tl_index(world, e);
    test_assert(index >= 0);
    test_assert(ecs_vec_count(&null->last.views) > 0);
    v = ecs_vec_first(&null->last.views);
    for (i = 0; i < ecs_vec_count(&null->last.uploads) && !result; i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == v->lights.buffer && u->offset == 0) {
            ecs_os_memcpy_t(out, box_render_packet_data(&null->last,
                u->data + (uint32_t)index * (uint32_t)sizeof(box_render_light_t)),
                box_render_light_t);
            result = true;
        }
    }

    return result;
}

/** Read a light as the backend got it in the last packet, which must have uploaded it.
 *
 * @param world The world
 * @param null The backend
 * @param e The light
 * @return The light
 */
static box_render_light_t tl_light(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e)
{
    box_render_light_t result = {0};
    test_assert(tl_rendered(world, null, e, &result));

    return result;
}

/** Create a point light with a white Rgb.
 *
 * @param world The world
 * @param intensity Its intensity
 * @param range Its range
 * @return The light
 */
static ecs_entity_t tl_point(
    ecs_world_t *world,
    float intensity,
    float range)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 2, 0});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PointLight, {.intensity = intensity, .range = range,
        .shadow_resolution = 512});

    return e;
}

/** Create a spot light with a white Rgb, shining straight down.
 *
 * @param world The world
 * @param intensity Its intensity
 * @param range Its range
 * @param shadows Whether it casts shadows
 * @return The light
 */
static ecs_entity_t tl_spot(
    ecs_world_t *world,
    float intensity,
    float range,
    bool shadows)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 4, 0});
    ecs_set(world, e, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, SpotLight, {.intensity = intensity, .range = range,
        .inner_angle = 20, .outer_angle = 30, .shadows = shadows, .shadow_resolution = 512});

    return e;
}

/** Create a world that renders into the null backend with a view that renders shadows, its
 * camera at (0, 2, 10).
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* tl_shadow_world(
    box_render_null_t **null)
{
    ecs_world_t *world = tr_world(null);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->shadows.enabled = true;
    ecs_modified(world, view, View);
    ecs_singleton_set(world, Scene, {.punctual_shadows = true});

    return world;
}

/** Find the tile of a light in the last packet.
 *
 * @param world The world
 * @param null The backend
 * @param light The light
 * @return Its tile, NULL for none
 */
static const box_render_shadow_tile_t* tl_tile(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t light)
{
    const box_render_shadow_tile_t *result = NULL;
    int32_t i;
    box_render_sync(world);
    for (i = 0; i < ecs_vec_count(&null->last.shadow_tiles); i ++) {
        const box_render_shadow_tile_t *t = ecs_vec_get_t(
            &null->last.shadow_tiles, box_render_shadow_tile_t, i);
        if (t->light == light) {
            result = t;
        }
    }

    return result;
}

/** Compute the cone falloff of a spot light, as the light extract does.
 *
 * @param inner Inner half angle in degrees
 * @param outer Outer half angle in degrees
 * @param scale Receives spot_scale
 * @param offset Receives spot_offset
 */
static void tl_cone(
    float inner,
    float outer,
    float *scale,
    float *offset)
{
    float cos_outer = cosf(glm_rad(outer));
    float cos_inner = cosf(glm_rad(inner));
    *scale = 1.0f / glm_max(cos_inner - cos_outer, 1e-4f);
    *offset = -cos_outer * *scale;
}

void TransitionLight_point_intensity(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tl_point(world, 4, 10);
    box_transition_set(world, e, ecs_id(PointLight), &tl_spec);
    PointLight rendered;
    box_render_light_t light;
    int32_t i;
    tr_frame(world);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - 4.0f) < TL_EPS);
    tr_frame(world);

    ecs_set(world, e, PointLight, {.intensity = 8, .range = 10, .shadow_resolution = 512});
    for (i = 0; i < 4; i ++) {
        float mid = 4.0f + 4.0f * (float)i / 4.0f;
        tr_frame(world);
        test_flt(ecs_get(world, e, PointLight)->intensity, 8);
        test_assert(box_render_transition_test_value(world, e, ecs_id(PointLight), &rendered));
        test_assert(fabsf(rendered.intensity - mid) < TL_EPS);
        test_assert(fabsf(rendered.range - 10.0f) < TL_EPS);
        light = tl_light(world, null, e);
        test_assert(fabsf(light.color[0] - mid) < TL_EPS);
        test_assert(fabsf(light.color[1] - mid) < TL_EPS);
        test_assert(fabsf(light.color[2] - mid) < TL_EPS);
        test_assert(fabsf(light.range - 10.0f) < TL_EPS);
        test_int(tr_records(null, BOX_RENDER_TRANSITION_LIGHT), 1);
    }

    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_LIGHT), 0);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - 8.0f) < TL_EPS);

    tr_frame(world);
    test_assert(!tl_rendered(world, null, e, &light));
    tr_fini(world, null);
}

void TransitionLight_point_range(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tl_point(world, 4, 10);
    box_transition_set(world, e, ecs_id(PointLight), &tl_spec);
    PointLight rendered;
    box_render_light_t light;
    tr_frames(world, 2);

    ecs_set(world, e, PointLight, {.intensity = 4, .range = 20, .shadow_resolution = 512});
    tr_frame(world);
    tr_frames(world, 2);
    test_flt(ecs_get(world, e, PointLight)->range, 20);
    test_assert(box_render_transition_test_value(world, e, ecs_id(PointLight), &rendered));
    test_assert(fabsf(rendered.range - 15.0f) < TL_EPS);
    test_assert(fabsf(rendered.intensity - 4.0f) < TL_EPS);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.range - 15.0f) < TL_EPS);
    test_assert(fabsf(light.color[0] - 4.0f) < TL_EPS);

    tr_frames(world, 2);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.range - 20.0f) < TL_EPS);
    tr_fini(world, null);
}

void TransitionLight_point_range_scaled(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {0, 0, 0});
    ecs_set(world, root, Scale, {2, 2, 2});
    ecs_entity_t e = tl_point(world, 4, 10);
    ecs_add_pair(world, e, EcsChildOf, root);
    box_transition_set(world, e, ecs_id(PointLight), &tl_spec);
    box_render_light_t light;
    tr_frames(world, 2);

    ecs_set(world, e, PointLight, {.intensity = 8, .range = 20, .shadow_resolution = 512});
    tr_frame(world);
    tr_frames(world, 2);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.range - 30.0f) < TL_EPS);
    test_assert(fabsf(light.color[0] - 24.0f) < TL_EPS);

    tr_frames(world, 2);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.range - 40.0f) < TL_EPS);
    test_assert(fabsf(light.color[0] - 32.0f) < TL_EPS);
    tr_fini(world, null);
}

void TransitionLight_range_to_zero(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rgb, {255, 255, 255});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t p = tl_point(world, 4, 10);
    ecs_entity_t sp = tl_spot(world, 4, 10, false);
    box_transition_set(world, p, ecs_id(PointLight), &tl_spec);
    box_transition_set(world, sp, ecs_id(SpotLight), &tl_spec);
    const box_render_view_t *v;
    box_bake_light_set_t set;
    box_render_light_t light;
    int32_t i, directional = 0;
    tr_frames(world, 2);

    ecs_set(world, p, PointLight, {.intensity = 4, .range = 0, .shadow_resolution = 512});
    ecs_set(world, sp, SpotLight, {.intensity = 4, .range = 0, .inner_angle = 20,
        .outer_angle = 30, .shadow_resolution = 512});
    tr_frame(world);
    tr_frames(world, 2);
    light = tl_light(world, null, p);
    test_assert(fabsf(light.range - 5.0f) < TL_EPS);
    light = tl_light(world, null, sp);
    test_assert(fabsf(light.range - 5.0f) < TL_EPS);
    v = ecs_vec_first(&null->last.views);
    test_uint(v->lights.directional, 1);
    test_uint(v->lights.count, 3);
    box_bake_view_lights(box_render_lights_state(world), 0, &set);
    test_int(set.count, 3);
    for (i = 0; i < set.count; i ++) {
        directional += set.lights[i].range > 0 ? 0 : 1;
    }
    test_int(directional, 1);
    test_assert(set.entities[0] == sun);

    tr_frames(world, 3);
    box_render_sync(world);
    box_bake_view_lights(box_render_lights_state(world), 0, &set);
    test_int(set.count, 1);
    test_assert(set.entities[0] == sun);
    tr_fini(world, null);
}

void TransitionLight_spot_intensity_range_angles(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tl_spot(world, 2, 10, false);
    box_transition_set(world, e, ecs_id(SpotLight), &tl_spec);
    SpotLight rendered;
    box_render_light_t light;
    float scale, offset;
    tr_frames(world, 2);

    ecs_set(world, e, SpotLight, {.intensity = 6, .range = 20, .inner_angle = 10,
        .outer_angle = 20, .shadow_resolution = 512});
    tr_frame(world);
    tr_frames(world, 2);
    test_flt(ecs_get(world, e, SpotLight)->outer_angle, 20);
    test_assert(box_render_transition_test_value(world, e, ecs_id(SpotLight), &rendered));
    test_assert(fabsf(rendered.intensity - 4.0f) < TL_EPS);
    test_assert(fabsf(rendered.range - 15.0f) < TL_EPS);
    test_assert(fabsf(rendered.inner_angle - 15.0f) < TL_EPS);
    test_assert(fabsf(rendered.outer_angle - 25.0f) < TL_EPS);
    light = tl_light(world, null, e);
    tl_cone(15.0f, 25.0f, &scale, &offset);
    test_assert(fabsf(light.color[0] - 4.0f) < TL_EPS);
    test_assert(fabsf(light.range - 15.0f) < TL_EPS);
    test_assert(fabsf(light.spot_scale - scale) < scale * TL_EPS);
    test_assert(fabsf(light.spot_offset - offset) < fabsf(offset) * TL_EPS);
    test_assert(light.direction[1] < -0.99f);

    tr_frames(world, 2);
    light = tl_light(world, null, e);
    tl_cone(10.0f, 20.0f, &scale, &offset);
    test_assert(fabsf(light.color[0] - 6.0f) < TL_EPS);
    test_assert(fabsf(light.range - 20.0f) < TL_EPS);
    test_assert(fabsf(light.spot_scale - scale) < scale * TL_EPS);
    tr_fini(world, null);
}

void TransitionLight_point_color(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tl_point(world, 2, 10);
    ecs_set(world, e, Rgb, {255, 0, 0});
    box_transition_set(world, e, ecs_id(Rgb), &tl_spec);
    box_render_light_t light;
    Rgb rgb;
    tr_frames(world, 2);

    ecs_set(world, e, Rgb, {0, 0, 255});
    tr_frame(world);
    tr_frames(world, 2);
    rgb = tr_rgb(world, e);
    test_int(rgb.r, 128);
    test_int(rgb.g, 0);
    test_int(rgb.b, 128);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_LIGHT), 1);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - tl_linear(128) * 2.0f) < TL_EPS);
    test_assert(fabsf(light.color[1]) < TL_EPS);
    test_assert(fabsf(light.color[2] - tl_linear(128) * 2.0f) < TL_EPS);

    tr_frames(world, 2);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0]) < TL_EPS);
    test_assert(fabsf(light.color[2] - 2.0f) < TL_EPS);
    tr_fini(world, null);
}

void TransitionLight_spot_color(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tl_spot(world, 2, 10, false);
    box_transition_set(world, e, ecs_id(Rgb), &tl_spec);
    box_render_light_t light;
    tr_frames(world, 2);

    ecs_set(world, e, Rgb, {0, 255, 0});
    tr_frame(world);
    tr_frame(world);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - tl_linear(191) * 2.0f) < TL_EPS);
    test_assert(fabsf(light.color[1] - 2.0f) < TL_EPS);
    test_assert(fabsf(light.color[2] - tl_linear(191) * 2.0f) < TL_EPS);
    tr_fini(world, null);
}

void TransitionLight_directional_color(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Rgb, {255, 0, 0});
    ecs_set(world, e, DirectionalLight, {.intensity = 3});
    box_transition_set(world, e, ecs_id(Rgb), &tl_spec);
    box_render_light_t light;
    tr_frames(world, 2);

    ecs_set(world, e, Rgb, {0, 0, 255});
    tr_frame(world);
    tr_frames(world, 2);
    light = tl_light(world, null, e);
    test_flt(light.range, 0);
    test_assert(fabsf(light.color[0] - tl_linear(128) * 3.0f) < TL_EPS);
    test_assert(fabsf(light.color[2] - tl_linear(128) * 3.0f) < TL_EPS);
    tr_fini(world, null);
}

void TransitionLight_point_add(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 2, 0});
    box_transition_set(world, e, ecs_id(PointLight), &tl_add_remove);
    PointLight rendered;
    box_render_light_t light;
    int32_t i;
    tr_frames(world, 2);

    ecs_set(world, e, PointLight, {.intensity = 4, .range = 10, .shadow_resolution = 512});
    for (i = 0; i < 4; i ++) {
        float mid = 4.0f * (float)i / 4.0f;
        tr_frame(world);
        test_assert(box_render_transition_test_value(world, e, ecs_id(PointLight), &rendered));
        test_assert(fabsf(rendered.intensity - mid) < TL_EPS);
        test_assert(fabsf(rendered.range - 10.0f) < TL_EPS);
        light = tl_light(world, null, e);
        test_assert(fabsf(light.color[0] - mid) < TL_EPS);
        test_assert(fabsf(light.range - 10.0f) < TL_EPS);
    }

    tr_frame(world);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - 4.0f) < TL_EPS);
    tr_fini(world, null);
}

void TransitionLight_point_remove(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tl_point(world, 4, 10);
    box_transition_set(world, e, ecs_id(PointLight), &tl_add_remove);
    box_render_light_t light;
    int32_t i;
    tr_frames(world, 2);

    ecs_remove(world, e, PointLight);
    for (i = 0; i < 4; i ++) {
        float mid = 4.0f - 4.0f * (float)i / 4.0f;
        tr_frame(world);
        test_assert(!ecs_has(world, e, PointLight));
        test_assert(tl_index(world, e) >= 0);
        light = tl_light(world, null, e);
        test_assert(fabsf(light.color[0] - mid) < TL_EPS);
        test_assert(fabsf(light.range - 10.0f) < TL_EPS);
    }

    tr_frame(world);
    test_int(tl_index(world, e), -1);
    tr_fini(world, null);
}

void TransitionLight_spot_add_remove(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 4, 0});
    ecs_set(world, e, Rotation, {(float)GLM_PI_2, 0, 0});
    box_transition_set(world, e, ecs_id(SpotLight), &tl_add_remove);
    box_render_light_t light;
    float scale, offset;
    tr_frames(world, 2);

    ecs_set(world, e, SpotLight, {.intensity = 4, .range = 10, .inner_angle = 10,
        .outer_angle = 20, .shadow_resolution = 512});
    tr_frame(world);
    tr_frames(world, 2);
    light = tl_light(world, null, e);
    tl_cone(10.0f, 20.0f, &scale, &offset);
    test_assert(fabsf(light.color[0] - 2.0f) < TL_EPS);
    test_assert(fabsf(light.spot_scale - scale) < scale * TL_EPS);
    tr_frames(world, 2);

    ecs_remove(world, e, SpotLight);
    tr_frame(world);
    tr_frame(world);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - 3.0f) < TL_EPS);
    test_assert(fabsf(light.spot_scale - scale) < scale * TL_EPS);
    tr_frames(world, 3);
    test_int(tl_index(world, e), -1);
    tr_fini(world, null);
}

void TransitionLight_remove_snaps(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tl_point(world, 4, 10);
    box_transition_set(world, e, ecs_id(PointLight), &tl_spec);
    tr_frames(world, 2);

    ecs_remove(world, e, PointLight);
    tr_frame(world);
    test_int(tl_index(world, e), -1);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_LIGHT), 0);
    tr_fini(world, null);
}

void TransitionLight_fade_to_zero(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tl_point(world, 4, 10);
    box_transition_set(world, e, ecs_id(PointLight), &tl_spec);
    box_render_light_t light;
    tr_frames(world, 2);

    ecs_set(world, e, PointLight, {.intensity = 0, .range = 10, .shadow_resolution = 512});
    tr_frame(world);
    tr_frame(world);
    test_assert(tl_index(world, e) >= 0);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - 3.0f) < TL_EPS);
    tr_frames(world, 3);
    test_int(tl_index(world, e), -1);
    tr_fini(world, null);
}

void TransitionLight_interrupted(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tl_point(world, 4, 10);
    box_transition_set(world, e, ecs_id(PointLight), &tl_spec);
    box_render_light_t light;
    tr_frames(world, 2);

    ecs_set(world, e, PointLight, {.intensity = 8, .range = 10, .shadow_resolution = 512});
    tr_frame(world);
    tr_frames(world, 2);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - 6.0f) < TL_EPS);

    ecs_set(world, e, PointLight, {.intensity = 2, .range = 10, .shadow_resolution = 512});
    tr_frame(world);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - 7.0f) < TL_EPS);
    tr_frames(world, 2);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - 4.5f) < TL_EPS);
    tr_frames(world, 2);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - 2.0f) < TL_EPS);
    tr_fini(world, null);
}

void TransitionLight_instance_of_prefab(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, Rgb, {255, 255, 255});
    box_transition_set(world, base, ecs_id(Rgb), &tl_spec);
    ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, base);
    ecs_set(world, e, Position, {0, 2, 0});
    ecs_set(world, e, PointLight, {.intensity = 2, .range = 10, .shadow_resolution = 512});
    box_render_light_t light;
    tr_frames(world, 2);

    ecs_set(world, base, Rgb, {0, 0, 0});
    tr_frame(world);
    tr_frames(world, 2);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - tl_linear(128) * 2.0f) < TL_EPS);
    tr_fini(world, null);
}

void TransitionLight_shadow_tile_intensity(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tl_shadow_world(&null);
    ecs_entity_t e = tl_spot(world, 4, 10, true);
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {0, 2, 0});
    ecs_set(world, caster, Box, {1, 0.1f, 1});
    box_transition_set(world, e, ecs_id(SpotLight), &tl_spec);
    box_transition_set(world, e, ecs_id(Rgb), &tl_spec);
    const box_render_shadow_tile_t *tile;
    box_render_light_t light;
    int32_t i;
    tr_frame(world);
    tile = tl_tile(world, null, e);
    test_assert(tile != NULL);
    test_bool(tile->render, true);
    tr_frame(world);
    test_bool(tl_tile(world, null, e)->render, false);

    ecs_set(world, e, SpotLight, {.intensity = 8, .range = 10, .inner_angle = 20,
        .outer_angle = 30, .shadows = true, .shadow_resolution = 512});
    ecs_set(world, e, Rgb, {255, 0, 0});
    for (i = 0; i < 5; i ++) {
        tr_frame(world);
        tile = tl_tile(world, null, e);
        test_assert(tile != NULL);
        test_bool(tile->render, false);
        light = tl_light(world, null, e);
        test_assert(light.shadow != BOX_RENDER_SHADOW_NONE);
    }

    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - 8.0f) < TL_EPS);
    test_assert(fabsf(light.color[1]) < TL_EPS);
    tr_fini(world, null);
}

void TransitionLight_shadow_tile_range(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tl_shadow_world(&null);
    ecs_entity_t e = tl_spot(world, 4, 10, true);
    box_transition_set(world, e, ecs_id(SpotLight), &tl_spec);
    const box_render_shadow_tile_t *tile;
    float far_;
    int32_t i;
    tr_frames(world, 2);
    far_ = tl_tile(world, null, e)->far_;

    ecs_set(world, e, SpotLight, {.intensity = 4, .range = 5, .inner_angle = 20,
        .outer_angle = 30, .shadows = true, .shadow_resolution = 512});
    for (i = 0; i < 4; i ++) {
        tr_frame(world);
        tile = tl_tile(world, null, e);
        test_assert(tile != NULL);
        test_flt(tile->far_, far_);
        test_bool(tile->render, false);
    }

    tr_frame(world);
    tile = tl_tile(world, null, e);
    test_assert(tile != NULL);
    test_assert(tile->far_ < far_);
    test_bool(tile->render, true);
    tr_fini(world, null);
}

void TransitionLight_no_transition_no_records(void) {
    // engine_core/transition
    box_render_null_t *null;
    ecs_world_t *world = tr_scene(&null);
    ecs_entity_t e = tl_point(world, 4, 10);
    ecs_entity_t other = tl_point(world, 2, 10);
    box_transition_set(world, other, ecs_id(PointLight), &tl_spec);
    box_render_light_t light;
    tr_frames(world, 2);
    test_assert(!tl_rendered(world, null, e, &light));

    ecs_set(world, e, PointLight, {.intensity = 8, .range = 10, .shadow_resolution = 512});
    tr_frame(world);
    test_int(tr_records(null, BOX_RENDER_TRANSITION_LIGHT), 0);
    light = tl_light(world, null, e);
    test_assert(fabsf(light.color[0] - 8.0f) < TL_EPS);
    tr_frame(world);
    test_assert(!tl_rendered(world, null, e, &light));
    test_assert(box_render_transitions_light(box_render_transitions_get(world), e) == NULL);
    tr_fini(world, null);
}

void TransitionLight_neutral_values(void) {
    // engine_core/transition
    PointLight point = {.intensity = 4, .range = 12, .bake = LightBakeModeRealtime,
        .shadows = true, .shadow_resolution = 256};
    SpotLight spot = {.intensity = 4, .range = 12, .inner_angle = 15, .outer_angle = 25,
        .bake = LightBakeModeRealtime, .shadows = true, .shadow_resolution = 256};
    float value[BOX_RENDER_TRANSITION_VALUE / 4], out[BOX_RENDER_TRANSITION_VALUE / 4];
    PointLight p;
    SpotLight s;

    box_render_transition_pack(BOX_RENDER_TRANSITION_POINT_LIGHT, &point, value);
    test_assert(box_render_transition_neutral(BOX_RENDER_TRANSITION_POINT_LIGHT, value, out));
    ecs_os_memcpy_t(&p, out, PointLight);
    test_flt(p.intensity, 0);
    test_flt(p.range, 12);
    test_int(p.bake, LightBakeModeRealtime);
    test_bool(p.shadows, true);
    test_int(p.shadow_resolution, 256);

    box_render_transition_pack(BOX_RENDER_TRANSITION_SPOT_LIGHT, &spot, value);
    test_assert(box_render_transition_neutral(BOX_RENDER_TRANSITION_SPOT_LIGHT, value, out));
    ecs_os_memcpy_t(&s, out, SpotLight);
    test_flt(s.intensity, 0);
    test_flt(s.range, 12);
    test_flt(s.inner_angle, 15);
    test_flt(s.outer_angle, 25);
    test_int(s.bake, LightBakeModeRealtime);
    test_bool(s.shadows, true);
    test_int(s.shadow_resolution, 256);

    spot.shadows = false;
    box_render_transition_pack(BOX_RENDER_TRANSITION_SPOT_LIGHT, &spot, out);
    test_assert(box_render_transition_equal(BOX_RENDER_TRANSITION_SPOT_LIGHT, value, out));
}
