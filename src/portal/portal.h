/**
 * @file portal/portal.h
 * @brief The portal-room index, traversal and visibility block of box.portal, shared with box
 * modules.
 *
 * The index holds the areas, portals and occluders of every scene in world space. A traversal
 * starts in the area of the eye and reaches the other areas, and the outdoors (area 0), only
 * through open portals. The visibility block packs what it saw for the cull pass.
 */

#ifndef BOX_PORTAL_PRIVATE_H
#define BOX_PORTAL_PRIVATE_H

#include <box.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Entries a traversal visits at most; a traversal that needs more is truncated */
#define BOX_PORTAL_ENTRIES_MAX (127)

/** Portals a traversal looks through in a row at most */
#define BOX_PORTAL_DEPTH_MAX (12)

/** Planes through the eye and the edges of a clipped portal an entry keeps at most */
#define BOX_PORTAL_HULL_PLANES_MAX (7)

/** Planes of an entry at most: its hull, the plane of its portal and two of the camera */
#define BOX_PORTAL_PLANES_MAX (BOX_PORTAL_HULL_PLANES_MAX + 3)

/** Corners of a portal clipped by the planes of an entry at most */
#define BOX_PORTAL_POLY_MAX (4 + BOX_PORTAL_PLANES_MAX)

/** Distance from the plane of a portal within which an eye inside its opening looks through it
 * with the frustum it has */
#define BOX_PORTAL_NEAR_PASS (0.35f)

/** Planes of an entry in a visibility block: its hull and the plane of its portal */
#define BOX_PORTAL_BLOCK_PLANES (8)

/** Entries of one area in a visibility block at most; an area seen through more is seen whole */
#define BOX_PORTAL_BLOCK_AREA_ENTRIES (4)

/** vec4 of a visibility block at most, the size the cull pass binds */
#define BOX_PORTAL_BLOCK_MAX (2048)

/** Depth an instance has to reach into the box of an area to be a member of it */
#define BOX_PORTAL_MEMBER_DEPTH (0.005f)

/** Smallest size of the world box of an instance on an axis in the membership test */
#define BOX_PORTAL_MEMBER_THIN (0.02f)

/** Part of the world box of an instance the boxes of the areas have to hold for it not to be
 * a member of the outdoors as well */
#define BOX_PORTAL_MEMBER_COVER (0.98f)

/** A room of the index: its outline in world x and z and its height range */
typedef struct box_portal_area_t {
    ecs_entity_t entity;    /**< Entity with the VisArea, 0 for the outdoors */
    ecs_entity_t scene;     /**< Scene of the entity, 0 for the default scene */
    int32_t point_first;    /**< First corner of its outline in the points of the index */
    int32_t point_count;    /**< Corners of its outline */
    float y;                /**< Height of the floor */
    float top;              /**< Height of the ceiling */
    vec2 min;               /**< Smallest x and z of the outline */
    vec2 max;               /**< Largest x and z of the outline */
    int32_t link_first;     /**< First of its portals in the links of the index */
    int32_t link_count;     /**< Portals on its sides */
    int32_t box_first;      /**< First of its boxes in the boxes of the index */
    int32_t box_count;      /**< Boxes that cover its volume */
    bool exact;             /**< The boxes cover its volume and nothing else */
} box_portal_area_t;

/** An axis aligned box of the volume of an area, as the cull pass tests it */
typedef struct box_portal_box_t {
    vec3 min;               /**< Smallest corner */
    vec3 max;               /**< Largest corner */
    int32_t area;           /**< Index of its area */
    bool exact;             /**< The boxes of its area cover the area and nothing else */
} box_portal_box_t;

