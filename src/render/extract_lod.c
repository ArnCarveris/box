/**
 * @file render/extract_lod.c
 * @brief Builds the LOD node hierarchy and walks it once per view of the frame.
 *
 * The hierarchy is renderer state, not the entity tree: every collision root is
 * a node, spatial clusters are stacked over those roots until few are left, and
 * the subtree of a root is materialised only while the walk descends into it.
 * A walk shows the leaves it reaches, hides the subtree of every node it
 * replaces and leaves one proxy instance in its place.
 */

#include "render_internal.h"
#include "../geometry/geometry_internal.h"
#include "../collision/collision_internal.h"
#include "../renderer/hiz.h"
#include <math.h>

/** Roots one cluster of the stacked cluster hierarchy groups */
#define BOX_RENDER_LOD_GROUP (8)

/** Roots the clustering stops stacking at */
#define BOX_RENDER_LOD_TOP (16)

/** Identity the first cluster node of the tree is given */
#define BOX_RENDER_LOD_CLUSTER_ID (0x4000000000000000ull)

/** The subtree of the node is hidden, so a collapse does not hide it again */
#define BOX_RENDER_LOD_HIDDEN (1024u)

/** One view of the frame the traversal measures and culls against */
typedef struct box_render_lod_view_t {
    box_lod_camera_t camera; /**< Camera the projected error is measured in */
    box_frustum_t frustum;   /**< Planes a rejected node stops the walk at */
    float viewport_px;       /**< Height of the viewport in pixels */
    float threshold;         /**< Projected error in pixels a node may not exceed */
    float hysteresis;        /**< Band around the threshold a switch has to cross */
    ecs_entity_t entity;     /**< View entity the statistics are written to */
} box_render_lod_view_t;

/** Everything the traversal owns, held by the render registry */
struct box_lod_state_t {
    box_lod_tree_t tree;   /**< The node hierarchy */
    ecs_map_t roots;       /**< Entity of a root to its node index plus one */
    ecs_vec_t added;       /**< ecs_entity_t roots waiting for a node */
    ecs_vec_t dead;        /**< ecs_entity_t roots whose node must go */
    ecs_vec_t moved;       /**< ecs_entity_t roots whose bounds changed */
    ecs_vec_t views;       /**< box_render_lod_view_t of this frame */
    ecs_vec_t results;     /**< int32_t nodes the walks of this frame resolved */
    ecs_vec_t proxies;     /**< box_instance_t the collapsed nodes are drawn as */
    ecs_world_t *world;    /**< World the callbacks read */
    RenderState *render;   /**< Registry the callbacks show and hide through */
    const box_render_lod_view_t *view; /**< View the running walk belongs to */
    int32_t build;         /**< Node the subtree build is adding children to */
    float sum_color[4];    /**< Volume weighted color the measuring walk collected */
    float sum_material[2]; /**< Volume weighted metallic and roughness it collected */
    float sum_weight;      /**< Volume the sums are weighted by */
    bool measure;          /**< The walk of the entities collects the look of a root */
    uint64_t cluster_id;   /**< Identity the next cluster node takes */
    uint32_t stamp;        /**< Frame the votes of the nodes belong to */
    uint32_t proxy_mesh;   /**< Mesh handle every proxy instance is drawn with */
    RenderMesh proxy;      /**< Unit box the proxy instances are scaled from */
    box_hiz_pyramid_t depth; /**< Depth grid of the first view, reduced on the CPU */
    float depth_clip[16];  /**< World to clip matrix that grid was drawn with */
    float depth_levels[BOX_LOD_DEPTH_MAX * BOX_LOD_DEPTH_MAX * 2]; /**< Texels of every level */
    bool proxy_ready;      /**< The unit box of the proxies was looked up */
    bool structure;        /**< A root appeared or went away since the last frame */
    bool active;           /**< The traversal ran on the previous frame */
};

