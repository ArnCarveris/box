#ifndef FLECS_ENGINE_MESH_OPS_H
#define FLECS_ENGINE_MESH_OPS_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_MESH_OPS_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* Mesh ops: solid modelling for asset scripts.
 *
 * The primitives in geometry3 answer "what shape is this part". mesh_ops
 * answers the two questions a primitive cannot: "what is left of this shape
 * once I cut something out of it", and "what shape does this outline sweep
 * out". A wall with six windows in it is a box minus six boxes; a sign is an
 * outline pushed out to a thickness; a lamp post is a silhouette spun around
 * its axis. Modelling those from primitives means either accepting seams
 * where parts abut, or hand-splitting the wall into a dozen boxes.
 *
 * Everything here produces an ordinary mesh asset: a prefab under
 * flecs.engine.mesh_ops holding a FlecsMesh3, which the entity is bound to
 * with IsA - exactly how Box and Cylinder bind to their shared meshes. The
 * normal renderer path draws the result, so materials, textures, wear,
 * shadows and instancing all work unchanged. Results are cached by a hash of
 * the description that produced them, so two walls with the same window
 * layout share one mesh and one render group; only the transform differs.
 *
 * Generation is deferred to the next frame rather than done in the component
 * hook, because a CSG tree is described by an entity's children and those do
 * not exist yet while the parent's own components are being set. Set the
 * components, run a frame, and the mesh is there.
 *
 *
 * Extrude - push a closed 2D outline out along Y
 *
 * The profile is a closed polygon in the XZ plane (the loop closes from the
 * last point back to the first; do not repeat it). It may be concave. height
 * is the thickness along Y; center puts the result on [-height/2, height/2]
 * instead of [0, height].
 *
 *   sign {
 *     Extrude: {
 *       profile: [{-1.0, 0.0}, {0.4, 0.0}, {0.4, -0.3},
 *                 {1.0, 0.25}, {0.4, 0.8}, {0.4, 0.5}, {-1.0, 0.5}],
 *       height: 0.08,
 *       center: true
 *     }
 *     Rgba: {200, 60, 40}
 *   }
 *
 *
 * Lathe - spin a silhouette around Y
 *
 * The profile is a list of (radius, y) points read as a closed polygon in the
 * radius/height plane, so a lamp post runs up one side of the silhouette and
 * back down the axis. segments is the number of sides around; smooth welds
 * the barrel's normals so it shades round instead of faceted (creases sharper
 * than 40 degrees stay sharp, so caps and shoulders keep their edge).
 *
 *   bollard {
 *     Lathe: {
 *       profile: [{0.0, 0.0}, {0.16, 0.0}, {0.16, 0.9},
 *                 {0.12, 0.95}, {0.12, 1.0}, {0.0, 1.05}],
 *       segments: 24,
 *       smooth: true
 *     }
 *   }
 *
 *
 * Csg - boolean the children of an entity together
 *
 * The operands are the entity's children, in creation order. Each one is any
 * entity that resolves to a mesh - a Box, Cylinder, Sphere, an Extrude or
 * Lathe, a glTF mesh - positioned with Position3/Rotation3/Scale3 relative to
 * the parent. The first child is the base; the rest are applied in order with
 * the parent's op. A child can override the op with CsgOperator, so one
 * operand subtracts while another adds.
 *
 * Operand children are marked with the CsgOperand tag and disabled, so they
 * do not render or take part in transforms; only the parent's result mesh is
 * drawn.
 *
 * An operand can be generated itself - an Extrude, a Lathe, another Csg with
 * children of its own - because generation runs deepest entity first, so a
 * lathed cutter has its mesh before the wall that subtracts it looks for one.
 *
 *   wall {
 *     Csg: {op: CsgDifference}
 *     Rgba: {180, 175, 165}
 *     shell   { Box: {8, 3, 0.3} }
 *     window0 { Box: {1.2, 1.4, 1}  Position3: {-2.4, 0.4, 0} }
 *     window1 { Box: {1.2, 1.4, 1}  Position3: { 0.0, 0.4, 0} }
 *     sill    { Box: {1.4, 0.1, 0.5} Position3: {-2.4, -0.4, 0}
 *               CsgOperator: {op: CsgUnion} }
 *   }
 *
 *
 * Modifiers - refine whatever mesh the entity ended up with
 *
 * Subdivide, Smooth and SmoothNormals apply to the entity's generated mesh in
 * that fixed order. They are part of the cache key, so a subdivided box is its
 * own shared asset and blocky boxes elsewhere are untouched.
 *
 *   Subdivide: {levels: 2}                   4x the triangles per level
 *   Smooth: {iterations: 6, factor: 0.5}     Laplacian relaxation, boundary
 *                                            vertices stay put
 *   SmoothNormals: {angle: 50}               weld and average normals across
 *                                            creases shallower than 50 deg
 *
 * To round off a primitive, wrap it in a Csg with a single operand. That is
 * also what turns any primitive into a mesh_ops mesh:
 *
 *   boulder {
 *     Csg: {op: CsgUnion}
 *     Subdivide: {levels: 2}
 *     Smooth: {iterations: 8, factor: 0.5}
 *     SmoothNormals: {angle: 60}
 *     rock { Box: {2, 1.6, 2} }
 *   }
 *
 * Modifiers also apply on their own to an entity that inherits a mesh from
 * somewhere else - a Cylinder, a Sphere, a glTF asset - in which case they
 * take over its IsA. Box, Quad, Triangle, RightTriangle, TrianglePrism and
 * RightTrianglePrism are the exception: those six are drawn by a batch that
 * keys on the component rather than on the inherited mesh, so modifying them
 * in place would draw them twice. mesh_ops warns and does nothing there; use
 * the Csg wrapper above.
 *
 * Subdivide then Smooth is the way to round off a blocky part: subdivision on
 * its own only adds triangles, and Laplacian smoothing needs those triangles
 * to have anything to relax. SmoothNormals changes no geometry at all - it is
 * how CSG output stops looking faceted, since Manifold merges coplanar faces
 * and hands back hard edges everywhere.
 *
 *
 * UVs
 *
 * Every generated mesh is unwrapped by box projection: each triangle is
 * projected along its dominant axis, in world units, so a texture tiles once
 * per unit and lines up across faces without the script having to unwrap
 * anything. TextureTransform scales that the usual way.
 */

