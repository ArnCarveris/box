/**
 * @file LagoonRiver.c
 * @brief The river of the Coconut Lagoon scene, test/assets/scenes/lagoon.flecs: the canoe
 * paddles over the whole water between the banks and stops at the visible waterline, where the
 * wet beach slope of a bank meets the water, and no fixed scenery of any zone of the banks
 * stands in that water.
 *
 * The scene is a copy of etc/scenes/lagoon.flecs and its scripts in etc/scenes/lagoon, which a
 * change to the scene copies too. It runs without a render backend.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Path of the lagoon scene in the repository */
#define LAGOON_PATH "test/assets/scenes/lagoon.flecs"

/** Seconds a frame advances */
#define LAGOON_DT (1.0f / 6.0f)

/** Frames of steering that take the canoe from the middle to either beach */
#define LAGOON_STEER_FRAMES (30)

/** Half width of the hull and X of the outrigger float from canoe_x, and its half width
 * (HULL_HALF_W, FLOAT_X and FLOAT_HALF_W of the scene) */
#define LAGOON_HULL_HALF_W (0.42f)
#define LAGOON_FLOAT_X (-2.45f)
#define LAGOON_FLOAT_HALF_W (0.15f)

/** The canoe stops at most this far short of the waterline */
#define LAGOON_SHORE_GAP (0.3f)

/** X of the outer shore of a bank (shore strip of the banks), beyond it is the open sea */
#define LAGOON_SEA_X (33.0f)

/** Sections of scenery per bank (SECTIONS of the scene) */
#define LAGOON_SECTIONS (6)

/** Distances the scenery is checked at: LAGOON_SAMPLES steps of LAGOON_SAMPLE_STEP cover the
 * 2016 units after which the zones of all sections repeat */
#define LAGOON_SAMPLES (60)
#define LAGOON_SAMPLE_STEP (34.0)

/** Create the lagoon scene.
 *
 * @return The world
 */
static ecs_world_t* lagoon_new(void)
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

    return world;
}

/** Get a script variable of the scene.
 *
 * @param world The world
 * @param path Path of the variable
 * @return Pointer to its value
 */
static void* lagoon_var(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t var = ecs_lookup(world, path);
    test_assert(var != 0);
    ecs_value_t v = ecs_mut_var_get(world, var);
    test_assert(v.ptr != NULL);

    return v.ptr;
}

/** Get the world transform of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param m Receives the transform
 */
static void lagoon_transform(
    ecs_world_t *world,
    ecs_entity_t e,
    mat4 m)
{
    WorldTransform buf;
    const WorldTransform *wt = box_transform_world(world, e, &buf);
    test_assert(wt != NULL);
    glm_mat4_copy((vec4*)wt->m, m);
}

/** Get the X where the top face of the wet beach slope of a bank meets the water (y 0): the
 * visible waterline.
 *
 * @param world The world
 * @param side 0 for the left bank, 1 for the right one
 * @return The X of the waterline
 */
static float lagoon_waterline(
    ecs_world_t *world,
    int32_t side)
{
    char path[64];
    snprintf(path, sizeof(path), "banks.bank_%d.beach", side);
    ecs_entity_t beach = ecs_lookup(world, path);
    test_assert(beach != 0);
    const Box *box = ecs_get(world, beach, Box);
    test_assert(box != NULL);

    mat4 m;
    vec3 a, b;
    lagoon_transform(world, beach, m);
    glm_mat4_mulv3(m, (vec3){-box->x * 0.5f, box->y * 0.5f, 0}, 1, a);
    glm_mat4_mulv3(m, (vec3){box->x * 0.5f, box->y * 0.5f, 0}, 1, b);
    test_assert((a[1] < 0) != (b[1] < 0));

    return a[0] + (b[0] - a[0]) * (0 - a[1]) / (b[1] - a[1]);
}

