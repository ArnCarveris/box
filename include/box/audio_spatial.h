/**
 * @file box/audio_spatial.h
 * @brief Spatial audio: the listener, spatial sources and emitters.
 */

#ifndef BOX_AUDIO_SPATIAL_H
#define BOX_AUDIO_SPATIAL_H

#include "types.h"

#ifdef BOX_AUDIO_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** The ears of the world: sources with AudioSpatial are heard from its WorldTransform.
 *
 * Without one the camera of the first enabled View that has one listens. With several the
 * first, by entity id, listens and one warning is logged.
 */
extern ECS_TAG_DECLARE(AudioListener);

/** Places an AudioSource in the world: it follows the WorldTransform of the entity, gets quieter
 * with its distance d from the listener and pans with its direction. On a sound it sets the
 * distances of box_audio_play_at.
 *
 * The gain is min / (min + rolloff * (d - min)) beyond min_distance, faded to 0 at max_distance.
 * A cutoff above 0 closes a 12 dB per octave lowpass exponentially with distance, from
 * BOX_AUDIO_SPATIAL_OPEN at min_distance to cutoff at max_distance.
 *
 * A looping source beyond max_distance uses no voice and keeps its place in time; a source that
 * does not loop does not start beyond it.
 */
ECS_STRUCT(AudioSpatial, {
    float min_distance;    /**< Full volume within this distance, default 1 */
    float max_distance;    /**< Silent from this distance, default 50 */
    float rolloff;         /**< How fast the volume falls beyond min_distance, default 1 */
    float cutoff;          /**< Low-pass cutoff in Hz at max_distance, 0 for no filter,
                            *   default 0; clamped to BOX_AUDIO_SPATIAL_CUTOFF_MIN, and one of
                            *   at least BOX_AUDIO_SPATIAL_OPEN filters nothing */
});

/** Volume an AudioEmitter places its plays in, in the space of the emitter entity, with the
 * conventions of EmitterShape. A zero radius or zero extents play at the origin */
ECS_ENUM(AudioEmitterShape, {
    AudioEmitterPoint = 0,      /**< The origin of the emitter */
    AudioEmitterDisc = 1,       /**< Inside the disc of radius around the origin in the XZ
                                 *   plane */
    AudioEmitterSphere = 2,     /**< Inside the sphere of radius around the origin */
    AudioEmitterHemisphere = 3, /**< Inside the upper half (+Y) of the sphere of radius */
    AudioEmitterBox = 4         /**< Inside the box of half size extents around the origin */
});

/** Plays a sound once at random intervals, at a random point of a shape, with a random volume
 * and pitch, seeded from seed, the entity and the count of plays: insects, drips and rain.
 *
 * The first play comes after an interval, on the clock of the output. Plays go the way of
 * box_audio_play_at, on the AudioOutput and with the AudioSpatial of the emitter, else of the
 * sound; its WorldTransform places the shape, without one the plays are not spatial.
 *
 * Plays beyond max_distance, over max_active or of a sound that is not ready are skipped. A
 * Disabled or deleted emitter and playing false stop new plays; playing true starts over with
 * a fresh interval. Changes apply from the next interval.
 */
ECS_STRUCT(AudioEmitter, {
    ecs_entity_t sound;    /**< AudioPcm, SoundFile or SynthSound to play */
    float interval_min;    /**< Seconds between plays, lower bound, default 1; at least
                            *   0.01 */
    float interval_max;    /**< Seconds between plays, upper bound, default 4; swapped with
                            *   interval_min when below it */
    AudioEmitterShape shape; /**< Volume plays are placed in, default AudioEmitterPoint */
    float radius;          /**< Radius of disc, sphere and hemisphere, default 0; its absolute
                            *   value */
    vec3 extents;          /**< Half size of the box, default {0, 0, 0}; their absolute
                            *   values */
    float volume_min;      /**< Linear gain, lower bound, default 1; at least 0 */
    float volume_max;      /**< Linear gain, upper bound, default 1 */
    float pitch_min;       /**< Playback ratio, lower bound, default 1 */
    float pitch_max;       /**< Playback ratio, upper bound, default 1 */
    int32_t max_active;    /**< Plays of this emitter that may sound at once, 0 for no limit,
                            *   default 0 */
    uint32_t seed;         /**< Seed of the randomness, default 0 */
    bool playing;          /**< Whether it plays, default true */
});

/** Cutoff in Hz of an open distance filter, where it starts at min_distance */
#define BOX_AUDIO_SPATIAL_OPEN 20000.0f

/** Lowest cutoff in Hz of a distance filter */
#define BOX_AUDIO_SPATIAL_CUTOFF_MIN 10.0f

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
