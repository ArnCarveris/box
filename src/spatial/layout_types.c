/**
 * @file spatial/layout_types.c
 * @brief Component and reflection registration of the spatial layout types.
 */

#include "layout.h"

ECS_COMPONENT_DECLARE(SpatialAxis);
ECS_COMPONENT_DECLARE(FitMode);
ECS_COMPONENT_DECLARE(AnchorPoint);
ECS_COMPONENT_DECLARE(AnchorFace);
ECS_COMPONENT_DECLARE(AlignRotation);
ECS_COMPONENT_DECLARE(DistributionMode);
ECS_COMPONENT_DECLARE(SpatialError);
ECS_COMPONENT_DECLARE(SpatialAnchor);
ECS_COMPONENT_DECLARE(Fit);
ECS_COMPONENT_DECLARE(SpatialAlign);
ECS_COMPONENT_DECLARE(Array);
ECS_COMPONENT_DECLARE(Distribute);
ECS_COMPONENT_DECLARE(AttachmentFrame);
ECS_COMPONENT_DECLARE(IntrinsicBounds);
ECS_COMPONENT_DECLARE(ResolvedBounds);
ECS_COMPONENT_DECLARE(ResolvedBox);
ECS_COMPONENT_DECLARE(SpatialDiagnostic);
ECS_COMPONENT_DECLARE(ArrayInstance);

#define MEMBER(T, m, MT) {.name = #m, .type = ecs_id(MT), .offset = offsetof(T, m)}
#define STRUCT(T, ...) ecs_struct(world, {.entity = ecs_id(T), .members = {__VA_ARGS__}})

