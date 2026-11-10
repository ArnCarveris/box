/**
 * @file audio/module_sound.h
 * @brief Functions of box.audio for sound files, sound ids, voices, music streams and decoding.
 */

#ifndef BOX_AUDIO_MODULE_SOUND_H
#define BOX_AUDIO_MODULE_SOUND_H

#include "module.h"

/** Register the sound file components' observers and the headless wait of their decodes.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_sample_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free the samples of the module and drop its reference to the decodes in flight.
 *
 * @param state The state
 */
void box_audio_sample_fini(
    box_audio_module_t *state);

/** Get the sample of a sound entity with a SoundFile.
 *
 * @param world The world
 * @param sound The sound entity
 * @return The sample, NULL when the entity has no SoundFile
 */
box_audio_sample_t* box_audio_sample_of(
    const ecs_world_t *world,
    ecs_entity_t sound);

/** Get the decoded frames of a sound file, for the tests.
 *
 * @param world The world
 * @param sound The sound entity
 * @param frames Receives the number of frames
 * @param channels Receives 1 or 2
 * @param sample_rate Receives the rate of the file
 * @return The frames, NULL while the sound is not ready
 */
const float* box_audio_sample_frames(
    const ecs_world_t *world,
    ecs_entity_t sound,
    int32_t *frames,
    int32_t *channels,
    float *sample_rate);

/** Count the decoded copies of sound files the module holds, loading or not.
 *
 * @param world The world
 * @return Number of samples
 */
int32_t box_audio_sample_count(
    const ecs_world_t *world);

/** Count the sound ids in use in the core of the current output.
 *
 * @param world The world
 * @return Number of sounds sent and not yet freed
 */
int32_t box_audio_module_sounds(
    const ecs_world_t *world);

/** Make room in the batch of the frame for one more command.
 *
 * @param state The state
 * @return Whether the batch has room
 */
bool box_audio_reserve(
    box_audio_module_t *state);

/** Pick a free sound id of the core, or log once that every id is taken. The caller marks
 * it in sound_used.
 *
 * @param state The state
 * @return The id, 0 when every id is taken
 */
uint32_t box_audio_sound_id(
    box_audio_module_t *state);

/** Pick the handle of a new voice.
 *
 * @param state The state
 * @return The handle, never 0
 */
uint32_t box_audio_new_voice(
    box_audio_module_t *state);

/** Register the observers of AudioStream.
 *
 * @param world The world
 * @param state The state
 */
void box_audio_music_init(
    ecs_world_t *world,
    box_audio_module_t *state);

/** Free the streams of the module; the jobs in flight free what they hold.
 *
 * @param state The state
 */
void box_audio_music_fini(
    box_audio_module_t *state);

/** Forget the voices of the streams after the output changed; they start again from where
 * they were.
 *
 * @param state The state
 */
void box_audio_music_reset(
    box_audio_module_t *state);

/** Start, stop and update the voices of the streams, into the batch of the frame.
 *
 * @param world The world
 * @param state The state
 * @param t The transport, NULL for none
 * @param waiting Whether the page waits for a gesture
 */
void box_audio_music_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t,
    bool waiting);

/** After the batch went out: send the decoded chunks, submit decodes and write the status.
 *
 * @param world The world
 * @param state The state
 * @param t The transport, NULL for none
 */
void box_audio_music_send(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t);

/** Count the streams the module holds, retired ones that fade out included.
 *
 * @param world The world
 * @return Number of streams
 */
int32_t box_audio_music_count(
    const ecs_world_t *world);

/** Open an Ogg Vorbis file in memory, which the decoder takes.
 *
 * @param bytes The file, freed with ecs_os_free by the decoder, also on failure
 * @param size Bytes of the file
 * @param error Receives the error code of stb_vorbis on failure
 * @return The decoder, NULL when the bytes are no Ogg Vorbis stream
 */
box_audio_vorbis_t* box_audio_vorbis_open(
    uint8_t *bytes,
    int32_t size,
    int32_t *error);

/** Close a decoder and free its file.
 *
 * @param v The decoder, may be NULL
 */
void box_audio_vorbis_close(
    box_audio_vorbis_t *v);

/** Get the frames of the file of a decoder.
 *
 * @param v The decoder
 * @return Number of frames
 */
int32_t box_audio_vorbis_frames(
    const box_audio_vorbis_t *v);

/** Get the channels a decoder writes: 1 or 2, more mixed down to 2.
 *
 * @param v The decoder
 * @return Number of channels
 */
int32_t box_audio_vorbis_channels(
    const box_audio_vorbis_t *v);

/** Get the sample rate of the file of a decoder.
 *
 * @param v The decoder
 * @return Frames per second
 */
float box_audio_vorbis_rate(
    const box_audio_vorbis_t *v);

/** Get the frame of the file the next read starts at.
 *
 * @param v The decoder
 * @return The frame
 */
int64_t box_audio_vorbis_position(
    const box_audio_vorbis_t *v);

/** Seek a decoder to a frame of the file.
 *
 * @param v The decoder
 * @param frame The frame, wrapped into the file
 * @return Whether the seek succeeded
 */
bool box_audio_vorbis_seek(
    box_audio_vorbis_t *v,
    int64_t frame);

/** Decode frames with the pull API of stb_vorbis. A loop goes on with the first frame after the
 * last, without a gap.
 *
 * @param v The decoder
 * @param out Receives frames times channels floats, interleaved
 * @param frames Frames to decode
 * @param loop Whether the end goes on with the start
 * @return Frames decoded, fewer than frames only at the end of a file that does not loop
 */
int32_t box_audio_vorbis_read(
    box_audio_vorbis_t *v,
    float *out,
    int32_t frames,
    bool loop);

/** Read a file into memory.
 *
 * @param path The file
 * @param size Receives the bytes read, 0 for an empty file, -1 when it cannot be read
 * @return The bytes, NULL when the file cannot be read, is empty or too large
 */
uint8_t* box_audio_music_read_file(
    const char *path,
    int32_t *size);

/** Decode the file of a job on a worker: WAV or FLAC to float frames, more than two channels
 * mixed down to stereo, at the sample rate of the file.
 *
 * @param job The job, receives the frames or an error naming the file
 */
void box_audio_decode(
    box_audio_sample_job_t *job);

/** Write a capture as a 16 bit stereo WAV file, through a temporary file and one rename.
 * A capture of a song gets a BOX_AUDIO_WAV_SONG_CHUNK after its data.
 *
 * @param path The file
 * @param data Interleaved stereo frames
 * @param frames Number of frames
 * @param sample_rate Frames per second
 * @param song The beat and tempo the capture started at, NULL when no song played
 * @return 0 on success
 */
int box_audio_wav_write(
    const char *path,
    const float *data,
    int32_t frames,
    int32_t sample_rate,
    const box_audio_wav_song_t *song);

#endif
