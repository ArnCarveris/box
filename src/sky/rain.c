/**
 * @file sky/rain.c
 * @brief Drives the column of drops the rain of a weather falls as.
 *
 * The rain of a world is an amount, not an entity: the first frame a weather
 * rains this file creates one particle emitter under the sky module and drives
 * it from then on. The emitter is a box of drops that follows the camera of the
 * first view, so a world of any size rains with one stream that is never larger
 * than what the camera can see, the drops are stretched along the way they fall
 * and they die on the ground instead of passing through it.
 *
 * A drop is spawned at the speed it falls at and at the slant the wind gives
 * it, and the gravity of the emitter is the one that holds it there against the
 * drag, so a drop leaves the cloud at its terminal velocity instead of picking
 * it up over the first second of its life and the wind bends the whole column
 * rather than the part of it that has been falling for a while.
 *
 * Everything the rain reaches is driven from the blended amount rather than
 * from the target of the weather, so a rain that is asked for arrives over the
 * transition of that weather: the rate, the size of a drop and the wind rise
 * with it, and the overcast and the fog of the world rise with it in the
 * weather blend itself.
 *
 * A rain that starts fills its column at once with a burst instead of letting
 * the first drops fall in from the top, which is what the playback of the
 * emitter is for: it is stopped while the world is dry and starting it again
 * rewinds the clock the burst goes off on. The burst is what the column holds
 * once the rain runs, which is the rate times the time a drop spawned anywhere
 * in the column takes to leave it, so the first frame of a rain looks like the
 * hundredth.
 *
 * The module neither imports box.particle nor is imported by it. The emitter is
 * only created when the world holds the component of one, so a world without
 * the particle module keeps raining on its materials and its sky and misses
 * nothing else.
 */

#include "sky_internal.h"
#include <box/particle.h>
#include <math.h>

/** Path of the component that tells the rain the particle module is present */
#define BOX_RAIN_COMPONENT "box.particle.ParticleEmitter"

/** Name the emitter of the rain of a world is created under */
#define BOX_RAIN_NAME "rain"

/** Read the rain of a world, or the defaults when it holds none.
 *
 * @param world The world
 * @param out The component to fill
 */
static void box_rain_params(
    ecs_world_t *world,
    Rain *out)
{
    const Rain *rain = ecs_singleton_get(world, Rain);
    if (rain) {
        *out = *rain;
    } else {
        memset(out, 0, sizeof(Rain));
    }

    out->max = out->max > 0 ? out->max : BOX_RAIN_MAX;
    out->rate = box_sky_or(out->rate, BOX_RAIN_RATE);
    out->area = box_sky_or(out->area, BOX_RAIN_AREA);
    out->height = box_sky_or(out->height, BOX_RAIN_HEIGHT);
    out->speed = box_sky_or(out->speed, BOX_RAIN_SPEED);
    out->size = box_sky_or(out->size, BOX_RAIN_SIZE);
    out->stretch = box_sky_or(out->stretch, BOX_RAIN_STRETCH);
    out->drag = box_sky_or(out->drag, BOX_RAIN_DRAG);
    out->opacity = box_sky_or(out->opacity, BOX_RAIN_OPACITY);
    if (!out->color.r && !out->color.g && !out->color.b) {
        out->color.r = BOX_RAIN_COLOR;
        out->color.g = BOX_RAIN_COLOR;
        out->color.b = BOX_RAIN_COLOR;
    }
}

/** Find the camera the column of the rain of a world follows.
 *
 * @param world The world
 * @param query The query over the cameras of the world
 * @param out Receives the position of the first camera
 * @return true when the world holds a camera with a transform
 */
static bool box_rain_eye(
    ecs_world_t *world,
    ecs_query_t *query,
    vec3 *out)
{
    bool result = false;
    if (!query) {
        goto done;
    }

    ecs_iter_t it = ecs_query_iter(world, query);
    if (ecs_query_next(&it)) {
        const WorldTransform *m = ecs_field(&it, WorldTransform, 0);
        if (it.count) {
            *out = mat4_get_translation(&m[0].m);
            result = true;
        }

        ecs_iter_fini(&it);
    }

done:
    return result;
}

/** Create the emitter of the rain of a world, stopped and holding no drops.
 *
 * The entity is created on the stage, so it is reachable from the frame after
 * this one and the frame that created it leaves it alone. It starts stopped
 * because the frame that plays it is the frame that rewinds its burst.
 *
 * @param stage The stage of the frame
 * @param world The world the module entity is looked up in
 * @return The emitter
 */
static ecs_entity_t box_rain_create(
    ecs_world_t *stage,
    ecs_world_t *world)
{
    ecs_entity_t result = ecs_entity(stage, {
        .name = BOX_RAIN_NAME,
        .parent = ecs_lookup(world, "box.sky")
    });

    ecs_set(stage, result, Position, {0, 0, 0});
    ecs_add(stage, result, DynamicTransform);
    ecs_set(stage, result, ParticleEmitter, {.rate = 0});
    ecs_set(stage, result, ParticleForces, {.drag = 0});
    ecs_set(stage, result, ParticleCollision, {.ground = true, .die = true});
    ecs_set(stage, result, ParticleAppearance, {.shape = ParticleCircle});
    ecs_set(stage, result, ParticlePlayback, {.state = ParticleStopped});
    ecs_set(stage, result, ParticleBurst, {.count = 0});

    return result;
}

