/**
 * @file BevelShapes.c
 * @brief Bevel on the round shapes, the triangle prisms and ProfileExtrusion: the rims in the
 * units of the shape, normals, texture coordinates, clamping, the mesh cache and how the mesh
 * resolves with the order of host and Bevel, removal and prefabs.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "mesh_cache.h"
#include "lod_keys.h"
#include "mesh_gen.h"

/** Profiles of the extrusions, a 2 by 1 rectangle with and without a 0.6 by 0.4 hole */
static const char *bs_profiles =
    "using box.geometry\n"
    "rect_outline { Path2D: {start: {-1, -0.5}, segments: ["
    "{kind: Line, end: {1, -0.5}}, {kind: Line, end: {1, 0.5}}, {kind: Line, end: {-1, 0.5}}],"
    " closed: true}}\n"
    "hole_outline { Path2D: {start: {-0.3, -0.2}, segments: ["
    "{kind: Line, end: {0.3, -0.2}}, {kind: Line, end: {0.3, 0.2}},"
    " {kind: Line, end: {-0.3, 0.2}}], closed: true}}\n"
    "wide_outline { Path2D: {start: {-1.5, -0.5}, segments: ["
    "{kind: Line, end: {1.5, -0.5}}, {kind: Line, end: {1.5, 0.5}},"
    " {kind: Line, end: {-1.5, 0.5}}], closed: true}}\n"
    "rect { Profile2D: {outer: rect_outline}}\n"
    "holed { Profile2D: {outer: rect_outline, holes: [hole_outline]}}\n";

/** Create a world with box.geometry and the profiles of the extrusions.
 *
 * @return The world
 */
static ecs_world_t* bs_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    test_int(ecs_script_run(world, "profiles", bs_profiles, NULL), 0);

    return world;
}

/** Get the resolved shape of an entity, asserting it has a mesh.
 *
 * @param world The world
 * @param e The entity
 * @return The resolved shape
 */
static const ShapeMesh* bs_shape(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_assert(sm->mesh != 0);
    test_assert(box_mesh_cache_get(box_mesh_cache_of(world), sm->mesh) != NULL);

    return sm;
}

/** Get the mesh of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The mesh
 */
static const box_mesh_t* bs_mesh(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return box_mesh_cache_get(box_mesh_cache_of(world), bs_shape(world, e)->mesh);
}

/** Get a vertex in the units of the shape, with its normal in those units.
 *
 * @param sm The resolved shape
 * @param v The vertex
 * @param pos Receives the position
 * @param normal Receives the unit normal
 */
static void bs_units(
    const ShapeMesh *sm,
    const box_vertex_t *v,
    vec3 pos,
    vec3 normal)
{
    box_surface_vertex(sm, v, pos);
    for (int32_t k = 0; k < 3; k ++) {
        pos[k] *= sm->scale[k];
        normal[k] = sm->scale[k] != 0.0f ? v->normal[k] / sm->scale[k] : 0.0f;
    }

    glm_vec3_normalize(normal);
}

/** Create an entity with a host shape and a Bevel.
 *
 * @param world The world
 * @param host The host component
 * @param value The host value
 * @param bevel The bevel
 * @return The entity
 */
static ecs_entity_t bs_entity(
    ecs_world_t *world,
    ecs_entity_t host,
    const void *value,
    Bevel bevel)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, host);
    ecs_entity_t e = ecs_new(world);
    ecs_set_id(world, e, host, (size_t)ti->size, value);
    ecs_set_ptr(world, e, Bevel, &bevel);

    return e;
}

/** Create an entity with a plain host shape.
 *
 * @param world The world
 * @param host The host component
 * @param value The host value
 * @return The entity
 */
static ecs_entity_t bs_plain(
    ecs_world_t *world,
    ecs_entity_t host,
    const void *value)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, host);
    ecs_entity_t e = ecs_new(world);
    ecs_set_id(world, e, host, (size_t)ti->size, value);

    return e;
}

/** Assert that every normal of a mesh has unit length.
 *
 * @param mesh The mesh
 */
static void bs_unit_normals(
    const box_mesh_t *mesh)
{
    test_assert(mesh->vertex_count > 0);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        test_assert(fabsf(glm_vec3_norm(mesh->vertices[i].normal) - 1.0f) < 1e-4f);
    }
}

/** Assert that two entities resolve to the same bounds and scale.
 *
 * @param world The world
 * @param a The first entity
 * @param b The second entity
 */
static void bs_same_bounds(
    ecs_world_t *world,
    ecs_entity_t a,
    ecs_entity_t b)
{
    const ShapeMesh *sa = bs_shape(world, a), *sb = bs_shape(world, b);
    const box_mesh_t *ma = bs_mesh(world, a), *mb = bs_mesh(world, b);
    for (int32_t k = 0; k < 3; k ++) {
        test_flt(sa->scale[k], sb->scale[k]);
        test_assert(fabsf(ma->min[k] - mb->min[k]) < 1e-4f);
        test_assert(fabsf(ma->max[k] - mb->max[k]) < 1e-4f);
    }
}

/** Assert that the bounds of a beveled entity lie within those of its unbeveled twin and keep
 * its height, the rims only round away corners.
 *
 * @param world The world
 * @param beveled The beveled entity
 * @param plain The unbeveled entity
 * @param axis The axis whose extent is kept
 */
static void bs_within_bounds(
    ecs_world_t *world,
    ecs_entity_t beveled,
    ecs_entity_t plain,
    int32_t axis)
{
    const ShapeMesh *sa = bs_shape(world, beveled), *sb = bs_shape(world, plain);
    const box_mesh_t *ma = bs_mesh(world, beveled), *mb = bs_mesh(world, plain);
    for (int32_t k = 0; k < 3; k ++) {
        test_flt(sa->scale[k], sb->scale[k]);
        test_assert(ma->min[k] >= mb->min[k] - 1e-4f);
        test_assert(ma->max[k] <= mb->max[k] + 1e-4f);
    }

    test_assert(fabsf(ma->min[axis] - mb->min[axis]) < 1e-4f);
    test_assert(fabsf(ma->max[axis] - mb->max[axis]) < 1e-4f);
}

/** Check the vertices of a round shape on one side of a rim center: each lies on the circle of
 * the rim in the units of the shape, with the normal pointing away from its center.
 *
 * Vertices whose distance to the axis is on the side of the center given by side, and whose
 * height is on the side given by up, are checked; the rest of the shape lies elsewhere.
 *
 * @param world The world
 * @param e The entity
 * @param cr Distance of the rim center to the axis
 * @param cy Height of the rim center
 * @param radius Radius of the rim
 * @param side 1 for vertices farther from the axis than the center, -1 for closer
 * @param up 1 for vertices above the center, -1 for below
 * @param normals True to check the normals
 * @return The number of vertices checked
 */
static int32_t bs_round_rim(
    ecs_world_t *world,
    ecs_entity_t e,
    float cr,
    float cy,
    float radius,
    float side,
    float up,
    bool normals)
{
    const ShapeMesh *sm = bs_shape(world, e);
    const box_mesh_t *mesh = bs_mesh(world, e);
    int32_t result = 0;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        vec3 pos, normal;
        bs_units(sm, &mesh->vertices[i], pos, normal);
        float r = hypotf(pos[0], pos[2]);
        if (side * (r - cr) < -1e-4f || up * (pos[1] - cy) < -1e-4f || r < 1e-5f) {
            continue;
        }

        float dr = r - cr, dy = pos[1] - cy;
        float distance = hypotf(dr, dy);
        test_assert(fabsf(distance - radius) < 2e-3f);
        if (normals) {
            float nr = (normal[0] * pos[0] + normal[2] * pos[2]) / r;
            test_assert(nr * dr / distance + normal[1] * dy / distance > 0.999f);
        }

        result ++;
    }

    return result;
}

/** Find the largest distance to the axis of the vertices at a height.
 *
 * @param world The world
 * @param e The entity
 * @param y The height in the units of the shape
 * @return The distance, -1 when no vertex is at that height
 */