void box_spatial_layout_types(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, SpatialAxis);
    ECS_COMPONENT_DEFINE(world, FitMode);
    ECS_COMPONENT_DEFINE(world, AnchorPoint);
    ECS_COMPONENT_DEFINE(world, AnchorFace);
    ECS_COMPONENT_DEFINE(world, AlignRotation);
    ECS_COMPONENT_DEFINE(world, DistributionMode);
    ECS_COMPONENT_DEFINE(world, SpatialError);
    ECS_COMPONENT_DEFINE(world, SpatialAnchor);
    ECS_COMPONENT_DEFINE(world, Fit);
    ecs_id(SpatialAlign) = ecs_component(
        world, {.entity = ecs_entity(world, {.name = "Align", .symbol = "SpatialAlign"}),
                .type = {.size = sizeof(SpatialAlign), .alignment = ECS_ALIGNOF(SpatialAlign)}});
    ECS_COMPONENT_DEFINE(world, Array);
    ECS_COMPONENT_DEFINE(world, Distribute);
    ECS_COMPONENT_DEFINE(world, AttachmentFrame);
    ECS_COMPONENT_DEFINE(world, IntrinsicBounds);
    ECS_COMPONENT_DEFINE(world, ResolvedBounds);
    ECS_COMPONENT_DEFINE(world, ResolvedBox);
    ECS_COMPONENT_DEFINE(world, SpatialDiagnostic);
    ECS_COMPONENT_DEFINE(world, ArrayInstance);

    ecs_enum(world, {.entity = ecs_id(SpatialAxis),
                     .constants = {{"X", SpatialX},
                                   {"Y", SpatialY},
                                   {"Z", SpatialZ},
                                   {"XY", SpatialXY},
                                   {"XZ", SpatialXZ},
                                   {"YZ", SpatialYZ},
                                   {"XYZ", SpatialXYZ}}});
    ecs_enum(world, {.entity = ecs_id(FitMode),
                     .constants = {{"Stretch", SpatialStretch},
                                   {"Contain", SpatialContain},
                                   {"Cover", SpatialCover}}});
    ecs_enum(world,
             {.entity = ecs_id(AnchorPoint),
              .constants = {{"Center", SpatialCenter}, {"Min", SpatialMin}, {"Max", SpatialMax}}});
    ecs_enum(world, {.entity = ecs_id(AnchorFace),
                     .constants = {{"None", SpatialNoFace},
                                   {"Front", SpatialFront},
                                   {"Back", SpatialBack},
                                   {"Top", SpatialTop},
                                   {"Bottom", SpatialBottom},
                                   {"Left", SpatialLeft},
                                   {"Right", SpatialRight}}});
    ecs_enum(world, {.entity = ecs_id(AlignRotation),
                     .constants = {{"Keep", SpatialKeep}, {"Match", SpatialMatch}}});
    ecs_enum(world, {.entity = ecs_id(DistributionMode),
                     .constants = {{"SpaceBetween", SpatialSpaceBetween},
                                   {"SpaceAround", SpatialSpaceAround},
                                   {"SpaceEvenly", SpatialSpaceEvenly},
                                   {"FixedGap", SpatialFixedGap}}});
    ecs_enum(world, {.entity = ecs_id(SpatialError),
                     .constants = {{"Ok", SpatialOk},
                                   {"InvalidTarget", SpatialInvalidTarget},
                                   {"MissingBounds", SpatialMissingBounds},
                                   {"ZeroBounds", SpatialZeroBounds},
                                   {"Cycle", SpatialCycle},
                                   {"Impossible", SpatialImpossible},
                                   {"InvalidAnchor", SpatialInvalidAnchor},
                                   {"ConflictingLayout", SpatialConflictingLayout}}});

    STRUCT(SpatialAnchor, MEMBER(SpatialAnchor, x, AnchorPoint),
           MEMBER(SpatialAnchor, y, AnchorPoint), MEMBER(SpatialAnchor, z, AnchorPoint),
           MEMBER(SpatialAnchor, face, AnchorFace), MEMBER(SpatialAnchor, frame, ecs_entity_t));
    const char *faces[] = {"Front", "Back", "Top", "Bottom", "Left", "Right"};
    for (int i = 0; i < 6; i++) {
        SpatialAnchor value = {.face = (AnchorFace)(i + 1)};
        ecs_const_var(world, {.name = faces[i],
                              .parent = ecs_get_scope(world),
                              .type = ecs_id(SpatialAnchor),
                              .value = &value});
    }

    STRUCT(Fit, MEMBER(Fit, target, ecs_entity_t), MEMBER(Fit, axes, SpatialAxis),
           MEMBER(Fit, padding, vec3), MEMBER(Fit, mode, FitMode));
    STRUCT(SpatialAlign, MEMBER(SpatialAlign, target, ecs_entity_t),
           MEMBER(SpatialAlign, self, SpatialAnchor), MEMBER(SpatialAlign, other, SpatialAnchor),
           MEMBER(SpatialAlign, rotation, AlignRotation), MEMBER(SpatialAlign, offset, vec3),
           MEMBER(SpatialAlign, rotation_offset, vec3));
    STRUCT(Array, MEMBER(Array, axis, SpatialAxis), MEMBER(Array, count, ecs_i32_t),
           MEMBER(Array, spacing, ecs_f32_t), MEMBER(Array, rotation_step, vec3),
           MEMBER(Array, scale_step, vec3));
    STRUCT(Distribute, MEMBER(Distribute, axis, SpatialAxis), MEMBER(Distribute, span, ecs_f32_t),
           MEMBER(Distribute, mode, DistributionMode), MEMBER(Distribute, gap, ecs_f32_t));
    STRUCT(AttachmentFrame, MEMBER(AttachmentFrame, position, vec3),
           MEMBER(AttachmentFrame, rotation, vec3));
    STRUCT(IntrinsicBounds, MEMBER(IntrinsicBounds, min, vec3), MEMBER(IntrinsicBounds, max, vec3));
    STRUCT(ResolvedBounds, MEMBER(ResolvedBounds, min, vec3), MEMBER(ResolvedBounds, max, vec3));
    STRUCT(ResolvedBox, MEMBER(ResolvedBox, x, ecs_f32_t), MEMBER(ResolvedBox, y, ecs_f32_t),
           MEMBER(ResolvedBox, z, ecs_f32_t));
    STRUCT(SpatialDiagnostic, MEMBER(SpatialDiagnostic, code, SpatialError),
           MEMBER(SpatialDiagnostic, related, ecs_entity_t));
    STRUCT(ArrayInstance, MEMBER(ArrayInstance, owner, ecs_entity_t),
           MEMBER(ArrayInstance, prototype, ecs_entity_t), MEMBER(ArrayInstance, index, ecs_i32_t));
}
