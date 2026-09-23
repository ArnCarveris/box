/**
 * @file sky/sky_internal.h
 * @brief Private state of the box.sky module: the shaders and the table invalidation.
 */

#ifndef BOX_SKY_INTERNAL_H
#define BOX_SKY_INTERNAL_H

#include <box.h>
#include <box/sky.h>
#include "../shader/shader_internal.h"

/** Kilometers the eye is lifted over the planet, the lift of the shaders */
#define BOX_SKY_LIFT (0.001f)

/** Direction and radiance of one body the sky holds
 *
 * The direction points from the camera toward the body, the radiance is the
 * one the body has over the atmosphere and the light is what the medium leaves
 * of it, which is what lights the geometry of a view.
 */
typedef struct box_body_t {
    float dir[3];   /**< Direction from the camera toward the body */
    float toa[3];   /**< Radiance of the body over the medium */
    float light[3]; /**< That radiance after the medium absorbed it */
    bool present;   /**< The world names the body and it holds a transform */
} box_body_t;

/** Blend state of a Weather, added next to it
 *
 * Holds the amounts a frame is drawn with so that a command writing a new target to
 * the component does not lose the current weather. The system copies them into
 * `Weather.current` every frame; a new weather starts at its authored amounts.
 */
typedef struct WeatherBlend {
    WeatherAmounts amounts; /**< Amounts the frame is drawn with */
    bool started;           /**< The amounts were blended at least once */
} WeatherBlend;

extern ECS_COMPONENT_DECLARE(WeatherBlend);

/** Module wide registry of the atmosphere, stored as a singleton */
typedef struct SkyState {
    ecs_query_t *atmospheres; /**< Entities holding an Atmosphere component */
    ecs_query_t *weathers;    /**< Entities holding a Weather component */
    ecs_query_t *cameras;     /**< Entities holding a Camera, whose first one the rain follows */
    ecs_entity_t rain;        /**< Emitter of the rain, created the first time a world rains */
    ecs_entity_t lut_shader;  /**< Shader entity of the table passes */
    ecs_entity_t sky_shader;  /**< Shader entity of the sky and haze pass */
    box_sky_t last;           /**< Parameter block the sky view table was rendered from */
    box_sky_t env_last;       /**< Parameter block the environment was filtered from */
    box_body_t sun;           /**< Sun of the frame, resolved while the views are filled */
    box_body_t moon;          /**< Moon of the frame, resolved with it */
    float phase;              /**< Lit fraction of the moon disk, from the two directions */
    float fade;               /**< Part of the star field the daylight leaves */
    float altitude;           /**< Altitude in kilometers that table was rendered for */
    float env_altitude;       /**< Altitude the environment of a view was filtered for */
    uint32_t generation;      /**< Rises every time that table is rendered again */
    uint32_t env_generation;  /**< Rises every time the environment is filtered again */
    bool lit;                 /**< The bodies of the frame were resolved */
    bool valid;               /**< A packet with an atmosphere was produced before */
} SkyState;

extern ECS_COMPONENT_DECLARE(SkyState);

/** Find the atmosphere of a world.
 *
 * @param world The world
 * @return The first entity holding an Atmosphere, 0 when the world has none
 */
ecs_entity_t box_sky_entity(
    ecs_world_t *world);

/** Read the weather amounts a frame of a world is drawn with.
 *
 * @param world The world
 * @param out The amounts to fill, left clear when the world has no weather
 * @return true when the world holds a Weather component
 */
bool box_sky_weather(
    ecs_world_t *world,
    WeatherAmounts *out);

/** Register the systems that blend the weather of a world.
 *
 * @param world The world
 */
void box_sky_weather_init(
    ecs_world_t *world);

/** Register the system that drives the rain emitter of a world.
 *
 * @param world The world
 */
void box_sky_rain_init(
    ecs_world_t *world);

/** Take a value or its default when it is not above zero.
 *
 * @param value The value
 * @param fallback The default
 * @return The value, or the default
 */
float box_sky_or(
    float value,
    float fallback);

/** Convert a display encoded color to the linear color the medium scatters.
 *
 * @param color The color
 * @param fallback Channel every component takes when the color is black
 * @param out The three floats to fill
 */
void box_sky_color(
    rgb color,
    uint8_t fallback,
    float *out);

/** Write the planet, the layer heights and every coefficient of the medium.
 *
 * @param a The atmosphere
 * @param sky The parameter block to fill
 */
void box_sky_medium(
    const Atmosphere *a,
    box_sky_t *sky);

/** Altitude of a point over the planet, in kilometers.
 *
 * @param a The atmosphere
 * @param y World height of the point
 * @return The altitude
 */
float box_sky_altitude_of(
    const Atmosphere *a,
    float y);

/** Fraction of the radiance of a body that survives the medium.
 *
 * The march is the transmittance table of the shaders in the units the medium
 * is given in, which is what keeps the light on the geometry and the disk in
 * the sky the same color.
 *
 * @param sky The parameter block holding the medium
 * @param r Radius the ray starts at, in kilometers from the planet center
 * @param mu Cosine between the ray and the zenith
 * @param out The three channels to fill
 */
void box_sky_transmittance(
    const box_sky_t *sky,
    float r,
    float mu,
    float *out);

/** Resolve the sun and the moon of the atmosphere and light a view with one.
 *
 * The view already holds the first directional light of the world, used when the
 * atmosphere names no sun. The brightest body at the camera becomes the view light,
 * with the transmittance of the air applied to its color.
 *
 * @param world The world
 * @param v The view to light
 * @return true when the world holds an atmosphere
 */
bool box_sky_celestial(
    ecs_world_t *world,
    box_view_t *v);

/** Copy the moon and the star field of the atmosphere into a packet.
 *
 * @param world The world
 * @param a The atmosphere
 * @param p The packet
 * @param sky The parameter block to fill
 */
void box_sky_bodies(
    ecs_world_t *world,
    const Atmosphere *a,
    const box_packet_t *p,
    box_sky_t *sky);

/** Register the system that advances the clock of a world.
 *
 * @param world The world
 */
void box_sky_time_init(
    ecs_world_t *world);

/** Copy the atmosphere of a world into a packet and mark what must be rendered.
 *
 * The views are already in the packet, so the sky view table follows the camera
 * of the first one and the environment generation of every view that takes its
 * image based lighting from the sky is mixed with the generation of that table.
 *
 * @param world The world
 * @param p The packet
 */
void box_sky_extract(
    ecs_world_t *world,
    box_packet_t *p);

#endif