static float bs_reach(
    ecs_world_t *world,
    ecs_entity_t e,
    float y)
{
    const ShapeMesh *sm = bs_shape(world, e);
    const box_mesh_t *mesh = bs_mesh(world, e);
    float result = -1.0f;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        vec3 pos, normal;
        bs_units(sm, &mesh->vertices[i], pos, normal);
        if (fabsf(pos[1] - y) < 1e-4f) {
            result = fmaxf(result, hypotf(pos[0], pos[2]));
        }
    }

    return result;
}

void BevelShapes_cylinder_bounds(void) {
    ecs_world_t *world = bs_world();
    Cylinder cylinder = { .segments = 24, .smooth = true, .length = 3, .radius = 0.5f };
    ecs_entity_t plain = bs_plain(world, ecs_id(Cylinder), &cylinder);
    ecs_entity_t e = bs_entity(world, ecs_id(Cylinder), &cylinder,
        (Bevel){ .radius = 0.2f, .segments = 4 });
    test_assert(bs_shape(world, e)->mesh != bs_shape(world, plain)->mesh);
    bs_same_bounds(world, e, plain);
    bs_unit_normals(bs_mesh(world, e));
    ecs_fini(world);
}

void BevelShapes_cylinder_rim(void) {
    ecs_world_t *world = bs_world();
    Cylinder cylinder = { .segments = 24, .smooth = true, .length = 3, .radius = 0.5f };
    ecs_entity_t e = bs_entity(world, ecs_id(Cylinder), &cylinder,
        (Bevel){ .radius = 0.2f, .segments = 4 });
    test_assert(bs_round_rim(world, e, 0.3f, 1.3f, 0.2f, 1, 1, true) > 24 * 4);
    test_assert(bs_round_rim(world, e, 0.3f, -1.3f, 0.2f, 1, -1, true) > 24 * 4);
    ecs_fini(world);
}

void BevelShapes_cylinder_top_only(void) {
    ecs_world_t *world = bs_world();
    Cylinder cylinder = { .segments = 16, .smooth = true, .length = 2, .radius = 1 };
    ecs_entity_t top = bs_entity(world, ecs_id(Cylinder), &cylinder,
        (Bevel){ .top = 0.3f, .segments = 4 });
    ecs_entity_t bottom = bs_entity(world, ecs_id(Cylinder), &cylinder,
        (Bevel){ .bottom = 0.3f, .segments = 4 });
    test_assert(fabsf(bs_reach(world, top, -1.0f) - 1.0f) < 1e-3f);
    test_assert(fabsf(bs_reach(world, top, 1.0f) - 0.7f) < 1e-3f);
    test_assert(bs_round_rim(world, top, 0.7f, 0.7f, 0.3f, 1, 1, true) > 0);
    test_assert(fabsf(bs_reach(world, bottom, 1.0f) - 1.0f) < 1e-3f);
    test_assert(fabsf(bs_reach(world, bottom, -1.0f) - 0.7f) < 1e-3f);
    test_assert(bs_round_rim(world, bottom, 0.7f, -0.7f, 0.3f, 1, -1, true) > 0);
    ecs_fini(world);
}

void BevelShapes_cylinder_flat_facets(void) {
    ecs_world_t *world = bs_world();
    Cylinder cylinder = { .segments = 8, .smooth = false, .length = 2, .radius = 1 };
    ecs_entity_t e = bs_entity(world, ecs_id(Cylinder), &cylinder,
        (Bevel){ .radius = 0.3f, .segments = 4 });
    const box_mesh_t *mesh = bs_mesh(world, e);
    int32_t sloped[8] = {0};
    bs_unit_normals(mesh);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const float *n = mesh->vertices[i].normal;
        float h = hypotf(n[0], n[2]);
        if (h < 1e-5f) {
            continue;
        }

        float angle = atan2f(n[2], n[0]);
        float column = angle / (2.0f * BOX_PI) * 8.0f - 0.5f;
        float nearest = roundf(column);
        test_assert(fabsf(column - nearest) < 1e-3f);
        if (fabsf(n[1]) > 1e-3f && fabsf(n[1]) < 0.999f) {
            sloped[((int32_t)nearest % 8 + 8) % 8] ++;
        }
    }

    for (int32_t s = 0; s < 8; s ++) {
        test_assert(sloped[s] >= 2 * 3 * 2);
    }

    ecs_fini(world);
}

void BevelShapes_cylinder_wall_uvs(void) {
    ecs_world_t *world = bs_world();
    Cylinder cylinder = { .segments = 16, .smooth = true, .length = 2, .radius = 0.5f };
    ecs_entity_t plain = bs_plain(world, ecs_id(Cylinder), &cylinder);
    ecs_entity_t e = bs_entity(world, ecs_id(Cylinder), &cylinder,
        (Bevel){ .radius = 0.2f, .segments = 4 });
    const box_mesh_t *pm = bs_mesh(world, plain);
    const box_mesh_t *mesh = bs_mesh(world, e);
    int32_t walls = 0;
    bool below = false, above = false;
    for (int32_t i = 0; i < pm->vertex_count; i ++) {
        const box_vertex_t *v = &pm->vertices[i];
        if (fabsf(v->normal[1]) < 1e-5f) {
            test_assert(fabsf(v->uv[1] - (0.5f - v->position[1])) < 1e-5f);
        }
    }

    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        if (fabsf(v->normal[1]) < 1e-5f) {
            test_assert(fabsf(v->uv[1] - (0.5f - v->position[1])) < 1e-5f);
            walls ++;
        } else if (hypotf(v->normal[0], v->normal[2]) > 1e-5f) {
            below |= v->uv[1] < 0.0f;
            above |= v->uv[1] > 1.0f;
        }
    }

    test_assert(walls > 0);
    test_assert(below);
    test_assert(above);
    ecs_fini(world);
}

void BevelShapes_frustum_bounds(void) {
    ecs_world_t *world = bs_world();
    Frustum frustum = { .segments = 24, .smooth = true, .length = 2,
        .radius_bottom = 1, .radius_top = 0.6f };
    ecs_entity_t plain = bs_plain(world, ecs_id(Frustum), &frustum);
    ecs_entity_t e = bs_entity(world, ecs_id(Frustum), &frustum,
        (Bevel){ .radius = 0.15f, .segments = 4 });
    test_assert(bs_shape(world, e)->mesh != bs_shape(world, plain)->mesh);
    bs_within_bounds(world, e, plain, 1);
    bs_unit_normals(bs_mesh(world, e));
    ecs_fini(world);
}

void BevelShapes_frustum_rim(void) {
    ecs_world_t *world = bs_world();
    Frustum frustum = { .segments = 24, .smooth = true, .length = 2,
        .radius_bottom = 1, .radius_top = 0.6f };
    ecs_entity_t e = bs_entity(world, ecs_id(Frustum), &frustum,
        (Bevel){ .bottom = 0.2f, .segments = 4 });
    float turn = BOX_PI - atan2f(2.0f, 0.4f);
    float setback = 0.2f * tanf(0.5f * turn);
    test_assert(bs_round_rim(world, e, 1.0f - setback, -0.8f, 0.2f, 1, -1, true) > 24);
    test_assert(fabsf(bs_reach(world, e, 1.0f) - 0.6f) < 1e-3f);
    ecs_fini(world);
}

void BevelShapes_frustum_steep(void) {
    ecs_world_t *world = bs_world();
    Frustum frustum = { .segments = 24, .smooth = true, .length = 0.3f,
        .radius_bottom = 1, .radius_top = 0.1f };
    ecs_entity_t e = bs_entity(world, ecs_id(Frustum), &frustum,
        (Bevel){ .bottom = 0.5f, .segments = 4 });
    const ShapeMesh *sm = bs_shape(world, e);
    const box_mesh_t *mesh = bs_mesh(world, e);
    float flat = 0.0f;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        vec3 pos, normal;
        bs_units(sm, &mesh->vertices[i], pos, normal);
        if (normal[1] < -0.9999f && fabsf(pos[1] + 0.15f) < 1e-4f) {
            flat = fmaxf(flat, hypotf(pos[0], pos[2]));
        }
    }

    test_assert(fabsf(flat - (1.0f - hypotf(0.9f, 0.3f))) < 5e-3f);
    bs_unit_normals(mesh);
    ecs_fini(world);
}

