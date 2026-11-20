/**
 * @file box/mesh_graph.h
 * @brief Mesh graphs: a shape whose mesh a graph of node entities computes on the job system.
 *
 * Meshes, curves and points flow between the nodes. The values of the nodes are cached by the
 * content hash of each node, so graphs that share a subgraph share its work.
 */

#ifndef BOX_MESH_GRAPH_H
#define BOX_MESH_GRAPH_H

#include "types.h"

#ifdef BOX_MESH_GRAPH_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Most input ports of a node kind */
#define BOX_MESH_NODE_INPUTS (4)

/** Most parameters of a node kind */
#define BOX_MESH_NODE_PARAMS (32)

/** Most float attributes of a value */
#define BOX_MESH_ATTRS (16)

/** Bytes of the name of an attribute, the terminating zero included */
#define BOX_MESH_ATTR_NAME (24)

/** Flag of a node kind whose value is random: it has an int32_t parameter named seed, and its
 * value is the same for the same seed on every run and platform */
#define BOX_MESH_NODE_RANDOM (1u << 0)

/** Prefix of the Mesh attributes that are the per vertex data of the shape: the attributes
 * data.0 to data.7 are channels 0 to 7 (Mesh3.data0 then data1, box_mesh_vertex_data); a mesh
 * without them has no data */
#define BOX_MESH_DATA_ATTR "data."

/** Data channels 0 to 2 of the bark of branching curves, as Tube writes them for
 * WindSwayHierarchy: the pivot of the branch, its root on the axis of its parent */
#define BOX_MESH_DATA_PIVOT (0)

/** Data channel of the bark of branching curves: the level of the branch, 0 for a stem */
#define BOX_MESH_DATA_LEVEL (3)

/** Data channel of the bark of branching curves: the phase of the branch in 0..1, from its
 * seed, so branches sway out of step */
#define BOX_MESH_DATA_PHASE (4)

/** Data channel of the bark of branching curves: the stiffness of the branch, the radius at
 * its base over the radius at the base of the stem it grows from, 1 for a stem */
#define BOX_MESH_DATA_STIFFNESS (5)

/** Data channel of the bark of branching curves: the distance in meters from the root of the
 * stem along the branches, which a VertexReveal grows the plant along */
#define BOX_MESH_DATA_GROWTH (6)

/** Data channel of the bark of branching curves: the fraction of the length of the branch */
#define BOX_MESH_DATA_ALONG (7)

/** A capture got its value */
#define BOX_MESH_CAPTURE_OK (0)

/** A capture misses the component it reads: the graph waits a frame for it, then fails */
#define BOX_MESH_CAPTURE_WAIT (1)

/** A capture reads a source that is still loading, such as another MeshGraph: the graph waits
 * until the source is ready */
#define BOX_MESH_CAPTURE_HOLD (2)

/** A capture failed, its error says why */
#define BOX_MESH_CAPTURE_ERROR (-1)

/** Type of a value that flows between the nodes of a MeshGraph */
ECS_ENUM(MeshValueType, {
    MeshValueMesh = 0,   /**< Indexed triangles with positions, normals and uvs */
    MeshValueCurves = 1, /**< Polylines with a radius and a frame per point */
    MeshValuePoints = 2, /**< Points with a rotation and a scale */
    MeshValueAny = 3     /**< An input that takes every type, or the output of a kind whose
                          *   type is the type of its inputs */
});

/** Progress of the evaluation of a MeshGraph */
ECS_ENUM(MeshGraphState, {
    MeshGraphLoading = 0, /**< An evaluation is in flight */
    MeshGraphReady = 1,   /**< The last evaluation succeeded and the shape has its mesh */
    MeshGraphFailed = 2   /**< The last evaluation failed, error says why */
});

