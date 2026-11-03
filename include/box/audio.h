/**
 * @file box/audio.h
 * @brief Data driven audio: buses, sources, sound files, music streams, one shot plays,
 * settings, status and captures.
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

/** A sound decoded from a WAV or FLAC file on the job system, found as a TexturePath is.
 *
 * Decoded fully at the rate of the file, mono or stereo, more channels mixed down; every
 * SoundFile with the same path shares one copy. A play of a sound that still loads starts once
 * ready when it loops and is dropped otherwise. On the web the decode stalls its frame.
 */
ECS_STRUCT(SoundFile, {
    char *path;            /**< Audio file, resolved as a texture path is */
});

/** Progress of the load of a SoundFile */
ECS_ENUM(SoundState, {
    SoundLoading = 0,      /**< The file decodes */
    SoundReady = 1,        /**< The samples are decoded and play */
    SoundFailed = 2        /**< The load failed, error says why */
});

/** Outcome of the load of a SoundFile, written by box.audio and read only for applications */
ECS_STRUCT(SoundStatus, {
    SoundState state;      /**< Progress of the load */
    int32_t frames;        /**< Frames of the decoded sound, 0 unless ready */
    int32_t channels;      /**< 1 or 2 after the mix down, 0 unless ready */
    float sample_rate;     /**< Frames per second of the file, 0 unless ready */
    char *error;           /**< Why the load failed, NULL unless state is SoundFailed */
});

/** A sound that plays while the entity exists and playing is true.
 *
 * A sound that does not loop sets playing to false when it ends; set it to true to play again.
 * A Disabled source is silent and deleting a source stops it with a short fade.
 */
ECS_STRUCT(AudioSource, {
    ecs_entity_t sound;    /**< The sound to play, an entity with AudioPcm or SoundFile */
    float volume;          /**< Linear gain, default 1 */
    float pitch;           /**< Ratio of the playback speed, default 1 */
    bool loop;             /**< Play the sound over and over, default false */
    bool playing;          /**< Whether it plays, default true */
});

/** Music: one Ogg Vorbis file that streams while the entity exists and playing is true.
 *
 * Decoded ahead in chunks, never whole: on a job natively, on the main thread for a bounded
 * time per frame on the web, which fetches the file next to the page if its file system lacks it.
 *
 * One stream plays at once; a second one logs one error and is silent. A new path fades the old
 * stream out and starts the new one. At its end a stream that does not loop sets playing to
 * false; a delete fades it out and frees it. With an AudioOutput it plays on that bus.
 */
ECS_STRUCT(AudioStream, {
    char *path;            /**< An .ogg file, found as a TexturePath is */
    float volume;          /**< Linear gain, default 1 */
    bool loop;             /**< Start over at the end without a gap, default true */
    bool playing;          /**< Whether it plays, default true; false fades it out and true
                            *   resumes it where it stopped */
});

/** Progress of an AudioStream */
ECS_ENUM(AudioStreamState, {
    AudioStreamLoading = 0,  /**< The file is fetched or opened, or decodes ahead of its start */
    AudioStreamPlaying = 1,  /**< It plays */
    AudioStreamStopped = 2,  /**< playing is false, or no output plays it */
    AudioStreamEnded = 3,    /**< A stream that does not loop played to its end */
    AudioStreamFailed = 4    /**< It cannot play, error says why */
});

/** What an AudioStream does, written by box.audio and read only for applications */
ECS_STRUCT(AudioStreamStatus, {
    AudioStreamState state; /**< What it does */
    int32_t frames;        /**< Frames of the file, 0 until it opened */
    int32_t channels;      /**< 1 or 2 after the mix down, 0 until it opened */
    float sample_rate;     /**< Frames per second of the file, 0 until it opened */
    float position;        /**< Seconds into the file of what plays */
    float buffered;        /**< Seconds decoded ahead of what plays */
    int32_t underruns;     /**< Times the output ran out of decoded frames and faded to silence */
    float decode_ms;       /**< Milliseconds the last decode took; on the web the main thread
                            *   spends it in its frame */
    float decode_max_ms;   /**< Largest decode_ms so far */
    char *error;           /**< Why it failed, NULL unless state is AudioStreamFailed */
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
 * gesture, or while the SoundFile of the sound still loads, it is dropped. Scripts call it as
 * box.audio.play(sound, volume).
 *
 * @param world The world
 * @param sound The sound, an entity with AudioPcm or SoundFile
 * @param volume Linear gain
 * @return Whether the play was queued
 */
bool box_audio_play(
    ecs_world_t *world,
    ecs_entity_t sound,
    float volume);

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
