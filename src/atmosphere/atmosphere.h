/**
 * @file atmosphere/atmosphere.h
 * @brief Private declarations of box.atmosphere: the medium and the CPU transmittance.
 *
 * The coefficients come from etc/shaders/include/atmosphere_medium.h, which the shaders read.
 */

#ifndef BOX_ATMOSPHERE_PRIVATE_H
#define BOX_ATMOSPHERE_PRIVATE_H

#include <box.h>
#include "../../etc/shaders/include/atmosphere_medium.h"

/** The medium of an atmosphere, its coefficients in km^-1 at density 1 */
typedef struct box_atmos_medium_t {
    float bottom;               /**< Radius of the ground in km */
    float top;                  /**< Radius of the top of the atmosphere in km */
    float rayleigh_height;      /**< Rayleigh scale height in km */
    float mie_height;           /**< Mie scale height in km */
    float rayleigh_scattering[3]; /**< Rayleigh scattering, also its extinction */
    float mie_scattering[3];    /**< Mie scattering, tinted */
    float mie_extinction[3];    /**< Mie extinction, tinted */
    float ozone_absorption[3];  /**< Ozone absorption at the peak of its tent */
} box_atmos_medium_t;

/** Check the values of an atmosphere.
 *
 * @param a The atmosphere
 * @return Why it renders nothing, NULL when it is valid
 */
const char* box_atmos_error(
    const Atmosphere *a);

/** Compute the medium of an atmosphere, as the shaders get it.
 *
 * @param a The atmosphere, valid by box_atmos_error
 * @param out Receives the medium
 */
void box_atmos_medium(
    const Atmosphere *a,
    box_atmos_medium_t *out);

/** Extinction of the medium at an altitude: Rayleigh, Mie and the ozone tent.
 *
 * @param m The medium
 * @param altitude Altitude above the ground in km
 * @param out Receives the extinction in km^-1
 */
void box_atmos_extinction(
    const box_atmos_medium_t *m,
    float altitude,
    float *out);

/** Transmittance from an altitude toward the top of the atmosphere, as the transmittance LUT.
 *
 * Integrates the extinction with BOX_ATMOS_TRANSMITTANCE_STEPS midpoint steps; a ray that hits
 * the ground transmits nothing.
 *
 * @param m The medium
 * @param altitude Altitude of the start above the ground in km
 * @param cos_zenith Cosine of the angle between the ray and the up direction
 * @param out Receives the linear rgb transmittance
 */
void box_atmos_transmittance(
    const box_atmos_medium_t *m,
    float altitude,
    float cos_zenith,
    float *out);

/** Fade of a light disk at the horizon, a smoothstep over its angular diameter.
 *
 * @param sin_elevation Sine of the elevation of the center of the disk
 * @param radius Angular radius of the disk in degrees
 * @return 0 for a disk below the horizon to 1 for one above it
 */
float box_atmos_horizon_fade(
    float sin_elevation,
    float radius);

/** Lit fraction of the moon disk seen from the ground.
 *
 * @param sun Normalized direction toward the sun
 * @param moon Normalized direction toward the moon
 * @return 0 for a new moon to 1 for a full moon
 */
float box_atmos_moon_illumination(
    const float *sun,
    const float *moon);

#endif
