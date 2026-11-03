/**
 * @file box/audio.h
 * @brief Data driven audio: buses, sources, one shot plays, settings, status and captures.
 *
 * Components that a script, the REST API or the set command write, which one system at the end
 * of the frame turns into one command batch for the audio core (docs/audio.md). The same
 * script plays the same on native, on the web and headless.
 */

#ifndef BOX_AUDIO_H
#define BOX_AUDIO_H

#include "types.h"

#ifdef BOX_AUDIO_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Longest capture in seconds */
#define BOX_AUDIO_CAPTURE_MAX (60.0f)

/** What the audio output does */
ECS_ENUM(AudioState, {
    AudioOff = 0,               /**< No output: a world that runs no app, or --no-audio */
    AudioNoDevice = 1,          /**< No device, or it was lost: the main thread renders the
                                 *   frame deltas, as headless runs do */
    AudioWaitingForGesture = 2, /**< The page waits for a key or a click to start its audio;
                                 *   plays that do not loop are dropped meanwhile */
    AudioRunning = 3,           /**< A device or the AudioWorklet of the page plays */
    AudioFailed = 4,            /**< The page could not start its audio, it runs silent */
    AudioStarting = 5,          /**< The page loads the worklet of the core */
    AudioSuspended = 6          /**< The page is hidden: its audio faded out and suspended */
});

/** A mixer bus. Sources and plays mix into a bus, a bus into its output, the master last.
 *
 * Up to 31 buses besides the master. A cycle through outputs and sends logs one error that
 * names the buses and keeps the last valid order. A Disabled bus is silent.
 */
ECS_STRUCT(AudioBus, {
    ecs_entity_t output;   /**< Bus it feeds, 0 (or a deleted bus) for the master */
    float volume;          /**< Linear gain, default 1 */
    float pan;             /**< -1 left to 1 right, default 0 */
    bool mute;             /**< Silence the bus, default false */
    ecs_entity_t send;     /**< Bus that also receives the output, after the volume, 0 for none */
    float send_level;      /**< Linear gain of the send, default 0 */
});

/** The bus a sound or a source plays on. On a source it wins over the one of its sound */
ECS_STRUCT(AudioOutput, {
    ecs_entity_t bus;      /**< Bus entity, 0 for the master */
});

/** Samples of a sound, the sound kind of this module. Owns its samples */
ECS_STRUCT(AudioPcm, {
    ecs_vec(float) samples; /**< Frames, interleaved when stereo */
    int32_t channels;      /**< 1 or 2, 0 for 1 */
    float sample_rate;     /**< Frames per second, 0 for the rate of the output */
});

/** A sound that plays while the entity exists and playing is true.
 *
 * A sound that does not loop sets playing to false when it ends; set it to true to play again.
 * A Disabled source is silent and deleting a source stops it with a short fade.
 */
ECS_STRUCT(AudioSource, {
    ecs_entity_t sound;    /**< The sound to play, an entity with AudioPcm */
    float volume;          /**< Linear gain, default 1 */
    float pitch;           /**< Ratio of the playback speed, default 1 */
    bool loop;             /**< Play the sound over and over, default false */
    bool playing;          /**< Whether it plays, default true */
});

/** The ears of the world: sources with AudioSpatial are heard from its WorldTransform.
 *
 * Without one the camera of the first enabled View that has one listens. With several the
 * first, by entity id, listens and one warning is logged.
 */
extern ECS_TAG_DECLARE(AudioListener);

/** Places an AudioSource in the world: it follows the WorldTransform of the entity, parents
 * included, gets quieter with its distance from the listener and pans with its direction.
 *
 * The gain is inverse distance, min / (min + rolloff * (d - min)) beyond min_distance, faded so
 * that it is exactly 0 at max_distance. Front and back sound alike.
 *
 * A looping source beyond max_distance uses no voice and keeps its place in time; a source
 * that does not loop does not start beyond it. On a sound it sets the distances of
 * box_audio_play_at.
 */
