/**
 * @file editor/inspect.c
 * @brief The inspector model: the fields of the selection, their assignment and the bound inputs.
 *
 * The fields are rebuilt every frame from the reflection data of the selected
 * entity and published only when their text changed.
 *
 * A bound text input that submitted a line assigns it through the meta cursor
 * of the component, in place and followed by modified, which a template prop
 * with a replace hook allows.
 */

#include "editor_internal.h"
#include <string.h>
#include <stdlib.h>

/** Levels of nested structs the grid flattens into rows */
#define BOX_EDITOR_NEST_MAX (3)

/** Release the strings of a field.
 *
 * @param field The field
 */
static void box_editor_field_fini(
    EditorField *field)
{
    int32_t i, count = ecs_vec_count(&field->options);
    for (i = 0; i < count; i ++) {
        ecs_os_free(ecs_vec_get_t(&field->options, char*, i)[0]);
    }

    ecs_vec_fini_t(NULL, &field->options, char*);
    ecs_os_free(field->member);
    ecs_os_free(field->label);
    ecs_os_free(field->value);
}

/** Release the fields of a model and empty it.
 *
 * @param inspector The model
 */
static void box_editor_fields_clear(
    EditorInspector *inspector)
{
    int32_t i, count = ecs_vec_count(&inspector->fields);
    for (i = 0; i < count; i ++) {
        box_editor_field_fini(ecs_vec_get_t(&inspector->fields, EditorField, i));
    }

    ecs_vec_clear(&inspector->fields);
    ecs_os_free(inspector->name);
    inspector->name = NULL;
}

/** Append a field to a model.
 *
 * @param inspector The model
 * @param component The component of the field
 * @param member Dotted member path, NULL for a header or a tag
 * @param label Text shown left of the widget
 * @param kind Widget of the field
 * @param value The value as text, NULL for none
 * @param editable Whether the value may be changed
 * @return The field
 */
static EditorField* box_editor_field(
    EditorInspector *inspector,
    ecs_entity_t component,
    const char *member,
    const char *label,
    EditorFieldKind kind,
    const char *value,
    bool editable)
{
    EditorField *field = ecs_vec_append_t(NULL, &inspector->fields, EditorField);
    field->component = component;
    field->member = ecs_os_strdup(member ? member : "");
    field->label = ecs_os_strdup(label ? label : "");
    field->kind = kind;
    field->value = ecs_os_strdup(value ? value : "");
    ecs_vec_init_t(NULL, &field->options, char*, 0);
    field->option = -1;
    field->editable = editable;
    field->expanded = true;

    return field;
}

/** A constant of an enum being sorted by value */
typedef struct box_editor_constant_t {
    const char *name; /**< Name of the constant */
    int64_t value;    /**< Value of the constant */
} box_editor_constant_t;

/** Compare two constants by value.
 *
 * @param a The first constant
 * @param b The second constant
 * @return Negative when a sorts first, positive when b does, 0 when equal
 */
static int box_editor_constant_compare(
    const void *a,
    const void *b)
{
    const box_editor_constant_t *c1 = a;
    const box_editor_constant_t *c2 = b;

    return c1->value < c2->value ? -1 : (c1->value > c2->value);
}

/** Fill the options of an enum field with the constants of its type.
 *
 * @param world The world
 * @param type The enum type
 * @param field The field
 * @param value The current value
 */
static void box_editor_field_options(
    ecs_world_t *world,
    ecs_entity_t type,
    EditorField *field,
    int64_t value)
{
    const EcsEnum *enum_type = ecs_get(world, type, EcsEnum);
    ecs_entity_t underlying = enum_type && enum_type->underlying_type ?
        enum_type->underlying_type : ecs_id(ecs_i32_t);
    ecs_vec_t constants;
    ecs_vec_init_t(NULL, &constants, box_editor_constant_t, 0);
    ecs_iter_t it = ecs_children(world, type);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const void *ptr = ecs_get_id(world, it.entities[i],
                ecs_pair(EcsConstant, underlying));
            if (!ptr) {
                continue;
            }

            ecs_meta_cursor_t cur = ecs_meta_cursor(world, underlying,
                ECS_CONST_CAST(void*, ptr));
            box_editor_constant_t *c = ecs_vec_append_t(
                NULL, &constants, box_editor_constant_t);
            c->name = ecs_get_name(world, it.entities[i]);
            c->value = ecs_meta_get_int(&cur);
        }
    }

    int32_t i, count = ecs_vec_count(&constants);
    qsort(ecs_vec_first(&constants), (size_t)count, sizeof(box_editor_constant_t),
        box_editor_constant_compare);
    for (i = 0; i < count; i ++) {
        box_editor_constant_t *c = ecs_vec_get_t(&constants, box_editor_constant_t, i);
        ecs_vec_append_t(NULL, &field->options, char*)[0] = ecs_os_strdup(c->name);
        if (c->value == value) {
            field->option = i;
            ecs_os_free(field->value);
            field->value = ecs_os_strdup(c->name);
        }
    }

    ecs_vec_fini_t(NULL, &constants, box_editor_constant_t);
}

