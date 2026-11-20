/**
 * @file MeshFoliage.c
 * @brief Tests for the foliage nodes of box.mesh: Leaves, Needles, Cards, CrownNormals and
 * CrownOcclusion, and the data channels of foliage.
 */

#include "mesh_test.h"

/** A small broadleaf with bent leaf cards as a script */
#define FOLIAGE_BROADLEAF "test/assets/mesh/broadleaf.flecs"

/** A small conifer with spruce fronds as a script */
#define FOLIAGE_CONIFER "test/assets/mesh/conifer.flecs"

/** Create a node.
 *
 * @param world The world
 * @param name The name
 * @param component The node component
 * @param value The node value
 * @return The node
 */
static ecs_entity_t foliage_node(
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

/** Create a straight Stem of 8 segments and length 4 from the origin.
 *
 * @param world The world
 * @param name The name
 * @param direction Its direction
 * @return The node
 */
static ecs_entity_t foliage_stem(
    ecs_world_t *world,
    const char *name,
    vec3 direction)
{
    MeshStem stem = { .length = 4, .segments = 8, .radius = 0.1f, .taper = 0.5f };
    glm_vec3_copy(direction, stem.direction);

    return foliage_node(world, name, ecs_id(MeshStem), &stem);
}

/** Create a vertical trunk with count horizontal branches of 4 segments, each half its length.
 *
 * @param world The world
 * @param count Branches
 * @return The Branches node
 */
static ecs_entity_t foliage_tree(
    ecs_world_t *world,
    int32_t count)
{
    ecs_entity_t trunk = foliage_stem(world, "trunk", (vec3){0, 1, 0});

    return foliage_node(world, "limbs", ecs_id(MeshBranches), &(MeshBranches){
        .curves = trunk, .count = count, .start = 0.3f, .end = 1.0f,
        .distribution = MeshBranchSpiral, .down_angle = GLM_PI_2f, .length = 0.5f,
        .taper = 0.5f, .segments = 4 });
}

/** Get an attribute of points, which must exist.
 *
 * @param p The points
 * @param name The name
 * @return The values
 */
static const float* points_attr(
    const box_mg_points_t *p,
    const char *name)
{
    const float *result = box_mg_attr_find(p->attrs, p->attr_count, name);
    test_assert(result != NULL);

    return result;
}

/** Get a data channel of a mesh, which must exist.
 *
 * @param m The mesh
 * @param channel The channel
 * @return The values
 */
static const float* foliage_data(
    const box_mg_mesh_t *m,
    int32_t channel)
{
    char name[BOX_MESH_ATTR_NAME];
    ecs_os_snprintf(name, ECS_SIZEOF(name), "%s%d", BOX_MESH_DATA_ATTR, channel);
    const float *result = box_mg_attr_find(m->attrs, m->attr_count, name);
    test_assert(result != NULL);

    return result;
}

/** Get the +Y (direction) and +Z (face) of a point.
 *
 * @param p The points
 * @param k The point
 * @param dir Receives the direction
 * @param face Receives the face
 */
static void point_frame(
    const box_mg_points_t *p,
    int32_t k,
    vec3 dir,
    vec3 face)
{
    versor q;
    glm_quat_copy((float*)p->rotations[k], q);
    glm_quat_rotatev(q, (vec3){0, 1, 0}, dir);
    glm_quat_rotatev(q, (vec3){0, 0, 1}, face);
}

/** Leaves places count nodes of per_node leaves on every curve, or density nodes per meter
 * of the range */
void MeshFoliage_leaves_count(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t stem = foliage_stem(world, "stem", (vec3){0, 1, 0});
    ecs_entity_t leaves = foliage_node(world, "leaves", ecs_id(MeshLeaves), &(MeshLeaves){
        .curves = stem, .count = 5, .per_node = 2, .end = 1, .angle = 0.8f, .size = 0.2f });
    box_mg_value_t *v = test_eval(world, leaves);
    test_int(v->type, MeshValuePoints);
    test_int(v->points.count, 10);
    box_mg_value_release(v);

    /* 2 nodes per meter over the upper half of a stem of 4 */
    ecs_set(world, leaves, MeshLeaves, { .curves = stem, .density = 2, .start = 0.5f,
        .end = 1, .angle = 0.8f });
    v = test_eval(world, leaves);
    test_int(v->points.count, 4);
    const float *t = points_attr(&v->points, "t");
    int32_t i;
    for (i = 0; i < v->points.count; i ++) {
        test_assert(t[i] >= 0.5f && t[i] <= 1.0f);
        test_near(v->points.scales[i][1], 0.1f, 1e-5f);
    }

    box_mg_value_release(v);

    /* The defaults of an added component: 6 nodes of one leaf */
    ecs_entity_t added = test_named(world, "added");
    ecs_add(world, added, MeshLeaves);
    MeshLeaves *n = ecs_get_mut(world, added, MeshLeaves);
    test_int(n->count, 6);
    test_flt(n->end, 1);
    test_flt(n->size, 0.1f);
    n->curves = stem;
    ecs_modified(world, added, MeshLeaves);
    v = test_eval(world, added);
    test_int(v->points.count, 6);
    box_mg_value_release(v);
    ecs_fini(world);
}

/** Leaves grow on the twigs (the highest level), at the tips of the childless curves or along
 * every curve of a level and up */
void MeshFoliage_leaves_placement(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t limbs = foliage_tree(world, 4);
    ecs_entity_t leaves = foliage_node(world, "leaves", ecs_id(MeshLeaves), &(MeshLeaves){
        .curves = limbs, .placement = MeshLeafTwigs, .count = 3, .end = 1, .angle = 0.8f });
    box_mg_value_t *v = test_eval(world, leaves);
    test_int(v->points.count, 4 * 3);
    const float *level = points_attr(&v->points, "level");
    const float *curve = points_attr(&v->points, "curve");
    int32_t i;
    for (i = 0; i < v->points.count; i ++) {
        test_flt(level[i], 1);
        test_assert(curve[i] >= 1);
    }

    box_mg_value_release(v);

    ecs_set(world, leaves, MeshLeaves, { .curves = limbs, .placement = MeshLeafLevels,
        .level = 0, .count = 3, .end = 1, .angle = 0.8f });
    v = test_eval(world, leaves);
    test_int(v->points.count, 5 * 3);
    box_mg_value_release(v);

    /* Clusters at the tips of the four limbs, all at the last point of a limb */
    ecs_set(world, leaves, MeshLeaves, { .curves = limbs, .placement = MeshLeafTips,
        .count = 5, .angle = 0.8f });
    v = test_eval(world, leaves);
    test_int(v->points.count, 4 * 5);
    const float *t = points_attr(&v->points, "t");
    for (i = 0; i < v->points.count; i ++) {
        test_flt(t[i], 1);
        if (i % 5) {
            test_near(glm_vec3_distance(v->points.positions[i], v->points.positions[i - 1]),
                0, 1e-5f);
        }
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

/** Leaves leave their twig at angle; the face looks away from the twig, up or out of the
 * crown, and the phyllotaxis turns the nodes around the twig */
void MeshFoliage_leaves_orientation(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t stem = foliage_stem(world, "stem", (vec3){0, 1, 0});
    ecs_entity_t leaves = foliage_node(world, "leaves", ecs_id(MeshLeaves), &(MeshLeaves){
        .curves = stem, .count = 6, .end = 1, .phyllotaxis = GLM_PIf, .angle = 0.8f,
        .orientation = MeshLeafAlong, .size = 0.2f });
    box_mg_value_t *v = test_eval(world, leaves);
    const box_mg_points_t *p = &v->points;
    int32_t i;
    for (i = 0; i < p->count; i ++) {
        vec3 dir, face, out;
        point_frame(p, i, dir, face);
        test_near(glm_vec3_dot(dir, (vec3){0, 1, 0}), cosf(0.8f), 1e-3f);
        test_near(glm_vec3_dot(dir, face), 0, 1e-4f);
        test_near(glm_vec3_norm(face), 1, 1e-4f);
        /* The face looks away from the stem */
        glm_vec3_copy((vec3){dir[0], 0, dir[2]}, out);
        test_assert(glm_vec3_dot(face, out) > 0.0f);
        if (i > 0) {
            vec3 prev, prev_face, a, b;
            point_frame(p, i - 1, prev, prev_face);
            glm_vec3_copy((vec3){prev[0], 0, prev[2]}, a);
            glm_vec3_copy((vec3){dir[0], 0, dir[2]}, b);
            glm_vec3_normalize(a);
            glm_vec3_normalize(b);
            /* Alternate leaves are half a turn apart */
            test_near(glm_vec3_dot(a, b), -1, 1e-3f);
        }
    }

    box_mg_value_release(v);

    ecs_set(world, leaves, MeshLeaves, { .curves = stem, .count = 6, .end = 1,
        .angle = 0.8f, .orientation = MeshLeafFaceUp, .size = 0.2f });
    v = test_eval(world, leaves);
    p = &v->points;
    for (i = 0; i < p->count; i ++) {
        vec3 dir, face;
        point_frame(p, i, dir, face);
        test_near(glm_vec3_dot(dir, face), 0, 1e-4f);
        test_near(glm_vec3_dot(face, (vec3){0, 1, 0}), sinf(0.8f), 1e-3f);
    }

    box_mg_value_release(v);

    /* A horizontal stem, the crown center below it: the faces look up and away from it */
    ecs_entity_t branch = foliage_stem(world, "branch", (vec3){1, 0, 0});
    ecs_set(world, leaves, MeshLeaves, { .curves = branch, .count = 8, .end = 1,
        .per_node = 2, .angle = 0.6f, .orientation = MeshLeafFaceOut, .center = {2, -3, 0},
        .size = 0.2f });
    v = test_eval(world, leaves);
    p = &v->points;
    test_int(p->count, 16);
    for (i = 0; i < p->count; i ++) {
        vec3 dir, face, out;
        point_frame(p, i, dir, face);
        glm_vec3_sub(p->positions[i], (vec3){2, -3, 0}, out);
        test_near(glm_vec3_dot(dir, face), 0, 1e-4f);
        test_assert(glm_vec3_dot(face, out) > 0.0f);
    }

    box_mg_value_release(v);

    /* A random tilt and bank turn the leaves off their rule */
    ecs_set(world, leaves, MeshLeaves, { .curves = stem, .count = 6, .end = 1,
        .angle = 0.8f, .orientation = MeshLeafFaceUp, .tilt = 0.5f, .bank = 0.5f,
        .size = 0.2f, .seed = 3 });
    v = test_eval(world, leaves);
    p = &v->points;
    bool turned = false;
    for (i = 0; i < p->count; i ++) {
        vec3 dir, face;
        point_frame(p, i, dir, face);
        test_near(glm_vec3_dot(dir, face), 0, 1e-4f);
        turned = turned || fabsf(glm_vec3_dot(dir, (vec3){0, 1, 0}) - cosf(0.8f)) > 0.01f;
    }

    test_assert(turned);
    box_mg_value_release(v);
    ecs_fini(world);
}

/** The size of a leaf changes along the range by size_tip and per level below the highest by
 * size_level */
void MeshFoliage_leaves_size(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t stem = foliage_stem(world, "stem", (vec3){0, 1, 0});
    ecs_entity_t leaves = foliage_node(world, "leaves", ecs_id(MeshLeaves), &(MeshLeaves){
        .curves = stem, .count = 4, .end = 1, .angle = 0.8f, .size = 0.2f, .size_tip = 0.5f });
    box_mg_value_t *v = test_eval(world, leaves);
    const box_mg_points_t *p = &v->points;
    test_int(p->count, 4);
    test_near(p->scales[0][0], 0.2f * (1.0f - 0.5f * 0.125f), 1e-5f);
    test_near(p->scales[3][0], 0.2f * (1.0f - 0.5f * 0.875f), 1e-5f);
    test_assert(p->scales[1][0] < p->scales[0][0]);
    test_flt(p->scales[2][1], p->scales[2][0]);
    test_flt(p->scales[2][2], p->scales[2][0]);
    box_mg_value_release(v);

    ecs_entity_t limbs = foliage_tree(world, 2);
    ecs_set(world, leaves, MeshLeaves, { .curves = limbs, .placement = MeshLeafLevels,
        .count = 2, .end = 1, .angle = 0.8f, .size = 0.2f, .size_level = 2 });
    v = test_eval(world, leaves);
    p = &v->points;
    const float *level = points_attr(p, "level");
    int32_t i;
    for (i = 0; i < p->count; i ++) {
        test_near(p->scales[i][0], level[i] == 0 ? 0.4f : 0.2f, 1e-5f);
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

/** The points of leaves carry the pivot of their twig, its level, the distance from the root
 * and a seed and hue per leaf */
void MeshFoliage_leaves_attrs(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t limbs = foliage_tree(world, 3);
    ecs_entity_t leaves = foliage_node(world, "leaves", ecs_id(MeshLeaves), &(MeshLeaves){
        .curves = limbs, .count = 40, .end = 1, .angle = 0.8f, .size = 0.1f, .seed = 9 });
    box_mg_value_t *curves = test_eval(world, limbs);
    box_mg_value_t *v = test_eval(world, leaves);
    const box_mg_curves_t *c = &curves->curves;
    const box_mg_points_t *p = &v->points;
    const float *curve = points_attr(p, "curve"), *t = points_attr(p, "t");
    const float *px = points_attr(p, "pivot_x"), *py = points_attr(p, "pivot_y");
    const float *pz = points_attr(p, "pivot_z"), *root = points_attr(p, "root_distance");
    const float *seed = points_attr(p, "seed"), *hue = points_attr(p, "hue");
    const float *index = points_attr(p, "index");
    int32_t i, low = 0;
    test_int(p->count, 3 * 40);
    for (i = 0; i < p->count; i ++) {
        int32_t ci = (int32_t)curve[i];
        const float *at = c->positions[c->offsets[ci]];
        test_near(px[i], at[0], 1e-5f);
        test_near(py[i], at[1], 1e-5f);
        test_near(pz[i], at[2], 1e-5f);
        test_assert(seed[i] >= 0.0f && seed[i] < 1.0f);
        test_assert(hue[i] >= 0.0f && hue[i] < 1.0f);
        test_flt(index[i], (float)(i % 40));
        if (i % 40) {
            test_assert(root[i] > root[i - 1]);
            test_assert(t[i] > t[i - 1]);
        } else {
            /* A limb starts on the trunk, 1.2 meters up at least */
            test_assert(root[i] > 1.2f);
        }

        low += seed[i] < 0.3f;
    }

    /* The seeds spread evenly, so a threshold thins the crown by its share */
    test_assert(low > 120 * 0.15f && low < 120 * 0.45f);
    box_mg_value_release(v);
    box_mg_value_release(curves);
    ecs_fini(world);
}

/** Needles lays a frond of a fir flat along each part of a twig, and a frond of a spruce as
 * two cards banked to either side */
void MeshFoliage_needles(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t branch = foliage_stem(world, "branch", (vec3){1, 0, 0});
    ecs_entity_t fronds = foliage_node(world, "fronds", ecs_id(MeshNeedles), &(MeshNeedles){
        .curves = branch, .style = MeshNeedleFir, .cards = 2, .end = 1, .width = 0.5f });
    box_mg_value_t *v = test_eval(world, fronds);
    const box_mg_points_t *p = &v->points;
    int32_t i;
    test_int(p->count, 2);
    for (i = 0; i < 2; i ++) {
        vec3 dir, face;
        point_frame(p, i, dir, face);
        test_near(dir[0], 1, 1e-4f);
        test_near(face[1], 1, 1e-4f);
        test_near(p->scales[i][1], 2, 1e-4f);
        test_near(p->scales[i][0], 1, 1e-4f);
        test_near(p->positions[i][0], 2.0f * (float)i, 1e-4f);
    }

    box_mg_value_release(v);

    ecs_set(world, fronds, MeshNeedles, { .curves = branch, .style = MeshNeedleSpruce,
        .cards = 3, .end = 1, .width = 0.4f, .bank = 0.6f });
    v = test_eval(world, fronds);
    p = &v->points;
    test_int(p->count, 6);
    for (i = 0; i < 6; i += 2) {
        vec3 dir, face, dir2, face2;
        point_frame(p, i, dir, face);
        point_frame(p, i + 1, dir2, face2);
        test_near(face[1], cosf(0.6f), 1e-3f);
        test_near(face2[1], cosf(0.6f), 1e-3f);
        test_near(face[2], -face2[2], 1e-3f);
        test_assert(fabsf(face[2]) > 0.5f);
        test_near(glm_vec3_dot(dir, dir2), 1, 1e-4f);
    }

    points_attr(p, "pivot_x");
    points_attr(p, "seed");
    points_attr(p, "hue");
    box_mg_value_release(v);
    ecs_fini(world);
}

/** Cards gives every point a card of the shape, double sided, its stem at the point */
void MeshFoliage_cards_shapes(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t stem = foliage_stem(world, "stem", (vec3){0, 1, 0});
    ecs_entity_t leaves = foliage_node(world, "leaves", ecs_id(MeshLeaves), &(MeshLeaves){
        .curves = stem, .count = 3, .end = 1, .angle = 0.8f, .size = 0.5f });
    ecs_entity_t cards = foliage_node(world, "cards", ecs_id(MeshCards), &(MeshCards){
        .points = leaves, .shape = MeshCardQuad, .double_sided = true, .flutter = 1 });
    box_mg_value_t *pts = test_eval(world, leaves);
    box_mg_value_t *v = test_eval(world, cards);
    test_int(v->type, MeshValueMesh);
    test_int(v->mesh.vertex_count, 3 * 2 * 6);
    test_int(v->mesh.index_count, 3 * 2 * 12);
    int32_t k, i;
    for (k = 0; k < 3; k ++) {
        float far = 0.0f;
        vec3 mid;
        for (i = k * 12; i < (k + 1) * 12; i ++) {
            float d = glm_vec3_distance(v->mesh.positions[i], pts->points.positions[k]);
            far = glm_max(far, d);
        }

        /* The middle of the stem row is on the point, the tip a card length of 0.5 away */
        glm_vec3_center(v->mesh.positions[k * 12], v->mesh.positions[k * 12 + 1], mid);
        test_near(glm_vec3_distance(mid, pts->points.positions[k]), 0, 1e-4f);
        test_assert(far > 0.45f && far < 0.6f);
    }

    /* The back of a card faces the other way */
    test_near(glm_vec3_dot(v->mesh.normals[0], v->mesh.normals[6]), -1, 1e-4f);
    box_mg_value_release(v);

    ecs_set(world, cards, MeshCards, { .points = leaves, .shape = MeshCardBent, .bend = 0.15f,
        .segments = 3 });
    v = test_eval(world, cards);
    test_int(v->mesh.vertex_count, 3 * 3 * 4);
    test_int(v->mesh.index_count, 3 * 2 * 3 * 6);
    box_mg_value_release(v);

    ecs_set(world, cards, MeshCards, { .points = leaves, .shape = MeshCardCross,
        .double_sided = true });
    v = test_eval(world, cards);
    test_int(v->mesh.vertex_count, 3 * 2 * 2 * 6);
    box_mg_value_release(v);

    /* Any mesh as the card */
    ecs_entity_t ball_shape = ecs_entity(world, { .name = "ball_shape" });
    ecs_add_id(world, ball_shape, EcsPrefab);
    ecs_set(world, ball_shape, IcoSphere, { .segments = 1, .smooth = true, .radius = 0.5f });
    ecs_entity_t ball = test_named(world, "ball");
    ecs_set(world, ball, MeshShape, { .entity = ball_shape });
    box_mg_value_t *bv = test_eval(world, ball);
    ecs_set(world, cards, MeshCards, { .points = leaves, .mesh = ball, .double_sided = true });
    v = test_eval(world, cards);
    test_int(v->mesh.vertex_count, 3 * bv->mesh.vertex_count);
    test_int(v->mesh.index_count, 3 * bv->mesh.index_count);
    box_mg_value_release(v);
    box_mg_value_release(bv);
    box_mg_value_release(pts);
    ecs_fini(world);
}

void MeshFoliage_cards_shared_back(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t stem = foliage_stem(world, "stem", (vec3){0, 1, 0});
    ecs_entity_t leaves = foliage_node(world, "leaves", ecs_id(MeshLeaves), &(MeshLeaves){
        .curves = stem, .count = 3, .end = 1, .angle = 0.8f, .size = 0.5f });
    ecs_entity_t cards = foliage_node(world, "cards", ecs_id(MeshCards), &(MeshCards){
        .points = leaves, .shape = MeshCardQuad, .segments = 1, .double_sided = true,
        .shared_back = true, .flutter = 1 });
    box_mg_value_t *v = test_eval(world, cards);
    test_int(v->mesh.vertex_count, 3 * 4);
    test_int(v->mesh.index_count, 3 * 12);
    int32_t k, i;
    for (k = 0; k < 3; k ++) {
        const uint32_t *idx = &v->mesh.indices[k * 12];
        for (i = 0; i < 12; i ++) {
            test_assert(idx[i] >= (uint32_t)(k * 4));
            test_assert(idx[i] < (uint32_t)(k * 4 + 4));
        }

        for (i = 0; i < 6; i += 3) {
            test_int(idx[6 + i], idx[i]);
            test_int(idx[6 + i + 1], idx[i + 2]);
            test_int(idx[6 + i + 2], idx[i + 1]);
        }
    }

    box_mg_value_release(v);
    ecs_fini(world);
}

/** Every vertex of a card holds the pivot and level of its twig, the flutter weight from its
 * stem to its tip, the seed and hue of its card, its growth and an open occlusion */
void MeshFoliage_cards_data(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t limbs = foliage_tree(world, 3);
    ecs_entity_t leaves = foliage_node(world, "leaves", ecs_id(MeshLeaves), &(MeshLeaves){
        .curves = limbs, .count = 4, .end = 1, .angle = 0.8f, .size = 0.3f, .seed = 2 });
    ecs_entity_t cards = foliage_node(world, "cards", ecs_id(MeshCards), &(MeshCards){
        .points = leaves, .shape = MeshCardQuad, .segments = 2, .double_sided = true,
        .flutter = 1 });
    box_mg_value_t *pts = test_eval(world, leaves);
    box_mg_value_t *v = test_eval(world, cards);
    const box_mg_points_t *p = &pts->points;
    const box_mg_mesh_t *m = &v->mesh;
    const float *card = box_mg_attr_find(m->attrs, m->attr_count, "card");
    const float *seed = points_attr(p, "seed"), *hue = points_attr(p, "hue");
    const float *px = points_attr(p, "pivot_x"), *py = points_attr(p, "pivot_y");
    const float *pz = points_attr(p, "pivot_z"), *root = points_attr(p, "root_distance");
    const float *d[8];
    int32_t i;
    test_assert(card != NULL);
    for (i = 0; i < 8; i ++) {
        d[i] = foliage_data(m, i);
    }

    test_int(m->vertex_count, p->count * 12);
    bool stem = false, tip = false;
    for (i = 0; i < m->vertex_count; i ++) {
        int32_t k = i / 12;
        float weight = 2.0f * (d[BOX_MESH_DATA_FLUTTER][i] - floorf(d[BOX_MESH_DATA_FLUTTER][i]));
        test_flt(card[i], (float)k);
        test_flt(d[BOX_MESH_DATA_PIVOT][i], px[k]);
        test_flt(d[BOX_MESH_DATA_PIVOT + 1][i], py[k]);
        test_flt(d[BOX_MESH_DATA_PIVOT + 2][i], pz[k]);
        test_flt(floorf(d[BOX_MESH_DATA_FLUTTER][i]), 1);
        test_flt(d[BOX_MESH_DATA_SEED][i], seed[k]);
        test_flt(d[BOX_MESH_DATA_HUE][i], hue[k]);
        test_flt(d[BOX_MESH_DATA_GROWTH][i], root[k]);
        test_flt(d[BOX_MESH_DATA_OCCLUSION][i], 1);
        test_assert(weight >= 0.0f && weight <= 1.0f);
        /* The flutter weight grows along the card from its stem to its tip */
        vec3 dir, rel;
        point_frame(p, k, dir, rel);
        glm_vec3_sub(m->positions[i], p->positions[k], rel);
        float along = glm_vec3_dot(rel, dir) / p->scales[k][1];
        test_near(weight, along, 1e-3f);
        if (along < 1e-3f) {
            stem = true;
        } else if (along > 0.999f) {
            tip = true;
        }
    }

    test_assert(stem);
    test_assert(tip);

    /* Without the attributes of Leaves the point is the pivot and the seed comes from the
     * index; a smaller flutter weighs the tip less */
    ecs_entity_t stem_curve = foliage_stem(world, "stem", (vec3){0, 1, 0});
    ecs_entity_t on_curve = foliage_node(world, "on_curve", ecs_id(MeshPointsOnCurves),
        &(MeshPointsOnCurves){ .curves = stem_curve, .count = 5, .end = 1 });
    ecs_set(world, cards, MeshCards, { .points = on_curve, .shape = MeshCardQuad,
        .flutter = 0.5f });
    box_mg_value_t *v2 = test_eval(world, cards);
    box_mg_value_t *p2 = test_eval(world, on_curve);
    m = &v2->mesh;
    float most = 0.0f;
    for (i = 0; i < m->vertex_count; i ++) {
        int32_t k = i / 6;
        test_flt(foliage_data(m, BOX_MESH_DATA_PIVOT + 1)[i], p2->points.positions[k][1]);
        test_assert(foliage_data(m, BOX_MESH_DATA_SEED)[i] >= 0.0f);
        most = glm_max(most, foliage_data(m, BOX_MESH_DATA_FLUTTER)[i]);
    }

    test_near(most, 0.25f, 1e-3f);
    box_mg_value_release(p2);
    box_mg_value_release(v2);
    box_mg_value_release(v);
    box_mg_value_release(pts);
    ecs_fini(world);
}

/** Create the leaf cards of a small crown: leaves facing out on the limbs of a tree.
 *
 * @param world The world
 * @return The Cards node
 */
static ecs_entity_t foliage_crown(
    ecs_world_t *world)
{
    ecs_entity_t limbs = foliage_tree(world, 8);
    ecs_entity_t twigs = foliage_node(world, "twigs", ecs_id(MeshBranches), &(MeshBranches){
        .curves = limbs, .count = 4, .start = 0.2f, .end = 1.0f,
        .distribution = MeshBranchOpposite, .down_angle = 0.8f, .length = 0.5f, .taper = 0.5f,
        .segments = 3 });
    ecs_entity_t leaves = foliage_node(world, "leaves", ecs_id(MeshLeaves), &(MeshLeaves){
        .curves = twigs, .count = 4, .per_node = 2, .end = 1, .angle = 0.8f,
        .orientation = MeshLeafFaceOut, .tilt = 0.3f, .bank = 0.3f, .size = 0.3f, .seed = 5 });

    return foliage_node(world, "cards", ecs_id(MeshCards), &(MeshCards){
        .points = leaves, .shape = MeshCardBent, .bend = 0.15f, .double_sided = true,
        .flutter = 1 });
}

/** CrownNormals with a strength of 1 turns every normal to the outward normal of the
 * envelope; a strength of 0 keeps the normals */
void MeshFoliage_crown_normals(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t cards = foliage_crown(world);
    ecs_entity_t normals = foliage_node(world, "normals", ecs_id(MeshCrownNormals),
        &(MeshCrownNormals){ .mesh = cards, .shape = MeshCrownEllipsoid, .strength = 1 });
    box_mg_value_t *in = test_eval(world, cards);
    box_mg_value_t *v = test_eval(world, normals);
    const box_mg_mesh_t *m = &v->mesh;
    vec3 lo, hi, center, half;
    int32_t i, facing = 0, outer = 0;
    test_int(m->vertex_count, in->mesh.vertex_count);
    test_bounds(m->positions, m->vertex_count, lo, hi);
    glm_vec3_center(lo, hi, center);
    glm_vec3_sub(hi, center, half);
    for (i = 0; i < m->vertex_count; i ++) {
        vec3 g;
        int32_t a;
        for (a = 0; a < 3; a ++) {
            g[a] = (m->positions[i][a] - center[a]) / (half[a] * half[a]);
        }

        test_near(glm_vec3_norm(m->normals[i]), 1, 1e-4f);
        if (glm_vec3_norm2(g) > 1e-8f) {
            glm_vec3_normalize(g);
            test_near(glm_vec3_dot(m->normals[i], g), 1, 1e-3f);
        }

        /* Both sides of a card face out */
        test_assert(glm_vec3_dot(m->normals[i], g) >= 0.0f);
    }

    /* The positions and the data are kept */
    test_assert(!memcmp(m->positions, in->mesh.positions,
        sizeof(vec3) * (size_t)m->vertex_count));
    test_assert(!memcmp(foliage_data(m, BOX_MESH_DATA_SEED),
        foliage_data(&in->mesh, BOX_MESH_DATA_SEED), sizeof(float) * (size_t)m->vertex_count));
    box_mg_value_release(v);

    /* The smoothed shape of the leaves: the normals of the outer leaves face away from the
     * middle of the crown */
    ecs_set(world, normals, MeshCrownNormals, { .mesh = cards, .shape = MeshCrownDensity,
        .strength = 1 });
    v = test_eval(world, normals);
    m = &v->mesh;
    test_bounds(m->positions, m->vertex_count, lo, hi);
    glm_vec3_center(lo, hi, center);
    float reach = 0.5f * glm_vec3_distance(lo, hi);
    for (i = 0; i < m->vertex_count; i ++) {
        vec3 out;
        glm_vec3_sub(m->positions[i], center, out);
        if (glm_vec3_norm(out) > 0.6f * reach) {
            outer ++;
            facing += glm_vec3_dot(m->normals[i], out) > 0.0f;
        }
    }

    test_assert(outer > 0);
    test_assert(facing > outer * 9 / 10);
    box_mg_value_release(v);

    ecs_set(world, normals, MeshCrownNormals, { .mesh = cards, .strength = 0 });
    v = test_eval(world, normals);
    test_assert(!memcmp(v->mesh.normals, in->mesh.normals,
        sizeof(vec3) * (size_t)in->mesh.vertex_count));
    box_mg_value_release(v);
    box_mg_value_release(in);
    ecs_fini(world);
}

/** CrownOcclusion darkens the vertices deeper in the crown: with an ellipsoid by their
 * distance from its center, with the density by the leaves outside them */
void MeshFoliage_crown_occlusion(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t cards = foliage_crown(world);
    ecs_entity_t occlusion = foliage_node(world, "occlusion", ecs_id(MeshCrownOcclusion),
        &(MeshCrownOcclusion){ .mesh = cards, .shape = MeshCrownEllipsoid, .center = {0, 2.6f, 0},
            .size = {2.5f, 2, 2.5f}, .strength = 0.8f });
    box_mg_value_t *in = test_eval(world, cards);
    box_mg_value_t *v = test_eval(world, occlusion);
    const box_mg_mesh_t *m = &v->mesh;
    const float *occ = foliage_data(m, BOX_MESH_DATA_OCCLUSION);
    const float *seed = foliage_data(m, BOX_MESH_DATA_SEED);
    const float *seed_in = foliage_data(&in->mesh, BOX_MESH_DATA_SEED);
    int32_t i, j;
    for (i = 0; i < m->vertex_count; i ++) {
        vec3 q = { m->positions[i][0] / 2.5f, (m->positions[i][1] - 2.6f) / 2.0f,
            m->positions[i][2] / 2.5f };
        float depth = glm_clamp(1.0f - glm_vec3_norm(q), 0.0f, 1.0f);
        test_near(occ[i], 1.0f - 0.8f * depth, 1e-4f);
        test_flt(seed[i], seed_in[i]);
    }

    /* Nearer the center is darker */
    for (i = 0; i < m->vertex_count; i += 37) {
        for (j = 0; j < m->vertex_count; j += 41) {
            vec3 a = { m->positions[i][0] / 2.5f, (m->positions[i][1] - 2.6f) / 2.0f,
                m->positions[i][2] / 2.5f };
            vec3 b = { m->positions[j][0] / 2.5f, (m->positions[j][1] - 2.6f) / 2.0f,
                m->positions[j][2] / 2.5f };
            if (glm_vec3_norm(a) + 0.05f < glm_vec3_norm(b) && glm_vec3_norm(a) < 0.95f) {
                test_assert(occ[i] < occ[j]);
            }
        }
    }

    box_mg_value_release(v);

    /* The density of the leaves: the inner third is darker than the outer third */
    ecs_set(world, occlusion, MeshCrownOcclusion, { .mesh = cards, .shape = MeshCrownDensity,
        .strength = 0.8f, .absorption = 0.5f });
    v = test_eval(world, occlusion);
    m = &v->mesh;
    occ = foliage_data(m, BOX_MESH_DATA_OCCLUSION);
    vec3 lo, hi, center;
    test_bounds(m->positions, m->vertex_count, lo, hi);
    glm_vec3_center(lo, hi, center);
    float reach = 0.5f * glm_vec3_distance(lo, hi), inner = 0, outer = 0;
    int32_t inner_n = 0, outer_n = 0;
    for (i = 0; i < m->vertex_count; i ++) {
        float r = glm_vec3_distance(m->positions[i], center) / reach;
        test_assert(occ[i] >= 0.2f - 1e-4f && occ[i] <= 1.0f);
        if (r < 0.33f) {
            inner += occ[i];
            inner_n ++;
        } else if (r > 0.66f) {
            outer += occ[i];
            outer_n ++;
        }
    }

    test_assert(inner_n > 0 && outer_n > 0);
    test_assert(inner / (float)inner_n < outer / (float)outer_n);
    box_mg_value_release(v);
    box_mg_value_release(in);
    ecs_fini(world);
}

/** Build a crown with leaves, cards, crown normals and occlusion in a new world and evaluate
 * it.
 *
 * @param seed Seed of the leaves
 * @return The world, whose node named "foliage" is the end of the graph
 */
static ecs_world_t* foliage_world(
    int32_t seed)
{
    ecs_world_t *world = test_world();
    ecs_entity_t limbs = foliage_tree(world, 5);
    ecs_entity_t leaves = foliage_node(world, "leaves", ecs_id(MeshLeaves), &(MeshLeaves){
        .curves = limbs, .count = 6, .end = 1, .angle = 0.8f, .orientation = MeshLeafFaceUp,
        .tilt = 0.4f, .bank = 0.4f, .size = 0.3f, .size_variation = 0.3f, .seed = seed });
    ecs_entity_t fronds = foliage_node(world, "fronds", ecs_id(MeshNeedles), &(MeshNeedles){
        .curves = limbs, .style = MeshNeedleSpruce, .cards = 2, .end = 1, .width = 0.5f,
        .bank = 0.5f, .tilt = 0.2f, .seed = seed });
    ecs_entity_t both = foliage_node(world, "both", ecs_id(MeshMerge), &(MeshMerge){
        .inputs = BOX_VEC_VIEW(ecs_entity_t, leaves, fronds) });
    ecs_entity_t cards = foliage_node(world, "cards", ecs_id(MeshCards), &(MeshCards){
        .points = both, .shape = MeshCardBent, .bend = 0.1f, .double_sided = true,
        .flutter = 1 });
    ecs_entity_t normals = foliage_node(world, "normals", ecs_id(MeshCrownNormals),
        &(MeshCrownNormals){ .mesh = cards, .shape = MeshCrownDensity, .strength = 0.7f });
    foliage_node(world, "foliage", ecs_id(MeshCrownOcclusion), &(MeshCrownOcclusion){
        .mesh = normals, .shape = MeshCrownDensity, .strength = 0.7f });

    return world;
}

/** The same graph gives the same foliage in another world, another seed other foliage */
void MeshFoliage_deterministic(void) {
    ecs_world_t *a = foliage_world(7), *b = foliage_world(7), *c = foliage_world(8);
    box_mg_value_t *va = test_eval(a, ecs_lookup(a, "foliage"));
    box_mg_value_t *vb = test_eval(b, ecs_lookup(b, "foliage"));
    box_mg_value_t *vc = test_eval(c, ecs_lookup(c, "foliage"));
    const box_mg_mesh_t *ma = &va->mesh, *mb = &vb->mesh;
    int32_t i;
    test_assert(ma->vertex_count > 0);
    test_int(ma->vertex_count, mb->vertex_count);
    test_int(ma->index_count, mb->index_count);
    test_int(ma->attr_count, mb->attr_count);
    test_assert(!memcmp(ma->positions, mb->positions, sizeof(vec3) * (size_t)ma->vertex_count));
    test_assert(!memcmp(ma->normals, mb->normals, sizeof(vec3) * (size_t)ma->vertex_count));
    test_assert(!memcmp(ma->indices, mb->indices,
        sizeof(uint32_t) * (size_t)ma->index_count));
    for (i = 0; i < ma->attr_count; i ++) {
        test_str(ma->attrs[i].name, mb->attrs[i].name);
        test_assert(!memcmp(ma->attrs[i].values, mb->attrs[i].values,
            sizeof(float) * (size_t)ma->vertex_count));
    }

    test_int(vc->mesh.vertex_count, ma->vertex_count);
    test_assert(memcmp(ma->positions, vc->mesh.positions,
        sizeof(vec3) * (size_t)ma->vertex_count));
    box_mg_value_release(va);
    box_mg_value_release(vb);
    box_mg_value_release(vc);
    ecs_fini(a);
    ecs_fini(b);
    ecs_fini(c);
}

/** Run a script of a plant with bark and foliage shapes and check the foliage mesh.
 *
 * @param file The script
 * @param name Name of the plant entity
 */
static void foliage_script(
    const char *file,
    const char *name)
{
    ecs_world_t *world = test_world();
    char *path = box_app_locate(file);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t plant = ecs_lookup(world, name);
    test_assert(plant != 0);
    ecs_entity_t bark = ecs_lookup_child(world, plant, "bark");
    ecs_entity_t crown = ecs_lookup_child(world, plant, "crown");
    test_assert(bark != 0);
    test_assert(crown != 0);
    const MeshGraphStatus *s = test_wait(world, crown);
    if (s->error) {
        printf("%s\n", s->error);
    }

    test_int(s->state, MeshGraphReady);
    test_assert(s->vertices > 0);
    test_int(test_wait(world, bark)->state, MeshGraphReady);
    const box_mesh_t *mesh = test_shape_mesh(world, crown);
    test_assert(mesh != NULL);
    test_assert(mesh->data != NULL);
    ecs_fini(world);
}

/** A broadleaf and a conifer graph from scripts dress their skeletons */
void MeshFoliage_script_crowns(void) {
    foliage_script(FOLIAGE_BROADLEAF, "broadleaf");
    foliage_script(FOLIAGE_CONIFER, "conifer");
}

/** The node kinds of foliage are registered with their inputs and outputs */
void MeshFoliage_node_kinds(void) {
    ecs_world_t *world = test_world();
    const MeshNodeKind *leaves = ecs_get(world, ecs_id(MeshLeaves), MeshNodeKind);
    test_assert(leaves != NULL);
    test_int(leaves->output, MeshValuePoints);
    test_assert(leaves->random);
    test_int(leaves->param_count, 20);
    test_int(ecs_get(world, ecs_id(MeshNeedles), MeshNodeKind)->output, MeshValuePoints);
    const MeshNodeKind *cards = ecs_get(world, ecs_id(MeshCards), MeshNodeKind);
    test_int(cards->output, MeshValueMesh);
    test_int(cards->input_count, 2);
    test_assert(cards->inputs[1].optional);
    test_int(ecs_get(world, ecs_id(MeshCrownNormals), MeshNodeKind)->output, MeshValueMesh);
    test_int(ecs_get(world, ecs_id(MeshCrownOcclusion), MeshNodeKind)->output, MeshValueMesh);
    test_assert(ecs_lookup(world, "box.mesh.nodes.Leaves") == ecs_id(MeshLeaves));
    test_assert(ecs_lookup(world, "box.mesh.nodes.Cards") == ecs_id(MeshCards));
    test_assert(ecs_lookup(world, "box.mesh.nodes.CrownNormals") == ecs_id(MeshCrownNormals));

    ecs_entity_t e = test_named(world, "defaults");
    ecs_add(world, e, MeshCards);
    test_assert(ecs_get(world, e, MeshCards)->double_sided);
    test_flt(ecs_get(world, e, MeshCards)->flutter, 1);
    ecs_add(world, e, MeshCrownNormals);
    test_flt(ecs_get(world, e, MeshCrownNormals)->strength, 0.75f);
    ecs_add(world, e, MeshCrownOcclusion);
    test_flt(ecs_get(world, e, MeshCrownOcclusion)->strength, 0.8f);
    ecs_add(world, e, MeshNeedles);
    test_flt(ecs_get(world, e, MeshNeedles)->width, 0.5f);
    ecs_fini(world);
}
