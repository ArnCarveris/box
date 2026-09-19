/**
 * @file ui/view.c
 * @brief Gives a UI tree the view, the camera and the layer that draw it.
 *
 * A created view draws the layer of its tree and nothing else, with a camera
 * that looks along the positive z axis at the plane of the tree, so a node at
 * the origin sits in the middle of the view and one unit is one logical pixel.
 */

#include "ui_internal.h"

/** Layer of a view that already draws a UI tree.
 *
 * @param world The world
 * @param view The view entity
 * @return The layer, 0 when the view draws no tree yet
 */
static ecs_entity_t box_ui_view_layer(
    ecs_world_t *world,
    ecs_entity_t view)
{
    ecs_entity_t result = 0;
    const View *v = ecs_get(world, view, View);
    if (!v) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < BOX_VIEW_LAYER_MAX && v->layers[i] && !result; i ++) {
        if (box_render_is_ui_layer(world, v->layers[i])) {
            result = v->layers[i];
        }
    }

done:
    return result;
}

/** Add the layer of a tree to the layer list of the view it was bound to.
 *
 * @param world The world
 * @param view The view entity
 * @param layer The layer of the tree
 */
static void box_ui_view_add_layer(
    ecs_world_t *world,
    ecs_entity_t view,
    ecs_entity_t layer)
{
    View *v = ecs_get_mut(world, view, View);
    if (!v) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < BOX_VIEW_LAYER_MAX && v->layers[i]; i ++) { }

    if (i == BOX_VIEW_LAYER_MAX) {
        ecs_warn("box.ui: view %s already lists %d layers, the tree is not shown",
            ecs_get_name(world, view), BOX_VIEW_LAYER_MAX);
        goto done;
    }

    v->layers[i] = layer;
    v->strict_layers = true;
    ecs_modified(world, view, View);
done:
    return;
}

/** Create the camera of a tree, an orthographic camera in logical pixels.
 *
 * @param world The world
 * @param height Logical pixels the camera shows vertically, 0 until it is known
 * @return The camera entity
 */
static ecs_entity_t box_ui_create_camera(
    ecs_world_t *world,
    float height)
{
    ecs_entity_t camera = ecs_new(world);

    ecs_set(world, camera, Position, { 0, 0, -BOX_UI_DEPTH });
    ecs_set(world, camera, Camera, {
        .near_ = BOX_UI_NEAR,
        .far_ = BOX_UI_FAR,
        .orthographic = true,
        .ortho_height = height
    });

    return camera;
}

/** Create the view of a tree, which draws the layer of that tree only.
 *
 * @param world The world
 * @param camera The camera of the tree
 * @param layer The layer of the tree
 * @param priority Order of the view in the frame
 * @return The view entity
 */
static ecs_entity_t box_ui_create_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    ecs_entity_t layer,
    int32_t priority)
{
    ecs_entity_t view = ecs_new(world);

    ecs_set(world, view, View, {
        .camera = camera,
        .layers = { layer },
        .clear_alpha = 1.0f,
        .priority = priority,
        .strict_layers = true,
        .shadows = false,
        .effects = false,
        .material_layers = true,
        .enabled = true
    });

    return view;
}

void box_ui_bind(
    ecs_world_t *world,
    Ui *ui,
    UiImpl *impl)
{
    if (impl->view && ecs_is_alive(world, impl->view)) {
        goto done;
    }

    if (ui->view && !ecs_is_alive(world, ui->view)) {
        goto done;
    }

    impl->view = ui->view;
    impl->owned = ui->view == 0;
    if (impl->view) {
        impl->layer = box_ui_view_layer(world, impl->view);
        const View *v = ecs_get(world, impl->view, View);
        impl->camera = v ? v->camera : 0;
    }

    if (!impl->layer) {
        impl->layer = ecs_new(world);
        box_render_add_ui_layer(world, impl->layer);
    }

    if (impl->owned) {
        impl->camera = box_ui_create_camera(world, ui->height);
        impl->view = box_ui_create_view(world, impl->camera, impl->layer,
            ui->priority);
        ui->view = impl->view;
    } else {
        box_ui_view_add_layer(world, impl->view, impl->layer);
    }

    box_hierarchy_set_parent(world, impl->layer, impl->view);
    impl->dirty = true;
done:
    return;
}

void box_ui_unbind(
    ecs_world_t *world,
    UiImpl *impl)
{
    if (impl->owned && ecs_is_alive(world, impl->view)) {
        ecs_delete(world, impl->view);
    }

    if (impl->owned && ecs_is_alive(world, impl->camera)) {
        ecs_delete(world, impl->camera);
    }

    impl->view = 0;
    impl->camera = 0;
    impl->layer = 0;
    impl->owned = false;
    impl->height = 0;
}

void box_ui_sync(
    ecs_world_t *world,
    const Ui *ui,
    UiImpl *impl)
{
    if (!impl->owned || !ecs_is_alive(world, impl->camera)) {
        goto done;
    }

    float height = ui->height;
    if (height <= 0) {
        const Surface *surface = ecs_singleton_get(world, Surface);
        float points = surface ?
            (float)(surface->point_height ? surface->point_height : surface->height) : 0;
        float scale = ui->scale > 0 ? ui->scale : BOX_UI_SCALE;

        height = points / scale;
    }

    if (height <= 0 || height == impl->height) {
        goto done;
    }

    Camera *camera = ecs_get_mut(world, impl->camera, Camera);
    if (!camera) {
        goto done;
    }

    camera->ortho_height = height;
    impl->height = height;
done:
    return;
}
