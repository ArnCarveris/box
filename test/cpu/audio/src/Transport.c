/**
 * @file Transport.c
 * @brief The null transport renders the frames of each delta on the main thread, the same for
 * the same deltas in every run.
 */

#include "util.h"
#include <flecs.h>
#include <transport.h>

/** Frames of a capture */
#define TRANSPORT_FRAMES (AUDIO_RATE * 2)

/** Output of the null transport */
typedef struct transport_capture_t {
    float *left;        /**< Left channel, NULL to only count */
    float *right;       /**< Right channel */
    int32_t frames;     /**< Frames received */
    int32_t calls;      /**< Blocks received */
} transport_capture_t;

static float transport_l[TRANSPORT_FRAMES];
static float transport_r[TRANSPORT_FRAMES];
static float transport_l2[TRANSPORT_FRAMES];
static float transport_r2[TRANSPORT_FRAMES];

static void transport_output(
    const float *left,
    const float *right,
    int32_t frames,
    void *ctx)
{
    transport_capture_t *c = ctx;
    if (c->left && c->frames + frames <= TRANSPORT_FRAMES) {
        memcpy(c->left + c->frames, left, (size_t)frames * sizeof(float));
        memcpy(c->right + c->frames, right, (size_t)frames * sizeof(float));
    }

    c->frames += frames;
    c->calls ++;
}

static box_audio_transport_t* transport_null(
    transport_capture_t *capture,
    float rate)
{
    box_audio_transport_t *t = box_audio_transport_null_new(&(box_audio_null_desc_t){
        .sample_rate = rate, .output = transport_output, .ctx = capture });
    test_assert(t != NULL);
    test_int(t->kind, BOX_AUDIO_TRANSPORT_NULL);
    test_int(t->state(t), BOX_AUDIO_STATE_NO_DEVICE);

    return t;
}

/* The cases without a world call no flecs function that would set up the OS API they allocate
 * and log with. */
void Transport_setup(void) {
    ecs_os_set_api_defaults();
}

void Transport_null_frames(void) {
    // audio/native
    transport_capture_t capture = {0};
    box_audio_transport_t *t = transport_null(&capture, AUDIO_RATE);
    int32_t i;
    test_flt(t->sample_rate, AUDIO_RATE);
    for (i = 0; i < 60; i ++) {
        int32_t before = capture.frames;
        t->update(t, 1.0f / 60.0f);
        test_int(capture.frames - before, 800);
    }

    test_int(capture.frames, AUDIO_RATE);

    t->update(t, 0.25f);
    test_int(capture.frames, AUDIO_RATE + AUDIO_RATE / 4);
    box_audio_transport_free(t);
}

void Transport_null_frames_44100(void) {
    // audio/native
    transport_capture_t capture = {0};
    box_audio_transport_t *t = transport_null(&capture, 44100);
    int32_t i;
    for (i = 0; i < 120; i ++) {
        t->update(t, 1.0f / 60.0f);
    }

    test_int(capture.frames, 88200);

    box_audio_transport_free(t);
    capture = (transport_capture_t){0};
    t = transport_null(&capture, 44100);
    for (i = 0; i < 1000; i ++) {
        t->update(t, 0.001f);
    }

    test_assert(capture.frames >= 44099 && capture.frames <= 44100);
    box_audio_transport_free(t);
}

void Transport_null_zero_delta(void) {
    // audio/native
    transport_capture_t capture = {0};
    box_audio_transport_t *t = transport_null(&capture, AUDIO_RATE);
    t->update(t, 0);
    t->update(t, -1.0f);
    test_int(capture.frames, 0);
    test_int(capture.calls, 0);
    box_audio_transport_free(t);
}

/** Play a fixed sequence through a null transport with a fixed delta.
 *
 * @param left Receives the left channel
 * @param right Receives the right channel
 * @return Frames rendered
 */
static int32_t transport_run(
    float *left,
    float *right)
{
    transport_capture_t capture = { .left = left, .right = right };
    box_audio_transport_t *t = transport_null(&capture, AUDIO_RATE);
    uint8_t bytes[256];
    box_audio_writer_t writer;
    int32_t frame;
    audio_sine(audio_sound_a, 4800, 440, 0.5f);
    audio_sine(audio_sound_b, 9600, 220, 0.3f);
    test_bool(t->sound(t, 1, audio_sound_a, 4800, 1, AUDIO_RATE), true);
    test_bool(t->sound(t, 2, audio_sound_b, 4800, 2, 44100), true);
    for (frame = 0; frame < 100; frame ++) {
        box_audio_writer_init(&writer, bytes, sizeof(bytes));
        if (frame % 10 == 0) {
            box_audio_write_play(&writer, (uint32_t)frame, 1 + (uint32_t)(frame / 10) % 2, 0,
                0.4f, 0.2f * (float)(frame % 5) - 0.4f, 1.0f + 0.05f * (float)(frame % 7),
                frame % 20 == 0 ? BOX_AUDIO_PLAY_LOOP : 0);
        }

        if (frame % 25 == 5) {
            box_audio_write_stop(&writer, (uint32_t)(frame - 5));
        }

        if (frame == 50) {
            box_audio_write_bus_set(&writer, 0, BOX_AUDIO_PARAM_VOLUME, 0.5f);
        }

        if (writer.size) {
            test_bool(t->commands(t, writer.data, writer.size), true);
        }

        t->update(t, 1.0f / 60.0f);
    }

    frame = (int32_t)(capture.frames);
    box_audio_transport_free(t);

    return frame;
}

