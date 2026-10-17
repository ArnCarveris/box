/**
 * @file Clipboard.c
 * @brief The in memory clipboard of a world, the paste shortcut and InputPaste events, and a
 * fake backend that owns the clipboard.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

/** Text the fake backend has on its clipboard */
static char backend_text[64];

/** Times the fake backend was written */
static int32_t backend_sets = 0;

/** Times the fake backend was read */
static int32_t backend_gets = 0;

/** Put text on the clipboard of the fake backend.
 *
 * @param world The world
 * @param text The text
 * @param ctx Unused
 */
static void BackendSet(
    ecs_world_t *world,
    const char *text,
    void *ctx)
{
    (void)world;
    (void)ctx;
    backend_sets ++;
    ecs_os_strncpy(backend_text, text, (int32_t)sizeof(backend_text) - 1);
}

/** Read the clipboard of the fake backend.
 *
 * @param world The world
 * @param ctx Unused
 * @return The text
 */
static const char* BackendGet(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    (void)ctx;
    backend_gets ++;

    return backend_text;
}

/** Reset the fake backend.
 *
 * @param text Text on its clipboard
 */
static void backend_reset(
    const char *text)
{
    ecs_os_zeromem(&backend_text);
    ecs_os_strncpy(backend_text, text, (int32_t)sizeof(backend_text) - 1);
    backend_sets = 0;
    backend_gets = 0;
}

/** Queue Ctrl+V, with optional other modifiers held around it.
 *
 * @param world The world
 * @param shift Hold shift too
 * @param alt Hold alt too
 */
static void queue_paste_shortcut(
    ecs_world_t *world,
    bool shift,
    bool alt)
{
    box_input_key(world, KeyCtrlLeft, true);
    if (shift) {
        box_input_key(world, KeyShiftLeft, true);
    }

    if (alt) {
        box_input_key(world, KeyAltLeft, true);
    }

    box_input_key(world, KeyV, true);
    box_input_queue(world, &(InputEvent){ .kind = InputKeyUp, .key = KeyV, .delay = 1 });
    box_input_key(world, KeyCtrlLeft, false);
    box_input_key(world, KeyShiftLeft, false);
    box_input_key(world, KeyAltLeft, false);
}

/** Get the text pasted this frame.
 *
 * @param world The world
 * @return Input.paste
 */
static const char* pasted(
    ecs_world_t *world)
{
    return ecs_singleton_get(world, Input)->paste;
}

/** Run frames until the synthetic queue is drained, joining what each frame pasted.
 *
 * @param world The world
 * @param frames Receives the frames that pasted, NULL to ignore
 * @return The pasted text, valid until the next call, empty for none
 */
static const char* drain_pasted(
    ecs_world_t *world,
    int32_t *frames)
{
    static char result[256];
    int32_t count = 0;
    result[0] = '\0';
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
        if (pasted(world)) {
            count ++;
            strncat(result, pasted(world), sizeof(result) - strlen(result) - 1);
        }
    }

    if (frames) {
        *frames = count;
    }

    return result;
}

void Clipboard_set_get(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "hello");
    test_str(box_input_clipboard_get(world), "hello");
    box_input_clipboard_set(world, "world");
    test_str(box_input_clipboard_get(world), "world");
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);

    ecs_fini(world);
}

void Clipboard_set_null_clears(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "hello");
    box_input_clipboard_set(world, NULL);
    test_str(box_input_clipboard_get(world), "");

    queue_paste_shortcut(world, false, false);
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);

    ecs_fini(world);
}

void Clipboard_get_empty(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_str(box_input_clipboard_get(world), "");
    test_assert(pasted(world) == NULL);

    ecs_fini(world);
}

void Clipboard_paste_shortcut(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "hello");
    queue_paste_shortcut(world, false, false);
    ecs_progress(world, 0);
    test_str(pasted(world), "hello");
    test_bool(ecs_singleton_get(world, Input)->keys[KeyV].pressed, true);
    test_str(ecs_singleton_get(world, Input)->text, "");

    ecs_fini(world);
}

void Clipboard_paste_lasts_one_frame(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "once");
    queue_paste_shortcut(world, false, false);
    ecs_progress(world, 0);
    test_str(pasted(world), "once");

    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);
    test_str(box_input_clipboard_get(world), "once");

    ecs_fini(world);
}

void Clipboard_paste_needs_ctrl(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "hello");
    box_input_key_press(world, KeyV, 1);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyV].pressed, true);
    test_str(drain_pasted(world, NULL), "");

    box_input_key(world, KeyShiftLeft, true);
    box_input_key_press(world, KeyV, 1);
    box_input_key(world, KeyShiftLeft, false);
    test_str(drain_pasted(world, NULL), "");

    box_input_key(world, KeyCtrlRight, true);
    box_input_key_press(world, KeyC, 1);
    box_input_key(world, KeyCtrlRight, false);
    test_str(drain_pasted(world, NULL), "");

    box_input_key(world, KeySuperLeft, true);
    box_input_key_press(world, KeyV, 1);
    box_input_key(world, KeySuperLeft, false);
