/**
 * @file command/cmd_counts.c
 * @brief The counts command: the entities, meshes, batches, instance slots and texture graphs
 * alive and the GPU memory the renderer counts, to see whether they level off over a long run.
 *
 * The GPU memory is the sum of the buffers and textures the renderer keeps counts of: the
 * instance pages, the texture arrays of the blend layers and the values, pooled textures and key
 * images of the texture graphs. Meshes, loaded textures and render targets are not counted.
 */

#include "command.h"
#include "../render/render.h"

/** Bytes per megabyte */
#define BOX_CMD_COUNTS_MB (1048576.0)

/** The counts command: print the counts in one line.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the counts
 */
static void box_cmd_counts(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entities_t entities = ecs_get_entities(world);
    box_render_draws_stats_t draws;
    box_render_gpu_stats_t gpu;
    box_render_draws_stats(world, &draws);
    box_render_gpu_stats(world, &gpu);

    int64_t graph_bytes = gpu.texture_graph_cached_bytes + gpu.texture_graph_pool_bytes +
        gpu.texture_graph_key_bytes;
    int64_t gpu_bytes = draws.page_bytes + gpu.blend_bytes + graph_bytes;
    *(char**)result->ptr = flecs_asprintf(
        "entities %d  meshes %d  batches %d  instances %d  slots %lld  pages %d  "
        "materials %d  graphs %d  gpu %.1f MB (pages %.1f, blend %.1f, graphs %.1f)",
        entities.alive_count, cache ? box_mesh_cache_count(cache) : 0,
        draws.batches + draws.impostor_batches, draws.instances + draws.dynamic,
        (long long)draws.page_slots, draws.pages, draws.materials, gpu.texture_graphs,
        (double)gpu_bytes / BOX_CMD_COUNTS_MB, (double)draws.page_bytes / BOX_CMD_COUNTS_MB,
        (double)gpu.blend_bytes / BOX_CMD_COUNTS_MB, (double)graph_bytes / BOX_CMD_COUNTS_MB);
}

void box_cmd_counts_init(
    ecs_world_t *world)
{
    box_command_register(world,
        "Print the entities, meshes, batches, instance slots and texture graphs alive and the "
        "GPU memory the renderer counts",
        (ecs_function_desc_t){
            .name = "counts",
            .return_type = ecs_id(ecs_string_t),
            .callback = box_cmd_counts
        });
}
