#ifndef FLECS_ENGINE_CURVES_H
#define FLECS_ENGINE_CURVES_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_CURVES_IMPL
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

ECS_STRUCT(FlecsBezierCurve, {
    float y0;
    float x1;
    float y1;
    float x2;
    float y2;
    float y3;
});

extern ECS_COMPONENT_DECLARE(FlecsBezierCurve);

float flecsEngine_bezierEval(
    const FlecsBezierCurve *curve,
    float t);

float flecsEngine_curveEval(
    const ecs_world_t *world,
    ecs_entity_t curve,
    float t,
    float fallback);

void FlecsEngineCurvesImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