/** Shape whose mesh is the value of a node of a graph of node entities.
 *
 * Each node entity has one node component such as MeshTube, whose entity members are its
 * inputs. A change of the graph evaluates it again on the job system; the shape has no mesh
 * before the first evaluation completed and keeps its mesh until the next one completed.
 *
 * Shapes that name different outputs of one graph, such as bark and leaves with two materials,
 * evaluate the nodes they share once. Prefab instances inherit it and share the prefab's mesh.
 *
 * @code
 * using box.mesh
 * prefab ball { IcoSphere: {segments: 2, smooth: true, radius: 0.2} }
 * rail {
 *   route  { Path3D: {start: {0, 0, 0}, segments: [{kind: Line, end: {4, 0, 0}}]} }
 *   curve  { nodes.Path: {path: route, radius: 0.05} }
 *   tube   { nodes.Tube: {curves: curve, sides: 12} }
 *   points { nodes.PointsOnCurves: {curves: curve, count: 5} }
 *   ball_n { nodes.Shape: {entity: ball} }
 *   balls  { nodes.InstanceOnPoints: {points: points, instance: ball_n} }
 *   all    { nodes.Merge: {inputs: [tube, balls]} }
 *   Graph: {output: all}
 * }
 * @endcode
 */
ECS_STRUCT(MeshGraph, {
    ecs_entity_t output; /**< Node whose value, a Mesh, is the mesh of the shape */
});

/** Outcome of the evaluations of a MeshGraph, written by the engine on the entity that owns
 * the MeshGraph and read only for applications.
 *
 * A cycle, a deleted node, an entity without a node component by the next frame, an input of
 * the wrong type or an output that is not a Mesh fails the graph; the shape keeps its mesh.
 */
ECS_STRUCT(MeshGraphStatus, {
    MeshGraphState state; /**< Progress of the last requested evaluation */
    int32_t revision;     /**< Meshes the shape got, the mesh changed when it increments */
    char *error;          /**< Why the last evaluation failed, NULL unless state is
                           *   MeshGraphFailed */
    int32_t vertices;     /**< Vertices of the current mesh */
    int32_t triangles;    /**< Triangles of the current mesh */
    float time;           /**< Seconds the evaluation of the current mesh took on its worker,
                           *   0 when every value came from the cache */
});

/** Settings of the cache of the node values of the mesh graphs, a singleton.
 *
 * Values are cached by the content hash of their node: its kind, its parameters and the hashes
 * of its inputs, or the data a source node read. An edit evaluates the nodes after the edited
 * one, and graphs with an identical subgraph share it, such as the variants of a tree.
 *
 * Values no graph uses stay while the cache fits the budget, the least recently used evicted
 * first; values in use are never evicted, even over the budget.
 */
ECS_STRUCT(MeshGraphCache, {
    int64_t budget; /**< Bytes the cache holds at most, 0 for 256 MB */
});

/** An input port of a node kind, as MeshNodeKind lists it */
ECS_STRUCT(MeshNodePort, {
    const char *name;   /**< Member of the ecs_entity_t of the input node, or of the
                         *   ecs_vec(ecs_entity_t) of the input nodes of a list */
    MeshValueType type; /**< Type the input node must output, MeshValueAny for every type */
    bool list;          /**< The member is a list of input nodes */
    bool optional;      /**< The input may have no node */
});

/** A parameter of a node kind, as MeshNodeKind lists it */
ECS_STRUCT(MeshNodeParam, {
    const char *name; /**< Member of the node component */
    int32_t offset;   /**< Byte offset of the member */
    int32_t size;     /**< Bytes of the member */
});

/** Description of a node kind, set by the engine on the component entity of each registered
 * kind so tools can list kinds and check connections from the world */
ECS_STRUCT(MeshNodeKind, {
    int32_t id;             /**< Id of the kind, from 1 */
    MeshValueType output;   /**< Type of the value of the node, MeshValueAny when it is the
                             *   type of its inputs */
    int32_t input_count;    /**< Inputs in inputs */
    MeshNodePort inputs[4]; /**< The BOX_MESH_NODE_INPUTS inputs, in order */
    int32_t param_count;    /**< Parameters in params */
    MeshNodeParam params[32]; /**< The BOX_MESH_NODE_PARAMS parameters, in order */
    bool source;            /**< The node reads entities of the world on the main thread */
    bool random;            /**< The node has a seed, see BOX_MESH_NODE_RANDOM */
});

/** Node of a MeshGraph whose Mesh is the mesh of the shape of an entity, through the mesh
 * cache: every registered shape is a source, such as Box, IcoSphere, Sweep or Lathe. The
 * instance scale of the shape is applied; its transform is not. */
ECS_STRUCT(MeshShape, {
    ecs_entity_t entity; /**< Entity with the shape, 0 for the node entity; a prefab, which
                          *   does not draw */
});

