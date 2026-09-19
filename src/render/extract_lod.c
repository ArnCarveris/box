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
#include "lod_cache.h"
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

/** Entities the traversal remembers per frame before it invalidates everything */
#define BOX_RENDER_LOD_TOUCH (4096)

/** The node is in the result list of this frame, which a second walk leaves alone */
#define BOX_RENDER_LOD_EMITTED (65536u)

/** One view of the frame the traversal measures and culls against */
typedef struct box_render_lod_view_t {
    box_lod_camera_t camera; /**< Camera the projected error is measured in */
    box_frustum_t frustum;   /**< Planes a rejected node stops the walk at */
    float viewport_px;       /**< Height of the viewport in pixels */
    float threshold;         /**< Projected error in pixels a node may not exceed */
    float hysteresis;        /**< Band around the threshold a switch has to cross */
    ecs_entity_t entity;     /**< View entity the statistics are written to */
    bool shadow;             /**< The view is the shadow pass of the view before it */
} box_render_lod_view_t;

/** One instance a generated proxy contributes, with the mesh it is drawn with */
typedef struct box_lod_draw_t {
    box_instance_t inst; /**< The instance */
    uint32_t mesh;       /**< Mesh handle */
} box_lod_draw_t;

/** One node whose proxy the traversal would like the generator to build */
typedef struct box_lod_want_t {
    int32_t node;  /**< Index of the node */
    float score;   /**< Pixels the node covers, which orders the generation */
    float need;    /**< Error in world units the node asks the proxy for */
} box_lod_want_t;

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
    ecs_vec_t parts;       /**< box_lod_draw_t of the generated proxies of this frame */
    ecs_vec_t order;       /**< int32_t parts, grouped by the mesh they are drawn with */
    ecs_vec_t groups;      /**< box_dynamic_batch_t one run of that order becomes */
    box_lod_want_t wants[BOX_LOD_CACHE_STARTS]; /**< Largest nodes waiting for a proxy */
    ecs_vec_t stats;       /**< box_lod_walk_stats_t of the walks of this frame */
    ecs_vec_t touched;     /**< ecs_entity_t whose change invalidates the proxies above it */
    ecs_vec_t inputs;      /**< box_lod_primitive_t of the subtree being collected */
    box_lod_cache_t cache; /**< Generated proxies, keyed by the identity of a node */
    box_lod_collect_t collect; /**< The collection the inputs belong to */
    mat4 collect_base;     /**< World to local of the node being collected */
    int32_t counts[BOX_LOD_LEVEL_MAX]; /**< Proxies drawn per representation */
    bool touch_all;        /**< More entities changed than the list can hold */
    bool quiet;            /**< The traversal is showing an entity, which is no change */
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
    ecs_vec_init_t(NULL, &result->parts, box_lod_draw_t, 0);
    ecs_vec_init_t(NULL, &result->order, int32_t, 0);
    ecs_vec_init_t(NULL, &result->groups, box_dynamic_batch_t, 0);
    ecs_vec_init_t(NULL, &result->stats, box_lod_walk_stats_t, 0);
    ecs_vec_init_t(NULL, &result->touched, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &result->inputs, box_lod_primitive_t, 0);
    box_lod_cache_init(&result->cache);
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
    ecs_vec_fini_t(NULL, &lod->parts, box_lod_draw_t);
    ecs_vec_fini_t(NULL, &lod->order, int32_t);
    ecs_vec_fini_t(NULL, &lod->groups, box_dynamic_batch_t);
    ecs_vec_fini_t(NULL, &lod->stats, box_lod_walk_stats_t);
    ecs_vec_fini_t(NULL, &lod->touched, ecs_entity_t);
    ecs_vec_fini_t(NULL, &lod->inputs, box_lod_primitive_t);
    box_lod_cache_fini(&lod->cache);
    ecs_os_free(lod);
done:
    return;
}

/** Hide or show one renderable, releasing or reclaiming its static slot.
 *
 * Showing an entity queues it for extraction again, which is not a change of
 * the entity, so the traversal keeps quiet while it does so and the proxies
 * above the entity survive.
 *
 * @param lod The state
 * @param e The entity
 * @param hidden The traversal replaced the entity by a proxy
 */
static void box_render_lod_mark(
    box_lod_state_t *lod,
    ecs_entity_t e,
    bool hidden)
{
    ecs_world_t *world = lod->world;
    RenderInstance *inst = ecs_get_mut(world, e, RenderInstance);
    if (!inst || inst->lod_hidden == hidden) {
        goto done;
    }

    inst->lod_hidden = hidden;
    if (!hidden) {
        lod->quiet = true;
        box_render_mark_dirty(world, e);
        lod->quiet = false;
        goto done;
    }

    if (box_render_has_slot(lod->render, e, inst)) {
        box_render_release_slot(world, lod->render, e, inst);
    }

done:
    return;
}

