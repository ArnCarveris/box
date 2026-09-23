/**
 * @file ui/text_input.c
 * @brief The editable line of a TextInput element, its caret, history and completion.
 *
 * Runs in the immediate update after the device layer and before the tree walk, so
 * typed characters lay out in the same frame. A focused input edits its line, writes
 * it as the Text of its element and places a caret quad.
 */

#include "ui_internal.h"
#include "../command/command_internal.h"

ECS_COMPONENT_DECLARE(TextInputImpl);

/** Bytes a line may grow to before a character is dropped */
#define BOX_UI_LINE_MAX (1024)

/** Test whether a byte continues a UTF-8 code point instead of starting one.
 *
 * @param c The byte
 * @return Whether the byte is a continuation byte
 */
static bool box_ui_input_tail(
    char c)
{
    return ((unsigned char)c & 0xC0) == 0x80;
}

/** Byte the code point before a position starts at.
 *
 * @param value The line
 * @param caret Byte the caret sits before
 * @return Byte the code point before the caret starts at
 */
static int32_t box_ui_input_prev(
    const char *value,
    int32_t caret)
{
    int32_t result = caret;
    if (result > 0) {
        result --;
    }

    while (result > 0 && box_ui_input_tail(value[result])) {
        result --;
    }

    return result;
}

/** Byte the code point after a position ends at.
 *
 * @param value The line
 * @param caret Byte the caret sits before
 * @return Byte after the code point the caret sits on
 */
static int32_t box_ui_input_next(
    const char *value,
    int32_t caret)
{
    int32_t length = (int32_t)ecs_os_strlen(value);
    int32_t result = caret < length ? caret + 1 : length;

    while (result < length && box_ui_input_tail(value[result])) {
        result ++;
    }

    return result;
}

/** Replace the line of an input, releasing the line it held.
 *
 * @param input The component
 * @param value The new line, the input takes ownership of it
 * @param caret Byte the caret sits before, clamped to the line
 */
static void box_ui_input_set(
    TextInput *input,
    char *value,
    int32_t caret)
{
    int32_t length = (int32_t)ecs_os_strlen(value);

    ecs_os_free(input->value);
    input->value = value;
    input->caret = caret < 0 ? 0 : (caret > length ? length : caret);
}

/** Remove a range of bytes from the line of an input.
 *
 * @param input The component
 * @param from First byte of the range
 * @param to Byte after the range
 */
static void box_ui_input_erase(
    TextInput *input,
    int32_t from,
    int32_t to)
{
    int32_t length = (int32_t)ecs_os_strlen(input->value);
    if (from >= to) {
        goto done;
    }

    ecs_os_memmove(&input->value[from], &input->value[to],
        (ecs_size_t)(length - to + 1));
    input->caret = from;
done:
    return;
}

/** Insert the bytes a frame typed at the caret of an input.
 *
 * @param input The component
 * @param text The bytes, not zero terminated
 * @param size Number of bytes
 */
static void box_ui_input_insert(
    TextInput *input,
    const char *text,
    int32_t size)
{
    int32_t length = (int32_t)ecs_os_strlen(input->value);
    if (!size || length + size >= BOX_UI_LINE_MAX) {
        goto done;
    }

    char *value = ecs_os_malloc(length + size + 1);
    ecs_os_memcpy(value, input->value, input->caret);
    ecs_os_memcpy(&value[input->caret], text, size);
    ecs_os_memcpy(&value[input->caret + size], &input->value[input->caret],
        length - input->caret + 1);
    box_ui_input_set(input, value, input->caret + size);
done:
    return;
}

/** Move the line of an input to submitted and empty the line.
 *
 * @param world The world
 * @param e The element
 * @param input The component
 */
static void box_ui_input_submit(
    ecs_world_t *world,
    ecs_entity_t e,
    TextInput *input)
{
    TextInputImpl *impl = ecs_get_mut(world, e, TextInputImpl);
    if (!impl || !input->value || !input->value[0]) {
        goto done;
    }

    if (ecs_vec_count(&impl->history) == BOX_UI_HISTORY_MAX) {
        ecs_os_free(ecs_vec_get_t(&impl->history, char*, 0)[0]);
        ecs_vec_remove_ordered_t(&impl->history, char*, 0);
    }

    ecs_vec_append_t(NULL, &impl->history, char*)[0] =
        ecs_os_strdup(input->value);
    impl->walk = ecs_vec_count(&impl->history);
    ecs_os_free(impl->stash);
    impl->stash = NULL;

    ecs_os_free(input->submitted);
    input->submitted = input->value;
    input->value = ecs_os_strdup("");
    input->caret = 0;
done:
    return;
}

