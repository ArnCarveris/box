/**
 * @file MeshLod.c
 * @brief Tests for the levels of detail of mesh graphs: the reductions of the node kinds by
 * level, the Detail node, the levels a MeshGraph shape builds and reports, the cache the levels
 * share and the variants the shape gives the mesh cache.
 */

#include "mesh_test.h"

/** A small broadleaf whose bark and crown shapes build four levels of detail */
#define LOD_TREE "test/assets/mesh/lod_tree.flecs"

/** Create a node.
 *
 * @param world The world
 * @param name The name
 * @param component The node component
 * @param value The node value
 * @return The node
 */
static ecs_entity_t lod_node(
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

/** Create a vertical Stem of radius 0.2 from the origin.
 *
 * @param world The world
 * @param name The name
 * @param segments Its segments
 * @return The node
 */
static ecs_entity_t lod_stem(
    ecs_world_t *world,
    const char *name,
    int32_t segments)
{
    return lod_node(world, name, ecs_id(MeshStem), &(MeshStem){
        .direction = {0, 1, 0}, .length = 4, .segments = segments, .radius = 0.2f,
        .taper = 0.8f });
}

/** Run the script of the tree of levels in a new world.
 *
 * @return The world, with the entity lod_tree
 */
static ecs_world_t* lod_tree_world(void)
{
    ecs_world_t *world = test_world();
    char *path = box_app_locate(LOD_TREE);
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    test_assert(ecs_lookup(world, "lod_tree") != 0);

    return world;
}

/** Find a node of the tree of levels.
 *
 * @param world The world
 * @param name Name of the node
 * @return The node
 */
static ecs_entity_t lod_tree_node(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t result = ecs_lookup_child(world, ecs_lookup(world, "lod_tree"), name);
    test_assert(result != 0);

    return result;
}

/** Get the triangles of the mesh of a node at a level.
 *
 * @param world The world
 * @param node The node
 * @param level The level
 * @return The triangles
 */
static int32_t lod_triangles(
    ecs_world_t *world,
    ecs_entity_t node,
    int32_t level)
{
    box_mg_value_t *v = box_mesh_graph_evaluate_level(world, node, level, NULL);
    test_assert(v != NULL);
    int32_t result = v->mesh.index_count / 3;
    box_mg_value_release(v);

    return result;
}

/** Sum the areas of the triangles of a mesh.
 *
 * @param m The mesh
 * @return The area
 */
static double lod_mesh_area(
    const box_mg_mesh_t *m)
{
    double result = 0.0;
    int32_t i;
    for (i = 0; i + 2 < m->index_count; i += 3) {
        vec3 ab, ac, n;
        glm_vec3_sub(m->positions[m->indices[i + 1]], m->positions[m->indices[i]], ab);
        glm_vec3_sub(m->positions[m->indices[i + 2]], m->positions[m->indices[i]], ac);
        glm_vec3_cross(ab, ac, n);
        result += 0.5 * (double)glm_vec3_norm(n);
    }

    return result;
}

/** Sum the areas of the cards of points: their x scale times their y scale.
 *
 * @param p The points
 * @return The area
 */
static double lod_points_area(
    const box_mg_points_t *p)
{
    double result = 0.0;
    int32_t k;
    for (k = 0; k < p->count; k ++) {
        result += fabs((double)p->scales[k][0] * (double)p->scales[k][1]);
    }

    return result;
}

/** A tube of 12 sides along a stem of 10 segments loses sides and rings by level: 0.6 of each
 * per level, at least 3 sides and one segment */
void MeshLod_tube_levels(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t stem = lod_stem(world, "stem", 10);
    ecs_entity_t tube = lod_node(world, "tube", ecs_id(MeshTube), &(MeshTube){
        .curves = stem, .sides = 12 });
    test_int(lod_triangles(world, tube, 0), 10 * 12 * 2);
    test_int(lod_triangles(world, tube, 1), 6 * 7 * 2);
    test_int(lod_triangles(world, tube, 2), 4 * 4 * 2);
    test_int(lod_triangles(world, tube, 3), 2 * 3 * 2);
    ecs_fini(world);
}

/** A node whose reductions are switched off has the hash of its full detail at every level, so
 * the levels share its value */
void MeshLod_reductions_off(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t stem = lod_stem(world, "stem", 10);
    ecs_entity_t tube = lod_node(world, "tube", ecs_id(MeshTube), &(MeshTube){
        .curves = stem, .sides = 12, .lod_sides = -1, .lod_rings = -1, .lod_thin = -1 });
    box_mg_value_t *full = test_eval(world, tube);
    box_mg_value_t *far = box_mesh_graph_evaluate_level(world, tube, 3, NULL);
    test_assert(far == full);
    test_int(far->mesh.index_count / 3, 10 * 12 * 2);
    box_mg_value_release(far);
    box_mg_value_release(full);

    box_mg_value_t *stem0 = test_eval(world, stem);
    box_mg_value_t *stem2 = box_mesh_graph_evaluate_level(world, stem, 2, NULL);
    test_assert(stem0 == stem2);
    box_mg_value_release(stem0);
    box_mg_value_release(stem2);
    ecs_fini(world);
}

/** A Detail node is the input of its level, the last input for the levels after it, and shares
 * the value of that input */
void MeshLod_detail_node(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t stem = lod_stem(world, "stem", 4);
    ecs_entity_t fine = lod_node(world, "fine", ecs_id(MeshTube), &(MeshTube){
        .curves = stem, .sides = 12, .lod_sides = -1, .lod_rings = -1, .lod_thin = -1 });
    ecs_entity_t coarse = lod_node(world, "coarse", ecs_id(MeshTube), &(MeshTube){
        .curves = stem, .sides = 4, .lod_sides = -1, .lod_rings = -1, .lod_thin = -1 });
    ecs_entity_t detail = lod_node(world, "detail", ecs_id(MeshDetail), &(MeshDetail){
        .levels = BOX_VEC_VIEW(ecs_entity_t, fine, coarse) });
    test_int(lod_triangles(world, detail, 0), 4 * 12 * 2);
    test_int(lod_triangles(world, detail, 1), 4 * 4 * 2);
    test_int(lod_triangles(world, detail, 3), 4 * 4 * 2);

    box_mg_value_t *a = test_eval(world, fine);
    box_mg_value_t *b = test_eval(world, detail);
    test_assert(a == b);
    box_mg_value_release(a);
    box_mg_value_release(b);

    ecs_entity_t shape = test_named(world, "shape");
    ecs_set(world, shape, MeshGraph, { .output = detail, .levels = 3 });
    const MeshGraphStatus *s = test_wait(world, shape);
    test_int(s->state, MeshGraphReady);
    test_int(s->levels, 3);
    test_int(s->level_triangles[0], 4 * 12 * 2);
    test_int(s->level_triangles[1], 4 * 4 * 2);
    test_int(s->level_triangles[2], 4 * 4 * 2);
    test_int(s->triangles, s->level_triangles[0]);
    test_int(s->vertices, s->level_vertices[0]);

    ecs_entity_t empty = lod_node(world, "empty", ecs_id(MeshDetail), &(MeshDetail){0});
    char *error = NULL;
    test_assert(box_mesh_graph_evaluate(world, empty, &error) == NULL);
    test_assert(error != NULL);
    test_assert(strstr(error, "levels") != NULL);
    ecs_os_free(error);
    ecs_fini(world);
}

/** Branches and Prune drop the curves thinner than a fraction of the stems: they keep their
 * place with the attribute dropped, and Tube gives them no bark */
void MeshLod_drop(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t stem = lod_stem(world, "stem", 12);
    ecs_entity_t kept = lod_node(world, "kept", ecs_id(MeshBranches), &(MeshBranches){
        .curves = stem, .count = 4, .end = 1, .down_angle = 0.8f, .length = 0.5f,
        .radius_exponent = 2, .taper = 0.8f, .segments = 6 });
    ecs_entity_t some = lod_node(world, "some", ecs_id(MeshBranches), &(MeshBranches){
        .curves = stem, .count = 4, .end = 1, .down_angle = 0.8f, .length = 0.5f,
        .radius_exponent = 2, .taper = 0.8f, .segments = 6, .drop = 0.35f });
    ecs_entity_t all = lod_node(world, "all", ecs_id(MeshBranches), &(MeshBranches){
        .curves = stem, .count = 4, .end = 1, .down_angle = 0.8f, .length = 0.5f,
        .radius_exponent = 2, .taper = 0.8f, .segments = 6, .drop = 1 });
    ecs_entity_t pruned = lod_node(world, "pruned", ecs_id(MeshPrune), &(MeshPrune){
        .curves = kept, .size = {1, 1, 1}, .level = 1, .drop = 1 });

    box_mg_value_t *v = test_eval(world, some);
    const box_mg_curves_t *c = &v->curves;
    const float *dropped = box_mg_attr_find(c->attrs, c->attr_count, "dropped");
    test_assert(dropped != NULL);
    int32_t i, count = 0;
    test_int(c->curve_count, 5);
    test_assert(dropped[c->offsets[0]] == 0);
    for (i = 1; i < c->curve_count; i ++) {
        count += dropped[c->offsets[i]] > 0.5f;
    }

    printf("dropped %d of %d branches\n", count, c->curve_count - 1);
    test_assert(count > 0);
    test_assert(count < c->curve_count - 1);
    box_mg_value_release(v);

    ecs_entity_t tube_kept = lod_node(world, "tube_kept", ecs_id(MeshTube), &(MeshTube){
        .curves = kept, .sides = 8, .lod_sides = -1, .lod_rings = -1, .lod_thin = -1 });
    ecs_entity_t tube_some = lod_node(world, "tube_some", ecs_id(MeshTube), &(MeshTube){
        .curves = some, .sides = 8, .lod_sides = -1, .lod_rings = -1, .lod_thin = -1 });
    ecs_entity_t tube_all = lod_node(world, "tube_all", ecs_id(MeshTube), &(MeshTube){
        .curves = all, .sides = 8, .lod_sides = -1, .lod_rings = -1, .lod_thin = -1 });
    ecs_entity_t tube_pruned = lod_node(world, "tube_pruned", ecs_id(MeshTube), &(MeshTube){
        .curves = pruned, .sides = 8, .lod_sides = -1, .lod_rings = -1, .lod_thin = -1 });
    int32_t t_kept = lod_triangles(world, tube_kept, 0);
    int32_t t_some = lod_triangles(world, tube_some, 0);
    test_assert(t_some < t_kept);
    test_int(lod_triangles(world, tube_all, 0), 12 * 8 * 2);
    test_int(lod_triangles(world, tube_pruned, 0), 12 * 8 * 2);
    ecs_fini(world);
}

/** Leaves keep fewer cards per level, about half each level, grown so that the area of the
 * cards stays that of the full crown, and the bounds of the crown stay close */
void MeshLod_leaf_coverage(void) {
    ecs_world_t *world = lod_tree_world();
    ecs_entity_t leaves = lod_tree_node(world, "leaves");
    ecs_entity_t cards = lod_tree_node(world, "cards");
    box_mg_value_t *p0 = test_eval(world, leaves);
    box_mg_value_t *m0 = test_eval(world, cards);
    double points0 = lod_points_area(&p0->points), area0 = lod_mesh_area(&m0->mesh);
    vec3 min0, max0;
    int32_t l;
    test_bounds(m0->mesh.positions, m0->mesh.vertex_count, min0, max0);
    test_assert(p0->points.count > 100);
    for (l = 1; l < 4; l ++) {
        box_mg_value_t *p = box_mesh_graph_evaluate_level(world, leaves, l, NULL);
        box_mg_value_t *m = box_mesh_graph_evaluate_level(world, cards, l, NULL);
        test_assert(p != NULL && m != NULL);
        double expect = (double)p0->points.count / (double)(1 << l);
        double points = lod_points_area(&p->points), area = lod_mesh_area(&m->mesh);
        vec3 min, max;
        test_bounds(m->mesh.positions, m->mesh.vertex_count, min, max);
        printf("level %d: %d leaves of %d, point area %f of %f, card area %f of %f\n", l,
            p->points.count, p0->points.count, points, points0, area, area0);
        test_assert((double)p->points.count > 0.6 * expect);
        test_assert((double)p->points.count < 1.4 * expect);
        test_assert(fabs(points - points0) < 1e-3 * points0);
        test_assert(fabs(area - area0) < 0.2 * area0);
        for (int32_t k = 0; k < 3; k ++) {
            float extent0 = max0[k] - min0[k], extent = max[k] - min[k];
            printf("  extent %d: %f of %f\n", k, (double)extent, (double)extent0);
            test_assert(fabsf(extent - extent0) < 0.3f * extent0);
        }

        box_mg_value_release(p);
        box_mg_value_release(m);
    }

    box_mg_value_release(p0);
    box_mg_value_release(m0);
    ecs_fini(world);
}

/** The levels of a tree evaluate the nodes no level changes once: the trunk is a cache hit at
 * every level, and the graph evaluates fewer nodes than one evaluation per level would */
void MeshLod_shared_nodes(void) {
    ecs_world_t *world = lod_tree_world();
    ecs_entity_t trunk = lod_tree_node(world, "trunk");
    ecs_entity_t bark = lod_tree_node(world, "bark_mesh");
    box_mesh_graph_stats_t s0, s1, s2;
    box_mg_value_t *v0 = test_eval(world, bark);
    box_mesh_graph_stats(world, &s0);
    test_int(s0.evaluated, 4);

    box_mg_value_t *t0 = test_eval(world, trunk);
    box_mg_value_t *t1 = box_mesh_graph_evaluate_level(world, trunk, 1, NULL);
    test_assert(t0 == t1);
    box_mesh_graph_stats(world, &s1);
    test_int(s1.evaluated, s0.evaluated);
    test_assert(s1.hits > s0.hits);

    box_mg_value_t *v1 = box_mesh_graph_evaluate_level(world, bark, 1, NULL);
    test_assert(v1 != NULL && v1 != v0);
    box_mesh_graph_stats(world, &s2);
    test_assert(s2.evaluated - s1.evaluated <= 3);
    test_assert(s2.hits > s1.hits);
    box_mg_value_release(v0);
    box_mg_value_release(v1);
    box_mg_value_release(t0);
    box_mg_value_release(t1);
    ecs_fini(world);

    /* The shapes: bark and crown share the trunk, limbs and twigs, and every level the trunk */
    world = lod_tree_world();
    ecs_entity_t bark_shape = lod_tree_node(world, "bark");
    ecs_entity_t crown_shape = lod_tree_node(world, "crown");
    test_int(test_wait(world, bark_shape)->state, MeshGraphReady);
    test_int(test_wait(world, crown_shape)->state, MeshGraphReady);
    box_mesh_graph_stats(world, &s0);
    printf("evaluated %d nodes for 4 levels of 8 nodes\n", s0.evaluated);
    test_assert(s0.evaluated <= 8 + 3 * 7);
    test_assert(s0.evaluated > 8);
    ecs_fini(world);
}

/** The bark and the crown of a tree report each level, whose triangles drop by the reductions:
 * the crown by about half its cards and half the rows of a card at the first level, then half
 * its cards per level, so the tree at its last level costs a small fraction of its full mesh */
void MeshLod_tree_levels(void) {
    ecs_world_t *world = lod_tree_world();
    ecs_entity_t bark = lod_tree_node(world, "bark");
    ecs_entity_t crown = lod_tree_node(world, "crown");
    const MeshGraphStatus *sb = test_wait(world, bark);
    test_int(sb->state, MeshGraphReady);
    MeshGraphStatus b = *sb;
    const MeshGraphStatus *sc = test_wait(world, crown);
    test_int(sc->state, MeshGraphReady);
    MeshGraphStatus c = *sc;
    int32_t l;
    test_int(b.levels, 4);
    test_int(c.levels, 4);
    test_int(b.triangles, b.level_triangles[0]);
    test_int(c.triangles, c.level_triangles[0]);
    for (l = 0; l < 4; l ++) {
        printf("level %d: bark %d triangles %d vertices, crown %d triangles %d vertices\n", l,
            b.level_triangles[l], b.level_vertices[l], c.level_triangles[l],
            c.level_vertices[l]);
        test_assert(b.level_triangles[l] > 0);
        test_assert(c.level_triangles[l] > 0);
        test_assert(b.level_vertices[l] > 0);
        test_assert(c.level_vertices[l] > 0);
        if (l) {
            test_assert(b.level_triangles[l] < b.level_triangles[l - 1]);
            test_assert(c.level_triangles[l] < c.level_triangles[l - 1]);
        }
    }

    float c0 = (float)c.level_triangles[0];
    test_assert((float)c.level_triangles[1] > 0.15f * c0);
    test_assert((float)c.level_triangles[1] < 0.35f * c0);
    test_assert((float)c.level_triangles[2] > 0.06f * c0);
    test_assert((float)c.level_triangles[2] < 0.2f * c0);
    test_assert((float)c.level_triangles[3] > 0.03f * c0);
    test_assert((float)c.level_triangles[3] < 0.1f * c0);
    test_assert(b.level_triangles[1] < b.level_triangles[0] * 6 / 10);
    float full = (float)(b.level_triangles[0] + c.level_triangles[0]);
    float far = (float)(b.level_triangles[3] + c.level_triangles[3]);
    printf("the last level costs %f of the full tree\n", (double)(far / full));
    test_assert(far < 0.15f * full);
    ecs_fini(world);
}

/** A MeshGraph with levels gives the mesh cache a variant per level, with the triangles of its
 * status; one without levels has none */
void MeshLod_shape_variants(void) {
    ecs_world_t *world = test_world();
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t stem = lod_stem(world, "stem", 10);
    ecs_entity_t tube = lod_node(world, "tube", ecs_id(MeshTube), &(MeshTube){
        .curves = stem, .sides = 12 });
    ecs_entity_t shape = test_named(world, "shape");
    ecs_set(world, shape, MeshGraph, { .output = tube, .levels = 4 });
    ecs_entity_t plain = test_named(world, "plain");
    ecs_set(world, plain, MeshGraph, { .output = tube });
    const MeshGraphStatus *s = test_wait(world, shape);
    test_int(s->state, MeshGraphReady);
    test_int(s->levels, 4);
    MeshGraphStatus status = *s;
    test_int(test_wait(world, plain)->state, MeshGraphReady);
    test_int(ecs_get(world, plain, MeshGraphStatus)->levels, 1);
    ecs_progress(world, 0);

    const ShapeMesh *sm = ecs_get(world, shape, ShapeMesh);
    test_assert(sm != NULL && sm->mesh != 0);
    uint32_t previous = sm->mesh;
    int32_t l;
    test_int(box_mesh_cache_get(cache, sm->mesh)->index_count / 3, 10 * 12 * 2);
    for (l = 1; l < 4; l ++) {
        uint32_t handle = box_mesh_cache_lod(cache, sm->mesh, l);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, handle);
        test_assert(mesh != NULL);
        test_assert(handle != previous);
        test_int(mesh->index_count / 3, status.level_triangles[l]);
        previous = handle;
    }

    test_int(status.level_triangles[3], 2 * 3 * 2);
    const ShapeMesh *pm = ecs_get(world, plain, ShapeMesh);
    test_assert(pm != NULL && pm->mesh != 0);
    test_assert(box_mesh_cache_lod(cache, pm->mesh, 2) == pm->mesh);

    /* Fewer levels rebuild the graph, the levels after the last are the last */
    ecs_set(world, shape, MeshGraph, { .output = tube, .levels = 2 });
    s = test_wait(world, shape);
    test_int(s->state, MeshGraphReady);
    test_int(s->levels, 2);
    ecs_progress(world, 0);
    sm = ecs_get(world, shape, ShapeMesh);
    uint32_t one = box_mesh_cache_lod(cache, sm->mesh, 1);
    test_assert(one != sm->mesh);
    test_assert(box_mesh_cache_lod(cache, sm->mesh, 3) == one);
    ecs_fini(world);
}

