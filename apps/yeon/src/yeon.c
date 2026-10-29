#define YEON_IMPL
#include "game.h"
#include <math.h>

#define YEON_CFG(world, name) ecs_const_var_get_t(world, "cfg." name, ecs_f32_t)
#define YEON_CFG_I(world, name) ecs_const_var_get_t(world, "cfg." name, ecs_i32_t)

static bool yeon_state_is(
    const ecs_world_t *world,
    const char *state)
{
    ecs_entity_t s = ecs_lookup_child(world, ecs_id(YeonGameState), state);

    return ecs_has_pair(world, ecs_id(YeonGameState), ecs_id(YeonGameState), s);
}

static float yeon_random01(
    YeonGame *g)
{
    uint64_t z = (g->seed += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;

    return (float)((z ^ (z >> 31)) >> 40) / (float)(1u << 24);
}

static float yeon_random_range(
    YeonGame *g,
    float min,
    float max)
{
    return min + (max - min) * yeon_random01(g);
}

static bool yeon_action_down(
    const ecs_world_t *world,
    ecs_entity_t action)
{
    const ActionState *s = action ? ecs_get(world, action, ActionState) : NULL;

    return s && s->down;
}

static void YeonFlight(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    YeonPose *r = ecs_field(it, YeonPose, 1);
    YeonKite *k = ecs_field(it, YeonKite, 2);
    const YeonGame *g = ecs_field(it, YeonGame, 3);
    const YeonControls *c = ecs_field(it, YeonControls, 4);
    bool playing = ecs_field_is_set(it, 5);
    float dt = it->delta_time;
    float min_y = YEON_CFG(it->world, "minY");
    float max_y = YEON_CFG(it->world, "maxY");
    float climb = YEON_CFG(it->world, "climbSpeed");
    float accel = YEON_CFG(it->world, "climbAccel");
    float floor_y = YEON_CFG(it->world, "fallFloorY");
    float input = 0.0f;

    if (c) {
        input += yeon_action_down(it->world, c->up) ? 1.0f : 0.0f;
        input -= yeon_action_down(it->world, c->down) ? 1.0f : 0.0f;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        if (playing) {
            float target = input * climb;
            k[i].v += (target - k[i].v) * fminf(dt * accel, 1.0f);
            p[i].y += k[i].v * dt;
            if (p[i].y < min_y) {
                p[i].y = min_y;
                k[i].v = 0.0f;
            } else if (p[i].y > max_y) {
                p[i].y = max_y;
                k[i].v = 0.0f;
            }

            r[i].roll = sinf(g->time * 1.7f) * 0.07f - k[i].v * 0.025f;
            r[i].pitch = -0.25f + sinf(g->time * 2.3f) * 0.04f;
        } else if (p[i].y > floor_y) {
            k[i].v += YEON_CFG(it->world, "gravity") * dt;
            p[i].y = fmaxf(p[i].y + k[i].v * dt, floor_y);
            p[i].x -= 3.0f * dt;
            r[i].roll += k[i].spin * dt;
        }
    }
}

static void YeonInvulnerability(
    ecs_iter_t *it)
{
    YeonInvulnerable *inv = ecs_field(it, YeonInvulnerable, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        inv[i].time_left -= it->delta_time;
        if (inv[i].time_left <= 0.0f) {
            ecs_remove(it->stage, it->entities[i], YeonInvulnerable);
        }
    }
}

static void YeonRivalMove(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    YeonPose *r = ecs_field(it, YeonPose, 1);
    const YeonRival *rv = ecs_field(it, YeonRival, 2);
    const YeonGame *g = ecs_field(it, YeonGame, 3);
    float despawn_x = YEON_CFG(it->world, "despawnX");

    for (int32_t i = 0; i < it->count; i ++) {
        float a = g->time * rv[i].frequency + rv[i].phase;
        p[i].x -= g->speed * rv[i].drift * it->delta_time;
        p[i].y = rv[i].base_y + sinf(a) * rv[i].amplitude;
        r[i].roll = cosf(a) * 0.2f;
        r[i].pitch = -0.2f;
        if (p[i].x < despawn_x) {
            ecs_delete(it->stage, it->entities[i]);
        }
    }
}

static void YeonLanternMove(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    const YeonLantern *l = ecs_field(it, YeonLantern, 1);
    YeonGame *g = ecs_field(it, YeonGame, 2);
    const Position *kp = ecs_get(it->world, g->player, Position);
    float despawn_x = YEON_CFG(it->world, "despawnX");
    float reach = YEON_CFG(it->world, "lanternReach");
    int32_t speed_up_every = YEON_CFG_I(it->world, "speedUpLanterns");

    for (int32_t i = 0; i < it->count; i ++) {
        p[i].x -= g->speed * it->delta_time;
        p[i].y = l[i].base_y + sinf(g->time * 2.0f + l[i].phase) * 0.35f;

        float dx = p[i].x - kp->x, dy = p[i].y - kp->y;
        if (dx * dx + dy * dy < reach * reach) {
            box_particle_burst_at(it->world, g->sparkle, (vec3){ p[i].x, p[i].y, p[i].z }, 0.0f);
            ecs_delete(it->stage, it->entities[i]);
            g->lanterns ++;
            if (!(g->lanterns % speed_up_every)) {
                g->speed += YEON_CFG(it->world, "speedUp");
            }

            ecs_singleton_modified(it->stage, YeonGame);
        } else if (p[i].x < despawn_x) {
            ecs_delete(it->stage, it->entities[i]);
        }
    }
}

static void YeonSceneryScroll(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    const YeonScenery *s = ecs_field(it, YeonScenery, 1);
    const YeonGame *g = ecs_field(it, YeonGame, 2);

    for (int32_t i = 0; i < it->count; i ++) {
        p[i].x -= g->speed * s[i].parallax * it->delta_time;
        if (p[i].x < s[i].wrap_min) {
            p[i].x += s[i].wrap_span;
        }
    }
}

static ecs_entity_t yeon_spawn(
    ecs_world_t *world,
    const YeonGame *g,
    ecs_entity_t prefab,
    float y,
    float z)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, g->sky_parent);
    ecs_add(world, e, DynamicTransform);
    ecs_add_pair(world, e, EcsIsA, prefab);
    ecs_set(world, e, Position, { YEON_CFG(world, "spawnX"), y, z });

    return e;
}

