/**
 * @file MeshNodes.c
 * @brief Tests for the node kinds of box.mesh, evaluated on the calling thread: counts, bounds,
 * attributes and determinism.
 */

#include "mesh_test.h"

/** Create a prefab with a shape and a Shape node of it.
 *
 * @param world The world
 * @param name Name of the node
 * @param component The shape component
 * @param value The shape value
 * @return The node
 */
static ecs_entity_t shape_node(
    ecs_world_t *world,
    const char *name,
    ecs_entity_t component,
    const void *value)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, component);
    ecs_entity_t source = ecs_new_w_id(world, EcsPrefab);
    ecs_set_id(world, source, component, (size_t)ti->size, value);
    ecs_entity_t node = test_named(world, name);
    ecs_set(world, node, MeshShape, { .entity = source });

    return node;
}

/** Create a Shape node of a box.
 *
 * @param world The world
 * @param x Width
 * @param y Height
 * @param z Depth
 * @return The node
 */
static ecs_entity_t box_node(
    ecs_world_t *world,
    float x,
    float y,
    float z)
{
    Box box = { x, y, z };

    return shape_node(world, "box", ecs_id(Box), &box);
}

/** Create a Shape node of an IcoSphere.
 *
 * @param world The world
 * @param name Name of the node
 * @param smooth Smooth shading
 * @return The node
 */
static ecs_entity_t sphere_node(
    ecs_world_t *world,
    const char *name,
    bool smooth)
{
    IcoSphere sphere = { .segments = 2, .smooth = smooth, .radius = 0.5f };

    return shape_node(world, name, ecs_id(IcoSphere), &sphere);
}

/** Create a node.
 *
 * @param world The world
 * @param name The name
 * @param component The node component
 * @param value The node value
 * @return The node
 */
static ecs_entity_t node_of(
    ecs_world_t *world,
    const char *name,
    ecs_entity_t component,
    const void *value)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, component);
    ecs_entity_t node = test_named(world, name);
    ecs_set_id(world, node, component, (size_t)ti->size, value);

    return node;
}

/** Turn +Y by the rotation of a point.
 *
 * @param p The points
 * @param i The point
 * @param out Receives the turned +Y
 */
static void point_up(
    const box_mg_points_t *p,
    int32_t i,
    vec3 out)
{
    glm_quat_rotatev(p->rotations[i], (vec3){0, 1, 0}, out);
}

/** Check that two vec3 are within a tolerance.
 *
 * @param a The value
 * @param b The expected value
 * @param eps The tolerance
 */
static void near3(
    const vec3 a,
    const vec3 b,
    float eps)
{
    test_near(a[0], b[0], eps);
    test_near(a[1], b[1], eps);
    test_near(a[2], b[2], eps);
}