#ifdef __APPLE__
    test_str(drain_pasted(world, NULL), "hello");
#else
    test_str(drain_pasted(world, NULL), "");
#endif

    int32_t frames = 0;
    box_input_key(world, KeyCtrlRight, true);
    box_input_key_press(world, KeyV, 1);
    box_input_key(world, KeyCtrlRight, false);
    test_str(drain_pasted(world, &frames), "hello");
    test_int(frames, 1);

    ecs_fini(world);
}

void Clipboard_paste_with_shift(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "shifted");
    queue_paste_shortcut(world, true, false);
    ecs_progress(world, 0);
    test_str(pasted(world), "shifted");

    ecs_fini(world);
}

void Clipboard_paste_not_with_alt(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "altgr");
    queue_paste_shortcut(world, false, true);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyV].pressed, true);
    test_assert(pasted(world) == NULL);
    test_str(drain_pasted(world, NULL), "");

    box_input_key(world, KeyAltRight, true);
    queue_paste_shortcut(world, true, false);
    box_input_key(world, KeyAltRight, false);
    test_str(drain_pasted(world, NULL), "");

    ecs_fini(world);
}

void Clipboard_paste_held_once(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "x");
    box_input_key(world, KeyCtrlLeft, true);
    box_input_key(world, KeyV, true);
    box_input_key(world, KeyV, true);
    ecs_progress(world, 0);
    test_str(pasted(world), "x");

    box_input_key(world, KeyV, true);
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);

    box_input_key(world, KeyV, false);
    box_input_key(world, KeyV, true);
    ecs_progress(world, 0);
    test_str(pasted(world), "x");

    ecs_fini(world);
}

void Clipboard_paste_ctrl_earlier_frame(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "later");
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyCtrlLeft });
    ecs_progress(world, 0);
    Input *in = ecs_singleton_get_mut(world, Input);
    ecs_os_zeromem(&in->keys);
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);

    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyV });
    ecs_progress(world, 0);
    test_str(pasted(world), "later");

    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyUp, .key = KeyCtrlLeft });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyUp, .key = KeyV });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyV });
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);

    ecs_fini(world);
}

void Clipboard_paste_platform_keys(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "platform");
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyV });
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);

    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyUp, .key = KeyV });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyCtrlLeft });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyV });
    ecs_progress(world, 0);
    test_str(pasted(world), "platform");

    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyUp, .key = KeyV });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyAltRight });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyV });
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);

    ecs_fini(world);
}

void Clipboard_paste_platform_repeat(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "r");
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyCtrlLeft });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyV });
    ecs_progress(world, 0);
    test_str(pasted(world), "r");

    box_input_platform_push(world, &(InputEvent){
        .kind = InputKeyDown, .key = KeyV, .repeat = true });
    box_input_platform_push(world, &(InputEvent){
        .kind = InputKeyDown, .key = KeyV, .repeat = true });
    ecs_progress(world, 0);
    test_str(pasted(world), "rr");

    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyV });
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);

    ecs_fini(world);
}

void Clipboard_paste_platform_unfocused(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "hidden");
    box_input_platform_push(world, &(InputEvent){ .kind = InputFocus, .code = 0 });
    ecs_progress(world, 0);

    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyCtrlLeft });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyV });
    box_input_platform_push(world, &(InputEvent){ .kind = InputPaste, .text = "event" });
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);
    test_str(box_input_clipboard_get(world), "hidden");

    ecs_fini(world);
}

void Clipboard_paste_event(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char text[] = "from the page";
    box_input_platform_push(world, &(InputEvent){ .kind = InputPaste, .text = text });
    text[0] = 'X';
    ecs_progress(world, 0);
    test_str(pasted(world), "from the page");
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);

    box_input_platform_push(world, &(InputEvent){ .kind = InputPaste });
    box_input_platform_push(world, &(InputEvent){ .kind = InputPaste, .text = "" });
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);

    ecs_fini(world);
}

void Clipboard_paste_event_updates_clipboard(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "old");
    box_input_platform_push(world, &(InputEvent){ .kind = InputPaste, .text = "new" });
    ecs_progress(world, 0);
    test_str(box_input_clipboard_get(world), "new");

    box_input_paste(world, "synthetic");
    ecs_progress(world, 0);
    test_str(pasted(world), "synthetic");
    test_str(box_input_clipboard_get(world), "new");

    ecs_fini(world);
}

void Clipboard_paste_queued(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_key_press(world, KeyA, 2);
    box_input_paste(world, "after");
    box_input_paste(world, NULL);
    box_input_paste(world, "");
    test_int(box_input_pending(world), 3);
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);
    ecs_progress(world, 0);
    test_str(pasted(world), "after");
    test_int(box_input_pending(world), 0);

    box_input_paste(world, "dropped");
    box_input_reset(world);
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);

    box_input_paste(world, "never applied");
    ecs_fini(world);
}

