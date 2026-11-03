/**
 * @file particle/timeline.h
 * @brief The rate and start speed curves of ParticleTimeline over the playback clock, evaluated
 * on the CPU by the curve module for the render extract.
 */

#ifndef BOX_PARTICLE_TIMELINE_H
#define BOX_PARTICLE_TIMELINE_H

#include <box.h>

/** Get the seconds of emission a frame owes at the rate of an emitter: the seconds of the frame,
 * or with a rate curve on a ParticleTimeline the area under it, at or above 0, over the frame
 * of the playback clock, once and then its last key or with loop, across its wraps.
 *
 * @param timeline The ParticleTimeline of the emitter, NULL for none
 * @param t0 Playback microseconds before the frame
 * @param t1 Playback microseconds after the frame
 * @param delta Seconds of the frame, the result without a curve
 * @return The seconds to multiply ParticleEmitter.rate with
 */
double box_particle_timeline_seconds(
    const ParticleTimeline *timeline,
    uint64_t t0,
    uint64_t t1,
    double delta);

/** Get the multiplier of the start speed of a ParticleTimeline at a time of the playback clock.
 *
 * @param timeline The ParticleTimeline of the emitter, NULL for none
 * @param time Microseconds of the playback clock
 * @return The multiplier, 1 without a curve
 */
float box_particle_timeline_speed(
    const ParticleTimeline *timeline,
    uint64_t time);

#endif
