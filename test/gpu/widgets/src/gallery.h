/**
 * @file gallery.h
 * @brief Loads the widget gallery scene and drives it through the command API.
 */

#ifndef BOX_WIDGETS_TEST_GALLERY_H
#define BOX_WIDGETS_TEST_GALLERY_H

#include <box.h>

/** Path of the header of the gallery, above the page */
#define HEADER "gallery.hud.root.header"

/** Path of the page of the gallery, the scroll view of the sections */
#define PAGE "gallery.hud.root.page"

/** Path of the Basics panel of the gallery */
#define BASICS PAGE ".top.basics"

/** Path of the Text panel of the gallery */
#define TEXT PAGE ".top.text"

/** Path of the Containers panel of the gallery */
#define CONTAINERS PAGE ".bottom.containers"

/** Path of the Form panel of the gallery */
#define FORM PAGE ".bottom.form"

/** Load the gallery scene into a new headless world and open a terminal session.
 *
 * @param session Receives the session
 * @return The world
 */
ecs_world_t* gallery_load(
    ecs_entity_t *session);

/** Run a command line that must succeed.
 *
 * @param world The world
 * @param session The terminal session
 * @param fmt Format of the command line
 * @param ... Arguments of the format
 * @return The output of the command, owned by the caller
 */
char* gallery_run(
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
void gallery_expect(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    const char *expect);

/** Run the input commands queued so far: step until the input queue is drained and the frames
 * after it have applied the events and the reactive updates.
 *
 * @param world The world
 * @param session The terminal session
 */
void gallery_settle(
    ecs_world_t *world,
    ecs_entity_t session);

/** Run the input commands queued so far with a number of frames after the queue is drained.
 *
 * @param world The world
 * @param session The terminal session
 * @param frames Frames to step after the queue is drained
 */
void gallery_settle_frames(
    ecs_world_t *world,
    ecs_entity_t session,
    int32_t frames);

/** Get the window point at a place in the bounds of a widget.
 *
 * @param world The world
 * @param path Path of the widget
 * @param fx Place across the width, -1 the left edge, 0 the center, 1 the right edge
 * @param fy Place across the height, -1 the top edge, 0 the center, 1 the bottom edge
 * @param out Receives the point as [x, y]
 * @param size Bytes of out
 */
void gallery_point(
    ecs_world_t *world,
    const char *path,
    float fx,
    float fy,
    char *out,
    size_t size);

/** Get the window rectangle of an element.
 *
 * @param world The world
 * @param path Path of the element
 * @param r Receives left, top, right and bottom in window points
 */
void gallery_rect(
    ecs_world_t *world,
    const char *path,
    float r[4]);

/** Scroll the page of the gallery as little as it takes to show all of an element, as far as
 * the page view is large enough, and run the frames that move it.
 *
 * @param world The world
 * @param session The terminal session
 * @param path Path of the element
 */
void gallery_reveal(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path);

/** Scroll the page of the gallery so that the top left of an element is at the top left of the
 * page view, as far as the page scrolls, and run the frames that move it.
 *
 * @param world The world
 * @param session The terminal session
 * @param path Path of the element
 */
void gallery_show(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path);

/** Move the cursor to a place on a widget and check that PickStatus reports a handler there.
 *
 * Scrolls the page first when the widget is not all in view.
 *
 * @param world The world
 * @param session The terminal session
 * @param path Path of the widget
 * @param fx Place across the width, as gallery_point
 * @param fy Place across the height, as gallery_point
 * @param handler Path of the handler PickStatus must report, "#0" for none
 */
void gallery_hover(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    float fx,
    float fy,
    const char *handler);

/** Click the left mouse button on a place of a widget after checking the handler there.
 *
 * @param world The world
 * @param session The terminal session
 * @param path Path of the widget
 * @param fx Place across the width, as gallery_point
 * @param fy Place across the height, as gallery_point
 * @param handler Path of the handler PickStatus must report, "#0" for none
 */
void gallery_click(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    float fx,
    float fy,
    const char *handler);

/** Press a key and run the input.
 *
 * @param world The world
 * @param session The terminal session
 * @param key The key, as the key.press command names it
 */
void gallery_key(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *key);

/** Get the entity with the keyboard focus.
 *
 * @param world The world
 * @param session The terminal session
 * @return Its path, "#0" for none, owned by the caller
 */
char* gallery_focused(
    ecs_world_t *world,
    ecs_entity_t session);

/** Get a member of the mut of a widget, as the get command prints it.
 *
 * @param world The world
 * @param session The terminal session
 * @param path Path of the widget below the Basics panel
 * @param tmpl Template of the widget, such as Slider
 * @param member Member of the mut
 * @return The value, owned by the caller
 */
char* gallery_mut(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    const char *tmpl,
    const char *member);

/** Get a floating point member of the mut of a widget.
 *
 * @param world The world
 * @param session The terminal session
 * @param path Path of the widget below the Basics panel
 * @param tmpl Template of the widget
 * @param member Member of the mut
 * @return The value
 */
double gallery_number(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    const char *tmpl,
    const char *member);

/** Tab until an entity has the focus, at most 40 times.
 *
 * @param world The world
 * @param session The terminal session
 * @param path Path of the entity
 */
void gallery_tab_to(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path);

/** Check the entity with the keyboard focus.
 *
 * @param world The world
 * @param session The terminal session
 * @param path Its path, "#0" for none
 */
void gallery_expect_focus(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path);

/** Run a command line that must succeed and read its output as a number.
 *
 * @param world The world
 * @param session The terminal session
 * @param fmt Format of the command line
 * @param ... Arguments of the format
 * @return The number the command printed
 */
double gallery_value(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *fmt,
    ...);

/** Get the number of changes the status line of the gallery counted.
 *
 * @param world The world
 * @param session The terminal session
 * @return The number of on.change events below the root
 */
int32_t gallery_changes(
    ecs_world_t *world,
    ecs_entity_t session);

#endif
