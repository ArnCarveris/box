/**
 * @file geometry/geometry.c
 * @brief Registers shape components and turns them into RenderMesh through observers.
 */

#define BOX_GEOMETRY_IMPL
#include "geometry_internal.h"

/** Assign the cached mesh for a key to an entity with the given instance scale.
 *
 * The mesh is set through the stage of the observer, because a shape that
 * arrives with an instantiated prefab is set while the entity is being moved
 * to its table, where a direct set would move it a second time.
 *
 * @param it The observer iterator
 * @param e The entity
 * @param key The mesh key
 * @param scale The instance scale
 */
static void box_shape_apply(
    ecs_iter_t *it,
    ecs_entity_t e,
    const box_mesh_key_t *key,
    vec3 scale)
{
    RenderMesh mesh = { .scale = scale };
    mesh.edge_bounds = key->kind == BoxShapeBox ||
        key->kind == BoxShapeTrianglePrism ||
        key->kind == BoxShapeRightTrianglePrism ||
        (key->kind >= BoxShapeSphere && key->kind <= BoxShapeRoundedBox);
    mesh.handle = box_mesh_cache_get(it->world, key, &mesh.bounds_min, &mesh.bounds_max);
    ecs_set_ptr(it->stage, e, RenderMesh, &mesh);
    box_mesh_lod_set(it->world, it->stage, e, key);
}

/** Key with only a kind, for unit shapes.
 *
 * @param kind The shape family
 * @return The key
 */
static box_mesh_key_t box_key(
    box_shape_kind_t kind)
{
    box_mesh_key_t key = { .kind = kind };

    return key;
}

/** Box observer.
 *
 * @param it Iterator over changed boxes
 */
static void box_on_box(
    ecs_iter_t *it)
{
    Box *b = ecs_field(it, Box, 0);
    box_mesh_key_t key = box_key(BoxShapeBox);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shape_apply(it, it->entities[i], &key, vec3_new(b[i].x, b[i].y, b[i].z));
    }
}

/** Quad observer.
 *
 * @param it Iterator over changed quads
 */
static void box_on_quad(
    ecs_iter_t *it)
{
    Quad *q = ecs_field(it, Quad, 0);
    box_mesh_key_t key = box_key(BoxShapeQuad);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shape_apply(it, it->entities[i], &key, vec3_new(q[i].x, q[i].y, 1));
    }
}

/** Triangle observer.
 *
 * @param it Iterator over changed triangles
 */
static void box_on_triangle(
    ecs_iter_t *it)
{
    Triangle *t = ecs_field(it, Triangle, 0);
    box_mesh_key_t key = box_key(BoxShapeTriangle);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shape_apply(it, it->entities[i], &key, vec3_new(t[i].x, t[i].y, 1));
    }
}

/** RightTriangle observer.
 *
 * @param it Iterator over changed triangles
 */
static void box_on_right_triangle(
    ecs_iter_t *it)
{
    RightTriangle *t = ecs_field(it, RightTriangle, 0);
    box_mesh_key_t key = box_key(BoxShapeRightTriangle);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shape_apply(it, it->entities[i], &key, vec3_new(t[i].x, t[i].y, 1));
    }
}

/** TrianglePrism observer.
 *
 * @param it Iterator over changed prisms
 */
static void box_on_triangle_prism(
    ecs_iter_t *it)
{
    TrianglePrism *t = ecs_field(it, TrianglePrism, 0);
    box_mesh_key_t key = box_key(BoxShapeTrianglePrism);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shape_apply(it, it->entities[i], &key, vec3_new(t[i].x, t[i].y, t[i].z));
    }
}

/** RightTrianglePrism observer.
 *
 * @param it Iterator over changed prisms
 */
static void box_on_right_triangle_prism(
    ecs_iter_t *it)
{
    RightTrianglePrism *t = ecs_field(it, RightTrianglePrism, 0);
    box_mesh_key_t key = box_key(BoxShapeRightTrianglePrism);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shape_apply(it, it->entities[i], &key, vec3_new(t[i].x, t[i].y, t[i].z));
    }
}

/** NGon observer.
 *
 * @param it Iterator over changed polygons
 */
static void box_on_ngon(
    ecs_iter_t *it)
{
    NGon *n = ecs_field(it, NGon, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_mesh_key_t key = { .kind = BoxShapeNGon, .i0 = n[i].sides };
        float r = n[i].radius > 0 ? n[i].radius : 1.0f;
        box_shape_apply(it, it->entities[i], &key, vec3_new(r, r, 1));
    }
}

/** Sphere observer.
 *
 * @param it Iterator over changed spheres
 */