box_lod_cache_t* box_render_lod_cache(
    RenderState *state)
{
    return state->lod ? &state->lod->cache : NULL;
}

void box_render_lod_touch(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e)
{
    box_lod_state_t *lod = state->lod;
    (void)world;
    if (!lod || !lod->active || lod->quiet || lod->touch_all) {
        goto done;
    }

    if (ecs_vec_count(&lod->touched) >= BOX_RENDER_LOD_TOUCH) {
        lod->touch_all = true;
        goto done;
    }

    ecs_vec_append_t(NULL, &lod->touched, ecs_entity_t)[0] = e;
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

    box_render_lod_mark(lod, e, lod->build != 0);
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
    (void)vote;

    if (n && !(n->votes & BOX_RENDER_LOD_EMITTED)) {
        n->votes |= BOX_RENDER_LOD_EMITTED;
        ecs_vec_append_t(NULL, &lod->results, int32_t)[0] = node;
    }
}

/** Draw a node as one box over its bounds, which every node can fall back to.
 *
 * @param lod The state
 * @param n The node
 */
static void box_render_lod_box(
    box_lod_state_t *lod,
    const box_lod_node_t *n)
{
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
}

/** The transform that takes the local space of a node into the world.
 *
 * A generated proxy is built in that space, so the entity of the node moves,
 * turns and scales without anything being built again; a cluster has no entity
 * of its own and its proxy is already in world space.
 *
 * @param lod The state
 * @param n The node
 * @return The transform
 */
static mat4 box_render_lod_base(
    const box_lod_state_t *lod,
    const box_lod_node_t *n)
{
    mat4 result = mat4_identity();
    if (!n->id || n->id >= BOX_RENDER_LOD_CLUSTER_ID) {
        goto done;
    }

    const WorldTransform *wt = ecs_get(lod->world, (ecs_entity_t)n->id, WorldTransform);
    if (wt) {
        result = wt->m;
    }

done:
    return result;
}

/** Draw one part of a generated proxy in the space of its node.
 *
 * @param lod The state
 * @param base Transform that takes the node into the world
 * @param part The part
 */
static void box_render_lod_part(
    box_lod_state_t *lod,
    const mat4 *base,
    const box_lod_part_t *part)
{
    mat4 m = mat4_mul(base, &part->transform);
    RenderMesh mesh = {
        .handle = part->mesh, .scale = vec3_new(1, 1, 1),
        .bounds_min = part->min, .bounds_max = part->max
    };
    box_lod_draw_t *draw = ecs_vec_append_t(NULL, &lod->parts, box_lod_draw_t);
    box_instance_t *inst = &draw->inst;
    int32_t row;

    memset(draw, 0, sizeof(box_lod_draw_t));
    draw->mesh = part->mesh;
    for (row = 0; row < 3; row ++) {
        inst->transform[row * 4 + 0] = m.v[0 + row];
        inst->transform[row * 4 + 1] = m.v[4 + row];
        inst->transform[row * 4 + 2] = m.v[8 + row];
        inst->transform[row * 4 + 3] = m.v[12 + row];
    }

    memcpy(inst->color, part->color, sizeof(inst->color));
    inst->params[0] = part->params[0];
    inst->params[1] = part->params[1];
    inst->uv_transform[0] = 1.0f;
    inst->uv_transform[1] = 1.0f;
    box_render_instance_bounds(&mesh, &m, inst->bounds);
}

/** Pixels one world unit of a node covers in the view the walk belongs to.
 *
 * @param lod The state
 * @param n The node
 * @return The scale, 0 when the frame has no view
 */
static float box_render_lod_scale(
    const box_lod_state_t *lod,
    const box_lod_node_t *n)
{
    const box_render_lod_view_t *view = lod->view;
    float result = 0.0f;

    if (!view && ecs_vec_count(&lod->views)) {
        view = ecs_vec_get_t(&lod->views, box_render_lod_view_t, 0);
    }

    if (!view) {
        goto done;
    }

    box_lod_bounds_t bounds = { .min = n->min, .max = n->max };

    result = box_lod_error_scale(&bounds, &view->camera, view->viewport_px);
done:
    return result;
}

/** The view the running walk belongs to, or the first view of the frame.
 *
 * @param lod The state
 * @return The view, NULL when the frame has none
 */