/** Node of a MeshGraph whose Mesh is the Mesh3 of an entity. Normals are derived per face
 * when it has none, and indices in order when it has none */
ECS_STRUCT(MeshMesh3, {
    ecs_entity_t entity; /**< Entity with the Mesh3, 0 for the node entity */
});

/** Node of a MeshGraph whose Curves is one curve sampled from the Path3D or Path2D of an
 * entity, with a rotation minimizing frame and a radius per point.
 *
 * A closed path repeats its first point at the end. A Path2D lies in the XY plane.
 */
ECS_STRUCT(MeshPath, {
    ecs_entity_t path;  /**< Entity with the Path3D or Path2D, 0 for the node entity */
    int32_t steps;      /**< Points per curved segment, 0 for 16, at most 64 */
    float radius;       /**< Radius at the start, 0 for 0.1 */
    float radius_end;   /**< Radius at the end, 0 for radius; linear along the length */
});

/** Node of a MeshGraph that moves, turns and scales a Mesh, Curves or Points. Normals and
 * frames turn with it, radii scale with the mean scale */
ECS_STRUCT(MeshTransform, {
    ecs_entity_t input; /**< Input node of any type */
    vec3 position;      /**< Offset, applied last */
    vec3 rotation;      /**< Rotation in radians, as Rotation: yaw, then pitch, then roll */
    vec3 scale;         /**< Scale, applied first, 1 when the component is added; a 0 is 1 */
});

/** Node of a MeshGraph whose value joins its inputs, which have one type. Attributes join by
 * name, an element without one gets 0; the parents of curves keep pointing at their curves */
ECS_STRUCT(MeshMerge, {
    ecs_vec(ecs_entity_t) inputs; /**< Input nodes, at least one */
});

/** Node of a MeshGraph that sweeps a profile along every curve of a Curves into a Mesh.
 *
 * Each point of a curve gets a ring of the profile in its frame, scaled by its radius times
 * radius. u runs around the profile from 0 to 1, v along the curve: its length times v_scale,
 * continuous from point to point.
 *
 * A curve with a parent gets a collar: it flares and its normals blend into the surface of the
 * parent where it leaves it, and its rings inside the parent stay inside. Curves with a level
 * attribute (Stem, Branches) write the bark data channels, BOX_MESH_DATA_PIVOT and after.
 */
ECS_STRUCT(MeshTube, {
    ecs_entity_t curves;  /**< Curves input node */
    ecs_entity_t profile; /**< Curves node whose first curve, in its XY plane, is the profile,
                           *   0 for a circle of radius 1; x is the normal of the frame */
    int32_t sides;        /**< Sides of the circle without a profile, 0 for 8, at least 3 */
    float radius;         /**< Factor of the radius of the curves, 0 for 1 */
    float v_scale;        /**< v per unit of length along the curve, 0 for 1 */
    bool caps;            /**< Close both ends of every curve with a fan */
    float collar;         /**< Length above the surface of the parent over which the collar of
                           *   a child blends, in base radii of the child; 0 for 2, negative
                           *   for no collar */
    float flare;          /**< Radius the collar adds where it meets the parent, a fraction of
                           *   the radius of the child; 0 for 0.5, negative for none */
    bool square_uv;       /**< u repeats a whole number of times around each curve, about its
                           *   circumference at the base times v_scale, so bark texels are
                           *   square */
    int32_t flutes;       /**< Buttress flutes around the base of the curves without a parent,
                           *   0 for none */
    float flute_depth;    /**< Radius a flute adds at its crest, a fraction of the radius */
    float flute_height;   /**< Length from the base over which the flutes fade out, 0 for three
                           *   base radii */
});

/** How Branches spreads the children of a parent around it */
ECS_ENUM(MeshBranchDistribution, {
    MeshBranchAlternate = 0, /**< One child per node, each half a turn from the last */
    MeshBranchOpposite = 1,  /**< Two opposite children per node, each pair a quarter turn
                              *   from the last */
    MeshBranchWhorled = 2,   /**< whorl children per node evenly around, each whorl half a
                              *   step from the last */
    MeshBranchSpiral = 3     /**< One child per node, each the phyllotaxis angle from the
                              *   last */
});

