/**
 * @file player.c
 * @brief The player: the astronaut walks with the held actions of OreActions while playing,
 * and the camera follows it from above at the zoom level of OreView in every state.
 *
 * The camera never rotates, so W walks toward -Z, S toward +Z, A toward -X and D toward +X.
 */

#include "game.h"
#include <math.h>

bool ore_action_down(
    const ecs_world_t *world,
    ecs_entity_t action)
{
    const ActionState *s = action ? ecs_get(world, action, ActionState) : NULL;

    return s && s->down;
}

/** Walk the astronaut: diagonals are normalized, the run action doubles the speed, the
 * astronaut faces where it walks and stays within cfg.mapClamp of the center on x and z.
 *
 * @param it Iterator over the players, with OreActions, while playing
 */
static void OrePlayerMove(
    ecs_iter_t *it)
{
    OrePlayer *player = ecs_field(it, OrePlayer, 0);
    Position *p = ecs_field(it, Position, 1);
    Rotation *r = ecs_field(it, Rotation, 2);
    const OreActions *a = ecs_field(it, OreActions, 3);
    float clamp = ORE_CFG(it->world, "mapClamp");
    float speed = ORE_CFG(it->world, "walkSpeed");
    float dx = (float)ore_action_down(it->world, a->right) -
        (float)ore_action_down(it->world, a->left);
    float dz = (float)ore_action_down(it->world, a->back) -
        (float)ore_action_down(it->world, a->forward);
    float len = sqrtf(dx * dx + dz * dz);
    bool moving = len > 0.0f;
    bool running = moving && ore_action_down(it->world, a->run);
    int32_t i;
    if (running) {
        speed *= ORE_CFG(it->world, "runMultiplier");
    }

    for (i = 0; i < it->count; i ++) {
        if (moving) {
            p[i].x += dx / len * speed * it->delta_time;
            p[i].z += dz / len * speed * it->delta_time;
            r[i].y = atan2f(dx, dz);
        }

        p[i].x = glm_clamp(p[i].x, -clamp, clamp);
        p[i].z = glm_clamp(p[i].z, -clamp, clamp);
        if (player[i].moving != moving || player[i].running != running) {
            player[i].moving = moving;
            player[i].running = running;
            ecs_modified(it->stage, it->entities[i], OrePlayer);
        }
    }
}

/** Step a glide value toward its target, or snap to it.
 *
 * @param value The value
 * @param target The target
 * @param alpha Fraction of the distance covered this frame
 * @param snap Whether to snap to the target
 * @return Whether the value changed
 */
static bool ore_glide(
    float *value,
    float target,
    float alpha,
    bool snap)
{
    float next = snap ? target : *value + (target - *value) * alpha;
    bool result = next != *value;
    *value = next;

    return result;
}

/** Place the camera at the glide of the view above an astronaut position, looking at its look
 * point along -Z; it is only set when it moves.
 *
 * @param world The world or a stage
 * @param view The view
 * @param at Position of the astronaut
 */
static void ore_camera_place(
    ecs_world_t *world,
    const OreView *view,
    const Position *at)
{
    const Position *cp = ecs_get(world, view->camera, Position);
    const Rotation *cr = ecs_get(world, view->camera, Rotation);
    Position pos = { at->x, view->height, at->z + view->back };
    Rotation rot = { -atan2f(view->height - view->look, view->back), 0.0f, 0.0f };
    if (!cp || cp->x != pos.x || cp->y != pos.y || cp->z != pos.z) {
        ecs_set_ptr(world, view->camera, Position, &pos);
    }

    if (!cr || cr->x != rot.x || cr->y != rot.y || cr->z != rot.z) {
        ecs_set_ptr(world, view->camera, Rotation, &rot);
    }
}

/** Step the zoom level with the wheel (up zooms in), glide the camera toward the level by
 * cfg.cameraGlide * dt of the distance a frame and place it above the astronaut.
 *
 * @param it Iterator over the players, with OreView and Input, in every state
 */
static void OreCameraFollow(
    ecs_iter_t *it)
{
    OreView *view = ecs_field(it, OreView, 0);
    const Input *input = ecs_field(it, Input, 1);
    const Position *p = ecs_field(it, Position, 3);
    OreZoom zoom = ecs_const_var_get_t(it->world, "cfg.zoom", OreZoom);
    float alpha = glm_min(ORE_CFG(it->world, "cameraGlide") * it->delta_time, 1.0f);
    float wheel = input->mouse.scroll[1];
    int32_t level = view->level + (wheel > 0 ? -1 : 0) + (wheel < 0 ? 1 : 0);
    bool snap = view->height <= 0;
    bool changed;
    level = level < 0 ? 0 : level;
    level = level >= ORE_ZOOM_LEVELS ? ORE_ZOOM_LEVELS - 1 : level;
    changed = level != view->level;
    view->level = level;
    changed |= ore_glide(&view->height, zoom.height[level], alpha, snap);
    changed |= ore_glide(&view->back, zoom.back[level], alpha, snap);
    changed |= ore_glide(&view->look, zoom.look[level], alpha, snap);
    if (changed) {
        ecs_singleton_modified(it->stage, OreView);
    }

    if (it->count && view->camera && ecs_is_alive(it->world, view->camera)) {
        ore_camera_place(it->stage, view, &p[0]);
    }
}

void ore_player_import(
    ecs_world_t *world)
{
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);

    ecs_add_pair(world, ecs_id(OrePlayer), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(OrePlayer), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(OrePlayer), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(OreLimb), EcsWith, ecs_id(Rotation));

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePlayerMove" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePlayer) },
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ecs_id(Rotation), .src.id = EcsSelf },
            { .id = ecs_id(OreActions), .src.id = ecs_id(OreActions), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OrePlayerMove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCameraFollow" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreView), .src.id = ecs_id(OreView), .inout = EcsInOut },
            { .id = ecs_id(Input), .src.id = ecs_id(Input), .inout = EcsIn },
            { .id = ecs_id(OrePlayer), .inout = EcsIn },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsIn }
        },
        .callback = OreCameraFollow
    });
}
