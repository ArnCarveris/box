/**
 * @file render/bake.h
 * @brief State of the bake extract shared by bake.c and bake_change.c, and the lights the light
 * extract (lights.c) collects for the captures.
 */

#ifndef BOX_RENDER_BAKE_H
#define BOX_RENDER_BAKE_H

#include "render.h"
#include "../geometry/mesh_cache.h"

/** Bounces a bake may have at most, as the range of Godot's LightmapGI */
#define BOX_BAKE_BOUNCES_MAX (16)

/** Passes of a bake at most, the first one and one per bounce */
#define BOX_BAKE_PASSES_MAX (BOX_BAKE_BOUNCES_MAX + 1)

/** A probe in a packet whose capture the backend has not reported yet */
typedef struct box_bake_flight_t {
    uint32_t probe;         /**< Index of the probe */
    int32_t pass;           /**< Pass of the capture */
    int32_t packet;         /**< Number of the packet, as RenderStatus.submitted after it */
} box_bake_flight_t;

/** The bounds a shape had before a change, taken when the change happened */
typedef struct box_bake_change_t {
    ecs_entity_t entity;    /**< The shape */
    ecs_entity_t scene;     /**< Scene of the old bounds */
    bool old;               /**< The shape had bounds in a scene before the change */
    bool geometry;          /**< The change can move, add or remove surfaces, not only their look */
    bool shifted;           /**< The shape moved in a frame with a shift of the world origin,
                             *   a move by the shift alone is no change */
    vec3 min;               /**< Minimum corner of the old bounds */
    vec3 max;               /**< Maximum corner of the old bounds */
} box_bake_change_t;

/** A region of a scene a static shape covered before or after a change */
typedef struct box_bake_region_t {
    ecs_entity_t scene;     /**< Scene of the region */
    bool geometry;          /**< Surfaces of the region moved, came or went */
    vec3 min;               /**< Minimum corner of the region */
    vec3 max;               /**< Maximum corner of the region */
} box_bake_region_t;

/** Lights of a scene in a frame, each with its entity and bake mode */
typedef struct box_bake_light_set_t {
    int32_t count;          /**< Lights */
    const box_render_light_t *lights; /**< The lights, directional first */
    const ecs_entity_t *entities; /**< The entity of each of them */
    const uint8_t *modes;   /**< The LightBakeMode of each of them */
} box_bake_light_set_t;

/** How far a directional light may drift from the state the probes were baked with before
 * it counts as a change, from LightBake or ReflectionBake */
typedef struct box_bake_threshold_t {
    float light;            /**< Degrees between the directions, 0 for any difference */
    float color;            /**< Largest channel difference of the linear colors over the
                             *   brightest channel of either, 0 for any difference */
} box_bake_threshold_t;

/** The lights of a scene in the last frame they were compared with, directional lights within
 * the thresholds as they were when they last counted as a change */
typedef struct box_bake_light_record_t {
    bool known;             /**< The record holds the lights of a frame */
    bool held;              /**< A directional light of that frame differs from the record
                             *   within the thresholds, which keeps its earlier state */
    box_render_view_lights_t view; /**< Lights of the views in that frame */
    ecs_vec_t lights;       /**< box_render_light_t of that frame */
    ecs_vec_t entities;     /**< ecs_entity_t, the entity of each light */
    ecs_vec_t modes;        /**< uint8_t, the LightBakeMode of each light */
    ecs_vec_t scratch;      /**< box_render_light_t, the lights of a frame being compared */
} box_bake_light_record_t;

/** Receives what a light that differs between two frames reaches.
 *
 * @param ctx Context of the comparison
 * @param light The light as it was or is, NULL when the whole scene changed
 */
typedef void (*box_bake_light_mark_t)(
    void *ctx,
    const box_render_light_t *light);

/** Weight of a new measurement in the GPU time per probe */
#define BOX_BAKE_SMOOTHING (0.5f)

/** A probe waiting for its capture and its squared distance to the nearest camera */
typedef struct box_bake_pick_t {
    float distance;         /**< Squared distance to the nearest camera of the scene */
    uint32_t probe;         /**< Index of the probe */
} box_bake_pick_t;

