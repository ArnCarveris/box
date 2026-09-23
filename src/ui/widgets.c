/**
 * @file ui/widgets.c
 * @brief Loads the script widget library into box.ui.widgets at import.
 */

#include "ui_internal.h"
#include "../shader/shader_internal.h"

void box_ui_widgets_load(
    ecs_world_t *world)
{
    char resolved[1024];
    if (!box_locate(BOX_UI_WIDGETS, resolved, sizeof(resolved))) {
        ecs_warn("box.ui: no widget library at '%s'", BOX_UI_WIDGETS);
        goto done;
    }

    ecs_entity_t script = ecs_entity(world, {
        .name = "widgets_script", .parent = ecs_lookup(world, "box.ui") });
    if (!ecs_script(world, { .entity = script, .filename = resolved })) {
        ecs_err("box.ui: failed to load '%s'", resolved);
    }

done:
    return;
}
