/**
 * @file LagoonBanks.c
 * @brief The banks of the Coconut Lagoon scene, test/assets/scenes/lagoon.flecs: the sand of the
 * banks and of the lagoon floor flows past with the scenery, the sand strips of a bank join
 * without a step, the bank props stand on the sand and the bank sections take turns between
 * zones, changing zone only as they jump back up the lagoon.
 *
 * The scene is a copy of etc/scenes/lagoon.flecs and its scripts in etc/scenes/lagoon, which a
 * change to the scene copies too. It runs without a render backend.
 *
 * The game scrolls the world toward +Z by distance, so a point fixed to the ground moves by the
 * change of distance between frames.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Path of the lagoon scene in the repository */
#define LAGOON_PATH "test/assets/scenes/lagoon.flecs"

/** Seconds a frame advances: the scene scripts run again every frame, so the tests take few
 * long frames, all before the first hazards show up at FIRST_HAZARD_TIME */
#define LAGOON_DT (1.0f / 6.0f)

/** X of the bank line (BANK_X of the scene) */
#define LAGOON_BANK_X (14.0f)

/** Texture coordinates that differ by less than this show the same texel */
#define LAGOON_UV_EPSILON (1e-3f)

/** A mirrored texture repeats every 2 texture widths */
#define LAGOON_UV_PERIOD (2.0f)

/** Sections of scenery per bank (SECTIONS of the scene) */
#define LAGOON_SECTIONS (6)

/** Z behind the camera where scenery jumps back up the lagoon (SCENERY_BEHIND of the scene) */
#define LAGOON_BEHIND (18.0f)

/** Distance after which the zones come by again along the lagoon: 4 zones of 4 sections of 42
 * units */
#define LAGOON_ZONE_CYCLE (672.0)

/** The zones of the bank sections */
#define LAGOON_ZONES (4)

/** Loaded lagoon scene, run for a number of frames */
typedef struct {
    ecs_world_t *world; /**< The world of the scene */
    int32_t frames;     /**< Frames the scene ran */
} lagoon_t;

/** Load the lagoon scene with frames of LAGOON_DT.
 *
 * @param l Receives the scene
 */
static void lagoon_new(
    lagoon_t *l)
{
    l->world = ecs_init();
    l->frames = 0;
    ECS_IMPORT(l->world, Box);

    char *path = box_app_locate(LAGOON_PATH);
    ecs_entity_t script = ecs_script(l->world, {
        .entity = ecs_new_from_path_w_sep(l->world, 0, LAGOON_PATH, "/", "/"),
        .filename = path
    });
    ecs_os_free(path);
    test_assert(script != 0);
    const EcsScript *loaded = ecs_get(l->world, script, EcsScript);
    test_assert(loaded != NULL);
    test_assert(loaded->error == false);
    ecs_singleton_set(l->world, App, {.delta_time = LAGOON_DT});
}

/** Run the scene until it completed a number of frames.
 *
 * @param l The scene
 * @param frames The frames
 */
static void lagoon_run_to(
    lagoon_t *l,
    int32_t frames)
{
    while (l->frames < frames) {
        ecs_progress(l->world, LAGOON_DT);
        l->frames ++;
    }
}

/** Jump the world to a distance, with no hazards spawning on the way.
 *
 * @param l The scene
 * @param distance The distance
 */
static void lagoon_set_distance(
    lagoon_t *l,
    double distance)
{
    test_int(ecs_mut_var_set_t(l->world, "state.next_row", ecs_f64_t, {1e12}), 0);
    test_int(ecs_mut_var_set_t(l->world, "state.distance", ecs_f64_t, {distance}), 0);
}

/** Get the distance the world scrolled by.
 *
 * @param l The scene
 * @return The distance
 */
static double lagoon_distance(
    const lagoon_t *l)
{
    ecs_entity_t var = ecs_lookup(l->world, "state.distance");
    test_assert(var != 0);
    ecs_value_t v = ecs_mut_var_get(l->world, var);
    test_assert(v.ptr != NULL);

    return *(double*)v.ptr;
}

/** Look up an entity of the scene.
 *
 * @param l The scene
 * @param path Path of the entity
 * @return The entity
 */
