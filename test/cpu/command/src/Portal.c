/**
 * @file Portal.c
 * @brief Tests for the portal commands: the options they write into the view, and the status
 * and the tree they print from what the renderer set on it.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

/** Create a world with a camera and the view that renders it, which clears.
 *
 * @param camera Receives the camera
 * @param view Receives the view
 * @return The world
 */
static ecs_world_t* portal_cmd_world(
    ecs_entity_t *camera,
    ecs_entity_t *view)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    *camera = ecs_entity(world, { .name = "eye" });
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f});
    *view = ecs_entity(world, { .name = "main_view" });
    ecs_set(world, *view, View, {
        .camera = *camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});

    return world;
}

/** Run a command line that has to succeed and check its output.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @param expect The output it must print
 */
static void portal_cmd_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line,
    const char *expect)
{
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, false);
    test_str(out, expect);
    ecs_os_free(out);
}

/** Run a command line that has to fail with a message.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @param message Text the error must hold
 */
static void portal_cmd_fail(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line,
    const char *message)
{
    bool error = false;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, true);
    test_assert(!strncmp(out, BOX_COMMAND_ERROR, strlen(BOX_COMMAND_ERROR)));
    test_assert(strstr(out, message) != NULL);
    ecs_os_free(out);
}

/** Run a command line that has to succeed and check that its output holds a text.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @param text The text the output must hold
 */
static void portal_cmd_has(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line,
    const char *text)
{
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, false);
    test_assert(strstr(out, text) != NULL);
    ecs_os_free(out);
}

void Portal_cull_toggles(void) {
    // command/portal
    ecs_entity_t camera, view;
    ecs_world_t *world = portal_cmd_world(&camera, &view);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    test_bool(ecs_get(world, view, View)->culling.portals, false);
    portal_cmd_run(world, s, "portal.cull", "portal.cull on");
    test_bool(ecs_get(world, view, View)->culling.portals, true);
    portal_cmd_run(world, s, "portal.cull", "portal.cull off");
    test_bool(ecs_get(world, view, View)->culling.portals, false);
    portal_cmd_run(world, s, "portal.cull on", "portal.cull on");
    portal_cmd_run(world, s, "portal.cull on", "portal.cull on");
    test_bool(ecs_get(world, view, View)->culling.portals, true);
    portal_cmd_run(world, s, "portal.cull off", "portal.cull off");
    test_bool(ecs_get(world, view, View)->culling.portals, false);

    ecs_fini(world);
}

void Portal_options_write_members(void) {
    // command/portal
    ecs_entity_t camera, view;
    ecs_world_t *world = portal_cmd_world(&camera, &view);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    portal_cmd_run(world, s, "portal.freeze on", "portal.freeze on");
    const View *v = ecs_get(world, view, View);
    test_bool(v->culling.freeze_portals, true);
    test_bool(v->culling.portals, false);
    test_bool(v->debug.portal.lines, false);

    portal_cmd_run(world, s, "portal.lines on", "portal.lines on");
    v = ecs_get(world, view, View);
    test_bool(v->debug.portal.lines, true);
    test_bool(v->debug.portal.areas, false);
    test_bool(v->debug.portal.occluders, false);

    portal_cmd_run(world, s, "portal.areas on", "portal.areas on");
    v = ecs_get(world, view, View);
    test_bool(v->debug.portal.areas, true);
    test_bool(v->debug.portal.occluders, false);

    portal_cmd_run(world, s, "portal.occluders on", "portal.occluders on");
    v = ecs_get(world, view, View);
    test_bool(v->debug.portal.occluders, true);
    test_bool(v->culling.freeze_portals, true);
    test_bool(v->culling.portals, false);

    portal_cmd_run(world, s, "portal.lines off", "portal.lines off");
    portal_cmd_run(world, s, "portal.areas", "portal.areas off");
    portal_cmd_run(world, s, "portal.freeze off", "portal.freeze off");
    v = ecs_get(world, view, View);
    test_bool(v->debug.portal.lines, false);
    test_bool(v->debug.portal.areas, false);
    test_bool(v->debug.portal.occluders, true);
    test_bool(v->culling.freeze_portals, false);

    ecs_fini(world);
}

