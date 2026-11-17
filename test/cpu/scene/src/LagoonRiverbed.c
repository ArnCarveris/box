/**
 * @file LagoonRiverbed.c
 * @brief The riverbed of the Coconut Lagoon scene, test/assets/scenes/lagoon.flecs: the details
 * on the sand floor under the water stay low on the floor, spread across the river, flow past
 * with the world and are pooled, so no entity is made or deleted as they wrap.
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
#define RIVERBED_PATH "test/assets/scenes/lagoon.flecs"

/** Seconds a frame advances, the scene scripts run again every frame */
#define RIVERBED_DT (1.0f / 6.0f)

/** Top of the sand floor (RB_FLOOR_Y of the scene) */
#define RIVERBED_FLOOR_Y (-1.7f)

/** Nothing on the riverbed reaches above this height */
#define RIVERBED_TOP_Y (-1.2f)

/** Half width of the floor between the bank shelves */
#define RIVERBED_HALF (11.0f)

/** Span the riverbed details wrap over (RB_SPAN of the scene) */
#define RIVERBED_SPAN (56.0f)

/** Z behind the camera where the details wrap (RB_BEHIND of the scene) */
#define RIVERBED_BEHIND (14.0f)

/** Most shapes the riverbed may have live */
#define RIVERBED_MAX_SHAPES (150)

/** Most entities a test collects */
#define RIVERBED_MAX_ENTITIES (1024)

/** Groups of riverbed items that sit on the ground and flow with it */
static const char *riverbed_groups[] = {
    "riverbed.ripples", "riverbed.corals", "riverbed.seagrass", "riverbed.starfish",
    "riverbed.shells", "riverbed.stones"
};

#define RIVERBED_GROUPS (6)

/** Load the lagoon scene with frames of RIVERBED_DT.
 *
 * @return The world of the scene
 */
static ecs_world_t* riverbed_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = box_app_locate(RIVERBED_PATH);
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_new_from_path_w_sep(world, 0, RIVERBED_PATH, "/", "/"),
        .filename = path
    });
    ecs_os_free(path);
    test_assert(script != 0);
    const EcsScript *loaded = ecs_get(world, script, EcsScript);
    test_assert(loaded != NULL);
    test_assert(loaded->error == false);
    ecs_singleton_set(world, App, {.delta_time = RIVERBED_DT});

    return world;
}

/** Run the scene for a number of frames.
 *
 * @param world The scene
 * @param frames The frames
 */
static void riverbed_run(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, RIVERBED_DT);
    }
}

/** Get the distance the world scrolled by.
 *
 * @param world The scene
 * @return The distance
 */
static double riverbed_distance(
    ecs_world_t *world)
{
    ecs_entity_t var = ecs_lookup(world, "state.distance");
    test_assert(var != 0);
    ecs_value_t v = ecs_mut_var_get(world, var);
    test_assert(v.ptr != NULL);

    return *(double*)v.ptr;
}

/** Look up an entity of the scene.
 *
 * @param world The scene
 * @param path Path of the entity
 * @return The entity
 */
static ecs_entity_t riverbed_lookup(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Get the world transform of an entity.
 *
 * @param world The scene
 * @param e The entity
 * @param m Receives the transform
 */
static void riverbed_transform(
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
 * @return Whether the entity has a shape the riverbed uses
 */
static bool riverbed_shape_bounds(
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

    const HemiSphere *hemi = ecs_get(world, e, HemiSphere);
    if (hemi) {
        float r = hemi->radius;
        glm_vec3_copy((vec3){-r, 0, -r}, min);
        glm_vec3_copy((vec3){r, r, r}, max);
        return true;
    }

    const Cone *cone = ecs_get(world, e, Cone);
    if (cone) {
        float r = cone->radius, h = cone->length / 2;
        glm_vec3_copy((vec3){-r, -h, -r}, min);
        glm_vec3_copy((vec3){r, h, r}, max);
        return true;
    }

    const Frustum *frustum = ecs_get(world, e, Frustum);
    if (frustum) {
        float r = glm_max(frustum->radius_bottom, frustum->radius_top);
        float h = frustum->length / 2;
        glm_vec3_copy((vec3){-r, -h, -r}, min);
        glm_vec3_copy((vec3){r, h, r}, max);
        return true;
    }

    const Star *star = ecs_get(world, e, Star);
    if (star) {
        float r = star->radius;
        glm_vec3_copy((vec3){-r, -r, 0}, min);
        glm_vec3_copy((vec3){r, r, 0}, max);
        return true;
    }

    return false;
}

/** Collect the entities in the tree of an entity, depth first.
 *
 * @param world The scene
 * @param parent The root of the tree, not collected
 * @param out Receives the entities
 * @param count The number of entities in out, updated
 */
static void riverbed_collect(
    ecs_world_t *world,
    ecs_entity_t parent,
    ecs_entity_t *out,
    int32_t *count)
{
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            test_assert(*count < RIVERBED_MAX_ENTITIES);
            out[(*count) ++] = it.entities[i];
            riverbed_collect(world, it.entities[i], out, count);
        }
    }
}

