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

/** Points along the slice of the depth of a pixel at which the apply draw finds where the density
 * of the volumes that partly cover the slice lies along the ray of the pixel */
#define BOX_FROXEL_APPLY_SAMPLES 8

/** Weight of the previous frame when the light injection blends a cell with the previous cells
 * reprojected to it, so it averages a jittered point per frame. Higher is smoother but lags
 * changing volumes and wind longer, BLEND^n of an old value is left after n frames, and trails a
 * turning camera further: at 0.85 a still view flickers about a quarter less than at 0.8, while
 * 0.9 leaves a shaft edge nearly twice as far behind a turn (FogVolume.history_camera_turns).
 * Cells no volume reaches take no history, so a removed volume leaves nothing behind at all */
#define BOX_FROXEL_HISTORY_BLEND 0.85

/** Edge of the workgroup of the light injection, which runs one thread per cell */
#define BOX_FROXEL_INJECT_GROUP 4

/** Edge of the workgroup of the integration, which runs one thread per column of cells */
#define BOX_FROXEL_INTEGRATE_GROUP 8

/** LightSettings.fog_distance of a view that leaves it 0 */
#define BOX_FROXEL_DISTANCE_DEFAULT 100.0

/** LightSettings.fog_distance at least */
#define BOX_FROXEL_DISTANCE_MIN 1.0
