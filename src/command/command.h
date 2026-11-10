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
 * @param desc The function, its parent box.command.commands when it has none
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

/** State argument of an on/off command such as audio.mute */
typedef enum box_cmd_switch_t {
    BoxCmdSwitchOff = 0,   /**< Turn off */
    BoxCmdSwitchOn = 1,    /**< Turn on */
    BoxCmdSwitchToggle = 2 /**< Flip the current state, when no argument is given */
} box_cmd_switch_t;

/** Register the audio command that prints the status of the audio and its subcommands, such
 * as audio.volume, audio.capture and audio.plot.
 *
 * @param world The world
 */
void box_cmd_audio_init(
    ecs_world_t *world);

/** Register the system that advances the renders of audio.render.
 *
 * @param world The world
 */
void box_cmd_audio_render_init(
    ecs_world_t *world);

/** Start audio.render: render a sound, an instrument, a bus stem or a section of the song on
 * its own, faster than real time, in an offline world, to a WAV resource that is filled once
 * the frames are in.
 *
 * @param world The world
 * @param subject song, or the path of a sound, an instrument or a bus
 * @param argc Number of options
 * @param options The key=value options
 * @param resource Receives the WAV resource, NULL in the browser
 * @return The name of the resource, or the file in the browser, which the caller frees, NULL
 * with an error logged when the subject cannot be rendered
 */
char* box_cmd_audio_render(
    ecs_world_t *world,
    const char *subject,
    int32_t argc,
    const char **options,
    ecs_shell_resource_t *resource);

/** Receives the frames of the master of a render of the song, after the limiter, as they
 * render */
typedef void (*box_cmd_audio_probe_frames_t)(
    const float *left,
    const float *right,
    int32_t count,
    void *ctx);

/** Called after each frame of the offline world of a render of the song once the timeline
 * plays, with the beat at the start of the frames its meters measured, those of the frame
 * before; the module of the offline world holds its meters, its counters and its AudioStatus */
typedef void (*box_cmd_audio_probe_frame_t)(
    ecs_world_t *world,
    double beat,
    void *ctx);

/** What a render of the song hands to its caller instead of writing a file, for audio.check */
typedef struct box_cmd_audio_probe_t {
    box_cmd_audio_probe_frames_t frames; /**< Gets the frames of the master, may be NULL */
    box_cmd_audio_probe_frame_t frame;   /**< Gets the frames of the offline world, may be
                                          *   NULL */
    void *ctx;                           /**< Context of the callbacks */
} box_cmd_audio_probe_t;

/** Play a section of the song, or of the tracks of the scene, through once at once: the
 * offline world of audio.render, run to its end within the call, without a file. The section
 * may last up to 600 s.
 *
 * @param world The live world
 * @param argc Number of options
 * @param options The key=value options of audio.render song: bars or beats, solo, mute, tail
 * @param probe Gets the frames and the frames of the offline world
 * @param failed Receives, owned, why the offline world did not play, such as a sound file that
 * did not load; NULL when it played or the options are invalid
 * @return 0 on success, nonzero when the options are invalid, with an error logged, or with
 * failed set
 */
int box_cmd_audio_render_probe(
    ecs_world_t *world,
    int32_t argc,
    const char **options,
    const box_cmd_audio_probe_t *probe,
    char **failed);

/** Lint the loaded song, or the tracks of the scene: audio.check [static] [bars=a..b]. One
 * line per finding with its severity, check, place and a hint, and a summary line.
 *
 * @param world The world
 * @param argc Number of arguments
 * @param argv static and bars=a..b in any order
 * @return The text, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_check(
    ecs_world_t *world,
    int32_t argc,
    const char **argv);

/** Set or toggle the solo of a bus, or clear every solo with none, as a script writes
 * AudioBus.
 *
 * @param world The world
 * @param name The path of the bus, or none
 * @param state On, off or toggle
 * @return The text, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_solo(
    ecs_world_t *world,
    const char *name,
    box_cmd_switch_t state);

/** Set or toggle the mute of a bus, as a script writes AudioBus.
 *
 * @param world The world
 * @param name The path of the bus, master for the master bus
 * @param state On, off or toggle
 * @return The text, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_bus_mute(
    ecs_world_t *world,
    const char *name,
    box_cmd_switch_t state);

/** Bypass an effect, or stop bypassing it: add or remove Disabled.
 *
 * @param world The world
 * @param name The path of the effect entity
 * @param state On, off or toggle
 * @return The text, which the caller frees, NULL with an error logged, also for an entity
 * that is no effect
 */
char* box_cmd_audio_bypass(
    ecs_world_t *world,
    const char *name,
    box_cmd_switch_t state);

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
 * @param velocity Velocity 0 to 1
 * @param seconds Seconds the note lasts
 * @return The text, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_note(
    ecs_world_t *world,
    const char *name,
    const char *note,
    double velocity,
    double seconds);

/** Analyze a WAV for the audio.analyze action: loudness, peaks, bands, stereo, onsets, tail,
 * silence, clicks, discontinuities, pitch and, for a capture of a song, a table per bar.
 *
 * @param world The world
 * @param wav A resource of the session, else a WAV file
 * @param from First second of the span
 * @param to Last second of the span, negative for the end
 * @return The report, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_analyze(
    ecs_world_t *world,
    const char *wav,
    double from,
    double to);

/** Load the bytes of a WAV for audio.analyze, diff and plot: a resource of the session, else
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

/** Draw a PNG of a WAV for the audio.plot action: audio.plot <wav> [kind] [size=WxH]
 * [from=s] [to=s] [min_db=dB], a spectrogram, waveform, spectrum or loudness.
 *
 * @param world The world
 * @param argc Number of arguments
 * @param argv The WAV, then the kind and the options
 * @param resource Receives the PNG resource, NULL in the browser
 * @return The name of the PNG resource, the file in the browser, which the caller frees,
 * NULL on an error
 */
char* box_cmd_audio_plot(
    ecs_world_t *world,
    int32_t argc,
    const char **argv,
    ecs_shell_resource_t *resource);

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

/** Compare two WAVs for the audio.diff action: bit identity, length, loudness, true peak,
 * bands, stereo, the null test residual after alignment, its loudest spans and the bars that
 * differ most.
 *
 * @param world The world
 * @param a The first WAV, a resource of the session or a file
 * @param b The second WAV, a resource of the session or a file
 * @param from First second of the span
 * @param to Last second of the span, negative for the end
 * @return The report, which the caller frees, NULL with an error logged
 */
char* box_cmd_audio_diff(
    ecs_world_t *world,
    const char *a,
    const char *b,
    double from,
    double to);

/** Print the notes of a track per bar, or as a text piano roll: audio.notes <track>
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

/** Append the kind and the parameters of an effect, as the audio command prints an insert slot.
 *
 * @param world The world
 * @param buf The output
 * @param e The effect entity
 * @return Whether the entity has an effect the command knows
 */
bool box_cmd_audio_effect_params(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t e);

/** Measure the levels of buses over a span of the live output: audio.meter [bus...]
 * [seconds|bars=a..b] starts a measurement, audio.meter without arguments prints its progress
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
 * notes outside the key: audio.chords [track...] [bars=a..b].
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
