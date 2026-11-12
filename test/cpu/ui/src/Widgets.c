/**
 * @file Widgets.c
 * @brief The widget library test/assets/ui/widgets.flecs: the box.widgets module and its
 * templates, a world without the library, the UiTheme of box.ui.theme, a script that assigns it
 * and ui.sibling.
 */

#include <box.h>
#include <bake_test.h>
#include "picking.h"
#include <stdlib.h>
#include <string.h>
#include "../../../../src/posix.h"

/** Templates of the widget library */
static const char *widgets_templates[] = {
    "Label", "Panel", "Separator", "HeadlessButton", "Button", "HeadlessToggle", "Checkbox",
    "Switch", "RadioGroup", "HeadlessRadioButton", "RadioButton", "HeadlessSlider", "Slider",
    "ProgressBar", "HeadlessStepper", "Stepper", "HeadlessTextInput", "TextInput", "Chevron", "HeadlessDropdown", "Dropdown",
    "HeadlessScrollView", "ScrollBar", "ScrollView", "HeadlessListView", "ListView",
    "HeadlessTabHeader", "TabHeader", "TabView", "HeadlessTab", "Tab", "HeadlessCollapsible",
    "Collapsible"
};

/** Number of templates of the widget library */
#define WIDGETS_TEMPLATES (33)

/** Templates of the library with a mut */
static const char *widgets_muts[] = {
    "Button.mut", "Checkbox.mut", "Switch.mut", "RadioGroup.mut", "RadioButton.mut",
    "Slider.mut", "ProgressBar.mut", "Stepper.mut", "TextInput.mut", "Dropdown.mut", "ScrollView.mut",
    "ListView.mut", "TabView.mut", "Tab.mut", "Collapsible.mut"
};

/** Number of templates of the library with a mut */
#define WIDGETS_MUTS (15)

/** Get the warnings and errors the log capture of a world kept.
 *
 * @param world The world
 * @return The messages, owned by the caller
 */
static char* widgets_warnings(
    ecs_world_t *world)
{
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *result = box_command_run(world, s, "logs warning 100", &error);
    test_bool(error, false);

    return result;
}

/** Check whether the struct of a type has a member.
 *
 * @param world The world
 * @param type Path of the type
 * @param name Name of the member
 * @return Whether the type has it
 */
static bool widgets_has_member(
    ecs_world_t *world,
    const char *type,
    const char *name)
{
    const EcsStruct *st = ecs_get(world, ecs_lookup(world, type), EcsStruct);
    bool result = false;
    int32_t i;
    test_assert(st != NULL);
    for (i = 0; i < ecs_vec_count(&st->members); i ++) {
        result |= !strcmp(ecs_vec_get_t(&st->members, ecs_member_t, i)->name, name);
    }

    return result;
}

/** Load the widget library and a scene that uses it into a new world.
 *
 * @param code The scene, which can use box.widgets
 * @return The world
 */
static ecs_world_t* widgets_scene(
    const char *code)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *widgets = box_app_locate("test/assets/ui/widgets.flecs");
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "box.ui.widgets" }),
        .filename = widgets
    }) != 0);
    ecs_os_free(widgets);
    test_assert(ecs_script(world, { .code = code }) != 0);

    return world;
}

/** Run frames, so the templates, the layout and the transforms settle.
 *
 * @param world The world
 */
static void widgets_frames(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
    }
}

/** Set a member of the mut of a template instance with the set command, which assigns it as a
 * script does; a mut has an on_replace hook, so it cannot be ensured.
 *
 * @param world The world
 * @param path Path of the instance
 * @param mut Path of the mut, such as box.widgets.ScrollView.mut
 * @param member Name of the member
 * @param value The value
 */
static void widgets_set_mut(
    ecs_world_t *world,
    const char *path,
    const char *mut,
    const char *member,
    double value)
{
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    char *line = flecs_asprintf("set %s %s {%s: %g}", path, mut, member, value);
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    if (error) {
        printf("command '%s' failed: %s\n", line, out);
    }

    test_bool(error, false);
    ecs_os_free(out);
    ecs_os_free(line);
}

/** Get the offset of an entity from another one, from their world transforms.
 *
 * @param world The world
 * @param e The entity
 * @param from The other entity
 * @param out Receives the offset on x and y
 */
static void widgets_offset(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t from,
    float out[2])
{
    const WorldTransform *a = ecs_get(world, e, WorldTransform);
    const WorldTransform *b = ecs_get(world, from, WorldTransform);
    test_assert(a != NULL && b != NULL);
    out[0] = a->m[3][0] - b->m[3][0];
    out[1] = a->m[3][1] - b->m[3][1];
}

/** A scroll view of 200 by 100 points with six blocks of 40 points in a column, which do not fit,
 * placed so it has a world transform */