/** Hold a key and steer the canoe toward a bank, checking every frame that neither the hull nor
 * the float crosses the waterline.
 *
 * @param world The scene
 * @param key KeyD to steer right, KeyA to steer left
 * @param frames The frames to steer for
 * @return The X where the canoe ends up
 */
static float lagoon_steer(
    ecs_world_t *world,
    Key key,
    int32_t frames)
{
    ecs_progress(world, LAGOON_DT);
    float left = lagoon_waterline(world, 0);
    float right = lagoon_waterline(world, 1);
    test_assert(left < -12);
    test_assert(right > 12);

    box_input_key(world, key, true);
    bool inside = true;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, LAGOON_DT);
        float x = *(float*)lagoon_var(world, "state.canoe_x");
        float hull = x + LAGOON_HULL_HALF_W;
        float flt = x + LAGOON_FLOAT_X - LAGOON_FLOAT_HALF_W;
        if (hull > right || flt < left) {
            printf("frame %d: canoe at %f, the hull reaches %f and the float %f, the waterlines "
                "are at %f and %f\n", i, x, hull, flt, left, right);
            inside = false;
        }
    }

    test_assert(inside);

    return *(float*)lagoon_var(world, "state.canoe_x");
}

/** Steering right, the hull stops at the waterline of the right beach, not short of it. */
void LagoonRiver_canoe_stops_at_right_waterline(void)
{
    ecs_world_t *world = lagoon_new();
    float x = lagoon_steer(world, KeyD, LAGOON_STEER_FRAMES);
    float right = lagoon_waterline(world, 1);
    float hull = x + LAGOON_HULL_HALF_W;
    if (right - hull > LAGOON_SHORE_GAP) {
        printf("the hull stops at %f, the waterline is at %f\n", hull, right);
    }

    test_assert(right - hull <= LAGOON_SHORE_GAP);
    test_assert(right - hull >= 0);
    test_assert(fabsf(*(float*)lagoon_var(world, "state.canoe_vx")) < 0.01f);
    ecs_fini(world);
}

/** Steering left, the float stops at the waterline of the left beach, not short of it. */
void LagoonRiver_canoe_stops_at_left_waterline(void)
{
    ecs_world_t *world = lagoon_new();
    float x = lagoon_steer(world, KeyA, LAGOON_STEER_FRAMES);
    float left = lagoon_waterline(world, 0);
    float flt = x + LAGOON_FLOAT_X - LAGOON_FLOAT_HALF_W;
    if (flt - left > LAGOON_SHORE_GAP) {
        printf("the float stops at %f, the waterline is at %f\n", flt, left);
    }

    test_assert(flt - left <= LAGOON_SHORE_GAP);
    test_assert(flt - left >= 0);
    test_assert(fabsf(*(float*)lagoon_var(world, "state.canoe_vx")) < 0.01f);
    ecs_fini(world);
}

/** Running onto a beach and staying there costs no life. Each side runs in its own scene: on its
 * way from one beach to the other the canoe would cross the lanes of the hazards. */
void LagoonRiver_shore_costs_no_life(void)
{
    Key keys[2] = {KeyD, KeyA};
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = lagoon_new();
        int32_t lives = *(int32_t*)lagoon_var(world, "state.lives");
        lagoon_steer(world, keys[k], 40);
        test_int(*(int32_t*)lagoon_var(world, "state.lives"), lives);
        ecs_fini(world);
    }
}

/** Jump the world to a distance, with no hazards spawning on the way.
 *
 * @param world The world
 * @param distance The distance
 */
static void lagoon_set_distance(
    ecs_world_t *world,
    double distance)
{
    test_int(ecs_mut_var_set_t(world, "state.next_row", ecs_f64_t, {1e12}), 0);
    test_int(ecs_mut_var_set_t(world, "state.distance", ecs_f64_t, {distance}), 0);
}

/** Get the reach across X of a sphere shape under a scaled and turned transform: the half width
 * of the ellipsoid it makes.
 *
 * @param world The world
 * @param e The shape
 * @param m Its world transform
 * @return The reach
 */
