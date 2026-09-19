/**
 * @file Rain.c
 * @brief Tests for the column of drops the rain of a weather drives.
 */

#include <box.h>
#include <box/sky.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/particle/particle_internal.h"
#include "../../../src/render/render_internal.h"

void Rain_a_world_without_rain_keeps_its_emitter_unmade(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, Weather, {.overcast = 1});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);

    test_uint(ecs_lookup(w, "box.sky.rain"), 0);

    ecs_fini(w);
}

void Rain_a_world_without_a_camera_keeps_its_emitter_unmade(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_singleton_set(w, Weather, {.rain = 1});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);

    test_uint(ecs_lookup(w, "box.sky.rain"), 0);

    ecs_fini(w);
}

void Rain_the_rain_makes_an_emitter_over_the_camera(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {3, 6, -4});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, Weather, {.rain = 1});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);
    ecs_entity_t e = ecs_lookup(w, "box.sky.rain");
    const Position *p = e ? ecs_get(w, e, Position) : NULL;
    const ParticleEmitter *emitter = e ? ecs_get(w, e, ParticleEmitter) : NULL;

    test_assert(e != 0);
    test_assert(p != NULL);
    test_assert(emitter != NULL);
    test_flt(p->x, 3);
    test_flt(p->z, -4);
    test_assert(p->y > 6);
    test_assert(emitter->rate > 0);

    ecs_fini(w);
}

void Rain_the_emitter_follows_the_camera(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_add(w, cam, DynamicTransform);
    ecs_singleton_set(w, Weather, {.rain = 1});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);
    ecs_set(w, cam, Position, {40, 2, 12});
    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);
    ecs_entity_t e = ecs_lookup(w, "box.sky.rain");
    const Position *p = ecs_get(w, e, Position);

    test_flt(p->x, 40);
    test_flt(p->z, 12);

    ecs_fini(w);
}

void Rain_the_rate_follows_the_intensity(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, Rain, {.rate = 1000});
    ecs_singleton_set(w, Weather, {.rain = 0.25f});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);
    ecs_entity_t e = ecs_lookup(w, "box.sky.rain");
    float light = ecs_get(w, e, ParticleEmitter)->rate;

    ecs_singleton_set(w, Weather, {.rain = 1, .transition = 0.0001f});
    ecs_progress(w, 1.0f);
    ecs_progress(w, 1.0f);
    float heavy = ecs_get(w, e, ParticleEmitter)->rate;

    test_flt(light, 250);
    test_flt(heavy, 1000);

    ecs_fini(w);
}

void Rain_the_drop_size_follows_the_intensity(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, Rain, {.size = 1});
    ecs_singleton_set(w, Weather, {.rain = 0.25f});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);
    ecs_entity_t e = ecs_lookup(w, "box.sky.rain");
    float light = ecs_get(w, e, ParticleEmitter)->size.min;

    ecs_singleton_set(w, Weather, {.rain = 1, .transition = 0.0001f});
    ecs_progress(w, 1.0f);
    ecs_progress(w, 1.0f);
    const ParticleEmitter *emitter = ecs_get(w, e, ParticleEmitter);

    test_assert(light < emitter->size.min);
    test_assert(light > BOX_RAIN_SIZE_LOW * 0.99f);
    test_flt(emitter->size.min, 1);
    test_flt(emitter->size.max, emitter->size.min);

    ecs_fini(w);
}

void Rain_the_wind_follows_the_intensity(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, Rain, {.wind = {8, 0, -4}, .drag = 2});
    ecs_singleton_set(w, Weather, {.rain = 0.5f});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);
    ecs_entity_t e = ecs_lookup(w, "box.sky.rain");
    const ParticleForces *forces = ecs_get(w, e, ParticleForces);

    test_flt(forces->wind.x, 4);
    test_flt(forces->wind.z, -2);
    test_flt(forces->drag, 2);
    test_assert(forces->gravity.y < 0);

    ecs_fini(w);
}

void Rain_the_drops_die_on_the_ground(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, Rain, {.ground = 1.5f});
    ecs_singleton_set(w, Weather, {.rain = 1});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);
    ecs_entity_t e = ecs_lookup(w, "box.sky.rain");
    const ParticleCollision *collision = ecs_get(w, e, ParticleCollision);

    test_bool(collision->ground, true);
    test_bool(collision->die, true);
    test_flt(collision->height, 1.5f);

    ecs_fini(w);
}

