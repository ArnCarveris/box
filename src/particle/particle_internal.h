/**
 * @file particle/particle_internal.h
 * @brief Private state of the box.particle module: the spawn clock and the shape tables.
 *
 * The module owns no particles. It keeps the spawn accounting of every emitter,
 * the sampled table of the shapes that need one and the two shader entities the
 * renderer builds its passes from.
 */

#ifndef BOX_PARTICLE_INTERNAL_H
#define BOX_PARTICLE_INTERNAL_H

#include <box.h>
#include <box/particle.h>
#include "../shader/shader_internal.h"

/** Bookkeeping the module keeps next to every ParticleEmitter.
 *
 * The accumulator carries the fraction of a particle the rate did not fill this
 * frame into the next one, so a fixed timestep spawns exactly rate particles
 * per second. The table holds the sampled points of a mesh or a spline shape
 * and is filled again when the shape, the source or the seed changes.
 */
typedef struct ParticleEmitterImpl {
    double accumulator;   /**< Fraction of a particle carried into the next frame */
    double time;          /**< Seconds the emitter has been playing, the clock of its bursts */
    double prev_time;     /**< Clock at the end of the previous frame, which dates a burst */
    uint32_t burst;       /**< Particles the bursts of the emitter ask for this frame */
    uint32_t flags;       /**< Playback bits the block of this frame carries */
    uint32_t spawned;     /**< Particles the emitter spawned since it started */
    uint32_t max;         /**< Pool size of the previous frame, a change resets the pool */
    uint32_t id;          /**< Pool of the emitter in the renderer */
    uint32_t points;      /**< Points the table holds */
    uint32_t seed;        /**< Seed the table was sampled with */
    int32_t shape;        /**< Shape the table was sampled for */
    int32_t state;        /**< Playback state of the previous frame, a PlaybackState */
    ecs_entity_t source;  /**< Source entity the table was sampled from */
    box_particle_point_t table[BOX_PARTICLE_POINTS]; /**< Sampled points of the shape */
} ParticleEmitterImpl;

extern ECS_COMPONENT_DECLARE(ParticleEmitterImpl);

/** Module wide registry of the emitters, stored as a singleton */
typedef struct ParticleState {
    ecs_query_t *emitters;    /**< Entities holding a ParticleEmitter */
    ecs_query_t *bursts;      /**< Burst schedules with the emitter they belong to */
    ecs_entity_t sim_shader;  /**< Shader entity of the spawn and the integrate pass */
    ecs_entity_t draw_shader; /**< Shader entity of the point sprite pass */
} ParticleState;

extern ECS_COMPONENT_DECLARE(ParticleState);

/** Write the emitters of a world into a frame packet.
 *
 * A world without the module or without an emitter leaves the packet alone,
 * which is what keeps a scene that has no particles unchanged.
 *
 * @param world The world
 * @param p The packet
 */
void box_particle_extract(
    ecs_world_t *world,
    box_packet_t *p);

/** Sample the spawn points of the mesh or spline shape of an emitter.
 *
 * The table is left alone while the shape, the source entity and the seed stay
 * the same, so the sampling runs once instead of once per frame.
 *
 * @param world The world
 * @param emitter The emitter
 * @param impl Its bookkeeping, which holds the table
 */
void box_particle_sample(
    ecs_world_t *world,
    const ParticleEmitter *emitter,
    ParticleEmitterImpl *impl);

/** Advance the clock of an emitter and answer its playback state.
 *
 * The clock only runs while the emitter plays, a start after a stop puts it
 * back to zero and a reset trigger empties the pool and is written back to
 * false, so the times the bursts of the emitter go off at are the seconds it
 * has been playing.
 *
 * @param playback The playback of the emitter, NULL when it has none
 * @param impl Its bookkeeping, which holds the clock
 * @param delta Seconds the frame advanced the world
 * @param flags Receives the pause and reset bits of the block
 * @return The state the emitter is in, a PlaybackState
 */
int32_t box_particle_playback(
    ParticlePlayback *playback,
    ParticleEmitterImpl *impl,
    float delta,
    uint32_t *flags);

/** Times a burst schedule goes off between two readings of the clock.
 *
 * A burst goes off `at` seconds into the playback and `repeat` more times every
 * `at` seconds after that; a burst without a time goes off once, on the first
 * frame the emitter plays.
 *
 * @param burst The schedule
 * @param from Clock at the end of the previous frame
 * @param to Clock at the end of this frame
 * @return The number of times the burst goes off
 */
int32_t box_particle_due(
    const ParticleBurst *burst,
    double from,
    double to);

/** Add what the bursts of every emitter of a world ask for to its bookkeeping.
 *
 * @param world The world
 * @param state The registry holding the burst query
 */
void box_particle_bursts(
    ecs_world_t *world,
    const ParticleState *state);

/** Register the playback functions of the module under box.particle.
 *
 * @param world The world
 * @param module The module entity the functions live under
 */
void box_particle_functions_init(
    ecs_world_t *world,
    ecs_entity_t module);

#endif
