/**
 * @file portal/freeze.c
 * @brief The cameras of the views whose traversal is frozen.
 */

#include "portal.h"

bool box_portal_freeze(
    const box_portal_index_t *index,
    ecs_entity_t view,
    bool freeze,
    box_portal_camera_t *camera)
{
    ecs_map_t *frozen = ECS_CONST_CAST(ecs_map_t*, &index->frozen);
    ecs_map_val_t *slot = ecs_map_get(frozen, view);
    bool result = false;

    if (!freeze) {
        if (slot) {
            ecs_os_free((box_portal_camera_t*)(uintptr_t)slot[0]);
            ecs_map_remove(frozen, view);
        }
    } else if (slot) {
        *camera = *(const box_portal_camera_t*)(uintptr_t)slot[0];
        result = true;
    } else {
        box_portal_camera_t *kept = ecs_os_malloc_t(box_portal_camera_t);

        *kept = *camera;
        ecs_map_insert(frozen, view, (ecs_map_val_t)(uintptr_t)kept);
    }

    return result;
}

void box_portal_freeze_fini(
    box_portal_index_t *index)
{
    ecs_map_iter_t it = ecs_map_iter(&index->frozen);

    while (ecs_map_next(&it)) {
        ecs_os_free((box_portal_camera_t*)(uintptr_t)ecs_map_value(&it));
    }

    ecs_map_fini(&index->frozen);
}
