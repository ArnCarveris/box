/**
 * @file render/draw_box_shadow.h
 * @brief Box shadows of 2D shapes drawn by the instanced batches.
 *
 * Included by draw.h. Each BoxShadow of a 2D shape gets a slot of its own in a 2D batch, which
 * these functions add, update, transition and remove alongside the shape.
 */

#ifndef BOX_RENDER_DRAW_BOX_SHADOW_H
#define BOX_RENDER_DRAW_BOX_SHADOW_H

/** Register the shadows of the 2D shapes: the BoxShadowSlot of every BoxShadow.
 *
 * @param world The world
 */
void box_draws_box_shadow_init(
    ecs_world_t *world);

/** Register the observers of the shadows: a set BoxShadow writes the shadow again, a removed one
 * frees its slot, also on a deleted or disabled entity.
 *
 * @param world The world
 * @param d The state
 */
void box_draws_box_shadow_observers(
    ecs_world_t *world,
    box_render_draws_t *d);

/** Bring the shadow of a changed entity into the batch of its shape and write its instance, or
 * free its slot when it draws no shadow: without a BoxShadow, without a drawn Rect or Circle, or
 * with an empty shadow box.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param s The DrawSlot of the entity, its shape already updated
 */
void box_draws_box_shadow_update(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const DrawSlot *s);

/** Get the shadow slots of the rows of a query result, so the per frame paths check for a shadow
 * once per table.
 *
 * @param it The query result
 * @return The BoxShadowSlot of each row, NULL when the table has none. A slot stays when its
 *         BoxShadow is removed, without a batch, so it costs a check of the batch per row
 */
const BoxShadowSlot* box_draws_box_shadows(
    const ecs_iter_t *it);

/** Write the streams of the shadow of an entity that its batch uploads every frame, as the
 * shape of the entity writes them: the transform of a DynamicTransform shape, the shading of a
 * DynamicMaterial shape. A shadow without a slot is left as it is.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param bs The BoxShadowSlot of the entity
 * @param s The DrawSlot of the entity
 * @param transform World transform of the entity, NULL for the identity
 * @param material Material components of the shape, read when kinds has the shading
 * @param kinds box_draw_kind_t, the streams to write
 */
void box_draws_box_shadow_write(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const BoxShadowSlot *bs,
    const DrawSlot *s,
    const WorldTransform *transform,
    const box_draw_material_t *material,
    uint8_t kinds);

/** Get the instance matrix of the shadow of an entity relative to the world transform of its
 * shape, as box_draws_box_shadow_write writes it: the quad of the shape stretched, moved by
 * the offset and scaled to the quad of the shadow.
 *
 * A transform transition moves the shadow with the world matrix it renders the shape at times
 * this matrix.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the matrix
 * @return Whether the entity draws a shadow box
 */
bool box_draws_box_shadow_instance(
    const ecs_world_t *world,
    ecs_entity_t e,
    mat4 out);

/** Get the target material of the shading record of the shadow of an entity, which a Tint or
 * Opacity transition patches: the Tint and Opacity of its shape, the inherited tints and fade,
 * the color of the shadow and the alpha the shape covers it with. No node is set.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param s The DrawSlot of the entity
 * @param material Material components of the shape (box_draws_shape_material)
 * @param out Receives the target material
 * @return Whether the entity draws a shadow
 */
bool box_draws_box_shadow_target(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const DrawSlot *s,
    const box_draw_material_t *material,
    box_draw_transition_material_t *out);

/** Get the target of the box shadow record of an entity whose BoxShadow transitions: the
 * target material of box_draws_box_shadow_target with the resolved Rect in shape2d, the
 * BoxShadow, the mesh scale and the world matrix of its shape.
 *
 * No node is set, rel is the identity and transform 0.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 * @param s The DrawSlot of the entity
 * @param material Material components of the shape (box_draws_shape_material)
 * @param out Receives the target
 * @return Whether the entity has a shadow slot
 */
bool box_draws_box_shadow_transition(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const DrawSlot *s,
    const box_draw_material_t *material,
    box_draw_transition_box_shadow_t *out);

/** Write the transform and the shading of a shadow from the target of a box shadow record and
 * a BoxShadow, as box_draws_box_shadow_update writes them. Pure: the resolver calls it on the
 * render thread.
 *
 * A shadow box without an area writes an instance that draws nothing. The clip and rank of the
 * shading are kept.
 *
 * @param material The material components, with the rendered Tint, Opacity and fade
 * @param record The target of the record
 * @param shadow The BoxShadow to draw, the value its node renders
 * @param world World matrix of the shape relative to the render origin, the one it renders at
 * @param origin The render origin of the packet
 * @param transform The box_render_transform_t of the shadow in the packet data, unaligned
 * @param shading The box_render_shading_t of the shadow in the packet data, unaligned
 */
void box_draws_box_shadow_patch(
    const box_draw_material_t *material,
    const box_draw_transition_box_shadow_t *record,
    const BoxShadow *shadow,
    mat4 world,
    const double *origin,
    void *transform,
    void *shading);

/** Get an instance matrix of the shadow of an entity, relative to the world transform of its
 * shape, whose quad holds every shadow a BoxShadow transition renders between per field bounds.
 *
 * It has the widest blur and spread, grown by half the range of the offset around its middle.
 *
 * @param world The world
 * @param e The entity
 * @param lo Per field minimum of offset_x, offset_y, blur and spread
 * @param hi Per field maximum of them
 * @param out Receives the matrix
 * @return Whether that shadow box has an area
 */
bool box_draws_box_shadow_bound(
    const ecs_world_t *world,
    ecs_entity_t e,
    const float *lo,
    const float *hi,
    mat4 out);

/** Get the alpha a shape covers what is under it with at its edge, which the shadow is cut out
 * by: its opacity, times the alpha of its border when it has one, for the blend modes that draw
 * over the target, 0 for the others (Additive, Multiply and the like), which cover nothing.
 *
 * Pure, so the resolver computes it again for the Border a transition renders.
 *
 * @param shape The resolved shape
 * @param blend The blend mode of the shape
 * @param alpha Resolved opacity of the shape
 * @return The alpha from 0 to 1
 */
float box_draws_box_shadow_cover(
    const Shape2D *shape,
    uint8_t blend,
    float alpha);

/** Encode the color and the cover alpha of the shading of a shadow from a shadow record, as
 * box_draws_box_shadow_update writes them. Pure: the resolver calls it on the render thread.
 *
 * @param material The material components, with the rendered Tint, Opacity and fade
 * @param target The target material of the record
 * @param dst The box_render_shading_t of the shadow in the packet data, unaligned
 */
void box_draws_box_shadow_encode(
    const box_draw_material_t *material,
    const box_draw_transition_material_t *target,
    void *dst);

/** Free the shadow slot of an entity.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 */
void box_draws_box_shadow_remove(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e);

/** Point the BoxShadowSlot of an entity at the slot its shadow moves to, when the shadow is the
 * instance that moves.
 *
 * @param world The world
 * @param moved The entity of the instance that moves
 * @param batch Index of the batch
 * @param from The slot the instance was in
 * @param to The slot the instance moves to
 * @return Whether the instance is the shadow of the entity
 */
bool box_draws_box_shadow_moved(
    ecs_world_t *world,
    ecs_entity_t moved,
    int32_t batch,
    int32_t from,
    int32_t to);

#endif
