/**
 * @file screen_test.h
 * @brief Helpers of the tests of the world-space screens of test/assets/portal: load a script,
 * run frames of 0.1 s, look up an entity, read a text or a number, and click an element aimed
 * like a walker's crosshair.
 */

#ifndef SCREEN_TEST_H
#define SCREEN_TEST_H

#include "portal_test.h"

/** A middle dot, UTF-8 */
#define MIDDLE_DOT "\xC2\xB7"

/** A degree sign, UTF-8 */
#define DEGREE "\xC2\xB0"

/** Run a script file of the test assets.
 *
 * @param world The world
 * @param file Path of the file from the repository root
 * @param name Name of the entity that holds the script, NULL for none
 */
static void screen_load(
    ecs_world_t *world,
    const char *file,
    const char *name)
{
    char *path = box_app_locate(file);
    test_assert(path != NULL);

    ecs_entity_t holder = 0;
    if (name) {
        holder = ecs_entity(world, { .name = name });
    }

    test_assert(ecs_script(world, { .entity = holder, .filename = path }) != 0);
    ecs_os_free(path);
}

/** Run frames of 0.1 s.
 *
 * @param world The world
 * @param frames Number of frames
 */
static void screen_run(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;

    for (i = 0; i < frames; i ++) {
        ecs_progress(world, 0.1f);
    }
}

/** Look up an entity that must exist.
 *
 * @param world The world
 * @param path Its path
 * @return The entity
 */
static ecs_entity_t screen_e(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Get the text of an element.
 *
 * @param world The world
 * @param path Path of the element with the Text
 * @return The text
 */
static const char* screen_text(
    ecs_world_t *world,
    const char *path)
{
    const Text *text = ecs_get(world, screen_e(world, path), Text);
    test_assert(text != NULL);
    test_assert(text->value != NULL);

    return text->value;
}

/** Aim the camera along -z at the middle of an element from a distance, as a walker aims its
 * crosshair, and click there: the middle of the view.
 *
 * @param world The world
 * @param path Path of the element
 * @param distance Distance of the camera from the plane of the screen
 */
static void screen_click(
    ecs_world_t *world,
    const char *path,
    float distance)
{
    WorldTransform buf;
    const WorldTransform *t = box_transform_world(world, screen_e(world, path), &buf);
    test_assert(t != NULL);

    ecs_set(world, screen_e(world, "camera"), Position, {
        t->m[3][0], t->m[3][1], t->m[3][2] + distance
    });
    screen_run(world, 2);

    float middle[2] = {0.5f, 0.5f};
    box_input_mouse_click(world, KeyMouseLeft, middle, true, 2);
    screen_run(world, 6);
}

/** Get a number or bool member of a component a script declared, such as PortalBreaker.
 *
 * @param world The world
 * @param path Path of the entity with the component
 * @param component Name of the component
 * @param member Name of the member
 * @return The value, a bool as 0 or 1
 */
static double screen_get(
    ecs_world_t *world,
    const char *path,
    const char *component,
    const char *member)
{
    ecs_entity_t id = screen_e(world, component);
    const void *ptr = ecs_get_id(world, screen_e(world, path), id);
    test_assert(ptr != NULL);

    ecs_meta_cursor_t cur = ecs_meta_cursor(world, id, ECS_CONST_CAST(void*, ptr));
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_member(&cur, member), 0);

    return ecs_meta_get_float(&cur);
}

/** Set a number or bool member of a component a script declared, as game code would.
 *
 * @param world The world
 * @param path Path of the entity with the component
 * @param component Name of the component
 * @param member Name of the member
 * @param value The value, a bool as 0 or 1
 */
static void screen_set(
    ecs_world_t *world,
    const char *path,
    const char *component,
    const char *member,
    double value)
{
    ecs_entity_t id = screen_e(world, component);
    ecs_entity_t e = screen_e(world, path);
    void *ptr = ecs_get_mut_id(world, e, id);
    test_assert(ptr != NULL);

    ecs_meta_cursor_t cur = ecs_meta_cursor(world, id, ptr);
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_member(&cur, member), 0);
    test_int(ecs_meta_set_float(&cur, value), 0);
    ecs_modified_id(world, e, id);
}

/** Get the fixed width a script gave an element.
 *
 * @param world The world
 * @param path Path of the element with the LayoutItem
 * @return The width in virtual pixels
 */
static float screen_width(
    ecs_world_t *world,
    const char *path)
{
    const LayoutItem *item = ecs_get(world, screen_e(world, path), LayoutItem);
    test_assert(item != NULL);

    return item->width.value;
}

#endif
