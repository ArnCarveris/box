/**
 * @file command/command.h
 * @brief Registration of the box commands and the helpers shared by the box.command sources.
 */

#ifndef BOX_COMMAND_INTERNAL_H
#define BOX_COMMAND_INTERNAL_H

#include <box.h>
#include "../app/app.h"
#include "../geometry/mesh_cache.h"

/** Register a command as a script function in box.command.commands.
 *
 * @param world The world
 * @param brief One line description, stored as the doc brief that help lists
 * @param desc The function, its parent is set to box.command.commands
 * @return The function entity
 */
ecs_entity_t box_command_register(
    ecs_world_t *world,
    const char *brief,
    ecs_function_desc_t desc);

/** Get the scope of the session that runs the current command.
 *
 * @param world The world
 * @return The scope, 0 for the root or outside of a command
 */
ecs_entity_t box_command_scope(
    const ecs_world_t *world);

/** Install the log capture and register the logs command.
 *
 * @param world The world
 */
void box_logs_init(
    ecs_world_t *world);

/** Register the key, mouse, input, clipboard and action commands.
 *
 * @param world The world
 */
void box_cmd_input_init(
    ecs_world_t *world);

/** Register the capture and render commands and the system that completes renders.
 *
 * @param world The world
 */
void box_cmd_render_init(
    ecs_world_t *world);

/** Get the size of the images of the render command, and of a capture or fit that finds no
 * surface for its camera: the size in pixels of the surface shown first.
 *
 * @param world The world
 * @param width Receives the width in pixels
 * @param height Receives the height in pixels
 */
void box_cmd_render_size(
    ecs_world_t *world,
    int32_t *width,
    int32_t *height);

/** Find the first view, in render order (View.order, then creation), that renders a camera.
 *
 * @param world The world
 * @param camera The camera, 0 for any camera with a Camera
 * @param root Parent the view must have, 0 for any parent
 * @param clearing Only consider views that clear
 * @return The view entity, 0 when there is none
 */
ecs_entity_t box_cmd_render_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    ecs_entity_t root,
    bool clearing);

/** Get the camera of a command: the camera argument, else the camera of the first view of the
 * surface shown first. Logs an error when the argument is not a camera or there is no camera.
 *
 * @param world The world
 * @param argc Number of arguments
 * @param argv The camera, omitted for the active camera
 * @return The camera, 0 on an error
 */
ecs_entity_t box_cmd_render_camera(
    ecs_world_t *world,
    int32_t argc,
    const ecs_value_t *argv);

/** Get the 8 world space corners of the local bounds of the mesh of a shape, with the scale
 * of its ShapeMesh and its WorldTransform.
 *
 * @param world The world
 * @param cache The mesh cache
 * @param e The entity
 * @param out Receives the corners
 * @return Whether the entity has a mesh in the cache
 */
bool box_cmd_render_corners(
    ecs_world_t *world,
    const box_mesh_cache_t *cache,
    ecs_entity_t e,
    vec3 out[8]);

/** Create a resource in the session of the command that runs, in the resource folder of the
 * process, and get its file.
 *
 * @param world The world
 * @param content_type The content type, such as "audio/wav"
 * @param resource Receives the resource
 * @return The file, which the caller frees; NULL with an error logged outside of a session,
 * NULL in the browser
 */
char* box_cmd_render_resource(
    ecs_world_t *world,
    const char *content_type,
    ecs_shell_resource_t *resource);

/** Register the audio command that prints the status of the audio, sets its volume and
 * mute and records captures.
 *
 * @param world The world
 */
void box_cmd_audio_init(
    ecs_world_t *world);

/** Register the system that advances the renders of audio render.
 *
 * @param world The world
 */
void box_cmd_audio_render_init(
    ecs_world_t *world);

/** Start audio render: render a sound, an instrument, a bus stem or a section of the song on
 * its own, faster than real time, in an offline world, to a WAV resource that is filled once
 * the frames are in.
 *
 * @param world The world
 * @param subject song, or the path of a sound, an instrument or a bus
 * @param argc Number of options
 * @param options The key=value options
 * @return The name of the resource, or the file in the browser, which the caller frees, NULL
 * with an error logged when the subject cannot be rendered
 */
char* box_cmd_audio_render(
    ecs_world_t *world,
    const char *subject,
    int32_t argc,
    const char **options);

/** Set or toggle the solo of a bus, or clear every solo with none, as a script writes
 * AudioBus.
 *
 * @param world The world
 * @param name The path of the bus, or none
 * @param value on or off, NULL or empty to toggle
 * @return The text, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_solo(
    ecs_world_t *world,
    const char *name,
    const char *value);

/** Set or toggle the mute of a bus, as a script writes AudioBus.
 *
 * @param world The world
 * @param name The path of the bus, master for the master bus
 * @param value on or off, NULL or empty to toggle
 * @return The text, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_bus_mute(
    ecs_world_t *world,
    const char *name,
    const char *value);

/** Bypass an effect, or stop bypassing it: add or remove Disabled.
 *
 * @param world The world
 * @param name The path of the effect entity
 * @param value on or off, NULL or empty to toggle
 * @return The text, which the caller frees, NULL with an error logged, also for an entity
 * that is no effect
 */
