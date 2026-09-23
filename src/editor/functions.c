/**
 * @file editor/functions.c
 * @brief The editor.* functions the overlay script calls from its async blocks.
 *
 * Every function returns a bool so a task can call it as `const ok = editor.select(e)`.
 */

#include "editor_internal.h"

/** editor.select: select an entity, the root of its instance unless that is opened.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the entity, 0 clears the selection
 * @param result Receives whether something is selected
 */
static void box_editor_fn_select(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(bool*)result->ptr = box_editor_select(
        ctx->world, *(ecs_entity_t*)argv[0].ptr) != 0;
}

/** editor.expand: show or hide the children of an entity in the tree.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the entity and whether it is expanded
 * @param result Receives true
 */
static void box_editor_fn_expand(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    box_editor_expand(ctx->world, *(ecs_entity_t*)argv[0].ptr, *(bool*)argv[1].ptr);
    *(bool*)result->ptr = true;
}

/** editor.collapse: hide or show the members of a component in the inspector.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the component and whether its members are hidden
 * @param result Receives true
 */
static void box_editor_fn_collapse(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    box_editor_collapse(ctx->world, *(ecs_entity_t*)argv[0].ptr, *(bool*)argv[1].ptr);
    *(bool*)result->ptr = true;
}

/** editor.open: open an instance, hiding the rest of the scene.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the instance root
 * @param result Receives whether the instance was opened
 */
static void box_editor_fn_open(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(bool*)result->ptr = box_editor_open(ctx->world, *(ecs_entity_t*)argv[0].ptr);
}

/** editor.close: close the innermost opened instance.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments, none
 * @param result Receives whether an instance was closed
 */
static void box_editor_fn_close(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    *(bool*)result->ptr = box_editor_close(ctx->world);
}

/** editor.tool: pick the tool applied to the selection.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the tool
 * @param result Receives true
 */
static void box_editor_fn_tool(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    Editor *editor = box_editor_get(ctx->world);
    editor->tool = *(EditorTool*)argv[0].ptr;
    box_editor_state(ctx->world)->grab.active = false;
    box_editor_modified(ctx->world);
    *(bool*)result->ptr = true;
}

/** editor.assign: assign a value written as text to a member of the selection.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the component, the member path and the value
 * @param result Receives whether the value was assigned
 */
static void box_editor_fn_assign(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const Editor *editor = ecs_singleton_get(ctx->world, Editor);
    *(bool*)result->ptr = box_editor_assign(ctx->world, editor->selected,
        *(ecs_entity_t*)argv[0].ptr, *(char**)argv[1].ptr, *(char**)argv[2].ptr);
}

/** editor.grab: start dragging a gizmo handle.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the handle and the press
 * @param result Receives whether the drag started
 */
static void box_editor_fn_grab(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(bool*)result->ptr = box_editor_grab(
        ctx->world, *(ecs_entity_t*)argv[0].ptr, argv[1].ptr);
}

/** editor.pull: move the selection with a drag of the held handle.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the handle and the drag
 * @param result Receives whether the selection changed
 */
static void box_editor_fn_pull(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(bool*)result->ptr = box_editor_pull(
        ctx->world, *(ecs_entity_t*)argv[0].ptr, argv[1].ptr);
}

/** editor.drop: end the drag of a handle.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments, none
 * @param result Receives whether a handle was held
 */
static void box_editor_fn_drop(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    *(bool*)result->ptr = box_editor_drop(ctx->world);
}

/** editor.point: select a control point of the selected spline.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the index, -1 for none
 * @param result Receives whether the point exists
 */
static void box_editor_fn_point(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(bool*)result->ptr = box_editor_point(ctx->world, *(int32_t*)argv[0].ptr);
}

/** editor.point_add: add a control point after the selected one.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments, none
 * @param result Receives whether a point was added
 */
static void box_editor_fn_point_add(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    *(bool*)result->ptr = box_editor_point_add(ctx->world);
}

/** editor.point_remove: remove the selected control point.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments, none
 * @param result Receives whether a point was removed
 */
static void box_editor_fn_point_remove(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    *(bool*)result->ptr = box_editor_point_remove(ctx->world);
}

void box_editor_functions_init(
    ecs_world_t *world,
    ecs_entity_t module)
{
    ecs_entity_t scope = ecs_entity(world, { .name = "editor", .parent = module });
    ecs_entity_t boolean = ecs_id(ecs_bool_t);
    ecs_entity_t entity = ecs_id(ecs_entity_t);
    ecs_entity_t string = ecs_id(ecs_string_t);
    ecs_entity_t hit = ecs_id(PickHit);

    ecs_function(world, { .name = "select", .parent = scope, .return_type = boolean,
        .params = {{ .name = "entity", .type = entity }},
        .callback = box_editor_fn_select });

    ecs_function(world, { .name = "expand", .parent = scope, .return_type = boolean,
        .params = {{ .name = "entity", .type = entity }, { .name = "expanded", .type = boolean }},
        .callback = box_editor_fn_expand });

    ecs_function(world, { .name = "collapse", .parent = scope, .return_type = boolean,
        .params = {
            { .name = "component", .type = entity },
            { .name = "collapsed", .type = boolean }
        },
        .callback = box_editor_fn_collapse });

    ecs_function(world, { .name = "open", .parent = scope, .return_type = boolean,
        .params = {{ .name = "entity", .type = entity }},
        .callback = box_editor_fn_open });

    ecs_function(world, { .name = "close", .parent = scope, .return_type = boolean,
        .callback = box_editor_fn_close });

    ecs_function(world, { .name = "tool", .parent = scope, .return_type = boolean,
        .params = {{ .name = "tool", .type = ecs_id(EditorTool) }},
        .callback = box_editor_fn_tool });

    ecs_function(world, { .name = "assign", .parent = scope, .return_type = boolean,
        .params = {
            { .name = "component", .type = entity },
            { .name = "member", .type = string },
            { .name = "value", .type = string }
        },
        .callback = box_editor_fn_assign });

    ecs_function(world, { .name = "grab", .parent = scope, .return_type = boolean,
        .params = {{ .name = "handle", .type = entity }, { .name = "hit", .type = hit }},
        .callback = box_editor_fn_grab });

    ecs_function(world, { .name = "pull", .parent = scope, .return_type = boolean,
        .params = {{ .name = "handle", .type = entity }, { .name = "hit", .type = hit }},
        .callback = box_editor_fn_pull });

    ecs_function(world, { .name = "drop", .parent = scope, .return_type = boolean,
        .callback = box_editor_fn_drop });

    ecs_function(world, { .name = "point", .parent = scope, .return_type = boolean,
        .params = {{ .name = "index", .type = ecs_id(ecs_i32_t) }},
        .callback = box_editor_fn_point });

    ecs_function(world, { .name = "point_add", .parent = scope, .return_type = boolean,
        .callback = box_editor_fn_point_add });

    ecs_function(world, { .name = "point_remove", .parent = scope, .return_type = boolean,
        .callback = box_editor_fn_point_remove });
}