static ecs_entity_t lagoon_lookup(
    const lagoon_t *l,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(l->world, path);
    test_assert(result != 0);

    return result;
}

/** Get the world transform of an entity.
 *
 * @param l The scene
 * @param e The entity
 * @param m Receives the transform
 */
static void lagoon_transform(
    const lagoon_t *l,
    ecs_entity_t e,
    mat4 m)
{
    WorldTransform buf;
    const WorldTransform *wt = box_transform_world(l->world, e, &buf);
    test_assert(wt != NULL);
    glm_mat4_copy((vec4*)wt->m, m);
}

/** Get the texture coordinates of the top face projection of a projected material at a world
 * point, as mapping_object, mapping_frame and mapping_uv of etc/shaders/include/material.wgsl make
 * them, with the TextureTransform and the TextureScroll offsets after time seconds.
 *
 * @param l The scene
 * @param e The shape
 * @param point The world point
 * @param time Seconds the scroll ran
 * @param uv Receives the texture coordinates
 */
static void lagoon_top_uv(
    const lagoon_t *l,
    ecs_entity_t e,
    const vec3 point,
    float time,
    vec2 uv)
{
    const TextureMapping *mapping = ecs_get(l->world, e, TextureMapping);
    test_assert(mapping != NULL);
    test_assert(mapping->mode != MappingUv);
    float size = mapping->size > 0 ? mapping->size : 1;

    const TextureTransform *transform = ecs_get(l->world, e, TextureTransform);
    float su = transform ? transform->scale_x : 1;
    float sv = transform ? transform->scale_y : 1;
    float ou = transform ? transform->offset_x : 0;
    float ov = transform ? transform->offset_y : 0;
    const TextureScroll *scroll = ecs_get(l->world, e, TextureScroll);
    if (scroll) {
        ou += scroll->velocity[0] * time;
        ov += scroll->velocity[1] * time;
    }

    vec3 p;
    if (mapping->mode == MappingWorld) {
        glm_vec3_copy((float*)point, p);
    } else if (mapping->mode == MappingObject) {
        mat4 m, inv;
        lagoon_transform(l, e, m);
        glm_mat4_inv(m, inv);
        glm_mat4_mulv3(inv, (float*)point, 1, p);
        p[0] *= glm_vec3_norm(m[0]);
        p[1] *= glm_vec3_norm(m[1]);
        p[2] *= glm_vec3_norm(m[2]);
    } else {
        ecs_entity_t frame = e;
        while (frame && !ecs_has(l->world, frame, TextureFrame)) {
            frame = ecs_get_parent(l->world, frame);
        }

        if (frame) {
            mat4 m, inv;
            lagoon_transform(l, frame, m);
            glm_mat4_inv(m, inv);
            glm_mat4_mulv3(inv, (float*)point, 1, p);
        } else {
            glm_vec3_copy((float*)point, p);
        }
    }

    uv[0] = p[2] / size * su + 0.5f * su + ou;
    uv[1] = -p[0] / size * sv + 0.5f * sv + ov;
}

/** Distance between two texture coordinates over the repeats of a mirrored texture.
 *
 * @param a A coordinate
 * @param b The other coordinate
 * @return The distance
 */
static float lagoon_uv_apart(
    float a,
    float b)
{
    float d = fmodf(fabsf(a - b), LAGOON_UV_PERIOD);

    return glm_min(d, LAGOON_UV_PERIOD - d);
}

/** A point on the top face of a sand shape, followed from one frame to another */
typedef struct {
    const char *path;   /**< Path of the shape */
    float x;            /**< Local X of the point on the top face */
    float z;            /**< Local Z of the point on the top face */
    vec3 point;         /**< World position of the point at the first frame */
    vec2 uv;            /**< Texture coordinates of the point at the first frame */
} lagoon_sample_t;

/** Check that the sand of shapes flows with the scenery from frame a to frame b: the texture at
 * a point of the ground stays on that point as it moves toward +Z by the change of distance.
 *
 * @param l The scene, at most at frame a
 * @param samples The points
 * @param count The number of points
 * @param a The first frame
 * @param b The second frame
 */
