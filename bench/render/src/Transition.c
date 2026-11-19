/**
 * @file Transition.c
 * @brief Benchmarks for the cost of transitions: what the paths that don't use them pay, which
 * must be nothing, and what users of the feature pay.
 *
 * Frames run against a backend that drops the packets, so a case measures the main thread,
 * and report the ms per frame of the transition systems. The large scene is that of Extract.c.
 *
 * The caster cases move 1000 boxes under shadowed spot lights, against the same scene without
 * shadows, to measure the range math of the shadow regions, and the shadeless case moves 1000
 * boxes with NoShadow under the same lights, which need no range.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"
#include "draw.h"

/** Static shapes of the scene */
#define BENCH_TR_STATIC (100000)

/** Dynamic shapes of the scene */
#define BENCH_TR_DYNAMIC (10000)

/** Shapes per row of the grid */
#define BENCH_TR_ROW (300)

/** Instances of the prefab of the prefab cases */
#define BENCH_TR_INSTANCES (10000)

/** Shadow casters of the caster cases */
#define BENCH_TR_CASTERS (1000)

/** Casters per row of the caster cases */
#define BENCH_TR_CASTER_ROW (40)

/** Spot lights of the caster cases, on a 4 by 4 grid */
#define BENCH_TR_SPOTS (16)

/** Frames the time of the systems is measured over after a case */
#define BENCH_TR_TIMED (32)

/** What a case does every frame */
typedef enum bench_tr_mode_t {
    BENCH_TR_NONE,          /**< No Transition, nothing changes */
    BENCH_TR_NONE_DYNAMIC,  /**< No Transition, 1% of the static shapes move per frame */
    BENCH_TR_IDLE,          /**< Shapes with Transition whose values do not change */
    BENCH_TR_ACTIVE,        /**< Shapes with Transition whose values change every frame */
    BENCH_TR_MOVE,          /**< Shapes with a Position transition, restarted every frame */
    BENCH_TR_TURN           /**< Shapes with Rotation and Scale transitions, restarted every
                             *   frame */
} bench_tr_mode_t;

/** State of a running case */
typedef struct bench_tr_t {
    ecs_world_t *world;     /**< The world */
    ecs_entity_t *shapes;   /**< The static shapes */
    ecs_entity_t *users;    /**< The shapes with Transition */
    int32_t user_count;     /**< Shapes with Transition */
    ecs_entity_t prefab;    /**< Prefab of the prefab cases, 0 for the others */
    bench_tr_mode_t mode;   /**< What a frame does */
    int32_t next;           /**< Next static shape a dynamic frame moves */
    int64_t frame;          /**< Frames run */
} bench_tr_t;

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_tr_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

/** Create a world that drops its packets, with a camera and a view.
 *
 * @return The world
 */
static ecs_world_t* bench_tr_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_backend_t backend = { .render = bench_tr_drop };
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});

    return world;
}

/** Create the scene of 100k static and 10k dynamic shapes, giving the first ones Transition.
 *
 * @param b The state of the case
 * @param users Static shapes that get a Transition of Position and Rgb
 */
static void bench_tr_scene(
    bench_tr_t *b,
    int32_t users)
{
    ecs_world_t *world = b->world;
    TransitionSpec spec = { .duration = 0.5f, .timing_function = EaseInOut };
    int32_t i;
    b->shapes = ecs_os_malloc_n(ecs_entity_t, BENCH_TR_STATIC);
    for (i = 0; i < BENCH_TR_STATIC; i ++) {
        ecs_entity_t e = b->shapes[i] = ecs_new(world);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_TR_ROW), 0, (float)(i / BENCH_TR_ROW)});
        ecs_set(world, e, Box, {0.5f, 0.5f, 0.5f});
        ecs_set(world, e, Rgb, {200, 100, 50});
        if (i < users) {
            box_transition_set(world, e, ecs_id(Position), &spec);
            box_transition_set(world, e, ecs_id(Rgb), &spec);
        }
    }

    for (i = 0; i < BENCH_TR_DYNAMIC; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_add(world, e, DynamicTransform);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_TR_ROW), 2, (float)(i / BENCH_TR_ROW)});
        ecs_set(world, e, Sphere, {.segments = 8, .radius = 0.25f});
    }

    b->users = b->shapes;
    b->user_count = users;
}

