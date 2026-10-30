/**
 * @file ViewOrder.c
 * @brief Tests for View.order: the views of the packet are in ascending order, views of the same
 * order in entity id order, and a changed order applies in the next frame.
 *
 * The cases render into the null backend with one camera that every view renders.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Create a world that renders into the null backend, with a camera at the origin.
 *
 * @param null Receives the backend
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* order_world(
    box_render_null_t **null,
    ecs_entity_t *camera)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    *camera = ecs_new(world);
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f});

    return world;
}

/** Create a view of a camera with an order.
 *
 * @param world The world
 * @param camera The camera
 * @param order The order
 * @return The view
 */
static ecs_entity_t order_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    int32_t order)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, View, {.camera = camera, .order = order});

    return result;
}

/** Run a frame into the null backend.
 *
 * @param world The world
 * @param null The backend
 * @return The packet the backend received
 */
static const box_render_packet_t* order_frame(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_progress(world, 0);
    box_render_sync(world);

    return &null->last;
}

/** Get the view entity at an index of the views of a packet.
 *
 * @param p The packet
 * @param index The index
 * @return The view entity
 */
static ecs_entity_t order_at(
    const box_render_packet_t *p,
    int32_t index)
{
    return ecs_vec_get_t(&p->views, box_render_view_t, index)->view;
}

void ViewOrder_default_is_zero(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, View);
    test_int(ecs_get(world, e, View)->order, 0);
    test_assert(BOX_VIEW_ORDER_EDITOR > 0);
    test_assert(BOX_VIEW_ORDER_CONSOLE > BOX_VIEW_ORDER_EDITOR);
    test_assert(BOX_VIEW_ORDER_FPS > BOX_VIEW_ORDER_EDITOR);
    test_assert(BOX_VIEW_ORDER_CONSOLE > BOX_VIEW_ORDER_FPS);
    ecs_fini(world);
}

void ViewOrder_render_ascending(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera;
    ecs_world_t *world = order_world(&null, &camera);
    ecs_entity_t top = order_view(world, camera, 2);
    ecs_entity_t bottom = order_view(world, camera, -1);
    ecs_entity_t middle = order_view(world, camera, 1);
    const box_render_packet_t *p = order_frame(world, null);
    test_int(ecs_vec_count(&p->views), 3);
    test_uint(order_at(p, 0), bottom);
    test_uint(order_at(p, 1), middle);
    test_uint(order_at(p, 2), top);
    test_int(ecs_vec_get_t(&p->views, box_render_view_t, 2)->order, 2);
    ecs_fini(world);
    box_render_null_free(null);
}

void ViewOrder_ties_keep_id_order(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera;
    ecs_world_t *world = order_world(&null, &camera);
    ecs_entity_t a = order_view(world, camera, 0);
    ecs_entity_t b = order_view(world, camera, 0);
    ecs_entity_t c = order_view(world, camera, 1);
    ecs_entity_t d = order_view(world, camera, 0);
    ecs_entity_t e = order_view(world, camera, 1);
    test_assert(a < b && b < c && c < d && d < e);
    const box_render_packet_t *p = order_frame(world, null);
    test_int(ecs_vec_count(&p->views), 5);
    test_uint(order_at(p, 0), a);
    test_uint(order_at(p, 1), b);
    test_uint(order_at(p, 2), d);
    test_uint(order_at(p, 3), c);
    test_uint(order_at(p, 4), e);
    ecs_fini(world);
    box_render_null_free(null);
}

void ViewOrder_render_runtime_change(void) {
    // renderer/views
    box_render_null_t *null;
    ecs_entity_t camera;
    ecs_world_t *world = order_world(&null, &camera);
    ecs_entity_t a = order_view(world, camera, 0);
    ecs_entity_t b = order_view(world, camera, 0);
    const box_render_packet_t *p = order_frame(world, null);
    test_uint(order_at(p, 0), a);
    test_uint(order_at(p, 1), b);

    ecs_get_mut(world, a, View)->order = 5;
    ecs_modified(world, a, View);
    p = order_frame(world, null);
    test_uint(order_at(p, 0), b);
    test_uint(order_at(p, 1), a);

    ecs_get_mut(world, a, View)->order = 0;
    ecs_modified(world, a, View);
    p = order_frame(world, null);
    test_uint(order_at(p, 0), a);
    test_uint(order_at(p, 1), b);
    ecs_fini(world);
    box_render_null_free(null);
}

void ViewOrder_compare(void) {
    // renderer/views
    test_assert(box_view_compare(0, 10, 0, 20) < 0);
    test_assert(box_view_compare(0, 20, 0, 10) > 0);
    test_assert(box_view_compare(1, 10, 0, 20) > 0);
    test_assert(box_view_compare(-1, 20, 0, 10) < 0);
    test_assert(box_view_compare(BOX_VIEW_ORDER_EDITOR, 30, BOX_VIEW_ORDER_CONSOLE, 10) < 0);
    test_assert(box_view_compare(BOX_VIEW_ORDER_EDITOR, 30, BOX_VIEW_ORDER_FPS, 10) < 0);
    test_assert(box_view_compare(BOX_VIEW_ORDER_FPS, 30, BOX_VIEW_ORDER_CONSOLE, 10) < 0);
    test_int(box_view_compare(3, 10, 3, 10), 0);
}
