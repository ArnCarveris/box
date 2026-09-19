/**
 * @file Extract.c
 * @brief Benchmarks for extracting and numbering a UI tree of ten thousand nodes.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/ui/ui_internal.h"

/** Nodes in the benchmarked tree, the root included */
#define BENCH_TREE_COUNT (10000)

/** Children one node of the benchmarked tree holds */
#define BENCH_TREE_FANOUT (8)

/** Build a UI tree of BENCH_TREE_COUNT nodes, each of them a rectangle.
 *
 * @param world The world
 * @param dynamic The tree is re-extracted every frame
 * @return The nodes, the root first, owned by the caller
 */
static ecs_entity_t* bench_ui_tree(
    ecs_world_t *world,
    bool dynamic)
{
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_TREE_COUNT);

    tree[0] = ecs_new(world);
    ecs_add(world, tree[0], Ui);
    ecs_get_mut(world, tree[0], Ui)->height = 540;
    ecs_modified(world, tree[0], Ui);
    ecs_set(world, tree[0], Position, {0, 0, 0});
    ecs_set(world, tree[0], Rect, {600, 300});
    if (dynamic) {
        ecs_add(world, tree[0], DynamicTransform);
    }

    int32_t i;
    for (i = 1; i < BENCH_TREE_COUNT; i ++) {
        tree[i] = ecs_new_w_pair(world, EcsChildOf,
            tree[(i - 1) / BENCH_TREE_FANOUT]);
        ecs_set(world, tree[i], Position, {(float)(i % 32), (float)(i % 16), 0});
        ecs_set(world, tree[i], Rect, {40, 20});
    }

    return tree;
}

/** Release the mesh uploads the first extract of a scene produced.
 *
 * @param packet The packet
 */
static void bench_ui_free_meshes(
    box_packet_t *packet)
{
    uint32_t i;
    for (i = 0; i < packet->mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet->mesh_create, i);

        free(cmd->vertices);
        free(cmd->indices);
    }
}

void Extract_extract_10k_tree(bench_t *b)
{
    // ui/ui_view
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxUi);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *tree = bench_ui_tree(world, true);

    box_ui_update(world);
    box_render_extract(world, &packet);
    bench_ui_free_meshes(&packet);

    bench_set_items(b, BENCH_TREE_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        bench_resume(b);
        box_ui_update(world);
        box_render_extract(world, &packet);
        bench_keep(packet.dynamic_instances.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(tree);
    ecs_fini(world);
}

void Extract_walk_10k_tree(bench_t *b)
{
    // ui/ui_view
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxUi);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *tree = bench_ui_tree(world, false);

    box_ui_update(world);
    box_render_extract(world, &packet);
    bench_ui_free_meshes(&packet);

    bench_set_items(b, BENCH_TREE_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        ecs_get_mut(world, tree[0], UiImpl)->dirty = true;
        bench_resume(b);
        box_ui_update(world);
        bench_keep(ecs_get(world, tree[0], UiImpl)->count);
    }

    box_packet_fini(&packet);
    ecs_os_free(tree);
    ecs_fini(world);
}

void Extract_extract_10k_static_tree(bench_t *b)
{
    // ui/ui_view
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxUi);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *tree = bench_ui_tree(world, false);

    box_ui_update(world);
    box_render_extract(world, &packet);
    bench_ui_free_meshes(&packet);
    float x = 0;

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        x += 1.0f;
        ecs_set(world, tree[BENCH_TREE_COUNT - 1], Position, {x, 0, 0});
        bench_resume(b);
        box_ui_update(world);
        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(tree);
    ecs_fini(world);
}
