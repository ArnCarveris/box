/**
 * @file Mixer.c
 * @brief Mixer channels of box.audio: the solo of tracks and groups, solo safe returns and SFX
 * buses, mute over solo, the sends of a channel and the AudioBus of the master.
 */

#include "world.h"

/** Set the kind of a bus.
 *
 * @param world The world
 * @param bus The bus
 * @param kind The kind
 */
static void mixer_kind(
    ecs_world_t *world,
    ecs_entity_t bus,
    AudioChannelKind kind)
{
    ecs_ensure(world, bus, AudioBus)->kind = kind;
    ecs_modified(world, bus, AudioBus);
}

/** Solo a bus or take its solo off.
 *
 * @param world The world
 * @param bus The bus
 * @param solo Whether it is soloed
 */
static void mixer_solo(
    ecs_world_t *world,
    ecs_entity_t bus,
    bool solo)
{
    ecs_ensure(world, bus, AudioBus)->solo = solo;
    ecs_modified(world, bus, AudioBus);
}

/** Mute a bus or take its mute off.
 *
 * @param world The world
 * @param bus The bus
 * @param mute Whether it is muted
 */
static void mixer_mute(
    ecs_world_t *world,
    ecs_entity_t bus,
    bool mute)
{
    ecs_ensure(world, bus, AudioBus)->mute = mute;
    ecs_modified(world, bus, AudioBus);
}

/** Get the mute box.audio sent for a bus, the solo included.
 *
 * @param world The world
 * @param bus The bus
 * @return Whether the bus is muted in the core
 */
static bool mixer_muted(
    ecs_world_t *world,
    ecs_entity_t bus)
{
    const AudioBusState *s = ecs_get(world, bus, AudioBusState);
    test_assert(s != NULL);

    return s->mute;
}

void Mixer_solo_track(void) {
    // audio/mixer
    ecs_world_t *world = audio_world();
    ecs_entity_t group = audio_bus(world, "group", 0, 1);
    ecs_entity_t t1 = audio_bus(world, "t1", group, 1);
    ecs_entity_t t2 = audio_bus(world, "t2", group, 1);
    ecs_entity_t hall = audio_bus(world, "hall", 0, 1);
    ecs_entity_t sfx = audio_bus(world, "sfx", 0, 1);
    ecs_entity_t dc = audio_dc(world, "dc", 1.0f, AUDIO_RATE);
    mixer_kind(world, hall, AudioChannelReturn);
    mixer_kind(world, sfx, AudioChannelSfx);
    audio_send(world, t1, hall, 1, false);
    audio_source(world, dc, 0.05f, true, t1);
    audio_source(world, dc, 0.1f, true, t2);
    audio_source(world, dc, 0.2f, true, sfx);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.4f) < 1e-5f);

    mixer_solo(world, t1, true);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.3f) < 1e-5f);
    test_bool(mixer_muted(world, t1), false);
    test_bool(mixer_muted(world, t2), true);
    test_bool(mixer_muted(world, group), false);
    test_bool(mixer_muted(world, hall), false);
    test_bool(mixer_muted(world, sfx), false);

    mixer_solo(world, t1, false);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.4f) < 1e-5f);
    test_bool(mixer_muted(world, t2), false);
    ecs_fini(world);
}

void Mixer_solo_group(void) {
    // audio/mixer
    ecs_world_t *world = audio_world();
    ecs_entity_t g1 = audio_bus(world, "g1", 0, 1);
    ecs_entity_t g2 = audio_bus(world, "g2", 0, 1);
    ecs_entity_t t1 = audio_bus(world, "t1", g1, 1);
    ecs_entity_t t2 = audio_bus(world, "t2", g1, 1);
    ecs_entity_t t3 = audio_bus(world, "t3", g2, 1);
    ecs_entity_t dc = audio_dc(world, "dc", 1.0f, AUDIO_RATE);
    audio_source(world, dc, 0.1f, true, t1);
    audio_source(world, dc, 0.2f, true, t2);
    audio_source(world, dc, 0.4f, true, t3);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.7f) < 1e-5f);

    mixer_solo(world, g1, true);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.3f) < 1e-5f);
    test_bool(mixer_muted(world, t1), false);
    test_bool(mixer_muted(world, t2), false);
    test_bool(mixer_muted(world, g1), false);
    test_bool(mixer_muted(world, g2), true);
    test_bool(mixer_muted(world, t3), true);

    mixer_solo(world, t3, true);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.7f) < 1e-5f);
    ecs_fini(world);
}