static float lagoon_ellipsoid_reach(
    ecs_world_t *world,
    ecs_entity_t e,
    mat4 m)
{
    const Sphere *sphere = ecs_get(world, e, Sphere);
    test_assert(sphere != NULL);

    return sphere->radius * sqrtf(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]);
}

/** Get the reach of a sphere shape across X: its radius times the largest horizontal scale.
 *
 * @param world The world
 * @param e The shape
 * @param m Its world transform
 * @return The reach
 */
static float lagoon_sphere_reach(
    ecs_world_t *world,
    ecs_entity_t e,
    mat4 m)
{
    const Sphere *sphere = ecs_get(world, e, Sphere);
    test_assert(sphere != NULL);

    return sphere->radius * glm_max(glm_vec3_norm(m[0]), glm_vec3_norm(m[2]));
}

/** Check that a shape of the scenery is out of the water between the waterlines.
 *
 * @param what Name of the shape
 * @param x World X of its center
 * @param reach Its reach across X
 * @param left The left waterline
 * @param right The right waterline
 * @return Whether it is out of the water
 */
static bool lagoon_out_of_water(
    const char *what,
    float x,
    float reach,
    float left,
    float right)
{
    bool out = x - reach >= right || x + reach <= left;
    if (!out) {
        printf("%s reaches over %f..%f, the water is %f..%f\n", what, x - reach, x + reach,
            left, right);
    }

    return out;
}

/** Check that the stilts or posts of a part of the scenery are out of the water.
 *
 * @param world The world
 * @param fmt Format of the path of a post, with its index
 * @param count Posts to look up, a missing one is skipped
 * @param left The left waterline
 * @param right The right waterline
 * @param clear Cleared if a post stands in the water
 * @return The posts checked
 */
static int32_t lagoon_posts_clear(
    ecs_world_t *world,
    const char *fmt,
    int32_t count,
    float left,
    float right,
    bool *clear)
{
    int32_t result = 0, k;
    for (k = 0; k < count; k ++) {
        char path[160];
        snprintf(path, sizeof(path), fmt, k);
        ecs_entity_t post = ecs_lookup(world, path);
        if (!post) {
            continue;
        }

        const Cylinder *c = ecs_get(world, post, Cylinder);
        test_assert(c != NULL);
        mat4 m;
        lagoon_transform(world, post, m);
        *clear &= lagoon_out_of_water(path, m[3][0], c->radius * glm_vec3_norm(m[0]), left,
            right);
        result ++;
    }

    return result;
}

/** Over a period of the zones no stilt of a hut, post or deck of a boardwalk, beached canoe,
 * surf foam, beached rock or islet stands in the water between the waterlines, and the islets
 * lie out at sea beyond the outer shore of the banks. Every section is checked, at distances
 * that show every zone in every place along the lagoon.
 *
 * Sections still growing in at the far end are scaled toward the bank, their islets are only
 * checked against the water.
 */
