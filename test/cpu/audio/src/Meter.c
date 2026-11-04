/**
 * @file Meter.c
 * @brief Meters of the mixer: the AudioMeter box.audio writes on the channels and effects that
 * have it, through the null transport at a fixed delta. The meters of the core are in
 * MeterCore.c.
 */

#include "world.h"

/** RMS of a sine of peak 0.5 */
#define METER_SINE_RMS 0.35355339f

/** Get the AudioMeter of an entity, which it has.
 *
 * @param world The world
 * @param e The entity
 * @return The meter
 */
static const AudioMeter* meter_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const AudioMeter *m = ecs_get(world, e, AudioMeter);
    test_assert(m != NULL);

    return m;
}

/** Give an entity an AudioMeter with values box.audio has to overwrite.
 *
 * @param world The world
 * @param e The entity
 */
static void meter_add(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_set(world, e, AudioMeter, { .peak_left = -1, .peak_right = -1, .rms_left = -1,
        .rms_right = -1, .reduction = -1 });
}

/** Check the four levels of an AudioMeter.
 *
 * @param m The meter
 * @param peak_left Expected left peak
 * @param peak_right Expected right peak
 * @param rms_left Expected left RMS
 * @param rms_right Expected right RMS
 * @param tolerance Largest difference
 */
static void meter_expect(
    const AudioMeter *m,
    float peak_left,
    float peak_right,
    float rms_left,
    float rms_right,
    float tolerance)
{
    test_assert(fabsf(m->peak_left - peak_left) <= tolerance);
    test_assert(fabsf(m->peak_right - peak_right) <= tolerance);
    test_assert(fabsf(m->rms_left - rms_left) <= tolerance);
    test_assert(fabsf(m->rms_right - rms_right) <= tolerance);
}

/** Iterate a query to its end, which takes its changes.
 *
 * @param world The world
 * @param q The query
 */
static void meter_drain(
    ecs_world_t *world,
    ecs_query_t *q)
{
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) { }
}

void Meter_sine(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t bus = audio_bus(world, "music", 0, 1);
    ecs_entity_t tone = audio_tone(world, "tone", 1000, 0.5f, AUDIO_RATE);
    audio_source(world, tone, 1, true, bus);
    meter_add(world, bus);
    audio_run(world, 60);
    meter_expect(meter_of(world, bus), 0.5f, 0.5f, METER_SINE_RMS, METER_SINE_RMS, 1e-3f);
    test_assert(meter_of(world, bus)->reduction == 0);
    ecs_fini(world);
}

void Meter_left_right(void) {
    // audio/meter
    static float pcm[4800 * 2];
    ecs_world_t *world = audio_world();
    ecs_entity_t bus = audio_bus(world, "a", 0, 1);
    ecs_entity_t sound;
    int32_t i;
    for (i = 0; i < 4800; i ++) {
        pcm[i * 2] = 0.4f;
        pcm[i * 2 + 1] = -0.1f;
    }

    sound = box_audio_sound_from_pcm(world, 0, pcm, 4800, 2, AUDIO_RATE);
    audio_source(world, sound, 1, true, bus);
    meter_add(world, bus);
    audio_run(world, 30);
    meter_expect(meter_of(world, bus), 0.4f, 0.1f, 0.4f, 0.1f, 1e-5f);

    ecs_ensure(world, bus, AudioBus)->pan = -1;
    ecs_modified(world, bus, AudioBus);
    audio_run(world, 30);
    meter_expect(meter_of(world, bus), 0.4f * 1.41421356f, 0, 0.4f * 1.41421356f, 0, 1e-5f);
    ecs_fini(world);
}

void Meter_mute(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t bus = audio_bus(world, "a", 0, 1);
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1, true, bus);
    meter_add(world, bus);
    audio_run(world, 30);
    meter_expect(meter_of(world, bus), 0.5f, 0.5f, 0.5f, 0.5f, 1e-5f);

    ecs_ensure(world, bus, AudioBus)->mute = true;
    ecs_modified(world, bus, AudioBus);
    audio_run(world, 2);
    test_assert(meter_of(world, bus)->peak_left == 0);
    test_assert(meter_of(world, bus)->peak_right == 0);
    test_assert(meter_of(world, bus)->rms_left > 0);
    audio_run(world, 30);
    meter_expect(meter_of(world, bus), 0, 0, 0, 0, 0);
    ecs_fini(world);
}

void Meter_group_sums(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t group = audio_bus(world, "group", 0, 1);
    ecs_entity_t a = audio_bus(world, "a", group, 1);
    ecs_entity_t b = audio_bus(world, "b", group, 1);
    audio_source(world, audio_dc(world, "dc_a", 0.25f, AUDIO_RATE), 1, true, a);
    audio_source(world, audio_dc(world, "dc_b", 0.125f, AUDIO_RATE), 1, true, b);
    meter_add(world, group);
    meter_add(world, a);
    meter_add(world, b);
    audio_run(world, 30);
    meter_expect(meter_of(world, a), 0.25f, 0.25f, 0.25f, 0.25f, 1e-5f);
    meter_expect(meter_of(world, b), 0.125f, 0.125f, 0.125f, 0.125f, 1e-5f);
    meter_expect(meter_of(world, group), 0.375f, 0.375f, 0.375f, 0.375f, 1e-5f);

    ecs_ensure(world, group, AudioBus)->volume = 0.5f;
    ecs_modified(world, group, AudioBus);
    audio_run(world, 30);
    meter_expect(meter_of(world, a), 0.25f, 0.25f, 0.25f, 0.25f, 1e-5f);
    meter_expect(meter_of(world, group), 0.1875f, 0.1875f, 0.1875f, 0.1875f, 1e-5f);
    ecs_fini(world);
}

