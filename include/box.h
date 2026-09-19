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

#endif