void BevelShapes_cone_bounds(void) {
    ecs_world_t *world = bs_world();
    Cone cone = { .segments = 24, .smooth = true, .length = 2, .radius = 0.5f };
    ecs_entity_t plain = bs_plain(world, ecs_id(Cone), &cone);
    ecs_entity_t e = bs_entity(world, ecs_id(Cone), &cone,
        (Bevel){ .bottom = 0.1f, .segments = 4 });
    test_assert(bs_shape(world, e)->mesh != bs_shape(world, plain)->mesh);
    bs_within_bounds(world, e, plain, 1);
    bs_unit_normals(bs_mesh(world, e));
    float turn = BOX_PI - atan2f(2.0f, 0.5f);
    float setback = 0.1f * tanf(0.5f * turn);
    test_assert(bs_round_rim(world, e, 0.5f - setback, -0.9f, 0.1f, 1, -1, true) > 24);
    ecs_fini(world);
}

void BevelShapes_cone_top_ignored(void) {
    ecs_world_t *world = bs_world();
    Cone cone = { .segments = 24, .smooth = true, .length = 2, .radius = 0.5f };
    ecs_entity_t plain = bs_plain(world, ecs_id(Cone), &cone);
    ecs_entity_t bottom = bs_entity(world, ecs_id(Cone), &cone,
        (Bevel){ .bottom = 0.1f, .segments = 4 });
    ecs_entity_t both = bs_entity(world, ecs_id(Cone), &cone,
        (Bevel){ .bottom = 0.1f, .top = 0.3f, .segments = 4 });
    ecs_entity_t top = bs_entity(world, ecs_id(Cone), &cone,
        (Bevel){ .top = 0.3f, .segments = 4 });
    test_uint(bs_shape(world, both)->mesh, bs_shape(world, bottom)->mesh);
    test_uint(bs_shape(world, top)->mesh, bs_shape(world, plain)->mesh);

    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const box_shape_t *variant = box_mesh_cache_shape(cache,
        ecs_pair(ecs_id(Bevel), ecs_id(Cone)));
    test_assert(variant != NULL);
    box_bevel_cylinder_t a = { .host = { 24, true, 2, 0.5f }, .bevel = { .bottom = 0.1f } };
    box_bevel_cylinder_t b = a;
    b.bevel.top = 0.3f;
    box_shape_key_t ka = {0}, kb = {0};
    variant->desc.key(&a, &ka);
    variant->desc.key(&b, &kb);
    test_assert(!ecs_os_memcmp_t(&ka, &kb, box_shape_key_t));
    ecs_fini(world);
}

void BevelShapes_tube_bounds(void) {
    ecs_world_t *world = bs_world();
    Tube tube = { .segments = 24, .smooth = true, .length = 2, .radius = 1, .thickness = 0.4f };
    ecs_entity_t plain = bs_plain(world, ecs_id(Tube), &tube);
    ecs_entity_t e = bs_entity(world, ecs_id(Tube), &tube,
        (Bevel){ .radius = 0.1f, .segments = 4 });
    test_assert(bs_shape(world, e)->mesh != bs_shape(world, plain)->mesh);
    bs_same_bounds(world, e, plain);
    bs_unit_normals(bs_mesh(world, e));
    ecs_fini(world);
}

void BevelShapes_tube_rims(void) {
    ecs_world_t *world = bs_world();
    Tube tube = { .segments = 24, .smooth = true, .length = 3, .radius = 1, .thickness = 0.4f };
    ecs_entity_t e = bs_entity(world, ecs_id(Tube), &tube,
        (Bevel){ .top = 0.15f, .bottom = 0.1f, .segments = 4 });
    test_assert(bs_round_rim(world, e, 0.85f, 1.35f, 0.15f, 1, 1, true) > 24);
    test_assert(bs_round_rim(world, e, 0.75f, 1.35f, 0.15f, -1, 1, true) > 24);
    test_assert(bs_round_rim(world, e, 0.9f, -1.4f, 0.1f, 1, -1, true) > 24);
    test_assert(bs_round_rim(world, e, 0.7f, -1.4f, 0.1f, -1, -1, true) > 24);
    ecs_fini(world);
}

void BevelShapes_tube_thin_wall(void) {
    ecs_world_t *world = bs_world();
    Tube tube = { .segments = 24, .smooth = true, .length = 2, .radius = 1, .thickness = 0.2f };
    ecs_entity_t e = bs_entity(world, ecs_id(Tube), &tube,
        (Bevel){ .top = 0.3f, .segments = 4 });
    test_assert(bs_round_rim(world, e, 0.9f, 0.9f, 0.1f, 1, 1, true) > 24);
    test_assert(bs_round_rim(world, e, 0.9f, 0.9f, 0.1f, -1, 1, true) > 24);
    test_assert(fabsf(bs_reach(world, e, -1.0f) - 1.0f) < 1e-3f);
    ecs_fini(world);
}

void BevelShapes_tube_partial(void) {
    ecs_world_t *world = bs_world();
    Tube tube = { .segments = 12, .smooth = true, .length = 2, .radius = 1,
        .thickness = 0.4f, .arc = BOX_PI };
    ecs_entity_t plain = bs_plain(world, ecs_id(Tube), &tube);
    ecs_entity_t e = bs_entity(world, ecs_id(Tube), &tube,
        (Bevel){ .radius = 0.1f, .segments = 4 });
    bs_within_bounds(world, e, plain, 1);
    bs_unit_normals(bs_mesh(world, e));
    test_assert(bs_round_rim(world, e, 0.9f, 0.9f, 0.1f, 1, 1, false) > 12);

    const box_mesh_t *mesh = bs_mesh(world, e);
    int32_t rounded = 0;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        if (v->normal[2] < -0.9999f && fabsf(v->position[2]) < 1e-5f) {
            float r = v->position[0], y = v->position[1] * 2.0f;
            if (r > 0.9f && y > 0.9f) {
                test_assert(fabsf(hypotf(r - 0.9f, y - 0.9f) - 0.1f) < 2e-3f);
                rounded ++;
            }
        }
    }

    test_assert(rounded >= 3);
    ecs_fini(world);
}

/** Distance from a point to a triangle in the XY plane, 0 inside.
 *
 * @param q The corners, counter-clockwise
 * @param p The point
 * @param closest Receives the closest point of the triangle
 * @return The distance
 */
static float bs_triangle_distance(
    const vec2 q[3],
    const float *p,
    vec2 closest)
{
    bool inside = true;
    float best = 1e9f;
    for (int32_t i = 0; i < 3; i ++) {
        const float *a = q[i], *b = q[(i + 1) % 3];
        float ex = b[0] - a[0], ey = b[1] - a[1];
        inside &= ex * (p[1] - a[1]) - ey * (p[0] - a[0]) >= -1e-6f;
        float t = glm_clamp(((p[0] - a[0]) * ex + (p[1] - a[1]) * ey) /
            (ex * ex + ey * ey), 0.0f, 1.0f);
        float cx = a[0] + ex * t, cy = a[1] + ey * t;
        float d = hypotf(p[0] - cx, p[1] - cy);
        if (d < best) {
            best = d;
            closest[0] = cx;
            closest[1] = cy;
        }
    }

    if (inside) {
        closest[0] = p[0];
        closest[1] = p[1];
        best = 0.0f;
    }

    return best;
}

/** Check that every vertex of a beveled prism lies at the radius from the prism shrunk by the
 * radius, with its normal pointing away from it.
 *
 * @param world The world
 * @param e The entity
 * @param right True for a RightTrianglePrism
 * @param size The size of the prism
 * @param radius The radius after clamping
 */
