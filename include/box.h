/**
 * @file box.h
 * @brief Public header for the box framework.
 *
 * Includes flecs and every box module header so applications only need this file.
 */

#ifndef BOX_H
#define BOX_H

#include <flecs.h>

#include "box/types.h"
#include "box/math.h"
#include "box/core.h"
#include "box/job.h"
#include "box/transform.h"
#include "box/camera.h"
#include "box/view.h"
#include "box/light.h"
#include "box/material.h"
#include "box/geometry.h"
#include "box/gltf.h"
#include "box/transition.h"
#include "box/render.h"
#include "box/shader.h"
#include "box/effects.h"
#include "box/texture.h"
#include "box/movement.h"
#include "box/collision.h"
#include "box/picking.h"
#include "box/spline.h"
#include "box/particle.h"
#include "box/lod.h"
#include "box/controller.h"
#include "box/surface.h"
#include "box/sky.h"
#include "box/input.h"
#include "box/command.h"
#include "box/checkpoint.h"
#include "box/monitor.h"
#include "box/ui.h"
#include "box/script.h"
#include "box/draw.h"
#include "box/editor.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register the box module, which imports every module of the framework.
 *
 * An application or a test imports this module and no other one, so every
 * world registers the modules in the same order and their component ids are
 * the same in every world.
 *
 * @param world The world
 */
void BoxImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
