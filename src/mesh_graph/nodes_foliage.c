/**
 * @file mesh_graph/nodes_foliage.c
 * @brief Nodes of mesh graphs that dress branching curves with foliage: Leaves, Needles,
 * Cards, CrownNormals and CrownOcclusion.
 */

#include "mesh_graph.h"
#include <math.h>

/** Most nodes of leaves on one curve */
#define BOX_MG_LEAF_NODES (100000)

/** Most leaves around one node */
#define BOX_MG_LEAF_PER_NODE (16)

/** Most fronds on one curve */
#define BOX_MG_FRONDS (64)

/** Most rows of quads along a card */
#define BOX_MG_CARD_SEGMENTS (16)

/** Fewest and most cells of the density grid of a crown along its longest side */
#define BOX_MG_CROWN_MIN_CELLS (4)
#define BOX_MG_CROWN_MAX_CELLS (64)

/** Attributes of the points of Leaves and Needles, in the order of box_mg_leaf_t.values */
#define BOX_MG_LEAF_ATTRS (10)

/** Names of the attributes of the points of Leaves and Needles */
static const char *box_mg_leaf_names[BOX_MG_LEAF_ATTRS] = {
    "t", "curve", "level", "pivot_x", "pivot_y", "pivot_z", "root_distance", "seed", "hue",
    "index"
};

/** The branching curves that foliage grows on */
typedef struct box_mg_foliage_t {
    const box_mg_curves_t *c;    /**< The curves */
    const float *level;          /**< Level attribute, NULL without one */
    const float *root;           /**< Root distance attribute, NULL without one */
    int32_t highest;             /**< Highest level of the curves */
    bool *childless;             /**< Per curve, whether no curve grows from it */
    float *lengths;              /**< Length up to each point of the curve being sampled */
} box_mg_foliage_t;

/** A leaf or a card of a frond about to be written to Points */
typedef struct box_mg_leaf_t {
    vec3 position;               /**< Position of the stem */
    vec3 dir;                    /**< Unit direction from the stem to the tip, +Y */
    vec3 face;                   /**< Unit normal of the face, perpendicular to dir, +Z */
    vec3 scale;                  /**< Scale */
    float values[BOX_MG_LEAF_ATTRS]; /**< The attributes, as box_mg_leaf_names */
} box_mg_leaf_t;

/** The envelope of a crown */
typedef struct box_mg_crown_t {
    MeshCrownShape shape;        /**< Shape of the envelope */
    vec3 center;                 /**< Center of the ellipsoid */
    vec3 size;                   /**< Half extents of the ellipsoid */
    vec3 centroid;               /**< Mean of the vertices */
    vec3 min;                    /**< Corner of the density grid */
    float cell;                  /**< Edge of a cell of the density grid */
    int32_t n[3];                /**< Cells of the density grid per axis */
    float *density;              /**< Density per cell, 1 at the densest, NULL for an ellipsoid */
} box_mg_crown_t;

/** Draw a float in -1..1.
 *
 * @param rand The sequence
 * @return The float
 */
static float box_mg_leaf_signed(
    box_mg_rand_t *rand)
{
    return box_mg_rand_float(rand) * 2.0f - 1.0f;
}

/** e to the power x from IEEE operations only, the same on every platform.
 *
 * @param x The exponent
 * @return The power
 */
static float box_mg_exp(
    float x)
{
    float z = glm_clamp(x, -80.0f, 80.0f) * 1.44269504f;
    float whole = floorf(z + 0.5f), g = (z - whole) * 0.693147181f;
    float ex = 1.0f + g * (1.0f + g * (0.5f + g * (1.0f / 6.0f + g * (1.0f / 24.0f +
        g * (1.0f / 120.0f + g * (1.0f / 720.0f))))));

    return ldexpf(ex, (int)whole);
}

/** Turn a vector about a unit axis.
 *
 * @param v The vector, turned in place
 * @param axis The unit axis
 * @param angle The angle in radians, counter-clockwise looking down the axis
 */
static void box_mg_turn(
    vec3 v,
    const vec3 axis,
    float angle)
{
    float s = 0.0f, c = 1.0f, d = glm_vec3_dot((float*)axis, v);
    vec3 cross, out;
    if (angle != 0.0f) {
        box_mg_sincos(angle, &s, &c);
        glm_vec3_cross((float*)axis, v, cross);
        glm_vec3_scale(v, c, out);
        glm_vec3_muladds(cross, s, out);
        glm_vec3_muladds((float*)axis, d * (1.0f - c), out);
        glm_vec3_copy(out, v);
    }
}

/** The part of a vector perpendicular to a unit direction, normalized.
 *
 * @param v The vector
 * @param dir The unit direction
 * @param out Receives the unit perpendicular
 * @return Whether the vector has a perpendicular part
 */
static bool box_mg_perpendicular(
    const vec3 v,
    const vec3 dir,
    vec3 out)
{
    vec3 p;
    glm_vec3_copy((float*)v, p);
    glm_vec3_muladds((float*)dir, -glm_vec3_dot((float*)v, (float*)dir), p);
    bool result = glm_vec3_norm2(p) >= 1e-10f;
    if (result) {
        glm_vec3_normalize_to(p, out);
    }

    return result;
}

/** Interpolate an attribute of curves at a sample.
 *
 * @param values The attribute
 * @param s The sample
 * @return The value
 */
static float box_mg_sample_attr(
    const float *values,
    const box_mg_sample_t *s)
{
    int32_t b = s->fraction > 0.0f ? s->point + 1 : s->point;

    return values[s->point] + (values[b] - values[s->point]) * s->fraction;
}

/** Find the levels, root distances and childless curves of branching curves.
 *
 * @param f The foliage
 * @param c The curves
 */
static void box_mg_foliage_init(
    box_mg_foliage_t *f,
    const box_mg_curves_t *c)
{
    int32_t i;
    ecs_os_memset_t(f, 0, box_mg_foliage_t);
    f->c = c;
    f->level = box_mg_attr_find(c->attrs, c->attr_count, "level");
    f->root = box_mg_attr_find(c->attrs, c->attr_count, "root_distance");
    f->childless = ecs_os_malloc_n(bool, c->curve_count + 1);
    f->lengths = ecs_os_malloc_n(float, c->point_count + 1);
    for (i = 0; i < c->curve_count; i ++) {
        f->childless[i] = true;
    }

    for (i = 0; i < c->curve_count; i ++) {
        int32_t p = c->parents ? c->parents[i] : -1;
        if (p >= 0 && p < c->curve_count) {
            f->childless[p] = false;
        }

        if (f->level && c->offsets[i + 1] > c->offsets[i]) {
            int32_t level = (int32_t)f->level[c->offsets[i]];
            f->highest = level > f->highest ? level : f->highest;
        }
    }
}

/** Free what foliage owns.
 *
 * @param f The foliage
 */
static void box_mg_foliage_fini(
    box_mg_foliage_t *f)
{
    ecs_os_free(f->childless);
    ecs_os_free(f->lengths);
}

/** Get the level of a curve.
 *
 * @param f The foliage
 * @param curve The curve
 * @return The level, 0 without levels
 */
static int32_t box_mg_foliage_level(
    const box_mg_foliage_t *f,
    int32_t curve)
{
    return f->level ? (int32_t)f->level[f->c->offsets[curve]] : 0;
}

/** Check whether a curve carries foliage.
 *
 * @param f The foliage
 * @param placement Where the foliage grows
 * @param level Lowest level of MeshLeafLevels
 * @param curve The curve
 * @return Whether it carries foliage
 */
static bool box_mg_foliage_carries(
    const box_mg_foliage_t *f,
    MeshLeafPlacement placement,
    int32_t level,
    int32_t curve)
{
    const box_mg_curves_t *c = f->c;
    bool result = false;
    if (c->offsets[curve + 1] - c->offsets[curve] < 2) {
        result = false;
    } else if (placement == MeshLeafTips) {
        result = f->childless[curve];
    } else if (placement == MeshLeafLevels) {
        result = box_mg_foliage_level(f, curve) >= level;
    } else {
        result = box_mg_foliage_level(f, curve) == f->highest;
    }

    return result;
}

/** Write the common attributes of a leaf on a curve: t, curve, level, pivot, root distance.
 *
 * @param f The foliage
 * @param curve The curve
 * @param s The sample of the leaf
 * @param distance Distance of the sample from the start of the curve
 * @param length Length of the curve
 * @param leaf The leaf
 */