static void bs_prism_rim(
    ecs_world_t *world,
    ecs_entity_t e,
    bool right,
    const vec3 size,
    float radius)
{
    const ShapeMesh *sm = bs_shape(world, e);
    const box_mesh_t *mesh = bs_mesh(world, e);
    vec2 p[3] = { { -0.5f * size[0], -0.5f * size[1] }, { 0.5f * size[0], -0.5f * size[1] },
        { right ? -0.5f * size[0] : 0.0f, 0.5f * size[1] } };
    vec2 n[3], q[3];
    for (int32_t i = 0; i < 3; i ++) {
        const float *a = p[i], *b = p[(i + 1) % 3];
        float length = hypotf(b[0] - a[0], b[1] - a[1]);
        n[i][0] = (b[1] - a[1]) / length;
        n[i][1] = (a[0] - b[0]) / length;
    }

    for (int32_t i = 0; i < 3; i ++) {
        const float *before = n[(i + 2) % 3], *after = n[i];
        float sum = 1.0f + before[0] * after[0] + before[1] * after[1];
        q[i][0] = p[i][0] - radius * (before[0] + after[0]) / sum;
        q[i][1] = p[i][1] - radius * (before[1] + after[1]) / sum;
    }

    float half = 0.5f * size[2] - radius;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        vec3 pos, normal;
        vec2 closest;
        bs_units(sm, &mesh->vertices[i], pos, normal);
        float dxy = bs_triangle_distance(q, pos, closest);
        float cz = glm_clamp(pos[2], -half, half);
        float dz = pos[2] - cz;
        float distance = hypotf(dxy, dz);
        test_assert(fabsf(distance - radius) < 2e-3f);
        vec3 away = { pos[0] - closest[0], pos[1] - closest[1], dz };
        glm_vec3_normalize(away);
        test_assert(glm_vec3_dot(away, normal) > 0.999f);
    }
}

void BevelShapes_prism_bounds(void) {
    ecs_world_t *world = bs_world();
    TrianglePrism prism = { 2, 1, 0.5f };
    ecs_entity_t plain = bs_plain(world, ecs_id(TrianglePrism), &prism);
    ecs_entity_t e = bs_entity(world, ecs_id(TrianglePrism), &prism,
        (Bevel){ .radius = 0.1f, .segments = 4 });
    test_assert(bs_shape(world, e)->mesh != bs_shape(world, plain)->mesh);
    bs_within_bounds(world, e, plain, 2);
    test_assert(fabsf(bs_mesh(world, e)->min[1] - bs_mesh(world, plain)->min[1]) < 1e-4f);
    bs_unit_normals(bs_mesh(world, e));
    ecs_fini(world);
}

void BevelShapes_prism_rim(void) {
    ecs_world_t *world = bs_world();
    TrianglePrism prism = { 2, 1, 0.5f };
    ecs_entity_t e = bs_entity(world, ecs_id(TrianglePrism), &prism,
        (Bevel){ .radius = 0.1f, .segments = 4 });
    bs_prism_rim(world, e, false, (vec3){ 2, 1, 0.5f }, 0.1f);
    ecs_fini(world);
}

void BevelShapes_prism_uvs(void) {
    ecs_world_t *world = bs_world();
    TrianglePrism prism = { 2, 1, 0.5f };
    ecs_entity_t e = bs_entity(world, ecs_id(TrianglePrism), &prism,
        (Bevel){ .radius = 0.1f, .segments = 4 });
    const box_mesh_t *mesh = bs_mesh(world, e);
    int32_t caps = 0, bottom = 0;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        if (fabsf(v->normal[2]) > 0.99999f) {
            test_assert(fabsf(v->uv[0] - (v->position[0] + 0.5f)) < 1e-4f);
            test_assert(fabsf(v->uv[1] - (0.5f - v->position[1])) < 1e-4f);
            caps ++;
        } else if (v->normal[1] < -0.99999f) {
            test_assert(fabsf(v->uv[0] - (v->position[0] + 0.5f)) < 1e-4f);
            test_assert(fabsf(v->uv[1] - (0.5f - v->position[2])) < 1e-4f);
            bottom ++;
        }
    }

    test_assert(caps >= 6);
    test_assert(bottom >= 4);
    ecs_fini(world);
}

void BevelShapes_prism_clamp(void) {
    ecs_world_t *world = bs_world();
    TrianglePrism prism = { 1, 1, 2 };
    ecs_entity_t e = bs_entity(world, ecs_id(TrianglePrism), &prism,
        (Bevel){ .radius = 5, .segments = 4 });
    float side = hypotf(0.5f, 1.0f);
    float inradius = 1.0f / (1.0f + 2.0f * side);
    const ShapeMesh *sm = bs_shape(world, e);
    const box_mesh_t *mesh = bs_mesh(world, e);
    int32_t top = 0;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        vec3 pos, normal;
        bs_units(sm, &mesh->vertices[i], pos, normal);
        test_assert(pos[2] < 1.0f + 1e-4f);
        if (pos[2] > 1.0f - 1e-4f) {
            test_assert(fabsf(pos[0]) < 1e-3f);
            test_assert(fabsf(pos[1] - (-0.5f + inradius)) < 1e-3f);
            top ++;
        }
    }

    test_assert(top > 0);
    bs_unit_normals(mesh);
    ecs_fini(world);
}

void BevelShapes_right_prism_rim(void) {
    ecs_world_t *world = bs_world();
    RightTrianglePrism prism = { 1.5f, 1, 2 };
    ecs_entity_t plain = bs_plain(world, ecs_id(RightTrianglePrism), &prism);
    ecs_entity_t e = bs_entity(world, ecs_id(RightTrianglePrism), &prism,
        (Bevel){ .radius = 0.15f, .segments = 6 });
    bs_within_bounds(world, e, plain, 2);
    bs_prism_rim(world, e, true, (vec3){ 1.5f, 1, 2 }, 0.15f);
    bs_unit_normals(bs_mesh(world, e));
    ecs_fini(world);
}

/** Create a ProfileExtrusion with a Bevel.
 *
 * @param world The world
 * @param profile Name of the profile
 * @param depth The depth
 * @param smooth Smooth shading
 * @param bevel The bevel
 * @return The entity
 */
static ecs_entity_t bs_extrusion(
    ecs_world_t *world,
    const char *profile,
    float depth,
    bool smooth,
    Bevel bevel)
{
    ProfileExtrusion value = { .profile = ecs_lookup(world, profile), .depth = depth,
        .cap_start = true, .cap_end = true, .smooth = smooth };

    return bs_entity(world, ecs_id(ProfileExtrusion), &value, bevel);
}

void BevelShapes_extrusion_bounds(void) {
    ecs_world_t *world = bs_world();
    const char *profiles[2] = { "rect", "holed" };
    for (int32_t p = 0; p < 2; p ++) {
        ProfileExtrusion value = { .profile = ecs_lookup(world, profiles[p]), .depth = 1,
            .cap_start = true, .cap_end = true };
        ecs_entity_t plain = bs_plain(world, ecs_id(ProfileExtrusion), &value);
        ecs_entity_t e = bs_extrusion(world, profiles[p], 1, false,
            (Bevel){ .radius = 0.1f, .segments = 4 });
        test_assert(bs_shape(world, e)->mesh != bs_shape(world, plain)->mesh);
        bs_same_bounds(world, e, plain);
        bs_unit_normals(bs_mesh(world, e));
        const ShapeMesh *a = bs_shape(world, e), *b = bs_shape(world, plain);
        for (int32_t k = 0; k < 3; k ++) {
            test_flt(a->min[k], b->min[k]);
            test_flt(a->max[k], b->max[k]);
        }
    }

    ecs_fini(world);
}

/** Check the rim of a beveled 2 by 1 rectangle extrusion against its +x wall: every vertex
 * beyond the start of the rim lies on its circle, with the normal pointing away from its center.
 *
 * @param world The world
 * @param e The entity
 * @param cy Height of the center of the rim
 * @param up 1 for the top rim, -1 for the bottom rim
 * @param radius The radius
 * @return The number of vertices checked
 */