/** An opening of the index: a rectangle in world space between two areas */
typedef struct box_portal_portal_t {
    ecs_entity_t entity;    /**< Entity with the VisPortal */
    ecs_entity_t scene;     /**< Scene of the entity, 0 for the default scene */
    vec3 verts[4];          /**< Corners, in order around the rectangle */
    vec3 center;            /**< Center */
    vec3 normal;            /**< Unit normal, from the front area toward the back area */
    vec3 right;             /**< Unit direction of its width */
    vec3 up;                /**< Unit direction of its height */
    float d;                /**< Plane offset: normal . p + d is 0 on the plane */
    float width;            /**< Width */
    float height;           /**< Height */
    vec3 min;               /**< Smallest corner of the box around it */
    vec3 max;               /**< Largest corner of the box around it */
    int32_t front;          /**< Area behind the normal, 0 for the outdoors */
    int32_t back;           /**< Area the normal points into, 0 for the outdoors */
    float open;             /**< How far it is open, 0 shut to 1 open */
    PortalSlide slide;      /**< Way its cover slid when it is partly open */
    VisPortal source;       /**< Its component when the index was built or last updated */
    bool pass_through;      /**< It does not narrow the frustum */
    bool double_side;       /**< It leads both ways whichever side the eye is on */
    bool sky_only;          /**< It lets the sky through, not what stands outdoors */
} box_portal_portal_t;

/** A flat blocker of the index */
typedef struct box_portal_occluder_t {
    ecs_entity_t entity;    /**< Entity with the VisOccluder */
    ecs_entity_t scene;     /**< Scene of the entity, 0 for the default scene */
    int32_t area;           /**< Area that holds its center, 0 for the outdoors */
    vec3 center;            /**< Center */
    vec3 normal;            /**< Unit normal */
    vec2 size;              /**< Width and height */
    bool auto_orient_y;     /**< It turns about y to face the eye */
} box_portal_occluder_t;

/** The areas, portals and occluders of a world, in world space */
typedef struct box_portal_index_t {
    ecs_vec_t areas;        /**< box_portal_area_t, the first is the outdoors */
    ecs_vec_t points;       /**< vec2, the outlines of the areas */
    ecs_vec_t boxes;        /**< box_portal_box_t, the boxes of the areas */
    ecs_vec_t portals;      /**< box_portal_portal_t */
    ecs_vec_t links;        /**< int32_t, the portals of each area */
    ecs_vec_t occluders;    /**< box_portal_occluder_t */
    ecs_map_t frames;       /**< Entities whose world transform placed an area, a portal or an
                             *   occluder; the index is rebuilt when one of them moves */
    ecs_query_t *area_query;     /**< Entities with VisArea */
    ecs_query_t *portal_query;   /**< Entities with VisPortal */
    ecs_query_t *occluder_query; /**< Entities with VisOccluder */
    ecs_map_t frozen;       /**< box_portal_camera_t of the views that keep a frozen traversal,
                             *   keyed by the view entity */
    bool dirty;             /**< A component or a transform changed since it was built */
    int32_t builds;         /**< Times it was built */
} box_portal_index_t;

/** Singleton through which the functions below find the index of a world */
typedef struct PortalIndex {
    box_portal_index_t *index; /**< The index */
} PortalIndex;

extern ECS_COMPONENT_DECLARE(PortalIndex);

/** The eye and the frustum a traversal starts with */
typedef struct box_portal_camera_t {
    vec3 eye;               /**< World position of the eye */
    vec4 planes[6];         /**< World space planes of the frustum, inside where non negative;
                             *   the last two bound the depth */
} box_portal_camera_t;

/** An area as seen through a chain of portals */
typedef struct box_portal_entry_t {
    int32_t area;           /**< Index of the area, 0 for the outdoors */
    int32_t parent;         /**< Entry it was reached from, -1 for the area of the eye */
    int32_t via;            /**< Portal it was reached through, -1 for the area of the eye */
    int32_t depth;          /**< Portals between the eye and the area */
    bool from_front;        /**< The portal was crossed from its front area */
    bool sky_only;          /**< Only the sky is seen in it: nothing is looked on through */
    bool whole;             /**< It has the frustum of the camera: the area of the eye and what
                             *   an opening the eye stands in leads to */
    int32_t plane_count;    /**< Planes */
    int32_t hull_count;     /**< The first of them go through the eye and the edges of the
                             *   clipped portal; the plane of the portal follows them */
    vec4 planes[BOX_PORTAL_PLANES_MAX]; /**< World space planes, inside where non negative */
} box_portal_entry_t;

