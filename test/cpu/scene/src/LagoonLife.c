/**
 * @file LagoonLife.c
 * @brief The life on and above the water of the Coconut Lagoon scene,
 * test/assets/scenes/lagoon.flecs: the wake follows the canoe and stops when the game is over,
 * flotsam drifts flat in the water and is pooled, fish jump away from the canoe's path and
 * seabirds circle high ahead with beating wings.
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
#define LIFE_PATH "test/assets/scenes/lagoon.flecs"

/** Seconds a frame advances, the scene scripts run again every frame */
#define LIFE_DT (1.0f / 6.0f)

/** Half width of the navigable water (WATER_HALF of the scene) */
#define LIFE_WATER_HALF (12.775f)

/** X of the outrigger float from canoe_x and the half widths of the float and the hull
 * (FLOAT_X, FLOAT_HALF_W and HULL_HALF_W of the scene) */
#define LIFE_FLOAT_X (-2.45f)
#define LIFE_FLOAT_HALF_W (0.15f)
#define LIFE_HULL_HALF_W (0.42f)

/** Flotsam slots (FLOTSAM of the scene) and the span they wrap over (LF_SPAN) */
#define LIFE_FLOTSAM (9)
#define LIFE_FLOTSAM_SPAN (120.0f)

/** No flotsam shape reaches above this height: it lies flat in the water */
#define LIFE_FLOTSAM_TOP (0.1f)

/** A jumping fish stays at least this far ahead of the canoe */
#define LIFE_FISH_AHEAD (10.0f)

/** Frames the fish test runs (20 seconds) and the jumps it sees at least */
#define LIFE_FISH_FRAMES (120)
#define LIFE_FISH_JUMPS (3)

/** Seabirds (BIRDS of the scene) and the height they keep above */
#define LIFE_BIRDS (3)
#define LIFE_BIRD_Y (8.0f)

/** Game over (GS_OVER of the scene) */
#define LIFE_GS_OVER (2)

/** Most entities a test collects */
#define LIFE_MAX_ENTITIES (512)

/** Load the lagoon scene with frames of LIFE_DT.
 *
 * @return The world of the scene
 */
static ecs_world_t* life_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = box_app_locate(LIFE_PATH);
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_new_from_path_w_sep(world, 0, LIFE_PATH, "/", "/"),
        .filename = path
    });
    ecs_os_free(path);
    test_assert(script != 0);
    const EcsScript *loaded = ecs_get(world, script, EcsScript);
    test_assert(loaded != NULL);
    test_assert(loaded->error == false);
    ecs_singleton_set(world, App, {.delta_time = LIFE_DT});

    return world;
}

/** Run the scene for a number of frames.
 *
 * @param world The scene
 * @param frames The frames
 */
static void life_run(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, LIFE_DT);
    }
}

/** Get a script variable of the scene.
 *
 * @param world The scene
 * @param path Path of the variable
 * @param var Receives the variable entity, may be NULL
 * @return Pointer to its value
 */
static void* life_var(
    ecs_world_t *world,
    const char *path,
    ecs_entity_t *var)
{
    ecs_entity_t e = ecs_lookup(world, path);
    test_assert(e != 0);
    ecs_value_t v = ecs_mut_var_get(world, e);
    test_assert(v.ptr != NULL);
    if (var) {
        *var = e;
    }

    return v.ptr;
}

/** Set an integer script variable of the scene.
 *
 * @param world The scene
 * @param path Path of the variable
 * @param value The value
 */
static void life_set_int(
    ecs_world_t *world,
    const char *path,
    int32_t value)
{
    ecs_entity_t var;
    int32_t *ptr = life_var(world, path, &var);
    if (*ptr != value) {
        *ptr = value;
        ecs_mut_var_modified(world, var);
    }
}

/** Look up an entity of the scene.
 *
 * @param world The scene
 * @param path Path of the entity
 * @return The entity
 */