static int32_t bs_extrusion_rim(
    ecs_world_t *world,
    ecs_entity_t e,
    float cy,
    float up,
    float radius)
{
    const ShapeMesh *sm = bs_shape(world, e);
    const box_mesh_t *mesh = bs_mesh(world, e);
    int32_t result = 0;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        vec3 pos, normal;
        bs_units(sm, &mesh->vertices[i], pos, normal);
        float cx = 1.0f - radius;
        if (pos[0] < cx - 1e-4f || up * (pos[1] - cy) < -1e-4f) {
            continue;
        }

        float dx = pos[0] - cx, dy = pos[1] - cy;
        float distance = hypotf(dx, dy);
        test_assert(fabsf(distance - radius) < 2e-3f);
        if (fabsf(pos[2]) < 0.5f - radius - 1e-3f && distance > 0.0f) {
            test_assert((normal[0] * dx + normal[1] * dy) / distance > 0.998f);
        }

        result ++;
    }

    return result;
}

void BevelShapes_extrusion_rim(void) {
    ecs_world_t *world = bs_world();
    ecs_entity_t e = bs_extrusion(world, "rect", 1, false,
        (Bevel){ .top = 0.2f, .bottom = 0.1f, .segments = 4 });
    test_assert(bs_extrusion_rim(world, e, 0.3f, 1, 0.2f) >= 4 * 2);
    test_assert(bs_extrusion_rim(world, e, -0.4f, -1, 0.1f) >= 4 * 2);
    ecs_fini(world);
}

void BevelShapes_extrusion_holes(void) {
    ecs_world_t *world = bs_world();
    ecs_entity_t e = bs_extrusion(world, "holed", 1, false,
        (Bevel){ .top = 0.1f, .segments = 4 });
    const ShapeMesh *sm = bs_shape(world, e);
    const box_mesh_t *mesh = bs_mesh(world, e);
    int32_t hole = 0;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        vec3 pos, normal;
        bs_units(sm, v, pos, normal);
        if (v->uv[0] >= 2.0f && pos[1] > 0.4f - 1e-4f && pos[0] > 0.3f - 1e-4f) {
            float dx = pos[0] - 0.4f, dy = pos[1] - 0.4f;
            test_assert(fabsf(hypotf(dx, dy) - 0.1f) < 2e-3f);
            hole ++;
        }
    }

    test_assert(hole >= 2 * 4);
    ecs_fini(world);
}

void BevelShapes_extrusion_top_only(void) {
    ecs_world_t *world = bs_world();
    ecs_entity_t e = bs_extrusion(world, "rect", 1, true,
        (Bevel){ .top = 0.2f, .segments = 4 });
    const ShapeMesh *sm = bs_shape(world, e);
    const box_mesh_t *mesh = bs_mesh(world, e);
    float top = 0.0f, bottom = 0.0f;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        vec3 pos, normal;
        bs_units(sm, &mesh->vertices[i], pos, normal);
        if (fabsf(pos[1] - 0.5f) < 1e-4f) {
            top = fmaxf(top, pos[0]);
        } else if (fabsf(pos[1] + 0.5f) < 1e-4f) {
            bottom = fmaxf(bottom, pos[0]);
        }
    }

    test_assert(fabsf(top - 0.8f) < 1e-4f);
    test_assert(fabsf(bottom - 1.0f) < 1e-4f);
    test_assert(bs_extrusion_rim(world, e, 0.3f, 1, 0.2f) > 0);
    ecs_fini(world);
}

void BevelShapes_extrusion_regions(void) {
    ecs_world_t *world = bs_world();
    ecs_entity_t e = bs_extrusion(world, "holed", 1, true,
        (Bevel){ .radius = 0.1f, .segments = 4 });
    const ShapeMesh *sm = bs_shape(world, e);
    const box_mesh_t *mesh = bs_mesh(world, e);
    int32_t counts[4] = {0}, rims = 0;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        vec3 pos, normal;
        bs_units(sm, v, pos, normal);
        int32_t region = box_profile_region(v);
        bool cap = fabsf(v->normal[0]) == 0.0f && fabsf(v->normal[2]) == 0.0f;
        if (cap) {
            test_int(region, v->normal[1] > 0.0f ? SurfaceEnd : SurfaceStart);
        } else {
            test_int(region, v->uv[0] >= 2.0f ? SurfaceInner : SurfaceOuter);
            rims += fabsf(v->normal[1]) > 1e-3f;
        }

        counts[region] ++;
    }

    for (int32_t r = 0; r < 4; r ++) {
        test_assert(counts[r] > 0);
    }

    test_assert(rims > 0);
    ecs_fini(world);
}

void BevelShapes_extrusion_path_change(void) {
    ecs_world_t *world = bs_world();
    ecs_entity_t e = bs_extrusion(world, "rect", 1, false,
        (Bevel){ .radius = 0.1f, .segments = 4 });
    uint32_t mesh = bs_shape(world, e)->mesh;
    test_assert(fabsf(bs_mesh(world, e)->max[0] - 1.0f) < 1e-4f);
    Profile2D wide = { .outer = ecs_lookup(world, "wide_outline") };
    ecs_set_ptr(world, ecs_lookup(world, "rect"), Profile2D, &wide);
    test_assert(bs_shape(world, e)->mesh != mesh);
    test_assert(fabsf(bs_mesh(world, e)->max[0] - 1.5f) < 1e-4f);

    const Path2D *path = ecs_get(world, ecs_lookup(world, "wide_outline"), Path2D);
    test_assert(path != NULL);
    mesh = bs_shape(world, e)->mesh;
    Path2D changed = {0};
    changed.start[0] = -1.5f;
    changed.start[1] = -0.5f;
    changed.closed = true;
    float ends[3][2] = { { 1, -0.5f }, { 1, 0.5f }, { -1.5f, 0.5f } };
    for (int32_t i = 0; i < 3; i ++) {
        PathSegment2D *segment = ecs_vec_append_t(NULL, &changed.segments, PathSegment2D);
        *segment = (PathSegment2D){ .kind = PathLine };
        segment->end[0] = ends[i][0];
        segment->end[1] = ends[i][1];
    }

    ecs_set_ptr(world, ecs_lookup(world, "wide_outline"), Path2D, &changed);
    ecs_vec_fini_t(NULL, &changed.segments, PathSegment2D);
    test_assert(bs_shape(world, e)->mesh != mesh);
    test_assert(bs_extrusion_rim(world, e, 0.4f, 1, 0.1f) > 0);
    ecs_fini(world);
}

void BevelShapes_clamp_wall(void) {
    ecs_world_t *world = bs_world();
    Cylinder cylinder = { .segments = 16, .smooth = true, .length = 0.4f, .radius = 1 };
    ecs_entity_t e = bs_entity(world, ecs_id(Cylinder), &cylinder,
        (Bevel){ .top = 0.3f, .bottom = 0.3f, .segments = 4 });
    test_assert(bs_round_rim(world, e, 0.8f, 0.0f, 0.2f, 1, 1, true) > 16);
    test_assert(bs_round_rim(world, e, 0.8f, 0.0f, 0.2f, 1, -1, true) > 16);
    test_assert(fabsf(bs_reach(world, e, 0.0f) - 1.0f) < 1e-3f);

    ecs_entity_t extrusion = bs_extrusion(world, "rect", 0.4f, false,
        (Bevel){ .top = 0.3f, .bottom = 0.3f, .segments = 4 });
    test_assert(bs_extrusion_rim(world, extrusion, 0.0f, 1, 0.2f) > 0);
    test_assert(bs_extrusion_rim(world, extrusion, 0.0f, -1, 0.2f) > 0);
    ecs_fini(world);
}

