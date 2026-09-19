/**
 * @file sky/sky.c
 * @brief Registers the box.sky module and the shaders its passes are built from.
 */

#define BOX_SKY_IMPL
#include "sky_internal.h"

ECS_COMPONENT_DECLARE(SkyState);

/** Create one shader entity of the module under box.sky.shaders.
 *
 * @param world The world
 * @param shaders Scope the Shader entities live in
 * @param name Name of the entity
 * @param path WGSL file of the shader
 * @return The shader entity
 */
static ecs_entity_t box_sky_shader(
    ecs_world_t *world,
    ecs_entity_t shaders,
    const char *name,
    const char *path)
{
    ecs_entity_t result = ecs_entity(world, { .name = name, .parent = shaders });
    Shader *source = ecs_ensure(world, result, Shader);
    source->path = ecs_os_strdup(path);
    source->macros = NULL;
    ecs_modified(world, result, Shader);

    return result;
}

/** Create the shaders of the module the first time a world gets an atmosphere.
 *
 * A world without one never compiles them, which is why the packet of a scene
 * that has no sky is the packet it produced before the module existed.
 *
 * @param it The observer iterator
 */
static void box_sky_on_atmosphere(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    SkyState *state = ecs_singleton_ensure(world, SkyState);
    if (state->lut_shader) {
        goto done;
    }

    ecs_entity_t shaders = ecs_lookup(world, "box.sky.shaders");
    ecs_entity_t scope = ecs_set_scope(world, 0);
    state->lut_shader = box_sky_shader(world, shaders, "sky_lut",
        "etc/shaders/sky_lut.wgsl");
    state->sky_shader = box_sky_shader(world, shaders, "sky", "etc/shaders/sky.wgsl");
    ecs_set_scope(world, scope);
    ecs_singleton_modified(world, SkyState);
done:
    return;
}

ecs_entity_t box_sky_entity(
    ecs_world_t *world)
{
    ecs_entity_t result = 0;
    const SkyState *state = ecs_singleton_get(world, SkyState);
    if (!state || !state->atmospheres) {
        goto done;
    }

    ecs_iter_t it = ecs_query_iter(world, state->atmospheres);
    if (ecs_query_next(&it)) {
        result = it.count ? it.entities[0] : 0;
        ecs_iter_fini(&it);
    }

done:
    return result;
}

void BoxSkyImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxSky);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxShader);

    ECS_META_COMPONENT(world, AtmosphereRayleigh);
    ECS_META_COMPONENT(world, AtmosphereMie);
    ECS_META_COMPONENT(world, Atmosphere);
    ECS_META_COMPONENT(world, Stars);
    ECS_META_COMPONENT(world, TimeOfDay);
    ECS_META_COMPONENT(world, WeatherAmounts);
    ECS_META_COMPONENT(world, Weather);
    ECS_COMPONENT_DEFINE(world, WeatherBlend);
    ECS_COMPONENT_DEFINE(world, SkyState);

    ecs_add_pair(world, ecs_id(Stars), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(Weather), EcsWith, ecs_id(WeatherBlend));
    ecs_set_hooks(world, Atmosphere, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, Stars, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, TimeOfDay, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, Weather, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, WeatherBlend, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, SkyState, { .ctor = flecs_default_ctor });

    ecs_query_t *query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "query" }),
        .terms = {{ .id = ecs_id(Atmosphere), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_query_t *weathers = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "weather_query" }),
        .terms = {{ .id = ecs_id(Weather), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_entity(world, { .name = "shaders" });
    SkyState *state = ecs_singleton_ensure(world, SkyState);
    state->atmospheres = query;
    state->weathers = weathers;
    ecs_singleton_modified(world, SkyState);

    box_sky_time_init(world);
    box_sky_weather_init(world);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Atmosphere) }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnAdd },
        .callback = box_sky_on_atmosphere
    });
}