static void YeonSpawn(
    ecs_iter_t *it)
{
    YeonGame *g = ecs_field(it, YeonGame, 0);
    float step = g->speed * it->delta_time;
    float min_y = YEON_CFG(it->world, "minY");
    float max_y = YEON_CFG(it->world, "maxY");

    g->time += it->delta_time;
    g->distance_since_rival += step;
    g->distance_since_lantern += step;

    if (g->distance_since_rival >= g->next_rival_distance) {
        float pick = yeon_random01(g);
        ecs_entity_t prefab = pick < 0.34f ? g->rival_red
            : pick < 0.67f ? g->rival_blue : g->rival_yellow;
        float amplitude = yeon_random_range(g, 0.4f, 2.2f);
        float base_y = yeon_random_range(g, min_y, max_y);
        ecs_entity_t e = yeon_spawn(it->stage, g, prefab, base_y, 0.0f);
        ecs_set(it->stage, e, YeonRival, {
            .base_y = base_y,
            .amplitude = amplitude,
            .frequency = yeon_random_range(g, 0.8f, 1.9f),
            .phase = yeon_random_range(g, 0.0f, 2.0f * GLM_PIf),
            .drift = yeon_random_range(g, 1.1f, 1.6f)
        });
        ecs_set(it->stage, e, YeonRope, {
            .anchor_dx = YEON_CFG(it->world, "rivalAnchorDx"),
            .anchor_y = YEON_CFG(it->world, "anchorY"),
            .attach_y = -0.1f,
            .slack = YEON_CFG(it->world, "ropeSlack"),
            .gravity = YEON_CFG(it->world, "ropeGravity")
        });
        g->distance_since_rival = 0.0f;
        g->next_rival_distance = yeon_random_range(g,
            YEON_CFG(it->world, "rivalGapMin"), YEON_CFG(it->world, "rivalGapMax"));
    }

    if (g->distance_since_lantern >= g->next_lantern_distance) {
        float base_y = yeon_random_range(g, min_y + 0.5f, max_y - 0.5f);
        ecs_entity_t e = yeon_spawn(it->stage, g, g->lantern, base_y, 0.0f);
        ecs_set(it->stage, e, YeonLantern, {
            .base_y = base_y,
            .phase = yeon_random_range(g, 0.0f, 2.0f * GLM_PIf)
        });
        g->distance_since_lantern = 0.0f;
        g->next_lantern_distance = yeon_random_range(g,
            YEON_CFG(it->world, "lanternGapMin"), YEON_CFG(it->world, "lanternGapMax"));
    }

    ecs_singleton_modified(it->stage, YeonGame);
}