static const box_render_lod_view_t* box_render_lod_current(
    const box_lod_state_t *lod)
{
    const box_render_lod_view_t *result = lod->view;
    if (!result && ecs_vec_count(&lod->views)) {
        result = ecs_vec_get_t(&lod->views, box_render_lod_view_t, 0);
    }

    return result;
}

/** Whether a node is small enough on screen for a proxy to stand in for it.
 *
 * A subtree that fills the view is never replaced by a generated proxy: the
 * error of such a proxy may well be under the threshold, but the detail it
 * drops is what the viewer is looking at, and the draws it saves are the ones
 * the frame is there for.
 *
 * @param lod The state
 * @param n The node
 * @param scale Pixels one world unit covers at the bounds of the node
 * @return True when a generated proxy may replace the node
 */
static bool box_render_lod_small(
    const box_lod_state_t *lod,
    const box_lod_node_t *n,
    float scale)
{
    const box_render_lod_view_t *view = box_render_lod_current(lod);
    vec3 size = vec3_sub(n->max, n->min);

    return view && scale * vec3_length(size) <= view->viewport_px * BOX_LOD_CACHE_SIZE;
}

/** The error in world units a node may be replaced with, 0 when it has none.
 *
 * @param lod The state
 * @param n The node
 * @return The error the view of the walk allows the node
 */
static float box_render_lod_need(
    const box_lod_state_t *lod,
    const box_lod_node_t *n)
{
    const box_render_lod_view_t *view = box_render_lod_current(lod);
    float scale = box_render_lod_scale(lod, n);

    return view && scale > 0.0f ? view->threshold / scale : 0.0f;
}

/** The projected error ladder of a node, the finest representation first.
 *
 * The ladder is always the same three levels, so a representation the cache
 * does not hold takes the error of the level under it and the walk falls back
 * to the box of the node without ever picking something it cannot draw. The
 * cheapest level that stays under the threshold wins, which is why a node the
 * box already stands in for never spends a generated proxy on itself.
 *
 * @param ctx The state
 * @param node Index of the node
 * @param error Receives the error of every level in world units
 * @return The number of levels
 */
static int32_t box_render_lod_ladder(
    void *ctx,
    int32_t node,
    float *error)
{
    box_lod_state_t *lod = ctx;
    const box_lod_node_t *n = box_lod_tree_get(&lod->tree, node);
    const box_lod_entry_t *entry = box_lod_cache_find(&lod->cache, n->id);
    bool usable = entry && entry->ready &&
        box_render_lod_small(lod, n, box_render_lod_scale(lod, n));
    float shape = n->error;
    float mesh = n->error;

    if (usable && entry->shape_count > 0 && entry->shape_count <= BOX_LOD_CACHE_SHAPES &&
        ecs_vec_count(&entry->parts) && entry->shape_error < shape)
    {
        shape = entry->shape_error;
    }

    mesh = shape;
    if (usable && entry->mesh && entry->mesh_error < mesh) {
        mesh = entry->mesh_error;
    }

    error[BOX_LOD_LEVEL_MESH - 1] = mesh;
    error[BOX_LOD_LEVEL_SHAPES - 1] = shape;
    error[BOX_LOD_LEVEL_BOX - 1] = n->error;

    return BOX_LOD_LEVEL_MAX;
}

/** Draw the representation of a node the walks replaced.
 *
 * @param lod The state
 * @param n The node
 */
static void box_render_lod_proxy(
    box_lod_state_t *lod,
    const box_lod_node_t *n)
{
    const box_lod_entry_t *entry = box_lod_cache_find(&lod->cache, n->id);
    int32_t level = n->level;
    if (!lod->proxy_ready) {
        goto done;
    }

    if (level < BOX_LOD_LEVEL_MESH || level > BOX_LOD_LEVEL_BOX || !entry ||
        !entry->ready)
    {
        level = BOX_LOD_LEVEL_BOX;
    }

    if (level == BOX_LOD_LEVEL_MESH && !entry->mesh) {
        level = BOX_LOD_LEVEL_SHAPES;
    }

    if (level == BOX_LOD_LEVEL_SHAPES && (!entry || !ecs_vec_count(&entry->parts))) {
        level = BOX_LOD_LEVEL_BOX;
    }

    lod->counts[level - 1] ++;
    if (level == BOX_LOD_LEVEL_BOX) {
        box_render_lod_box(lod, n);
        goto done;
    }

    mat4 base = box_render_lod_base(lod, n);
    if (level == BOX_LOD_LEVEL_MESH) {
        box_lod_part_t part = {
            .transform = entry->transform, .min = entry->min, .max = entry->max,
            .mesh = entry->mesh
        };

        memcpy(part.color, entry->color, sizeof(part.color));
        part.params[0] = entry->params[0];
        part.params[1] = entry->params[1];
        box_render_lod_part(lod, &base, &part);
        goto done;
    }

    int32_t i, count = ecs_vec_count(&entry->parts);
    for (i = 0; i < count; i ++) {
        box_render_lod_part(lod, &base,
            ecs_vec_get_t(&entry->parts, box_lod_part_t, i));
    }

done:
    return;
}

