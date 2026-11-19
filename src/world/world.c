/**
 * @file world/world.c
 * @brief Registers the box.world module: the world origin, the focus and the conversions
 * between local and absolute positions in C and script.
 */

#define BOX_WORLD_IMPL
#include "world.h"

ECS_TAG_DECLARE(WorldFocus);
ECS_TAG_DECLARE(WorldOriginFixed);

/** Construct WorldOrigin values at the absolute origin with the default threshold and step.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_world_origin_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    WorldOrigin *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (WorldOrigin){
            .threshold = BOX_WORLD_ORIGIN_THRESHOLD,
            .step = BOX_WORLD_ORIGIN_STEP,
            .enabled = true
        };
    }
}

/** Add WorldOriginStatus next to a WorldOrigin singleton that was added.
 *
 * @param it The iterator of the observer
 */
static void box_world_origin_on_add(
    ecs_iter_t *it)
{
    ecs_singleton_add(it->world, WorldOriginStatus);
}

ecs_entity_t box_world_scene_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    while (e && !result) {
        if (ecs_has(world, e, Scene)) {
            result = e;
        }

        e = ecs_get_parent(world, e);
    }

    return result;
}

bool box_world_is_root(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = true;
    ecs_entity_t parent = ecs_get_parent(world, e);
    while (parent && result) {
        result = !ecs_has(world, parent, WorldTransform);
        parent = ecs_get_parent(world, parent);
    }

    return result;
}

/** Get the local world position of an entity as of now: the Position of a root, which the
 * transforms of this frame did not compute yet, else the translation of its world transform.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the position
 * @return Whether the entity has a place in the world
 */
static bool box_world_position(
    const ecs_world_t *world,
    ecs_entity_t e,
    vec3 out)
{
    const Position *p = ecs_get(world, e, Position);
    WorldTransform buf;
    const WorldTransform *t = NULL;
    bool result = false;
    if (p && !ecs_has_id(world, e, ManualTransform) && box_world_is_root(world, e)) {
        out[0] = p->x;
        out[1] = p->y;
        out[2] = p->z;
        result = true;
    } else if ((t = box_transform_world(world, e, &buf))) {
        glm_vec3_copy(ECS_CONST_CAST(float*, t->m[3]), out);
        result = true;
    }

    return result;
}

/** Find the camera of the first View by order that has one.
 *
 * @param world The world
 * @return The camera, 0 when no view has one
 */
static ecs_entity_t box_world_view_camera(
    const ecs_world_t *world)
{
    ecs_entity_t result = 0, first = 0;
    int32_t order = 0, i;
    ecs_iter_t it = ecs_each(ECS_CONST_CAST(ecs_world_t*, world), View);
    while (ecs_each_next(&it)) {
        const View *views = ecs_field(&it, View, 0);
        for (i = 0; i < it.count; i ++) {
            if (!views[i].camera || !ecs_is_alive(world, views[i].camera)) {
                continue;
            }

            if (!first || box_view_compare(views[i].order, it.entities[i], order, first) < 0) {
                first = it.entities[i];
                order = views[i].order;
                result = views[i].camera;
            }
        }
    }

    return result;
}

/** Add a focus to the output when it has a place in the world.
 *
 * @param world The world
 * @param e The entity
 * @param out The foci, NULL to count them
 * @param max Foci out holds
 * @param count Foci so far, incremented for the entity
 */
static void box_world_focus_add(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_world_focus_t *out,
    int32_t max,
    int32_t *count)
{
    vec3 position;
    if (!box_world_position(world, e, position)) {
        goto done;
    }

    if (out && *count < max) {
        out[*count].entity = e;
        out[*count].scene = box_world_scene_of(world, e);
        glm_vec3_copy(position, out[*count].position);
    }

    (*count) ++;
done:
    return;
}

int32_t box_world_focus(
    const ecs_world_t *world,
    box_world_focus_t *out,
    int32_t max)
{
    int32_t result = 0, i;
    ecs_iter_t it = ecs_each_id(ECS_CONST_CAST(ecs_world_t*, world), WorldFocus);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            box_world_focus_add(world, it.entities[i], out, max, &result);
        }
    }

    if (!result) {
        ecs_entity_t camera = box_world_view_camera(world);
        if (camera) {
            box_world_focus_add(world, camera, out, max, &result);
        }
    }

    return result;
}

