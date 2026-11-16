/**
 * @file render/transitions.h
 * @brief Private state of the transition extract, shared by the files of the game thread side:
 * transitions.c and the BoxShadow transitions of transition_box_shadow.c.
 */

#ifndef BOX_RENDER_TRANSITIONS_H
#define BOX_RENDER_TRANSITIONS_H

#include "draw.h"

/** Fields of a component whose range of targets a slot keeps: the three of Position, Rotation
 * and Scale, offset_x, offset_y, blur and spread of BoxShadow, intensity and range of PointLight,
 * intensity, range, inner and outer angle of SpotLight */
#define BOX_TRANSITION_RANGE (4)

/** A transitioned component of an entity, as the extract last sent it */
typedef struct box_transition_slot_t {
    bool present;                   /**< A target was sent and is the value of the node */
    bool leaving;                   /**< The component was removed with on_remove and the node
                                     *   renders its way to the neutral value: present stays set
                                     *   and the node owns the value until it no longer runs */
    bool absent;                    /**< The last extract saw the node with an on_add entry and
                                     *   no value of the component, owned or inherited, so the
                                     *   next value is an add */
    TimingFunction timing_function; /**< Curve of the last start */
    float delay;                    /**< Delay of the last start */
    float duration;                 /**< Duration of the last start */
    double start;                   /**< Time of the last start */
    uint8_t value[BOX_RENDER_TRANSITION_VALUE]; /**< The last target sent */
    float lo[BOX_TRANSITION_RANGE]; /**< Per field minimum of the targets sent since it was
                                     *   last idle, which bounds the value it renders: Position,
                                     *   Rotation, Scale, the floats of BoxShadow and the lights
                                     *   only */
    float hi[BOX_TRANSITION_RANGE]; /**< Per field maximum of those targets */
} box_transition_slot_t;

/** Private state of an entity with Transition */
typedef struct box_transition_node_t {
    ecs_entity_t entity;            /**< The entity, 0 for a free node */
    int64_t seen;                   /**< Stamp of the last extract that matched the entity */
    uint32_t live;                  /**< 1 << component per slot that is present, leaving or
                                     *   absent, so the slots of the others are not read */
    double until;                   /**< Latest end of the transitions its slots started, past
                                     *   which none of them runs */
    box_transition_slot_t slots[BOX_RENDER_TRANSITION_COMPONENTS]; /**< By
                                     *   box_render_transition_component_t */
} box_transition_node_t;

/** An entity a transform transition moves in this frame, in the order of the walk: a node
 * before its descendants, so a parent node comes before its child nodes */
typedef struct box_transition_item_t {
    ecs_entity_t entity;            /**< The entity */
    uint32_t node;                  /**< The node that moves it, its own for a node */
    uint32_t parent;                /**< Parent node of a node, 0 for none or a member */
    int32_t up;                     /**< Item of its parent, -1 for a node without parent node */
    bool is_node;                   /**< The entity has an active transition of the walk */
    bool opacity;                   /**< An Opacity transition of its node or a node above runs,
                                     *   for the walk of Tint and Opacity */
} box_transition_item_t;

/** The composed Tint and Opacity of a chain of entities of the walk of Tint and Opacity: a
 * display encoded color c becomes c * tint.keep + tint.add */
typedef struct box_transition_fade_rel_t {
    box_draw_tint_t tint;           /**< Composed tints, applied from the top down */
    float alpha;                    /**< Product of the opacities */
} box_transition_fade_rel_t;

/** A drawn entity whose BoxShadow has an active transition in this frame */
typedef struct box_transition_box_shadow_t {
    ecs_entity_t entity;            /**< The entity */
    uint32_t node;                  /**< Node of the entity */
    uint32_t moving;                /**< Node of a transform transition that moves the entity in
                                     *   this frame, 0 for none */
    mat4 rel;                       /**< The target chain from that node down to the entity */
} box_transition_box_shadow_t;