/** The reducing node kinds say so in their NodeKind, and the Detail kind is registered */
void MeshLod_node_kinds(void) {
    ecs_world_t *world = test_world();
    test_assert(ecs_get(world, ecs_id(MeshTube), MeshNodeKind)->detail);
    test_assert(ecs_get(world, ecs_id(MeshBranches), MeshNodeKind)->detail);
    test_assert(ecs_get(world, ecs_id(MeshPrune), MeshNodeKind)->detail);
    test_assert(ecs_get(world, ecs_id(MeshLeaves), MeshNodeKind)->detail);
    test_assert(ecs_get(world, ecs_id(MeshNeedles), MeshNodeKind)->detail);
    test_assert(ecs_get(world, ecs_id(MeshCards), MeshNodeKind)->detail);
    test_assert(ecs_get(world, ecs_id(MeshDisplace), MeshNodeKind)->detail);
    test_assert(ecs_get(world, ecs_id(MeshPointsOnMesh), MeshNodeKind)->detail);
    test_assert(!ecs_get(world, ecs_id(MeshStem), MeshNodeKind)->detail);
    const MeshNodeKind *detail = ecs_get(world, ecs_id(MeshDetail), MeshNodeKind);
    test_assert(detail != NULL);
    test_assert(detail->detail);
    test_int(detail->output, MeshValueAny);
    test_assert(detail->inputs[0].list);
    test_assert(ecs_lookup(world, "box.mesh.nodes.Detail") == ecs_id(MeshDetail));
    ecs_fini(world);
}

