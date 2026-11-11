/**
 * @file geometry/paths.h
 * @brief Sampling and evaluation of 2D and 3D path components.
 */

#ifndef BOX_GEOMETRY_PATHS_H
#define BOX_GEOMETRY_PATHS_H

#include "mesh_cache.h"

#define BOX_PATH_STEPS 16
#define BOX_PATH_MAX_STEPS 64

bool box_path2_sample(const ecs_world_t *world, ecs_entity_t entity, ecs_vec_t *points);
bool box_path3_sample(const ecs_world_t *world, ecs_entity_t entity, ecs_vec_t *points);
bool box_path2_sample_steps(const ecs_world_t *world, ecs_entity_t entity, int steps,
    ecs_vec_t *points);
bool box_path3_sample_steps(const ecs_world_t *world, ecs_entity_t entity, int steps,
    ecs_vec_t *points);
int box_path_steps(int32_t value);
bool box_path2_evaluate(const Path2D *path, float t, vec2 point);
bool box_path3_evaluate(const Path3D *path, float t, vec3 point);

/* The segments of a valid path, including the closing segment that a closed path whose last
 * segment does not end at the start gets. The closing segment is Catmull-Rom when the last
 * segment is, otherwise a line. */
int box_path2_count(const Path2D *path);
int box_path3_count(const Path3D *path);

/* Resolve segment index, 0 to the count - 1, of a valid path to the four points of its cubic
 * Bezier and return its kind. Evaluation, sampling and surface packing all go through this. A
 * Catmull-Rom segment takes its neighbours from the adjacent anchors, the endpoint itself at the
 * ends of an open path and wrapping around a closed one. A 2D path leaves z at 0. */
PathSegmentKind box_path2_cubic(const Path2D *path, int index, vec3 out[4]);
PathSegmentKind box_path3_cubic(const Path3D *path, int index, vec3 out[4]);
void box_geometry_paths_init(ecs_world_t *world);

#endif