static void yeon_hit(
    ecs_world_t *world,
    const YeonGame *g)
{
    const Position *p = ecs_get(world, g->player, Position);
    YeonLives *lives = ecs_get_mut(world, g->player, YeonLives);
    box_particle_burst_at(world, g->cut, (vec3){ p->x, p->y, p->z }, 0.0f);
    lives->lives_left --;
    ecs_modified(world, g->player, YeonLives);
    if (lives->lives_left <= 0) {
        ecs_add_pair(world, ecs_id(YeonGameState), ecs_id(YeonGameState),
            ecs_lookup_child(world, ecs_id(YeonGameState), "Over"));
        ecs_set(world, g->player, YeonKite, { .v = 4.0f, .spin = 2.5f });
        YeonRope *rope = ecs_get_mut(world, g->player, YeonRope);
        if (rope) {
            rope->cut = true;
            ecs_modified(world, g->player, YeonRope);
        }
    } else {
        ecs_set(world, g->player, YeonInvulnerable, { YEON_CFG(world, "invulnTime") });
    }
}

static void YeonCollision(
    ecs_iter_t *it)
{
    const YeonGame *g = ecs_singleton_get(it->world, YeonGame);
    const Position *kp = ecs_get(it->world, g->player, Position);
    float reach = YEON_CFG(it->world, "rivalReach");
    bool done = ecs_has(it->world, g->player, YeonInvulnerable);

    while (!done && ecs_iter_next(it)) {
        const Position *p = ecs_field(it, Position, 0);
        for (int32_t i = 0; i < it->count && !done; i ++) {
            float dx = p[i].x - kp->x, dy = p[i].y - kp->y;
            done = dx * dx + dy * dy < reach * reach;
        }

        if (done) {
            yeon_hit(it->stage, g);
        }
    }

    if (done) {
        ecs_iter_fini(it);
    }
}

static void yeon_rope_init(
    YeonRope *rope,
    float ax,
    float ay,
    float bx,
    float by)
{
    for (int32_t i = 0; i < YEON_ROPE_POINTS; i ++) {
        float t = (float)i / (float)(YEON_ROPE_POINTS - 1);
        rope->x[i] = rope->prev_x[i] = ax + (bx - ax) * t;
        rope->y[i] = rope->prev_y[i] = ay + (by - ay) * t;
    }

    rope->ready = true;
}

static void yeon_rope_step(
    YeonRope *rope,
    float time,
    float dt)
{
    int32_t first = rope->cut ? 0 : 1;
    for (int32_t i = first; i < YEON_ROPE_POINTS - 1; i ++) {
        float vx = (rope->x[i] - rope->prev_x[i]) * 0.985f;
        float vy = (rope->y[i] - rope->prev_y[i]) * 0.985f;
        float along = sinf(GLM_PIf * (float)i / (float)(YEON_ROPE_POINTS - 1));
        rope->prev_x[i] = rope->x[i];
        rope->prev_y[i] = rope->y[i];
        rope->x[i] += vx;
        rope->y[i] += vy + rope->gravity * dt * dt +
            sinf(time * 6.5f + (float)i * 0.9f) * 0.12f * along * dt;
    }
}

static void yeon_rope_relax(
    YeonRope *rope)
{
    for (int32_t iter = 0; iter < 24; iter ++) {
        for (int32_t i = 0; i < YEON_ROPE_POINTS - 1; i ++) {
            float w0 = (i == 0 && !rope->cut) ? 0.0f : 1.0f;
            float w1 = (i + 1 == YEON_ROPE_POINTS - 1) ? 0.0f : 1.0f;
            float dx = rope->x[i + 1] - rope->x[i];
            float dy = rope->y[i + 1] - rope->y[i];
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 1e-6f && w0 + w1 > 0.0f) {
                float f = (d - rope->segment) / (d * (w0 + w1));
                rope->x[i] += dx * f * w0;
                rope->y[i] += dy * f * w0;
                rope->x[i + 1] -= dx * f * w1;
                rope->y[i + 1] -= dy * f * w1;
            }
        }
    }
}

