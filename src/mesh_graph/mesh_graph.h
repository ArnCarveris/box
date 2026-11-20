/**
 * @file mesh_graph/mesh_graph.h
 * @brief Private state of box.mesh: node kinds, graphs, the value cache and evaluation plans.
 *
 * The build on the main thread walks a graph into a plan: a snapshot of every node in
 * topological order with its content hash. A worker evaluates the plan, the completion on the
 * main thread caches its values and gives the shapes their meshes.
 */

#ifndef BOX_MESH_GRAPH_INTERNAL_H
#define BOX_MESH_GRAPH_INTERNAL_H

#include <box.h>

/* Values are the same on every platform: no fused multiply-adds the compiler picks */
#ifdef __clang__
#pragma STDC FP_CONTRACT OFF
#endif

/** Bytes the value cache holds when MeshGraphCache sets no budget */
#define BOX_MESH_CACHE_BUDGET (256ll * 1024 * 1024)

/** The golden angle in radians, the turn between the children of a spiral by default */
#define BOX_MG_GOLDEN_ANGLE (2.39996323f)

/** Most nodes a graph reaches */
#define BOX_MESH_GRAPH_NODES (4096)

/** Seconds a headless frame waits at most for the evaluations in flight */
#define BOX_MESH_HEADLESS_WAIT (30.0)

/** A registered node kind */
typedef struct box_mg_kind_t {
    int32_t id;                  /**< Id of the kind, from 1 */
    ecs_entity_t component;      /**< Component of the nodes */
    ecs_size_t size;             /**< Size of the component */
    MeshValueType output;        /**< Type of the value */
    int32_t input_count;         /**< Inputs in inputs */
    box_mesh_node_input_t inputs[BOX_MESH_NODE_INPUTS]; /**< Inputs, names owned */
    int32_t param_count;         /**< Parameters in params */
    box_mesh_node_param_t params[BOX_MESH_NODE_PARAMS]; /**< Parameters, names owned */
    box_mesh_node_capture_t capture; /**< Reads a source node on the main thread */
    box_mesh_node_eval_t eval;   /**< Computes the value on a worker */
    uint32_t flags;              /**< BOX_MESH_NODE_RANDOM */
} box_mg_kind_t;

/** Counter of the evaluations in flight, shared by the registry and its jobs */
typedef struct box_mg_sync_t {
    ecs_os_mutex_t lock;         /**< Protects working */
    int32_t working;             /**< Jobs whose work did not finish */
    int32_t refs;                /**< The registry and every job, changed on the main thread */
} box_mg_sync_t;

/** A value of the cache */
typedef struct box_mg_entry_t {
    uint64_t hash;               /**< Content hash of the node of the value */
    box_mg_value_t *value;       /**< The value, with a reference of the cache */
    int64_t bytes;               /**< Bytes of the value */
    uint64_t used;               /**< Clock of the last use, for the eviction order */
} box_mg_entry_t;

/** The value delivered for an output node, which the MeshGraph shapes naming it draw */
typedef struct box_mg_output_t {
    ecs_entity_t node;           /**< The output node */
    box_mg_value_t *value;       /**< The delivered Mesh, NULL before the first */
    uint64_t hash;               /**< Content hash of the delivered value, 0 without one */
    int32_t users;               /**< Graphs naming the node as their output */
} box_mg_output_t;

/** An entity that owns a MeshGraph */
typedef struct box_mg_record_t {
    ecs_entity_t entity;         /**< The shape entity */
    ecs_entity_t output;         /**< Output node of the record, whose box_mg_output_t it uses */
    ecs_vec_t reached;           /**< ecs_entity_t, the entities the last build reached */
    bool dirty;                  /**< The graph builds at the next build */
    bool waiting;                /**< The last build waited for a node without a node
                                  *   component, the next build fails unless it got one */
    bool held;                   /**< The last build waited for a source that is loading */
    int32_t submitted;           /**< Generation of the last evaluation requested */
    int32_t delivered;           /**< Generation of the delivered mesh */
    int32_t revision;            /**< Meshes delivered */
    int32_t vertices;            /**< Vertices of the delivered mesh */
    int32_t triangles;           /**< Triangles of the delivered mesh */
    float time;                  /**< Seconds the evaluation of the delivered mesh took */
} box_mg_record_t;

