/**
 * @file render/lights.h
 * @brief Private state of the light extract, shared by lights.c and the light transitions of
 * lights_transition.c.
 */

#ifndef BOX_RENDER_LIGHTS_H
#define BOX_RENDER_LIGHTS_H

#include "bake.h"
#include "../atmosphere/atmosphere.h"

/** Smallest range a light record of a transition writes, as a range of 0 is a directional
 * light */
#define BOX_LIGHTS_MIN_RANGE (0.001f)

/** Linear value of every display encoded channel value */
extern float box_lights_srgb[256];

/** The lists of lights of a scene, by the modes they hold */
typedef enum box_light_list_kind_t {
    BOX_LIGHTS_VIEW,        /**< Lights of the views: every light, or without the Baked ones */
    BOX_LIGHTS_CAPTURE,     /**< Mixed and Baked lights, of captures over a black ambient */
    BOX_LIGHTS_MIXED,       /**< Mixed lights, of captures over a previous pass */
    BOX_LIGHTS_BAKED,       /**< Baked lights, whose direct light the projection adds */
    BOX_LIGHTS_LISTS        /**< Number of lists */
} box_light_list_kind_t;

/** A list of lights of a scene and what its light buffer holds on the backend */
typedef struct box_light_list_t {
    uint32_t buffer;        /**< Handle of the light buffer, 0 before it is created */
    uint32_t capacity;      /**< Lights the light buffer holds, 0 when the backend lacks it */
    uint32_t directional;   /**< Directional lights at the start of lights */
    ecs_vec_t lights;       /**< box_render_light_t of this frame */
    ecs_vec_t entities;     /**< ecs_entity_t, the entity of each light of this frame */
    ecs_vec_t modes;        /**< uint8_t, the LightBakeMode of each light of this frame */
    ecs_vec_t sent;         /**< box_render_light_t the light buffer holds */
} box_light_list_t;

/** A light of an atmosphere: its sun or its moon */
typedef struct box_light_atmos_light_t {
    bool found;             /**< The light was collected in this frame */
    float direction[3];     /**< Normalized direction toward the light */
    float color[3];         /**< Color of the light before the transmittance */
    float transmittance[3]; /**< Transmittance from the ground altitude toward the light */
    float fade;             /**< Fade of its disk at the horizon */
    float attenuated[3];    /**< Color times the transmittance and the fade, for the moon also
                             *   its illumination */
} box_light_atmos_light_t;

/** The Atmosphere of a scene in this frame */
typedef struct box_light_atmos_t {
    ecs_entity_t entity;    /**< Entity with the Atmosphere, 0 when the scene has none */
    const char *error;      /**< Why its values are invalid, NULL when they are valid */
    Atmosphere value;       /**< Its values */
    box_atmos_medium_t medium; /**< Its medium, set when it is valid */
    box_light_atmos_light_t sun; /**< The sun */
    box_light_atmos_light_t moon; /**< The moon */
    float moon_illumination; /**< Lit fraction of the moon, 0 without a sun or a moon */
} box_light_atmos_t;

/** A directional light of a scene in this frame, as its entity sets it */
typedef struct box_light_sun_t {
    ecs_entity_t entity;    /**< The light entity */
    box_render_light_t light; /**< The light before any attenuation */
} box_light_sun_t;

/** A shadow casting spot or point light of the view list of a scene in this frame */
typedef struct box_light_caster_t {
    ecs_entity_t entity;    /**< The light entity */
    int32_t index;          /**< Index of the light in the view list */
    int32_t resolution;     /**< SpotLight.shadow_resolution or PointLight.shadow_resolution */
    float range;            /**< Range of the tile: that of the light, or while it transitions
                             *   the largest range it renders, so the tile does not change
                             *   in every frame of the transition */
    float outer;            /**< Outer half angle in degrees of a spot light, the largest one
                             *   it renders while it transitions */
    bool point;             /**< A point light, whose tile is octahedral */
} box_light_caster_t;

/** A tile the shadow atlas of a scene holds, as it last rendered or kept it */
typedef struct box_light_tile_t {
    ecs_entity_t light;     /**< Entity of the light */
    int32_t resolution;     /**< Its shadow_resolution */
    bool point;             /**< A point light tile */
    box_render_shadow_record_t record; /**< Its record, which holds the projection and place */
} box_light_tile_t;

/** The shadow atlas of a scene and what the backend holds of it */
typedef struct box_light_atlas_t {
    ShadowAtlas settings;   /**< ShadowAtlas of the scene this frame, resolved */
    bool found;             /**< The scene has a ShadowAtlas this frame */
    uint32_t handle;        /**< Handle of the atlas and its tile buffer, 0 while it has none */
    int32_t resolution;     /**< Edge the backend has the atlas at, 0 when it lacks it */
    bool buffer;            /**< The backend has the tile buffer */
    ecs_vec_t records;      /**< box_render_shadow_record_t of this frame */
    ecs_vec_t tiles;        /**< box_render_shadow_tile_t of this frame, as records */
    ecs_vec_t sent;         /**< box_render_shadow_record_t the tile buffer holds */
    ecs_vec_t known;        /**< box_light_tile_t, the tiles of the last frame, which the atlas
                             *   holds */
} box_light_atlas_t;

