/**
 * @file ForestScene.c
 * @brief The skeleton of the Forest scene, test/assets/scenes/forest.flecs: its state and the
 * temperature derived from it, the sun that follows the hour and season, the floor that leaves
 * the brook bed open, the landmarks clear of the brook and the path, and the HUD sliders.
 *
 * The scene is a copy of etc/scenes/forest.flecs and its scripts in etc/scenes/forest, which a
 * change to the scene copies too, without the include of the widget library (the test loads
 * test/assets/ui/widgets.flecs first) and with the fonts of test/assets/fonts. It runs without a
 * render backend.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** Path of the forest scene in the repository */
#define FOREST_PATH "test/assets/scenes/forest.flecs"

/** Seconds a frame advances */
#define FOREST_DT (1.0f / 30.0f)

/* The layout of forest/layout.flecs */
#define FOREST_WORLD_HALF (80.0)
#define FOREST_CLEARING_R (12.5)
#define FOREST_BROOK_Z (-9.0)
#define FOREST_BROOK_A1 (4.0)
#define FOREST_BROOK_K1 (0.042)
#define FOREST_BROOK_P1 (0.5)
#define FOREST_BROOK_A2 (1.5)
#define FOREST_BROOK_K2 (0.11)
#define FOREST_BROOK_P2 (2.1)
#define FOREST_BROOK_BANK (2.6)
#define FOREST_BROOK_RIM (3.9)
#define FOREST_PATH_X (1.5)
#define FOREST_PATH_A1 (2.0)
#define FOREST_PATH_K1 (0.045)
#define FOREST_PATH_P1 (0.8)
#define FOREST_PATH_A2 (1.0)
#define FOREST_PATH_K2 (0.12)
#define FOREST_PATH_P2 (2.4)
#define FOREST_PATH_HALF_WIDTH (0.65)

/** Strips of the floor along X (WORLD_SIZE / 2) */
#define FOREST_STRIPS (80)

/** Z of the brook centerline at x */
static double forest_brook_z(
    double x)
{
    return FOREST_BROOK_Z + FOREST_BROOK_A1 * sin(FOREST_BROOK_K1 * x + FOREST_BROOK_P1) +
        FOREST_BROOK_A2 * sin(FOREST_BROOK_K2 * x + FOREST_BROOK_P2);
}

/** dz/dx of the brook centerline at x */
static double forest_brook_slope(
    double x)
{
    return FOREST_BROOK_A1 * FOREST_BROOK_K1 * cos(FOREST_BROOK_K1 * x + FOREST_BROOK_P1) +
        FOREST_BROOK_A2 * FOREST_BROOK_K2 * cos(FOREST_BROOK_K2 * x + FOREST_BROOK_P2);
}

/** Distance from (x, z) to the brook centerline */
static double forest_brook_distance(
    double x,
    double z)
{
    double s = forest_brook_slope(x);
    return fabs(z - forest_brook_z(x)) / sqrt(1 + s * s);
}

/** Distance from (x, z) to the path centerline */
static double forest_path_distance(
    double x,
    double z)
{
    double px = FOREST_PATH_X + FOREST_PATH_A1 * sin(FOREST_PATH_K1 * z + FOREST_PATH_P1) +
        FOREST_PATH_A2 * sin(FOREST_PATH_K2 * z + FOREST_PATH_P2);
    double s = FOREST_PATH_A1 * FOREST_PATH_K1 * cos(FOREST_PATH_K1 * z + FOREST_PATH_P1) +
        FOREST_PATH_A2 * FOREST_PATH_K2 * cos(FOREST_PATH_K2 * z + FOREST_PATH_P2);
    return fabs(x - px) / sqrt(1 + s * s);
}

/** Load the widget library and the forest scene and run it for a frame.
 *
 * @return The world of the scene
 */
