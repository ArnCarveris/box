/**
 * @file box/lod.h
 * @brief Hierarchical LOD: the proxy generator that replaces a subtree with fewer primitives.
 *
 * Generates proxies from the primitives of a subtree: clusters by material key and proximity
 * become a few boxes, spheres and capsules with an error in world units. A builder holds the
 * working memory, so rebuilding does not allocate.
 *
 * box_lod_error_px projects such an error to pixels for perspective or orthographic cameras and
 * box_lod_select picks the level of a node, keeping the current one inside a hysteresis band.
 */

#ifndef BOX_LOD_H
#define BOX_LOD_H

#include "types.h"
#include "geometry.h"

#ifdef BOX_LOD_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

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
 * Levels run from most detailed to cheapest, so error rises with the level. The band is a
 * fraction of the threshold: 0.25 keeps a level while its error is within [threshold * 0.75,
 * threshold * 1.25], so a camera hovering at a boundary does not switch every frame.
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
 * A node moves coarser only when that level stays under the narrowed threshold, and finer only
 * when its current level passes the widened one. Level zero is the floor even when it exceeds
 * the threshold.
 *
 * @param desc The levels, the current level and the band
 * @return The level of this frame
 */
int32_t box_lod_select(
    const box_lod_select_desc_t *desc);

/** Part of the triangles a simplification keeps when its description asks for none */
#define BOX_LOD_SIMPLIFY_RATIO (0.25f)

/** Part of a merged mesh whose triangles share one material key.
 *
 * Groups are sorted by material key and their index ranges do not overlap, so
 * one group is one draw call over the range of indices it names.
 */
typedef struct box_lod_mesh_group_t {
    uint64_t material;   /**< Material key every triangle of the group shares */
    int32_t first_index; /**< First index of the group in the merged mesh */
    int32_t index_count; /**< Indices the group holds */
    float error;         /**< Error the simplification of the group cost, in world units */
} box_lod_mesh_group_t;

/** One mesh of the subtree a merge collapses.
 *
 * A source names its entity like box_lod_primitive_t, with a world transform and material key,
 * and hands over Mesh3 arrays instead of bounds, so authored and cached meshes merge without a
 * copy.
 */
typedef struct box_lod_mesh_source_t {
    mat4 transform;          /**< World transform of the mesh */
    const vec3 *vertices;    /**< Vertex positions in the space of the transform */
    const vec3 *normals;     /**< Vertex normals, NULL to derive them from the triangles */
    const vec2 *uvs;         /**< Texture coordinates, NULL for zeros */
    const rgba *colors;      /**< Vertex colors, NULL for opaque white */
    const uint32_t *indices; /**< Three indices per triangle, NULL to read them in order */
    int32_t vertex_count;    /**< Vertices the mesh holds */
    int32_t index_count;     /**< Indices the mesh holds, 0 to take the vertex count */
    uint64_t material;       /**< Material key, opaque to the merge */
} box_lod_mesh_source_t;

/** What a merge is allowed to do.
 *
 * Welding joins vertices at the same world position whatever their normals and averages the
 * normals, letting simplification cross source seams. Without it vertices only join when all
 * attributes match, keeping every hard edge.
 */
typedef struct box_lod_merge_desc_t {
    const box_lod_mesh_source_t *sources; /**< Meshes of the subtree */
    int32_t count;                        /**< Number of meshes */
    bool weld;                            /**< Join the vertices that share a position */
} box_lod_merge_desc_t;

/** The mesh a subtree merges into.
 *
 * The arrays point into the builder and stay valid until it runs again. Positions and normals
 * are in world space, groups split indices by material key, and the error is how far the surface
 * moved in world units, zero for a mesh that was only merged.
 */
typedef struct box_lod_mesh_t {
    const vec3 *vertices;               /**< World position of every vertex */
    const vec3 *normals;                /**< World normal of every vertex, unit length */
    const vec2 *uvs;                    /**< Texture coordinate of every vertex */
    const rgba *colors;                 /**< Color of every vertex */
    const uint32_t *indices;            /**< Three indices per triangle */
    int32_t vertex_count;               /**< Vertices the mesh holds */
    int32_t index_count;                /**< Indices the mesh holds */
    const box_lod_mesh_group_t *groups; /**< One group per material key of the sources */
    int32_t group_count;                /**< Number of groups */
    vec3 min;                           /**< Minimum corner of the world bounds */
    vec3 max;                           /**< Maximum corner of the world bounds */
    float error;                        /**< Error of the mesh in world units */
} box_lod_mesh_t;

