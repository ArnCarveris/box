/**
 * @file box/lod.h
 * @brief Hierarchical LOD: the proxy generator that replaces a subtree with fewer primitives.
 *
 * The generator is a pure function over the primitives of a subtree: it clusters
 * them by material key and proximity and emits a handful of boxes, spheres and
 * capsules that stand in for the whole, together with the approximation error in
 * world units that the screen space metric later turns into pixels. A builder
 * holds the working memory, so a subtree that is rebuilt does not allocate.
 *
 * The module also owns the screen space metric that turns such an error into
 * pixels: box_lod_error_px projects it at the nearest point of the bounds of a
 * node for a perspective or an orthographic camera, and box_lod_select turns the
 * projected errors of the levels of a node into the level of this frame, keeping
 * the current one until the error leaves the band around the threshold.
 */

#ifndef BOX_LOD_H
#define BOX_LOD_H

#include "types.h"
#include "geometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Shapes a proxy aims for when its description asks for none */
#define BOX_LOD_MAX_SHAPES (8)

/** Refinement passes one cluster count takes when its description asks for none */
#define BOX_LOD_ITERATIONS (8)

/** Segments a generated sphere or capsule starts at, an upper bound for reduction */
#define BOX_LOD_SEGMENTS (8)

/** One primitive of the subtree a proxy replaces.
 *
 * The generator works on the world aligned bounds of the local box, so a shape
 * hands it the transform of its entity and the bounds of its mesh. Primitives
 * that carry a different material key never end up in the same proxy shape.
 */
typedef struct box_lod_primitive_t {
    mat4 transform;    /**< World transform of the shape */
    vec3 min;          /**< Minimum corner of the bounds, in the space of the shape */
    vec3 max;          /**< Maximum corner of the bounds, in the space of the shape */
    uint64_t material; /**< Material key, opaque to the generator */
    float volume;      /**< Volume the shape fills, 0 to take the volume of its bounds */
} box_lod_primitive_t;

/** Primitive a generated proxy shape uses */
typedef enum box_lod_shape_kind_t {
    BoxLodShapeBox = 0,    /**< Axis aligned box, which every cluster falls back to */
    BoxLodShapeSphere = 1, /**< Sphere, for a cluster whose bounds are near cubic */
    BoxLodShapeCapsule = 2 /**< Capsule along Y, for a cluster that is tall and round */
} box_lod_shape_kind_t;

/** One primitive the generator emits in place of a cluster of inputs.
 *
 * The shape is axis aligned in world space and needs no rotation: only the
 * member of the kind it names is filled, the other two are zero.
 */
typedef struct box_lod_shape_t {
    box_lod_shape_kind_t kind; /**< Which of the three shapes below is filled */
    vec3 position;             /**< Center of the shape in world space */
    Box box;                   /**< Size when the kind is a box */
    Sphere sphere;             /**< Radius when the kind is a sphere */
    Capsule capsule;           /**< Radius and length when the kind is a capsule */
    uint64_t material;         /**< Material key every input of the cluster shares */
    int32_t count;             /**< Inputs the shape stands in for */
    float error;               /**< Approximation error of the shape in world units */
} box_lod_shape_t;

/** The proxy a subtree collapses into.
 *
 * The shapes point into the builder that produced them and stay valid until the
 * same builder runs again. The error is the edge of the volume the proxy gets
 * wrong against the union of the inputs, so it is a length in world units.
 */
typedef struct box_lod_proxy_t {
    const box_lod_shape_t *shapes; /**< Shapes of the proxy, owned by the builder */
    int32_t count;                 /**< Number of shapes */
    float error;                   /**< Approximation error of the proxy in world units */
    vec3 min;                      /**< Minimum corner of the world bounds of the inputs */
    vec3 max;                      /**< Maximum corner of the world bounds of the inputs */
} box_lod_proxy_t;

/** What a proxy is allowed to cost.
 *
 * The generator stops splitting once the shape count reaches the target, unless
 * the error is still above the bound: then it spends more shapes on the cluster
 * that misses the most, up to one shape per input.
 */
typedef struct box_lod_proxy_desc_t {
    const box_lod_primitive_t *primitives; /**< Inputs of the subtree */
    int32_t count;      /**< Number of inputs */
    int32_t max_shapes; /**< Shapes the proxy aims for, 0 for BOX_LOD_MAX_SHAPES */
    float max_error;    /**< Error bound in world units, 0 for no bound */
    int32_t iterations; /**< Refinement passes per cluster count, 0 for BOX_LOD_ITERATIONS */
} box_lod_proxy_desc_t;

/** Working memory of the proxy generator.
 *
 * A builder is initialised once and reused for every subtree, which is what
 * keeps a steady state rebuild free of allocations. Its vectors hold private
 * types, so only the generator reads them.
 */