void Portal_named_view(void) {
    // command/portal
    ecs_entity_t camera, view;
    ecs_world_t *world = portal_cmd_world(&camera, &view);
    ecs_entity_t other = ecs_entity(world, { .name = "other_view" });
    ecs_set(world, other, View, {.camera = camera});
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    portal_cmd_run(world, s, "portal.lines on other_view", "portal.lines on");
    test_bool(ecs_get(world, other, View)->debug.portal.lines, true);
    test_bool(ecs_get(world, view, View)->debug.portal.lines, false);

    portal_cmd_run(world, s, "portal.lines on", "portal.lines on");
    test_bool(ecs_get(world, view, View)->debug.portal.lines, true);

    ecs_fini(world);
}

void Portal_not_a_view(void) {
    // command/portal
    ecs_entity_t camera, view;
    ecs_world_t *world = portal_cmd_world(&camera, &view);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    portal_cmd_fail(world, s, "portal.cull on eye", "not a view");
    portal_cmd_fail(world, s, "portal.tree eye", "not a view");
    portal_cmd_fail(world, s, "portal eye", "not a view");
    test_bool(ecs_get(world, view, View)->culling.portals, false);

    ecs_fini(world);
}

void Portal_no_view(void) {
    // command/portal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    portal_cmd_fail(world, s, "portal.cull on", "no view");
    portal_cmd_fail(world, s, "portal", "no view");

    ecs_fini(world);
}

void Portal_invalid_state(void) {
    // command/portal
    ecs_entity_t camera, view;
    ecs_world_t *world = portal_cmd_world(&camera, &view);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    portal_cmd_fail(world, s, "portal.lines maybe", "maybe");
    test_bool(ecs_get(world, view, View)->debug.portal.lines, false);

    ecs_fini(world);
}

void Portal_status_without_traversal(void) {
    // command/portal
    ecs_entity_t camera, view;
    ecs_world_t *world = portal_cmd_world(&camera, &view);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    portal_cmd_run(world, s, "portal",
        "portal view main_view\n"
        "cull off  freeze off  lines off  areas off  occluders off\n"
        "no traversal: it runs while the view culls by portals or draws their lines");
    portal_cmd_has(world, s, "portal.tree", "no traversal");

    ecs_fini(world);
}

void Portal_status_prints_traversal(void) {
    // command/portal
    ecs_entity_t camera, view;
    ecs_world_t *world = portal_cmd_world(&camera, &view);
    ecs_entity_t hall = ecs_entity(world, { .name = "hall" });
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    portal_cmd_run(world, s, "portal.cull on", "portal.cull on");
    portal_cmd_run(world, s, "portal.lines on", "portal.lines on");
    ecs_set(world, view, PortalStatus, {
        .area = hall, .areas = 3, .entries = 4, .portals_tested = 7, .portals_passed = 3,
        .portals_closed = 1, .outdoors = true, .culling = true });

    portal_cmd_run(world, s, "portal",
        "portal view main_view\n"
        "cull on  freeze off  lines on  areas off  occluders off\n"
        "camera area hall\n"
        "areas 3  entries 4  outdoors seen  view culls by it\n"
        "portals tested 7  passed 3  closed 1");
    portal_cmd_run(world, s, "portal.status",
        "portal view main_view\n"
        "cull on  freeze off  lines on  areas off  occluders off\n"
        "camera area hall\n"
        "areas 3  entries 4  outdoors seen  view culls by it\n"
        "portals tested 7  passed 3  closed 1");

    ecs_set(world, view, PortalStatus, {
        .area = 0, .areas = 1, .entries = 1, .frozen = true, .truncated = true });
    portal_cmd_has(world, s, "portal", "camera area outdoors  [frozen]");
    portal_cmd_has(world, s, "portal", "outdoors not seen  view culls nothing  [truncated]");

    ecs_fini(world);
}