box_lod_state_t* box_render_lod_new(void)
{
    box_lod_state_t *result = ecs_os_calloc_t(box_lod_state_t);
    box_lod_tree_init(&result->tree);
    ecs_map_init(&result->roots, NULL);
    ecs_vec_init_t(NULL, &result->added, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &result->dead, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &result->moved, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &result->views, box_render_lod_view_t, 0);
    ecs_vec_init_t(NULL, &result->results, int32_t, 0);
    ecs_vec_init_t(NULL, &result->proxies, box_instance_t, 0);
    result->cluster_id = BOX_RENDER_LOD_CLUSTER_ID;

    return result;
}

void box_render_lod_free(
    box_lod_state_t *lod)
{
    if (!lod) {
        goto done;
    }

    box_lod_tree_fini(&lod->tree);
    ecs_map_fini(&lod->roots);
    ecs_vec_fini_t(NULL, &lod->added, ecs_entity_t);
    ecs_vec_fini_t(NULL, &lod->dead, ecs_entity_t);
    ecs_vec_fini_t(NULL, &lod->moved, ecs_entity_t);
    ecs_vec_fini_t(NULL, &lod->views, box_render_lod_view_t);
    ecs_vec_fini_t(NULL, &lod->results, int32_t);
    ecs_vec_fini_t(NULL, &lod->proxies, box_instance_t);
    ecs_os_free(lod);
done:
    return;
}

/** Hide or show one renderable, releasing or reclaiming its static slot.
 *
 * @param world The world
 * @param state The registry
 * @param e The entity
 * @param hidden The traversal replaced the entity by a proxy
 */
static void box_render_lod_mark(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e,
    bool hidden)
{
    RenderInstance *inst = ecs_get_mut(world, e, RenderInstance);
    if (!inst || inst->lod_hidden == hidden) {
        goto done;
    }

    inst->lod_hidden = hidden;
    if (!hidden) {
        box_render_mark_dirty(world, e);
        goto done;
    }

    if (box_render_has_slot(state, e, inst)) {
        box_render_release_slot(world, state, e, inst);
    }

done:
    return;
}

/** Add one renderable to the look and the error a root is given while hidden.
 *
 * A root that was never opened still needs the color its proxy is drawn in and
 * the volume its subtree fills, which is what its error is measured from, so
 * the walk that hides a new root collects both on its way through the subtree.
 *
 * @param lod The state
 * @param e The entity
 */
static void box_render_lod_measure(
    box_lod_state_t *lod,
    ecs_entity_t e)
{
    ecs_world_t *world = lod->world;
    const RenderInstance *inst = ecs_get(world, e, RenderInstance);
    const AABB *aabb = ecs_get(world, e, AABB);
    if (!inst || !aabb || inst->dynamic || inst->transparent) {
        goto done;
    }

    const Rgb *rgb = ecs_get(world, e, Rgb);
    const Opacity *opacity = ecs_get(world, e, Opacity);
    const PbrMaterial *pbr = ecs_get(world, e, PbrMaterial);
    vec3 size = vec3_sub(aabb->max, aabb->min);
    float weight = size.x * size.y * size.z;

    weight = weight > 0.0f ? weight : 1.0f;
    lod->sum_color[0] += (rgb ? box_srgb_to_linear(rgb->r) : 1.0f) * weight;
    lod->sum_color[1] += (rgb ? box_srgb_to_linear(rgb->g) : 1.0f) * weight;
    lod->sum_color[2] += (rgb ? box_srgb_to_linear(rgb->b) : 1.0f) * weight;
    lod->sum_color[3] += (opacity ? opacity->value : 1.0f) * weight;
    lod->sum_material[0] += (pbr ? pbr->metallic : 0.0f) * weight;
    lod->sum_material[1] += (pbr ? pbr->roughness : 0.5f) * weight;
    lod->sum_weight += weight;
done:
    return;
}

/** Hide or show the renderables of an entity and of everything below it.
 *
 * @param world The world
 * @param e The entity
 * @param ctx The state, whose build flag holds what to write
 */