static ecs_entity_t life_lookup(
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

/** Get the world transform of an entity.
 *
 * @param world The scene
 * @param e The entity
 * @param m Receives the transform
 */
static void life_transform(
    ecs_world_t *world,
    ecs_entity_t e,
    mat4 m)
{
    WorldTransform buf;
    const WorldTransform *wt = box_transform_world(world, e, &buf);
    test_assert(wt != NULL);
    glm_mat4_copy((vec4*)wt->m, m);
}

/** Get the local bounds of the shape of an entity.
 *
 * @param world The scene
 * @param e The entity
 * @param min Receives the lower corner
 * @param max Receives the upper corner
 * @return Whether the entity has a shape the scene's life uses
 */
static bool life_shape_bounds(
    ecs_world_t *world,
    ecs_entity_t e,
    vec3 min,
    vec3 max)
{
    const Box *box = ecs_get(world, e, Box);
    if (box) {
        glm_vec3_copy((vec3){-box->x / 2, -box->y / 2, -box->z / 2}, min);
        glm_vec3_copy((vec3){box->x / 2, box->y / 2, box->z / 2}, max);
        return true;
    }

    const Sphere *sphere = ecs_get(world, e, Sphere);
    if (sphere) {
        float r = sphere->radius;
        glm_vec3_copy((vec3){-r, -r, -r}, min);
        glm_vec3_copy((vec3){r, r, r}, max);
        return true;
    }

    const Cylinder *cylinder = ecs_get(world, e, Cylinder);
    if (cylinder) {
        float r = cylinder->radius, h = cylinder->length / 2;
        glm_vec3_copy((vec3){-r, -h, -r}, min);
        glm_vec3_copy((vec3){r, h, r}, max);
        return true;
    }

    const Cone *cone = ecs_get(world, e, Cone);
    if (cone) {
        float r = cone->radius, h = cone->length / 2;
        glm_vec3_copy((vec3){-r, -h, -r}, min);
        glm_vec3_copy((vec3){r, h, r}, max);
        return true;
    }

    return false;
}

/** Get the world bounds of the shape of an entity.
 *
 * @param world The scene
 * @param e The entity
 * @param lo Receives the lower corner
 * @param hi Receives the upper corner
 * @return Whether the entity has a shape
 */
static bool life_shape_world(
    ecs_world_t *world,
    ecs_entity_t e,
    vec3 lo,
    vec3 hi)
{
    vec3 min, max;
    if (!life_shape_bounds(world, e, min, max)) {
        return false;
    }

    mat4 m;
    life_transform(world, e, m);
    int32_t c;
    for (c = 0; c < 8; c ++) {
        vec3 p = {
            c & 1 ? max[0] : min[0],
            c & 2 ? max[1] : min[1],
            c & 4 ? max[2] : min[2]
        };
        vec3 w;
        glm_mat4_mulv3(m, p, 1, w);
        if (!c) {
            glm_vec3_copy(w, lo);
            glm_vec3_copy(w, hi);
        } else {
            glm_vec3_minv(lo, w, lo);
            glm_vec3_maxv(hi, w, hi);
        }
    }

    return true;
}

/** Collect the entities in the tree of an entity, depth first.
 *
 * @param world The scene
 * @param parent The root of the tree, not collected
 * @param out Receives the entities
 * @param count The number of entities in out, updated
 */
static void life_collect(
    ecs_world_t *world,
    ecs_entity_t parent,
    ecs_entity_t *out,
    int32_t *count)
{
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            test_assert(*count < LIFE_MAX_ENTITIES);
            out[(*count) ++] = it.entities[i];
            life_collect(world, it.entities[i], out, count);
        }
    }
}

/** Get the emitter of a wake layer.
 *
 * @param world The scene
 * @param path Path of the layer
 * @return The emitter
 */