static void box_mg_leaf_on_curve(
    const box_mg_foliage_t *f,
    int32_t curve,
    const box_mg_sample_t *s,
    float distance,
    float length,
    box_mg_leaf_t *leaf)
{
    const box_mg_curves_t *c = f->c;
    const float *pivot = c->positions[c->offsets[curve]];
    glm_vec3_copy((float*)s->position, leaf->position);
    leaf->values[0] = length > 0.0f ? distance / length : 0.0f;
    leaf->values[1] = (float)curve;
    leaf->values[2] = (float)box_mg_foliage_level(f, curve);
    leaf->values[3] = pivot[0];
    leaf->values[4] = pivot[1];
    leaf->values[5] = pivot[2];
    leaf->values[6] = f->root ? box_mg_sample_attr(f->root, s) : distance;
}

/** Add the attributes of the points of Leaves and Needles.
 *
 * @param p The points, resized
 * @param attrs Receives the values of each attribute
 */
static void box_mg_leaf_attrs_add(
    box_mg_points_t *p,
    float **attrs)
{
    int32_t a;
    for (a = 0; a < BOX_MG_LEAF_ATTRS; a ++) {
        attrs[a] = box_mg_attr_add(p->attrs, &p->attr_count, box_mg_leaf_names[a], p->count);
    }
}

/** Write a leaf to a point.
 *
 * @param p The points
 * @param attrs The values of each attribute
 * @param k The point
 * @param leaf The leaf
 */
static void box_mg_leaf_write(
    box_mg_points_t *p,
    float **attrs,
    int32_t k,
    const box_mg_leaf_t *leaf)
{
    mat3 m;
    int32_t a;
    glm_vec3_cross((float*)leaf->dir, (float*)leaf->face, m[0]);
    glm_vec3_normalize(m[0]);
    glm_vec3_copy((float*)leaf->dir, m[1]);
    glm_vec3_cross(m[0], m[1], m[2]);
    glm_mat3_quat(m, p->rotations[k]);
    glm_quat_normalize(p->rotations[k]);
    glm_vec3_copy((float*)leaf->position, p->positions[k]);
    glm_vec3_copy((float*)leaf->scale, p->scales[k]);
    for (a = 0; a < BOX_MG_LEAF_ATTRS; a ++) {
        if (attrs[a]) {
            attrs[a][k] = leaf->values[a];
        }
    }
}

/** Count the nodes of leaves on a curve.
 *
 * @param n The node
 * @param range Length of the range of the curve
 * @return The nodes
 */
static int32_t box_mg_leaf_nodes(
    const MeshLeaves *n,
    float range)
{
    int32_t result = n->count > 0 ? n->count : 6;
    if (n->placement != MeshLeafTips && n->density > 0.0f) {
        result = (int32_t)glm_min(floorf(n->density * range + 0.5f), (float)BOX_MG_LEAF_NODES);
    }

    result = result < 0 ? 0 : result;

    return result > BOX_MG_LEAF_NODES ? BOX_MG_LEAF_NODES : result;
}

/** Find the center of the crown that the faces of MeshLeafFaceOut look away from.
 *
 * @param n The node
 * @param f The foliage
 * @param out Receives the center
 */
static void box_mg_leaf_center(
    const MeshLeaves *n,
    const box_mg_foliage_t *f,
    vec3 out)
{
    const box_mg_curves_t *c = f->c;
    vec3 lo = { 1e30f, 1e30f, 1e30f }, hi = { -1e30f, -1e30f, -1e30f };
    int32_t i, j;
    bool any = false;
    glm_vec3_copy((float*)n->center, out);
    bool fixed = n->center[0] != 0.0f || n->center[1] != 0.0f || n->center[2] != 0.0f;
    for (i = 0; !fixed && i < c->curve_count; i ++) {
        if (!box_mg_foliage_carries(f, n->placement, n->level, i)) {
            continue;
        }

        for (j = c->offsets[i]; j < c->offsets[i + 1]; j ++) {
            glm_vec3_minv(lo, c->positions[j], lo);
            glm_vec3_maxv(hi, c->positions[j], hi);
            any = true;
        }
    }

    if (any) {
        glm_vec3_center(lo, hi, out);
    }
}

/** Turn a leaf: its face by the orientation, then the random tilt and bank.
 *
 * @param n The node
 * @param s The sample of its node on the twig
 * @param roll Turn of the leaf around the twig
 * @param center Center of the crown
 * @param tilt Random tilt in -1..1
 * @param bank Random bank in -1..1
 * @param leaf The leaf, whose position is set
 */
static void box_mg_leaf_orient(
    const MeshLeaves *n,
    const box_mg_sample_t *s,
    float roll,
    const vec3 center,
    float tilt,
    float bank,
    box_mg_leaf_t *leaf)
{
    float sr, cr, sa, ca;
    vec3 bin, radial, side, along, axis, out;
    box_mg_sincos(roll, &sr, &cr);
    box_mg_sincos(n->angle, &sa, &ca);
    glm_vec3_cross((float*)s->tangent, (float*)s->normal, bin);
    glm_vec3_scale((float*)s->normal, cr, radial);
    glm_vec3_muladds(bin, sr, radial);
    glm_vec3_normalize(radial);
    glm_vec3_scale((float*)s->tangent, ca, leaf->dir);
    glm_vec3_muladds(radial, sa, leaf->dir);
    glm_vec3_normalize(leaf->dir);
    glm_vec3_cross((float*)s->tangent, radial, side);
    glm_vec3_normalize(side);
    glm_vec3_cross(side, leaf->dir, along);
    glm_vec3_normalize(along);
    glm_vec3_copy(along, leaf->face);
    if (n->orientation == MeshLeafFaceUp) {
        if (!box_mg_perpendicular((vec3){ 0.0f, 1.0f, 0.0f }, leaf->dir, leaf->face)) {
            glm_vec3_copy(along, leaf->face);
        }
    } else if (n->orientation == MeshLeafFaceOut) {
        glm_vec3_sub(leaf->position, (float*)center, out);
        if (!box_mg_perpendicular(out, leaf->dir, leaf->face)) {
            glm_vec3_copy(along, leaf->face);
        }
    }

    glm_vec3_cross(leaf->dir, leaf->face, axis);
    glm_vec3_normalize(axis);
    box_mg_turn(leaf->dir, axis, n->tilt * tilt);
    box_mg_turn(leaf->face, axis, n->tilt * tilt);
    box_mg_turn(leaf->face, leaf->dir, n->bank * bank);
    glm_vec3_normalize(leaf->dir);
    if (!box_mg_perpendicular(leaf->face, leaf->dir, leaf->face)) {
        glm_vec3_copy(along, leaf->face);
    }
}

/** Place the leaves of one curve.
 *
 * @param n The node
 * @param f The foliage
 * @param curve The curve
 * @param center Center of the crown
 * @param p The points, NULL to count them
 * @param attrs The values of each attribute
 * @param k First point of the curve
 * @return Number of leaves
 */
static int32_t box_mg_leaves_curve(
    const MeshLeaves *n,
    const box_mg_foliage_t *f,
    int32_t curve,
    const vec3 center,
    box_mg_points_t *p,
    float **attrs,
    int32_t k)
{
    const box_mg_curves_t *c = f->c;
    int32_t first = c->offsets[curve], count = c->offsets[curve + 1] - first, i, j, l;
    int32_t per_node = n->per_node > 0 ? n->per_node : 1;
    float start = glm_clamp(n->start, 0.0f, 1.0f);
    float end = glm_clamp(n->end > 0.0f ? n->end : 1.0f, 0.0f, 1.0f);
    float length = box_mg_curve_lengths(c, curve, f->lengths);
    bool tips = n->placement == MeshLeafTips;
    per_node = per_node > BOX_MG_LEAF_PER_NODE ? BOX_MG_LEAF_PER_NODE : per_node;
    int32_t nodes = box_mg_leaf_nodes(n, fabsf(end - start) * length);
    float phyllotaxis = n->phyllotaxis != 0.0f ? n->phyllotaxis : BOX_MG_GOLDEN_ANGLE;
    float size = n->size > 0.0f ? n->size : 0.1f;
    float tip = n->size_tip > 0.0f ? n->size_tip : 1.0f;
    float per_level = n->size_level > 0.0f ? n->size_level : 1.0f;
    int32_t level = box_mg_foliage_level(f, curve);
    box_mg_rand_t rand;
    box_mg_rand_seed(&rand, n->seed, (uint32_t)curve);
    for (l = level; l < f->highest; l ++) {
        size *= per_level;
    }

    for (i = 0; p && i < nodes; i ++) {
        float u = tips ? 1.0f : ((float)i + 0.5f) / (float)nodes;
        float t = tips ? 1.0f : start + (end - start) * u;
        box_mg_sample_t s;
        box_mg_curve_sample(c, first, count, f->lengths, t * length, &s);
        for (j = 0; j < per_node; j ++, k ++) {
            float roll = (float)i * phyllotaxis + (float)j * 2.0f * GLM_PIf / (float)per_node;
            float r_size = box_mg_leaf_signed(&rand);
            float r_tilt = box_mg_leaf_signed(&rand);
            float r_bank = box_mg_leaf_signed(&rand);
            float seed = box_mg_rand_float(&rand);
            float hue = box_mg_rand_float(&rand);
            float scale = size * (1.0f + (tip - 1.0f) * u) *
                glm_max(0.0f, 1.0f + n->size_variation * r_size);
            box_mg_leaf_t leaf = {0};
            box_mg_leaf_on_curve(f, curve, &s, t * length, length, &leaf);
            box_mg_leaf_orient(n, &s, roll, center, r_tilt, r_bank, &leaf);
            glm_vec3_fill(leaf.scale, scale);
            leaf.values[7] = seed;
            leaf.values[8] = hue;
            leaf.values[9] = (float)(i * per_node + j);
            box_mg_leaf_write(p, attrs, k, &leaf);
        }
    }

    return nodes * per_node;
}

