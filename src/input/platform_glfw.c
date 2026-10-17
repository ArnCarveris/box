/**
 * @file input/platform_glfw.c
 * @brief The glfw backend: window callbacks push device events, stamped with the surface of the
 * window, to the world owning the window.
 *
 * glfw runs the callbacks inside glfwPollEvents on the main thread, which box.surface calls once
 * per frame in SurfacePoll (PreFrame), before InputUpdate (OnLoad) applies them.
 *
 * The backend also holds the clipboard, read when the paste shortcut goes down, and shows the
 * pointer cursor shapes with the standard cursors of glfw.
 */

#include "input.h"
#include "platform.h"

#ifdef BOX_PLATFORM_GLFW

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

/** A window whose events go to a world */
typedef struct box_glfw_window_t {
    GLFWwindow *window;               /**< The glfw window */
    box_input_glfw_target_t *target;  /**< World and surface of the window */
} box_glfw_window_t;

/** Shapes of PointerCursor */
#define BOX_GLFW_CURSORS (CursorNotAllowed + 1)

/** State of the glfw backend of a world */
typedef struct box_glfw_backend_t {
    ecs_world_t *world;                    /**< The world */
    GLFWcursor *cursors[BOX_GLFW_CURSORS]; /**< Standard cursor of each shape, created once */
    bool tried[BOX_GLFW_CURSORS];          /**< The cursor of the shape was created, or failed */
    int32_t session;                       /**< box_glfw_session the cursors were created in */
    PointerCursor shape;                   /**< Shape shown in the windows of the world */
} box_glfw_backend_t;

/** box_glfw_window_t of the attached windows; glfw is process wide and runs on the main thread */
static ecs_vec_t box_glfw_windows;

/** Counts the times every window was detached, after which box.surface terminates glfw and
 * with it every cursor */
static int32_t box_glfw_session;

/** glfw standard cursor of each shape, 0 for the default arrow */
static const int box_glfw_shapes[BOX_GLFW_CURSORS] = {
    [CursorDefault] = 0,
    [CursorPointer] = GLFW_POINTING_HAND_CURSOR,
    [CursorText] = GLFW_IBEAM_CURSOR,
    [CursorCrosshair] = GLFW_CROSSHAIR_CURSOR,
    [CursorResizeH] = GLFW_RESIZE_EW_CURSOR,
    [CursorResizeV] = GLFW_RESIZE_NS_CURSOR,
    [CursorMove] = GLFW_RESIZE_ALL_CURSOR,
    [CursorNotAllowed] = GLFW_NOT_ALLOWED_CURSOR
};

/** Capture or release the cursor in the windows of a surface: glfw's disabled cursor, which
 * hides it, keeps it in the window and reports unbounded positions, with raw motion when the
 * platform supports it, the backend capture callback.
 *
 * @param world The world
 * @param surface The surface, 0 for every window of the world
 * @param captured Whether the cursor is captured
 * @param ctx Unused
 * @return Whether the cursor is captured, glfw changes the mode at once
 */
static bool box_glfw_capture(
    ecs_world_t *world,
    ecs_entity_t surface,
    bool captured,
    void *ctx)
{
    box_glfw_window_t *windows = ecs_vec_first_t(&box_glfw_windows, box_glfw_window_t);
    bool result = false;
    int32_t i;
    (void)ctx;
    for (i = 0; i < ecs_vec_count(&box_glfw_windows); i ++) {
        const box_input_glfw_target_t *t = windows[i].target;
        if (t->world != world || (surface && t->surface != surface)) {
            continue;
        }

        glfwSetInputMode(windows[i].window, GLFW_CURSOR,
            captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(windows[i].window, GLFW_RAW_MOUSE_MOTION,
                captured ? GLFW_TRUE : GLFW_FALSE);
        }

        result = captured;
    }

    return result;
}

/** Forget the cursors of a backend that glfw terminated since they were created.
 *
 * @param backend The backend state
 */
