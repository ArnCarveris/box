/**
 * @file text/msdf.h
 * @brief Multi channel signed distance fields of glyph outlines, generated on a worker thread.
 */

#ifndef BOX_TEXT_MSDF_H
#define BOX_TEXT_MSDF_H

#include <box.h>

/** A point of an outline in font units, y up */
typedef struct box_msdf_point_t {
    double x; /**< Horizontal coordinate */
    double y; /**< Vertical coordinate */
} box_msdf_point_t;

/** An edge of an outline: a line or a quadratic curve, with the channels it shapes */
typedef struct box_msdf_edge_t {
    box_msdf_point_t p[3]; /**< Start, control and end; a line keeps its midpoint as control */
    int32_t degree;        /**< 1 for a line, 2 for a quadratic curve */
    int32_t color;         /**< Channels of the edge, a bit each for red, green and blue */
} box_msdf_edge_t;

/** A closed outline made of contours of edges */
typedef struct box_msdf_shape_t {
    ecs_vec_t edges;        /**< box_msdf_edge_t, the edges of each contour follow each other */
    ecs_vec_t contours;     /**< int32_t, edges per contour */
    box_msdf_point_t start; /**< First point of the contour being built */
    box_msdf_point_t pen;   /**< Last point of the contour being built */
    bool open;              /**< A contour is being built */
} box_msdf_shape_t;

/** Area of texels a field is generated for */
typedef struct box_msdf_raster_t {
    double scale;   /**< Texels per font unit */
    double left;    /**< Font x of the left edge of the first column */
    double top;     /**< Font y of the top edge of the first row */
    int32_t width;  /**< Columns */
    int32_t height; /**< Rows */
    double range;   /**< Texels of distance the 0 to 1 of a channel spans */
} box_msdf_raster_t;

/** Initialize an empty shape.
 *
 * @param shape The shape
 */
void box_msdf_shape_init(
    box_msdf_shape_t *shape);

/** Free the edges of a shape.
 *
 * @param shape The shape
 */
void box_msdf_shape_fini(
    box_msdf_shape_t *shape);

/** Close the contour being built and start a new one.
 *
 * @param shape The shape
 * @param x Font x of the first point
 * @param y Font y of the first point
 */
void box_msdf_move_to(
    box_msdf_shape_t *shape,
    double x,
    double y);

/** Add a line from the pen to a point.
 *
 * @param shape The shape
 * @param x Font x of the end
 * @param y Font y of the end
 */
void box_msdf_line_to(
    box_msdf_shape_t *shape,
    double x,
    double y);

/** Add a quadratic curve from the pen to a point.
 *
 * @param shape The shape
 * @param cx Font x of the control point
 * @param cy Font y of the control point
 * @param x Font x of the end
 * @param y Font y of the end
 */
void box_msdf_quad_to(
    box_msdf_shape_t *shape,
    double cx,
    double cy,
    double x,
    double y);

/** Add a cubic curve from the pen to a point, approximated by quadratic curves.
 *
 * @param shape The shape
 * @param c1x Font x of the first control point
 * @param c1y Font y of the first control point
 * @param c2x Font x of the second control point
 * @param c2y Font y of the second control point
 * @param x Font x of the end
 * @param y Font y of the end
 */
void box_msdf_cubic_to(
    box_msdf_shape_t *shape,
    double c1x,
    double c1y,
    double c2x,
    double c2y,
    double x,
    double y);

/** Close the last contour and color the edges of every contour.
 *
 * @param shape The shape
 */
void box_msdf_shape_finish(
    box_msdf_shape_t *shape);

/** Generate the field of a shape: RGB the multi channel distance, A the true distance.
 *
 * A value of 0.5 is on the outline, larger values are inside.
 *
 * @param shape The finished shape
 * @param raster The texels to fill
 * @param pixels RGBA8 rows of width texels
 */
void box_msdf_generate(
    const box_msdf_shape_t *shape,
    const box_msdf_raster_t *raster,
    uint8_t *pixels);

#endif
