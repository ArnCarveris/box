/**
 * @file geometry/shape2d.h
 * @brief Private resolved form of the 2D shapes, read by the renderer to fill instances.
 */

#ifndef BOX_GEOMETRY_SHAPE2D_H
#define BOX_GEOMETRY_SHAPE2D_H

#include "mesh_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Kind of a resolved 2D shape */
typedef enum box_shape2d_kind_t {
    BOX_SHAPE2D_NONE = 0, /**< The entity has no 2D shape */
    BOX_SHAPE2D_RECT = 1, /**< Rounded rectangle, a Rect or a Circle */
    BOX_SHAPE2D_LINE = 2  /**< Line segment */
} box_shape2d_kind_t;

/** What the Rect, Circle, Line, Border and Image of an entity resolve to.
 *
 * Added with every 2D shape; ShapeMesh holds the shared unit quad and the size of a rectangle.
 */
typedef struct Shape2D {
    int32_t kind;           /**< box_shape2d_kind_t */
    float size[2];          /**< Width and height of a rectangle */
    float line[4];          /**< Start and end of a line, x1, y1, x2, y2 */
    float width;            /**< Width of a line */
    float radius;           /**< Corner radius of a rectangle */
    float border;           /**< Width of the border */
    rgba border_color;      /**< Color of the border, display encoded */
    float uv[4];            /**< Image area: u scale, v scale, u offset, v offset */
    ecs_entity_t texture;   /**< Texture of the Image, 0 for none */
    bool sliced;            /**< A Rect whose Image has an ImageSlice, drawn by the slice
                             *   pipeline of the renderer with uv holding its slice index */
} Shape2D;

/** Component id of Shape2D */
extern ECS_COMPONENT_DECLARE(Shape2D);

/** Register the 2D shapes: Rect, Circle and Line, with Border, Image and ImageSlice.
 *
 * @param world The world
 */
void box_geometry_shapes2d_init(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
