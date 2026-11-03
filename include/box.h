/**
 * @file box.h
 * @brief Includes the headers of every box module.
 */

#ifndef BOX_H
#define BOX_H

/* This generated file contains includes for project dependencies */
#include "box/bake_config.h"
#include "box/curve.h"
#include "box/transform.h"
#include "box/geometry.h"
#include "box/camera.h"
#include "box/light.h"
#include "box/texture.h"
#include "box/material.h"
#include "box/surface.h"
#include "box/view.h"
#include "box/effects.h"
#include "box/movement.h"
#include "box/layout.h"
#include "box/text.h"
#include "box/app.h"
#include "box/audio.h"
#include "box/render.h"
#include "box/shader.h"
#include "box/command.h"
#include "box/input.h"
#include "box/controllers.h"
#include "box/controllers.h"
#include "box/perf.h"
#include "box/projection.h"
#include "box/spatial.h"
#include "box/picking.h"
#include "box/ui.h"
#include "box/console.h"
#include "box/editor.h"
#include "box/fps.h"
#include "box/atmosphere.h"
#include "box/particle.h"
#include "box/transition.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register the box module and every module it contains.
 *
 * @param world The world
 */
void BoxImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif

