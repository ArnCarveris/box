/**
 * @file Sky.c
 * @brief Tests for extracting the atmosphere into the frame packet.
 */

#include <box.h>
#include <box/sky.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t sun;

void Sky_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {1.0f, 0, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 2});
}

void Sky_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Sky_world_without_an_atmosphere_has_no_sky(void)
{
    // sky/atmosphere
    box_render_extract(w, &packet);
    test_int(packet.sky.active, 0);
    test_int(packet.view.sky, 0);
    test_uint(packet.sky_lut_shader, BOX_SHADER_NONE);
    test_uint(packet.sky_shader, BOX_SHADER_NONE);
}

void Sky_world_without_an_atmosphere_registers_no_shader(void)
{
    // sky/atmosphere
    box_render_extract(w, &packet);
    test_assert(ecs_lookup(w, "box.sky.shaders.sky_lut") == 0);
    test_assert(ecs_lookup(w, "box.sky.shaders.sky") == 0);
}

void Sky_atmosphere_draws_the_sky_of_every_view(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);
    test_int(packet.sky.active, 1);
    test_int(packet.view.sky, 1);
    test_int(packet.views.count, 1);
    test_int(((box_view_t*)box_vec_at(&packet.views, 0))->sky, 1);
}

void Sky_atmosphere_registers_the_shaders_that_compile(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    ecs_entity_t lut = ecs_lookup(w, "box.sky.shaders.sky_lut");
    ecs_entity_t pass = ecs_lookup(w, "box.sky.shaders.sky");
    test_assert(lut != 0);
    test_assert(pass != 0);

    const ShaderStatus *lut_status = ecs_get(w, lut, ShaderStatus);
    const ShaderStatus *pass_status = ecs_get(w, pass, ShaderStatus);
    test_assert(lut_status != NULL);
    test_assert(pass_status != NULL);
    test_str(lut_status->error ? lut_status->error : "", "");
    test_str(pass_status->error ? pass_status->error : "", "");

    box_render_extract(w, &packet);
    test_assert(packet.sky_lut_shader != BOX_SHADER_NONE);
    test_assert(packet.sky_shader != BOX_SHADER_NONE);
}

void Sky_sun_direction_comes_from_the_referenced_light(void)
{
    // sky/atmosphere
    ecs_entity_t other = ecs_new(w);
    ecs_set(w, other, Rotation, {0, 0, 0});
    ecs_set(w, other, DirectionalLight, {.intensity = 1});
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.sun_dir[1] - sinf(1.0f)) < 1e-4f);
    test_assert(fabsf(packet.sky.sun_dir[2] + cosf(1.0f)) < 1e-4f);
}

void Sky_sun_falls_back_to_the_first_directional_light(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {0});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.sun_dir[0] + packet.view.sun_dir[0]) < 1e-6f);
    test_assert(fabsf(packet.sky.sun_dir[1] + packet.view.sun_dir[1]) < 1e-6f);
    test_assert(fabsf(packet.sky.sun_dir[2] + packet.view.sun_dir[2]) < 1e-6f);
}

void Sky_sun_color_is_the_linear_light_color_times_its_intensity(void)
{
    // sky/atmosphere
    ecs_set(w, sun, Rgb, {255, 128, 0});
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.sun_color[0] - 2.0f) < 1e-5f);
    test_assert(fabsf(packet.sky.sun_color[1] - box_srgb_to_linear(128) * 2.0f) < 1e-5f);
    test_flt(packet.sky.sun_color[2], 0);
}

void Sky_zero_values_take_the_defaults_of_the_medium(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);

    test_flt(packet.sky.bounds[0], BOX_SKY_PLANET_KM);
    test_flt(packet.sky.bounds[1], BOX_SKY_PLANET_KM + BOX_SKY_THICKNESS_KM);
    test_flt(packet.sky.bounds[2], BOX_SKY_RAYLEIGH_KM);
    test_flt(packet.sky.bounds[3], BOX_SKY_MIE_KM);
    test_flt(packet.sky.world_scale[0], BOX_SKY_UNITS_PER_KM);
    test_assert(fabsf(packet.sky.world_scale[1] - 1.0f / BOX_SKY_UNITS_PER_KM) < 1e-9f);
    test_flt(packet.sky.mie_params[1], BOX_SKY_AERIAL_KM);
    test_assert(fabsf(packet.sky.mie_params[2] - cosf(BOX_SKY_DISK_RADIUS)) < 1e-6f);
    test_flt(packet.sky.sun_color[3], 1);
}

void Sky_rayleigh_scale_multiplies_the_scattering(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);
    float blue = packet.sky.scat_rayleigh[2];
    test_assert(blue > packet.sky.scat_rayleigh[0]);

    Atmosphere *atmosphere = ecs_get_mut(w, a, Atmosphere);
    atmosphere->rayleigh.scattering_scale = 2.0f;
    ecs_modified(w, a, Atmosphere);
    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_assert(fabsf(packet.sky.scat_rayleigh[2] - blue * 2.0f) < 1e-7f);
}