static void lagoon_sand_flows(
    lagoon_t *l,
    lagoon_sample_t *samples,
    int32_t count,
    int32_t a,
    int32_t b)
{
    int32_t i;
    lagoon_run_to(l, a);
    double da = lagoon_distance(l);
    for (i = 0; i < count; i ++) {
        lagoon_sample_t *s = &samples[i];
        ecs_entity_t e = lagoon_lookup(l, s->path);
        const Box *box = ecs_get(l->world, e, Box);
        test_assert(box != NULL);
        mat4 m;
        lagoon_transform(l, e, m);
        glm_mat4_mulv3(m, (vec3){s->x, box->y * 0.5f, s->z}, 1, s->point);
        lagoon_top_uv(l, e, s->point, (float)a * LAGOON_DT, s->uv);
    }

    lagoon_run_to(l, b);
    double db = lagoon_distance(l);
    test_assert(db > da + 0.1);
    bool flows = true;
    for (i = 0; i < count; i ++) {
        lagoon_sample_t *s = &samples[i];
        ecs_entity_t e = lagoon_lookup(l, s->path);
        vec3 point = {s->point[0], s->point[1], s->point[2] + (float)(db - da)};
        vec2 uv;
        lagoon_top_uv(l, e, point, (float)b * LAGOON_DT, uv);
        if (lagoon_uv_apart(s->uv[0], uv[0]) > LAGOON_UV_EPSILON ||
            lagoon_uv_apart(s->uv[1], uv[1]) > LAGOON_UV_EPSILON)
        {
            printf("%s: the ground moved by %f from frame %d to %d, the texture coordinates "
                "of a point on it went from {%f, %f} to {%f, %f}\n", s->path, db - da, a, b,
                s->uv[0], s->uv[1], uv[0], uv[1]);
            flows = false;
        }
    }

    test_assert(flows);
}

/** Get the height of the top most sand face of the banks of a side at a world X and Z.
 *
 * @param l The scene
 * @param side 0 for the left bank, 1 for the right one
 * @param x The world X
 * @param z The world Z
 * @param y Receives the height
 * @return Whether a sand face is there
 */
static bool lagoon_sand_y(
    const lagoon_t *l,
    int32_t side,
    float x,
    float z,
    float *y)
{
    char path[64];
    snprintf(path, sizeof(path), "banks.bank_%d", side);
    ecs_entity_t bank = lagoon_lookup(l, path);
    bool result = false;

    ecs_iter_t it = ecs_children(l->world, bank);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            const Box *box = ecs_get(l->world, e, Box);
            if (!box) {
                continue;
            }

            mat4 m, inv;
            lagoon_transform(l, e, m);
            glm_mat4_inv(m, inv);
            vec3 o, d;
            glm_mat4_mulv3(inv, (vec3){x, 100, z}, 1, o);
            glm_mat4_mulv3(inv, (vec3){0, -1, 0}, 0, d);
            if (fabsf(d[1]) < 1e-6f) {
                continue;
            }

            float t = (box->y * 0.5f - o[1]) / d[1];
            float lx = o[0] + d[0] * t;
            float lz = o[2] + d[2] * t;
            if (t < 0 || fabsf(lx) > box->x * 0.5f || fabsf(lz) > box->z * 0.5f) {
                continue;
            }

            vec3 hit;
            glm_mat4_mulv3(m, (vec3){lx, box->y * 0.5f, lz}, 1, hit);
            if (!result || hit[1] > *y) {
                *y = hit[1];
            }

            result = true;
        }
    }

    return result;
}

/** The sand strips of both banks flow with the scenery: frames 7 and 9 straddle a wrap of the
 * strips at a distance of 12, frames 12 and 13 scroll the world by 1.5 units.
 */
void LagoonBanks_bank_sand_flows(void)
{
    const char *strips[] = {"beach", "shelf", "top", "dune", "shore"};
    lagoon_sample_t samples[10];
    char paths[10][64];
    int32_t s, i;
    for (s = 0; s < 2; s ++) {
        for (i = 0; i < 5; i ++) {
            lagoon_sample_t *sample = &samples[s * 5 + i];
            snprintf(paths[s * 5 + i], 64, "banks.bank_%d.%s", s, strips[i]);
            *sample = (lagoon_sample_t){.path = paths[s * 5 + i], .x = 0, .z = 520};
        }
    }

    lagoon_t l;
    lagoon_new(&l);
    lagoon_sand_flows(&l, samples, 10, 7, 9);
    lagoon_sand_flows(&l, samples, 10, 12, 13);
    ecs_fini(l.world);
}

