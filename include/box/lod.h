/**
 * @file box/lod.h
 * @brief Hierarchical LOD: the proxy generator that replaces a subtree with fewer primitives.
 *
 * The generator is a pure function over the primitives of a subtree: it clusters
 * them by material key and proximity and emits a handful of boxes, spheres and
 * capsules that stand in for the whole, together with the approximation error in
 * world units that the screen space metric later turns into pixels. A builder
 * holds the working memory, so a subtree that is rebuilt does not allocate.
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