static void box_on_sphere(
    ecs_iter_t *it)
{
    Sphere *s = ecs_field(it, Sphere, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_mesh_key_t key = { .kind = BoxShapeSphere, .i0 = s[i].segments, .smooth = s[i].smooth };
        float r = s[i].radius > 0 ? s[i].radius : 1.0f;
        box_shape_apply(it, it->entities[i], &key, vec3_new(r, r, r));
    }
}

/** HemiSphere observer.
 *
 * @param it Iterator over changed hemispheres
 */
static void box_on_hemisphere(
    ecs_iter_t *it)
{
    HemiSphere *s = ecs_field(it, HemiSphere, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_mesh_key_t key = {
            .kind = BoxShapeHemiSphere, .i0 = s[i].segments, .smooth = s[i].smooth
        };
        float r = s[i].radius > 0 ? s[i].radius : 1.0f;
        box_shape_apply(it, it->entities[i], &key, vec3_new(r, r, r));
    }
}

/** IcoSphere observer.
 *
 * @param it Iterator over changed icospheres
 */
static void box_on_icosphere(
    ecs_iter_t *it)
{
    IcoSphere *s = ecs_field(it, IcoSphere, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_mesh_key_t key = {
            .kind = BoxShapeIcoSphere, .i0 = s[i].segments, .smooth = s[i].smooth
        };
        float r = s[i].radius > 0 ? s[i].radius : 1.0f;
        box_shape_apply(it, it->entities[i], &key, vec3_new(r, r, r));
    }
}

/** Cone observer.
 *
 * @param it Iterator over changed cones
 */
static void box_on_cone(
    ecs_iter_t *it)
{
    Cone *c = ecs_field(it, Cone, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_mesh_key_t key = { .kind = BoxShapeCone, .i0 = c[i].segments, .smooth = c[i].smooth };
        float r = c[i].radius > 0 ? c[i].radius : 1.0f;
        float l = c[i].length > 0 ? c[i].length : 1.0f;
        box_shape_apply(it, it->entities[i], &key, vec3_new(r, l, r));
    }
}

/** Cylinder observer.
 *
 * @param it Iterator over changed cylinders
 */
static void box_on_cylinder(
    ecs_iter_t *it)
{
    Cylinder *c = ecs_field(it, Cylinder, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_mesh_key_t key = {
            .kind = BoxShapeCylinder, .i0 = c[i].segments, .smooth = c[i].smooth
        };
        float r = c[i].radius > 0 ? c[i].radius : 1.0f;
        float l = c[i].length > 0 ? c[i].length : 1.0f;
        box_shape_apply(it, it->entities[i], &key, vec3_new(r, l, r));
    }
}

/** Torus observer.
 *
 * @param it Iterator over changed tori
 */
static void box_on_torus(
    ecs_iter_t *it)
{
    Torus *t = ecs_field(it, Torus, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        float r = t[i].radius > 0 ? t[i].radius : 1.0f;
        float tube = t[i].tube > 0 ? t[i].tube : r * 0.25f;
        box_mesh_key_t key = {
            .kind = BoxShapeTorus, .i0 = t[i].segments, .i1 = t[i].rings,
            .smooth = t[i].smooth, .f0 = tube / r
        };
        box_shape_apply(it, it->entities[i], &key, vec3_new(r, r, r));
    }
}

/** Capsule observer.
 *
 * @param it Iterator over changed capsules
 */
static void box_on_capsule(
    ecs_iter_t *it)
{
    Capsule *c = ecs_field(it, Capsule, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        float r = c[i].radius > 0 ? c[i].radius : 1.0f;
        box_mesh_key_t key = {
            .kind = BoxShapeCapsule, .i0 = c[i].segments, .smooth = c[i].smooth,
            .f0 = c[i].length / r
        };
        box_shape_apply(it, it->entities[i], &key, vec3_new(r, r, r));
    }
}

/** Tube observer.
 *
 * @param it Iterator over changed tubes
 */
static void box_on_tube(
    ecs_iter_t *it)
{
    Tube *t = ecs_field(it, Tube, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        float r = t[i].radius > 0 ? t[i].radius : 1.0f;
        float l = t[i].length > 0 ? t[i].length : 1.0f;
        float thickness = t[i].thickness > 0 ? t[i].thickness : r * 0.2f;
        box_mesh_key_t key = {
            .kind = BoxShapeTube, .i0 = t[i].segments, .smooth = t[i].smooth,
            .f0 = thickness / r, .f1 = t[i].arc > 0 ? t[i].arc : 6.28318530f
        };
        box_shape_apply(it, it->entities[i], &key, vec3_new(r, l, r));
    }
}

/** Frustum observer.
 *
 * @param it Iterator over changed frustums
 */