/** Displace drops an octave per level and PointsOnMesh keeps half its points */
void MeshLod_displace_points(void) {
    ecs_world_t *world = test_world();
    ecs_entity_t ball_shape = ecs_entity(world, { .name = "ball_shape" });
    ecs_add_id(world, ball_shape, EcsPrefab);
    ecs_set(world, ball_shape, IcoSphere, { .segments = 2, .smooth = true, .radius = 1 });
    ecs_entity_t ball = test_named(world, "ball");
    ecs_set(world, ball, MeshShape, { .entity = ball_shape });
    ecs_entity_t rough = lod_node(world, "rough", ecs_id(MeshDisplace), &(MeshDisplace){
        .mesh = ball, .amount = 0.1f, .octaves = 3, .seed = 1 });
    ecs_entity_t smooth = lod_node(world, "smooth", ecs_id(MeshDisplace), &(MeshDisplace){
        .mesh = ball, .amount = 0.1f, .octaves = 2, .seed = 1 });
    box_mg_value_t *a = box_mesh_graph_evaluate_level(world, rough, 1, NULL);
    box_mg_value_t *b = test_eval(world, smooth);
    test_assert(a == b);
    box_mg_value_release(a);
    box_mg_value_release(b);

    ecs_entity_t points = lod_node(world, "points", ecs_id(MeshPointsOnMesh),
        &(MeshPointsOnMesh){ .mesh = ball, .count = 40, .seed = 2 });
    box_mg_value_t *p0 = test_eval(world, points);
    box_mg_value_t *p2 = box_mesh_graph_evaluate_level(world, points, 2, NULL);
    test_int(p0->points.count, 40);
    test_int(p2->points.count, 10);
    box_mg_value_release(p0);
    box_mg_value_release(p2);
    ecs_fini(world);
}
