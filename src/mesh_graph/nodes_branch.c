/**
 * @file mesh_graph/nodes_branch.c
 * @brief Nodes of mesh graphs that grow branching curves: Stem, Branches and Prune.
 */

#include "mesh_graph.h"
#include <math.h>

/** Most segments of a stem or a branch */
#define BOX_MG_BRANCH_SEGMENTS (256)

/** Most children a Branches sprouts on one parent */
#define BOX_MG_BRANCH_CHILDREN (10000)

/** How a curve grows from its root, step by step */
typedef struct box_mg_grow_t {
    vec3 position;               /**< Root */
    vec3 direction;              /**< Unit direction at the root */
    float length;                /**< Length */
    float radius;                /**< Radius at the root */
    float taper;                 /**< Fraction of the radius the tip loses */
    int32_t segments;            /**< Segments */
    vec3 bend;                   /**< Turn of the direction per meter */
    float noise;                 /**< Turn of the noise per meter */
    float frequency;             /**< Noise cells per meter */
    int32_t seed;                /**< Seed of the noise */
} box_mg_grow_t;

/** The settings of a Branches with its defaults resolved */
typedef struct box_mg_branching_t {
    const MeshBranches *n;       /**< The node */
    int32_t per_node;            /**< Children per node */
    int32_t segments;            /**< Segments of a child */
    float start;                 /**< Start of the range, a fraction of the parent length */
    float end;                   /**< End of the range */
    float length;                /**< Length of a child, a fraction of the parent length */
    float pipe;                  /**< Radius of a child as a fraction of its parent's */
    vec3 light;                  /**< Unit direction toward the light */
    int32_t level;               /**< Level of the parents */
} box_mg_branching_t;

/** x to the power y for x > 0 from IEEE operations only, the same on every platform.
 *
 * @param x The base
 * @param y The exponent
 * @return The power
 */
static float box_mg_pow(
    float x,
    float y)
{
    int e = 0;
    float m = frexpf(x, &e);
    float t = (m - 1.0f) / (m + 1.0f), t2 = t * t;
    float ln = 2.0f * t * (1.0f + t2 * (1.0f / 3.0f + t2 * (1.0f / 5.0f + t2 * (1.0f / 7.0f +
        t2 * (1.0f / 9.0f + t2 * (1.0f / 11.0f))))));
    float z = y * ((float)e + ln * 1.44269504f);
    float whole = floorf(z + 0.5f), g = (z - whole) * 0.693147181f;
    float ex = 1.0f + g * (1.0f + g * (0.5f + g * (1.0f / 6.0f + g * (1.0f / 24.0f +
        g * (1.0f / 120.0f + g * (1.0f / 720.0f))))));

    return ldexpf(ex, (int)whole);
}

/** Draw a float in -1..1.
 *
 * @param rand The sequence
 * @return The float
 */
static float box_mg_rand_signed(
    box_mg_rand_t *rand)
{
    return box_mg_rand_float(rand) * 2.0f - 1.0f;
}

/** Add the attributes of branching curves that a node writes, empty, when they are missing.
 *
 * @param c The curves
 */
static void box_mg_branch_attrs_add(
    box_mg_curves_t *c)
{
    static const char *names[] = { "level", "parent_t", "root_distance", "phase", "tip" };
    int32_t i;
    for (i = 0; i < 5; i ++) {
        box_mg_attr_add(c->attrs, &c->attr_count, names[i], c->point_count);
    }
}

/** Write the attributes of a new curve: its level, where it sits on its parent, the distance
 * of its points from the root and its phase.
 *
 * @param c The curves
 * @param curve The curve
 * @param level The level
 * @param parent_t Fraction of the length of the parent where it starts
 * @param root Distance of its first point from the root
 * @param phase The phase
 */
static void box_mg_branch_attrs(
    box_mg_curves_t *c,
    int32_t curve,
    float level,
    float parent_t,
    float root,
    float phase)
{
    float *lv = box_mg_attr_find(c->attrs, c->attr_count, "level");
    float *pt = box_mg_attr_find(c->attrs, c->attr_count, "parent_t");
    float *rd = box_mg_attr_find(c->attrs, c->attr_count, "root_distance");
    float *ph = box_mg_attr_find(c->attrs, c->attr_count, "phase");
    int32_t first = c->offsets[curve], last = c->offsets[curve + 1], i;
    float distance = root;
    for (i = first; i < last; i ++) {
        if (i > first) {
            distance += glm_vec3_distance(c->positions[i - 1], c->positions[i]);
        }

        if (lv) {
            lv[i] = level;
        }

        if (pt) {
            pt[i] = parent_t;
        }

        if (rd) {
            rd[i] = distance;
        }

        if (ph) {
            ph[i] = phase;
        }
    }
}

/** Grow the points of a curve: positions and radii, the direction turning by the bend and
 * the noise after every segment.
 *
 * @param c The curves, whose offsets give the curve segments + 1 points
 * @param curve The curve
 * @param g How it grows
 */
