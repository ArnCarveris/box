/**
 * @file Modulators.c
 * @brief Tests for light modulator plugins: the generated include, params and reload.
 */

#include <box.h>
#include <box/sky.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t sun;
static int32_t modulator_errors;
static ecs_os_api_log_t modulator_log_prev;

/** Log callback that counts errors instead of printing them.
 *
 * @param level Log level, -3 for an error
 * @param file Source file of the message
 * @param line Source line of the message
 * @param msg The message
 */
static void modulator_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    (void)msg;
    modulator_errors += level == -3;
}

/** Source of the mesh shader in the packet, NULL when it was not recomposed.
 *
 * @return The source the renderer would compile
 */
static const char* modulator_mesh_source(void)
{
    const char *result = NULL;
    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        box_shader_cmd_t *cmd = box_vec_at(&packet.shader_create, i);
        if (cmd->handle == BOX_SHADER_MESH) {
            result = cmd->source;
        }
    }

    return result;
}

void Modulators_setup(void)
{
    modulator_errors = 0;
    w = ecs_init();
    modulator_log_prev = ecs_os_api.log_;
    ecs_os_api.log_ = modulator_log;
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {1.0f, 0, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
}

void Modulators_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
    ecs_os_api.log_ = modulator_log_prev;
}

void Modulators_the_include_is_generated_per_light(void)
{
    // ore_engine/cloud_shadows
    ecs_entity_t clouds = ecs_new(w);
    ecs_set(w, clouds, Shader, {.path = "etc/shaders/light/clouds.wgsl"});
    ecs_set(w, sun, LightModulator, {.shader = clouds, .enabled = true});
    ecs_set(w, sun, CloudShadows, {.scale = 30, .density = 0.5f});

    ecs_entity_t lamp = ecs_new(w);
    ecs_set(w, lamp, Position, {0, 2, 5});
    ecs_set(w, lamp, PointLight, {.intensity = 4, .range = 20});
    ecs_set(w, lamp, LightModulator, {.shader = clouds, .enabled = true});
    ecs_set(w, lamp, CloudShadows, {.scale = 10, .density = 0.25f});

    box_render_extract(w, &packet);

    const char *source = modulator_mesh_source();
    test_assert(source != NULL);
    test_assert(strstr(source, "fn box_light_modulate_0(") != NULL);
    test_assert(strstr(source, "fn box_light_modulate_1(") != NULL);
    test_assert(strstr(source, "fn box_light_modulate_2(") == NULL);
    test_assert(strstr(source, "fn box_light_modulate_all(") != NULL);
    test_int(modulator_errors, 0);
}

void Modulators_params_reach_the_packet(void)
{
    // ore_engine/cloud_shadows
    ecs_entity_t clouds = ecs_new(w);
    ecs_set(w, clouds, Shader, {.path = "etc/shaders/light/clouds.wgsl"});
    ecs_set(w, sun, LightModulator, {.shader = clouds, .enabled = true});
    ecs_set(w, sun, CloudShadows, {
        .scale = 30, .speed = 6, .density = 0.5f, .softness = 0.2f});

    box_render_extract(w, &packet);

    test_int(packet.modulator_count, 1);
    test_flt(packet.modulator_params[0].v[0], 30);
    test_flt(packet.modulator_params[0].v[1], 6);
    test_flt(packet.modulator_params[0].v[2], 0.5f);
    test_flt(packet.modulator_params[0].v[3], 0.2f);
    test_flt(packet.modulator_params[0].v[4], 0);
}

void Modulators_a_light_without_one_compiles_the_plain_variant(void)
{
    // ore_engine/cloud_shadows
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 5});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);

    const char *source = modulator_mesh_source();
    test_assert(source != NULL);
    test_assert(strstr(source, "fn box_light_modulate_0(") == NULL);
    test_assert(strstr(source, "fn box_light_modulate_all(") != NULL);
    test_int(packet.modulator_count, 0);
    test_int(packet.view.sun_modulator, 0);
    test_int(modulator_errors, 0);
}

