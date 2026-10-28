#ifndef FLECS_ENGINE_TILE_STREAMER_H
#define FLECS_ENGINE_TILE_STREAMER_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_TILE_STREAMER_IMPL
#define ECS_META_IMPL EXTERN
#endif
typedef struct {
    ecs_entity_t tmpl;
    float x, y, z;
    float yaw;
    float extent;
    uint64_t seed;
    const void *props;
    ecs_size_t props_size;
} FlecsTileSpawn;

ECS_STRUCT(FlecsTileGrid, {
    float tile_size;
    float radius;
    float hysteresis;     /* drop past radius * this; < 1 means 1.4 */
    int32_t spawn_budget;
    bool all_resident;
    float always_extent;
    float radius_per_extent;
    float radius_min;
    float radius_max;
});
ECS_STRUCT(FlecsTileFocus, {
    float x;
    float z;
});

ECS_STRUCT(FlecsTileStats, {
    int32_t tiles;
    int32_t resident;
    int32_t records;
    int32_t live;
    int32_t pending;
    int32_t spawned_frame;
    int32_t always;
});
float flecsEngine_tileStreamer_radiusFor(
    const FlecsTileGrid *grid,
    float extent);
extern ECS_TAG_DECLARE(FlecsTileResident);

void FlecsEngineTile_streamerImport(
    ecs_world_t *world);
ecs_entity_t flecsEngine_tileStreamer_create(
    ecs_world_t *world,
    const char *name);
ecs_entity_t flecsEngine_tileStreamer_record(
    ecs_world_t *world,
    ecs_entity_t streamer,
    const FlecsTileSpawn *spawn);
ecs_entity_t flecsEngine_tileStreamer_tileAt(
    ecs_world_t *world,
    ecs_entity_t streamer,
    float x,
    float z);
ecs_entity_t flecsEngine_tileStreamer_spawn(
    ecs_world_t *world,
    ecs_entity_t parent,
    const FlecsTileSpawn *spawn);
int32_t flecsEngine_tileStreamer_fill(
    ecs_world_t *world,
    ecs_entity_t streamer,
    float x,
    float z,
    float radius);
int32_t flecsEngine_tileStreamer_instantiate(
    ecs_world_t *world,
    ecs_entity_t tile);
void flecsEngine_tileStreamer_evict(
    ecs_world_t *world,
    ecs_entity_t tile);

#endif