/** Remember a node whose proxy the generator has not built yet.
 *
 * The score is the size of the node on screen in the first view of the frame,
 * so only the largest handful of the nodes a frame replaced are kept and the
 * nearest subtrees get their proxy first.
 *
 * @param lod The state
 * @param node Index of the node
 * @param n The node
 */
static void box_render_lod_want(
    box_lod_state_t *lod,
    int32_t node,
    const box_lod_node_t *n)
{
    const box_lod_entry_t *entry = box_lod_cache_find(&lod->cache, n->id);
    bool waiting = entry && (entry->job || (entry->ready && !entry->stale));
    float scale = box_render_lod_scale(lod, n);
    const box_render_lod_view_t *view = box_render_lod_current(lod);
    if (waiting || scale <= 0.0f || !view || !box_render_lod_small(lod, n, scale)) {
        goto done;
    }

    vec3 size = vec3_sub(n->max, n->min);
    float score = scale * vec3_length(size);
    int32_t i, worst = 0;

    for (i = 0; i < BOX_LOD_CACHE_STARTS; i ++) {
        if (lod->wants[i].node < 0) {
            worst = i;
            break;
        }

        if (lod->wants[i].score < lod->wants[worst].score) {
            worst = i;
        }
    }

    if (lod->wants[worst].node >= 0 && lod->wants[worst].score >= score) {
        goto done;
    }

    lod->wants[worst].node = node;
    lod->wants[worst].score = score;
    lod->wants[worst].need = view->threshold / scale;
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
            if (n->leaves >= BOX_LOD_CACHE_SHAPES) {
                box_render_lod_want(lod, index, n);
            }

            continue;
        }

        if (n->votes & BOX_LOD_VOTE_DRAW) {
            n->flags &= ~BOX_RENDER_LOD_HIDDEN;
            box_render_lod_mark(lod, (ecs_entity_t)n->id, false);
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
            box_render_lod_want(lod, index, n);
        }
    }
}

/** Append the generated parts of this frame, one batch per mesh they share.
 *
 * The parts are counted per mesh and scattered into that order, so a batch is
 * one run of the order and the cost of grouping them is one pass over the
 * parts whatever the number of meshes is.
 *
 * @param lod The state
 * @param p The packet
 */
static void box_render_lod_parts(
    box_lod_state_t *lod,
    box_packet_t *p)
{
    int32_t count = ecs_vec_count(&lod->parts);
    ecs_map_t index;
    int32_t i, running = 0;
    if (!count) {
        goto done;
    }

    ecs_map_init(&index, NULL);
    ecs_vec_clear(&lod->groups);
    ecs_vec_set_count_t(NULL, &lod->order, int32_t, count);
    for (i = 0; i < count; i ++) {
        uint32_t mesh = ecs_vec_get_t(&lod->parts, box_lod_draw_t, i)->mesh;
        ecs_map_val_t *slot = ecs_map_ensure(&index, mesh);
        if (!slot[0]) {
            box_dynamic_batch_t *g =
                ecs_vec_append_t(NULL, &lod->groups, box_dynamic_batch_t);

            g->mesh = mesh;
            g->first = 0;
            g->count = 0;
            slot[0] = (ecs_map_val_t)ecs_vec_count(&lod->groups);
        }

        ecs_vec_get_t(&lod->groups, box_dynamic_batch_t, (int32_t)slot[0] - 1)->count ++;
    }

    int32_t groups = ecs_vec_count(&lod->groups);
    int32_t *cursor = ecs_os_malloc_n(int32_t, groups);
    for (i = 0; i < groups; i ++) {
        box_dynamic_batch_t *g = ecs_vec_get_t(&lod->groups, box_dynamic_batch_t, i);

        g->first = (uint32_t)running;
        cursor[i] = running;
        running += (int32_t)g->count;
    }

    for (i = 0; i < count; i ++) {
        uint32_t mesh = ecs_vec_get_t(&lod->parts, box_lod_draw_t, i)->mesh;
        int32_t g = (int32_t)ecs_map_get(&index, mesh)[0] - 1;

        ecs_vec_get_t(&lod->order, int32_t, cursor[g] ++)[0] = i;
    }

    for (i = 0; i < groups; i ++) {
        const box_dynamic_batch_t *g =
            ecs_vec_get_t(&lod->groups, box_dynamic_batch_t, i);
        box_dynamic_batch_t *batch = box_vec_push(&p->dynamic_batches);
        uint32_t k;

        batch->mesh = g->mesh;
        batch->first = p->dynamic_instances.count;
        batch->count = g->count;
        for (k = 0; k < g->count; k ++) {
            int32_t part = ecs_vec_get_t(&lod->order, int32_t,
                (int32_t)(g->first + k))[0];
            box_instance_t *dst = box_vec_push(&p->dynamic_instances);

            dst[0] = ecs_vec_get_t(&lod->parts, box_lod_draw_t, part)->inst;
        }
    }

    ecs_os_free(cursor);
    ecs_map_fini(&index);
done:
    return;
}