static void box_mg_grow(
    box_mg_curves_t *c,
    int32_t curve,
    const box_mg_grow_t *g)
{
    int32_t first = c->offsets[curve], j, segments = g->segments > 0 ? g->segments : 1;
    float ds = g->length / (float)segments, taper = glm_clamp(g->taper, 0.0f, 1.0f);
    vec3 p, d;
    glm_vec3_copy((float*)g->position, p);
    glm_vec3_copy((float*)g->direction, d);
    for (j = 0; j <= segments; j ++) {
        glm_vec3_copy(p, c->positions[first + j]);
        c->radii[first + j] = g->radius * (1.0f - taper * (float)j / (float)segments);
        vec3 turned;
        glm_vec3_scale((float*)g->bend, ds, turned);
        if (g->noise != 0.0f) {
            vec3 q, r = { 17.31f, -5.77f, 9.13f }, s = { -3.71f, 11.13f, -7.37f };
            glm_vec3_scale(p, g->frequency, q);
            glm_vec3_add(q, r, r);
            glm_vec3_add(q, s, s);
            turned[0] += g->noise * ds * box_mg_noise(q, 2, g->seed);
            turned[1] += g->noise * ds * box_mg_noise(r, 2, g->seed);
            turned[2] += g->noise * ds * box_mg_noise(s, 2, g->seed);
        }

        glm_vec3_add(d, turned, turned);
        if (glm_vec3_norm2(turned) > 1e-12f) {
            glm_vec3_normalize_to(turned, d);
        }

        glm_vec3_muladds(d, ds, p);
    }
}

/** Turn the normals of a curve around its tangents by an angle per unit of length.
 *
 * @param c The curves
 * @param curve The curve
 * @param twist Radians per unit of length
 */
static void box_mg_twist(
    box_mg_curves_t *c,
    int32_t curve,
    float twist)
{
    int32_t first = c->offsets[curve], last = c->offsets[curve + 1], i;
    float distance = 0.0f;
    for (i = first; i < last && twist != 0.0f; i ++) {
        float s, co;
        vec3 b, n;
        if (i > first) {
            distance += glm_vec3_distance(c->positions[i - 1], c->positions[i]);
        }

        box_mg_sincos(twist * distance, &s, &co);
        glm_vec3_cross(c->tangents[i], c->normals[i], b);
        glm_vec3_scale(c->normals[i], co, n);
        glm_vec3_muladds(b, s, n);
        glm_vec3_normalize_to(n, c->normals[i]);
    }
}

/** Grow the stems of a MeshStem.
 *
 * @param node The MeshStem
 * @param inputs The points, NULL for one stem
 * @param counts Values per port
 * @param out The curves
 * @param error Unused
 * @return 0
 */
static int box_mesh_stem_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshStem *n = node;
    const box_mg_points_t *pts = counts[0] ? &inputs[0]->points : NULL;
    box_mg_curves_t *c = &out->curves;
    int32_t stems = pts ? pts->count : 1, s;
    int32_t segments = n->segments > 0 ? n->segments : 8;
    vec3 dir = { n->direction[0], n->direction[1], n->direction[2] };
    (void)error;
    segments = segments > BOX_MG_BRANCH_SEGMENTS ? BOX_MG_BRANCH_SEGMENTS : segments;
    if (glm_vec3_norm2(dir) < 1e-12f) {
        glm_vec3_copy((vec3){ 0.0f, 1.0f, 0.0f }, dir);
    }

    glm_vec3_normalize(dir);
    box_mg_branch_attrs_add(c);
    box_mg_curves_resize(c, stems, stems * (segments + 1));
    for (s = 0; s <= stems; s ++) {
        c->offsets[s] = s * (segments + 1);
    }

    for (s = 0; s < stems; s ++) {
        box_mg_rand_t rand;
        float scale = pts && pts->scales[s][1] != 0.0f ? fabsf(pts->scales[s][1]) : 1.0f;
        box_mg_grow_t g = {
            .length = (n->length > 0.0f ? n->length : 1.0f) * scale,
            .radius = (n->radius > 0.0f ? n->radius : 0.1f) * scale,
            .taper = n->taper,
            .segments = segments,
            .bend = { 0.0f, n->gravitropism, 0.0f },
            .noise = n->noise,
            .frequency = n->noise_frequency > 0.0f ? n->noise_frequency : 1.0f
        };
        box_mg_rand_seed(&rand, n->seed, (uint32_t)s);
        g.length *= glm_max(0.0f, 1.0f + n->length_variation * box_mg_rand_signed(&rand));
        g.length = glm_max(g.length, 1e-4f);
        g.seed = (int32_t)(box_mg_rand_float(&rand) * 16777216.0f);
        float phase = box_mg_rand_float(&rand);
        if (pts) {
            versor q;
            glm_quat_copy((float*)pts->rotations[s], q);
            glm_vec3_copy(pts->positions[s], g.position);
            glm_quat_rotatev(q, dir, g.direction);
            glm_vec3_normalize(g.direction);
        } else {
            glm_vec3_copy((float*)n->position, g.position);
            glm_vec3_copy(dir, g.direction);
        }

        box_mg_grow(c, s, &g);
        box_mg_branch_attrs(c, s, 0.0f, 0.0f, 0.0f, phase);
    }

    box_mg_curves_frames(c);
    for (s = 0; s < stems; s ++) {
        box_mg_twist(c, s, n->twist);
    }

    box_mg_curves_tips(c);

    return 0;
}

