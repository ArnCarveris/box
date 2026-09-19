/**
 * @file Appearance.c
 * @brief Tests for the curves of an emitter and for how its particles are drawn.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/particle/particle_internal.h"
#include "../../../src/particle/simulate.h"

void Appearance_an_emitter_without_curves_writes_constants(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    int32_t i;
    for (i = 0; i < BOX_PARTICLE_KEYS; i ++) {
        test_flt(block->size_curve[i], 1);
        test_flt(block->opacity_curve[i], 1);
        test_flt(block->turn_curve[i], 0);
        test_flt(block->gradient[i][0], 1);
        test_flt(block->gradient[i][2], 1);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_the_size_and_opacity_curves_reach_the_block(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleCurves, {
        .size = {0.5f, 1, 2, 3},
        .opacity = {0, 1, 0.5f, 0},
        .rotation = {0, 0.25f, 0.5f, 1}
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->size_curve[0], 0.5f);
    test_flt(block->size_curve[3], 3);
    test_flt(block->opacity_curve[1], 1);
    test_flt(block->opacity_curve[3], 0);
    test_flt(block->turn_curve[2], 0.5f);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_the_gradient_reaches_the_block_as_linear(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleCurves, {
        .color = {
            .start = {255, 255, 255}, .early = {255, 128, 0},
            .late = {128, 0, 0}, .end = {0, 0, 0}
        }
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->gradient[0][0], box_srgb_to_linear(255));
    test_flt(block->gradient[1][1], box_srgb_to_linear(128));
    test_flt(block->gradient[2][0], box_srgb_to_linear(128));
    test_flt(block->gradient[3][2], box_srgb_to_linear(0));

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_authored_curves_keep_the_other_defaults(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);

    test_int(ecs_script_run(w, "test",
        "using box.particle\n"
        "puff {\n"
        "  ParticleEmitter: {rate: 100}\n"
        "  ParticleCurves: {size: {start: 1, early: 2, late: 3, end: 4}}\n"
        "}\n", NULL), 0);

    ecs_entity_t e = ecs_lookup(w, "puff");
    test_assert(e != 0);

    const ParticleCurves *c = ecs_get(w, e, ParticleCurves);
    test_assert(c != NULL);
    test_flt(c->size.end, 4);
    test_flt(c->opacity.start, 1);
    test_flt(c->opacity.end, 1);
    test_flt(c->rotation.end, 0);
    test_int(c->color.late.r, 255);
    test_int(c->color.end.b, 255);

    ecs_fini(w);
}

void Appearance_an_emitter_without_an_appearance_draws_circles(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->appearance, BOX_PARTICLE_DRAW_CIRCLE);
    test_int(block->texture, 0);
    test_int(block->mesh, 0);
    test_int(block->flags & BOX_PARTICLE_ADDITIVE, 0);
    test_int(block->flags & BOX_PARTICLE_SOFT, 0);
    test_flt(block->sheet[0], 1);
    test_flt(block->sheet[1], 1);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_an_additive_sprite_reaches_the_block(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleAppearance, {
        .shape = ParticleSprite, .blend = ParticleAdditive
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->appearance, BOX_PARTICLE_DRAW_SPRITE);
    test_assert((block->flags & BOX_PARTICLE_ADDITIVE) != 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_a_sheet_and_its_frame_rate_reach_the_block(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleAppearance, {
        .shape = ParticleSprite, .columns = 4, .rows = 2, .fps = 24
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->sheet[0], 4);
    test_flt(block->sheet[1], 2);
    test_flt(block->sheet[2], 24);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_a_softness_sets_its_flag(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleAppearance, {.softness = 0.75f});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->sheet[3], 0.75f);
    test_assert((block->flags & BOX_PARTICLE_SOFT) != 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_a_texture_entity_binds_its_handle(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t texture = ecs_new(w);
    ecs_set(w, texture, RenderTexture, {.handle = 9, .width = 64, .height = 64});

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleAppearance, {.shape = ParticleSprite, .texture = texture});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->texture, 9);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_a_texture_through_pbr_textures_binds_its_handle(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t texture = ecs_new(w);
    ecs_set(w, texture, RenderTexture, {.handle = 4, .width = 64, .height = 64});

    ecs_entity_t material = ecs_new(w);
    ecs_set(w, material, PbrTextures, {.albedo = texture});

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleAppearance, {.shape = ParticleSprite, .texture = material});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->texture, 4);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_a_texture_through_an_image_binds_its_handle(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t texture = ecs_new(w);
    ecs_set(w, texture, RenderTexture, {.handle = 6, .width = 32, .height = 32});

    ecs_entity_t panel = ecs_new(w);
    ecs_set(w, panel, Image, {.texture = texture});

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleAppearance, {.shape = ParticleSprite, .texture = panel});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->texture, 6);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_a_billboard_binds_the_mesh_of_its_entity(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t shard = ecs_new(w);
    ecs_set(w, shard, RenderMesh, {.handle = 12, .scale = {1, 1, 1}});

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleAppearance, {.shape = ParticleBillboard, .mesh = shard});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->appearance, BOX_PARTICLE_DRAW_BILLBOARD);
    test_int(block->mesh, 12);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_the_origin_of_an_emitter_sorts_its_pool(void)
{
    // particles/appearance
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, Position, {3, 4, 5});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->origin[0], 3);
    test_flt(block->origin[1], 4);
    test_flt(block->origin[2], 5);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Appearance_a_curve_hands_out_its_keys(void)
{
    // particles/appearance
    float keys[BOX_PARTICLE_CURVE_KEYS] = { 0, 2, 6, 3 };

    test_flt(box_particle_curve(keys, 0), 0);
    test_assert(fabsf(box_particle_curve(keys, 1.0f / 3.0f) - 2.0f) < 0.0001f);
    test_assert(fabsf(box_particle_curve(keys, 2.0f / 3.0f) - 6.0f) < 0.0001f);
    test_flt(box_particle_curve(keys, 1), 3);
}

void Appearance_a_curve_is_the_line_between_two_keys(void)
{
    // particles/appearance
    float keys[BOX_PARTICLE_CURVE_KEYS] = { 0, 3, 3, 9 };

    test_assert(fabsf(box_particle_curve(keys, 1.0f / 6.0f) - 1.5f) < 0.0001f);
    test_assert(fabsf(box_particle_curve(keys, 0.5f) - 3.0f) < 0.0001f);
    test_assert(fabsf(box_particle_curve(keys, 5.0f / 6.0f) - 6.0f) < 0.0001f);
}

void Appearance_a_curve_is_held_inside_its_range(void)
{
    // particles/appearance
    float keys[BOX_PARTICLE_CURVE_KEYS] = { 1, 2, 3, 4 };

    test_flt(box_particle_curve(keys, -2), 1);
    test_flt(box_particle_curve(keys, 4), 4);
}

void Appearance_a_flat_curve_is_a_constant(void)
{
    // particles/appearance
    float keys[BOX_PARTICLE_CURVE_KEYS] = { 1, 1, 1, 1 };
    int32_t i;
    for (i = 0; i <= 20; i ++) {
        test_flt(box_particle_curve(keys, (float)i / 20.0f), 1);
    }
}

void Appearance_a_sheet_plays_once_over_the_life(void)
{
    // particles/appearance
    test_int(box_particle_frame(2, 2, 0, 0.0f, 1.0f), 0);
    test_int(box_particle_frame(2, 2, 0, 0.3f, 1.0f), 1);
    test_int(box_particle_frame(2, 2, 0, 0.6f, 1.0f), 2);
    test_int(box_particle_frame(2, 2, 0, 1.0f, 1.0f), 3);
}

void Appearance_a_frame_rate_plays_the_sheet_at_that_rate(void)
{
    // particles/appearance
    test_int(box_particle_frame(4, 1, 10, 0.0f, 2.0f), 0);
    test_int(box_particle_frame(4, 1, 10, 0.1f, 2.0f), 2);
    test_int(box_particle_frame(4, 1, 10, 0.25f, 2.0f), 1);
    test_int(box_particle_frame(4, 1, 10, 0.5f, 2.0f), 2);
}

void Appearance_a_sheet_of_one_frame_never_leaves_it(void)
{
    // particles/appearance
    int32_t i;
    for (i = 0; i <= 10; i ++) {
        test_int(box_particle_frame(1, 1, 0, (float)i / 10.0f, 1.0f), 0);
        test_int(box_particle_frame(0, 0, 60, (float)i / 10.0f, 1.0f), 0);
    }
}

void Appearance_a_particle_without_softness_is_left_alone(void)
{
    // particles/appearance
    test_flt(box_particle_soft(10, 10, 0), 1);
    test_flt(box_particle_soft(1, 100, 0), 1);
}

void Appearance_a_particle_at_the_surface_is_gone(void)
{
    // particles/appearance
    test_flt(box_particle_soft(10, 10, 0.5f), 0);
    test_flt(box_particle_soft(10, 12, 0.5f), 0);
}

void Appearance_a_particle_fades_in_over_the_softness(void)
{
    // particles/appearance
    test_flt(box_particle_soft(10.25f, 10, 0.5f), 0.5f);
    test_flt(box_particle_soft(10.5f, 10, 0.5f), 1);
    test_flt(box_particle_soft(40, 10, 0.5f), 1);
}