static const ParticleEmitter* life_emitter(
    ecs_world_t *world,
    const char *path)
{
    const ParticleEmitter *em = ecs_get(world, life_lookup(world, path), ParticleEmitter);
    test_assert(em != NULL);

    return em;
}

/** While the canoe paddles the wake emits foam that flows back at the world's pace from the
 * stern of the hull, of the float and from the bow, and follows the canoe across the water. Once
 * the game is over, it stops.
 */
void LagoonLife_wake_follows_canoe(void)
{
    ecs_world_t *world = life_new();
    life_run(world, 3);

    const char *layers[4] = {"wake.hull", "wake.float", "wake.bow_0", "wake.bow_1"};
    float offsets[4] = {0, LIFE_FLOAT_X, -0.12f, 0.12f};
    float speed = *(float*)life_var(world, "state.speed", NULL);
    int32_t k;
    for (k = 0; k < 4; k ++) {
        const ParticleEmitter *em = life_emitter(world, layers[k]);
        test_assert(em->rate > 0);
        test_assert(em->speed.max <= speed * 1.1f);
        test_assert(em->speed.min >= speed * 0.8f);
        test_assert(em->direction[2] > 0.9f);
    }

    /* Steer right for a while, the layers move along */
    box_input_key(world, KeyD, true);
    life_run(world, 4);
    box_input_key(world, KeyD, false);
    life_run(world, 1);
    float x = *(float*)life_var(world, "state.canoe_x", NULL);
    test_assert(x > 1);
    for (k = 0; k < 4; k ++) {
        mat4 m;
        life_transform(world, life_lookup(world, layers[k]), m);
        test_flt(m[3][0], x + offsets[k]);
        test_assert(fabsf(m[3][1]) < 0.1f);
    }

    life_set_int(world, "state.game_state", LIFE_GS_OVER);
    life_run(world, 2);
    for (k = 0; k < 4; k ++) {
        test_flt(life_emitter(world, layers[k])->rate, 0);
    }

    ecs_fini(world);
}

/** Flotsam lies flat in the water, between the waterlines, and is pooled: while it wraps back
 * up the lagoon the same entities stay alive and no new ones appear.
 */
void LagoonLife_flotsam_pooled_in_water(void)
{
    ecs_world_t *world = life_new();
    life_run(world, 2);

    static ecs_entity_t before[LIFE_MAX_ENTITIES];
    static ecs_entity_t after[LIFE_MAX_ENTITIES];
    int32_t count_before = 0, count_after = 0, i, f;
    ecs_entity_t root = life_lookup(world, "flotsam");
    life_collect(world, root, before, &count_before);
    test_assert(count_before >= LIFE_FLOTSAM);

    bool inside = true;
    int32_t shapes = 0;
    for (f = 0; f < 4; f ++) {
        for (i = 0; i < count_before; i ++) {
            vec3 lo, hi;
            if (!life_shape_world(world, before[i], lo, hi)) {
                continue;
            }

            shapes ++;
            if (hi[1] > LIFE_FLOTSAM_TOP || lo[0] < -LIFE_WATER_HALF ||
                hi[0] > LIFE_WATER_HALF)
            {
                char *path = ecs_get_path(world, before[i]);
                printf("%s spans %f..%f in x and reaches up to %f\n", path, lo[0], hi[0],
                    hi[1]);
                ecs_os_free(path);
                inside = false;
            }
        }

        life_run(world, 20);
    }

    test_assert(shapes > LIFE_FLOTSAM * 4);
    test_assert(inside);

    /* 80 frames at speed 9 and up scroll the world by more than the span, every item wrapped */
    double distance = *(double*)life_var(world, "state.distance", NULL);
    test_assert(distance > LIFE_FLOTSAM_SPAN);
    life_collect(world, root, after, &count_after);
    test_int(count_after, count_before);
    for (i = 0; i < count_before; i ++) {
        int32_t j;
        test_assert(ecs_is_alive(world, before[i]));
        for (j = 0; j < count_after; j ++) {
            if (after[j] == before[i]) {
                break;
            }
        }

        test_assert(j < count_after);
    }

    ecs_fini(world);
}