#define WIDGETS_TALL_VIEW \
    "using box.widgets\n" \
    "using box.layout\n" \
    "using box.geometry\n" \
    "using box.transform\n" \
    "ScrollView view(width: 200, height: 100) {\n" \
    "  Position: {0, 0, 0}\n" \
    "  b0 { LayoutItem: {width: {mode: Fixed, value: 50}, height: {mode: Fixed, value: 40}} }\n" \
    "  b1 { LayoutItem: {width: {mode: Fixed, value: 50}, height: {mode: Fixed, value: 40}} }\n" \
    "  b2 { LayoutItem: {width: {mode: Fixed, value: 50}, height: {mode: Fixed, value: 40}} }\n" \
    "  b3 { LayoutItem: {width: {mode: Fixed, value: 50}, height: {mode: Fixed, value: 40}} }\n" \
    "  b4 { LayoutItem: {width: {mode: Fixed, value: 50}, height: {mode: Fixed, value: 40}} }\n" \
    "  b5 { LayoutItem: {width: {mode: Fixed, value: 50}, height: {mode: Fixed, value: 40}} }\n" \
    "}\n"

void Widgets_library_loads(void) {
    // ui/widgets
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *widgets = box_app_locate("test/assets/ui/widgets.flecs");
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "box.ui.widgets" }),
        .filename = widgets
    }) != 0);
    ecs_os_free(widgets);

    ecs_entity_t module = ecs_lookup(world, "box.widgets");
    test_assert(module != 0);
    test_assert(ecs_has_id(world, module, EcsModule));

    ecs_entity_t script = ecs_lookup(world, "box.ui.widgets");
    test_assert(script != 0);
    const EcsScript *loaded = ecs_get(world, script, EcsScript);
    test_assert(loaded != NULL);
    test_assert(loaded->script != NULL);
    test_assert(loaded->error == NULL);
    test_assert(strstr(loaded->filename, "test/assets/ui/widgets.flecs") != NULL);

    int32_t scripts = 0;
    ecs_iter_t it = ecs_each_id(world, ecs_id(EcsScript));
    while (ecs_each_next(&it)) {
        const EcsScript *sc = ecs_field(&it, EcsScript, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (sc[i].filename && strstr(sc[i].filename, "test/assets/ui/")) {
                test_assert(sc[i].error == NULL);
                scripts ++;
            }
        }
    }

    test_int(scripts, 15);

    char *logs = widgets_warnings(world);
    test_str(logs, "");
    ecs_os_free(logs);

    ecs_fini(world);
}

void Widgets_templates_exist(void) {
    // ui/widgets
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *widgets = box_app_locate("test/assets/ui/widgets.flecs");
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "box.ui.widgets" }),
        .filename = widgets
    }) != 0);
    ecs_os_free(widgets);
    int32_t i;

    for (i = 0; i < WIDGETS_TEMPLATES; i ++) {
        char *path = flecs_asprintf("box.widgets.%s", widgets_templates[i]);
        ecs_entity_t t = ecs_lookup(world, path);
        if (!t) {
            printf("missing template %s\n", path);
        }

        test_assert(t != 0);
        test_assert(ecs_has(world, t, EcsComponent));

        test_int(ecs_count_id(world, t), 0);
        ecs_os_free(path);
    }

    for (i = 0; i < WIDGETS_MUTS; i ++) {
        char *path = flecs_asprintf("box.widgets.%s", widgets_muts[i]);
        ecs_entity_t m = ecs_lookup(world, path);
        test_assert(m != 0);
        test_assert(ecs_has(world, m, EcsComponent));
        ecs_os_free(path);
    }

    test_bool(widgets_has_member(world, "box.widgets.Slider", "value"), true);
    test_bool(widgets_has_member(world, "box.widgets.Checkbox", "value"), true);
    test_bool(widgets_has_member(world, "box.widgets.RadioGroup", "value"), true);
    test_bool(widgets_has_member(world, "box.widgets.Stepper", "value"), true);
    test_bool(widgets_has_member(world, "box.widgets.Slider.mut", "current"), true);
    test_bool(widgets_has_member(world, "box.widgets.Checkbox.mut", "current"), true);
    test_bool(widgets_has_member(world, "box.widgets.Switch.mut", "current"), true);
    test_bool(widgets_has_member(world, "box.widgets.RadioGroup.mut", "current"), true);
    test_bool(widgets_has_member(world, "box.widgets.Stepper.mut", "current"), true);
    test_bool(widgets_has_member(world, "box.widgets.TextInput", "value"), true);
    test_bool(widgets_has_member(world, "box.widgets.TextInput.mut", "current"), true);
    test_bool(widgets_has_member(world, "box.widgets.Dropdown", "value"), true);
    test_bool(widgets_has_member(world, "box.widgets.ListView", "value"), true);
    test_bool(widgets_has_member(world, "box.widgets.TabView", "value"), true);
    test_bool(widgets_has_member(world, "box.widgets.Collapsible", "value"), true);
    test_bool(widgets_has_member(world, "box.widgets.Dropdown.mut", "current"), true);
    test_bool(widgets_has_member(world, "box.widgets.ListView.mut", "current"), true);
    test_bool(widgets_has_member(world, "box.widgets.TabView.mut", "current"), true);
    test_bool(widgets_has_member(world, "box.widgets.Collapsible.mut", "current"), true);

    ecs_entity_t dark = ecs_lookup(world, "box.widgets.DarkTheme");
    ecs_entity_t light = ecs_lookup(world, "box.widgets.LightTheme");
    test_assert(dark != 0);
    test_assert(light != 0);
    ecs_value_t dv = ecs_const_var_get(world, dark);
    ecs_value_t lv = ecs_const_var_get(world, light);
    test_assert(dv.type == ecs_id(UiTheme));
    test_assert(lv.type == ecs_id(UiTheme));
    test_assert(((UiTheme*)lv.ptr)->background.r > 200);

    ecs_fini(world);
}

