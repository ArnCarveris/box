/**
 * @file command/cmd_portal.c
 * @brief The portal commands: the options of the portal visibility of a view, and what its
 * camera sees through the portals.
 *
 * The options are members the commands write: CullSettings.portals, CullSettings.freeze_portals
 * and the members of ViewDebug.portal. portal.tree and portal.status read the PortalTree and
 * the PortalStatus the renderer sets on the view.
 */

#include "command.h"

/** Entries portal.tree prints at most */
#define BOX_CMD_PORTAL_TREE_ROWS (32)

/** The options the commands switch */
typedef enum box_cmd_portal_option_t {
    BoxCmdPortalCull = 1,       /**< Cull by the portals the camera sees through */
    BoxCmdPortalFreeze,         /**< Keep the traversal of where the camera was */
    BoxCmdPortalLines,          /**< Draw the outlines of the portals */
    BoxCmdPortalAreas,          /**< Draw the volumes of the areas */
    BoxCmdPortalOccluders       /**< Draw the occluders */
} box_cmd_portal_option_t;

/** Get the view a command works on: the view argument, else the first view that clears, in
 * render order.
 *
 * @param world The world
 * @param argc Number of arguments
 * @param argv The arguments
 * @param index Index of the view argument
 * @return The view entity, 0 with an error logged
 */
static ecs_entity_t box_cmd_portal_view(
    ecs_world_t *world,
    int32_t argc,
    const ecs_value_t *argv,
    int32_t index)
{
    ecs_entity_t result = argc > index ? *(const ecs_entity_t*)argv[index].ptr : 0;

    if (result) {
        if (!ecs_is_alive(world, result) || !ecs_has(world, result, View)) {
            ecs_err("'%s' is not a view", ecs_get_path(world, result));
            result = 0;
        }
    } else {
        result = box_cmd_render_view(world, 0, 0, true);
        if (!result) {
            ecs_err("there is no view");
        }
    }

    return result;
}

/** Get the member of a view an option is.
 *
 * @param view The view
 * @param option The option
 * @return The member
 */
static bool* box_cmd_portal_flag(
    View *view,
    box_cmd_portal_option_t option)
{
    bool *result = &view->debug.portal.occluders;

    if (option == BoxCmdPortalCull) {
        result = &view->culling.portals;
    } else if (option == BoxCmdPortalFreeze) {
        result = &view->culling.freeze_portals;
    } else if (option == BoxCmdPortalLines) {
        result = &view->debug.portal.lines;
    } else if (option == BoxCmdPortalAreas) {
        result = &view->debug.portal.areas;
    }

    return result;
}

/** Get the name of an option.
 *
 * @param option The option
 * @return The name of its command
 */
static const char* box_cmd_portal_name(
    box_cmd_portal_option_t option)
{
    const char *result = "portal.occluders";

    if (option == BoxCmdPortalCull) {
        result = "portal.cull";
    } else if (option == BoxCmdPortalFreeze) {
        result = "portal.freeze";
    } else if (option == BoxCmdPortalLines) {
        result = "portal.lines";
    } else if (option == BoxCmdPortalAreas) {
        result = "portal.areas";
    }

    return result;
}

/** Check that a world has the portal module.
 *
 * @param world The world
 * @return Whether it has
 */
static bool box_cmd_portal_ready(
    const ecs_world_t *world)
{
    bool result = ecs_id(PortalStatus) != 0 && ecs_id(View) != 0;

    if (!result) {
        ecs_err("box.portal is not imported");
    }

    return result;
}

/** The portal.cull, freeze, lines, areas and occluders commands: set or toggle an option of
 * a view.
 *
 * @param ctx The call context, ctx->ctx is the option
 * @param argc Number of arguments
 * @param argv The state and the view
 * @param result Receives the option and its state
 */
static void box_cmd_portal_option(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    box_cmd_portal_option_t option = (box_cmd_portal_option_t)(uintptr_t)ctx->ctx;
    box_cmd_switch_t state = argc > 0 ? (box_cmd_switch_t)*(const int32_t*)argv[0].ptr :
        BoxCmdSwitchToggle;
    ecs_entity_t entity = box_cmd_portal_ready(world)
        ? box_cmd_portal_view(world, argc, argv, 1)
        : 0;

    if (entity) {
        View *view = ecs_get_mut(world, entity, View);
        bool *flag = box_cmd_portal_flag(view, option);

        *flag = state == BoxCmdSwitchToggle ? !*flag : state == BoxCmdSwitchOn;
        ecs_modified(world, entity, View);
        ecs_strbuf_t buf = ECS_STRBUF_INIT;

        ecs_strbuf_append(&buf, "%s %s", box_cmd_portal_name(option), *flag ? "on" : "off");
        *(char**)result->ptr = ecs_strbuf_get(&buf);
    }
}

