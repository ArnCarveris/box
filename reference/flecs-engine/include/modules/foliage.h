#ifndef FLECS_ENGINE_FOLIAGE_H
#define FLECS_ENGINE_FOLIAGE_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_FOLIAGE_IMPL
#define ECS_META_IMPL EXTERN
#endif


ECS_ENUM(FlecsTreeSpecies, {
    FlecsTreePlane,     /* London plane: broad, mottled bark, palmate leaves */
    FlecsTreeGinkgo,    /* upright, open, fan leaves */
    FlecsTreePine,      /* conifer: single leader, whorls, needle clusters */
    FlecsTreePalm,      /* bare curved stem, crown of fronds */
    FlecsTreePoplar,    /* Lombardy: tall, narrow, near-vertical branches */
    FlecsTreeShrub,     /* no trunk, dense low mass */
    FlecsTreeHedge      /* cards packed into a width x depth footprint */
});

extern ECS_COMPONENT_DECLARE(FlecsTreeSpecies);

ECS_STRUCT(FlecsTree, {
    FlecsTreeSpecies species;
    float height;         /* metres, default 8 (0.9 for shrub/hedge) */
    float trunk_radius;   /* metres at the base, default height * 0.035 */
    int32_t seed;         /* shape variant, folded to 32 values */
    int32_t levels;       /* branch generations, default 3, max 5 */
    float spread;         /* 1 = species default; < 1 narrow, > 1 wide */
    float leaf_density;   /* 1 = species default card count */
    float leaf_size;      /* 1 = species default card size */
    float bare;           /* 0 full canopy .. 1 leafless */
    float wind;           /* sway amplitude as a fraction of height */
    float width;          /* Hedge footprint along x, metres */
    float depth;          /* Hedge footprint along z, metres */
});

extern ECS_COMPONENT_DECLARE(FlecsTree);

ECS_STRUCT(FlecsFoliageLodConfig, {
    float lod1_dist;      /* default 60 */
    float lod2_dist;      /* default 150 */
    float cull_dist;      /* 0 = never, default 0 */
    float hysteresis;     /* default 12 */
    float step;           /* camera movement that triggers a pass, default 8 */
});

extern ECS_COMPONENT_DECLARE(FlecsFoliageLodConfig);

extern ECS_TAG_DECLARE(FlecsFoliageNoLod);

ECS_STRUCT(FlecsTreeLod, {
    int32_t level;
    ecs_entity_t asset;
});

extern ECS_COMPONENT_DECLARE(FlecsTreeLod);

int32_t flecsEngine_treeTriangleCount(
    const ecs_world_t *world,
    ecs_entity_t tree);

int32_t flecsEngine_treeCardCount(
    const ecs_world_t *world,
    ecs_entity_t tree);

void FlecsEngineFoliageImport(
    ecs_world_t *world);

#endif