void Transport_null_bit_identical(void) {
    // audio/native
    int32_t a = transport_run(transport_l, transport_r);
    int32_t b = transport_run(transport_l2, transport_r2);
    test_int(a, 80000);
    test_int(b, a);
    test_assert(memcmp(transport_l, transport_l2, (size_t)a * sizeof(float)) == 0);
    test_assert(memcmp(transport_r, transport_r2, (size_t)a * sizeof(float)) == 0);
    test_assert(audio_peak(transport_l, 0, a) > 0.01f);
    test_assert(audio_peak(transport_r, 0, a) > 0.01f);
}

void Transport_null_sound_free(void) {
    // audio/native
    transport_capture_t capture = {0};
    box_audio_transport_t *t = transport_null(&capture, AUDIO_RATE);
    box_audio_event_t events[64];
    uint8_t bytes[128];
    box_audio_writer_t writer;
    bool ended = false, freed = false;
    int32_t count, i, frame;
    audio_sine(audio_sound_a, 480, 440, 0.5f);
    test_bool(t->sound(t, 7, audio_sound_a, 480, 1, AUDIO_RATE), true);

    audio_fill(audio_sound_a, 480, 0);
    box_audio_writer_init(&writer, bytes, sizeof(bytes));
    box_audio_write_play(&writer, 3, 7, 0, 1, 0, 1, 0);
    box_audio_write_release(&writer, 7);
    test_bool(t->commands(t, writer.data, writer.size), true);
    for (frame = 0; frame < 10; frame ++) {
        t->update(t, 1.0f / 60.0f);
        count = t->read_events(t, events, 64);
        for (i = 0; i < count; i ++) {
            ended |= events[i].type == BOX_AUDIO_EVENT_VOICE_END && events[i].id == 3;
            freed |= events[i].type == BOX_AUDIO_EVENT_SOUND_FREE && events[i].id == 7;
        }
    }

    test_bool(ended, true);
    test_bool(freed, true);

    test_bool(t->sound(t, 7, audio_sound_b, 100, 1, AUDIO_RATE), true);
    box_audio_transport_free(t);
}

void Transport_null_sound_in_use(void) {
    // audio/native
    transport_capture_t capture = {0};
    box_audio_transport_t *t = transport_null(&capture, AUDIO_RATE);
    audio_sine(audio_sound_a, 480, 440, 0.5f);
    test_bool(t->sound(t, 1, audio_sound_a, 480, 1, AUDIO_RATE), true);
    test_bool(t->sound(t, 1, audio_sound_a, 480, 1, AUDIO_RATE), false);
    test_bool(t->sound(t, BOX_AUDIO_SOUND_MAX, audio_sound_a, 480, 1, AUDIO_RATE), false);
    test_bool(t->sound(t, 2, audio_sound_a, 480, 3, AUDIO_RATE), false);
    test_bool(t->sound(t, 2, NULL, 480, 1, AUDIO_RATE), false);
    test_bool(t->commands(t, NULL, 0), false);
    box_audio_transport_free(t);
}

void Transport_command_before_render(void) {
    // audio/native
    transport_capture_t capture = { .left = transport_l, .right = transport_r };
    box_audio_transport_t *t = transport_null(&capture, AUDIO_RATE);
    uint8_t bytes[128];
    box_audio_writer_t writer;
    audio_fill(audio_sound_a, 4800, 0.5f);
    test_bool(t->sound(t, 1, audio_sound_a, 4800, 1, AUDIO_RATE), true);
    box_audio_writer_init(&writer, bytes, sizeof(bytes));
    box_audio_write_play(&writer, 1, 1, 0, 1, 0, 1, 0);
    test_bool(t->commands(t, writer.data, writer.size), true);
    t->update(t, 0.0125f);

    test_int(capture.frames, 600);
    test_assert(transport_l[0] < 0.01f);
    test_assert(transport_l[AUDIO_RAMP + 10] > 0.49f);
    box_audio_transport_free(t);
}
