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
#include <string.h>

/** Selector of the canvas element of the shell page */
#define BOX_WEB_CANVAS "#canvas"

/** State of the browser backend of a world */
typedef struct box_web_t {
    ecs_world_t *world;     /**< World the events are pushed to */
    ecs_entity_t surface;   /**< Surface that presents to the canvas, stamped on the events */
    ecs_entity_t status;    /**< SurfaceStatus component, 0 until the world has one */
    ecs_entity_t component; /**< Surface component, 0 until the world has one */
    vec2 size;              /**< Point size of the surface that presents to the canvas */
    box_input_dom_pointer_t pointer; /**< Cursor and pointer lock of the canvas */
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

/** Start pushing the events of the canvas: its surface is stamped on them.
 *
 * @param web The backend state
 */
static void box_web_begin(
    const box_web_t *web)
{
    box_input_platform_source(web->world, web->surface);
}

/** Stop stamping the surface of the canvas on pushed events.
 *
 * @param web The backend state
 */
static void box_web_end(
    const box_web_t *web)
{
    box_input_platform_source(web->world, 0);
}

/** Get the size in CSS pixels of the image of the canvas, which the page scales to fit its
 * element while keeping the aspect ratio.
 *
 * @param out Receives the width and height
 */
EM_JS(void, box_web_canvas_image, (float *out), {
    var c = document.querySelector('#canvas');
    var r = c.getBoundingClientRect();
    var s = Math.min(r.width / Math.max(c.width, 1), r.height / Math.max(c.height, 1));
    HEAPF32[out >> 2] = Math.max(c.width, 1) * s;
    HEAPF32[(out >> 2) + 1] = Math.max(c.height, 1) * s;
});

/** Check whether the page has the keyboard focus, which a pointer lock ended by a blur lacks.
 *
 * @return Whether the document has focus
 */
EM_JS(bool, box_web_has_focus, (void), {
    return document.hasFocus();
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
    bool result;
    box_web_begin(web);
    result = box_input_dom_on_key(web->world, event->code, event->key,
        type == EMSCRIPTEN_EVENT_KEYDOWN, event->repeat, event->ctrlKey || event->metaKey);
    box_web_end(web);

    return result;
}

/** Push a cursor move in surface points: where the pointer is, or while the canvas holds the
 * pointer lock, the last position moved by the movement of the event, unbounded by the canvas.
 *
 * @param web The backend state
 * @param event The mouse event
 */
static void box_web_push_move(
    box_web_t *web,
    const EmscriptenMouseEvent *event)
{
    float point[2] = { 0, 0 }, image[2] = { 0, 0 };
    vec2 position, movement;
    box_web_canvas_point((double)event->clientX, (double)event->clientY, point);
    box_web_canvas_image(image);
    position[0] = point[0] * web->size[0];
    position[1] = point[1] * web->size[1];
    movement[0] = image[0] > 0 ? (float)event->movementX * web->size[0] / image[0] : 0;
    movement[1] = image[1] > 0 ? (float)event->movementY * web->size[1] / image[1] : 0;
    box_web_begin(web);
    box_input_dom_on_move(web->world, &web->pointer, position, movement);
    box_web_end(web);
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
            .key = key,
            .surface = web->surface
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
    box_web_begin(web);
    box_input_dom_on_wheel(web->world, event->deltaX, event->deltaY, (int32_t)event->deltaMode);
    box_web_end(web);

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
        .kind = InputFocus, .code = type == EMSCRIPTEN_EVENT_FOCUS, .surface = web->surface });

    return false;
}

/** Track the pointer lock of the canvas.
 *
 * @param type The event type
 * @param event The pointer lock change
 * @param ctx The backend state
 * @return false, the page keeps acting on the event
 */
static bool box_web_lock(
    int type,
    const EmscriptenPointerlockChangeEvent *event,
    void *ctx)
{
    box_web_t *web = ctx;
    bool locked = event->isActive && !strcmp(event->id, "canvas");
    bool exit;
    (void)type;
    box_web_begin(web);
    exit = box_input_dom_on_lock(web->world, &web->pointer, locked, box_web_has_focus());
    box_web_end(web);
    if (exit) {
        emscripten_exit_pointerlock();
    }

    return false;
}

/** Report a pointer lock the browser refused.
 *
 * @param type The event type
 * @param reserved Unused
 * @param ctx The backend state
 * @return false, the page keeps acting on the event
 */
