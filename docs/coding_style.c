/**
 * @file coding_style.c
 * @brief A small code example that outlines the coding style for box.
 * 
 * No more than three lines of detailed explanation on what the purpose of this 
 * file is. Ground explanation in current state of the file, this is not the place
 * for capturing design decisions, reasoning, or architecture talk.
 */

/* The maximum length of a line is 100 characters. */

#include <stdio.h>
#include <flecs.h>

/** A type describing a point */
typedef {
    float x;
    float y;
} point_t; // Private types start with lower case and end with _t

/** A component describing a line */
typedef {
    point_t start;
    point_t stop;
} Line; // Components start with capital

/** A function that sums two numbers.
 * 
 * @param a The first number
 * @param b The second number
 * @return The result of a + b
 */
int sum(
    int a, 
    int b) 
{
    return a + b;
}

/** The main function
 * 
 * @param argc Argument count.
 * @param argv Argument array
 * @return 0 if success, nonzero if not successful
 */
int main(
    int argc, 
    char *argv[]) 
{
    ecs_world_t *world = ecs_init();
    if (!world) {
        ecs_err("Failed to create world");
        goto error; // one exit point per function
    } else {
        ecs_trace("World loaded");
    }

    ecs_entity_t e = ecs_script(world, { // notice the newline before ecs_script
        .filename = "scene.flecs"
    });
    if (!e) {
        ecs_err("failed to load script");
        goto error;
    }

    return ecs_fini(world); // notice the newline before return
error:
    return -1;
}