/** The lights the probe captures of a scene are lit with in a frame, from the light extract */
typedef struct box_bake_lights_t {
    box_render_view_lights_t first; /**< Lights of a pass over a black ambient: the Mixed and
                                     *   Baked lights, with the ambient of the views */
    box_render_view_lights_t later; /**< Lights of a pass over a previous one, which holds the
                                     *   direct light of the Baked lights: the Mixed lights */
    uint32_t baked;         /**< Light buffer of the Baked lights, 0 for none */
    uint32_t baked_count;   /**< Baked lights */
    uint32_t baked_directional; /**< Directional lights at the start of the Baked lights */
    int32_t count;          /**< Mixed and Baked lights, 0 without LightBake */
    const box_render_light_t *lights; /**< The Mixed and Baked lights, directional first */
    const ecs_entity_t *entities; /**< The entity of each of them */
    const uint8_t *modes;   /**< The LightBakeMode of each of them */
} box_bake_lights_t;

/** Grid of probes of a volume */
typedef struct box_bake_grid_t {
    float origin[3];        /**< World position of the first probe */
    float spacing;          /**< World units between neighbouring probes */
    int32_t size[3];        /**< Probes per axis, 0 for a volume without probes */
} box_bake_grid_t;

/** State of an area grid besides its volume, see bake_area.c */
typedef struct box_bake_area_t box_bake_area_t;

