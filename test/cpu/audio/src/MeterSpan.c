/**
 * @file MeterSpan.c
 * @brief The levels of buses over a span: the audio meter command measures the live output of
 * the null transport, per bus the peak, the RMS and the loudness, per effect the gain
 * reduction, and leaves the AudioMeter of the entities alone.
 */

#include "world.h"
#include <stdio.h>

/** Frames of the tests a measurement of one second takes, with its skipped reads */
#define METER_SPAN_SECOND 70

/** Levels of a bus as the table prints them */
typedef struct meter_span_row_t {
    float peak;         /**< dBFS */
    float rms;          /**< dB */
    float integrated;   /**< LUFS */
    float short_term;   /**< LUFS */
} meter_span_row_t;

/** Run a command line of the session and check that it succeeds.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @return The output, which the caller frees
 */
static char* meter_span_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line)
{
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, false);
    test_assert(out != NULL);

    return out;
}

/** Read the levels of a bus from the table.
 *
 * @param out The table
 * @param name The name of the bus
 * @return The levels
 */
static meter_span_row_t meter_span_row(
    const char *out,
    const char *name)
{
    char head[128];
    const char *line;
    meter_span_row_t row = {0};
    snprintf(head, sizeof(head), "\n%s: peak ", name);
    line = strstr(out, head);
    test_assert(line != NULL);
    test_int(sscanf(line + strlen(head), "%f dBFS, rms %f dB, integrated %f LUFS, "
        "short-term max %f LUFS", &row.peak, &row.rms, &row.integrated, &row.short_term), 4);

    return row;
}

/** Create a bus with a looping sine of 1 kHz at a peak of 0.5.
 *
 * @param world The world
 * @param name Name of the bus
 * @param volume Volume of the bus
 * @return The bus
 */
static ecs_entity_t meter_span_bus(
    ecs_world_t *world,
    const char *name,
    float volume)
{
    char sound[64];
    ecs_entity_t bus = audio_bus(world, name, 0, volume);
    snprintf(sound, sizeof(sound), "%s_tone", name);
    audio_source(world, audio_tone(world, sound, 1000, 0.5f, AUDIO_RATE), 1, true, bus);

    return bus;
}

/** Measure two buses at volumes 1 and 0.5 over one second.
 *
 * @return The table, which the caller frees
 */
static char* meter_span_two_buses(void)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    char *out;
    meter_span_bus(world, "loud", 1);
    meter_span_bus(world, "quiet", 0.5f);
    audio_run(world, 10);
    out = meter_span_run(world, s, "audio meter loud quiet 1");
    test_assert(strncmp(out, "meter: measuring 1.000 s of the live output in real time: "
        "loud, quiet\n", 70) == 0);
    ecs_os_free(out);
    audio_run(world, METER_SPAN_SECOND);
    out = meter_span_run(world, s, "audio meter");
    ecs_fini(world);

    return out;
}

void MeterSpan_bus_difference(void) {
    // audio/meter
    char *out = meter_span_two_buses();
    meter_span_row_t loud = meter_span_row(out, "loud");
    meter_span_row_t quiet = meter_span_row(out, "quiet");
    float expect = (float)(20.0 * log10(2.0));
    test_assert(strncmp(out, "meter: live output, 1.0", 23) == 0);
    test_assert(fabsf(loud.peak - quiet.peak - expect) <= 0.1f);
    test_assert(fabsf(loud.rms - quiet.rms - expect) <= 0.1f);
    test_assert(fabsf(loud.integrated - quiet.integrated - expect) <= 0.1f);
    test_assert(fabsf(loud.short_term - quiet.short_term - expect) <= 0.1f);
    test_assert(fabsf(loud.peak - loud.rms - 3.01f) <= 0.1f);
    test_assert(fabsf(loud.integrated - loud.short_term) <= 0.1f);
    test_assert(fabsf(loud.integrated - loud.peak) <= 0.2f);
    test_assert(strstr(out, "\nmaster: ") == NULL);
    test_assert(strstr(out, "\nheadroom: ") != NULL);
    test_assert(strstr(out, ", loudest loud at ") != NULL);
    test_assert(strstr(out, "; master limiter max reduction 0.0 dB") != NULL);
    ecs_os_free(out);
}

