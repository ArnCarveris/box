#ifndef FLECS_ENGINE_ASSET_AUDIT_H
#define FLECS_ENGINE_ASSET_AUDIT_H

#define FLECS_ASSET_AUDIT_FLOATING (0)
#define FLECS_ASSET_AUDIT_INVERTED (1)
#define FLECS_ASSET_AUDIT_OVERSHOOT (2)
#define FLECS_ASSET_AUDIT_ROOF (3)

#define FLECS_ASSET_VIEW_3Q (0)
#define FLECS_ASSET_VIEW_FRONT (1)
#define FLECS_ASSET_VIEW_SIDE (2)
#define FLECS_ASSET_VIEW_TOP (3)
#define FLECS_ASSET_VIEW_BOTTOM (4)
#define FLECS_ASSET_VIEW_BACK (5)
#define FLECS_ASSET_VIEW_LEFT (6)
#define FLECS_ASSET_VIEW_RIGHT (7)
#define FLECS_ASSET_VIEW_COUNT (8)

typedef struct {
    float touch_eps;
    float ground_band;
    float min_extent;
    float aspect;
    float overshoot;
    float inverted_ratio;
    float inverted_dot;
    float roof_margin;
    float roof_uncovered;
    int32_t raster_width;
    int32_t max_issues;
} FlecsAssetAuditDesc;

typedef struct {
    ecs_entity_t part;
    int32_t kind;
    float value;
    float point[3];
} FlecsAssetAuditIssue;

typedef struct {
    int32_t parts;
    int32_t triangles;
    int32_t floating;
    int32_t inverted;
    int32_t overshoot;
    int32_t roof;
    int32_t backface_px;
    int32_t covered_px;
    float aabb_min[3];
    float aabb_max[3];
    int32_t issue_count;
    FlecsAssetAuditIssue *issues;
    ecs_entity_t root;
} FlecsAssetAuditResult;

void flecsEngine_assetAudit_defaults(
    FlecsAssetAuditDesc *desc);

bool flecsEngine_assetAudit_bounds(
    ecs_world_t *world,
    ecs_entity_t root,
    float *aabb_min_out,
    float *aabb_max_out,
    int32_t *parts_out,
    int32_t *triangles_out);

bool flecsEngine_assetAudit_run(
    ecs_world_t *world,
    ecs_entity_t root,
    const FlecsAssetAuditDesc *desc,
    FlecsAssetAuditResult *out);

void flecsEngine_assetAudit_fini(
    FlecsAssetAuditResult *result);

void flecsEngine_assetAudit_print(
    const ecs_world_t *world,
    const FlecsAssetAuditResult *result,
    const char *label,
    int32_t limit);

const char* flecsEngine_assetAudit_kindStr(
    int32_t kind);

void flecsEngine_assetAudit_frameView(
    const float *aabb_min,
    const float *aabb_max,
    int32_t view,
    float fov,
    float aspect,
    float *eye_out,
    float *target_out);

void flecsEngine_assetAudit_frameOrtho(
    const float *aabb_min,
    const float *aabb_max,
    int32_t view,
    float aspect,
    float margin,
    float *eye_out,
    float *target_out,
    float *ortho_height_out,
    float *near_out,
    float *far_out);

bool flecsEngine_assetAudit_wireRaster(
    ecs_world_t *world,
    ecs_entity_t root,
    const float *eye,
    const float *target,
    float fov,
    float ortho_height,
    int32_t width,
    int32_t height,
    uint8_t **rgb_out);

bool flecsEngine_assetAudit_backfaceRaster(
    ecs_world_t *world,
    ecs_entity_t root,
    const float *eye,
    const float *target,
    float fov,
    int32_t width,
    int32_t height,
    uint8_t **rgb_out,
    int32_t *backface_px_out,
    int32_t *covered_px_out);

#endif