/** Cells per axis of the grid that finds the nearest kept point of a thinned point, at most */
#define BOX_MG_THIN_GRID (32)

/** Grid of the kept points of a thinning, by the cell of their middle. */
typedef struct box_mg_thin_grid_t {
    vec3 min;          /**< Corner of the grid */
    float cell;        /**< Size of a cell */
    int32_t dim[3];    /**< Cells per axis */
    int32_t *start;    /**< First entry of a cell in points, one more than the cells */
    int32_t *points;   /**< Kept points by cell */
} box_mg_thin_grid_t;

/** Get the cell of a position, clamped to the grid.
 *
 * @param g The grid
 * @param v The position
 * @param out Receives the cell coordinates
 */
static void box_mg_thin_cell(
    const box_mg_thin_grid_t *g,
    const vec3 v,
    int32_t out[3])
{
    int32_t a;
    for (a = 0; a < 3; a ++) {
        int32_t c = (int32_t)floorf((v[a] - g->min[a]) / g->cell);
        out[a] = c < 0 ? 0 : c >= g->dim[a] ? g->dim[a] - 1 : c;
    }
}

/** Put the kept points in a grid over the middles of all points, with about one kept point
 * per cell.
 *
 * @param g The grid to fill
 * @param mid Middles of all points
 * @param kept Whether a point is kept
 * @param count Number of points
 * @param kept_count Number of kept points
 */
static void box_mg_thin_grid_init(
    box_mg_thin_grid_t *g,
    vec3 *mid,
    const bool *kept,
    int32_t count,
    int32_t kept_count)
{
    vec3 max;
    int32_t k, a, cells;
    glm_vec3_copy(mid[0], g->min);
    glm_vec3_copy(mid[0], max);
    for (k = 1; k < count; k ++) {
        glm_vec3_minv(g->min, mid[k], g->min);
        glm_vec3_maxv(max, mid[k], max);
    }

    float ext = 0.0f;
    double volume = 1.0;
    for (a = 0; a < 3; a ++) {
        ext = glm_max(ext, max[a] - g->min[a]);
    }

    ext = glm_max(ext, 1e-6f);
    for (a = 0; a < 3; a ++) {
        volume *= (double)glm_max(max[a] - g->min[a], ext * 0.01f);
    }

    g->cell = glm_max((float)cbrt(volume / (double)kept_count), ext / BOX_MG_THIN_GRID);
    cells = 1;
    for (a = 0; a < 3; a ++) {
        int32_t dim = (int32_t)((max[a] - g->min[a]) / g->cell) + 1;
        g->dim[a] = dim < 1 ? 1 : dim > BOX_MG_THIN_GRID ? BOX_MG_THIN_GRID : dim;
        cells *= g->dim[a];
    }

    g->start = ecs_os_calloc_n(int32_t, cells + 1);
    g->points = ecs_os_malloc_n(int32_t, kept_count);
    int32_t *cell_of = ecs_os_malloc_n(int32_t, count);
    for (k = 0; k < count; k ++) {
        int32_t c[3];
        if (!kept[k]) {
            continue;
        }

        box_mg_thin_cell(g, mid[k], c);
        cell_of[k] = (c[2] * g->dim[1] + c[1]) * g->dim[0] + c[0];
        g->start[cell_of[k] + 1] ++;
    }

    for (k = 0; k < cells; k ++) {
        g->start[k + 1] += g->start[k];
    }

    int32_t *fill = ecs_os_malloc_n(int32_t, cells);
    ecs_os_memcpy_n(fill, g->start, int32_t, cells);
    for (k = 0; k < count; k ++) {
        if (kept[k]) {
            g->points[fill[cell_of[k]] ++] = k;
        }
    }

    ecs_os_free(fill);
    ecs_os_free(cell_of);
}

/** Find the kept point whose middle is nearest to a position, searching shells of cells
 * outward until no nearer one can be found: a point in shell r is at least r - 1 cells away.
 *
 * @param g The grid
 * @param mid Middles of all points
 * @param v The position
 * @return The nearest kept point
 */
static int32_t box_mg_thin_nearest(
    const box_mg_thin_grid_t *g,
    vec3 *mid,
    vec3 v)
{
    int32_t c[3], r, x, y, z, i, best = -1;
    int32_t reach = g->dim[0] > g->dim[1] ? g->dim[0] : g->dim[1];
    reach = reach > g->dim[2] ? reach : g->dim[2];
    float best_d = 0.0f;
    box_mg_thin_cell(g, v, c);
    for (r = 0; r <= reach; r ++) {
        if (best >= 0 && (float)(r - 1) * g->cell > sqrtf(best_d)) {
            break;
        }

        for (z = c[2] - r; z <= c[2] + r; z ++) {
            for (y = c[1] - r; y <= c[1] + r; y ++) {
                for (x = c[0] - r; x <= c[0] + r; x ++) {
                    if (x < 0 || y < 0 || z < 0 || x >= g->dim[0] || y >= g->dim[1] ||
                        z >= g->dim[2])
                    {
                        continue;
                    }

                    if (abs(x - c[0]) != r && abs(y - c[1]) != r && abs(z - c[2]) != r) {
                        continue;
                    }

                    int32_t cell = (z * g->dim[1] + y) * g->dim[0] + x;
                    for (i = g->start[cell]; i < g->start[cell + 1]; i ++) {
                        int32_t k = g->points[i];
                        float d = glm_vec3_distance2(mid[k], v);
                        if (best < 0 || d < best_d) {
                            best = k;
                            best_d = d;
                        }
                    }
                }
            }
        }
    }

    return best;
}

/** Free a grid of kept points.
 *
 * @param g The grid
 */
static void box_mg_thin_grid_fini(
    box_mg_thin_grid_t *g)
{
    ecs_os_free(g->start);
    ecs_os_free(g->points);
}

/** Thin the points of leaves or cards to the ones whose seed is under keep, the one with the
 * lowest seed when none is, and merge each other point into the kept point nearest to it.
 *
 * A kept point grows to the summed area of its points and moves its middle to their area
 * weighted middle, so the cards cover the crown where the dropped ones did.
 *
 * @param p The points, with the attributes of Leaves
 * @param keep Fraction of the points kept, no thinning outside 0..1
 */
