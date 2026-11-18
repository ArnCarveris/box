/**
 * @file LagoonSky.c
 * @brief The sky and the reflections of the Coconut Lagoon scene,
 * test/assets/scenes/lagoon.flecs: the water mirrors the scene with a PlanarReflection that the
 * scene turns on, and soft cloud layers drift through the sky above the camera.
 *
 * The scene is a copy of etc/scenes/lagoon.flecs and its scripts in etc/scenes/lagoon, which a
 * change to the scene copies too. It runs without a render backend.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** Path of the lagoon scene in the repository */
#define LAGOON_PATH "test/assets/scenes/lagoon.flecs"

/** Seconds a frame advances */
#define LAGOON_DT (1.0f / 6.0f)

/** A speed of the world well above the start speed */
#define LAGOON_FAST_SPEED (18.0f)

/** GS_OVER of the scene (state.flecs), the game state once the canoe capsized */
#define LAGOON_GS_OVER (2)

/** Load the lagoon scene and run it for a frame.
 *
 * @return The world of the scene
 */
static ecs_world_t* lagoon_sky_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = box_app_locate(LAGOON_PATH);
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_new_from_path_w_sep(world, 0, LAGOON_PATH, "/", "/"),
        .filename = path
    });
    ecs_os_free(path);
    test_assert(script != 0);
    const EcsScript *loaded = ecs_get(world, script, EcsScript);
    test_assert(loaded != NULL);
    test_assert(loaded->error == false);
    ecs_singleton_set(world, App, {.delta_time = LAGOON_DT, .headless = true});
    ecs_progress(world, LAGOON_DT);

    return world;
}

/** The water has a PlanarReflection with a reduced image and the scene renders planar
 * reflections, while the water casts no shadow.
 */
void LagoonSky_water_reflects(void)
{
    ecs_world_t *world = lagoon_sky_new();

    const Scene *scene = ecs_singleton_get(world, Scene);
    test_assert(scene != NULL);
    test_bool(scene->planar_reflections, true);
    test_bool(scene->water, true);

    ecs_entity_t water = ecs_lookup(world, "lagoon.water");
    test_assert(water != 0);
    test_assert(ecs_has(world, water, Water));
    test_assert(ecs_has(world, water, NoShadow));
    const PlanarReflection *refl = ecs_get(world, water, PlanarReflection);
    test_assert(refl != NULL);
    test_assert(refl->resolution > 0.124f);
    test_assert(refl->resolution <= 0.5f);
    test_assert(refl->distortion > 0);

    ecs_fini(world);
}

/** At least one cloud layer with a texture covers the sky above the camera and drifts with a
 * slow wind.
 */
void LagoonSky_clouds_drift(void)
{
    ecs_world_t *world = lagoon_sky_new();

    const Position *cam = ecs_get(world, ecs_lookup(world, "camera"), Position);
    test_assert(cam != NULL);

    int32_t layers = 0;
    ecs_iter_t it = ecs_each(world, CloudLayer);
    while (ecs_each_next(&it)) {
        CloudLayer *c = ecs_field(&it, CloudLayer, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            test_assert(c[i].texture != 0);
            test_assert(c[i].size > 0);
            test_assert(c[i].density > 0);
            test_assert(c[i].altitude > cam->y);
            float speed = sqrtf(c[i].velocity[0] * c[i].velocity[0] +
                c[i].velocity[1] * c[i].velocity[1]);
            test_assert(speed > 0);
            test_assert(speed < 30);
            layers ++;
        }
    }

    test_assert(layers >= 1);
    test_assert(layers <= 2);
    ecs_fini(world);
}

/** The water tints the floor seen through it: its absorption is not white, and turquoise, with
 * the red absorbed the most.
 */
void LagoonSky_water_absorbs(void)
{
    ecs_world_t *world = lagoon_sky_new();

    ecs_entity_t water = ecs_lookup(world, "lagoon.water");
    test_assert(water != 0);
    const Water *w = ecs_get(world, water, Water);
    test_assert(w != NULL);
    test_assert(w->absorption.r < 255 || w->absorption.g < 255 || w->absorption.b < 255);
    test_assert(w->absorption.r < w->absorption.g);
    test_assert(w->absorption.r < w->absorption.b);
    test_assert(w->visibility > 0);

    ecs_fini(world);
}

/** Read an f32 script variable of the scene.
 *
 * @param world The scene
 * @param path Path of the variable
 * @return Its value
 */
static float lagoon_sky_f32(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t var = ecs_lookup(world, path);
    test_assert(var != 0);
    ecs_value_t v = ecs_mut_var_get(world, var);
    test_assert(v.ptr != NULL);

    return *(float*)v.ptr;
}

/** Check that the waves of the water move toward the camera (+Z) at speed units a second: U of
 * the world mapping runs along +Z, so the U velocity is -speed / mapping size, with a small
 * cross drift along V.
 *
 * @param world The scene
 * @param speed The speed of the world
 */
static void lagoon_sky_check_flow(
    ecs_world_t *world,
    float speed)
{
    ecs_entity_t water = ecs_lookup(world, "lagoon.water");
    test_assert(water != 0);
    const TextureMapping *mapping = ecs_get(world, water, TextureMapping);
    test_assert(mapping != NULL);
    test_assert(mapping->mode == MappingWorld);
    test_assert(mapping->size > 0);
    const TextureScroll *scroll = ecs_get(world, water, TextureScroll);
    test_assert(scroll != NULL);
    test_assert(fabsf(scroll->velocity[0] + speed / mapping->size) < 1e-3f);
    test_assert(fabsf(scroll->velocity[1]) < 0.1f);
}

/** The waves of the water flow toward the camera with the banks and the floor at the speed of
 * the world, and stop with it once the canoe capsizes.
 */
void LagoonSky_water_flows(void)
{
    ecs_world_t *world = lagoon_sky_new();
    int32_t i;

    // No rows spawn, so nothing ends the run
    test_int(ecs_mut_var_set_t(world, "state.next_row", ecs_f64_t, {1e12}), 0);

    // At the start speed
    float speed = lagoon_sky_f32(world, "state.speed");
    test_assert(speed > 0);
    lagoon_sky_check_flow(world, speed);

    // At a faster speed: without rows the speed stays where it is set
    float fast = LAGOON_FAST_SPEED;
    test_assert(fast > speed);
    test_int(ecs_mut_var_set_t(world, "state.speed", ecs_f32_t, {fast}), 0);
    for (i = 0; i < 2; i ++) {
        ecs_progress(world, LAGOON_DT);
    }
    speed = lagoon_sky_f32(world, "state.speed");
    test_assert(fabsf(speed - fast) < 1e-3f);
    lagoon_sky_check_flow(world, speed);

    // The world stops once the game is over, and the water with it
    test_int(ecs_mut_var_set_t(world, "state.game_state", ecs_i32_t, {LAGOON_GS_OVER}), 0);
    for (i = 0; i < 2; i ++) {
        ecs_progress(world, LAGOON_DT);
    }
    lagoon_sky_check_flow(world, 0);

    ecs_fini(world);
}
