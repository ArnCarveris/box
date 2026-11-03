/**
 * @file audio/output.h
 * @brief The audio output of a world: the transport an app run opens, updated every frame.
 *
 * Importing box opens nothing. box_app_run opens the output: the device when a real app runs,
 * the null transport under --headless or when there is no device, nothing with --no-audio.
 */

#ifndef BOX_AUDIO_OUTPUT_H
#define BOX_AUDIO_OUTPUT_H

#include <flecs.h>
#include "transport.h"

/** How to open the output of a world */
typedef struct box_audio_output_desc_t {
    bool headless;                /**< Open no device: the null transport renders the deltas */
    bool disabled;                /**< No output at all, --no-audio */
    box_audio_backend_t backend;  /**< Backends the device may open with */
    box_audio_output_t output;    /**< Receives the frames the null transport renders, for
                                   * the tests, NULL for none */
    void *ctx;                    /**< Context of output */
} box_audio_output_desc_t;

/** Register the output component and its frame system, at the import of box.app.
 *
 * @param world The world
 */
void box_audio_output_init(
    ecs_world_t *world);

/** Open the output of a world, replacing an output it has. No device is not an error: the
 * world then gets the null transport and its state is BOX_AUDIO_STATE_NO_DEVICE.
 *
 * @param world The world
 * @param desc How to open it
 */
void box_audio_output_open(
    ecs_world_t *world,
    const box_audio_output_desc_t *desc);

/** Give a world a transport as its output, replacing an output it has.
 *
 * @param world The world
 * @param transport The transport, owned by the world from now on, NULL to close the output
 */
void box_audio_output_use(
    ecs_world_t *world,
    box_audio_transport_t *transport);

/** Close the output of a world.
 *
 * @param world The world
 */
void box_audio_output_close(
    ecs_world_t *world);

/** Get the transport of a world.
 *
 * @param world The world
 * @return The transport, NULL when the world has no output
 */
box_audio_transport_t* box_audio_output_transport(
    const ecs_world_t *world);

/** Get the serial of the output of a world, which changes with every open, so that box.audio
 * sends its sounds and its state again to a new transport.
 *
 * @param world The world
 * @return The serial, 0 when the world has no output
 */
uint32_t box_audio_output_serial(
    const ecs_world_t *world);

/** Get the state of the output of a world.
 *
 * @param world The world
 * @return The state, BOX_AUDIO_STATE_OFF when the world has no output
 */
box_audio_state_t box_audio_output_state(
    const ecs_world_t *world);

#endif