static void box_render_lod_subtree(
    ecs_world_t *world,
    ecs_entity_t e,
    void *ctx)
{
    box_lod_state_t *lod = ctx;
    if (ecs_has_id(world, e, EcsPrefab)) {
        goto done;
    }

    box_render_lod_mark(world, lod->render, e, lod->build != 0);
    if (lod->measure) {
        box_render_lod_measure(lod, e);
    }

    box_hierarchy_children(world, e, box_render_lod_subtree, lod);
done:
    return;
}

/** Hide or show everything the subtree of a node stands for.
 *
 * A cluster node stands for the roots below it, which is why the walk of the
 * entities starts at every child of a node without an entity of its own.
 *
 * @param lod The state
 * @param node Index of the node
 * @param hidden The traversal replaced the subtree by a proxy
 */
static void box_render_lod_hide(
    box_lod_state_t *lod,
    int32_t node,
    bool hidden)
{
    box_lod_node_t *n = box_lod_tree_get(&lod->tree, node);
    if (!n) {
        goto done;
    }

    lod->build = hidden ? 1 : 0;
    if (n->id && n->id < BOX_RENDER_LOD_CLUSTER_ID) {
        box_render_lod_subtree(lod->world, (ecs_entity_t)n->id, lod);
        goto done;
    }

    int32_t child = n->first_child;
    while (child >= 0) {
        box_lod_node_t *c = box_lod_tree_get(&lod->tree, child);

        box_render_lod_hide(lod, child, hidden);
        child = c ? c->next : -1;
    }

done:
    lod->build = 0;
}

/** Give a leaf node the bounds, the error and the look of its entity.
 *
 * @param lod The state
 * @param node Index of the node
 * @param e The entity
 * @return True when the entity is a static renderable the traversal owns
 */
static bool box_render_lod_leaf(
    box_lod_state_t *lod,
    int32_t node,
    ecs_entity_t e)
{
    ecs_world_t *world = lod->world;
    const RenderInstance *inst = ecs_get(world, e, RenderInstance);
    const AABB *aabb = ecs_get(world, e, AABB);
    bool result = inst && aabb && !inst->dynamic && !inst->transparent;
    if (!result) {
        goto done;
    }

    const Rgb *rgb = ecs_get(world, e, Rgb);
    const Opacity *opacity = ecs_get(world, e, Opacity);
    const PbrMaterial *pbr = ecs_get(world, e, PbrMaterial);
    vec3 size = vec3_sub(aabb->max, aabb->min);
    float color[4] = {
        rgb ? box_srgb_to_linear(rgb->r) : 1.0f,
        rgb ? box_srgb_to_linear(rgb->g) : 1.0f,
        rgb ? box_srgb_to_linear(rgb->b) : 1.0f,
        opacity ? opacity->value : 1.0f
    };

    box_lod_tree_set_bounds(&lod->tree, node, aabb->min, aabb->max, 0);
    box_lod_tree_set_look(&lod->tree, node, color, pbr ? pbr->metallic : 0.0f,
        pbr ? pbr->roughness : 0.5f, size.x * size.y * size.z);
done:
    return result;
}

/** Add a node for one child entity and for everything renderable below it.
 *
 * @param world The world
 * @param e The entity
 * @param ctx The state, whose build field names the parent node
 */
static void box_render_lod_build(
    ecs_world_t *world,
    ecs_entity_t e,
    void *ctx)
{
    box_lod_state_t *lod = ctx;
    if (ecs_has_id(world, e, EcsPrefab)) {
        goto done;
    }

    int32_t parent = lod->build;
    int32_t node = box_lod_tree_add(&lod->tree, parent, e);
    if (ecs_has_id(world, e, LodPin)) {
        box_lod_tree_get(&lod->tree, node)->flags |= BOX_LOD_NODE_PIN;
    }

    lod->build = node;
    box_hierarchy_children(world, e, box_render_lod_build, lod);
    lod->build = parent;

    box_lod_node_t *n = box_lod_tree_get(&lod->tree, node);
    if (n->flags & BOX_LOD_NODE_LEAF && !box_render_lod_leaf(lod, node, e)) {
        box_lod_tree_remove(&lod->tree, node);
    }

done:
    return;
}

