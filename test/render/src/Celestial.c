/**
 * @file Celestial.c
 * @brief Tests for the sun, the moon and the star field of the atmosphere.
 */

#include <box.h>
#include <box/sky.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t sun;
static ecs_entity_t atmosphere;

void Celestial_setup(void)
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
    ecs_set(w, sun, Rotation, {1.2f, 0, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    atmosphere = ecs_new(w);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun});
}

void Celestial_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Celestial_a_moon_opposite_the_sun_is_full(void)
{
    // sky/celestial
    ecs_entity_t moon = ecs_new(w);
    ecs_set(w, moon, Rotation, {1.2f - 3.14159265f, 0, 0});
    ecs_set(w, moon, CelestialLight, {.toa_intensity = 1});
    ecs_set(w, moon, DirectionalLight, {.intensity = 1});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .moon = moon});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.moon_color[3] - 1.0f) < 1e-4f);
}

void Celestial_a_moon_at_the_sun_is_new(void)
{
    // sky/celestial
    ecs_entity_t moon = ecs_new(w);
    ecs_set(w, moon, Rotation, {1.2f, 0, 0});
    ecs_set(w, moon, DirectionalLight, {.intensity = 1});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .moon = moon});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.moon_color[3]) < 1e-4f);
}

void Celestial_a_moon_at_a_right_angle_is_half(void)
{
    // sky/celestial
    ecs_entity_t moon = ecs_new(w);
    ecs_set(w, moon, Rotation, {1.2f - 1.57079633f, 0, 0});
    ecs_set(w, moon, DirectionalLight, {.intensity = 1});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .moon = moon});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.moon_color[3] - 0.5f) < 1e-4f);
}

void Celestial_the_moon_direction_comes_from_its_light(void)
{
    // sky/celestial
    ecs_entity_t moon = ecs_new(w);
    ecs_set(w, moon, Rotation, {0.5f, 0, 0});
    ecs_set(w, moon, DirectionalLight, {.intensity = 1});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .moon = moon});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.moon_dir[1] - sinf(0.5f)) < 1e-4f);
    test_assert(fabsf(packet.sky.moon_dir[2] + cosf(0.5f)) < 1e-4f);
}

void Celestial_the_moon_disk_takes_its_angular_radius(void)
{
    // sky/celestial
    ecs_entity_t moon = ecs_new(w);
    ecs_set(w, moon, Rotation, {0.5f, 0, 0});
    ecs_set(w, moon, DirectionalLight, {.intensity = 1});
    ecs_set(w, atmosphere, Atmosphere, {
        .sun = sun, .moon = moon, .moon_angular_radius = 0.01f});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.moon_dir[3] - cosf(0.01f)) < 1e-6f);
}

void Celestial_a_default_moon_disk_takes_the_default_radius(void)
{
    // sky/celestial
    ecs_entity_t moon = ecs_new(w);
    ecs_set(w, moon, Rotation, {0.5f, 0, 0});
    ecs_set(w, moon, DirectionalLight, {.intensity = 1});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .moon = moon});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.moon_dir[3] - cosf(BOX_SKY_MOON_RADIUS)) < 1e-6f);
}

void Celestial_a_world_without_a_moon_draws_none(void)
{
    // sky/celestial
    box_render_extract(w, &packet);

    test_flt(packet.sky.moon_dir[3], 1.0f);
    test_flt(packet.sky.moon_color[0], 0.0f);
    test_flt(packet.sky.moon_color[3], 0.0f);
}

void Celestial_the_moon_radiance_comes_from_its_celestial_light(void)
{
    // sky/celestial
    ecs_entity_t moon = ecs_new(w);
    ecs_set(w, moon, Rotation, {0.5f, 0, 0});
    ecs_set(w, moon, DirectionalLight, {.intensity = 1});
    ecs_set(w, moon, CelestialLight, {.toa_intensity = 4, .toa_color = {255, 128, 0}});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .moon = moon});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.moon_color[0] - 4.0f) < 1e-4f);
    test_assert(fabsf(packet.sky.moon_color[1] - 4.0f * box_srgb_to_linear(128)) < 1e-4f);
    test_flt(packet.sky.moon_color[2], 0.0f);
}

void Celestial_a_world_without_stars_draws_none(void)
{
    // sky/celestial
    box_render_extract(w, &packet);

    test_flt(packet.sky.stars[0], 0.0f);
    test_flt(packet.sky.stars_color[3], 0.0f);
}

void Celestial_a_star_field_takes_the_defaults(void)
{
    // sky/celestial
    ecs_entity_t stars = ecs_new(w);
    ecs_set(w, stars, Stars, {0});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .stars = stars});
    box_render_extract(w, &packet);

    test_flt(packet.sky.stars[0], BOX_SKY_STARS_DENSITY);
    test_flt(packet.sky.stars[1], BOX_SKY_STARS_SIZE);
    test_flt(packet.sky.stars[2], 0.0f);
}