/** What a simplification aims for.
 *
 * The simplifier stops at the first goal it reaches: never fewer triangles than the ratio and
 * never more surface movement than the bound, so a group may keep more triangles than asked.
 */
typedef struct box_lod_simplify_desc_t {
    float ratio;      /**< Part of the triangles to keep, 0 for BOX_LOD_SIMPLIFY_RATIO */
    float max_error;  /**< Error bound in world units, 0 for the size of the mesh */
    bool lock_border; /**< Leave the vertices on the border of the mesh where they are */
} box_lod_simplify_desc_t;

/** Working memory of the mesh merge and of its simplification.
 *
 * A builder holds one merged mesh, which the simplification rewrites in place,
 * and is reused for every subtree, which is what keeps a rebuild free of
 * allocations. Its vectors are read through the mesh a build hands out.
 */
typedef struct box_lod_mesh_builder_t {
    ecs_vec_t vertices; /**< World position of every vertex */
    ecs_vec_t normals;  /**< Summed world normal of every vertex */
    ecs_vec_t uvs;      /**< Texture coordinate of every vertex */
    ecs_vec_t colors;   /**< Color of every vertex */
    ecs_vec_t keys;     /**< Normal a vertex was matched on, which its sum loses */
    ecs_vec_t indices;  /**< Three indices per triangle */
    ecs_vec_t groups;   /**< One group per material key of the sources */
    ecs_vec_t table;    /**< Open addressed table the merge looks vertices up in */
    ecs_vec_t scratch;  /**< Bytes the merge orders sources in and the simplification remaps in */
    vec3 min;           /**< Minimum corner of the world bounds of the mesh */
    vec3 max;           /**< Maximum corner of the world bounds of the mesh */
} box_lod_mesh_builder_t;

/** Initialise a mesh builder.
 *
 * @param builder The builder
 */
void box_lod_mesh_builder_init(
    box_lod_mesh_builder_t *builder);

/** Release the working memory of a mesh builder.
 *
 * @param builder The builder
 */
void box_lod_mesh_builder_fini(
    box_lod_mesh_builder_t *builder);

/** Merge the meshes of a subtree into one world space mesh.
 *
 * Every source is transformed into world space and appended to the group of its
 * material key, so the result is one vertex buffer and one index buffer with a
 * range per material. The result is the same for the same inputs.
 *
 * @param builder The builder that holds the mesh
 * @param desc The meshes of the subtree
 * @param out Receives the merged mesh
 * @return True when the mesh has triangles, false for an empty subtree
 */
bool box_lod_merge(
    box_lod_mesh_builder_t *builder,
    const box_lod_merge_desc_t *desc,
    box_lod_mesh_t *out);

/** Simplify the mesh a builder holds, in place.
 *
 * Each group is simplified on its own so triangles keep their material, unused vertices are
 * dropped and the mesh error is the largest group error in world units. The result is
 * deterministic for the same mesh.
 *
 * @param builder The builder holding the mesh a merge produced
 * @param desc What the simplification aims for
 * @param out Receives the simplified mesh
 * @return True when the mesh has triangles, false for an empty mesh
 */
bool box_lod_simplify(
    box_lod_mesh_builder_t *builder,
    const box_lod_simplify_desc_t *desc,
    box_lod_mesh_t *out);

/** Levels one reduction ladder can hold, which is the deepest halving it does */
#define BOX_LOD_LEVELS (6)

/** Segment count a reduced shape never goes below */
#define BOX_LOD_MIN_SEGMENTS (4)

/** The segment counts one parametric shape reduces through.
 *
 * Level zero is the authored count and each level after halves the one before until the floor
 * of the shape, so a shape authored at or below its floor has one level. Every level is an
 * ordinary mesh of the shape that instances at that level share.
 */