/** The volume of one scene with LightBake, or of one area with LightBake that a mover carries */
typedef struct box_bake_volume_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    ecs_entity_t entity;    /**< Entity with the LightBake of the scene */
    ecs_entity_t area;      /**< Entity with the VisArea of an area grid, 0 for a scene */
    ecs_entity_t mover;     /**< Mover that carries the area of an area grid, 0 for none */
    mat4 frame;             /**< Grid frame to world in this frame: the mover without its scale
                             *   for an area grid, whose grid, probes and lights are in it */
    mat4 rows;              /**< World to grid frame, the inverse of frame */
    box_bake_area_t *area_data; /**< State of an area grid, NULL for a scene */
    int32_t captures;       /**< Probe captures the backend reported for the volume */
    uint32_t handle;        /**< Volume handle, 0 while the grid has no probes */
    int32_t probes;         /**< Probes of the grid */
    box_bake_grid_t grid;   /**< The grid the backend has */
    float probe_spacing;    /**< LightBake.probe_spacing the grid was placed with */
    float bounds_padding;   /**< LightBake.bounds_padding the grid was placed with */
    bool placed;            /**< The grid was placed at least once */
    bool seen;              /**< A LightBake of the scene exists in this frame */
    bool sent;              /**< The backend has the volume with this grid */
    bool viewed;            /**< A view of the scene is in the packet of this frame */
    bool converged;         /**< The backend reported every pass of every valid probe */
    bool pending;           /**< The bake can progress this frame, computes and has not
                             *   converged */
    bool resort;            /**< The queue is sorted again before the next probes are taken */
    bool debug_warned;      /**< A view with ViewDebug.light_probes was warned that the grid has
                             *   more than BOX_RENDER_DEBUG_PROBES_MAX probes */
    int32_t regrids;        /**< Times the grid was placed again after it had probes */
    bool stream_warned;     /**< The warning that the bake does not support streamed scenes,
                             *   whose static bounds keep changing, was logged */
    float sh[BOX_RENDER_SH_COEFFICIENTS][4]; /**< Irradiance the probes are filled with */
    ecs_entity_t environment; /**< LightBake.environment of this frame */
    uint32_t env_handle;    /**< Cubemap of that source the captures see where nothing covers
                             *   them, 0 for the hemisphere while it has no content */
    int32_t env_content;    /**< EnvironmentStatus.version that cubemap holds, of its last
                             *   content that did more than move, 0 without one */
    uint32_t filled_env;    /**< env_handle the probes were filled with, or rebaked for */
    int32_t filled_content; /**< env_content the probes were filled with, or rebaked for */
    float ambient[BOX_RENDER_SH_COEFFICIENTS][4]; /**< Irradiance of the views of this frame */
    ecs_vec_t valid;        /**< uint32_t, one bit per probe a static surface can use */
    ecs_vec_t checked;      /**< uint32_t, one bit per probe checked again in this frame */
    ecs_vec_t queue;        /**< uint32_t, probes waiting for capture, the nearest last */
    ecs_vec_t flight;       /**< box_bake_flight_t, probes in packets not reported captured */
    ecs_vec_t done;         /**< uint8_t, passes the backend reported for every probe */
    ecs_vec_t cameras;      /**< vec3, cameras of the views of the scene in this frame */
    ecs_vec_t sorted;       /**< vec3, cameras the queue was sorted for */
    box_bake_lights_t lights; /**< Lights of the captures in this frame, with the ambient of
                               *   the first view of the scene */
    vec3 min;               /**< Minimum corner of the static bounds of the grid */
    vec3 max;               /**< Maximum corner of the static bounds of the grid */
    int32_t capture_size;   /**< LightBake.capture_size of this frame */
    float budget_ms;        /**< LightBake.budget_ms of this frame */
    bool compute;           /**< LightBake.compute of this frame: new captures are scheduled */
    float credit;           /**< Share of a probe the budget of the last frame left */
    int32_t bounces;        /**< LightBake.bounces of this frame */
    uint32_t generation;    /**< Number of the bake, so captures of an earlier one are ignored */
    int32_t passes;         /**< Passes of the bake, one more than its bounces */
    int32_t submitted;      /**< Passes whose every probe went into a packet */
    int32_t completed;      /**< Passes the backend reported for every valid probe */
    int32_t count;          /**< Probes every pass of the bake captures */
    int32_t valid_count;    /**< Probes a static surface can use */
    int32_t reported[BOX_BAKE_PASSES_MAX]; /**< Probes the backend reported per pass */
    int32_t first;          /**< Probe buffer the first pass writes, later passes alternate */
    int32_t display;        /**< Probe buffer the views sample */
    int32_t stage;          /**< Probe buffer a staged bake writes and shows when done */
    int32_t baked_size;     /**< capture_size the bake started with */
    bool filled;            /**< The buffer the views sample holds the ambient, no pass */
    bool staged;            /**< The bake writes the stage buffer, shown once every pass is in */
    bool copy;              /**< The shown buffer is copied into the stage buffer next packet */
    bool mask;              /**< The shown mask is sent next packet */
    bool fresh;             /**< A bake of every probe started in this frame */
    ecs_vec_t members;      /**< uint32_t, probes the bake captures in every pass */
    ecs_vec_t dirty;        /**< uint32_t, probes a change queued for the next bake */
    ecs_vec_t flags;        /**< uint8_t per probe, 1 while it is in dirty */
    ecs_vec_t shown;        /**< uint32_t, the validity mask views sample with */
    box_bake_light_record_t known; /**< Lights of the captures in the last frame */
    box_bake_threshold_t threshold; /**< Thresholds of the LightBake of this frame */
    bool relit;             /**< A directional light, the ambient or the environment queued
                             *   every probe since the volume was filled */
    bool settled;           /**< The bake converged once since the volume was filled, or
                             *   completed with only a relight queued; PerfRun waits no more */
    int32_t switches;       /**< Times the views switched to a pass of a bake or a bake
                             *   converged, which changes the light reflections capture */
    const char *error;      /**< Why the volume bakes nothing, NULL for none */
} box_bake_volume_t;

