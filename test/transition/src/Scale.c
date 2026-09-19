/**
 * @file Scale.c
 * @brief Tests for the scene of ten thousand entities with ten transitions each.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"

/** Entities the tests build the benchmark scene with */
#define SCALE_ENTITIES (500)

/** Bytes one transitioning entity of the scene may put on a packet */
#define SCALE_ENTITY_BYTES (900)

/** Entities the churn of the benchmark scene restarts every frame, per ten thousand */
#define SCALE_CHURN (167)

/** Allocations the hooked OS API has seen since the last reset */
static int32_t scale_allocs;

/** The OS API the test replaced, restored when the test ends */
static ecs_os_api_t scale_api;

static ecs_world_t *w;
static box_packet_t packet;

/** Count an allocation and forward it to the system allocator.
 *
 * @param size Bytes to allocate
 * @return The allocation
 */
static void* scale_malloc(
    ecs_size_t size)
{
    scale_allocs ++;

    return malloc((size_t)size);
}

/** Count a zeroed allocation and forward it to the system allocator.
 *
 * @param size Bytes to allocate
 * @return The allocation
 */
static void* scale_calloc(
    ecs_size_t size)
{
    scale_allocs ++;

    return calloc(1, (size_t)size);
}

/** Count a resize and forward it to the system allocator.
 *
 * @param ptr The allocation
 * @param size Bytes to resize it to
 * @return The allocation
 */
static void* scale_realloc(
    void *ptr,
    ecs_size_t size)
{
    scale_allocs ++;

    return realloc(ptr, (size_t)size);
}

/** Release an allocation.
 *
 * @param ptr The allocation
 */
static void scale_free(
    void *ptr)
{
    free(ptr);
}

void Scale_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    scale_api = ecs_os_api;
    scale_allocs = 0;
}

void Scale_teardown(void)
{
    ecs_os_api = scale_api;
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scale_packet_of_the_scene_stays_within_its_budget(void)
{
    // transitions/scale
    test_int(ecs_script_run(w, "test",
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.transition\n"
        "struct BenchSingle(value: f32)\n"
        "struct BenchPair(a: f32, b: f32)\n"
        "struct BenchTriple(x: f32, y: f32, z: f32)\n"
        "struct BenchQuad(x: f32, y: f32, z: f32, w: f32)\n"
        "BenchSingle { Transitionable }\n"
        "BenchPair { Transitionable }\n"
        "BenchTriple { Transitionable }\n"
        "BenchQuad { Transitionable }\n"
        "for i in 0..500 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Rotation: {0, 0, 0}\n"
        "    Scale: {1, 1, 1}\n"
        "    Box: {0.8, 0.8, 0.8}\n"
        "    Rgb: {40, 60, 90}\n"
        "    Emissive: {strength: 0, color: {255, 200, 50}}\n"
        "    Tint: {255, 255, 255, 0}\n"
        "    BenchSingle: {0}\n"
        "    BenchPair: {0, 0}\n"
        "    BenchTriple: {0, 0, 0}\n"
        "    BenchQuad: {0, 0, 0, 0}\n"
        "    Transition: [\n"
        "      Position: {duration: 8, delay: (i % 60) * 0.0166667},\n"
        "      Rotation: {duration: 8.25, delay: (i % 60) * 0.0166667},\n"
        "      Scale: {duration: 8.5, delay: (i % 60) * 0.0166667},\n"
        "      Rgb: {duration: 8.75, delay: (i % 60) * 0.0166667},\n"
        "      Emissive: {duration: 9, delay: (i % 60) * 0.0166667},\n"
        "      Tint: {duration: 9.25, delay: (i % 60) * 0.0166667},\n"
        "      BenchSingle: {duration: 9.5},\n"
        "      BenchPair: {duration: 9.75},\n"
        "      BenchTriple: {duration: 10},\n"
        "      BenchQuad: {duration: 10.25}\n"
        "    ]\n"
        "    Position: {i, 1, 0}\n"
        "    Rotation: {0.1, 0.2, 0}\n"
        "    Scale: {1.4, 1.4, 1.4}\n"
        "    Rgb: {200, 180, 120}\n"
        "    Emissive: {strength: 2, color: {255, 200, 50}}\n"
        "    Tint: {255, 200, 200, 60}\n"
        "    BenchSingle: {1}\n"
        "    BenchPair: {1, 1}\n"
        "    BenchTriple: {1, 1, 1}\n"
        "    BenchQuad: {1, 1, 1, 1}\n"
        "  }\n"
        "}\n", NULL), 0);

    box_render_extract(w, &packet);

    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, i);

        free(mesh->vertices);
        free(mesh->indices);
    }

    size_t bytes = packet.transitions.count * sizeof(box_transition_cmd_t) +
        packet.transition_records.count * sizeof(box_transition_rec_t) +
        packet.static_updates.count * sizeof(box_static_update_t);

    test_int(packet.transitions.count, SCALE_ENTITIES);
    test_int(packet.transition_records.count, SCALE_ENTITIES * 6);
    test_assert(bytes <= SCALE_ENTITIES * SCALE_ENTITY_BYTES);

    box_packet_reset(&packet);
    ecs_progress(w, 1.0 / 60.0);
    box_render_extract(w, &packet);
    test_int(packet.transitions.count, 0);
    test_int(packet.transition_records.count, 0);
    test_int(packet.static_updates.count, 0);
}