/** Walk the history of an input by one step, up toward the older lines.
 *
 * @param world The world
 * @param e The element
 * @param input The component
 * @param step -1 for the older line, 1 for the newer one
 */
static void box_ui_input_walk(
    ecs_world_t *world,
    ecs_entity_t e,
    TextInput *input,
    int32_t step)
{
    TextInputImpl *impl = ecs_get_mut(world, e, TextInputImpl);
    int32_t count = impl ? ecs_vec_count(&impl->history) : 0;
    if (!count) {
        goto done;
    }

    if (impl->walk == count && step < 0) {
        ecs_os_free(impl->stash);
        impl->stash = ecs_os_strdup(input->value ? input->value : "");
    }

    int32_t walk = impl->walk + step;
    if (walk < 0) {
        walk = 0;
    }

    if (walk > count) {
        walk = count;
    }

    impl->walk = walk;
    if (walk == count) {
        box_ui_input_set(input, ecs_os_strdup(
            impl->stash ? impl->stash : ""), BOX_UI_LINE_MAX);
    } else {
        box_ui_input_set(input, ecs_os_strdup(
            ecs_vec_get_t(&impl->history, char*, walk)[0]), BOX_UI_LINE_MAX);
    }

done:
    return;
}

/** Complete the word the caret sits in against the commands of a session.
 *
 * @param world The world
 * @param input The component
 * @param session Session the entity paths resolve against, 0 for the root
 */
static void box_ui_input_complete(
    ecs_world_t *world,
    TextInput *input,
    ecs_entity_t session)
{
    int32_t caret = input->caret;
    char *line = box_command_complete(
        world, session, input->value ? input->value : "", &caret, NULL);

    if (line) {
        box_ui_input_set(input, line, caret);
    }
}

/** Apply the characters a frame collected to the line of an input.
 *
 * A newline submits the line, which is how injected text drives the console,
 * and the other control characters are dropped.
 *
 * @param world The world
 * @param e The element
 * @param input The component
 * @param in The device state of the frame
 */
static void box_ui_input_text(
    ecs_world_t *world,
    ecs_entity_t e,
    TextInput *input,
    const Input *in)
{
    int32_t i, start = 0;
    for (i = 0; i < in->text_len; i ++) {
        char c = in->text[i];
        if ((unsigned char)c >= ' ' && c != 127) {
            continue;
        }

        box_ui_input_insert(input, &in->text[start], i - start);
        start = i + 1;
        if (c == '\n' || c == '\r') {
            box_ui_input_submit(world, e, input);
        }
    }

    box_ui_input_insert(input, &in->text[start], in->text_len - start);
}

/** Apply the edit keys of a frame to the line of an input.
 *
 * @param world The world
 * @param e The element
 * @param input The component
 * @param in The device state of the frame
 * @param session Session the completion resolves against, 0 for the root
 */
static void box_ui_input_keys(
    ecs_world_t *world,
    ecs_entity_t e,
    TextInput *input,
    const Input *in,
    ecs_entity_t session)
{
    int32_t length = (int32_t)ecs_os_strlen(input->value);
    if (in->keys[BoxKeyBackspace].pressed) {
        box_ui_input_erase(input,
            box_ui_input_prev(input->value, input->caret), input->caret);
    }

    if (in->keys[BoxKeyDelete].pressed) {
        box_ui_input_erase(input, input->caret,
            box_ui_input_next(input->value, input->caret));
    }

    if (in->keys[BoxKeyLeft].pressed) {
        input->caret = box_ui_input_prev(input->value, input->caret);
    }

    if (in->keys[BoxKeyRight].pressed) {
        input->caret = box_ui_input_next(input->value, input->caret);
    }

    if (in->keys[BoxKeyHome].pressed) {
        input->caret = 0;
    }

    if (in->keys[BoxKeyEnd].pressed) {
        input->caret = length;
    }

    if (input->history && in->keys[BoxKeyUp].pressed) {
        box_ui_input_walk(world, e, input, -1);
    }

    if (input->history && in->keys[BoxKeyDown].pressed) {
        box_ui_input_walk(world, e, input, 1);
    }

    if (input->complete && in->keys[BoxKeyTab].pressed) {
        box_ui_input_complete(world, input, session);
    }

    if (in->keys[BoxKeyEnter].pressed || in->keys[BoxKeyKpEnter].pressed) {
        box_ui_input_submit(world, e, input);
    }
}