static void box_on_frustum(
    ecs_iter_t *it)
{
    Frustum *f = ecs_field(it, Frustum, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        float rb = f[i].radius_bottom > 0 ? f[i].radius_bottom : 1.0f;
        float l = f[i].length > 0 ? f[i].length : 1.0f;
        box_mesh_key_t key = {
            .kind = BoxShapeFrustum, .i0 = f[i].segments, .smooth = f[i].smooth,
            .f0 = f[i].radius_top / rb
        };
        box_shape_apply(it, it->entities[i], &key, vec3_new(rb, l, rb));
    }
}

/** RoundedBox observer.
 *
 * @param it Iterator over changed rounded boxes
 */
static void box_on_rounded_box(
    ecs_iter_t *it)
{
    RoundedBox *b = ecs_field(it, RoundedBox, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_mesh_key_t key = {
            .kind = BoxShapeRoundedBox, .i0 = b[i].segments,
            .f0 = b[i].x, .f1 = b[i].y, .f2 = b[i].z, .f3 = b[i].radius
        };
        box_shape_apply(it, it->entities[i], &key, vec3_new(1, 1, 1));
    }
}

/** Register a shape observer.
 *
 * @param world The world
 * @param component The shape component
 * @param callback The observer callback
 */
static void box_shape_watch(
    ecs_world_t *world,
    ecs_entity_t component,
    ecs_iter_action_t callback)
{
    ecs_add_pair(world, component, EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, component, EcsWith, ecs_id(RenderMesh));
    ecs_observer(world, {
        .query.terms = {{ .id = component, .inout = EcsIn }},
        .events = { EcsOnSet },
        .callback = callback
    });
}

void BoxGeometryImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxGeometry);
    ECS_IMPORT(world, BoxRender);

    ECS_META_COMPONENT(world, SweepVertex);
    ECS_META_COMPONENT(world, Mesh3);
    ECS_META_COMPONENT(world, Box);
    ECS_META_COMPONENT(world, Quad);
    ECS_META_COMPONENT(world, Triangle);
    ECS_META_COMPONENT(world, RightTriangle);
    ECS_META_COMPONENT(world, TrianglePrism);
    ECS_META_COMPONENT(world, RightTrianglePrism);
    ECS_META_COMPONENT(world, NGon);
    ECS_META_COMPONENT(world, Sphere);
    ECS_META_COMPONENT(world, HemiSphere);
    ECS_META_COMPONENT(world, IcoSphere);
    ECS_META_COMPONENT(world, Cone);
    ECS_META_COMPONENT(world, Cylinder);
    ECS_META_COMPONENT(world, Torus);
    ECS_META_COMPONENT(world, Capsule);
    ECS_META_COMPONENT(world, Tube);
    ECS_META_COMPONENT(world, Frustum);
    ECS_META_COMPONENT(world, RoundedBox);
    ECS_META_COMPONENT(world, Rect);
    ECS_META_COMPONENT(world, Circle);
    ECS_META_COMPONENT(world, Border);
    ECS_META_COMPONENT(world, Line);
    ECS_META_COMPONENT(world, ImageFit);
    ECS_META_COMPONENT(world, Image);
    ECS_META_COMPONENT(world, Star);

    box_mesh_cache_init(world);
    box_mesh_lod_init(world);
    box_mesh3_init(world);
    box_shape_init(world);
    box_shape2d_init(world);
    box_shape_example_init(world);

    box_shape_watch(world, ecs_id(Box), box_on_box);
    box_shape_watch(world, ecs_id(Quad), box_on_quad);
    box_shape_watch(world, ecs_id(Triangle), box_on_triangle);
    box_shape_watch(world, ecs_id(RightTriangle), box_on_right_triangle);
    box_shape_watch(world, ecs_id(TrianglePrism), box_on_triangle_prism);
    box_shape_watch(world, ecs_id(RightTrianglePrism), box_on_right_triangle_prism);
    box_shape_watch(world, ecs_id(NGon), box_on_ngon);
    box_shape_watch(world, ecs_id(Sphere), box_on_sphere);
    box_shape_watch(world, ecs_id(HemiSphere), box_on_hemisphere);
    box_shape_watch(world, ecs_id(IcoSphere), box_on_icosphere);
    box_shape_watch(world, ecs_id(Cone), box_on_cone);
    box_shape_watch(world, ecs_id(Cylinder), box_on_cylinder);
    box_shape_watch(world, ecs_id(Torus), box_on_torus);
    box_shape_watch(world, ecs_id(Capsule), box_on_capsule);
    box_shape_watch(world, ecs_id(Tube), box_on_tube);
    box_shape_watch(world, ecs_id(Frustum), box_on_frustum);
    box_shape_watch(world, ecs_id(RoundedBox), box_on_rounded_box);
}