/** Widget of a primitive type.
 *
 * @param world The world
 * @param type The type
 * @return The kind of field the type is edited with
 */
static EditorFieldKind box_editor_primitive_kind(
    const ecs_world_t *world,
    ecs_entity_t type)
{
    const EcsPrimitive *primitive = ecs_get(world, type, EcsPrimitive);
    EditorFieldKind result = FieldValue;
    if (!primitive) {
        goto done;
    }

    switch (primitive->kind) {
    case EcsBool:
        result = FieldBool;
        break;
    case EcsChar:
    case EcsString:
        result = FieldText;
        break;
    case EcsEntity:
    case EcsId:
        result = FieldEntity;
        break;
    case EcsByte:
    case EcsU8:
    case EcsU16:
    case EcsU32:
    case EcsU64:
    case EcsI8:
    case EcsI16:
    case EcsI32:
    case EcsI64:
    case EcsF32:
    case EcsF64:
    case EcsIPtr:
    case EcsUPtr:
        result = FieldNumber;
        break;
    default:
        break;
    }

done:
    return result;
}

/** Tell whether a component is written by the engine, so the grid does not edit it.
 *
 * @param world The world
 * @param component The component
 * @return true when the engine owns the value
 */
static bool box_editor_engine_written(
    const ecs_world_t *world,
    ecs_entity_t component)
{
    static const char *names[] = {
        "WorldTransform", "AABB", "Collider", "RootBounds", "SplineInfo", "Bounds",
        "DrawInstance", "EditorTree", "EditorInspector", "ScriptStats", NULL
    };
    const char *name = ecs_get_name(world, component);
    bool result = false;
    int32_t i;
    for (i = 0; name && names[i] && !result; i ++) {
        result = !strcmp(name, names[i]);
    }

    return result;
}

/** Value of an inline array member as text.
 *
 * @param world The world
 * @param cur Cursor at the member
 * @param type Element type of the array
 * @param count Elements of the array
 * @return The text, the caller frees it
 */
static char* box_editor_array_text(
    ecs_world_t *world,
    ecs_meta_cursor_t *cur,
    ecs_entity_t type,
    int32_t count)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    const EcsComponent *component = ecs_get(world, type, EcsComponent);
    ecs_size_t size = component ? component->size : 0;
    const char *base = ecs_meta_get_ptr(cur);
    int32_t i;
    ecs_strbuf_appendch(&buf, '[');
    for (i = 0; i < count && size; i ++) {
        char *elem = ecs_ptr_to_expr(world, type, base + i * size);
        if (i) {
            ecs_strbuf_appendlit(&buf, ", ");
        }

        ecs_strbuf_appendstr(&buf, elem);
        ecs_os_free(elem);
    }

    ecs_strbuf_appendch(&buf, ']');

    return ecs_strbuf_get(&buf);
}

/** Append the rows of the members a cursor points into, recursing into structs.
 *
 * @param world The world
 * @param inspector The model
 * @param cur Cursor at a struct value
 * @param component The component the members belong to
 * @param prefix Dotted path of the struct inside the component, empty at the top
 * @param depth Structs entered so far
 * @param editable Whether the component may be changed
 */
