/**
 * @file particle/particle.c
 * @brief Registers the box.particle module and the shaders its passes are built from.
 *
 * The shaders are ordinary Shader entities under box.particle.shaders, created
 * the first time a world gets an emitter, so a scene without particles compiles
 * nothing and sends nothing.
 */

#define BOX_PARTICLE_IMPL
#include "particle_internal.h"

ECS_COMPONENT_DECLARE(ParticleEmitterImpl);
ECS_COMPONENT_DECLARE(ParticleState);

/** Particles an emitter keeps alive when the author names no maximum */
#define BOX_PARTICLE_DEFAULT_MAX (1000)

/** Particles an emitter spawns per second when the author names no rate */
#define BOX_PARTICLE_DEFAULT_RATE (100.0f)

/** Size in world units a particle starts at when the author names none */
#define BOX_PARTICLE_DEFAULT_SIZE (0.1f)

/** Half angle in degrees of the cone an emitter spreads over by default */
#define BOX_PARTICLE_DEFAULT_SPREAD (15.0f)

/** Emitter constructor: a white stream of a thousand particles leaving upwards.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_particle_emitter_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ParticleEmitter *e = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        memset(&e[i], 0, sizeof(ParticleEmitter));
        e[i].max = BOX_PARTICLE_DEFAULT_MAX;
        e[i].rate = BOX_PARTICLE_DEFAULT_RATE;
        e[i].shape = EmitterPoint;
        e[i].radius = 1.0f;
        e[i].extents = vec3_new(0.5f, 0.5f, 0.5f);
        e[i].direction = vec3_new(0, 1, 0);
        e[i].spread = BOX_PARTICLE_DEFAULT_SPREAD;
        e[i].speed.min = 1.0f;
        e[i].speed.max = 2.0f;
        e[i].lifetime.min = 1.0f;
        e[i].lifetime.max = 2.0f;
        e[i].size.min = BOX_PARTICLE_DEFAULT_SIZE;
        e[i].size.max = BOX_PARTICLE_DEFAULT_SIZE;
        e[i].opacity.min = 1.0f;
        e[i].opacity.max = 1.0f;
        e[i].color.r = 255;
        e[i].color.g = 255;
        e[i].color.b = 255;
        e[i].seed = 1;
    }
}

/** Create one shader entity of the module under box.particle.shaders.
 *
 * @param world The world
 * @param shaders Scope the Shader entities live in
 * @param name Name of the entity
 * @param path WGSL file of the shader
 * @return The shader entity
 */
static ecs_entity_t box_particle_shader(
    ecs_world_t *world,
    ecs_entity_t shaders,
    const char *name,
    const char *path)
{
    char macros[64];
    ecs_os_snprintf(macros, sizeof(macros), "PARTICLE_GROUP=%d", BOX_PARTICLE_GROUP);
    ecs_entity_t result = ecs_entity(world, { .name = name, .parent = shaders });
    Shader *source = ecs_ensure(world, result, Shader);
    source->path = ecs_os_strdup(path);
    source->macros = ecs_os_strdup(macros);
    ecs_modified(world, result, Shader);

    return result;
}

/** Create the shaders of the module the first time a world gets an emitter.
 *
 * @param it The observer iterator
 */
static void box_particle_on_emitter(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    ParticleState *state = ecs_singleton_ensure(world, ParticleState);
    if (state->sim_shader) {
        goto done;
    }

    ecs_entity_t shaders = ecs_lookup(world, "box.particle.shaders");
    ecs_entity_t scope = ecs_set_scope(world, 0);
    state->sim_shader = box_particle_shader(world, shaders, "particle_sim",
        "etc/shaders/particle_sim.wgsl");
    state->draw_shader = box_particle_shader(world, shaders, "particle_draw",
        "etc/shaders/particle_draw.wgsl");
    ecs_set_scope(world, scope);
    ecs_singleton_modified(world, ParticleState);
done:
    return;
}

void BoxParticleImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxParticle);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxSpline);
    ECS_IMPORT(world, BoxShader);

    ECS_META_COMPONENT(world, EmitterShape);
    ECS_META_COMPONENT(world, ParticleRange);
    ECS_META_COMPONENT(world, ParticleEmitter);
    ECS_COMPONENT_DEFINE(world, ParticleEmitterImpl);
    ECS_COMPONENT_DEFINE(world, ParticleState);

    ecs_set_hooks(world, ParticleEmitter, { .ctor = box_particle_emitter_ctor });
    ecs_set_hooks(world, ParticleEmitterImpl, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, ParticleState, { .ctor = flecs_default_ctor });
    ecs_add_pair(world, ecs_id(ParticleEmitter), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(ParticleEmitter), EcsWith, ecs_id(ParticleEmitterImpl));
    ecs_add_pair(world, ecs_id(ParticleEmitterImpl), EcsOnInstantiate, EcsDontInherit);

    ecs_entity(world, { .name = "shaders" });
    ecs_query_t *query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "query" }),
        .terms = {
            { .id = ecs_id(ParticleEmitter), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            { .id = ecs_id(ParticleEmitterImpl), .inout = EcsInOut }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ParticleState *state = ecs_singleton_ensure(world, ParticleState);
    state->emitters = query;
    ecs_singleton_modified(world, ParticleState);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(ParticleEmitter) }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnAdd },
        .callback = box_particle_on_emitter
    });
}
