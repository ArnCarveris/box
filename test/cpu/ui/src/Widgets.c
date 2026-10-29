/**
 * @file Widgets.c
 * @brief The widget library etc/ui/widgets.flecs: the box.widgets module and its templates, a
 * world without the library, the UiTheme of box.ui.theme, a script that assigns it and
 * ui.sibling.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *result = box_terminal_run(world, s, "logs warning 100", &error);
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

void Widgets_library_loads(void) {
    // ui/widgets
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *widgets = box_app_locate("etc/ui/widgets.flecs");
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
    test_assert(strstr(loaded->filename, "etc/ui/widgets.flecs") != NULL);

    int32_t scripts = 0;
    ecs_iter_t it = ecs_each_id(world, ecs_id(EcsScript));
    while (ecs_each_next(&it)) {
        const EcsScript *sc = ecs_field(&it, EcsScript, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (sc[i].filename && strstr(sc[i].filename, "etc/ui/")) {
                test_assert(sc[i].error == NULL);
                scripts ++;
            }
        }
    }

    test_int(scripts, 14);

    char *logs = widgets_warnings(world);
    test_str(logs, "");
    ecs_os_free(logs);

    ecs_fini(world);
}

void Widgets_templates_exist(void) {
    // ui/widgets
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *widgets = box_app_locate("etc/ui/widgets.flecs");
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
    char *widgets = box_app_locate("etc/ui/widgets.flecs");
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
    char *widgets = box_app_locate("etc/ui/widgets.flecs");
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