typedef struct box_lod_ladder_t {
    int32_t segments[BOX_LOD_LEVELS]; /**< Segment count of every level, the finest first */
    int32_t count;                    /**< Levels the ladder holds, at least one */
} box_lod_ladder_t;

/** What the reduction of one instance measures.
 *
 * The error of a level is the chord error of its segment count at the instance radius, in world
 * units. The span is the part of a full circle the segments cover, so arcs such as a rounded box
 * corner or an open tube sweep are measured as the full circle.
 */
typedef struct box_lod_reduce_desc_t {
    const box_lod_ladder_t *ladder; /**< Segment counts the shape reduces through */
    box_lod_bounds_t bounds;        /**< World bounds of the instance */
    box_lod_camera_t camera;        /**< Camera the error is measured in */
    float viewport_px;              /**< Height of the viewport in pixels */
    float radius;                   /**< Radius of the arc in world units */
    float span;                     /**< Part of a full circle the segments cover, 0 for all */
    float threshold;                /**< Projected error in pixels a level may not exceed */
    float hysteresis;               /**< Band around the threshold, 0 switches at it */
    int32_t current;                /**< Level of the previous frame, negative for none */
} box_lod_reduce_desc_t;

/** Build the level ladder of a parametric shape.
 *
 * @param authored Segment count the shape was authored with, the cap
 * @param min_segments Lowest segment count the shape stays usable at
 * @param out Receives the ladder
 * @return The number of levels, one for a shape that never reduces
 */
int32_t box_lod_ladder(
    int32_t authored,
    int32_t min_segments,
    box_lod_ladder_t *out);

/** Chord error of a segment count at a radius.
 *
 * The error is the sagitta of one segment: the distance between the arc and
 * the chord that replaces it, which is how far the surface of the shape moves
 * when it is drawn with that many segments.
 *
 * @param segments Segments a full circle is drawn with
 * @param radius Radius of the circle in world units
 * @return The error in world units, 0 for a degenerate circle
 */
float box_lod_chord_error(
    int32_t segments,
    float radius);

/** Pick the level of one instance from its projected chord error.
 *
 * The level of the previous frame is kept while its error stays inside the
 * band around the threshold, exactly like box_lod_select, whose levels this
 * builds from the ladder and the radius of the instance.
 *
 * @param desc The ladder, the instance and the threshold of the view
 * @return The level of this frame, zero for the authored segment count
 */
int32_t box_lod_reduce(
    const box_lod_reduce_desc_t *desc);

/** Subtree that detail pruning never drops.
 *
 * The tag holds the node it sits on and every node below it in the traversal,
 * so a sign, an antenna or any small part that must stay readable survives the
 * contribution test whatever its projected size is.
 */
extern ECS_TAG_DECLARE(LodPin);

/** What the traversal of one view did this frame, written on the view entity.
 *
 * The counts are the cost of the frame: visited is the nodes the walk looked at (which must not
 * grow with the leaves), drawn is the leaves resolved to an instance and proxies is the nodes
 * replaced by a cheaper representation.
 */
ECS_STRUCT(LodStats, {
    int32_t visited;  /**< Nodes the walk of this frame looked at */
    int32_t drawn;    /**< Leaves the walk resolved to an instance */
    int32_t proxies;  /**< Nodes the walk replaced by a proxy */
    int32_t rejected; /**< Nodes the frustum stopped */
    int32_t pruned;   /**< Nodes the contribution test dropped */
    int32_t nodes;    /**< Nodes the hierarchy holds */
    int32_t meshes;   /**< Proxies drawn as a merged and simplified mesh */
    int32_t shapes;   /**< Proxies drawn as a generated set of primitives */
    int32_t boxes;    /**< Proxies still drawn as the box over their bounds */
    int32_t impostors; /**< Proxies drawn as a billboard of their impostor atlas */
    int32_t queued;   /**< Proxies waiting for the generator */
    int32_t cached;   /**< Proxies the cache of the renderer holds */
});

/** The node has no children and stands for one renderable instance */
#define BOX_LOD_NODE_LEAF (1u)

