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
    BOX_SHAPE2D_LINE = 2, /**< Line segment */
    BOX_SHAPE2D_POLYGON = 3 /**< Polygon2D, a filled outline or a stroked open path */
} box_shape2d_kind_t;

/** What the Rect, Circle, Line, Polygon2D, Border and Image of an entity resolve to.
 *
 * Added with every 2D shape; ShapeMesh holds the shared unit quad and the size of a rectangle.
 * A polygon scales the quad to bounds centered on the origin that hold its outline, twice the
 * largest distance of the bounds from the origin on each axis, as a line does; the shader draws
 * the bounds themselves (center and size) from its record (box_draws_polygon_of).
 */
typedef struct Shape2D {
    int32_t kind;           /**< box_shape2d_kind_t */
    float size[2];          /**< Width and height of a rectangle, of the bounds of a polygon */
    float center[2];        /**< Center of the bounds of a polygon */
    int32_t edges;          /**< Edges of a polygon, its outline and holes */
    float line[4];          /**< Start and end of a line, x1, y1, x2, y2 */
    float width;            /**< Width of a line, of the stroke of an open polygon, 0 for a
                             *   filled polygon */
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

/** Sample the edges of a Polygon2D: its outline and holes, scaled and offset.
 *
 * Every edge is a vec4 of its start x, y and end x, y. A closed outline or hole ends with the
 * edge back to its start; an open path is a stroke, its edges the segments of the polyline. A
 * Path3D is seen from above, its x and z as x and y.
 *
 * @param world The world
 * @param polygon The polygon
 * @param edges Receives the edges, appended as vec4
 * @param open Receives whether the path is open, a stroke
 * @return Whether the polygon has a valid path, false leaves edges as they were
 */
bool box_shape2d_polygon_edges(
    const ecs_world_t *world,
    const Polygon2D *polygon,
    ecs_vec_t *edges,
    bool *open);

/** Get the signed distance of a point to a polygon, negative inside, as shape2d.wgsl does.
 *
 * A filled polygon is inside where a ray from the point crosses its edges an odd number of times
 * (the even-odd rule); a stroke within half its width of an edge.
 *
 * @param edges The edges, box_shape2d_polygon_edges
 * @param count Number of edges
 * @param stroke Half the width of a stroke, 0 for a filled polygon
 * @param x X of the point
 * @param y Y of the point
 * @return The distance
 */
float box_shape2d_polygon_distance(
    const vec4 *edges,
    int32_t count,
    float stroke,
    float x,
    float y);

/** Check whether a point of the local plane of an entity is inside its Polygon2D.
 *
 * @param world The world
 * @param e The entity
 * @param x X of the point
 * @param y Y of the point
 * @return Whether it is inside, false for an entity without a drawn Polygon2D
 */
bool box_shape2d_polygon_contains(
    const ecs_world_t *world,
    ecs_entity_t e,
    float x,
    float y);

/** Register the 2D shapes: Rect, Circle, Line and Polygon2D, with Border, Image and ImageSlice.
 *
 * @param world The world
 */
void box_geometry_shapes2d_init(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
