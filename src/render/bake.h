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
    vec3 min;               /**< Minimum corner of the old bounds */
    vec3 max;               /**< Maximum corner of the old bounds */
} box_bake_change_t;

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

/** The volume of one scene with LightBake */
typedef struct box_bake_volume_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    ecs_entity_t entity;    /**< Entity with the LightBake of the scene */
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
    bool pending;           /**< The bake can progress this frame and has not converged */
    bool resort;            /**< The queue is sorted again before the next probes are taken */
    float sh[BOX_RENDER_SH_COEFFICIENTS][4]; /**< Irradiance the probes are filled with */
    float ambient[BOX_RENDER_SH_COEFFICIENTS][4]; /**< Irradiance of the views of this frame */
    ecs_vec_t valid;        /**< uint32_t, one bit per probe outside static geometry */
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
    int32_t bounces;        /**< LightBake.bounces of this frame */
    uint32_t generation;    /**< Number of the bake, so captures of an earlier one are ignored */
    int32_t passes;         /**< Passes of the bake, one more than its bounces */
    int32_t submitted;      /**< Passes whose every probe went into a packet */
    int32_t completed;      /**< Passes the backend reported for every valid probe */
    int32_t count;          /**< Probes every pass of the bake captures */
    int32_t valid_count;    /**< Probes outside static geometry */
    int32_t reported[BOX_BAKE_PASSES_MAX]; /**< Probes the backend reported per pass */
    int32_t first;          /**< Probe buffer the first pass writes, later passes alternate */
    int32_t display;        /**< Probe buffer the views sample */
    int32_t stage;          /**< Probe buffer a staged bake writes and shows when done */
    int32_t baked_size;     /**< capture_size the bake started with */
    bool filled;            /**< The buffer the views sample holds the ambient, no pass */
    bool staged;            /**< The bake writes the stage buffer, shown once every pass is in */
    bool copy;              /**< The shown buffer is copied into the stage buffer next packet */
    bool mask;              /**< The shown mask is sent next packet */
    bool lights_known;      /**< baked_lights and light_list hold the lights of a frame */
    bool fresh;             /**< A bake of every probe started in this frame */
    ecs_vec_t members;      /**< uint32_t, probes the bake captures in every pass */
    ecs_vec_t dirty;        /**< uint32_t, probes a change queued for the next bake */
    ecs_vec_t flags;        /**< uint8_t per probe, 1 while it is in dirty */
    ecs_vec_t shown;        /**< uint32_t, the validity mask views sample with */
    ecs_vec_t light_list;   /**< box_render_light_t of the scene in the last frame */
    ecs_vec_t light_entities; /**< ecs_entity_t, the entity of each light of light_list */
    ecs_vec_t light_modes;  /**< uint8_t, the LightBakeMode of each light of light_list */
    box_render_view_lights_t baked_lights; /**< Lights of the views in the last frame */
    const char *error;      /**< Why the volume bakes nothing, NULL for none */
} box_bake_volume_t;

/** Main thread state of the bake extract */
typedef struct box_render_bake_t {
    ecs_query_t *bakes;     /**< Entities with LightBake and their scene */
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
    const void *lights_state; /**< State of the light extract, looked up once */
    ecs_map_t light_map;    /**< Light entity to its index in a light_list, scratch */
    const void *backend;    /**< Context of the backend the volumes were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;         /**< Generation of that backend, see box_render_backend_t */
} box_render_bake_t;

/** Find the volume of a scene.
 *
 * @param b The state
 * @param scene The scene root
 * @return The volume, NULL when the scene has none
 */
box_bake_volume_t* box_bake_find(
    const box_render_bake_t *b,
    ecs_entity_t scene);

/** Check whether a probe of a volume is outside static geometry.
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

/** Queue the probes the shapes that changed since the last extract can reach, around the
 * bounds they had and the bounds they have now.
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
 * Reads the lights box_bake_scene_lights gave the volume in this frame.
 *
 * @param b The state
 * @param v The volume
 */
void box_bake_lights(
    box_render_bake_t *b,
    box_bake_volume_t *v);

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