/** The node groups roots the traversal put together, not an authored entity */
#define BOX_LOD_NODE_CLUSTER (2u)

/** The node or an ancestor carries LodPin, so pruning never drops it */
#define BOX_LOD_NODE_PIN (4u)

/** The children of the node exist, so the walk may descend into it */
#define BOX_LOD_NODE_OPEN (8u)

/** The walk of this frame decided to descend into the node */
#define BOX_LOD_VOTE_DESCEND (1u)

/** The walk of this frame decided to replace the node by its proxy */
#define BOX_LOD_VOTE_COLLAPSE (2u)

/** The walk of this frame resolved the node to a drawn leaf */
#define BOX_LOD_VOTE_DRAW (4u)

/** The walk of this frame dropped the node because it contributes too little */
#define BOX_LOD_VOTE_PRUNE (8u)

/** Part of its size a node contributes when its material matches its parent */
#define BOX_LOD_CONTRAST_FLOOR (0.25f)

/** Roots a tree keeps loose before it groups them into spatial clusters */
#define BOX_LOD_CLUSTER_MIN (16)

/** Roots one cluster node groups when the tree clusters loose roots */
#define BOX_LOD_CLUSTER_SIZE (8)

/** One node of the hierarchy the traversal walks.
 *
 * A node is a place in the tree: its id names the entity it was built from, or zero for a
 * cluster. Children are a linked list, so subtrees are materialised and released in place.
 * Interior bounds cover the subtree and the error is what its proxy loses, in world units.
 */
typedef struct box_lod_node_t {
    vec3 min;            /**< Minimum corner of the world bounds of the subtree */
    vec3 max;            /**< Maximum corner of the world bounds of the subtree */
    float error;         /**< Detail the proxy of the node loses, in world units */
    float weight;        /**< Volume the subtree of the node fills */
    float contribution;  /**< Size the node adds to its parent, in world units */
    float color[4];      /**< Linear albedo and opacity the subtree averages to */
    float material[2];   /**< Metallic and roughness the subtree averages to */
    uint64_t id;         /**< Entity the node was built from, 0 for a cluster */
    int32_t parent;      /**< Parent node, negative for a root */
    int32_t first_child; /**< First child node, negative for none */
    int32_t next;        /**< Next sibling, negative for the last */
    int32_t leaves;      /**< Leaves the subtree of the node holds */
    int32_t level;       /**< Level of the previous frame, 0 descends and 1 or more collapses */
    uint32_t flags;      /**< BOX_LOD_NODE bits of the node */
    uint32_t votes;      /**< BOX_LOD_VOTE bits the walks of this frame left */
    uint32_t stamp;      /**< Walk the votes belong to, which is what clears them */
} box_lod_node_t;

/** The node hierarchy the traversal walks, owned by the renderer.
 *
 * Nodes live in one vector named by index; released indices go to a free list and are reused.
 * The roots are a separate linked list, so template instances are added and dropped without
 * touching the rest of the tree.
 */
typedef struct box_lod_tree_t {
    ecs_vec_t nodes; /**< box_lod_node_t of the tree, live and free */
    ecs_vec_t free;  /**< int32_t indices of the nodes waiting for reuse */
    ecs_vec_t stack; /**< int32_t the aggregation and the release descend with */
    ecs_vec_t walk;  /**< int32_t the walk descends with, its own so a callback may build */
    ecs_vec_t order; /**< int32_t roots the clustering sorts */
    int32_t root;    /**< First root, negative for an empty tree */
    int32_t count;   /**< Live nodes the tree holds */
    uint32_t stamp;  /**< Walks the tree has run, which stamps the votes */
} box_lod_tree_t;

/** Culling of a view, which stops the walk at every node it rejects.
 *
 * @param ctx The context of the walk
 * @param node The node the walk reached
 * @return True when the view may see the node
 */
typedef bool (*box_lod_visible_t)(
    void *ctx,
    const box_lod_node_t *node);

/** Materialises the children of a node the walk wants to descend into.
 *
 * @param ctx The context of the walk
 * @param node Index of the node
 * @return True when the node is open afterwards
 */
typedef bool (*box_lod_open_t)(
    void *ctx,
    int32_t node);

