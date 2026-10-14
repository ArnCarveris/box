/**
 * @file input/platform_web.c
 * @brief The browser backend: canvas and window events push device events to the world.
 *
 * Keys, buttons, the wheel and focus come from the canvas, cursor moves and button releases
 * from the page so a drag continues outside it. The cursor is reported in surface points.
 */

#include "input.h"
#include "platform.h"

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

/** Selector of the canvas element of the shell page */
#define BOX_WEB_CANVAS "#canvas"

/** State of the browser backend of a world */
typedef struct box_web_t {
    ecs_world_t *world;     /**< World the events are pushed to */
    ecs_entity_t status;    /**< SurfaceStatus component, 0 until the world has one */
    vec2 size;              /**< Point size of the first surface, the canvas */
} box_web_t;

/** Check whether the page has the canvas, which a run under node does not.
 *
 * @return Whether the canvas exists
 */
EM_JS(bool, box_web_has_canvas, (void), {
    return typeof document !== 'undefined' && document.querySelector('#canvas') !== null;
});

/** Convert a page position to 0..1 of the image of the canvas, which the page scales to fit
 * its element while keeping the aspect ratio.
 *
 * @param x Client x of the event
 * @param y Client y of the event
 * @param out Receives the normalized x and y
 */
EM_JS(void, box_web_canvas_point, (double x, double y, float *out), {
    var c = document.querySelector('#canvas');
    var r = c.getBoundingClientRect();
    var s = Math.min(r.width / Math.max(c.width, 1), r.height / Math.max(c.height, 1));
    var w = Math.max(c.width, 1) * s;
    var h = Math.max(c.height, 1) * s;
    HEAPF32[out >> 2] = w > 0 ? (x - r.left - (r.width - w) / 2) / w : 0;
    HEAPF32[(out >> 2) + 1] = h > 0 ? (y - r.top - (r.height - h) / 2) / h : 0;
});

/** Push a key event and its text.
 *
 * @param type Keydown or keyup
 * @param event The keyboard event
 * @param ctx The backend state
 * @return Whether the page must not act on the event
 */
static bool box_web_key(
    int type,
    const EmscriptenKeyboardEvent *event,
    void *ctx)
{
    box_web_t *web = ctx;

    return box_input_dom_on_key(web->world, event->code, event->key,
        type == EMSCRIPTEN_EVENT_KEYDOWN, event->repeat, event->ctrlKey || event->metaKey);
}

/** Push a cursor move in surface points.
 *
 * @param web The backend state
 * @param event The mouse event
 */
static void box_web_push_move(
    box_web_t *web,
    const EmscriptenMouseEvent *event)
{
    float point[2] = { 0, 0 };
    box_web_canvas_point((double)event->clientX, (double)event->clientY, point);
    box_input_platform_push(web->world, &(InputEvent){
        .kind = InputMove,
        .value = { point[0] * web->size[0], point[1] * web->size[1] }
    });
}

/** Push a cursor move.
 *
 * @param type The event type
 * @param event The mouse event
 * @param ctx The backend state
 * @return false, the page keeps acting on the event
 */
static bool box_web_move(
    int type,
    const EmscriptenMouseEvent *event,
    void *ctx)
{
    (void)type;
    box_web_push_move(ctx, event);

    return false;
}

/** Push a mouse button event at the cursor position.
 *
 * @param type Mousedown or mouseup
 * @param event The mouse event
 * @param ctx The backend state
 * @return false, so the canvas still takes the keyboard focus
 */
static bool box_web_button(
    int type,
    const EmscriptenMouseEvent *event,
    void *ctx)
{
    box_web_t *web = ctx;
    Key key = box_input_dom_button(event->button);
    if (key) {
        box_web_push_move(web, event);
        box_input_platform_push(web->world, &(InputEvent){
            .kind = type == EMSCRIPTEN_EVENT_MOUSEDOWN ? InputKeyDown : InputKeyUp,
            .key = key
        });
    }

    return false;
}