/** Main thread state of the bake extract */
typedef struct box_render_bake_t {
    ecs_query_t *bakes;     /**< Entities with LightBake and their scene */
    ecs_query_t *blends;    /**< Shapes with a TextureBlend, owned or inherited, whose layers
                             *   a change of a layer entity reaches */
    ecs_vec_t volumes;      /**< box_bake_volume_t */
    ecs_vec_t free_handles; /**< uint32_t, volume handles to reuse */
    uint32_t next_handle;   /**< Next volume handle */
    ecs_vec_t hits;         /**< ecs_entity_t, scratch of the overlap queries */
    int32_t indexed;        /**< Entities of the spatial index at the last extract */
    ecs_vec_t picks;        /**< box_bake_pick_t, scratch of the choice of the next probes */
    ecs_vec_t captured;     /**< box_render_bake_result_t, scratch of the reported captures */
    float probe_ms;         /**< GPU milliseconds per probe capture */
    bool measured;          /**< probe_ms comes from a measurement */
    int32_t pending;        /**< Probes left in the volumes that can progress */
    ecs_vec_t changes;      /**< box_bake_change_t, shapes changed since the last extract */
    ecs_vec_t regions;      /**< box_bake_region_t, what those shapes covered before and after
                             *   their change, kept until the next extract */
    bool reflecting;        /**< A scene has ReflectionBake, so changes are recorded without a
                             *   LightBake too */
    bool shifting;          /**< The world origin shifted since the last extract */
    const void *lights_state; /**< State of the light extract, looked up once */
    ecs_map_t light_map;    /**< Light entity to its index in a light record, scratch */
    const void *backend;    /**< Context of the backend the volumes were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;         /**< Generation of that backend, see box_render_backend_t */
    box_task_id_t task;     /**< Report of the probes left, 0 while no volume progresses */
    int32_t task_total;     /**< Total of the report at the last frame */
    bool task_finished;     /**< The report shows its total done and ends at the next frame */
} box_render_bake_t;

/** Find the volume of a scene, never that of one of its areas.
 *
 * @param b The state
 * @param scene The scene root
 * @return The volume, NULL when the scene has none
 */
box_bake_volume_t* box_bake_find(
    const box_render_bake_t *b,
    ecs_entity_t scene);

/** Add an empty volume of a scene to the state.
 *
 * @param b The state
 * @param scene The scene root
 * @return The volume, valid until the next volume is added
 */
box_bake_volume_t* box_bake_volume_add(
    box_render_bake_t *b,
    ecs_entity_t scene);

/** Place probes on the lattice (k + 1/2) * spacing over padded bounds, spaced wider above the
 * probes a volume can have.
 *
 * @param min Minimum corner of the bounds
 * @param max Maximum corner of the bounds
 * @param spacing Units between probes, no grid when not positive
 * @param padding Units added on every side of the bounds
 * @param out Receives the grid
 * @return Probes of the grid
 */
int32_t box_bake_place(
    const vec3 min,
    const vec3 max,
    float spacing,
    float padding,
    box_bake_grid_t *out);

/** Take a new grid for a volume when it differs from its grid: resized on its lattice when the
 * backend has probes of the old one, else sent again; validated either way.
 *
 * @param world The world
 * @param b The state
 * @param p The packet
 * @param v The volume
 * @param grid The new grid
 * @param probes Probes of the new grid
 */
void box_bake_regrid(
    const ecs_world_t *world,
    box_render_bake_t *b,
    box_render_packet_t *p,
    box_bake_volume_t *v,
    const box_bake_grid_t *grid,
    int32_t probes);

/** Write the SH irradiance of the hemisphere ambient of view lights.
 *
 * @param l The lights
 * @param out Receives the coefficients
 */
void box_bake_ambient(
    const box_render_view_lights_t *l,
    float out[BOX_RENDER_SH_COEFFICIENTS][4]);

/** Get the transform of an entity relative to the mover that carries it, composed from the
 * local transforms in between, so it does not change while only the mover moves.
 *
 * @param world The world
 * @param e The entity
 * @param mover The mover, an ancestor of the entity or the entity itself
 * @param out Receives the transform, times the scale of the mover
 * @return Whether the mover is the entity or one of its ancestors
 */
bool box_bake_relative(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t mover,
    mat4 out);

/** Find the area grid of an area.
 *
 * @param b The state
 * @param area Entity with the VisArea
 * @return The volume, NULL when the area has none
 */
box_bake_volume_t* box_bake_find_area(
    const box_render_bake_t *b,
    ecs_entity_t area);