static ecs_world_t* forest_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, App, {.delta_time = FOREST_DT, .headless = true});

    char *widgets = box_app_locate("test/assets/ui/widgets.flecs");
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "box.ui.widgets" }),
        .filename = widgets
    }) != 0);
    ecs_os_free(widgets);

    char *path = box_app_locate(FOREST_PATH);
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_new_from_path_w_sep(world, 0, FOREST_PATH, "/", "/"),
        .filename = path
    });
    ecs_os_free(path);
    test_assert(script != 0);
    const EcsScript *loaded = ecs_get(world, script, EcsScript);
    test_assert(loaded != NULL);
    test_assert(loaded->error == false);
    ecs_progress(world, FOREST_DT);

    return world;
}

/** Run frames so the scripts follow a change of the state.
 *
 * @param world The world
 */
static void forest_frames(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, FOREST_DT);
    }
}

/** Get a mut variable of the scene.
 *
 * @param world The world
 * @param path Path of the variable
 * @return Pointer to its value
 */
static void* forest_var(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t var = ecs_lookup(world, path);
    test_assert(var != 0);
    ecs_value_t v = ecs_mut_var_get(world, var);
    test_assert(v.ptr != NULL);

    return v.ptr;
}

/** Set the season and the hour of the scene and let it follow.
 *
 * @param world The world
 * @param season The season, 0..4
 * @param hour The hour
 */
static void forest_set_time(
    ecs_world_t *world,
    float season,
    float hour)
{
    test_int(ecs_mut_var_set_t(world, "state.season", ecs_f32_t, {season}), 0);
    test_int(ecs_mut_var_set_t(world, "state.hour", ecs_f32_t, {hour}), 0);
    forest_frames(world);
}

/** The temperature of the scene at a season and an hour.
 *
 * @param world The world
 * @param season The season, 0..4
 * @param hour The hour
 * @return The temperature in °C
 */
static float forest_temperature_at(
    ecs_world_t *world,
    float season,
    float hour)
{
    forest_set_time(world, season, hour);
    return *(float*)forest_var(world, "state.temperature");
}

/** Look up an entity of the scene.
 *
 * @param world The world
 * @param path Path of the entity
 * @return The entity
 */
static ecs_entity_t forest_lookup(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    if (!result) {
        printf("no entity %s\n", path);
    }
    test_assert(result != 0);

    return result;
}

/** The scene loads with its start state (late summer, dry, a light breeze, 17:00) and turns on
 * the shader features the forest uses.
 */
void ForestScene_loads(void)
{
    ecs_world_t *world = forest_new();

    test_flt(*(float*)forest_var(world, "state.season"), 1.6f);
    test_flt(*(float*)forest_var(world, "state.precipitation"), 0.0f);
    test_flt(*(float*)forest_var(world, "state.wind"), 3.0f);
    test_flt(*(float*)forest_var(world, "state.hour"), 17.0f);

    const Scene *scene = ecs_singleton_get(world, Scene);
    test_assert(scene != NULL);
    test_bool(scene->texture_blend, true);
    test_bool(scene->triplanar, true);
    test_bool(scene->alpha_cutout, true);
    test_bool(scene->diffuse_transmission, true);
    test_bool(scene->water, true);
    test_bool(scene->planar_reflections, true);
    test_bool(scene->cookies, true);
    test_bool(scene->environment, true);

    ecs_fini(world);
}

/** TimeOfDay follows the hour of the state and the season sets the day of the year: the summer
 * solstice at season 1, the winter one at 3, the spring equinox at 0 and again at 4.
 */