void Scale_steady_state_does_not_allocate(void)
{
    // transitions/scale
    test_int(ecs_script_run(w, "test",
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.transition\n"
        "for i in 0..500 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Rotation: {0, 0, 0}\n"
        "    Scale: {1, 1, 1}\n"
        "    Box: {0.8, 0.8, 0.8}\n"
        "    Rgb: {40, 60, 90}\n"
        "    Emissive: {strength: 0, color: {255, 200, 50}}\n"
        "    Tint: {255, 255, 255, 0}\n"
        "    Transition: [\n"
        "      Position: {duration: 8, delay: (i % 60) * 0.0166667},\n"
        "      Rotation: {duration: 8.25},\n"
        "      Scale: {duration: 8.5},\n"
        "      Rgb: {duration: 8.75},\n"
        "      Emissive: {duration: 9},\n"
        "      Tint: {duration: 9.25}\n"
        "    ]\n"
        "    Position: {i, 1, 0}\n"
        "    Rotation: {0.1, 0.2, 0}\n"
        "    Scale: {1.4, 1.4, 1.4}\n"
        "    Rgb: {200, 180, 120}\n"
        "    Emissive: {strength: 2, color: {255, 200, 50}}\n"
        "    Tint: {255, 200, 200, 60}\n"
        "  }\n"
        "}\n", NULL), 0);

    box_render_extract(w, &packet);

    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, i);

        free(mesh->vertices);
        free(mesh->indices);
    }

    ecs_entity_t moving[SCALE_ENTITIES];
    ecs_query_t *q = ecs_query(w, {
        .terms = {{ ecs_id(Position) }, { ecs_id(Transition) }}
    });
    ecs_iter_t it = ecs_query_iter(w, q);
    int32_t found = 0;
    while (ecs_query_next(&it)) {
        int32_t k;
        for (k = 0; k < it.count && found < SCALE_ENTITIES; k ++) {
            moving[found ++] = it.entities[k];
        }
    }

    ecs_query_fini(q);
    test_int(found, SCALE_ENTITIES);

    int32_t frame;
    for (frame = 0; frame < 8; frame ++) {
        int32_t churn;

        box_packet_reset(&packet);
        for (churn = 0; churn < SCALE_CHURN / 20; churn ++) {
            ecs_entity_t e = moving[(frame * 8 + churn) % SCALE_ENTITIES];

            ecs_set(w, e, Position, {(float)churn, 2, 0});
        }

        ecs_progress(w, 1.0 / 60.0);
        box_render_extract(w, &packet);
    }

    ecs_os_api.malloc_ = scale_malloc;
    ecs_os_api.calloc_ = scale_calloc;
    ecs_os_api.realloc_ = scale_realloc;
    ecs_os_api.free_ = scale_free;
    scale_allocs = 0;

    int32_t churn;

    box_packet_reset(&packet);
    for (churn = 0; churn < SCALE_CHURN / 20; churn ++) {
        ecs_entity_t e = moving[(9 * 8 + churn) % SCALE_ENTITIES];

        ecs_set(w, e, Position, {(float)churn, 3, 0});
    }

    ecs_progress(w, 1.0 / 60.0);
    box_render_extract(w, &packet);
    ecs_os_api = scale_api;
    test_assert(packet.transitions.count != 0);
    test_int(scale_allocs, 0);
}