void Mixer_mute_beats_solo(void) {
    // audio/mixer
    ecs_world_t *world = audio_world();
    ecs_entity_t group = audio_bus(world, "group", 0, 1);
    ecs_entity_t t1 = audio_bus(world, "t1", group, 1);
    ecs_entity_t t2 = audio_bus(world, "t2", group, 1);
    ecs_entity_t dc = audio_dc(world, "dc", 1.0f, AUDIO_RATE);
    audio_source(world, dc, 0.1f, true, t1);
    audio_source(world, dc, 0.2f, true, t2);
    mixer_solo(world, group, true);
    mixer_mute(world, t1, true);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.2f) < 1e-5f);

    mixer_solo(world, t2, true);
    mixer_mute(world, t2, true);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME)) < 1e-6f);
    test_bool(mixer_muted(world, t2), true);

    mixer_mute(world, t2, false);
    mixer_mute(world, group, true);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME)) < 1e-6f);
    test_bool(mixer_muted(world, group), true);
    ecs_fini(world);
}

void Mixer_cycle_third_send(void) {
    // audio/mixer
    ecs_world_t *world = audio_world();
    ecs_entity_t a = audio_bus(world, "a", 0, 1);
    ecs_entity_t b = audio_bus(world, "b", 0, 1);
    ecs_entity_t c = audio_bus(world, "c", 0, 1);
    audio_run(world, 1);
    box_audio_module_stats_t before = box_audio_module_stats(world);

    audio_send(world, a, c, 0.5f, false);
    audio_send(world, a, c, 0.5f, true);
    audio_send(world, a, b, 0.5f, false);
    ecs_ensure(world, b, AudioBus)->output = a;
    ecs_modified(world, b, AudioBus);
    audio_log_start();
    audio_run(world, 3);
    audio_log_stop();

    box_audio_module_stats_t after = box_audio_module_stats(world);
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "cycle") != NULL);
    test_assert(strstr(audio_log_last, "a") != NULL);
    test_assert(strstr(audio_log_last, "b") != NULL);
    test_int(after.cycles, before.cycles + 1);
    test_int(after.orders, before.orders);

    ecs_ensure(world, b, AudioBus)->output = 0;
    ecs_modified(world, b, AudioBus);
    audio_run(world, 1);
    test_int(box_audio_module_stats(world).orders, before.orders + 1);
    test_assert(audio_position(world, a) < audio_position(world, b));
    test_assert(audio_position(world, a) < audio_position(world, c));
    ecs_fini(world);
}

void Mixer_sends_over_max(void) {
    // audio/mixer
    ecs_world_t *world = audio_world();
    ecs_entity_t muted = audio_bus(world, "muted", 0, 1);
    ecs_entity_t a = audio_bus(world, "a", muted, 1);
    ecs_entity_t dc = audio_dc(world, "dc", 1.0f, AUDIO_RATE);
    int32_t i;
    mixer_mute(world, muted, true);
    for (i = 0; i < BOX_AUDIO_SENDS + 1; i ++) {
        audio_send(world, a, audio_bus(world, NULL, 0, 1), 1, false);
    }

    audio_source(world, dc, 0.1f, true, a);
    audio_log_start();
    audio_run(world, 10);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "'a'") != NULL);
    test_assert(strstr(audio_log_last, "sends") != NULL);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.1f * BOX_AUDIO_SENDS) < 1e-5f);

    audio_log_start();
    ecs_ensure(world, a, AudioBus)->volume = 0.5f;
    ecs_modified(world, a, AudioBus);
    audio_run(world, 10);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.05f * BOX_AUDIO_SENDS) < 1e-5f);
    ecs_fini(world);
}

void Mixer_pre_fader_send(void) {
    // audio/mixer
    ecs_world_t *world = audio_world();
    ecs_entity_t track = audio_bus(world, "track", 0, 0.5f);
    ecs_entity_t hall = audio_bus(world, "hall", 0, 1);
    ecs_entity_t dc = audio_dc(world, "dc", 1.0f, AUDIO_RATE);
    mixer_kind(world, hall, AudioChannelReturn);
    audio_send(world, track, hall, 0.5f, true);
    audio_source(world, dc, 0.2f, true, track);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.2f) < 1e-5f);

    ecs_ensure(world, track, AudioBus)->volume = 0;
    ecs_modified(world, track, AudioBus);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.1f) < 1e-5f);

    mixer_mute(world, track, true);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME)) < 1e-6f);

    mixer_mute(world, track, false);
    mixer_solo(world, audio_bus(world, "other", 0, 1), true);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME)) < 1e-6f);
    test_bool(mixer_muted(world, track), true);
    test_bool(mixer_muted(world, hall), false);
    ecs_fini(world);
}