void Rain_the_drops_are_drawn_as_streaks(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, Rain, {.stretch = 0.2f});
    ecs_singleton_set(w, Weather, {.rain = 1});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    ecs_entity_t e = ecs_lookup(w, "box.sky.rain");
    const box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);

    test_flt(ecs_get(w, e, ParticleAppearance)->stretch, 0.2f);
    test_int(packet.particles.count, 1);
    test_flt(block->stretch, 0.2f);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Rain_the_column_takes_the_shape_of_the_rain(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 5, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, Rain, {.max = 4000, .area = 30, .height = 20});
    ecs_singleton_set(w, Weather, {.rain = 1});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);
    ecs_entity_t e = ecs_lookup(w, "box.sky.rain");
    const ParticleEmitter *emitter = ecs_get(w, e, ParticleEmitter);
    const Position *p = ecs_get(w, e, Position);

    test_int(emitter->shape, EmitterBox);
    test_int(emitter->max, 4000);
    test_flt(emitter->extents.x, 30);
    test_flt(emitter->extents.y, 10);
    test_flt(emitter->extents.z, 30);
    test_flt(emitter->direction.y, -1);
    test_flt(p->y, 5.0f + 20.0f * BOX_RAIN_LIFT);

    ecs_fini(w);
}

void Rain_a_rain_that_clears_stops_the_emitter(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, Weather, {.rain = 1});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);
    ecs_entity_t e = ecs_lookup(w, "box.sky.rain");
    test_int(ecs_get(w, e, ParticlePlayback)->state, ParticlePlaying);

    ecs_singleton_set(w, Weather, {.rain = 0, .transition = 0.0001f});
    ecs_progress(w, 1.0f);
    ecs_progress(w, 1.0f);
    const ParticleEmitter *emitter = ecs_get(w, e, ParticleEmitter);

    test_int(ecs_get(w, e, ParticlePlayback)->state, ParticleStopped);
    test_flt(emitter->rate, 0);

    ecs_fini(w);
}

void Rain_the_first_frame_of_a_rain_fills_the_column(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, Rain, {.max = 8000, .rate = 4000});
    ecs_singleton_set(w, Weather, {.rain = 1});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);
    ecs_entity_t e = ecs_lookup(w, "box.sky.rain");
    int32_t count = ecs_get(w, e, ParticleBurst)->count;
    box_particle_extract(w, &packet);
    const box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);

    test_assert(count > 1000);
    test_assert(count <= 8000);
    test_assert(block->spawn >= (uint32_t)count);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Rain_the_rain_raises_the_overcast_of_the_sky(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {1.0f, 0, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t atmosphere = ecs_new(w);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun});

    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    float clear = packet.sky.night[3];

    box_packet_reset(&packet);
    ecs_singleton_set(w, Weather, {.rain = 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    test_flt(clear, 0);
    test_flt(packet.sky.night[3], BOX_WEATHER_RAIN_OVERCAST);

    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Rain_the_rain_raises_the_fog_of_the_air(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {1.0f, 0, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t atmosphere = ecs_new(w);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun});
    ecs_entity_t fog = ecs_lookup(w, "box.effects.stack.fog");
    ecs_get_mut(w, fog, Effect)->enabled = true;
    ecs_modified(w, fog, Effect);

    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    float clear = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];

    box_packet_reset(&packet);
    ecs_singleton_set(w, Weather, {.rain = 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    float wet = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];
    float expect = clear * (1.0f + BOX_WEATHER_RAIN_FOG * (BOX_WEATHER_FOG_GAIN - 1.0f));

    test_assert(clear > 0.0f);
    test_assert(fabsf(wet - expect) < clear * 0.01f);

    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Rain_the_rain_wets_the_material_layers(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t wetness = ecs_lookup(w, "box.material.layers.wetness");
    ecs_singleton_set(w, Weather, {.rain = 0.75f});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);

    test_flt(ecs_get(w, wetness, LayerParams)->a.w, 0.75f);

    ecs_fini(w);
}

void Rain_the_weather_keeps_the_amounts_it_was_asked_for(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxParticle);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_singleton_set(w, Weather, {.rain = 1});

    ecs_progress(w, 1.0f / 60.0f);
    const Weather *weather = ecs_singleton_get(w, Weather);

    test_flt(weather->current.rain, 1);
    test_flt(weather->current.overcast, 0);
    test_flt(weather->current.fog, 0);

    ecs_fini(w);
}

void Rain_a_world_without_the_particle_module_still_wets_and_greys(void)
{
    // particles/rain
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t wetness = ecs_lookup(w, "box.material.layers.wetness");
    ecs_singleton_set(w, Weather, {.rain = 1});

    ecs_progress(w, 1.0f / 60.0f);
    ecs_progress(w, 1.0f / 60.0f);

    test_uint(ecs_lookup(w, "box.sky.rain"), 0);
    test_flt(ecs_get(w, wetness, LayerParams)->a.w, 1);
    test_flt(ecs_singleton_get(w, Weather)->current.rain, 1);

    ecs_fini(w);
}