/** Get the lowest and highest world Y of the shape of an entity.
 *
 * @param world The scene
 * @param e The entity
 * @param lo Receives the lowest Y
 * @param hi Receives the highest Y
 * @return Whether the entity has a shape
 */
static bool riverbed_shape_y(
    ecs_world_t *world,
    ecs_entity_t e,
    float *lo,
    float *hi)
{
    vec3 min, max;
    if (!riverbed_shape_bounds(world, e, min, max)) {
        return false;
    }

    mat4 m;
    riverbed_transform(world, e, m);
    int32_t c;
    for (c = 0; c < 8; c ++) {
        vec3 p = {
            c & 1 ? max[0] : min[0],
            c & 2 ? max[1] : min[1],
            c & 4 ? max[2] : min[2]
        };
        vec3 w;
        glm_mat4_mulv3(m, p, 1, w);
        if (!c || w[1] < *lo) {
            *lo = w[1];
        }

        if (!c || w[1] > *hi) {
            *hi = w[1];
        }
    }

    return true;
}

/** The riverbed has a modest number of shapes, and over many frames, while its details wrap
 * back up the lagoon, the same entities stay alive and no new ones appear.
 */
void LagoonRiverbed_pooled(void)
{
    ecs_world_t *world = riverbed_new();
    riverbed_run(world, 2);

    static ecs_entity_t before[RIVERBED_MAX_ENTITIES];
    static ecs_entity_t after[RIVERBED_MAX_ENTITIES];
    int32_t count_before = 0, count_after = 0, shapes = 0, i;
    ecs_entity_t root = riverbed_lookup(world, "riverbed");
    riverbed_collect(world, root, before, &count_before);
    for (i = 0; i < count_before; i ++) {
        vec3 min, max;
        if (riverbed_shape_bounds(world, before[i], min, max)) {
            shapes ++;
        }
    }

    printf("the riverbed has %d entities and %d shapes\n", count_before, shapes);
    test_assert(shapes >= 80);
    test_assert(shapes <= RIVERBED_MAX_SHAPES);

    /* 40 frames at speed 9 and up scroll the world by more than 60 units, most details wrap */
    riverbed_run(world, 40);
    test_assert(riverbed_distance(world) > 60);
    riverbed_collect(world, root, after, &count_after);
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

/** Every riverbed shape sits on the sand floor, below RIVERBED_TOP_Y so that it never reaches
 * up toward the hazards at the surface, and within the river between the bank shelves.
 */
void LagoonRiverbed_stays_low(void)
{
    ecs_world_t *world = riverbed_new();
    static ecs_entity_t entities[RIVERBED_MAX_ENTITIES];
    bool low = true;
    int32_t checked = 0, frame;
    for (frame = 0; frame < 3; frame ++) {
        riverbed_run(world, 5);
        int32_t count = 0, i;
        riverbed_collect(world, riverbed_lookup(world, "riverbed"), entities, &count);
        for (i = 0; i < count; i ++) {
            float lo, hi;
            if (!riverbed_shape_y(world, entities[i], &lo, &hi)) {
                continue;
            }

            mat4 m;
            riverbed_transform(world, entities[i], m);
            if (hi > RIVERBED_TOP_Y || lo < RIVERBED_FLOOR_Y - 1.0f ||
                fabsf(m[3][0]) > RIVERBED_HALF)
            {
                char *path = ecs_get_path(world, entities[i]);
                printf("%s spans y %f..%f at x %f\n", path, lo, hi, m[3][0]);
                ecs_os_free(path);
                low = false;
            }

            checked ++;
        }
    }

    test_assert(checked > 200);
    test_assert(low);
    ecs_fini(world);
}

/** The riverbed details spread over the full width of the river, on both sides of the
 * channel, and over the whole span along it.
 */
void LagoonRiverbed_spreads(void)
{
    ecs_world_t *world = riverbed_new();
    riverbed_run(world, 2);

    float min_x = 0, max_x = 0, min_z = 0, max_z = 0;
    int32_t left = 0, right = 0, middle = 0, g;
    for (g = 0; g < RIVERBED_GROUPS; g ++) {
        ecs_iter_t it = ecs_children(world, riverbed_lookup(world, riverbed_groups[g]));
        while (ecs_children_next(&it)) {
            int32_t i;
            for (i = 0; i < it.count; i ++) {
                mat4 m;
                riverbed_transform(world, it.entities[i], m);
                float x = m[3][0], z = m[3][2];
                min_x = glm_min(min_x, x);
                max_x = glm_max(max_x, x);
                min_z = glm_min(min_z, z);
                max_z = glm_max(max_z, z);
                left += x < -3;
                right += x > 3;
                middle += fabsf(x) <= 3;
            }
        }
    }

    printf("riverbed x %f..%f, z %f..%f, %d left, %d middle, %d right\n",
        min_x, max_x, min_z, max_z, left, middle, right);
    test_assert(min_x < -7);
    test_assert(max_x > 7);
    test_assert(left > 5);
    test_assert(right > 5);
    test_assert(middle > 5);
    test_assert(max_z > RIVERBED_BEHIND - 20);
    test_assert(min_z < RIVERBED_BEHIND - RIVERBED_SPAN + 20);
    ecs_fini(world);
}

/** The riverbed details flow with the ground: from one frame to the next a detail moves toward
 * +Z by the change of distance and keeps its X, or it wrapped from behind the camera to the far
 * end of the span, where it is too small to see.
 */
void LagoonRiverbed_flows(void)
{
    ecs_world_t *world = riverbed_new();
    static ecs_entity_t items[RIVERBED_MAX_ENTITIES];
    static vec3 pos[RIVERBED_MAX_ENTITIES];
    bool flows = true;
    int32_t wrapped = 0, moved = 0, frame;

    riverbed_run(world, 2);
    for (frame = 0; frame < 30; frame ++) {
        int32_t count = 0, g, i;
        for (g = 0; g < RIVERBED_GROUPS; g ++) {
            ecs_iter_t it = ecs_children(world, riverbed_lookup(world, riverbed_groups[g]));
            while (ecs_children_next(&it)) {
                for (i = 0; i < it.count; i ++) {
                    items[count] = it.entities[i];
                    mat4 m;
                    riverbed_transform(world, it.entities[i], m);
                    glm_vec3_copy(m[3], pos[count]);
                    count ++;
                }
            }
        }

        double da = riverbed_distance(world);
        riverbed_run(world, 1);
        double db = riverbed_distance(world);
        float step = (float)(db - da);
        test_assert(step > 0.5f);

        for (i = 0; i < count; i ++) {
            mat4 m;
            riverbed_transform(world, items[i], m);
            float dz = m[3][2] - pos[i][2];
            if (fabsf(dz - step) < 1e-3f && fabsf(m[3][0] - pos[i][0]) < 1e-3f) {
                moved ++;
                continue;
            }

            /* A wrap: from near the wrap behind the camera to the far end, small there */
            float size = glm_vec3_norm(m[0]);
            if (pos[i][2] > RIVERBED_BEHIND - step - 1e-3f &&
                m[3][2] < RIVERBED_BEHIND - RIVERBED_SPAN + step + 1e-3f &&
                size < 0.05f)
            {
                wrapped ++;
                continue;
            }

            char *path = ecs_get_path(world, items[i]);
            printf("%s went from {%f, %f} to {%f, %f} at scale %f, the ground moved %f\n",
                path, pos[i][0], pos[i][2], m[3][0], m[3][2], size, step);
            ecs_os_free(path);
            flows = false;
        }
    }

    printf("%d moves, %d wraps\n", moved, wrapped);
    test_assert(flows);
    test_assert(wrapped > 5);
    ecs_fini(world);
}

/** The details grow in at the far end of the span and are full size near the canoe. */
void LagoonRiverbed_grows_in(void)
{
    ecs_world_t *world = riverbed_new();
    riverbed_run(world, 2);

    int32_t far = 0, near = 0, g;
    bool grows = true;
    for (g = 0; g < RIVERBED_GROUPS; g ++) {
        ecs_iter_t it = ecs_children(world, riverbed_lookup(world, riverbed_groups[g]));
        while (ecs_children_next(&it)) {
            int32_t i;
            for (i = 0; i < it.count; i ++) {
                mat4 m;
                riverbed_transform(world, it.entities[i], m);
                float z = m[3][2], size = glm_vec3_norm(m[0]);
                if (z < RIVERBED_BEHIND - RIVERBED_SPAN + 2) {
                    far ++;
                    if (size > 0.1f) {
                        printf("a detail at z %f has a scale of %f\n", z, size);
                        grows = false;
                    }
                } else if (z > -26 && z < RIVERBED_BEHIND) {
                    near ++;
                    if (size < 0.3f) {
                        printf("a detail at z %f has a scale of %f\n", z, size);
                        grows = false;
                    }
                }
            }
        }
    }

    test_assert(near > 10);
    test_assert(grows);
    (void)far;
    ecs_fini(world);
}