void box_render_lod_proxies(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    box_lod_state_t *lod = state->lod;
    int32_t count = lod ? ecs_vec_count(&lod->proxies) : 0;
    (void)world;
    if (!lod) {
        goto done;
    }

    if (count) {
        box_dynamic_batch_t *batch = box_vec_push(&p->dynamic_batches);
        int32_t i;

        batch->mesh = lod->proxy_mesh;
        batch->first = p->dynamic_instances.count;
        batch->count = (uint32_t)count;
        for (i = 0; i < count; i ++) {
            box_instance_t *dst = box_vec_push(&p->dynamic_instances);

            dst[0] = ecs_vec_get_t(&lod->proxies, box_instance_t, i)[0];
        }
    }

    box_render_lod_parts(lod, p);
done:
    return;
}

/** Add one renderable of a subtree to the inputs of a generation.
 *
 * @param lod The state
 * @param e The entity
 */
static void box_render_lod_input(
    box_lod_state_t *lod,
    ecs_entity_t e)
{
    ecs_world_t *world = lod->world;
    const RenderInstance *inst = ecs_get(world, e, RenderInstance);
    const RenderMesh *mesh = ecs_get(world, e, RenderMesh);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    if (!inst || !mesh || !wt || inst->dynamic || inst->transparent) {
        goto done;
    }

    if (lod->collect.count >= BOX_LOD_CACHE_INPUTS) {
        lod->collect.full = true;
        goto done;
    }

    const Rgb *rgb = ecs_get(world, e, Rgb);
    const Opacity *opacity = ecs_get(world, e, Opacity);
    const PbrMaterial *pbr = ecs_get(world, e, PbrMaterial);
    mat4 scaling = mat4_scaling(mesh->scale);
    mat4 world_m = mat4_mul(&wt->m, &scaling);
    mat4 local = mat4_mul(&lod->collect_base, &world_m);
    float alpha = opacity ? opacity->value : 1.0f;
    rgba color = {
        rgb ? rgb->r : 255, rgb ? rgb->g : 255, rgb ? rgb->b : 255,
        (uint8_t)((alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha)) * 255.0f + 0.5f)
    };
    vec3 extent = vec3_sub(mesh->bounds_max, mesh->bounds_min);
    float sx = vec3_length(vec3_new(local.v[0], local.v[1], local.v[2]));
    float sy = vec3_length(vec3_new(local.v[4], local.v[5], local.v[6]));
    float sz = vec3_length(vec3_new(local.v[8], local.v[9], local.v[10]));
    box_lod_primitive_t *prim =
        ecs_vec_append_t(NULL, &lod->inputs, box_lod_primitive_t);

    prim->transform = local;
    prim->min = mesh->bounds_min;
    prim->max = mesh->bounds_max;
    prim->material = box_lod_cache_material(color, pbr ? pbr->metallic : 0.0f,
        pbr ? pbr->roughness : 0.5f);
    prim->volume = extent.x * sx * extent.y * sy * extent.z * sz;
    lod->collect.count ++;
done:
    return;
}

/** Add an entity and everything below it to the inputs of a generation.
 *
 * @param world The world
 * @param e The entity
 * @param ctx The state
 */
static void box_render_lod_gather(
    ecs_world_t *world,
    ecs_entity_t e,
    void *ctx)
{
    box_lod_state_t *lod = ctx;
    if (ecs_has_id(world, e, EcsPrefab)) {
        goto done;
    }

    box_render_lod_input(lod, e);
    box_hierarchy_children(world, e, box_render_lod_gather, lod);
done:
    return;
}

/** Add the subtree of a node to the inputs, descending through its clusters.
 *
 * @param lod The state
 * @param node Index of the node
 */