static void box_glfw_cursors_check(
    box_glfw_backend_t *backend)
{
    if (backend->session != box_glfw_session) {
        ecs_os_zeromem(&backend->cursors);
        ecs_os_zeromem(&backend->tried);
        backend->session = box_glfw_session;
    }
}

/** Get the standard cursor of a shape, creating it the first time.
 *
 * @param backend The backend state
 * @param shape The shape
 * @return The cursor, NULL for the default arrow or a shape the platform lacks
 */
static GLFWcursor* box_glfw_cursor_of(
    box_glfw_backend_t *backend,
    PointerCursor shape)
{
    box_glfw_cursors_check(backend);
    if (shape > CursorDefault && shape < BOX_GLFW_CURSORS && !backend->tried[shape]) {
        backend->cursors[shape] = glfwCreateStandardCursor(box_glfw_shapes[shape]);
        backend->tried[shape] = true;
    }

    return shape > CursorDefault && shape < BOX_GLFW_CURSORS ? backend->cursors[shape] : NULL;
}

/** Show a cursor shape in the windows of a world, the cursor callback of the backend.
 *
 * A disabled (captured) cursor stays hidden; glfw shows the shape once it is normal again.
 *
 * @param world The world
 * @param shape The shape
 * @param ctx The backend state
 */
static void box_glfw_cursor_shape(
    ecs_world_t *world,
    PointerCursor shape,
    void *ctx)
{
    box_glfw_backend_t *backend = ctx;
    box_glfw_window_t *windows = ecs_vec_first_t(&box_glfw_windows, box_glfw_window_t);
    GLFWcursor *cursor = box_glfw_cursor_of(backend, shape);
    int32_t i;
    backend->shape = shape;
    for (i = 0; i < ecs_vec_count(&box_glfw_windows); i ++) {
        if (windows[i].target->world == world) {
            glfwSetCursor(windows[i].window, cursor);
        }
    }
}

/** Get a window of a world, which the clipboard needs glfw to be initialized for.
 *
 * @param world The world
 * @return The window, NULL when the world has none
 */
static GLFWwindow* box_glfw_window_of(
    const ecs_world_t *world)
{
    box_glfw_window_t *windows = ecs_vec_first_t(&box_glfw_windows, box_glfw_window_t);
    GLFWwindow *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&box_glfw_windows) && !result; i ++) {
        if (windows[i].target->world == world) {
            result = windows[i].window;
        }
    }

    return result;
}

/** Put text on the clipboard, the clipboard_set callback of the backend.
 *
 * @param world The world
 * @param text UTF-8 text
 * @param ctx The backend state
 */
static void box_glfw_clipboard_set(
    ecs_world_t *world,
    const char *text,
    void *ctx)
{
    GLFWwindow *window = box_glfw_window_of(world);
    (void)ctx;
    if (window) {
        glfwSetClipboardString(window, text);
    }
}

/** Read the clipboard, the clipboard_get callback of the backend.
 *
 * @param world The world
 * @param ctx The backend state
 * @return The text, NULL when the world has no window or the clipboard holds no text
 */
static const char* box_glfw_clipboard_get(
    ecs_world_t *world,
    void *ctx)
{
    GLFWwindow *window = box_glfw_window_of(world);
    (void)ctx;

    return window ? glfwGetClipboardString(window) : NULL;
}

/** Destroy the cursors of the backend unless glfw already did, and release its state.
 *
 * @param ctx The backend state
 */
static void box_glfw_free(
    void *ctx)
{
    box_glfw_backend_t *backend = ctx;
    int32_t i;
    box_glfw_cursors_check(backend);
    for (i = 0; i < BOX_GLFW_CURSORS && ecs_vec_count(&box_glfw_windows); i ++) {
        if (backend->cursors[i]) {
            glfwDestroyCursor(backend->cursors[i]);
        }
    }

    ecs_os_free(backend);
}