static void box_editor_members(
    ecs_world_t *world,
    EditorInspector *inspector,
    ecs_meta_cursor_t *cur,
    ecs_entity_t component,
    const char *prefix,
    int32_t depth,
    bool editable)
{
    ecs_entity_t struct_type = ecs_meta_get_type(cur);
    const EcsStruct *st = ecs_get(world, struct_type, EcsStruct);
    int32_t i, count = st ? ecs_vec_count(&st->members) : 0;
    if (!count || ecs_meta_push(cur)) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const char *name = ecs_meta_get_member(cur);
        ecs_entity_t type = ecs_meta_get_type(cur);
        const EcsType *meta = ecs_get(world, type, EcsType);
        char *path = prefix[0] ? flecs_asprintf("%s.%s", prefix, name) : ecs_os_strdup(name);
        void *ptr = ecs_meta_get_ptr(cur);
        const EcsMember *m = ecs_get(world, ecs_meta_get_member_id(cur), EcsMember);
        bool collection = ecs_meta_is_collection(cur) || (m && m->count > 1);
        if (meta && meta->kind == EcsStructType && !collection && depth < BOX_EDITOR_NEST_MAX) {
            box_editor_field(inspector, component, path, path, FieldValue, "", false);
            box_editor_members(world, inspector, cur, component, path, depth + 1, editable);
        } else if (meta && meta->kind == EcsEnumType && !collection) {
            EditorField *field = box_editor_field(
                inspector, component, path, path, FieldEnum, "", editable);
            box_editor_field_options(world, type, field, ecs_meta_get_int(cur));
        } else if (meta && meta->kind == EcsPrimitiveType && !collection) {
            EditorFieldKind kind = box_editor_primitive_kind(world, type);
            char *value = NULL;
            if (kind == FieldText) {
                const char *str = ecs_meta_get_string(cur);
                value = ecs_os_strdup(str ? str : "");
            } else if (kind == FieldEntity) {
                ecs_entity_t target = ecs_meta_get_entity(cur);
                value = target && ecs_is_alive(world, target) ?
                    ecs_get_path(world, target) : ecs_os_strdup("");
            } else {
                value = ecs_ptr_to_expr(world, type, ptr);
            }

            box_editor_field(inspector, component, path, path, kind, value,
                editable && kind != FieldValue);
            ecs_os_free(value);
        } else if (collection && meta && meta->kind != EcsVectorType) {
            char *value = box_editor_array_text(world, cur, type, m ? m->count : 0);
            box_editor_field(inspector, component, path, path, FieldValue, value, false);
            ecs_os_free(value);
        } else {
            char *value = ecs_ptr_to_expr(world, type, ptr);
            box_editor_field(inspector, component, path, path, FieldValue, value, false);
            ecs_os_free(value);
        }

        ecs_os_free(path);
        if (i < count - 1) {
            ecs_meta_next(cur);
        }
    }

    ecs_meta_pop(cur);
done:
    return;
}

/** Tell whether an id is shown in the grid.
 *
 * @param world The world
 * @param id The id
 * @return true when the grid shows the id
 */
static bool box_editor_id_shown(
    const ecs_world_t *world,
    ecs_id_t id)
{
    bool result = true;
    ecs_entity_t first = ECS_IS_PAIR(id) ? ecs_pair_first(world, id) : id;
    if (!first || !ecs_is_alive(world, first)) {
        result = false;
        goto done;
    }

    if (ECS_IS_PAIR(id) && (first == EcsChildOf || first == EcsScriptTemplate ||
        first == EcsIsA || first == EcsOnInstantiate))
    {
        result = false;
        goto done;
    }

    ecs_entity_t parent = ecs_get_parent(world, first);
    while (parent) {
        const char *name = ecs_get_name(world, parent);
        if (name && !ecs_get_parent(world, parent) && !strcmp(name, "flecs")) {
            result = false;
            break;
        }

        parent = ecs_get_parent(world, parent);
    }

done:
    return result;
}

/** Append the rows of one id of the selection.
 *
 * @param world The world
 * @param inspector The model
 * @param e The selected entity
 * @param id The id
 */
