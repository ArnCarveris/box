/**
 * @file player.c
 * @brief The player: the astronaut walks with the held actions of OreActions while playing,
 * hovers on its jetpack over the tiles of buildings, and the camera follows it from above at the
 * zoom level of OreView in every state.
 *
 * While playing OrePlayerFx advances its walk cycle (OreGait), bobs it, kicks up its foot dust
 * and plays its jet plume in the air, and OreLimbs swings its limbs (OreLimb). Its death
 * (ore_player_die) plays the splat and drops it on its face.
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

/** Step the height of the jetpack toward its target: up at rise, down at fall units a second.
 *
 * @param hover The height
 * @param target The height it heads for
 * @param rise Units a second it climbs
 * @param fall Units a second it sinks
 * @param dt Seconds of the frame
 * @return The new height
 */
static float ore_hover_step(
    float hover,
    float target,
    float rise,
    float fall,
    float dt)
{
    float result = hover;
    if (hover < target) {
        result = glm_min(hover + rise * dt, target);
    } else if (hover > target) {
        result = glm_max(hover - fall * dt, target);
    }

    return result;
}

/** Walk the astronaut: diagonals are normalized, the run action doubles the speed, the
 * astronaut faces where it walks and stays within cfg.mapClamp of the center on x and z.
 *
 * Over a tile with a building it hovers on its jetpack at cfg.hoverHeight, rising at
 * cfg.hoverRise and falling at cfg.hoverFall units a second.
 *
 * @param it Iterator over the players, with OreActions and OreMap, while playing
 */
static void OrePlayerMove(
    ecs_iter_t *it)
{
    OrePlayer *player = ecs_field(it, OrePlayer, 0);
    Position *p = ecs_field(it, Position, 1);
    Rotation *r = ecs_field(it, Rotation, 2);
    const OreActions *a = ecs_field(it, OreActions, 3);
    OreMap *map = ecs_field(it, OreMap, 4);
    ore_grid_t grid = ore_grid_get(it->world);
    float hover_height = ORE_CFG(it->world, "hoverHeight");
    float rise = ORE_CFG(it->world, "hoverRise");
    float fall = ORE_CFG(it->world, "hoverFall");
    float airborne = ORE_CFG(it->world, "airborne");
    float clamp = ORE_CFG(it->world, "mapClamp");
    float speed = ORE_CFG(it->world, "walkSpeed");
    float dx = (float)ore_action_down(it->world, a->right) -
        (float)ore_action_down(it->world, a->left);
    float dz = (float)ore_action_down(it->world, a->back) -
        (float)ore_action_down(it->world, a->forward);
    float len = sqrtf(dx * dx + dz * dz);
    bool moving = len > 0.0f;
    bool running = moving && ore_action_down(it->world, a->run);
    ecs_entity_t *cell;
    float hover;
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
        cell = ore_map_building(map, ore_row_at(&grid, p[i].z), ore_col_at(&grid, p[i].x));
        hover = ore_hover_step(player[i].hover, cell && *cell ? hover_height : 0, rise, fall,
            it->delta_time);
        p[i].y = hover;
        if (player[i].moving != moving || player[i].running != running ||
            player[i].hover != hover)
        {
            player[i].moving = moving;
            player[i].running = running;
            player[i].hover = hover;
            player[i].airborne = hover > airborne;
            ecs_modified(it->stage, it->entities[i], OrePlayer);
        }
    }
}

void ore_player_die(
    ecs_world_t *world,
    ecs_entity_t player)
{
    const ecs_world_t *real = ecs_get_world(world);
    const Position *p = ecs_get(world, player, Position);
    const OreEffects *fx = ecs_singleton_get(world, OreEffects);
    if (p && fx) {
        vec3 at = { p->x, p->y + ORE_CFG(real, "splatLift"), p->z };
        ore_burst(world, fx->splat_burst, at, 1.0f);
    }

    ecs_set(world, player, Rotation, { ORE_CFG(real, "deathPitch"), 0, 0 });
    ore_state_set(world, OreGameStateLost);
}

/** Set the playback of the jet plume of an astronaut (its child jet, a JetPlume); it is only
 * written when that changes.
 *
 * @param world The world
 * @param stage The stage of the system
 * @param player The astronaut
 * @param state The playback state of the plume
 * @param playing_only Only change a plume that plays
 */
static void ore_jet_set(
    const ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t player,
    PlaybackState state,
    bool playing_only)
{
    ecs_entity_t jet = ecs_lookup_child(world, player, "jet");
    const ParticlePlayback *playback = jet ? ecs_get(world, jet, ParticlePlayback) : NULL;
    ParticlePlayback next = playback ? *playback : (ParticlePlayback){ .state = ParticlePlaying };
    if (jet && next.state != state && (!playing_only || next.state == ParticlePlaying)) {
        next.state = state;
        next.reset = false;
        ecs_set_ptr(stage, jet, ParticlePlayback, &next);
    }
}

/** Animate the astronaut after its walk, the rest of the original's OrePlayerMove.
 *
 * While it moves its walk cycle advances its speed times cfg.stepRate a second and on the ground
 * it bobs |sin(cycle)| * cfg.bobHeight up. Every half step on the ground kicks up FootDust at its
 * feet, and the jet plume plays while it is airborne.
 *
 * @param it Iterator over the players, with OreEffects, while playing
 */