/** Create a world that drops its packets, lit with shadows, with a floor and spot lights
 * over the area of the caster cases.
 *
 * @param shadows Whether the spot lights have shadows, which turns the shadow tiles on
 * @return The world
 */
static ecs_world_t* bench_tr_lit_world(
    bool shadows)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_backend_t backend = { .render = bench_tr_drop };
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 30, 45});
    ecs_set(world, camera, Rotation, {-0.6f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .shadows = {.enabled = true, .cascades = 4, .resolution = 2048,
            .max_distance = 100, .split_lambda = 0.7f, .depth_bias = 2,
            .normal_offset = 2, .filter_size = 3, .fade = 0.2f, .blend = 0.1f}});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {60, 0.2f, 40});
    ecs_set(world, floor, Rgb, {90, 96, 104});
    int32_t i;
    for (i = 0; i < BENCH_TR_SPOTS; i ++) {
        ecs_entity_t spot = ecs_new(world);
        ecs_set(world, spot, Position, {(float)(i % 4) * 10.0f - 15.0f, 12,
            (float)(i / 4) * 7.0f - 10.5f});
        ecs_set(world, spot, Rotation, {1.3f, 0, 0});
        ecs_set(world, spot, SpotLight, {.intensity = 200 * GLM_PIf, .range = 30,
            .inner_angle = 30, .outer_angle = 40, .shadows = shadows,
            .shadow_resolution = 512});
    }

    return world;
}

/** Get the place of a caster of the caster cases on its grid.
 *
 * @param i The caster
 * @param out Receives its position
 */
static void bench_tr_caster_place(
    int32_t i,
    Position *out)
{
    out->x = (float)(i % BENCH_TR_CASTER_ROW) - (float)BENCH_TR_CASTER_ROW * 0.5f;
    out->y = 0.5f;
    out->z = (float)(i / BENCH_TR_CASTER_ROW) - 12.5f;
}

/** Create the 1000 boxes of the caster cases, each with transitions of the components the mode
 * changes, or children of one box with them.
 *
 * @param b The state of the case
 * @param children Whether the boxes are plain children of one transitioning box, which is the
 * only shape with Transition
 * @param cast Whether the boxes cast shadows, else they have NoShadow
 */
static void bench_tr_caster_scene(
    bench_tr_t *b,
    bool children,
    bool cast)
{
    ecs_world_t *world = b->world;
    TransitionSpec spec = { .duration = 0.5f, .timing_function = EaseInOut };
    ecs_entity_t parent = 0;
    int32_t i, first = 0;
    b->shapes = ecs_os_malloc_n(ecs_entity_t, BENCH_TR_CASTERS + 1);
    if (children) {
        parent = b->shapes[0] = ecs_new(world);
        ecs_set(world, parent, Position, {0, 0, 0});
        ecs_set(world, parent, Rotation, {0, 0, 0});
        ecs_set(world, parent, Scale, {1, 1, 1});
        ecs_set(world, parent, Box, {0.5f, 0.5f, 0.5f});
        ecs_set(world, parent, Rgb, {200, 100, 50});
        first = 1;
    }

    for (i = 0; i < BENCH_TR_CASTERS; i ++) {
        ecs_entity_t e = b->shapes[first + i] = parent
            ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
        Position at;
        bench_tr_caster_place(i, &at);
        ecs_set_ptr(world, e, Position, &at);
        ecs_set(world, e, Box, {0.4f, 0.8f, 0.4f});
        ecs_set(world, e, Rgb, {200, 120, 80});
        if (!cast) {
            ecs_add(world, e, NoShadow);
        }

        if (b->mode == BENCH_TR_TURN) {
            ecs_set(world, e, Rotation, {0, 0, 0});
            ecs_set(world, e, Scale, {1, 1, 1});
        }
    }

    b->users = b->shapes;
    b->user_count = children ? 1 : BENCH_TR_CASTERS;
    for (i = 0; i < b->user_count; i ++) {
        if (b->mode == BENCH_TR_MOVE) {
            box_transition_set(world, b->users[i], ecs_id(Position), &spec);
        } else {
            box_transition_set(world, b->users[i], ecs_id(Rotation), &spec);
            box_transition_set(world, b->users[i], ecs_id(Scale), &spec);
        }
    }
}

