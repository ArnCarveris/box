/**
 * @file sky/sky_internal.h
 * @brief Private state of the box.sky module: the shaders and the table invalidation.
 */

#ifndef BOX_SKY_INTERNAL_H
#define BOX_SKY_INTERNAL_H

#include <box.h>
#include <box/sky.h>
#include "../shader/shader_internal.h"

/** Module wide registry of the atmosphere, stored as a singleton */
typedef struct SkyState {
    ecs_query_t *atmospheres; /**< Entities holding an Atmosphere component */
    ecs_entity_t lut_shader;  /**< Shader entity of the table passes */
    ecs_entity_t sky_shader;  /**< Shader entity of the sky and haze pass */
    box_sky_t last;           /**< Parameter block the sky view table was rendered from */
    float altitude;           /**< Altitude in kilometers that table was rendered for */
    uint32_t generation;      /**< Rises every time that table is rendered again */
    bool valid;               /**< A packet with an atmosphere was produced before */
} SkyState;

extern ECS_COMPONENT_DECLARE(SkyState);

/** Find the atmosphere of a world.
 *
 * @param world The world
 * @return The first entity holding an Atmosphere, 0 when the world has none
 */
ecs_entity_t box_sky_entity(
    ecs_world_t *world);

/** Copy the atmosphere of a world into a packet and mark what must be rendered.
 *
 * The views are already in the packet, so the sky view table follows the camera
 * of the first one and the environment generation of every view that takes its
 * image based lighting from the sky is mixed with the generation of that table.
 *
 * @param world The world
 * @param p The packet
 */
void box_sky_extract(
    ecs_world_t *world,
    box_packet_t *p);

#endif