void Clipboard_paste_joined(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_clipboard_set(world, "c");
    box_input_platform_push(world, &(InputEvent){ .kind = InputPaste, .text = "a" });
    box_input_platform_push(world, &(InputEvent){ .kind = InputPaste, .text = "b" });
    queue_paste_shortcut(world, false, false);
    ecs_progress(world, 0);
    test_str(pasted(world), "abb");

    box_input_platform_push(world, &(InputEvent){ .kind = InputPaste, .text = "left over" });
    ecs_fini(world);
}

void Clipboard_paste_empty_clipboard(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    queue_paste_shortcut(world, false, false);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyV].pressed, true);
    test_assert(pasted(world) == NULL);

    ecs_fini(world);
}

void Clipboard_utf8_round_trip(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *text = "caf\xc3\xa9 \xe2\x9c\x93 \xf0\x9f\x98\x80 \xe4\xb8\xad\n\ttab";
    box_input_clipboard_set(world, text);
    test_str(box_input_clipboard_get(world), text);
    queue_paste_shortcut(world, false, false);
    ecs_progress(world, 0);
    test_str(pasted(world), text);

    ecs_fini(world);
}

void Clipboard_paste_ill_formed(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_platform_push(world, &(InputEvent){
        .kind = InputPaste, .text = "a\xff" "b\xe2\x9c" "c\xc0\xaf" });
    ecs_progress(world, 0);
    test_str(pasted(world),
        "a\xef\xbf\xbd" "b\xef\xbf\xbd" "c\xef\xbf\xbd\xef\xbf\xbd");

    ecs_fini(world);
}

void Clipboard_backend_set(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    backend_reset("");
    box_input_platform_set(world, &(box_input_platform_t){ .clipboard_set = BackendSet });
    box_input_clipboard_set(world, "to the platform");
    test_int(backend_sets, 1);
    test_str(backend_text, "to the platform");
    test_str(box_input_clipboard_get(world), "to the platform");

    box_input_clipboard_set(world, NULL);
    test_int(backend_sets, 2);
    test_str(backend_text, "");

    ecs_fini(world);
}

void Clipboard_backend_get(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    backend_reset("copied elsewhere");
    box_input_platform_set(world, &(box_input_platform_t){
        .clipboard_set = BackendSet, .clipboard_get = BackendGet });
    test_str(box_input_clipboard_get(world), "copied elsewhere");
    test_int(backend_gets, 1);

    box_input_clipboard_set(world, "mine");
    test_str(box_input_clipboard_get(world), "mine");
    test_int(backend_gets, 2);

    ecs_fini(world);
}

void Clipboard_backend_shortcut_not_doubled(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    backend_reset("platform text");
    box_input_platform_set(world, &(box_input_platform_t){
        .clipboard_set = BackendSet, .clipboard_get = BackendGet });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyCtrlLeft });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyV });
    ecs_progress(world, 0);
    test_assert(pasted(world) == NULL);
    test_int(backend_gets, 0);

    box_input_platform_push(world, &(InputEvent){ .kind = InputPaste, .text = "platform text" });
    ecs_progress(world, 0);
    test_str(pasted(world), "platform text");

    ecs_fini(world);
}

void Clipboard_backend_synthetic_shortcut(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    int32_t frames = 0;
    backend_reset("platform text");
    box_input_platform_set(world, &(box_input_platform_t){
        .clipboard_set = BackendSet, .clipboard_get = BackendGet });
    queue_paste_shortcut(world, false, false);
    test_str(drain_pasted(world, &frames), "platform text");
    test_int(frames, 1);
    test_int(backend_gets, 1);

    box_input_platform_set(world, &(box_input_platform_t){ .clipboard_set = BackendSet });
    box_input_clipboard_set(world, "written");
    queue_paste_shortcut(world, false, false);
    test_str(drain_pasted(world, &frames), "written");
    test_int(frames, 1);

    ecs_fini(world);
}

void Clipboard_backend_replaced_keeps_memory(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    backend_reset("");
    box_input_platform_set(world, &(box_input_platform_t){ .clipboard_set = BackendSet });
    box_input_clipboard_set(world, "kept");
    box_input_platform_set(world, NULL);
    test_str(box_input_clipboard_get(world), "kept");

    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyCtrlLeft });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyV });
    ecs_progress(world, 0);
    test_str(pasted(world), "kept");

    ecs_fini(world);
}

void Clipboard_worlds_separate(void) {
    // widgets/clipboard_cursor
    ecs_world_t *a = ecs_init();
    ECS_IMPORT(a, Box);
    ecs_world_t *b = ecs_init();
    ECS_IMPORT(b, Box);

    box_input_clipboard_set(a, "in a");
    test_str(box_input_clipboard_get(a), "in a");
    test_str(box_input_clipboard_get(b), "");

    queue_paste_shortcut(b, false, false);
    ecs_progress(b, 0);
    test_assert(ecs_singleton_get(b, Input)->paste == NULL);

    ecs_fini(a);
    ecs_fini(b);
}