void MeshNodes_shape_box(void) {
    ecs_world_t *world = test_world();
    box_mg_value_t *v = test_eval(world, box_node(world, 2, 1, 1));
    vec3 min, max;
    int32_t i;
    test_int(v->type, MeshValueMesh);
    test_assert(v->mesh.vertex_count > 0);
    test_assert(v->mesh.index_count > 0);
    test_int(v->mesh.index_count % 3, 0);
    test_bounds(v->mesh.positions, v->mesh.vertex_count, min, max);
    near3(min, (vec3){-1, -0.5f, -0.5f}, 1e-4f);
    near3(max, (vec3){1, 0.5f, 0.5f}, 1e-4f);
    for (i = 0; i < v->mesh.vertex_count; i ++) {
        test_near(glm_vec3_norm(v->mesh.normals[i]), 1, 1e-4f);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_shape_sphere(void) {
    ecs_world_t *world = test_world();
    box_mg_value_t *v = test_eval(world, sphere_node(world, "ball", true));
    int32_t i;
    for (i = 0; i < v->mesh.vertex_count; i ++) {
        vec3 dir;
        test_near(glm_vec3_norm(v->mesh.positions[i]), 0.5f, 1e-3f);
        glm_vec3_normalize_to(v->mesh.positions[i], dir);
        test_assert(glm_vec3_dot(dir, v->mesh.normals[i]) > 0.99f);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_shape_lathe(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t profile = test_named(world, "profile");
    PathSegment2D segment = { .kind = PathLine, .end = { 0.5f, 2 } };
    ecs_set(world, profile, Path2D, { .start = { 0.5f, 0 },
        .segments = BOX_VEC_VIEW(PathSegment2D, segment) });
    Lathe lathe = { .profile = profile, .segments = 16, .rings = 4 };
    box_mg_value_t *v = test_eval(world, shape_node(world, "lathe", ecs_id(Lathe), &lathe));
    vec3 min, max;
    test_assert(v->mesh.vertex_count > 0);
    test_bounds(v->mesh.positions, v->mesh.vertex_count, min, max);
    test_near(min[1], 0, 1e-3f);
    test_near(max[1], 2, 1e-3f);
    test_near(max[0], 0.5f, 0.02f);
    test_near(min[2], -0.5f, 0.02f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_shape_missing(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t plain = test_named(world, "plain");
    ecs_entity_t node = test_named(world, "node");
    ecs_set(world, node, MeshShape, { .entity = plain });
    char *error = NULL;
    test_assert(box_mesh_graph_evaluate(world, node, &error) == NULL);
    test_assert(error != NULL);
    test_assert(strstr(error, "'node'") != NULL);
    test_assert(strstr(error, "has no shape") != NULL);
    ecs_os_free(error);

    ecs_fini(world);
}

void MeshNodes_mesh3(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t e = test_named(world, "quad");
    ecs_set(world, e, Mesh3, {
        .vertices = BOX_VEC_VIEW(vec3, {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}),
        .normals = BOX_VEC_VIEW(vec3, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}),
        .uvs = BOX_VEC_VIEW(vec2, {0, 0}, {1, 0}, {1, 1}, {0, 1}),
        .indices = BOX_VEC_VIEW(uint32_t, 0, 1, 2, 0, 2, 3)
    });
    ecs_entity_t node = node_of(world, "mesh3", ecs_id(MeshMesh3), &(MeshMesh3){ .entity = e });
    box_mg_value_t *v = test_eval(world, node);
    test_int(v->mesh.vertex_count, 4);
    test_int(v->mesh.index_count, 6);
    near3(v->mesh.positions[2], (vec3){1, 1, 0}, 0);
    near3(v->mesh.normals[3], (vec3){0, 0, 1}, 0);
    test_near(v->mesh.uvs[1][0], 1, 0);
    test_int(v->mesh.indices[5], 3);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_mesh3_flat_normals(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t e = test_named(world, "tri");
    ecs_set(world, e, Mesh3, {
        .vertices = BOX_VEC_VIEW(vec3, {0, 0, 0}, {1, 0, 0}, {0, 1, 0})
    });
    box_mg_value_t *v = test_eval(world, node_of(world, "mesh3", ecs_id(MeshMesh3),
        &(MeshMesh3){ .entity = e }));
    int32_t i;
    test_int(v->mesh.vertex_count, 3);
    test_int(v->mesh.index_count, 3);
    for (i = 0; i < 3; i ++) {
        near3(v->mesh.normals[i], (vec3){0, 0, 1}, 1e-5f);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_path_line(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    box_mg_value_t *v = test_eval(world, r.curve);
    const box_mg_curves_t *c = &v->curves;
    test_int(v->type, MeshValueCurves);
    test_int(c->curve_count, 1);
    test_int(c->point_count, 2);
    test_int(c->offsets[0], 0);
    test_int(c->offsets[1], 2);
    test_int(c->parents[0], -1);
    near3(c->positions[1], (vec3){4, 0, 0}, 0);
    test_near(c->radii[0], 0.25f, 0);
    test_near(c->radii[1], 0.25f, 0);
    near3(c->tangents[0], (vec3){1, 0, 0}, 1e-6f);
    near3(c->normals[0], (vec3){0, 1, 0}, 1e-6f);
    near3(c->normals[1], (vec3){0, 1, 0}, 1e-6f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_path_curve_frames(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t path = test_named(world, "path");
    ecs_set(world, path, Path3D, { .start = {0, 0, 0},
        .segments = BOX_VEC_VIEW(PathSegment3D,
            { .kind = PathCatmullRom, .end = {1, 1, 0} },
            { .kind = PathCatmullRom, .end = {2, 0, 1} },
            { .kind = PathCatmullRom, .end = {3, 2, 2} }) });
    box_mg_value_t *v = test_eval(world, node_of(world, "curve", ecs_id(MeshPath),
        &(MeshPath){ .path = path, .steps = 8 }));
    const box_mg_curves_t *c = &v->curves;
    int32_t i;
    test_int(c->point_count, 1 + 3 * 8);
    for (i = 0; i < c->point_count; i ++) {
        test_near(glm_vec3_norm(c->tangents[i]), 1, 1e-4f);
        test_near(glm_vec3_norm(c->normals[i]), 1, 1e-4f);
        test_assert(fabsf(glm_vec3_dot(c->tangents[i], c->normals[i])) < 1e-3f);
        if (i) {
            test_assert(glm_vec3_dot(c->normals[i - 1], c->normals[i]) > 0.5f);
        }
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_path_closed(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t path = test_named(world, "square");
    ecs_set(world, path, Path3D, { .start = {0, 0, 0}, .closed = true,
        .segments = BOX_VEC_VIEW(PathSegment3D,
            { .kind = PathLine, .end = {1, 0, 0} },
            { .kind = PathLine, .end = {1, 0, 1} },
            { .kind = PathLine, .end = {0, 0, 1} }) });
    box_mg_value_t *v = test_eval(world, node_of(world, "curve", ecs_id(MeshPath),
        &(MeshPath){ .path = path }));
    const box_mg_curves_t *c = &v->curves;
    test_int(c->point_count, 5);
    near3(c->positions[0], c->positions[4], 0);
    near3(c->positions[2], (vec3){1, 0, 1}, 0);
    test_near(c->radii[0], 0.1f, 1e-6f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_path_taper(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t path = test_line(world, "line", (vec3){0, 0, 0}, (vec3){0, 2, 0});
    box_mg_value_t *v = test_eval(world, node_of(world, "curve", ecs_id(MeshPath),
        &(MeshPath){ .path = path, .radius = 0.2f, .radius_end = 0.1f }));
    const box_mg_curves_t *c = &v->curves;
    test_near(c->radii[0], 0.2f, 1e-6f);
    test_near(c->radii[c->point_count - 1], 0.1f, 1e-6f);
    near3(c->tangents[0], (vec3){0, 1, 0}, 1e-6f);
    test_near(glm_vec3_dot(c->normals[0], c->tangents[0]), 0, 1e-5f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_transform_mesh(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t ball = sphere_node(world, "ball", true);
    ecs_entity_t moved = node_of(world, "moved", ecs_id(MeshTransform), &(MeshTransform){
        .input = ball, .position = {1, 2, 3}, .scale = {2, 2, 2} });
    ecs_entity_t box = box_node(world, 2, 1, 1);
    ecs_entity_t turned = node_of(world, "turned", ecs_id(MeshTransform), &(MeshTransform){
        .input = box, .rotation = {0, GLM_PI_2f, 0}, .scale = {1, 1, 1} });
    box_mg_value_t *v = test_eval(world, moved);
    vec3 min, max, center;
    test_bounds(v->mesh.positions, v->mesh.vertex_count, min, max);
    glm_vec3_center(min, max, center);
    near3(center, (vec3){1, 2, 3}, 1e-3f);
    test_near(max[1] - min[1], 2, 1e-3f);
    box_mg_value_release(v);

    v = test_eval(world, turned);
    test_bounds(v->mesh.positions, v->mesh.vertex_count, min, max);
    near3(min, (vec3){-0.5f, -0.5f, -1}, 1e-4f);
    near3(max, (vec3){0.5f, 0.5f, 1}, 1e-4f);
    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_transform_points(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t turned = node_of(world, "turned", ecs_id(MeshTransform), &(MeshTransform){
        .input = r.points, .rotation = {0, GLM_PI_2f, 0}, .scale = {1, 1, 1} });
    box_mg_value_t *v = test_eval(world, turned);
    vec3 up;
    test_int(v->type, MeshValuePoints);
    test_int(v->points.count, 5);
    near3(v->points.positions[4], (vec3){0, 0, -4}, 1e-4f);
    point_up(&v->points, 4, up);
    near3(up, (vec3){0, 0, -1}, 1e-4f);
    test_assert(box_mg_attr_find(v->points.attrs, v->points.attr_count, "t") != NULL);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_merge_meshes(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t ball = sphere_node(world, "ball", true);
    ecs_entity_t box = box_node(world, 1, 1, 1);
    ecs_entity_t all = test_named(world, "all");
    ecs_set(world, all, MeshMerge, { .inputs = BOX_VEC_VIEW(ecs_entity_t, ball, box) });
    box_mg_value_t *a = test_eval(world, ball), *b = test_eval(world, box);
    box_mg_value_t *v = test_eval(world, all);
    test_int(v->mesh.vertex_count, a->mesh.vertex_count + b->mesh.vertex_count);
    test_int(v->mesh.index_count, a->mesh.index_count + b->mesh.index_count);
    test_int(v->mesh.indices[a->mesh.index_count],
        b->mesh.indices[0] + (uint32_t)a->mesh.vertex_count);

    box_mg_value_release(a);
    box_mg_value_release(b);
    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_merge_curves(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t p0 = test_line(world, "p0", (vec3){0, 0, 0}, (vec3){1, 0, 0});
    ecs_entity_t p1 = test_line(world, "p1", (vec3){0, 1, 0}, (vec3){1, 1, 0});
    ecs_entity_t c0 = node_of(world, "c0", ecs_id(MeshPath), &(MeshPath){ .path = p0 });
    ecs_entity_t c1 = node_of(world, "c1", ecs_id(MeshPath), &(MeshPath){ .path = p1 });
    ecs_entity_t all = test_named(world, "all");
    ecs_set(world, all, MeshMerge, { .inputs = BOX_VEC_VIEW(ecs_entity_t, c0, c1) });
    box_mg_value_t *v = test_eval(world, all);
    const box_mg_curves_t *c = &v->curves;
    test_int(c->curve_count, 2);
    test_int(c->point_count, 4);
    test_int(c->offsets[1], 2);
    test_int(c->offsets[2], 4);
    test_int(c->parents[0], -1);
    test_int(c->parents[1], -1);
    near3(c->positions[2], (vec3){0, 1, 0}, 0);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_merge_mixed_types(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t all = test_named(world, "mixed");
    ecs_set(world, all, MeshMerge, { .inputs = BOX_VEC_VIEW(ecs_entity_t, r.tube, r.curve) });
    char *error = NULL;
    test_assert(box_mesh_graph_evaluate(world, all, &error) == NULL);
    test_assert(error != NULL);
    test_assert(strstr(error, "inputs of 'mixed' output Mesh and Curves") != NULL);
    ecs_os_free(error);

    ecs_fini(world);
}

void MeshNodes_tube_counts(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    box_mg_value_t *v = test_eval(world, r.tube);
    vec3 min, max;
    test_int(v->mesh.vertex_count, 2 * 9);
    test_int(v->mesh.index_count, 8 * 6);
    test_bounds(v->mesh.positions, v->mesh.vertex_count, min, max);
    near3(min, (vec3){0, -0.25f, -0.25f}, 1e-5f);
    near3(max, (vec3){4, 0.25f, 0.25f}, 1e-5f);
    near3(v->mesh.normals[0], (vec3){0, 1, 0}, 1e-5f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_tube_uvs(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t half = node_of(world, "half", ecs_id(MeshTube), &(MeshTube){
        .curves = r.curve, .sides = 8, .v_scale = 0.5f });
    box_mg_value_t *v = test_eval(world, r.tube);
    test_near(v->mesh.uvs[0][0], 0, 0);
    test_near(v->mesh.uvs[8][0], 1, 1e-6f);
    test_near(v->mesh.uvs[4][0], 0.5f, 1e-5f);
    test_near(v->mesh.uvs[0][1], 0, 0);
    test_near(v->mesh.uvs[9][1], 4, 1e-6f);
    box_mg_value_release(v);

    v = test_eval(world, half);
    test_near(v->mesh.uvs[9][1], 2, 1e-6f);
    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_tube_caps(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t capped = node_of(world, "capped", ecs_id(MeshTube), &(MeshTube){
        .curves = r.curve, .sides = 8, .caps = true });
    box_mg_value_t *v = test_eval(world, capped);
    test_int(v->mesh.vertex_count, 2 * 9 + 2 * 9);
    test_int(v->mesh.index_count, 8 * 6 + 2 * 8 * 3);
    near3(v->mesh.normals[18], (vec3){-1, 0, 0}, 1e-5f);
    near3(v->mesh.normals[27], (vec3){1, 0, 0}, 1e-5f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_tube_profile(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t square = test_named(world, "square");
    ecs_set(world, square, Path2D, { .start = {-1, -1}, .closed = true,
        .segments = BOX_VEC_VIEW(PathSegment2D,
            { .kind = PathLine, .end = {1, -1} },
            { .kind = PathLine, .end = {1, 1} },
            { .kind = PathLine, .end = {-1, 1} }) });
    ecs_entity_t profile = node_of(world, "profile", ecs_id(MeshPath),
        &(MeshPath){ .path = square });
    ecs_entity_t tube = node_of(world, "boxy", ecs_id(MeshTube), &(MeshTube){
        .curves = r.curve, .profile = profile });
    box_mg_value_t *v = test_eval(world, tube);
    vec3 min, max;
    test_int(v->mesh.vertex_count, 2 * 5);
    test_int(v->mesh.index_count, 4 * 6);
    test_bounds(v->mesh.positions, v->mesh.vertex_count, min, max);
    near3(min, (vec3){0, -0.25f, -0.25f}, 1e-5f);
    near3(max, (vec3){4, 0.25f, 0.25f}, 1e-5f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_points_on_curves_count(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    box_mg_value_t *v = test_eval(world, r.points);
    const box_mg_points_t *p = &v->points;
    const float *t = box_mg_attr_find(p->attrs, p->attr_count, "t");
    const float *index = box_mg_attr_find(p->attrs, p->attr_count, "index");
    const float *radius = box_mg_attr_find(p->attrs, p->attr_count, "radius");
    const float *distance = box_mg_attr_find(p->attrs, p->attr_count, "distance");
    const float *curve = box_mg_attr_find(p->attrs, p->attr_count, "curve");
    int32_t i;
    test_int(p->count, 5);
    test_assert(t && index && radius && distance && curve);
    for (i = 0; i < 5; i ++) {
        vec3 up;
        near3(p->positions[i], (vec3){(float)i, 0, 0}, 1e-5f);
        test_near(t[i], (float)i / 4.0f, 1e-6f);
        test_near(distance[i], (float)i, 1e-5f);
        test_near(index[i], (float)i, 0);
        test_near(radius[i], 0.25f, 1e-6f);
        test_near(curve[i], 0, 0);
        point_up(p, i, up);
        near3(up, (vec3){1, 0, 0}, 1e-5f);
        near3(p->scales[i], (vec3){1, 1, 1}, 0);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_points_on_curves_spacing(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t spaced = node_of(world, "spaced", ecs_id(MeshPointsOnCurves),
        &(MeshPointsOnCurves){ .curves = r.curve, .spacing = 1.5f });
    box_mg_value_t *v = test_eval(world, spaced);
    test_int(v->points.count, 3);
    test_near(v->points.positions[0][0], 0, 1e-5f);
    test_near(v->points.positions[1][0], 1.5f, 1e-5f);
    test_near(v->points.positions[2][0], 3, 1e-5f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_points_on_curves_range(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t ranged = node_of(world, "ranged", ecs_id(MeshPointsOnCurves),
        &(MeshPointsOnCurves){ .curves = r.curve, .count = 3, .start = 0.25f, .end = 0.75f });
    box_mg_value_t *v = test_eval(world, ranged);
    const float *t = box_mg_attr_find(v->points.attrs, v->points.attr_count, "t");
    test_int(v->points.count, 3);
    test_near(v->points.positions[0][0], 1, 1e-5f);
    test_near(v->points.positions[2][0], 3, 1e-5f);
    test_near(t[0], 0.25f, 1e-6f);
    test_near(t[1], 0.5f, 1e-6f);
    test_near(t[2], 0.75f, 1e-6f);

    box_mg_value_release(v);
    ecs_fini(world);
}

/** Evaluate jittered points on a rail in a new world.
 *
 * @param seed Seed of the jitter
 * @return The x of each of 9 points, freed by the caller
 */
static float* jittered(
    int32_t seed)
{
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t node = node_of(world, "jittered", ecs_id(MeshPointsOnCurves),
        &(MeshPointsOnCurves){ .curves = r.curve, .count = 9, .jitter = 0.8f, .seed = seed });
    box_mg_value_t *v = test_eval(world, node);
    float *result = ecs_os_malloc_n(float, 9);
    int32_t i;
    test_int(v->points.count, 9);
    for (i = 0; i < 9; i ++) {
        result[i] = v->points.positions[i][0];
        test_assert(result[i] >= 0.0f && result[i] <= 4.0f);
        test_assert(fabsf(result[i] - 0.5f * (float)i) <= 0.2f + 1e-5f);
    }

    box_mg_value_release(v);
    ecs_fini(world);

    return result;
}

void MeshNodes_points_on_curves_jitter(void) {
    float *a = jittered(1), *b = jittered(1), *c = jittered(2);
    test_assert(!memcmp(a, b, sizeof(float) * 9));
    test_assert(memcmp(a, c, sizeof(float) * 9) != 0);
    ecs_os_free(a);
    ecs_os_free(b);
    ecs_os_free(c);
}

void MeshNodes_points_on_mesh_count(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t box = box_node(world, 1, 1, 1);
    ecs_entity_t node = node_of(world, "scatter", ecs_id(MeshPointsOnMesh),
        &(MeshPointsOnMesh){ .mesh = box, .count = 50, .seed = 3 });
    box_mg_value_t *v = test_eval(world, node);
    const box_mg_points_t *p = &v->points;
    int32_t i, k;
    test_int(p->count, 50);
    test_assert(box_mg_attr_find(p->attrs, p->attr_count, "u") != NULL);
    for (i = 0; i < p->count; i ++) {
        float largest = 0;
        int32_t axis = 0;
        vec3 up;
        for (k = 0; k < 3; k ++) {
            test_assert(fabsf(p->positions[i][k]) <= 0.5f + 1e-5f);
            if (fabsf(p->positions[i][k]) > largest) {
                largest = fabsf(p->positions[i][k]);
                axis = k;
            }
        }

        test_near(largest, 0.5f, 1e-4f);
        point_up(p, i, up);
        test_near(up[axis], p->positions[i][axis] > 0 ? 1.0f : -1.0f, 1e-4f);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

/** Scatter points on a sphere in a new world.
 *
 * @param seed Seed of the placement
 * @return The hash of the points
 */
static uint64_t scattered(
    int32_t seed)
{
    ecs_world_t *world = test_world();
    ecs_entity_t ball = sphere_node(world, "ball", true);
    ecs_entity_t node = node_of(world, "scatter", ecs_id(MeshPointsOnMesh),
        &(MeshPointsOnMesh){ .mesh = ball, .count = 40, .seed = seed });
    box_mg_value_t *v = test_eval(world, node);
    uint64_t result = box_mg_value_hash(0, v);
    test_int(v->points.count, 40);
    box_mg_value_release(v);
    ecs_fini(world);

    return result;
}

void MeshNodes_points_on_mesh_seeded(void) {
    uint64_t a = scattered(3), b = scattered(3), c = scattered(4);
    test_assert(a == b);
    test_assert(a != c);
}

void MeshNodes_points_on_mesh_spacing(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t box = box_node(world, 1, 1, 1);
    ecs_entity_t node = node_of(world, "sparse", ecs_id(MeshPointsOnMesh),
        &(MeshPointsOnMesh){ .mesh = box, .count = 500, .min_spacing = 0.3f, .seed = 1 });
    box_mg_value_t *v = test_eval(world, node);
    const box_mg_points_t *p = &v->points;
    int32_t i, j;
    test_assert(p->count > 10);
    test_assert(p->count < 500);
    for (i = 0; i < p->count; i ++) {
        for (j = i + 1; j < p->count; j ++) {
            test_assert(glm_vec3_distance(p->positions[i], p->positions[j]) >= 0.3f - 1e-5f);
        }
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_instance_on_points(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    box_mg_value_t *ball = test_eval(world, r.ball);
    box_mg_value_t *v = test_eval(world, r.balls);
    const float *instance = box_mg_attr_find(v->mesh.attrs, v->mesh.attr_count, "instance");
    vec3 min, max;
    test_int(v->mesh.vertex_count, 5 * ball->mesh.vertex_count);
    test_int(v->mesh.index_count, 5 * ball->mesh.index_count);
    test_assert(instance != NULL);
    test_near(instance[0], 0, 0);
    test_near(instance[v->mesh.vertex_count - 1], 4, 0);
    test_bounds(v->mesh.positions, v->mesh.vertex_count, min, max);
    test_assert(min[0] >= -0.5f - 1e-4f && min[0] < -0.35f);
    test_assert(max[0] <= 4.5f + 1e-4f && max[0] > 4.35f);
    test_int(v->mesh.indices[ball->mesh.index_count],
        ball->mesh.indices[0] + (uint32_t)ball->mesh.vertex_count);

    box_mg_value_release(ball);
    box_mg_value_release(v);
    ecs_fini(world);
}

/** Displace a sphere in a new world.
 *
 * @param seed Seed of the noise
 * @return The hash of the mesh
 */
static uint64_t displaced(
    int32_t seed)
{
    ecs_world_t *world = test_world();
    ecs_entity_t ball = sphere_node(world, "ball", true);
    ecs_entity_t node = node_of(world, "rough", ecs_id(MeshDisplace),
        &(MeshDisplace){ .mesh = ball, .amount = 0.1f, .frequency = 3, .octaves = 3,
            .seed = seed });
    box_mg_value_t *v = test_eval(world, node);
    uint64_t result = box_mg_value_hash(0, v);
    box_mg_value_release(v);
    ecs_fini(world);

    return result;
}

void MeshNodes_displace_seeded(void) {
    uint64_t a = displaced(1), b = displaced(1), c = displaced(2);
    test_assert(a == b);
    test_assert(a != c);
}

void MeshNodes_displace_along_normal(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t ball = sphere_node(world, "ball", true);
    ecs_entity_t node = node_of(world, "rough", ecs_id(MeshDisplace),
        &(MeshDisplace){ .mesh = ball, .amount = 0.1f, .frequency = 3, .octaves = 2,
            .seed = 7 });
    box_mg_value_t *src = test_eval(world, ball), *v = test_eval(world, node);
    int32_t i, moved = 0;
    test_int(v->mesh.vertex_count, src->mesh.vertex_count);
    for (i = 0; i < v->mesh.vertex_count; i ++) {
        vec3 d, cross;
        glm_vec3_sub(v->mesh.positions[i], src->mesh.positions[i], d);
        glm_vec3_cross(d, src->mesh.normals[i], cross);
        test_assert(glm_vec3_norm(cross) < 1e-5f);
        test_assert(glm_vec3_norm(d) <= 0.1f + 1e-5f);
        moved += glm_vec3_norm(d) > 1e-4f;
    }

    test_assert(moved > v->mesh.vertex_count / 2);
    box_mg_value_release(src);
    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_normals_flat(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t ball = sphere_node(world, "ball", true);
    ecs_entity_t node = node_of(world, "flat", ecs_id(MeshNormals),
        &(MeshNormals){ .mesh = ball, .flat = true });
    box_mg_value_t *v = test_eval(world, node);
    int32_t i;
    test_int(v->mesh.vertex_count, v->mesh.index_count);
    for (i = 0; i + 2 < v->mesh.index_count; i += 3) {
        vec3 ab, ac, n;
        glm_vec3_sub(v->mesh.positions[i + 1], v->mesh.positions[i], ab);
        glm_vec3_sub(v->mesh.positions[i + 2], v->mesh.positions[i], ac);
        glm_vec3_cross(ab, ac, n);
        glm_vec3_normalize(n);
        near3(v->mesh.normals[i], n, 1e-4f);
        test_assert(glm_vec3_dot(n, v->mesh.positions[i]) > 0);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_normals_smooth(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t ball = sphere_node(world, "faceted", false);
    ecs_entity_t node = node_of(world, "smooth", ecs_id(MeshNormals),
        &(MeshNormals){ .mesh = ball });
    box_mg_value_t *src = test_eval(world, ball), *v = test_eval(world, node);
    int32_t i;
    test_int(v->mesh.index_count, src->mesh.index_count);
    test_assert(v->mesh.vertex_count < src->mesh.vertex_count);
    for (i = 0; i < v->mesh.vertex_count; i ++) {
        vec3 dir;
        glm_vec3_normalize_to(v->mesh.positions[i], dir);
        test_assert(glm_vec3_dot(dir, v->mesh.normals[i]) > 0.98f);
    }

    box_mg_value_release(src);
    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshNodes_normals_angle(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t box = box_node(world, 1, 1, 1);
    ecs_entity_t node = node_of(world, "hard", ecs_id(MeshNormals),
        &(MeshNormals){ .mesh = box, .angle = 0.5f });
    box_mg_value_t *src = test_eval(world, box), *v = test_eval(world, node);
    int32_t i;
    test_int(v->mesh.vertex_count, src->mesh.vertex_count);
    for (i = 0; i < v->mesh.vertex_count; i ++) {
        float largest = glm_vec3_max((vec3){ fabsf(v->mesh.normals[i][0]),
            fabsf(v->mesh.normals[i][1]), fabsf(v->mesh.normals[i][2]) });
        test_near(largest, 1, 1e-5f);
    }

    box_mg_value_release(src);
    box_mg_value_release(v);
    ecs_fini(world);
}

/** Evaluate a graph that uses every random kind in a new world.
 *
 * @return The hash of its mesh
 */
static uint64_t everything(void)
{
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t rough = node_of(world, "rough", ecs_id(MeshDisplace),
        &(MeshDisplace){ .mesh = r.all, .amount = 0.05f, .frequency = 2, .octaves = 2,
            .seed = 9 });
    ecs_entity_t spots = node_of(world, "spots", ecs_id(MeshPointsOnMesh),
        &(MeshPointsOnMesh){ .mesh = rough, .count = 30, .min_spacing = 0.05f, .seed = 5 });
    ecs_entity_t dots = node_of(world, "dots", ecs_id(MeshInstanceOnPoints),
        &(MeshInstanceOnPoints){ .points = spots, .instance = r.ball });
    ecs_entity_t smooth = node_of(world, "smooth", ecs_id(MeshNormals),
        &(MeshNormals){ .mesh = dots, .angle = 1 });
    box_mg_value_t *v = test_eval(world, smooth);
    uint64_t result = box_mg_value_hash(0, v);
    test_assert(v->mesh.vertex_count > 0);
    box_mg_value_release(v);
    ecs_fini(world);

    return result;
}

void MeshNodes_deterministic_across_worlds(void) {
    uint64_t a = everything(), b = everything();
    test_assert(a == b);
}

void MeshNodes_sincos(void) {
    int32_t i;
    for (i = -2000; i <= 2000; i ++) {
        float a = (float)i * 0.0137f, s, c;
        box_mg_sincos(a, &s, &c);
        test_near(s, sinf(a), 2e-6f);
        test_near(c, cosf(a), 2e-6f);
    }

    mat4 expect;
    mat3 r;
    vec3 angles = { 0.3f, -1.2f, 2.5f };
    glm_euler_yxz(angles, expect);
    box_mg_euler(angles, r);
    for (i = 0; i < 9; i ++) {
        test_near(r[i / 3][i % 3], expect[i / 3][i % 3], 2e-6f);
    }
}