/** The state of the mesh graphs of a world */
typedef struct box_mesh_graphs_t {
    ecs_vec_t kinds;             /**< box_mg_kind_t*, by id - 1 */
    ecs_map_t kind_index;        /**< Component to kind id */
    ecs_map_t records;           /**< Entity to box_mg_record_t* */
    ecs_map_t outputs;           /**< Output node to box_mg_output_t* */
    ecs_map_t reached;           /**< Entity to the number of records that reached it */
    ecs_map_t cache;             /**< Hash to box_mg_entry_t* */
    int64_t cache_bytes;         /**< Bytes of the values of the cache */
    uint64_t clock;              /**< Clock of the uses of the cache */
    box_mg_sync_t *sync;         /**< Counter of the evaluations in flight */
    box_mesh_graph_stats_t stats; /**< Counters */
} box_mesh_graphs_t;

/** A node of a plan */
typedef struct box_mg_node_t {
    ecs_entity_t entity;         /**< The node entity, of the first walk that reached it */
    char *name;                  /**< Path of the node entity, for errors */
    int32_t kind;                /**< Id of the kind */
    box_mesh_node_eval_t eval;   /**< Computes the value, NULL for a source */
    void *params;                /**< Snapshot of the node component, owned */
    uint64_t hash;               /**< Content hash */
    MeshValueType type;          /**< Type of the value */
    int32_t link;                /**< First input in the links of the plan */
    int32_t counts[BOX_MESH_NODE_INPUTS]; /**< Inputs of each port in the links */
    box_mg_value_t *value;       /**< The value, with a reference of the plan, NULL until it
                                  *   is evaluated */
    bool needed;                 /**< A target needs the value */
    bool computed;               /**< The plan computed the value, so the cache takes it */
    char *error;                 /**< Why the evaluation failed, NULL while it did not */
} box_mg_node_t;

/** A record whose output a plan evaluates */
typedef struct box_mg_target_t {
    ecs_entity_t entity;         /**< The shape entity */
    ecs_entity_t output;         /**< The output node it requested */
    int32_t node;                /**< Index of its output node in the plan */
    int32_t generation;          /**< Generation of the request */
} box_mg_target_t;

/** Nodes to evaluate, the context of a job */
typedef struct box_mg_plan_t {
    ecs_vec_t nodes;             /**< box_mg_node_t, inputs before the nodes that read them */
    ecs_vec_t links;             /**< int32_t, the input nodes of the ports of every node */
    ecs_map_t by_hash;           /**< Hash to index + 1 of a node */
    ecs_vec_t targets;           /**< box_mg_target_t */
    box_mg_sync_t *sync;         /**< Counter of the evaluations in flight, with a reference */
    double time;                 /**< Seconds the evaluation took */
} box_mg_plan_t;

/** Get the state of the mesh graphs of a world.
 *
 * @param world The world
 * @return The state, NULL without box.mesh
 */
box_mesh_graphs_t* box_mesh_graphs_get(
    const ecs_world_t *world);

/** Find the kind of a node component.
 *
 * @param g The state
 * @param component The component
 * @return The kind, NULL when the component is not a node kind
 */
box_mg_kind_t* box_mesh_kind_of(
    const box_mesh_graphs_t *g,
    ecs_entity_t component);

/** Get a kind by id.
 *
 * @param g The state
 * @param id The id
 * @return The kind, NULL for an unknown id
 */
box_mg_kind_t* box_mesh_kind(
    const box_mesh_graphs_t *g,
    int32_t id);

/** Mark the records that reached an entity, so they build again; 0 marks every record.
 *
 * @param g The state
 * @param e The entity
 */
void box_mesh_graphs_touch(
    box_mesh_graphs_t *g,
    ecs_entity_t e);

/** Initialize a plan.
 *
 * @param plan The plan
 */
void box_mg_plan_init(
    box_mg_plan_t *plan);

/** Free what a plan owns and drop its references.
 *
 * @param plan The plan
 */
void box_mg_plan_fini(
    box_mg_plan_t *plan);

/** Outcome of the walk of a graph into a plan */
typedef enum box_mg_walk_t {
    BOX_MG_WALK_OK = 0,      /**< The output is a node of the plan */
    BOX_MG_WALK_WAIT = 1,    /**< A node has no node component or a source misses its data */
    BOX_MG_WALK_HOLD = 2,    /**< A source is loading */
    BOX_MG_WALK_ERROR = 3    /**< The graph is invalid, the error says why */
} box_mg_walk_t;