/** What a traversal saw */
typedef struct box_portal_vis_t {
    ecs_entity_t scene;     /**< Scene that was traversed */
    int32_t root;           /**< Index of the area that holds the eye, 0 for the outdoors */
    ecs_vec_t entries;      /**< box_portal_entry_t, parents before their children */
    ecs_vec_t pending;      /**< box_portal_entry_t still to visit, empty after a traversal */
    int32_t tested;         /**< Portals it looked at */
    int32_t passed;         /**< Portals it looked through */
    int32_t closed;         /**< Portals that were closed */
    int32_t occluded;       /**< Portals an occluder hid */
    bool sky;               /**< The outdoors is seen */
    bool truncated;         /**< It ran out of entries: areas it did not reach may be seen */
} box_portal_vis_t;

/** Get the index of a world, built again when a component or a transform changed.
 *
 * @param world The world
 * @return The index, NULL when the world does not import box.portal
 */
const box_portal_index_t* box_portal_index(
    ecs_world_t *world);

/** Create the index of a world and the observers that mark it changed.
 *
 * @param world The world
 */
void box_portal_index_import(
    ecs_world_t *world);

/** Check whether a scene has areas.
 *
 * @param index The index
 * @param scene The scene, 0 for the default scene
 * @return Whether an area of the index is in the scene
 */
bool box_portal_index_has(
    const box_portal_index_t *index,
    ecs_entity_t scene);

/** Find the area of a scene that holds a point.
 *
 * @param index The index
 * @param scene The scene, 0 for the default scene
 * @param p The point, in world space
 * @return Index of the first area that holds it, 0 for the outdoors
 */
int32_t box_portal_index_area_at(
    const box_portal_index_t *index,
    ecs_entity_t scene,
    const float *p);

/** Find the portal of an entity.
 *
 * @param index The index
 * @param portal Entity with the VisPortal
 * @return Index of the portal, -1 when the index has none for it
 */
int32_t box_portal_index_portal(
    const box_portal_index_t *index,
    ecs_entity_t portal);

/** How far open a portal counts as shut, so nothing is seen through it */
#define BOX_PORTAL_SHUT (0.02f)

/** Find the area that holds a point.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param p The point, in world space
 * @return Entity of the area, 0 for the outdoors
 */
ecs_entity_t box_portal_area_at(
    ecs_world_t *world,
    ecs_entity_t scene,
    const float *p);

/** Find the areas the world box of a shape is a member of: every area it reaches into, and the
 * outdoors when the areas do not hold all of it.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param min Smallest corner of the box
 * @param max Largest corner of the box
 * @param areas Receives the entities of the areas (ecs_entity_t), appended
 * @param outdoors Receives whether it is a member of the outdoors
 */
void box_portal_members(
    ecs_world_t *world,
    ecs_entity_t scene,
    const float *min,
    const float *max,
    ecs_vec_t *areas,
    bool *outdoors);

/** Set the eye and the frustum planes of a camera from its matrices.
 *
 * @param camera Receives the eye and the planes
 * @param view_proj World to clip space, with 0 <= z <= w
 * @param eye World position of the eye
 */
void box_portal_camera_init(
    box_portal_camera_t *camera,
    mat4 view_proj,
    const float *eye);

/** Clip a convex polygon by planes.
 *
 * @param poly Corners of the polygon, in order
 * @param count Corners
 * @param planes Planes, inside where non negative
 * @param plane_count Planes
 * @param out Receives the corners of what is inside every plane, BOX_PORTAL_POLY_MAX at most
 * @return Corners written, 0 when nothing is left
 */
int32_t box_portal_clip(
    const vec3 *poly,
    int32_t count,
    const vec4 *planes,
    int32_t plane_count,
    vec3 *out);

/** Write the planes through an eye and the edges of a convex polygon, which bound what the eye
 * sees through the polygon. With more edges than planes the shortest edges get none, which
 * only widens what is seen.
 *
 * @param eye The eye
 * @param poly Corners of the polygon, in order
 * @param count Corners
 * @param out Receives the planes, inside where non negative
 * @param max Planes to write at most
 * @return Planes written
 */
