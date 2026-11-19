/**
 * @file froxel.h
 * @brief Size and slicing of the froxel grid of the fog volumes of a view, read by the C backend
 * and the shaders.
 *
 * Plain #define lines only, so the WGSL preprocessor and a C compiler both include this file.
 * The grid has one cell per tile of the render size and BOX_FROXEL_SLICES depth slices from the
 * near plane to LightSettings.fog_distance beyond it; froxel.wgsl maps depths to slices.
 */

#pragma once

/** Depth slices of the grid */
#define BOX_FROXEL_SLICES 64

/** Edge of the tile of a cell in pixels at least */
#define BOX_FROXEL_TILE_MIN 8

/** Cells along the larger edge of the render size at most: a larger image takes larger tiles */
#define BOX_FROXEL_CELLS_MAX 240

/** Exponent of the slice distribution of a perspective camera: the share x of the depth range
 * of slice share t is (2^(E t) - 1) / (2^E - 1), so slices thicken exponentially from the near
 * plane, without spending most of them on the first meter like a pure logarithmic split */
#define BOX_FROXEL_EXPONENT 8.0

/** Points along the depth of a cell, evenly from its near to its far side, at which the light
 * injection takes the share of each volume that covers the cell and the shadow of the sun */
#define BOX_FROXEL_SAMPLES 4

/** Edge of the workgroup of the light injection, which runs one thread per cell */
#define BOX_FROXEL_INJECT_GROUP 4

/** Edge of the workgroup of the integration, which runs one thread per column of cells */
#define BOX_FROXEL_INTEGRATE_GROUP 8

/** LightSettings.fog_distance of a view that leaves it 0 */
#define BOX_FROXEL_DISTANCE_DEFAULT 100.0

/** LightSettings.fog_distance at least */
#define BOX_FROXEL_DISTANCE_MIN 1.0