void Widgets_missing_file(void) {
    // ui/widgets
    char cwd[4096], dir[] = "/tmp/box_widgets_XXXXXX";
    test_assert(getcwd(cwd, sizeof(cwd)) != NULL);
    test_assert(mkdtemp(dir) != NULL);
    test_int(chdir(dir), 0);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_lookup(world, "box.widgets") == 0);
    test_assert(ecs_lookup(world, "box.ui.widgets") == 0);
    test_assert(ecs_lookup(world, "box.ui.theme") != 0);
    test_assert(ecs_get(world, ecs_lookup(world, "box.ui.theme"), UiTheme) != NULL);
    char *logs = widgets_warnings(world);
    test_str(logs, "");
    ecs_os_free(logs);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_fini(world);

    test_int(chdir(cwd), 0);
    test_int(rmdir(dir), 0);
}

void Widgets_theme_dark(void) {
    // ui/widgets
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *widgets = box_app_locate("test/assets/ui/widgets.flecs");
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "box.ui.widgets" }),
        .filename = widgets
    }) != 0);
    ecs_os_free(widgets);

    ecs_entity_t theme = ecs_lookup(world, "box.ui.theme");
    test_assert(theme != 0);
    const UiTheme *t = ecs_get(world, theme, UiTheme);
    test_assert(t != NULL);
    test_int(t->surface.r, 46);
    test_int(t->surface.g, 50);
    test_int(t->surface.b, 58);
    test_int(t->accent.r, 98);
    test_int(t->accent.g, 168);
    test_int(t->accent.b, 122);
    test_int(t->text.r, 214);
    test_int(t->text.g, 218);
    test_int(t->text.b, 224);
    test_assert(t->font_size > 0);
    test_assert(t->control_height > 0);
    test_assert(t->disabled_opacity > 0 && t->disabled_opacity < 1);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, UiTheme);
    test_assert(!ecs_os_memcmp_t(ecs_get(world, e, UiTheme), t, UiTheme));
    ecs_value_t dark = ecs_const_var_get(world, ecs_lookup(world, "box.widgets.DarkTheme"));
    test_assert(dark.ptr != NULL);
    test_assert(!ecs_os_memcmp_t(dark.ptr, t, UiTheme));

    ecs_fini(world);
}

void Widgets_theme_from_script(void) {
    // ui/widgets
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *widgets = box_app_locate("test/assets/ui/widgets.flecs");
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "box.ui.widgets" }),
        .filename = widgets
    }) != 0);
    ecs_os_free(widgets);
    ecs_entity_t theme = ecs_lookup(world, "box.ui.theme");
    test_assert(theme != 0);

    /* A scene with a theme switch declares the theme entity; it never takes it over, so an
     * update or the delete of the scene keeps it and its value */
    ecs_entity_t script = ecs_script(world, { .code =
        "using box.widgets\n"
        "box {\n"
        "  ui {\n"
        "    theme {\n"
        "      box.ui.UiTheme: LightTheme\n"
        "    }\n"
        "  }\n"
        "}\n"
    });
    test_assert(script != 0);
    test_assert(ecs_lookup(world, "box.ui.theme") == theme);
    test_int(ecs_get(world, theme, UiTheme)->background.r, 232);
    test_bool(ecs_has_pair(world, theme, ecs_id(EcsScript), script), false);

    test_int(ecs_script_update(world, script, 0, "using box.widgets\n"), 0);
    test_assert(ecs_is_alive(world, theme));
    test_assert(ecs_lookup(world, "box.ui.theme") == theme);
    test_int(ecs_get(world, theme, UiTheme)->background.r, 232);

    ecs_delete(world, script);
    test_assert(ecs_is_alive(world, theme));
    test_assert(ecs_get(world, theme, UiTheme) != NULL);
    ecs_progress(world, 0);

    ecs_fini(world);
}

