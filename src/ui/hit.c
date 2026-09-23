/**
 * @file ui/hit.c
 * @brief Resolves the cursor against the rectangles of the UI trees of a view.
 *
 * Installs the pick source box.picking asks before the ray query of a view; an
 * element under the cursor consumes the pick. The deepest node of the topmost subtree
 * that holds the point wins and is written to the UiCapture singleton.
 *
 * Hidden nodes and nodes with HitThrough are skipped.
 */

#include "ui_internal.h"

/** Determinant below which the frame of a node is degenerate and holds nothing */
#define BOX_UI_HIT_EPSILON (1e-6f)

/** The registry of the world, read without taking the deferred copy of a stage.
 *
 * @param world The world
 * @return The registry, or NULL when box.ui is not imported
 */
static UiState* box_ui_hit_state(
    const ecs_world_t *world)
{
    return ECS_CONST_CAST(UiState*, ecs_singleton_get(world, UiState));
}

/** Whether the rectangle of a node holds a point of the plane of its tree.
 *
 * The point is taken into the frame of the node through its world transform, and the
 * comparison is scaled by the determinant of that frame instead of dividing by it.
 *
 * @param t World transform of the node
 * @param b Bounds of the node
 * @param x X of the point in the plane of the tree
 * @param y Y of the point in the plane of the tree
 * @return true when the point is inside the rectangle
 */
static bool box_ui_hit_inside(
    const WorldTransform *t,
    const Bounds *b,
    float x,
    float y)
{
    vec3 ax = mat4_transform_dir(&t->m, vec3_new(1, 0, 0));
    vec3 ay = mat4_transform_dir(&t->m, vec3_new(0, 1, 0));
    vec3 origin = mat4_get_translation(&t->m);
    float det = ax.x * ay.y - ay.x * ax.y;
    float area = det < 0 ? -det : det;
    bool result = false;
    if (b->width > 0 && b->height > 0 && area > BOX_UI_HIT_EPSILON) {
        float dx = x - origin.x;
        float dy = y - origin.y;
        float lx = ay.y * dx - ay.x * dy;
        float ly = ax.x * dy - ax.y * dx;
        float hx = b->width * 0.5f * area;
        float hy = b->height * 0.5f * area;

        result = lx >= -hx && lx <= hx && ly >= -hy && ly <= hy;
    }

    return result;
}

/** Whether a subtree that composites cuts a node off from the cursor.
 *
 * A transparent subtree is rendered into a target the size of the Bounds of its
 * element and is clipped to it, so a child that sticks out of that element is
 * not drawn and is not hit either.
 *
 * @param world The world
 * @param e The node
 * @param x X of the point in the plane of the tree
 * @param y Y of the point in the plane of the tree
 * @return true when a group above the node clips the point away
 */
static bool box_ui_hit_clipped(
    const ecs_world_t *world,
    ecs_entity_t e,
    float x,
    float y)
{
    bool result = false;
    ecs_entity_t cur = box_hierarchy_parent(world, e);
    while (cur && !result) {
        const Bounds *b = ecs_has(world, cur, GroupImpl) ?
            ecs_get(world, cur, Bounds) : NULL;
        const WorldTransform *t = b ? ecs_get(world, cur, WorldTransform) : NULL;
        if (t && !box_ui_hit_inside(t, b, x, y)) {
            result = true;
        }

        cur = box_hierarchy_parent(world, cur);
    }

    return result;
}

/** Whether the clip rectangle of a node leaves the point of a probe out.
 *
 * The rectangle is the one the elements that clip above the node left on it, in
 * the plane of the tree, so a child that a scrolled element cut off is not hit
 * either.
 *
 * @param world The world
 * @param e The node
 * @param x X of the point in the plane of the tree
 * @param y Y of the point in the plane of the tree
 * @return true when the clip of the node leaves the point out
 */
static bool box_ui_hit_clipped_out(
    const ecs_world_t *world,
    ecs_entity_t e,
    float x,
    float y)
{
    const RenderClip *clip = ecs_get(world, e, RenderClip);

    return clip != NULL && (x < clip->min[0] || x > clip->max[0] ||
        y < clip->min[1] || y > clip->max[1]);
}