int32_t box_portal_hull_planes(
    const float *eye,
    const vec3 *poly,
    int32_t count,
    vec4 *out,
    int32_t max);

/** Check whether a box reaches inside every plane.
 *
 * @param min Smallest corner
 * @param max Largest corner
 * @param planes Planes, inside where non negative
 * @param count Planes
 * @return false when the box is outside one of the planes
 */
bool box_portal_box_inside(
    const float *min,
    const float *max,
    const vec4 *planes,
    int32_t count);

/** Check whether every corner of a box is inside every plane.
 *
 * @param min Smallest corner
 * @param max Largest corner
 * @param planes Planes, inside where non negative
 * @param count Planes
 * @return Whether the planes hold the whole box
 */
bool box_portal_box_contained(
    const float *min,
    const float *max,
    const vec4 *planes,
    int32_t count);

/** Start an empty traversal result.
 *
 * @param vis The result
 */
void box_portal_vis_init(
    box_portal_vis_t *vis);

/** Free a traversal result.
 *
 * @param vis The result
 */
void box_portal_vis_fini(
    box_portal_vis_t *vis);

/** Walk the areas a camera sees: the area that holds its eye, then every area behind an open
 * portal that is in the frustum of the entry it is looked at from, with the frustum the portal
 * leaves of it.
 *
 * @param index The index
 * @param scene The scene, 0 for the default scene
 * @param camera The eye and its frustum
 * @param vis Receives the entries, cleared first
 */
void box_portal_traverse(
    const box_portal_index_t *index,
    ecs_entity_t scene,
    const box_portal_camera_t *camera,
    box_portal_vis_t *vis);

/** Keep the camera a view looked from when its traversal froze, and give it back while it
 * stays frozen.
 *
 * @param index The index, which holds the cameras
 * @param view The view
 * @param freeze Whether the traversal of the view is frozen
 * @param camera The camera of the view, replaced by the frozen one when there is one
 * @return Whether the camera was replaced: false for the frame that froze, which draws from
 * the camera as it is, and for a view that is not frozen
 */
bool box_portal_freeze(
    const box_portal_index_t *index,
    ecs_entity_t view,
    bool freeze,
    box_portal_camera_t *camera);

/** Free the frozen cameras of an index.
 *
 * @param index The index
 */
void box_portal_freeze_fini(
    box_portal_index_t *index);

/** Write the corners of an occluder as an eye sees it and the box around them.
 *
 * @param o The occluder
 * @param eye The eye, which an occluder with auto_orient_y turns to
 * @param normal Receives the normal it has for the eye
 * @param verts Receives the four corners
 * @param min Receives the smallest corner of the box around them
 * @param max Receives the largest corner of the box around them
 */
void box_portal_occluder_corners(
    const box_portal_occluder_t *o,
    const float *eye,
    float *normal,
    vec3 *verts,
    float *min,
    float *max);

/** Pack what a traversal saw for the cull pass.
 *
 * The block is a run of vec4, laid out as block.c describes, with indices relative to its
 * first vec4.
 *
 * @param index The index
 * @param vis The traversal
 * @param out Receives the block (vec4), appended
 * @param max vec4 the block may take at most
 * @return Whether a block was written: false for a truncated traversal, a scene without areas
 * or a block that does not fit, all of which have to draw every instance
 */
bool box_portal_block_pack(
    const box_portal_index_t *index,
    const box_portal_vis_t *vis,
    ecs_vec_t *out,
    int32_t max);

/** Check whether a traversal sees a world box, exactly as the cull pass does (portal_visible of
 * cull.wgsl): it is seen when it is inside the planes of an entry of an area it is a member of.
 *
 * @param block The block box_portal_block_pack wrote
 * @param min Smallest corner of the box
 * @param max Largest corner of the box
 * @return Whether an entry sees the box
 */
bool box_portal_block_sees(
    const vec4 *block,
    const float *min,
    const float *max);

/** Register the system that tells sensors whether an actor is near.
 *
 * @param world The world
 */
void box_portal_sensors_import(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