static void box_mg_points_thin(
    box_mg_points_t *p,
    float keep)
{
    const float *seed = box_mg_attr_find(p->attrs, p->attr_count, "seed");
    bool *kept = NULL;
    vec3 *mid = NULL, *sum = NULL;
    double *area = NULL;
    int32_t k, a, lowest = 0, count = 0;
    if (keep <= 0.0f || keep >= 1.0f || !p->count) {
        goto done;
    }

    kept = ecs_os_calloc_n(bool, p->count);
    mid = ecs_os_malloc_n(vec3, p->count);
    sum = ecs_os_calloc_n(vec3, p->count);
    area = ecs_os_calloc_n(double, p->count);
    for (k = 0; k < p->count; k ++) {
        float s;
        if (seed) {
            s = seed[k];
        } else {
            box_mg_rand_t rand;
            box_mg_rand_seed(&rand, 0, (uint32_t)k);
            s = box_mg_rand_float(&rand);
        }

        vec3 up = { 0.0f, 1.0f, 0.0f }, along;
        glm_quat_rotatev(p->rotations[k], up, along);
        glm_vec3_scale(along, 0.5f * p->scales[k][1], along);
        glm_vec3_add(p->positions[k], along, mid[k]);
        kept[k] = s < keep;
        count += kept[k];
        if (seed && seed[k] < seed[lowest]) {
            lowest = k;
        }
    }

    if (!count) {
        kept[lowest] = true;
        count = 1;
    }

    box_mg_thin_grid_t grid;
    box_mg_thin_grid_init(&grid, mid, kept, p->count, count);
    for (k = 0; k < p->count; k ++) {
        double size = fabs((double)p->scales[k][0] * (double)p->scales[k][1]);
        int32_t to = kept[k] ? k : box_mg_thin_nearest(&grid, mid, mid[k]);
        area[to] += size;
        glm_vec3_muladds(mid[k], (float)size, sum[to]);
    }

    box_mg_thin_grid_fini(&grid);

    int32_t to = 0;
    for (k = 0; k < p->count; k ++) {
        if (!kept[k]) {
            continue;
        }

        double own = fabs((double)p->scales[k][0] * (double)p->scales[k][1]);
        float grow = own > 0.0 ? (float)sqrt(area[k] / own) : 1.0f;
        vec3 up = { 0.0f, 1.0f, 0.0f }, along, middle;
        if (area[k] > 0.0) {
            glm_vec3_scale(sum[k], (float)(1.0 / area[k]), middle);
        } else {
            glm_vec3_copy(mid[k], middle);
        }

        glm_quat_rotatev(p->rotations[k], up, along);
        glm_vec3_scale(along, -0.5f * p->scales[k][1] * grow, along);
        glm_vec3_add(middle, along, p->positions[to]);
        glm_vec4_copy(p->rotations[k], p->rotations[to]);
        glm_vec3_scale(p->scales[k], grow, p->scales[to]);
        for (a = 0; a < p->attr_count; a ++) {
            p->attrs[a].values[to] = p->attrs[a].values[k];
        }

        to ++;
    }

    box_mg_points_resize(p, to);
done:
    ecs_os_free(kept);
    ecs_os_free(mid);
    ecs_os_free(sum);
    ecs_os_free(area);
}

/** The keep of a Leaves or a Needles at a level of detail: lod_keep to the power of the level
 * times its keep.
 *
 * @param keep The keep of the node
 * @param lod_keep The lod_keep of the node
 * @param level The level
 */
static void box_mg_keep_lod(
    float *keep,
    float lod_keep,
    int32_t level)
{
    float f = box_mg_lod_factor(lod_keep, 0.5f, level);
    if (f < 1.0f) {
        *keep = (*keep > 0.0f ? glm_min(*keep, 1.0f) : 1.0f) * f;
    }
}

/** Reduce a MeshLeaves for a level of detail: fewer, larger leaves.
 *
 * @param node The snapshot of the MeshLeaves
 * @param level The level
 */
static void box_mesh_leaves_lod(
    void *node,
    int32_t level)
{
    MeshLeaves *n = node;
    box_mg_keep_lod(&n->keep, n->lod_keep, level);
}

/** Reduce a MeshNeedles for a level of detail: fewer, larger cards.
 *
 * @param node The snapshot of the MeshNeedles
 * @param level The level
 */
static void box_mesh_needles_lod(
    void *node,
    int32_t level)
{
    MeshNeedles *n = node;
    box_mg_keep_lod(&n->keep, n->lod_keep, level);
}

/** Place the leaves of a MeshLeaves on branching curves.
 *
 * @param node The MeshLeaves
 * @param inputs The curves
 * @param counts Values per port
 * @param out The points
 * @param error Unused
 * @return 0
 */
static int box_mesh_leaves_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshLeaves *n = node;
    box_mg_points_t *p = &out->points;
    box_mg_foliage_t f;
    float *attrs[BOX_MG_LEAF_ATTRS];
    int32_t i, total = 0, k = 0;
    vec3 center;
    (void)counts;
    (void)error;
    box_mg_foliage_init(&f, &inputs[0]->curves);
    for (i = 0; i < f.c->curve_count; i ++) {
        if (box_mg_foliage_carries(&f, n->placement, n->level, i)) {
            total += box_mg_leaves_curve(n, &f, i, NULL, NULL, NULL, 0);
        }
    }

    box_mg_leaf_center(n, &f, center);
    box_mg_points_resize(p, total);
    box_mg_leaf_attrs_add(p, attrs);
    for (i = 0; i < f.c->curve_count; i ++) {
        if (box_mg_foliage_carries(&f, n->placement, n->level, i)) {
            k += box_mg_leaves_curve(n, &f, i, center, p, attrs, k);
        }
    }

    box_mg_points_thin(p, n->keep);
    box_mg_foliage_fini(&f);

    return 0;
}

/** Place the cards of the fronds of one curve.
 *
 * @param n The node
 * @param f The foliage
 * @param curve The curve
 * @param p The points, NULL to count them
 * @param attrs The values of each attribute
 * @param k First point of the curve
 * @return Number of cards
 */
static int32_t box_mg_needles_curve(
    const MeshNeedles *n,
    const box_mg_foliage_t *f,
    int32_t curve,
    box_mg_points_t *p,
    float **attrs,
    int32_t k)
{
    const box_mg_curves_t *c = f->c;
    int32_t first = c->offsets[curve], count = c->offsets[curve + 1] - first, i, j;
    int32_t fronds = n->cards > 0 ? (n->cards < BOX_MG_FRONDS ? n->cards : BOX_MG_FRONDS) : 1;
    int32_t per = n->style == MeshNeedleSpruce ? 2 : 1;
    float start = glm_clamp(n->start, 0.0f, 1.0f);
    float end = glm_clamp(n->end > 0.0f ? n->end : 1.0f, 0.0f, 1.0f);
    float width = n->width > 0.0f ? n->width : 0.5f;
    float length = box_mg_curve_lengths(c, curve, f->lengths);
    vec3 up = { 0.0f, 1.0f, 0.0f };
    box_mg_rand_t rand;
    box_mg_rand_seed(&rand, n->seed, (uint32_t)curve);
    for (i = 0; p && i < fronds; i ++) {
        float da = (start + (end - start) * (float)i / (float)fronds) * length;
        float db = (start + (end - start) * (float)(i + 1) / (float)fronds) * length;
        box_mg_sample_t sa, sb;
        vec3 dir, face0, axis;
        box_mg_curve_sample(c, first, count, f->lengths, da, &sa);
        box_mg_curve_sample(c, first, count, f->lengths, db, &sb);
        glm_vec3_sub(sb.position, sa.position, dir);
        float span = glm_vec3_norm(dir);
        if (span > 1e-6f) {
            glm_vec3_scale(dir, 1.0f / span, dir);
        } else {
            glm_vec3_copy(sa.tangent, dir);
            span = 1e-6f;
        }

        glm_vec3_cross(dir, up, axis);
        if (n->droop != 0.0f && glm_vec3_norm2(axis) > 1e-10f) {
            glm_vec3_normalize(axis);
            box_mg_turn(dir, axis, -n->droop);
            glm_vec3_normalize(dir);
        }

        if (!box_mg_perpendicular(up, dir, face0) &&
            !box_mg_perpendicular(sa.normal, dir, face0)) {
            glm_vec3_copy((vec3){ 0.0f, 0.0f, 1.0f }, face0);
        }

        for (j = 0; j < per; j ++, k ++) {
            float r_tilt = box_mg_leaf_signed(&rand);
            float seed = box_mg_rand_float(&rand);
            float hue = box_mg_rand_float(&rand);
            float bank = per > 1 ? (j ? -n->bank : n->bank) : 0.0f;
            box_mg_leaf_t leaf = {0};
            box_mg_leaf_on_curve(f, curve, &sa, da, length, &leaf);
            glm_vec3_copy(dir, leaf.dir);
            glm_vec3_copy(face0, leaf.face);
            box_mg_turn(leaf.face, leaf.dir, bank);
            glm_vec3_cross(leaf.dir, leaf.face, axis);
            glm_vec3_normalize(axis);
            box_mg_turn(leaf.dir, axis, n->tilt * r_tilt);
            box_mg_turn(leaf.face, axis, n->tilt * r_tilt);
            glm_vec3_normalize(leaf.dir);
            if (!box_mg_perpendicular(leaf.face, leaf.dir, leaf.face)) {
                glm_vec3_copy(face0, leaf.face);
            }

            leaf.scale[0] = width * span;
            leaf.scale[1] = span;
            leaf.scale[2] = span;
            leaf.values[7] = seed;
            leaf.values[8] = hue;
            leaf.values[9] = (float)(i * per + j);
            box_mg_leaf_write(p, attrs, k, &leaf);
        }
    }

    return fronds * per;
}

