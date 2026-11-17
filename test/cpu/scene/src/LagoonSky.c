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