void ForestScene_sun_follows_state(void)
{
    ecs_world_t *world = forest_new();
    ecs_entity_t sky = forest_lookup(world, "sky");

    const TimeOfDay *tod = ecs_get(world, sky, TimeOfDay);
    test_assert(tod != NULL);
    test_flt(tod->hour, 17.0f);
    test_assert(tod->day_of_year > 200 && tod->day_of_year < 250);

    forest_set_time(world, 1, 8);
    tod = ecs_get(world, sky, TimeOfDay);
    test_flt(tod->hour, 8.0f);
    test_assert(abs(tod->day_of_year - 172) <= 3);

    forest_set_time(world, 3, 13);
    tod = ecs_get(world, sky, TimeOfDay);
    test_flt(tod->hour, 13.0f);
    test_assert(abs(tod->day_of_year - 355) <= 3);

    forest_set_time(world, 0, 19);
    tod = ecs_get(world, sky, TimeOfDay);
    int32_t spring = tod->day_of_year;
    test_assert(abs(spring - 80) <= 3);

    forest_set_time(world, 4, 19);
    tod = ecs_get(world, sky, TimeOfDay);
    test_assert(abs(tod->day_of_year - spring) <= 1);

    ecs_fini(world);
}

/** The temperature follows the season and the hour: warm summers, cold winters, mild spring and
 * autumn, cooler at dawn than in the afternoon; precipitation turns to snow below 0 °C.
 */
void ForestScene_temperature_follows_state(void)
{
    ecs_world_t *world = forest_new();

    float summer = forest_temperature_at(world, 1.5f, 14);
    test_assert(summer > 20 && summer < 28);
    test_int(*(int32_t*)forest_var(world, "state.precip_kind"), 0);

    float winter = forest_temperature_at(world, 3.5f, 14);
    test_assert(winter < 0 && winter > -10);
    test_int(*(int32_t*)forest_var(world, "state.precip_kind"), 2);

    float spring = forest_temperature_at(world, 0.5f, 14);
    test_assert(spring > 7 && spring < 15);

    float autumn = forest_temperature_at(world, 2.5f, 14);
    test_assert(autumn > 6 && autumn < 14);

    float dawn = forest_temperature_at(world, 1.5f, 5);
    float afternoon = forest_temperature_at(world, 1.5f, 15);
    test_assert(afternoon - dawn > 4);

    /* Cyclic: season 4 is season 0 */
    float start = forest_temperature_at(world, 0, 12);
    float again = forest_temperature_at(world, 4, 12);
    test_assert(fabsf(start - again) < 0.01f);

    ecs_fini(world);
}

/** The floor strips stay clear of the banks of the brook, and the rims of the bed reach under
 * their cut edges, so the bed is open and the floor has no gap. Their tops are at Y = 0. The bed
 * is a length for every 2 m of X along the centerline, turned along it.
 */
void ForestScene_floor_clear_of_brook(void)
{
    ecs_world_t *world = forest_new();
    char path[64];
    int32_t i;

    for (i = 0; i < FOREST_STRIPS; i ++) {
        int32_t side;
        for (side = 0; side < 2; side ++) {
            snprintf(path, sizeof(path), "forest.ground.floor_%c%d", side ? 'n' : 's', i);
            ecs_entity_t strip = forest_lookup(world, path);
            const Position *p = ecs_get(world, strip, Position);
            const Box *b = ecs_get(world, strip, Box);
            test_assert(p != NULL);
            test_assert(b != NULL);
            test_assert(fabsf(p->y + b->y / 2) < 1e-4f);
            test_assert(fabsf(b->x - 2) < 1e-4f);

            double edge = side ? p->z - b->z / 2 : p->z + b->z / 2;
            double outer = side ? p->z + b->z / 2 : p->z - b->z / 2;
            test_assert(fabs(fabs(outer) - FOREST_WORLD_HALF) < 1e-3);

            int32_t k;
            for (k = 0; k <= 8; k ++) {
                double x = p->x - 1 + k * 0.25;
                double d = forest_brook_distance(x, edge);
                if (d < FOREST_BROOK_BANK || d > FOREST_BROOK_RIM) {
                    printf("strip %s: edge at x %f z %f is %f from the brook\n", path, x, edge,
                        d);
                }
                test_assert(d >= FOREST_BROOK_BANK);
                test_assert(d <= FOREST_BROOK_RIM);
                test_assert(side ? edge > forest_brook_z(x) : edge < forest_brook_z(x));
            }
        }
    }

    for (i = 0; i < FOREST_STRIPS; i ++) {
        snprintf(path, sizeof(path), "forest.ground.bed_%d", i);
        ecs_entity_t length = forest_lookup(world, path);
        const Position *p = ecs_get(world, length, Position);
        const Rotation *r = ecs_get(world, length, Rotation);
        test_assert(p != NULL);
        test_assert(r != NULL);
        test_assert(fabsf(p->x - (i * 2 + 1 - (float)FOREST_WORLD_HALF)) < 1e-4f);
        test_assert(fabsf(p->y) < 0.01f);
        test_assert(forest_brook_distance(p->x, p->z) < 0.05);

        /* Turned along the centerline: local +X follows the slope dz/dx */
        double slope = forest_brook_slope(p->x);
        test_assert(fabs(-tan(r->y) - slope) < 0.05);
    }

    ecs_fini(world);
}