/** Pixels the text before the caret of an input takes in the font of its element.
 *
 * @param world The world
 * @param e The element
 * @param input The component
 * @param height Receives the height of the caret
 * @return The width of the text before the caret
 */
static float box_ui_input_offset(
    ecs_world_t *world,
    ecs_entity_t e,
    const TextInput *input,
    float *height)
{
    box_ui_atlas_t *atlas = box_ui_atlas_of(world, e);
    const Text *stored = ecs_get(world, e, Text);
    int32_t caret = input->caret;
    char *prefix = ecs_os_malloc(caret + 1);
    box_ui_extent_t extent;
    Text text = { .value = prefix };
    float trailing = 0;
    int32_t i;

    ecs_os_memcpy(prefix, input->value, caret);
    prefix[caret] = '\0';
    box_ui_text_run(world, atlas, &text, 0, 0, &extent);

    for (i = caret; i > 0 && (prefix[i - 1] == ' ' || prefix[i - 1] == '\t'); i --) {
        const box_ui_glyph_t *glyph = box_ui_atlas_glyph(world, atlas, ' ');

        trailing += glyph ? glyph->advance * (prefix[i - 1] == '\t' ? 4.0f : 1.0f) : 0;
    }

    ecs_os_free(prefix);
    height[0] = box_ui_text_line_height(atlas, stored);

    return extent.width + trailing;
}

/** Give the element of an input the quad that shows its caret.
 *
 * The quad is pinned to the left edge of the box of the element, so it takes no
 * room in the row or the column its element is placed in and the layout pass
 * moves it with everything else.
 *
 * @param world The world
 * @param e The element
 * @param input The component
 */
static void box_ui_input_caret(
    ecs_world_t *world,
    ecs_entity_t e,
    const TextInput *input)
{
    TextInputImpl *impl = ecs_get_mut(world, e, TextInputImpl);
    if (!impl) {
        goto done;
    }

    if (!input->focused) {
        if (impl->caret && ecs_is_alive(world, impl->caret)) {
            ecs_delete(world, impl->caret);
        }

        impl->caret = 0;
        goto done;
    }

    float height = 0;
    float offset = box_ui_input_offset(world, e, input, &height);
    if (!impl->caret || !ecs_is_alive(world, impl->caret)) {
        ecs_entity_t quad = ecs_entity(world, { .name = "caret", .parent = e });
        const Rgb *rgb = ecs_get(world, e, Rgb);

        ecs_set(world, quad, Rgb, { rgb ? rgb->r : 1, rgb ? rgb->g : 1,
            rgb ? rgb->b : 1 });
        impl = ecs_get_mut(world, e, TextInputImpl);
        impl->caret = quad;
        impl->offset = -1;
    }

    if (impl->offset == offset && impl->height == height) {
        goto done;
    }

    ecs_set(world, impl->caret, Anchor, {
        .h = AlignLeft, .v = VAlignMiddle, .x = offset });
    ecs_set(world, impl->caret, Rect, { BOX_UI_CARET_WIDTH, height });
    impl = ecs_get_mut(world, e, TextInputImpl);
    impl->offset = offset;
    impl->height = height;
done:
    return;
}

/** Let one focused input edit its line with the frame and show the result.
 *
 * @param world The world
 * @param e The element
 * @param session Session the completion resolves against, 0 for the root
 * @param in The device state of the frame
 */