void Scale_steady_state_evaluation_reuses_its_arrays(void)
{
    // transitions/scale
    box_transition_pool_t pool;
    box_transition_cmd_t cmd;
    box_transition_rec_t records[BOX_DISPLAY_CHANNELS];
    box_instance_t base;
    uint32_t channel;

    memset(&base, 0, sizeof(base));
    memset(&cmd, 0, sizeof(cmd));
    memset(records, 0, sizeof(records));
    box_transition_pool_init(&pool, (uint32_t)sizeof(box_instance_t));
    cmd.count = BOX_DISPLAY_CHANNELS;
    cmd.present = (1u << BOX_DISPLAY_CHANNELS) - 1u;
    cmd.posed = 1;
    cmd.parent[0] = cmd.parent[5] = cmd.parent[10] = cmd.parent[15] = 1.0f;
    cmd.mesh_scale[0] = cmd.mesh_scale[1] = cmd.mesh_scale[2] = 1.0f;
    for (channel = 0; channel < BOX_DISPLAY_CHANNELS; channel ++) {
        records[channel].channel = channel;
        records[channel].count = 3;
        records[channel].duration = 4.0f;
        records[channel].to[0] = 10.0f;
    }

    uint32_t i;
    for (i = 0; i < 256; i ++) {
        cmd.slot = i;
        box_transition_pool_start(&pool, BOX_TRANSITION_NONE, &cmd, records, &base);
    }

    uint32_t entries = pool.entries.capacity;
    uint32_t lanes = pool.lanes[BOX_DISPLAY_RGB].times.capacity;
    double time = 0;
    for (i = 0; i < 64; i ++) {
        time += 1.0 / 60.0;
        box_transition_pool_eval(&pool, time);

        uint32_t j;
        for (j = 0; j < pool.entries.count; j ++) {
            test_bool(box_transition_pool_compose(&pool, j), true);
        }

        cmd.slot = i % 256;
        box_transition_pool_start(&pool, i % 256, &cmd, records, &base);
        box_transition_pool_stop(&pool, (i + 1) % pool.entries.count);
        box_transition_pool_start(&pool, BOX_TRANSITION_NONE, &cmd, records, &base);
    }

    test_int(pool.entries.count, 256);
    test_int(pool.lanes[BOX_DISPLAY_RGB].times.count, 256);
    test_int(pool.entries.capacity, entries);
    test_int(pool.lanes[BOX_DISPLAY_RGB].times.capacity, lanes);
    box_transition_pool_fini(&pool);
}

