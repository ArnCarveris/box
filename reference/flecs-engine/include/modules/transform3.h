#ifndef FLECS_ENGINE_TRANSFORM3_H
#define FLECS_ENGINE_TRANSFORM3_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_TRANSFORM3_IMPL
#define ECS_META_IMPL EXTERN
#endif

typedef flecs_vec3_t FlecsPosition3;
typedef flecs_vec3_t FlecsRotation3;
typedef flecs_vec3_t FlecsScale3;
typedef flecs_vec3_t FlecsLookAt;

extern ECS_COMPONENT_DECLARE(FlecsPosition3);
extern ECS_COMPONENT_DECLARE(FlecsRotation3);
extern ECS_COMPONENT_DECLARE(FlecsScale3);
extern ECS_COMPONENT_DECLARE(FlecsLookAt);

extern ECS_TAG_DECLARE(FlecsDynamicTransform);
extern ECS_TAG_DECLARE(FlecsManualTransform);
extern ECS_TAG_DECLARE(FlecsRigidSubtree);

ECS_STRUCT(FlecsWorldTransform3, {
    mat4 m;
});

ECS_STRUCT(FlecsAABB, {
    float min[3];
    float max[3];
});

/* Gimbal-safe orientation, opt-in alternative to Rotation3.
 *
 * Rotation3 composes its Euler angles as Rx * Ry * Rz: pitch is applied in
 * the world frame, ahead of yaw, so at yaw +/-90 degrees (a heading down the
 * world X axis) pitch no longer tilts the entity's forward axis at all. A
 * vehicle following an east-west grade cannot be oriented with it.
 *
 * Orientation3 composes Ry * Rx * Rz - yaw, then pitch in the body frame,
 * then roll around the forward axis - which is the aircraft convention and
 * has no singularity at any heading. The remaining degenerate case is
 * looking straight up or down, where yaw and roll describe the same turn;
 * nothing is lost there either, and no value produces a NaN.
 *
 * Forward is local +Z, matching LookAt, Billboard and Text3:
 *
 *   forward = { cos(pitch) * sin(yaw), -sin(pitch), cos(pitch) * cos(yaw) }
 *
 * An entity that owns both components is oriented by this one; Rotation3 is
 * left untouched and keeps its meaning everywhere else. Systems that steer an
 * entity by writing Rotation3 - Billboard, LookAt, the camera controller -
 * therefore have no effect on an entity that also owns Orientation3; drive
 * such an entity with flecsEngine_transform3Orient instead. */
ECS_STRUCT(FlecsOrientation3, {
    float pitch;
    float yaw;
    float roll;
});

extern ECS_COMPONENT_DECLARE(FlecsOrientation3);

void flecsEngine_transform3SetPosition(
    ecs_world_t *world,
    ecs_entity_t entity,
    float x,
    float y,
    float z);

void flecsEngine_transform3SetRotation(
    ecs_world_t *world,
    ecs_entity_t entity,
    float x,
    float y,
    float z);

void flecsEngine_transform3SetScale(
    ecs_world_t *world,
    ecs_entity_t entity,
    float x,
    float y,
    float z);

void flecsEngine_transform3SetOrientation(
    ecs_world_t *world,
    ecs_entity_t entity,
    float pitch,
    float yaw,
    float roll);

/* Orientation that points local +Z along forward. up is optional: when given
 * it is the direction the entity's local +Y should lean toward, which is
 * what banks a vehicle into a curve; without it roll is 0 (level). Returns
 * false, leaving out untouched, for a zero-length forward. Pure math, no
 * world access. */
bool flecsEngine_orientationFromForward(
    const flecs_vec3_t *forward,
    const flecs_vec3_t *up,
    FlecsOrientation3 *out);

/* The orientation's axes in world space. Any output may be NULL. */
void flecsEngine_orientationBasis(
    const FlecsOrientation3 *orientation,
    flecs_vec3_t *forward,
    flecs_vec3_t *up,
    flecs_vec3_t *right);

/* Point an entity along forward in one call: the shape a mover wants every
 * frame. Adds Orientation3 if the entity does not have it yet. Returns false
 * for a zero-length forward, leaving the entity as it was. */
bool flecsEngine_transform3Orient(
    ecs_world_t *world,
    ecs_entity_t entity,
    const flecs_vec3_t *forward,
    const flecs_vec3_t *up);

#endif