void Meter_master_before_limiter(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t master = ecs_lookup(world, "box.audio.master");
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    test_assert(master != 0);
    audio_source(world, dc, 4, true, 0);
    meter_add(world, master);
    audio_run(world, 30);
    meter_expect(meter_of(world, master), 2.0f, 2.0f, 2.0f, 2.0f, 1e-5f);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - BOX_AUDIO_LIMITER_THRESHOLD) < 1e-4f);

    ecs_singleton_ensure(world, AudioSettings)->master_volume = 0.25f;
    ecs_singleton_modified(world, AudioSettings);
    audio_run(world, 30);
    meter_expect(meter_of(world, master), 0.5f, 0.5f, 0.5f, 0.5f, 1e-5f);
    ecs_fini(world);
}

void Meter_opt_in(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t master = ecs_lookup(world, "box.audio.master");
    ecs_entity_t a = audio_bus(world, "a", 0, 1);
    ecs_entity_t b = audio_bus(world, "b", 0, 1);
    ecs_entity_t dc = audio_dc(world, "dc", 0.25f, AUDIO_RATE);
    audio_source(world, dc, 1, true, a);
    audio_source(world, dc, 1, true, b);
    meter_add(world, a);
    ecs_query_t *buses = ecs_query(world, {
        .terms = {{ .id = ecs_id(AudioBus), .inout = EcsIn }},
        .flags = EcsQueryDetectChanges | EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });
    ecs_query_t *meters = ecs_query(world, {
        .terms = {{ .id = ecs_id(AudioMeter), .inout = EcsIn }},
        .flags = EcsQueryDetectChanges | EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });
    audio_run(world, 2);
    meter_drain(world, buses);
    meter_drain(world, meters);
    audio_run(world, 5);
    test_bool(ecs_query_changed(buses), false);
    test_bool(ecs_query_changed(meters), true);
    test_assert(!ecs_has(world, b, AudioMeter));
    test_assert(!ecs_has(world, master, AudioMeter));
    meter_expect(meter_of(world, a), 0.25f, 0.25f, 0.25f, 0.25f, 1e-5f);

    /* Without any AudioMeter nothing is written, and the module still measures */
    ecs_remove(world, a, AudioMeter);
    audio_run(world, 1);
    meter_drain(world, buses);
    audio_run(world, 5);
    test_bool(ecs_query_changed(buses), false);
    test_assert(!ecs_has(world, a, AudioMeter));
    AudioMeter m;
    test_bool(box_audio_module_meter(world, b, &m), true);
    test_assert(fabsf(m.peak_left - 0.25f) < 1e-5f);
    test_assert(fabsf(m.rms_right - 0.25f) < 1e-5f);
    ecs_query_fini(buses);
    ecs_query_fini(meters);
    ecs_fini(world);
}

void Meter_effect(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t hall = audio_bus(world, "hall", 0, 1);
    ecs_entity_t verb = ecs_entity(world, { .name = "verb" });
    ecs_entity_t other = ecs_entity(world, { .name = "other" });
    ecs_add(world, verb, Reverb);
    audio_insert(world, hall, 0, verb);
    audio_source(world, audio_dc(world, "dc", 0.25f, AUDIO_RATE), 1, true, hall);
    meter_add(world, verb);
    meter_add(world, other);
    audio_run(world, 5);
    test_assert(box_audio_module_effect_slot(world, verb) >= 0);
    meter_expect(meter_of(world, verb), 0, 0, 0, 0, 0);
    test_assert(meter_of(world, verb)->reduction == 0);
    meter_expect(meter_of(world, other), 0, 0, 0, 0, 0);
    test_assert(meter_of(world, other)->reduction == 0);
    ecs_fini(world);
}

void Meter_bus_deleted(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t a = audio_bus(world, "a", 0, 1);
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1, true, a);
    audio_run(world, 30);
    int32_t index = box_audio_module_bus_index(world, a);
    test_assert(index > 0);
    ecs_delete(world, a);
    audio_run(world, 2);
    ecs_entity_t b = audio_bus(world, "b", 0, 1);
    meter_add(world, b);
    audio_run(world, 1);
    /* A new bus that gets the index of the deleted one starts from silence */
    test_assert(meter_of(world, b)->peak_left == 0);
    test_assert(meter_of(world, b)->rms_left == 0);
    ecs_fini(world);
}

void Meter_command(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t music = audio_bus(world, "music", 0, 1);
    bool error = false;
    audio_source(world, audio_dc(world, "dc", 0.5f, AUDIO_RATE), 1, true, music);
    audio_run(world, 30);
    char *out = box_command_run(world, s, "audio", &error);
    test_bool(error, false);
    test_assert(out != NULL);
    test_assert(strstr(out, "peak -6.0/-6.0 dB, rms -6.0/-6.0 dB") != NULL);
    test_assert(strstr(out, "\nmaster: volume 1.00, peak -6.0/-6.0 dB") != NULL);
    ecs_os_free(out);

    ecs_ensure(world, music, AudioBus)->mute = true;
    ecs_modified(world, music, AudioBus);
    audio_run(world, 30);
    out = box_command_run(world, s, "audio", &error);
    test_bool(error, false);
    test_assert(strstr(out, "peak -inf/-inf dB, rms -inf/-inf dB") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}
