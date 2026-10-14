/**
 * @file box.h
 * @brief Includes the headers of every box module.
 */

#ifndef BOX_H
#define BOX_H

/* This generated file contains includes for project dependencies */
#include "box/bake_config.h"
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
#include "box/app.h"
#include "box/render.h"
#include "box/command.h"

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