static void box_ui_input_edit(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t session,
    const Input *in)
{
    TextInput *input = ecs_get_mut(world, e, TextInput);
    if (!input) {
        goto done;
    }

    if (!input->value) {
        input->value = ecs_os_strdup("");
    }

    char *before = ecs_os_strdup(input->value);
    if (input->focused && in->focused) {
        box_ui_input_keys(world, e, input, in, session);
        input = ecs_get_mut(world, e, TextInput);
        box_ui_input_text(world, e, input, in);
        input = ecs_get_mut(world, e, TextInput);
    }

    if (ecs_os_strcmp(before, input->value)) {
        ecs_set(world, e, Text, { .value = ecs_os_strdup(input->value) });
    } else if (!ecs_has(world, e, Text)) {
        ecs_set(world, e, Text, { .value = ecs_os_strdup(input->value) });
    }

    ecs_os_free(before);
    box_ui_input_caret(world, e, ecs_get(world, e, TextInput));
done:
    return;
}

void box_ui_text_input_update(
    ecs_world_t *world)
{
    const Input *in = ecs_id(Input) ? ecs_singleton_get(world, Input) : NULL;
    const Console *console = ecs_id(Console) ?
        ecs_singleton_get(world, Console) : NULL;
    ecs_entity_t session = console ? console->session : 0;
    if (!in) {
        goto done;
    }

    ecs_vec_t inputs;
    ecs_vec_init_t(NULL, &inputs, ecs_entity_t, 0);
    ecs_iter_t it = ecs_each(world, TextInput);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_vec_append_t(NULL, &inputs, ecs_entity_t)[0] = it.entities[i];
        }
    }

    int32_t i, count = ecs_vec_count(&inputs);
    for (i = 0; i < count; i ++) {
        box_ui_input_edit(
            world, ecs_vec_get_t(&inputs, ecs_entity_t, i)[0], session, in);
    }

    ecs_vec_fini_t(NULL, &inputs, ecs_entity_t);
done:
    return;
}

/** TextInputImpl constructor: an input without history and without a caret.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_ui_input_impl_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    TextInputImpl *impl = ptr;
    int32_t i;

    memset(ptr, 0, (size_t)count * sizeof(TextInputImpl));
    for (i = 0; i < count; i ++) {
        ecs_vec_init_t(NULL, &impl[i].history, char*, 0);
        impl[i].offset = -1;
    }
}

/** TextInputImpl destructor: release the history and the line it stashed.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_ui_input_impl_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    TextInputImpl *impl = ptr;
    int32_t i, h;
    for (i = 0; i < count; i ++) {
        for (h = 0; h < ecs_vec_count(&impl[i].history); h ++) {
            ecs_os_free(ecs_vec_get_t(&impl[i].history, char*, h)[0]);
        }

        ecs_vec_fini_t(NULL, &impl[i].history, char*);
        ecs_os_free(impl[i].stash);
        impl[i].stash = NULL;
    }
}

/** TextInput observer: an input that takes the focus takes it from every other one.
 *
 * @param it Iterator over the inputs that were set
 */
static void box_ui_input_focused(
    ecs_iter_t *it)
{
    const TextInput *input = ecs_field(it, TextInput, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (!input[i].focused) {
            continue;
        }

        ecs_iter_t others = ecs_each(it->world, TextInput);
        while (ecs_each_next(&others)) {
            TextInput *other = ecs_field(&others, TextInput, 0);
            int32_t o;
            for (o = 0; o < others.count; o ++) {
                if (others.entities[o] != it->entities[i] && other[o].focused) {
                    other[o].focused = false;
                    ecs_modified(it->world, others.entities[o], TextInput);
                }
            }
        }
    }
}

/** TextInput observer: an element that loses the component loses its state.
 *
 * @param it Iterator over the elements that lose the component
 */
static void box_ui_input_dropped(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_remove(it->stage, it->entities[i], TextInputImpl);
    }
}

void box_ui_text_input_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, TextInputImpl);

    ecs_set_hooks(world, TextInputImpl, {
        .ctor = box_ui_input_impl_ctor,
        .dtor = box_ui_input_impl_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });

    ecs_add_pair(world, ecs_id(TextInput), EcsWith, ecs_id(TextInputImpl));
    ecs_add_pair(world, ecs_id(TextInputImpl), EcsOnInstantiate, EcsDontInherit);

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(TextInput), .inout = EcsInOutNone },
            { .id = ecs_id(TextInputImpl), .inout = EcsInOutFilter }
        },
        .events = { EcsOnRemove },
        .callback = box_ui_input_dropped
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(TextInput), .inout = EcsIn }},
        .events = { EcsOnSet },
        .callback = box_ui_input_focused
    });
}