static bool box_web_lock_error(
    int type,
    const void *reserved,
    void *ctx)
{
    box_web_t *web = ctx;
    (void)type;
    (void)reserved;
    box_web_begin(web);
    box_input_dom_on_lock_error(web->world, &web->pointer, box_web_has_focus());
    box_web_end(web);

    return false;
}

/** Capture the cursor with the Pointer Lock API, the capture callback of the backend.
 *
 * Browsers only grant a lock during a user gesture, so the request waits for the next input
 * event handler of the page; a release exits the lock and cancels a request still waiting.
 *
 * @param world The world
 * @param surface The surface, the canvas has one
 * @param captured Whether the cursor is captured
 * @param ctx The backend state
 * @return Whether the canvas holds the lock already
 */
static bool box_web_capture(
    ecs_world_t *world,
    ecs_entity_t surface,
    bool captured,
    void *ctx)
{
    box_web_t *web = ctx;
    (void)world;
    (void)surface;
    web->pointer.want = captured;
    if (captured && !web->pointer.locked) {
        emscripten_request_pointerlock(BOX_WEB_CANVAS, true);
    } else if (!captured) {
        emscripten_exit_pointerlock();
    }

    return captured && web->pointer.locked;
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
    emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, web, false,
        web ? box_web_lock : NULL);
    emscripten_set_pointerlockerror_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, web, false,
        web ? box_web_lock_error : NULL);
}

/** Follow the canvas to another surface once its surface is gone: the first surface without a
 * texture, which the renderer gives the canvas next.
 *
 * @param world The world
 * @param web The backend state
 */
static void box_web_follow(
    ecs_world_t *world,
    box_web_t *web)
{
    ecs_entity_t next = 0;
    ecs_iter_t it;
    int32_t i;
    if (!web->component) {
        web->component = ecs_lookup(world, "box.surface.Surface");
    }

    if (!web->component || (web->surface && ecs_is_alive(world, web->surface))) {
        goto done;
    }

    it = ecs_each_id(world, web->component);
    while (ecs_each_next(&it)) {
        const Surface *s = ecs_field_w_size(&it, sizeof(Surface), 0);
        for (i = 0; i < it.count; i ++) {
            if (!s[i].texture && (!next || it.entities[i] < next)) {
                next = it.entities[i];
            }
        }
    }

    web->surface = next;
done:
    return;
}

/** Push the point size of the surface of the canvas when it changed, the poll of the backend.
 *
 * @param world The world
 * @param ctx The backend state
 */
static void box_web_poll(
    ecs_world_t *world,
    void *ctx)
{
    box_web_t *web = ctx;
    const SurfaceStatus *s;
    float w, h;
    if (!web->status) {
        web->status = ecs_lookup(world, "box.surface.SurfaceStatus");
    }

    box_web_follow(world, web);
    s = web->status && web->surface && ecs_is_alive(world, web->surface) ?
        ecs_get_id(world, web->surface, web->status) : NULL;
    if (!s) {
        goto done;
    }

    w = (float)s->point_width;
    h = (float)s->point_height;
    if (w != web->size[0] || h != web->size[1]) {
        web->size[0] = w;
        web->size[1] = h;
        box_input_platform_push(world, &(InputEvent){
            .kind = InputResize, .value = { w, h }, .surface = web->surface });
    }

done:
    return;
}

/** Remove the callbacks, exit or cancel the pointer lock and release the backend state.
 *
 * @param ctx The backend state
 */
static void box_web_free(
    void *ctx)
{
    box_web_t *web = ctx;
    box_web_callbacks(NULL);
    emscripten_exit_pointerlock();

    ecs_os_free(ctx);
}

void box_input_web_attach(
    ecs_world_t *world,
    ecs_entity_t surface)
{
    box_web_t *web;
    if (!ecs_lookup(world, "box.input") || !box_web_has_canvas()) {
        goto done;
    }

    if (box_input_device(world)->backend.poll == box_web_poll) {
        web = box_input_device(world)->backend.ctx;
        if (!web->surface || !ecs_is_alive(world, web->surface)) {
            web->surface = surface;
        }

        goto done;
    }

    if (box_input_device(world)->backend.poll) {
        goto done;
    }

    web = ecs_os_calloc_t(box_web_t);
    web->world = world;
    web->surface = surface;
    box_web_callbacks(web);
    box_input_platform_set(world, &(box_input_platform_t){
        .poll = box_web_poll, .free = box_web_free, .capture = box_web_capture, .ctx = web });

done:
    return;
}

#else

void box_input_web_attach(
    ecs_world_t *world,
    ecs_entity_t surface)
{
    (void)world;
    (void)surface;
}

#endif