/** Whether the tree of a root is drawn by the view a pick asks about.
 *
 * @param world The world
 * @param root The root of the tree
 * @param view The view, 0 for the trees of every view
 * @return true when the tree belongs to that view
 */
static bool box_ui_hit_in_view(
    const ecs_world_t *world,
    ecs_entity_t root,
    ecs_entity_t view)
{
    const UiImpl *impl = view ? ecs_get(world, root, UiImpl) : NULL;

    return !view || (impl != NULL && impl->view == view);
}

/** Whether any tree of the world is drawn by a view.
 *
 * Answered from the tree roots, so a view without a tree does not walk the nodes of
 * trees that other views draw.
 *
 * @param world The world
 * @param view The view
 * @return true when a tree is drawn by that view
 */
static bool box_ui_hit_has_view(
    const ecs_world_t *world,
    ecs_entity_t view)
{
    bool result = false;
    ecs_iter_t it = ecs_each_id(world, ecs_id(UiImpl));
    while (!result && ecs_each_next(&it)) {
        const UiImpl *impl = ecs_field(&it, UiImpl, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            result = result || impl[i].view == view;
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** What the walk over the nodes of the world is looking for and has found */
typedef struct box_ui_probe_t {
    float x;             /**< X of the point in the plane of the trees */
    float y;             /**< Y of the point in the plane of the trees */
    ecs_entity_t view;   /**< View whose trees are tested, 0 for every tree */
    ecs_entity_t root;   /**< Root the view of the last node belonged to */
    ecs_entity_t best;   /**< Node the point is in that is drawn over the rest */
    int32_t order;       /**< Place of that node in the walk of its tree */
    bool in_view;        /**< The tree of that root is drawn by the view */
    bool groups;         /**< The world holds subtrees that composite and clip */
    bool clips;          /**< The world holds elements that clip what they hold */
} box_ui_probe_t;

/** Test the nodes of one table against the point of a probe.
 *
 * @param world The world
 * @param probe The probe, which keeps the best node so far
 * @param it Iterator over the nodes of one table
 */
static void box_ui_hit_probe(
    const ecs_world_t *world,
    box_ui_probe_t *probe,
    ecs_iter_t *it)
{
    const UiNode *nodes = ecs_field(it, UiNode, 0);
    const Bounds *bounds = ecs_field(it, Bounds, 1);
    const WorldTransform *xf = ecs_field(it, WorldTransform, 2);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (nodes[i].order <= probe->order) {
            continue;
        }

        if (nodes[i].root != probe->root) {
            probe->root = nodes[i].root;
            probe->in_view = box_ui_hit_in_view(
                world, probe->root, probe->view);
        }

        if (!probe->in_view) {
            continue;
        }

        if (!box_ui_hit_inside(&xf[i], &bounds[i], probe->x, probe->y)) {
            continue;
        }

        if (probe->groups &&
            box_ui_hit_clipped(world, it->entities[i], probe->x, probe->y))
        {
            continue;
        }

        if (probe->clips &&
            box_ui_hit_clipped_out(world, it->entities[i], probe->x, probe->y))
        {
            continue;
        }

        if (box_render_is_hidden(world, it->entities[i])) {
            continue;
        }

        probe->best = it->entities[i];
        probe->order = nodes[i].order;
    }
}

ecs_entity_t box_ui_hit(
    const ecs_world_t *world,
    ecs_entity_t view,
    float x,
    float y)
{
    UiState *state = box_ui_hit_state(world);
    box_ui_probe_t probe = { .x = x, .y = y, .view = view, .order = -1 };
    if (!state || !state->hits || !state->trees) {
        goto done;
    }

    if (view && !box_ui_hit_has_view(world, view)) {
        goto done;
    }

    probe.groups = state->groups != 0;
    probe.clips = state->scrolls != 0;
    ecs_iter_t it = ecs_query_iter(world, state->hits);
    while (ecs_query_next(&it)) {
        box_ui_hit_probe(world, &probe, &it);
    }

done:
    return probe.best;
}

/** Answer the pick of a UI view from the rectangles of the trees it draws.
 *
 * The cursor ray is intersected with the z = 0 plane of the trees. The hit reports that
 * point, the camera as normal and no collider.
 *
 * @param world The world
 * @param view The view being picked
 * @param cursor Normalized position in the image of the view
 * @param out Receives the entity, the point and the normal of the hit
 * @return true when an element of the view is under the cursor
 */
static bool box_ui_hit_pick(
    const ecs_world_t *world,
    ecs_entity_t view,
    vec2 cursor,
    PickHit *out)
{
    UiState *state = box_ui_hit_state(world);
    vec3 origin, dir;
    bool result = false;
    if (!state || !state->trees || !view) {
        goto done;
    }

    if (!box_picking_ray(world, view, cursor, &origin, &dir)) {
        goto done;
    }

    if (dir.z > -BOX_UI_HIT_EPSILON && dir.z < BOX_UI_HIT_EPSILON) {
        goto done;
    }

    float distance = -origin.z / dir.z;
    if (distance < 0) {
        goto done;
    }

    vec3 point = vec3_add(origin, vec3_scale(dir, distance));
    ecs_entity_t e = box_ui_hit(world, view, point.x, point.y);
    if (!e) {
        goto done;
    }

    out->entity = e;
    out->point = point;
    out->normal = vec3_scale(dir, -1.0f);
    out->distance = distance;
    result = true;
done:
    return result;
}

/** Node of a tree the pick of this frame left the Hovered tag on.
 *
 * The tag sits on at most one entity of the world, so the answer costs a walk
 * over a single table and needs no state of its own; the pick keeps it current
 * for a still cursor as well, because a tree that changed drops its cached hit.
 *
 * @param world The world
 * @return The node under the cursor, or 0 when the cursor is beside every tree
 */
static ecs_entity_t box_ui_hit_hovered(
    const ecs_world_t *world)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_each_id(world, Hovered);
    while (!result && ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (ecs_has(it.world, it.entities[i], UiNode)) {
                result = it.entities[i];
            }
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Write what the hit test of this frame left the UI holding of the pointer.
 *
 * @param it Iterator of the system, which has no terms
 */
static void BoxUiCapture(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    UiCapture *capture = ecs_get_mut(world, ecs_id(UiCapture), UiCapture);
    ecs_entity_t element = box_ui_hit_hovered(world);
    capture->element = element;
    capture->mouse = element != 0;
}

bool box_ui_mouse_captured(
    const ecs_world_t *world)
{
    const UiCapture *capture = ecs_id(UiCapture)
        ? ecs_singleton_get(world, UiCapture)
        : NULL;

    return capture != NULL && capture->mouse;
}

/** Tell whether the tree of an element lets events no node took through.
 *
 * @param world The world
 * @param e The element the hit test answered with
 * @return true when the Ui of its tree has passthrough set
 */
static bool box_ui_hit_through(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const UiNode *node = ecs_get(world, e, UiNode);
    const Ui *ui = node ? ecs_get(world, node->root, Ui) : NULL;

    return ui != NULL && ui->passthrough;
}

void box_ui_hit_init(
    ecs_world_t *world)
{
    UiState *state = box_ui_state(world);

    state->hits = ecs_query(world, {
        .terms = {
            { .id = ecs_id(UiNode), .inout = EcsIn },
            { .id = ecs_id(Bounds), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            { .id = UiQuad, .oper = EcsNot },
            { .id = HitThrough, .oper = EcsNot }
        }
    });

    ECS_COMPONENT_DEFINE(world, UiCapture);

    ecs_struct(world, {
        .entity = ecs_id(UiCapture),
        .members = {
            { .name = "mouse", .type = ecs_id(ecs_bool_t) },
            { .name = "element", .type = ecs_id(ecs_entity_t) }
        }
    });

    ecs_set_hooks(world, UiCapture, { .ctor = flecs_default_ctor });
    ecs_singleton_ensure(world, UiCapture);
    ecs_singleton_modified(world, UiCapture);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxUiCapture" }),
        .phase = EcsPostLoad,
        .query.terms = {{ .id = ecs_id(UiCapture) }},
        .callback = BoxUiCapture,
        .immediate = true
    });

    box_picking_set_source(world, box_ui_hit_pick);
    box_picking_set_through(world, box_ui_hit_through);
}
