/**
 * @file Inspector.c
 * @brief Tests for the property grid model, assignment through it and its widgets.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/editor/editor_internal.h"

static ecs_world_t *w;
static ecs_entity_t rock;
static ecs_entity_t road;

void Inspector_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 1280;
    s->height = 720;
    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_entity(w, { .name = "main" });
    ecs_set(w, view, View, { .camera = cam, .enabled = true });
    ecs_entity_t scene = ecs_entity(w, { .name = "scene" });
    road = ecs_entity(w, { .name = "road", .parent = scene });
    Spline *spline = ecs_ensure(w, road, Spline);
    ecs_vec_init_t(NULL, &spline->points, vec3, 0);
    *ecs_vec_append_t(NULL, &spline->points, vec3) = vec3_new(0, 0, 0);
    *ecs_vec_append_t(NULL, &spline->points, vec3) = vec3_new(1, 0, 0);
    *ecs_vec_append_t(NULL, &spline->points, vec3) = vec3_new(2, 0, 1);
    spline->tension = 0.5f;
    ecs_modified(w, road, Spline);
    rock = ecs_entity(w, { .name = "rock", .parent = scene });
    ecs_set(w, rock, Position, {0, 0, 0});
    ecs_set(w, rock, Sphere, { .segments = 12, .smooth = false, .radius = 1 });
    ecs_set(w, rock, Font, { .path = ecs_os_strdup("a.ttf"), .size = 12 });
    ecs_set(w, rock, WorldAnchor, { .entity = road, .offset = {1, 2, 3} });
    box_editor_enable(w, true);
    ecs_progress(w, 0);
}

void Inspector_teardown(void)
{
    ecs_fini(w);
}

void Inspector_nothing_selected_gives_no_fields(void)
{
    // editor/module
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    test_uint(insp->entity, 0);
    test_int(ecs_vec_count(&insp->fields), 0);
}

void Inspector_fields_list_the_components_of_the_selection(void)
{
    // editor/module
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    test_uint(insp->entity, rock);
    test_str(insp->name, "scene.rock");
    int32_t i, count = ecs_vec_count(&insp->fields);
    bool position = false, sphere = false;
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->kind == FieldHeader && !strcmp(f->label, "box.transform.Position")) {
            position = true;
            test_uint(f->component, ecs_id(Position));
            test_bool(f->editable, true);
        }

        if (f->kind == FieldHeader && !strcmp(f->label, "box.geometry.Sphere")) {
            sphere = true;
        }
    }

    test_bool(position, true);
    test_bool(sphere, true);
}

void Inspector_members_get_a_widget_by_type(void)
{
    // editor/module
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    int32_t i, count = ecs_vec_count(&insp->fields), seen = 0;
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->component == ecs_id(Sphere) && !strcmp(f->member, "segments")) {
            test_int((int)f->kind, (int)FieldNumber);
            test_str(f->value, "12");
            seen ++;
        } else if (f->component == ecs_id(Sphere) && !strcmp(f->member, "smooth")) {
            test_int((int)f->kind, (int)FieldBool);
            test_str(f->value, "false");
            seen ++;
        } else if (f->component == ecs_id(Sphere) && !strcmp(f->member, "radius")) {
            test_int((int)f->kind, (int)FieldNumber);
            test_str(f->value, "1");
            seen ++;
        } else if (f->component == ecs_id(Font) && !strcmp(f->member, "path")) {
            test_int((int)f->kind, (int)FieldText);
            test_str(f->value, "a.ttf");
            seen ++;
        } else if (f->component == ecs_id(WorldAnchor) && !strcmp(f->member, "entity")) {
            test_int((int)f->kind, (int)FieldEntity);
            test_str(f->value, "scene.road");
            seen ++;
        }
    }

    test_int(seen, 5);
}

void Inspector_nested_struct_members_are_flattened(void)
{
    // editor/module
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    int32_t i, count = ecs_vec_count(&insp->fields), seen = 0;
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->component == ecs_id(WorldAnchor) && !strcmp(f->member, "offset")) {
            test_int((int)f->kind, (int)FieldValue);
            test_bool(f->editable, false);
            seen ++;
        } else if (f->component == ecs_id(WorldAnchor) && !strcmp(f->member, "offset.y")) {
            test_int((int)f->kind, (int)FieldNumber);
            test_str(f->value, "2");
            test_bool(f->editable, true);
            seen ++;
        }
    }

    test_int(seen, 2);
}

void Inspector_enum_field_lists_its_constants(void)
{
    // editor/module
    box_editor_select(w, road);
    ecs_progress(w, 0);
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    int32_t i, count = ecs_vec_count(&insp->fields), seen = 0;
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->component == ecs_id(Spline) && !strcmp(f->member, "type")) {
            test_int((int)f->kind, (int)FieldEnum);
            test_int(ecs_vec_count(&f->options), 3);
            test_str(ecs_vec_get_t(&f->options, char*, 0)[0], "SplineCatmullRom");
            test_str(ecs_vec_get_t(&f->options, char*, 1)[0], "SplineLinear");
            test_str(ecs_vec_get_t(&f->options, char*, 2)[0], "SplineBezier");
            test_int(f->option, 0);
            test_str(f->value, "SplineCatmullRom");
            seen ++;
        } else if (f->component == ecs_id(Spline) && !strcmp(f->member, "points")) {
            test_int((int)f->kind, (int)FieldValue);
            test_bool(f->editable, false);
            seen ++;
        }
    }

    test_int(seen, 2);
}

void Inspector_engine_written_components_are_read_only(void)
{
    // editor/module
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    int32_t i, count = ecs_vec_count(&insp->fields), seen = 0;
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->component == ecs_id(WorldTransform)) {
            test_bool(f->editable, false);
            seen ++;
        }
    }

    test_assert(seen > 0);
}

void Inspector_tags_and_pairs_are_listed_by_name(void)
{
    // editor/module
    ecs_add_id(w, rock, Pickable);
    ecs_entity_t layer = ecs_entity(w, { .name = "layer_a" });
    ecs_add_pair(w, rock, Layer, layer);
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    int32_t i, count = ecs_vec_count(&insp->fields);
    bool tag = false, pair = false;
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->kind == FieldTag && !strcmp(f->label, "box.picking.Pickable")) {
            tag = true;
        }

        if (f->kind == FieldTag && strstr(f->label, "Layer") && strstr(f->label, "layer_a")) {
            pair = true;
        }
    }

    test_bool(tag, true);
    test_bool(pair, true);
}

void Inspector_fields_are_published_once_per_change(void)
{
    // editor/module
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    int32_t version = ecs_singleton_get(w, EditorInspector)->version;
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(w, 0);
    }

    test_int(ecs_singleton_get(w, EditorInspector)->version, version);
    ecs_set(w, rock, Sphere, { .segments = 12, .radius = 3 });
    ecs_progress(w, 0);
    test_int(ecs_singleton_get(w, EditorInspector)->version, version + 1);
}

void Inspector_assign_sets_a_number(void)
{
    // editor/module
    test_bool(box_editor_assign(w, rock, ecs_id(Sphere), "radius", "2.5"), true);
    test_flt(ecs_get(w, rock, Sphere)->radius, 2.5);
    test_bool(box_editor_assign(w, rock, ecs_id(Sphere), "segments", "7"), true);
    test_int(ecs_get(w, rock, Sphere)->segments, 7);
}

void Inspector_assign_sets_an_enum_by_name(void)
{
    // editor/module
    test_bool(box_editor_assign(w, road, ecs_id(Spline), "type", "SplineLinear"), true);
    test_int((int)ecs_get(w, road, Spline)->type, (int)SplineLinear);
}

void Inspector_assign_sets_a_bool(void)
{
    // editor/module
    test_bool(box_editor_assign(w, rock, ecs_id(Sphere), "smooth", "true"), true);
    test_bool(ecs_get(w, rock, Sphere)->smooth, true);
    test_bool(box_editor_assign(w, rock, ecs_id(Sphere), "smooth", "false"), true);
    test_bool(ecs_get(w, rock, Sphere)->smooth, false);
}

void Inspector_assign_sets_a_string(void)
{
    // editor/module
    test_bool(box_editor_assign(w, rock, ecs_id(Font), "path", "b.ttf"), true);
    test_str(ecs_get(w, rock, Font)->path, "b.ttf");
}

void Inspector_assign_sets_an_entity_by_path(void)
{
    // editor/module
    test_bool(box_editor_assign(w, rock, ecs_id(WorldAnchor), "entity", "cam"), true);
    test_uint(ecs_get(w, rock, WorldAnchor)->entity, ecs_lookup(w, "cam"));
    test_bool(box_editor_assign(w, rock, ecs_id(WorldAnchor), "offset.z", "9"), true);
    test_flt(ecs_get(w, rock, WorldAnchor)->offset.z, 9);
}

void Inspector_assign_rejects_a_bad_value(void)
{
    // editor/module
    test_bool(box_editor_assign(w, rock, ecs_id(Sphere), "radius", "banana"), false);
    test_flt(ecs_get(w, rock, Sphere)->radius, 1);
    test_bool(box_editor_assign(w, rock, ecs_id(Sphere), "nope", "1"), false);
    test_bool(box_editor_assign(w, rock, ecs_id(Spline), "type", "SplineLinear"), false);
}

void Inspector_submitted_line_of_a_bound_input_is_assigned(void)
{
    // editor/module
    box_editor_select(w, rock);
    ecs_entity_t root = ecs_entity(w, { .name = "panel" });
    ecs_add(w, root, Ui);
    ecs_entity_t input = ecs_entity(w, { .name = "input", .parent = root });
    ecs_set(w, input, Layout, { .width = 100, .height = 20 });
    ecs_set(w, input, TextInput, { .value = ecs_os_strdup(""), .submitted = ecs_os_strdup("3.5") });
    ecs_set(w, input, EditorBinding, {
        .component = ecs_id(Sphere), .member = ecs_os_strdup("radius") });
    ecs_progress(w, 0);
    test_flt(ecs_get(w, rock, Sphere)->radius, 3.5);
    const TextInput *ti = ecs_get(w, input, TextInput);
    test_str(ti->submitted, "");
    test_str(ti->value, "3.5");
    test_bool(ti->focused, false);
}

void Inspector_focused_field_captures_the_input_maps(void)
{
    // editor/module
    ecs_entity_t map = ecs_lookup(w, "box.input.map");
    test_assert(map != 0);
    test_bool(ecs_get(w, map, InputMap)->enabled, true);
    ecs_entity_t root = ecs_entity(w, { .name = "panel" });
    ecs_add(w, root, Ui);
    ecs_entity_t input = ecs_entity(w, { .name = "input", .parent = root });
    ecs_set(w, input, Layout, { .width = 100, .height = 20 });
    ecs_set(w, input, TextInput, { .value = ecs_os_strdup(""), .focused = true });
    ecs_set(w, input, EditorBinding, {
        .component = ecs_id(Sphere), .member = ecs_os_strdup("radius") });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, map, InputMap)->enabled, false);
    ecs_get_mut(w, input, TextInput)->focused = false;
    ecs_progress(w, 0);
    test_bool(ecs_get(w, map, InputMap)->enabled, true);
}

void Inspector_grid_rows_are_widgets(void)
{
    // editor/ui
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t fields = ecs_lookup(w, "box.editor.ui.overlay.inspector.fields");
    test_assert(fields != 0);
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    int32_t i, count = ecs_vec_count(&insp->fields);
    test_assert(count > 0);
    for (i = 0; i < count; i ++) {
        char name[16];
        snprintf(name, sizeof(name), "f_%d", i);
        ecs_entity_t row = ecs_lookup_child(w, fields, name);
        test_assert(row != 0);
        test_assert(ecs_get(w, row, Bounds) != NULL);
    }

    test_assert(ecs_lookup(w, "box.editor.ui.overlay.inspector.header.title") != 0);
    test_str(ecs_get(w, ecs_lookup(w, "box.editor.ui.overlay.inspector.header.title"), Text)->value,
        "scene.rock");
}

void Inspector_clicking_a_checkbox_flips_the_member(void)
{
    // editor/ui
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    int32_t i, count = ecs_vec_count(&insp->fields), at = -1;
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->component == ecs_id(Sphere) && !strcmp(f->member, "smooth")) {
            at = i;
        }
    }

    test_assert(at >= 0);
    char name[16];
    snprintf(name, sizeof(name), "f_%d", at);
    ecs_entity_t rows = ecs_lookup(w, "box.editor.ui.overlay.inspector.fields");
    ecs_entity_t row = ecs_lookup_child(w, rows, name);
    ecs_entity_t box = ecs_lookup_child(w, row, "box");
    test_assert(box != 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {1100, 300} });
    ecs_progress(w, 0);
    float direction = -3;
    int32_t turns = 0;
    while (ecs_get(w, box, Bounds)->y < -330 && turns < 40) {
        float before = ecs_get(w, box, Bounds)->y;
        box_input_inject(w, &(InputEvent){ .kind = InputScroll, .value = {0, direction} });
        ecs_progress(w, 0);
        ecs_progress(w, 0);
        if (ecs_get(w, box, Bounds)->y < before) {
            direction = -direction;
        }

        turns ++;
    }

    const Bounds *b = ecs_get(w, box, Bounds);
    test_assert(b->y >= -330);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640 + b->x, 360 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_bool(ecs_get(w, rock, Sphere)->smooth, true);
}

void Inspector_collapsing_a_component_hides_its_members(void)
{
    // editor/ui
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    box_editor_collapse(w, ecs_id(Sphere), true);
    ecs_progress(w, 0);
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    int32_t i, count = ecs_vec_count(&insp->fields), members = 0, headers = 0;
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->component != ecs_id(Sphere)) {
            continue;
        }

        if (f->kind == FieldHeader) {
            test_bool(f->expanded, false);
            headers ++;
        } else {
            members ++;
        }
    }

    test_int(headers, 1);
    test_int(members, 0);
    box_editor_collapse(w, ecs_id(Sphere), false);
    ecs_progress(w, 0);
    insp = ecs_singleton_get(w, EditorInspector);
    count = ecs_vec_count(&insp->fields);
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->component == ecs_id(Sphere) && f->kind != FieldHeader) {
            members ++;
        }
    }

    test_int(members, 3);
}

void Inspector_clicking_a_header_collapses_its_component(void)
{
    // editor/ui
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    int32_t i, count = ecs_vec_count(&insp->fields), at = -1;
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->component == ecs_id(Sphere) && f->kind == FieldHeader) {
            at = i;
        }
    }

    test_assert(at >= 0);
    char name[16];
    snprintf(name, sizeof(name), "f_%d", at);
    ecs_entity_t fields = ecs_lookup(w, "box.editor.ui.overlay.inspector.fields");
    ecs_entity_t row = ecs_lookup_child(w, fields, name);
    const Bounds *b = ecs_get(w, row, Bounds);
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {640 + b->x, 360 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    insp = ecs_singleton_get(w, EditorInspector);
    count = ecs_vec_count(&insp->fields);
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->component == ecs_id(Sphere)) {
            test_int((int)f->kind, (int)FieldHeader);
            test_bool(f->expanded, false);
        }
    }
}

void Inspector_fields_scroll_with_the_wheel(void)
{
    // editor/ui
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t fields = ecs_lookup(w, "box.editor.ui.overlay.inspector.fields");
    const Bounds *b = ecs_get(w, fields, Bounds);
    test_assert(b != NULL);
    test_assert(b->height <= 720);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {1100, 400} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputScroll, .value = {0, -3} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    float moved = ecs_get(w, fields, Scroll)->offset.y;
    if (moved == 0) {
        box_input_inject(w, &(InputEvent){ .kind = InputScroll, .value = {0, 3} });
        ecs_progress(w, 0);
        ecs_progress(w, 0);
        moved = ecs_get(w, fields, Scroll)->offset.y;
    }

    test_assert(moved != 0);
}

void Inspector_hovering_rows_after_a_collapse(void)
{
    // editor/ui
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t fields = ecs_lookup(w, "box.editor.ui.overlay.inspector.fields");
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    int32_t i, count = ecs_vec_count(&insp->fields), at = -1;
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        if (f->component == ecs_id(Position) && f->kind == FieldHeader) {
            at = i;
        }
    }

    test_assert(at >= 0);
    int32_t round;
    for (round = 0; round < 3; round ++) {
        char name[16];
        snprintf(name, sizeof(name), "f_%d", at);
        ecs_entity_t row = ecs_lookup_child(w, fields, name);
        const Bounds *b = ecs_get(w, row, Bounds);
        box_input_inject(w, &(InputEvent){
            .kind = InputMouseMove, .value = {640 + b->x, 360 - b->y} });
        ecs_progress(w, 0);
        box_input_inject(w, &(InputEvent){
            .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
        ecs_progress(w, 0);
        ecs_progress(w, 0);
        ecs_progress(w, 0);
        insp = ecs_singleton_get(w, EditorInspector);
        count = ecs_vec_count(&insp->fields);
        for (i = 0; i < count; i ++) {
            snprintf(name, sizeof(name), "f_%d", i);
            row = ecs_lookup_child(w, fields, name);
            test_assert(row != 0);
            b = ecs_get(w, row, Bounds);
            test_assert(b != NULL);
            if (b->y < -360 || b->y > 360) {
                continue;
            }

            box_input_inject(w, &(InputEvent){
                .kind = InputMouseMove, .value = {640 + b->x, 360 - b->y} });
            ecs_progress(w, 0);
            ecs_progress(w, 0);
        }
    }
}

void Inspector_rows_keep_the_children_of_their_template(void)
{
    // editor/ui
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    box_editor_collapse(w, ecs_id(Position), true);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t fields = ecs_lookup(w, "box.editor.ui.overlay.inspector.fields");
    const EditorInspector *insp = ecs_singleton_get(w, EditorInspector);
    int32_t i, count = ecs_vec_count(&insp->fields);
    test_assert(count > 0);
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&insp->fields, EditorField, i);
        char name[16];
        snprintf(name, sizeof(name), "f_%d", i);
        ecs_entity_t row = ecs_lookup_child(w, fields, name);
        test_assert(row != 0);
        ecs_entity_t label = ecs_lookup_child(w, row, "name");
        test_assert(label != 0);
        test_str(ecs_get(w, label, Text)->value, f->label);
        if (f->kind == FieldHeader) {
            test_assert(ecs_lookup_child(w, row, "arrow") != 0);
        } else if (f->kind == FieldBool) {
            test_assert(ecs_lookup_child(w, row, "box") != 0);
        } else if (f->kind == FieldEnum) {
            test_assert(ecs_lookup_child(w, row, "box") != 0);
        } else if (f->kind == FieldValue) {
            test_assert(ecs_lookup_child(w, row, "value_text") != 0);
        } else if (f->kind != FieldTag) {
            test_assert(ecs_lookup_child(w, row, "input") != 0);
        }
    }
}