/** Keep the area grid of a LightBake on a VisArea for this frame: its settings, the mover, the
 * frame of the grid, its placement in that frame and its portals.
 *
 * @param world The world
 * @param b The state
 * @param p The packet
 * @param scene Scene of the area
 * @param area Entity with the VisArea and the LightBake
 * @param bake The LightBake
 */
void box_bake_area_update(
    ecs_world_t *world,
    box_render_bake_t *b,
    box_render_packet_t *p,
    ecs_entity_t scene,
    ecs_entity_t area,
    const LightBake *bake);

/** Mark the probes of an area grid inside its area as valid.
 *
 * @param v The volume, with its grid placed
 */
void box_bake_area_validate(
    box_bake_volume_t *v);

/** Point the views of the packet at the area grid of their scene nearest their camera, give the
 * area grids of the scenes of the views their lights and collect those cameras in grid frames.
 *
 * @param world The world
 * @param b The state
 * @param p The packet
 */
void box_bake_area_views(
    ecs_world_t *world,
    box_render_bake_t *b,
    box_render_packet_t *p);

/** Write the frame, area and portals of an area grid into a volume description.
 *
 * @param v The volume
 * @param desc The description
 */
void box_bake_area_desc(
    box_bake_volume_t *v,
    box_render_volume_desc_t *desc);

/** Send the frame of every area grid the backend has whose frame changed since it was sent.
 *
 * @param b The state
 * @param p The packet
 */
void box_bake_area_frames(
    box_render_bake_t *b,
    box_render_packet_t *p);

/** Queue the probes of an area grid a change of the lights its mover carries can reach,
 * compared in the grid frame, so a mover that moves and turns changes none.
 *
 * @param v The volume
 */
void box_bake_area_lights(
    box_bake_volume_t *v);

/** Free the state of an area grid, releasing its light buffers.
 *
 * @param b The state
 * @param v The volume
 * @param p The packet, NULL when the backend goes away with it
 */
void box_bake_area_fini(
    box_render_bake_t *b,
    box_bake_volume_t *v,
    box_render_packet_t *p);

/** Check whether a probe of a volume is one a static surface can use.
 *
 * @param v The volume
 * @param probe Index of the probe
 * @return Whether the probe is valid
 */
bool box_bake_valid(
    const box_bake_volume_t *v,
    uint32_t probe);

/** Get the world position of a probe of a volume.
 *
 * @param v The volume
 * @param index Index of the probe
 * @param out Receives the position
 */
void box_bake_probe_position(
    const box_bake_volume_t *v,
    uint32_t index,
    vec3 out);

/** Check whether a position lies inside a static shape of a scene that the bake captures.
 *
 * @param world The world
 * @param b The state, owns the scratch of the overlap queries
 * @param cache The mesh cache
 * @param scene The scene
 * @param pos The position
 * @return Whether the position is inside
 */
bool box_bake_occupied(
    const ecs_world_t *world,
    box_render_bake_t *b,
    const box_mesh_cache_t *cache,
    ecs_entity_t scene,
    const vec3 pos);

/** Get how far on any axis a point of a static surface can be from a probe it blends: a spacing
 * inside the grid, where it blends the corners of its cell, and as far past it as negative
 * padding moves the border probes off the bounds, where it clamps to them.
 *
 * @param v The volume
 * @return The distance in world units
 */
float box_bake_reach(
    const box_bake_volume_t *v);

/** Check whether a static surface can use a probe: it is outside static geometry and a static
 * shape lies within the reach of it on every axis.
 *
 * @param world The world
 * @param b The state, owns the scratch of the overlap queries
 * @param cache The mesh cache
 * @param v The volume
 * @param pos The position of the probe
 * @return Whether the probe is usable
 */
bool box_bake_usable(
    const ecs_world_t *world,
    box_render_bake_t *b,
    const box_mesh_cache_t *cache,
    const box_bake_volume_t *v,
    const vec3 pos);

/** Queue a probe of a volume for the next bake when it is valid and not queued yet.
 *
 * @param v The volume
 * @param probe Index of the probe
 */
void box_bake_mark(
    box_bake_volume_t *v,
    uint32_t probe);

/** Queue every valid probe of a volume for the next bake.
 *
 * @param v The volume
 */