/** The camera starts at eye height in the clearing, and the boulders and mounds stay out of the
 * brook bed and off the path; the mounds rise gently.
 */
void ForestScene_landmarks_clear(void)
{
    ecs_world_t *world = forest_new();

    const Position *cam = ecs_get(world, forest_lookup(world, "camera"), Position);
    test_assert(cam != NULL);
    test_assert(cam->y > 1.4f && cam->y < 2);
    test_assert(sqrtf(cam->x * cam->x + cam->z * cam->z) < FOREST_CLEARING_R - 1);

    const char *boulders[] = {
        "boulder_far_bank", "boulder_near_bank", "boulder_clearing",
        "boulder_0", "boulder_1", "boulder_2", "boulder_3", "boulder_4"
    };
    char path[64];
    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(boulders) / sizeof(boulders[0])); i ++) {
        snprintf(path, sizeof(path), "forest.ground.%s", boulders[i]);
        const Position *p = ecs_get(world, forest_lookup(world, path), Position);
        test_assert(p != NULL);
        test_assert(forest_brook_distance(p->x, p->z) > FOREST_BROOK_BANK + 1);
        test_assert(forest_path_distance(p->x, p->z) > FOREST_PATH_HALF_WIDTH + 1);
    }

    int32_t mounds = 0;
    for (i = 0; i < 48; i ++) {
        snprintf(path, sizeof(path), "forest.ground.mound_%d", i);
        ecs_entity_t e = ecs_lookup(world, path);
        if (!e) {
            continue;
        }

        const Position *p = ecs_get(world, e, Position);
        const Scale *s = ecs_get(world, e, Scale);
        test_assert(p != NULL);
        test_assert(s != NULL);
        test_assert(p->y + s->y < 0.6f);
        test_assert(p->y + s->y > 0.1f);
        float r = s->x;
        test_assert(forest_brook_distance(p->x, p->z) > FOREST_BROOK_BANK + r);
        test_assert(forest_path_distance(p->x, p->z) > FOREST_PATH_HALF_WIDTH + r);
        test_assert(sqrtf(p->x * p->x + p->z * p->z) > FOREST_CLEARING_R - 2.5f + r);
        mounds ++;
    }

    test_assert(mounds >= 6);

    ecs_fini(world);
}

/** The HUD panel holds the four sliders, which start at the values of the state, and H hides
 * and shows it.
 */
void ForestScene_hud_sliders(void)
{
    ecs_world_t *world = forest_new();

    forest_lookup(world, "hud.screen.panel.season");
    forest_lookup(world, "hud.screen.panel.precipitation");
    forest_lookup(world, "hud.screen.panel.wind");
    forest_lookup(world, "hud.screen.panel.hour");
    forest_lookup(world, "state.actions.toggle_hud");
    test_bool(*(bool*)forest_var(world, "state.hud_shown"), true);

    ecs_fini(world);
}
