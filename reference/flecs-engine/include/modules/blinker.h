#ifndef FLECS_ENGINE_BLINKER_H
#define FLECS_ENGINE_BLINKER_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_BLINKER_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsBlinker, {
    float interval;
    float low;
    float high;
});

extern ECS_COMPONENT_DECLARE(FlecsBlinker);

float flecsEngine_blinkerPhase(
    ecs_entity_t entity);

float flecsEngine_blinkerValue(
    const FlecsBlinker *blinker,
    ecs_entity_t entity,
    double time);

#endif