/** Place the fronds of a MeshNeedles along branching curves.
 *
 * @param node The MeshNeedles
 * @param inputs The curves
 * @param counts Values per port
 * @param out The points
 * @param error Unused
 * @return 0
 */
static int box_mesh_needles_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshNeedles *n = node;
    box_mg_points_t *p = &out->points;
    box_mg_foliage_t f;
    float *attrs[BOX_MG_LEAF_ATTRS];
    int32_t i, total = 0, k = 0;
    (void)counts;
    (void)error;
    box_mg_foliage_init(&f, &inputs[0]->curves);
    for (i = 0; i < f.c->curve_count; i ++) {
        if (box_mg_foliage_carries(&f, n->placement, n->level, i)) {
            total += box_mg_needles_curve(n, &f, i, NULL, NULL, 0);
        }
    }

    box_mg_points_resize(p, total);
    box_mg_leaf_attrs_add(p, attrs);
    for (i = 0; i < f.c->curve_count; i ++) {
        if (box_mg_foliage_carries(&f, n->placement, n->level, i)) {
            k += box_mg_needles_curve(n, &f, i, p, attrs, k);
        }
    }

    box_mg_points_thin(p, n->keep);
    box_mg_foliage_fini(&f);

    return 0;
}

/** Append a sheet of quads to the template of a card: cols columns across its width and rows
 * rows along its length, its stem at the origin, +Y to its tip and +Z its face.
 *
 * @param m The template
 * @param cols Columns of vertices, 2 or 3
 * @param rows Rows of quads
 * @param width Width of the card
 * @param bend Depth of the fold and the curl, 0 for a flat sheet
 * @param cross Turn the sheet a quarter turn about Y
 */
static void box_mg_card_sheet(
    box_mg_mesh_t *m,
    int32_t cols,
    int32_t rows,
    float width,
    float bend,
    bool cross)
{
    int32_t vbase = m->vertex_count, ibase = m->index_count, r, k;
    box_mg_mesh_resize(m, vbase + cols * (rows + 1), ibase + (cols - 1) * rows * 6);
    for (r = 0; r <= rows; r ++) {
        for (k = 0; k < cols; k ++) {
            int32_t v = vbase + r * cols + k;
            float u = (float)k / (float)(cols - 1), y = (float)r / (float)rows;
            float x = (u - 0.5f) * width, across = x * 2.0f / width;
            float sign = across > 1e-6f ? 1.0f : (across < -1e-6f ? -1.0f : 0.0f);
            float z = bend * (fabsf(across) * 0.6f - y * y);
            vec3 normal = { -bend * 1.2f / width * sign, 2.0f * bend * y, 1.0f };
            glm_vec3_normalize(normal);
            if (cross) {
                m->positions[v][0] = z;
                m->positions[v][1] = y;
                m->positions[v][2] = -x;
                m->normals[v][0] = normal[2];
                m->normals[v][1] = normal[1];
                m->normals[v][2] = -normal[0];
            } else {
                m->positions[v][0] = x;
                m->positions[v][1] = y;
                m->positions[v][2] = z;
                glm_vec3_copy(normal, m->normals[v]);
            }

            m->uvs[v][0] = u;
            m->uvs[v][1] = 1.0f - y;
        }
    }

    for (r = 0; r < rows; r ++) {
        for (k = 0; k + 1 < cols; k ++) {
            uint32_t a = (uint32_t)(vbase + r * cols + k), b = a + 1;
            uint32_t d = a + (uint32_t)cols, e = d + 1;
            uint32_t *idx = &m->indices[ibase + (r * (cols - 1) + k) * 6];
            idx[0] = a;
            idx[1] = b;
            idx[2] = e;
            idx[3] = a;
            idx[4] = e;
            idx[5] = d;
        }
    }
}

/** Give the template of a card a back: its triangles wound the other way, on a copy of its
 * vertices with the normals turned around, or on its own vertices when shared.
 *
 * @param m The template
 * @param shared Wind the back on the vertices of the front
 */
static void box_mg_card_back(
    box_mg_mesh_t *m,
    bool shared)
{
    int32_t vc = m->vertex_count, ic = m->index_count, i;
    uint32_t offset = shared ? 0u : (uint32_t)vc;
    box_mg_mesh_resize(m, shared ? vc : vc * 2, ic * 2);
    for (i = 0; !shared && i < vc; i ++) {
        glm_vec3_copy(m->positions[i], m->positions[vc + i]);
        glm_vec3_negate_to(m->normals[i], m->normals[vc + i]);
        glm_vec2_copy(m->uvs[i], m->uvs[vc + i]);
    }

    for (i = 0; i + 2 < ic; i += 3) {
        m->indices[ic + i] = m->indices[i] + offset;
        m->indices[ic + i + 1] = m->indices[i + 2] + offset;
        m->indices[ic + i + 2] = m->indices[i + 1] + offset;
    }
}

/** Build the template of the card of a MeshCards.
 *
 * @param n The node
 * @param mesh The mesh input, NULL for the shape
 * @param out The empty template
 */
static void box_mg_card_template(
    const MeshCards *n,
    const box_mg_value_t *mesh,
    box_mg_value_t *out)
{
    int32_t rows = n->segments > 0 ? n->segments : 2;
    float width = n->width > 0.0f ? n->width : 1.0f;
    rows = rows > BOX_MG_CARD_SEGMENTS ? BOX_MG_CARD_SEGMENTS : rows;
    if (mesh) {
        box_mg_value_copy(out, mesh);
    } else if (n->shape == MeshCardBent) {
        box_mg_card_sheet(&out->mesh, 3, rows, width, n->bend, false);
    } else {
        box_mg_card_sheet(&out->mesh, 2, rows, width, 0.0f, false);
        if (n->shape == MeshCardCross) {
            box_mg_card_sheet(&out->mesh, 2, rows, width, 0.0f, true);
        }
    }

    if (!mesh && n->double_sided) {
        box_mg_card_back(&out->mesh, n->shared_back);
    }
}

/** Reduce a MeshCards for a level of detail: lod_segments to the power of the level of its
 * segments, at least one kept, and from lod_flat flat cards of one row with a shared back.
 *
 * @param node The snapshot of the MeshCards
 * @param level The level
 */
static void box_mesh_cards_lod(
    void *node,
    int32_t level)
{
    MeshCards *n = node;
    int32_t rows = n->segments > 0 ? n->segments : 2;
    rows = rows > BOX_MG_CARD_SEGMENTS ? BOX_MG_CARD_SEGMENTS : rows;
    int32_t reduced = (int32_t)((float)rows * box_mg_lod_factor(n->lod_segments, 0.5f, level) +
        0.5f);
    reduced = reduced < 1 ? 1 : reduced;
    if (reduced != rows) {
        n->segments = reduced;
    }

    if (n->lod_flat > 0 && level >= n->lod_flat) {
        n->shape = n->shape == MeshCardBent ? MeshCardQuad : n->shape;
        n->segments = 1;
        n->shared_back = true;
    }
}

/** Instance the card of a MeshCards on Points with the data of foliage.
 *
 * @param node The MeshCards
 * @param inputs The points and the card mesh
 * @param counts Values per port
 * @param out The mesh
 * @param error Unused
 * @return 0
 */