void BevelShapes_cache_shared(void) {
    ecs_world_t *world = bs_world();
    Cylinder cylinder = { .segments = 24, .smooth = true, .length = 2, .radius = 0.5f };
    Cylinder twice = { .segments = 24, .smooth = true, .length = 4, .radius = 1 };
    Cylinder taller = { .segments = 24, .smooth = true, .length = 3, .radius = 0.5f };
    Bevel bevel = { .top = 0.1f, .segments = 4 };
    Bevel doubled = { .top = 0.2f, .segments = 4 };
    ecs_entity_t a = bs_entity(world, ecs_id(Cylinder), &cylinder, bevel);
    ecs_entity_t b = bs_entity(world, ecs_id(Cylinder), &cylinder, bevel);
    ecs_entity_t c = bs_entity(world, ecs_id(Cylinder), &twice, doubled);
    ecs_entity_t near = bs_entity(world, ecs_id(Cylinder), &cylinder,
        (Bevel){ .top = 0.1f + 1e-6f, .segments = 4 });
    uint32_t mesh = bs_shape(world, a)->mesh;
    test_uint(bs_shape(world, b)->mesh, mesh);
    test_uint(bs_shape(world, c)->mesh, mesh);
    test_flt(bs_shape(world, c)->scale[0], 1.0f);
    test_uint(bs_shape(world, near)->mesh, mesh);

    ecs_entity_t radius = bs_entity(world, ecs_id(Cylinder), &cylinder,
        (Bevel){ .top = 0.15f, .segments = 4 });
    ecs_entity_t segments = bs_entity(world, ecs_id(Cylinder), &cylinder,
        (Bevel){ .top = 0.1f, .segments = 6 });
    ecs_entity_t proportion = bs_entity(world, ecs_id(Cylinder), &taller, bevel);
    ecs_entity_t bottom = bs_entity(world, ecs_id(Cylinder), &cylinder,
        (Bevel){ .bottom = 0.1f, .segments = 4 });
    test_assert(bs_shape(world, radius)->mesh != mesh);
    test_assert(bs_shape(world, segments)->mesh != mesh);
    test_assert(bs_shape(world, proportion)->mesh != mesh);
    test_assert(bs_shape(world, bottom)->mesh != mesh);

    TrianglePrism prism = { 1, 1, 1 }, scaled = { 2, 2, 2 }, flat = { 1, 0.5f, 1 };
    ecs_entity_t pa = bs_entity(world, ecs_id(TrianglePrism), &prism,
        (Bevel){ .radius = 0.1f, .segments = 4 });
    ecs_entity_t pb = bs_entity(world, ecs_id(TrianglePrism), &scaled,
        (Bevel){ .radius = 0.2f, .segments = 4 });
    ecs_entity_t pc = bs_entity(world, ecs_id(TrianglePrism), &flat,
        (Bevel){ .radius = 0.1f, .segments = 4 });
    test_uint(bs_shape(world, pa)->mesh, bs_shape(world, pb)->mesh);
    test_assert(bs_shape(world, pc)->mesh != bs_shape(world, pa)->mesh);

    ecs_entity_t ea = bs_extrusion(world, "rect", 1, false, (Bevel){ .top = 0.1f, .segments = 4 });
    ecs_entity_t eb = bs_extrusion(world, "rect", 1, false, (Bevel){ .top = 0.1f, .segments = 4 });
    ecs_entity_t ec = bs_extrusion(world, "rect", 1, false, (Bevel){ .top = 0.2f, .segments = 4 });
    ecs_entity_t ed = bs_extrusion(world, "rect", 1, false, (Bevel){ .top = 0.1f, .segments = 2 });
    test_uint(bs_shape(world, ea)->mesh, bs_shape(world, eb)->mesh);
    test_assert(bs_shape(world, ec)->mesh != bs_shape(world, ea)->mesh);
    test_assert(bs_shape(world, ed)->mesh != bs_shape(world, ea)->mesh);
    ecs_fini(world);
}

void BevelShapes_cache_cylinder_frustum(void) {
    ecs_world_t *world = bs_world();
    Cylinder cylinder = { .segments = 24, .smooth = true, .length = 2, .radius = 0.5f };
    Frustum frustum = { .segments = 24, .smooth = true, .length = 2,
        .radius_bottom = 0.5f, .radius_top = 0.5f };
    Bevel bevel = { .radius = 0.1f, .segments = 4 };
    ecs_entity_t a = bs_entity(world, ecs_id(Cylinder), &cylinder, bevel);
    ecs_entity_t b = bs_entity(world, ecs_id(Frustum), &frustum, bevel);
    test_assert(bs_shape(world, a)->mesh != bs_shape(world, b)->mesh);
    const box_mesh_t *ma = bs_mesh(world, a), *mb = bs_mesh(world, b);
    test_int(ma->vertex_count, mb->vertex_count);
    for (int32_t k = 0; k < 3; k ++) {
        test_flt(bs_shape(world, a)->scale[k], bs_shape(world, b)->scale[k]);
    }

    ecs_fini(world);
}

/** Host shape of the resolve tests */
typedef struct bs_host_t {
    ecs_entity_t id;    /**< The host component */
    const void *value;  /**< The host value */
    const void *other;  /**< Another host value that bevels differently */
} bs_host_t;

/** Values of the hosts of the resolve tests */
typedef struct bs_values_t {
    Cylinder cylinder[2];          /**< Cylinders */
    Frustum frustum[2];            /**< Frustums */
    Cone cone[2];                  /**< Cones */
    Tube tube[2];                  /**< Full tubes */
    Tube partial[2];               /**< Partial tubes */
    TrianglePrism prism[2];        /**< Prisms */
    RightTrianglePrism right[2];   /**< Right prisms */
    ProfileExtrusion extrusion[2]; /**< Extrusions of a rectangle */
    ProfileExtrusion holed[2];     /**< Extrusions of a rectangle with a hole */
} bs_values_t;

/** Fill the hosts of the resolve tests.
 *
 * @param world The world
 * @param values Storage of the host values
 * @param hosts The hosts to fill
 * @return The number of hosts
 */
static int32_t bs_hosts(
    ecs_world_t *world,
    bs_values_t *values,
    bs_host_t *hosts)
{
    ecs_entity_t rect = ecs_lookup(world, "rect"), holed = ecs_lookup(world, "holed");
    *values = (bs_values_t){
        .cylinder = { { 24, true, 2, 0.5f }, { 24, true, 3, 0.5f } },
        .frustum = { { 24, true, 2, 1, 0.6f }, { 24, true, 2, 1, 0.4f } },
        .cone = { { 24, true, 2, 0.5f }, { 24, true, 1, 0.5f } },
        .tube = { { 24, true, 2, 1, 0.4f, 0 }, { 24, true, 2, 1, 0.3f, 0 } },
        .partial = { { 24, true, 2, 1, 0.4f, BOX_PI }, { 24, true, 2, 1, 0.4f, 2 } },
        .prism = { { 2, 1, 0.5f }, { 1, 1, 0.5f } },
        .right = { { 2, 1, 0.5f }, { 1, 1, 0.5f } },
        .extrusion = { { rect, 1, true, true, false }, { rect, 2, true, true, false } },
        .holed = { { holed, 1, true, true, true }, { holed, 0.5f, true, true, true } }
    };
    hosts[0] = (bs_host_t){ ecs_id(Cylinder), &values->cylinder[0], &values->cylinder[1] };
    hosts[1] = (bs_host_t){ ecs_id(Frustum), &values->frustum[0], &values->frustum[1] };
    hosts[2] = (bs_host_t){ ecs_id(Cone), &values->cone[0], &values->cone[1] };
    hosts[3] = (bs_host_t){ ecs_id(Tube), &values->tube[0], &values->tube[1] };
    hosts[4] = (bs_host_t){ ecs_id(Tube), &values->partial[0], &values->partial[1] };
    hosts[5] = (bs_host_t){ ecs_id(TrianglePrism), &values->prism[0], &values->prism[1] };
    hosts[6] = (bs_host_t){ ecs_id(RightTrianglePrism), &values->right[0], &values->right[1] };
    hosts[7] = (bs_host_t){
        ecs_id(ProfileExtrusion), &values->extrusion[0], &values->extrusion[1] };
    hosts[8] = (bs_host_t){ ecs_id(ProfileExtrusion), &values->holed[0], &values->holed[1] };

    return 9;
}

/** Set a host value on an entity.
 *
 * @param world The world or stage
 * @param e The entity
 * @param host The host
 * @param value The value
 */
static void bs_set_host(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t host,
    const void *value)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, host);
    ecs_set_id(world, e, host, (size_t)ti->size, value);
}

/** Assert that two entities resolve to the same mesh and scale.
 *
 * @param world The world
 * @param a The first entity
 * @param b The second entity
 */