/** Shape of the envelope of Prune */
ECS_ENUM(MeshEnvelopeShape, {
    MeshEnvelopeNone = 0,      /**< No envelope, only the radius prunes */
    MeshEnvelopeEllipsoid = 1, /**< Ellipsoid around center with the half extents of size */
    MeshEnvelopeCone = 2,      /**< Cone from its base at center, radius size x and z, up to
                                *   its apex size y above */
    MeshEnvelopeCylinder = 3   /**< Cylinder from its base at center, radius size x and z,
                                *   size y high */
});

/** Node of a MeshGraph whose Curves are stems: a curve that grows from a point or from each
 * point of a Points, bending by noise and gravitropism as it goes.
 *
 * A stem has the attributes level 0, parent_t 0, tip (1 on its last point), root_distance
 * (from its root in meters) and phase (0..1 from the seed), which Branches, Tube, Prune and
 * PointsOnCurves read. Defaults are set when the component is added.
 */
ECS_STRUCT(MeshStem, {
    ecs_entity_t points;    /**< Points node, a stem grows from each along its +Y turned to
                             *   direction and scaled by its y scale; 0 for one stem */
    vec3 position;          /**< Root of the stem without points */
    vec3 direction;         /**< Start direction, 0 for +Y */
    float length;           /**< Length, 5 when added, 0 for 1 */
    float length_variation; /**< Random change of the length, a fraction of it */
    int32_t segments;       /**< Segments of a stem, 12 when added, 0 for 8, at most 256 */
    float radius;           /**< Radius at the root, 0.2 when added, 0 for 0.1 */
    float taper;            /**< Fraction of the radius the tip loses, 0.8 when added */
    float noise;            /**< Bend of the noise, radians per meter */
    float noise_frequency;  /**< Noise cells per meter, 0 for 1 */
    float gravitropism;     /**< Bend toward +Y in radians per meter, negative bends down */
    float twist;            /**< Turn of the frame around the stem, radians per meter */
    int32_t seed;           /**< Seed of the variation, noise and phase */
});

/** Node of a MeshGraph that sprouts child curves along the curves of the highest level of a
 * Curves, which it outputs with the children, so a chain of Branches grows level by level.
 *
 * A child starts on the axis of its parent, so a Tube sinks it into the parent; its radius
 * is the parent radius there split by the pipe model among the children of its node and the
 * parent. Children get level, parent_t, tip, root_distance and phase like a Stem.
 */
ECS_STRUCT(MeshBranches, {
    ecs_entity_t curves;     /**< Curves input node: parents and their ancestors */
    int32_t count;           /**< Children per parent in whole nodes, 4 when added */
    float density;           /**< Children per meter of the range instead of count, 0 for
                              *   count */
    float start;             /**< Start of the range, a fraction of the parent length */
    float end;               /**< End of the range, 1 when added, 0 for 1 */
    MeshBranchDistribution distribution; /**< How the children turn around the parent */
    int32_t whorl;           /**< Children per node of a Whorled distribution, 0 for 3 */
    float phyllotaxis;       /**< Turn between children of a Spiral in radians, 0 for the
                              *   golden angle */
    float down_angle;        /**< Angle between a child and its parent at the start of the
                              *   range in radians, 0.8 when added */
    float down_angle_change; /**< Change of the down angle from the start to the end of the
                              *   range */
    float roll;              /**< Turn added to every child around its parent, radians */
    float length;            /**< Length of a child as a fraction of the length of its
                              *   parent, 0.5 when added, 0 for 0.5 */
    float length_curve[5];   /**< Factor of the length at 0, 1/4, 1/2, 3/4 and 1 of the range,
                              *   linear between them; all 0 for 1 */
    float radius_exponent;   /**< Exponent of the pipe model, 0 for 2: the area is kept */
    float taper;             /**< Fraction of its radius the tip of a child loses, 0.8 when
                              *   added */
    int32_t segments;        /**< Segments of a child, 0 for 6, at most 256 */
    float gravity;           /**< Bend toward -Y, radians per meter */
    float phototropism;      /**< Bend toward light, radians per meter */
    vec3 light;              /**< Direction toward the light, 0 for +Y */
    float noise;             /**< Bend of the noise, radians per meter */
    float angle_variation;   /**< Random change of the down angle, radians */
    float roll_variation;    /**< Random change of the roll, radians */
    float length_variation;  /**< Random change of the length, a fraction of it */
    float radius_variation;  /**< Random change of the radius, a fraction of it */
    int32_t seed;            /**< Seed of the variations, the noise and the phases */
});