typedef struct box_lod_builder_t {
    ecs_vec_t inputs;   /**< World bounds and weight of every input */
    ecs_vec_t groups;   /**< One group per material key of the inputs */
    ecs_vec_t clusters; /**< Clusters of the group being refined */
    ecs_vec_t members;  /**< Cluster index of every input */
    ecs_vec_t nearest;  /**< Distance of every input to its nearest seed */
    ecs_vec_t shapes;   /**< Shapes of the proxy the last build produced */
} box_lod_builder_t;

/** Initialise a proxy builder.
 *
 * @param builder The builder
 */
void box_lod_builder_init(
    box_lod_builder_t *builder);

/** Release the working memory of a proxy builder.
 *
 * @param builder The builder
 */
void box_lod_builder_fini(
    box_lod_builder_t *builder);

/** Build the proxy of a subtree.
 *
 * The result is the same for the same inputs, and the shapes it points at stay
 * valid until the builder runs again.
 *
 * @param builder The builder that holds the working memory
 * @param desc The inputs and the budget of the proxy
 * @param out Receives the proxy
 * @return True when the proxy has shapes, false for an empty subtree
 */
bool box_lod_proxy_build(
    box_lod_builder_t *builder,
    const box_lod_proxy_desc_t *desc,
    box_lod_proxy_t *out);

/** Default vertical field of view the metric projects a camera with, in degrees */
#define BOX_LOD_FOV (60.0f)

/** Default visible height in world units of an orthographic camera */
#define BOX_LOD_ORTHO_HEIGHT (10.0f)

/** Distance the perspective metric clamps to, so a camera in the bounds stays finite */
#define BOX_LOD_MIN_DISTANCE (0.001f)

/** World bounds of a node, which the screen space metric projects.
 *
 * The corners may come in either order: the metric normalizes them, so the
 * bounds of an empty subtree are harmless.
 */
typedef struct box_lod_bounds_t {
    vec3 min; /**< One corner of the bounds in world space */
    vec3 max; /**< The opposite corner of the bounds in world space */
} box_lod_bounds_t;

/** The camera a screen space error is measured in.
 *
 * A perspective camera projects with its vertical field of view and the
 * distance to the bounds, so its error falls off with distance; an orthographic
 * camera projects with its visible height alone and its error does not.
 */
typedef struct box_lod_camera_t {
    vec3 position;      /**< Camera position in world space */
    float fov;          /**< Vertical field of view in degrees, 0 for BOX_LOD_FOV */
    float ortho_height; /**< Visible height in world units, 0 for BOX_LOD_ORTHO_HEIGHT */
    bool orthographic;  /**< The camera projects orthographically */
} box_lod_camera_t;

/** The levels of a node and the band the switch between them needs.
 *
 * The levels run from the most detailed to the cheapest, so their projected
 * error rises with the level. The band is a fraction of the threshold: a
 * hysteresis of 0.25 lets a node keep its level until its error leaves
 * [threshold * 0.75, threshold * 1.25], which is what keeps a camera hovering
 * at a boundary from switching every frame.
 */
typedef struct box_lod_select_desc_t {
    const float *error_px; /**< Projected error in pixels of every level */
    int32_t count;         /**< Number of levels */
    int32_t current;       /**< Level of the previous frame, negative for none */
    float threshold;       /**< Projected error in pixels a level may not exceed */
    float hysteresis;      /**< Band around the threshold, 0 switches at it, at most 1 */
} box_lod_select_desc_t;

/** Pixels one world unit covers at the nearest point of bounds.
 *
 * The projected error of a level is this scale times the error of its
 * representation, so a caller with more than one level computes the scale once.
 *
 * @param bounds World bounds of the node
 * @param camera The camera the error is measured in
 * @param viewport_px Height of the viewport in pixels
 * @return The pixels one world unit covers, 0 for an empty viewport
 */
float box_lod_error_scale(
    const box_lod_bounds_t *bounds,
    const box_lod_camera_t *camera,
    float viewport_px);

/** Projected error of a representation in pixels.
 *
 * The error of the representation is a length in world units, which the metric
 * projects at the nearest point of the bounds, the point where it shows most.
 *
 * @param bounds World bounds of the node
 * @param error Error of the representation in world units
 * @param camera The camera the error is measured in
 * @param viewport_px Height of the viewport in pixels
 * @return The error in pixels, 0 for a representation without error
 */
float box_lod_error_px(
    const box_lod_bounds_t *bounds,
    float error,
    const box_lod_camera_t *camera,
    float viewport_px);

/** Pick the level of a node, keeping the current one inside the band.
 *
 * The node moves to a coarser level only when that level stays under the
 * narrowed threshold, and to a finer one only when its current level passes the
 * widened threshold. Level zero is the floor: a node whose finest level already
 * exceeds the threshold keeps it.
 *
 * @param desc The levels, the current level and the band
 * @return The level of this frame
 */
int32_t box_lod_select(
    const box_lod_select_desc_t *desc);

/** Register the box.lod module.
 *
 * @param world The world
 */
void BoxLodImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