void Widgets_sibling(void) {
    // ui/widgets
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.layout\n"
        "using box.picking\n"
        "Tag {}\n"
        "group {\n"
        "  Layout: {}\n"
        "  a { Tag }\n"
        "  b {}\n"
        "  c { Tag }\n"
        "  d { Tag }\n"
        "}\n"
        "lone {}\n"
        "const n1 = ui.sibling(group.a, 1, 0)\n"
        "const n2 = ui.sibling(group.a, 1, Tag)\n"
        "const n3 = ui.sibling(group.d, 1, Tag)\n"
        "const n4 = ui.sibling(group.a, -1, Tag)\n"
        "const n5 = ui.sibling(group.c, 2, Tag)\n"
        "const n6 = ui.sibling(group.b, 1, Tag)\n"
        "const n7 = ui.sibling(group.a, 0, Tag)\n"
        "const n8 = ui.sibling(group.a, 1, Interactive)\n"
        "r1 { (flecs.core.IsA, n1) }\n"
        "r2 { (flecs.core.IsA, n2) }\n"
        "r3 { (flecs.core.IsA, n3) }\n"
        "r4 { (flecs.core.IsA, n4) }\n"
        "r5 { (flecs.core.IsA, n5) }\n"
        "r6 { (flecs.core.IsA, n6) }\n"
        "r7 { (flecs.core.IsA, n7) }\n"
        "r8 { (flecs.core.IsA, n8) }\n"
    }) != 0);

    ecs_entity_t a = ecs_lookup(world, "group.a");
    ecs_entity_t b = ecs_lookup(world, "group.b");
    ecs_entity_t c = ecs_lookup(world, "group.c");
    ecs_entity_t d = ecs_lookup(world, "group.d");
    test_assert(ecs_get_target(world, ecs_lookup(world, "r1"), EcsIsA, 0) == b);
    test_assert(ecs_get_target(world, ecs_lookup(world, "r2"), EcsIsA, 0) == c);
    test_assert(ecs_get_target(world, ecs_lookup(world, "r3"), EcsIsA, 0) == a);
    test_assert(ecs_get_target(world, ecs_lookup(world, "r4"), EcsIsA, 0) == d);
    test_assert(ecs_get_target(world, ecs_lookup(world, "r5"), EcsIsA, 0) == a);
    test_assert(ecs_get_target(world, ecs_lookup(world, "r6"), EcsIsA, 0) == c);
    test_assert(ecs_get_target(world, ecs_lookup(world, "r7"), EcsIsA, 0) == a);
    test_assert(ecs_get_target(world, ecs_lookup(world, "r8"), EcsIsA, 0) == a);

    ecs_fini(world);
}


void Widgets_scroll_bar_in_view(void) {
    // ui/widgets
    ecs_world_t *world = widgets_scene(WIDGETS_TALL_VIEW);
    widgets_frames(world);

    ecs_entity_t view = ecs_lookup(world, "view");
    ecs_entity_t bars = ecs_lookup(world, "view.scrollbars");
    ecs_entity_t bar = ecs_lookup(world, "view.scrollbars.vscroll");
    ecs_entity_t knob = ecs_lookup(world, "view.scrollbars.vscroll.knob");
    test_assert(view != 0 && bars != 0 && bar != 0 && knob != 0);
    test_assert(ecs_lookup(world, "view.scrollbars.hscroll") == 0);
    const LayoutContent *c = ecs_get(world, view, LayoutContent);
    test_assert(c != NULL);
    test_assert(c->max_y > 0);

    /* The bars are a clipped overlay, so the clips of the view and its ancestors clip them, out
     * of the column and drawn and picked over the content */
    const LayoutFloat *overlay = ecs_get(world, bars, LayoutFloat);
    test_assert(overlay != NULL);
    test_bool(overlay->clip, true);
    test_bool(ecs_has(world, bar, LayoutFloat), false);
    ecs_entities_t children = ecs_get_ordered_children(world, view);
    test_assert(children.count == 7);
    test_bool(box_pick_drawn_after(world, bar, ecs_lookup(world, "view.b5")), true);

    /* The track runs one point inside the right, top and bottom edges, 10 points thick, opaque
     * on a color of its own */
    float at[2];
    widgets_offset(world, bar, view, at);
    test_flt(at[0], 100 - 1 - 5);
    test_flt(at[1], 0);
    const Rect *r = ecs_get(world, bar, Rect);
    test_assert(r != NULL);
    test_flt(r->width, 10);
    test_flt(r->height, 98);
    const UiTheme *t = ecs_get(world, ecs_lookup(world, "box.ui.theme"), UiTheme);
    const Rgb *track = ecs_get(world, bar, Rgb);
    const Rgb *surface = ecs_get(world, view, Rgb);
    test_assert(track != NULL && surface != NULL);
    test_int(track->r, t->surface.r);
    test_assert(track->r != surface->r || track->g != surface->g || track->b != surface->b);
    const Opacity *o = ecs_get(world, bar, Opacity);
    test_assert(o == NULL || o->value == 1);

    /* The thumb starts at the top of the track */
    float thumb[2];
    widgets_offset(world, knob, view, thumb);
    const Rect *k = ecs_get(world, knob, Rect);
    test_assert(k != NULL);
    test_flt(thumb[1] + k->height / 2, 50 - 1 - 2);

    /* Scrolled, the bar stays where it is and the thumb moves down, in the frame that moves the
     * content: the bar never jumps with the content for a frame */
    float block[2], shifted[2];
    widgets_offset(world, ecs_lookup(world, "view.b0"), view, block);
    widgets_set_mut(world, "view", "box.widgets.ScrollView.mut", "y", 40);
    ecs_progress(world, 0);
    widgets_offset(world, ecs_lookup(world, "view.b0"), view, shifted);
    test_flt(shifted[1], block[1] + 40);
    widgets_offset(world, bar, view, at);
    test_flt(at[0], 100 - 1 - 5);
    test_flt(at[1], 0);
    float moved[2];
    widgets_offset(world, knob, view, moved);
    test_assert(moved[1] < thumb[1] - 1);
    widgets_frames(world);
    c = ecs_get(world, view, LayoutContent);
    test_flt(c->y, 40);
    widgets_offset(world, bar, view, at);
    test_flt(at[1], 0);

    /* At the end the thumb ends at the end of the track */
    widgets_set_mut(world, "view", "box.widgets.ScrollView.mut", "y", 1000);
    widgets_frames(world);
    widgets_offset(world, bar, view, at);
    test_flt(at[1], 0);
    widgets_offset(world, knob, view, moved);
    k = ecs_get(world, knob, Rect);
    test_flt(moved[1] - k->height / 2, -50 + 1 + 2);

    char *logs = widgets_warnings(world);
    test_str(logs, "");
    ecs_os_free(logs);
    ecs_fini(world);
}

