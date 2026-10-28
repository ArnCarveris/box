#ifndef FLECS_ENGINE_RENDER_GRAPH_H
#define FLECS_ENGINE_RENDER_GRAPH_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_RENDER_GRAPH_IMPL
#define ECS_META_IMPL EXTERN
#endif


ECS_ENUM(FlecsRenderResourceKind, {
    FlecsResourceColor,
    FlecsResourceDepth,
    FlecsResourceGBuffer,
    FlecsResourceHiZ,
    FlecsResourceLuma,
    FlecsResourceExposure,
    FlecsResourceCustom
});

ECS_ENUM(FlecsRenderResourceFormat, {
    FlecsResourceFormatHdr,
    FlecsResourceFormatSurface,
    FlecsResourceFormatDepth,
    FlecsResourceFormatR32F,
    FlecsResourceFormatRgba8
});

ECS_STRUCT(FlecsRenderResource, {
    FlecsRenderResourceKind kind;
    FlecsRenderResourceFormat format;
    ecs_f32_t scale;

    ecs_bool_t transient;
});

extern ECS_COMPONENT_DECLARE(FlecsRenderResource);

ECS_STRUCT(FlecsRenderGraph, {
    ecs_entity_t output;
});

extern ECS_COMPONENT_DECLARE(FlecsRenderGraph);

extern ECS_TAG_DECLARE(FlecsReads);
extern ECS_TAG_DECLARE(FlecsWrites);
extern ECS_TAG_DECLARE(FlecsAfter);
extern ECS_TAG_DECLARE(FlecsRenderPass);

ECS_STRUCT(flecs_render_pass_t, {
    ecs_entity_t effect;
    ecs_bool_t enabled;
    ecs_i32_t input;
    ecs_i32_t output;
    ecs_entity_t reads;
    ecs_entity_t writes;
});

ECS_STRUCT(FlecsRenderPasses, {
    ecs_entity_t graph;
    ecs_i32_t pass_count;
    ecs_i32_t enabled_count;
    ecs_i32_t target_count;
    ecs_i32_t compile_count;
    ecs_bool_t valid;
    ecs_bool_t derived;
    ecs_bool_t lowered;
    ecs_vec_t passes;
});

extern ECS_COMPONENT_DECLARE(FlecsRenderPasses);

#define FLECS_ENGINE_RES_SCENE_COLOR "flecs.engine.resources.scene_color"
#define FLECS_ENGINE_RES_SCENE_DEPTH "flecs.engine.resources.scene_depth"
#define FLECS_ENGINE_RES_OPAQUE_COLOR "flecs.engine.resources.opaque_color"
#define FLECS_ENGINE_RES_OPAQUE_DEPTH "flecs.engine.resources.opaque_depth"
#define FLECS_ENGINE_RES_GBUFFER "flecs.engine.resources.gbuffer"
#define FLECS_ENGINE_RES_HIZ "flecs.engine.resources.hiz"
#define FLECS_ENGINE_RES_ATMOSPHERE_LUT "flecs.engine.resources.atmosphere_lut"
#define FLECS_ENGINE_RES_SHADOW_MAP "flecs.engine.resources.shadow_map"
#define FLECS_ENGINE_RES_EXPOSURE "flecs.engine.resources.exposure"
#define FLECS_ENGINE_RES_LUMA "flecs.engine.resources.luma"
#define FLECS_ENGINE_RES_OUTPUT "flecs.engine.resources.output"

ecs_entity_t flecsEngine_renderResource(
    const ecs_world_t *world,
    const char *name);

void flecsEngine_renderGraph_defaultReads(
    ecs_world_t *world,
    ecs_id_t component,
    const char *resource);

void flecsEngine_renderGraph_defaultWrites(
    ecs_world_t *world,
    ecs_id_t component,
    const char *resource);

void flecsEngine_renderGraph_defaults(
    ecs_world_t *world,
    ecs_id_t component,
    const char **reads,
    const char **writes);

bool flecsEngine_renderGraph_passReads(
    const ecs_world_t *world,
    ecs_entity_t effect,
    ecs_entity_t resource);

const FlecsRenderPasses* flecsEngine_renderGraph_compile(
    ecs_world_t *world,
    ecs_entity_t view);

#endif