/** Start pushing the events of a window: its surface is stamped on them until the end.
 *
 * @param window The glfw window
 * @return The world the window sends its events to
 */
static ecs_world_t* box_glfw_begin(
    GLFWwindow *window)
{
    box_input_glfw_target_t *target = glfwGetWindowUserPointer(window);
    box_input_platform_source(target->world, target->surface);

    return target->world;
}

/** Stop stamping the surface of a window on pushed events.
 *
 * @param world The world of the window
 */
static void box_glfw_end(
    ecs_world_t *world)
{
    box_input_platform_source(world, 0);
}

/** Check whether a key event is the paste shortcut: V pressed or repeated with control, or
 * command on macOS, and without alt.
 *
 * @param key The glfw key code
 * @param action Release, press or repeat
 * @param mods Modifier bits
 * @return Whether the event pastes
 */
static bool box_glfw_paste_shortcut(
    int key,
    int action,
    int mods)
{
    int ctrl = GLFW_MOD_CONTROL;
#ifdef __APPLE__
    ctrl |= GLFW_MOD_SUPER;
#endif

    return key == GLFW_KEY_V && action != GLFW_RELEASE && (mods & ctrl) &&
        !(mods & GLFW_MOD_ALT);
}

/** Push a key event, and the clipboard text when the key is the paste shortcut.
 *
 * @param window The glfw window
 * @param key The glfw key code
 * @param scancode The platform scancode
 * @param action Release, press or repeat
 * @param mods Modifier bits
 */
static void box_glfw_key(
    GLFWwindow *window,
    int key,
    int scancode,
    int action,
    int mods)
{
    ecs_world_t *world = box_glfw_begin(window);
    const char *text;
    (void)scancode;
    box_input_glfw_on_key(world, key, action);
    if (box_glfw_paste_shortcut(key, action, mods)) {
        text = glfwGetClipboardString(window);
        if (text && text[0]) {
            box_input_platform_push(world, &(InputEvent){
                .kind = InputPaste, .text = ECS_CONST_CAST(char*, text) });
        }
    }

    box_glfw_end(world);
}

/** Push a mouse button event.
 *
 * @param window The glfw window
 * @param button The glfw button
 * @param action Release or press
 * @param mods Modifier bits
 */
static void box_glfw_button(
    GLFWwindow *window,
    int button,
    int action,
    int mods)
{
    ecs_world_t *world = box_glfw_begin(window);
    (void)mods;
    box_input_glfw_on_button(world, button, action);
    box_glfw_end(world);
}

/** Push an event of a window.
 *
 * @param window The glfw window
 * @param event The event
 */
static void box_glfw_push(
    GLFWwindow *window,
    const InputEvent *event)
{
    ecs_world_t *world = box_glfw_begin(window);
    box_input_platform_push(world, event);
    box_glfw_end(world);
}

/** Push a typed character.
 *
 * @param window The glfw window
 * @param codepoint The unicode code point
 */
static void box_glfw_char(
    GLFWwindow *window,
    unsigned int codepoint)
{
    box_glfw_push(window, &(InputEvent){ .kind = InputText, .code = (int32_t)codepoint });
}

/** Push a cursor move in window points.
 *
 * @param window The glfw window
 * @param x Cursor x
 * @param y Cursor y
 */
static void box_glfw_cursor(
    GLFWwindow *window,
    double x,
    double y)
{
    box_glfw_push(window, &(InputEvent){ .kind = InputMove, .value = { (float)x, (float)y } });
}

/** Push a wheel movement.
 *
 * @param window The glfw window
 * @param x Horizontal offset
 * @param y Vertical offset, positive when the wheel turns away from the user
 */
static void box_glfw_scroll(
    GLFWwindow *window,
    double x,
    double y)
{
    box_glfw_push(window, &(InputEvent){ .kind = InputScroll, .value = { (float)x, (float)y } });
}