void Widgets_scroll_gutter(void) {
    // ui/widgets
    ecs_world_t *world = widgets_scene(
        "using box.widgets\n"
        "using box.layout\n"
        "ScrollView view(width: 200, height: 100, horizontal: true) {\n"
        "  b0 { LayoutItem: {width: {mode: Fixed, value: 50}, height: {mode: Fixed, value: 40}} }\n"
        "}\n");
    widgets_frames(world);

    /* Everything fits: no bars and no room for them */
    ecs_entity_t view = ecs_lookup(world, "view");
    test_assert(ecs_lookup(world, "view.scrollbars") == 0);
    const Layout *l = ecs_get(world, view, Layout);
    test_assert(l != NULL);
    test_flt(l->padding.right, 8);
    test_flt(l->padding.bottom, 8);
    test_flt(l->padding.left, 8);
    test_flt(l->padding.top, 8);
    const LayoutContent *c = ecs_get(world, view, LayoutContent);
    test_flt(c->height, 40 + 16);

    char *logs = widgets_warnings(world);
    test_str(logs, "");
    ecs_os_free(logs);
    ecs_fini(world);
}

void Widgets_scroll_gutter_with_bar(void) {
    // ui/widgets
    ecs_world_t *world = widgets_scene(WIDGETS_TALL_VIEW);
    widgets_frames(world);

    /* The vertical bar shows: room on the right, the bottom keeps the padding below the last
     * block, the bars taking no place in the column */
    ecs_entity_t view = ecs_lookup(world, "view");
    const Layout *l = ecs_get(world, view, Layout);
    test_assert(l != NULL);
    test_flt(l->padding.right, 18);
    test_flt(l->padding.bottom, 8);
    const LayoutContent *c = ecs_get(world, view, LayoutContent);
    test_flt(c->height, 6 * 40 + 5 * 8 + 16);
    test_flt(c->max_y, c->height - 100);

    ecs_fini(world);
}

void Widgets_scroll_thumb_hover_look(void) {
    // ui/widgets
    ecs_world_t *world = widgets_scene(WIDGETS_TALL_VIEW);
    widgets_frames(world);
    ecs_entity_t view = ecs_lookup(world, "view");
    ecs_entity_t knob = ecs_lookup(world, "view.scrollbars.vscroll.knob");
    test_assert(knob != 0);
    test_bool(ecs_has(world, knob, Tint), false);

    /* Over the thumb it lightens toward the text color */
    widgets_set_mut(world, "view", "box.widgets.ScrollView.mut", "over", 1);
    widgets_frames(world);
    knob = ecs_lookup(world, "view.scrollbars.vscroll.knob");
    test_bool(ecs_has(world, knob, Tint), true);

    /* Dragged it takes the text color */
    widgets_set_mut(world, "view", "box.widgets.ScrollView.mut", "drag", 1);
    widgets_frames(world);
    knob = ecs_lookup(world, "view.scrollbars.vscroll.knob");
    test_bool(ecs_has(world, knob, Tint), false);
    const UiTheme *t = ecs_get(world, ecs_lookup(world, "box.ui.theme"), UiTheme);
    test_int(ecs_get(world, knob, Rgb)->r, t->text.r);

    ecs_fini(world);
}