void Celestial_the_star_field_takes_its_own_values(void)
{
    // sky/celestial
    ecs_entity_t stars = ecs_new(w);
    ecs_set(w, stars, Stars, {.density = 80, .size = 0.004f, .twinkle = 0.3f});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .stars = stars});
    box_render_extract(w, &packet);

    test_flt(packet.sky.stars[0], 80.0f);
    test_flt(packet.sky.stars[1], 0.004f);
    test_flt(packet.sky.stars[2], 0.3f);
}

void Celestial_the_star_radiance_comes_from_its_celestial_light(void)
{
    // sky/celestial
    ecs_entity_t stars = ecs_new(w);
    ecs_set(w, stars, Stars, {0});
    ecs_set(w, stars, CelestialLight, {.toa_intensity = 2, .toa_color = {255, 255, 128}});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .stars = stars});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.stars_color[0] - 2.0f) < 1e-4f);
    test_assert(fabsf(packet.sky.stars_color[2] - 2.0f * box_srgb_to_linear(128)) < 1e-4f);
}

void Celestial_an_unrotated_star_field_keeps_the_axes_of_the_world(void)
{
    // sky/celestial
    ecs_entity_t stars = ecs_new(w);
    ecs_set(w, stars, Stars, {0});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .stars = stars});
    box_render_extract(w, &packet);

    test_flt(packet.sky.stars_rot[0], 1.0f);
    test_flt(packet.sky.stars_rot[5], 1.0f);
    test_flt(packet.sky.stars_rot[10], 1.0f);
}

void Celestial_a_rotated_star_field_turns_its_axes(void)
{
    // sky/celestial
    ecs_entity_t stars = ecs_new(w);
    ecs_set(w, stars, Stars, {0});
    ecs_set(w, stars, Rotation, {0, 1.57079633f, 0});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .stars = stars});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.stars_rot[0]) < 1e-4f);
    test_assert(fabsf(packet.sky.stars_rot[2] + 1.0f) < 1e-4f);
    test_assert(fabsf(packet.sky.stars_rot[8] - 1.0f) < 1e-4f);
    test_assert(fabsf(packet.sky.stars_rot[10]) < 1e-4f);
}

void Celestial_the_star_field_stands_in_the_dark(void)
{
    // sky/celestial
    ecs_entity_t stars = ecs_new(w);
    ecs_set(w, stars, Stars, {0});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .stars = stars});
    ecs_set(w, sun, Rotation, {-0.6f, 0, 0});
    box_render_extract(w, &packet);

    test_flt(packet.sky.stars_color[3], 1.0f);
}

void Celestial_the_star_field_is_gone_by_day(void)
{
    // sky/celestial
    ecs_entity_t stars = ecs_new(w);
    ecs_set(w, stars, Stars, {0});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .stars = stars});
    ecs_set(w, sun, Rotation, {1.2f, 0, 0});
    box_render_extract(w, &packet);

    test_flt(packet.sky.stars_color[3], 0.0f);
}

void Celestial_the_star_fade_is_half_between_the_two_elevations(void)
{
    // sky/celestial
    ecs_entity_t stars = ecs_new(w);
    ecs_set(w, stars, Stars, {.fade_start = -0.2f, .fade_end = 0.2f});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .stars = stars});
    ecs_set(w, sun, Rotation, {0, 0, 0});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.stars_color[3] - 0.5f) < 1e-4f);
}

void Celestial_the_star_fade_follows_the_elevations_of_the_field(void)
{
    // sky/celestial
    ecs_entity_t stars = ecs_new(w);
    ecs_set(w, stars, Stars, {.fade_start = 0.4f, .fade_end = 0.8f});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .stars = stars});
    ecs_set(w, sun, Rotation, {asinf(0.2f), 0, 0});
    box_render_extract(w, &packet);

    test_flt(packet.sky.stars_color[3], 1.0f);
}

void Celestial_the_sky_keeps_the_radiance_over_the_atmosphere(void)
{
    // sky/celestial
    ecs_set(w, sun, CelestialLight, {.toa_intensity = 12});
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.sun_color[0] - 12.0f) < 1e-4f);
}

void Celestial_the_air_takes_the_blue_out_of_a_low_sun(void)
{
    // sky/celestial
    ecs_set(w, sun, Rotation, {asinf(0.02f), 0, 0});
    box_render_extract(w, &packet);
    const box_view_t *low = box_vec_at(&packet.views, 0);
    float low_ratio = low->sun_color[2] / low->sun_color[0];
    float low_red = low->sun_color[0];

    box_packet_reset(&packet);
    ecs_set(w, sun, Rotation, {asinf(0.9f), 0, 0});
    box_render_extract(w, &packet);
    const box_view_t *high = box_vec_at(&packet.views, 0);
    float high_ratio = high->sun_color[2] / high->sun_color[0];

    test_assert(low_ratio < high_ratio * 0.5f);
    test_assert(high->sun_color[0] > low_red);
}