/** Node of a MeshGraph that removes or shortens the curves of a Curves outside an envelope
 * and the curves thinner than a radius, each with its children.
 *
 * The envelope is the profile when it has one: its x is the radius around the vertical axis
 * through center at the height y above center, as a Lathe turns a Path2D.
 */
ECS_STRUCT(MeshPrune, {
    ecs_entity_t curves;     /**< Curves input node */
    ecs_entity_t profile;    /**< Curves node whose first curve is the envelope, 0 for shape */
    MeshEnvelopeShape shape; /**< Envelope without a profile */
    vec3 center;             /**< Center of the ellipsoid, center of the base of the cone or
                              *   cylinder, origin of the profile */
    vec3 size;               /**< Half extents of the ellipsoid, radii and height of the cone
                              *   or cylinder; a 0 is 1 */
    float min_radius;        /**< Curves whose base radius is below it are removed */
    int32_t level;           /**< Curves of a lower level are kept, 1 when added */
    bool shorten;            /**< Cut a curve where it leaves the envelope instead of removing
                              *   it; it tapers to the radius of its old tip, its children
                              *   beyond the cut are removed and the others thinned to fit */
});

/** Node of a MeshGraph that places Points along every curve of a Curves.
 *
 * A point turns +Y to the tangent of the curve and +Z to its normal, so a shape along Y such
 * as a Cylinder follows the curve.
 *
 * Each point has the attributes t (the fraction of the length of its curve), distance (from the
 * start of its curve), curve, radius and index, then the attributes of the curves.
 */
ECS_STRUCT(MeshPointsOnCurves, {
    ecs_entity_t curves; /**< Curves input node */
    int32_t count;       /**< Points per curve without spacing, 0 for 8; one point sits at
                          *   the start of the range, more span it */
    float spacing;       /**< Length between points, 0 to place count points instead */
    float start;         /**< Start of the range as a fraction of the length of a curve */
    float end;           /**< End of the range as a fraction of the length, 0 for 1 */
    float jitter;        /**< Random shift of each point along the curve, as a fraction of the
                          *   distance between points */
    int32_t seed;        /**< Seed of the jitter */
});

/** Node of a MeshGraph that scatters Points over the triangles of a Mesh, area weighted.
 *
 * A point turns +Y to the interpolated normal, unless vertical keeps it upright, and has the
 * attributes u and v of the interpolated uv. With a min_spacing it stops at fewer points when
 * the surface has no room for more.
 */
ECS_STRUCT(MeshPointsOnMesh, {
    ecs_entity_t mesh;  /**< Mesh input node */
    int32_t count;      /**< Points, 0 for 32 */
    float min_spacing;  /**< Least distance between two points, 0 for none */
    int32_t seed;       /**< Seed of the placement */
    bool vertical;      /**< Keep the points upright instead of turning them to the normal */
});

/** Node of a MeshGraph whose Mesh holds a copy of a Mesh at every point of a Points, turned
 * and scaled by the point */
ECS_STRUCT(MeshInstanceOnPoints, {
    ecs_entity_t points;   /**< Points input node */
    ecs_entity_t instance; /**< Mesh input node that is copied */
    bool ignore_rotation;  /**< Copies keep the orientation of the mesh */
    bool ignore_scale;     /**< Copies keep the size of the mesh */
});

/** Node of a MeshGraph that moves the vertices of a Mesh along their normals by fractal
 * gradient noise of their position in object space, from -amount to amount. Normals are
 * kept; a Normals node after it shades the new shape */
ECS_STRUCT(MeshDisplace, {
    ecs_entity_t mesh; /**< Mesh input node */
    float amount;      /**< Largest distance a vertex moves, 0.1 when the component is added */
    float frequency;   /**< Noise cells per unit of length, 0 for 1 */
    int32_t octaves;   /**< Octaves of the noise, 0 for 1, at most 8 */
    int32_t seed;      /**< Seed of the noise */
});