void Widgets_panel_title_in_column(void) {
    // ui/widgets
    ecs_world_t *world = widgets_scene(
        "using box.widgets\n"
        "using box.layout\n"
        "using box.transform\n"
        "Panel panel(title: \"Title\", width: 300) {\n"
        "  Position: {0, 0, 0}\n"
        "  b0 { LayoutItem: {width: {mode: Fixed, value: 50}, height: {mode: Fixed, value: 40}} }\n"
        "}\n");
    widgets_frames(world);

    /* The title is the first child of the column, not a float that leaves the clips around the
     * panel; the block follows it after the spacing */
    ecs_entity_t panel = ecs_lookup(world, "panel");
    ecs_entity_t heading = ecs_lookup(world, "panel.heading");
    ecs_entity_t block = ecs_lookup(world, "panel.b0");
    test_assert(panel != 0 && heading != 0 && block != 0);
    test_bool(ecs_has(world, heading, LayoutFloat), false);
    ecs_entities_t children = ecs_get_ordered_children(world, panel);
    test_assert(children.count == 2);
    test_assert(children.ids[0] == heading);

    const LayoutBounds *pb = ecs_get(world, panel, LayoutBounds);
    const LayoutBounds *hb = ecs_get(world, heading, LayoutBounds);
    test_assert(pb != NULL && hb != NULL);
    test_flt(hb->height, 18 * 1.25f);
    test_flt(pb->height, 12 + 18 * 1.25f + 8 + 40 + 12);
    float at[2];
    widgets_offset(world, heading, panel, at);
    test_flt(at[0] - hb->width / 2, -150 + 12);
    test_flt(at[1] + hb->height / 2, pb->height / 2 - 12);

    ecs_fini(world);
}

void Widgets_list_keeps_width(void) {
    // ui/widgets
    ecs_world_t *world = widgets_scene(
        "using box.widgets\n"
        "using box.layout\n"
        "using box.transform\n"
        "column {\n"
        "  Position: {0, 0, 0}\n"
        "  Layout: {direction: Column}\n"
        "  LayoutItem: {width: {mode: Fixed, value: 300}}\n"
        "  ListView list(items: [\"a\", \"b\", \"c\", \"d\", \"e\", \"f\", \"g\", \"h\"], height: 100)\n"
        "}\n");
    widgets_frames(world);

    /* The list scrolls and shows its bar, which the layout does not measure into the content:
     * the list keeps the width of its column, frame after frame */
    ecs_entity_t list = ecs_lookup(world, "column.list");
    test_assert(list != 0);
    test_assert(ecs_get(world, list, LayoutContent)->max_y > 0);
    test_flt(ecs_get(world, list, LayoutBounds)->width, 300);
    test_flt(ecs_get(world, ecs_lookup(world, "column"), LayoutBounds)->width, 300);
    widgets_frames(world);
    widgets_frames(world);
    test_flt(ecs_get(world, list, LayoutBounds)->width, 300);
    test_flt(ecs_get(world, list, LayoutContent)->max_x, 0);
    ecs_entity_t bar = ecs_lookup(world, "column.list.scrollbars.vscroll");
    test_assert(bar != 0);

    /* The bar sits one point inside the right edge, the rows left of it */
    float at[2];
    widgets_offset(world, bar, list, at);
    test_flt(at[0], 150 - 1 - 5);
    test_flt(ecs_get(world, ecs_lookup(world, "column.list.rows"), LayoutBounds)->width,
        300 - 1 - 1 - 10);

    char *logs = widgets_warnings(world);
    test_str(logs, "");
    ecs_os_free(logs);
    ecs_fini(world);
}