/** Main thread state of the transition extract */
struct box_render_transitions_t {
    ecs_query_t *query;             /**< Transition(self|up IsA), prefabs included */
    ecs_map_t by_entity;            /**< Entity to node id */
    ecs_vec_t nodes;                /**< box_transition_node_t by node id, 0 unused */
    ecs_vec_t free;                 /**< uint32_t node ids to reuse */
    ecs_vec_t running;              /**< int32_t ids of the nodes a transition can run or leave
                                     *   in this frame, so the walks skip idle ones */
    int64_t stamp;                  /**< Extracts run */
    ecs_map_t moving;               /**< Entity to node id, nodes with an active transform
                                     *   transition in this frame */
    ecs_vec_t items;                /**< box_transition_item_t of this frame */
    ecs_vec_t stack;                /**< int32_t items of the walk still to visit */
    ecs_vec_t rels;                 /**< mat4 per item: the target chain from its node down to
                                     *   it, identity for a node */
    ecs_map_t tagged;               /**< Entity with TransitionTransform to the stamp of the
                                     *   last extract that kept it */
    ecs_vec_t tag_add;              /**< ecs_entity_t that get TransitionTransform */
    ecs_vec_t tag_remove;           /**< ecs_entity_t that lose TransitionTransform */
    ecs_vec_t ranges;               /**< box_draw_transition_range_t per item: the world
                                     *   matrices the node of the item can render with, while
                                     *   the draws record shadow regions */
    ecs_vec_t casting;              /**< bool per item: a shadow caster slot is at or below
                                     *   it, so it needs a range, while the draws record shadow
                                     *   regions */
    ecs_vec_t shadow_regions;       /**< box_render_shadow_region_t, the bounds of every pose
                                     *   the moving shadow casters can render at this frame */
    ecs_vec_t shadow_last;          /**< box_render_shadow_region_t, those of the last frame */
    box_render_draws_t *draws;      /**< State of the batches, stored when the slot system is
                                     *   registered, so no frame looks it up by name */
    ecs_vec_t material_slots;       /**< box_transition_material_t, the slots of the material
                                     *   table an active material transition patches in this
                                     *   frame, kept until the next so their target uploads */
    ecs_vec_t shadings;             /**< box_transition_material_t, the 2D shapes and texts an
                                     *   active material transition patches in this frame */
    ecs_map_t tagged_material;      /**< Entity with TransitionMaterial to the stamp of the last
                                     *   extract that kept it */
    ecs_map_t visited;              /**< Entity to 1, entities the walk of IsA instances saw */
    ecs_vec_t instances;            /**< ecs_entity_t, stack of the walk of IsA instances */
    ecs_map_t fading;               /**< Entity to node id, nodes with an active Tint or Opacity
                                     *   transition in this frame */
    ecs_vec_t fade_items;           /**< box_transition_item_t of the walk of Tint and Opacity
                                     *   in this frame, a node before its descendants */
    ecs_vec_t fade_rels;            /**< box_transition_fade_rel_t per fade item: the chain from
                                     *   its node, excluded, down to it, included; identity for
                                     *   a node */
    ecs_map_t fade_index;           /**< Entity to its fade item plus 1 */
    ecs_map_t tagged_opacity;       /**< Entity with TransitionOpacity to the stamp of the last
                                     *   extract that kept it */
    ecs_vec_t box_shadows;          /**< box_transition_box_shadow_t, the drawn entities whose
                                     *   BoxShadow transitions in this frame */
    ecs_map_t box_shadow_index;     /**< Entity to its entry of box_shadows plus 1 */
    ecs_map_t tagged_box_shadow;    /**< Entity with TransitionBoxShadow to the stamp of the
                                     *   last extract that kept it */
    int32_t leaving;                /**< Slots that are leaving in this frame, 0 skips the
                                     *   lookup of a leaving owner */
    ecs_vec_t lights;               /**< box_render_light_transition_t, the lights whose light
                                     *   or Rgb transitions in this frame */
    ecs_map_t light_index;          /**< Entity to its entry of lights plus 1 */
};

/** A record an active material transition patches: a slot of the material table or a drawn
 * 2D shape or text, and the node of each of its components that runs a transition */
typedef struct box_transition_material_t {
    ecs_entity_t entity;            /**< Owner of the slot, or the drawn entity */
    uint32_t slot;                  /**< Slot of the material table */
    bool shadow;                    /**< An AlphaCutoff or TextureTransform of the slot runs */
    uint32_t nodes[BOX_RENDER_TRANSITION_COMPONENTS]; /**< Node of a component that runs a
                                     *   transition, 0 for the target */
} box_transition_material_t;

/** Get the component id of a transitionable component.
 *
 * @param component The component
 * @return The id
 */
ecs_entity_t box_transitions_id(
    box_render_transition_component_t component);

