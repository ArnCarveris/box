#ifndef BOX_GEOMETRY_PATHS_H
#define BOX_GEOMETRY_PATHS_H

#include "mesh_cache.h"

bool box_path2_sample(const ecs_world_t *world, ecs_entity_t entity, ecs_vec_t *points);
bool box_path3_sample(const ecs_world_t *world, ecs_entity_t entity, ecs_vec_t *points);
bool box_path2_evaluate(const Path2D *path, float t, vec2 point);
bool box_path3_evaluate(const Path3D *path, float t, vec3 point);
void box_path_cubic_evaluate(const vec3 start, const vec3 control1, const vec3 control2,
    const vec3 end, float t, vec3 out);
void box_geometry_paths_init(ecs_world_t *world);

#endif