static void bs_same_shape(
    ecs_world_t *world,
    ecs_entity_t a,
    ecs_entity_t b)
{
    const ShapeMesh *sa = bs_shape(world, a), *sb = bs_shape(world, b);
    test_uint(sa->mesh, sb->mesh);
    for (int32_t k = 0; k < 3; k ++) {
        test_flt(sa->scale[k], sb->scale[k]);
    }
}

void BevelShapes_resolve_order(void) {
    ecs_world_t *world = bs_world();
    bs_values_t values;
    bs_host_t hosts[9];
    int32_t count = bs_hosts(world, &values, hosts);
    Bevel bevel = { .radius = 0.08f, .segments = 4 };
    for (int32_t h = 0; h < count; h ++) {
        ecs_entity_t host_first = ecs_new(world);
        bs_set_host(world, host_first, hosts[h].id, hosts[h].value);
        ecs_set_ptr(world, host_first, Bevel, &bevel);

        ecs_entity_t bevel_first = ecs_new(world);
        ecs_set_ptr(world, bevel_first, Bevel, &bevel);
        test_assert(!ecs_has(world, bevel_first, ShapeMesh));
        bs_set_host(world, bevel_first, hosts[h].id, hosts[h].value);

        ecs_entity_t deferred = ecs_new(world);
        ecs_entity_t deferred_bevel_first = ecs_new(world);
        ecs_readonly_begin(world, false);
        ecs_world_t *stage = ecs_get_stage(world, 0);
        bs_set_host(stage, deferred, hosts[h].id, hosts[h].value);
        ecs_set_ptr(stage, deferred, Bevel, &bevel);
        ecs_set_ptr(stage, deferred_bevel_first, Bevel, &bevel);
        bs_set_host(stage, deferred_bevel_first, hosts[h].id, hosts[h].value);
        ecs_readonly_end(world);

        ecs_entity_t plain = bs_plain(world, hosts[h].id, hosts[h].value);
        test_assert(bs_shape(world, host_first)->mesh != bs_shape(world, plain)->mesh);
        bs_same_shape(world, bevel_first, host_first);
        bs_same_shape(world, deferred, host_first);
        bs_same_shape(world, deferred_bevel_first, host_first);
    }

    ecs_fini(world);
}

void BevelShapes_resolve_flat(void) {
    ecs_world_t *world = bs_world();
    bs_values_t values;
    bs_host_t hosts[9];
    int32_t count = bs_hosts(world, &values, hosts);
    for (int32_t h = 0; h < count; h ++) {
        ecs_entity_t plain = bs_plain(world, hosts[h].id, hosts[h].value);
        ecs_entity_t flat = bs_entity(world, hosts[h].id, hosts[h].value,
            (Bevel){ .segments = 4 });
        ecs_entity_t negative = bs_entity(world, hosts[h].id, hosts[h].value,
            (Bevel){ .radius = -1, .segments = 4 });
        ecs_entity_t edges = bs_entity(world, hosts[h].id, hosts[h].value,
            (Bevel){ .top_front = 0.1f, .front_left = 0.1f, .segments = 4 });
        bs_same_shape(world, flat, plain);
        bs_same_shape(world, negative, plain);
        bs_same_shape(world, edges, plain);
    }

    TrianglePrism prism = { 2, 1, 0.5f };
    ecs_entity_t plain = bs_plain(world, ecs_id(TrianglePrism), &prism);
    ecs_entity_t rims = bs_entity(world, ecs_id(TrianglePrism), &prism,
        (Bevel){ .top = 0.1f, .bottom = 0.1f, .segments = 4 });
    bs_same_shape(world, rims, plain);
    ecs_fini(world);
}

void BevelShapes_resolve_remove_bevel(void) {
    ecs_world_t *world = bs_world();
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    bs_values_t values;
    bs_host_t hosts[9];
    int32_t count = bs_hosts(world, &values, hosts);
    for (int32_t h = 0; h < count; h ++) {
        ecs_entity_t plain = bs_plain(world, hosts[h].id, hosts[h].value);
        ecs_entity_t e = bs_entity(world, hosts[h].id, hosts[h].value,
            (Bevel){ .radius = 0.08f, .segments = 4 });
        uint32_t beveled = bs_shape(world, e)->mesh;
        test_assert(beveled != bs_shape(world, plain)->mesh);
        ecs_remove(world, e, Bevel);
        bs_same_shape(world, e, plain);
        test_assert(box_mesh_cache_get(cache, beveled) == NULL);
    }

    ecs_fini(world);
}

void BevelShapes_resolve_remove_host(void) {
    ecs_world_t *world = bs_world();
    bs_values_t values;
    bs_host_t hosts[9];
    int32_t count = bs_hosts(world, &values, hosts);
    Bevel bevel = { .radius = 0.08f, .segments = 4 };
    for (int32_t h = 0; h < count; h ++) {
        ecs_entity_t reference = bs_entity(world, hosts[h].id, hosts[h].value, bevel);
        ecs_entity_t e = bs_entity(world, hosts[h].id, hosts[h].value, bevel);
        bs_same_shape(world, e, reference);
        ecs_remove_id(world, e, hosts[h].id);
        test_assert(!ecs_has(world, e, ShapeMesh));
        test_assert(ecs_has(world, e, Bevel));
        bs_set_host(world, e, hosts[h].id, hosts[h].value);
        bs_same_shape(world, e, reference);
    }

    ecs_fini(world);
}

void BevelShapes_resolve_delete(void) {
    ecs_world_t *world = bs_world();
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    bs_values_t values;
    bs_host_t hosts[9];
    int32_t count = bs_hosts(world, &values, hosts);
    for (int32_t h = 0; h < count; h ++) {
        int32_t before = box_mesh_cache_count(cache);
        ecs_entity_t e = bs_entity(world, hosts[h].id, hosts[h].value,
            (Bevel){ .radius = 0.08f, .segments = 4 });
        uint32_t mesh = bs_shape(world, e)->mesh;
        test_int(box_mesh_cache_users(cache, mesh), 1);
        test_int(box_mesh_cache_count(cache), before + 1);
        ecs_delete(world, e);
        test_assert(box_mesh_cache_get(cache, mesh) == NULL);
        test_int(box_mesh_cache_count(cache), before);
    }

    ecs_fini(world);
}

void BevelShapes_resolve_prefab(void) {
    ecs_world_t *world = bs_world();
    bs_values_t values;
    bs_host_t hosts[9];
    int32_t count = bs_hosts(world, &values, hosts);
    Bevel bevel = { .radius = 0.08f, .segments = 4 };
    Bevel other = { .radius = 0.12f, .segments = 4 };
    for (int32_t h = 0; h < count; h ++) {
        ecs_entity_t reference = bs_entity(world, hosts[h].id, hosts[h].value, bevel);
        ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
        bs_set_host(world, prefab, hosts[h].id, hosts[h].value);
        ecs_set_ptr(world, prefab, Bevel, &bevel);
        ecs_entity_t inherited = ecs_new_w_pair(world, EcsIsA, prefab);
        bs_same_shape(world, inherited, reference);

        ecs_entity_t bevel_override = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_entity_t host_override = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set_ptr(world, bevel_override, Bevel, &other);
        bs_set_host(world, host_override, hosts[h].id, hosts[h].other);
        ecs_entity_t bevel_reference = bs_entity(world, hosts[h].id, hosts[h].value, other);
        ecs_entity_t host_reference = bs_entity(world, hosts[h].id, hosts[h].other, bevel);
        bs_same_shape(world, bevel_override, bevel_reference);
        bs_same_shape(world, host_override, host_reference);
        test_assert(bs_shape(world, bevel_override)->mesh != bs_shape(world, inherited)->mesh);
        test_assert(bs_shape(world, host_override)->mesh != bs_shape(world, inherited)->mesh);
    }

    ecs_fini(world);
}