/** Create the scene of one prefab with Rgb and Transition and 10k mesh instances that inherit
 * its Rgb.
 *
 * @param b The state of the case
 */
static void bench_tr_prefab_scene(
    bench_tr_t *b)
{
    ecs_world_t *world = b->world;
    int32_t i;
    b->prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, b->prefab, Box, {0.5f, 0.5f, 0.5f});
    ecs_set(world, b->prefab, Rgb, {200, 100, 50});
    box_transition_set(world, b->prefab, ecs_id(Rgb), &(TransitionSpec){ .duration = 0.5f });
    for (i = 0; i < BENCH_TR_INSTANCES; i ++) {
        ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, b->prefab);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_TR_ROW), 0, (float)(i / BENCH_TR_ROW)});
    }
}

/** Change what the mode of a case changes and run a frame.
 *
 * An active frame writes a new value every frame, which restarts the transitions, so they stay
 * active whatever the frame time.
 *
 * @param b The state of the case
 */
static void bench_tr_frame(
    bench_tr_t *b)
{
    ecs_world_t *world = b->world;
    float y = (float)(b->frame % 64) * 0.01f + 0.01f;
    uint8_t g = (uint8_t)(b->frame % 200);
    int32_t i;
    if (b->mode == BENCH_TR_NONE_DYNAMIC) {
        for (i = 0; i < BENCH_TR_STATIC / 100; i ++) {
            ecs_entity_t e = b->shapes[b->next];
            b->next = (b->next + 1) % BENCH_TR_STATIC;
            ecs_set(world, e, Position, {(float)(b->next % BENCH_TR_ROW), y, 0});
        }
    }

    if (b->mode == BENCH_TR_ACTIVE) {
        for (i = 0; i < b->user_count; i ++) {
            ecs_set(world, b->users[i], Position,
                {(float)(i % BENCH_TR_ROW), y, (float)(i / BENCH_TR_ROW)});
            ecs_set(world, b->users[i], Rgb, {200, g, 50});
        }

        if (b->prefab) {
            ecs_set(world, b->prefab, Rgb, {200, g, 50});
        }
    }

    if (b->mode == BENCH_TR_MOVE) {
        for (i = 0; i < b->user_count; i ++) {
            Position at;
            bench_tr_caster_place(i, &at);
            at.y += y;
            ecs_set_ptr(world, b->users[i], Position, &at);
        }
    }

    if (b->mode == BENCH_TR_TURN) {
        for (i = 0; i < b->user_count; i ++) {
            ecs_set(world, b->users[i], Rotation, {0, y * 4.0f, y});
            ecs_set(world, b->users[i], Scale, {1 + y, 1, 1 + y});
        }
    }

    b->frame ++;
    ecs_progress(world, 0);
}

/** Get the time a system spent in the last frame, over every stage.
 *
 * @param world The world
 * @param name Path of the system
 * @return Seconds, 0 without the system
 */
static double bench_tr_system_time(
    const ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_lookup(world, name);
    const ecs_system_t *s = e ? ecs_system_get(world, e) : NULL;
    double result = 0;
    int32_t i;
    for (i = 0; s && i < ecs_vec_count(&s->time_spent); i ++) {
        result += (double)*ecs_vec_get_t(&s->time_spent, ecs_ftime_t, i);
    }

    return result;
}

/** Run frames of a case and report its counters.
 *
 * @param bench The benchmark
 * @param b The state of the case, with its scene
 */