/** Receives a node the walk resolved and the decision it took.
 *
 * @param ctx The context of the walk
 * @param node Index of the node
 * @param vote The BOX_LOD_VOTE bit the walk decided on
 */
typedef void (*box_lod_emit_t)(
    void *ctx,
    int32_t node,
    uint32_t vote);

/** Representations one node can be replaced by, the node itself excluded */
#define BOX_LOD_LEVEL_MAX (4)

/** Fills the error ladder of a node the walk is about to replace.
 *
 * Levels run from finest to cheapest, so errors rise with the level and the walk takes the
 * cheapest under the threshold. Without a generator every node has the single level its own
 * error describes.
 *
 * @param ctx The context of the walk
 * @param node Index of the node
 * @param error Receives the error in world units of every level
 * @return The number of levels, 0 to fall back to the error of the node
 */
typedef int32_t (*box_lod_levels_t)(
    void *ctx,
    int32_t node,
    float *error);

/** What one walk of the tree measures and what it reports its result to.
 *
 * The visible callback culls: false for a node the frustum or depth pyramid rejects stops the
 * walk there. The emit callback receives every node the walk resolved, as a leaf or a proxy.
 */
typedef struct box_lod_walk_desc_t {
    box_lod_camera_t camera;    /**< Camera of the view the walk measures in */
    float viewport_px;          /**< Height of the viewport in pixels */
    float threshold;            /**< Projected error in pixels a node may not exceed */
    float hysteresis;           /**< Band around the threshold a switch has to cross */
    float prune_px;             /**< Projected contribution a node is dropped under, 0 for none */
    uint32_t stamp;             /**< Frame the votes belong to, 0 to count this walk as one */
    box_lod_visible_t visible;  /**< Frustum of the view, NULL to accept every node */
    box_lod_visible_t hidden;   /**< Depth test of the view, NULL to test no node */
    box_lod_open_t open;        /**< Materialises the children of a node, NULL for none */
    box_lod_emit_t emit;        /**< Receives a resolved node, NULL to only count */
    box_lod_levels_t levels;    /**< Error ladder of a node, NULL for its own error */
    bool secondary;             /**< A pass beside the view, which never refines its choice */
    void *ctx;                  /**< Passed to the callbacks */
} box_lod_walk_desc_t;

/** What one walk of the tree cost and what it resolved.
 *
 * The counts are what LodStats publishes: a walk that stops at a proxy never
 * counts the leaves below it, which is what makes visited the cost of the
 * frame instead of the size of the scene.
 */
typedef struct box_lod_walk_stats_t {
    int32_t visited;  /**< Nodes the walk looked at */
    int32_t drawn;    /**< Leaves the walk resolved to an instance */
    int32_t proxies;  /**< Nodes the walk replaced by their proxy */
    int32_t rejected; /**< Nodes the frustum of the view stopped */
    int32_t occluded; /**< Nodes the depth test stopped at their proxy */
    int32_t pruned;   /**< Nodes the contribution test dropped */
} box_lod_walk_stats_t;

/** Initialise an empty tree.
 *
 * @param tree The tree
 */
void box_lod_tree_init(
    box_lod_tree_t *tree);

/** Release the memory of a tree.
 *
 * @param tree The tree
 */
void box_lod_tree_fini(
    box_lod_tree_t *tree);

/** Drop every node of a tree, keeping its memory.
 *
 * @param tree The tree
 */
void box_lod_tree_clear(
    box_lod_tree_t *tree);

/** The node an index names.
 *
 * @param tree The tree
 * @param node Index of the node, negative for none
 * @return The node, NULL when the index names none
 */
box_lod_node_t* box_lod_tree_get(
    const box_lod_tree_t *tree,
    int32_t node);

/** Add a node under a parent, or a root when the parent is negative.
 *
 * A new node is a leaf of zero size: a caller that gives it children turns it
 * into an interior node and box_lod_tree_refresh aggregates its bounds.
 *
 * @param tree The tree
 * @param parent Index of the parent, negative for a root
 * @param id Entity the node stands for, 0 for a cluster
 * @return Index of the new node
 */
