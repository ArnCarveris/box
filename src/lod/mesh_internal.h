/**
 * @file lod/mesh_internal.h
 * @brief Private helpers the mesh merge shares with its simplification.
 *
 * The merge owns the vectors of the builder and the simplification rewrites
 * them, so both need the same two services: a scratch buffer that outlives one
 * call and the bounds of the mesh the vectors currently hold.
 */

#ifndef BOX_LOD_MESH_INTERNAL_H
#define BOX_LOD_MESH_INTERNAL_H

#include <box.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>

/** Entries the weld table of a merge starts at, a power of two */
#define BOX_LOD_TABLE_MIN (64)

/** Hand out the scratch buffer of a builder, grown to a size.
 *
 * @param builder The builder
 * @param size Bytes the caller writes in the buffer
 * @return The buffer, valid until the next call
 */
void* box_lod_mesh_scratch(
    box_lod_mesh_builder_t *builder,
    int32_t size);

/** Recompute the world bounds of the mesh a builder holds.
 *
 * @param builder The builder
 */
void box_lod_mesh_bounds(
    box_lod_mesh_builder_t *builder);

/** Describe the mesh a builder holds.
 *
 * @param builder The builder
 * @param error Error of the mesh in world units
 * @param out Receives the mesh, ignored when it is NULL
 * @return True when the mesh has triangles
 */
bool box_lod_mesh_result(
    box_lod_mesh_builder_t *builder,
    float error,
    box_lod_mesh_t *out);

#endif
