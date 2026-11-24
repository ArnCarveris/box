/**
 * @file render/reflect.h
 * @brief State of the reflection probes of a scene, shared by reflect.c and reflect_follow.c,
 * which moves the scene probe along with the focus of a streamed scene.
 */

#ifndef BOX_RENDER_REFLECT_H
#define BOX_RENDER_REFLECT_H

#include "bake.h"

/** A probe of a scene */
typedef struct box_reflect_probe_t {
    ecs_entity_t entity;    /**< Entity with the ReflectionProbe, 0 for the scene probe */
    box_render_reflection_probe_t data; /**< Boxes and capture point, slot when resident */
    int32_t slot;           /**< Cubemap slot, -1 when the probe is not resident */
    bool seen;              /**< The probe exists in this frame */
    bool captured;          /**< The backend reported the capture of the probe into its slot */
    bool queued;            /**< A change since that capture or the one in flight queued the
                             *   probe for a new capture */
    uint32_t ticket;        /**< Ticket of the capture in flight */
    int32_t packet;         /**< Packet of the capture in flight, as RenderStatus.submitted after
                             *   it, 0 for none */
    ecs_entity_t mover;     /**< Mover that carries the probe, 0 for none */
    box_render_reflection_probe_t local; /**< Boxes and capture point of a probe a mover
                             *   carries in the frame of the mover, which keeps its capture */
} box_reflect_probe_t;

/** The probes of one scene with ReflectionBake */
typedef struct box_reflect_scene_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    ecs_entity_t entity;    /**< Entity with the ReflectionBake of the scene */
    bool seen;              /**< A ReflectionBake of the scene exists in this frame */
    bool placed;            /**< The scene probe was placed at least once */
    bool sent;              /**< The backend has the buffer with the probes of sent */
    float height;           /**< ReflectionBake.scene_probe_height the scene probe was placed at */
    vec3 scene_capture;     /**< Capture point of the scene probe */
    bool follow;            /**< The scene probe follows a focus of the scene in this frame */
    bool anchored;          /**< The scene probe was placed at the focus at anchor */
    vec3 anchor;            /**< Focus position the scene probe was last placed at */
    float follow_radius;    /**< ReflectionBake.follow_radius of this frame */
    float follow_blend;     /**< ReflectionBake.follow_blend of this frame */
    int32_t previous;       /**< Slot holding the previous capture of the scene probe after it
                             *   followed the focus, shown until the new one is ready and then
                             *   faded out, -1 for none */
    float fade;             /**< Weight of that previous capture, 1 until the new one is ready */
    ecs_vec_t probes;       /**< box_reflect_probe_t */
    ecs_map_t lookup;       /**< Entity of a ReflectionProbe to its index in probes */
    int32_t scene_index;    /**< Index of the scene probe in probes, -1 for none */
    ecs_vec_t slots;        /**< uint8_t per slot, 1 while a probe has it */
    int32_t resident;       /**< Probes with a slot */
    ecs_vec_t cameras;      /**< vec3, cameras of the views of the scene in this frame */
    ecs_vec_t list;         /**< box_render_reflection_probe_t, resident probes in blend order */
    ecs_vec_t sent_list;    /**< box_render_reflection_probe_t the backend has */
    uint32_t buffer;        /**< Handle of the reflection probe buffer, 0 for none */
    uint32_t capacity;      /**< Probes the buffer holds, 0 while the backend lacks it */
    const char *error;      /**< Why the scene captures nothing, NULL for none */
    int32_t resolution;     /**< ReflectionBake.resolution of this frame */
    float budget_ms;        /**< ReflectionBake.budget_ms of this frame */
    uint32_t cubes;         /**< Slots of the cubemap array the backend has, 0 for none */
    int32_t made;           /**< Resolution of that cubemap array */
    uint32_t ticket;        /**< Ticket of the next capture */
    int32_t captured;       /**< Resident probes the backend reported captured */
    bool bounded;           /**< The static bounds were taken at least once */
    bool viewed;            /**< A view of the scene is in the packet of this frame */
    bool pending;           /**< The captures can progress and a resident probe is uncaptured */
    vec3 min;               /**< Minimum corner of the static bounds of the scene */
    vec3 max;               /**< Maximum corner of the static bounds of the scene */
    box_render_view_lights_t lights; /**< Lights of the first view of the scene in this frame */
    uint32_t volume;        /**< Probe volume a view of the scene samples, 0 for none */
    uint32_t volume_buffer; /**< Probe buffer of that volume the views show */
    uint32_t area_volume;   /**< Area grid a view of the scene samples, 0 for none */
    uint32_t area_volume_buffer; /**< Probe buffer of that grid the views show */
    int32_t captures;       /**< Captures the backend reported since the scene got its
                             *   ReflectionBake */
    box_bake_light_record_t record; /**< Lights of the views when the scene was last viewed */
    uint32_t env_handle;    /**< Cubemap of ReflectionBake.environment the captures see where
                             *   nothing covers them, 0 for the hemisphere while it has no
                             *   content */
    int32_t env_content;    /**< EnvironmentStatus.version that cubemap holds, of its last
                             *   content that did more than move, 0 without one */
    bool env_known;         /**< env_was_handle and env_was_content hold those of a frame */
    uint32_t env_was_handle; /**< env_handle when the probes were last invalidated */
    int32_t env_was_content; /**< env_content when the probes were last invalidated */
    bool bake_known;        /**< bake_handle and bake_switches hold the light bake of a frame */
    uint32_t bake_handle;   /**< Volume handle of the light bake of the scene, 0 for none */
    int32_t bake_switches;  /**< box_bake_volume_t.switches of that light bake */
    bool bake_settled;      /**< That light bake settled or does not compute, or the scene
                             *   has none */
    box_bake_threshold_t threshold; /**< Thresholds of the ReflectionBake of this frame */
    int32_t landed;         /**< Resident probes with a capture, current or not */
    bool relit;             /**< A directional light, the ambient, the environment or a settled
                             *   light bake queued every probe since the captures were dropped */
    bool settled;           /**< The captures converged once since they were dropped, or every
                             *   resident probe has one with only a relight queued; PerfRun
                             *   waits no more */
} box_reflect_scene_t;

