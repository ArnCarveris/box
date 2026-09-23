/**
 * @file particle/playback.c
 * @brief The clock of an emitter and the burst schedules that ride on it.
 *
 * ParticlePlayback holds the state and clock of an emitter, and ParticleBurst holds a
 * schedule that fires on that clock. The clock only runs while the emitter plays.
 *
 * The bursts of a frame are the difference between the burst count at the end of this
 * frame and the previous one, so nothing is carried between frames. A burst may sit on
 * the emitter or any of its descendants.
 */

#include "particle_internal.h"
#include <math.h>

/** Particles one burst may ask for, which is the ceiling of the pool as well */
#define BOX_PARTICLE_BURST_MAX (BOX_PARTICLE_MAX)

/** Reset the clock and the spawn accounting of an emitter.
 *
 * @param impl The bookkeeping
 */
static void box_particle_rewind(
    ParticleEmitterImpl *impl)
{
    impl->time = 0;
    impl->prev_time = 0;
    impl->accumulator = 0;
}

int32_t box_particle_playback(
    ParticlePlayback *playback,
    ParticleEmitterImpl *impl,
    float delta,
    uint32_t *flags)
{
    int32_t result = ParticlePlaying;
    if (playback) {
        result = playback->state;
    }

    if (result != impl->state && result == ParticlePlaying &&
        impl->state == ParticleStopped)
    {
        box_particle_rewind(impl);
    }

    impl->state = result;
    if (playback && playback->reset) {
        box_particle_rewind(impl);
        playback->reset = false;
        *flags |= BOX_PARTICLE_RESET;
    }

    impl->burst = 0;
    impl->prev_time = impl->time;
    if (result == ParticlePlaying && delta > 0.0f) {
        impl->time += (double)delta;
    }

    if (result == ParticlePaused) {
        *flags |= BOX_PARTICLE_PAUSED;
    }

    if (playback) {
        playback->time = impl->time;
    }

    return result;
}

int32_t box_particle_due(
    const ParticleBurst *burst,
    double from,
    double to)
{
    int32_t result = 0;
    if (burst->count <= 0 || to <= from) {
        goto done;
    }

    if (burst->at <= 0.0f) {
        result = from <= 0.0 ? 1 : 0;
        goto done;
    }

    double period = (double)burst->at;
    double limit = burst->repeat < 0 ? -1.0 : (double)burst->repeat + 1.0;
    double before = floor(from / period);
    double after = floor(to / period);
    if (limit >= 0.0) {
        before = before > limit ? limit : before;
        after = after > limit ? limit : after;
    }

    result = (int32_t)(after - before);
done:
    return result;
}

/** Add the particles one burst asks for this frame to its emitter.
 *
 * @param burst The schedule
 * @param impl The bookkeeping of the emitter it belongs to
 */
static void box_particle_burst_add(
    const ParticleBurst *burst,
    ParticleEmitterImpl *impl)
{
    if (impl->state != ParticlePlaying) {
        goto done;
    }

    int32_t times = box_particle_due(burst, impl->prev_time, impl->time);
    if (times <= 0) {
        goto done;
    }

    double asked = (double)impl->burst + (double)times * (double)burst->count;
    if (asked > (double)BOX_PARTICLE_BURST_MAX) {
        asked = (double)BOX_PARTICLE_BURST_MAX;
    }

    impl->burst = (uint32_t)asked;
done:
    return;
}

void box_particle_bursts(
    ecs_world_t *world,
    const ParticleState *state)
{
    if (!state->bursts) {
        goto done;
    }

    ecs_iter_t it = ecs_query_iter(world, state->bursts);
    while (ecs_query_next(&it)) {
        const ParticleBurst *burst = ecs_field(&it, ParticleBurst, 0);
        ParticleEmitterImpl *impl = ecs_field(&it, ParticleEmitterImpl, 1);
        int32_t i;
        if (!ecs_field_is_self(&it, 1)) {
            for (i = 0; i < it.count; i ++) {
                box_particle_burst_add(&burst[i], impl);
            }

            continue;
        }

        for (i = 0; i < it.count; i ++) {
            box_particle_burst_add(&burst[i], &impl[i]);
        }
    }

done:
    return;
}