void Modulators_the_sun_carries_its_modulator_bit(void)
{
    // ore_engine/cloud_shadows
    ecs_entity_t clouds = ecs_new(w);
    ecs_set(w, clouds, Shader, {.path = "etc/shaders/light/clouds.wgsl"});
    ecs_set(w, sun, LightModulator, {.shader = clouds, .enabled = true});
    ecs_set(w, sun, CloudShadows, {.density = 0.5f});
    box_render_extract(w, &packet);

    test_int(packet.view.sun_modulator, 1);
}

void Modulators_a_point_light_carries_its_modulator_bit(void)
{
    // ore_engine/cloud_shadows
    ecs_entity_t caustics = ecs_new(w);
    ecs_set(w, caustics, Shader, {.path = "etc/shaders/light/caustics.wgsl"});
    ecs_entity_t lamp = ecs_new(w);
    ecs_set(w, lamp, Position, {0, 2, 5});
    ecs_set(w, lamp, PointLight, {.intensity = 4, .range = 20});
    ecs_set(w, lamp, LightModulator, {.shader = caustics, .enabled = true});
    ecs_set(w, lamp, Caustics, {.scale = 2, .intensity = 1.5f});
    box_render_extract(w, &packet);

    test_int(packet.lights.count, 1);
    box_light_t *light = box_vec_at(&packet.lights, 0);
    test_int(light->modulator, 1);
    test_int(packet.view.sun_modulator, 0);
    test_flt(packet.modulator_params[0].v[2], 1.5f);
}

void Modulators_several_modulators_on_one_light_multiply(void)
{
    // ore_engine/cloud_shadows
    ecs_entity_t clouds = ecs_new(w);
    ecs_set(w, clouds, Shader, {.path = "etc/shaders/light/clouds.wgsl"});
    ecs_entity_t caustics = ecs_new(w);
    ecs_set(w, caustics, Shader, {.path = "etc/shaders/light/caustics.wgsl"});

    ecs_set(w, sun, LightModulator, {.shader = clouds, .enabled = true});
    ecs_set(w, sun, CloudShadows, {.density = 0.5f});
    ecs_entity_t second = ecs_new_w_pair(w, EcsChildOf, sun);
    ecs_set(w, second, LightModulator, {.shader = caustics, .enabled = true});
    ecs_set(w, second, Caustics, {.intensity = 2});

    box_render_extract(w, &packet);

    test_int(packet.modulator_count, 2);
    test_int(packet.view.sun_modulator, 3);
    const char *source = modulator_mesh_source();
    test_assert(source != NULL);
    test_assert(strstr(source, "box_light_modulate_0(p, n, l, time") != NULL);
    test_assert(strstr(source, "box_light_modulate_1(p, n, l, time") != NULL);
}

void Modulators_a_disabled_modulator_is_not_registered(void)
{
    // ore_engine/cloud_shadows
    ecs_entity_t clouds = ecs_new(w);
    ecs_set(w, clouds, Shader, {.path = "etc/shaders/light/clouds.wgsl"});
    ecs_set(w, sun, LightModulator, {.shader = clouds, .enabled = true});
    ecs_set(w, sun, CloudShadows, {.density = 0.5f});
    box_render_extract(w, &packet);
    test_int(packet.modulator_count, 1);

    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_vec_clear(&packet.shader_create);
    ecs_set(w, sun, LightModulator, {.shader = clouds, .enabled = false});
    box_render_extract(w, &packet);

    test_int(packet.modulator_count, 0);
    test_int(packet.view.sun_modulator, 0);
    const char *source = modulator_mesh_source();
    test_assert(source != NULL);
    test_assert(strstr(source, "fn box_light_modulate_0(") == NULL);
}