static void box_editor_id_fields(
    ecs_world_t *world,
    EditorInspector *inspector,
    ecs_entity_t e,
    ecs_id_t id)
{
    if (ECS_IS_PAIR(id)) {
        char *name = ecs_id_str(world, id);
        box_editor_field(inspector, 0, NULL, name, FieldTag, "", false);
        ecs_os_free(name);
        goto done;
    }

    const EcsType *meta = ecs_get(world, id, EcsType);
    const EcsComponent *component = ecs_get(world, id, EcsComponent);
    char *name = ecs_get_path(world, id);
    if (!component || !component->size) {
        box_editor_field(inspector, id, NULL, name, FieldTag, "", false);
    } else if (meta && meta->kind == EcsStructType) {
        bool editable = !box_editor_engine_written(world, id) &&
            box_editor_editable(world, e, id);
        bool expanded = ecs_map_get(&box_editor_state(world)->collapsed, id) == NULL;
        EditorField *header = box_editor_field(
            inspector, id, NULL, name, FieldHeader, "", editable);
        header->expanded = expanded;
        if (expanded) {
            void *ptr = ECS_CONST_CAST(void*, ecs_get_id(world, e, id));
            ecs_meta_cursor_t cur = ecs_meta_cursor(world, id, ptr);
            box_editor_members(world, inspector, &cur, id, "", 0, editable);
        }
    } else if (meta) {
        bool expanded = ecs_map_get(&box_editor_state(world)->collapsed, id) == NULL;
        EditorField *header = box_editor_field(inspector, id, NULL, name, FieldHeader, "", false);
        header->expanded = expanded;
        if (expanded) {
            char *value = ecs_ptr_to_expr(world, id, ecs_get_id(world, e, id));
            box_editor_field(inspector, id, "", "value", FieldValue, value, false);
            ecs_os_free(value);
        }
    }

    ecs_os_free(name);
done:
    return;
}

/** Serialize the fields of a model, so two rebuilds can be compared.
 *
 * @param inspector The model
 * @return The text, the caller frees it
 */
static char* box_editor_fields_text(
    const EditorInspector *inspector)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i, count = ecs_vec_count(&inspector->fields);
    ecs_strbuf_append(&buf, "%s|%u|", inspector->name ? inspector->name : "",
        (uint32_t)inspector->entity);
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&inspector->fields, EditorField, i);
        ecs_strbuf_append(&buf, "%u:%s:%s:%d:%s:%d:%d:%d;", (uint32_t)f->component,
            f->member, f->label, (int)f->kind, f->value, f->option, (int)f->editable,
            (int)f->expanded);
    }

    return ecs_strbuf_get(&buf);
}

void box_editor_inspector_update(
    ecs_world_t *world,
    EditorState *state)
{
    const Editor *editor = ecs_singleton_get(world, Editor);
    ecs_entity_t e = editor->selected;
    EditorInspector fresh = { .entity = 0 };
    ecs_vec_init_t(NULL, &fresh.fields, EditorField, 0);
    if (e && ecs_is_alive(world, e)) {
        fresh.entity = e;
        fresh.name = ecs_get_path(world, e);
        const ecs_type_t *type = ecs_get_type(world, e);
        int32_t i;
        for (i = 0; type && i < type->count; i ++) {
            if (box_editor_id_shown(world, type->array[i])) {
                box_editor_id_fields(world, &fresh, e, type->array[i]);
            }
        }
    }

    char *text = box_editor_fields_text(&fresh);
    if (state->inspector_text && !strcmp(state->inspector_text, text)) {
        ecs_os_free(text);
        box_editor_fields_clear(&fresh);
        ecs_vec_fini_t(NULL, &fresh.fields, EditorField);
        goto done;
    }

    ecs_os_free(state->inspector_text);
    state->inspector_text = text;
    EditorInspector *inspector = ecs_get_mut(world, ecs_id(EditorInspector), EditorInspector);
    box_editor_fields_clear(inspector);
    ecs_vec_fini_t(NULL, &inspector->fields, EditorField);
    inspector->fields = fresh.fields;
    inspector->entity = fresh.entity;
    inspector->name = fresh.name;
    inspector->version ++;
    ecs_modified(world, ecs_id(EditorInspector), EditorInspector);
done:
    return;
}

bool box_editor_assign(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component,
    const char *member,
    const char *value)
{
    bool result = false;
    if (!e || !ecs_is_alive(world, e) || !component || !ecs_has_id(world, e, component)) {
        goto done;
    }

    if (!box_editor_editable(world, e, component)) {
        goto done;
    }

    void *ptr = ECS_CONST_CAST(void*, ecs_get_id(world, e, component));
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, component, ptr);
    if (member && member[0] && (ecs_meta_push(&cur) || ecs_meta_dotmember(&cur, member))) {
        goto done;
    }

    ecs_log_set_level(-3);
    int set = ecs_meta_set_string(&cur, value ? value : "");
    ecs_log_set_level(-1);
    if (set) {
        goto done;
    }

    ecs_modified_id(world, e, component);
    box_editor_write_through(world, e, component);
    EditorState *state = box_editor_state(world);
    ecs_os_free(state->inspector_text);
    state->inspector_text = NULL;
    result = true;
