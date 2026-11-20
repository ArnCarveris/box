/**
 * @file fps/gpu.h
 * @brief The stats of the GPU stats panel: what the renderer and the GPU did per frame.
 */

#ifndef BOX_FPS_GPU_H
#define BOX_FPS_GPU_H

#include <box.h>

/** Register the hooks of GpuPanel, the script functions box.fps.count and box.fps.bytes that
 * the panel formats its numbers with, and the system that writes GpuStats while it is open.
 *
 * @param world The world
 * @param module The box.fps module
 */
void box_fps_gpu_init(
    ecs_world_t *world,
    ecs_entity_t module);

#endif
