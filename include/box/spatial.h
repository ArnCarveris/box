/**
 * @file box/spatial.h
 * @brief Spatial index per scene over the world bounds of shapes.
 *
 * Static shapes are placed in a bounding volume hierarchy once and again on change, shapes
 * with DynamicTransform every frame; ray casts and overlap queries are scoped to one scene.
 * Prefabs and SpatialIgnore subtrees are not indexed; queries skip disabled entities.
 */

#ifndef BOX_SPATIAL_H
#define BOX_SPATIAL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Axes a layout rule applies to, as a bit set */
typedef enum SpatialAxis {
    SpatialX = 1,   /**< The X axis */
    SpatialY = 2,   /**< The Y axis */
    SpatialZ = 4,   /**< The Z axis */
    SpatialXY = 3,  /**< The X and Y axes */
    SpatialXZ = 5,  /**< The X and Z axes */
    SpatialYZ = 6,  /**< The Y and Z axes */
    SpatialXYZ = 7  /**< All axes */
} SpatialAxis;

/** How a Fit scales an entity to the bounds of its target */
typedef enum FitMode {
    SpatialStretch, /**< Scale every axis to fill the target */
    SpatialContain, /**< Scale uniformly so that every axis fits inside the target */
    SpatialCover    /**< Scale uniformly so that every axis covers the target */
} FitMode;

/** Point on an axis of the bounds of an entity */
typedef enum AnchorPoint {
    SpatialCenter, /**< Center of the bounds */
    SpatialMin,    /**< Minimum of the bounds */
    SpatialMax     /**< Maximum of the bounds */
} AnchorPoint;

/** Face of the bounds of an entity, which also orients the anchor */
typedef enum AnchorFace {
    SpatialNoFace, /**< No face */
    SpatialFront,  /**< Positive Z face */
    SpatialBack,   /**< Negative Z face */
    SpatialTop,    /**< Positive Y face */
    SpatialBottom, /**< Negative Y face */
    SpatialLeft,   /**< Negative X face */
    SpatialRight   /**< Positive X face */
} AnchorFace;

/** Rotation of an entity aligned by SpatialAlign */
typedef enum AlignRotation {
    SpatialKeep, /**< Keep the rotation of the entity */
    SpatialMatch /**< Match the rotation of the anchor of the target */
} AlignRotation;

/** How Distribute spaces the children of an entity */
typedef enum DistributionMode {
    SpatialSpaceBetween, /**< Equal gaps between children, none at the ends */
    SpatialSpaceAround,  /**< Equal space around every child, half gaps at the ends */
    SpatialSpaceEvenly,  /**< Equal gaps between children and at the ends */
    SpatialFixedGap      /**< Children separated by Distribute::gap, centered */
} DistributionMode;

/** Error of a layout rule, reported with SpatialDiagnostic */
typedef enum SpatialError {
    SpatialOk,               /**< No error */
    SpatialInvalidTarget,    /**< The target is not an alive entity */
    SpatialMissingBounds,    /**< An entity has no geometry bounds */
    SpatialZeroBounds,       /**< Bounds or a transform are zero-sized or singular */
    SpatialCycle,            /**< Layout rules depend on each other */
    SpatialImpossible,       /**< The rule has invalid values or cannot be satisfied */
    SpatialInvalidAnchor,    /**< The anchor has invalid values or an invalid frame */
    SpatialConflictingLayout /**< More than one layout rule places the entity */
} SpatialError;

/** Point and orientation on an entity that SpatialAlign aligns */
typedef struct SpatialAnchor {
    AnchorPoint x;      /**< Point on the X axis of the bounds */
    AnchorPoint y;      /**< Point on the Y axis of the bounds */
    AnchorPoint z;      /**< Point on the Z axis of the bounds */
    AnchorFace face;    /**< Face of the bounds, overrides the point on its axis */
    ecs_entity_t frame; /**< Child with an AttachmentFrame, overrides the point and face */
} SpatialAnchor;