/** Count the nodes of children on a parent.
 *
 * @param b The settings
 * @param length Length of the parent
 * @return The nodes, each with per_node children
 */
static int32_t box_mg_branch_nodes(
    const box_mg_branching_t *b,
    float length)
{
    const MeshBranches *n = b->n;
    float range = fabsf(b->end - b->start) * length;
    int32_t children = n->count;
    if (n->density > 0.0f) {
        children = (int32_t)glm_min(floorf(n->density * range + 0.5f),
            (float)BOX_MG_BRANCH_CHILDREN);
    }

    children = children < 0 ? 0 : children;
    children = children > BOX_MG_BRANCH_CHILDREN ? BOX_MG_BRANCH_CHILDREN : children;
    int32_t nodes = children / b->per_node;

    return children > 0 && !nodes ? 1 : nodes;
}

/** Evaluate the length curve of a Branches at a fraction of the range.
 *
 * @param n The node
 * @param u The fraction
 * @return The factor of the length
 */
static float box_mg_length_curve(
    const MeshBranches *n,
    float u)
{
    const float *k = n->length_curve;
    float result = 1.0f;
    if (k[0] != 0.0f || k[1] != 0.0f || k[2] != 0.0f || k[3] != 0.0f || k[4] != 0.0f) {
        float x = glm_clamp(u, 0.0f, 1.0f) * 4.0f;
        int32_t i = (int32_t)floorf(x);
        i = i > 3 ? 3 : i;
        result = k[i] + (k[i + 1] - k[i]) * (x - (float)i);
    }

    return result;
}

/** Turn of a child around its parent by the distribution.
 *
 * @param b The settings
 * @param node The node of the child on the parent
 * @param child The child in its node
 * @return The turn in radians
 */
static float box_mg_branch_roll(
    const box_mg_branching_t *b,
    int32_t node,
    int32_t child)
{
    const MeshBranches *n = b->n;
    float i = (float)node, j = (float)child, k = (float)b->per_node, result = 0.0f;
    if (n->distribution == MeshBranchOpposite) {
        result = i * GLM_PIf * 0.5f + j * GLM_PIf;
    } else if (n->distribution == MeshBranchWhorled) {
        result = i * GLM_PIf / k + j * 2.0f * GLM_PIf / k;
    } else if (n->distribution == MeshBranchSpiral) {
        result = i * (n->phyllotaxis != 0.0f ? n->phyllotaxis : BOX_MG_GOLDEN_ANGLE);
    } else {
        result = i * GLM_PIf;
    }

    return result + n->roll;
}

/** Sprout the children of one node of a parent into the curves.
 *
 * @param b The settings
 * @param in The input curves, where the parent is
 * @param parent The parent
 * @param lengths Length of the parent up to each of its points
 * @param node The node
 * @param nodes Nodes on the parent
 * @param rand The random sequence of the parent
 * @param c The output curves
 * @param curve First curve of the node in c, whose offset is set
 */
static void box_mg_branch_node(
    const box_mg_branching_t *b,
    const box_mg_curves_t *in,
    int32_t parent,
    const float *lengths,
    int32_t node,
    int32_t nodes,
    box_mg_rand_t *rand,
    box_mg_curves_t *c,
    int32_t curve)
{
    const MeshBranches *n = b->n;
    int32_t first = in->offsets[parent], count = in->offsets[parent + 1] - first, j;
    float total = lengths[count - 1], u = ((float)node + 0.5f) / (float)nodes;
    float t = b->start + (b->end - b->start) * u;
    const float *rd = box_mg_attr_find(in->attrs, in->attr_count, "root_distance");
    box_mg_sample_t s;
    box_mg_curve_sample(in, first, count, lengths, t * total, &s);
    float root = t * total;
    if (rd) {
        int32_t next = s.fraction > 0.0f ? s.point + 1 : s.point;
        root = rd[s.point] + (rd[next] - rd[s.point]) * s.fraction;
    }

    for (j = 0; j < b->per_node; j ++) {
        int32_t ci = curve + j;
        float down = n->down_angle + n->down_angle_change * u +
            n->angle_variation * box_mg_rand_signed(rand);
        float roll = box_mg_branch_roll(b, node, j) + n->roll_variation * box_mg_rand_signed(rand);
        float lf = 1.0f + n->length_variation * box_mg_rand_signed(rand);
        float rf = 1.0f + n->radius_variation * box_mg_rand_signed(rand);
        int32_t seed = (int32_t)(box_mg_rand_float(rand) * 16777216.0f);
        float phase = box_mg_rand_float(rand);
        float sr, cr, sd, cd;
        vec3 bin, radial;
        box_mg_sincos(roll, &sr, &cr);
        box_mg_sincos(down, &sd, &cd);
        glm_vec3_cross(s.tangent, s.normal, bin);
        glm_vec3_scale(s.normal, cr, radial);
        glm_vec3_muladds(bin, sr, radial);
        box_mg_grow_t g = {
            .length = glm_max(total * b->length * box_mg_length_curve(n, u) * lf, 1e-4f),
            .radius = glm_min(s.radius * b->pipe * glm_max(rf, 0.0f), s.radius * 0.95f),
            .taper = n->taper,
            .segments = b->segments,
            .noise = n->noise,
            .frequency = 1.0f,
            .seed = seed
        };
        glm_vec3_copy(s.position, g.position);
        glm_vec3_scale(s.tangent, cd, g.direction);
        glm_vec3_muladds(radial, sd, g.direction);
        glm_vec3_normalize(g.direction);
        glm_vec3_scale((float*)b->light, n->phototropism, g.bend);
        g.bend[1] -= n->gravity;
        c->offsets[ci + 1] = c->offsets[ci] + b->segments + 1;
        c->parents[ci] = parent;
        box_mg_grow(c, ci, &g);
        box_mg_branch_attrs(c, ci, (float)(b->level + 1), t, root, phase);
    }
}