void Widgets_scroll_content_added_after_bars(void) {
    // ui/widgets
    ecs_world_t *world = widgets_scene(WIDGETS_TALL_VIEW);
    widgets_frames(world);
    ecs_entity_t view = ecs_lookup(world, "view");
    ecs_entity_t bars = ecs_lookup(world, "view.scrollbars");
    ecs_entity_t bar = ecs_lookup(world, "view.scrollbars.vscroll");
    test_assert(view != 0 && bars != 0 && bar != 0);

    /* Content added after the bars show comes after them among the children */
    test_assert(ecs_script_run(world, "more",
        "using box.layout\n"
        "view {\n"
        "  for i in 6..10 {\n"
        "    \"b{i}\" {\n"
        "      LayoutItem: {width: {mode: Fixed, value: 50}, height: {mode: Fixed, value: 40}}\n"
        "    }\n"
        "  }\n"
        "}\n", NULL) == 0);
    widgets_frames(world);
    ecs_entity_t last = ecs_lookup(world, "view.b9");
    test_assert(last != 0);
    test_assert(ecs_lookup(world, "view.scrollbars") == bars);
    test_assert(ecs_lookup(world, "view.scrollbars.vscroll") == bar);
    ecs_entities_t children = ecs_get_ordered_children(world, view);
    test_int(children.count, 11);
    test_assert(children.ids[children.count - 1] == last);

    /* The column lays out the ten blocks without a gap for the bars */
    const LayoutContent *c = ecs_get(world, view, LayoutContent);
    test_flt(c->height, 10 * 40 + 9 * 8 + 16);
    float first[2], end[2];
    widgets_offset(world, ecs_lookup(world, "view.b0"), view, first);
    widgets_offset(world, last, view, end);
    test_flt(first[1] - end[1], 9 * 48);

    /* The bar stays at the right edge of the view, the thumb at the top of the track, shorter
     * for the longer content */
    float at[2];
    widgets_offset(world, bar, view, at);
    test_flt(at[0], 100 - 1 - 5);
    test_flt(at[1], 0);
    test_flt(ecs_get(world, bar, Rect)->height, 98);
    ecs_entity_t knob = ecs_lookup(world, "view.scrollbars.vscroll.knob");
    test_assert(knob != 0);
    const Rect *k = ecs_get(world, knob, Rect);
    float thumb[2];
    widgets_offset(world, knob, view, thumb);
    test_flt(thumb[1] + k->height / 2, 50 - 1 - 2);
    test_flt(k->height + 4, 98 * 100 / c->height);

    /* It draws and picks over every block, the ones added after it included */
    int32_t i;
    for (i = 0; i < children.count; i ++) {
        if (children.ids[i] != bars) {
            test_bool(box_pick_drawn_after(world, bar, children.ids[i]), true);
            test_bool(box_pick_drawn_after(world, knob, children.ids[i]), true);
        }
    }

    /* Scrolled to the end it stays where it is, the thumb at the end of the track */
    widgets_set_mut(world, "view", "box.widgets.ScrollView.mut", "y", 1000);
    widgets_frames(world);
    c = ecs_get(world, view, LayoutContent);
    test_flt(c->y, c->max_y);
    widgets_offset(world, bar, view, at);
    test_flt(at[0], 100 - 1 - 5);
    test_flt(at[1], 0);
    widgets_offset(world, knob, view, thumb);
    k = ecs_get(world, knob, Rect);
    test_flt(thumb[1] - k->height / 2, -50 + 1 + 2);
    widgets_offset(world, last, view, end);
    test_flt(end[1] - 20, -50 + 8);

    char *logs = widgets_warnings(world);
    test_str(logs, "");
    ecs_os_free(logs);
    ecs_fini(world);
}

/** A screen UI with a HeadlessButton that taps actions.play and one without an action, which
 * reports a change, and a probe that counts the on.change of the second, its on.activate events
 * and those ui.activation counts.
 *
 * Two disabled buttons, one with the action, the other watched by a second probe.
 *
 * On screen go is at [440, 260], plain at [840, 260], stop at [440, 460], off at [840, 460]. */