void box_bake_mark_all(
    box_bake_volume_t *v);

/** Get the state of the bake extract of a world.
 *
 * @param world The world
 * @return The state, NULL without the extract
 */
box_render_bake_t* box_bake_state(
    const ecs_world_t *world);

/** Move the probe grid of the volume of a scene, its static bounds and its cameras by the
 * offset of a shift of the world origin, and take the moves of the static shapes in this
 * frame by the offset alone as no change, so a shift rebakes nothing.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param offset Added to the world positions
 */
void box_render_bake_shift(
    ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 offset);

/** Collect the regions the shapes that changed since the last extract covered before and after
 * their change into the regions of the state, and queue the probes they can reach.
 *
 * @param world The world
 * @param b The state
 */
void box_bake_changes(
    ecs_world_t *world,
    box_render_bake_t *b);

/** Queue the probes a change of the lights of the scene of a volume can reach since the last
 * frame, light by light (a change of bake mode included): a point or spot light the probes
 * within its range, a directional light or the default sun the whole scene.
 *
 * A directional light counts only past the thresholds of the volume.
 *
 * Reads the lights box_bake_scene_lights gave the volume in this frame.
 *
 * @param b The state
 * @param v The volume
 */
void box_bake_lights(
    box_render_bake_t *b,
    box_bake_volume_t *v);

/** Prepare an empty light record.
 *
 * @param r The record
 */
void box_bake_light_record_init(
    box_bake_light_record_t *r);

/** Free a light record.
 *
 * @param r The record
 */
void box_bake_light_record_fini(
    box_bake_light_record_t *r);

/** Compare the lights of a frame with a record and keep them in it.
 *
 * Once the record knows a frame, mark receives NULL when the default sun came or went, or with
 * ambient when the ambient light changed.
 *
 * Otherwise it receives each light added, removed or changed, its bake mode included, matched
 * by entity, as it was and as it is. A directional light within the thresholds is no change:
 * the record keeps its state, so small changes add up, and held of the record is set.
 *
 * @param map Scratch map of light entities
 * @param r The record
 * @param view The lights of the views in this frame
 * @param frame The lights in this frame
 * @param ambient Whether a change of the ambient light changes the whole scene
 * @param threshold How far a directional light may drift without counting
 * @param mark Receives what changed
 * @param ctx Context of mark
 */
void box_bake_lights_compare(
    ecs_map_t *map,
    box_bake_light_record_t *r,
    const box_render_view_lights_t *view,
    const box_bake_light_set_t *frame,
    bool ambient,
    const box_bake_threshold_t *threshold,
    box_bake_light_mark_t mark,
    void *ctx);

/** Get the lights the views of a scene and its reflection captures are lit with in this frame:
 * every light, the Baked ones excepted when the scene has LightBake. Empty for a scene without
 * light entities, which the default sun lights.
 *
 * @param state The state of the light extract
 * @param scene The scene, 0 for the default scene
 * @param out Receives the lights, valid until the next light extract
 */
void box_bake_view_lights(
    const void *state,
    ecs_entity_t scene,
    box_bake_light_set_t *out);

/** Get the lights the probe captures of a scene are lit with in this frame.
 *
 * A pass over a black ambient is lit by the Mixed and Baked lights, a later pass by the Mixed
 * ones, whose first directional light shadows it; the projection adds the direct light of the
 * Baked lights to every probe.
 *
 * While every light is Mixed both are the lights of the views. A scene without light entities
 * is lit by the default sun the views have.
 *
 * @param state The state of the light extract
 * @param scene The scene, 0 for the default scene
 * @param view The lights of the views of the scene, with their ambient
 * @param out Receives the lights, valid until the next light extract
 */
void box_bake_scene_lights(
    const void *state,
    ecs_entity_t scene,
    const box_render_view_lights_t *view,
    box_bake_lights_t *out);

/** Register the observers of the changes of static shapes that rebake the probes near them.
 *
 * @param world The world
 * @param b The state
 */
void box_bake_observers(
    ecs_world_t *world,
    box_render_bake_t *b);

#endif