/** Now and then a fish leaps out of the water ahead of the canoe, never in the canoe's path and
 * always inside the water. The canoe is kept afloat so that the game runs on.
 */
void LagoonLife_fish_jumps_clear_of_canoe(void)
{
    ecs_world_t *world = life_new();
    int32_t jumps = 0, i;
    bool was_live = false, clear = true;
    for (i = 0; i < LIFE_FISH_FRAMES; i ++) {
        life_set_int(world, "state.lives", 3);
        ecs_progress(world, LIFE_DT);
        bool live = *(bool*)life_var(world, "state.fish_live", NULL);
        if (live) {
            if (!was_live) {
                jumps ++;
            }

            test_assert(ecs_lookup(world, "fish_jump.pitch.model") != 0);
            mat4 m;
            life_transform(world, life_lookup(world, "fish_jump"), m);
            float x = *(float*)life_var(world, "state.canoe_x", NULL);
            bool in_path = m[3][0] > x + LIFE_FLOAT_X - LIFE_FLOAT_HALF_W - 0.5f &&
                m[3][0] < x + LIFE_HULL_HALF_W + 0.5f;
            if (in_path || m[3][2] > -LIFE_FISH_AHEAD ||
                fabsf(m[3][0]) > LIFE_WATER_HALF - 0.5f)
            {
                printf("frame %d: the fish is at %f, %f, the canoe at %f\n", i, m[3][0],
                    m[3][2], x);
                clear = false;
            }
        }

        was_live = live;
    }

    test_assert(jumps >= LIFE_FISH_JUMPS);
    test_assert(clear);
    ecs_fini(world);
}

/** The seabirds fly high over the lagoon ahead, circle head first and beat their wings. */
void LagoonLife_birds_circle_high(void)
{
    ecs_world_t *world = life_new();
    life_run(world, 2);

    vec3 start[LIFE_BIRDS];
    float wing[LIFE_BIRDS];
    int32_t b;
    char path[128];
    for (b = 0; b < LIFE_BIRDS; b ++) {
        snprintf(path, sizeof(path), "seabirds.circle_%d.bird", b);
        mat4 m;
        life_transform(world, life_lookup(world, path), m);
        glm_vec3_copy(m[3], start[b]);
        test_assert(start[b][1] > LIFE_BIRD_Y);
        test_assert(start[b][2] < -20);

        snprintf(path, sizeof(path), "seabirds.circle_%d.bird.bank.gull.wing_r", b);
        ecs_entity_t w = life_lookup(world, path);
        test_assert(ecs_has(world, w, Animator));
        wing[b] = ecs_get(world, w, Rotation)->z;
    }

    life_run(world, 3);
    for (b = 0; b < LIFE_BIRDS; b ++) {
        snprintf(path, sizeof(path), "seabirds.circle_%d.bird", b);
        mat4 m;
        life_transform(world, life_lookup(world, path), m);
        test_assert(m[3][1] > LIFE_BIRD_Y);
        test_assert(glm_vec3_distance(m[3], start[b]) > 0.5f);

        /* The bird flies nose first: its nose points along -Z of its transform */
        vec3 moved, nose;
        glm_vec3_sub(m[3], start[b], moved);
        moved[1] = 0;
        glm_vec3_normalize(moved);
        glm_vec3_negate_to(m[2], nose);
        nose[1] = 0;
        glm_vec3_normalize(nose);
        test_assert(glm_vec3_dot(moved, nose) > 0.9f);

        snprintf(path, sizeof(path), "seabirds.circle_%d.bird.bank.gull.wing_r", b);
        float z = ecs_get(world, life_lookup(world, path), Rotation)->z;
        test_assert(fabsf(z - wing[b]) > 1e-4f);
    }

    ecs_fini(world);
}