static int box_mesh_cards_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshCards *n = node;
    const box_mg_points_t *p = &inputs[0]->points;
    box_mg_mesh_t *m = &out->mesh;
    box_mg_value_t *tmpl = box_mg_value_new(MeshValueMesh);
    const box_mg_mesh_t *src = &tmpl->mesh;
    float flutter = glm_clamp(n->flutter, 0.0f, 1.0f), reach = 1e-6f;
    float *data[8];
    int32_t vc, ic, k, i;
    (void)error;
    box_mg_card_template(n, counts[1] ? inputs[1] : NULL, tmpl);
    vc = src->vertex_count;
    ic = src->index_count;
    for (i = 0; i < vc; i ++) {
        reach = glm_max(reach, src->positions[i][1]);
    }

    const float *pivot[3] = {
        box_mg_attr_find(p->attrs, p->attr_count, "pivot_x"),
        box_mg_attr_find(p->attrs, p->attr_count, "pivot_y"),
        box_mg_attr_find(p->attrs, p->attr_count, "pivot_z")
    };
    const float *level = box_mg_attr_find(p->attrs, p->attr_count, "level");
    const float *seed = box_mg_attr_find(p->attrs, p->attr_count, "seed");
    const float *hue = box_mg_attr_find(p->attrs, p->attr_count, "hue");
    const float *root = box_mg_attr_find(p->attrs, p->attr_count, "root_distance");
    bool pivots = pivot[0] && pivot[1] && pivot[2];
    box_mg_mesh_resize(m, p->count * vc, p->count * ic);
    float *card = box_mg_attr_add(m->attrs, &m->attr_count, "card", m->vertex_count);
    for (i = 0; i < 8; i ++) {
        char name[BOX_MESH_ATTR_NAME];
        ecs_os_snprintf(name, ECS_SIZEOF(name), "%s%d", BOX_MESH_DATA_ATTR, i);
        data[i] = box_mg_attr_add(m->attrs, &m->attr_count, name, m->vertex_count);
    }

    for (k = 0; k < p->count; k ++) {
        mat3 r;
        vec3 s, at;
        int32_t vbase = k * vc, ibase = k * ic;
        box_mg_rand_t rand;
        box_mg_rand_seed(&rand, 0, (uint32_t)k);
        float fallback_seed = box_mg_rand_float(&rand);
        float fallback_hue = box_mg_rand_float(&rand);
        float lv = level ? floorf(level[k] + 0.5f) : 0.0f;
        float d[8] = {
            0.0f, 0.0f, 0.0f, 0.0f,
            seed ? seed[k] : fallback_seed, hue ? hue[k] : fallback_hue,
            root ? root[k] : 0.0f, 1.0f
        };
        glm_quat_mat3(p->rotations[k], r);
        for (i = 0; i < 3; i ++) {
            s[i] = p->scales[k][i] != 0.0f ? p->scales[k][i] : 1.0f;
            at[i] = pivots ? pivot[i][k] : p->positions[k][i];
            d[BOX_MESH_DATA_PIVOT + i] = at[i];
        }

        for (i = 0; i < vc; i ++) {
            int32_t v = vbase + i, ch;
            vec3 pos, nm = { src->normals[i][0] / s[0], src->normals[i][1] / s[1],
                src->normals[i][2] / s[2] };
            glm_vec3_mul(src->positions[i], s, pos);
            glm_mat3_mulv(r, pos, m->positions[v]);
            glm_vec3_add(m->positions[v], p->positions[k], m->positions[v]);
            glm_mat3_mulv(r, nm, m->normals[v]);
            glm_vec3_normalize(m->normals[v]);
            glm_vec2_copy(src->uvs[i], m->uvs[v]);
            float weight = flutter * glm_clamp(src->positions[i][1] / reach, 0.0f, 1.0f);
            d[BOX_MESH_DATA_FLUTTER] = lv + 0.5f * weight;
            card[v] = (float)k;
            for (ch = 0; ch < 8; ch ++) {
                if (data[ch]) {
                    data[ch][v] = d[ch];
                }
            }
        }

        bool flip = s[0] * s[1] * s[2] < 0.0f;
        for (i = 0; i < ic; i ++) {
            int32_t from = flip && i % 3 ? i + (i % 3 == 1 ? 1 : -1) : i;
            m->indices[ibase + i] = src->indices[from] + (uint32_t)vbase;
        }
    }

    box_mg_value_release(tmpl);

    return 0;
}

/** Get the density of a crown at a cell, 0 outside the grid.
 *
 * @param cr The crown
 * @param x Column
 * @param y Row
 * @param z Layer
 * @return The density
 */
static float box_mg_crown_cell(
    const box_mg_crown_t *cr,
    int32_t x,
    int32_t y,
    int32_t z)
{
    float result = 0.0f;
    if (x >= 0 && y >= 0 && z >= 0 && x < cr->n[0] && y < cr->n[1] && z < cr->n[2]) {
        result = cr->density[(z * cr->n[1] + y) * cr->n[0] + x];
    }

    return result;
}

/** Sample the density of a crown at a position, trilinear between the cell centers.
 *
 * @param cr The crown
 * @param p The position
 * @return The density
 */
static float box_mg_crown_density(
    const box_mg_crown_t *cr,
    const vec3 p)
{
    int32_t i[3], a;
    float f[3];
    for (a = 0; a < 3; a ++) {
        float g = (p[a] - cr->min[a]) / cr->cell - 0.5f;
        float fl = floorf(g);
        i[a] = (int32_t)fl;
        f[a] = g - fl;
    }

    float c00 = box_mg_crown_cell(cr, i[0], i[1], i[2]) * (1.0f - f[0]) +
        box_mg_crown_cell(cr, i[0] + 1, i[1], i[2]) * f[0];
    float c10 = box_mg_crown_cell(cr, i[0], i[1] + 1, i[2]) * (1.0f - f[0]) +
        box_mg_crown_cell(cr, i[0] + 1, i[1] + 1, i[2]) * f[0];
    float c01 = box_mg_crown_cell(cr, i[0], i[1], i[2] + 1) * (1.0f - f[0]) +
        box_mg_crown_cell(cr, i[0] + 1, i[1], i[2] + 1) * f[0];
    float c11 = box_mg_crown_cell(cr, i[0], i[1] + 1, i[2] + 1) * (1.0f - f[0]) +
        box_mg_crown_cell(cr, i[0] + 1, i[1] + 1, i[2] + 1) * f[0];
    float c0 = c00 * (1.0f - f[1]) + c10 * f[1], c1 = c01 * (1.0f - f[1]) + c11 * f[1];

    return c0 * (1.0f - f[2]) + c1 * f[2];
}

/** Blur the density grid of a crown once along each axis by 1 2 1.
 *
 * @param cr The crown
 * @param tmp Scratch of the size of the grid
 */
static void box_mg_crown_blur(
    box_mg_crown_t *cr,
    float *tmp)
{
    int32_t a, x, y, z;
    for (a = 0; a < 3; a ++) {
        int32_t o[3] = { a == 0, a == 1, a == 2 };
        for (z = 0; z < cr->n[2]; z ++) {
            for (y = 0; y < cr->n[1]; y ++) {
                for (x = 0; x < cr->n[0]; x ++) {
                    tmp[(z * cr->n[1] + y) * cr->n[0] + x] =
                        0.25f * box_mg_crown_cell(cr, x - o[0], y - o[1], z - o[2]) +
                        0.5f * box_mg_crown_cell(cr, x, y, z) +
                        0.25f * box_mg_crown_cell(cr, x + o[0], y + o[1], z + o[2]);
                }
            }
        }

        ecs_os_memcpy_n(cr->density, tmp, float, cr->n[0] * cr->n[1] * cr->n[2]);
    }
}

/** Build the density grid of a crown from the vertices of a mesh.
 *
 * @param cr The crown, with its bounds in min and size
 * @param m The mesh
 * @param lo Least corner of the bounds of the mesh
 * @param hi Greatest corner of the bounds of the mesh
 * @param resolution Cells along the longest side, 0 for 16
 * @param smoothing Blur passes, 0 for 2
 */