void Celestial_a_sun_over_the_atmosphere_keeps_its_radiance(void)
{
    // sky/celestial
    ecs_set(w, sun, Rotation, {1.57079633f, 0, 0});
    ecs_set(w, sun, CelestialLight, {.toa_intensity = 10});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .planet_radius_km = 6360,
        .atmosphere_thickness_km = 0.001f});
    box_render_extract(w, &packet);
    const box_view_t *v = box_vec_at(&packet.views, 0);

    test_assert(v->sun_color[0] > 9.9f);
    test_assert(v->sun_color[0] <= 10.0f);
}

void Celestial_a_world_without_an_atmosphere_keeps_its_light(void)
{
    // sky/celestial
    ecs_delete(w, atmosphere);
    box_render_extract(w, &packet);
    const box_view_t *v = box_vec_at(&packet.views, 0);

    test_flt(v->sun_intensity, 3.0f);
    test_flt(v->sun_color[0], 1.0f);
}

void Celestial_a_night_without_a_moon_leaves_the_view_dark(void)
{
    // sky/celestial
    ecs_set(w, sun, Rotation, {-0.5f, 0, 0});
    box_render_extract(w, &packet);
    const box_view_t *v = box_vec_at(&packet.views, 0);

    test_assert(v->sun_color[0] < 1e-4f);
    test_assert(v->sun_color[1] < 1e-4f);
    test_assert(v->sun_color[2] < 1e-4f);
}

void Celestial_a_full_moon_lights_the_view_after_sunset(void)
{
    // sky/celestial
    ecs_entity_t moon = ecs_new(w);
    ecs_set(w, moon, Rotation, {3.14159265f - 0.5f, 0, 0});
    ecs_set(w, moon, DirectionalLight, {.intensity = 1});
    ecs_set(w, moon, CelestialLight, {.toa_intensity = 0.02f});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .moon = moon});
    ecs_set(w, sun, Rotation, {-0.5f, 0, 0});
    box_render_extract(w, &packet);
    const box_view_t *v = box_vec_at(&packet.views, 0);

    test_assert(v->sun_color[0] > 1e-4f);
    test_assert(fabsf(v->sun_dir[1] + sinf(3.14159265f - 0.5f)) < 1e-4f);
}

void Celestial_a_new_moon_leaves_the_night_dark(void)
{
    // sky/celestial
    ecs_entity_t moon = ecs_new(w);
    ecs_set(w, moon, Rotation, {-0.5f, 0, 0});
    ecs_set(w, moon, DirectionalLight, {.intensity = 1});
    ecs_set(w, moon, CelestialLight, {.toa_intensity = 0.02f});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .moon = moon});
    ecs_set(w, sun, Rotation, {-0.5f, 0, 0});
    box_render_extract(w, &packet);
    const box_view_t *v = box_vec_at(&packet.views, 0);

    test_assert(v->sun_color[0] < 1e-6f);
}

void Celestial_the_sun_outshines_the_moon_by_day(void)
{
    // sky/celestial
    ecs_entity_t moon = ecs_new(w);
    ecs_set(w, moon, Rotation, {1.2f - 3.14159265f, 0, 0});
    ecs_set(w, moon, DirectionalLight, {.intensity = 1});
    ecs_set(w, moon, CelestialLight, {.toa_intensity = 0.02f});
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .moon = moon});
    box_render_extract(w, &packet);
    const box_view_t *v = box_vec_at(&packet.views, 0);

    test_assert(fabsf(v->sun_dir[1] + sinf(1.2f)) < 1e-4f);
    test_assert(v->sun_color[0] > 1.0f);
}

void Celestial_the_sun_of_the_sky_is_the_light_of_the_view(void)
{
    // sky/celestial
    ecs_set(w, sun, Rotation, {0.8f, 0, 0});
    box_render_extract(w, &packet);
    const box_view_t *v = box_vec_at(&packet.views, 0);

    test_assert(fabsf(packet.sky.sun_dir[0] + v->sun_dir[0]) < 1e-5f);
    test_assert(fabsf(packet.sky.sun_dir[1] + v->sun_dir[1]) < 1e-5f);
    test_assert(fabsf(packet.sky.sun_dir[2] + v->sun_dir[2]) < 1e-5f);
}

void Celestial_a_camera_over_the_haze_keeps_more_of_the_sun(void)
{
    // sky/celestial
    ecs_set(w, sun, Rotation, {asinf(0.1f), 0, 0});
    box_render_extract(w, &packet);
    const box_view_t *low = box_vec_at(&packet.views, 0);
    float ground = low->sun_color[0];

    box_packet_reset(&packet);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .ground_altitude_km = 12});
    box_render_extract(w, &packet);
    const box_view_t *high = box_vec_at(&packet.views, 0);

    test_assert(high->sun_color[0] > ground);
}
