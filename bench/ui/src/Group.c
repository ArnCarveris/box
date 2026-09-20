/**
 * @file Group.c
 * @brief Benchmarks for the subtrees that composite through a target of their own.
 *
 * Ten transparent panels are ten more views, ten more targets and ten more
 * layers, and the frame has to walk, lay out and extract all of them. The same
 * ten panels without the opacity are the price of the panels alone, so the two
 * cases together say what group transparency costs.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/ui/ui_internal.h"

/** Transparent panels the benchmarked tree holds */
#define BENCH_GROUP_PANELS (10)

/** Children every panel of the benchmarked tree holds */
#define BENCH_GROUP_CHILDREN (8)

/** Nodes the benchmarked tree holds, the root and the panels included */
#define BENCH_GROUP_COUNT \
    (1 + BENCH_GROUP_PANELS * (1 + BENCH_GROUP_CHILDREN))

/** Build a tree of ten panels of eight overlapping children each.
 *
 * @param world The world
 * @param alpha Opacity of every panel, one leaves the panels opaque
 * @return The nodes, the root first, owned by the caller
 */
static ecs_entity_t* bench_ui_panels(
    ecs_world_t *world,
    float alpha)
{
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_GROUP_COUNT);
    int32_t next = 0;

    tree[next] = ecs_new(world);
    ecs_add(world, tree[next], Ui);
    ecs_get_mut(world, tree[next], Ui)->height = 1080;
    ecs_modified(world, tree[next], Ui);
    ecs_set(world, tree[next], Position, {0, 0, 0});
    ecs_set(world, tree[next], Rect, {1920, 1080});
    next ++;

    int32_t panel;
    for (panel = 0; panel < BENCH_GROUP_PANELS; panel ++) {
        ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, tree[0]);

        ecs_set(world, e, Position, {(float)(panel * 120 - 540), 0, 0});
        ecs_set(world, e, Rect, {320, 240});
        ecs_set(world, e, Border, {.radius = 16});
        ecs_set(world, e, Rgb, {40, 48, 68});
        if (alpha < 1.0f) {
            ecs_set(world, e, Opacity, {alpha});
        }

        tree[next ++] = e;
        int32_t child;
        for (child = 0; child < BENCH_GROUP_CHILDREN; child ++) {
            ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, e);

            ecs_set(world, c, Position, {(float)(child * 20 - 70), 0, 0});
            ecs_set(world, c, Circle, {.radius = 60});
            ecs_set(world, c, Rgb, {200, 120, 90});
            tree[next ++] = c;
        }
    }

    return tree;
}

/** Release the mesh uploads the first extract of a scene produced.
 *
 * @param packet The packet
 */
static void bench_ui_group_meshes(
    box_packet_t *packet)
{
    uint32_t i;
    for (i = 0; i < packet->mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet->mesh_create, i);

        free(cmd->vertices);
        free(cmd->indices);
    }
}

void Group_extract_ten_transparent_panels(bench_t *b)
{
    // ui/ui_visuals
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *tree = bench_ui_panels(world, 0.5f);

    box_ui_update(world);
    box_render_extract(world, &packet);
    bench_ui_group_meshes(&packet);

    bench_set_items(b, BENCH_GROUP_PANELS);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        ecs_get_mut(world, tree[0], UiImpl)->relayout = true;
        bench_resume(b);
        box_ui_update(world);
        box_render_extract(world, &packet);
        bench_keep(packet.views.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(tree);
    ecs_fini(world);
}

void Group_extract_ten_opaque_panels(bench_t *b)
{
    // ui/ui_visuals
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *tree = bench_ui_panels(world, 1.0f);

    box_ui_update(world);
    box_render_extract(world, &packet);
    bench_ui_group_meshes(&packet);

    bench_set_items(b, BENCH_GROUP_PANELS);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        ecs_get_mut(world, tree[0], UiImpl)->relayout = true;
        bench_resume(b);
        box_ui_update(world);
        box_render_extract(world, &packet);
        bench_keep(packet.views.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(tree);
    ecs_fini(world);
}

void Group_ten_transparent_panels_unchanged(bench_t *b)
{
    // ui/ui_visuals
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *tree = bench_ui_panels(world, 0.5f);

    box_ui_update(world);
    box_render_extract(world, &packet);
    bench_ui_group_meshes(&packet);

    bench_set_items(b, BENCH_GROUP_PANELS);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        bench_resume(b);
        box_ui_update(world);
        box_render_extract(world, &packet);
        bench_keep(packet.views.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(tree);
    ecs_fini(world);
}