static void box_mg_crown_grid(
    box_mg_crown_t *cr,
    const box_mg_mesh_t *m,
    const vec3 lo,
    const vec3 hi,
    int32_t resolution,
    int32_t smoothing)
{
    int32_t res = resolution > 0 ? resolution : 16, a, v, pass, total;
    int32_t passes = smoothing > 0 ? smoothing : 2;
    float extent = 1e-3f, peak = 0.0f;
    res = res < BOX_MG_CROWN_MIN_CELLS ? BOX_MG_CROWN_MIN_CELLS : res;
    res = res > BOX_MG_CROWN_MAX_CELLS ? BOX_MG_CROWN_MAX_CELLS : res;
    passes = passes > 16 ? 16 : passes;
    for (a = 0; a < 3; a ++) {
        extent = glm_max(extent, hi[a] - lo[a]);
    }

    cr->cell = extent / (float)(res - 3);
    for (a = 0; a < 3; a ++) {
        cr->n[a] = (int32_t)ceilf((hi[a] - lo[a]) / cr->cell - 1e-4f) + 3;
        cr->n[a] = cr->n[a] < 4 ? 4 : (cr->n[a] > res + 1 ? res + 1 : cr->n[a]);
        cr->min[a] = (lo[a] + hi[a]) * 0.5f - (float)cr->n[a] * cr->cell * 0.5f;
    }

    total = cr->n[0] * cr->n[1] * cr->n[2];
    cr->density = ecs_os_calloc_n(float, total);
    for (v = 0; v < m->vertex_count; v ++) {
        int32_t i[3], dx, dy, dz;
        float f[3];
        for (a = 0; a < 3; a ++) {
            float g = (m->positions[v][a] - cr->min[a]) / cr->cell - 0.5f;
            float fl = floorf(g);
            i[a] = (int32_t)fl;
            f[a] = g - fl;
        }

        for (dz = 0; dz < 2; dz ++) {
            for (dy = 0; dy < 2; dy ++) {
                for (dx = 0; dx < 2; dx ++) {
                    int32_t x = i[0] + dx, y = i[1] + dy, z = i[2] + dz;
                    if (x < 0 || y < 0 || z < 0 || x >= cr->n[0] || y >= cr->n[1] ||
                        z >= cr->n[2])
                    {
                        continue;
                    }

                    cr->density[(z * cr->n[1] + y) * cr->n[0] + x] +=
                        (dx ? f[0] : 1.0f - f[0]) * (dy ? f[1] : 1.0f - f[1]) *
                        (dz ? f[2] : 1.0f - f[2]);
                }
            }
        }
    }

    float *tmp = ecs_os_malloc_n(float, total);
    for (pass = 0; pass < passes; pass ++) {
        box_mg_crown_blur(cr, tmp);
    }

    ecs_os_free(tmp);
    for (v = 0; v < total; v ++) {
        peak = glm_max(peak, cr->density[v]);
    }

    for (v = 0; peak > 0.0f && v < total; v ++) {
        cr->density[v] /= peak;
    }
}

/** Build the envelope of a crown around the vertices of a mesh.
 *
 * @param cr The crown
 * @param m The mesh
 * @param shape The shape of the envelope
 * @param center Center of the ellipsoid
 * @param size Half extents of the ellipsoid, all 0 to fit it
 * @param resolution Cells of the density grid along its longest side
 * @param smoothing Blur passes of the density grid
 */
static void box_mg_crown_init(
    box_mg_crown_t *cr,
    const box_mg_mesh_t *m,
    MeshCrownShape shape,
    const vec3 center,
    const vec3 size,
    int32_t resolution,
    int32_t smoothing)
{
    vec3 lo = { 0.0f, 0.0f, 0.0f }, hi = { 0.0f, 0.0f, 0.0f };
    int32_t i;
    ecs_os_memset_t(cr, 0, box_mg_crown_t);
    cr->shape = shape;
    if (m->vertex_count > 0) {
        glm_vec3_copy(m->positions[0], lo);
        glm_vec3_copy(m->positions[0], hi);
    }

    for (i = 0; i < m->vertex_count; i ++) {
        glm_vec3_minv(lo, m->positions[i], lo);
        glm_vec3_maxv(hi, m->positions[i], hi);
        glm_vec3_add(cr->centroid, m->positions[i], cr->centroid);
    }

    if (m->vertex_count > 0) {
        glm_vec3_scale(cr->centroid, 1.0f / (float)m->vertex_count, cr->centroid);
    }

    if (size[0] == 0.0f && size[1] == 0.0f && size[2] == 0.0f) {
        glm_vec3_center(lo, hi, cr->center);
        for (i = 0; i < 3; i ++) {
            cr->size[i] = glm_max((hi[i] - lo[i]) * 0.5f, 1e-4f);
        }
    } else {
        glm_vec3_copy((float*)center, cr->center);
        for (i = 0; i < 3; i ++) {
            cr->size[i] = size[i] != 0.0f ? fabsf(size[i]) : 1.0f;
        }
    }

    if (shape == MeshCrownDensity) {
        box_mg_crown_grid(cr, m, lo, hi, resolution, smoothing);
    }
}

/** Free what the envelope of a crown owns.
 *
 * @param cr The crown
 */
static void box_mg_crown_fini(
    box_mg_crown_t *cr)
{
    ecs_os_free(cr->density);
}

/** The distance of a position from the center of the ellipsoid of a crown in units of its half
 * extents: 0 at the center, 1 on the surface.
 *
 * @param cr The crown
 * @param p The position
 * @param q Receives the position relative to the center over the half extents
 * @return The distance
 */
static float box_mg_crown_radius(
    const box_mg_crown_t *cr,
    const vec3 p,
    vec3 q)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        q[i] = (p[i] - cr->center[i]) / cr->size[i];
    }

    return sqrtf(glm_vec3_norm2(q));
}

/** The outward unit normal of the envelope of a crown at a position.
 *
 * @param cr The crown
 * @param p The position
 * @param out Receives the normal
 * @return Whether the envelope has a normal there
 */
static bool box_mg_crown_normal(
    const box_mg_crown_t *cr,
    const vec3 p,
    vec3 out)
{
    vec3 q, n, radial;
    int32_t i;
    if (cr->shape == MeshCrownDensity) {
        float h = cr->cell * 0.5f;
        for (i = 0; i < 3; i ++) {
            vec3 a, b;
            glm_vec3_copy((float*)p, a);
            glm_vec3_copy((float*)p, b);
            a[i] += h;
            b[i] -= h;
            n[i] = box_mg_crown_density(cr, b) - box_mg_crown_density(cr, a);
        }

        glm_vec3_sub((float*)p, (float*)cr->centroid, radial);
        if (glm_vec3_norm2(radial) > 1e-12f) {
            glm_vec3_normalize(radial);
            glm_vec3_muladds(radial, 0.02f, n);
        }
    } else {
        box_mg_crown_radius(cr, p, q);
        for (i = 0; i < 3; i ++) {
            n[i] = q[i] / cr->size[i];
        }
    }

    bool result = glm_vec3_norm2(n) >= 1e-14f;
    if (result) {
        glm_vec3_normalize_to(n, out);
    }

    return result;
}

/** The occlusion of a position by the depth in a crown, 1 open.
 *
 * @param cr The crown
 * @param p The position
 * @param strength Occlusion at full depth
 * @param absorption Occlusion per cell of full density
 * @return The occlusion
 */
static float box_mg_crown_occlusion(
    const box_mg_crown_t *cr,
    const vec3 p,
    float strength,
    float absorption)
{
    vec3 q, d, at;
    float depth = 0.0f;
    if (cr->shape == MeshCrownDensity) {
        int32_t steps = 2 * (cr->n[0] + cr->n[1] + cr->n[2]), s, a;
        float step = cr->cell * 0.5f, sum = 0.0f;
        if (!box_mg_crown_normal(cr, p, d)) {
            glm_vec3_copy((vec3){ 0.0f, 1.0f, 0.0f }, d);
        }

        glm_vec3_copy((float*)p, at);
        for (s = 0; s < steps; s ++) {
            bool inside = true;
            glm_vec3_muladds(d, step, at);
            for (a = 0; a < 3; a ++) {
                inside = inside && at[a] >= cr->min[a] &&
                    at[a] <= cr->min[a] + (float)cr->n[a] * cr->cell;
            }

            if (!inside) {
                break;
            }

            sum += box_mg_crown_density(cr, at) * 0.5f;
        }

        depth = 1.0f - box_mg_exp(-absorption * sum);
    } else {
        depth = glm_clamp(1.0f - box_mg_crown_radius(cr, p, q), 0.0f, 1.0f);
    }

    return 1.0f - strength * depth;
}

/** Bend the normals of foliage toward the normals of the envelope of its crown.
 *
 * @param node The MeshCrownNormals
 * @param inputs The mesh
 * @param counts Values per port
 * @param out The mesh
 * @param error Unused
 * @return 0
 */
static int box_mesh_crown_normals_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshCrownNormals *n = node;
    box_mg_mesh_t *m = &out->mesh;
    float strength = glm_clamp(n->strength, 0.0f, 1.0f);
    box_mg_crown_t cr;
    int32_t i;
    (void)counts;
    (void)error;
    box_mg_value_copy(out, inputs[0]);
    box_mg_crown_init(&cr, m, n->shape, n->center, n->size, n->resolution, n->smoothing);
    for (i = 0; strength > 0.0f && i < m->vertex_count; i ++) {
        vec3 env, blended;
        if (!box_mg_crown_normal(&cr, m->positions[i], env)) {
            continue;
        }

        glm_vec3_scale(m->normals[i], 1.0f - strength, blended);
        glm_vec3_muladds(env, strength, blended);
        if (glm_vec3_norm2(blended) < 1e-12f) {
            glm_vec3_copy(env, blended);
        }

        glm_vec3_normalize_to(blended, m->normals[i]);
    }

    box_mg_crown_fini(&cr);

    return 0;
}

