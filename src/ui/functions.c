/**
 * @file ui/functions.c
 * @brief The text.measure, text.wrap and text.label helpers scripts build widgets with.
 *
 * Scripts that do `using box.ui` call them as text.measure(element, value). They take
 * an element so the font resolves as in layout: the Font of the element, else the
 * nearest one above it, else the engine font.
 */

#include "ui_internal.h"

/** Longest word box_ui_fn_wrap measures, longer ones are measured truncated */
#define BOX_UI_WORD_MAX (256)

/** text.measure: the width and the height a string takes in the font of an element.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the element and the string
 * @param result Receives the size in logical pixels
 */
static void box_ui_fn_measure(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t e = *(ecs_entity_t*)argv[0].ptr;
    char *value = *(char**)argv[1].ptr;
    const Text *stored = ecs_get(world, e, Text);
    Text text = {
        .value = value,
        .line_height = stored ? stored->line_height : 0,
        .align = stored ? stored->align : AlignLeft
    };
    box_ui_extent_t extent;
    (void)argc;

    box_ui_text_run(world, box_ui_atlas_of(world, e), &text, 0, 0, &extent);
    *(vec2*)result->ptr = (vec2){ extent.width, extent.height };
}

/** Width one word takes in the font of an atlas.
 *
 * @param world The world
 * @param atlas The atlas
 * @param word The word, not zero terminated
 * @param size Bytes of the word
 * @return The width in logical pixels
 */
static float box_ui_fn_word(
    ecs_world_t *world,
    box_ui_atlas_t *atlas,
    const char *word,
    int32_t size)
{
    char scratch[BOX_UI_WORD_MAX];
    Text one = { .value = scratch };
    box_ui_extent_t extent;

    if (size >= BOX_UI_WORD_MAX) {
        size = BOX_UI_WORD_MAX - 1;
    }

    ecs_os_memcpy(scratch, word, size);
    scratch[size] = '\0';
    box_ui_text_run(world, atlas, &one, 0, 0, &extent);

    return extent.width;
}

/** Advance of a space in the font of an atlas.
 *
 * @param world The world
 * @param atlas The atlas
 * @return The advance in logical pixels
 */
static float box_ui_fn_space(
    ecs_world_t *world,
    box_ui_atlas_t *atlas)
{
    const box_ui_glyph_t *glyph = atlas->failed ?
        NULL : box_ui_atlas_glyph(world, atlas, ' ');

    return glyph ? glyph->advance : 0;
}

/** text.wrap: the string with a newline wherever it no longer fits a width.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the element, the string and the width
 * @param result Receives the wrapped string
 */
static void box_ui_fn_wrap(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t e = *(ecs_entity_t*)argv[0].ptr;
    const char *value = *(char**)argv[1].ptr;
    float width = *(ecs_f32_t*)argv[2].ptr;
    box_ui_atlas_t *atlas = box_ui_atlas_of(world, e);
    float space = box_ui_fn_space(world, atlas);
    ecs_strbuf_t out = ECS_STRBUF_INIT;
    const char *cur = value ? value : "";
    float pen = 0;
    bool first = true;

    (void)argc;
    while (cur[0]) {
        const char *word = cur;
        while (cur[0] && cur[0] != ' ' && cur[0] != '\n') {
            cur ++;
        }

        int32_t size = (int32_t)(cur - word);
        if (size) {
            float length = box_ui_fn_word(world, atlas, word, size);

            if (!first && width > 0 && pen + space + length > width) {
                ecs_strbuf_appendch(&out, '\n');
                pen = 0;
            } else if (!first) {
                ecs_strbuf_appendch(&out, ' ');
                pen += space;
            }

            ecs_strbuf_appendstrn(&out, word, size);
            pen += length;
            first = false;
        }

        while (cur[0] == ' ') {
            cur ++;
        }

        if (cur[0] == '\n') {
            ecs_strbuf_appendch(&out, '\n');
            pen = 0;
            first = true;
            cur ++;
        }
    }

    *(char**)result->ptr = ecs_strbuf_get(&out);
}

/** text.label: the named text element of a parent, created the first time.
 *
 * The element is looked up by name, so a script that runs again finds the
 * element it made before instead of adding a second one, and the value is the
 * only thing that changes.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the parent, the name and the string
 * @param result Receives the element
 */
static void box_ui_fn_label(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t parent = *(ecs_entity_t*)argv[0].ptr;
    char *name = *(char**)argv[1].ptr;
    char *value = *(char**)argv[2].ptr;
    ecs_entity_t e = ecs_entity(world, { .name = name, .parent = parent });
    (void)argc;

    ecs_set(world, e, Text, { .value = ecs_os_strdup(value ? value : "") });
    *(ecs_entity_t*)result->ptr = e;
}

/** Find the input of an element: the element itself or its first descendant with one.
 *
 * @param world The world
 * @param e The element
 * @return The entity that carries a TextInput, 0 when none does
 */
static ecs_entity_t box_ui_fn_input_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    if (ecs_has(world, e, TextInput)) {
        result = e;
        goto done;
    }

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count && !result; i ++) {
            result = box_ui_fn_input_of(world, it.entities[i]);
        }

        if (result) {
            ecs_iter_fini(&it);
            break;
        }
    }

done:
    return result;
}

/** text.focus: give the input of an element the keyboard, taking it from the others.
 *
 * The input is the element itself or its first descendant with a TextInput, so
 * a widget can wrap its line in a box and focus the box.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the element
 * @param result Receives whether the element has a TextInput that took the focus
 */
static void box_ui_fn_focus(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t e = *(ecs_entity_t*)argv[0].ptr;
    ecs_entity_t target = e && ecs_is_alive(world, e) ? box_ui_fn_input_of(world, e) : 0;
    TextInput *input = target ? ecs_get_mut(world, target, TextInput) : NULL;
    (void)argc;

    if (input) {
        input->focused = true;
        ecs_modified(world, target, TextInput);
    }

    *(bool*)result->ptr = input != NULL;
}

void box_ui_functions_init(
    ecs_world_t *world,
    ecs_entity_t module)
{
    ecs_entity_t scope = ecs_entity(world, {
        .name = "text", .parent = module
    });

    ecs_function(world, {
        .name = "measure",
        .parent = scope,
        .return_type = ecs_id(vec2),
        .params = {
            { .name = "element", .type = ecs_id(ecs_entity_t) },
            { .name = "value", .type = ecs_id(ecs_string_t) }
        },
        .callback = box_ui_fn_measure
    });

    ecs_function(world, {
        .name = "wrap",
        .parent = scope,
        .return_type = ecs_id(ecs_string_t),
        .params = {
            { .name = "element", .type = ecs_id(ecs_entity_t) },
            { .name = "value", .type = ecs_id(ecs_string_t) },
            { .name = "width", .type = ecs_id(ecs_f32_t) }
        },
        .callback = box_ui_fn_wrap
    });

    ecs_function(world, {
        .name = "focus",
        .parent = scope,
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "element", .type = ecs_id(ecs_entity_t) }
        },
        .callback = box_ui_fn_focus
    });

    ecs_function(world, {
        .name = "label",
        .parent = scope,
        .return_type = ecs_id(ecs_entity_t),
        .params = {
            { .name = "parent", .type = ecs_id(ecs_entity_t) },
            { .name = "name", .type = ecs_id(ecs_string_t) },
            { .name = "value", .type = ecs_id(ecs_string_t) }
        },
        .callback = box_ui_fn_label
    });
}
