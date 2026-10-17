/**
 * @file atmosphere_medium.h
 * @brief Coefficients of the atmosphere medium, read by the C transmittance and the shaders.
 *
 * Plain #define lines only, so the WGSL preprocessor and a C compiler both include this file.
 * Coefficients are per km at density 1 for the scales of 1, after Hillaire 2020.
 */

#pragma once

/** Rayleigh scattering of the air, which is also its extinction */
#define BOX_ATMOS_RAYLEIGH_R 0.005802
#define BOX_ATMOS_RAYLEIGH_G 0.013558
#define BOX_ATMOS_RAYLEIGH_B 0.033100

/** Mie scattering of the aerosols, the same for every channel before the tint */
#define BOX_ATMOS_MIE_SCATTERING 0.003996

/** Mie extinction of the aerosols, scattering plus absorption */
#define BOX_ATMOS_MIE_EXTINCTION 0.004400

/** Ozone absorption */
#define BOX_ATMOS_OZONE_R 0.000650
#define BOX_ATMOS_OZONE_G 0.001881
#define BOX_ATMOS_OZONE_B 0.000085

/** Altitude in km of the peak of the ozone density tent */
#define BOX_ATMOS_OZONE_CENTER 25.0

/** Half width in km of the ozone density tent */
#define BOX_ATMOS_OZONE_WIDTH 15.0

/** Altitude in km a ray starts at least above the ground */
#define BOX_ATMOS_PLANET_OFFSET 0.001

/** Midpoint steps of a transmittance integration */
#define BOX_ATMOS_TRANSMITTANCE_STEPS 40