/** Materialise the children of a node the walk wants to descend into.
 *
 * @param ctx The state
 * @param node Index of the node
 * @return True when the node holds children afterwards
 */
static bool box_render_lod_open(
    void *ctx,
    int32_t node)
{
    box_lod_state_t *lod = ctx;
    box_lod_node_t *n = box_lod_tree_get(&lod->tree, node);
    bool result = false;
    if (!n || !n->id || n->id >= BOX_RENDER_LOD_CLUSTER_ID) {
        goto done;
    }

    lod->build = node;
    box_hierarchy_children(lod->world, (ecs_entity_t)n->id, box_render_lod_build, lod);
    lod->build = 0;
    box_lod_tree_refresh(&lod->tree, node);

    n = box_lod_tree_get(&lod->tree, node);
    result = (n->flags & BOX_LOD_NODE_OPEN) != 0;
done:
    return result;
}

/** Answer whether the view of the running walk can see a node.
 *
 * @param ctx The state
 * @param node The node
 * @return True when the frustum of the view keeps it
 */
static bool box_render_lod_visible(
    void *ctx,
    const box_lod_node_t *node)
{
    const box_lod_state_t *lod = ctx;

    return box_frustum_aabb(&lod->view->frustum, (const float*)&node->min,
        (const float*)&node->max);
}

/** Reduce the depth grid the renderer read back into a pyramid the walk tests.
 *
 * The grid the packet carries is one level of the pyramid of the first view,
 * which the same halving rule as the renderer turns into a chain down to a
 * single texel, so the rectangle of a node is tested at the level that matches
 * its size.
 *
 * @param lod The state
 * @param p The packet the renderer returned
 */
static void box_render_lod_depth(
    box_lod_state_t *lod,
    const box_packet_t *p)
{
    memset(&lod->depth, 0, sizeof(box_hiz_pyramid_t));
    if (!p->lod_depth.valid || !p->lod_depth.width || !p->lod_depth.height) {
        goto done;
    }

    uint32_t w = p->lod_depth.width;
    uint32_t h = p->lod_depth.height;
    uint32_t offset = w * h;
    float *values = lod->depth_levels;

    memcpy(values, p->lod_depth.values, (size_t)offset * sizeof(float));
    memcpy(lod->depth_clip, p->lod_depth.clip, sizeof(lod->depth_clip));
    lod->depth.levels[0] = values;
    lod->depth.width = w;
    lod->depth.height = h;
    lod->depth.count = 1;

    while (lod->depth.count < BOX_HIZ_LEVELS_MAX && (w > 1 || h > 1)) {
        const float *src = lod->depth.levels[lod->depth.count - 1];
        uint32_t next_w = box_hiz_extent(lod->depth.width, lod->depth.count);
        uint32_t next_h = box_hiz_extent(lod->depth.height, lod->depth.count);
        float *dst = values + offset;
        uint32_t y;

        for (y = 0; y < next_h; y ++) {
            uint32_t x;
            for (x = 0; x < next_w; x ++) {
                dst[y * next_w + x] = box_hiz_reduce(src, w, h, x, y);
            }
        }

        lod->depth.levels[lod->depth.count] = dst;
        lod->depth.count ++;
        offset += next_w * next_h;
        w = next_w;
        h = next_h;
    }

done:
    return;
}

/** Answer whether the depth grid of the first view hides a node.
 *
 * @param ctx The state
 * @param node The node
 * @return True when every texel the node covers is nearer than the node
 */
static bool box_render_lod_occluded(
    void *ctx,
    const box_lod_node_t *node)
{
    const box_lod_state_t *lod = ctx;
    bool result = false;
    if (!lod->depth.count) {
        goto done;
    }

    vec3 center = vec3_scale(vec3_add(node->min, node->max), 0.5f);
    float radius = vec3_length(vec3_sub(node->max, node->min)) * 0.5f;
    box_hiz_rect_t rect;
    if (!box_hiz_project(lod->depth_clip, (const float*)&center, radius, &rect)) {
        goto done;
    }

    result = !box_hiz_visible(&lod->depth, &rect);
done:
    return result;
}

