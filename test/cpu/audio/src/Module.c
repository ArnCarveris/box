/**
 * @file Module.c
 * @brief The box.audio module: defaults from C and script, the processing order of buses,
 * cycles, deleted buses, frames without changes and an import without a device.
 */

#include "world.h"

void Module_defaults_c(void) {
    // audio/module
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, AudioBus);
    ecs_add(world, e, AudioSource);
    ecs_add(world, e, AudioOutput);

    const AudioBus *bus = ecs_get(world, e, AudioBus);
    test_flt(bus->volume, 1.0f);
    test_flt(bus->pan, 0.0f);
    test_bool(bus->mute, false);
    test_uint(bus->output, 0);
    test_flt(bus->width, 1.0f);
    test_bool(bus->solo, false);
    test_int(bus->kind, AudioChannelGroup);
    test_int(ecs_vec_count(&bus->sends), 0);

    const AudioSource *src = ecs_get(world, e, AudioSource);
    test_flt(src->volume, 1.0f);
    test_flt(src->pitch, 1.0f);
    test_bool(src->loop, false);
    test_bool(src->playing, true);
    test_uint(src->sound, 0);

    test_uint(ecs_get(world, e, AudioOutput)->bus, 0);

    const AudioSettings *settings = ecs_singleton_get(world, AudioSettings);
    test_assert(settings != NULL);
    test_flt(settings->master_volume, 1.0f);
    test_bool(settings->mute, false);

    const AudioStatus *status = ecs_singleton_get(world, AudioStatus);
    test_assert(status != NULL);
    test_int(status->state, AudioOff);
    ecs_fini(world);
}

void Module_defaults_script(void) {
    // audio/module
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "defaults",
        "using box.audio\n"
        "bus { Bus: {} }\n"
        "src { Source: {} }\n", NULL) == 0);

    ecs_entity_t bus = ecs_lookup(world, "bus");
    ecs_entity_t src = ecs_lookup(world, "src");
    test_assert(bus != 0);
    test_assert(src != 0);
    test_flt(ecs_get(world, bus, AudioBus)->volume, 1.0f);
    test_flt(ecs_get(world, bus, AudioBus)->pan, 0.0f);
    test_flt(ecs_get(world, src, AudioSource)->volume, 1.0f);
    test_flt(ecs_get(world, src, AudioSource)->pitch, 1.0f);
    test_bool(ecs_get(world, src, AudioSource)->playing, true);
    ecs_fini(world);
}

void Module_partial_keeps_defaults(void) {
    // audio/module
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "partial",
        "using box.audio\n"
        "bus { Bus: {pan: 0.5} }\n"
        "src { Source: {loop: true} }\n", NULL) == 0);

    const AudioBus *bus = ecs_get(world, ecs_lookup(world, "bus"), AudioBus);
    test_flt(bus->pan, 0.5f);
    test_flt(bus->volume, 1.0f);
    const AudioSource *src = ecs_get(world, ecs_lookup(world, "src"), AudioSource);
    test_bool(src->loop, true);
    test_flt(src->volume, 1.0f);
    test_flt(src->pitch, 1.0f);
    test_bool(src->playing, true);
    ecs_fini(world);
}

void Module_order_tree(void) {
    // audio/module
    ecs_world_t *world = audio_world();
    ecs_entity_t a = audio_bus(world, "a", 0, 1.0f);
    ecs_entity_t b = audio_bus(world, "b", a, 1.0f);
    ecs_entity_t c = audio_bus(world, "c", a, 1.0f);
    ecs_entity_t d = audio_bus(world, "d", b, 1.0f);
    audio_run(world, 1);

    int32_t pa = audio_position(world, a), pb = audio_position(world, b);
    int32_t pc = audio_position(world, c), pd = audio_position(world, d);
    test_assert(pa >= 0 && pb >= 0 && pc >= 0 && pd >= 0);
    test_assert(pd < pb);
    test_assert(pb < pa);
    test_assert(pc < pa);
    test_int(ecs_singleton_get(world, AudioStatus)->buses, 5);
    ecs_fini(world);
}

void Module_order_chain_send(void) {
    // audio/module
    ecs_world_t *world = audio_world();
    ecs_entity_t a = audio_bus(world, "a", 0, 1.0f);
    ecs_entity_t b = audio_bus(world, "b", a, 1.0f);
    ecs_entity_t c = audio_bus(world, "c", 0, 1.0f);
    audio_send(world, c, b, 0.5f, false);
    audio_run(world, 1);

    int32_t pa = audio_position(world, a), pb = audio_position(world, b);
    int32_t pc = audio_position(world, c);
    test_assert(pa >= 0 && pb >= 0 && pc >= 0);
    test_assert(pc < pb);
    test_assert(pb < pa);
    ecs_fini(world);
}