/** Node of a MeshGraph that computes the normals of a Mesh again.
 *
 * Smooth normals average the faces around a position whose normals are less than angle
 * apart, so edges sharper than angle stay hard; flat normals give every face its own.
 */
ECS_STRUCT(MeshNormals, {
    ecs_entity_t mesh; /**< Mesh input node */
    float angle;       /**< Largest angle in radians between faces that share a normal, 0 for
                        *   pi: every edge smooth */
    bool flat;         /**< Give every triangle its face normal */
});

/** Node of a MeshGraph that cuts a Mesh with a plane: the vertices in front of the plane, on the
 * side its normal points to, move onto it, so the mesh gets a flat face there, as a boulder
 * split along a joint or a slab with a flat bed. The topology is kept.
 *
 * With a relief the face is not quite flat: each moved vertex sinks below the plane by fractal
 * noise of its position, up to relief. Normals are kept; a Normals node with an angle after it
 * gives the face a hard edge.
 */
ECS_STRUCT(MeshCut, {
    ecs_entity_t mesh; /**< Mesh input node */
    vec3 position;     /**< A point of the plane */
    vec3 normal;       /**< Normal of the plane toward the part cut away, zero for +Y */
    float relief;      /**< Largest depth of the noise on the face, 0 for a flat face */
    float frequency;   /**< Noise cells per unit of length of the relief, 0 for 1 */
    int32_t seed;      /**< Seed of the relief */
});

/** Node of a MeshGraph that layers a Mesh into strata along an axis, as bedded sandstone or
 * shale: the mesh is cut into layers of a thickness that each pull in or stand out by a random
 * fraction across the axis, and the coordinate along the axis steps into terraces, flat at the
 * bed of every layer and steep between them.
 */
ECS_STRUCT(MeshStrata, {
    ecs_entity_t mesh; /**< Mesh input node */
    vec3 axis;         /**< Direction across the layers through the origin, zero for +Y */
    float thickness;   /**< Thickness of a layer, 0 for 0.2 */
    float sharpness;   /**< 0 keeps the coordinate along the axis, 1 steps it into flat
                        *   terraces; clamped to 0..1 */
    float inset;       /**< Largest fraction a layer pulls in toward the axis, chosen per layer */
    int32_t seed;      /**< Seed of the insets and of the phase of the layers */
});

/** Node of a MeshGraph that turns and scales the points of a Points at random, so the copies of
 * an InstanceOnPoints differ: each point spins about its Y axis, tilts its Y axis and scales
 * uniformly between scale_min and scale_max. Attributes are kept.
 */
ECS_STRUCT(MeshJitter, {
    ecs_entity_t points; /**< Points input node */
    float spin;          /**< Largest turn about the Y axis of a point in radians, both ways */
    float tilt;          /**< Largest tilt of the Y axis of a point in radians */
    float scale_min;     /**< Least factor of the scale, 0 for 1 */
    float scale_max;     /**< Largest factor of the scale, below scale_min for scale_min */
    int32_t seed;        /**< Seed of the turns and scales */
});

/** Float attribute of the elements of a value, such as the fraction along a curve */
typedef struct box_mg_attr_t {
    char name[BOX_MESH_ATTR_NAME]; /**< Name, unique in the value */
    float *values;                 /**< One value per element */
} box_mg_attr_t;

/** Mesh value of a node: indexed triangles, counter-clockwise front faces */
typedef struct box_mg_mesh_t {
    int32_t vertex_count;    /**< Vertices */
    int32_t index_count;     /**< Indices, three per triangle */
    vec3 *positions;         /**< Position per vertex */
    vec3 *normals;           /**< Unit normal per vertex */
    vec2 *uvs;               /**< Texture coordinate per vertex */
    uint32_t *indices;       /**< Vertex indices */
    int32_t attr_count;      /**< Attributes in attrs */
    box_mg_attr_t attrs[BOX_MESH_ATTRS]; /**< Attributes per vertex */
} box_mg_mesh_t;

