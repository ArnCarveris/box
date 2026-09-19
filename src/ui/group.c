/**
 * @file ui/group.c
 * @brief Composites a transparent subtree as one image instead of per element.
 *
 * An element with an Opacity below one that holds children would otherwise hand
 * that opacity to every node below it, and two children that overlap would show
 * through each other. Such a subtree gets a view of its own that renders it at
 * full strength into a target the size of its Bounds, and a quad in the layer
 * above shows that target once with the opacity of the element. Everything else
 * is untouched: a subtree that is fully opaque, and an element with an opacity
 * but no children, keeps the one draw it always had.
 */

#include "ui_internal.h"
#include <math.h>

/** Pixels of the target one logical pixel of a tree covers.
 *
 * @param world The world
 * @param root The root of the tree
 * @return The factor, one when the tree does not know its view size
 */
static float box_ui_group_scale(
    ecs_world_t *world,
    ecs_entity_t root)
{
    float result = 1.0f;
    const UiImpl *impl = ecs_get(world, root, UiImpl);
    const Surface *surface = ecs_singleton_get(world, Surface);
    if (!impl || !surface || impl->height <= 0 || surface->height <= 0) {
        goto done;
    }

    result = (float)surface->height / impl->height;
done:
    return result;
}

/** Size in pixels one side of a target takes for a side of a box.
 *
 * @param size The side of the box in logical pixels
 * @param scale Pixels one logical pixel covers
 * @return The side in pixels, at least one and at most BOX_UI_GROUP_MAX
 */
static int32_t box_ui_group_side(
    float size,
    float scale)
{
    float pixels = ceilf(size * scale);
    int32_t result = (int32_t)pixels;

    result = result > 0 ? result : 1;

    return result < BOX_UI_GROUP_MAX ? result : BOX_UI_GROUP_MAX;
}

void box_ui_group_release(
    ecs_world_t *world,
    GroupImpl *group)
{
    ecs_entity_t owned[] = { group->quad, group->view, group->camera,
        group->target, group->layer };
    int32_t i;
    for (i = 0; i < 5; i ++) {
        if (owned[i] && ecs_is_alive(world, owned[i])) {
            ecs_delete(world, owned[i]);
        }
    }

    memset(group, 0, sizeof(GroupImpl));
}

/** Create the layer, the target, the camera, the view and the quad of a group.
 *
 * @param world The world
 * @param e The element whose subtree composites
 * @param priority Order of the view of the group in the frame
 * @param group Receives the entities the group owns
 */
static void box_ui_group_create(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t priority,
    GroupImpl *group)
{
    ecs_entity_t layer = ecs_new(world);
    ecs_entity_t target = ecs_new(world);
    ecs_entity_t camera = ecs_new(world);
    ecs_entity_t view = ecs_new(world);
    ecs_entity_t quad = ecs_new_w_pair(world, EcsChildOf, e);

    box_render_add_ui_layer(world, layer);
    ecs_set(world, target, RenderTarget, { .width = 1, .height = 1 });
    ecs_set(world, camera, Camera, {
        .near_ = BOX_UI_NEAR,
        .far_ = BOX_UI_FAR,
        .orthographic = true,
        .ortho_height = 1
    });

    ecs_set(world, view, View, {
        .camera = camera,
        .target = target,
        .layers = { layer },
        .clear_alpha = 0,
        .priority = priority,
        .strict_layers = true,
        .shadows = false,
        .effects = false,
        .material_layers = true,
        .enabled = true
    });

    ecs_add_id(world, quad, UiComposite);
    ecs_add_id(world, quad, UiQuad);
    ecs_set(world, quad, Position, { 0, 0, 0 });
    ecs_set(world, quad, Rgb, { 255, 255, 255 });
    ecs_set_ptr(world, quad, RenderGroup, &(RenderGroup){ .target = target });
    group->layer = layer;
    group->target = target;
    group->camera = camera;
    group->view = view;
    group->quad = quad;
}

/** Give the target, the camera and the quad of a group the box of its element.
 *
 * @param world The world
 * @param p The place of the element
 * @param opacity Opacity the subtree composites with
 * @param scale Pixels one logical pixel of the tree covers
 * @param group The group state of the element
 */
static void box_ui_group_sync(
    ecs_world_t *world,
    const UiPlace *p,
    float opacity,
    float scale,
    GroupImpl *group)
{
    int32_t width = box_ui_group_side(p->width, scale);
    int32_t height = box_ui_group_side(p->height, scale);
    const WorldTransform *wt = ecs_get(world, p->e, WorldTransform);
    float x = wt ? wt->m.v[12] : p->x;
    float y = wt ? wt->m.v[13] : p->y;

    if (group->width != width || group->height != height) {
        ecs_set(world, group->target, RenderTarget, {
            .width = width, .height = height });
        group->width = width;
        group->height = height;
    }

    Camera *camera = ecs_get_mut(world, group->camera, Camera);
    if (camera && camera->ortho_height != p->height) {
        camera->ortho_height = p->height;
        ecs_modified(world, group->camera, Camera);
    }

    ecs_set(world, group->camera, Position, { x, y, -BOX_UI_DEPTH });
    Rect *rect = ecs_get_mut(world, group->quad, Rect);
    if (!rect) {
        ecs_set(world, group->quad, Rect, { p->width, p->height });
    } else if (rect->width != p->width || rect->height != p->height) {
        rect->width = p->width;
        rect->height = p->height;
        ecs_modified(world, group->quad, Rect);
    }

    Opacity *alpha = ecs_get_mut(world, group->quad, Opacity);
    if (!alpha) {
        ecs_set(world, group->quad, Opacity, { opacity });
    } else if (alpha->value != opacity) {
        alpha->value = opacity;
        ecs_modified(world, group->quad, Opacity);
    }
}

