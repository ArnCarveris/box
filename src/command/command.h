/**
 * @file command/command.h
 * @brief Registration of the box commands and the helpers shared by the box.command sources.
 */

#ifndef BOX_COMMAND_INTERNAL_H
#define BOX_COMMAND_INTERNAL_H

#include <box.h>
#include "../app/app.h"

/** Register a command as a script function in box.command.commands.
 *
 * @param world The world
 * @param brief One line description, stored as the doc brief that help lists
 * @param desc The function, its parent is set to box.command.commands
 * @return The function entity
 */
ecs_entity_t box_command_register(
    ecs_world_t *world,
    const char *brief,
    ecs_function_desc_t desc);

/** Get the scope of the session that runs the current command.
 *
 * @param world The world
 * @return The scope, 0 for the root or outside of a command
 */
ecs_entity_t box_command_scope(
    const ecs_world_t *world);

/** Install the log capture and register the logs command.
 *
 * @param world The world
 */
void box_logs_init(
    ecs_world_t *world);

/** Register the key, mouse, input, clipboard and action commands.
 *
 * @param world The world
 */
void box_cmd_input_init(
    ecs_world_t *world);

/** Register the capture and render commands and the system that completes renders.
 *
 * @param world The world
 */
void box_cmd_render_init(
    ecs_world_t *world);

/** Register the fps and frames commands that print the frame timing.
 *
 * @param world The world
 */
void box_cmd_frames_init(
    ecs_world_t *world);

/** Register the edit command that opens and closes the editor.
 *
 * @param world The world
 */
void box_cmd_edit_init(
    ecs_world_t *world);

#endif