static void box_render_lod_gather_node(
    box_lod_state_t *lod,
    int32_t node)
{
    const box_lod_node_t *n = box_lod_tree_get(&lod->tree, node);
    if (!n) {
        goto done;
    }

    if (n->id && n->id < BOX_RENDER_LOD_CLUSTER_ID) {
        box_render_lod_gather(lod->world, (ecs_entity_t)n->id, lod);
        goto done;
    }

    int32_t child = n->first_child;
    while (child >= 0) {
        const box_lod_node_t *c = box_lod_tree_get(&lod->tree, child);

        box_render_lod_gather_node(lod, child);
        child = c ? c->next : -1;
    }

done:
    return;
}

/** Collect the primitives of the subtree of a node in its own space.
 *
 * @param lod The state
 * @param node Index of the node
 * @return True when the subtree fits in one generation
 */
static bool box_render_lod_collect(
    box_lod_state_t *lod,
    int32_t node)
{
    const box_lod_node_t *n = box_lod_tree_get(&lod->tree, node);
    mat4 base = box_render_lod_base(lod, n);

    lod->collect_base = mat4_inverse(&base);
    lod->collect.count = 0;
    lod->collect.max = BOX_LOD_CACHE_INPUTS;
    lod->collect.full = false;
    ecs_vec_clear(&lod->inputs);
    box_render_lod_gather_node(lod, node);
    lod->collect.primitives = ecs_vec_first_t(&lod->inputs, box_lod_primitive_t);

    return lod->collect.count > 0 && !lod->collect.full;
}

/** The root a node belongs to, which drops its entry with it.
 *
 * @param lod The state
 * @param node Index of the node
 * @return The identity of the root, 0 for a node under no entity
 */
static uint64_t box_render_lod_owner(
    const box_lod_state_t *lod,
    int32_t node)
{
    uint64_t result = 0;
    int32_t cur = node;
    while (cur >= 0) {
        const box_lod_node_t *n = box_lod_tree_get(&lod->tree, cur);

        if (!n) {
            break;
        }

        if (n->id && n->id < BOX_RENDER_LOD_CLUSTER_ID) {
            result = n->id;
        }

        cur = n->parent;
    }

    return result;
}

/** Forget the nodes the previous frame wanted a proxy for.
 *
 * @param lod The state
 */
static void box_render_lod_clear_wants(
    box_lod_state_t *lod)
{
    int32_t i;
    for (i = 0; i < BOX_LOD_CACHE_STARTS; i ++) {
        lod->wants[i].node = -1;
        lod->wants[i].score = 0.0f;
        lod->wants[i].need = 0.0f;
    }
}

/** Hand the largest of the wanted proxies of this frame to the generator.
 *
 * Only a handful of subtrees are collected per frame, the largest on screen
 * first, so the pass over the entities of a subtree never grows with the
 * number of nodes one frame walked.
 *
 * @param lod The state
 */
static void box_render_lod_generate(
    box_lod_state_t *lod)
{
    int32_t i;
    for (i = 0; i < BOX_LOD_CACHE_STARTS; i ++) {
        int32_t index = lod->wants[i].node;
        const box_lod_node_t *n = index >= 0 ? box_lod_tree_get(&lod->tree, index) : NULL;
        if (!n || !box_render_lod_collect(lod, index)) {
            continue;
        }

        vec3 center = vec3_scale(vec3_add(n->min, n->max), 0.5f);

        box_lod_cache_submit(lod->world, &lod->cache, n->id,
            box_render_lod_owner(lod, index), &lod->collect, lod->wants[i].need,
            (int32_t)lod->wants[i].score, center);
    }

    box_render_lod_clear_wants(lod);
}

/** Throw away every proxy that holds an entity, and nothing else.
 *
 * @param lod The state
 * @param e The entity
 */
static void box_render_lod_invalidate(
    box_lod_state_t *lod,
    ecs_entity_t e)
{
    ecs_world_t *world = lod->world;
    ecs_entity_t cur = e;
    ecs_map_val_t *slot = NULL;

    while (cur && !slot) {
        box_lod_cache_invalidate(&lod->cache, cur);
        slot = ecs_map_get(&lod->roots, cur);
        cur = slot ? cur : box_hierarchy_parent(world, cur);
    }

    if (!slot) {
        goto done;
    }

    const box_lod_node_t *n = box_lod_tree_get(&lod->tree, (int32_t)slot[0] - 1);
    while (n && n->parent >= 0) {
        n = box_lod_tree_get(&lod->tree, n->parent);
        if (n) {
            box_lod_cache_invalidate(&lod->cache, n->id);
        }
    }

done:
    return;
}

/** Apply the changes the extract of the previous frame reported.
 *
 * @param lod The state
 */