/** The TimeOfDay of a scene in this frame */
typedef struct box_light_tod_t {
    ecs_entity_t entity;    /**< Entity with the TimeOfDay, 0 when the scene has none */
    ecs_entity_t sun;       /**< TimeOfDay.sun, 0 for the first directional light */
    ecs_entity_t moon;      /**< TimeOfDay.moon, 0 for none */
    bool sun_found;         /**< The sun was collected in this frame */
    bool moon_found;        /**< The moon was collected in this frame */
} box_light_tod_t;

/** The lights of one scene */
typedef struct box_light_scene_t {
    ecs_entity_t scene;     /**< Scene root, 0 for the default scene */
    dvec3 origin;           /**< Render origin of the frame the light positions are relative to */
    bool has_lights;        /**< The scene has a light entity, even one that lights nothing */
    bool has_ambient;       /**< The scene has an AmbientLight */
    bool baking;            /**< The scene has LightBake, so bake modes apply */
    bool split;             /**< Baking with a light that is not Mixed: the capture, mixed and
                             *   baked lists are kept, else the captures use the view list */
    bool atmos_unlit;       /**< A scene without light entities with a valid atmosphere: its
                             *   views get its empty view list rather than the default sun */
    box_light_atmos_t atmos; /**< The atmosphere of the scene */
    box_light_tod_t tod;    /**< The time of day of the scene */
    float sky[3];           /**< Linear ambient light from above, summed */
    float ground[3];        /**< Linear ambient light from below, summed */
    box_light_list_t lists[BOX_LIGHTS_LISTS]; /**< The lists, by box_light_list_kind_t */
    ecs_vec_t casters;      /**< box_light_caster_t, the shadow casting spot and point lights
                             *   of this frame */
    ecs_vec_t suns;         /**< box_light_sun_t, the directional lights of this frame */
    box_light_atlas_t atlas; /**< The shadow atlas of the scene */
} box_light_scene_t;

/** A view that point and spot lights of its scene are culled against */
typedef struct box_light_cull_view_t {
    ecs_entity_t scene;     /**< Scene whose lights the view shades with */
    vec4 planes[6];         /**< Planes of its frustum, normals of unit length facing inward */
    int32_t plane_count;    /**< Planes in planes */
    vec3 eye;               /**< Position of its camera */
    float pixels;           /**< Pixels per world unit at a distance of 1 for a perspective
                             *   camera, 0 to cull by the frustum only */
} box_light_cull_view_t;

/** Main thread state of the light extract */
typedef struct box_render_lights_t {
    dvec3 origin;           /**< Render origin of the frame (box_render_origin) */
    ecs_query_t *directional;   /**< Entities with DirectionalLight */
    ecs_query_t *point;         /**< Entities with PointLight */
    ecs_query_t *spot;          /**< Entities with SpotLight */
    ecs_query_t *ambient;       /**< Entities with AmbientLight */
    ecs_query_t *bakes;         /**< Entities with LightBake */
    ecs_query_t *atlases;       /**< Entities with ShadowAtlas */
    ecs_query_t *atmospheres;   /**< Entities with Atmosphere, NULL without box.atmosphere */
    ecs_query_t *times;         /**< Entities with TimeOfDay, NULL without box.atmosphere */
    box_render_fog_t *fog;      /**< Fog volumes of the views, see box_render_fog_views */
    box_render_stars_t *stars;  /**< Star fields of the views, see box_render_star_views */
    box_render_transitions_t *transitions; /**< State of the transition extract, which lists
                                 *   the lights that transition, NULL without it */
    ecs_vec_t scenes;           /**< box_light_scene_t */
    ecs_map_t lookup;           /**< Scene root plus one to the index of its scene plus one */
    ecs_vec_t free_buffers;     /**< uint32_t, light buffer handles to reuse */
    uint32_t next_buffer;       /**< Next light buffer handle */
    ecs_vec_t free_atlases;     /**< uint32_t, shadow atlas handles to reuse */
    uint32_t next_atlas;        /**< Next shadow atlas handle */
    ecs_vec_t requests;         /**< box_render_shadow_request_t, scratch of the allocation */
    ecs_vec_t places;           /**< box_render_shadow_place_t, scratch of the allocation */
    ecs_vec_t candidates;       /**< int32_t, scratch: the spot of each request */
    ecs_vec_t views;            /**< int32_t, scratch: the views that sample an atlas */
    ecs_vec_t *regions;         /**< box_render_shadow_region_t the shadow casters changed since
                                 *   the last extract, NULL without the draw extract */
    bool uncached;              /**< Every tile renders every frame, see box_render_shadow_cache */
    bool default_sent;          /**< The backend has the buffer of the default sun */
    const void *backend;        /**< Context of the backend the buffers were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;         /**< Generation of that backend, see box_render_backend_t */
    ecs_vec_t cull_views;       /**< box_light_cull_view_t, the views of this frame */
    bool cull;                  /**< Lights are culled in this frame: no reflection probe or
                                 *   planar reflection renders the lights of a scene beyond its
                                 *   views */
    box_render_lights_stats_t stats; /**< Counters of the last extract */
    double time;                /**< Seconds the world advanced over the extracts, in double
                                 *   precision for the offset of the light cookies */
} box_render_lights_t;