/** Walk a graph from its output into a plan, dedupe its nodes by hash and look them up in the
 * cache.
 *
 * @param world The world
 * @param g The state
 * @param plan The plan
 * @param graph Name of the graph in errors
 * @param output The output node
 * @param wait Wait for a node without a node component instead of failing
 * @param reached Receives the entities the walk reached, NULL for none
 * @param node Receives the index of the output in the plan
 * @param error Receives why it failed, freed with ecs_os_free
 * @return The outcome
 */
box_mg_walk_t box_mg_plan_walk(
    const ecs_world_t *world,
    box_mesh_graphs_t *g,
    box_mg_plan_t *plan,
    const char *graph,
    ecs_entity_t output,
    bool wait,
    ecs_vec_t *reached,
    int32_t *node,
    char **error);

/** Mark the nodes the targets need: a target, and the inputs of a needed node without a value.
 *
 * @param plan The plan
 * @param node A target node
 */
void box_mg_plan_need(
    box_mg_plan_t *plan,
    int32_t node);

/** Evaluate the needed nodes of a plan without a value, on any thread.
 *
 * @param plan The plan
 */
void box_mg_plan_eval(
    box_mg_plan_t *plan);

/** Take the values a plan computed into the cache, as the values of the cache when it had
 * them already, then evict values over the budget.
 *
 * @param world The world
 * @param g The state
 * @param plan The plan
 */
void box_mg_plan_cache(
    const ecs_world_t *world,
    box_mesh_graphs_t *g,
    box_mg_plan_t *plan);

/** Evict unused values of the cache while it holds more than its budget.
 *
 * @param world The world
 * @param g The state
 */
void box_mg_cache_trim(
    const ecs_world_t *world,
    box_mesh_graphs_t *g);

/** Free every value of the cache.
 *
 * @param g The state
 */
void box_mg_cache_fini(
    box_mesh_graphs_t *g);

/** Submit the plans of the records that changed: build them, check them and evaluate them.
 *
 * @param world The world or a stage
 * @param g The state
 */
void box_mesh_graphs_build(
    ecs_world_t *world,
    box_mesh_graphs_t *g);

/** Write the status of a record.
 *
 * @param world The world or a stage
 * @param r The record
 * @param state The state
 * @param error Why it failed, NULL unless state is MeshGraphFailed
 */
void box_mesh_graph_status(
    ecs_world_t *world,
    const box_mg_record_t *r,
    MeshGraphState state,
    const char *error);

/** Find the output record of a node, adding it when there is none.
 *
 * @param g The state
 * @param node The output node
 * @return The output record
 */
box_mg_output_t* box_mesh_graph_output(
    box_mesh_graphs_t *g,
    ecs_entity_t node);

/** Forget the entities a record reached.
 *
 * @param g The state
 * @param r The record
 */
void box_mesh_graph_unreach(
    box_mesh_graphs_t *g,
    box_mg_record_t *r);

/** Give the shapes that name an output its new value: the delivered value of the output, and
 * a MeshGraph set on each shape so the shape resolves it.
 *
 * @param world The world
 * @param g The state
 * @param output The output node
 * @param value The value, a reference is taken
 * @param hash The content hash of the value
 */
void box_mesh_graph_deliver(
    ecs_world_t *world,
    box_mesh_graphs_t *g,
    ecs_entity_t output,
    box_mg_value_t *value,
    uint64_t hash);

/** Add bytes to a 64 bit hash.
 *
 * @param hash The hash so far
 * @param data The bytes
 * @param size Number of bytes
 * @return The hash with the bytes
 */
uint64_t box_mg_hash(
    uint64_t hash,
    const void *data,
    int64_t size);

/** Add the content of a value to a 64 bit hash.
 *
 * @param hash The hash so far
 * @param value The value
 * @return The hash with the value
 */
uint64_t box_mg_value_hash(
    uint64_t hash,
    const box_mg_value_t *value);

/** Get the bytes of the arrays of a value.
 *
 * @param value The value
 * @return The bytes
 */
int64_t box_mg_value_bytes(
    const box_mg_value_t *value);

/** Copy the content of a value into an empty value of its type.
 *
 * @param dst The empty value
 * @param src The value
 */
void box_mg_value_copy(
    box_mg_value_t *dst,
    const box_mg_value_t *src);

/** Name of a value type for errors.
 *
 * @param type The type
 * @return The name
 */
const char* box_mg_type_name(
    MeshValueType type);

/** State of the seeded random numbers of a node */
typedef struct box_mg_rand_t {
    uint64_t state;              /**< SplitMix64 state */
} box_mg_rand_t;

