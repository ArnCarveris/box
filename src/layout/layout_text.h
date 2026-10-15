/**
 * @file layout/layout_text.h
 * @brief The hooks box.text registers so box.layout measures and wraps texts.
 *
 * box.text imports box.layout, so box.layout never calls into box.text: texts register these
 * hooks when box.text is imported, and a world without box.text lays out without texts.
 */

#ifndef BOX_LAYOUT_TEXT_H
#define BOX_LAYOUT_TEXT_H

#include <box.h>

/** Measure an element as a text.
 *
 * @param world The world
 * @param e The element
 * @param size Receives the width and height of its lines at their own width
 * @param least Receives the narrowest width it wraps to
 * @param wraps Receives whether the element takes the width of the layout
 * @return False when the element has no text and no text size
 */
typedef bool (*box_layout_measure_t)(
    const ecs_world_t *world,
    ecs_entity_t e,
    float *size,
    float *least,
    bool *wraps);

/** Give a text the width the layout resolved for it.
 *
 * @param world The world
 * @param e The element
 * @param width The width, 0 when the element left its layout
 * @return The height of its lines at that width
 */
typedef float (*box_layout_wrap_t)(
    ecs_world_t *world,
    ecs_entity_t e,
    float width);

/** The hooks of the texts of a world */
typedef struct box_layout_text_t {
    ecs_entity_t component;       /**< Component of a text, whose elements take a width */
    ecs_entity_t bounds;          /**< Component whose changes lay out a text again */
    box_layout_measure_t measure; /**< Measures an element as a text */
    box_layout_wrap_t wrap;       /**< Gives a text its width */
} box_layout_text_t;

/** Register the hooks of the texts of a world with box.layout.
 *
 * @param world The world, box.layout imported
 * @param text The hooks
 */
void box_layout_text_register(
    ecs_world_t *world,
    const box_layout_text_t *text);

#endif
