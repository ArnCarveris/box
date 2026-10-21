/**
 * @file box/spatial.h
 * @brief Spatial index per scene over the world bounds of shapes.
 *
 * Static shapes are placed in a bounding volume hierarchy once and again on change, shapes
 * with DynamicTransform every frame; ray casts and overlap queries are scoped to one scene.
 */

#ifndef BOX_SPATIAL_H
#define BOX_SPATIAL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SpatialAxis {
    SpatialX = 1, SpatialY = 2, SpatialZ = 4,
    SpatialXY = 3, SpatialXZ = 5, SpatialYZ = 6, SpatialXYZ = 7
} SpatialAxis;

typedef enum FitMode { SpatialStretch, SpatialContain, SpatialCover } FitMode;
typedef enum AnchorPoint { SpatialCenter, SpatialMin, SpatialMax } AnchorPoint;
typedef enum AnchorFace {
    SpatialNoFace, SpatialFront, SpatialBack, SpatialTop,
    SpatialBottom, SpatialLeft, SpatialRight
} AnchorFace;
typedef enum AlignRotation { SpatialKeep, SpatialMatch } AlignRotation;
typedef enum DistributionMode {
    SpatialSpaceBetween, SpatialSpaceAround, SpatialSpaceEvenly, SpatialFixedGap
} DistributionMode;
typedef enum SpatialError {
    SpatialOk, SpatialInvalidTarget, SpatialMissingBounds, SpatialZeroBounds,
    SpatialCycle, SpatialImpossible, SpatialInvalidAnchor, SpatialConflictingLayout
} SpatialError;

typedef struct SpatialAnchor {
    AnchorPoint x, y, z;
    AnchorFace face;
    ecs_entity_t frame;
} SpatialAnchor;

typedef struct Fit {
    ecs_entity_t target;
    SpatialAxis axes;
    vec3 padding;
    FitMode mode;
} Fit;

typedef struct SpatialAlign {
    ecs_entity_t target;
    SpatialAnchor self, other;
    AlignRotation rotation;
    vec3 offset;
    vec3 rotation_offset;
} SpatialAlign;

typedef struct Array {
    SpatialAxis axis;
    int32_t count;
    float spacing;
    vec3 rotation_step;
    vec3 scale_step;
} Array;

typedef struct Distribute {
    SpatialAxis axis;
    float span;
    DistributionMode mode;
    float gap;
} Distribute;

typedef struct AttachmentFrame {
    vec3 position;
    vec3 rotation;
} AttachmentFrame;

typedef struct IntrinsicBounds { vec3 min, max; } IntrinsicBounds;
typedef struct ResolvedBounds { vec3 min, max; } ResolvedBounds;
typedef struct ResolvedBox { float x, y, z; } ResolvedBox;
typedef struct SpatialDiagnostic {
    SpatialError code;
    ecs_entity_t related;
} SpatialDiagnostic;

typedef struct ArrayInstance {
    ecs_entity_t owner;
    ecs_entity_t prototype;
    int32_t index;
} ArrayInstance;

extern ECS_COMPONENT_DECLARE(SpatialAxis);
extern ECS_COMPONENT_DECLARE(FitMode);
extern ECS_COMPONENT_DECLARE(AnchorPoint);
extern ECS_COMPONENT_DECLARE(AnchorFace);
extern ECS_COMPONENT_DECLARE(AlignRotation);
extern ECS_COMPONENT_DECLARE(DistributionMode);
extern ECS_COMPONENT_DECLARE(SpatialError);
extern ECS_COMPONENT_DECLARE(SpatialAnchor);
extern ECS_COMPONENT_DECLARE(Fit);
extern ECS_COMPONENT_DECLARE(SpatialAlign);
extern ECS_COMPONENT_DECLARE(Array);
extern ECS_COMPONENT_DECLARE(Distribute);
extern ECS_COMPONENT_DECLARE(AttachmentFrame);
extern ECS_COMPONENT_DECLARE(IntrinsicBounds);
extern ECS_COMPONENT_DECLARE(ResolvedBounds);
extern ECS_COMPONENT_DECLARE(ResolvedBox);
extern ECS_COMPONENT_DECLARE(SpatialDiagnostic);
extern ECS_COMPONENT_DECLARE(ArrayInstance);

bool box_spatial_layout_update(ecs_world_t *world);

/** Register the box.spatial module.
 *
 * @param world The world
 */
void BoxSpatialImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
