/**
 * @file ui/world.c
 * @brief Pins a UI element to the screen projection of a world entity or point.
 *
 * A WorldAnchor is placed by layout at the point its view camera projects. A pass
 * after the frame transforms compares that projection with the last one and lays the
 * tree out again only when it moved.
 */

#include "ui_internal.h"
#include <math.h>

/** Vertical field of view a camera without one projects with, in degrees */
#define BOX_UI_WORLD_FOV (60.0f)

/** Near plane a camera without one projects with */
#define BOX_UI_WORLD_NEAR (0.1f)

/** Far plane an orthographic camera without one projects with */
#define BOX_UI_WORLD_FAR (1000.0f)

/** Visible height an orthographic camera without one projects with */
#define BOX_UI_WORLD_ORTHO_HEIGHT (10.0f)

/** Radians per degree */
#define BOX_UI_WORLD_RADIANS (3.14159265f / 180.0f)

ECS_COMPONENT_DECLARE(WorldAnchorImpl);
ECS_TAG_DECLARE(UiWorldHidden);

/** Width over height of the surface, which a view without a target renders at.
 *
 * @param world The world
 * @return The aspect ratio, one when the surface says nothing about its size
 */
static float box_ui_world_surface_aspect(
    ecs_world_t *world)
{
    float result = 1.0f;
    const Surface *surface = ecs_singleton_get(world, Surface);
    if (!surface) {
        goto done;
    }

    float w = (float)surface->scaled_width;
    float h = (float)surface->scaled_height;
    if (w <= 0 || h <= 0) {
        w = (float)(surface->actual_width ? surface->actual_width : surface->width);
        h = (float)(surface->actual_height ? surface->actual_height : surface->height);
    }

    if (w > 0 && h > 0) {
        result = w / h;
    }

done:
    return result;
}

/** Find the camera an anchor projects through and the aspect of its view.
 *
 * @param world The world
 * @param anchor The anchor
 * @param aspect Receives the width over height of the view
 * @return The camera entity, 0 when the anchor has none
 */
static ecs_entity_t box_ui_world_camera(
    ecs_world_t *world,
    const WorldAnchor *anchor,
    float *aspect)
{
    ecs_entity_t result = 0;
    *aspect = 0;
    if (anchor->view && ecs_is_alive(world, anchor->view)) {
        const View *view = ecs_get(world, anchor->view, View);
        const RenderTarget *target = view && view->target ?
            ecs_get(world, view->target, RenderTarget) : NULL;
        result = view ? view->camera : 0;
        if (target && target->width > 0 && target->height > 0) {
            *aspect = (float)target->width / (float)target->height;
        }
    } else {
        result = box_render_active_camera(world);
    }

    if (*aspect <= 0) {
        *aspect = box_ui_world_surface_aspect(world);
    }

    return result;
}

/** Build the view and projection matrices of the camera of an anchor.
 *
 * @param world The world
 * @param anchor The anchor
 * @param view Receives the view matrix
 * @param proj Receives the projection matrix
 * @return Whether the anchor has a camera with a transform to project through
 */
static bool box_ui_world_lens(
    ecs_world_t *world,
    const WorldAnchor *anchor,
    mat4 *view,
    mat4 *proj)
{
    bool result = false;
    float aspect;
    ecs_entity_t camera = box_ui_world_camera(world, anchor, &aspect);
    const Camera *c = camera ? ecs_get(world, camera, Camera) : NULL;
    const WorldTransform *wt = camera ? ecs_get(world, camera, WorldTransform) : NULL;
    if (!c || !wt) {
        goto done;
    }

    float near = c->near_ > 0 ? c->near_ : BOX_UI_WORLD_NEAR;
    float far = c->far_ > 0 ? c->far_ : BOX_UI_WORLD_FAR;
    float ratio = c->aspect_ratio > 0 ? c->aspect_ratio : aspect;

    *view = mat4_inverse(&wt->m);
    if (c->orthographic) {
        float height = c->ortho_height > 0 ?
            c->ortho_height : BOX_UI_WORLD_ORTHO_HEIGHT;
        *proj = mat4_orthographic(height, ratio, near, far);
    } else {
        float fov = c->fov > 0 ? c->fov : BOX_UI_WORLD_FOV;
        *proj = mat4_perspective(fov * BOX_UI_WORLD_RADIANS, ratio, near);
    }

    result = true;
done:
    return result;
}

/** The world point an anchor follows: the entity it names plus its offset.
 *
 * @param world The world
 * @param anchor The anchor
 * @return The point in world units
 */