void MeterSpan_deterministic(void) {
    // audio/meter
    char *a = meter_span_two_buses();
    char *b = meter_span_two_buses();
    test_str(a, b);
    ecs_os_free(a);
    ecs_os_free(b);
}

void MeterSpan_default_buses(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    meter_span_bus(world, "loud", 1);
    meter_span_bus(world, "quiet", 0.5f);
    audio_run(world, 10);
    char *out = meter_span_run(world, s, "audio meter 0.5");
    test_assert(strstr(out, ": loud, quiet, master\n") != NULL ||
        strstr(out, ": quiet, loud, master\n") != NULL);
    ecs_os_free(out);

    out = meter_span_run(world, s, "audio meter");
    test_assert(strncmp(out, "meter: measuring, ", 18) == 0);
    ecs_os_free(out);

    audio_run(world, METER_SPAN_SECOND);
    out = meter_span_run(world, s, "audio meter");
    meter_span_row_t loud = meter_span_row(out, "loud");
    meter_span_row_t master = meter_span_row(out, "master");
    test_assert(fabsf(master.peak - loud.peak - (float)(20.0 * log10(1.5))) <= 0.1f);
    ecs_os_free(out);

    out = meter_span_run(world, s, "audio meter");
    test_assert(strncmp(out, "meter: measuring 3.000 s", 24) == 0);
    ecs_os_free(out);
    ecs_fini(world);
}

void MeterSpan_compressor(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    meter_span_bus(world, "dry", 1);
    ecs_entity_t squash = meter_span_bus(world, "squash", 1);
    ecs_entity_t calm = meter_span_bus(world, "calm", 1);
    ecs_entity_t below = ecs_entity(world, { .name = "below" });
    ecs_entity_t above = ecs_entity(world, { .name = "above" });
    ecs_set(world, below, Compressor, { .threshold = -30, .ratio = 4, .attack = 0.001f,
        .release = 0.1f, .knee = 0, .makeup = 0, .mix = 1 });
    ecs_set(world, above, Compressor, { .threshold = 0, .ratio = 4, .attack = 0.001f,
        .release = 0.1f, .knee = 0, .makeup = 0, .mix = 1 });
    audio_insert(world, squash, 0, below);
    audio_insert(world, calm, 0, above);
    audio_run(world, 30);

    char *out = meter_span_run(world, s, "audio meter dry squash calm 1");
    ecs_os_free(out);
    audio_run(world, METER_SPAN_SECOND);
    out = meter_span_run(world, s, "audio meter");
    meter_span_row_t dry = meter_span_row(out, "dry");
    test_assert(dry.peak < -1 && dry.peak > -30);
    test_assert(strstr(out, "\n  insert 0 above compressor: never engages") != NULL);

    float expect = (dry.peak + 30) * (1 - 1.0f / 4);
    const char *line = strstr(out, "\n  insert 0 below compressor: reduction avg ");
    test_assert(line != NULL);
    float avg = 0, max = 0;
    test_int(sscanf(line, "\n  insert 0 below compressor: reduction avg %f dB, max %f dB",
        &avg, &max), 2);
    test_assert(fabsf(max - expect) <= 1.0f);
    test_assert(fabsf(avg - expect) <= 1.0f);
    test_assert(avg <= max);

    meter_span_row_t squashed = meter_span_row(out, "squash");
    test_assert(fabsf(dry.peak - squashed.peak - expect) <= 1.0f);
    ecs_os_free(out);
    ecs_fini(world);
}