/** Curves value of a node: polylines whose points have a radius and a frame */
typedef struct box_mg_curves_t {
    int32_t curve_count;     /**< Curves */
    int32_t point_count;     /**< Points of every curve */
    int32_t *offsets;        /**< First point of each curve, curve_count + 1 entries */
    int32_t *parents;        /**< Index of the parent curve of each curve, -1 for none */
    vec3 *positions;         /**< Position per point */
    float *radii;            /**< Radius per point */
    vec3 *tangents;          /**< Unit tangent per point */
    vec3 *normals;           /**< Unit normal per point, perpendicular to the tangent */
    int32_t attr_count;      /**< Attributes in attrs */
    box_mg_attr_t attrs[BOX_MESH_ATTRS]; /**< Attributes per point */
} box_mg_curves_t;

/** Points value of a node */
typedef struct box_mg_points_t {
    int32_t count;           /**< Points */
    vec3 *positions;         /**< Position per point */
    versor *rotations;       /**< Rotation per point */
    vec3 *scales;            /**< Scale per point */
    int32_t attr_count;      /**< Attributes in attrs */
    box_mg_attr_t attrs[BOX_MESH_ATTRS]; /**< Attributes per point */
} box_mg_points_t;

/** Value of a node, immutable once computed and shared by reference */
typedef struct box_mg_value_t {
    MeshValueType type;      /**< Which of mesh, curves and points holds the value */
    int32_t refs;            /**< References, changed on the main thread only */
    box_mg_mesh_t mesh;      /**< The Mesh of a MeshValueMesh */
    box_mg_curves_t curves;  /**< The Curves of a MeshValueCurves */
    box_mg_points_t points;  /**< The Points of a MeshValuePoints */
} box_mg_value_t;

/** An input of a node kind: an ecs_entity_t member, or an ecs_vec(ecs_entity_t) for a list */
typedef struct box_mesh_node_input_t {
    const char *name;    /**< Name of the member, NULL ends the list */
    ecs_size_t offset;   /**< Offset of the member */
    MeshValueType type;  /**< Type the input node must output */
    bool list;           /**< The member is an ecs_vec(ecs_entity_t) of input nodes */
    bool optional;       /**< The input may have no node */
} box_mesh_node_input_t;

/** A parameter of a node kind: a member copied into the snapshot of a node */
typedef struct box_mesh_node_param_t {
    const char *name;    /**< Name of the member, NULL ends the list */
    ecs_size_t offset;   /**< Offset of the member */
    ecs_size_t size;     /**< Bytes of the member, without pointers */
} box_mesh_node_param_t;

/** Read the value of a source node from the world, on the main thread.
 *
 * @param world The world
 * @param node The node entity
 * @param component The node component
 * @param out The value to fill, of the output type of the kind
 * @param source Receives the entity it read, whose changes evaluate the graph again
 * @param error Receives why it failed, freed with ecs_os_free
 * @return BOX_MESH_CAPTURE_OK, BOX_MESH_CAPTURE_WAIT, BOX_MESH_CAPTURE_HOLD or
 * BOX_MESH_CAPTURE_ERROR
 */
typedef int (*box_mesh_node_capture_t)(
    const ecs_world_t *world,
    ecs_entity_t node,
    const void *component,
    box_mg_value_t *out,
    ecs_entity_t *source,
    char **error);

/** Compute the value of a node from its inputs, on a worker thread without the world.
 *
 * @param node Snapshot of the node component: its parameters, other members zeroed
 * @param inputs Values of the input nodes of every port in order, NULL for a port without one
 * @param counts Values of each port in inputs: 1, 0 without a node, the length of a list
 * @param out The value to fill, of the output type of the node
 * @param error Receives why it failed, freed with ecs_os_free
 * @return 0 on success, nonzero when it failed
 */
typedef int (*box_mesh_node_eval_t)(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error);

/** Description of a node kind for box_mesh_node_register, with capture or eval */
typedef struct box_mesh_node_desc_t {
    ecs_entity_t component;       /**< Component of the nodes of the kind */
    MeshValueType output;         /**< Type of the value, MeshValueAny for the type of the
                                   *   inputs of type MeshValueAny, which must be one type */
    box_mesh_node_input_t inputs[BOX_MESH_NODE_INPUTS]; /**< Inputs in order */
    box_mesh_node_param_t params[BOX_MESH_NODE_PARAMS]; /**< Parameters in order */
    box_mesh_node_capture_t capture; /**< Reads a source node, NULL for a computed kind */
    box_mesh_node_eval_t eval;    /**< Computes the value, NULL for a source kind */
    uint32_t flags;               /**< BOX_MESH_NODE_RANDOM */
} box_mesh_node_desc_t;