void Mixer_master_bus(void) {
    // audio/mixer
    static float pcm[4800 * 2];
    ecs_world_t *world = audio_world();
    ecs_entity_t master = ecs_lookup(world, "box.audio.master");
    ecs_entity_t a = audio_bus(world, "a", 0, 1);
    ecs_entity_t sound;
    AudioBus *mb;
    int32_t i;
    test_assert(master != 0);
    for (i = 0; i < 4800; i ++) {
        pcm[i * 2] = 0.4f;
        pcm[i * 2 + 1] = 0.1f;
    }

    sound = box_audio_sound_from_pcm(world, 0, pcm, 4800, 2, AUDIO_RATE);
    audio_source(world, sound, 1, true, 0);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.4f) < 1e-5f);
    test_assert(fabsf(audio_level_right(AUDIO_FRAME) - 0.1f) < 1e-5f);

    audio_log_start();
    mb = ecs_ensure(world, master, AudioBus);
    mb->width = 0;
    mb->output = a;
    mb->solo = true;
    ecs_vec_append_t(NULL, &mb->sends, AudioSend)[0] = (AudioSend){ .bus = a, .level = 1 };
    ecs_modified(world, master, AudioBus);
    audio_run(world, 10);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_int(box_audio_module_bus_index(world, master), 0);
    test_int(audio_position(world, master), -1);
    test_bool(mixer_muted(world, a), false);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.25f) < 1e-5f);
    test_assert(fabsf(audio_level_right(AUDIO_FRAME) - 0.25f) < 1e-5f);

    mb = ecs_ensure(world, master, AudioBus);
    mb->width = 1;
    mb->pan = 1;
    ecs_modified(world, master, AudioBus);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME)) < 1e-6f);
    test_assert(fabsf(audio_level_right(AUDIO_FRAME) - 0.1f * 1.41421356f) < 1e-5f);

    ecs_ensure(world, master, AudioBus)->volume = 0.5f;
    ecs_modified(world, master, AudioBus);
    ecs_singleton_ensure(world, AudioSettings)->master_volume = 0.5f;
    ecs_singleton_modified(world, AudioSettings);
    audio_run(world, 10);
    test_assert(fabsf(audio_level_right(AUDIO_FRAME) - 0.025f * 1.41421356f) < 1e-5f);

    ecs_remove(world, master, AudioBus);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.2f) < 1e-5f);
    test_assert(fabsf(audio_level_right(AUDIO_FRAME) - 0.05f) < 1e-5f);
    ecs_fini(world);
}

void Mixer_scene_script(void) {
    // audio/mixer
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/channels.flecs");
    audio_log_start();
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "channels_scene" }),
        .code = code
    });
    audio_log_stop();
    ecs_os_free(code);
    test_assert(script != 0);
    test_int(audio_log_errors, 0);

    ecs_entity_t master = ecs_lookup(world, "box.audio.master");
    ecs_entity_t hall = ecs_lookup(world, "hall"), sfx = ecs_lookup(world, "sfx");
    ecs_entity_t band = ecs_lookup(world, "band"), lead = ecs_lookup(world, "lead");
    ecs_entity_t pad = ecs_lookup(world, "pad");
    test_assert(hall && sfx && band && lead && pad);
    const AudioBus *mb = ecs_get(world, master, AudioBus);
    test_assert(mb != NULL);
    test_flt(mb->volume, 0.8f);
    test_flt(mb->width, 0.5f);
    test_int(ecs_get(world, hall, AudioBus)->kind, AudioChannelReturn);
    test_int(ecs_get(world, sfx, AudioBus)->kind, AudioChannelSfx);
    test_int(ecs_get(world, band, AudioBus)->kind, AudioChannelGroup);
    const AudioBus *lb = ecs_get(world, lead, AudioBus);
    test_bool(lb->solo, true);
    test_flt(lb->width, 0.0f);
    test_int(ecs_vec_count(&lb->sends), 1);
    const AudioBus *pb = ecs_get(world, pad, AudioBus);
    test_int(ecs_vec_count(&pb->sends), 1);
    test_assert(ecs_vec_first_t(&pb->sends, AudioSend)[0].bus == hall);
    test_flt(ecs_vec_first_t(&pb->sends, AudioSend)[0].level, 0.5f);
    test_bool(ecs_vec_first_t(&pb->sends, AudioSend)[0].pre_fader, true);

    ecs_entity_t dc = audio_dc(world, "dc", 1.0f, AUDIO_RATE);
    audio_source(world, dc, 0.1f, true, lead);
    audio_source(world, dc, 0.2f, true, pad);
    audio_source(world, dc, 0.4f, true, sfx);
    audio_log_start();
    audio_run(world, 10);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_bool(mixer_muted(world, pad), true);
    test_bool(mixer_muted(world, lead), false);
    test_bool(mixer_muted(world, band), false);
    test_bool(mixer_muted(world, hall), false);
    test_bool(mixer_muted(world, sfx), false);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.8f * (0.05f + 0.025f + 0.4f)) < 1e-5f);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - audio_level_right(AUDIO_FRAME)) < 1e-6f);
    ecs_fini(world);
}