/** Push a wheel movement.
 *
 * @param type The event type
 * @param event The wheel event
 * @param ctx The backend state
 * @return true, the page does not scroll under the canvas
 */
static bool box_web_wheel(
    int type,
    const EmscriptenWheelEvent *event,
    void *ctx)
{
    box_web_t *web = ctx;
    (void)type;
    box_input_dom_on_wheel(web->world, event->deltaX, event->deltaY, (int32_t)event->deltaMode);

    return true;
}

/** Push a focus change of the canvas.
 *
 * @param type Focus or blur
 * @param event The focus event
 * @param ctx The backend state
 * @return false, the page keeps acting on the event
 */
static bool box_web_focus(
    int type,
    const EmscriptenFocusEvent *event,
    void *ctx)
{
    box_web_t *web = ctx;
    (void)event;
    box_input_platform_push(web->world, &(InputEvent){
        .kind = InputFocus, .code = type == EMSCRIPTEN_EVENT_FOCUS });

    return false;
}

/** Register or, with NULL state, remove the callbacks of the backend.
 *
 * @param web The backend state, NULL to remove the callbacks
 */
static void box_web_callbacks(
    box_web_t *web)
{
    const char *page = EMSCRIPTEN_EVENT_TARGET_WINDOW;
    emscripten_set_keydown_callback(BOX_WEB_CANVAS, web, false, web ? box_web_key : NULL);
    emscripten_set_keyup_callback(BOX_WEB_CANVAS, web, false, web ? box_web_key : NULL);
    emscripten_set_mousedown_callback(BOX_WEB_CANVAS, web, false, web ? box_web_button : NULL);
    emscripten_set_mouseup_callback(page, web, false, web ? box_web_button : NULL);
    emscripten_set_mousemove_callback(page, web, false, web ? box_web_move : NULL);
    emscripten_set_wheel_callback(BOX_WEB_CANVAS, web, false, web ? box_web_wheel : NULL);
    emscripten_set_focus_callback(BOX_WEB_CANVAS, web, false, web ? box_web_focus : NULL);
    emscripten_set_blur_callback(BOX_WEB_CANVAS, web, false, web ? box_web_focus : NULL);
}

/** Push the point size of the first surface when it changed, the poll of the backend.
 *
 * @param world The world
 * @param ctx The backend state
 */
static void box_web_poll(
    ecs_world_t *world,
    void *ctx)
{
    box_web_t *web = ctx;
    ecs_iter_t it;
    if (!web->status) {
        web->status = ecs_lookup(world, "box.surface.SurfaceStatus");
    }

    if (!web->status) {
        goto done;
    }

    it = ecs_each_id(world, web->status);
    if (ecs_each_next(&it)) {
        const SurfaceStatus *s = ecs_field_w_size(&it, sizeof(SurfaceStatus), 0);
        float w = (float)s->point_width, h = (float)s->point_height;
        if (w != web->size[0] || h != web->size[1]) {
            web->size[0] = w;
            web->size[1] = h;
            box_input_platform_push(world, &(InputEvent){
                .kind = InputResize, .value = { w, h } });
        }

        ecs_iter_fini(&it);
    }

done:
    return;
}

/** Remove the callbacks and release the backend state.
 *
 * @param ctx The backend state
 */
static void box_web_free(
    void *ctx)
{
    box_web_callbacks(NULL);
    ecs_os_free(ctx);
}

void box_input_web_attach(
    ecs_world_t *world)
{
    box_web_t *web;
    if (!ecs_lookup(world, "box.input") || !box_web_has_canvas()) {
        goto done;
    }

    if (box_input_device(world)->backend.poll) {
        goto done;
    }

    web = ecs_os_calloc_t(box_web_t);
    web->world = world;
    box_web_callbacks(web);
    box_input_platform_set(world, &(box_input_platform_t){
        .poll = box_web_poll, .free = box_web_free, .ctx = web });

done:
    return;
}

#else

void box_input_web_attach(
    ecs_world_t *world)
{
    (void)world;
}

#endif