char* box_cmd_audio_bypass(
    ecs_world_t *world,
    const char *name,
    const char *value);

/** Parse a note: a name such as C4, F#3 or Bb2, with C4 at 60, or a MIDI number 0 to 127.
 *
 * @param name The note
 * @param pitch Receives the MIDI note number
 * @return Whether the note is valid
 */
bool box_cmd_audio_note_parse(
    const char *name,
    float *pitch);

/** Play one note of a sound or an instrument now on the live output, through the bus of the
 * instrument, and release it after a number of seconds.
 *
 * @param world The world
 * @param name The path of the sound or the instrument
 * @param note The note, as box_cmd_audio_note_parse
 * @param vel Velocity 0 to 1, NULL or empty for 1
 * @param dur Seconds, NULL or empty for 0.5
 * @return The text, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_note(
    ecs_world_t *world,
    const char *name,
    const char *note,
    const char *vel,
    const char *dur);

/** Analyze a WAV for the audio analyze action: loudness, peaks, bands, stereo, onsets, tail,
 * silence, clicks, discontinuities, pitch and, for a capture of a song, a table per bar.
 *
 * @param world The world
 * @param wav A resource of the session, else a WAV file
 * @param from First second of the span, NULL or empty for the start
 * @param to Last second of the span, NULL or empty for the end
 * @return The report, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_analyze(
    ecs_world_t *world,
    const char *wav,
    const char *from,
    const char *to);

/** Load the bytes of a WAV for audio analyze, diff and plot: a resource of the session, else
 * a file as written, else a file found from the working directory or the executable.
 *
 * @param world The world
 * @param wav The resource or file
 * @param size Receives the bytes read
 * @return The bytes, which the caller frees, NULL with an error logged
 */
uint8_t* box_cmd_audio_wav_load(
    ecs_world_t *world,
    const char *wav,
    int64_t *size);

/** Draw a PNG of a WAV for the audio plot action: audio plot <wav> [kind] [size=WxH]
 * [from=s] [to=s] [min_db=dB], a spectrogram, waveform, spectrum or loudness.
 *
 * @param world The world
 * @param argc Number of arguments
 * @param argv The WAV, then the kind and the options
 * @return The name of the PNG resource, the file in the browser, which the caller frees,
 * NULL on an error
 */
char* box_cmd_audio_plot(
    ecs_world_t *world,
    int32_t argc,
    const char **argv);

struct box_audio_wav_t;

/** Load and decode a WAV for an action of the audio command, with box_cmd_audio_wav_load.
 *
 * @param world The world
 * @param wav A resource of the session, else a WAV file
 * @param action The action for the errors, such as "analyze"
 * @param out Receives the frames, freed with box_audio_wav_fini
 * @return 0 on success, -1 with an error logged
 */
int box_cmd_audio_wav_decode(
    ecs_world_t *world,
    const char *wav,
    const char *action,
    struct box_audio_wav_t *out);

/** Compare two WAVs for the audio diff action: bit identity, length, loudness, true peak,
 * bands, stereo, the null test residual after alignment, its loudest spans and the bars that
 * differ most.
 *
 * @param world The world
 * @param a The first WAV, a resource of the session or a file
 * @param b The second WAV, a resource of the session or a file
 * @param from First second of the span, NULL or empty for the start
 * @param to Last second of the span, NULL or empty for the end
 * @return The report, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_diff(
    ecs_world_t *world,
    const char *a,
    const char *b,
    const char *from,
    const char *to);

/** Print the notes of a track per bar, or as a text piano roll: audio notes <track>
 * [bars=a..b] [roll].
 *
 * @param world The world
 * @param argc Number of arguments
 * @param argv The track, then bars=a..b and roll in any order
 * @return The text, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_notes(
    ecs_world_t *world,
    int32_t argc,
    const char **argv);

/** Measure the levels of buses over a span of the live output: audio meter [bus...]
 * [seconds|bars=a..b] starts a measurement, audio meter without arguments prints its progress
 * and, once it is measured, the table of the buses, their effects and the headroom.
 *
 * @param world The world
 * @param argc Number of arguments
 * @param argv The buses, none for every bus and the master, the seconds and bars=a..b
 * @return The text, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_meter_span(
    ecs_world_t *world,
    int32_t argc,
    const char **argv);

/** Print the chords of a section per beat or per bar, its key, the key of the song and the
 * notes outside the key: audio chords [track...] [bars=a..b].
 *
 * @param world The world
 * @param argc Number of arguments
 * @param argv The tracks, none for every track, and bars=a..b
 * @return The text, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_chords(
    ecs_world_t *world,
    int32_t argc,
    const char **argv);

/** Register the fit command that moves a camera to a tight fit of its scene.
 *
 * @param world The world
 */
void box_cmd_fit_init(
    ecs_world_t *world);

/** Register the fps and frames commands that print the frame timing.
 *
 * @param world The world
 */
void box_cmd_frames_init(
    ecs_world_t *world);

/** Register the edit command that opens and closes the editor.
 *
 * @param world The world
 */
void box_cmd_edit_init(
    ecs_world_t *world);

#endif
