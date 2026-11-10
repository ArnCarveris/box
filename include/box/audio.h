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
#include "curve.h"
#include "audio_mixer.h"
#include "audio_synth.h"
#include "audio_effects.h"
#include "audio_spatial.h"
#include "audio_music.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest capture in seconds */
#define BOX_AUDIO_CAPTURE_MAX (60.0f)

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
 * @param sound The sound, an entity with AudioPcm, SoundFile or SynthSound
 * @param volume Linear gain
 * @return Whether the play was queued
 */
bool box_audio_play(
    ecs_world_t *world,
    ecs_entity_t sound,
    float volume);

/** Play a sound once on the bus of its AudioOutput, starting on the next beat or bar of the
 * Transport.
 *
 * It starts sample accurately on the first boundary at or after the clock of the output, after
 * a loop wrap on the timeline from loop_start on. With the transport stopped, without a
 * Transport or with AudioQuantizeNone it starts at once, as box_audio_play.
 *
 * The transport never stops or moves it once queued. The rules of box_audio_play apply.
 * Scripts call it as box.audio.play_quantized(sound, volume, quantize).
 *
 * @param world The world
 * @param sound The sound, an entity with AudioPcm, SoundFile or SynthSound
 * @param volume Linear gain
 * @param quantize Where it starts
 * @return Whether the play was queued
 */
bool box_audio_play_quantized(
    ecs_world_t *world,
    ecs_entity_t sound,
    float volume,
    AudioQuantize quantize);

/** Play a note of an Instrument, without an entity per note.
 *
 * The note starts at the clock of the output, so it applies at the next block as a play does,
 * and with a gate ends duration beats later at the tempo of the Transport (120 without one),
 * also while the transport is stopped, which does not affect the note.
 *
 * It plays on the bus of the AudioOutput of the instrument, else of its sound, else the
 * master. The rules of box_audio_play apply. Scripts call it as
 * box.audio.play_note(instrument, pitch, velocity, duration).
 *
 * @param world The world
 * @param instrument The entity with Instrument
 * @param pitch MIDI note number, a float so microtones work
 * @param velocity Velocity, 0..1
 * @param duration Beats
 * @return Whether the note was queued
 */
bool box_audio_play_note(
    ecs_world_t *world,
    ecs_entity_t instrument,
    float pitch,
    float velocity,
    float duration);

/** Play a sound once at a place in the world, heard from the listener as an AudioSpatial
 * source that stays where it started.
 *
 * The distances are those of the AudioSpatial of the sound, the defaults without one. A play
 * beyond max_distance of the listener does not start. Scripts call it as
 * box.audio.play_at(sound, position, volume).
 *
 * @param world The world
 * @param sound The sound, an entity with AudioPcm, SoundFile or SynthSound
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

/** Gain, pan and distance filter of a spatial source, as box.audio computes them: the gain and
 * the pan of box_audio_spatial_gain, and the cutoff of box_audio_spatial_cutoff.
 *
 * @param spatial The distances, NULL for the defaults
 * @param listener World transform of the listener
 * @param position World position of the source
 * @param pan Receives the pan, -1 left to 1 right, may be NULL
 * @param cutoff Receives the cutoff in Hz, 0 for an open filter, may be NULL
 * @return The gain, 0 at and beyond max_distance
 */
float box_audio_spatial_place(
    const AudioSpatial *spatial,
    const mat4 listener,
    const vec3 position,
    float *pan,
    float *cutoff);

/** Cutoff of the distance filter of a spatial source at a distance, as box.audio computes it.
 *
 * @param spatial The distances and cutoff, NULL for the defaults, which filter nothing
 * @param distance Distance from the listener
 * @return The cutoff in Hz, 0 when the filter is open: within min_distance or without a cutoff
 */
float box_audio_spatial_cutoff(
    const AudioSpatial *spatial,
    float distance);

/** Load a song: set the file of the AudioSong singleton. The engine stops and unloads the
 * song that plays over the next frames and then loads this one; AudioSongStatus follows it.
 *
 * @param world The world
 * @param file The .flecs file of the song, found as the path of a SoundFile; NULL or empty
 * unloads the song
 */
void box_audio_song_load(
    ecs_world_t *world,
    const char *file);

/** Unload the song: remove the AudioSong singleton. The song fades out and everything it
 * created is deleted over the next frames.
 *
 * @param world The world
 */
void box_audio_song_unload(
    ecs_world_t *world);

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

#endif