/** The lagoon floor seen through the clear water beside the banks, in front of the canoe, flows
 * with the scenery.
 */
void LagoonBanks_floor_sand_flows(void)
{
    lagoon_sample_t samples[2] = {
        {.path = "lagoon.floor", .x = -LAGOON_BANK_X + 2, .z = 780},
        {.path = "lagoon.floor", .x = LAGOON_BANK_X - 2, .z = 780}
    };

    lagoon_t l;
    lagoon_new(&l);
    lagoon_sand_flows(&l, samples, 2, 7, 9);
    lagoon_sand_flows(&l, samples, 2, 12, 13);
    ecs_fini(l.world);
}

/** Across a bank, from the waterline to the dunes, the top most sand face has no step. */
void LagoonBanks_bank_sand_joins(void)
{
    lagoon_t l;
    lagoon_new(&l);
    lagoon_run_to(&l, 3);

    const float step = 0.005f;
    const float max_rise = 0.004f;
    bool joins = true;
    int32_t s;
    for (s = 0; s < 2; s ++) {
        float side = s == 0 ? -1.0f : 1.0f;
        float prev = 0, u;
        bool have = false;
        for (u = -0.6f; u <= 12.0f; u += step) {
            float y;
            float x = side * (LAGOON_BANK_X + u);
            if (!lagoon_sand_y(&l, s, x, -10, &y)) {
                have = false;
                continue;
            }

            if (have && fabsf(y - prev) > max_rise) {
                printf("bank_%d: the sand steps from %f to %f at x %f\n", s, prev, y, x);
                joins = false;
            }

            prev = y;
            have = true;
        }
    }

    test_assert(joins);
    ecs_fini(l.world);
}

/** The props of the bank sections stand on the sand: their origin is at or a little below it,
 * not above. Distances over a cycle of zones show every zone. Sections still growing in at the
 * far end are skipped, and so are props behind the camera, past the end of the sand strips.
 */
void LagoonBanks_bank_props_on_sand(void)
{
    lagoon_t l;
    lagoon_new(&l);
    lagoon_run_to(&l, 3);

    const char *props[] = {"palm_0", "palm_1", "palm_2", "bush_0", "bush_1", "bush_2", "flowers",
        "nets_0", "nets_1", "fire", "cliff_0", "cliff_2", "slab_0", "slab_2", "tree_0", "tree_2",
        "fern_0", "fern_3", "leafy_0", "leafy_2"};
    const int32_t prop_count = sizeof(props) / sizeof(props[0]);
    int32_t checked = 0;
    bool seated = true;
    int32_t d, s, i, p;
    for (d = 0; d < 12; d ++) {
        if (d) {
            lagoon_set_distance(&l, d * 173.0);
            lagoon_run_to(&l, l.frames + 1);
        }

        for (s = 0; s < 2; s ++) {
            for (i = 0; i < LAGOON_SECTIONS; i ++) {
                char path[64];
                snprintf(path, sizeof(path), "sections.sec_%d_%d", s, i);
                ecs_entity_t sec = lagoon_lookup(&l, path);
                const Scale *scale = ecs_get(l.world, sec, Scale);
                if (scale && fabsf(scale->y - 1) > 1e-3f) {
                    continue;
                }

                ecs_entity_t stretch = ecs_lookup_child(l.world, sec, "stretch");
                test_assert(stretch != 0);
                for (p = 0; p < prop_count; p ++) {
                    ecs_entity_t e = ecs_lookup_child(l.world, stretch, props[p]);
                    if (!e) {
                        continue;
                    }

                    mat4 m;
                    lagoon_transform(&l, e, m);
                    if (m[3][2] > LAGOON_BEHIND) {
                        continue;
                    }

                    float y;
                    if (!lagoon_sand_y(&l, s, m[3][0], m[3][2], &y)) {
                        printf("%s.%s stands at x %f, z %f, off the sand\n", path, props[p],
                            m[3][0], m[3][2]);
                        seated = false;
                        continue;
                    }

                    if (m[3][1] > y + 0.02f || m[3][1] < y - 0.3f) {
                        printf("%s.%s stands at %f, the sand under it is at %f\n",
                            path, props[p], m[3][1], y);
                        seated = false;
                    }

                    checked ++;
                }
            }
        }
    }

    test_assert(checked > 100);
    test_assert(seated);
    ecs_fini(l.world);
}