/** Drop the curves with a parent of a level or higher whose base radius is under a fraction
 * of the largest base radius of the curves without a parent, and the children of dropped
 * curves: give their points the attribute dropped 1, added when there is room.
 *
 * @param c The curves
 * @param drop The fraction
 * @param level Lowest level of the dropped curves
 */
static void box_mg_curves_drop(
    box_mg_curves_t *c,
    float drop,
    int32_t level)
{
    const float *lv = box_mg_attr_find(c->attrs, c->attr_count, "level");
    float largest = 0.0f;
    int32_t i, j, rounds = 0;
    bool changed = true;
    for (i = 0; i < c->curve_count; i ++) {
        if (c->parents[i] < 0 && c->offsets[i + 1] > c->offsets[i]) {
            largest = glm_max(largest, c->radii[c->offsets[i]]);
        }
    }

    float *d = box_mg_attr_add(c->attrs, &c->attr_count, "dropped", c->point_count);
    float limit = drop * largest;
    for (i = 0; d && i < c->curve_count; i ++) {
        int32_t first = c->offsets[i], last = c->offsets[i + 1];
        int32_t p = c->parents[i];
        if (first >= last || d[first] > 0.5f || p < 0 || p >= c->curve_count ||
            (lv ? (int32_t)lv[first] : 1) < level || c->radii[first] >= limit)
        {
            continue;
        }

        for (j = first; j < last; j ++) {
            d[j] = 1.0f;
        }
    }

    while (d && changed && rounds < c->curve_count + 1) {
        changed = false;
        for (i = 0; i < c->curve_count; i ++) {
            int32_t first = c->offsets[i], last = c->offsets[i + 1], p = c->parents[i];
            if (first >= last || d[first] > 0.5f || p < 0 || p >= c->curve_count ||
                c->offsets[p + 1] <= c->offsets[p] || d[c->offsets[p]] <= 0.5f)
            {
                continue;
            }

            for (j = first; j < last; j ++) {
                d[j] = 1.0f;
            }

            changed = true;
        }

        rounds ++;
    }
}

/** Grow the drop of a node by its lod_drop per level of detail.
 *
 * @param drop The drop of the node
 * @param lod_drop The lod_drop of the node
 * @param level The level
 */
static void box_mg_drop_lod(
    float *drop,
    float lod_drop,
    int32_t level)
{
    float grow = lod_drop > 0.0f ? lod_drop : (lod_drop < 0.0f ? 0.0f : 0.08f);
    if (grow > 0.0f) {
        *drop = glm_max(*drop, 0.0f) + grow * (float)level;
    }
}

/** Reduce a MeshBranches for a level of detail: its drop grows by lod_drop per level.
 *
 * @param node The snapshot of the MeshBranches
 * @param level The level
 */
static void box_mesh_branches_lod(
    void *node,
    int32_t level)
{
    MeshBranches *n = node;
    box_mg_drop_lod(&n->drop, n->lod_drop, level);
}

/** Reduce a MeshPrune for a level of detail: its drop grows by lod_drop per level.
 *
 * @param node The snapshot of the MeshPrune
 * @param level The level
 */
static void box_mesh_prune_lod(
    void *node,
    int32_t level)
{
    MeshPrune *n = node;
    box_mg_drop_lod(&n->drop, n->lod_drop, level);
}

/** Resolve the settings of a Branches and find the level of its parents.
 *
 * @param n The node
 * @param in The input curves
 * @param b Receives the settings
 */