/** Push a focus change. A gain first reports where the cursor is, because macOS sends no
 * cursor moves to a window without focus and the click that focuses it has no move of its own.
 *
 * @param window The glfw window
 * @param focused Nonzero when the window has keyboard focus
 */
static void box_glfw_focus(
    GLFWwindow *window,
    int focused)
{
    double x = 0, y = 0;
    if (focused) {
        glfwGetCursorPos(window, &x, &y);
        box_glfw_cursor(window, x, y);
    }

    box_glfw_push(window, &(InputEvent){ .kind = InputFocus, .code = focused ? 1 : 0 });
}

/** Push a window size in points.
 *
 * @param window The glfw window
 * @param width Width in points
 * @param height Height in points
 */
static void box_glfw_size(
    GLFWwindow *window,
    int width,
    int height)
{
    box_glfw_push(window, &(InputEvent){
        .kind = InputResize, .value = { (float)width, (float)height } });
}

void box_input_glfw_attach(
    box_input_glfw_target_t *target,
    void *window)
{
    GLFWwindow *w = window;
    box_glfw_backend_t *backend;
    InputDevice *dev;
    int width = 0, height = 0;
    if (!ecs_lookup(target->world, "box.input")) {
        goto done;
    }

    glfwSetWindowUserPointer(w, target);
    glfwSetKeyCallback(w, box_glfw_key);
    glfwSetMouseButtonCallback(w, box_glfw_button);
    glfwSetCharCallback(w, box_glfw_char);
    glfwSetCursorPosCallback(w, box_glfw_cursor);
    glfwSetScrollCallback(w, box_glfw_scroll);
    glfwSetWindowFocusCallback(w, box_glfw_focus);
    glfwSetWindowSizeCallback(w, box_glfw_size);
    glfwGetWindowSize(w, &width, &height);
    box_glfw_size(w, width, height);
    if (glfwGetWindowAttrib(w, GLFW_FOCUSED)) {
        box_glfw_focus(w, 1);
    }

    if (!box_glfw_windows.array) {
        ecs_vec_init_t(NULL, &box_glfw_windows, box_glfw_window_t, 1);
    }

    ecs_vec_append_t(NULL, &box_glfw_windows, box_glfw_window_t)[0] =
        (box_glfw_window_t){ .window = w, .target = target };
    dev = box_input_device(target->world);
    if (dev->backend.capture == box_glfw_capture) {
        backend = dev->backend.ctx;
        glfwSetCursor(w, box_glfw_cursor_of(backend, backend->shape));
    } else if (!dev->backend.poll && !dev->backend.capture) {
        backend = ecs_os_calloc_t(box_glfw_backend_t);
        backend->world = target->world;
        backend->session = box_glfw_session;
        box_input_platform_set(target->world, &(box_input_platform_t){
            .free = box_glfw_free,
            .capture = box_glfw_capture,
            .clipboard_set = box_glfw_clipboard_set,
            .clipboard_get = box_glfw_clipboard_get,
            .cursor = box_glfw_cursor_shape,
            .ctx = backend });
    }

done:
    return;
}

void box_input_glfw_detach(
    void *window)
{
    box_glfw_window_t *windows = ecs_vec_first_t(&box_glfw_windows, box_glfw_window_t);
    int32_t i, count = ecs_vec_count(&box_glfw_windows);
    for (i = 0; i < count; i ++) {
        if (windows[i].window == window) {
            windows[i] = windows[count - 1];
            ecs_vec_remove_last(&box_glfw_windows);
            break;
        }
    }

    if (box_glfw_windows.array && !ecs_vec_count(&box_glfw_windows)) {
        ecs_vec_fini_t(NULL, &box_glfw_windows, box_glfw_window_t);
        box_glfw_session ++;
    }
}

#else

void box_input_glfw_attach(
    box_input_glfw_target_t *target,
    void *window)
{
    (void)target;
    (void)window;
}

void box_input_glfw_detach(
    void *window)
{
    (void)window;
}

#endif