/** Remember a node the walk resolved, once per frame however many views saw it.
 *
 * @param ctx The state
 * @param node Index of the node
 * @param vote The decision the walk took
 */
static void box_render_lod_emit(
    void *ctx,
    int32_t node,
    uint32_t vote)
{
    box_lod_state_t *lod = ctx;
    box_lod_node_t *n = box_lod_tree_get(&lod->tree, node);
    if (n && n->votes == vote) {
        ecs_vec_append_t(NULL, &lod->results, int32_t)[0] = node;
    }
}

/** Draw the proxy of a collapsed node as one box over its bounds.
 *
 * @param lod The state
 * @param n The node
 */
static void box_render_lod_proxy(
    box_lod_state_t *lod,
    const box_lod_node_t *n)
{
    if (!lod->proxy_ready) {
        goto done;
    }

    vec3 center = vec3_scale(vec3_add(n->min, n->max), 0.5f);
    vec3 size = vec3_sub(n->max, n->min);
    box_instance_t *inst = ecs_vec_append_t(NULL, &lod->proxies, box_instance_t);
    mat4 m = mat4_identity();

    memset(inst, 0, sizeof(box_instance_t));
    m.v[0] = size.x;
    m.v[5] = size.y;
    m.v[10] = size.z;
    m.v[12] = center.x;
    m.v[13] = center.y;
    m.v[14] = center.z;
    inst->transform[0] = size.x;
    inst->transform[5] = size.y;
    inst->transform[10] = size.z;
    inst->transform[3] = center.x;
    inst->transform[7] = center.y;
    inst->transform[11] = center.z;
    memcpy(inst->color, n->color, sizeof(inst->color));
    inst->params[0] = n->material[0];
    inst->params[1] = n->material[1];
    inst->uv_transform[0] = 1.0f;
    inst->uv_transform[1] = 1.0f;
    box_render_instance_bounds(&lod->proxy, &m, inst->bounds);
done:
    return;
}

/** Apply what the walks of this frame decided, node by node.
 *
 * A node the walks descended into keeps its subtree visible, a leaf they
 * reached is shown and a node they replaced hides its subtree once, releases
 * its children and is drawn as a proxy for as long as it stays replaced.
 *
 * @param lod The state
 */
static void box_render_lod_resolve(
    box_lod_state_t *lod)
{
    int32_t count = ecs_vec_count(&lod->results);
    int32_t i;
    for (i = 0; i < count; i ++) {
        int32_t index = ecs_vec_get_t(&lod->results, int32_t, i)[0];
        box_lod_node_t *n = box_lod_tree_get(&lod->tree, index);
        if (!n || n->stamp != lod->stamp) {
            continue;
        }

        if (n->votes & BOX_LOD_VOTE_DESCEND) {
            n->flags &= ~BOX_RENDER_LOD_HIDDEN;
            continue;
        }

        if (n->votes & BOX_LOD_VOTE_DRAW) {
            n->flags &= ~BOX_RENDER_LOD_HIDDEN;
            box_render_lod_mark(lod->world, lod->render, (ecs_entity_t)n->id, false);
            continue;
        }

        if (!(n->flags & BOX_RENDER_LOD_HIDDEN)) {
            box_render_lod_hide(lod, index, true);
            n = box_lod_tree_get(&lod->tree, index);
            n->flags |= BOX_RENDER_LOD_HIDDEN;
        }

        bool owned = n->id && n->id < BOX_RENDER_LOD_CLUSTER_ID;
        if (owned && (n->flags & BOX_LOD_NODE_OPEN)) {
            box_lod_tree_close(&lod->tree, index);
            n = box_lod_tree_get(&lod->tree, index);
        }

        if (n->votes & BOX_LOD_VOTE_COLLAPSE) {
            box_render_lod_proxy(lod, n);
        }
    }
}