void Modulators_a_broken_plugin_reports_and_keeps_the_pipeline(void)
{
    // ore_engine/cloud_shadows
    const char *path = "/tmp/box_broken_modulator.wgsl";
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fprintf(file, "#include \"no_such_include.wgsl\"\n");
    fclose(file);

    ecs_entity_t broken = ecs_new(w);
    ecs_set(w, broken, Shader, {.path = (char*)path});
    ecs_set(w, sun, LightModulator, {.shader = broken, .enabled = true});
    ecs_set(w, sun, CloudShadows, {.density = 0.5f});
    box_render_extract(w, &packet);

    const ShaderStatus *status = ecs_get(w, broken, ShaderStatus);
    test_assert(status != NULL);
    test_bool(status->ok, false);
    test_assert(status->error != NULL);
    test_assert(status->error[0] != '\0');

    const char *source = modulator_mesh_source();
    test_assert(source != NULL);
    test_assert(strstr(source, "fn box_light_modulate_0(") != NULL);
    test_assert(strstr(source, "fn box_light_modulate_all(") != NULL);
    test_assert(strstr(source, "fn mesh_shade(") != NULL);
    remove(path);
}

void Modulators_changing_the_plugin_recomposes_the_mesh_shader(void)
{
    // ore_engine/cloud_shadows
    ecs_entity_t plugin = ecs_new(w);
    ecs_set(w, plugin, Shader, {.path = "etc/shaders/light/clouds.wgsl"});
    ecs_set(w, sun, LightModulator, {.shader = plugin, .enabled = true});
    ecs_set(w, sun, CloudShadows, {.density = 0.5f});
    box_render_extract(w, &packet);
    test_assert(strstr(modulator_mesh_source(), "let deck = box_modulator_fbm(") != NULL);

    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_vec_clear(&packet.shader_create);
    ecs_set(w, plugin, Shader, {.path = "etc/shaders/light/caustics.wgsl"});
    box_render_extract(w, &packet);

    const char *source = modulator_mesh_source();
    test_assert(source != NULL);
    test_assert(strstr(source, "let deck = box_modulator_fbm(") == NULL);
    test_assert(strstr(source, "let bands = pow(") != NULL);
    test_assert(strstr(source, "fn box_light_modulate_0(") != NULL);
}

void Modulators_the_overcast_drives_a_density_of_zero(void)
{
    // ore_engine/cloud_shadows
    ecs_entity_t atmosphere = ecs_new(w);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun});
    ecs_entity_t clouds = ecs_new(w);
    ecs_set(w, clouds, Shader, {.path = "etc/shaders/light/clouds.wgsl"});
    ecs_set(w, sun, LightModulator, {.shader = clouds, .enabled = true});
    ecs_set(w, sun, CloudShadows, {.scale = 30});
    ecs_singleton_set(w, Weather, {.overcast = 0.75f});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    const CloudShadows *deck = ecs_get(w, sun, CloudShadows);
    test_assert(fabsf(deck->density - 0.75f) < 1e-5f);
    test_assert(fabsf(packet.modulator_params[0].v[2] - 0.75f) < 1e-5f);
}

void Modulators_a_written_density_is_kept(void)
{
    // ore_engine/cloud_shadows
    ecs_entity_t atmosphere = ecs_new(w);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun});
    ecs_entity_t clouds = ecs_new(w);
    ecs_set(w, clouds, Shader, {.path = "etc/shaders/light/clouds.wgsl"});
    ecs_set(w, sun, LightModulator, {.shader = clouds, .enabled = true});
    ecs_set(w, sun, CloudShadows, {.scale = 30, .density = 0.2f});
    ecs_singleton_set(w, Weather, {.overcast = 0.75f});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    const CloudShadows *deck = ecs_get(w, sun, CloudShadows);
    test_assert(fabsf(deck->density - 0.2f) < 1e-5f);
    test_assert(fabsf(packet.modulator_params[0].v[2] - 0.2f) < 1e-5f);
}