done:
    return result;
}

void box_editor_collapse(
    ecs_world_t *world,
    ecs_entity_t component,
    bool collapsed)
{
    EditorState *state = box_editor_state(world);
    if (collapsed) {
        ecs_map_ensure(&state->collapsed, component)[0] = 1;
    } else {
        ecs_map_remove(&state->collapsed, component);
    }
}

/** Disable every enabled input map, remembering which ones.
 *
 * @param world The world
 * @param state The module state
 */
static void box_editor_capture_maps(
    ecs_world_t *world,
    EditorState *state)
{
    ecs_iter_t it = ecs_each(world, InputMap);
    while (ecs_each_next(&it)) {
        InputMap *map = ecs_field(&it, InputMap, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (map[i].enabled) {
                map[i].enabled = false;
                ecs_vec_append_t(NULL, &state->captured, ecs_entity_t)[0] = it.entities[i];
            }
        }
    }

    state->capturing = true;
}

void box_editor_release_maps(
    ecs_world_t *world,
    EditorState *state)
{
    int32_t i, count = ecs_vec_count(&state->captured);
    for (i = 0; i < count; i ++) {
        ecs_entity_t map = ecs_vec_get_t(&state->captured, ecs_entity_t, i)[0];
        InputMap *m = ecs_is_alive(world, map) ? ecs_get_mut(world, map, InputMap) : NULL;
        if (m) {
            m->enabled = true;
        }
    }

    ecs_vec_clear(&state->captured);
    state->capturing = false;
}

/** Show the value a field holds now in the input that edits it.
 *
 * @param world The world
 * @param e The input element
 * @param binding The binding of the input
 */
static void box_editor_binding_refresh(
    ecs_world_t *world,
    ecs_entity_t e,
    const EditorBinding *binding)
{
    const EditorInspector *inspector = ecs_singleton_get(world, EditorInspector);
    int32_t i, count = ecs_vec_count(&inspector->fields);
    const char *value = "";
    for (i = 0; i < count; i ++) {
        const EditorField *f = ecs_vec_get_t(&inspector->fields, EditorField, i);
        if (f->component == binding->component && f->member &&
            !strcmp(f->member, binding->member ? binding->member : ""))
        {
            value = f->value;
            break;
        }
    }

    TextInput *input = ecs_get_mut(world, e, TextInput);
    ecs_os_free(input->value);
    input->value = ecs_os_strdup(value);
    input->caret = (int32_t)strlen(value);
    ecs_os_free(input->submitted);
    input->submitted = ecs_os_strdup("");
    input->focused = false;
    ecs_set(world, e, Text, { .value = ecs_os_strdup(value) });
}

void box_editor_bindings_update(
    ecs_world_t *world,
    EditorState *state)
{
    const Editor *editor = ecs_singleton_get(world, Editor);
    ecs_vec_t submitted;
    ecs_vec_init_t(NULL, &submitted, ecs_entity_t, 0);
    bool focused = false;
    ecs_iter_t it = ecs_each(world, EditorBinding);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const TextInput *input = ecs_get(world, it.entities[i], TextInput);
            if (!input) {
                continue;
            }

            focused = focused || input->focused;
            if (input->submitted && input->submitted[0]) {
                ecs_vec_append_t(NULL, &submitted, ecs_entity_t)[0] = it.entities[i];
            }
        }
    }

    int32_t i, count = ecs_vec_count(&submitted);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_vec_get_t(&submitted, ecs_entity_t, i)[0];
        const EditorBinding *binding = ecs_get(world, e, EditorBinding);
        const TextInput *input = ecs_get(world, e, TextInput);
        box_editor_assign(world, editor->selected, binding->component,
            binding->member, input->submitted);
        state = box_editor_state(world);
        box_editor_inspector_update(world, state);
        box_editor_binding_refresh(world, e, binding);
        focused = false;
    }

    ecs_vec_fini_t(NULL, &submitted, ecs_entity_t);
    state = box_editor_state(world);
    if (focused && !state->capturing) {
        box_editor_capture_maps(world, state);
    } else if (!focused && state->capturing) {
        box_editor_release_maps(world, state);
    }
}
