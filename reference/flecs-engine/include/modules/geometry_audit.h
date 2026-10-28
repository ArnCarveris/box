#ifndef FLECS_ENGINE_GEOMETRY_AUDIT_H
#define FLECS_ENGINE_GEOMETRY_AUDIT_H

#define FLECS_GEOMETRY_AUDIT_COPLANAR (0)
#define FLECS_GEOMETRY_AUDIT_NEAR (1)
#define FLECS_GEOMETRY_AUDIT_FLUSH_EDGE (2)

typedef struct {
    ecs_entity_t part_a;
    ecs_entity_t part_b;
    float normal[3];
    float plane_d;
    float offset;
    float area;
    float point[3];
    int32_t kind;
} FlecsGeometryAuditPair;

typedef struct {
    float eps;
    float coplanar_eps;
    float min_area;
    float angle_deg;
    int32_t max_pairs;
    bool flush_edges;
} FlecsGeometryAuditDesc;

typedef struct {
    int32_t parts;
    int32_t triangles;
    int32_t faces;
    int32_t coplanar;
    int32_t near;
    int32_t flush_edge;
    float coplanar_area;
    int32_t pair_count;
    FlecsGeometryAuditPair *pairs;
    ecs_entity_t root;
} FlecsGeometryAuditResult;

void flecsEngine_geometryAudit_defaults(
    FlecsGeometryAuditDesc *desc);

bool flecsEngine_geometryAudit_run(
    ecs_world_t *world,
    ecs_entity_t root,
    const FlecsGeometryAuditDesc *desc,
    FlecsGeometryAuditResult *out);

void flecsEngine_geometryAudit_fini(
    FlecsGeometryAuditResult *result);

void flecsEngine_geometryAudit_print(
    const ecs_world_t *world,
    const FlecsGeometryAuditResult *result,
    const char *label,
    int32_t limit);

const char* flecsEngine_geometryAudit_kindStr(
    int32_t kind);

void FlecsEngineGeometry_auditImport(
    ecs_world_t *world);

#endif