void Module_cycle_keeps_order(void) {
    // audio/module
    ecs_world_t *world = audio_world();
    ecs_entity_t a = audio_bus(world, "a", 0, 1.0f);
    ecs_entity_t b = audio_bus(world, "b", a, 1.0f);
    audio_run(world, 1);
    test_assert(audio_position(world, b) < audio_position(world, a));
    box_audio_module_stats_t before = box_audio_module_stats(world);

    audio_log_start();
    ecs_ensure(world, a, AudioBus)->output = b;
    ecs_modified(world, a, AudioBus);
    audio_run(world, 3);
    audio_log_stop();

    box_audio_module_stats_t after = box_audio_module_stats(world);
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "cycle") != NULL);
    test_assert(strstr(audio_log_last, "a") != NULL);
    test_assert(strstr(audio_log_last, "b") != NULL);
    test_int(after.cycles, before.cycles + 1);
    test_int(after.orders, before.orders);
    test_assert(audio_position(world, b) < audio_position(world, a));

    ecs_ensure(world, a, AudioBus)->output = 0;
    ecs_modified(world, a, AudioBus);
    audio_run(world, 1);
    test_int(box_audio_module_stats(world).orders, before.orders + 1);
    ecs_fini(world);
}

void Module_deleted_bus_reroutes(void) {
    // audio/module
    ecs_world_t *world = audio_world();
    ecs_entity_t a = audio_bus(world, "a", 0, 0.5f);
    ecs_entity_t b = audio_bus(world, "b", a, 1.0f);
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1.0f, true, b);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);

    ecs_delete(world, a);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    test_int(audio_position(world, b), 0);

    ecs_delete(world, b);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    ecs_fini(world);
}

void Module_bus_volume_pan_mute(void) {
    // audio/module
    ecs_world_t *world = audio_world();
    ecs_entity_t a = audio_bus(world, "a", 0, 0.5f);
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1.0f, true, a);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);

    ecs_ensure(world, a, AudioBus)->mute = true;
    ecs_modified(world, a, AudioBus);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.0f);

    ecs_ensure(world, a, AudioBus)->mute = false;
    ecs_ensure(world, a, AudioBus)->pan = 1.0f;
    ecs_modified(world, a, AudioBus);
    audio_run(world, 5);
    test_assert(fabsf(audio_level(AUDIO_FRAME)) < 1e-4f);
    test_assert(audio_tap_r[audio_tap_count - 1] > 0.3f);
    ecs_fini(world);
}

void Module_master_settings(void) {
    // audio/module
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1.0f, true, 0);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);

    ecs_singleton_ensure(world, AudioSettings)->master_volume = 0.5f;
    ecs_singleton_modified(world, AudioSettings);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.25f);

    ecs_singleton_ensure(world, AudioSettings)->mute = true;
    ecs_singleton_modified(world, AudioSettings);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.0f);
    ecs_fini(world);
}

void Module_no_commands_unchanged(void) {
    // audio/module
    ecs_world_t *world = audio_world();
    ecs_entity_t a = audio_bus(world, "a", 0, 0.5f);
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    ecs_entity_t src = audio_source(world, dc, 1.0f, true, a);
    audio_run(world, 2);
    box_audio_module_stats_t before = box_audio_module_stats(world);
    test_assert(before.batches > 0);

    audio_run(world, 10);
    box_audio_module_stats_t after = box_audio_module_stats(world);
    test_int(after.batches, before.batches);
    test_int(after.commands, before.commands);

    ecs_ensure(world, src, AudioSource)->volume = 0.5f;
    ecs_modified(world, src, AudioSource);
    audio_run(world, 1);
    after = box_audio_module_stats(world);
    test_int(after.batches, before.batches + 1);
    test_int(after.commands, before.commands + 1);

    audio_run(world, 5);
    test_int(box_audio_module_stats(world).batches, before.batches + 1);
    ecs_fini(world);
}

void Module_one_batch_per_frame(void) {
    // audio/module
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    int32_t i;
    for (i = 0; i < 8; i ++) {
        audio_bus(world, NULL, 0, 1.0f);
        audio_source(world, dc, 0.05f, true, 0);
    }

    test_bool(box_audio_play(world, dc, 0.1f), true);
    test_bool(box_audio_play(world, dc, 0.1f), true);
    audio_run(world, 1);
    test_int(box_audio_module_stats(world).batches, 1);
    ecs_fini(world);
}

void Module_import_opens_no_device(void) {
    // audio/module
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, 100);
    audio_source(world, dc, 1.0f, true, 0);
    audio_bus(world, "a", 0, 1.0f);

    audio_log_start();
    audio_run(world, 5);
    audio_log_stop();

    test_int(audio_log_errors, 0);
    test_assert(box_audio_output_transport(world) == NULL);
    test_int(ecs_singleton_get(world, AudioStatus)->state, AudioOff);
    test_int(box_audio_module_stats(world).batches, 0);
    ecs_fini(world);
}

void Module_new_output_resends(void) {
    // audio/module
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = audio_bus(world, "a", 0, 0.5f);
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1.0f, true, a);
    audio_run(world, 2);
    test_int(box_audio_module_stats(world).batches, 0);

    box_audio_output_open(world, &(box_audio_output_desc_t){ .headless = true });
    audio_run(world, 10);
    test_int(ecs_singleton_get(world, AudioStatus)->state, AudioNoDevice);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_int(ecs_singleton_get(world, AudioStatus)->sample_rate, 48000);
    ecs_fini(world);
}
