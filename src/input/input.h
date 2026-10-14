/**
 * @file input/input.h
 * @brief Private state of the box.input module: the event queues, the backend and the key tables.
 */

#ifndef BOX_INPUT_INTERNAL_H
#define BOX_INPUT_INTERNAL_H

#include <box.h>

/** One script event the frame emits after the Input singleton is written */
typedef struct box_input_emit_t {
    bool is_mouse;                    /**< The event is a mouse event, else a keyboard event */
    EcsScriptMouseEvent mouse;        /**< Mouse event to emit */
    EcsScriptKeyboardEvent keyboard;  /**< Keyboard event to emit */
} box_input_emit_t;

/** Queues and backend of the module, a private singleton */
typedef struct InputDevice {
    ecs_vec_t platform;            /**< InputEvent pushed by the backend since the last frame */
    ecs_vec_t queue;               /**< InputEvent of the synthetic queue, applied in order */
    ecs_vec_t emits;               /**< box_input_emit_t of the frame */
    box_input_platform_t backend;  /**< Backend that feeds real device input */
    ecs_entity_t screen;           /**< Target of the mouse events, focused for keyboard events */
    bool has_position;             /**< A cursor position arrived since focus was gained */
} InputDevice;

extern ECS_COMPONENT_DECLARE(InputDevice);

/** Get the device state of a world.
 *
 * @param world The world or a stage
 * @return The device state
 */
InputDevice* box_input_device(
    ecs_world_t *world);

/** Get the key value a keyboard event carries for a key, as a browser reports it.
 *
 * @param key The key
 * @return The value, such as a, Enter or ArrowLeft, NULL for a mouse button or invalid key
 */
const char* box_input_key_value(
    Key key);

/** Get the key that types a character without modifiers.
 *
 * @param code The unicode code point
 * @return The key, KeyNone when no key types it
 */
Key box_input_key_from_char(
    int32_t code);

/** Clear the per frame state, apply the platform and synthetic events and emit them.
 *
 * @param it Iterator of the system, which has no terms
 */
void BoxInputUpdate(
    ecs_iter_t *it);

#endif
