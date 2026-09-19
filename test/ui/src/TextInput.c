/**
 * @file TextInput.c
 * @brief Tests for the editable line of a TextInput element, its caret and its history.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;
static ecs_entity_t element;

void TextInput_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
    ECS_IMPORT(w, BoxCommand);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 960;
    s->height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    element = ecs_entity(w, { .name = "input", .parent = root });
    ecs_set(w, element, Layout, { .width = 400, .height = 20 });
    ecs_set(w, element, TextInput, { .focused = true });
    ecs_progress(w, 0);
}

void TextInput_teardown(void)
{
    ecs_fini(w);
}

void TextInput_typed_text_reaches_the_line(void)
{
    // ui/console
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'b' });
    ecs_progress(w, 0);

    const TextInput *input = ecs_get(w, element, TextInput);
    test_str(input->value, "ab");
    test_int(input->caret, 2);
}

void TextInput_line_is_shown_as_the_text_of_the_element(void)
{
    // ui/console
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'h' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'i' });
    ecs_progress(w, 0);

    const Text *text = ecs_get(w, element, Text);
    test_assert(text != NULL);
    test_str(text->value, "hi");
}

void TextInput_unfocused_input_takes_nothing(void)
{
    // ui/console
    ecs_get_mut(w, element, TextInput)->focused = false;
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    ecs_progress(w, 0);

    test_str(ecs_get(w, element, TextInput)->value, "");
}

void TextInput_backspace_removes_the_character_before_the_caret(void)
{
    // ui/console
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'b' });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyBackspace, .duration = 1 });
    ecs_progress(w, 0);

    const TextInput *input = ecs_get(w, element, TextInput);
    test_str(input->value, "a");
    test_int(input->caret, 1);
}

void TextInput_arrows_move_the_caret_over_the_line(void)
{
    // ui/console
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'b' });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyLeft, .duration = 1 });
    ecs_progress(w, 0);
    test_int(ecs_get(w, element, TextInput)->caret, 1);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyHome, .duration = 1 });
    ecs_progress(w, 0);
    test_int(ecs_get(w, element, TextInput)->caret, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEnd, .duration = 1 });
    ecs_progress(w, 0);
    test_int(ecs_get(w, element, TextInput)->caret, 2);
}

void TextInput_typing_inserts_at_the_caret(void)
{
    // ui/console
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'c' });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyLeft, .duration = 1 });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'b' });
    ecs_progress(w, 0);

    test_str(ecs_get(w, element, TextInput)->value, "abc");
}

void TextInput_enter_submits_the_line_and_empties_it(void)
{
    // ui/console
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'l' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 's' });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEnter, .duration = 1 });
    ecs_progress(w, 0);

    const TextInput *input = ecs_get(w, element, TextInput);
    test_str(input->submitted, "ls");
    test_str(input->value, "");
    test_int(input->caret, 0);
}

void TextInput_newline_in_the_typed_text_submits_the_line(void)
{
    // ui/console
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'l' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 's' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = '\n' });
    ecs_progress(w, 0);

    const TextInput *input = ecs_get(w, element, TextInput);
    test_str(input->submitted, "ls");
    test_str(input->value, "");
}

void TextInput_history_walks_the_lines_that_were_submitted(void)
{
    // ui/console
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = '\n' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'b', .delay = 1 });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = '\n', .delay = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_str(ecs_get(w, element, TextInput)->value, "");

    ecs_get_mut(w, element, TextInput)->history = true;
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyUp, .duration = 1 });
    ecs_progress(w, 0);
    test_str(ecs_get(w, element, TextInput)->value, "b");

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyUp, .duration = 1 });
    ecs_progress(w, 0);
    test_str(ecs_get(w, element, TextInput)->value, "a");

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyDown, .duration = 1 });
    ecs_progress(w, 0);
    test_str(ecs_get(w, element, TextInput)->value, "b");
}

void TextInput_history_comes_back_to_the_line_being_typed(void)
{
    // ui/console
    ecs_get_mut(w, element, TextInput)->history = true;
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = '\n' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'z', .delay = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_str(ecs_get(w, element, TextInput)->value, "z");

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyUp, .duration = 1 });
    ecs_progress(w, 0);
    test_str(ecs_get(w, element, TextInput)->value, "a");

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyDown, .duration = 1 });
    ecs_progress(w, 0);
    test_str(ecs_get(w, element, TextInput)->value, "z");
}

void TextInput_tab_completes_a_command_name(void)
{
    // ui/console
    ecs_singleton_set(w, Console, { .open = false });
    ecs_progress(w, 0);
    ecs_get_mut(w, element, TextInput)->complete = true;
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'c' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'p' });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyTab, .duration = 1 });
    ecs_progress(w, 0);

    const TextInput *input = ecs_get(w, element, TextInput);
    test_str(input->value, "capture ");
    test_int(input->caret, 8);
}

void TextInput_caret_sits_at_the_width_of_the_text_before_it(void)
{
    // ui/console
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'b' });
    ecs_progress(w, 0);

    ecs_entity_t caret = ecs_lookup_child(w, element, "caret");
    test_assert(caret != 0);
    const Anchor *anchor = ecs_get(w, caret, Anchor);
    test_assert(anchor != NULL);
    test_assert(anchor->x > 0);
    float wide = anchor->x;

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyHome, .duration = 1 });
    ecs_progress(w, 0);
    test_assert(ecs_get(w, caret, Anchor)->x == 0);
    test_assert(wide > 0);
}

void TextInput_caret_advances_by_the_width_of_a_typed_space(void)
{
    // ore_engine/console_caret_space
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    ecs_progress(w, 0);

    ecs_entity_t caret = ecs_lookup_child(w, element, "caret");
    test_assert(caret != 0);
    float after_a = ecs_get(w, caret, Anchor)->x;

    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = ' ' });
    ecs_progress(w, 0);
    float after_first_space = ecs_get(w, caret, Anchor)->x;
    test_assert(after_first_space > after_a);

    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = ' ' });
    ecs_progress(w, 0);
    float after_second_space = ecs_get(w, caret, Anchor)->x;
    test_assert(after_second_space > after_first_space);

    float first_step = after_first_space - after_a;
    float second_step = after_second_space - after_first_space;
    test_assert(fabsf(first_step - second_step) < 0.01f);
}

void TextInput_caret_goes_with_the_focus(void)
{
    // ui/console
    ecs_progress(w, 0);
    test_assert(ecs_lookup_child(w, element, "caret") != 0);

    ecs_get_mut(w, element, TextInput)->focused = false;
    ecs_progress(w, 0);
    test_assert(ecs_lookup_child(w, element, "caret") == 0);

    ecs_get_mut(w, element, TextInput)->focused = true;
    ecs_progress(w, 0);
    test_assert(ecs_lookup_child(w, element, "caret") != 0);
}
