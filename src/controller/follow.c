/**
 * @file controller/follow.c
 * @brief The OnUpdate system that follows a target with a third person camera.
 *
 * The camera glides toward the place its preset asks for, lagging a running
 * target and settling when it stops. It moves like the fly camera, so geometry
 * between it and the target pushes it in.
 */

#include "controller_internal.h"
#include <math.h>

/** How fast the camera follows its place when the controller does not say */
#define BOX_FOLLOW_GLIDE (6.0f)

/** Radius the camera keeps free of geometry when the controller does not say */
#define BOX_FOLLOW_RADIUS (0.5f)

/** Count the zoom presets a follow camera authored.
 *
 * @param fc The controller
 * @return Presets before the first empty one
 */
static int32_t box_follow_levels(
    const FollowCamera *fc)
{
    int32_t result = 0;
    while (result < BOX_FOLLOW_LEVEL_MAX) {
        vec3 offset = fc->levels[result].offset;
        if (!offset.x && !offset.y && !offset.z) {
            break;
        }

        result ++;
    }

    return result;
}

/** Read what the zoom actions and the wheel ask of the cameras of this frame.
 *
 * The wheel only reaches a camera while the cursor is off the UI, which is what
 * keeps a scroll over a panel from zooming the scene behind it.
 *
 * @param world The world
 * @param actions The actions of the module
 * @return Presets to step, negative toward the target
 */
static int32_t box_follow_step(
    ecs_world_t *world,
    const ControllerActions *actions)
{
    const Input *in = ecs_singleton_get(world, Input);
    float scroll = in && !box_ui_mouse_captured(world)
        ? in->mouse.scroll_delta.y
        : 0;
    int32_t result = 0;
    if (scroll > 0 || box_controller_pressed(world, actions->zoom_in)) {
        result --;
    }

    if (scroll < 0 || box_controller_pressed(world, actions->zoom_out)) {
        result ++;
    }

    return result;
}

/** Step the preset of a camera and clamp it to the presets it authored.
 *
 * @param fc The controller, its level is written
 * @param step Presets to step
 */
static void box_follow_zoom(
    FollowCamera *fc,
    int32_t step)
{
    int32_t levels = box_follow_levels(fc), level = fc->level + step;
    if (level >= levels) {
        level = levels ? levels - 1 : 0;
    }

    fc->level = level < 0 ? 0 : level;
}

/** Find the place the camera and its view point belong this frame.
 *
 * @param fc The controller
 * @param target Position of the target
 * @param eye Receives where the camera belongs
 * @param focus Receives the point it looks at
 */
static void box_follow_place(
    const FollowCamera *fc,
    const Position *target,
    vec3 *eye,
    vec3 *focus)
{
    int32_t levels = box_follow_levels(fc);
    vec3 offset = fc->offset, look = fc->look_offset;
    if (levels) {
        int32_t level = fc->level < levels ? fc->level : levels - 1;
        offset = fc->levels[level].offset;
        look = fc->levels[level].look_offset;
    }

    eye[0] = vec3_new(
        target->x + offset.x, target->y + offset.y, target->z + offset.z);
    focus[0] = vec3_new(
        target->x + look.x, target->y + look.y, target->z + look.z);
}

/** Move a point toward a target over the frame at a rate per second.
 *
 * @param from The point
 * @param to Where it is headed
 * @param rate How fast it closes the distance, per second
 * @param dt Seconds the frame took
 * @return The point after the frame
 */
static vec3 box_follow_glide(
    vec3 from,
    vec3 to,
    float rate,
    float dt)
{
    float alpha = 1.0f - expf(-rate * dt);

    return vec3_new(
        from.x + (to.x - from.x) * alpha,
        from.y + (to.y - from.y) * alpha,
        from.z + (to.z - from.z) * alpha);
}

/** Move the camera to where it belongs, sliding along what stands in the way.
 *
 * @param world The world
 * @param fc The controller
 * @param e The entity of the camera
 * @param p The position, written
 * @param eye Where the camera belongs this frame
 * @param dt Seconds the frame took
 */
static void box_follow_travel(
    ecs_world_t *world,
    const FollowCamera *fc,
    ecs_entity_t e,
    Position *p,
    vec3 eye,
    float dt)
{
    float radius = box_controller_value(fc->radius, BOX_FOLLOW_RADIUS);
    box_controller_body_t body = {
        .entity = e,
        .radius = radius,
        .height = radius * 2.0f,
        .enabled = !fc->no_collision
    };

    vec3 foot = vec3_new(eye.x, eye.y - radius, eye.z);
    if (fc->placed && dt > 0) {
        vec3 from = vec3_new(p->x, p->y - radius, p->z);
        vec3 velocity = vec3_scale(vec3_sub(foot, from), 1.0f / dt);
        foot = from;
        box_controller_move(world, &body, &foot, &velocity, dt);
    }

    p->x = foot.x;
    p->y = foot.y + radius;
    p->z = foot.z;
}

/** Follow one target with one camera over the frame.
 *
 * @param world The world
 * @param step Presets the zoom of this frame asks the camera to move
 * @param fc The controller
 * @param e The entity of the camera
 * @param p The position, written
 * @param look The point it looks at, written
 * @param dt Seconds the frame took
 */
static void box_follow_one(
    ecs_world_t *world,
    int32_t step,
    FollowCamera *fc,
    ecs_entity_t e,
    Position *p,
    LookAt *look,
    float dt)
{
    const Position *tp = fc->target && ecs_is_alive(world, fc->target)
        ? ecs_get(world, fc->target, Position)
        : NULL;
    vec3 eye, focus, velocity = vec3_new(0, 0, 0);
    float glide;
    if (!tp) {
        goto done;
    }

    box_follow_zoom(fc, step);
    box_follow_place(fc, tp, &eye, &focus);
    glide = box_controller_value(fc->glide, BOX_FOLLOW_GLIDE);
    if (fc->placed) {
        vec3 at = vec3_new(p->x, p->y, p->z);
        vec3 on = vec3_new(look->x, look->y, look->z);
        eye = box_follow_glide(at, eye, glide, dt);
        focus = box_follow_glide(on, focus, glide, dt);
    }

    box_follow_travel(world, fc, e, p, eye, dt);
    box_controller_clearance(world, fc->terrain, fc->clearance, p, &velocity);
    look->x = focus.x;
    look->y = focus.y;
    look->z = focus.z;
    fc->placed = true;
done:
    return;
}

void BoxFollowCameraUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FollowCamera *fc = ecs_field(it, FollowCamera, 0);
    Position *p = ecs_field(it, Position, 1);
    LookAt *look = ecs_field(it, LookAt, 2);
    const ControllerActions *actions = ecs_singleton_get(
        world, ControllerActions);
    float dt = it->delta_time;
    int32_t i, step = box_follow_step(world, actions);
    for (i = 0; dt > 0 && i < it->count; i ++) {
        box_follow_one(
            world, step, &fc[i], it->entities[i], &p[i], &look[i], dt);
    }
}