/** Get the zone of a bank section from the props it has: the driftwood of the palm beach, the
 * huts of the village, the cliffs of the headland or the trees of the jungle.
 *
 * @param l The scene
 * @param stretch The LgBankSection instance of the section
 * @return The zone, -1 for none or more than one
 */
static int32_t lagoon_zone(
    const lagoon_t *l,
    ecs_entity_t stretch)
{
    const char *markers[LAGOON_ZONES] = {"drift", "hut_0", "cliff_0", "tree_0"};
    int32_t result = -1, z;
    for (z = 0; z < LAGOON_ZONES; z ++) {
        if (ecs_lookup_child(l->world, stretch, markers[z])) {
            if (result != -1) {
                return -1;
            }

            result = z;
        }
    }

    return result;
}

/** Over a cycle of the zones along the lagoon every section shows one zone at a time, every zone
 * comes by in full view on both banks, and a section only changes zone when it jumps back up the
 * lagoon: between the samples its Z went down, from behind the camera to the far end.
 */
void LagoonBanks_zones_take_turns(void)
{
    lagoon_t l;
    lagoon_new(&l);
    lagoon_run_to(&l, 1);

    int32_t prev_zone[2][LAGOON_SECTIONS];
    float prev_z[2][LAGOON_SECTIONS];
    int32_t seen[2][LAGOON_ZONES] = {{0}};
    bool one_zone = true, hidden = true;
    int32_t changes = 0;
    int32_t f, s, i, z;
    const double step = 10.5;
    const int32_t samples = (int32_t)(LAGOON_ZONE_CYCLE / step) + 2;
    for (f = 0; f < samples; f ++) {
        lagoon_set_distance(&l, f * step);
        lagoon_run_to(&l, l.frames + 1);

        for (s = 0; s < 2; s ++) {
            for (i = 0; i < LAGOON_SECTIONS; i ++) {
                char path[64];
                snprintf(path, sizeof(path), "sections.sec_%d_%d", s, i);
                ecs_entity_t sec = lagoon_lookup(&l, path);
                ecs_entity_t stretch = ecs_lookup_child(l.world, sec, "stretch");
                test_assert(stretch != 0);
                const Position *pos = ecs_get(l.world, sec, Position);
                test_assert(pos != NULL);
                const Scale *scale = ecs_get(l.world, sec, Scale);
                test_assert(scale != NULL);

                int32_t zone = lagoon_zone(&l, stretch);
                if (zone < 0) {
                    printf("%s at distance %f is in no zone or in more than one\n",
                        path, f * step);
                    one_zone = false;
                    continue;
                }

                if (fabsf(scale->x - 1) < 1e-3f && pos->z > -60 && pos->z < LAGOON_BEHIND) {
                    seen[s][zone] ++;
                }

                if (f && zone != prev_zone[s][i]) {
                    changes ++;
                    if (pos->z >= prev_z[s][i] || prev_z[s][i] < LAGOON_BEHIND - step - 1) {
                        printf("%s changed from zone %d to %d at distance %f, from Z %f to %f\n",
                            path, prev_zone[s][i], zone, f * step, prev_z[s][i], pos->z);
                        hidden = false;
                    }
                }

                prev_zone[s][i] = zone;
                prev_z[s][i] = pos->z;
            }
        }
    }

    for (s = 0; s < 2; s ++) {
        for (z = 0; z < LAGOON_ZONES; z ++) {
            if (!seen[s][z]) {
                printf("bank %d never shows zone %d in full view\n", s, z);
                one_zone = false;
            }
        }
    }

    test_assert(changes > 0);
    test_assert(one_zone);
    test_assert(hidden);
    ecs_fini(l.world);
}