static void YeonRopeSim(
    ecs_iter_t *it)
{
    const Position *p = ecs_field(it, Position, 0);
    YeonRope *rope = ecs_field(it, YeonRope, 1);
    const YeonGame *g = ecs_field(it, YeonGame, 2);
    float dt = fminf(it->delta_time, 1.0f / 30.0f);
    int32_t last = YEON_ROPE_POINTS - 1;

    for (int32_t i = 0; i < it->count; i ++) {
        YeonRope *r = &rope[i];
        float ax = p[i].x, ay = p[i].y + r->attach_y;
        float bx = p[i].x + r->anchor_dx, by = r->anchor_y;
        if (!r->ready) {
            yeon_rope_init(r, ax, ay, bx, by);
        }

        if (!r->cut) {
            float dx = bx - ax, dy = by - ay;
            r->segment = sqrtf(dx * dx + dy * dy) * (1.0f + r->slack) / (float)last;
            r->x[0] = r->prev_x[0] = ax;
            r->y[0] = r->prev_y[0] = ay;
        }

        r->x[last] = r->prev_x[last] = bx;
        r->y[last] = r->prev_y[last] = by;

        if (dt > 0.0f) {
            yeon_rope_step(r, g->time, dt);
            yeon_rope_relax(r);
        }
    }
}

static void YeonRopeSegments(
    ecs_iter_t *it)
{
    const YeonRopeSegment *s = ecs_field(it, YeonRopeSegment, 0);
    Position *p = ecs_field(it, Position, 1);
    Rotation *r = ecs_field(it, Rotation, 2);
    Scale *sc = ecs_field(it, Scale, 3);
    const YeonRope *rope = ecs_field(it, YeonRope, 4);
    const Position *root = ecs_field(it, Position, 5);

    for (int32_t i = 0; i < it->count; i ++) {
        int32_t a = glm_clamp(s[i].index, 0, YEON_ROPE_POINTS - 2);
        float dx = rope->x[a + 1] - rope->x[a];
        float dy = rope->y[a + 1] - rope->y[a];
        p[i].x = 0.5f * (rope->x[a] + rope->x[a + 1]) - root->x;
        p[i].y = 0.5f * (rope->y[a] + rope->y[a + 1]) - root->y;
        p[i].z = 0.06f;
        r[i] = (Rotation){ 0.0f, 0.0f, atan2f(-dx, dy) };
        sc[i] = (Scale){ 1.0f, sqrtf(dx * dx + dy * dy) + 0.02f, 1.0f };
    }
}

static void YeonBodyPose(
    ecs_iter_t *it)
{
    const YeonBody *b = ecs_field(it, YeonBody, 0);
    Rotation *r = ecs_field(it, Rotation, 1);
    const YeonPose *pose = ecs_field(it, YeonPose, 2);

    for (int32_t i = 0; i < it->count; i ++) {
        r[i] = (Rotation){ pose->pitch, 0.0f, pose->roll * b[i].roll_scale };
    }
}

static void YeonDrift(
    ecs_iter_t *it)
{
    const YeonGame *g = ecs_field(it, YeonGame, 0);
    float drift = ecs_field_is_set(it, 1) ? -g->speed : 0.0f;
    const ParticleForces *f = ecs_get(it->world, g->effect_drift, ParticleForces);
    if (f->drift[0] != drift) {
        ParticleForces forces = *f;
        forces.drift[0] = drift;
        ecs_set_ptr(it->stage, g->effect_drift, ParticleForces, &forces);
    }
}

static void yeon_resolve(
    ecs_script_future_t *future,
    bool value)
{
    ecs_script_future_resolve(future, &(ecs_value_t){
        .type = ecs_id(ecs_bool_t), .ptr = &value });
    ecs_script_future_release(future);
}

static void yeon_restart(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t parent = ecs_singleton_get(world, YeonGame)->sky_parent;
    bool result = yeon_state_is(world, "Over");
    (void)argc;
    (void)argv;

    if (result) {
        ecs_entity_t script = ecs_get_target(world, parent, ecs_id(EcsScript), 0);
        result = script && !ecs_script_reload(world, script, NULL);
    }

    yeon_resolve(future, result);
}

void YeonImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, Yeon);
    ECS_IMPORT(world, Box);

    ECS_META_COMPONENT(world, YeonGameState);
    ECS_META_COMPONENT(world, YeonGame);
    ECS_META_COMPONENT(world, YeonControls);
    ECS_META_COMPONENT(world, YeonKite);
    ECS_META_COMPONENT(world, YeonPose);
    ECS_META_COMPONENT(world, YeonBody);
    ECS_META_COMPONENT(world, YeonRope);
    ECS_META_COMPONENT(world, YeonRopeSegment);
    ECS_META_COMPONENT(world, YeonLives);
    ECS_META_COMPONENT(world, YeonInvulnerable);
    ECS_META_COMPONENT(world, YeonRival);
    ECS_META_COMPONENT(world, YeonLantern);
    ECS_META_COMPONENT(world, YeonScenery);

    ecs_add_id(world, ecs_id(YeonGameState), EcsExclusive);
    ecs_add_id(world, ecs_id(YeonGameState), EcsPairIsTag);
    ecs_add_pair(world, ecs_id(YeonKite), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(YeonKite), EcsWith, ecs_id(YeonPose));
    ecs_add_pair(world, ecs_id(YeonKite), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(YeonRival), EcsWith, ecs_id(YeonPose));
    ecs_add_pair(world, ecs_id(YeonBody), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(YeonBody), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(YeonRopeSegment), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(YeonRopeSegment), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(YeonRopeSegment), EcsWith, ecs_id(Scale));
    ecs_add_pair(world, ecs_id(YeonRopeSegment), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(YeonRival), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(YeonLantern), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(YeonScenery), EcsWith, DynamicTransform);

    ecs_entity_t state = ecs_id(YeonGameState), game = ecs_id(YeonGame);
    ecs_entity_t controls = ecs_id(YeonControls);
    ecs_id_t playing = ecs_pair(state, ecs_lookup_child(world, state, "Playing"));

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "YeonFlight" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position) }, { .id = ecs_id(YeonPose) }, { .id = ecs_id(YeonKite) },
            { .id = game, .src.id = game, .inout = EcsIn },
            { .id = controls, .src.id = controls, .inout = EcsIn, .oper = EcsOptional },
            { .id = playing, .src.id = state, .oper = EcsOptional }
        },
        .callback = YeonFlight
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "YeonInvulnerability" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(YeonInvulnerable) }, { .id = playing, .src.id = state }
        },
        .callback = YeonInvulnerability
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "YeonRivalMove" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ecs_id(YeonPose), .src.id = EcsSelf },
            { .id = ecs_id(YeonRival), .src.id = EcsSelf, .inout = EcsIn },
            { .id = game, .src.id = game, .inout = EcsIn }, { .id = playing, .src.id = state }
        },
        .callback = YeonRivalMove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "YeonLanternMove" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ecs_id(YeonLantern), .src.id = EcsSelf, .inout = EcsIn },
            { .id = game, .src.id = game, .inout = EcsInOut }, { .id = playing, .src.id = state }
        },
        .callback = YeonLanternMove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "YeonSceneryScroll" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ecs_id(YeonScenery), .src.id = EcsSelf, .inout = EcsIn },
            { .id = game, .src.id = game, .inout = EcsIn }, { .id = playing, .src.id = state }
        },
        .callback = YeonSceneryScroll
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "YeonSpawn" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = game, .src.id = game, .inout = EcsInOut }, { .id = playing, .src.id = state }
        },
        .callback = YeonSpawn
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "YeonCollision" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(YeonRival), .src.id = EcsSelf, .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .run = YeonCollision
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "YeonRopeSim" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(YeonRope), .src.id = EcsSelf },
            { .id = game, .src.id = game, .inout = EcsIn }
        },
        .callback = YeonRopeSim
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "YeonRopeSegments" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(YeonRopeSegment), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ecs_id(Rotation), .src.id = EcsSelf },
            { .id = ecs_id(Scale), .src.id = EcsSelf },
            { .id = ecs_id(YeonRope), .src.id = EcsUp, .trav = EcsChildOf, .inout = EcsIn },
            { .id = ecs_id(Position), .src.id = EcsUp, .trav = EcsChildOf, .inout = EcsIn }
        },
        .callback = YeonRopeSegments
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "YeonBodyPose" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(YeonBody), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(Rotation), .src.id = EcsSelf },
            { .id = ecs_id(YeonPose), .src.id = EcsUp, .trav = EcsChildOf, .inout = EcsIn }
        },
        .callback = YeonBodyPose
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "YeonDrift" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = game, .src.id = game, .inout = EcsIn },
            { .id = playing, .src.id = state, .oper = EcsOptional }
        },
        .callback = YeonDrift
    });

    ecs_async_function(world, {
        .name = "restart",
        .parent = ecs_id(Yeon),
        .return_type = ecs_id(ecs_bool_t),
        .callback = yeon_restart
    });
}
