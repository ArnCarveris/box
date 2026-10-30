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

/** Distance from a point to an edge, ordered by magnitude, then by how head on the edge is */
typedef struct box_msdf_dist_t {
    double distance; /**< Signed distance in font units, positive inside */
    double dot;      /**< Cosine between the edge end and the point, 0 along the edge */
} box_msdf_dist_t;

/** Nearest edge of one channel among a set of edges, after msdfgen's perpendicular selector */
typedef struct box_msdf_sel_t {
    box_msdf_dist_t near;  /**< True distance to the nearest edge */
    double neg;            /**< Nearest perpendicular distance beyond an edge end that is <= 0 */
    double pos;            /**< Nearest perpendicular distance beyond an edge end that is >= 0 */
    int32_t edge;          /**< Index of the nearest edge, -1 before one is found */
    double param;          /**< Parameter of the nearest point, outside 0..1 beyond an end */
} box_msdf_sel_t;

/** Nearest edges of red, green and blue among a set of edges */
typedef struct box_msdf_multi_t {
    box_msdf_sel_t c[3]; /**< Red, green and blue */
} box_msdf_multi_t;

/** Distances of a point: red, green, blue and the true distance, positive inside */
typedef struct box_msdf_value_t {
    double v[4]; /**< Red, green, blue and true distance in font units */
} box_msdf_value_t;

/** Constants of an edge that every texel uses, computed once per field */
typedef struct box_msdf_prep_t {
    box_msdf_point_t a_dir; /**< Unit direction at the start, zero for a degenerate edge */
    box_msdf_point_t b_dir; /**< Unit direction at the end, zero for a degenerate edge */
    box_msdf_point_t a_bis; /**< Unit bisector of the corner at the start */
    box_msdf_point_t b_bis; /**< Unit bisector of the corner at the end */
    box_msdf_point_t min;   /**< Lower corner of the box around the points of the edge */
    box_msdf_point_t max;   /**< Upper corner of the box around the points of the edge */
    double extent;          /**< Largest magnitude of a coordinate of the edge, plus one */
} box_msdf_prep_t;

/** Terms of an end of an edge that rule out its perpendicular distance at the texels of a row.
 * At a texel x, x - x0 times the direction plus the row term gives the distance ahead of the
 * end, the distance on the side of the corner bisector and the perpendicular distance. */
typedef struct box_msdf_cull_end_t {
    double x0;     /**< x of the end */
    double ux;     /**< x of the unit direction away from the edge */
    double uy;     /**< y of the unit direction away from the edge */
    double bx;     /**< x of the unit bisector, pointing away from the corner */
    double ahead;  /**< Row term of the distance ahead of the end */
    double side;   /**< Row term of the distance along the bisector */
    double perp;   /**< Row term of the perpendicular distance */
} box_msdf_cull_end_t;

/** Constants of an edge for the texels of one row, to skip the edge where it cannot matter */
typedef struct box_msdf_cull_t {
    box_msdf_cull_end_t a; /**< The start */
    box_msdf_cull_end_t b; /**< The end */
    double dy;             /**< Distance from the row to the box around the edge */
    double slack;          /**< Margin for the rounding of the row, without the texel x */
} box_msdf_cull_t;

/** Edges of a contour that are not culled along a span of a row */
typedef struct box_msdf_span_t {
    double x1;       /**< x of the last texel of the span, the span starts anew after it */
    double limit[3]; /**< Largest limit of each channel at a texel the edges hold for */
    int32_t count;   /**< Number of edges, stored from the first edge of the contour on */
} box_msdf_span_t;

/** State of the generation of one field */
typedef struct box_msdf_gen_t {
    const box_msdf_shape_t *shape;   /**< The shape */
    const box_msdf_raster_t *raster; /**< The texels */
    int32_t *windings;               /**< Orientation of each contour: 1, -1 or 0 */
    box_msdf_prep_t *prep;           /**< Constants of each edge */
    box_msdf_cull_t *cull;           /**< Constants of each edge for the current row */
    box_msdf_span_t *span;           /**< Span of each contour in the current row */
    int32_t *span_edges;             /**< Edges of the spans, in the slots of each contour */
    box_msdf_multi_t *multi;         /**< Scratch selectors, one per contour, the nearest edges
                                      *   of the previous texel until a texel resets them */
    box_msdf_value_t *contour;       /**< Scratch distances, one per contour */
    float *values;                   /**< Four floats per texel, 0.5 on the outline */
    uint8_t *stencil;                /**< BOX_MSDF_ERROR and BOX_MSDF_PROTECTED per texel */
} box_msdf_gen_t;

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

/** Solve a x^2 + b x + c = 0.
 *
 * @param x Receives the roots
 * @param a The quadratic coefficient
 * @param b The linear coefficient
 * @param c The constant
 * @return The number of roots
 */
int32_t box_msdf_quadratic(
    double x[2],
    double a,
    double b,
    double c);

/** Median of three floats.
 *
 * @param a The first value
 * @param b The second value
 * @param c The third value
 * @return The median
 */
float box_msdf_median3(
    float a,
    float b,
    float c);

/** Median of the three channels of a texel.
 *
 * @param v The channels
 * @return The median
 */
float box_msdf_median(
    const float *v);

/** Correct the artifacts of the field after msdfgen's MSDFErrorCorrection without a distance
 * check: protect corners and edges, then flatten the texels whose interpolation leaves the range
 * their distance allows.
 *
 * @param gen The generation
 */
void box_msdf_correct(
    box_msdf_gen_t *gen);

#endif