static void box_mg_branching_init(
    const MeshBranches *n,
    const box_mg_curves_t *in,
    box_mg_branching_t *b)
{
    const float *lv = box_mg_attr_find(in->attrs, in->attr_count, "level");
    float exponent = n->radius_exponent > 0.0f ? n->radius_exponent : 2.0f;
    int32_t i;
    ecs_os_memset_t(b, 0, box_mg_branching_t);
    b->n = n;
    b->per_node = 1;
    if (n->distribution == MeshBranchOpposite) {
        b->per_node = 2;
    } else if (n->distribution == MeshBranchWhorled) {
        b->per_node = n->whorl > 0 ? (n->whorl < 64 ? n->whorl : 64) : 3;
    }

    b->segments = n->segments > 0 ? n->segments : 6;
    b->segments = b->segments > BOX_MG_BRANCH_SEGMENTS ? BOX_MG_BRANCH_SEGMENTS : b->segments;
    b->start = glm_clamp(n->start, 0.0f, 1.0f);
    b->end = glm_clamp(n->end > 0.0f ? n->end : 1.0f, 0.0f, 1.0f);
    b->length = n->length > 0.0f ? n->length : 0.5f;
    b->pipe = box_mg_pow((float)(b->per_node + 1), -1.0f / exponent);
    glm_vec3_copy((float*)n->light, b->light);
    if (glm_vec3_norm2(b->light) < 1e-12f) {
        glm_vec3_copy((vec3){ 0.0f, 1.0f, 0.0f }, b->light);
    }

    glm_vec3_normalize(b->light);
    for (i = 0; lv && i < in->curve_count; i ++) {
        if (in->offsets[i + 1] > in->offsets[i]) {
            int32_t level = (int32_t)lv[in->offsets[i]];
            b->level = level > b->level ? level : b->level;
        }
    }
}

/** Check whether a curve is a parent of a Branches: of the highest level, with a segment.
 *
 * @param b The settings
 * @param in The input curves
 * @param curve The curve
 * @return Whether it sprouts children
 */
static bool box_mg_branch_parent(
    const box_mg_branching_t *b,
    const box_mg_curves_t *in,
    int32_t curve)
{
    const float *lv = box_mg_attr_find(in->attrs, in->attr_count, "level");
    int32_t first = in->offsets[curve];

    return in->offsets[curve + 1] - first >= 2 && (!lv || (int32_t)lv[first] == b->level);
}

/** Sprout child curves along the curves of the highest level of a Curves.
 *
 * @param node The MeshBranches
 * @param inputs The curves
 * @param counts Values per port
 * @param out The curves with the children
 * @param error Unused
 * @return 0
 */
static int box_mesh_branches_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const box_mg_curves_t *in = &inputs[0]->curves;
    box_mg_curves_t *c = &out->curves;
    box_mg_branching_t b;
    int32_t i, k, children = 0, curve = in->curve_count;
    float *lengths = ecs_os_malloc_n(float, in->point_count + 1);
    (void)counts;
    (void)error;
    box_mg_branching_init(node, in, &b);
    for (i = 0; i < in->curve_count; i ++) {
        if (box_mg_branch_parent(&b, in, i)) {
            float length = box_mg_curve_lengths(in, i, lengths);
            children += box_mg_branch_nodes(&b, length) * b.per_node;
        }
    }

    box_mg_value_copy(out, inputs[0]);
    box_mg_branch_attrs_add(c);
    box_mg_curves_resize(c, in->curve_count + children,
        in->point_count + children * (b.segments + 1));
    for (i = 0; i < in->curve_count; i ++) {
        if (!box_mg_branch_parent(&b, in, i)) {
            continue;
        }

        box_mg_rand_t rand;
        float length = box_mg_curve_lengths(in, i, lengths);
        int32_t nodes = box_mg_branch_nodes(&b, length);
        box_mg_rand_seed(&rand, b.n->seed, (uint32_t)i * 7919u + (uint32_t)b.level);
        for (k = 0; k < nodes; k ++) {
            box_mg_branch_node(&b, in, i, lengths, k, nodes, &rand, c, curve);
            curve += b.per_node;
        }
    }

    box_mg_curves_frames_range(c, in->curve_count, c->curve_count);
    box_mg_curves_tips(c);
    if (b.n->drop > 0.0f) {
        box_mg_curves_drop(c, b.n->drop, 1);
    }

    ecs_os_free(lengths);

    return 0;
}

/** Radius of the envelope of a profile at a height: the largest x where the profile crosses
 * the height.
 *
 * @param profile The profile curves, its first curve is the profile
 * @param y The height above the center
 * @return The radius, negative where the profile does not reach
 */
static float box_mg_profile_radius(
    const box_mg_curves_t *profile,
    float y)
{
    int32_t first = profile->offsets[0], last = profile->offsets[1], i;
    float result = -1.0f;
    for (i = first; i + 1 < last; i ++) {
        const float *a = profile->positions[i], *b = profile->positions[i + 1];
        float lo = glm_min(a[1], b[1]), hi = glm_max(a[1], b[1]);
        if (y < lo || y > hi) {
            continue;
        }

        float f = hi > lo ? (y - a[1]) / (b[1] - a[1]) : 0.0f;
        result = glm_max(result, fabsf(a[0] + (b[0] - a[0]) * f));
    }

    return result;
}

/** Check whether a point is inside the envelope of a Prune.
 *
 * @param n The node
 * @param profile The profile curves, NULL for the shape
 * @param p The point
 * @return Whether it is inside
 */
