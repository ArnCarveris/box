/**
 * @file MeshBranch.c
 * @brief Tests for the branching nodes of box.mesh: Stem, Branches, Prune and the collars,
 * bark uvs and bark data of Tube on branching curves.
 */

#include "mesh_test.h"

/** A three level tree graph as a script */
#define BRANCH_TREE "test/assets/mesh/tree.flecs"

/** Create a node.
 *
 * @param world The world
 * @param name The name
 * @param component The node component
 * @param value The node value
 * @return The node
 */
static ecs_entity_t branch_node(
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

/** Create a straight vertical Stem of 8 segments from the origin.
 *
 * @param world The world
 * @param length Length
 * @param radius Radius at the root
 * @param taper Fraction of the radius the tip loses
 * @return The node
 */
static ecs_entity_t trunk_node(
    ecs_world_t *world,
    float length,
    float radius,
    float taper)
{
    return branch_node(world, "trunk", ecs_id(MeshStem), &(MeshStem){
        .length = length, .segments = 8, .radius = radius, .taper = taper });
}

/** Create a Branches on a parent node with horizontal children and no variation.
 *
 * @param world The world
 * @param name The name
 * @param parent The parent node
 * @param distribution The distribution
 * @param count Children per parent
 * @param length Length of a child, a fraction of the parent length
 * @return The node
 */
static ecs_entity_t branches_node(
    ecs_world_t *world,
    const char *name,
    ecs_entity_t parent,
    MeshBranchDistribution distribution,
    int32_t count,
    float length)
{
    return branch_node(world, name, ecs_id(MeshBranches), &(MeshBranches){
        .curves = parent, .count = count, .end = 1.0f, .distribution = distribution,
        .down_angle = GLM_PI_2f, .length = length, .taper = 0.5f, .segments = 4 });
}

/** Get the unit direction of the first segment of a curve.
 *
 * @param c The curves
 * @param curve The curve
 * @param out Receives the direction
 */
static void curve_direction(
    const box_mg_curves_t *c,
    int32_t curve,
    vec3 out)
{
    int32_t first = c->offsets[curve];
    glm_vec3_sub(c->positions[first + 1], c->positions[first], out);
    glm_vec3_normalize(out);
}

/** Get the length of a curve.
 *
 * @param c The curves
 * @param curve The curve
 * @return The length
 */
static float curve_length(
    const box_mg_curves_t *c,
    int32_t curve)
{
    float *lengths = ecs_os_malloc_n(float, c->point_count + 1);
    float result = box_mg_curve_lengths(c, curve, lengths);
    ecs_os_free(lengths);

    return result;
}

/** Get an attribute of curves, which must exist.
 *
 * @param c The curves
 * @param name The name
 * @return The values
 */
static const float* curve_attr(
    const box_mg_curves_t *c,
    const char *name)
{
    const float *result = box_mg_attr_find(c->attrs, c->attr_count, name);
    test_assert(result != NULL);

    return result;
}

/** Get a data channel of a mesh, which must exist.
 *
 * @param m The mesh
 * @param channel The channel
 * @return The values
 */
static const float* mesh_data(
    const box_mg_mesh_t *m,
    int32_t channel)
{
    char name[BOX_MESH_ATTR_NAME];
    ecs_os_snprintf(name, ECS_SIZEOF(name), "%s%d", BOX_MESH_DATA_ATTR, channel);
    const float *result = box_mg_attr_find(m->attrs, m->attr_count, name);
    test_assert(result != NULL);

    return result;
}

void MeshBranch_stem(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    box_mg_value_t *v = test_eval(world, trunk);
    const box_mg_curves_t *c = &v->curves;
    int32_t i;
    test_int(c->curve_count, 1);
    test_int(c->point_count, 9);
    test_int(c->parents[0], -1);
    const float *level = curve_attr(c, "level"), *tip = curve_attr(c, "tip");
    const float *root = curve_attr(c, "root_distance");
    for (i = 0; i < 9; i ++) {
        near3(c->positions[i], (vec3){0, (float)i * 0.5f, 0}, 1e-5f);
        test_near(c->radii[i], 0.2f * (1.0f - 0.5f * (float)i / 8.0f), 1e-6f);
        near3(c->tangents[i], (vec3){0, 1, 0}, 1e-5f);
        test_near(level[i], 0, 0);
        test_near(tip[i], i == 8 ? 1.0f : 0.0f, 0);
        test_near(root[i], (float)i * 0.5f, 1e-5f);
    }

    curve_attr(c, "parent_t");
    curve_attr(c, "phase");
    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_stem_on_points(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    ecs_entity_t stems = branch_node(world, "stems", ecs_id(MeshStem), &(MeshStem){
        .points = r.points, .length = 2, .segments = 4, .radius = 0.1f });
    box_mg_value_t *v = test_eval(world, stems);
    const box_mg_curves_t *c = &v->curves;
    int32_t i;
    test_int(c->curve_count, 5);
    test_int(c->point_count, 25);
    for (i = 0; i < 5; i ++) {
        vec3 d;
        near3(c->positions[c->offsets[i]], (vec3){(float)i, 0, 0}, 1e-5f);
        curve_direction(c, i, d);
        near3(d, (vec3){1, 0, 0}, 1e-5f);
        test_near(curve_length(c, i), 2, 1e-4f);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_stem_gravitropism(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t up = branch_node(world, "up", ecs_id(MeshStem), &(MeshStem){
        .direction = {1, 0, 0}, .length = 3, .segments = 12, .radius = 0.1f,
        .gravitropism = 0.5f });
    ecs_entity_t down = branch_node(world, "down", ecs_id(MeshStem), &(MeshStem){
        .direction = {1, 0, 0}, .length = 3, .segments = 12, .radius = 0.1f,
        .gravitropism = -0.5f });
    box_mg_value_t *a = test_eval(world, up), *b = test_eval(world, down);
    test_assert(a->curves.positions[12][1] > 0.5f);
    test_assert(b->curves.positions[12][1] < -0.5f);
    test_near(a->curves.positions[12][1], -b->curves.positions[12][1], 1e-4f);
    test_near(curve_length(&a->curves, 0), 3, 1e-3f);

    box_mg_value_release(a);
    box_mg_value_release(b);
    ecs_fini(world);
}

void MeshBranch_stem_twist(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t straight = trunk_node(world, 4, 0.2f, 0);
    ecs_entity_t twisted = branch_node(world, "twisted", ecs_id(MeshStem), &(MeshStem){
        .length = 4, .segments = 8, .radius = 0.2f, .twist = GLM_PI_2f });
    box_mg_value_t *a = test_eval(world, straight), *b = test_eval(world, twisted);
    near3(b->curves.normals[0], a->curves.normals[0], 1e-5f);
    test_near(glm_vec3_dot(a->curves.normals[2], b->curves.normals[2]), 0, 1e-4f);
    test_near(glm_vec3_dot(a->curves.normals[4], b->curves.normals[4]), -1, 1e-4f);

    box_mg_value_release(a);
    box_mg_value_release(b);
    ecs_fini(world);
}

void MeshBranch_alternate(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 6, 0.25f);
    box_mg_value_t *v = test_eval(world, limbs);
    const box_mg_curves_t *c = &v->curves;
    const float *level = curve_attr(c, "level"), *pt = curve_attr(c, "parent_t");
    const float *tip = curve_attr(c, "tip");
    int32_t i;
    test_int(c->curve_count, 7);
    test_int(c->point_count, 9 + 6 * 5);
    test_near(tip[8], 0, 0);
    for (i = 1; i <= 6; i ++) {
        int32_t first = c->offsets[i];
        float t = ((float)(i - 1) + 0.5f) / 6.0f;
        vec3 d;
        test_int(c->parents[i], 0);
        test_int(c->offsets[i + 1] - first, 5);
        test_near(level[first], 1, 0);
        test_near(pt[first], t, 1e-5f);
        test_near(tip[first + 4], 1, 0);
        near3(c->positions[first], (vec3){0, 4 * t, 0}, 1e-4f);
        curve_direction(c, i, d);
        test_near(d[1], 0, 1e-4f);
        test_near(curve_length(c, i), 1, 1e-4f);
        if (i > 1) {
            vec3 prev;
            curve_direction(c, i - 1, prev);
            test_near(glm_vec3_dot(d, prev), -1, 1e-4f);
        }
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_opposite(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchOpposite, 6, 0.25f);
    box_mg_value_t *v = test_eval(world, limbs);
    const box_mg_curves_t *c = &v->curves;
    int32_t node;
    test_int(c->curve_count, 7);
    for (node = 0; node < 3; node ++) {
        vec3 a, b;
        int32_t i = 1 + node * 2;
        curve_direction(c, i, a);
        curve_direction(c, i + 1, b);
        near3(c->positions[c->offsets[i]], c->positions[c->offsets[i + 1]], 1e-6f);
        test_near(glm_vec3_dot(a, b), -1, 1e-4f);
        if (node) {
            vec3 prev;
            curve_direction(c, i - 2, prev);
            test_near(glm_vec3_dot(a, prev), 0, 1e-4f);
        }
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_whorled(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branch_node(world, "limbs", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .count = 6, .end = 1, .distribution = MeshBranchWhorled,
        .whorl = 3, .down_angle = GLM_PI_2f, .length = 0.25f, .segments = 4 });
    box_mg_value_t *v = test_eval(world, limbs);
    const box_mg_curves_t *c = &v->curves;
    vec3 a, b, d;
    test_int(c->curve_count, 7);
    curve_direction(c, 1, a);
    curve_direction(c, 2, b);
    curve_direction(c, 4, d);
    test_near(glm_vec3_dot(a, b), -0.5f, 1e-4f);
    test_near(glm_vec3_dot(a, d), 0.5f, 1e-4f);
    test_near(c->positions[c->offsets[1]][1], c->positions[c->offsets[3]][1], 1e-6f);
    test_assert(c->positions[c->offsets[4]][1] > c->positions[c->offsets[3]][1] + 1);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_spiral(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchSpiral, 5, 0.25f);
    ecs_entity_t quarter = branch_node(world, "quarter", ecs_id(MeshBranches),
        &(MeshBranches){ .curves = trunk, .count = 5, .end = 1,
            .distribution = MeshBranchSpiral, .phyllotaxis = GLM_PI_2f,
            .down_angle = GLM_PI_2f, .length = 0.25f, .segments = 4 });
    box_mg_value_t *v = test_eval(world, limbs), *q = test_eval(world, quarter);
    int32_t i;
    test_int(v->curves.curve_count, 6);
    for (i = 2; i <= 5; i ++) {
        vec3 a, b;
        curve_direction(&v->curves, i - 1, a);
        curve_direction(&v->curves, i, b);
        test_near(glm_vec3_dot(a, b), -0.73736887f, 1e-4f);
        curve_direction(&q->curves, i - 1, a);
        curve_direction(&q->curves, i, b);
        test_near(glm_vec3_dot(a, b), 0, 1e-4f);
    }

    box_mg_value_release(v);
    box_mg_value_release(q);
    ecs_fini(world);
}

void MeshBranch_density(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branch_node(world, "limbs", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .density = 2, .start = 0.5f, .end = 1,
        .distribution = MeshBranchAlternate, .down_angle = 1, .length = 0.25f,
        .segments = 4 });
    box_mg_value_t *v = test_eval(world, limbs);
    const box_mg_curves_t *c = &v->curves;
    int32_t i;
    test_int(c->curve_count, 5);
    for (i = 1; i < 5; i ++) {
        float y = c->positions[c->offsets[i]][1];
        test_assert(y > 2 && y < 4);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_down_angle_change(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branch_node(world, "limbs", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .count = 2, .end = 1, .distribution = MeshBranchAlternate,
        .down_angle = 1.2f, .down_angle_change = -0.8f, .length = 0.25f, .segments = 4 });
    box_mg_value_t *v = test_eval(world, limbs);
    vec3 a, b;
    curve_direction(&v->curves, 1, a);
    curve_direction(&v->curves, 2, b);
    test_near(a[1], cosf(1.2f - 0.8f * 0.25f), 1e-4f);
    test_near(b[1], cosf(1.2f - 0.8f * 0.75f), 1e-4f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_length_curve(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branch_node(world, "limbs", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .count = 4, .end = 1, .distribution = MeshBranchAlternate,
        .down_angle = 1, .length = 0.5f, .length_curve = {1, 1, 0.5f, 0.5f, 0},
        .segments = 4 });
    box_mg_value_t *v = test_eval(world, limbs);
    test_near(curve_length(&v->curves, 1), 2, 1e-4f);
    test_near(curve_length(&v->curves, 2), 2 * 0.75f, 1e-4f);
    test_near(curve_length(&v->curves, 3), 2 * 0.5f, 1e-4f);
    test_near(curve_length(&v->curves, 4), 2 * 0.25f, 1e-4f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_pipe_model(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t area = branches_node(world, "area", trunk, MeshBranchOpposite, 6, 0.25f);
    ecs_entity_t cube = branch_node(world, "cube", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .count = 6, .end = 1, .distribution = MeshBranchOpposite,
        .down_angle = 1, .length = 0.25f, .radius_exponent = 3, .segments = 4 });
    box_mg_value_t *a = test_eval(world, area), *b = test_eval(world, cube);
    int32_t i;
    for (i = 1; i <= 6; i ++) {
        float y = a->curves.positions[a->curves.offsets[i]][1];
        float rp = 0.2f * (1.0f - 0.5f * y / 4.0f);
        float ra = a->curves.radii[a->curves.offsets[i]];
        float rb = b->curves.radii[b->curves.offsets[i]];
        test_near(ra, rp / sqrtf(3.0f), 1e-5f);
        test_near(rb, rp * powf(3.0f, -1.0f / 3.0f), 1e-5f);
        test_near(rp * rp, 3 * ra * ra, 1e-5f);
        test_near(a->curves.radii[a->curves.offsets[i + 1] - 1], 0.5f * ra, 1e-5f);
    }

    box_mg_value_release(a);
    box_mg_value_release(b);
    ecs_fini(world);
}

void MeshBranch_levels(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 4, 0.5f);
    ecs_entity_t twigs = branches_node(world, "twigs", limbs, MeshBranchOpposite, 2, 0.5f);
    box_mg_value_t *v = test_eval(world, twigs);
    const box_mg_curves_t *c = &v->curves;
    const float *level = curve_attr(c, "level"), *root = curve_attr(c, "root_distance");
    const float *pt = curve_attr(c, "parent_t"), *tip = curve_attr(c, "tip");
    int32_t i;
    test_int(c->curve_count, 1 + 4 + 4 * 2);
    for (i = 5; i < c->curve_count; i ++) {
        int32_t first = c->offsets[i], p = c->parents[i];
        test_assert(p >= 1 && p <= 4);
        test_near(level[first], 2, 0);
        test_near(level[c->offsets[p]], 1, 0);
        test_near(tip[c->offsets[p + 1] - 1], 0, 0);
        test_near(tip[c->offsets[i + 1] - 1], 1, 0);
        test_near(pt[first], 0.5f, 1e-5f);
        test_near(root[first], root[c->offsets[p]] + 0.5f * curve_length(c, p), 1e-4f);
        test_near(curve_length(c, i), 0.5f * curve_length(c, p), 1e-4f);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_bends(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t droop = branch_node(world, "droop", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .count = 1, .end = 1, .down_angle = GLM_PI_2f, .length = 0.5f,
        .segments = 8, .gravity = 0.4f });
    ecs_entity_t reach = branch_node(world, "reach", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .count = 1, .end = 1, .down_angle = GLM_PI_2f, .length = 0.5f,
        .segments = 8, .phototropism = 0.4f, .light = {1, 0, 0} });
    box_mg_value_t *a = test_eval(world, droop), *b = test_eval(world, reach);
    int32_t first = a->curves.offsets[1], last = a->curves.offsets[2] - 1;
    test_assert(a->curves.positions[last][1] < a->curves.positions[first][1] - 0.3f);
    vec3 d0, d1;
    curve_direction(&b->curves, 1, d0);
    glm_vec3_sub(b->curves.positions[last], b->curves.positions[last - 1], d1);
    glm_vec3_normalize(d1);
    test_assert(d1[0] > d0[0] + 0.1f);

    box_mg_value_release(a);
    box_mg_value_release(b);
    ecs_fini(world);
}

void MeshBranch_branches_curve(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t up = branch_node(world, "up", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .count = 1, .end = 1, .down_angle = GLM_PI_2f, .length = 0.5f,
        .segments = 8, .curve = 1 });
    ecs_entity_t down = branch_node(world, "down", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .count = 1, .end = 1, .down_angle = GLM_PI_2f, .length = 0.5f,
        .segments = 8, .curve = -1 });
    box_mg_value_t *a = test_eval(world, up), *b = test_eval(world, down);
    int32_t last = a->curves.offsets[2] - 1;
    vec3 da, db;
    glm_vec3_sub(a->curves.positions[last], a->curves.positions[last - 1], da);
    glm_vec3_normalize(da);
    glm_vec3_sub(b->curves.positions[last], b->curves.positions[last - 1], db);
    glm_vec3_normalize(db);
    test_near(da[1], sinf(1.0f), 1e-3f);
    test_near(db[1], -sinf(1.0f), 1e-3f);
    test_near(curve_length(&a->curves, 1), 2.0f, 1e-4f);

    box_mg_value_release(a);
    box_mg_value_release(b);
    ecs_fini(world);
}

void MeshBranch_branches_curve_back(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t s = branch_node(world, "s", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .count = 1, .end = 1, .down_angle = GLM_PI_2f, .length = 0.5f,
        .segments = 8, .curve = 0.5f, .curve_back = -0.5f });
    box_mg_value_t *v = test_eval(world, s);
    int32_t first = v->curves.offsets[1], last = v->curves.offsets[2] - 1;
    vec3 mid, end;
    glm_vec3_sub(v->curves.positions[first + 4], v->curves.positions[first + 3], mid);
    glm_vec3_normalize(mid);
    glm_vec3_sub(v->curves.positions[last], v->curves.positions[last - 1], end);
    glm_vec3_normalize(end);
    test_near(mid[1], sinf(0.5f), 1e-3f);
    test_near(end[1], 0, 1e-3f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_branches_kink(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t straight = branch_node(world, "straight", ecs_id(MeshBranches),
        &(MeshBranches){ .curves = trunk, .count = 3, .end = 1, .down_angle = GLM_PI_2f,
        .length = 0.5f, .segments = 8 });
    ecs_entity_t kinked = branch_node(world, "kinked", ecs_id(MeshBranches),
        &(MeshBranches){ .curves = trunk, .count = 3, .end = 1, .down_angle = GLM_PI_2f,
        .length = 0.5f, .segments = 8, .curve_variation = 0.3f });
    box_mg_value_t *a = test_eval(world, straight), *b = test_eval(world, kinked);
    int32_t i, j;
    float most = 0;
    for (i = 1; i <= 3; i ++) {
        int32_t first = b->curves.offsets[i];
        for (j = first + 1; j < b->curves.offsets[i + 1] - 1; j ++) {
            vec3 s0, s1, k0, k1;
            glm_vec3_sub(a->curves.positions[j], a->curves.positions[j - 1], s0);
            glm_vec3_sub(a->curves.positions[j + 1], a->curves.positions[j], s1);
            glm_vec3_sub(b->curves.positions[j], b->curves.positions[j - 1], k0);
            glm_vec3_sub(b->curves.positions[j + 1], b->curves.positions[j], k1);
            glm_vec3_normalize(s0);
            glm_vec3_normalize(s1);
            glm_vec3_normalize(k0);
            glm_vec3_normalize(k1);
            test_near(glm_vec3_dot(s0, s1), 1, 1e-5f);
            float turn = acosf(glm_clamp(glm_vec3_dot(k0, k1), -1.0f, 1.0f));
            test_assert(turn <= 0.3f + 1e-3f);
            most = glm_max(most, turn);
        }

        test_near(curve_length(&b->curves, i), curve_length(&a->curves, i), 1e-4f);
    }

    test_assert(most > 0.05f);

    box_mg_value_release(a);
    box_mg_value_release(b);
    ecs_fini(world);
}

void MeshBranch_pipe_model_tips(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchOpposite, 6, 0.25f);
    ecs_entity_t pipe = branch_node(world, "pipe", ecs_id(MeshPipeModel), &(MeshPipeModel){
        .curves = limbs, .exponent = 2, .tip_length = 0.1f });
    box_mg_value_t *v = test_eval(world, pipe);
    const box_mg_curves_t *c = &v->curves;
    int32_t i, j;
    test_int(c->curve_count, 7);
    test_near(c->radii[c->offsets[0]], 0.2f, 1e-5f);
    for (i = 0; i < c->curve_count; i ++) {
        for (j = c->offsets[i] + 1; j < c->offsets[i + 1]; j ++) {
            test_assert(c->radii[j] <= c->radii[j - 1] + 1e-6f);
        }
    }

    for (i = 1; i < c->curve_count; i ++) {
        float base = c->radii[c->offsets[i]], tip = c->radii[c->offsets[i + 1] - 1];
        float length = curve_length(c, i);
        test_near(base * base * 0.1f, tip * tip * (length + 0.1f), 1e-6f);
        test_assert(tip > 0);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_pipe_model_conserve(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchOpposite, 6, 0.25f);
    ecs_entity_t pipe = branch_node(world, "pipe", ecs_id(MeshPipeModel), &(MeshPipeModel){
        .curves = limbs, .exponent = 2, .radius = 0.3f, .tip_length = 0.1f });
    box_mg_value_t *v = test_eval(world, pipe);
    const box_mg_curves_t *c = &v->curves;
    float root = c->radii[c->offsets[0]], shoots = curve_length(c, 0) + 0.1f;
    int32_t i;
    test_near(root, 0.3f, 1e-5f);
    for (i = 1; i < c->curve_count; i ++) {
        shoots += curve_length(c, i) + 0.1f;
    }

    for (i = 1; i < c->curve_count; i ++) {
        float base = c->radii[c->offsets[i]];
        test_near(base * base / (root * root), (curve_length(c, i) + 0.1f) / shoots, 1e-4f);
        test_assert(base < root);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_deterministic(void) {
    ecs_world_t *world = test_world();
    MeshBranches value = { .count = 5, .end = 1, .distribution = MeshBranchSpiral,
        .down_angle = 0.8f, .length = 0.4f, .segments = 5, .noise = 0.5f,
        .angle_variation = 0.3f, .roll_variation = 0.5f, .length_variation = 0.3f,
        .radius_variation = 0.2f, .seed = 7 };
    ecs_entity_t trunk = branch_node(world, "trunk", ecs_id(MeshStem), &(MeshStem){
        .length = 4, .segments = 8, .radius = 0.2f, .noise = 0.3f, .seed = 3 });
    value.curves = trunk;
    ecs_entity_t a = branch_node(world, "a", ecs_id(MeshBranches), &value);
    box_mg_value_t *va = test_eval(world, a);
    ecs_world_t *other = test_world();
    ecs_entity_t trunk2 = branch_node(other, "trunk", ecs_id(MeshStem), &(MeshStem){
        .length = 4, .segments = 8, .radius = 0.2f, .noise = 0.3f, .seed = 3 });
    value.curves = trunk2;
    ecs_entity_t b = branch_node(other, "b", ecs_id(MeshBranches), &value);
    value.seed = 8;
    ecs_entity_t c = branch_node(other, "c", ecs_id(MeshBranches), &value);
    box_mg_value_t *vb = test_eval(other, b), *vc = test_eval(other, c);
    test_int(va->curves.point_count, vb->curves.point_count);
    test_assert(!memcmp(va->curves.positions, vb->curves.positions,
        sizeof(vec3) * (size_t)va->curves.point_count));
    test_assert(!memcmp(va->curves.radii, vb->curves.radii,
        sizeof(float) * (size_t)va->curves.point_count));
    test_int(vc->curves.point_count, va->curves.point_count);
    test_assert(memcmp(va->curves.positions, vc->curves.positions,
        sizeof(vec3) * (size_t)va->curves.point_count) != 0);

    box_mg_value_release(va);
    box_mg_value_release(vb);
    box_mg_value_release(vc);
    ecs_fini(other);
    ecs_fini(world);
}

void MeshBranch_prune_remove(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 4, 0.5f);
    ecs_entity_t twigs = branches_node(world, "twigs", limbs, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t prune = branch_node(world, "prune", ecs_id(MeshPrune), &(MeshPrune){
        .curves = twigs, .shape = MeshEnvelopeEllipsoid, .center = {0, 2, 0},
        .size = {1, 3, 1}, .level = 1 });
    box_mg_value_t *v = test_eval(world, prune);
    test_int(v->curves.curve_count, 1);
    test_int(v->curves.point_count, 9);
    test_near(curve_attr(&v->curves, "tip")[8], 1, 0);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_prune_shorten(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 4, 0.5f);
    ecs_entity_t twigs = branches_node(world, "twigs", limbs, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t prune = branch_node(world, "prune", ecs_id(MeshPrune), &(MeshPrune){
        .curves = twigs, .shape = MeshEnvelopeEllipsoid, .center = {0, 2, 0},
        .size = {1.2f, 3, 1.2f}, .level = 1, .shorten = true });
    box_mg_value_t *v = test_eval(world, prune);
    const box_mg_curves_t *c = &v->curves;
    const float *level = curve_attr(c, "level"), *pt = curve_attr(c, "parent_t");
    int32_t i;
    /* The limbs are cut at about 1.04 to 1.18 m, which keeps the first twig of each (at 0.5 m)
     * and removes the second (at 1.5 m) */
    test_int(c->curve_count, 1 + 4 + 4);
    for (i = 1; i < c->curve_count; i ++) {
        int32_t first = c->offsets[i], last = c->offsets[i + 1] - 1;
        const float *p = c->positions[last];
        float x = p[0] / 1.2f, y = (p[1] - 2) / 3, z = p[2] / 1.2f;
        test_assert(x * x + y * y + z * z <= 1.0001f);
        if (level[first] == 1) {
            test_near(x * x + y * y + z * z, 1, 1e-3f);
            test_assert(curve_length(c, i) < 1.2f + 1e-3f);
            /* A cut limb tapers to the radius its tip had */
            test_near(c->radii[last], 0.5f * c->radii[first], 1e-5f);
        } else {
            test_near(level[first], 2, 0);
            test_near(level[c->offsets[c->parents[i]]], 1, 0);
            test_near(pt[first], 0.5f / curve_length(c, c->parents[i]), 1e-4f);
        }
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_prune_shapes(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 8, 0.5f);
    ecs_entity_t cone = branch_node(world, "cone", ecs_id(MeshPrune), &(MeshPrune){
        .curves = limbs, .shape = MeshEnvelopeCone, .size = {2, 4, 2}, .level = 1,
        .shorten = true });
    ecs_entity_t cylinder = branch_node(world, "cylinder", ecs_id(MeshPrune), &(MeshPrune){
        .curves = limbs, .shape = MeshEnvelopeCylinder, .size = {1, 4, 1}, .level = 1,
        .shorten = true });
    ecs_entity_t line = test_named(world, "outline");
    ecs_set(world, line, Path2D, { .start = {1, 0},
        .segments = BOX_VEC_VIEW(PathSegment2D, { .kind = PathLine, .end = {1, 5} }) });
    ecs_entity_t outline = branch_node(world, "outline_curve", ecs_id(MeshPath),
        &(MeshPath){ .path = line });
    ecs_entity_t lathe = branch_node(world, "lathe", ecs_id(MeshPrune), &(MeshPrune){
        .curves = limbs, .profile = outline, .level = 1, .shorten = true });
    box_mg_value_t *a = test_eval(world, cone), *b = test_eval(world, cylinder);
    box_mg_value_t *d = test_eval(world, lathe);
    int32_t i;
    test_int(a->curves.curve_count, 9);
    test_int(b->curves.curve_count, 9);
    test_int(d->curves.curve_count, 9);
    for (i = 1; i < 9; i ++) {
        const float *p = a->curves.positions[a->curves.offsets[i + 1] - 1];
        test_near(sqrtf(p[0] * p[0] + p[2] * p[2]), 2 * (1 - p[1] / 4), 1e-3f);
        p = b->curves.positions[b->curves.offsets[i + 1] - 1];
        test_near(sqrtf(p[0] * p[0] + p[2] * p[2]), 1, 1e-3f);
        p = d->curves.positions[d->curves.offsets[i + 1] - 1];
        test_near(sqrtf(p[0] * p[0] + p[2] * p[2]), 1, 1e-3f);
    }

    box_mg_value_release(a);
    box_mg_value_release(b);
    box_mg_value_release(d);
    ecs_fini(world);
}

void MeshBranch_prune_radius(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 4, 0.5f);
    ecs_entity_t twigs = branches_node(world, "twigs", limbs, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t prune = branch_node(world, "prune", ecs_id(MeshPrune), &(MeshPrune){
        .curves = twigs, .min_radius = 0.11f, .level = 1 });
    box_mg_value_t *v = test_eval(world, twigs), *p = test_eval(world, prune);
    int32_t i, kept = 1;
    for (i = 1; i < 5; i ++) {
        kept += v->curves.radii[v->curves.offsets[i]] >= 0.11f ? 1 : 0;
    }

    test_assert(kept > 1 && kept < 5);
    test_int(p->curves.curve_count, kept);
    for (i = 1; i < p->curves.curve_count; i ++) {
        test_assert(p->curves.radii[p->curves.offsets[i]] >= 0.11f);
        test_int(p->curves.parents[i], 0);
    }

    box_mg_value_release(v);
    box_mg_value_release(p);
    ecs_fini(world);
}

void MeshBranch_collar_inside(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.3f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchSpiral, 5, 0.5f);
    ecs_entity_t tube = branch_node(world, "bark", ecs_id(MeshTube), &(MeshTube){
        .curves = limbs, .sides = 12 });
    box_mg_value_t *v = test_eval(world, tube), *c = test_eval(world, limbs);
    const box_mg_mesh_t *m = &v->mesh;
    int32_t ring = 13, i, j, k, base = 9 * ring;
    for (i = 0; i < 5; i ++) {
        float length = curve_length(&c->curves, i + 1);
        int32_t rings = 1;
        while (m->uvs[base + (rings - 1) * ring][1] < length - 1e-4f) {
            rings ++;
        }

        test_assert(rings > 5);
        for (k = 0; k < ring; k ++) {
            const float *p = m->positions[base + k], *n = m->normals[base + k];
            float rp = 0.3f * (1.0f - 0.5f * p[1] / 4.0f);
            float d = sqrtf(p[0] * p[0] + p[2] * p[2]);
            test_assert(d < rp);
            if (d > 1e-3f) {
                vec3 radial = { p[0] / d, 0, p[2] / d };
                test_assert(glm_vec3_dot((float*)n, radial) > 0.99f);
            }
        }

        for (k = 0; k < ring; k ++) {
            const float *p = m->positions[base + (rings - 1) * ring + k];
            test_assert(sqrtf(p[0] * p[0] + p[2] * p[2]) > 0.3f);
        }

        for (j = 1; j < rings; j ++) {
            test_assert(m->uvs[base + j * ring][1] > m->uvs[base + (j - 1) * ring][1]);
            test_assert(m->uvs[base + j * ring][1] - m->uvs[base + (j - 1) * ring][1] <
                0.5f + 1e-4f);
        }

        test_assert(m->uvs[base + ring][1] < 0.5f * 0.5f);
        base += rings * ring;
    }

    test_int(base, m->vertex_count);
    box_mg_value_release(c);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_collar_flare(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.3f, 0);
    ecs_entity_t limbs = branch_node(world, "limbs", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .count = 1, .start = 0.5f, .end = 0.5f, .down_angle = GLM_PI_2f,
        .length = 0.5f, .segments = 20 });
    ecs_entity_t flared = branch_node(world, "flared", ecs_id(MeshTube), &(MeshTube){
        .curves = limbs, .sides = 8 });
    ecs_entity_t plain = branch_node(world, "plain", ecs_id(MeshTube), &(MeshTube){
        .curves = limbs, .sides = 8, .collar = -1, .flare = -1 });
    box_mg_value_t *a = test_eval(world, flared), *b = test_eval(world, plain);
    int32_t ring = 9, base = 9 * ring, j, rings = a->mesh.vertex_count / ring - 9, flared_rings = 0;
    test_int(b->mesh.vertex_count, (9 + 21) * ring);
    test_assert(rings > 21);
    for (j = 0; j < rings; j ++) {
        const float *p = a->mesh.positions[base + j * ring];
        float z = 0.5f * (p[2] + a->mesh.positions[base + j * ring + 4][2]);
        float ra = glm_vec3_distance((float*)p, a->mesh.positions[base + j * ring + 4]);
        int32_t jb = (int32_t)floorf(fabsf(z) / 0.1f + 0.5f);
        const float *q = b->mesh.positions[base + jb * ring];
        float zb = 0.5f * (q[2] + b->mesh.positions[base + jb * ring + 4][2]);
        float rb = glm_vec3_distance((float*)q, b->mesh.positions[base + jb * ring + 4]);
        if (fabsf(z) > 0.32f && fabsf(z) < 0.45f) {
            test_assert(ra > rb * 1.05f);
            flared_rings ++;
        } else if (fabsf(z) > 1.5f) {
            test_near(z, zb, 1e-4f);
            test_near(ra, rb, 1e-5f);
        }
    }

    test_assert(flared_rings > 1);

    box_mg_value_release(a);
    box_mg_value_release(b);
    ecs_fini(world);
}

void MeshBranch_bark_uvs(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.5f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t tube = branch_node(world, "bark", ecs_id(MeshTube), &(MeshTube){
        .curves = limbs, .sides = 8, .square_uv = true });
    box_mg_value_t *v = test_eval(world, tube), *c = test_eval(world, limbs);
    const box_mg_mesh_t *m = &v->mesh;
    int32_t ring = 9, i, j, k, base = 0;
    test_near(m->uvs[8][0], 3, 1e-5f);
    test_near(m->uvs[4][0], 1.5f, 1e-5f);
    for (i = 0; i < 3; i ++) {
        int32_t first = c->curves.offsets[i], count = c->curves.offsets[i + 1] - first;
        float length = curve_length(&c->curves, i);
        float tiles = m->uvs[base + 8][0];
        test_assert(tiles >= 1 && tiles == floorf(tiles));
        test_near(m->uvs[base][1], 0, 0);
        for (j = 1; m->uvs[base + (j - 1) * ring][1] < length - 1e-4f; j ++) {
            float step = m->uvs[base + j * ring][1] - m->uvs[base + (j - 1) * ring][1];
            test_assert(step > 0 && step < length / (float)(count - 1) + 1e-5f);
            for (k = 0; k < ring; k ++) {
                test_near(m->uvs[base + j * ring + k][1], m->uvs[base + j * ring][1], 0);
                test_near(m->uvs[base + j * ring + k][0], m->uvs[base + k][0], 0);
            }
        }

        test_near(m->uvs[base + (j - 1) * ring][1], length, 1e-4f);
        test_assert(i || j == count);
        for (k = 1; k < ring; k ++) {
            test_assert(m->uvs[base + k][0] > m->uvs[base + k - 1][0]);
        }

        base += j * ring;
    }

    test_int(base, m->vertex_count);

    box_mg_value_release(v);
    box_mg_value_release(c);
    ecs_fini(world);
}

void MeshBranch_flutes(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.5f, 0);
    ecs_entity_t tube = branch_node(world, "bark", ecs_id(MeshTube), &(MeshTube){
        .curves = trunk, .sides = 16, .flutes = 4, .flute_depth = 0.5f,
        .flute_height = 1 });
    box_mg_value_t *v = test_eval(world, tube);
    const box_mg_mesh_t *m = &v->mesh;
    int32_t ring = 17, k;
    for (k = 0; k < 16; k ++) {
        const float *p = m->positions[k];
        float r = sqrtf(p[0] * p[0] + p[2] * p[2]);
        if (k % 4 == 0) {
            test_near(r, 0.75f, 1e-4f);
        } else if (k % 4 == 2) {
            test_near(r, 0.5f, 1e-4f);
        }

        p = m->positions[4 * ring + k];
        test_near(sqrtf(p[0] * p[0] + p[2] * p[2]), 0.5f, 1e-4f);
        near3(m->normals[4 * ring + k], (vec3){p[0] / 0.5f, 0, p[2] / 0.5f}, 1e-4f);
    }

    vec3 n;
    glm_vec3_normalize_to(m->normals[1], n);
    vec3 radial = { m->positions[1][0], 0, m->positions[1][2] };
    glm_vec3_normalize(radial);
    test_assert(glm_vec3_dot(n, radial) < 0.99f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_bark_data(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t tube = branch_node(world, "bark", ecs_id(MeshTube), &(MeshTube){
        .curves = limbs, .sides = 6, .caps = true });
    box_mg_value_t *v = test_eval(world, tube), *c = test_eval(world, limbs);
    const box_mg_mesh_t *m = &v->mesh;
    const box_mg_curves_t *cv = &c->curves;
    const float *phase = curve_attr(cv, "phase"), *root = curve_attr(cv, "root_distance");
    int32_t ring = 7, i, j, k, base = 0;
    for (i = 0; i < 3; i ++) {
        int32_t first = cv->offsets[i], count = cv->offsets[i + 1] - first;
        float length = curve_length(cv, i), along = 0;
        for (j = 0; along < length - 1e-4f; j ++) {
            along = m->uvs[base + j * ring][1];
            for (k = 0; k < ring; k ++) {
                int32_t vx = base + j * ring + k;
                /* The pivot snapped to the grid, as its descendants unpack it */
                for (int32_t a = 0; a < 3; a ++) {
                    test_flt(mesh_data(m, BOX_MESH_DATA_PIVOT + a)[vx],
                        sway_snap(cv->positions[first][a]));
                }
                test_near(mesh_data(m, BOX_MESH_DATA_LEVEL)[vx], i ? 1.0f : 0.0f, 0);
                test_near(mesh_data(m, BOX_MESH_DATA_PHASE)[vx], phase[first], 0);
                test_near(mesh_data(m, BOX_MESH_DATA_STIFFNESS)[vx],
                    cv->radii[first] / 0.2f, 1e-6f);
                test_near(mesh_data(m, BOX_MESH_DATA_GROWTH)[vx], root[first] + along, 1e-5f);
                test_near(mesh_data(m, BOX_MESH_DATA_ALONG)[vx], along / length, 1e-5f);
            }
        }

        test_near(mesh_data(m, BOX_MESH_DATA_GROWTH)[base + (j - 1) * ring],
            root[first + count - 1], 1e-5f);
        int32_t end = base + j * ring + 2 * ring;
        test_near(mesh_data(m, BOX_MESH_DATA_ALONG)[base + j * ring], 0, 0);
        test_near(mesh_data(m, BOX_MESH_DATA_ALONG)[end - 1], 1, 1e-5f);
        test_near(mesh_data(m, BOX_MESH_DATA_LEVEL)[end - 1], i ? 1.0f : 0.0f, 0);
        base = end;
    }

    test_int(base, m->vertex_count);
    test_assert(phase[cv->offsets[1]] != phase[cv->offsets[2]]);

    box_mg_value_release(v);
    box_mg_value_release(c);
    ecs_fini(world);
}

void MeshBranch_no_data_without_level(void) {
    ecs_world_t *world = test_world();
    test_rail_t r;
    test_rail(world, 0, &r);
    box_mg_value_t *v = test_eval(world, r.tube);
    test_int(v->mesh.attr_count, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, MeshGraph, { .output = r.tube });
    test_int(test_wait(world, e)->state, MeshGraphReady);
    const box_mesh_t *mesh = test_shape_mesh(world, e);
    test_assert(mesh != NULL);
    test_assert(mesh->data == NULL);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_shape_data(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t tube = branch_node(world, "bark", ecs_id(MeshTube), &(MeshTube){
        .curves = limbs, .sides = 6 });
    ecs_entity_t moved = branch_node(world, "moved", ecs_id(MeshTransform), &(MeshTransform){
        .input = tube, .position = {1, 2, 3}, .scale = {1, 1, 1} });
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, MeshGraph, { .output = moved });
    test_int(test_wait(world, e)->state, MeshGraphReady);
    const box_mesh_t *mesh = test_shape_mesh(world, e);
    box_mg_value_t *v = test_eval(world, moved), *c = test_eval(world, limbs);
    const box_mg_mesh_t *m = &v->mesh;
    int32_t i, k;
    test_assert(mesh != NULL);
    test_assert(mesh->data != NULL);
    test_int(mesh->vertex_count, m->vertex_count);
    for (i = 0; i < m->vertex_count; i ++) {
        for (k = 0; k < 8; k ++) {
            test_near(mesh->data[i * 2 + k / 4][k % 4], mesh_data(m, k)[i], 0);
        }
    }

    const float *p = c->curves.positions[c->curves.offsets[2]];
    int32_t last = m->vertex_count - 1;
    test_flt(mesh_data(m, BOX_MESH_DATA_PIVOT)[last], sway_snap(p[0]) + 1);
    test_flt(mesh_data(m, BOX_MESH_DATA_PIVOT + 1)[last], sway_snap(p[1]) + 2);
    test_flt(mesh_data(m, BOX_MESH_DATA_PIVOT + 2)[last], sway_snap(p[2]) + 3);

    box_mg_value_release(v);
    box_mg_value_release(c);
    ecs_fini(world);
}

/** Tube writes in the channels 8 to 15 the pivots of the ancestors of the branch of every
 * vertex and the stiffnesses of the branch and its ancestors, as box_wind_sway_vertex packs
 * them, so a WindSway with pivots turns a branch of level 3 and 4 with every ancestor; a
 * MeshGraph shape gives them to its mesh as its upper data */
void MeshBranch_sway_chain(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t twigs = branches_node(world, "twigs", limbs, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t shoots = branches_node(world, "shoots", twigs, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t sprigs = branches_node(world, "sprigs", shoots, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t tube = branch_node(world, "bark", ecs_id(MeshTube), &(MeshTube){
        .curves = sprigs, .sides = 4 });
    box_mg_value_t *v = test_eval(world, tube), *c = test_eval(world, sprigs);
    const box_mg_mesh_t *m = &v->mesh;
    const box_mg_curves_t *cv = &c->curves;
    const float *level = curve_attr(cv, "level");
    const float *d[16];
    int32_t i, j, k, counts[5] = {0};
    test_int(cv->curve_count, 1 + 2 + 4 + 8 + 16);
    for (k = 0; k < 16; k ++) {
        d[k] = mesh_data(m, k);
    }

    for (i = 0; i < m->vertex_count; i ++) {
        int32_t curve = -1;
        for (k = 0; k < cv->curve_count && curve < 0; k ++) {
            const float *p = cv->positions[cv->offsets[k]];
            if (sway_snap(p[0]) == d[0][i] && sway_snap(p[1]) == d[1][i] &&
                sway_snap(p[2]) == d[2][i])
            {
                curve = k;
            }
        }

        test_assert(curve >= 0);
        int32_t lv = (int32_t)level[cv->offsets[curve]];
        test_assert(lv >= 0 && lv <= 4);
        test_flt(d[BOX_MESH_DATA_LEVEL][i], (float)lv);
        counts[lv] ++;

        vec4 d2 = { d[8][i], d[9][i], d[10][i], d[11][i] };
        vec4 d3 = { d[12][i], d[13][i], d[14][i], d[15][i] };
        vec3 ancestors[3];
        box_wind_sway_ancestors(d2, d3, ancestors);
        uint32_t packed = (uint32_t)d2[3];
        int32_t at = curve;
        for (j = 0; j < lv; j ++) {
            float expected = cv->radii[cv->offsets[at]] / 0.2f;
            float stiffness = (float)((packed >> (6 * j)) & 63u) / 63.0f;
            test_assert(fabsf(stiffness - expected) <= 0.5f / 63.0f + 1e-5f);
            at = cv->parents[at];
            test_assert(at >= 0);
            test_int((int32_t)level[cv->offsets[at]], lv - j - 1);
            if (j == 0 && lv >= 2) {
                /* The parent pivot, the same value as the pivot of the parent */
                for (k = 0; k < 3; k ++) {
                    test_flt(d2[k], sway_snap(cv->positions[cv->offsets[at]][k]));
                }
            }

            if (j < 3 && lv - j >= 2) {
                for (k = 0; k < 3; k ++) {
                    test_flt(ancestors[j][k], sway_snap(cv->positions[cv->offsets[at]][k]));
                }
            }
        }

        test_assert(packed >> (6 * lv) == 0);
    }

    for (k = 0; k <= 4; k ++) {
        test_assert(counts[k] > 0);
    }

    /* The shape gives the channels 8 to 15 to its mesh as the upper data */
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, MeshGraph, { .output = tube });
    test_int(test_wait(world, e)->state, MeshGraphReady);
    const box_mesh_t *mesh = test_shape_mesh(world, e);
    test_assert(mesh != NULL);
    test_assert(mesh->data != NULL);
    test_assert(mesh->data_upper != NULL);
    test_int(mesh->vertex_count, m->vertex_count);
    for (i = 0; i < m->vertex_count; i ++) {
        for (k = 0; k < 8; k ++) {
            test_flt(mesh->data[i * 2 + k / 4][k % 4], d[k][i]);
            test_flt(mesh->data_upper[i * 2 + k / 4][k % 4], d[8 + k][i]);
        }
    }

    box_mg_value_release(v);
    box_mg_value_release(c);
    ecs_fini(world);
}

/** MeshTransform with a rotation, a scale and an off grid position moves the pivots of the
 * WindSway data so that a branch of level 3 and 4 still unpacks the same grid cells for its
 * grandparent and great grandparent as the vertices of those branches hold for themselves, so
 * the deep branches turn with exactly the turns of their ancestors */
void MeshBranch_sway_chain_transformed(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t twigs = branches_node(world, "twigs", limbs, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t shoots = branches_node(world, "shoots", twigs, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t sprigs = branches_node(world, "sprigs", shoots, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t tube = branch_node(world, "bark", ecs_id(MeshTube), &(MeshTube){
        .curves = sprigs, .sides = 4 });
    ecs_entity_t moved = branch_node(world, "moved", ecs_id(MeshTransform), &(MeshTransform){
        .input = tube, .position = {0.3013f, -1.0007f, 2.1234f}, .rotation = {0.7f, 0.3f, -0.2f},
        .scale = {2.03f, 1.7f, 2.03f} });
    ecs_entity_t twice = branch_node(world, "twice", ecs_id(MeshTransform), &(MeshTransform){
        .input = moved, .position = {-0.0011f, 0.0042f, 0.5003f}, .rotation = {-0.4f, 0, 0.1f},
        .scale = {0.77f, 0.77f, 0.77f} });
    box_mg_value_t *v = test_eval(world, tube), *c = test_eval(world, sprigs);
    box_mg_value_t *outs[2] = { test_eval(world, moved), test_eval(world, twice) };
    const box_mg_mesh_t *m = &v->mesh;
    const box_mg_curves_t *cv = &c->curves;
    const float *level = curve_attr(cv, "level");
    int32_t *curve_of = ecs_os_malloc_n(int32_t, m->vertex_count);
    int32_t *first = ecs_os_malloc_n(int32_t, cv->curve_count);
    int32_t i, j, k, o, deep = 0;
    for (k = 0; k < cv->curve_count; k ++) {
        first[k] = -1;
    }

    /* The branch of every vertex, from its pivot before the transform */
    for (i = 0; i < m->vertex_count; i ++) {
        const float *d0 = mesh_data(m, 0), *d1 = mesh_data(m, 1), *d2 = mesh_data(m, 2);
        curve_of[i] = -1;
        for (k = 0; k < cv->curve_count && curve_of[i] < 0; k ++) {
            const float *p = cv->positions[cv->offsets[k]];
            if (sway_snap(p[0]) == d0[i] && sway_snap(p[1]) == d1[i] &&
                sway_snap(p[2]) == d2[i])
            {
                curve_of[i] = k;
            }
        }

        test_assert(curve_of[i] >= 0);
        if (first[curve_of[i]] < 0) {
            first[curve_of[i]] = i;
        }
    }

    for (o = 0; o < 2; o ++) {
        const box_mg_mesh_t *t = &outs[o]->mesh;
        const float *d[16];
        test_int(t->vertex_count, m->vertex_count);
        for (k = 0; k < 16; k ++) {
            d[k] = mesh_data(t, k);
        }

        for (i = 0; i < t->vertex_count; i ++) {
            int32_t curve = curve_of[i], at = curve;
            int32_t lv = (int32_t)level[cv->offsets[curve]];
            vec4 d2 = { d[8][i], d[9][i], d[10][i], d[11][i] };
            vec4 d3 = { d[12][i], d[13][i], d[14][i], d[15][i] };
            vec3 ancestors[3];
            box_wind_sway_ancestors(d2, d3, ancestors);
            for (k = 0; k < 3; k ++) {
                /* Every vertex of a branch holds the same pivot, on the grid */
                test_flt(d[k][i], d[k][first[curve]]);
                test_flt(d[k][i], sway_snap(d[k][i]));
            }

            /* Each ancestor unpacks to the grid cell of the pivot of its own vertices */
            for (j = 0; j < 3 && lv - j >= 2; j ++) {
                at = cv->parents[at];
                test_assert(at >= 0 && first[at] >= 0);
                for (k = 0; k < 3; k ++) {
                    test_flt(floorf(ancestors[j][k] * BOX_MATERIAL_SWAY_GRID),
                        floorf(d[k][first[at]] * BOX_MATERIAL_SWAY_GRID));
                }
            }

            deep += lv >= 3;
        }
    }

    test_assert(deep > 0);
    ecs_os_free(first);
    ecs_os_free(curve_of);
    box_mg_value_release(outs[1]);
    box_mg_value_release(outs[0]);
    box_mg_value_release(v);
    box_mg_value_release(c);
    ecs_fini(world);
}

void MeshBranch_points_attrs(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t trunk = trunk_node(world, 4, 0.2f, 0.5f);
    ecs_entity_t limbs = branches_node(world, "limbs", trunk, MeshBranchAlternate, 2, 0.5f);
    ecs_entity_t points = branch_node(world, "points", ecs_id(MeshPointsOnCurves),
        &(MeshPointsOnCurves){ .curves = limbs, .count = 3, .end = 1 });
    box_mg_value_t *v = test_eval(world, points);
    const box_mg_points_t *p = &v->points;
    const float *level = box_mg_attr_find(p->attrs, p->attr_count, "level");
    const float *pt = box_mg_attr_find(p->attrs, p->attr_count, "parent_t");
    const float *tip = box_mg_attr_find(p->attrs, p->attr_count, "tip");
    const float *radius = box_mg_attr_find(p->attrs, p->attr_count, "radius");
    test_int(p->count, 9);
    test_assert(level && pt && tip && radius);
    test_near(level[0], 0, 0);
    test_near(tip[2], 0, 0);
    test_near(level[3], 1, 0);
    test_near(pt[3], 0.25f, 1e-5f);
    test_near(tip[3], 0, 0);
    test_near(tip[5], 1, 1e-6f);
    test_near(level[6], 1, 0);
    test_near(pt[6], 0.75f, 1e-5f);
    test_near(tip[8], 1, 1e-6f);

    box_mg_value_release(v);
    ecs_fini(world);
}

void MeshBranch_script_tree(void) {
    ecs_world_t *world = test_world();
    char *path = box_app_locate(BRANCH_TREE);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t tree = ecs_lookup(world, "tree");
    test_assert(tree != 0);
    const MeshGraphStatus *status = test_wait(world, tree);
    if (status->error) {
        printf("%s\n", status->error);
    }

    test_int(status->state, MeshGraphReady);
    test_assert(status->triangles > 500);
    const box_mesh_t *mesh = test_shape_mesh(world, tree);
    test_assert(mesh != NULL);
    test_assert(mesh->data != NULL);
    test_assert(mesh->max[1] > 3.5f && mesh->max[1] < 5.5f);
    test_assert(mesh->min[1] > -0.5f);
    const MeshBranches *twigs = ecs_get(world, ecs_lookup(world, "tree.twigs"), MeshBranches);
    test_assert(twigs != NULL);
    test_int(twigs->distribution, MeshBranchOpposite);
    test_near(twigs->radius_exponent, 1.2f, 1e-6f);
    const MeshBranches *limbs = ecs_get(world, ecs_lookup(world, "tree.limbs"), MeshBranches);
    test_assert(limbs != NULL);
    test_near(limbs->taper, 0.8f, 0);

    ecs_fini(world);
}
