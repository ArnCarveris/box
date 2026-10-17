/**
 * @file showcase.h
 * @brief Loads the UI showcase scene and drives it through the command API, shared by the suites.
 */

#ifndef BOX_UISHOWCASE_TEST_SHOWCASE_H
#define BOX_UISHOWCASE_TEST_SHOWCASE_H

#include <box.h>

/** Load the showcase scene into a new headless world and open a terminal session.
 *
 * @param session Receives the session
 * @return The world
 */
ecs_world_t* showcase_load(
    ecs_entity_t *session);

/** Run a command line that must succeed.
 *
 * @param world The world
 * @param session The terminal session
 * @param fmt Format of the command line
 * @param ... Arguments of the format
 * @return The output of the command, owned by the caller
 */
char* showcase_run(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *fmt,
    ...);

/** Run a command line and compare its output.
 *
 * @param world The world
 * @param session The terminal session
 * @param line The command line
 * @param expect The output it must print
 */
void showcase_expect(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    const char *expect);

/** Step a number of frames.
 *
 * @param world The world
 * @param frames The number of frames
 */
void showcase_step(
    ecs_world_t *world,
    int32_t frames);

/** Run the input commands queued so far: step until the input queue is drained and the frames
 * after it have applied the actions and the reactive updates.
 *
 * @param world The world
 * @param session The terminal session
 */
void showcase_settle(
    ecs_world_t *world,
    ecs_entity_t session);

/** Move the cursor to a point and check the entity that handles the pointer there.
 *
 * @param world The world
 * @param session The terminal session
 * @param point The window points, as [x, y]
 * @param handler Path of the handler PickStatus must report
 */
void showcase_hover(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *point,
    const char *handler);

/** Click the left mouse button at a point and run the input.
 *
 * @param world The world
 * @param session The terminal session
 * @param point The window points, as [x, y]
 */
void showcase_click(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *point);

/** Press a key and run the input.
 *
 * @param world The world
 * @param session The terminal session
 * @param key The key, as the key.press command names it
 */
void showcase_key(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *key);

/** Get a member of the state of the showcase.
 *
 * @param world The world
 * @param session The terminal session
 * @param member The member of Showcase.mut
 * @return The value as the command prints it, owned by the caller
 */
char* showcase_state(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *member);

/** Get a floating point value a command prints.
 *
 * @param world The world
 * @param session The terminal session
 * @param line The command line
 * @return The value
 */
double showcase_value(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line);

/** Get a floating point member of the state of the showcase.
 *
 * @param world The world
 * @param session The terminal session
 * @param member The member of Showcase.mut
 * @return The value
 */
double showcase_number(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *member);

#endif