/** Scale an entity so that its bounds fit the bounds of a target */
typedef struct Fit {
    ecs_entity_t target; /**< Entity to fit to */
    SpatialAxis axes;    /**< Axes to fit, all axes when 0 */
    vec3 padding;        /**< Space kept free on both sides of every axis of the target */
    FitMode mode;        /**< How the axes are scaled */
} Fit;

/** Place an entity so that an anchor on it meets an anchor on a target */
typedef struct SpatialAlign {
    ecs_entity_t target;    /**< Entity to align to */
    SpatialAnchor self;     /**< Anchor on the entity */
    SpatialAnchor other;    /**< Anchor on the target */
    AlignRotation rotation; /**< Whether the entity takes the rotation of the target anchor */
    vec3 offset;            /**< Offset in the frame of the target anchor */
    vec3 rotation_offset;   /**< Rotation applied on top of the aligned rotation */
} SpatialAlign;

/** Repeat the children of an entity along an axis */
typedef struct Array {
    SpatialAxis axis;   /**< Single axis along which instances are placed */
    int32_t count;      /**< Number of instances of every child */
    float spacing;      /**< Distance between instances */
    vec3 rotation_step; /**< Rotation added per instance */
    vec3 scale_step;    /**< Scale added per instance */
} Array;

/** Place the children of an entity next to each other along an axis */
typedef struct Distribute {
    SpatialAxis axis;      /**< Single axis along which children are placed */
    float span;            /**< Length to distribute over, no limit for SpatialFixedGap when 0 */
    DistributionMode mode; /**< How children are spaced */
    float gap;             /**< Gap between children for SpatialFixedGap */
} Distribute;

/** Frame on the parent of an entity that a SpatialAnchor can refer to */
typedef struct AttachmentFrame {
    vec3 position; /**< Position in the space of the parent */
    vec3 rotation; /**< Rotation in the space of the parent */
} AttachmentFrame;

/** Bounds of an entity managed by the layout, before Fit scales it */
typedef struct IntrinsicBounds {
    vec3 min; /**< Minimum in local space */
    vec3 max; /**< Maximum in local space */
} IntrinsicBounds;

/** Bounds of an entity managed by the layout, after Fit scales it */
typedef struct ResolvedBounds {
    vec3 min; /**< Minimum in local space */
    vec3 max; /**< Maximum in local space */
} ResolvedBounds;

/** Size of the Box of a fitted entity, after Fit scales it */
typedef struct ResolvedBox {
    float x; /**< Size on the X axis */
    float y; /**< Size on the Y axis */
    float z; /**< Size on the Z axis */
} ResolvedBox;

/** Error of the layout rules of an entity */
typedef struct SpatialDiagnostic {
    SpatialError code;    /**< The error */
    ecs_entity_t related; /**< The entity the error is about */
} SpatialDiagnostic;

/** Instance an Array created for one of its children */
typedef struct ArrayInstance {
    ecs_entity_t owner;     /**< The entity with the Array */
    ecs_entity_t prototype; /**< The child the instance was created from */
    int32_t index;          /**< Index of the instance in the array */
} ArrayInstance;

/** Keeps an entity and its descendants out of the spatial index, for actors that move every
 * frame and that no query needs, such as enemies: they then cost box.spatial.Update nothing.
 *
 * No query finds them: picks neither hit them nor are blocked by them, Interactive or not, nor
 * do move controllers, box_spatial_bounds, design mode or bake regions. Adding or removing the
 * tag takes effect in the next update. See docs/spatial-index.md.
 */
extern ECS_TAG_DECLARE(SpatialIgnore);

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

/** Solve the spatial layout for the inputs that changed since the last update.
 *
 * Entities that follow DynamicTransform move without change events, so what depends on them is
 * solved again on every update.
 *
 * @param world The world
 * @return Whether every layout rule was solved without error, false while results are written
 */
bool box_spatial_layout_update(
    ecs_world_t *world);

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