void Portal_tree_prints_entries(void) {
    // command/portal
    ecs_entity_t camera, view;
    ecs_world_t *world = portal_cmd_world(&camera, &view);
    ecs_entity_t hall = ecs_entity(world, { .name = "hall" });
    ecs_entity_t store = ecs_entity(world, { .name = "store" });
    ecs_entity_t vault = ecs_entity(world, { .name = "vault" });
    ecs_entity_t door = ecs_entity(world, { .name = "hall_store" });
    ecs_entity_t window = ecs_entity(world, { .name = "store_vault" });
    ecs_entity_t hatch = ecs_entity(world, { .name = "entry" });
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    ecs_set(world, door, VisPortal, {.kind = PortalKindDoor});
    ecs_set(world, window, VisPortal, {.kind = PortalKindWindow});
    ecs_set(world, hatch, VisPortal, {.kind = PortalKindHatch});

    PortalTree tree;
    ecs_os_memset_t(&tree, 0, PortalTree);
    tree.count = 4;
    tree.entries[0] = (PortalTreeEntry){ .area = hall };
    tree.entries[1] = (PortalTreeEntry){ .area = store, .via = door, .depth = 1 };
    tree.entries[2] = (PortalTreeEntry){ .area = vault, .via = window, .depth = 2 };
    tree.entries[3] = (PortalTreeEntry){ .area = 0, .via = hatch, .depth = 1, .sky_only = true };
    ecs_set_ptr(world, view, PortalTree, &tree);
    ecs_set(world, view, PortalStatus, { .area = hall, .portals_passed = 3 });

    portal_cmd_run(world, s, "portal.tree",
        "PORTAL TREE  4 entries  3 passed\n"
        "hall\n"
        "  - door store\n"
        "    - window vault\n"
        "  - hatch outdoors  [sky only]");

    ecs_set(world, view, PortalStatus, { .area = hall, .portals_passed = 3, .frozen = true });
    portal_cmd_has(world, s, "portal.tree", "3 passed  [frozen]\n");

    ecs_fini(world);
}

void Portal_tree_limits_rows(void) {
    // command/portal
    ecs_entity_t camera, view;
    ecs_world_t *world = portal_cmd_world(&camera, &view);
    ecs_entity_t hall = ecs_entity(world, { .name = "hall" });
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    PortalTree tree;
    ecs_os_memset_t(&tree, 0, PortalTree);
    tree.count = 40;
    for (int32_t i = 0; i < tree.count; i ++) {
        tree.entries[i].area = hall;
    }

    ecs_set_ptr(world, view, PortalTree, &tree);
    ecs_set(world, view, PortalStatus, { .area = hall });

    bool error = true;
    char *out = box_command_run(world, s, "portal.tree", &error);
    test_bool(error, false);
    test_assert(strstr(out, "PORTAL TREE  40 entries") != NULL);
    test_assert(strstr(out, "\n  ... 8 more") != NULL);
    int32_t lines = 1;
    for (const char *c = out; *c; c ++) {
        lines += *c == '\n';
    }

    test_int(lines, 1 + 32 + 1);
    ecs_os_free(out);

    ecs_fini(world);
}

void Portal_help_lists_portal(void) {
    // command/portal
    ecs_entity_t camera, view;
    ecs_world_t *world = portal_cmd_world(&camera, &view);

    test_assert(ecs_lookup(world, "box.command.commands.portal") != 0);
    test_assert(ecs_lookup(world, "box.command.commands.portal.cull") != 0);
    test_assert(ecs_lookup(world, "box.command.commands.portal.freeze") != 0);
    test_assert(ecs_lookup(world, "box.command.commands.portal.lines") != 0);
    test_assert(ecs_lookup(world, "box.command.commands.portal.areas") != 0);
    test_assert(ecs_lookup(world, "box.command.commands.portal.occluders") != 0);
    test_assert(ecs_lookup(world, "box.command.commands.portal.tree") != 0);
    test_assert(ecs_lookup(world, "box.command.commands.portal.status") != 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, s, "help portal.lines", NULL);
    test_assert(strstr(out, "portal.lines [state] [view]") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}