void box_render_lod_proxies(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    box_lod_state_t *lod = state->lod;
    int32_t count = lod ? ecs_vec_count(&lod->proxies) : 0;
    (void)world;
    if (!count) {
        goto done;
    }

    box_dynamic_batch_t *batch = box_vec_push(&p->dynamic_batches);
    batch->mesh = lod->proxy_mesh;
    batch->first = p->dynamic_instances.count;
    batch->count = (uint32_t)count;

    int32_t i;
    for (i = 0; i < count; i ++) {
        box_instance_t *dst = box_vec_push(&p->dynamic_instances);

        dst[0] = ecs_vec_get_t(&lod->proxies, box_instance_t, i)[0];
    }

done:
    return;
}

/** Give a new root node its bounds, its error and the look of its subtree.
 *
 * The subtree is hidden in the same walk, so a root that appears costs one
 * pass over its entities and nothing per frame until a view reaches it.
 *
 * @param lod The state
 * @param node Index of the node
 * @param rb The aggregated bounds of the root
 */
static void box_render_lod_root(
    box_lod_state_t *lod,
    int32_t node,
    const RootBounds *rb)
{
    memset(lod->sum_color, 0, sizeof(lod->sum_color));
    memset(lod->sum_material, 0, sizeof(lod->sum_material));
    lod->sum_weight = 0.0f;
    lod->measure = true;
    box_render_lod_hide(lod, node, true);
    lod->measure = false;

    box_lod_node_t *n = box_lod_tree_get(&lod->tree, node);
    float inv = lod->sum_weight > 0.0f ? 1.0f / lod->sum_weight : 1.0f;
    float color[4] = {
        lod->sum_color[0] * inv, lod->sum_color[1] * inv,
        lod->sum_color[2] * inv, lod->sum_weight > 0.0f ? lod->sum_color[3] * inv : 1.0f
    };

    box_lod_tree_set_bounds(&lod->tree, node, rb->min, rb->max,
        box_lod_tree_shell(rb->min, rb->max, lod->sum_weight));
    box_lod_tree_set_look(&lod->tree, node, color, lod->sum_material[0] * inv,
        lod->sum_weight > 0.0f ? lod->sum_material[1] * inv : 0.5f, lod->sum_weight);
    n->leaves = rb->count > 0 ? rb->count : 1;
    n->flags = (n->flags & ~BOX_LOD_NODE_LEAF) | BOX_RENDER_LOD_HIDDEN;
}

/** Stack cluster nodes over the roots until only a handful are left.
 *
 * @param lod The state
 */
static void box_render_lod_recluster(
    box_lod_state_t *lod)
{
    box_lod_tree_uncluster(&lod->tree);

    int32_t roots = ecs_map_count(&lod->roots);
    while (roots > BOX_RENDER_LOD_TOP) {
        int32_t made = box_lod_tree_cluster(&lod->tree, BOX_RENDER_LOD_GROUP);
        if (!made) {
            break;
        }

        roots = made;
    }

    int32_t root = lod->tree.root;
    while (root >= 0) {
        box_lod_node_t *n = box_lod_tree_get(&lod->tree, root);

        if (n->flags & BOX_LOD_NODE_CLUSTER) {
            n->id = lod->cluster_id ++;
        }

        root = n->next;
    }

    box_lod_tree_refresh(&lod->tree, -1);
}

/** Bring the root nodes of the tree in line with the roots of the world.
 *
 * @param lod The state
 * @param state The registry
 */