void LagoonRiver_scenery_clear_of_water(void)
{
    ecs_world_t *world = lagoon_new();
    ecs_progress(world, LAGOON_DT);
    int32_t huts = 0, posts = 0, decks = 0, canoes = 0, foam = 0, rocks = 0, isles = 0;
    bool clear = true;
    int32_t f, s, i, k;
    for (f = 0; f < LAGOON_SAMPLES; f ++) {
        lagoon_set_distance(world, f * LAGOON_SAMPLE_STEP);
        ecs_progress(world, LAGOON_DT);

        float left = lagoon_waterline(world, 0);
        float right = lagoon_waterline(world, 1);

        for (s = 0; s < 2; s ++) {
            for (i = 0; i < LAGOON_SECTIONS; i ++) {
                char path[160], fmt[160];
                snprintf(path, sizeof(path), "sections.sec_%d_%d.stretch", s, i);
                ecs_entity_t stretch = ecs_lookup(world, path);
                test_assert(stretch != 0);
                mat4 m;

                for (k = 0; k < 2; k ++) {
                    snprintf(fmt, sizeof(fmt),
                        "sections.sec_%d_%d.stretch.hut_%d.house.stilt_%%d", s, i, k);
                    huts += lagoon_posts_clear(world, fmt, 4, left, right, &clear);
                }

                for (k = 0; k < 5; k ++) {
                    snprintf(fmt, sizeof(fmt),
                        "sections.sec_%d_%d.stretch.jetty.post_%d_%%d", s, i, k);
                    posts += lagoon_posts_clear(world, fmt, 2, left, right, &clear);
                }

                snprintf(path, sizeof(path), "sections.sec_%d_%d.stretch.jetty.deck", s, i);
                ecs_entity_t deck = ecs_lookup(world, path);
                if (deck) {
                    const Box *box = ecs_get(world, deck, Box);
                    test_assert(box != NULL);
                    lagoon_transform(world, deck, m);
                    clear &= lagoon_out_of_water(path, m[3][0],
                        box->x * 0.5f * glm_vec3_norm(m[0]), left, right);
                    decks ++;
                }

                for (k = 0; k < 2; k ++) {
                    snprintf(path, sizeof(path),
                        "sections.sec_%d_%d.stretch.canoe_%d.turn.boat.hull", s, i, k);
                    ecs_entity_t hull = ecs_lookup(world, path);
                    if (!hull) {
                        continue;
                    }

                    lagoon_transform(world, hull, m);
                    clear &= lagoon_out_of_water(path, m[3][0],
                        lagoon_ellipsoid_reach(world, hull, m), left, right);
                    canoes ++;
                }

                for (k = 0; k < 4; k ++) {
                    snprintf(path, sizeof(path), "sections.sec_%d_%d.stretch.foam_%d", s, i, k);
                    ecs_entity_t patch = ecs_lookup(world, path);
                    if (!patch) {
                        continue;
                    }

                    lagoon_transform(world, patch, m);
                    clear &= lagoon_out_of_water(path, m[3][0],
                        lagoon_ellipsoid_reach(world, patch, m), left, right);
                    foam ++;
                }

                for (k = 0; k < 4; k ++) {
                    snprintf(path, sizeof(path), "sections.sec_%d_%d.stretch.rock_%d", s, i, k);
                    ecs_entity_t rock = ecs_lookup(world, path);
                    if (!rock) {
                        continue;
                    }

                    lagoon_transform(world, rock, m);
                    clear &= lagoon_out_of_water(path, m[3][0],
                        lagoon_sphere_reach(world, rock, m), left, right);
                    rocks ++;
                }

                snprintf(path, sizeof(path),
                    "sections.sec_%d_%d.stretch.isle.land.shallows", s, i);
                ecs_entity_t isle = ecs_lookup(world, path);
                if (isle) {
                    lagoon_transform(world, isle, m);
                    float reach = lagoon_sphere_reach(world, isle, m);
                    clear &= lagoon_out_of_water(path, m[3][0], reach, left, right);

                    const Scale *scale = ecs_get(world, ecs_get_parent(world, stretch), Scale);
                    if (!scale || fabsf(scale->x - 1) < 1e-3f) {
                        if (fabsf(m[3][0]) - reach < LAGOON_SEA_X) {
                            printf("%s reaches in to %f, the sea starts at %f\n", path,
                                fabsf(m[3][0]) - reach, LAGOON_SEA_X);
                            clear = false;
                        }
                    }

                    isles ++;
                }
            }
        }
    }

    test_assert(huts > 0);
    test_assert(posts > 0);
    test_assert(decks > 0);
    test_assert(canoes > 0);
    test_assert(foam > 0);
    test_assert(rocks > 0);
    test_assert(isles > 0);
    test_assert(clear);
    ecs_fini(world);
}
