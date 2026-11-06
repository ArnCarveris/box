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