/** Check whether an entry opts in to animating adding or removing its component, and the
 * component has a neutral value for it.
 *
 * @param world The world
 * @param component The component
 * @param spec The valid entry, NULL for none
 * @param remove Whether to check on_remove, else on_add
 * @return Whether the change animates
 */
bool box_transitions_opts(
    const ecs_world_t *world,
    box_render_transition_component_t component,
    const TransitionSpec *spec,
    bool remove);

/** Update the live slots of a node and the end of its transitions after a compare of a slot.
 *
 * @param node The node
 * @param component The component of the slot
 */
void box_transitions_track(
    box_transition_node_t *node,
    box_render_transition_component_t component);

/** Get the valid transitions of the components a Transition map lists, and whether they make
 * the entity a node: a component its table owns or one that animates in or out.
 *
 * @param world The world
 * @param map The Transition map of the entity
 * @param columns The columns of the transitionable components in its table, NULL for none
 * @param specs Receives the transition of each component, NULL for none
 * @param mask Receives 1 << component per component with a transition
 * @return Whether a listed component makes the entity a node
 */
bool box_transitions_specs(
    const ecs_world_t *world,
    const Transition *map,
    void *const *columns,
    const TransitionSpec **specs,
    uint32_t *mask);

/** Compare a transitioned component of an entity to the last target sent, send what changed:
 * the first value sets without a transition, a changed value starts one with the spec of the
 * entry, and a value whose entry or component is gone snaps to its target.
 *
 * With on_add a value of a node that had none, owned or inherited, in the last frame sets the
 * neutral value and starts from it in the same packet. With on_remove a removed value without an
 * inherited one starts to the neutral value and the slot leaves until it no longer runs.
 *
 * A transform component, a BoxShadow and a light keep the range of their targets since they
 * were last idle, widened by what the curve overshoots, as the resolver starts from the value it
 * renders. A light whose fields are unchanged still takes the members that switch at once, so a
 * light that leaves keeps its bake mode and shadows.
 *
 * A removed material lobe keeps its shapes on the extended mesh shader while it leaves, so they
 * are marked as changed when it stops leaving (box_transitions_lobes_left).
 *
 * @param world The world
 * @param s The state of the extract
 * @param p The packet
 * @param e The entity of the node
 * @param node The node id
 * @param component The component
 * @param slot The slot of the component
 * @param spec The valid entry of the component, NULL when there is none
 * @param value The value the entity owns, NULL when it owns none
 */
void box_transitions_compare(
    ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_packet_t *p,
    ecs_entity_t e,
    uint32_t node,
    box_render_transition_component_t component,
    box_transition_slot_t *slot,
    const TransitionSpec *spec,
    const void *value);

/** Mark the shapes that read the material lobes of a node as changed when one of them leaves:
 * Sheen, ClearCoat, SpecularAnisotropy, DiffuseTransmission or Fresnel with on_remove keeps
 * them on the extended mesh shader (box_render_transitions_leaving), so their batch key changes
 * when the node is destroyed. Marks the entity of the node and its IsA instances.
 *
 * @param world The world
 * @param s The state of the extract
 * @param node The node
 */
void box_transitions_lobes_left(
    ecs_world_t *world,
    box_render_transitions_t *s,
    const box_transition_node_t *node);

/** Check whether a transition of a node can run or leave at a time: before the end of the
 * transitions its slots started, or with a light slot that runs or leaves.
 *
 * @param node The node
 * @param time World time, the time of the packet
 * @return Whether the walks of the frame visit it (running)
 */
bool box_transitions_runs(
    const box_transition_node_t *node,
    double time);

/** Send the destruction of the nodes the extract did not see in this frame.
 *
 * @param world The world
 * @param s The state of the extract
 * @param p The packet, NULL when the query matched no entity
 * @return The packet, created when a node is destroyed
 */
box_render_packet_t* box_transitions_sweep(
    ecs_world_t *world,
    box_render_transitions_t *s,
    box_render_packet_t *p);

/** Get a node of the running list.
 *
 * @param s The state of the extract
 * @param index Index in the running list
 * @param id Receives the node id
 * @return The node
 */
const box_transition_node_t* box_transitions_running(
    const box_render_transitions_t *s,
    int32_t index,
    int32_t *id);

/** Get the private state of a node.
 *
 * @param s The state of the extract
 * @param node The node id
 * @return The node, NULL when the id is not in use
 */
