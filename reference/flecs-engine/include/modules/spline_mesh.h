#ifndef FLECS_ENGINE_SPLINE_MESH_H
#define FLECS_ENGINE_SPLINE_MESH_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_SPLINE_MESH_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsSplineProfile, {
    ecs_vec_t profile;         int32_t slices;            bool cap_ends;
    float uv_scale;        });

ECS_STRUCT(FlecsSplineMesh, {
    ecs_entity_t profile;
    flecs_vec3_t p0;
    flecs_vec3_t t0;
    flecs_vec3_t p1;
    flecs_vec3_t t1;
    flecs_vec3_t up0;
    flecs_vec3_t up1;
    float roll0;               float roll1;
    float length;              float v_scale;         });

ECS_STRUCT(FlecsSplineMeshConfig, {
    int32_t slices;        });

void flecsEngine_splineMesh_evaluate(
    const FlecsSplineMesh *sm,
    float t,
    vec3 out_pos,
    mat3 out_frame);

float flecsEngine_splineMesh_length(
    const FlecsSplineMesh *sm);

void flecsEngine_splineMesh_aabb(
    const FlecsSplineMesh *sm,
    const FlecsAABB *profile_aabb,
    FlecsAABB *out);

void flecsEngine_splineMesh_worldAabb(
    const ecs_world_t *world,
    const FlecsSplineMesh *sm,
    const FlecsWorldTransform3 *wt,
    FlecsAABB *out);

bool flecsEngine_splineMesh_soup(
    const ecs_world_t *world,
    const FlecsSplineMesh *sm,
    FlecsMesh3 *out_mesh);

void FlecsEngineSpline_meshImport(
    ecs_world_t *world);

#endif
