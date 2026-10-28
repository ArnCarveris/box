#ifndef FLECS_ENGINE_TEXT3_H
#define FLECS_ENGINE_TEXT3_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_TEXT3_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* World-space text: a string an entity renders as 3D geometry.
 *
 * The glyph outlines of the ui2d font are filled and extruded into a mesh the
 * entity owns, so text is ordinary opaque geometry: lit, depth tested,
 * shadow casting, and readable from any camera. No texture and no atlas are
 * involved, which also means the geometry is built entirely on the CPU and
 * works in a headless world.
 *
 * The text lies in the entity's local XY plane, faces +Z and advances along
 * +X from the baseline of the first line at the entity's origin. Extrusion
 * runs from z 0 to z thickness, so text placed on the face of a sign stands
 * proud of it. Add flecs.engine.camera.Billboard to keep it turned toward
 * the camera.
 *
 * Zero members take a default: size 1 m line height, thickness 6% of size,
 * line_spacing 1.2. Newlines split lines; align positions each line relative
 * to the entity's x origin.
 *
 *   sign {
 *       Position3: {0, 3, 0}
 *       Text3: {text: "Central", size: 0.6, align: 1}
 *       Rgba: {230, 220, 190}
 *   }
 *
 * Setting the text again rebuilds the mesh in place: no entity, table or
 * buffer is added per update, which makes a per-second countdown affordable.
 * An entity that inherits no material gets the module's default text
 * material, so a bare Text3 is visible without authoring a prefab. */

ECS_ENUM(FlecsText3Align, {
    FlecsText3Left,
    FlecsText3Center,
    FlecsText3Right,
});

ECS_STRUCT(FlecsText3, {
    char *text;
    float size;
    float thickness;
    float line_spacing;
    FlecsText3Align align;
});

/* Width in meters of the widest line, with the same defaults the renderer
 * applies. Works headless; does not touch the entity. */
float flecsEngine_text3Width(
    ecs_world_t *world,
    const FlecsText3 *text);

/* Local-space bounds of the geometry the component describes. Returns false
 * for text that produces no geometry (empty string, whitespace only). */
bool flecsEngine_text3Bounds(
    ecs_world_t *world,
    const FlecsText3 *text,
    FlecsAABB *out);

void FlecsEngineText3Import(
    ecs_world_t *world);

#endif