static bool box_mg_envelope_inside(
    const MeshPrune *n,
    const box_mg_curves_t *profile,
    const vec3 p)
{
    vec3 l, s;
    bool result = true;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        l[i] = p[i] - n->center[i];
        s[i] = n->size[i] != 0.0f ? fabsf(n->size[i]) : 1.0f;
    }

    float x = l[0] / s[0], y = l[1] / s[1], z = l[2] / s[2], h = x * x + z * z;
    if (profile) {
        float r = box_mg_profile_radius(profile, l[1]);
        result = r >= 0.0f && l[0] * l[0] + l[2] * l[2] <= r * r;
    } else if (n->shape == MeshEnvelopeEllipsoid) {
        result = h + y * y <= 1.0f;
    } else if (n->shape == MeshEnvelopeCone) {
        result = y >= 0.0f && y <= 1.0f && h <= (1.0f - y) * (1.0f - y);
    } else if (n->shape == MeshEnvelopeCylinder) {
        result = y >= 0.0f && y <= 1.0f && h <= 1.0f;
    }

    return result;
}

/** What Prune keeps of a curve */
typedef struct box_mg_keep_t {
    bool removed;                /**< The curve is removed */
    int32_t points;              /**< Points it keeps, the last one cut when cut < 1 */
    float cut;                   /**< Fraction of the last kept segment up to the cut */
    float kept;                  /**< Fraction of its length it keeps */
} box_mg_keep_t;

/** Decide what Prune keeps of one curve by its radius and its envelope.
 *
 * @param n The node
 * @param in The curves
 * @param profile The profile curves, NULL for the shape
 * @param curve The curve
 * @param out Receives what it keeps
 */