/** Find the views the point and spot lights are culled against in this frame, from the views of
 * the packet, and whether lights are culled at all.
 *
 * @param world The world
 * @param l The state
 * @param p The packet, with the views of the frame
 */
void box_lights_cull_views(
    ecs_world_t *world,
    box_render_lights_t *l,
    const box_render_packet_t *p);

/** Check whether no view of a scene can see what a point or spot light lights: its range sphere
 * is outside the frustum of every view of the scene, or smaller than BOX_LIGHTS_CULL_PIXELS. A
 * scene without views or that bakes keeps its lights.
 *
 * @param l The state
 * @param s The scene of the light
 * @param transform World transform of the light
 * @param range Range of the light before the scale of its transform
 * @return Whether the light is culled
 */
bool box_lights_culled(
    box_render_lights_t *l,
    const box_light_scene_t *s,
    const WorldTransform *transform,
    float range);

/** Find the lights of a scene, adding an entry when it has none.
 *
 * @param l The state
 * @param scene The scene root
 * @return The scene, valid until the next entry is added
 */
box_light_scene_t* box_lights_ensure(
    box_render_lights_t *l,
    ecs_entity_t scene);

/** Write the linear color of a light, scaled by its intensity: the irradiance it gives a
 * surface facing it, which a Lambert surface reflects as albedo / pi times that.
 *
 * @param rgb Color of the light, NULL for white
 * @param intensity Irradiance scale
 * @param out Receives the color
 */
void box_lights_color(
    const Rgb *rgb,
    float intensity,
    float *out);

/** Write the cone falloff of a spot light.
 *
 * @param inner Full intensity half angle in degrees
 * @param outer Zero intensity half angle in degrees
 * @param out The light, receives spot_scale and spot_offset
 */
void box_lights_cone(
    float inner,
    float outer,
    box_render_light_t *out);

/** Add a point light to its scene, with its tile when it casts shadows. A light that
 * transitions is added while its target lights nothing, as it renders on its way.
 *
 * @param s The scene
 * @param e The light entity
 * @param transform Its world transform
 * @param rgb Its color, NULL for white
 * @param p The light
 * @param lt Its transition in this frame, NULL for none
 */
void box_lights_point_light(
    box_light_scene_t *s,
    ecs_entity_t e,
    const WorldTransform *transform,
    const Rgb *rgb,
    const PointLight *p,
    const box_render_light_transition_t *lt);

/** Add a spot light to its scene, with its tile when it casts shadows. A light that
 * transitions is added while its target lights nothing, as it renders on its way.
 *
 * @param s The scene
 * @param e The light entity
 * @param transform Its world transform
 * @param rgb Its color, NULL for white
 * @param sp The light
 * @param lt Its transition in this frame, NULL for none
 */
void box_lights_spot_light(
    box_light_scene_t *s,
    ecs_entity_t e,
    const WorldTransform *transform,
    const Rgb *rgb,
    const SpotLight *sp,
    const box_render_light_transition_t *lt);

/** Check whether a light of a list transitions in this frame.
 *
 * @param l The state
 * @param s The list
 * @return Whether the transition extract lists one of its lights
 */
bool box_lights_list_transitions(
    const box_render_lights_t *l,
    const box_light_list_t *s);

/** Send a light record for every light of a light buffer upload that transitions.
 *
 * @param l The state
 * @param s The list
 * @param p The packet
 * @param data Packet data offset of the upload
 */
void box_lights_send_transitions(
    const box_render_lights_t *l,
    const box_light_list_t *s,
    box_render_packet_t *p,
    uint32_t data);

/** Collect the point and spot lights that render their way out after an on_remove: their
 * entity no longer has the light, the transition extract keeps its neutral value.
 *
 * @param world The world
 * @param l The state
 */
void box_lights_collect_leaving(
    ecs_world_t *world,
    box_render_lights_t *l);

#endif