void MeterSpan_no_meter_left(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t loud = meter_span_bus(world, "loud", 1);
    ecs_entity_t watched = meter_span_bus(world, "watched", 0.5f);
    ecs_entity_t glue = ecs_entity(world, { .name = "glue" });
    ecs_entity_t master = ecs_lookup(world, "box.audio.master");
    test_assert(master != 0);
    ecs_set(world, glue, Compressor, { .threshold = -30, .ratio = 4, .attack = 0.001f,
        .release = 0.1f, .knee = 0, .makeup = 0, .mix = 1 });
    audio_insert(world, loud, 0, glue);
    ecs_add(world, watched, AudioMeter);
    audio_run(world, 10);
    test_assert(!ecs_has(world, master, AudioMeter));

    char *out = meter_span_run(world, s, "audio meter 1");
    ecs_os_free(out);
    int32_t i;
    for (i = 0; i < METER_SPAN_SECOND; i ++) {
        audio_run(world, 1);
        /* A UI reads the same levels as without the measurement */
        AudioMeter m;
        test_assert(box_audio_module_meter(world, watched, &m));
        const AudioMeter *w = ecs_get(world, watched, AudioMeter);
        test_assert(w != NULL);
        test_assert(w->peak_left == m.peak_left);
        test_assert(w->rms_left == m.rms_left);
        test_assert(w->rms_right == m.rms_right);
    }

    out = meter_span_run(world, s, "audio meter");
    test_assert(strstr(out, "\nloud: peak ") != NULL);
    test_assert(strstr(out, "\nwatched: peak ") != NULL);
    test_assert(strstr(out, "\nmaster: peak ") != NULL);
    test_assert(strstr(out, "\n  insert 0 glue compressor: reduction avg ") != NULL);
    ecs_os_free(out);
    audio_run(world, 2);

    test_assert(!ecs_has(world, loud, AudioMeter));
    test_assert(!ecs_has(world, glue, AudioMeter));
    test_assert(!ecs_has(world, master, AudioMeter));
    test_assert(ecs_has(world, watched, AudioMeter));
    const AudioMeter *w = ecs_get(world, watched, AudioMeter);
    AudioMeter m;
    test_assert(box_audio_module_meter(world, watched, &m));
    test_assert(w->rms_left == m.rms_left);
    ecs_fini(world);
}

void MeterSpan_limiter(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    meter_span_bus(world, "hot", 4);
    audio_run(world, 10);
    char *out = meter_span_run(world, s, "audio meter 0.5");
    ecs_os_free(out);
    audio_run(world, METER_SPAN_SECOND);
    out = meter_span_run(world, s, "audio meter");
    meter_span_row_t master = meter_span_row(out, "master");
    test_assert(master.peak > 0);
    const char *line = strstr(out, "; master limiter max reduction ");
    test_assert(line != NULL);
    float reduction = 0;
    test_int(sscanf(line, "; master limiter max reduction %f dB", &reduction), 1);
    test_assert(fabsf(reduction - (master.peak + 0.45f)) <= 0.1f);
    test_assert(strstr(out, "\nheadroom: -") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void MeterSpan_song_bars(void) {
    // audio/meter
    ecs_world_t *world = audio_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    int32_t i;
    box_audio_song_load(world, "test/assets/audio/songs/basic.flecs");
    for (i = 0; i < 600; i ++) {
        audio_run(world, 1);
        if (ecs_singleton_get(world, AudioSongStatus)->state != SongLoading) {
            break;
        }
    }

    test_int(ecs_singleton_get(world, AudioSongStatus)->state, SongReady);
    audio_run(world, 2);
    char *out = meter_span_run(world, s, "audio meter basic_lead bars=2..2");
    test_assert(strstr(out, ", bars 2..2 at 96.0 bpm 3/4 from beat 3.000: basic_lead\n") !=
        NULL);
    ecs_os_free(out);
    const Transport *tr = ecs_singleton_get(world, Transport);
    test_assert(tr->playing);
    test_assert(tr->position == 3);

    audio_run(world, 120);
    out = meter_span_run(world, s, "audio meter");
    test_assert(strncmp(out, "meter: live output, 1.8", 23) == 0);
    meter_span_row_t lead = meter_span_row(out, "basic_lead");
    test_assert(lead.peak > -40 && lead.peak < 0);
    test_assert(lead.integrated > -60);
    ecs_os_free(out);
    ecs_fini(world);
}