static void box_mg_prune_curve(
    const MeshPrune *n,
    const box_mg_curves_t *in,
    const box_mg_curves_t *profile,
    int32_t curve,
    box_mg_keep_t *out)
{
    const float *lv = box_mg_attr_find(in->attrs, in->attr_count, "level");
    int32_t first = in->offsets[curve], count = in->offsets[curve + 1] - first, k, i;
    bool envelope = profile || n->shape != MeshEnvelopeNone;
    *out = (box_mg_keep_t){ .removed = count < 1, .points = count, .cut = 1.0f, .kept = 1.0f };
    if (out->removed || (lv ? (int32_t)lv[first] : 0) < n->level) {
        goto done;
    }

    out->removed = in->radii[first] < n->min_radius;
    for (k = 0; envelope && !out->removed && k < count; k ++) {
        if (!box_mg_envelope_inside(n, profile, in->positions[first + k])) {
            break;
        }
    }

    if (out->removed || !envelope || k == count) {
        goto done;
    }

    out->removed = k == 0 || !n->shorten;
    float lo = 0.0f, hi = 1.0f;
    for (i = 0; !out->removed && i < 24; i ++) {
        float mid = 0.5f * (lo + hi);
        vec3 p;
        glm_vec3_lerp(in->positions[first + k - 1], in->positions[first + k], mid, p);
        if (box_mg_envelope_inside(n, profile, p)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    if (!out->removed) {
        float *lengths = ecs_os_malloc_n(float, count);
        float total = box_mg_curve_lengths(in, curve, lengths);
        float kept = lengths[k - 1] + (lengths[k] - lengths[k - 1]) * lo;
        out->points = k + 1;
        out->cut = lo;
        out->kept = total > 0.0f ? kept / total : 1.0f;
        ecs_os_free(lengths);
    }

done:
    return;
}

/** Remove the children of removed curves and the children beyond the cut of their parent,
 * until nothing changes.
 *
 * @param in The curves
 * @param keep What each curve keeps
 */
static void box_mg_prune_children(
    const box_mg_curves_t *in,
    box_mg_keep_t *keep)
{
    const float *pt = box_mg_attr_find(in->attrs, in->attr_count, "parent_t");
    bool changed = true;
    int32_t i, rounds = 0;
    while (changed && rounds < in->curve_count + 1) {
        changed = false;
        for (i = 0; i < in->curve_count; i ++) {
            int32_t p = in->parents[i];
            if (keep[i].removed || p < 0 || p >= in->curve_count) {
                continue;
            }

            float t = pt && keep[i].points ? pt[in->offsets[i]] : 0.0f;
            if (keep[p].removed || t > keep[p].kept + 1e-5f) {
                keep[i].removed = true;
                changed = true;
            }
        }

        rounds ++;
    }
}

/** Copy the kept points of a curve into the pruned curves, the last one moved to the cut.
 *
 * @param in The curves
 * @param curve The curve of in
 * @param keep What it keeps
 * @param c The pruned curves
 * @param to The point of c of its first point
 */
static void box_mg_prune_copy(
    const box_mg_curves_t *in,
    int32_t curve,
    const box_mg_keep_t *keep,
    box_mg_curves_t *c,
    int32_t to)
{
    int32_t first = in->offsets[curve], j, a;
    for (j = 0; j < keep->points; j ++) {
        int32_t src = first + j, dst = to + j;
        bool cut = j == keep->points - 1 && keep->cut < 1.0f && j > 0;
        float f = cut ? keep->cut : 0.0f;
        int32_t prev = cut ? src - 1 : src;
        glm_vec3_lerp(in->positions[prev], in->positions[src], cut ? f : 1.0f, c->positions[dst]);
        c->radii[dst] = in->radii[prev] + (in->radii[src] - in->radii[prev]) * (cut ? f : 1.0f);
        glm_vec3_copy(in->tangents[cut ? prev : src], c->tangents[dst]);
        glm_vec3_copy(in->normals[cut ? prev : src], c->normals[dst]);
        for (a = 0; a < in->attr_count; a ++) {
            const float *v = in->attrs[a].values;
            float *w = box_mg_attr_find(c->attrs, c->attr_count, in->attrs[a].name);
            if (w) {
                w[dst] = v[prev] + (v[src] - v[prev]) * (cut ? f : 1.0f);
            }
        }
    }
}

/** Taper the shortened curves to the radius their tip had, so they do not end in a cut, and
 * thin their children that became as thick as them where they sprout.
 *
 * @param in The curves before pruning
 * @param keep What each curve keeps
 * @param map Curve of c of each curve of in, -1 for removed
 * @param c The pruned curves
 */
static void box_mg_prune_taper(
    const box_mg_curves_t *in,
    const box_mg_keep_t *keep,
    const int32_t *map,
    box_mg_curves_t *c)
{
    const float *pt = box_mg_attr_find(c->attrs, c->attr_count, "parent_t");
    float *lengths = ecs_os_malloc_n(float, c->point_count + 1);
    bool *thinned = ecs_os_calloc_n(bool, c->curve_count + 1);
    int32_t i, j;
    for (i = 0; i < in->curve_count; i ++) {
        int32_t ci = map[i];
        if (ci < 0) {
            continue;
        }

        int32_t first = c->offsets[ci], count = c->offsets[ci + 1] - first;
        float total = box_mg_curve_lengths(c, ci, lengths);
        float tip = in->radii[in->offsets[i + 1] - 1], cut = c->radii[first + count - 1];
        if (keep[i].kept < 1.0f && cut > tip && total > 0.0f) {
            for (j = 0; j < count; j ++) {
                c->radii[first + j] -= (cut - tip) * lengths[j] / total;
            }

            thinned[ci] = true;
        }

        int32_t p = c->parents[ci];
        if (p < 0 || p >= ci || !thinned[p] || c->offsets[p + 1] - c->offsets[p] < 2) {
            continue;
        }

        box_mg_sample_t s;
        float length = box_mg_curve_lengths(c, p, lengths);
        box_mg_curve_sample(c, c->offsets[p], c->offsets[p + 1] - c->offsets[p], lengths,
            (pt ? pt[first] : 0.0f) * length, &s);
        float limit = 0.95f * s.radius;
        if (count && c->radii[first] > limit && c->radii[first] > 0.0f) {
            float f = limit / c->radii[first];
            for (j = 0; j < count; j ++) {
                c->radii[first + j] *= f;
            }

            thinned[ci] = true;
        }
    }

    ecs_os_free(thinned);
    ecs_os_free(lengths);
}

/** Remove or shorten the curves outside an envelope and below a radius.
 *
 * @param node The MeshPrune
 * @param inputs The curves and the profile
 * @param counts Values per port
 * @param out The pruned curves
 * @param error Unused
 * @return 0
 */
static int box_mesh_prune_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshPrune *n = node;
    const box_mg_curves_t *in = &inputs[0]->curves;
    const box_mg_curves_t *profile = counts[1] ? &inputs[1]->curves : NULL;
    box_mg_curves_t *c = &out->curves;
    int32_t i, curves = 0, points = 0;
    box_mg_keep_t *keep = ecs_os_calloc_n(box_mg_keep_t, in->curve_count + 1);
    int32_t *map = ecs_os_malloc_n(int32_t, in->curve_count + 1);
    (void)error;
    if (profile && (!profile->curve_count || profile->offsets[1] - profile->offsets[0] < 2)) {
        profile = NULL;
    }

    for (i = 0; i < in->curve_count; i ++) {
        box_mg_prune_curve(n, in, profile, i, &keep[i]);
    }

    box_mg_prune_children(in, keep);
    for (i = 0; i < in->curve_count; i ++) {
        map[i] = keep[i].removed ? -1 : curves;
        curves += keep[i].removed ? 0 : 1;
        points += keep[i].removed ? 0 : keep[i].points;
    }

    for (i = 0; i < in->attr_count; i ++) {
        box_mg_attr_add(c->attrs, &c->attr_count, in->attrs[i].name, 0);
    }

    box_mg_curves_resize(c, curves, points);
    float *pt = box_mg_attr_find(c->attrs, c->attr_count, "parent_t");
    for (i = 0; i < in->curve_count; i ++) {
        int32_t ci = map[i], p = in->parents[i], j;
        if (ci < 0) {
            continue;
        }

        c->offsets[ci + 1] = c->offsets[ci] + keep[i].points;
        c->parents[ci] = p >= 0 && p < in->curve_count ? map[p] : -1;
        box_mg_prune_copy(in, i, &keep[i], c, c->offsets[ci]);
        for (j = c->offsets[ci]; pt && c->parents[ci] >= 0 && j < c->offsets[ci + 1]; j ++) {
            pt[j] = glm_min(1.0f, pt[j] / glm_max(keep[p].kept, 1e-6f));
        }
    }

    box_mg_prune_taper(in, keep, map, c);
    box_mg_curves_tips(c);
    if (n->drop > 0.0f) {
        box_mg_curves_drop(c, n->drop, n->level);
    }

    ecs_os_free(map);
    ecs_os_free(keep);

    return 0;
}

/** Construct MeshStem values with the documented defaults.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_stem_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshStem *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshStem){ .direction = { 0.0f, 1.0f, 0.0f }, .length = 5.0f,
            .segments = 12, .radius = 0.2f, .taper = 0.8f, .noise_frequency = 1.0f };
    }
}

/** Construct MeshBranches values with the documented defaults.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_branches_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshBranches *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshBranches){ .count = 4, .end = 1.0f, .down_angle = 0.8f,
            .length = 0.5f, .radius_exponent = 2.0f, .taper = 0.8f, .segments = 6 };
    }
}

/** Construct MeshPrune values with the documented defaults.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_prune_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshPrune *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshPrune){ .size = { 1.0f, 1.0f, 1.0f }, .level = 1 };
    }
}

/* Parameter of node kind T: its member name, offset and size. */
#define BOX_MG_PARAM(T, member) { #member, offsetof(T, member), ECS_SIZEOF(((T*)0)->member) }

void box_mesh_nodes_branch_register(
    ecs_world_t *world)
{
    ecs_set_hooks(world, MeshStem, { .ctor = box_mesh_stem_ctor });
    ecs_set_hooks(world, MeshBranches, { .ctor = box_mesh_branches_ctor });
    ecs_set_hooks(world, MeshPrune, { .ctor = box_mesh_prune_ctor });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshStem),
        .output = MeshValueCurves,
        .inputs = {{ "points", offsetof(MeshStem, points), MeshValuePoints, .optional = true }},
        .params = {
            BOX_MG_PARAM(MeshStem, position), BOX_MG_PARAM(MeshStem, direction),
            BOX_MG_PARAM(MeshStem, length), BOX_MG_PARAM(MeshStem, length_variation),
            BOX_MG_PARAM(MeshStem, segments), BOX_MG_PARAM(MeshStem, radius),
            BOX_MG_PARAM(MeshStem, taper), BOX_MG_PARAM(MeshStem, noise),
            BOX_MG_PARAM(MeshStem, noise_frequency), BOX_MG_PARAM(MeshStem, gravitropism),
            BOX_MG_PARAM(MeshStem, twist), BOX_MG_PARAM(MeshStem, seed)
        },
        .eval = box_mesh_stem_eval,
        .flags = BOX_MESH_NODE_RANDOM
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshBranches),
        .output = MeshValueCurves,
        .inputs = {{ "curves", offsetof(MeshBranches, curves), MeshValueCurves }},
        .params = {
            BOX_MG_PARAM(MeshBranches, count), BOX_MG_PARAM(MeshBranches, density),
            BOX_MG_PARAM(MeshBranches, start), BOX_MG_PARAM(MeshBranches, end),
            BOX_MG_PARAM(MeshBranches, distribution), BOX_MG_PARAM(MeshBranches, whorl),
            BOX_MG_PARAM(MeshBranches, phyllotaxis), BOX_MG_PARAM(MeshBranches, down_angle),
            BOX_MG_PARAM(MeshBranches, down_angle_change), BOX_MG_PARAM(MeshBranches, roll),
            BOX_MG_PARAM(MeshBranches, length), BOX_MG_PARAM(MeshBranches, length_curve),
            BOX_MG_PARAM(MeshBranches, radius_exponent), BOX_MG_PARAM(MeshBranches, taper),
            BOX_MG_PARAM(MeshBranches, segments), BOX_MG_PARAM(MeshBranches, gravity),
            BOX_MG_PARAM(MeshBranches, phototropism), BOX_MG_PARAM(MeshBranches, light),
            BOX_MG_PARAM(MeshBranches, noise), BOX_MG_PARAM(MeshBranches, angle_variation),
            BOX_MG_PARAM(MeshBranches, roll_variation),
            BOX_MG_PARAM(MeshBranches, length_variation),
            BOX_MG_PARAM(MeshBranches, radius_variation), BOX_MG_PARAM(MeshBranches, seed),
            BOX_MG_PARAM(MeshBranches, drop), BOX_MG_PARAM(MeshBranches, lod_drop)
        },
        .eval = box_mesh_branches_eval,
        .lod = box_mesh_branches_lod,
        .flags = BOX_MESH_NODE_RANDOM
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshPrune),
        .output = MeshValueCurves,
        .inputs = {
            { "curves", offsetof(MeshPrune, curves), MeshValueCurves },
            { "profile", offsetof(MeshPrune, profile), MeshValueCurves, .optional = true }
        },
        .params = {
            BOX_MG_PARAM(MeshPrune, shape), BOX_MG_PARAM(MeshPrune, center),
            BOX_MG_PARAM(MeshPrune, size), BOX_MG_PARAM(MeshPrune, min_radius),
            BOX_MG_PARAM(MeshPrune, level), BOX_MG_PARAM(MeshPrune, shorten),
            BOX_MG_PARAM(MeshPrune, drop), BOX_MG_PARAM(MeshPrune, lod_drop)
        },
        .eval = box_mesh_prune_eval,
        .lod = box_mesh_prune_lod
    });
}