/** Write the occlusion of foliage by its depth in its crown to its occlusion data channel.
 *
 * @param node The MeshCrownOcclusion
 * @param inputs The mesh
 * @param counts Values per port
 * @param out The mesh
 * @param error Receives why it failed
 * @return 0 on success
 */
static int box_mesh_crown_occlusion_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshCrownOcclusion *n = node;
    box_mg_mesh_t *m = &out->mesh;
    float strength = glm_clamp(n->strength, 0.0f, 1.0f);
    float absorption = n->absorption > 0.0f ? n->absorption : 0.5f;
    char name[BOX_MESH_ATTR_NAME];
    box_mg_crown_t cr;
    int32_t i;
    (void)counts;
    box_mg_value_copy(out, inputs[0]);
    ecs_os_snprintf(name, ECS_SIZEOF(name), "%s%d", BOX_MESH_DATA_ATTR,
        BOX_MESH_DATA_OCCLUSION);
    float *occlusion = box_mg_attr_add(m->attrs, &m->attr_count, name, m->vertex_count);
    int result = 0;
    if (occlusion) {
        box_mg_crown_init(&cr, m, n->shape, n->center, n->size, n->resolution,
            n->smoothing);
        for (i = 0; i < m->vertex_count; i ++) {
            occlusion[i] = box_mg_crown_occlusion(&cr, m->positions[i], strength, absorption);
        }

        box_mg_crown_fini(&cr);
    } else {
        *error = flecs_asprintf("the mesh has no room for the attribute %s", name);
        result = -1;
    }

    return result;
}

/** Construct MeshLeaves values with the documented defaults.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_leaves_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshLeaves *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshLeaves){ .count = 6, .end = 1.0f, .angle = 0.8f, .size = 0.1f };
    }
}

/** Construct MeshNeedles values with the documented defaults.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_needles_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshNeedles *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshNeedles){ .end = 1.0f, .width = 0.5f, .bank = 0.6f };
    }
}

/** Construct MeshCards values with the documented defaults.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_cards_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshCards *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshCards){ .bend = 0.15f, .double_sided = true, .flutter = 1.0f };
    }
}

/** Construct MeshCrownNormals values with the documented defaults.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_crown_normals_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshCrownNormals *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshCrownNormals){ .strength = 0.75f };
    }
}

/** Construct MeshCrownOcclusion values with the documented defaults.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_crown_occlusion_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshCrownOcclusion *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshCrownOcclusion){ .strength = 0.8f };
    }
}

/* Parameter of node kind T: its member name, offset and size. */
#define BOX_MG_PARAM(T, member) { #member, offsetof(T, member), ECS_SIZEOF(((T*)0)->member) }

void box_mesh_nodes_foliage_register(
    ecs_world_t *world)
{
    ecs_set_hooks(world, MeshLeaves, { .ctor = box_mesh_leaves_ctor });
    ecs_set_hooks(world, MeshNeedles, { .ctor = box_mesh_needles_ctor });
    ecs_set_hooks(world, MeshCards, { .ctor = box_mesh_cards_ctor });
    ecs_set_hooks(world, MeshCrownNormals, { .ctor = box_mesh_crown_normals_ctor });
    ecs_set_hooks(world, MeshCrownOcclusion, { .ctor = box_mesh_crown_occlusion_ctor });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshLeaves),
        .output = MeshValuePoints,
        .inputs = {{ "curves", offsetof(MeshLeaves, curves), MeshValueCurves }},
        .params = {
            BOX_MG_PARAM(MeshLeaves, placement), BOX_MG_PARAM(MeshLeaves, level),
            BOX_MG_PARAM(MeshLeaves, count), BOX_MG_PARAM(MeshLeaves, density),
            BOX_MG_PARAM(MeshLeaves, per_node), BOX_MG_PARAM(MeshLeaves, start),
            BOX_MG_PARAM(MeshLeaves, end), BOX_MG_PARAM(MeshLeaves, phyllotaxis),
            BOX_MG_PARAM(MeshLeaves, angle), BOX_MG_PARAM(MeshLeaves, orientation),
            BOX_MG_PARAM(MeshLeaves, center), BOX_MG_PARAM(MeshLeaves, tilt),
            BOX_MG_PARAM(MeshLeaves, bank), BOX_MG_PARAM(MeshLeaves, size),
            BOX_MG_PARAM(MeshLeaves, size_tip), BOX_MG_PARAM(MeshLeaves, size_level),
            BOX_MG_PARAM(MeshLeaves, size_variation), BOX_MG_PARAM(MeshLeaves, seed),
            BOX_MG_PARAM(MeshLeaves, keep), BOX_MG_PARAM(MeshLeaves, lod_keep)
        },
        .eval = box_mesh_leaves_eval,
        .lod = box_mesh_leaves_lod,
        .flags = BOX_MESH_NODE_RANDOM
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshNeedles),
        .output = MeshValuePoints,
        .inputs = {{ "curves", offsetof(MeshNeedles, curves), MeshValueCurves }},
        .params = {
            BOX_MG_PARAM(MeshNeedles, placement), BOX_MG_PARAM(MeshNeedles, level),
            BOX_MG_PARAM(MeshNeedles, style), BOX_MG_PARAM(MeshNeedles, cards),
            BOX_MG_PARAM(MeshNeedles, start), BOX_MG_PARAM(MeshNeedles, end),
            BOX_MG_PARAM(MeshNeedles, width), BOX_MG_PARAM(MeshNeedles, bank),
            BOX_MG_PARAM(MeshNeedles, droop), BOX_MG_PARAM(MeshNeedles, tilt),
            BOX_MG_PARAM(MeshNeedles, seed), BOX_MG_PARAM(MeshNeedles, keep),
            BOX_MG_PARAM(MeshNeedles, lod_keep)
        },
        .eval = box_mesh_needles_eval,
        .lod = box_mesh_needles_lod,
        .flags = BOX_MESH_NODE_RANDOM
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshCards),
        .output = MeshValueMesh,
        .inputs = {
            { "points", offsetof(MeshCards, points), MeshValuePoints },
            { "mesh", offsetof(MeshCards, mesh), MeshValueMesh, .optional = true }
        },
        .params = {
            BOX_MG_PARAM(MeshCards, shape), BOX_MG_PARAM(MeshCards, width),
            BOX_MG_PARAM(MeshCards, bend), BOX_MG_PARAM(MeshCards, segments),
            BOX_MG_PARAM(MeshCards, double_sided), BOX_MG_PARAM(MeshCards, shared_back),
            BOX_MG_PARAM(MeshCards, flutter), BOX_MG_PARAM(MeshCards, lod_segments),
            BOX_MG_PARAM(MeshCards, lod_flat)
        },
        .eval = box_mesh_cards_eval,
        .lod = box_mesh_cards_lod
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshCrownNormals),
        .output = MeshValueMesh,
        .inputs = {{ "mesh", offsetof(MeshCrownNormals, mesh), MeshValueMesh }},
        .params = {
            BOX_MG_PARAM(MeshCrownNormals, shape), BOX_MG_PARAM(MeshCrownNormals, center),
            BOX_MG_PARAM(MeshCrownNormals, size), BOX_MG_PARAM(MeshCrownNormals, strength),
            BOX_MG_PARAM(MeshCrownNormals, resolution),
            BOX_MG_PARAM(MeshCrownNormals, smoothing)
        },
        .eval = box_mesh_crown_normals_eval
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshCrownOcclusion),
        .output = MeshValueMesh,
        .inputs = {{ "mesh", offsetof(MeshCrownOcclusion, mesh), MeshValueMesh }},
        .params = {
            BOX_MG_PARAM(MeshCrownOcclusion, shape), BOX_MG_PARAM(MeshCrownOcclusion, center),
            BOX_MG_PARAM(MeshCrownOcclusion, size),
            BOX_MG_PARAM(MeshCrownOcclusion, strength),
            BOX_MG_PARAM(MeshCrownOcclusion, absorption),
            BOX_MG_PARAM(MeshCrownOcclusion, resolution),
            BOX_MG_PARAM(MeshCrownOcclusion, smoothing)
        },
        .eval = box_mesh_crown_occlusion_eval
    });
}
