#ifndef BOX_EFFECTS_INTERNAL_H
#define BOX_EFFECTS_INTERNAL_H

#include <box.h>
#include "../shader/shader_internal.h"

/** Parameter components the registry remembers the layout of */
#define BOX_EFFECT_LAYOUT_MAX (8)

/** Module wide registry of the effects, stored as a singleton */
typedef struct EffectState {
    ecs_id_t layout_id[BOX_EFFECT_LAYOUT_MAX]; /**< Parameter components already seen */
    bool layout_raw[BOX_EFFECT_LAYOUT_MAX];    /**< The component is floats end to end */
    int32_t layout_size[BOX_EFFECT_LAYOUT_MAX]; /**< Bytes such a component copies */
    int32_t layout_count; /**< Entries of that table in use */
    ecs_entity_t texture_entity[BOX_EFFECTS_MAX];
    bool texture_sent[BOX_EFFECTS_MAX];
    bool texture_failed[BOX_EFFECTS_MAX];
    int32_t texture_count;
} EffectState;

extern ECS_COMPONENT_DECLARE(EffectState);

/** Make sure the display transform lookup cube reached the renderer.
 *
 * The cube is read and sent the first time a pass asks for it; a cube that
 * cannot be read is reported once and the pass falls back to the neutral curve.
 *
 * @param state The registry
 * @param p The packet
 * @return true when the renderer has the cube
 */
int32_t box_effect_texture(
    ecs_world_t *world,
    EffectState *state,
    ecs_entity_t texture,
    box_packet_t *p);

void box_effects_extract(
    ecs_world_t *world,
    ecs_entity_t e,
    box_packet_t *p,
    box_view_t *v);

#endif