static void bench_tr_run(
    bench_t *bench,
    bench_tr_t *b)
{
    ecs_world_t *world = b->world;
    box_render_transitions_t *s = box_render_transitions_get(world);
    int32_t i, visited = 0;
    bench_tr_frame(b);
    bench_tr_frame(b);
    bench_set_items(bench, 1);
    while (bench_iter(bench)) {
        bench_tr_frame(b);
        visited = box_render_transitions_material_visited(s);
        bench_keep(visited);
    }

    box_render_sync(world);
    box_render_draws_t *d = box_draws_state(world);
    bench_counter(bench, "shadow_record", d && d->shadow_record ? 1.0 : 0.0);
    bench_counter(bench, "material_visited", (double)visited);
    bench_counter(bench, "resolver_active", (double)box_render_transition_test_active(world));

    ecs_measure_system_time(world, true);
    double extract = 0, slots = 0;
    for (i = 0; i < BENCH_TR_TIMED; i ++) {
        bench_tr_frame(b);
        extract += bench_tr_system_time(world, "box.render.extract.Transitions");
        slots += bench_tr_system_time(world, "box.render.extract.TransitionSlots");
    }

    bench_counter(bench, "transitions_ms", (extract + slots) * 1000.0 / BENCH_TR_TIMED);

    box_render_sync(world);
    ecs_fini(world);
    ecs_os_free(b->shapes);
}

/** Run a case of the large scene.
 *
 * @param bench The benchmark
 * @param users Static shapes with Transition
 * @param mode What a frame does
 */
static void bench_tr_large(
    bench_t *bench,
    int32_t users,
    bench_tr_mode_t mode)
{
    bench_tr_t b = { .world = bench_tr_world(), .mode = mode };
    bench_tr_scene(&b, users);
    bench_tr_run(bench, &b);
}

/** Run a caster case: 1000 boxes under spot lights.
 *
 * @param bench The benchmark
 * @param mode BENCH_TR_MOVE or BENCH_TR_TURN
 * @param shadows Whether the spot lights have shadows, which turns the shadow tiles on
 * @param children Whether the boxes are plain children of one transitioning box
 * @param cast Whether the boxes cast shadows, else they have NoShadow
 */
static void bench_tr_casters(
    bench_t *bench,
    bench_tr_mode_t mode,
    bool shadows,
    bool children,
    bool cast)
{
    bench_tr_t b = { .world = bench_tr_lit_world(shadows), .mode = mode };
    bench_tr_caster_scene(&b, children, cast);
    bench_tr_run(bench, &b);
}

/** Run a case of the prefab scene.
 *
 * @param bench The benchmark
 * @param mode What a frame does
 */
static void bench_tr_prefab(
    bench_t *bench,
    bench_tr_mode_t mode)
{
    bench_tr_t b = { .world = bench_tr_world(), .mode = mode };
    bench_tr_prefab_scene(&b);
    bench_tr_run(bench, &b);
}

void Transition_none_static(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_large(b, 0, BENCH_TR_NONE);
}

void Transition_none_dynamic(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_large(b, 0, BENCH_TR_NONE_DYNAMIC);
}

void Transition_one_idle(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_large(b, 1, BENCH_TR_IDLE);
}

void Transition_one_active(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_large(b, 1, BENCH_TR_ACTIVE);
}

void Transition_idle_1000(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_large(b, 1000, BENCH_TR_IDLE);
}

void Transition_active_1000(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_large(b, 1000, BENCH_TR_ACTIVE);
}

void Transition_prefab_rgb_idle(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_prefab(b, BENCH_TR_IDLE);
}

void Transition_prefab_rgb_active(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_prefab(b, BENCH_TR_ACTIVE);
}

void Transition_casters_move_1000_shadows(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_casters(b, BENCH_TR_MOVE, true, false, true);
}

void Transition_casters_move_1000_no_shadows(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_casters(b, BENCH_TR_MOVE, false, false, true);
}

void Transition_casters_turn_1000_shadows(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_casters(b, BENCH_TR_TURN, true, false, true);
}

void Transition_casters_parent_1000_shadows(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_casters(b, BENCH_TR_TURN, true, true, true);
}

void Transition_shadeless_move_1000_shadows(
    bench_t *b)
{
    // engine_core/transition
    bench_tr_casters(b, BENCH_TR_MOVE, true, false, false);
}

void Transition_resolve_empty(
    bench_t *b)
{
    // engine_core/transition
    box_render_transition_table_t t;
    box_render_packet_t p;
    box_render_transition_table_init(&t);
    box_render_packet_init(&p);
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        box_render_transition_resolve(&t, &p);
        bench_keep(box_render_transition_table_active(&t));
    }

    box_render_packet_fini(&p);
    box_render_transition_table_fini(&t);
}