static void OrePlayerFx(
    ecs_iter_t *it)
{
    const OrePlayer *player = ecs_field(it, OrePlayer, 0);
    OreGait *gait = ecs_field(it, OreGait, 1);
    Position *p = ecs_field(it, Position, 2);
    const OreEffects *fx = ecs_field(it, OreEffects, 3);
    float speed = ORE_CFG(it->world, "walkSpeed");
    float run = ORE_CFG(it->world, "runMultiplier");
    float rate = ORE_CFG(it->world, "stepRate");
    float bob = ORE_CFG(it->world, "bobHeight");
    float dust_y = ORE_CFG(it->world, "footDustY");
    float two_pi = 2.0f * GLM_PIf;
    int32_t i, step;
    for (i = 0; i < it->count; i ++) {
        bool grounded = !player[i].airborne;
        if (player[i].moving) {
            gait[i].cycle += it->delta_time * speed * (player[i].running ? run : 1.0f) * rate;
            gait[i].cycle = fmodf(gait[i].cycle, two_pi);
            if (grounded) {
                p[i].y += fabsf(sinf(gait[i].cycle)) * bob;
            }
        }

        step = (int32_t)floorf(gait[i].cycle / GLM_PIf);
        if (step != gait[i].step) {
            gait[i].step = step;
            if (player[i].moving && grounded) {
                vec3 at = { p[i].x, dust_y, p[i].z };
                ore_burst(it->stage, fx->foot_dust, at, 1.0f);
            }
        }

        ore_jet_set(it->world, it->stage, it->entities[i],
            player[i].airborne ? ParticlePlaying : ParticleStopped, false);
    }
}

/** Pause a playing jet plume outside of play, so it stands still with the game; OrePlayerFx
 * plays or stops it again when the game resumes.
 *
 * @param it Iterator over the players, while not playing
 */
static void OrePlayerFxHold(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ore_jet_set(it->world, it->stage, it->entities[i], ParticlePaused, true);
    }
}

/** Swing the limbs of the astronaut toward their pose, covering cfg.limbBlend * dt of the
 * distance a frame.
 *
 * On the ground a moving astronaut swings them sin(cycle + phase) * swing, a standing one holds
 * them straight.
 *
 * In the air they trail cfg.airTrail times their swing behind while it moves (times the run
 * multiplier while running, at most cfg.airTrailMax) and dangle cfg.airDangle times their swing
 * at cfg.airDangleRate radians a second of play.
 *
 * @param it Iterator over the limbs of the players, with OreClock, while playing
 */
static void OreLimbs(
    ecs_iter_t *it)
{
    const OreLimb *limb = ecs_field(it, OreLimb, 0);
    Rotation *r = ecs_field(it, Rotation, 1);
    const OrePlayer *player = ecs_field(it, OrePlayer, 2);
    const OreGait *gait = ecs_field(it, OreGait, 3);
    const OreClock *clock = ecs_field(it, OreClock, 4);
    float alpha = glm_min(it->delta_time * ORE_CFG(it->world, "limbBlend"), 1.0f);
    float dangle = ORE_CFG(it->world, "airDangle");
    float dangle_rate = ORE_CFG(it->world, "airDangleRate");
    float lag = 0, target;
    int32_t i;
    if (player->moving) {
        lag = glm_min(ORE_CFG(it->world, "airTrail") *
            (player->running ? ORE_CFG(it->world, "runMultiplier") : 1.0f),
            ORE_CFG(it->world, "airTrailMax"));
    }

    for (i = 0; i < it->count; i ++) {
        if (player->airborne) {
            target = (lag + sinf(clock->elapsed * dangle_rate + limb[i].phase) * dangle) *
                limb[i].swing;
        } else {
            target = player->moving ? sinf(gait->cycle + limb[i].phase) * limb[i].swing : 0;
        }

        r[i].x += (target - r[i].x) * alpha;
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
    ecs_add_pair(world, ecs_id(OrePlayer), EcsWith, ecs_id(OreGait));
    ecs_add_pair(world, ecs_id(OreLimb), EcsWith, ecs_id(Rotation));

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePlayerMove" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePlayer) },
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ecs_id(Rotation), .src.id = EcsSelf },
            { .id = ecs_id(OreActions), .src.id = ecs_id(OreActions), .inout = EcsIn },
            { .id = ecs_id(OreMap), .src.id = ecs_id(OreMap), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OrePlayerMove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePlayerFx" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePlayer), .inout = EcsIn },
            { .id = ecs_id(OreGait), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OreEffects), .src.id = ecs_id(OreEffects), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OrePlayerFx
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePlayerFxHold" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePlayer), .inout = EcsInOutNone },
            { .id = playing, .src.id = state, .oper = EcsNot }
        },
        .callback = OrePlayerFxHold
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreLimbs" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreLimb), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(Rotation), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(OrePlayer), .src.id = EcsUp, .trav = EcsChildOf, .inout = EcsIn },
            { .id = ecs_id(OreGait), .src.id = EcsUp, .trav = EcsChildOf, .inout = EcsIn },
            { .id = ecs_id(OreClock), .src.id = ecs_id(OreClock), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreLimbs
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
