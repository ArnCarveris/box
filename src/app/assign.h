/**
 * @file app/assign.h
 * @brief The script function flecs.script.on.assign of box.app.
 */

#ifndef BOX_APP_ASSIGN_H
#define BOX_APP_ASSIGN_H

#include <box.h>

/** Register flecs.script.on.assign and the observer that resolves its waits.
 *
 * @param world The world
 */
void box_app_assign_init(
    ecs_world_t *world);

#endif