#define WIDGETS_BUTTONS \
    "using flecs.script\n" \
    "using box.widgets\n" \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.input\n" \
    "using box.ui\n" \
    "actions {\n" \
    "  play { Action: {} }\n" \
    "}\n" \
    "template ButtonProbe {\n" \
    "  prop target: entity = 0\n" \
    "  mut changes: i32 = 0\n" \
    "  mut raw: i32 = 0\n" \
    "  mut activations: i32 = 0\n" \
    "  async {\n" \
    "    while true {\n" \
    "      await on.change(target)\n" \
    "      changes = changes + 1\n" \
    "    }\n" \
    "  }\n" \
    "  async {\n" \
    "    while true {\n" \
    "      const e = await on.activate(target)\n" \
    "      raw = raw + 1\n" \
    "      if ui.activation(e) == target {\n" \
    "        activations = activations + 1\n" \
    "      }\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "hud {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "  HeadlessButton go(action: actions.play) {\n" \
    "    Position: {-200, 100, 1}\n" \
    "    Rect: {100, 40}\n" \
    "  }\n" \
    "  HeadlessButton plain() {\n" \
    "    Position: {200, 100, 1}\n" \
    "    Rect: {100, 40}\n" \
    "  }\n" \
    "  HeadlessButton stop(action: actions.play, disabled: true) {\n" \
    "    Position: {-200, -100, 1}\n" \
    "    Rect: {100, 40}\n" \
    "  }\n" \
    "  HeadlessButton off(disabled: true) {\n" \
    "    Position: {200, -100, 1}\n" \
    "    Rect: {100, 40}\n" \
    "  }\n" \
    "}\n" \
    "hud_view {\n" \
    "  View: {camera: hud.camera}\n" \
    "}\n" \
    "probe { ButtonProbe: {target: hud.plain} }\n" \
    "probe_off { ButtonProbe: {target: hud.off} }\n"

/** The muts of ButtonProbe */
typedef struct {
    int32_t changes;     /**< Times on.change of the button resolved */
    int32_t raw;         /**< Every on.activate event of flecs on the button */
    int32_t activations; /**< Those ui.activation counts as an activation of the button */
} widgets_button_t;

/** Run a terminal command and frames until the input queue is drained, counting the frames in
 * which actions.play was pressed.
 *
 * @param world The world
 * @param s The terminal session
 * @param cmd The command
 * @return Frames in which actions.play was pressed
 */
static int32_t widgets_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd)
{
    ecs_entity_t play = ecs_lookup(world, "actions.play");
    bool error = true;
    int32_t i, result = 0;
    char *out = box_command_run(world, s, cmd, &error);
    if (error) {
        printf("command '%s' failed: %s\n", cmd, out);
    }

    test_bool(error, false);
    ecs_os_free(out);
    for (i = 0; i < 6 || box_input_pending(world); i ++) {
        ecs_progress(world, 0);
        result += ecs_get(world, play, ActionState)->pressed;
    }

    return result;
}

/** Get the muts of a ButtonProbe.
 *
 * @param world The world
 * @param name Name of the probe
 * @return The muts
 */
static const widgets_button_t* widgets_probe(
    ecs_world_t *world,
    const char *name)
{
    const widgets_button_t *result = ecs_get_id(world, ecs_lookup(world, name),
        ecs_lookup(world, "ButtonProbe.mut"));
    test_assert(result != NULL);

    return result;
}

/** Get the muts of the ButtonProbe of plain.
 *
 * @param world The world
 * @return The muts
 */
static const widgets_button_t* widgets_button(
    ecs_world_t *world)
{
    return widgets_probe(world, "probe");
}

void Widgets_shell_click_taps_button_action(void) {
    // ui/widgets
    ecs_world_t *world = widgets_scene(WIDGETS_BUTTONS);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    widgets_frames(world);

    // A real click taps the action
    test_int(widgets_run(world, s, "mouse_click left [440, 260]"), 1);

    // The click command of the shell on the button does too, a right click does not
    test_int(widgets_run(world, s, "click hud.go"), 1);
    test_int(widgets_run(world, s, "click hud.go right"), 0);

    ecs_fini(world);
}

void Widgets_shell_click_changes_button(void) {
    // ui/widgets
    ecs_world_t *world = widgets_scene(WIDGETS_BUTTONS);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    widgets_frames(world);
    test_int(widgets_button(world)->changes, 0);

    // flecs delivers on.activate for the click command of the shell, which ui.activation counts
    // and the button reports as a change
    widgets_run(world, s, "click hud.plain");
    test_int(widgets_button(world)->raw, 1);
    test_int(widgets_button(world)->activations, 1);
    test_int(widgets_button(world)->changes, 1);

    // As for a real click
    widgets_run(world, s, "mouse_click left [840, 260]");
    test_int(widgets_button(world)->raw, 2);
    test_int(widgets_button(world)->activations, 2);
    test_int(widgets_button(world)->changes, 2);

    // Also while the real pointer is elsewhere
    widgets_run(world, s, "mouse_move [1200, 50]");
    widgets_run(world, s, "click hud.plain");
    test_int(widgets_button(world)->raw, 3);
    test_int(widgets_button(world)->activations, 3);
    test_int(widgets_button(world)->changes, 3);

    // A right click does not activate it
    widgets_run(world, s, "click hud.plain right");
    test_int(widgets_button(world)->activations, 3);
    test_int(widgets_button(world)->changes, 3);

    ecs_fini(world);
}

void Widgets_shell_press_release_button(void) {
    // ui/widgets
    ecs_world_t *world = widgets_scene(WIDGETS_BUTTONS);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    widgets_frames(world);

    // release after press on the button activates it as click does
    widgets_run(world, s, "press hud.plain");
    test_int(widgets_button(world)->changes, 0);
    widgets_run(world, s, "release");
    test_int(widgets_button(world)->raw, 1);
    test_int(widgets_button(world)->activations, 1);
    test_int(widgets_button(world)->changes, 1);

    // and taps the action of a button with one
    test_int(widgets_run(world, s, "press hud.go"), 0);
    test_int(widgets_run(world, s, "release"), 1);

    // A right press and release does not
    widgets_run(world, s, "press hud.plain right");
    widgets_run(world, s, "release");
    test_int(widgets_button(world)->activations, 1);
    test_int(widgets_button(world)->changes, 1);
    widgets_run(world, s, "press hud.go right");
    test_int(widgets_run(world, s, "release"), 0);

    ecs_fini(world);
}

void Widgets_shell_click_disabled_button(void) {
    // ui/widgets
    ecs_world_t *world = widgets_scene(WIDGETS_BUTTONS);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    widgets_frames(world);

    // A disabled button does not report a change for the click command of the shell
    widgets_run(world, s, "click hud.off");
    test_int(widgets_probe(world, "probe_off")->raw, 1);
    test_int(widgets_probe(world, "probe_off")->activations, 0);
    test_int(widgets_probe(world, "probe_off")->changes, 0);

    // nor taps its action
    test_int(widgets_run(world, s, "click hud.stop"), 0);

    // As for a real click
    test_int(widgets_run(world, s, "mouse_click left [440, 460]"), 0);
    widgets_run(world, s, "mouse_click left [840, 460]");
    test_int(widgets_probe(world, "probe_off")->changes, 0);

    // The enabled buttons still activate
    test_int(widgets_run(world, s, "click hud.go"), 1);
    widgets_run(world, s, "click hud.plain");
    test_int(widgets_button(world)->changes, 1);

    ecs_fini(world);
}