ECS_STRUCT(AudioSpatial, {
    float min_distance;    /**< Full volume within this distance, default 1 */
    float max_distance;    /**< Silent from this distance, default 50 */
    float rolloff;         /**< How fast the volume falls beyond min_distance, default 1 */
});

/** Settings of the audio of the world, a singleton */
ECS_STRUCT(AudioSettings, {
    float master_volume;   /**< Linear gain of the master bus, default 1 */
    bool mute;             /**< Silence everything, default false */
});

/** What the audio output does, a singleton written by box.audio and read only for apps */
ECS_STRUCT(AudioStatus, {
    AudioState state;      /**< What the output does */
    int32_t sample_rate;   /**< Frames per second of the output, 0 while not known */
    int32_t voices;        /**< Voices that play, without the ones that fade out */
    int64_t voices_stolen; /**< Voices the voice cap of 64 stopped so far */
    float load;            /**< Share of real time the render takes, 0 where it is not
                            *   measured (the web) */
    int32_t buses;         /**< Buses in the processing order, the master included */
});

/** Give an entity the samples of a sound, to play with AudioSource or box_audio_play.
 *
 * @param world The world
 * @param sound The entity, 0 to create one
 * @param data Frames, interleaved when stereo, copied
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param sample_rate Frames per second, 0 for the rate of the output
 * @return The sound, 0 when the arguments are invalid
 */
ecs_entity_t box_audio_sound_from_pcm(
    ecs_world_t *world,
    ecs_entity_t sound,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate);

/** Play a sound once on the bus of its AudioOutput, without an entity per play.
 *
 * The play goes out with the command batch at the end of the frame. While the page waits for a
 * gesture it is dropped. Scripts call it as box.audio.play(sound, volume).
 *
 * @param world The world
 * @param sound The sound, an entity with AudioPcm
 * @param volume Linear gain
 * @return Whether the play was queued
 */
bool box_audio_play(
    ecs_world_t *world,
    ecs_entity_t sound,
    float volume);

/** Play a sound once at a place in the world, heard from the listener as an AudioSpatial
 * source that stays where it started.
 *
 * The distances are those of the AudioSpatial of the sound, the defaults without one. A play
 * beyond max_distance of the listener does not start. Scripts call it as
 * box.audio.play_at(sound, position, volume).
 *
 * @param world The world
 * @param sound The sound, an entity with AudioPcm
 * @param position World position of the play
 * @param volume Linear gain
 * @return Whether the play was queued
 */
bool box_audio_play_at(
    ecs_world_t *world,
    ecs_entity_t sound,
    const vec3 position,
    float volume);

/** Gain and pan of a spatial source, as box.audio computes them.
 *
 * The pan is the component of the direction along the right axis of the listener over the
 * distance; within min_distance it is divided by min_distance instead, so a source that passes
 * through the listener moves across instead of jumping from one side to the other.
 *
 * @param spatial The distances, NULL for the defaults
 * @param listener World transform of the listener
 * @param position World position of the source
 * @param pan Receives the pan, -1 left to 1 right, may be NULL
 * @return The gain, 0 at and beyond max_distance
 */
float box_audio_spatial_gain(
    const AudioSpatial *spatial,
    const mat4 listener,
    const vec3 position,
    float *pan);

/** Record the master output of the next seconds to a 16 bit stereo WAV file.
 *
 * The capture taps the core wherever it runs, so it works with every output. The file is
 * written in one rename once the frames are in. Headless with a fixed delta time two runs
 * write identical files.
 *
 * @param world The world
 * @param seconds Seconds to record, at most BOX_AUDIO_CAPTURE_MAX
 * @param path The file
 * @return 0 when the capture started, nonzero with an error logged when it did not
 */
int box_audio_capture(
    ecs_world_t *world,
    float seconds,
    const char *path);

/** Check whether a capture still records.
 *
 * @param world The world
 * @return Whether a capture has not written its file yet
 */
bool box_audio_capture_pending(
    const ecs_world_t *world);

/** Register the box.audio module. Importing it opens no device.
 *
 * @param world The world
 */
void BoxAudioImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
