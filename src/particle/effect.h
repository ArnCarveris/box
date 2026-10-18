/**
 * @file particle/effect.h
 * @brief Effect templates played once at a point from a pool of instances per template.
 */

#ifndef BOX_PARTICLE_EFFECT_H
#define BOX_PARTICLE_EFFECT_H

#include <box.h>

/** Register the effect pools, the system that plays and retires their instances and the
 * box.particle.burst_at script function, called by the import of box.particle after the
 * particle extract was handed the emitters.
 *
 * @param world The world
 */
void box_particle_effects_import(
    ecs_world_t *world);

#endif