box_transition_node_t* box_transitions_node_state(
    const box_render_transitions_t *s,
    uint32_t node);

/** Keep a private tag on an entity in this frame, queueing its add when it does not have it.
 *
 * @param s The state of the extract
 * @param tagged The entities with the tag to the stamp of the last extract that kept them
 * @param e The entity
 */
void box_transitions_keep(
    box_render_transitions_t *s,
    ecs_map_t *tagged,
    ecs_entity_t e);

/** Take a private tag from the entities this frame did not keep it on and give it to the new
 * ones, which rekeys their slots.
 *
 * @param world The world
 * @param s The state of the extract
 * @param tagged The entities with the tag to the stamp of the last extract that kept them
 * @param tag The tag
 */
void box_transitions_retag(
    ecs_world_t *world,
    box_render_transitions_t *s,
    ecs_map_t *tagged,
    ecs_entity_t tag);

/** Get the packet data offset of the shading of a slot of a batch that uploaded its run into
 * this packet: a box_render_shading_t of a 2D batch, a box_render_mesh_shading_t of a mesh
 * batch.
 *
 * @param d The state of the batches
 * @param p The packet
 * @param batch Index of the batch
 * @param slot The slot
 * @param out Receives the offset
 * @return Whether the batch uploaded the shading of the slot
 */
bool box_transitions_shading_data(
    box_render_draws_t *d,
    const box_render_packet_t *p,
    int32_t batch,
    int32_t slot,
    uint32_t *out);

/** Point the target material of a shading record of an entity at the fade node it is under,
 * with the chain from the node down to it, when an active Tint or Opacity transition reaches
 * it. The node of the entity itself renders its own Tint and Opacity.
 *
 * @param s The state of the extract
 * @param e The entity
 * @param time World time, the time of the packet
 * @param target The target material
 */
void box_transitions_fade_target(
    const box_render_transitions_t *s,
    ecs_entity_t e,
    double time,
    box_draw_transition_material_t *target);

/** Give the components of a target material whose node leaves them (box_transition_slot_t
 * leaving) the neutral value the node animates to, and set their present bit, as the entity no
 * longer has them and the resolver only patches a component the target has.
 *
 * @param s The state of the extract
 * @param target The target material, with its nodes
 */
void box_transitions_leaving_target(
    const box_render_transitions_t *s,
    box_draw_transition_material_t *target);

/** Find the drawn entities whose BoxShadow has an active transition in this frame, before the
 * batches are sent, and keep TransitionBoxShadow on them, which keys their shadow slot dynamic.
 *
 * @param world The world
 * @param s The state of the extract
 * @param p The packet, NULL when no entity has Transition
 */
void box_transitions_box_shadows(
    ecs_world_t *world,
    box_render_transitions_t *s,
    const box_render_packet_t *p);

/** Get the entry of an entity whose BoxShadow transitions in this frame.
 *
 * @param s The state of the extract
 * @param e The entity
 * @return The entry, NULL for none
 */
box_transition_box_shadow_t* box_transitions_box_shadow_of(
    const box_render_transitions_t *s,
    ecs_entity_t e);

/** Send the box shadow record of every drawn entity whose BoxShadow transitions in this frame,
 * after the poses, which the records read.
 *
 * The slot is looked up again, so a shadow that lost its slot since the walk, as a removed
 * BoxShadow frees it, sends nothing.
 *
 * @param world The world
 * @param s The state of the extract
 * @param d The state of the batches
 * @param p The packet
 */
void box_transitions_send_box_shadows(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    box_render_draws_t *d,
    box_render_packet_t *p);

/** Find the lights whose PointLight or SpotLight runs or leaves, or whose Rgb runs, in this
 * frame: the entities of the nodes and their IsA instances, prefabs excluded. A frame without
 * such a node visits no entity.
 *
 * @param world The world
 * @param s The state of the extract
 * @param p The packet, NULL when no entity has Transition
 */
void box_transitions_lights(
    ecs_world_t *world,
    box_render_transitions_t *s,
    const box_render_packet_t *p);

/** Find the node of the value of a component an entity renders: the node of the entity or the
 * IsA base that owns the value, or while no entity owns one, the node that leaves it.
 *
 * @param world The world
 * @param s The state of the extract
 * @param e The entity
 * @param component The component
 * @return The node, 0 for none
 */
uint32_t box_transitions_owner(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    ecs_entity_t e,
    box_render_transition_component_t component);

#endif