/** Seed a random sequence.
 *
 * @param rand The sequence
 * @param seed The seed of the node
 * @param stream Number that separates the sequences of one seed
 */
void box_mg_rand_seed(
    box_mg_rand_t *rand,
    int32_t seed,
    uint32_t stream);

/** Draw a float in 0..1, 1 excluded.
 *
 * @param rand The sequence
 * @return The float
 */
float box_mg_rand_float(
    box_mg_rand_t *rand);

/** Sine and cosine of an angle from IEEE operations only, the same on every platform.
 *
 * @param angle The angle in radians
 * @param sine Receives the sine
 * @param cosine Receives the cosine
 */
void box_mg_sincos(
    float angle,
    float *sine,
    float *cosine);

/** The rotation of euler angles in the order of Rotation (glm_euler_yxz), from box_mg_sincos.
 *
 * @param angles The angles in radians
 * @param out Receives the rotation
 */
void box_mg_euler(
    const vec3 angles,
    mat3 out);

/** Fractal gradient noise of a point, about -1..1.
 *
 * @param p The point
 * @param octaves Octaves, 1 to 8
 * @param seed Seed of the noise
 * @return The noise
 */
float box_mg_noise(
    const vec3 p,
    int32_t octaves,
    int32_t seed);

/** Compute the per point tangents and rotation minimizing normals of the curves of a value
 * from their positions.
 *
 * @param curves The curves
 */
void box_mg_curves_frames(
    box_mg_curves_t *curves);

/** Compute the tangents and rotation minimizing normals of a range of curves.
 *
 * @param curves The curves
 * @param from First curve
 * @param to Curve after the last
 */
void box_mg_curves_frames_range(
    box_mg_curves_t *curves,
    int32_t from,
    int32_t to);

/** A point sampled on a curve */
typedef struct box_mg_sample_t {
    vec3 position;               /**< Position */
    vec3 tangent;                /**< Unit tangent */
    vec3 normal;                 /**< Unit normal, perpendicular to the tangent */
    float radius;                /**< Radius */
    int32_t point;               /**< First point of the segment of the sample */
    float fraction;              /**< Fraction along the segment */
} box_mg_sample_t;

/** Sample a curve at a distance from its start.
 *
 * @param c The curves
 * @param first First point of the curve
 * @param count Points of the curve
 * @param lengths Length up to each point
 * @param distance The distance
 * @param out Receives the sample
 */
void box_mg_curve_sample(
    const box_mg_curves_t *c,
    int32_t first,
    int32_t count,
    const float *lengths,
    float distance,
    box_mg_sample_t *out);

/** Set the tip attribute of curves: 1 on the last point of every curve without children, 0
 * on the other points; added when there is room.
 *
 * @param curves The curves
 */
void box_mg_curves_tips(
    box_mg_curves_t *curves);

/** Compute the length of a curve up to each of its points.
 *
 * @param curves The curves
 * @param curve The curve
 * @param lengths Receives the length up to each point, the count of the curve
 * @return The length of the curve
 */
float box_mg_curve_lengths(
    const box_mg_curves_t *curves,
    int32_t curve,
    float *lengths);

/** Give every triangle of a mesh its own vertices with the face normal, attributes copied.
 *
 * @param mesh The mesh
 */
void box_mg_mesh_facet(
    box_mg_mesh_t *mesh);

/** Register the source node kinds: Shape, Mesh3 and Path.
 *
 * @param world The world
 */
void box_mesh_nodes_source_register(
    ecs_world_t *world);

/** Register the node kinds that change meshes: Transform, Merge, Displace and Normals.
 *
 * @param world The world
 */
void box_mesh_nodes_mesh_register(
    ecs_world_t *world);

/** Register the node kinds of curves and points: Tube, PointsOnCurves, PointsOnMesh and
 * InstanceOnPoints.
 *
 * @param world The world
 */
void box_mesh_nodes_curves_register(
    ecs_world_t *world);

/** Register the node kinds of branching curves: Stem, Branches and Prune.
 *
 * @param world The world
 */
void box_mesh_nodes_branch_register(
    ecs_world_t *world);

/** Register the node kinds of foliage: Leaves, Needles, Cards, CrownNormals and
 * CrownOcclusion.
 *
 * @param world The world
 */
void box_mesh_nodes_foliage_register(
    ecs_world_t *world);

/** Register the MeshGraph shape and its observers.
 *
 * @param world The world
 */
void box_mesh_graph_shape_init(
    ecs_world_t *world);

#endif