void box_world_to_absolute(
    const ecs_world_t *world,
    const vec3 local,
    double out[3])
{
    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    out[0] = (o ? o->x : 0.0) + (double)local[0];
    out[1] = (o ? o->y : 0.0) + (double)local[1];
    out[2] = (o ? o->z : 0.0) + (double)local[2];
}

void box_world_to_local(
    const ecs_world_t *world,
    const double absolute[3],
    vec3 out)
{
    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    out[0] = (float)(absolute[0] - (o ? o->x : 0.0));
    out[1] = (float)(absolute[1] - (o ? o->y : 0.0));
    out[2] = (float)(absolute[2] - (o ? o->z : 0.0));
}

/** Get one axis of the absolute position of the local origin.
 *
 * @param world The world
 * @param axis 0 for X, 1 for Y, 2 for Z
 * @return The coordinate, 0 without WorldOrigin
 */
static double box_world_origin_axis(
    const ecs_world_t *world,
    int32_t axis)
{
    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    double result = 0.0;
    if (o) {
        result = axis == 0 ? o->x : axis == 1 ? o->y : o->z;
    }

    return result;
}

/** Script function that adds the origin to a local coordinate, the axis in its context.
 *
 * @param ctx The function context, its ctx the axis
 * @param argc Number of arguments
 * @param argv The local coordinate
 * @param result Receives the absolute coordinate
 */
static void box_world_absolute_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    int32_t axis = (int32_t)(intptr_t)ctx->ctx;
    (void)argc;
    *(double*)result->ptr = *(const double*)argv[0].ptr + box_world_origin_axis(ctx->world, axis);
}

/** Script function that subtracts the origin from an absolute coordinate, the axis in its
 * context.
 *
 * @param ctx The function context, its ctx the axis
 * @param argc Number of arguments
 * @param argv The absolute coordinate
 * @param result Receives the local coordinate
 */
static void box_world_local_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    int32_t axis = (int32_t)(intptr_t)ctx->ctx;
    (void)argc;
    *(double*)result->ptr = *(const double*)argv[0].ptr - box_world_origin_axis(ctx->world, axis);
}

/** Register the script functions that convert between local and absolute coordinates.
 *
 * @param world The world
 */
static void box_world_functions_init(
    ecs_world_t *world)
{
    static const char *absolute[3] = { "absolute_x", "absolute_y", "absolute_z" };
    static const char *local[3] = { "local_x", "local_y", "local_z" };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_function(world, {
            .name = absolute[i],
            .parent = ecs_get_scope(world),
            .return_type = ecs_id(ecs_f64_t),
            .params = {{ .name = "local", .type = ecs_id(ecs_f64_t) }},
            .callback = box_world_absolute_function,
            .ctx = (void*)(intptr_t)i
        });
        ecs_function(world, {
            .name = local[i],
            .parent = ecs_get_scope(world),
            .return_type = ecs_id(ecs_f64_t),
            .params = {{ .name = "absolute", .type = ecs_id(ecs_f64_t) }},
            .callback = box_world_local_function,
            .ctx = (void*)(intptr_t)i
        });
    }
}

void BoxWorldImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxWorld);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxMovement);
    ECS_IMPORT(world, BoxLayout);
    ECS_IMPORT(world, BoxControllers);
    ECS_IMPORT(world, BoxParticle);

    ECS_TAG_DEFINE(world, WorldFocus);
    ECS_TAG_DEFINE(world, WorldOriginFixed);
    ECS_META_COMPONENT(world, WorldOrigin);
    ECS_META_COMPONENT(world, WorldOriginStatus);
    ECS_META_COMPONENT(world, WorldOriginShift);

    ecs_set_hooks(world, WorldOrigin, { .ctor = box_world_origin_ctor });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldOrigin), .src.id = ecs_id(WorldOrigin) }},
        .events = { EcsOnAdd },
        .callback = box_world_origin_on_add
    });

    box_world_functions_init(world);
    box_world_origin_import(world);
}