/** Counters of the evaluations and the value cache of the mesh graphs of a world */
typedef struct box_mesh_graph_stats_t {
    int32_t jobs;      /**< Jobs submitted */
    int32_t evaluated; /**< Node values computed */
    int32_t hits;      /**< Node values found in the cache instead of computed */
    int32_t values;    /**< Values in the cache */
    int64_t bytes;     /**< Bytes of the values in the cache */
    int32_t evicted;   /**< Values evicted from the cache */
} box_mesh_graph_stats_t;

/** Register a node kind, or replace the kind of a component registered before; the graphs
 * that use it are evaluated again. box.mesh registers the built in kinds this way.
 *
 * @param world The world, with box.mesh imported
 * @param desc The kind
 * @return The id of the kind, 0 when the description is invalid
 */
int32_t box_mesh_node_register(
    ecs_world_t *world,
    const box_mesh_node_desc_t *desc);

/** Evaluate the value of a node on the calling thread, through the value cache, as a tool that
 * previews a node does. Source nodes whose source is not ready fail.
 *
 * @param world The world
 * @param node The node entity
 * @param error Receives why it failed, freed with ecs_os_free, may be NULL
 * @return The value with a reference for the caller, released with box_mg_value_release, NULL
 * when the evaluation failed
 */
box_mg_value_t* box_mesh_graph_evaluate(
    ecs_world_t *world,
    ecs_entity_t node,
    char **error);

/** Read the counters of the mesh graphs of a world.
 *
 * @param world The world
 * @param stats Receives the counters, zeroed without box.mesh
 */
void box_mesh_graph_stats(
    const ecs_world_t *world,
    box_mesh_graph_stats_t *stats);

/** Create an empty value with one reference.
 *
 * @param type The type of the value
 * @return The value
 */
box_mg_value_t* box_mg_value_new(
    MeshValueType type);

/** Take a reference to a value.
 *
 * @param value The value, NULL is ignored
 * @return The value
 */
box_mg_value_t* box_mg_value_keep(
    box_mg_value_t *value);

/** Drop a reference to a value, freeing it with the last one.
 *
 * @param value The value, NULL is ignored
 */
void box_mg_value_release(
    box_mg_value_t *value);

/** Free the arrays of a value and empty it, keeping its type and references.
 *
 * @param value The value
 */
void box_mg_value_clear(
    box_mg_value_t *value);

/** Resize the arrays of a mesh and of its attributes; new elements are zeroed.
 *
 * @param mesh The mesh
 * @param vertices Number of vertices
 * @param indices Number of indices
 */
void box_mg_mesh_resize(
    box_mg_mesh_t *mesh,
    int32_t vertices,
    int32_t indices);

/** Resize the arrays of curves and of their attributes; new elements are zeroed, new curves
 * have no parent.
 *
 * @param curves The curves
 * @param curve_count Number of curves
 * @param point_count Number of points
 */
void box_mg_curves_resize(
    box_mg_curves_t *curves,
    int32_t curve_count,
    int32_t point_count);

/** Resize the arrays of points and of their attributes; new points have no rotation and a
 * scale of 1.
 *
 * @param points The points
 * @param count Number of points
 */
void box_mg_points_resize(
    box_mg_points_t *points,
    int32_t count);

/** Find an attribute by name.
 *
 * @param attrs The attributes
 * @param count Number of attributes
 * @param name The name
 * @return The values, NULL when there is no such attribute
 */
float* box_mg_attr_find(
    const box_mg_attr_t *attrs,
    int32_t count,
    const char *name);

/** Find an attribute or add it zeroed when there is room.
 *
 * @param attrs The attributes
 * @param count Number of attributes, incremented when one is added
 * @param name The name, shortened to BOX_MESH_ATTR_NAME - 1 bytes
 * @param elements Elements of the value, the length of a new attribute
 * @return The values, NULL when the value has BOX_MESH_ATTRS attributes already
 */
float* box_mg_attr_add(
    box_mg_attr_t *attrs,
    int32_t *count,
    const char *name,
    int32_t elements);

/** Register the box.mesh module.
 *
 * @param world The world
 */
void BoxMeshImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