void Sky_black_ground_albedo_takes_its_default(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);

    float expected = box_srgb_to_linear(BOX_SKY_GROUND_ALBEDO);
    test_assert(fabsf(packet.sky.ground_albedo[0] - expected) < 1e-6f);
    test_assert(fabsf(packet.sky.ground_albedo[2] - expected) < 1e-6f);
}

void Sky_negative_aerial_perspective_turns_the_haze_off(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun, .aerial_perspective_intensity = -1});
    box_render_extract(w, &packet);
    test_flt(packet.sky.sun_color[3], 0);
}

void Sky_tables_are_dirty_on_the_first_frame(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);
    test_int(packet.sky.lut_dirty, 1);
    test_int(packet.sky.view_dirty, 1);
    test_int(packet.sky.generation, 1);
}

void Sky_tables_stay_clean_when_nothing_changes(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);
    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_int(packet.sky.lut_dirty, 0);
    test_int(packet.sky.view_dirty, 0);
    test_int(packet.sky.generation, 1);
}

void Sky_changing_the_medium_makes_the_tables_dirty(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);
    box_packet_reset(&packet);

    Atmosphere *atmosphere = ecs_get_mut(w, a, Atmosphere);
    atmosphere->mie.scattering_scale = 3.0f;
    ecs_modified(w, a, Atmosphere);
    box_render_extract(w, &packet);
    test_int(packet.sky.lut_dirty, 1);
    test_int(packet.sky.view_dirty, 1);
    test_int(packet.sky.generation, 2);
}

void Sky_changing_the_disk_leaves_the_tables_alone(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);
    box_packet_reset(&packet);

    Atmosphere *atmosphere = ecs_get_mut(w, a, Atmosphere);
    atmosphere->sun_disk_intensity = 12.0f;
    ecs_modified(w, a, Atmosphere);
    box_render_extract(w, &packet);
    test_int(packet.sky.lut_dirty, 0);
    test_int(packet.sky.view_dirty, 0);
    test_flt(packet.sky.mie_params[3], 12.0f);
}

void Sky_a_sun_that_hardly_moves_keeps_the_sky_view(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);
    box_packet_reset(&packet);

    ecs_set(w, sun, Rotation, {1.0f + BOX_SKY_SUN_EPSILON * 0.25f, 0, 0});
    box_render_extract(w, &packet);
    test_int(packet.sky.view_dirty, 0);
    test_int(packet.sky.generation, 1);
}

void Sky_a_sun_that_moves_renders_the_sky_view_again(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);
    box_packet_reset(&packet);

    ecs_set(w, sun, Rotation, {1.0f + BOX_SKY_SUN_EPSILON * 8.0f, 0, 0});
    box_render_extract(w, &packet);
    test_int(packet.sky.lut_dirty, 0);
    test_int(packet.sky.view_dirty, 1);
    test_int(packet.sky.generation, 2);
}

void Sky_small_steps_of_the_sun_add_up_to_a_new_sky_view(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);

    float angle = 1.0f;
    int32_t i;
    for (i = 0; i < 8; i ++) {
        angle += BOX_SKY_SUN_EPSILON * 0.25f;
        ecs_set(w, sun, Rotation, {angle, 0, 0});
        box_packet_reset(&packet);
        box_render_extract(w, &packet);
    }

    test_int(packet.sky.generation, 2);
}

void Sky_a_camera_that_rises_renders_the_sky_view_again(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);
    box_packet_reset(&packet);

    ecs_iter_t it = ecs_each(w, Camera);
    while (ecs_each_next(&it)) {
        ecs_set(w, it.entities[0], Position, {0, 500, 0});
    }

    box_render_extract(w, &packet);
    test_int(packet.sky.lut_dirty, 0);
    test_int(packet.sky.view_dirty, 1);
    test_int(packet.sky.generation, 2);
}

void Sky_environment_source_marks_the_view(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);
    test_int(packet.view.sky_env, 0);

    ecs_singleton_set(w, EnvironmentLight, {.intensity = 1, .source = a});
    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_int(packet.view.sky_env, 1);
}

void Sky_environment_generation_follows_the_sky_view(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    ecs_singleton_set(w, EnvironmentLight, {.intensity = 1, .source = a});
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;

    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_uint(packet.view.env_generation, first);

    ecs_set(w, sun, Rotation, {1.0f + BOX_SKY_SUN_EPSILON * 8.0f, 0, 0});
    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_assert(packet.view.env_generation != first);
}

void Sky_environment_of_a_gradient_view_is_untouched(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {10, 20, 30},
        .ground_color = {30, 20, 10},
        .intensity = 1
    });
    box_render_extract(w, &packet);
    uint32_t generation = packet.view.env_generation;
    test_int(packet.view.sky_env, 0);

    ecs_set(w, sun, Rotation, {1.0f + BOX_SKY_SUN_EPSILON * 8.0f, 0, 0});
    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_uint(packet.view.env_generation, generation);
}

void Sky_removing_the_atmosphere_clears_the_sky(void)
{
    // sky/atmosphere
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Atmosphere, {.sun = sun});
    box_render_extract(w, &packet);
    test_int(packet.sky.active, 1);

    ecs_delete(w, a);
    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_int(packet.sky.active, 0);
    test_int(packet.view.sky, 0);
}