static void box_render_lod_sync(
    box_lod_state_t *lod,
    RenderState *state)
{
    ecs_world_t *world = lod->world;
    ecs_vec_clear(&lod->dead);
    if (!state->lod_roots || !ecs_query_changed(state->lod_roots)) {
        goto done;
    }

    int32_t found = 0;
    ecs_iter_t it = ecs_query_iter(world, state->lod_roots);
    while (ecs_query_next(&it)) {
        const RootBounds *rb = ecs_field(&it, RootBounds, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            found ++;
            if (ecs_map_get(&lod->roots, it.entities[i]) || !rb[i].count) {
                continue;
            }

            int32_t node = box_lod_tree_add(&lod->tree, -1, it.entities[i]);

            box_render_lod_root(lod, node, &rb[i]);
            ecs_map_insert(&lod->roots, it.entities[i], (ecs_map_val_t)(node + 1));
            lod->structure = true;
        }
    }

    if (found != ecs_map_count(&lod->roots)) {
        ecs_map_iter_t mit = ecs_map_iter(&lod->roots);
        while (ecs_map_next(&mit)) {
            ecs_entity_t e = (ecs_entity_t)ecs_map_key(&mit);
            if (!ecs_is_alive(world, e) || !ecs_has(world, e, RootBounds)) {
                ecs_vec_append_t(NULL, &lod->dead, ecs_entity_t)[0] = e;
            }
        }
    }

    int32_t i, count = ecs_vec_count(&lod->dead);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_vec_get_t(&lod->dead, ecs_entity_t, i)[0];
        ecs_map_val_t *slot = ecs_map_get(&lod->roots, e);

        box_lod_tree_remove(&lod->tree, (int32_t)slot[0] - 1);
        ecs_map_remove(&lod->roots, e);
        lod->structure = true;
    }

done:
    if (lod->structure) {
        box_render_lod_recluster(lod);
        lod->structure = false;
    }
}

void box_render_lod_roots(
    ecs_world_t *world,
    ecs_entity_t root_bounds)
{
    if (!root_bounds || !ecs_lookup(world, "box.render.RenderState")) {
        goto done;
    }

    RenderState *state = box_render_state(world);
    if (!state || state->lod_roots) {
        goto done;
    }

    state->lod_roots = ecs_query(world, {
        .terms = {{ .id = root_bounds, .inout = EcsIn }},
        .flags = EcsQueryDetectChanges,
        .cache_kind = EcsQueryCacheAuto
    });
done:
    return;
}

/** Read the camera, the frustum and the threshold of one view of the frame.
 *
 * @param world The world
 * @param src The view of the frame
 * @param p The packet, for the size a view without a target draws at
 * @param out Receives the view
 * @return True when the view takes part in the traversal
 */
static bool box_render_lod_view(
    ecs_world_t *world,
    const box_view_src_t *src,
    const box_packet_t *p,
    box_render_lod_view_t *out)
{
    box_view_t v;
    const LodSettings *s = box_render_view_setting(world, src->entity, ecs_id(LodSettings));
    const Camera *c = src->camera ? ecs_get(world, src->camera, Camera) : NULL;
    bool result = s && s->enabled && c && ecs_has(world, src->camera, WorldTransform);
    if (!result) {
        goto done;
    }

    box_render_view_probe(world, src, p, &v);
    memset(out, 0, sizeof(box_render_lod_view_t));
    out->entity = src->entity;
    out->camera.position = vec3_new(v.eye[0], v.eye[1], v.eye[2]);
    out->camera.fov = c->fov;
    out->camera.ortho_height = c->ortho_height;
    out->camera.orthographic = c->orthographic;
    out->viewport_px = (float)v.height;
    out->threshold = v.lod_error_px;
    out->hysteresis = v.lod_hysteresis;
    box_frustum_of_view(&v, &out->frustum);
    result = out->viewport_px > 0.0f && out->threshold > 0.0f;
done:
    return result;
}

/** Collect the views the traversal of this frame walks for.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 * @param lod The state
 * @return The number of views, 0 when no view asks for the traversal
 */
static int32_t box_render_lod_views(
    ecs_world_t *world,
    RenderState *state,
    const box_packet_t *p,
    box_lod_state_t *lod)
{
    box_render_lod_view_t view;
    int32_t count = ecs_vec_count(&state->view_list);
    int32_t i;

    ecs_vec_clear(&lod->views);
    for (i = 0; i < count; i ++) {
        const box_view_src_t *src = ecs_vec_get_t(&state->view_list, box_view_src_t, i);
        if (box_render_lod_view(world, src, p, &view)) {
            ecs_vec_append_t(NULL, &lod->views, box_render_lod_view_t)[0] = view;
        }
    }

    if (!count) {
        box_view_src_t src = { .camera = box_render_active_camera(world) };
        if (box_render_lod_view(world, &src, p, &view)) {
            ecs_vec_append_t(NULL, &lod->views, box_render_lod_view_t)[0] = view;
        }
    }

    return ecs_vec_count(&lod->views);
}

