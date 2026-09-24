/**
 * @file editor/editor.c
 * @brief Registers the box.editor module: its singleton, its models, its scripts and its system.
 *
 * The Editor singleton is switched by the editor command through
 * box_editor_enable. The update system returns at once while the editor is
 * off, so an editor that is off costs one flag test per frame.
 */

#define BOX_EDITOR_IMPL
#include "editor_internal.h"
#include "../shader/shader_internal.h"

ECS_COMPONENT_DECLARE(EditorState);

/** Editor constructor: off, the select tool, nothing selected and no point.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_editor_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Editor *editor = ptr;
    int32_t i;

    memset(ptr, 0, (size_t)count * sizeof(Editor));
    for (i = 0; i < count; i ++) {
        editor[i].point = -1;
    }
}

/** EditorState destructor: release the maps, the levels and the texts.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_editor_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    EditorState *state = ptr;
    int32_t i, l;
    for (i = 0; i < count; i ++) {
        for (l = 0; l < ecs_vec_count(&state[i].levels); l ++) {
            ecs_vec_fini_t(NULL, &ecs_vec_get_t(&state[i].levels,
                box_editor_level_t, l)->hidden, ecs_entity_t);
        }

        ecs_vec_fini_t(NULL, &state[i].levels, box_editor_level_t);
        ecs_vec_fini_t(NULL, &state[i].captured, ecs_entity_t);
        ecs_map_fini(&state[i].expanded);
        ecs_map_fini(&state[i].collapsed);
        if (state[i].roots) {
            ecs_query_fini(state[i].roots);
        }

        ecs_os_free(state[i].inspector_text);
        state[i].inspector_text = NULL;
    }
}

EditorState* box_editor_state(
    ecs_world_t *world)
{
    EditorState *result = NULL;
    if (ecs_id(EditorState) && !ecs_is_fini(world)) {
        result = ecs_get_mut(world, ecs_id(EditorState), EditorState);
    }

    return result;
}

Editor* box_editor_get(
    ecs_world_t *world)
{
    return ecs_get_mut(world, ecs_id(Editor), Editor);
}

void box_editor_modified(
    ecs_world_t *world)
{
    ecs_modified(world, ecs_id(Editor), Editor);
}

/** Load a script of the editor into a scope of the module.
 *
 * @param world The world
 * @param path Path of the script
 * @param name Name of the script entity
 * @return The script entity, 0 when the file is missing or fails
 */
static ecs_entity_t box_editor_load(
    ecs_world_t *world,
    const char *path,
    const char *name)
{
    char resolved[1024];
    ecs_entity_t result = 0;
    if (!box_locate(path, resolved, sizeof(resolved))) {
        ecs_warn("box.editor: no script at '%s'", path);
        goto done;
    }

    ecs_entity_t script = ecs_entity(world, {
        .name = name, .parent = ecs_lookup(world, "box.editor") });
    if (!ecs_script(world, { .entity = script, .filename = resolved })) {
        ecs_err("box.editor: failed to load '%s'", resolved);
        goto done;
    }

    result = script;
done:
    return result;
}

void box_editor_enable(
    ecs_world_t *world,
    bool enabled)
{
    EditorState *state = box_editor_state(world);
    state->tree_dirty = true;
    Editor *editor = box_editor_get(world);
    if (!enabled) {
        state->grab.active = false;
        box_editor_release_maps(world, state);
        box_editor_close_all(world);
        editor = box_editor_get(world);
        editor->selected = 0;
        editor->opened = 0;
        editor->point = -1;
        editor->instance = false;
        editor->spline = false;
        editor->transform = false;
        editor->depth = 0;
        editor->handle = 0;
    }

    editor->enabled = enabled;
    box_editor_modified(world);
}

/** Tell whether a text input has the keyboard focus.
 *
 * @param world The world
 * @return true when some TextInput is focused
 */
static bool box_editor_typing(
    ecs_world_t *world)
{
    bool result = false;
    ecs_iter_t it = ecs_each(world, TextInput);
    while (ecs_each_next(&it)) {
        TextInput *input = ecs_field(&it, TextInput, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            result = result || input[i].focused;
        }
    }

    return result;
}

/** Delete the selected entity when backspace is pressed outside a text input.
 *
 * @param world The world
 * @param in The input state of the frame
 */
static void box_editor_delete_key(
    ecs_world_t *world,
    const Input *in)
{
    const Editor *editor = ecs_singleton_get(world, Editor);
    ecs_entity_t e = editor->selected;
    if (!in || !in->keys[BoxKeyBackspace].pressed || !e || !ecs_is_alive(world, e)) {
        goto done;
    }

    EditorKind kind = box_editor_kind(world, e);
    if (kind != KindEntity && kind != KindInstance) {
        goto done;
    }

    if (box_editor_state(world)->grab.active || box_editor_typing(world)) {
        goto done;
    }

    ecs_delete(world, e);
done:
    return;
}

