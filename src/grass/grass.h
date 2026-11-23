/**
 * @file grass/grass.h
 * @brief The values of a Grass with its defaults resolved, shared by box.grass and the render
 * extract.
 */

#ifndef BOX_GRASS_INTERNAL_H
#define BOX_GRASS_INTERNAL_H

#include <box.h>

/** Blades per square meter of a Grass with a density of 0 or less */
#define BOX_GRASS_DENSITY (48.0f)

/** Height below which the blades of a new Grass fold into a V */
#define BOX_GRASS_FOLD_HEIGHT (0.35f)

/** Distance from the camera within which the blades of a new Grass cast shadows */
#define BOX_GRASS_SHADOW_DISTANCE (30.0f)

/** Distance between the clumps of a Grass with a clump size of 0 or less */
#define BOX_GRASS_CLUMP_SIZE (1.2f)

/** How strongly the blades of a new Grass follow their clump */
#define BOX_GRASS_CLUMP_FOLLOW (0.7f)

/** How much the color and height of a new Grass vary over the world */
#define BOX_GRASS_VARIATION (0.5f)

/** Resolve the defaults of a Grass: sizes of 0 or less and not finite take their defaults, the
 * largest of a range is at least its smallest, a clump follow and a variation that are not
 * finite take their defaults and the others are clamped to 0 to 1, a fold height that is not
 * finite takes its default and a negative one is 0, the density is clamped to BOX_GRASS_DENSITY_MAX,
 * each distance of the levels is at least the one before and the last at most
 * BOX_GRASS_DISTANCE_MAX, a mask outside 0 to 3 is -1, and a shadow distance that is not finite
 * takes its default, a negative one is 0 and a larger one than the last distance of the levels
 * is that distance.
 *
 * @param value The Grass
 * @param out Receives the resolved values
 */
void box_grass_resolve(
    const Grass *value,
    Grass *out);

/** Register the extract that appends every Grass to the render packet (render/grass.c).
 *
 * @param world The world
 */
void box_render_grass_init(
    ecs_world_t *world);

#endif