ECS_ENUM(FlecsCsgOp, {
    FlecsCsgUnion,
    FlecsCsgDifference,
    FlecsCsgIntersection
});

extern ECS_COMPONENT_DECLARE(FlecsCsgOp);

/* Closed polygon in XZ, extruded along Y. */
ECS_STRUCT(FlecsExtrude, {
    ecs_vec_t profile;   /* vec<vec2>, XZ, closed implicitly, may be concave */
    float height;
    bool center;
});

/* Silhouette of (radius, y) points, revolved around Y. */
ECS_STRUCT(FlecsLathe, {
    ecs_vec_t profile;   /* vec<vec2>, (radius, y), closed implicitly */
    int32_t segments;
    bool smooth;
});

/* Boolean the entity's children together into one mesh. */
ECS_STRUCT(FlecsCsg, {
    FlecsCsgOp op;
});

/* Per-operand override of the parent's op. */
ECS_STRUCT(FlecsCsgOperator, {
    FlecsCsgOp op;
});

/* Added by the engine to the children a Csg consumed. */
extern ECS_TAG_DECLARE(FlecsCsgOperand);

/* Split every triangle into four, levels times. */
ECS_STRUCT(FlecsSubdivide, {
    int32_t levels;
});

/* Laplacian relaxation; boundary vertices are kept in place. */
ECS_STRUCT(FlecsSmooth, {
    int32_t iterations;
    float factor;
});

/* Weld and average normals across creases below angle (degrees). */
ECS_STRUCT(FlecsSmoothNormals, {
    float angle;
});

void FlecsEngineMesh_opsImport(
    ecs_world_t *world);

double flecsEngine_meshOps_buildSeconds(void);

#endif