static vec3 box_ui_world_point(
    ecs_world_t *world,
    const WorldAnchor *anchor)
{
    vec3 result = anchor->offset;
    if (!anchor->entity || !ecs_is_alive(world, anchor->entity)) {
        goto done;
    }

    const WorldTransform *wt = ecs_get(world, anchor->entity, WorldTransform);
    const Position *p = ecs_get(world, anchor->entity, Position);
    if (wt) {
        result = vec3_add(mat4_get_translation(&wt->m), anchor->offset);
    } else if (p) {
        result = vec3_add(vec3_new(p->x, p->y, p->z), anchor->offset);
    }

done:
    return result;
}

bool box_ui_world_project(
    ecs_world_t *world,
    const WorldAnchor *anchor,
    ecs_entity_t root,
    float *x,
    float *y)
{
    bool result = false;
    mat4 view, proj;
    const UiImpl *impl = ecs_get(world, root, UiImpl);
    *x = 0;
    *y = 0;
    if (!impl || !box_ui_world_lens(world, anchor, &view, &proj)) {
        goto done;
    }

    vec3 v = mat4_transform_point(&view, box_ui_world_point(world, anchor));
    float cx = proj.v[0] * v.x + proj.v[4] * v.y + proj.v[8] * v.z + proj.v[12];
    float cy = proj.v[1] * v.x + proj.v[5] * v.y + proj.v[9] * v.z + proj.v[13];
    float cw = proj.v[3] * v.x + proj.v[7] * v.y + proj.v[11] * v.z + proj.v[15];
    if (cw <= 1e-6f) {
        goto done;
    }

    float nx = cx / cw;
    float ny = cy / cw;
    if (nx < -1.0f || nx > 1.0f || ny < -1.0f || ny > 1.0f) {
        goto done;
    }

    *x = nx * impl->width * 0.5f;
    *y = ny * impl->height * 0.5f;
    result = true;
done:
    return result;
}

/** Hide an element the anchor lost, or show one the anchor hid before.
 *
 * @param world The world
 * @param e The element
 * @param visible Whether the projection of the element is in the view
 */
static void box_ui_world_show(
    ecs_world_t *world,
    ecs_entity_t e,
    bool visible)
{
    bool ours = ecs_has_id(world, e, UiWorldHidden);
    if (!visible && !ours && !ecs_has_id(world, e, Hidden)) {
        ecs_add_id(world, e, Hidden);
        ecs_add_id(world, e, UiWorldHidden);
    } else if (visible && ours) {
        ecs_remove_id(world, e, Hidden);
        ecs_remove_id(world, e, UiWorldHidden);
    }
}

void box_ui_world_pin(
    ecs_world_t *world,
    ecs_entity_t root,
    UiPlace *p)
{
    float x, y;
    bool visible = box_ui_world_project(world, &p->world, root, &x, &y);
    WorldAnchorImpl *impl = ecs_get_mut(world, p->e, WorldAnchorImpl);
    if (!impl) {
        ecs_set(world, p->e, WorldAnchorImpl, { x, y, visible });
    } else {
        impl->x = x;
        impl->y = y;
        impl->visible = visible;
    }

    p->x = x;
    p->y = y;
    box_ui_world_show(world, p->e, visible);
}

/** Mark the tree of every anchored element whose projection moved this frame.
 *
 * @param it Iterator over the anchored elements of the world
 */
static void BoxUiWorldAnchorMark(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const WorldAnchor *anchor = ecs_field(it, WorldAnchor, 0);
    const UiNode *node = ecs_field(it, UiNode, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        float x, y;
        bool visible = box_ui_world_project(world, &anchor[i], node[i].root, &x, &y);
        const WorldAnchorImpl *last = ecs_get(world, it->entities[i], WorldAnchorImpl);
        bool moved = !last || last->visible != visible ||
            (visible && (last->x != x || last->y != y));
        UiImpl *impl = moved ? ecs_get_mut(world, node[i].root, UiImpl) : NULL;
        if (impl) {
            impl->relayout = true;
            box_ui_state(world)->anchors_moved = true;
        }
    }
}

/** Lay out the trees the mark pass flagged, so a bar lands where its thing is.
 *
 * @param it Iterator over the registry singleton
 */
static void BoxUiWorldAnchorLayout(
    ecs_iter_t *it)
{
    UiState *state = box_ui_state(it->world);
    if (state->anchors_moved) {
        state->anchors_moved = false;
        box_ui_update(it->world);
    }
}

void box_ui_world_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxUiWorldAnchorMark" }),
        .phase = EcsPostUpdate,
        .query.terms = {
            { .id = ecs_id(WorldAnchor), .inout = EcsIn },
            { .id = ecs_id(UiNode), .inout = EcsIn }
        },
        .callback = BoxUiWorldAnchorMark,
        .immediate = true
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxUiWorldAnchorLayout" }),
        .phase = EcsPostUpdate,
        .query.terms = {{ .id = ecs_id(UiState) }},
        .callback = BoxUiWorldAnchorLayout,
        .immediate = true
    });
}