void Scale_lanes_hold_what_the_single_instance_fill_computes(void)
{
    // transitions/scale
    box_transition_pool_t pool;
    box_transition_cmd_t cmd;
    box_transition_rec_t records[3];
    box_instance_t fill;
    box_instance_t base;

    memset(&fill, 0, sizeof(fill));
    memset(&base, 0, sizeof(base));
    memset(&cmd, 0, sizeof(cmd));
    memset(records, 0, sizeof(records));
    box_transition_pool_init(&pool, (uint32_t)sizeof(box_instance_t));
    cmd.count = 3;
    cmd.posed = 1;
    cmd.present = (1u << BOX_DISPLAY_POSITION) | (1u << BOX_DISPLAY_SCALE) |
        (1u << BOX_DISPLAY_RGB) | (1u << BOX_DISPLAY_EMISSIVE);
    cmd.parent[0] = cmd.parent[5] = cmd.parent[10] = cmd.parent[15] = 1.0f;
    cmd.mesh_scale[0] = cmd.mesh_scale[1] = cmd.mesh_scale[2] = 2.0f;
    cmd.bounds_max[0] = cmd.bounds_max[1] = cmd.bounds_max[2] = 0.5f;
    cmd.bounds_min[0] = cmd.bounds_min[1] = cmd.bounds_min[2] = -0.5f;
    cmd.values[BOX_DISPLAY_SCALE][0] = 1.0f;
    cmd.values[BOX_DISPLAY_SCALE][1] = 1.0f;
    cmd.values[BOX_DISPLAY_SCALE][2] = 1.0f;
    cmd.values[BOX_DISPLAY_RGB][0] = 200.0f;
    cmd.values[BOX_DISPLAY_EMISSIVE][0] = 2.0f;
    cmd.values[BOX_DISPLAY_EMISSIVE][1] = 255.0f;

    records[0].channel = BOX_DISPLAY_POSITION;
    records[0].first = 1;
    records[0].count = 1;
    records[0].duration = 2.0f;
    records[0].delay = 0.25f;
    records[0].timing = BOX_TIMING_EASE_IN_OUT;
    records[0].to[0] = 40.0f;
    records[1].channel = BOX_DISPLAY_RGB;
    records[1].count = 3;
    records[1].duration = 1.0f;
    records[1].timing = BOX_TIMING_LINEAR;
    records[1].to[0] = 200.0f;
    records[2].channel = BOX_DISPLAY_EMISSIVE;
    records[2].count = 4;
    records[2].duration = 3.0f;
    records[2].timing = BOX_TIMING_EASE;
    records[2].to[0] = 2.0f;
    records[2].to[1] = 255.0f;
    box_transition_pool_start(&pool, BOX_TRANSITION_NONE, &cmd, records, &base);

    double time;
    for (time = 0; time < 3.5; time += 0.375) {
        bool running = box_transition_fill(&cmd, records, time, &fill);

        box_transition_pool_eval(&pool, time);
        test_bool(box_transition_pool_compose(&pool, 0), running);

        box_instance_t *out = box_transition_pool_base(&pool, 0);
        int32_t j;

        for (j = 0; j < 12; j ++) {
            test_flt(out->transform[j], fill.transform[j]);
        }

        for (j = 0; j < 4; j ++) {
            test_flt(out->color[j], fill.color[j]);
            test_flt(out->emissive[j], fill.emissive[j]);
            test_flt(out->bounds[j], fill.bounds[j]);
        }
    }

    box_transition_pool_fini(&pool);
}

void Scale_staggered_scene_spreads_the_starts_over_the_second(void)
{
    // transitions/scale
    test_int(ecs_script_run(w, "test",
        "using box.transform\n"
        "using box.geometry\n"
        "using box.transition\n"
        "for i in 0..60 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Box: {0.8, 0.8, 0.8}\n"
        "    Transition: [Position: {duration: 8, delay: (i % 60) * 0.0166667}]\n"
        "    Position: {i, 1, 0}\n"
        "  }\n"
        "}\n", NULL), 0);

    box_render_extract(w, &packet);

    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *mesh = box_vec_at(&packet.mesh_create, i);

        free(mesh->vertices);
        free(mesh->indices);
    }

    test_int(packet.transition_records.count, 60);

    float lowest = 1000;
    float highest = 0;
    for (i = 0; i < packet.transition_records.count; i ++) {
        box_transition_rec_t *rec = box_vec_at(&packet.transition_records, i);

        lowest = rec->delay < lowest ? rec->delay : lowest;
        highest = rec->delay > highest ? rec->delay : highest;
    }

    test_flt(lowest, 0);
    test_assert(highest > 0.98);
    test_assert(highest < 1.0);
}