/** Seconds a drop lives, which is the fall from the top of the column.
 *
 * A drop that reaches the ground dies on it, so the life only has to outlast
 * that fall; the margin covers the drop the wind blew sideways and the one the
 * drag has not brought up to its terminal speed yet.
 *
 * @param rain The rain of the world
 * @param top World height the column reaches to
 * @return The life in seconds
 */
static float box_rain_life(
    const Rain *rain,
    float top)
{
    float fall = top - rain->ground;
    if (fall < rain->height) {
        fall = rain->height;
    }

    return fall / rain->speed * BOX_RAIN_LIFE_MARGIN;
}

/** Drive the emitter of the rain of a world from the amount that falls on it.
 *
 * The pool keeps the size it was created with, because a pool that changes size
 * is emptied, so a rain that thins spawns fewer drops rather than holding fewer
 * of them. A world that dries up stops the emitter, which ends the emission and
 * leaves the drops that are in the air to reach the ground.
 *
 * @param world The world
 * @param e The emitter
 * @param rain The rain of the world
 * @param amount Part of the rain that falls this frame, 0 to 1
 * @param eye Position of the camera the column follows
 */
static void box_rain_drive(
    ecs_world_t *world,
    ecs_entity_t e,
    const Rain *rain,
    float amount,
    vec3 eye)
{
    Position *p = ecs_get_mut(world, e, Position);
    ParticleEmitter *emitter = ecs_get_mut(world, e, ParticleEmitter);
    ParticleForces *forces = ecs_get_mut(world, e, ParticleForces);
    ParticleCollision *collision = ecs_get_mut(world, e, ParticleCollision);
    ParticleAppearance *look = ecs_get_mut(world, e, ParticleAppearance);
    ParticlePlayback *play = ecs_get_mut(world, e, ParticlePlayback);
    ParticleBurst *burst = ecs_get_mut(world, e, ParticleBurst);
    if (!p || !emitter || !forces || !collision || !look || !play || !burst) {
        goto done;
    }

    float half = rain->height * 0.5f;
    float lift = rain->height * BOX_RAIN_LIFT;
    float size = rain->size * (BOX_RAIN_SIZE_LOW + (1.0f - BOX_RAIN_SIZE_LOW) * amount);
    float life = box_rain_life(rain, eye.y + lift + half);
    *p = (Position){ eye.x, eye.y + lift, eye.z };
    emitter->max = rain->max;
    emitter->rate = rain->rate * amount;
    emitter->shape = EmitterBox;
    emitter->extents = vec3_new(rain->area, half, rain->area);
    vec3 fall = vec3_new(rain->wind.x * amount, -rain->speed, rain->wind.z * amount);
    float terminal = vec3_length(fall);
    emitter->direction = vec3_normalize(fall);
    emitter->spread = 0;
    emitter->speed = (ParticleRange){ terminal, terminal };
    emitter->lifetime = (ParticleRange){ life, life };
    emitter->size = (ParticleRange){ size, size };
    emitter->opacity = (ParticleRange){ rain->opacity, rain->opacity };
    emitter->color = rain->color;
    emitter->seed = BOX_RAIN_SEED;
    forces->gravity = vec3_new(0, -rain->speed * rain->drag, 0);
    forces->wind = vec3_scale(rain->wind, amount);
    forces->drag = rain->drag;
    collision->height = rain->ground;
    look->stretch = rain->stretch;
    play->state = amount < BOX_RAIN_EPSILON ? ParticleStopped : ParticlePlaying;
    burst->count = (int32_t)(emitter->rate * rain->height / (2.0f * rain->speed));
    if (burst->count > rain->max) {
        burst->count = rain->max;
    }

done:
    return;
}

/** Create and drive the rain emitter of a world that rains.
 *
 * @param it The system iterator
 */
static void BoxSkyRainUpdate(
    ecs_iter_t *it)
{
    const Weather *w = ecs_field(it, Weather, 0);
    SkyState *state = ecs_get_mut(it->world, ecs_id(SkyState), SkyState);
    float amount = it->count ? w[0].current.rain : 0.0f;
    vec3 eye;
    if (!state || !ecs_lookup(it->world, BOX_RAIN_COMPONENT)) {
        goto done;
    }

    if (state->rain && !ecs_is_alive(it->world, state->rain)) {
        state->rain = 0;
    }

    if (!state->rain && amount < BOX_RAIN_EPSILON) {
        goto done;
    }

    if (!box_rain_eye(it->world, state->cameras, &eye)) {
        goto done;
    }

    if (!state->rain) {
        state->rain = box_rain_create(it->stage, it->world);
        goto done;
    }

    Rain rain;
    box_rain_params(it->world, &rain);
    box_rain_drive(it->world, state->rain, &rain, amount, eye);
done:
    return;
}

void box_sky_rain_init(
    ecs_world_t *world)
{
    ecs_query_t *cameras = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "camera_query" }),
        .terms = {
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            { .id = ecs_id(Camera), .inout = EcsInOutNone }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    SkyState *state = ecs_singleton_ensure(world, SkyState);
    state->cameras = cameras;
    ecs_singleton_modified(world, SkyState);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxSkyRainUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {{ .id = ecs_id(Weather), .inout = EcsIn }},
        .callback = BoxSkyRainUpdate
    });
}