static void box_render_lod_changes(
    box_lod_state_t *lod)
{
    int32_t i, count = ecs_vec_count(&lod->touched);
    if (lod->touch_all) {
        box_lod_cache_clear(lod->world, &lod->cache);
        lod->touch_all = false;
        goto done;
    }

    for (i = 0; i < count; i ++) {
        box_render_lod_invalidate(lod,
            ecs_vec_get_t(&lod->touched, ecs_entity_t, i)[0]);
    }

done:
    ecs_vec_clear(&lod->touched);
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
    if (ecs_has_id(lod->world, (ecs_entity_t)n->id, LodPin)) {
        n->flags |= BOX_LOD_NODE_PIN;
    }
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

    box_lod_cache_drop_root(lod->world, &lod->cache, 0);

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

/** Follow a root that moved, which is not a reason to build anything again.
 *
 * A rigid move only changes the place of the root, and the proxies of its
 * subtree are held in the space of the node they belong to, so the node takes
 * its new bounds and keeps everything the generator built for it. A root whose
 * leaf count changed is a different subtree and gives its proxies up.
 *
 * @param lod The state
 * @param node Index of the root node
 * @param rb The aggregated bounds of the root
 */
static void box_render_lod_moved(
    box_lod_state_t *lod,
    int32_t node,
    const RootBounds *rb)
{
    box_lod_node_t *n = box_lod_tree_get(&lod->tree, node);
    int32_t leaves = rb->count > 0 ? rb->count : 1;
    if (!n) {
        goto done;
    }

    if (n->leaves != leaves) {
        box_lod_cache_drop_root(lod->world, &lod->cache, n->id);
        box_render_lod_root(lod, node, rb);
        goto done;
    }

    if (!memcmp(&n->min, &rb->min, sizeof(vec3)) &&
        !memcmp(&n->max, &rb->max, sizeof(vec3)))
    {
        goto done;
    }

    float filled = n->weight;
    uint32_t flags = n->flags;

    box_lod_tree_set_bounds(&lod->tree, node, rb->min, rb->max,
        box_lod_tree_shell(rb->min, rb->max, filled));
    n = box_lod_tree_get(&lod->tree, node);
    n->weight = filled;
    n->flags = flags;
done:
    return;
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
            ecs_map_val_t *slot = ecs_map_get(&lod->roots, it.entities[i]);

            found ++;
            if (slot) {
                box_render_lod_moved(lod, (int32_t)slot[0] - 1, &rb[i]);
                continue;
            }

            if (!rb[i].count) {
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
        box_lod_cache_drop_root(world, &lod->cache, e);
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

/** Give a view the shadow pass it casts, measured against its own threshold.
 *
 * The pass is the widest cascade of the view: an orthographic camera over the
 * sphere the cascade covers, drawn at the resolution of the shadow map, which
 * is why a caster the camera cannot see is allowed the coarser error of
 * shadow_error_px instead of the error of the view.
 *
 * @param world The world
 * @param state The registry
 * @param src The view of the frame
 * @param v The probed view, whose cascades this fits
 * @param out Receives the shadow pass, taking the rest from the view before it
 * @return True when the view casts shadows
 */
static bool box_render_lod_shadow(
    ecs_world_t *world,
    RenderState *state,
    const box_view_src_t *src,
    box_view_t *v,
    box_render_lod_view_t *out)
{
    bool result = false;
    if (v->lod_shadow_error_px <= 0.0f) {
        goto done;
    }

    if (!src->entity) {
        v->shadows = 1;
    }

    bool sun = box_render_view_sun(world, state, v);
    box_render_extract_shadows(world, src->entity, sun, v);
    if (!v->shadow_cascades) {
        goto done;
    }

    uint32_t last = v->shadow_cascades - 1;
    float radius = v->shadow_bounds[last * 4 + 3];

    out[1] = out[0];
    out[1].shadow = true;
    out[1].camera.orthographic = true;
    out[1].camera.ortho_height = radius * 2.0f;
    out[1].viewport_px = (float)v->shadow_resolution;
    out[1].threshold = v->lod_shadow_error_px;
    box_frustum_of_clip(v->shadow_matrix + last * 16, &out[1].frustum);
    result = out[1].viewport_px > 0.0f && out[1].threshold > 0.0f;
done:
    return result;
}

/** Read the camera, the frustum and the thresholds of one view of the frame.
 *
 * @param world The world
 * @param state The registry
 * @param src The view of the frame
 * @param p The packet, for the size a view without a target draws at
 * @param out Receives the view and, after it, its shadow pass
 * @return The number of views the traversal walks for this view
 */
static int32_t box_render_lod_view(
    ecs_world_t *world,
    RenderState *state,
    const box_view_src_t *src,
    const box_packet_t *p,
    box_render_lod_view_t *out)
{
    box_view_t v;
    const LodSettings *s = box_render_view_setting(world, src->entity, ecs_id(LodSettings));
    const Camera *c = src->camera ? ecs_get(world, src->camera, Camera) : NULL;
    int32_t result = 0;
    if (!s || !s->enabled || !c || !ecs_has(world, src->camera, WorldTransform)) {
        goto done;
    }

    box_render_view_probe(world, src, p, &v);
    memset(out, 0, 2 * sizeof(box_render_lod_view_t));
    out->entity = src->entity;
    out->camera.position = vec3_new(v.eye[0], v.eye[1], v.eye[2]);
    out->camera.fov = c->fov;
    out->camera.ortho_height = c->ortho_height;
    out->camera.orthographic = c->orthographic;
    out->viewport_px = (float)v.height;
    out->threshold = v.lod_error_px;
    out->hysteresis = v.lod_hysteresis;
    box_frustum_of_view(&v, &out->frustum);
    if (out->viewport_px <= 0.0f || out->threshold <= 0.0f) {
        goto done;
    }

    result = 1;
    if (box_render_lod_shadow(world, state, src, &v, out)) {
        result = 2;
    }

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
    box_render_lod_view_t view[2];
    int32_t count = ecs_vec_count(&state->view_list);
    int32_t i, j, made;

    ecs_vec_clear(&lod->views);
    for (i = 0; i < count; i ++) {
        const box_view_src_t *src = ecs_vec_get_t(&state->view_list, box_view_src_t, i);

        made = box_render_lod_view(world, state, src, p, view);
        for (j = 0; j < made; j ++) {
            ecs_vec_append_t(NULL, &lod->views, box_render_lod_view_t)[0] = view[j];
        }
    }

    if (!count) {
        box_view_src_t src = { .camera = box_render_active_camera(world) };

        made = box_render_lod_view(world, state, &src, p, view);
        for (j = 0; j < made; j ++) {
            ecs_vec_append_t(NULL, &lod->views, box_render_lod_view_t)[0] = view[j];
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
        .nodes = lod->tree.count,
        .meshes = lod->counts[BOX_LOD_LEVEL_MESH - 1],
        .shapes = lod->counts[BOX_LOD_LEVEL_SHAPES - 1],
        .boxes = lod->counts[BOX_LOD_LEVEL_BOX - 1],
        .queued = lod->cache.queued,
        .cached = ecs_map_count(&lod->cache.entries)
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

    box_lod_cache_clear(lod->world, &lod->cache);
    box_render_lod_clear_wants(lod);
    box_lod_tree_clear(&lod->tree);
    ecs_map_clear(&lod->roots);
    ecs_vec_clear(&lod->proxies);
    ecs_vec_clear(&lod->parts);
    ecs_vec_clear(&lod->touched);
    lod->touch_all = false;
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
    box_lod_cache_sweep(world, &lod->cache);
    box_render_lod_changes(lod);
    box_render_lod_sync(lod, state);
    ecs_vec_clear(&lod->results);
    ecs_vec_clear(&lod->proxies);
    ecs_vec_clear(&lod->parts);
    ecs_vec_clear(&lod->stats);
    memset(lod->counts, 0, sizeof(lod->counts));
    box_render_lod_clear_wants(lod);

    int32_t i, count = ecs_vec_count(&lod->views);
    for (i = 0; i < count; i ++) {
        const box_render_lod_view_t *view =
            ecs_vec_get_t(&lod->views, box_render_lod_view_t, i);
        box_lod_walk_desc_t desc = {
            .camera = view->camera, .viewport_px = view->viewport_px,
            .threshold = view->threshold, .hysteresis = view->hysteresis,
            .prune_px = view->threshold, .stamp = lod->stamp,
            .visible = box_render_lod_visible,
            .hidden = (i || view->shadow) ? NULL : box_render_lod_occluded,
            .open = box_render_lod_open, .emit = box_render_lod_emit,
            .levels = box_render_lod_ladder, .ctx = lod
        };

        lod->view = view;
        box_lod_tree_walk(&lod->tree, &desc,
            ecs_vec_append_t(NULL, &lod->stats, box_lod_walk_stats_t));
    }

    lod->view = NULL;
    box_render_lod_resolve(lod);
    box_render_lod_generate(lod);
    for (i = 0; i < count; i ++) {
        const box_render_lod_view_t *view =
            ecs_vec_get_t(&lod->views, box_render_lod_view_t, i);

        if (!view->shadow) {
            box_render_lod_publish(world, lod, view->entity,
                ecs_vec_get_t(&lod->stats, box_lod_walk_stats_t, i));
        }
    }

done:
    return;
}