/** Publish what one walk cost on the view it belongs to and on the singleton.
 *
 * @param world The world
 * @param lod The state
 * @param e The view entity, 0 for the view a world without views gets
 * @param stats What the walk cost
 */
static void box_render_lod_publish(
    ecs_world_t *world,
    const box_lod_state_t *lod,
    ecs_entity_t e,
    const box_lod_walk_stats_t *stats)
{
    LodStats value = {
        .visited = stats->visited, .drawn = stats->drawn, .proxies = stats->proxies,
        .rejected = stats->rejected + stats->occluded, .pruned = stats->pruned,
        .nodes = lod->tree.count
    };

    if (e && ecs_has(world, e, LodStats)) {
        ecs_set_ptr(world, e, LodStats, &value);
    }

    if (ecs_singleton_get(world, LodStats)) {
        ecs_singleton_set_ptr(world, LodStats, &value);
    }
}

/** Give the traversal the unit box every proxy instance is drawn with.
 *
 * @param world The world
 * @param lod The state
 */
static void box_render_lod_mesh(
    ecs_world_t *world,
    box_lod_state_t *lod)
{
    if (lod->proxy_ready || !ecs_lookup(world, "box.geometry.GeometryState")) {
        goto done;
    }

    box_mesh_key_t key = { .kind = BoxShapeBox };
    vec3 min, max;

    lod->proxy_mesh = box_mesh_cache_get(world, &key, &min, &max);
    lod->proxy.handle = lod->proxy_mesh;
    lod->proxy.scale = vec3_new(1, 1, 1);
    lod->proxy.bounds_min = min;
    lod->proxy.bounds_max = max;
    lod->proxy_ready = true;
done:
    return;
}

/** Give every entity the traversal hid back to the ordinary extract.
 *
 * @param lod The state
 */
static void box_render_lod_stop(
    box_lod_state_t *lod)
{
    int32_t root = lod->tree.root;
    while (root >= 0) {
        box_lod_node_t *n = box_lod_tree_get(&lod->tree, root);
        int32_t next = n->next;

        box_render_lod_hide(lod, root, false);
        root = next;
    }

    box_lod_tree_clear(&lod->tree);
    ecs_map_clear(&lod->roots);
    ecs_vec_clear(&lod->proxies);
    lod->structure = false;
    lod->active = false;
}

void box_render_lod_update(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    box_lod_state_t *lod = state->lod;
    lod->world = world;
    lod->render = state;
    if (!box_render_lod_views(world, state, p, lod)) {
        if (lod->active) {
            box_render_lod_stop(lod);
        }

        goto done;
    }

    box_render_lod_mesh(world, lod);
    box_render_lod_depth(lod, p);
    lod->active = true;
    lod->stamp ++;
    box_render_lod_sync(lod, state);
    ecs_vec_clear(&lod->results);
    ecs_vec_clear(&lod->proxies);

    int32_t i, count = ecs_vec_count(&lod->views);
    for (i = 0; i < count; i ++) {
        const box_render_lod_view_t *view =
            ecs_vec_get_t(&lod->views, box_render_lod_view_t, i);
        box_lod_walk_stats_t stats;
        box_lod_walk_desc_t desc = {
            .camera = view->camera, .viewport_px = view->viewport_px,
            .threshold = view->threshold, .hysteresis = view->hysteresis,
            .stamp = lod->stamp, .visible = box_render_lod_visible,
            .hidden = i ? NULL : box_render_lod_occluded,
            .open = box_render_lod_open, .emit = box_render_lod_emit, .ctx = lod
        };

        lod->view = view;
        box_lod_tree_walk(&lod->tree, &desc, &stats);
        box_render_lod_publish(world, lod, view->entity, &stats);
    }

    lod->view = NULL;
    box_render_lod_resolve(lod);
done:
    return;
}
