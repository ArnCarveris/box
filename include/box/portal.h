/**
 * @file box/portal.h
 * @brief Authoring components of a portal-room world: areas, portals, occluders and doors.
 *
 * The box.portal module only registers the components, so scenes can describe a sectored
 * building the way a portal visibility system reads it: an area is an extruded 2D outline, a
 * portal is a planar opening that connects the areas on its two sides, an occluder is a flat
 * blocker and a door drives the closed flag of its portal. Nothing here culls, draws or
 * collides; a system that does reads these components.
 *
 * Positions are in the space of the entity's parent. The area outline, the portal and occluder
 * center and every normal are in that space too.
 */

#ifndef BOX_PORTAL_H
#define BOX_PORTAL_H

#include "types.h"

#ifdef BOX_PORTAL_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Surfaces of an area, each the entity of a material prefab, 0 for the default */
ECS_STRUCT(VisAreaMaterials, {
    ecs_entity_t floor;    /**< Floor */
    ecs_entity_t wall;     /**< Inner side of the walls */
    ecs_entity_t ceiling;  /**< Ceiling */
    ecs_entity_t exterior; /**< Outer side of the walls, where the area has an outside */
    ecs_entity_t roof;     /**< Roof above the ceiling */
});

/** A room: the outline extruded from y up by height.
 *
 * Portals connect areas; every portal with an area on one side only opens to the outdoors.
 * Edges that two areas share get no exterior face, and an area below ground gets no shell.
 *
 * @par A room with a door to the outside:
 * @code
 * hall {
 *   VisArea: {shape: [{-6, 0}, {6, 0}, {6, 10}, {-6, 10}], y: 0, height: 5,
 *     ambient: {0.018, 0.018, 0.022}, hub: {0, 1.8, 5}}
 * }
 * @endcode
 */
ECS_STRUCT(VisArea, {
    ecs_vec(vec2) shape;        /**< Outline corners in x and z, in either winding */
    float y;                    /**< Height of the floor */
    float height;               /**< Distance from the floor to the ceiling */
    vec3 ambient;               /**< Ambient light of the area, linear */
    float sun;                  /**< Unshadowed sun on top of the sun through portals, for
                                 *   roofless areas, default 0 */
    vec4 fog;                   /**< Fog color in x, y, z and density in w, density 0 for none */
    vec3 hub;                   /**< Point of the area that routes between areas */
    VisAreaMaterials materials; /**< Surfaces */
    float shell_from;           /**< Height below which the walls get no outer shell, 0 for the
                                 *   whole area */
    bool no_terrain;            /**< The terrain does not cut into the area */
    bool no_nav;                /**< Routes do not pass through the area */
});

/** What an opening is */
ECS_ENUM(PortalKind, {
    PortalKindDoor,   /**< Opening in a wall, a door */
    PortalKindWindow, /**< Opening in a wall, glazed */
    PortalKindHatch   /**< Opening in a floor or ceiling */
});

/** A planar opening between two areas, centered on the entity.
 *
 * The plane has the normal and extends size.x along the horizontal axis of the plane (for a
 * hatch, along x) and size.y along the other. The areas on the two sides are found by probing
 * along the normal. When one side holds no area it is the outdoors.
 *
 * @par A door in the hall's north wall:
 * @code
 * hall_corridor {
 *   Position: {0, 1.3, 10}
 *   VisPortal: {kind: Door, size: {2.4, 2.6}, normal: {0, 0, 1}}
 * }
 * @endcode
 */
ECS_STRUCT(VisPortal, {
    PortalKind kind;     /**< What the opening is */
    vec2 size;           /**< Width and height of the opening */
    vec3 normal;         /**< Normal of the plane, unit */
    bool glass;          /**< A pane fills the opening */
    vec4 glass_tint;     /**< Tint in x, y, z and opacity in w of the pane, 0 for the default
                          *   pane */
    bool closed;         /**< Nothing is seen through it, a door sets it */
    bool locked;         /**< A door cannot open it */
    bool pass_through;   /**< Actors walk through it without a door */
    bool double_side;    /**< It leads both ways whichever side the viewer is on */
    bool sky_only;       /**< It lets the sky through, not the area behind it */
});

/** A flat blocker, centered on the entity, that hides what is behind it from the viewer.
 *
 * With auto_orient_y the plane turns about the vertical axis to face the viewer.
 */
ECS_STRUCT(VisOccluder, {
    vec2 size;           /**< Width and height */
    vec3 normal;         /**< Normal of the plane, unit */
    bool auto_orient_y;  /**< The plane turns about y to face the viewer */
});

/** How a door moves */
ECS_ENUM(PortalDoorStyle, {
    PortalDoorStylePlain, /**< A sliding panel in a wall */
    PortalDoorStyleShip   /**< A watertight panel with a raised frame */
});

/** The way a door cover slides when it opens */
ECS_ENUM(PortalDoorSlide, {
    PortalDoorSlideSide, /**< Along the plane, to the side */
    PortalDoorSlideDown, /**< Along the plane, down */
    PortalDoorSlideUp    /**< Along the plane, up */
});

/** A door that fills a portal and drives its closed flag.
 *
 * @par An automatic door:
 * @code
 * entry_door { PortalDoor: {portal: p_entry, auto_open: true} }
 * @endcode
 */
ECS_STRUCT(PortalDoor, {
    ecs_entity_t portal;    /**< The VisPortal it fills */
    bool auto_open;         /**< It opens for actors that come near */
    bool locked;            /**< It starts locked */
    PortalDoorStyle style;  /**< How it looks and moves */
    PortalDoorSlide slide;  /**< Where its panel slides to */
    float radius;           /**< Distance at which an actor opens it, 0 for 1.8 */
    float lift;             /**< Distance the panel lifts clear of its frame before it slides */
    float speed;            /**< Travel in widths per second, 0 for 1.2 */
    ecs_entity_t material;  /**< Material prefab of the panel, 0 for the default */
});

/** Register the box.portal module.
 *
 * @param world The world
 */
void BoxPortalImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