/** Free the slot of the previous capture of the scene probe when it has one.
 *
 * @param s The scene
 */
void box_reflect_drop_previous(
    box_reflect_scene_t *s);

/** Move the next capture of the scene probe that followed the focus to a new place into a free
 * slot, keeping its capture in its old slot as the previous one that shows until the new
 * capture is ready and then fades out.
 *
 * @param s The scene
 * @param probe The scene probe
 * @return Whether it moved, false without a current capture to keep or a free slot
 */
bool box_reflect_hold(
    box_reflect_scene_t *s,
    box_reflect_probe_t *probe);

/** Fade out the previous capture of the scene probe once its new capture is ready, freeing its
 * slot when it is gone.
 *
 * @param s The scene
 * @param delta Seconds since the last frame
 */
void box_reflect_fade(
    box_reflect_scene_t *s,
    float delta);

/** Check whether a sphere around a point reaches a box.
 *
 * @param point The center of the sphere
 * @param center World center of the box
 * @param half World half size of the box
 * @param radius Radius of the sphere
 * @return Whether the box is within radius of the point
 */
bool box_reflect_within(
    const float *point,
    const vec3 center,
    const vec3 half,
    float radius);

/** Find the focus a scene probe follows: the first focus of the scene, see box_world_focus.
 *
 * @param world The world
 * @param scene The scene
 * @param out Receives the local world position of the focus
 * @return Whether the scene has a focus
 */
bool box_reflect_focus(
    const ecs_world_t *world,
    ecs_entity_t scene,
    vec3 out);

/** Place the scene probe of a scene that follows the focus at height above it, when it was
 * not placed there yet or the focus moved more than distance from where it was placed.
 *
 * @param s The scene
 * @param focus The local world position of the focus
 * @param height ReflectionBake.scene_probe_height
 * @param distance ReflectionBake.follow_distance
 */
void box_reflect_place_focus(
    box_reflect_scene_t *s,
    const vec3 focus,
    float height,
    float distance);

#endif
