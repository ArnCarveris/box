/**
 * @file effects/effects_internal.h
 * @brief Private state of the box.effects module: the stack query and the chain builder.
 */

#ifndef BOX_EFFECTS_INTERNAL_H
#define BOX_EFFECTS_INTERNAL_H

#include <box.h>
#include "../shader/shader_internal.h"

/** One entry of the effect stack while it is being ordered */
typedef struct box_effect_src_t {
    ecs_entity_t entity; /**< The effect entity */
    int32_t order;       /**< Order the chain was sorted by */
} box_effect_src_t;

/** Module wide registry of the effects, stored as a singleton */
typedef struct EffectState {
    ecs_query_t *effects; /**< Entities holding an Effect component */
    ecs_vec_t stack;      /**< box_effect_src_t of the chain being built */
    int32_t enabled;      /**< Effects with a shader that are on, 0 skips every chain */
} EffectState;

extern ECS_COMPONENT_DECLARE(EffectState);

/** Build the effect chain of one view into a packet.
 *
 * @param world The world
 * @param e The View entity, 0 for the view a world without views gets
 * @param p The packet the passes are appended to
 * @param v The view, whose effect range and inputs are filled in
 */
void box_effects_extract(
    ecs_world_t *world,
    ecs_entity_t e,
    box_packet_t *p,
    box_view_t *v);

#endif