/** Refresh the models and draw the gizmos of the frame.
 *
 * @param it Iterator of the system, which has no terms
 */
static void BoxEditorUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    EditorState *state = box_editor_state(world);
    const Editor *editor = ecs_singleton_get(world, Editor);
    if (!editor->enabled) {
        goto done;
    }

    if (editor->selected && !ecs_is_alive(world, editor->selected)) {
        box_editor_select(world, 0);
        state = box_editor_state(world);
    }

    const Input *in = ecs_singleton_get(world, Input);
    box_editor_delete_key(world, in);
    state = box_editor_state(world);
    if (state->grab.active && in && !in->keys[BoxKeyMouseLeft].state) {
        box_editor_drop(world);
        state = box_editor_state(world);
    }

    box_editor_bindings_update(world, state);
    state = box_editor_state(world);
    box_editor_inspector_update(world, state);
    state = box_editor_state(world);
    box_editor_tree_update(world, state);
    state = box_editor_state(world);
    box_editor_gizmo_draw(world, state);
done:
    return;
}

void BoxEditorImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxEditor);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxRender);
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxCollision);
    ECS_IMPORT(world, BoxPicking);
    ECS_IMPORT(world, BoxSpline);
    ECS_IMPORT(world, BoxDraw);
    ECS_IMPORT(world, BoxUi);

    ECS_META_COMPONENT(world, EditorTool);
    ECS_META_COMPONENT(world, EditorKind);
    ECS_META_COMPONENT(world, EditorFieldKind);
    ECS_META_COMPONENT(world, Editor);
    ECS_META_COMPONENT(world, EditorNode);
    ECS_META_COMPONENT(world, EditorTree);
    ECS_META_COMPONENT(world, EditorField);
    ECS_META_COMPONENT(world, EditorInspector);
    ECS_META_COMPONENT(world, HandleMode);
    ECS_META_COMPONENT(world, GizmoHandle);
    ECS_META_COMPONENT(world, EditorBinding);
    ECS_COMPONENT_DEFINE(world, EditorState);

    ecs_entity_t options = ecs_lookup_child(world, ecs_id(EditorField), "options");
    const EcsMember *member = options ? ecs_get(world, options, EcsMember) : NULL;
    if (member && !ecs_lookup(world, "Strings")) {
        ecs_set_alias(world, member->type, "Strings");
    }

    ecs_set_hooks(world, Editor, { .ctor = box_editor_ctor });
    ecs_set_hooks(world, GizmoHandle, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, EditorState, {
        .ctor = flecs_default_ctor,
        .dtor = box_editor_state_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });

    ecs_add_pair(world, ecs_id(GizmoHandle), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(EditorBinding), EcsOnInstantiate, EcsDontInherit);

    EditorState *state = ecs_singleton_ensure(world, EditorState);
    ecs_map_init(&state->expanded, NULL);
    ecs_map_init(&state->collapsed, NULL);
    ecs_vec_init_t(NULL, &state->levels, box_editor_level_t, 0);
    ecs_vec_init_t(NULL, &state->captured, ecs_entity_t, 0);
    state->tree_dirty = true;
    state->roots = ecs_query(world, {
        .terms = {{ .id = ecs_pair(EcsChildOf, EcsWildcard), .oper = EcsNot }},
        .flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled
    });

    ecs_singleton_ensure(world, Editor);
    EditorTree *tree = ecs_singleton_ensure(world, EditorTree);
    ecs_vec_init_t(NULL, &tree->rows, EditorNode, 0);
    ecs_singleton_modified(world, EditorTree);
    EditorInspector *inspector = ecs_singleton_ensure(world, EditorInspector);
    ecs_vec_init_t(NULL, &inspector->fields, EditorField, 0);
    ecs_singleton_modified(world, EditorInspector);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxEditorUpdate" }),
        .phase = EcsOnUpdate,
        .callback = BoxEditorUpdate,
        .immediate = true
    });

    box_editor_functions_init(world, ecs_id(BoxEditor));
    state = box_editor_state(world);
    state->gizmos = box_editor_load(world, BOX_EDITOR_GIZMOS, "gizmos_script");
    state = box_editor_state(world);
    state->script = box_editor_load(world, BOX_EDITOR_SCRIPT, "ui_script");
    ecs_singleton_modified(world, Editor);
}