int32_t box_lod_tree_add(
    box_lod_tree_t *tree,
    int32_t parent,
    uint64_t id);

/** Drop a node and every node below it.
 *
 * @param tree The tree
 * @param node Index of the node
 */
void box_lod_tree_remove(
    box_lod_tree_t *tree,
    int32_t node);

/** Drop the children of a node, keeping the node itself.
 *
 * This is how a collapsed subtree gives its nodes back: the node keeps its
 * bounds, its leaf count and its decision, and the walk stops at it until the
 * caller materialises its children again.
 *
 * @param tree The tree
 * @param node Index of the node
 */
void box_lod_tree_close(
    box_lod_tree_t *tree,
    int32_t node);

/** Give a leaf its world bounds and the detail it holds.
 *
 * @param tree The tree
 * @param node Index of the node
 * @param min Minimum corner of its world bounds
 * @param max Maximum corner of its world bounds
 * @param error Detail its proxy loses in world units, 0 for a leaf that loses none
 */
void box_lod_tree_set_bounds(
    box_lod_tree_t *tree,
    int32_t node,
    vec3 min,
    vec3 max,
    float error);

/** Give a leaf the look its proxy stands in for.
 *
 * The averages of an interior node are the averages of its leaves, weighted by
 * the volume each of them fills, so the box that replaces a subtree carries the
 * color the subtree had.
 *
 * @param tree The tree
 * @param node Index of the node
 * @param color Linear albedo and opacity of the leaf
 * @param metallic Metallic value of the leaf
 * @param roughness Roughness value of the leaf
 * @param weight Volume the leaf fills, 0 for one
 */
void box_lod_tree_set_look(
    box_lod_tree_t *tree,
    int32_t node,
    const float *color,
    float metallic,
    float roughness,
    float weight);

/** Thickness of the shell a box gets wrong against the volume it stands for.
 *
 * A bounding box proxy misses the volume between the box and what the subtree fills; spread over
 * the box surface that volume is the average distance between the two surfaces, which the screen
 * space metric projects.
 *
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param filled Volume the subtree fills
 * @return The thickness in world units, 0 for a box the subtree fills
 */
float box_lod_tree_shell(
    vec3 min,
    vec3 max,
    float filled);

/** Aggregate the bounds, the leaf count and the error of every interior node.
 *
 * A node that is not open keeps the bounds, averages and leaf count the caller gave it, so a
 * released root still stands for its subtree. Naming a node aggregates only its materialised
 * subtree, which keeps opening one node cheap.
 *
 * @param tree The tree
 * @param node Index of the node to aggregate, negative for the whole tree
 */
void box_lod_tree_refresh(
    box_lod_tree_t *tree,
    int32_t node);

/** Drop every cluster node, giving its children back to the root list.
 *
 * @param tree The tree
 * @return The number of clusters the tree dropped
 */
int32_t box_lod_tree_uncluster(
    box_lod_tree_t *tree);

/** Group loose roots into cluster nodes the walk can reject as one.
 *
 * The roots are sorted along the axis of widest center spread and cut into groups of the given
 * size, each becoming a cluster node with the roots as children. Entities are not changed: no
 * entity gains a parent.
 *
 * @param tree The tree
 * @param group Roots one cluster holds, 0 for BOX_LOD_CLUSTER_SIZE
 * @return The number of clusters the tree gained
 */
int32_t box_lod_tree_cluster(
    box_lod_tree_t *tree,
    int32_t group);

/** Walk the tree of one view and resolve every node it reaches.
 *
 * Starting at the roots, the walk rejects a node by the view frustum, replaces it by its proxy
 * when the depth pyramid hides it or its projected error is under the threshold, or descends. A
 * hidden node still leaves its proxy, which the renderer culling tests again.
 *
 * @param tree The tree
 * @param desc The view, its threshold and its callbacks
 * @param out Receives what the walk cost, NULL to drop it
 */
void box_lod_tree_walk(
    box_lod_tree_t *tree,
    const box_lod_walk_desc_t *desc,
    box_lod_walk_stats_t *out);

/** Register the box.lod module.
 *
 * @param world The world
 */
void BoxLodImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