/** Get the name of an area, or of the outdoors.
 *
 * @param world The world
 * @param area The area entity, 0 for the outdoors
 * @return The name, valid until the entity is renamed
 */
static const char* box_cmd_portal_area(
    const ecs_world_t *world,
    ecs_entity_t area)
{
    const char *result = "outdoors";

    if (area) {
        result = ecs_get_name(world, area);
        result = result ? result : "area";
    }

    return result;
}

/** The portal.status command: the options of a view and the numbers of its last traversal.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The view
 * @param result Receives the lines
 */
static void box_cmd_portal_status(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t entity = box_cmd_portal_ready(world)
        ? box_cmd_portal_view(world, argc, argv, 0)
        : 0;

    if (entity) {
        const View *view = ecs_get(world, entity, View);
        const PortalStatus *status = ecs_get(world, entity, PortalStatus);
        ecs_strbuf_t buf = ECS_STRBUF_INIT;

        ecs_strbuf_append(&buf, "portal view %s\n", ecs_get_path(world, entity));
        ecs_strbuf_append(&buf, "cull %s  freeze %s  lines %s  areas %s  occluders %s\n",
            view->culling.portals ? "on" : "off", view->culling.freeze_portals ? "on" : "off",
            view->debug.portal.lines ? "on" : "off", view->debug.portal.areas ? "on" : "off",
            view->debug.portal.occluders ? "on" : "off");

        if (status) {
            ecs_strbuf_append(&buf, "camera area %s%s\n",
                box_cmd_portal_area(world, status->area), status->frozen ? "  [frozen]" : "");
            ecs_strbuf_append(&buf, "areas %d  entries %d  outdoors %s  view culls %s%s\n",
                status->areas, status->entries, status->outdoors ? "seen" : "not seen",
                status->culling ? "by it" : "nothing",
                status->truncated ? "  [truncated]" : "");
            ecs_strbuf_append(&buf, "portals tested %d  passed %d  closed %d",
                status->portals_tested, status->portals_passed, status->portals_closed);
        } else {
            ecs_strbuf_appendstr(&buf,
                "no traversal: it runs while the view culls by portals or draws their lines");
        }

        *(char**)result->ptr = ecs_strbuf_get(&buf);
    }
}

/** Get the name of the kind of a portal.
 *
 * @param world The world
 * @param portal The portal entity
 * @return The kind
 */
static const char* box_cmd_portal_kind(
    const ecs_world_t *world,
    ecs_entity_t portal)
{
    const VisPortal *p = portal && ecs_is_alive(world, portal)
        ? ecs_get(world, portal, VisPortal)
        : NULL;
    const char *result = "portal";

    if (p && p->kind == PortalKindWindow) {
        result = "window";
    } else if (p && p->kind == PortalKindHatch) {
        result = "hatch";
    } else if (p) {
        result = "door";
    }

    return result;
}

/** The portal.tree command: the areas the camera of a view sees through portals.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The view
 * @param result Receives the lines
 */