void BevelShapes_box_top_bottom(void) {
    ecs_world_t *world = bs_world();
    Box box = { 2, 1, 1.5f };
    ecs_entity_t rims = bs_entity(world, ecs_id(Box), &box,
        (Bevel){ .top = 0.2f, .bottom = 0.1f, .segments = 4 });
    ecs_entity_t named = bs_entity(world, ecs_id(Box), &box,
        (Bevel){ .top_front = 0.2f, .top_back = 0.2f, .top_left = 0.2f, .top_right = 0.2f,
            .bottom_front = 0.1f, .bottom_back = 0.1f, .bottom_left = 0.1f,
            .bottom_right = 0.1f, .segments = 4 });
    bs_same_shape(world, rims, named);

    ecs_entity_t plain = bs_plain(world, ecs_id(Box), &box);
    test_assert(bs_shape(world, rims)->mesh != bs_shape(world, plain)->mesh);
    ecs_entity_t top = bs_entity(world, ecs_id(Box), &box,
        (Bevel){ .top = 0.2f, .segments = 4 });
    ecs_entity_t top_named = bs_entity(world, ecs_id(Box), &box,
        (Bevel){ .top_front = 0.2f, .top_back = 0.2f, .top_left = 0.2f, .top_right = 0.2f,
            .segments = 4 });
    bs_same_shape(world, top, top_named);
    ecs_fini(world);
}

void BevelShapes_box_edge_wins(void) {
    ecs_world_t *world = bs_world();
    Box box = { 2, 1, 1.5f };
    ecs_entity_t mixed = bs_entity(world, ecs_id(Box), &box,
        (Bevel){ .top = 0.2f, .top_front = 0.3f, .bottom_left = 0.05f, .segments = 4 });
    ecs_entity_t named = bs_entity(world, ecs_id(Box), &box,
        (Bevel){ .top_front = 0.3f, .top_back = 0.2f, .top_left = 0.2f, .top_right = 0.2f,
            .bottom_left = 0.05f, .segments = 4 });
    bs_same_shape(world, mixed, named);

    ecs_entity_t uniform = bs_entity(world, ecs_id(Box), &box,
        (Bevel){ .radius = 0.1f, .top = 0.3f, .segments = 4 });
    ecs_entity_t radius = bs_entity(world, ecs_id(Box), &box,
        (Bevel){ .radius = 0.1f, .segments = 4 });
    bs_same_shape(world, uniform, radius);
    ecs_fini(world);
}

void BevelShapes_no_effect(void) {
    ecs_world_t *world = bs_world();
    Sphere sphere = { .segments = 12, .smooth = true, .radius = 1 };
    HemiSphere hemisphere = { .segments = 12, .smooth = true, .radius = 1 };
    IcoSphere icosphere = { .segments = 2, .smooth = true, .radius = 1 };
    Capsule capsule = { .segments = 12, .smooth = true, .length = 1, .radius = 0.5f };
    Torus torus = { .segments = 12, .rings = 8, .smooth = true, .radius = 1, .tube = 0.2f };
    Quad quad = { 1, 2 };
    Triangle triangle = { 1, 2 };
    RightTriangle right = { 1, 2 };
    NGon ngon = { .sides = 6, .radius = 1 };
    RoundedBox rounded = { .x = 1, .y = 2, .z = 3, .radius = 0.2f, .segments = 4 };
    struct { ecs_entity_t id; const void *value; } shapes[] = {
        { ecs_id(Sphere), &sphere }, { ecs_id(HemiSphere), &hemisphere },
        { ecs_id(IcoSphere), &icosphere }, { ecs_id(Capsule), &capsule },
        { ecs_id(Torus), &torus }, { ecs_id(Quad), &quad }, { ecs_id(Triangle), &triangle },
        { ecs_id(RightTriangle), &right }, { ecs_id(NGon), &ngon },
        { ecs_id(RoundedBox), &rounded }
    };
    for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i ++) {
        ecs_entity_t plain = bs_plain(world, shapes[i].id, shapes[i].value);
        ecs_entity_t e = bs_entity(world, shapes[i].id, shapes[i].value,
            (Bevel){ .radius = 0.1f, .top = 0.1f, .bottom = 0.1f, .segments = 4 });
        bs_same_shape(world, e, plain);
    }

    ecs_fini(world);
}

void BevelShapes_lod_keys(void) {
    // renderer/meshes
    ecs_world_t *world = bs_world();
    Cylinder cylinder = { .segments = 24, .smooth = true, .length = 2, .radius = 1 };
    Frustum frustum = { .segments = 24, .smooth = true, .length = 2, .radius_bottom = 1,
        .radius_top = 0.5f };
    Tube tube = { .segments = 24, .smooth = true, .length = 2, .radius = 1, .thickness = 0.3f };
    Bevel bevel = { .radius = 0.1f, .segments = 8 };
    ecs_entity_t round[4] = {
        bs_entity(world, ecs_id(Cylinder), &cylinder, bevel),
        bs_entity(world, ecs_id(Cone), &cylinder, bevel),
        bs_entity(world, ecs_id(Frustum), &frustum, bevel),
        bs_entity(world, ecs_id(Tube), &tube, bevel)
    };

    /* The segments around the axis and the steps per rim, above the marker, halve */
    for (int32_t i = 0; i < 4; i ++) {
        ecs_entity_t e = round[i];
        test_int(lod_key(world, e, 0)->i[1] >> 8, 8);
        test_int(lod_key(world, e, 1)->i[0], 12);
        test_int(lod_key(world, e, 1)->i[1] >> 8, 4);
        test_int(lod_key(world, e, 2)->i[0], 6);
        test_int(lod_key(world, e, 2)->i[1] >> 8, 2);
        test_int(lod_key(world, e, 3)->i[0], 3);
        test_int(lod_key(world, e, 3)->i[1] >> 8, 2);
        lod_test_fewer_indices(world, e, 1);
        lod_test_fewer_indices(world, e, 3);
    }

    /* A faceted host keeps its segments, its rims still drop steps */
    cylinder.smooth = false;
    ecs_entity_t faceted = bs_entity(world, ecs_id(Cylinder), &cylinder, bevel);
    test_int(lod_key(world, faceted, 1)->i[0], 24);
    test_int(lod_key(world, faceted, 1)->i[1] >> 8, 4);
    test_int(lod_key(world, faceted, 3)->i[0], 24);

    TrianglePrism prism = { 1, 1, 2 };
    ecs_entity_t prisms[2] = {
        bs_entity(world, ecs_id(TrianglePrism), &prism, bevel),
        bs_entity(world, ecs_id(RightTrianglePrism), &prism, bevel)
    };
    for (int32_t i = 0; i < 2; i ++) {
        test_int(lod_key(world, prisms[i], 1)->i[0], 4);
        test_int(lod_key(world, prisms[i], 2)->i[0], 2);
        test_uint(lod_mesh(world, prisms[i], 3), lod_mesh(world, prisms[i], 2));
        lod_test_fewer_indices(world, prisms[i], 1);
    }

    /* The steps per rim of an extrusion are in i[0], at least 2 */
    ecs_entity_t extrusion = bs_extrusion(world, "rect", 1, false, bevel);
    test_int(lod_key(world, extrusion, 0)->i[0], 8);
    test_int(lod_key(world, extrusion, 1)->i[0], 4);
    test_int(lod_key(world, extrusion, 2)->i[0], 2);
    test_uint(lod_mesh(world, extrusion, 3), lod_mesh(world, extrusion, 2));
    lod_test_fewer_indices(world, extrusion, 1);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const box_mesh_t *base = box_mesh_cache_get(cache, lod_mesh(world, extrusion, 0));
    const box_mesh_t *variant = box_mesh_cache_get(cache, lod_mesh(world, extrusion, 1));
    test_assert(variant->surface_assets == NULL || variant->surface_assets != base->surface_assets);

    /* A plain ProfileExtrusion has no segments and no levels */
    ProfileExtrusion plain = { .profile = ecs_lookup(world, "rect"), .depth = 1,
        .cap_start = true, .cap_end = true };
    ecs_entity_t flat = bs_plain(world, ecs_id(ProfileExtrusion), &plain);
    test_uint(lod_mesh(world, flat, 3), lod_mesh(world, flat, 0));
    ecs_fini(world);
}
