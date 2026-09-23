/**
 * @file Tree.c
 * @brief Tests for the tree model and the rows of the tree panel.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/editor/editor_internal.h"

static ecs_world_t *w;

static void Tree_noop(ecs_iter_t *it)
{
    (void)it;
}

void Tree_setup(void)
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
    ecs_entity_t s2 = ecs_script(w, { .code =
        "using box.transform\n"
        "using box.geometry\n"
        "template Tree {\n"
        "  trunk {\n"
        "    Position: {0, 1, 0}\n"
        "    Box: {1, 2, 1}\n"
        "  }\n"
        "}\n"
        "prefab Crate {\n"
        "  Box: {1, 1, 1}\n"
        "}\n"
        "scene {\n"
        "  Tree t1() {\n"
        "    Position: {-4, 0, 0}\n"
        "  }\n"
        "  crate : Crate {\n"
        "    Position: {0, -2, 0}\n"
        "  }\n"
        "  rock {\n"
        "    Position: {0, 0, 0}\n"
        "    Sphere: {radius: 1}\n"
        "  }\n"
        "}\n" });
    test_assert(s2 != 0);
    box_editor_enable(w, true);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
}

void Tree_teardown(void)
{
    ecs_fini(w);
}

void Tree_rows_list_the_root_entities_by_name(void)
{
    // editor/module
    const EditorTree *tree = ecs_singleton_get(w, EditorTree);
    int32_t i, box_at = -1, cam_at = -1, scene_at = -1, count = ecs_vec_count(&tree->rows);
    test_assert(count > 3);
    for (i = 0; i < count; i ++) {
        const EditorNode *n = ecs_vec_get_t(&tree->rows, EditorNode, i);
        test_int(n->depth, 0);
        if (!strcmp(n->name, "box")) {
            box_at = i;
        } else if (!strcmp(n->name, "cam")) {
            cam_at = i;
        } else if (!strcmp(n->name, "scene")) {
            scene_at = i;
        }
    }

    test_assert(box_at >= 0 && cam_at > box_at && scene_at > cam_at);
    test_uint(ecs_vec_get_t(&tree->rows, EditorNode, scene_at)->entity, ecs_lookup(w, "scene"));
    test_bool(ecs_vec_get_t(&tree->rows, EditorNode, scene_at)->children, true);
    test_bool(ecs_vec_get_t(&tree->rows, EditorNode, scene_at)->expanded, false);
    test_bool(ecs_vec_get_t(&tree->rows, EditorNode, cam_at)->children, false);
}

void Tree_collapsed_entity_hides_its_children(void)
{
    // editor/module
    const EditorTree *tree = ecs_singleton_get(w, EditorTree);
    ecs_entity_t rock = ecs_lookup(w, "scene.rock");
    int32_t i, count = ecs_vec_count(&tree->rows);
    for (i = 0; i < count; i ++) {
        test_assert(ecs_vec_get_t(&tree->rows, EditorNode, i)->entity != rock);
    }
}

void Tree_expand_shows_the_children_below_the_parent(void)
{
    // editor/module
    box_editor_expand(w, ecs_lookup(w, "scene"), true);
    ecs_progress(w, 0);
    const EditorTree *tree = ecs_singleton_get(w, EditorTree);
    int32_t i, count = ecs_vec_count(&tree->rows), scene_at = -1;
    for (i = 0; i < count; i ++) {
        if (ecs_vec_get_t(&tree->rows, EditorNode, i)->entity == ecs_lookup(w, "scene")) {
            scene_at = i;
        }
    }

    test_assert(scene_at >= 0 && scene_at + 3 < count);
    test_bool(ecs_vec_get_t(&tree->rows, EditorNode, scene_at)->expanded, true);
    test_str(ecs_vec_get_t(&tree->rows, EditorNode, scene_at + 1)->name, "crate");
    test_str(ecs_vec_get_t(&tree->rows, EditorNode, scene_at + 2)->name, "rock");
    test_str(ecs_vec_get_t(&tree->rows, EditorNode, scene_at + 3)->name, "t1");
    test_int(ecs_vec_get_t(&tree->rows, EditorNode, scene_at + 1)->depth, 1);
    test_int(ecs_vec_get_t(&tree->rows, EditorNode, scene_at + 3)->depth, 1);

    box_editor_expand(w, ecs_lookup(w, "scene"), false);
    ecs_progress(w, 0);
    tree = ecs_singleton_get(w, EditorTree);
    count = ecs_vec_count(&tree->rows);
    for (i = 0; i < count; i ++) {
        test_assert(ecs_vec_get_t(&tree->rows, EditorNode, i)->entity !=
            ecs_lookup(w, "scene.rock"));
    }
}

void Tree_selecting_reveals_the_ancestors(void)
{
    // editor/module
    box_editor_select(w, ecs_lookup(w, "scene.t1.trunk"));
    ecs_progress(w, 0);
    const EditorTree *tree = ecs_singleton_get(w, EditorTree);
    int32_t i, count = ecs_vec_count(&tree->rows);
    bool seen = false;
    for (i = 0; i < count; i ++) {
        const EditorNode *n = ecs_vec_get_t(&tree->rows, EditorNode, i);
        if (n->entity == ecs_lookup(w, "scene.t1")) {
            seen = true;
            test_int(n->depth, 1);
            test_bool(n->selected, true);
            test_int((int)n->kind, (int)KindInstance);
        } else {
            test_bool(n->selected, false);
        }
    }

    test_bool(seen, true);
}

void Tree_kinds_are_detected(void)
{
    // editor/module
    ecs_entity_t query = ecs_query(w, { .entity = ecs_entity(w, { .name = "q" }),
        .terms = {{ .id = ecs_id(Position) }} })->entity;
    ecs_entity_t observer = ecs_observer(w, { .entity = ecs_entity(w, { .name = "o" }),
        .query.terms = {{ .id = ecs_id(Position) }}, .events = { EcsOnSet },
        .callback = Tree_noop });
    test_int((int)box_editor_kind(w, ecs_lookup(w, "box")), (int)KindModule);
    test_int((int)box_editor_kind(w, ecs_id(Position)), (int)KindComponent);
    test_int((int)box_editor_kind(w, Hidden), (int)KindTag);
    test_int((int)box_editor_kind(w, ecs_lookup(w, "Tree")), (int)KindTemplate);
    test_int((int)box_editor_kind(w, ecs_lookup(w, "Crate")), (int)KindPrefab);
    test_int((int)box_editor_kind(w, ecs_lookup(w, "scene.t1")), (int)KindInstance);
    test_int((int)box_editor_kind(w, ecs_lookup(w, "scene.crate")), (int)KindInstance);
    test_int((int)box_editor_kind(w, ecs_lookup(w, "scene.rock")), (int)KindEntity);
    test_int((int)box_editor_kind(w, ecs_lookup(w, "box.editor.BoxEditorUpdate")), (int)KindSystem);
    test_int((int)box_editor_kind(w, query), (int)KindQuery);
    test_int((int)box_editor_kind(w, observer), (int)KindObserver);
}

void Tree_nameless_engine_entities_are_skipped(void)
{
    // editor/module
    ecs_query_t *q = ecs_query(w, { .terms = {{ .id = ecs_id(Position) }} });
    ecs_entity_t named = ecs_query(w, { .entity = ecs_entity(w, { .name = "named_query" }),
        .terms = {{ .id = ecs_id(Position) }} })->entity;
    ecs_entity_t plain = ecs_new(w);
    ecs_set(w, plain, Position, {1, 2, 3});
    box_editor_expand(w, 0, true);
    ecs_progress(w, 0);
    const EditorTree *tree = ecs_singleton_get(w, EditorTree);
    int32_t i, count = ecs_vec_count(&tree->rows);
    bool saw_named = false, saw_plain = false;
    for (i = 0; i < count; i ++) {
        const EditorNode *n = ecs_vec_get_t(&tree->rows, EditorNode, i);
        test_assert(n->entity != q->entity);
        saw_named = saw_named || n->entity == named;
        saw_plain = saw_plain || n->entity == plain;
    }

    test_bool(saw_named, true);
    test_bool(saw_plain, true);
}

void Tree_rows_are_widgets_in_model_order(void)
{
    // editor/ui
    box_editor_expand(w, ecs_lookup(w, "scene"), true);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t rows = ecs_lookup(w, "box.editor.ui.overlay.tree_panel.rows");
    test_assert(rows != 0);
    const EditorTree *tree = ecs_singleton_get(w, EditorTree);
    int32_t i, count = ecs_vec_count(&tree->rows);
    float last_y = 1e9f;
    for (i = 0; i < count; i ++) {
        const EditorNode *n = ecs_vec_get_t(&tree->rows, EditorNode, i);
        char name[32];
        snprintf(name, sizeof(name), "n_%d", (int)n->id);
        ecs_entity_t row = ecs_lookup_child(w, rows, name);
        test_assert(row != 0);
        test_int(ecs_get(w, row, Order)->value, i);
        const Bounds *b = ecs_get(w, row, Bounds);
        test_assert(b != NULL);
        test_assert(b->y < last_y);
        last_y = b->y;
    }
}

void Tree_clicking_a_row_selects_its_entity(void)
{
    // editor/ui
    box_editor_expand(w, ecs_lookup(w, "scene"), true);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t rock = ecs_lookup(w, "scene.rock");
    char name[32];
    snprintf(name, sizeof(name), "n_%u", (uint32_t)rock);
    ecs_entity_t rows = ecs_lookup(w, "box.editor.ui.overlay.tree_panel.rows");
    ecs_entity_t row = ecs_lookup_child(w, rows, name);
    test_assert(row != 0);
    const Bounds *b = ecs_get(w, row, Bounds);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640 + b->x, 360 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, rock);
}

void Tree_clicking_the_arrow_expands_the_row(void)
{
    // editor/ui
    ecs_entity_t scene = ecs_lookup(w, "scene");
    char name[32];
    snprintf(name, sizeof(name), "n_%u", (uint32_t)scene);
    ecs_entity_t rows = ecs_lookup(w, "box.editor.ui.overlay.tree_panel.rows");
    ecs_entity_t row = ecs_lookup_child(w, rows, name);
    test_assert(row != 0);
    const Bounds *b = ecs_get(w, row, Bounds);
    float x = 640 + b->x - b->width / 2 + 14;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {x, 360 - b->y} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, 0);
    const EditorTree *tree = ecs_singleton_get(w, EditorTree);
    int32_t i, count = ecs_vec_count(&tree->rows);
    bool seen = false;
    for (i = 0; i < count; i ++) {
        seen = seen ||
            ecs_vec_get_t(&tree->rows, EditorNode, i)->entity == ecs_lookup(w, "scene.rock");
    }

    test_bool(seen, true);
}

void Tree_tree_follows_a_scene_that_changed(void)
{
    // editor/module
    int32_t version = ecs_singleton_get(w, EditorTree)->version;
    ecs_entity_t zebra = ecs_entity(w, { .name = "zebra" });
    int32_t i;
    for (i = 0; i < BOX_EDITOR_TREE_INTERVAL + 1; i ++) {
        ecs_progress(w, 0);
    }

    const EditorTree *tree = ecs_singleton_get(w, EditorTree);
    test_assert(tree->version > version);
    bool seen = false;
    int32_t count = ecs_vec_count(&tree->rows);
    for (i = 0; i < count; i ++) {
        seen = seen || ecs_vec_get_t(&tree->rows, EditorNode, i)->entity == zebra;
    }

    test_bool(seen, true);
}

void Tree_variables_of_flecs_core_have_no_children(void)
{
    // editor/module
    box_editor_expand(w, ecs_lookup(w, "flecs"), true);
    box_editor_expand(w, ecs_lookup(w, "flecs.core"), true);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    const EditorTree *tree = ecs_singleton_get(w, EditorTree);
    int32_t i, count = ecs_vec_count(&tree->rows);
    bool seen = false;
    for (i = 0; i < count; i ++) {
        const EditorNode *n = ecs_vec_get_t(&tree->rows, EditorNode, i);
        if (n->entity == EcsWildcard || n->entity == EcsAny || n->entity == EcsThis) {
            test_bool(n->children, false);
            seen = true;
        }
    }

    test_bool(seen, true);
    box_editor_expand(w, EcsWildcard, true);
    ecs_progress(w, 0);
}

void Tree_rows_scroll_with_the_wheel(void)
{
    // editor/ui
    int32_t i;
    for (i = 0; i < 60; i ++) {
        char name[16];
        snprintf(name, sizeof(name), "row_%02d", i);
        ecs_entity(w, { .name = name });
    }

    box_editor_expand(w, 0, true);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t rows = ecs_lookup(w, "box.editor.ui.overlay.tree_panel.rows");
    const Bounds *b = ecs_get(w, rows, Bounds);
    test_assert(b != NULL);
    test_assert(b->height <= 720);
    const Scroll *scroll = ecs_get(w, rows, Scroll);
    test_assert(scroll != NULL);
    test_flt(scroll->offset.y, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {130, 400} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputScroll, .value = {0, -3} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    float moved = ecs_get(w, rows, Scroll)->offset.y;
    if (moved == 0) {
        box_input_inject(w, &(InputEvent){ .kind = InputScroll, .value = {0, 3} });
        ecs_progress(w, 0);
        ecs_progress(w, 0);
        moved = ecs_get(w, rows, Scroll)->offset.y;
    }

    test_assert(moved != 0);
}