static void box_cmd_portal_tree(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t entity = box_cmd_portal_ready(world)
        ? box_cmd_portal_view(world, argc, argv, 0)
        : 0;

    if (entity) {
        const PortalStatus *status = ecs_get(world, entity, PortalStatus);
        const PortalTree *tree = ecs_get(world, entity, PortalTree);
        ecs_strbuf_t buf = ECS_STRBUF_INIT;
        int32_t i, k, shown = tree ? tree->count : 0;

        if (!status || !tree) {
            ecs_strbuf_appendstr(&buf,
                "no traversal: it runs while the view culls by portals or draws their lines");
        } else {
            ecs_strbuf_append(&buf, "PORTAL TREE  %d entries  %d passed%s%s", tree->count,
                status->portals_passed, status->truncated ? "  [truncated]" : "",
                status->frozen ? "  [frozen]" : "");
            shown = shown < BOX_CMD_PORTAL_TREE_ROWS ? shown : BOX_CMD_PORTAL_TREE_ROWS;

            for (i = 0; i < shown; i ++) {
                const PortalTreeEntry *e = &tree->entries[i];
                int32_t depth = e->depth < 10 ? e->depth : 10;

                ecs_strbuf_appendstr(&buf, "\n");
                for (k = 0; k < depth; k ++) {
                    ecs_strbuf_appendlit(&buf, "  ");
                }

                ecs_strbuf_append(&buf, "%s%s%s%s", e->depth ? "- " : "",
                    e->via ? box_cmd_portal_kind(world, e->via) : "",
                    e->via ? " " : "", box_cmd_portal_area(world, e->area));
                ecs_strbuf_append(&buf, "%s", e->sky_only ? "  [sky only]" : "");
            }

            if (tree->count > shown) {
                ecs_strbuf_append(&buf, "\n  ... %d more", tree->count - shown);
            }
        }

        *(char**)result->ptr = ecs_strbuf_get(&buf);
    }
}

/** Register the switch of an option.
 *
 * @param world The world
 * @param parent The portal command
 * @param sw The Switch enum
 * @param option The option
 * @param name The name of the subcommand
 * @param brief Description
 */
static void box_cmd_portal_register_option(
    ecs_world_t *world,
    ecs_entity_t parent,
    ecs_entity_t sw,
    box_cmd_portal_option_t option,
    const char *name,
    const char *brief)
{
    box_command_register(world, brief, (ecs_function_desc_t){
        .name = name,
        .parent = parent,
        .return_type = ecs_id(ecs_string_t),
        .params = {
            { .name = "state", .type = sw, .optional = true,
              .brief = "on or off (default: toggle)" },
            { .name = "view", .type = ecs_id(ecs_entity_t), .optional = true,
              .brief = "View to set (default: the first view that clears)" }
        },
        .callback = box_cmd_portal_option,
        .ctx = (void*)(uintptr_t)option
    });
}

void box_cmd_portal_init(
    ecs_world_t *world)
{
    ECS_IMPORT(world, BoxPortal);

    ecs_entity_t sw = ecs_lookup(world, "box.command.Switch");
    ecs_entity_t portal = box_command_register(world,
        "Print the portal options and the last traversal of a view; the portal.* commands "
        "change and inspect them",
        (ecs_function_desc_t){
            .name = "portal",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "view", .type = ecs_id(ecs_entity_t), .optional = true,
                  .brief = "View to print (default: the first view that clears)" }
            },
            .callback = box_cmd_portal_status
        });

    box_command_register(world, "Print the portal options and the last traversal of a view",
        (ecs_function_desc_t){
            .name = "status",
            .parent = portal,
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "view", .type = ecs_id(ecs_entity_t), .optional = true,
                  .brief = "View to print (default: the first view that clears)" }
            },
            .callback = box_cmd_portal_status
        });

    box_command_register(world,
        "Print the areas the camera of a view sees through portals, as an indented tree",
        (ecs_function_desc_t){
            .name = "tree",
            .parent = portal,
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "view", .type = ecs_id(ecs_entity_t), .optional = true,
                  .brief = "View to print (default: the first view that clears)" }
            },
            .callback = box_cmd_portal_tree
        });

    box_cmd_portal_register_option(world, portal, sw, BoxCmdPortalCull, "cull",
        "Set or toggle culling by portals: draw only the areas the camera sees (CullSettings."
        "portals)");
    box_cmd_portal_register_option(world, portal, sw, BoxCmdPortalFreeze, "freeze",
        "Set or toggle freezing the visibility: keep the areas seen from where the camera is "
        "now while it moves (CullSettings.freeze_portals)");
    box_cmd_portal_register_option(world, portal, sw, BoxCmdPortalLines, "lines",
        "Set or toggle the outlines of the portals the camera sees and the part of each a "
        "frustum leaves (ViewDebug.portal.lines)");
    box_cmd_portal_register_option(world, portal, sw, BoxCmdPortalAreas, "areas",
        "Set or toggle the volumes of the areas the camera sees (ViewDebug.portal.areas)");
    box_cmd_portal_register_option(world, portal, sw, BoxCmdPortalOccluders, "occluders",
        "Set or toggle the occluders in the areas the camera sees (ViewDebug.portal.occluders)");
}
