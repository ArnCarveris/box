/**
 * @file Widgets.c
 * @brief Tests for the script widget library: each widget through the input events.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;

void Widgets_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 960;
    s->height = 540;
}

void Widgets_teardown(void)
{
    ecs_fini(w);
}

void Widgets_the_library_loads_into_its_scope(void)
{
    // ore_engine/ui_canonical
    const char *names[] = { "Style", "DefaultStyle", "Button", "ProgressBar",
        "Slider", "Checkbox", "RadioGroup", "RadioButton", "Dropdown",
        "DropdownItem", "TabStrip", "Tab", "List", "ListRow", "Grid",
        "GridCell", "Tooltip", "Modal", "ScrollView" };
    int32_t i;
    for (i = 0; i < 19; i ++) {
        char path[64];
        snprintf(path, sizeof(path), "box.ui.widgets.%s", names[i]);
        test_assert(ecs_lookup(w, path) != 0);
    }
}

void Widgets_button_shows_hover_pressed_and_disabled_looks(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.transform\n"
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Button on(label: \"OK\")\n"
        "  Button off(label: \"No\", disabled: true) { Position: {0, -100, 0} }\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t on = ecs_lookup(w, "ui.on");
    ecs_entity_t off = ecs_lookup(w, "ui.off");
    test_assert(on != 0 && off != 0);
    ecs_progress(w, 0);

    test_int(ecs_get(w, on, Rgb)->r, 46);
    test_int(ecs_get(w, off, Rgb)->r, 58);
    test_str(ecs_get(w, ecs_lookup(w, "ui.on.caption"), Text)->value, "OK");

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_int(ecs_get(w, on, Rgb)->r, 60);

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 2 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_int(ecs_get(w, on, Rgb)->r, 24);

    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_int(ecs_get(w, on, Rgb)->r, 60);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 370} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_int(ecs_get(w, on, Rgb)->r, 46);
    test_int(ecs_get(w, off, Rgb)->r, 58);
}

void Widgets_button_carries_its_action_unless_disabled(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.transform\n"
        "using box.input\n"
        "using box.picking\n"
        "using box.ui\n"
        "using box.ui.widgets\n"
        "actions { Go { InputAction: {description: \"Go\"} } }\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Button on(action: actions.Go)\n"
        "  Button off(action: actions.Go, disabled: true) { Position: {0, -100, 0} }\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t on = ecs_lookup(w, "ui.on");
    ecs_entity_t off = ecs_lookup(w, "ui.off");
    ecs_entity_t go = ecs_lookup(w, "actions.Go");
    test_assert(on != 0 && off != 0 && go != 0);
    ecs_progress(w, 0);

    test_assert(ecs_get(w, on, Action) != NULL);
    test_uint(ecs_get(w, on, Action)->action, go);
    test_assert(ecs_get(w, off, Action) == NULL);
}

void Widgets_progress_bar_fills_to_its_value(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  ProgressBar bar(value: 25, width: 164, height: 12)\n"
        "}\n" });
    test_assert(s != 0);
    ecs_progress(w, 0);

    ecs_entity_t fill = ecs_lookup(w, "ui.bar.fill");
    test_assert(fill != 0);
    test_flt(ecs_get(w, fill, Rect)->width, 40);
    test_flt(ecs_get(w, fill, Rect)->height, 8);
    test_flt(ecs_get(w, fill, Bounds)->x, -82 + 2 + 20);
}

void Widgets_slider_follows_a_press_and_a_drag(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Slider slider(value: 25, width: 200, height: 24)\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t fill = ecs_lookup(w, "ui.slider.fill");
    test_assert(fill != 0);
    ecs_progress(w, 0);
    test_flt(ecs_get(w, fill, Rect)->width, 46);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 4 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_flt(ecs_get(w, fill, Rect)->width, 92);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {572, 270} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_flt(ecs_get(w, fill, Rect)->width, 184);
}

void Widgets_checkbox_toggles_on_click(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Checkbox check(label: \"Sound\")\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t box = ecs_lookup(w, "ui.check.box");
    test_assert(box != 0);
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "ui.check.box.mark") == 0);

    const Bounds *b = ecs_get(w, box, Bounds);
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {480 + b->x, 270 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "ui.check.box.mark") != 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "ui.check.box.mark") == 0);
}

void Widgets_radio_group_activates_the_clicked_button(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  RadioGroup group(value: \"Oak\") {\n"
        "    RadioButton oak(label: \"Oak\")\n"
        "    RadioButton pine(label: \"Pine\")\n"
        "  }\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t pine = ecs_lookup(w, "ui.group.pine");
    test_assert(pine != 0);
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "ui.group.oak.indicator.dot") != 0);
    test_assert(ecs_lookup(w, "ui.group.pine.indicator.dot") == 0);

    const Bounds *b = ecs_get(w, pine, Bounds);
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {480 + b->x, 270 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "ui.group.oak.indicator.dot") == 0);
    test_assert(ecs_lookup(w, "ui.group.pine.indicator.dot") != 0);
}

void Widgets_dropdown_expands_and_selects(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Dropdown drop(labels: [\"Apple\", \"Pear\", \"Plum\"], width: 160, height: 28)\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t caption = ecs_lookup(w, "ui.drop.caption");
    test_assert(caption != 0);
    ecs_progress(w, 0);
    test_str(ecs_get(w, caption, Text)->value, "Apple");
    test_assert(ecs_lookup(w, "ui.drop.item_0") == 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t item = ecs_lookup(w, "ui.drop.item_1");
    test_assert(item != 0);
    test_flt(ecs_get(w, item, Bounds)->y, -56);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 326} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_str(ecs_get(w, caption, Text)->value, "Pear");
    test_assert(ecs_lookup(w, "ui.drop.item_1") == 0);
}

void Widgets_tab_strip_activates_the_clicked_tab(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  TabStrip tabs(labels: [\"One\", \"Two\"], width: 90, height: 28)\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t one = ecs_lookup(w, "ui.tabs.tab_0");
    ecs_entity_t two = ecs_lookup(w, "ui.tabs.tab_1");
    test_assert(one != 0 && two != 0);
    ecs_progress(w, 0);
    test_int(ecs_get(w, one, Rgb)->g, 168);
    test_int(ecs_get(w, two, Rgb)->g, 50);

    const Bounds *b = ecs_get(w, two, Bounds);
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {480 + b->x, 270 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_int(ecs_get(w, one, Rgb)->g, 50);
    test_int(ecs_get(w, two, Rgb)->g, 168);
}

void Widgets_list_selects_the_clicked_row(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  List list(items: [\"A\", \"B\", \"C\"], width: 160, height: 96, row_height: 24)\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t row = ecs_lookup(w, "ui.list.row_2");
    test_assert(row != 0);
    ecs_progress(w, 0);
    test_int(ecs_get(w, row, Rgb)->g, 35);

    const Bounds *b = ecs_get(w, row, Bounds);
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {480 + b->x, 270 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_int(ecs_get(w, row, Rgb)->g, 168);
    test_int(ecs_get(w, ecs_lookup(w, "ui.list.row_0"), Rgb)->g, 35);
}

void Widgets_grid_places_cells_in_reading_order(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Grid grid(columns: 2, cell: 40, spacing: 4) {\n"
        "    GridCell c0(index: 0)\n"
        "    GridCell c1(index: 1)\n"
        "    GridCell c2(index: 2)\n"
        "  }\n"
        "}\n" });
    test_assert(s != 0);
    ecs_progress(w, 0);

    test_flt(ecs_get(w, ecs_lookup(w, "ui.grid.c0"), Bounds)->x, -22);
    test_flt(ecs_get(w, ecs_lookup(w, "ui.grid.c0"), Bounds)->y, 0);
    test_flt(ecs_get(w, ecs_lookup(w, "ui.grid.c1"), Bounds)->x, 22);
    test_flt(ecs_get(w, ecs_lookup(w, "ui.grid.c2"), Bounds)->x, -22);
    test_flt(ecs_get(w, ecs_lookup(w, "ui.grid.c2"), Bounds)->y, -44);
    test_flt(ecs_get(w, ecs_lookup(w, "ui.grid.c2"), Rect)->width, 40);
}

void Widgets_tooltip_shows_while_its_target_is_hovered(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.render\n"
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Button button(label: \"Hover me\") {\n"
        "    Tooltip tip(text: \"Hello\", target: ui.button)\n"
        "  }\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t tip = ecs_lookup(w, "ui.button.tip");
    test_assert(tip != 0);
    ecs_progress(w, 0);
    test_assert(ecs_has_id(w, tip, Hidden));

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_assert(!ecs_has_id(w, tip, Hidden));
    test_str(ecs_get(w, ecs_lookup(w, "ui.button.tip.caption"), Text)->value, "Hello");
    test_assert(ecs_get(w, tip, Bounds)->y < -14);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {900, 500} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_assert(ecs_has_id(w, tip, Hidden));
}

void Widgets_modal_hides_while_closed(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.render\n"
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  Modal closed(open: false, title: \"Later\")\n"
        "  Modal shown(open: true, title: \"Now\", width: 300, height: 200)\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t closed = ecs_lookup(w, "ui.closed");
    ecs_entity_t shown = ecs_lookup(w, "ui.shown");
    test_assert(closed != 0 && shown != 0);
    ecs_progress(w, 0);

    test_assert(ecs_has_id(w, closed, Hidden));
    test_assert(!ecs_has_id(w, shown, Hidden));
    test_flt(ecs_get(w, ecs_lookup(w, "ui.shown.backdrop"), Bounds)->width, 960);
    test_flt(ecs_get(w, ecs_lookup(w, "ui.shown.backdrop"), Bounds)->height, 540);
    test_flt(ecs_get(w, ecs_lookup(w, "ui.shown.panel"), Bounds)->width, 300);
    test_str(ecs_get(w, ecs_lookup(w, "ui.shown.panel.heading"), Text)->value, "Now");
}

void Widgets_scroll_view_clips_and_scrolls_its_rows(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.geometry\n"
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  ScrollView view(width: 200, height: 100) {\n"
        "    for i in 0..10 {\n"
        "      \"row_{i}\" { Rect: {160, 20} }\n"
        "    }\n"
        "  }\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t view = ecs_lookup(w, "ui.view");
    test_assert(view != 0);
    ecs_progress(w, 0);
    test_flt(ecs_get(w, view, Bounds)->height, 100);
    test_int(ecs_get(w, view, Scroll)->y, OverflowAuto);
    test_flt(ecs_get(w, view, Scroll)->offset.y, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputScroll, .value = {0, -3} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_assert(ecs_get(w, view, Scroll)->offset.y > 0);
}

void Widgets_a_style_sheet_recolors_a_widget(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.ui\n"
        "using box.ui.widgets\n"
        "prefab Loud {\n"
        "  Style: {accent: {250, 20, 30}, panel_dark: {1, 2, 3}, font: 16, radius: 2, pad: 4}\n"
        "}\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  ProgressBar bar(value: 50, style: Loud)\n"
        "}\n" });
    test_assert(s != 0);
    ecs_progress(w, 0);

    ecs_entity_t bar = ecs_lookup(w, "ui.bar");
    ecs_entity_t fill = ecs_lookup(w, "ui.bar.fill");
    test_assert(bar != 0 && fill != 0);
    test_int(ecs_get(w, fill, Rgb)->r, 250);
    test_int(ecs_get(w, bar, Rgb)->b, 3);
    test_flt(ecs_get(w, bar, Border)->radius, 2);
}

void Widgets_a_layout_sizes_to_the_widgets_it_holds(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.geometry\n"
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  panel {\n"
        "    Layout: {direction: Column, padding: 10, spacing: 6}\n"
        "    Rect: {}\n"
        "    Button a(label: \"A\", width: 120, height: 28)\n"
        "    Button b(label: \"B\", width: 160, height: 28)\n"
        "  }\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t panel = ecs_lookup(w, "ui.panel");
    test_assert(panel != 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);

    test_flt(ecs_get(w, panel, Bounds)->width, 180);
    test_flt(ecs_get(w, panel, Bounds)->height, 82);
    test_flt(ecs_get(w, panel, Rect)->width, 180);
    test_flt(ecs_get(w, ecs_lookup(w, "ui.panel.a"), Bounds)->y, 17);
}

void Widgets_a_nested_layout_sizes_to_its_widget_rows(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.ui\n"
        "using box.ui.widgets\n"
        "ui {\n"
        "  Ui: {height: 540}\n"
        "  panel {\n"
        "    Position: {0, 0, 0}\n"
        "    Layout: {direction: Column, padding: 16, spacing: 14, h: Left}\n"
        "    Rect: {}\n"
        "    Rgb: {24, 28, 38}\n"
        "    buttons {\n"
        "      Layout: {direction: Row, spacing: 10}\n"
        "      Button plain(label: \"Plain\")\n"
        "      Button off(label: \"Off\", disabled: true)\n"
        "    }\n"
        "    Checkbox check(label: \"Sound on\", value: true)\n"
        "    TabStrip tabs(labels: [\"Build\", \"Craft\"], value: 1)\n"
        "  }\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t panel = ecs_lookup(w, "ui.panel");
    ecs_entity_t buttons = ecs_lookup(w, "ui.panel.buttons");
    test_assert(panel != 0 && buttons != 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);

    test_flt(ecs_get(w, buttons, Bounds)->width, 250);
    test_flt(ecs_get(w, panel, Bounds)->width, 282);
    test_flt(ecs_get(w, panel, Rect)->width, 282);
    test_flt(ecs_get(w, panel, Bounds)->x, 0);
    test_flt(ecs_get(w, buttons, Bounds)->x, -282 / 2 + 16 + 125);
}