/** Give one element of the collected tree the group it needs, or take it away.
 *
 * @param world The world
 * @param index Place of the element in the collected tree
 * @param scale Pixels one logical pixel of the tree covers
 * @return True when the element composites its subtree
 */
static bool box_ui_group_element(
    ecs_world_t *world,
    int32_t index,
    float scale)
{
    UiPlace p = ecs_vec_get_t(&box_ui_state(world)->places, UiPlace, index)[0];
    const Opacity *opacity = ecs_get(world, p.e, Opacity);
    GroupImpl *group = ecs_get_mut(world, p.e, GroupImpl);
    bool result = opacity && opacity->value < 1.0f && p.kids + p.heirs > 0 &&
        p.width > 0 && p.height > 0;
    if (!result) {
        if (group) {
            box_ui_group_release(world, group);
            ecs_remove(world, p.e, GroupImpl);
            box_ui_state(world)->respawn = true;
        }

        goto done;
    }

    if (!group) {
        GroupImpl created;

        memset(&created, 0, sizeof(GroupImpl));
        box_ui_group_create(world, p.e, BOX_UI_PRIORITY, &created);
        ecs_set_ptr(world, p.e, GroupImpl, &created);
        box_ui_state(world)->respawn = true;
    }

    box_ui_group_sync(world, &p, opacity->value, scale,
        ecs_get_mut(world, p.e, GroupImpl));
done:
    return result;
}

/** Order the view of every group so an inner subtree renders before an outer one.
 *
 * @param world The world
 * @param base Order of the view of the tree in the frame
 */
static void box_ui_group_order(
    ecs_world_t *world,
    int32_t base)
{
    int32_t i, count = ecs_vec_count(&box_ui_state(world)->places);
    for (i = 0; i < count; i ++) {
        UiState *state = box_ui_state(world);
        UiPlace *place = ecs_vec_get_t(&state->places, UiPlace, i);
        UiPlace *parent = place->parent >= 0 ?
            ecs_vec_get_t(&state->places, UiPlace, place->parent) : NULL;
        if (place->aux) {
            continue;
        }

        const GroupImpl *group = ecs_get(world, place->e, GroupImpl);

        place->depth = parent ? parent->depth + (group ? 1 : 0) : 0;
        if (!group) {
            continue;
        }

        int32_t priority = base - place->depth - 1;
        View *view = ecs_get_mut(world, group->view, View);
        if (view && view->priority != priority) {
            view->priority = priority;
            ecs_modified(world, group->view, View);
        }
    }
}

void box_ui_group_build(
    ecs_world_t *world,
    ecs_entity_t root)
{
    UiState *state = box_ui_state(world);
    if (!state->opacities) {
        goto done;
    }

    const Ui *ui = ecs_get(world, root, Ui);
    int32_t base = ui ? ui->priority : BOX_UI_PRIORITY;
    float scale = box_ui_group_scale(world, root);
    int32_t i, count = ecs_vec_count(&state->places);
    for (i = count - 1; i >= 0; i --) {
        UiPlace *place = ecs_vec_get_t(&box_ui_state(world)->places, UiPlace, i);
        int32_t parent = place->parent;
        if (place->aux) {
            continue;
        }

        if (box_ui_group_element(world, i, scale) && parent >= 0) {
            ecs_vec_get_t(&box_ui_state(world)->places, UiPlace, parent)->heirs ++;
        }
    }

    box_ui_group_order(world, base);
done:
    return;
}

/** Opacity observer: count them so a world without one skips the group pass.
 *
 * @param it Iterator over the entities that gained or lost an opacity
 */
static void box_ui_on_opacity(
    ecs_iter_t *it)
{
    UiState *state = box_ui_state(it->world);

    state->opacities += it->event == EcsOnAdd ? it->count : -it->count;
}

/** GroupImpl observer: count the groups and release what a lost one owned.
 *
 * @param it Iterator over the elements that gained or lost a group
 */
static void box_ui_on_group(
    ecs_iter_t *it)
{
    GroupImpl *group = ecs_field(it, GroupImpl, 0);
    UiState *state = box_ui_state(it->world);
    if (it->event == EcsOnAdd) {
        state->groups += it->count;
        goto done;
    }

    state->groups -= it->count;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_ui_group_release(it->stage, &group[i]);
    }

done:
    return;
}

void box_ui_group_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Opacity), .inout = EcsInOutNone }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_ui_on_opacity
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(GroupImpl), .inout = EcsInOut }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_ui_on_group
    });
}
